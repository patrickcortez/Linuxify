/*
 * Compile: g++ mount.cpp -o mount.exe
 * Leveled File System Shell - Level-First Architecture
 */

#include "fs_common.hpp"
#include "journal.hpp"
#include "permissions.hpp"
#include "fs_entry.hpp"
#include "fs_context.hpp"

class FileSystemShell {
    DiskDevice disk;
    SuperBlock sb;
    Journal* journal;
    
    PermissionCache permCache;
    EntryReader* entryReader;
    EntryWriter* entryWriter;
    EntryFinder* entryFinder;
    
    struct {
        uint64_t currentDirCluster; 
        uint64_t currentContentCluster; 
        uint64_t rootContentCluster;
        uint64_t currentLevelID;
        uint64_t rootLevelID;
        uint32_t currentFolderPerms;
        string currentPath;
        string currentVersion;
    } context;

public:
    FileSystemShell() : journal(nullptr), entryReader(nullptr), entryWriter(nullptr), entryFinder(nullptr) {
        memset(&context, 0, sizeof(context));
        context.currentPath = "/";
        context.currentFolderPerms = PERM_ROOT_DEFAULT;
    }
    
    ~FileSystemShell() {
        if (journal) delete journal;
        if (entryReader) delete entryReader;
        if (entryWriter) delete entryWriter;
        if (entryFinder) delete entryFinder;
    }

    LevelDescriptor* findLevelByID(uint64_t levelID) {
        if (sb.levelRegistryCluster == 0) return nullptr;
        
        static LevelDescriptor registry[SECTOR_SIZE / sizeof(LevelDescriptor)];
        vector<uint64_t> chain = getChain(sb.levelRegistryCluster);
        
        for (uint64_t c : chain) {
            for (int s = 0; s < 8; s++) {
                disk.readSector(c * 8 + s, registry);
                for (int j = 0; j < SECTOR_SIZE / sizeof(LevelDescriptor); j++) {
                    if (registry[j].levelID == levelID && (registry[j].flags & LEVEL_FLAG_ACTIVE)) {
                        return &registry[j];
                    }
                }
            }
        }
        return nullptr;
    }
    
    LevelDescriptor* findLevelByName(const string& name) {
        if (sb.levelRegistryCluster == 0) return nullptr;
        
        static LevelDescriptor registry[SECTOR_SIZE / sizeof(LevelDescriptor)];
        vector<uint64_t> chain = getChain(sb.levelRegistryCluster);
        
        for (uint64_t c : chain) {
            for (int s = 0; s < 8; s++) {
                disk.readSector(c * 8 + s, registry);
                for (int j = 0; j < SECTOR_SIZE / sizeof(LevelDescriptor); j++) {
                    if ((registry[j].flags & LEVEL_FLAG_ACTIVE) && string(registry[j].name) == name) {
                        return &registry[j];
                    }
                }
            }
        }
        return nullptr;
    }

    bool mount(char driveLetter) {
        if (!disk.open(driveLetter)) return false;
        
        if (!disk.readSector(0, &sb)) {
            disk.close();
            return false;
        }
        if (sb.magic != MAGIC) {
            if (!tryBackupSuperblock()) {
                disk.close();
                return false;
            }
        }

        journal = new Journal(&disk, &sb);
        journal->replayJournal();

        context.currentDirCluster = sb.rootDirCluster;
        context.currentPath = "/";
        context.rootLevelID = sb.rootLevelID;
        context.currentFolderPerms = PERM_ROOT_DEFAULT;
        
        if (entryReader) delete entryReader;
        if (entryWriter) delete entryWriter;
        if (entryFinder) delete entryFinder;
        entryReader = new EntryReader(disk);
        entryWriter = new EntryWriter(disk);
        entryFinder = new EntryFinder(disk);
        permCache.clear();
        
        cout << "Mounted successfully. At Root.\n";
        
        if(loadVersion("master")) {
            cout << "Context: master (Level ID: " << context.currentLevelID << ")\n";
            context.rootContentCluster = context.currentContentCluster;
        } else {
            cout << "No master version.\n";
            context.rootContentCluster = 0;
        }
        return true;
    }

    bool mountImage(const string& path) {
        if (!disk.openFile(path)) {
            cout << "Failed to open image file: " << path << "\n";
            return false;
        }
        
        if (!disk.readSector(0, &sb)) {
            cout << "Failed to read superblock.\n";
            disk.close();
            return false;
        }
        if (sb.magic != MAGIC) {
            if (!tryBackupSuperblock()) {
                cout << "Invalid magic: " << hex << sb.magic << dec << "\n";
                disk.close();
                return false;
            }
        }

        journal = new Journal(&disk, &sb);
        journal->replayJournal();

        context.currentDirCluster = sb.rootDirCluster;
        context.currentPath = "/";
        context.rootLevelID = sb.rootLevelID;
        context.currentFolderPerms = PERM_ROOT_DEFAULT;
        
        if (entryReader) delete entryReader;
        if (entryWriter) delete entryWriter;
        if (entryFinder) delete entryFinder;
        entryReader = new EntryReader(disk);
        entryWriter = new EntryWriter(disk);
        entryFinder = new EntryFinder(disk);
        permCache.clear();
        
        cout << "=== Leveled File System v2 ===\n";
        cout << "  Image: " << path << " (" << (disk.getDiskSize()/1024/1024) << " MB)\n";
        
        if (sb.levelRegistryCluster != 0) {
            cout << "  Level Registry: Cluster " << sb.levelRegistryCluster << "\n";
            cout << "  Total Levels: " << sb.totalLevels << "\n";
        }
        
        if(loadVersion("master")) {
            cout << "  Active Level: master (ID: " << context.currentLevelID << ")\n";
            context.rootContentCluster = context.currentContentCluster;
        } else {
            cout << "No master version found. Creating...\n";
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            memset(vps, 0, sizeof(vps));
            strcpy(vps[0].versionName, "master");
            vps[0].isActive = 1;
            vps[0].contentTableCluster = allocCluster();
            vps[0].levelID = LEVEL_ID_MASTER;
            vps[0].parentLevelID = LEVEL_ID_NONE;
            vps[0].flags = LEVEL_FLAG_ACTIVE;
            disk.writeSector(sb.rootDirCluster * 8, vps);
            loadVersion("master");
            context.rootContentCluster = context.currentContentCluster;
        }
        return true;
    }
    
    bool isReservedCluster(uint64_t cluster) {
        if (cluster == 0) return true;
        if (cluster == sb.backupSBCluster) return true;
        if (cluster >= sb.litStartCluster && cluster < sb.litStartCluster + sb.litClusters) return true;
        if (cluster >= sb.labPoolStart && cluster < sb.labPoolStart + sb.labPoolClusters) return true;
        if (cluster >= sb.levelRegistryCluster && cluster < sb.levelRegistryCluster + sb.levelRegistryClusters) return true;
        if (cluster >= sb.journalStartCluster && cluster < sb.journalStartCluster + (sb.journalSectors / SECTORS_PER_CLUSTER + 1)) return true;
        if (cluster >= sb.rootDirCluster && cluster <= sb.rootDirCluster + 1) return true;
        return false;
    }

    LABEntry getLABEntry(uint64_t cluster) {
        LABEntry result;
        memset(&result, 0, sizeof(result));
        result.nextCluster = LAT_FREE;
        result.levelID = LEVEL_ID_NONE;
        result.flags = 0;
        result.refCount = 0;
        
        if (cluster == 0 || cluster >= sb.totalClusters) {
            return result;
        }
        
        if (isReservedCluster(cluster)) {
            result.nextCluster = LAT_END;
            return result;
        }
        
        uint64_t litIndex = cluster / CLUSTERS_PER_LIT_ENTRY;
        uint64_t labOffset = cluster % CLUSTERS_PER_LIT_ENTRY;
        
        uint64_t litClusterIdx = litIndex / (CLUSTER_SIZE / sizeof(LITEntry));
        uint64_t litEntryIdx = litIndex % (CLUSTER_SIZE / sizeof(LITEntry));
        
        if (sb.litStartCluster + litClusterIdx >= sb.totalClusters) {
            return result;
        }
        
        char* litBuffer = new char[CLUSTER_SIZE];
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.readSector((sb.litStartCluster + litClusterIdx) * SECTORS_PER_CLUSTER + s,
                litBuffer + s * SECTOR_SIZE);
        }
        LITEntry* litEntries = (LITEntry*)litBuffer;
        
        uint64_t labCluster = litEntries[litEntryIdx].labCluster;
        delete[] litBuffer;
        
        if (labCluster == LIT_EMPTY || labCluster == 0) {
            return result;
        }
        
        if (labCluster < sb.labPoolStart || labCluster >= sb.labPoolStart + sb.labPoolClusters) {
            return result;
        }
        
        char* labBuffer = new char[CLUSTER_SIZE];
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.readSector(labCluster * SECTORS_PER_CLUSTER + s,
                labBuffer + s * SECTOR_SIZE);
        }
        LABEntry* labEntries = (LABEntry*)labBuffer;
        
        result = labEntries[labOffset];
        delete[] labBuffer;
        return result;
    }
    
    uint64_t getLATEntry(uint64_t cluster) {
        if (cluster == 0 || cluster >= sb.totalClusters) return LAT_END;
        if (isReservedCluster(cluster)) return LAT_END;
        LABEntry entry = getLABEntry(cluster);
        return entry.nextCluster;
    }
    
    void setLABEntry(uint64_t cluster, LABEntry value) {
        uint64_t litIndex = cluster / CLUSTERS_PER_LIT_ENTRY;
        uint64_t labOffset = cluster % CLUSTERS_PER_LIT_ENTRY;
        
        uint64_t litClusterIdx = litIndex / (CLUSTER_SIZE / sizeof(LITEntry));
        uint64_t litEntryIdx = litIndex % (CLUSTER_SIZE / sizeof(LITEntry));
        
        char* litBuffer = new char[CLUSTER_SIZE];
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.readSector((sb.litStartCluster + litClusterIdx) * SECTORS_PER_CLUSTER + s,
                litBuffer + s * SECTOR_SIZE);
        }
        LITEntry* litEntries = (LITEntry*)litBuffer;
        
        uint64_t labCluster = litEntries[litEntryIdx].labCluster;
        
        if (labCluster == LIT_EMPTY || labCluster == 0) {
            uint64_t newLABCluster = sb.labPoolStart + sb.nextFreeLAB;
            sb.nextFreeLAB++;
            
            char* newLABBuffer = new char[CLUSTER_SIZE];
            memset(newLABBuffer, 0, CLUSTER_SIZE);
            LABEntry* newLAB = (LABEntry*)newLABBuffer;
            for (int i = 0; i < LAB_ENTRIES_PER_CLUSTER; i++) {
                newLAB[i].nextCluster = LAT_FREE;
                newLAB[i].levelID = LEVEL_ID_NONE;
                newLAB[i].flags = 0;
                newLAB[i].refCount = 0;
            }
            for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                disk.writeSector(newLABCluster * SECTORS_PER_CLUSTER + s,
                    newLABBuffer + s * SECTOR_SIZE);
            }
            delete[] newLABBuffer;
            
            litEntries[litEntryIdx].labCluster = newLABCluster;
            litEntries[litEntryIdx].baseCluster = litIndex * CLUSTERS_PER_LIT_ENTRY;
            litEntries[litEntryIdx].allocatedCount = 0;
            litEntries[litEntryIdx].flags = 0;
            labCluster = newLABCluster;
            
            for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                disk.writeSector((sb.litStartCluster + litClusterIdx) * SECTORS_PER_CLUSTER + s,
                    litBuffer + s * SECTOR_SIZE);
            }
            
            writeSuperBlock();
        }
        
        delete[] litBuffer;
        
        char* labBuffer = new char[CLUSTER_SIZE];
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.readSector(labCluster * SECTORS_PER_CLUSTER + s,
                labBuffer + s * SECTOR_SIZE);
        }
        LABEntry* labEntries = (LABEntry*)labBuffer;
        
        labEntries[labOffset] = value;
        
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.writeSector(labCluster * SECTORS_PER_CLUSTER + s,
                labBuffer + s * SECTOR_SIZE);
        }
        delete[] labBuffer;
    }

    void setLATEntry(uint64_t cluster, uint64_t value) {
        LABEntry entry = getLABEntry(cluster);
        entry.nextCluster = value;
        if (value == LAT_END) entry.flags |= LAT_FLAG_USED;
        setLABEntry(cluster, entry);
    }
    
    void setLATEntryWithLevel(uint64_t cluster, uint64_t value, uint32_t levelID) {
        LABEntry entry;
        entry.nextCluster = value;
        entry.levelID = levelID;
        entry.flags = LAT_FLAG_USED;
        entry.refCount = 1;
        setLABEntry(cluster, entry);
    }
    
    void freeCluster(uint64_t cluster) {
        if (cluster == 0 || isReservedCluster(cluster)) return;
        
        LABEntry entry;
        entry.nextCluster = LAT_FREE;
        entry.levelID = LEVEL_ID_NONE;
        entry.flags = 0;
        entry.refCount = 0;
        setLABEntry(cluster, entry);
        
        sb.totalFreeClusters++;
        writeSuperBlock();
    }
    
    void freeChain(uint64_t startCluster) {
        if (startCluster == 0 || isReservedCluster(startCluster)) return;
        
        vector<uint64_t> chain = getChain(startCluster);
        for (uint64_t c : chain) {
            LABEntry entry;
            entry.nextCluster = LAT_FREE;
            entry.levelID = LEVEL_ID_NONE;
            entry.flags = 0;
            entry.refCount = 0;
            setLABEntry(c, entry);
            sb.totalFreeClusters++;
        }
        writeSuperBlock();
    }

    uint64_t allocCluster() {
        return allocClusterForLevel(context.currentLevelID);
    }
    
    uint64_t skipPastReserved(uint64_t c) {
        if (c == 0) return 1;
        if (c == sb.backupSBCluster) return c + 1;
        if (c >= sb.litStartCluster && c < sb.litStartCluster + sb.litClusters) 
            return sb.litStartCluster + sb.litClusters;
        if (c >= sb.labPoolStart && c < sb.labPoolStart + sb.labPoolClusters) 
            return sb.labPoolStart + sb.labPoolClusters;
        if (c >= sb.levelRegistryCluster && c < sb.levelRegistryCluster + sb.levelRegistryClusters) 
            return sb.levelRegistryCluster + sb.levelRegistryClusters;
        if (c >= sb.journalStartCluster && c < sb.journalStartCluster + (sb.journalSectors / SECTORS_PER_CLUSTER + 1)) 
            return sb.journalStartCluster + (sb.journalSectors / SECTORS_PER_CLUSTER + 1);
        if (c >= sb.rootDirCluster && c <= sb.rootDirCluster + 1) 
            return sb.rootDirCluster + 2;
        return c;
    }
    
    uint64_t allocClusterForLevel(uint32_t levelID) {
        uint64_t c = sb.freeClusterHint;
        if (c == 0) c = 1;
        
        uint64_t startC = c;
        bool wrapped = false;
        
        while (true) {
            c = skipPastReserved(c);
            
            if (c >= sb.totalClusters) {
                if (wrapped) return 0;
                wrapped = true;
                c = 1;
                continue;
            }
            
            if (wrapped && c >= startC) return 0;
            
            if (!isReservedCluster(c)) {
                LABEntry entry = getLABEntry(c);
                if (entry.nextCluster == LAT_FREE && entry.flags == 0) {
                    setLATEntryWithLevel(c, LAT_END, levelID);
                    sb.freeClusterHint = c + 1;
                    sb.totalFreeClusters--;
                    writeSuperBlock();
                    return c;
                }
            }
            
            c++;
        }
        return 0;
    }

    vector<uint64_t> getChain(uint64_t startCluster) {
        vector<uint64_t> chain;
        if (startCluster == 0 || startCluster >= sb.totalClusters) return chain;
        
        chain.push_back(startCluster);
        
        if (isReservedCluster(startCluster)) {
            return chain;
        }
        
        uint64_t current = getLATEntry(startCluster);
        while (current != 0 && current != LAT_END && current != LAT_BAD && current < sb.totalClusters) {
            if (find(chain.begin(), chain.end(), current) != chain.end()) break;
            chain.push_back(current);
            if (chain.size() > 1000000) break;
            current = getLATEntry(current);
        }
        return chain;
    }


    // Helper: Try loading backup superblock
    bool tryBackupSuperblock() {
        if (sb.backupSBCluster == 0) return false;
        
        SuperBlock backupSB;
        if (!disk.readSector(sb.backupSBCluster * 8, &backupSB)) return false;
        if (backupSB.magic != MAGIC) return false;
        
        cout << "[Recovery] Primary SuperBlock corrupt. Using backup from cluster " << sb.backupSBCluster << "\n";
        sb = backupSB;
        // Restore primary from backup
        disk.writeSector(0, &sb);
        return true;
    }
    
    // Helper: Write superblock to both locations
    void writeSuperBlock() {
        disk.writeSector(0, &sb);
        if (sb.backupSBCluster != 0) {
            disk.writeSector(sb.backupSBCluster * 8, &sb);
        }
    }

    bool isMounted() { return disk.isOpen(); }

    bool loadVersion(const string& ver) {
        vector<uint64_t> chain = getChain(context.currentDirCluster);
        
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, vps);
                for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive && string(vps[j].versionName) == ver) {
                        context.currentContentCluster = vps[j].contentTableCluster;
                        context.currentLevelID = vps[j].levelID;
                        context.currentVersion = ver;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    struct PathResult {
        uint64_t parentCluster;
        string name;
        bool valid;
    };

    PathResult resolvePath(string path) {
        if (path.empty()) return {context.currentContentCluster, "", false};
        
        uint64_t current = context.currentContentCluster;
        if (path[0] == '/') {
            if (context.rootContentCluster == 0) return {0, "", false};
            current = context.rootContentCluster;
            path = path.substr(1);
        }
        if (path.empty()) return {current, "", true};

        size_t pos = 0;
        string token;
        vector<string> parts;
        while ((pos = path.find('/')) != string::npos) {
            token = path.substr(0, pos);
            if (!token.empty()) parts.push_back(token);
            path.erase(0, pos + 1);
        }
        if (!path.empty()) parts.push_back(path);

        if (parts.empty()) return {current, "", true};

        for (size_t i = 0; i < parts.size() - 1; i++) {
            string folderName = parts[i];
            string levelName = "master";
            size_t colon = folderName.find(':');
            if (colon != string::npos) {
                levelName = folderName.substr(colon + 1);
                folderName = folderName.substr(0, colon);
            }

            DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
            uint64_t folderCluster = 0;
            bool found = false;
            
            // Follow LAT chain for directory search
            vector<uint64_t> chain = getChain(current);
            for (uint64_t c : chain) {
                for (int s = 0; s < 8; s++) {
                    memset(entries, 0, sizeof(entries));
                    disk.readSector(c * 8 + s, entries);
                    for (int j = 0; j < SECTOR_SIZE/sizeof(DirEntry); j++) {
                        if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == folderName) {
                            folderCluster = entries[j].startCluster;
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
                if (found) break;
            }


            if (!found) return {0, "", false};

            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            bool levelFound = false;
            uint64_t nextContent = 0;
            for (int s = 0; s < 8; s++) {
                disk.readSector(folderCluster * 8 + s, vps);
                for (int j = 0; j < SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive && string(vps[j].versionName) == levelName) {
                        nextContent = vps[j].contentTableCluster;
                        levelFound = true;
                        break;
                    }
                }
                if (levelFound) break;
            }

            if (!levelFound) return {0, "", false};
            current = nextContent;
        }

        return {current, parts.back(), true};
    }

    void look(string target = "") {
        if (!disk.isOpen()) return;
        
        // Check if current folder allows read (when looking at current dir)
        if (target.empty() && !(context.currentFolderPerms & PERM_READ)) {
            cout << "Permission denied: no read access to current folder.\n";
            return;
        }
        
        uint64_t contentCluster = context.currentContentCluster;
        string itemsTitle = context.currentPath + " (" + context.currentVersion + ")";

        if (!target.empty()) {
            PathResult res = resolvePath(target);
            if (!res.valid) {
                 cout << "Path not found.\n";
                 return;
            }
            string folderName = res.name;
            string levelName = "";
            size_t colon = folderName.find(':');
            if (colon != string::npos) {
                levelName = folderName.substr(colon + 1);
                folderName = folderName.substr(0, colon);
            }

             DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
             uint64_t foundCluster = 0;
             uint8_t foundType = 0;
             bool found = false;

             // Follow LAT chain for target search
             vector<uint64_t> chain = getChain(res.parentCluster);
             for (uint64_t c : chain) {
                 for (int i=0; i<8; i++) {
                     memset(entries, 0, sizeof(entries));
                     disk.readSector(c * 8 + i, entries);
                     for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                         if (entries[j].type != TYPE_FREE && string(entries[j].name) == folderName) {
                             foundCluster = entries[j].startCluster;
                             foundType = entries[j].type;
                             found = true;
                             break;
                         }
                     }
                     if (found) break;
                 }
                 if (found) break;
             }

             
             if (!found) { cout << "Target not found.\n"; return; }
             
             if (foundType == TYPE_FILE) {
                 cout << "File: " << folderName << "\n";
                 return; 
             }
             
             if (levelName.empty()) {
                  cout << "Levels of " << folderName << ":\n";
                  VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
                  for (int i=0; i<8; i++) {
                      disk.readSector(foundCluster * 8 + i, vps);
                      for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                          if (vps[j].isActive) cout << " [" << vps[j].versionName << "]\n";
                      }
                  }
                  return;
             } else {
                 VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
                 bool lvlFound = false;
                 for (int i=0; i<8; i++) {
                     disk.readSector(foundCluster * 8 + i, vps);
                     for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                         if (vps[j].isActive && string(vps[j].versionName) == levelName) {
                             contentCluster = vps[j].contentTableCluster;
                             itemsTitle = folderName + ":" + levelName;
                             lvlFound = true;
                             break;
                         }
                     }
                     if (lvlFound) break;
                 }
                 if (!lvlFound) { cout << "Level '" << levelName << "' not found.\n"; return; }
             }
        }
        
        cout << "Content of " << itemsTitle << ":\n";
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        bool empty = true;
        
        // Follow LAT chain for directory contents
        vector<uint64_t> chain = getChain(contentCluster);
        for (uint64_t c : chain) {
            for (int i=0; i<8; i++) {
                memset(entries, 0, sizeof(entries));
                if (!disk.readSector(c * 8 + i, entries)) continue;
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FILE || entries[j].type == TYPE_LEVELED_DIR || 
                        entries[j].type == TYPE_SYMLINK || entries[j].type == TYPE_HARDLINK ||
                        entries[j].type == TYPE_LEVEL_MOUNT) {
                        empty = false;
                        entries[j].name[23] = '\0';
                        string typeStr;
                        if (entries[j].type == TYPE_LEVELED_DIR) typeStr = "<L-DIR>";
                        else if (entries[j].type == TYPE_FILE) typeStr = "<FILE>";
                        else if (entries[j].type == TYPE_SYMLINK) typeStr = "<SYMLNK>";
                        else if (entries[j].type == TYPE_HARDLINK) typeStr = "<HDLINK>";
                        else if (entries[j].type == TYPE_LEVEL_MOUNT) typeStr = "<LVLMNT>";
                        
                        entries[j].extension[7] = '\0';
                        string displayName = entries[j].name;
                        if (entries[j].type == TYPE_FILE && entries[j].extension[0] != '\0') {
                            displayName += ".";
                            displayName += entries[j].extension;
                        }
                        
                        cout << setw(8) << left << typeStr << " " << displayName;
                        
                        if (entries[j].type == TYPE_SYMLINK && entries[j].startCluster != 0) {
                            char targetBuf[CLUSTER_SIZE];
                            memset(targetBuf, 0, CLUSTER_SIZE);
                            for (int s=0; s<8; s++) {
                                disk.readSector(entries[j].startCluster * 8 + s, targetBuf + (s * SECTOR_SIZE));
                            }
                            cout << " -> " << targetBuf;
                        }
                        if (entries[j].type == TYPE_LEVEL_MOUNT) {
                            LevelDescriptor* lvl = findLevelByID(entries[j].startCluster);
                            if (lvl) cout << " -> Level '" << lvl->name << "' (ID: " << entries[j].startCluster << ")";
                            else cout << " -> Level ID: " << entries[j].startCluster;
                        }
                        cout << "\n";
                    }
                }
            }
        }
        if (empty) cout << "(empty)\n";
        cout.flush();
    }

    void lookTarget(string target) {
        if (!disk.isOpen()) return;
        
        string folderName = target;
        string levelName = "";
        size_t colon = target.find(':');
        if (colon != string::npos) {
            folderName = target.substr(0, colon);
            levelName = target.substr(colon + 1);
        }
        
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        uint64_t folderCluster = 0;
        for (int i=0; i<8; i++) {
            memset(entries, 0, sizeof(entries));
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == folderName) {
                    folderCluster = entries[j].startCluster;
                    break;
                }
            }
            if (folderCluster) break;
        }
        
        if (!folderCluster) {
            cout << "Folder '" << folderName << "' not found.\n";
            return;
        }
        
        if (levelName.empty()) {
            cout << "Levels of '" << folderName << "':\n";
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            int count = 0;
            for (int i=0; i<8; i++) {
                disk.readSector(folderCluster * 8 + i, vps);
                for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive) {
                        cout << "  [" << vps[j].versionName << "]\n";
                        count++;
                    }
                }
            }
            if (count == 0) cout << "  (no levels)\n";
        } else {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            uint64_t contentCluster = 0;
            for (int i=0; i<8; i++) {
                disk.readSector(folderCluster * 8 + i, vps);
                for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive && string(vps[j].versionName) == levelName) {
                        contentCluster = vps[j].contentTableCluster;
                        break;
                    }
                }
                if (contentCluster) break;
            }
            
            if (!contentCluster) {
                cout << "Level '" << levelName << "' not found in '" << folderName << "'.\n";
                return;
            }
            
            cout << "Content of " << folderName << ":" << levelName << ":\n";
            bool empty = true;
            for (int i=0; i<8; i++) {
                memset(entries, 0, sizeof(entries));
                disk.readSector(contentCluster * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FILE || entries[j].type == TYPE_LEVELED_DIR) {
                        empty = false;
                        entries[j].name[31] = '\0';
                        string typeStr = (entries[j].type == TYPE_LEVELED_DIR) ? "<L-DIR>" : "<FILE>";
                        cout << setw(8) << left << typeStr << " " << entries[j].name << "\n";
                    }
                }
            }
            if (empty) cout << "(empty)\n";
        }
        cout.flush();
    }

    void dirTreeRecurse(uint64_t contentCluster, string prefix, bool isLast) {
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        vector<pair<string, uint64_t>> folders;
        vector<string> files;
        
        for (int i=0; i<8; i++) {
            memset(entries, 0, sizeof(entries));
            disk.readSector(contentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_LEVELED_DIR) {
                    entries[j].name[23] = '\0';
                    folders.push_back({string(entries[j].name), entries[j].startCluster});
                } else if (entries[j].type == TYPE_FILE) {
                    entries[j].name[23] = '\0';
                    entries[j].extension[7] = '\0';
                    string displayName = entries[j].name;
                    if (entries[j].extension[0] != '\0') {
                        displayName += ".";
                        displayName += entries[j].extension;
                    }
                    files.push_back(displayName);
                }
            }
        }
        
        for (size_t i = 0; i < files.size(); i++) {
            bool last = (i == files.size() - 1) && folders.empty();
            cout << prefix << (last ? "└── " : "├── ") << files[i] << "\n";
        }
        
        for (size_t i = 0; i < folders.size(); i++) {
            bool last = (i == folders.size() - 1);
            cout << prefix << (last ? "└── " : "├── ") << "[" << folders[i].first << "]" << "\n";
            
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int s=0; s<8; s++) {
                disk.readSector(folders[i].second * 8 + s, vps);
                for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive) {
                        string newPrefix = prefix + (last ? "    " : "│   ");
                        bool lastLevel = true;
                        for (int k=j+1; k<SECTOR_SIZE/sizeof(VersionEntry); k++) {
                            if (vps[k].isActive) { lastLevel = false; break; }
                        }
                        if (lastLevel) {
                            for (int ns=s+1; ns<8 && lastLevel; ns++) {
                                VersionEntry vp2[SECTOR_SIZE/sizeof(VersionEntry)];
                                disk.readSector(folders[i].second * 8 + ns, vp2);
                                for (int k=0; k<SECTOR_SIZE/sizeof(VersionEntry); k++) {
                                    if (vp2[k].isActive) { lastLevel = false; break; }
                                }
                            }
                        }
                        cout << newPrefix << (lastLevel ? "└── " : "├── ") << ":" << vps[j].versionName << "\n";
                        dirTreeRecurse(vps[j].contentTableCluster, newPrefix + (lastLevel ? "    " : "│   "), lastLevel);
                    }
                }
            }
        }
    }

    void dirTree() {
        if (!disk.isOpen()) return;
        cout << context.currentPath << " (" << context.currentVersion << ")\n";
        dirTreeRecurse(context.currentContentCluster, "", true);
        cout.flush();
    }

    void createSymlink(string linkPath, string targetPath) {
        if (!disk.isOpen()) return;
        PathResult res = resolvePath(linkPath);
        if (!res.valid) { cout << "Invalid link path.\n"; return; }
        
        // Create symlink entry first
        createInCluster(res.parentCluster, "symlink", res.name);
        
        // Find the newly created symlink entry and set its target
        vector<uint64_t> chain = getChain(res.parentCluster);
        for (uint64_t c : chain) {
            for (int i=0; i<8; i++) {
                DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_SYMLINK && string(entries[j].name) == res.name) {
                        // Allocate cluster to store target path
                        uint64_t targetCluster = allocCluster();
                        if (targetCluster == 0) {
                            cout << "Disk full. Cannot create symlink.\n";
                            entries[j].type = TYPE_FREE;
                            disk.writeSector(c * 8 + i, entries);
                            return;
                        }
                        
                        // Write target path to cluster
                        char buffer[CLUSTER_SIZE];
                        memset(buffer, 0, CLUSTER_SIZE);
                        strncpy(buffer, targetPath.c_str(), CLUSTER_SIZE - 1);
                        for (int s=0; s<8; s++) {
                            disk.writeSector(targetCluster * 8 + s, buffer + (s * SECTOR_SIZE));
                        }
                        
                        entries[j].startCluster = targetCluster;
                        entries[j].size = targetPath.length();
                        disk.writeSector(c * 8 + i, entries);
                        cout << "Symlink '" << res.name << "' -> '" << targetPath << "' created.\n";
                        return;
                    }
                }
            }
        }
    }
    
    void createHardlink(string linkPath, string targetPath) {
        if (!disk.isOpen()) return;
        
        // Resolve target file
        PathResult targetRes = resolvePath(targetPath);
        if (!targetRes.valid) { cout << "Target file not found.\n"; return; }
        
        // Find target file entry
        DirEntry targetEntry;
        bool found = false;
        vector<uint64_t> targetChain = getChain(targetRes.parentCluster);
        for (uint64_t c : targetChain) {
            for (int i=0; i<8; i++) {
                DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FILE && string(entries[j].name) == targetRes.name) {
                        targetEntry = entries[j];
                        found = true;
                        
                        // Increment reference count
                        entries[j].attributes++;
                        disk.writeSector(c * 8 + i, entries);
                        goto found_target;
                    }
                }
            }
        }
found_target:
        if (!found) { cout << "Target must be a regular file.\n"; return; }
        
        // Create hardlink entry
        PathResult linkRes = resolvePath(linkPath);
        if (!linkRes.valid) { cout << "Invalid link path.\n"; return; }
        
        createInCluster(linkRes.parentCluster, "hardlink", linkRes.name);
        
        // Update the hardlink to point to same data cluster
        vector<uint64_t> linkChain = getChain(linkRes.parentCluster);
        for (uint64_t c : linkChain) {
            for (int i=0; i<8; i++) {
                DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_HARDLINK && string(entries[j].name) == linkRes.name) {
                        entries[j].startCluster = targetEntry.startCluster;
                        entries[j].size = targetEntry.size;
                        entries[j].attributes = targetEntry.attributes;  // Share refcount
                        disk.writeSector(c * 8 + i, entries);
                        cout << "Hardlink '" << linkRes.name << "' -> '" << targetPath << "' created.\n";
                        return;
                    }
                }
            }
        }
    }
    
    void create(string type, string path, string extension = "") {
        if (!disk.isOpen()) return;
        
        // Check if current folder allows write
        if (!(context.currentFolderPerms & PERM_WRITE)) {
            cout << "Permission denied: current folder is read-only.\n";
            return;
        }
        
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path location.\n"; return; }
        
        createInCluster(res.parentCluster, type, res.name, extension);
    }
    
    void createInCluster(uint64_t contentCluster, string type, string name, string extension = "") {
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        int freeSector = -1;
        int freeIdx = -1;
        uint64_t targetCluster = contentCluster;
        
        vector<uint64_t> chain = getChain(contentCluster);
        
        if (chain.empty()) {
            cout << "Error: Invalid directory cluster. Cannot create.\n";
            return;
        }
        
        bool slotFound = false;
        
        for (uint64_t c : chain) {
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FREE) {
                        freeSector = i;
                        freeIdx = j;
                        targetCluster = c;
                        slotFound = true;
                        goto found_slot;
                    }
                }
            }
        }

found_slot:
        if (!slotFound) {
            uint64_t lastCluster = chain.back();
            uint64_t newCluster = allocCluster();
            if (newCluster == 0) {
                cout << "Disk full. Cannot create " << type << ".\n";
                return;
            }
            
            setLATEntry(lastCluster, newCluster);
            
            memset(entries, 0, sizeof(entries));
            for (int i=0; i<8; i++) {
                disk.writeSector(newCluster * 8 + i, entries);
            }
            
            targetCluster = newCluster;
            freeSector = 0;
            freeIdx = 0;
            
            disk.readSector(newCluster * 8, entries);
        }
        
        DirEntry* target = &entries[freeIdx];
        memset(target, 0, sizeof(DirEntry));
        strncpy(target->name, name.c_str(), 23);
        target->name[23] = '\0';
        strncpy(target->extension, extension.c_str(), 7);
        target->extension[7] = '\0';
        target->createTime = time(0);
        target->modTime = time(0);
        
        if (type == "folder") {
            target->type = TYPE_LEVELED_DIR;
            target->startCluster = allocCluster();
            target->attributes = PERM_DIR_DEFAULT;
            
            if (target->startCluster != 0) {
                VersionEntry vTable[CLUSTER_SIZE / sizeof(VersionEntry)];
                memset(vTable, 0, sizeof(vTable));
                strcpy(vTable[0].versionName, "master");
                vTable[0].isActive = 1;
                vTable[0].contentTableCluster = allocCluster();
                vTable[0].levelID = context.currentLevelID;
                vTable[0].parentLevelID = context.rootLevelID;
                vTable[0].flags = LEVEL_FLAG_ACTIVE;
                vTable[0].permissions = PERM_DIR_DEFAULT;
                vTable[0].createTime = time(0);
                vTable[0].modTime = time(0);
                vTable[0].isLocked = 0;
                vTable[0].isSnapshot = 0;
                
                for (int s = 0; s < 8; s++) {
                    disk.writeSector(target->startCluster * 8 + s, ((char*)vTable) + s * SECTOR_SIZE);
                }
                
                if (vTable[0].contentTableCluster != 0) {
                    DirEntry emptyContent[CLUSTER_SIZE / sizeof(DirEntry)];
                    memset(emptyContent, 0, sizeof(emptyContent));
                    for (int s = 0; s < 8; s++) {
                        disk.writeSector(vTable[0].contentTableCluster * 8 + s, ((char*)emptyContent) + s * SECTOR_SIZE);
                    }
                }
            }
        } else if (type == "symlink") {
            target->type = TYPE_SYMLINK;
            target->startCluster = 0;
            target->size = 0;
            target->attributes = PERM_DEFAULT;
        } else if (type == "hardlink") {
            target->type = TYPE_HARDLINK;
            target->startCluster = 0;
            target->size = 0;
            target->attributes = PERM_DEFAULT;
        } else {
            target->type = TYPE_FILE;
            target->startCluster = allocCluster();
            target->size = 0;
            target->attributes = PERM_DEFAULT;
        }
        
        disk.writeSector(targetCluster * 8 + freeSector, entries);
        
        string displayName = name;
        if (!extension.empty()) displayName += "." + extension;
        cout << "Created " << type << " " << displayName << " [" << getPermsStr(target->attributes) << "]\n";
    }
    
    string getPermsStr(uint32_t attrs) {
        string s;
        s += (attrs & PERM_READ) ? 'r' : '-';
        s += (attrs & PERM_WRITE) ? 'w' : '-';
        s += (attrs & PERM_EXEC) ? 'x' : '-';
        return s;
    }
    
    string formatTime(uint32_t t) {
        if (t == 0) return "----";
        time_t tt = t;
        struct tm* tm = localtime(&tt);
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
        return string(buf);
    }
    
    bool checkCurrentDirWrite() {
        if (context.currentPath == "/") return true;
        return PermissionChecker::checkWrite(context.currentFolderPerms);
    }
    
    bool checkCurrentDirRead() {
        if (context.currentPath == "/") return true;
        return PermissionChecker::checkRead(context.currentFolderPerms);
    }
    
    bool checkCurrentDirExec() {
        if (context.currentPath == "/") return true;
        return PermissionChecker::checkExec(context.currentFolderPerms);
    }
    
    uint32_t getEntryPermsFromDisk(uint64_t cluster, const string& name) {
        if (!entryFinder) return PERM_DEFAULT;
        FindResult result = entryFinder->findByName(cluster, name);
        if (result.found) {
            return result.entry.attributes;
        }
        return PERM_DEFAULT;
    }
    
    
    void perms(string options, string path) {
        if (!disk.isOpen()) return;
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path.\n"; return; }
        
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        vector<uint64_t> chain = getChain(res.parentCluster);
        
        for (uint64_t c : chain) {
            for (int i = 0; i < 8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j = 0; j < SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type != TYPE_FREE && string(entries[j].name) == res.name) {
                        // Parse options: +r, -r, +w, -w, +e, -e
                        if (options == "+r") entries[j].attributes |= PERM_READ;
                        else if (options == "-r") entries[j].attributes &= ~PERM_READ;
                        else if (options == "+w") entries[j].attributes |= PERM_WRITE;
                        else if (options == "-w") entries[j].attributes &= ~PERM_WRITE;
                        else if (options == "+x") entries[j].attributes |= PERM_EXEC;
                        else if (options == "-x") entries[j].attributes &= ~PERM_EXEC;
                        else { cout << "Invalid option. Use +r,-r,+w,-w,+x,-x\n"; return; }
                        
                        entries[j].modTime = time(0);
                        disk.writeSector(c * 8 + i, entries);
                        permCache.clear();
                        cout << "Permissions: " << PermissionChecker::getPermsString(entries[j].attributes) << "\n";
                        return;
                    }
                }
            }
        }
        cout << "File not found.\n";
    }
    
    void lookDetailed(string path = "") {
        if (!disk.isOpen()) return;
        
        uint64_t contentCluster = context.currentContentCluster;
        string title = context.currentPath;
        
        if (!path.empty()) {
            PathResult res = resolvePath(path);
            if (!res.valid) { cout << "Invalid path.\n"; return; }
            
            // Find the folder
            DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
            vector<uint64_t> chain = getChain(res.parentCluster);
            for (uint64_t c : chain) {
                for (int i = 0; i < 8; i++) {
                    disk.readSector(c * 8 + i, entries);
                    for (int j = 0; j < SECTOR_SIZE/sizeof(DirEntry); j++) {
                        if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == res.name) {
                            // Need to pick a level - use first active
                            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
                            disk.readSector(entries[j].startCluster * 8, vps);
                            for (int v = 0; v < SECTOR_SIZE/sizeof(VersionEntry); v++) {
                                if (vps[v].isActive) {
                                    contentCluster = vps[v].contentTableCluster;
                                    title = path + ":" + vps[v].versionName;
                                    goto found;
                                }
                            }
                        }
                    }
                }
            }
            cout << "Folder not found.\n";
            return;
        }
        
found:
        cout << "\n" << title << " (detailed):\n";
        cout << string(70, '-') << "\n";
        cout << setw(8) << left << "Type" << " " << setw(5) << "Perms" << " " 
             << setw(10) << right << "Size" << "  " << setw(16) << left << "Modified" << "  Name\n";
        cout << string(70, '-') << "\n";
        
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        bool empty = true;
        
        vector<uint64_t> chain = getChain(contentCluster);
        for (uint64_t c : chain) {
            for (int i = 0; i < 8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j = 0; j < SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type != TYPE_FREE && entries[j].name[0] != '\0') {
                        empty = false;
                        entries[j].name[23] = '\0';
                        entries[j].extension[7] = '\0';
                        
                        string typeStr;
                        if (entries[j].type == TYPE_LEVELED_DIR) typeStr = "<DIR>";
                        else if (entries[j].type == TYPE_FILE) typeStr = "<FILE>";
                        else if (entries[j].type == TYPE_SYMLINK) typeStr = "<LINK>";
                        else if (entries[j].type == TYPE_HARDLINK) typeStr = "<HARD>";
                        else if (entries[j].type == TYPE_LEVEL_MOUNT) typeStr = "<MNT>";
                        else typeStr = "<?>";
                        
                        string displayName = entries[j].name;
                        if (entries[j].type == TYPE_FILE && entries[j].extension[0] != '\0') {
                            displayName += ".";
                            displayName += entries[j].extension;
                        }
                        
                        string perms = getPermsStr(entries[j].attributes);
                        string sizeStr = (entries[j].type == TYPE_FILE) ? to_string(entries[j].size) : "-";
                        string modStr = formatTime(entries[j].modTime);
                        
                        cout << setw(8) << left << typeStr << " " 
                             << setw(5) << perms << " "
                             << setw(10) << right << sizeStr << "  "
                             << setw(16) << left << modStr << "  " 
                             << displayName << "\n";
                    }
                }
            }
        }
        if (empty) cout << "(empty)\n";
        cout << string(70, '-') << "\n";
    }
    
    // Filesystem check - validates integrity
    void fsck() {
        if (!disk.isOpen()) return;
        
        cout << "\n=== LevelFS Filesystem Check ===\n\n";
        
        int errors = 0;
        int warnings = 0;
        
        // Check 1: SuperBlock integrity
        cout << "[1/5] Checking SuperBlock...\n";
        if (sb.magic != MAGIC) {
            cout << "  ERROR: Invalid magic number!\n";
            errors++;
        } else {
            cout << "  OK: Magic number valid.\n";
        }
        
        if (sb.totalClusters == 0 || sb.totalClusters > 0xFFFFFFFF) {
            cout << "  ERROR: Invalid cluster count!\n";
            errors++;
        } else {
            cout << "  OK: Cluster count valid (" << sb.totalClusters << ").\n";
        }
        
        // Check 2: Root directory
        cout << "[2/5] Checking root directory...\n";
        if (sb.rootDirCluster == 0 || sb.rootDirCluster >= sb.totalClusters) {
            cout << "  ERROR: Invalid root directory cluster!\n";
            errors++;
        } else {
            uint8_t testBuf[SECTOR_SIZE];
            if (!disk.readSector(sb.rootDirCluster * 8, testBuf)) {
                cout << "  ERROR: Cannot read root directory!\n";
                errors++;
            } else {
                cout << "  OK: Root directory readable.\n";
            }
        }
        
        // Check 3: Level registry
        cout << "[3/5] Checking level registry...\n";
        if (sb.levelRegistryCluster == 0) {
            cout << "  WARNING: No level registry.\n";
            warnings++;
        } else {
            int levelCount = 0;
            vector<uint64_t> chain = getChain(sb.levelRegistryCluster);
            for (uint64_t c : chain) {
                LevelDescriptor reg[SECTOR_SIZE / sizeof(LevelDescriptor)];
                for (int s = 0; s < 8; s++) {
                    disk.readSector(c * 8 + s, reg);
                    for (int j = 0; j < SECTOR_SIZE / sizeof(LevelDescriptor); j++) {
                        if (reg[j].flags & LEVEL_FLAG_ACTIVE) levelCount++;
                    }
                }
            }
            cout << "  OK: " << levelCount << " active levels found.\n";
        }
        
        // Check 4: Free space consistency
        cout << "[4/5] Checking free space... ";
        cout.flush();
        
        uint64_t reportedFree = sb.totalFreeClusters;
        uint64_t sampleFree = 0;
        uint64_t sampleCount = 0;
        
        // Fast sampling: check every 100th cluster with spinner
        uint64_t dataStart = sb.labPoolStart + sb.labPoolClusters;
        uint64_t totalToCheck = min(sb.totalClusters - dataStart, (uint64_t)100000);
        const char spinner[] = "|/-\\";
        int spinIdx = 0;
        
        for (uint64_t i = 0; i < totalToCheck; i += 100) {
            uint64_t cluster = dataStart + i;
            if (cluster >= sb.totalClusters) break;
            
            LABEntry lab = getLABEntry(cluster);
            if (lab.nextCluster == LAT_FREE) sampleFree++;
            sampleCount++;
            
            // Update spinner every 1000 samples
            if (sampleCount % 10 == 0) {
                cout << "\b" << spinner[spinIdx++ % 4];
                cout.flush();
            }
        }
        
        cout << "\b \n";  // Clear spinner
        
        // Estimate total free from sample
        if (sampleCount > 0 && sampleFree > 0) {
            uint64_t estimatedFree = (sampleFree * 100);  // Scale up from sampling
            cout << "  OK: ~" << estimatedFree << " free clusters (sampled).\n";
        } else if (reportedFree > 0) {
            cout << "  OK: " << reportedFree << " free clusters (from superblock).\n";
        } else {
            cout << "  WARNING: Disk may be full.\n";
            warnings++;
        }
        
        // Check 5: Journal
        cout << "[5/5] Checking journal...\n";
        if (sb.journalStartCluster == 0) {
            cout << "  WARNING: No journal configured.\n";
            warnings++;
        } else {
            cout << "  OK: Journal at cluster " << sb.journalStartCluster << ".\n";
        }
        
        cout << "\n=== Check Complete ===\n";
        cout << "Errors: " << errors << ", Warnings: " << warnings << "\n";
        if (errors == 0) {
            cout << "Filesystem appears healthy.\n";
        } else {
            cout << "Filesystem has errors. Consider reformatting.\n";
        }
    }
    
    // Analyze fragmentation
    void fragInfo() {
        if (!disk.isOpen()) return;
        
        cout << "\n=== Fragmentation Analysis ===\n\n";
        
        int totalFiles = 0;
        int fragmentedFiles = 0;
        int totalFragments = 0;
        
        // Scan current directory for files
        vector<DirEntry> entries = readDirEntries(context.currentContentCluster);
        
        for (const auto& e : entries) {
            if (e.type == TYPE_FILE && e.startCluster != 0) {
                totalFiles++;
                vector<uint64_t> chain = getChain(e.startCluster);
                
                int fragments = 1;
                for (size_t i = 1; i < chain.size(); i++) {
                    if (chain[i] != chain[i-1] + 1) fragments++;
                }
                
                if (fragments > 1) {
                    fragmentedFiles++;
                    totalFragments += fragments;
                    
                    char nameBuf[25];
                    strncpy(nameBuf, e.name, 24);
                    nameBuf[24] = '\0';
                    string displayName = nameBuf;
                    if (e.extension[0]) {
                        displayName += ".";
                        displayName += e.extension;
                    }
                    cout << "  " << displayName << ": " << fragments << " fragments (" 
                         << chain.size() << " clusters)\n";
                }
            }
        }
        
        cout << "\nSummary:\n";
        cout << "  Total files: " << totalFiles << "\n";
        cout << "  Fragmented files: " << fragmentedFiles << "\n";
        
        if (totalFiles > 0) {
            int pct = (fragmentedFiles * 100) / totalFiles;
            cout << "  Fragmentation: " << pct << "%\n";
        }
    }
    
    // Defragment a single file
    bool defragFile(DirEntry& entry, uint64_t entrySector, int entryIdx) {
        if (entry.type != TYPE_FILE || entry.startCluster == 0) return true;
        
        vector<uint64_t> oldChain = getChain(entry.startCluster);
        if (oldChain.size() <= 1) return true;  // Already contiguous or empty
        
        // Check if already contiguous
        bool isContiguous = true;
        for (size_t i = 1; i < oldChain.size(); i++) {
            if (oldChain[i] != oldChain[i-1] + 1) {
                isContiguous = false;
                break;
            }
        }
        if (isContiguous) return true;
        
        // Need to find contiguous space
        uint64_t neededClusters = oldChain.size();
        uint64_t newStart = 0;
        uint64_t consecutive = 0;
        uint64_t dataStart = sb.labPoolStart + sb.labPoolClusters;
        
        for (uint64_t c = dataStart; c < sb.totalClusters; c++) {
            LABEntry lab = getLABEntry(c);
            if (lab.nextCluster == LAT_FREE) {
                if (consecutive == 0) newStart = c;
                consecutive++;
                if (consecutive >= neededClusters) break;
            } else {
                consecutive = 0;
            }
        }
        
        if (consecutive < neededClusters) {
            return false;  // Not enough contiguous space
        }
        
        // Copy data to new location
        for (size_t i = 0; i < oldChain.size(); i++) {
            uint8_t buffer[CLUSTER_SIZE];
            for (int s = 0; s < 8; s++) {
                disk.readSector(oldChain[i] * 8 + s, buffer + s * SECTOR_SIZE);
            }
            for (int s = 0; s < 8; s++) {
                disk.writeSector((newStart + i) * 8 + s, buffer + s * SECTOR_SIZE);
            }
            
            // Update LAT for new cluster
            if (i < oldChain.size() - 1) {
                setLATEntry(newStart + i, newStart + i + 1);
            } else {
                setLATEntry(newStart + i, LAT_END);
            }
            
            // Free old cluster
            setLATEntry(oldChain[i], LAT_FREE);
        }
        
        // Update entry
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        disk.readSector(entrySector, entries);
        entries[entryIdx].startCluster = newStart;
        disk.writeSector(entrySector, entries);
        
        return true;
    }
    
    // Defragment disk
    void defrag() {
        if (!disk.isOpen()) return;
        
        cout << "\n=== Disk Defragmentation ===\n\n";
        cout << "Analyzing fragmentation...\n";
        
        int defragged = 0;
        int failed = 0;
        
        vector<uint64_t> chain = getChain(context.currentContentCluster);
        for (uint64_t c : chain) {
            for (int i = 0; i < 8; i++) {
                DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
                disk.readSector(c * 8 + i, entries);
                
                for (int j = 0; j < SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FILE && entries[j].startCluster != 0) {
                        entries[j].name[23] = '\0';
                        string displayName = entries[j].name;
                        if (entries[j].extension[0]) {
                            displayName += ".";
                            displayName += entries[j].extension;
                        }
                        
                        cout << "  Processing: " << displayName << "... ";
                        cout.flush();
                        
                        if (defragFile(entries[j], c * 8 + i, j)) {
                            cout << "OK\n";
                            defragged++;
                        } else {
                            cout << "SKIP (no contiguous space)\n";
                            failed++;
                        }
                    }
                }
            }
        }
        
        cout << "\nDefragmentation complete.\n";
        cout << "  Files processed: " << defragged << "\n";
        cout << "  Files skipped: " << failed << "\n";
    }
    
    void createLevelMount(string path, uint64_t levelID) {
        if (!disk.isOpen()) return;
        
        LevelDescriptor* level = findLevelByID(levelID);
        if (!level) {
            cout << "Level ID " << levelID << " not found.\n";
            return;
        }
        
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path.\n"; return; }
        
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        vector<uint64_t> chain = getChain(res.parentCluster);
        
        for (uint64_t c : chain) {
            for (int i = 0; i < 8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j = 0; j < SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FREE) {
                        strcpy(entries[j].name, res.name.c_str());
                        entries[j].type = TYPE_LEVEL_MOUNT;
                        entries[j].startCluster = levelID;
                        entries[j].size = 0;
                        entries[j].createTime = time(0);
                        entries[j].modTime = time(0);
                        disk.writeSector(c * 8 + i, entries);
                        
                        level->refCount++;
                        updateLevelDescriptor(*level);
                        
                        cout << "Mounted level '" << level->name << "' (ID: " << levelID 
                             << ") at '" << res.name << "'\n";
                        return;
                    }
                }
            }
        }
        cout << "No space to create mount point.\n";
    }
    
    void updateLevelDescriptor(LevelDescriptor& updated) {
        vector<uint64_t> chain = getChain(sb.levelRegistryCluster);
        for (uint64_t c : chain) {
            LevelDescriptor registry[CLUSTER_SIZE / sizeof(LevelDescriptor)];
            for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                disk.readSector(c * SECTORS_PER_CLUSTER + s,
                    ((char*)registry) + s * SECTOR_SIZE);
            }
            for (int j = 0; j < CLUSTER_SIZE / sizeof(LevelDescriptor); j++) {
                if (registry[j].levelID == updated.levelID) {
                    registry[j] = updated;
                    for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                        disk.writeSector(c * SECTORS_PER_CLUSTER + s,
                            ((char*)registry) + s * SECTOR_SIZE);
                    }
                    return;
                }
            }
        }
    }

    
    void nav(string path) {
        if (!disk.isOpen()) return;
        if (path == "..") {
            context.currentDirCluster = sb.rootDirCluster;
            context.currentPath = "/";
            context.currentFolderPerms = PERM_READ | PERM_WRITE | PERM_EXEC;  // Root has full perms
            loadVersion("master");
            return;
        }
        string folderName = path;
        string levelName = "";
        size_t colon = path.find(':');
        if (colon != string::npos) {
            folderName = path.substr(0, colon);
            levelName = path.substr(colon+1);
        } else if (path.rfind(":", 0) == 0) {
             if (switchLevel(path.substr(1))) cout << "Switched to " << path.substr(1) << endl;
             else cout << "Version not found.\n";
             return;
        }

        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        
        vector<uint64_t> chain = getChain(context.currentContentCluster);
        for (uint64_t c : chain) {
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == folderName) {
                        // Check permissions before entering
                        if (!(entries[j].attributes & PERM_READ)) {
                            cout << "Permission denied: no read access to '" << folderName << "'.\n";
                            return;
                        }
                        if (!(entries[j].attributes & PERM_EXEC)) {
                            cout << "Permission denied: no execute access to enter '" << folderName << "'.\n";
                            return;
                        }
                        context.currentFolderPerms = entries[j].attributes;
                        enterFolder(entries[j].startCluster, folderName, levelName);
                        return;
                    }
                    if (entries[j].type == TYPE_LEVEL_MOUNT && string(entries[j].name) == folderName) {
                        uint64_t mountedLevelID = entries[j].startCluster;
                        LevelDescriptor* level = findLevelByID(mountedLevelID);
                        if (level) {
                            context.currentContentCluster = level->rootContentCluster;
                            context.currentLevelID = level->levelID;
                            context.currentVersion = level->name;
                            context.currentPath += folderName + "/";
                            cout << "Entered level mount '" << folderName << "' -> Level '" 
                                 << level->name << "' (ID: " << level->levelID << ")\n";
                        } else {
                            cout << "Mounted level not found.\n";
                        }
                        return;
                    }
                }
            }
        }
        cout << "Folder not found.\n";
    }
    
    void enterFolder(uint64_t cluster, string name, string level) {
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        vector<string> versions;
        for (int i=0; i<8; i++) {
             disk.readSector(cluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive) versions.push_back(vps[j].versionName);
             }
        }
        if (versions.empty()) {
            cout << "Folder " << name << " has no versions.\n";
            cout << "Create default 'main'? (y/n): ";
            char ans; cin >> ans;
            if (ans == 'y') {
                addLevel(cluster, "main");
                level = "main";
            } else return;
        }
        if (level.empty()) {
            cout << "Available versions: ";
            for (const auto& v : versions) cout << "[" << v << "] ";
            cout << "\nSelect version: ";
            cin >> level;
        }
        bool found = false;
        uint64_t newContent = 0;
        for (int i=0; i<8; i++) {
             disk.readSector(cluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == level) {
                     newContent = vps[j].contentTableCluster;
                     found = true;
                     break;
                 }
             }
             if(found) break;
        }
        if (!found) { cout << "Version not found.\n"; return; }
        context.currentDirCluster = cluster;
        context.currentContentCluster = newContent;
        context.currentVersion = level;
        context.currentPath += name + "/";
    }
    
    bool switchLevel(string ver) { return loadVersion(ver); }
    
    uint64_t registerNewLevel(const string& name, uint64_t parentLevelID, uint64_t contentCluster) {
        if (sb.levelRegistryCluster == 0) return 0;
        
        uint64_t newLevelID = sb.nextLevelID++;
        sb.totalLevels++;
        
        SYSTEMTIME st;
        GetSystemTime(&st);
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        uint64_t timestamp = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        
        vector<uint64_t> chain = getChain(sb.levelRegistryCluster);
        
        for (uint64_t c : chain) {
            LevelDescriptor registry[SECTOR_SIZE / sizeof(LevelDescriptor)];
            for (int s = 0; s < 8; s++) {
                disk.readSector(c * 8 + s, registry);
                for (int j = 0; j < SECTOR_SIZE / sizeof(LevelDescriptor); j++) {
                    if (registry[j].levelID == 0 && !(registry[j].flags & LEVEL_FLAG_ACTIVE)) {
                        strcpy(registry[j].name, name.c_str());
                        registry[j].levelID = newLevelID;
                        registry[j].parentLevelID = parentLevelID;
                        registry[j].rootContentCluster = contentCluster;
                        registry[j].createTime = timestamp;
                        registry[j].modTime = timestamp;
                        registry[j].flags = LEVEL_FLAG_ACTIVE;
                        registry[j].refCount = 1;
                        registry[j].childCount = 0;
                        disk.writeSector(c * 8 + s, registry);
                        
                        writeSuperBlock();
                        return newLevelID;
                    }
                }
            }
        }
        
        return 0;
    }
    
    void addLevel(uint64_t cluster, string name) {
        uint64_t cont = allocCluster();
        if (cont == 0) {
            cout << "Disk full. Cannot add level.\n";
            return;
        }
        
        uint64_t newLevelID = registerNewLevel(name, context.currentLevelID, cont);
        
        vector<uint64_t> chain = getChain(cluster);
        
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i=0; i<8; i++) {
                 disk.readSector(c * 8 + i, vps);
                 for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                     if (!vps[j].isActive) {
                         strcpy(vps[j].versionName, name.c_str());
                         vps[j].contentTableCluster = cont;
                         vps[j].isActive = 1;
                         vps[j].levelID = newLevelID;
                         vps[j].parentLevelID = context.currentLevelID;
                         vps[j].flags = LEVEL_FLAG_ACTIVE;
                         disk.writeSector(c * 8 + i, vps);
                         cout << "Added level '" << name << "' (ID: " << newLevelID << ", Parent: " << context.currentLevelID << ")\n";
                         return;
                     }
                 }
            }
        }
        
        uint64_t lastCluster = chain.back();
        uint64_t newCluster = allocCluster();
        if (newCluster == 0) {
            cout << "Disk full. Cannot add level.\n";
            return;
        }
        
        setLATEntry(lastCluster, newCluster);
        
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        memset(vps, 0, sizeof(vps));
        strcpy(vps[0].versionName, name.c_str());
        vps[0].contentTableCluster = cont;
        vps[0].isActive = 1;
        vps[0].levelID = newLevelID;
        vps[0].parentLevelID = context.currentLevelID;
        vps[0].flags = LEVEL_FLAG_ACTIVE;
        for (int i=0; i<8; i++) disk.writeSector(newCluster * 8 + i, vps);
        cout << "Added level '" << name << "' (ID: " << newLevelID << ", extended chain)\n";
    }
    
    void branchLevel(string folderName, string parentLevelName, string newLevelName) {
        if (!disk.isOpen()) return;
        
        uint64_t folderCluster = findFolderCluster(folderName);
        if (!folderCluster) {
            cout << "Folder '" << folderName << "' not found.\n";
            return;
        }
        
        VersionEntry* parentEntry = nullptr;
        VersionEntry foundEntry;
        uint64_t parentLevelID = 0;
        
        vector<uint64_t> chain = getChain(folderCluster);
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i = 0; i < 8; i++) {
                disk.readSector(c * 8 + i, vps);
                for (int j = 0; j < SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive && string(vps[j].versionName) == parentLevelName) {
                        foundEntry = vps[j];
                        parentLevelID = vps[j].levelID;
                        parentEntry = &foundEntry;
                        break;
                    }
                }
                if (parentEntry) break;
            }
            if (parentEntry) break;
        }
        
        if (!parentEntry) {
            cout << "Parent level '" << parentLevelName << "' not found.\n";
            return;
        }
        
        uint64_t newContentCluster = allocCluster();
        if (newContentCluster == 0) {
            cout << "Disk full. Cannot branch level.\n";
            return;
        }
        
        DirEntry emptyContent[CLUSTER_SIZE / sizeof(DirEntry)];
        memset(emptyContent, 0, sizeof(emptyContent));
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.writeSector(newContentCluster * SECTORS_PER_CLUSTER + s,
                ((char*)emptyContent) + s * SECTOR_SIZE);
        }
        
        uint64_t newLevelID = sb.nextLevelID++;
        sb.totalLevels++;
        
        SYSTEMTIME st;
        GetSystemTime(&st);
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        uint64_t timestamp = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        
        vector<uint64_t> regChain = getChain(sb.levelRegistryCluster);
        for (uint64_t c : regChain) {
            LevelDescriptor registry[CLUSTER_SIZE / sizeof(LevelDescriptor)];
            for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                disk.readSector(c * SECTORS_PER_CLUSTER + s,
                    ((char*)registry) + s * SECTOR_SIZE);
            }
            for (int j = 0; j < CLUSTER_SIZE / sizeof(LevelDescriptor); j++) {
                if (registry[j].levelID == 0 && !(registry[j].flags & LEVEL_FLAG_ACTIVE)) {
                    strcpy(registry[j].name, newLevelName.c_str());
                    registry[j].levelID = newLevelID;
                    registry[j].parentLevelID = parentLevelID;
                    registry[j].rootContentCluster = newContentCluster;
                    registry[j].createTime = timestamp;
                    registry[j].modTime = timestamp;
                    registry[j].flags = LEVEL_FLAG_ACTIVE | LEVEL_FLAG_DERIVED;
                    registry[j].refCount = 1;
                    registry[j].childCount = 0;
                    
                    for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                        disk.writeSector(c * SECTORS_PER_CLUSTER + s,
                            ((char*)registry) + s * SECTOR_SIZE);
                    }
                    goto registry_done;
                }
            }
        }
        registry_done:
        
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i = 0; i < 8; i++) {
                disk.readSector(c * 8 + i, vps);
                for (int j = 0; j < SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (!vps[j].isActive) {
                        strcpy(vps[j].versionName, newLevelName.c_str());
                        vps[j].contentTableCluster = newContentCluster;
                        vps[j].isActive = 1;
                        vps[j].levelID = newLevelID;
                        vps[j].parentLevelID = parentLevelID;
                        vps[j].flags = LEVEL_FLAG_ACTIVE | LEVEL_FLAG_DERIVED;
                        disk.writeSector(c * 8 + i, vps);
                        
                        writeSuperBlock();
                        cout << "Branched level '" << newLevelName << "' (ID: " << newLevelID 
                             << ") from '" << parentLevelName << "' (ID: " << parentLevelID << ")\n";
                        return;
                    }
                }
            }
        }
        
        cout << "No space in version table. Cannot branch.\n";
    }
    
    uint64_t findFolderCluster(string folderName) {
        if (folderName == ".") return context.currentDirCluster;
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            memset(entries, 0, sizeof(entries));
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == folderName) {
                    return entries[j].startCluster;
                }
            }
        }
        return 0;
    }
    
    void levelAdd(string folderName, string levelName) {
        if (!disk.isOpen()) return;
        uint64_t cluster = findFolderCluster(folderName);
        if (!cluster) {
            cout << "Folder '" << folderName << "' not found.\n";
            return;
        }
        addLevel(cluster, levelName);
    }
    
    void levelBranch(string folderName, string parentLevel, string newLevel) {
        branchLevel(folderName, parentLevel, newLevel);
    }
    
    void linkLevel(string dir1Path, string dir2Path, string sharedLevelName) {
        if (!disk.isOpen()) return;
        
        // Resolve both directory paths
        PathResult res1 = resolvePath(dir1Path);
        PathResult res2 = resolvePath(dir2Path);
        
        if (!res1.valid || !res2.valid) {
            cout << "Invalid directory path.\n";
            return;
        }
        
        // Find both directory clusters
        vector<DirEntry> entries1 = readDirEntries(res1.parentCluster);
        vector<DirEntry> entries2 = readDirEntries(res2.parentCluster);
        
        uint64_t dir1Cluster = 0;
        uint64_t dir2Cluster = 0;
        
        // Find first directory
        for (const auto& e : entries1) {
            if (e.type == TYPE_LEVELED_DIR && string(e.name) == res1.name) {
                dir1Cluster = e.startCluster;
                break;
            }
        }
        
        // Find second directory
        for (const auto& e : entries2) {
            if (e.type == TYPE_LEVELED_DIR && string(e.name) == res2.name) {
                dir2Cluster = e.startCluster;
                break;
            }
        }
        
        if (!dir1Cluster || !dir2Cluster) {
            cout << "One or both directories not found.\n";
            return;
        }
        
        if (dir1Cluster == dir2Cluster) {
            cout << "Cannot link a directory to itself.\n";
            return;
        }
        
        // Allocate shared content table cluster
        uint64_t sharedContentCluster = allocCluster();
        if (sharedContentCluster == 0) {
            cout << "Disk full. Cannot create shared level.\n";
            return;
        }
        
        // Initialize shared content cluster
        DirEntry emptyEntries[SECTOR_SIZE/sizeof(DirEntry)];
        memset(emptyEntries, 0, sizeof(emptyEntries));
        for (int i=0; i<8; i++) {
            disk.writeSector(sharedContentCluster * 8 + i, emptyEntries);
        }
        
        // Add level to first directory
        bool added1 = addLevelWithCluster(dir1Cluster, sharedLevelName, sharedContentCluster);
        if (!added1) {
            cout << "Failed to add level to first directory.\n";
            return;
        }
        
        // Add level to second directory (pointing to SAME cluster)
        bool added2 = addLevelWithCluster(dir2Cluster, sharedLevelName, sharedContentCluster);
        if (!added2) {
            cout << "Failed to add level to second directory.\n";
            return;
        }
        
        cout << "Created shared level '" << sharedLevelName << "' linking:\n";
        cout << "  " << dir1Path << " <-> " << dir2Path << "\n";
        cout << "Changes in one will appear in the other (DAG structure).\n";
    }
    
    bool addLevelWithCluster(uint64_t dirCluster, string levelName, uint64_t contentCluster) {
        // Follow LAT chain to find free slot or extend
        vector<uint64_t> chain = getChain(dirCluster);
        
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i=0; i<8; i++) {
                 disk.readSector(c * 8 + i, vps);
                 for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                     if (!vps[j].isActive) {
                         // Found free slot
                         strcpy(vps[j].versionName, levelName.c_str());
                         vps[j].contentTableCluster = contentCluster;
                         vps[j].isActive = 1;
                         disk.writeSector(c * 8 + i, vps);
                         return true;
                     }
                 }
            }
        }
        
        // No free slots - extend chain
        uint64_t lastCluster = chain.back();
        uint64_t newCluster = allocCluster();
        if (newCluster == 0) return false;
        
        setLATEntry(lastCluster, newCluster);
        
        // Initialize new cluster
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        memset(vps, 0, sizeof(vps));
        strcpy(vps[0].versionName, levelName.c_str());
        vps[0].contentTableCluster = contentCluster;
        vps[0].isActive = 1;
        for (int i=0; i<8; i++) disk.writeSector(newCluster * 8 + i, vps);
        
        return true;
    }

    vector<DirEntry> readDirEntries(uint64_t cluster) {
        vector<DirEntry> entries;
        vector<uint64_t> chain = getChain(cluster);
        
        for (uint64_t c : chain) {
            for (int i = 0; i < 8; i++) { 
                uint8_t buffer[SECTOR_SIZE];
                disk.readSector(c * 8 + i, buffer);
                DirEntry* de = (DirEntry*)buffer;
                for (int j = 0; j < SECTOR_SIZE / sizeof(DirEntry); j++) {
                    if (de[j].type != TYPE_FREE) {
                        entries.push_back(de[j]);
                    }
                }
            }
        }
        return entries;
    }

    void read(string path) {
        if (!disk.isOpen()) return;
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path.\n"; return; }
        
        vector<DirEntry> entries = readDirEntries(res.parentCluster);
        bool found = false;
        DirEntry fileEntry;
        
        // Find entry (can be file, symlink, or hardlink)
        for (const auto& e : entries) {
            if ((e.type == TYPE_FILE || e.type == TYPE_SYMLINK || e.type ==TYPE_HARDLINK) && 
                string(e.name) == res.name) {
                fileEntry = e;
                found = true;
                break;
            }
        }
        
        if (!found) { cout << "File not found.\n"; return; }
        
        // Check read permission
        if (!(fileEntry.attributes & PERM_READ)) {
            cout << "Permission denied: no read access.\n";
            return;
        }
        
        
        // Follow symlink (with loop detection)
        int symlinkDepth = 0;
        while (fileEntry.type == TYPE_SYMLINK && symlinkDepth < 10) {
            if (fileEntry.startCluster == 0) {
                cout << "Broken symlink.\n";
                return;
            }
            
            // Read target path from symlink cluster
            char targetPath[CLUSTER_SIZE];
            memset(targetPath, 0, CLUSTER_SIZE);
            for (int i=0; i<8; i++) {
                disk.readSector(fileEntry.startCluster * 8 + i, targetPath + (i * SECTOR_SIZE));
            }
            
            // Resolve target
            PathResult targetRes = resolvePath(string(targetPath));
            if (!targetRes.valid) {
                cout << "Broken symlink: target '" << targetPath << "' not found.\n";
                return;
            }
            
            // Get target entry
            vector<DirEntry> targetEntries = readDirEntries(targetRes.parentCluster);
            found = false;
            for (const auto& e : targetEntries) {
                if ((e.type == TYPE_FILE || e.type == TYPE_HARDLINK) && string(e.name) == targetRes.name) {
                    fileEntry = e;
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                cout << "Broken symlink: target not found.\n";
                return;
            }
            symlinkDepth++;
        }
        
        if (symlinkDepth >= 10) {
            cout << "Symlink loop detected or max depth exceeded.\n";
            return;
        }
        
        // Now read the actual file (regular or hardlink)
        if (fileEntry.size == 0) return;
        
        vector<uint64_t> chain = getChain(fileEntry.startCluster);
        uint64_t remaining = fileEntry.size;
        
        for (uint64_t c : chain) {
            if (remaining == 0) break;
            uint64_t toRead = std::min((uint64_t)CLUSTER_SIZE, remaining);
            
            char buffer[CLUSTER_SIZE];
            for (int i=0; i<8; i++) {
                disk.readSector(c*8 + i, buffer + (i*SECTOR_SIZE));
            }
            cout.write(buffer, toRead);
            remaining -= toRead;
        }
        cout << endl;
    }



    void del(string path, bool recursive) {
        if (!disk.isOpen()) return;
        
        // Check if current folder allows write (for deletion)
        if (!(context.currentFolderPerms & PERM_WRITE)) {
            cout << "Permission denied: current folder is read-only.\n";
            return;
        }
        
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path.\n"; return; }

        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        uint64_t foundCluster = 0;
        int foundSector = -1, foundIdx = -1;
        
        // Follow LAT chain for deletion search
        vector<uint64_t> chain = getChain(res.parentCluster);
        for (uint64_t c : chain) {
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type != TYPE_FREE && string(entries[j].name) == res.name) {
                        if (entries[j].type == TYPE_LEVELED_DIR && !recursive) {
                             VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
                             for(int k=0; k<8; k++) {
                                 disk.readSector(entries[j].startCluster*8 + k, vps);
                                 for(int l=0; l<SECTOR_SIZE/sizeof(VersionEntry); l++) {
                                     if(vps[l].isActive) {
                                         cout << "Folder not empty. Use -r.\n";
                                         return;
                                     }
                                 }
                             }
                        }
                        
                        // Check write permission for files
                        if (entries[j].type == TYPE_FILE && !(entries[j].attributes & PERM_WRITE)) {
                            cout << "Permission denied: no write access to delete '" << res.name << "'.\n";
                            return;
                        }
                        
                        // Handle hardlink reference counting
                        if (entries[j].type == TYPE_HARDLINK || entries[j].type == TYPE_FILE) {
                            if (entries[j].attributes > 1) {
                                // Decrement reference count for all links pointing to this data
                                uint64_t dataCluster = entries[j].startCluster;
                                decrementRefCount(dataCluster);
                                cout << "Deleted hardlink " << res.name << " (refcount decremented).\n";
                            } else {
                                cout << "Deleted " << res.name << ".\n";
                            }
                        } else if (entries[j].type == TYPE_SYMLINK) {
                            cout << "Deleted symlink " << res.name << ".\n";
                        } else {
                            cout << "Deleted " << res.name << ".\n";
                        }
                        
                        if (entries[j].startCluster != 0 && entries[j].type != TYPE_SYMLINK) {
                            if (entries[j].type == TYPE_LEVELED_DIR) {
                                VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
                                disk.readSector(entries[j].startCluster * 8, vps);
                                for (int v = 0; v < SECTOR_SIZE/sizeof(VersionEntry); v++) {
                                    if (vps[v].isActive && vps[v].contentTableCluster != 0) {
                                        freeChain(vps[v].contentTableCluster);
                                    }
                                }
                                freeChain(entries[j].startCluster);
                            } else {
                                freeChain(entries[j].startCluster);
                            }
                        }
                        
                        entries[j].type = TYPE_FREE;
                        memset(&entries[j], 0, sizeof(DirEntry));
                        disk.writeSector(c * 8 + i, entries);
                        return;
                    }
                }
            }
        }
        cout << "Target not found.\n";
    }
    
    void decrementRefCount(uint64_t dataCluster) {
        // Find all entries using this data cluster and decrement their refcount
        vector<uint64_t> dirChain = getChain(context.rootContentCluster);
        for (uint64_t c : dirChain) {
            for (int i=0; i<8; i++) {
                DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
                disk.readSector(c * 8 + i, entries);
                bool modified = false;
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if ((entries[j].type == TYPE_FILE || entries[j].type == TYPE_HARDLINK) &&
                        entries[j].startCluster == dataCluster && entries[j].attributes > 0) {
                        entries[j].attributes--;
                        modified = true;
                    }
                }
                if (modified) disk.writeSector(c * 8 + i, entries);
            }
        }
    }

    void move(string srcPath, string dstPath) {
        if (!disk.isOpen()) return;
        
        PathResult srcRes = resolvePath(srcPath);
        if (!srcRes.valid) { cout << "Invalid source path.\n"; return; }
        
        PathResult dstRes = resolvePath(dstPath);
        if (!dstRes.valid) { cout << "Invalid destination path.\n"; return; }

        DirEntry srcEntry;
        bool found = false;
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        
        // Find Source - Follow LAT chain
        vector<uint64_t> srcChain = getChain(srcRes.parentCluster);
        for (uint64_t c : srcChain) {
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type != TYPE_FREE && string(entries[j].name) == srcRes.name) {
                        srcEntry = entries[j];
                        found = true;
                        entries[j].type = TYPE_FREE;
                        disk.writeSector(c * 8 + i, entries);
                        goto found_src;
                    }
                }
            }
        }
found_src:
        if (!found) { cout << "Source not found.\n"; return; }
        
        // Write to Dest - Follow LAT chain
        vector<uint64_t> dstChain = getChain(dstRes.parentCluster);
        for (uint64_t c : dstChain) {
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_FREE) {
                        entries[j] = srcEntry;
                        strcpy(entries[j].name, dstRes.name.c_str());
                        disk.writeSector(c * 8 + i, entries);
                        cout << "Moved " << srcPath << " to " << dstPath << endl;
                        return;
                    }
                }
            }
        }
    }

    void levelRemove(string folderName, string levelName) {
        if (!disk.isOpen()) return;
        uint64_t cluster = findFolderCluster(folderName);
        if (!cluster) {
            cout << "Folder '" << folderName << "' not found.\n";
            return;
        }
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(cluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == levelName) {
                     if (levelName == "master") {
                         cout << "Cannot remove master level.\n";
                         return;
                     }
                     vps[j].isActive = 0;
                     disk.writeSector(cluster * 8 + i, vps);
                     cout << "Removed level " << levelName << " from " << folderName << endl;
                     return;
                 }
             }
        }
        cout << "Level '" << levelName << "' not found.\n";
    }

    void levelRename(string folderName, string oldName, string newName) {
        if (!disk.isOpen()) return;
        uint64_t cluster = findFolderCluster(folderName);
        if (!cluster) {
            cout << "Folder '" << folderName << "' not found.\n";
            return;
        }
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(cluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == oldName) {
                     strcpy(vps[j].versionName, newName.c_str());
                     disk.writeSector(cluster * 8 + i, vps);
                     cout << "Renamed level " << oldName << " to " << newName << " in " << folderName << "\n";
                     return;
                 }
             }
        }
        cout << "Level '" << oldName << "' not found.\n";
    }

    void current() {
        if (!disk.isOpen()) {
            cout << "Not mounted.\n";
            return;
        }
        cout << "Path: " << context.currentPath << "\n";
        cout << "Level: " << context.currentVersion << "\n";
        cout << "Directory Cluster: " << context.currentDirCluster << "\n";
        cout << "Content Cluster: " << context.currentContentCluster << "\n";
    }

    void write(string path) {
        if (!disk.isOpen()) return;
        
        // Check if current folder allows write (for new files)
        if (!(context.currentFolderPerms & PERM_WRITE)) {
            cout << "Permission denied: current folder is read-only.\n";
            return;
        }
        
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path location.\n"; return; }
        string name = res.name;
        uint64_t contentCluster = res.parentCluster;
        
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        int foundSector = -1, foundIdx = -1;
        bool isNew = true;
        
        // Scan directory (following chain) for name or free slot
        // NOTE: readDirEntries simplifies this but we need sector location to Update.
        // So we must manually scan.
        vector<uint64_t> dirChain = getChain(contentCluster);
        DirEntry foundEntry;

        // First pass: look for existing file
        for (uint64_t c : dirChain) {
            for (int i=0; i<8; i++) {
                uint8_t sec[SECTOR_SIZE];
                disk.readSector(c*8 + i, sec);
                DirEntry* des = (DirEntry*)sec;
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (des[j].type == TYPE_FILE && string(des[j].name) == name) {
                        foundEntry = des[j];
                        isNew = false;
                        foundSector = (c*8) + i; // absolute sector
                        foundIdx = j;
                        goto scan_done;
                    }
                }
            }
        }
        
        // Second pass: look for free slot
        if (isNew) {
            for (uint64_t c : dirChain) {
                 for (int i=0; i<8; i++) {
                     uint8_t sec[SECTOR_SIZE];
                     disk.readSector(c*8 + i, sec);
                     DirEntry* des = (DirEntry*)sec;
                     for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                         if (des[j].type == TYPE_FREE) {
                             foundSector = (c*8) + i;
                             foundIdx = j;
                             goto scan_done;
                         }
                     }
                 }
            }
            cout << "Directory full.\n";
            return;
        }

scan_done:
        // Check write permission for existing files
        if (!isNew && !(foundEntry.attributes & PERM_WRITE)) {
            cout << "Permission denied: no write access to '" << name << "'.\n";
            return;
        }
        
        cout << "--- Editor: " << name << " ---\n";
        cout << "Type content. End with line '.done'\n";
        string content, line;
        while (getline(cin, line)) {
            if (line == ".done") break;
            content += line + "\n";
        }
        if (content.empty()) { cout << "No content.\n"; return; }
        
        vector<uint8_t> data(content.begin(), content.end());

        // Log operation to journal
        uint64_t txId = journal->logOperation(OP_WRITE, contentCluster, name);

        // Update Entry
        uint8_t sectorData[SECTOR_SIZE];
        disk.readSector(foundSector, sectorData);
        DirEntry* entryPtr = (DirEntry*)sectorData + foundIdx;
        
        if (isNew) {
            entryPtr->type = TYPE_FILE;
            strncpy(entryPtr->name, name.c_str(), 23);
            entryPtr->startCluster = allocCluster();
            entryPtr->createTime = time(0);
            entryPtr->attributes = PERM_DEFAULT;
            if (entryPtr->startCluster == 0) { cout << "Disk full.\n"; return; }
        }
        
        entryPtr->modTime = time(0);
        entryPtr->size = data.size();
        
        uint64_t startCluster = entryPtr->startCluster;
        disk.writeSector(foundSector, sectorData);
        
        uint64_t current = startCluster;
        uint64_t offset = 0;
        uint64_t total = data.size();
        
        while (offset < total) {
             uint64_t chunk = std::min((uint64_t)CLUSTER_SIZE, total - offset);
             uint8_t buffer[CLUSTER_SIZE];
             memset(buffer, 0, CLUSTER_SIZE);
             memcpy(buffer, data.data() + offset, chunk);
             
             for (int i=0; i<8; i++) {
                 disk.writeSector(current*8 + i, buffer + (i*SECTOR_SIZE));
             }
             
             offset += chunk;
             
             if (offset < total) {
                 uint64_t next = getLATEntry(current);
                 if (next == LAT_END || next == 0) {
                     next = allocCluster();
                     if (next == 0) { cout << "Disk full during write.\n"; break; }
                     setLATEntry(current, next);
                 }
                 current = next;
             }
        }
        setLATEntry(current, LAT_END);
        
        journal->commitOperation(txId);
        
        cout << "Written " << total << " bytes.\n";
    }

    void setVerbose(bool v) {
        disk.setVerbose(v);
        cout << "Disk logging " << (v ? "ENABLED" : "DISABLED") << ".\n";
    }
    
    void listAllLevels() {
        if (!disk.isOpen()) { cout << "Not mounted.\n"; return; }
        if (sb.levelRegistryCluster == 0) { cout << "No Level Registry.\n"; return; }
        
        cout << "\n=== Global Level Registry ===\n";
        cout << "  Total Levels: " << sb.totalLevels << "\n";
        cout << "  Next Level ID: " << sb.nextLevelID << "\n\n";
        cout << setw(4) << "ID" << "  " << setw(16) << left << "Name" 
             << setw(8) << "Parent" << setw(10) << "RefCount" << "Flags\n";
        cout << string(50, '-') << "\n";
        
        vector<uint64_t> chain = getChain(sb.levelRegistryCluster);
        
        for (uint64_t c : chain) {
            LevelDescriptor registry[SECTOR_SIZE / sizeof(LevelDescriptor)];
            for (int s = 0; s < 8; s++) {
                disk.readSector(c * 8 + s, registry);
                for (int j = 0; j < SECTOR_SIZE / sizeof(LevelDescriptor); j++) {
                    if (registry[j].flags & LEVEL_FLAG_ACTIVE) {
                        string flagStr = "";
                        if (registry[j].flags & LEVEL_FLAG_SHARED) flagStr += "SHR ";
                        if (registry[j].flags & LEVEL_FLAG_LOCKED) flagStr += "LCK ";
                        if (registry[j].flags & LEVEL_FLAG_SNAPSHOT) flagStr += "SNP ";
                        if (registry[j].flags & LEVEL_FLAG_DERIVED) flagStr += "DRV ";
                        if (flagStr.empty()) flagStr = "ACT";
                        
                        cout << setw(4) << right << registry[j].levelID << "  "
                             << setw(16) << left << registry[j].name
                             << setw(8) << right << registry[j].parentLevelID
                             << setw(10) << registry[j].refCount
                             << flagStr << "\n";
                    }
                }
            }
        }
        cout << "\n";
    }
    
    uint64_t getCurrentLevelID() { return context.currentLevelID; }
    string getCurrentPath() { return context.currentPath; }
    string getCurrentVersion() { return context.currentVersion; }
};

int main(int argc, char** argv) {
    FileSystemShell fs;
    string input;
    
    // Welcome Screen
    cout << "==========================================\n";
    cout << "      Welcome to Linuxify LevelFS        \n";
    cout << "==========================================\n";
    cout << "Type 'help' for commands.\n";
    cout << "Type 'log on' to see disk operations.\n\n";
    
    if (argc > 1) {
        string arg = argv[1];
        if (arg.length() == 1 && isalpha(arg[0])) {
            fs.mount(arg[0]);
        } else {
            cout << "Error: Invalid disk '" << arg << "'. This tool only supports physical disks (e.g. 'D'). Image files are not supported.\n";
            return 1;
        }
    } else {
        cout << "Usage: mount.exe <DriveLetter>\n";
    }

    while (true) {
        try {
            if (fs.isMounted()) {
                cout << "fs:" << fs.getCurrentPath() << ":" << fs.getCurrentVersion() << "$ " << flush;
            } else {
                cout << "fs> " << flush;
            }
            
            if (!getline(cin, input)) break;
            if (input.empty()) continue;

            stringstream ss(input);
            string cmd;
            ss >> cmd;

            if (cmd == "exit") break;
            if (cmd == "mount") {
                string arg; ss >> arg;
                if (arg.length() == 1 && isalpha(arg[0])) {
                    fs.mount(arg[0]);
                } else {
                     cout << "Error: Invalid disk '" << arg << "'. This tool only supports physical disks (e.g. 'D'). Image files are not supported.\n";
                }
            }
            else if (cmd == "log") {
                string state; ss >> state;
                if (state == "on") fs.setVerbose(true);
                else if (state == "off") fs.setVerbose(false);
                else cout << "Usage: log <on|off>\n";
            }
            else if (cmd == "look") {
                string arg; ss >> arg;
                if (arg == "-d") {
                    string path; ss >> path;
                    fs.lookDetailed(path);
                } else {
                    fs.look(arg);
                }
            }
            else if (cmd == "perms") {
                string option, path;
                ss >> option >> path;
                if (option.empty() || path.empty()) {
                    cout << "Usage: perms <+r|-r|+w|-w|+x|-x> <path>\n";
                } else {
                    fs.perms(option, path);
                }
            }
            else if (cmd == "dir-tree") fs.dirTree();
            else if (cmd == "create") {
                string type, name, ext;
                ss >> type >> name >> ext;
                fs.create(type, name, ext);
            }
            else if (cmd == "nav") {
                string path; ss >> path;
                fs.nav(path);
            }
            else if (cmd == "read") {
                string file; ss >> file;
                fs.read(file);
            }
            else if (cmd == "del") {
                string arg1, arg2;
                ss >> arg1;
                fs.del(arg1, arg2 == "-r");
            }
            else if (cmd == "move") {
                string src, dst;
                ss >> src >> dst;
                fs.move(src, dst);
            }
            else if (cmd == "level") {
                string sub, arg1, arg2, arg3;
                ss >> sub;
                if (sub == "add") {
                    ss >> arg1 >> arg2;
                    if (!arg1.empty() && !arg2.empty()) fs.levelAdd(arg1, arg2);
                    else cout << "Usage: level add <folder|.> <levelname>\n";
                }
                else if (sub == "branch") {
                    ss >> arg1 >> arg2 >> arg3;
                    if (!arg1.empty() && !arg2.empty() && !arg3.empty()) fs.levelBranch(arg1, arg2, arg3);
                    else cout << "Usage: level branch <folder|.> <parent_level> <new_level>\n";
                }
                else if (sub == "remove") {
                    ss >> arg1 >> arg2;
                    if (!arg1.empty() && !arg2.empty()) fs.levelRemove(arg1, arg2);
                    else cout << "Usage: level remove <folder|.> <levelname>\n";
                }
                else if (sub == "rename") {
                    ss >> arg1 >> arg2 >> arg3;
                    if (!arg1.empty() && !arg2.empty() && !arg3.empty()) fs.levelRename(arg1, arg2, arg3);
                    else cout << "Usage: level rename <folder|.> <old> <new>\n";
                }
            }
            else if (cmd == "link") {
                string dir1, dir2, levelName;
                ss >> dir1 >> dir2 >> levelName;
                if (dir1.empty() || dir2.empty() || levelName.empty()) {
                    cout << "Usage: link <dir1> <dir2> <shared_level_name>\n";
                } else {
                    fs.linkLevel(dir1, dir2, levelName);
                }
            }
            else if (cmd == "mount-level") {
                string path, levelIdStr;
                ss >> path >> levelIdStr;
                if (path.empty() || levelIdStr.empty()) {
                    cout << "Usage: mount-level <path> <levelID>\n";
                } else {
                    fs.createLevelMount(path, stoull(levelIdStr));
                }
            }
            else if (cmd == "current") fs.current();
            else if (cmd == "levels") fs.listAllLevels();
            else if (cmd == "symlink") {
                string target, link;
                ss >> target >> link;
                if (target.empty() || link.empty()) cout << "Usage: symlink <target> <linkname>\n";
                else fs.createSymlink(link, target);
            }
            else if (cmd == "hardlink") {
                string target, link;
                ss >> target >> link;
                if (target.empty() || link.empty()) cout << "Usage: hardlink <target> <linkname>\n";
                else fs.createHardlink(link, target);
            }
            else if (cmd == "write") {
                string file; ss >> file;
                if (file.empty()) cout << "Usage: write <filename>\n";
                else fs.write(file);
            }
            else if (cmd == "help") {
                cout << "Commands:\n";
                cout << "  mount <path>  - Mount disk/image\n";
                cout << "  log <on|off>  - Toggle disk op logging\n";
                cout << "  look          - List directory contents\n";
                cout << "  look <folder> - List levels of a folder\n";
                cout << "  look <f>:<l>  - List contents of folder:level\n";
                cout << "  look -d [path]- Detailed view (size, perms, time)\n";
                cout << "  dir-tree      - Display directory tree\n";
                cout << "  current       - Show current path and level\n";
                cout << "  levels        - List all levels in registry\n";
                cout << "  create folder <n> - Create folder\n";
                cout << "  create file <n> [ext] - Create file (e.g. readme txt)\n";
                cout << "  write <name>  - Text editor for file\n";
                cout << "  read <name>   - Read file contents\n";
                cout << "  perms <+/-rwx> <file> - Set permissions (+r,-w,+x...)\n";
                cout << "  symlink <target> <link> - Create symbolic link\n";
                cout << "  hardlink <target> <link> - Create hard link\n";
                cout << "  mount-level <path> <id> - Mount level by ID at path\n";
                cout << "  nav <path>    - Navigate to folder\n";
                cout << "  del <name>    - Delete entry\n";
                cout << "  move <s> <d>  - Move/rename entry\n";
                cout << "  level add <f> <n>    - Add level to folder/.\n";
                cout << "  level branch <f> <p> <n> - Branch level from parent\n";
                cout << "  level remove <f> <n> - Remove level from folder/.\n";
                cout << "  link <dir1> <dir2> <level> - Create shared level (DAG)\n";
                cout << "  fsck          - Check filesystem integrity\n";
                cout << "  fraginfo      - Show fragmentation info\n";
                cout << "  defrag        - Defragment disk\n";
                cout << "  exit          - Exit\n";
            }
            else if (cmd == "fsck") fs.fsck();
            else if (cmd == "fraginfo") fs.fragInfo();
            else if (cmd == "defrag") fs.defrag();
            else cout << "Unknown command. Type 'help' for list.\n";
        } catch (const exception& e) {
            cout << "Error: " << e.what() << "\n";
        } catch (...) {
            cout << "Unknown error occurred.\n";
        }
    }
    return 0;
}
