/*
 * Compile: g++ mount.cpp -o mount.exe
 */

#include "fs_common.hpp"
#include "journal.hpp"

class FileSystemShell {
    DiskDevice disk;
    SuperBlock sb;
    Journal* journal;  // Pointer because we initialize it after reading SB
    
    struct {
        uint64_t currentDirCluster; 
        uint64_t currentContentCluster; 
        uint64_t rootContentCluster;
        string currentPath;
        string currentVersion;
    } context;

public:
    FileSystemShell() : journal(nullptr) {
        memset(&context, 0, sizeof(context));
        context.currentPath = "/";
    }
    
    ~FileSystemShell() {
        if (journal) delete journal;
    }

    bool mount(char driveLetter) {
        if (!disk.open(driveLetter)) return false;
        
        if (!disk.readSector(0, &sb)) {
            disk.close();
            return false;
        }
        if (sb.magic != MAGIC) {
            // Try backup superblock
            if (!tryBackupSuperblock()) {
                disk.close();
                return false;
            }
        }

        // Initialize Journal
        journal = new Journal(&disk, &sb);
        journal->replayJournal();  // Crash recovery

        context.currentDirCluster = sb.rootDirCluster;
        context.currentPath = "/";
        
        cout << "Mounted successfully. At Root.\n";
        
        if(loadVersion("master")) {
            cout << "Context: master\n";
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
            // Try backup superblock
            if (!tryBackupSuperblock()) {
                cout << "Invalid magic: " << hex << sb.magic << dec << "\n";
                disk.close();
                return false;
            }
        }

        // Initialize Journal
        journal = new Journal(&disk, &sb);
        journal->replayJournal();  // Crash recovery

        context.currentDirCluster = sb.rootDirCluster;
        context.currentPath = "/";
        
        cout << "Mounted image: " << path << " (" << (disk.getDiskSize()/1024/1024) << " MB)\n";
        
        if(loadVersion("master")) {
            cout << "Context: master\n";
            context.rootContentCluster = context.currentContentCluster;
        } else {
            cout << "No master version found. Creating...\n";
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            memset(vps, 0, sizeof(vps));
            strcpy(vps[0].versionName, "master");
            vps[0].isActive = 1;
            vps[0].contentTableCluster = allocCluster();
            disk.writeSector(sb.rootDirCluster * 8, vps);
            loadVersion("master");
            context.rootContentCluster = context.currentContentCluster;
        }
        return true;
    }

    uint64_t getLATEntry(uint64_t cluster) {
        uint64_t latOffset = cluster * sizeof(uint64_t);
        uint64_t sectorOffset = latOffset / SECTOR_SIZE;
        uint64_t entryOffset = latOffset % SECTOR_SIZE;
        
        uint64_t sectorIdx = (sb.latStartCluster * 8) + sectorOffset;
        
        uint8_t buffer[SECTOR_SIZE];
        disk.readSector(sectorIdx, buffer);
        uint64_t* entry = (uint64_t*)(buffer + entryOffset);
        return *entry;
    }

    void setLATEntry(uint64_t cluster, uint64_t value) {
        uint64_t latOffset = cluster * sizeof(uint64_t);
        uint64_t sectorOffset = latOffset / SECTOR_SIZE;
        uint64_t entryOffset = latOffset % SECTOR_SIZE;
        
        uint64_t sectorIdx = (sb.latStartCluster * 8) + sectorOffset;
        
        uint8_t buffer[SECTOR_SIZE];
        disk.readSector(sectorIdx, buffer);
        uint64_t* entry = (uint64_t*)(buffer + entryOffset);
        *entry = value;
        
        disk.writeSector(sectorIdx, buffer);
    }

    uint64_t allocCluster() {
        // Simple scan for Free Cluster in LAT
        for (uint64_t i = 0; i < sb.latSectors; i++) {
             uint64_t sectorIdx = (sb.latStartCluster * 8) + i;
             uint8_t buffer[SECTOR_SIZE];
             disk.readSector(sectorIdx, buffer);
             uint64_t* entries = (uint64_t*)buffer;
             
             for (int j = 0; j < SECTOR_SIZE / 8; j++) {
                 if (entries[j] == LAT_FREE) {
                     uint64_t clusterIdx = (i * (SECTOR_SIZE/8)) + j;
                     if (clusterIdx == 0) continue; 
                     
                     if (clusterIdx < (sb.totalSectors / sb.clusterSize)) {
                         setLATEntry(clusterIdx, LAT_END);
                         return clusterIdx;
                     }
                 }
             }
        }
        return 0; // No space
    }

    // Helper to traverse chain
    vector<uint64_t> getChain(uint64_t startCluster) {
        vector<uint64_t> chain;
        uint64_t current = startCluster;
        while (current != 0 && current != LAT_END && current != LAT_BAD && current < (sb.totalSectors/sb.clusterSize)) {
            chain.push_back(current);
            current = getLATEntry(current);
            if (find(chain.begin(), chain.end(), current) != chain.end()) break; // Cycle
            if (chain.size() > 1000000) break; // Safety
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
        // Read all VersionEntries following the chain
        vector<uint64_t> chain = getChain(context.currentDirCluster);
        
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, vps);
                for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                    if (vps[j].isActive && string(vps[j].versionName) == ver) {
                        context.currentContentCluster = vps[j].contentTableCluster;
                        context.currentVersion = ver;  // FIX: Update the version name
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
                        entries[j].type == TYPE_SYMLINK || entries[j].type == TYPE_HARDLINK) {
                        empty = false;
                        entries[j].name[31] = '\0';
                        string typeStr;
                        if (entries[j].type == TYPE_LEVELED_DIR) typeStr = "<L-DIR>";
                        else if (entries[j].type == TYPE_FILE) typeStr = "<FILE>";
                        else if (entries[j].type == TYPE_SYMLINK) typeStr = "<SYMLNK>";
                        else if (entries[j].type == TYPE_HARDLINK) typeStr = "<HDLINK>";
                        
                        cout << setw(8) << left << typeStr << " " << entries[j].name;
                        
                        // Show target for symlinks
                        if (entries[j].type == TYPE_SYMLINK && entries[j].startCluster != 0) {
                            char targetBuf[CLUSTER_SIZE];
                            memset(targetBuf, 0, CLUSTER_SIZE);
                            for (int s=0; s<8; s++) {
                                disk.readSector(entries[j].startCluster * 8 + s, targetBuf + (s * SECTOR_SIZE));
                            }
                            cout << " -> " << targetBuf;
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
                    entries[j].name[31] = '\0';
                    folders.push_back({string(entries[j].name), entries[j].startCluster});
                } else if (entries[j].type == TYPE_FILE) {
                    entries[j].name[31] = '\0';
                    files.push_back(string(entries[j].name));
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
    
    void create(string type, string path) {
        if (!disk.isOpen()) return;
        PathResult res = resolvePath(path);
        if (!res.valid) { cout << "Invalid path location.\n"; return; }
        
        createInCluster(res.parentCluster, type, res.name);
    }
    
    void createInCluster(uint64_t contentCluster, string type, string name) {
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        int freeSector = -1;
        int freeIdx = -1;
        uint64_t targetCluster = contentCluster;
        
        // Follow LAT chain to find free slot
        vector<uint64_t> chain = getChain(contentCluster);
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
        // If no free slot found, extend the chain
        if (!slotFound) {
            uint64_t lastCluster = chain.back();
            uint64_t newCluster = allocCluster();
            if (newCluster == 0) {
                cout << "Disk full. Cannot create " << type << ".\n";
                return;
            }
            
            // Link new cluster to chain
            setLATEntry(lastCluster, newCluster);
            
            // Initialize new cluster
            memset(entries, 0, sizeof(entries));
            for (int i=0; i<8; i++) {
                disk.writeSector(newCluster * 8 + i, entries);
            }
            
            targetCluster = newCluster;
            freeSector = 0;
            freeIdx = 0;
            
            // Re-read for modification
            disk.readSector(newCluster * 8, entries);
            cout << "[Extended directory chain to cluster " << newCluster << "]\n";
        }
        
        DirEntry* target = &entries[freeIdx];
        strcpy(target->name, name.c_str());
        
        if (type == "folder") {
            target->type = TYPE_LEVELED_DIR;
            target->startCluster = allocCluster();
        } else if (type == "symlink") {
            target->type = TYPE_SYMLINK;
            target->startCluster = 0;  // Will be set by createSymlink
            target->size = 0;
        } else if (type == "hardlink") {
            target->type = TYPE_HARDLINK;
            target->startCluster = 0;  // Will be set by createHardlink
            target->size = 0;
            target->attributes = 1;  // Reference count starts at 1
        } else {  // regular file
            target->type = TYPE_FILE;
            target->startCluster = allocCluster(); 
            target->size = 0;
        }
        disk.writeSector(targetCluster * 8 + freeSector, entries);
        cout << "Created " << type << " " << name << ".\n";
    }

    
    void nav(string path) {
        if (!disk.isOpen()) return;
        if (path == "..") {
            context.currentDirCluster = sb.rootDirCluster;
            context.currentPath = "/";
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
        
        // Follow LAT chain for folder search
        vector<uint64_t> chain = getChain(context.currentContentCluster);
        for (uint64_t c : chain) {
            for (int i=0; i<8; i++) {
                disk.readSector(c * 8 + i, entries);
                for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                    if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == folderName) {
                        enterFolder(entries[j].startCluster, folderName, levelName);
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
    
    void addLevel(uint64_t cluster, string name) {
        uint64_t cont = allocCluster();
        
        // Follow LAT chain to find free slot or extend
        vector<uint64_t> chain = getChain(cluster);
        
        for (uint64_t c : chain) {
            VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
            for (int i=0; i<8; i++) {
                 disk.readSector(c * 8 + i, vps);
                 for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                     if (!vps[j].isActive) {
                         // Found free slot
                         strcpy(vps[j].versionName, name.c_str());
                         vps[j].contentTableCluster = cont;
                         vps[j].isActive = 1;
                         disk.writeSector(c * 8 + i, vps);
                         cout << "Added level " << name << endl;
                         return;
                     }
                 }
            }
        }
        
        // No free slots - extend chain
        uint64_t lastCluster = chain.back();
        uint64_t newCluster = allocCluster();
        if (newCluster == 0) {
            cout << "Disk full. Cannot add level.\n";
            return;
        }
        
        setLATEntry(lastCluster, newCluster);
        
        // Initialize new cluster
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        memset(vps, 0, sizeof(vps));
        strcpy(vps[0].versionName, name.c_str());
        vps[0].contentTableCluster = cont;
        vps[0].isActive = 1;
        for (int i=0; i<8; i++) disk.writeSector(newCluster * 8 + i, vps);
        cout << "Added level " << name << " (extended chain).\n";
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
                        
                        entries[j].type = TYPE_FREE;
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
            strncpy(entryPtr->name, name.c_str(), 31);
            entryPtr->startCluster = allocCluster();
            entryPtr->createTime = time(0);
            if (entryPtr->startCluster == 0) { cout << "Disk full.\n"; return; }
        }
        
        entryPtr->modTime = time(0);
        entryPtr->size = data.size();
        
        uint64_t startCluster = entryPtr->startCluster;
        disk.writeSector(foundSector, sectorData);
        
        // Write Data (LAT Chaining)
        uint64_t current = startCluster;
        uint64_t offset = 0;
        uint64_t total = data.size();
        
        while (offset < total) {
             uint64_t chunk = std::min((uint64_t)CLUSTER_SIZE, total - offset);
             uint8_t buffer[CLUSTER_SIZE];
             memset(buffer, 0, CLUSTER_SIZE);
             memcpy(buffer, data.data() + offset, chunk);
             
             // Write 8 sectors
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
        
        // Commit transaction
        journal->commitOperation(txId);
        
        cout << "Written " << total << " bytes.\n";
    }

    void setVerbose(bool v) {
        disk.setVerbose(v);
        cout << "Disk logging " << (v ? "ENABLED" : "DISABLED") << ".\n";
    }
    
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
                fs.look(arg);
            }
            else if (cmd == "dir-tree") fs.dirTree();
            else if (cmd == "create") {
                string type, name;
                ss >> type >> name;
                fs.create(type, name);
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
            else if (cmd == "current") fs.current();
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
                cout << "  dir-tree      - Display directory tree\n";
                cout << "  current       - Show current path and level\n";
                cout << "  create folder <n> - Create folder\n";
                cout << "  create file <n>   - Create file\n";
                cout << "  write <name>  - Text editor for file\n";
                cout << "  read <name>   - Read file contents\n";
                cout << "  symlink <target> <link> - Create symbolic link\n";
                cout << "  hardlink <target> <link> - Create hard link\n";
                cout << "  nav <path>    - Navigate to folder\n";
                cout << "  del <name>    - Delete entry\n";
                cout << "  move <s> <d>  - Move/rename entry\n";
                cout << "  level add <f> <n>    - Add level to folder/.\n";
                cout << "  level remove <f> <n> - Remove level from folder/.\n";
                cout << "  link <dir1> <dir2> <level> - Create shared level (DAG)\n";
                cout << "  exit          - Exit\n";
            }
            else cout << "Unknown command. Type 'help' for list.\n";
        } catch (const exception& e) {
            cout << "Error: " << e.what() << "\n";
        } catch (...) {
            cout << "Unknown error occurred.\n";
        }
    }
    return 0;
}
