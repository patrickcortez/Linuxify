/*
 * Compile: g++ mount.cpp -o mount.exe
 */

#include "fs_common.hpp"

class FileSystemShell {
    DiskDevice disk;
    SuperBlock sb;
    
    struct {
        uint64_t currentDirCluster; 
        uint64_t currentContentCluster; 
        string currentPath;
        string currentVersion;
    } context;

public:
    FileSystemShell() {
        memset(&context, 0, sizeof(context));
        context.currentPath = "/";
    }

    bool mount(char driveLetter) {
        if (!disk.open(driveLetter)) return false;
        
        if (!disk.readSector(0, &sb)) {
            disk.close();
            return false;
        }
        if (sb.magic != MAGIC) {
            disk.close();
            return false;
        }

        context.currentDirCluster = sb.rootDirCluster;
        context.currentPath = "/";
        
        cout << "Mounted successfully. At Root.\n";
        
        if(loadVersion("master")) cout << "Context: master\n";
        else cout << "No master version.\n";
        return true;
    }

    uint64_t allocCluster() {
        disk.readSector(0, &sb);
        uint8_t bitmapBlock[SECTOR_SIZE];
        for (uint64_t i = 0; i < sb.freeMapSectors; i++) {
            disk.readSector(sb.freeMapCluster + i, bitmapBlock);
            for (int byte = 0; byte < SECTOR_SIZE; byte++) {
                if (bitmapBlock[byte] != 0xFF) { 
                    for (int bit = 0; bit < 8; bit++) {
                        if (!(bitmapBlock[byte] & (1 << bit))) {
                            bitmapBlock[byte] |= (1 << bit);
                            disk.writeSector(sb.freeMapCluster + i, bitmapBlock);
                            uint64_t clusterIdx = (i * SECTOR_SIZE * 8) + (byte * 8) + bit;
                            uint64_t absCluster = sb.rootDirCluster + clusterIdx;
                            char z[SECTOR_SIZE] = {0};
                            for(int c=0; c<8; c++) disk.writeSector(absCluster*8 + c, z);
                            return absCluster; 
                        }
                    }
                }
            }
        }
        return 0;
    }

    bool loadVersion(const string& ver) {
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(context.currentDirCluster * 8 + i, vps);
             int count = SECTOR_SIZE/sizeof(VersionEntry);
             for(int j=0; j<count; j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == ver) {
                     context.currentContentCluster = vps[j].contentTableCluster;
                     context.currentVersion = ver;
                     return true;
                 }
             }
        }
        return false;
    }

    bool isMounted() { return disk.isOpen(); }

    void look() {
        if (!disk.isOpen()) return;
        cout << "Content of " << context.currentPath << " (" << context.currentVersion << "):\n";
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        bool empty = true;
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type != TYPE_FREE) {
                    empty = false;
                    string typeStr = (entries[j].type == TYPE_LEVELED_DIR) ? "<L-DIR>" : "<FILE>";
                    cout << setw(8) << left << typeStr << " " << entries[j].name << endl;
                }
            }
        }
        if (empty) cout << "(empty)\n";
    }

    void create(string type, string name) {
        if (!disk.isOpen()) return;
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        int freeSector = -1;
        int freeIdx = -1;
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_FREE) {
                    freeSector = i;
                    freeIdx = j;
                    goto found_slot;
                }
            }
        }
        cout << "Directory full.\n";
        return;

found_slot:
        DirEntry* target = &entries[freeIdx];
        strcpy(target->name, name.c_str());
        if (type == "folder") {
            target->type = TYPE_LEVELED_DIR;
            target->startCluster = allocCluster();
        } else {
            target->type = TYPE_FILE;
            target->startCluster = allocCluster(); 
            target->size = 0;
        }
        disk.writeSector(context.currentContentCluster * 8 + freeSector, entries);
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
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == folderName) {
                    enterFolder(entries[j].startCluster, folderName, levelName);
                    return;
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
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(cluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (!vps[j].isActive) {
                     strcpy(vps[j].versionName, name.c_str());
                     vps[j].contentTableCluster = cont;
                     vps[j].isActive = 1;
                     disk.writeSector(cluster * 8 + i, vps);
                     cout << "Added level " << name << endl;
                     return;
                 }
             }
        }
    }
    
    void levelAdd(string name) { addLevel(context.currentDirCluster, name); }

    void read(string name) {
        if (!disk.isOpen()) return;
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_FILE && string(entries[j].name) == name) {
                    if (entries[j].size == 0) return;
                    uint64_t current = entries[j].startCluster;
                    uint64_t remaining = entries[j].size;
                    char buffer[SECTOR_SIZE * 8];
                    while (current != 0 && remaining > 0) {
                        disk.readSector(current * 8, buffer, 8);
                        uint64_t chunk = (remaining > SECTOR_SIZE * 8) ? SECTOR_SIZE * 8 : remaining;
                        cout.write(buffer, chunk);
                        remaining -= chunk;
                        break; 
                    }
                    cout << endl;
                    return;
                }
            }
        }
        cout << "File not found.\n";
    }

    void del(string target, bool recursive) {
        if (!disk.isOpen()) return;
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type != TYPE_FREE && string(entries[j].name) == target) {
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
                    entries[j].type = TYPE_FREE;
                    disk.writeSector(context.currentContentCluster * 8 + i, entries);
                    cout << "Deleted " << target << endl;
                    return;
                }
            }
        }
        cout << "Target not found.\n";
    }

    void move(string src, string dst) {
        if (!disk.isOpen()) return;
        DirEntry srcEntry;
        bool found = false;
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type != TYPE_FREE && string(entries[j].name) == src) {
                    srcEntry = entries[j];
                    found = true;
                    entries[j].type = TYPE_FREE;
                    disk.writeSector(context.currentContentCluster * 8 + i, entries);
                    goto found_src;
                }
            }
        }
found_src:
        if (!found) { cout << "Source not found.\n"; return; }
        for (int i=0; i<8; i++) {
            disk.readSector(context.currentContentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_FREE) {
                    entries[j] = srcEntry;
                    strcpy(entries[j].name, dst.c_str());
                    disk.writeSector(context.currentContentCluster * 8 + i, entries);
                    cout << "Moved " << src << " to " << dst << endl;
                    return;
                }
            }
        }
    }

    void levelRemove(string name) {
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(context.currentDirCluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == name) {
                     if (name == "master") return;
                     vps[j].isActive = 0;
                     disk.writeSector(context.currentDirCluster * 8 + i, vps);
                     if (context.currentVersion == name) context.currentVersion = "";
                     cout << "Removed level " << name << endl;
                     return;
                 }
             }
        }
    }

    void levelRename(string oldName, string newName) {
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(context.currentDirCluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == oldName) {
                     strcpy(vps[j].versionName, newName.c_str());
                     disk.writeSector(context.currentDirCluster * 8 + i, vps);
                     if (context.currentVersion == oldName) context.currentVersion = newName;
                     cout << "Renamed level " << oldName << "\n";
                     return;
                 }
             }
        }
    }
};

int main(int argc, char** argv) {
    FileSystemShell fs;
    string input;
    
    cout << "--- Leveled FS Shell ---\n";
    
    if (argc > 1) {
        fs.mount(argv[1][0]);
    } else {
        cout << "Usage: mount.exe <DriveLetter>\n";
        cout << "Or type 'mount <DriveLetter>' here.\n";
    }

    while (true) {
        cout << "user@fs> ";
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        stringstream ss(input);
        string cmd;
        ss >> cmd;

        if (cmd == "exit") break;
        if (cmd == "mount") {
            char drv; ss >> drv;
            fs.mount(drv);
        }
        else if (cmd == "look") fs.look();
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
            if (arg1 == "-r") {
                ss >> arg2;
                fs.del(arg2, true);
            } else fs.del(arg1, false);
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
                if (arg1 == ".") fs.levelAdd(arg2);
            }
            else if (sub == "remove") {
                ss >> arg1 >> arg2;
                if (arg1 == ".") fs.levelRemove(arg2);
            }
            else if (sub == "rename") {
                ss >> arg1 >> arg2 >> arg3;
                if (arg1 == ".") fs.levelRename(arg2, arg3);
            }
        }
        else cout << "Unknown command.\n";
    }
    return 0;
}
