/*
 * permissions.hpp - Complete Permission System for LevelFS
 * 
 * Compile: Include in mount.cpp with #include "permissions.hpp"
 */

#ifndef PERMISSIONS_HPP
#define PERMISSIONS_HPP

#include "fs_common.hpp"
#include <vector>
#include <string>
#include <ctime>
#include <algorithm>
#include <sstream>

using namespace std;

struct PermissionResult {
    uint32_t perms;
    bool found;
    string errorMessage;
    
    PermissionResult() : perms(0), found(false), errorMessage("") {}
    PermissionResult(uint32_t p, bool f, const string& e) : perms(p), found(f), errorMessage(e) {}
    
    bool hasRead() const { return found && (perms & PERM_READ); }
    bool hasWrite() const { return found && (perms & PERM_WRITE); }
    bool hasExec() const { return found && (perms & PERM_EXEC); }
};

class PermissionCache {
private:
    struct CacheEntry {
        string path;
        uint32_t perms;
        time_t cacheTime;
    };
    
    vector<CacheEntry> cache;
    static const size_t MAX_ENTRIES = 64;
    static const int TTL_SECONDS = 30;

public:
    PermissionCache() {}
    
    void clear() {
        cache.clear();
    }
    
    void add(const string& path, uint32_t perms) {
        removeExpired();
        
        for (auto& entry : cache) {
            if (entry.path == path) {
                entry.perms = perms;
                entry.cacheTime = time(nullptr);
                return;
            }
        }
        
        if (cache.size() >= MAX_ENTRIES) {
            cache.erase(cache.begin());
        }
        
        cache.push_back({path, perms, time(nullptr)});
    }
    
    PermissionResult get(const string& path) {
        removeExpired();
        
        for (const auto& entry : cache) {
            if (entry.path == path) {
                return PermissionResult(entry.perms, true, "");
            }
        }
        
        return PermissionResult(0, false, "Not in cache");
    }
    
    void invalidatePath(const string& path) {
        cache.erase(
            remove_if(cache.begin(), cache.end(),
                [&path](const CacheEntry& e) { return e.path == path; }),
            cache.end()
        );
    }
    
    void invalidateAll() {
        cache.clear();
    }

private:
    void removeExpired() {
        time_t now = time(nullptr);
        cache.erase(
            remove_if(cache.begin(), cache.end(),
                [now, this](const CacheEntry& e) { return (now - e.cacheTime) > TTL_SECONDS; }),
            cache.end()
        );
    }
};

class PermissionResolver {
private:
    DiskDevice& disk;
    PermissionCache& cache;
    uint64_t rootContentCluster;
    
    struct ClusterVisitTracker {
        vector<uint64_t> visited;
        
        bool hasVisited(uint64_t cluster) {
            return find(visited.begin(), visited.end(), cluster) != visited.end();
        }
        
        void markVisited(uint64_t cluster) {
            visited.push_back(cluster);
        }
    };

public:
    PermissionResolver(DiskDevice& d, PermissionCache& c, uint64_t rootCluster) 
        : disk(d), cache(c), rootContentCluster(rootCluster) {}
    
    PermissionResult readEntryPerms(uint64_t parentCluster, const string& entryName,
                                     uint64_t (*getChain)(uint64_t)) {
        if (!disk.isOpen()) {
            return PermissionResult(0, false, "Disk not open");
        }
        
        vector<uint64_t> chain;
        uint64_t current = parentCluster;
        int chainLimit = 1000;
        
        while (current != 0 && current != LAT_END && chainLimit-- > 0) {
            chain.push_back(current);
            
            LABEntry lab;
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            disk.readSector(labSector, labBuffer);
            lab = labBuffer[labOffset];
            
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        if (chain.empty()) {
            return PermissionResult(0, false, "Invalid cluster chain");
        }
        
        for (uint64_t c : chain) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(c * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type != TYPE_FREE) {
                        char nameBuf[25];
                        memcpy(nameBuf, entries[idx].name, 24);
                        nameBuf[24] = '\0';
                        
                        if (string(nameBuf) == entryName) {
                            return PermissionResult(entries[idx].attributes, true, "");
                        }
                    }
                }
            }
        }
        
        return PermissionResult(0, false, "Entry not found: " + entryName);
    }
    
    PermissionResult writeEntryPerms(uint64_t parentCluster, const string& entryName, 
                                      uint32_t newPerms) {
        if (!disk.isOpen()) {
            return PermissionResult(0, false, "Disk not open");
        }
        
        uint64_t current = parentCluster;
        int chainLimit = 1000;
        
        while (current != 0 && current != LAT_END && chainLimit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                uint64_t sectorNum = current * SECTORS_PER_CLUSTER + sector;
                
                if (!disk.readSector(sectorNum, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type != TYPE_FREE) {
                        char nameBuf[25];
                        memcpy(nameBuf, entries[idx].name, 24);
                        nameBuf[24] = '\0';
                        
                        if (string(nameBuf) == entryName) {
                            entries[idx].attributes = newPerms;
                            entries[idx].modTime = time(nullptr);
                            
                            if (!disk.writeSector(sectorNum, entries)) {
                                return PermissionResult(0, false, "Disk write error");
                            }
                            
                            cache.invalidateAll();
                            return PermissionResult(newPerms, true, "");
                        }
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            disk.readSector(labSector, labBuffer);
            LABEntry lab = labBuffer[labOffset];
            
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        return PermissionResult(0, false, "Entry not found: " + entryName);
    }
    
    PermissionResult searchFolderPermsInTree(uint64_t searchCluster, uint64_t targetCluster,
                                              int depth, ClusterVisitTracker& tracker) {
        if (depth > 50) {
            return PermissionResult(PERM_DEFAULT, false, "Max recursion depth exceeded");
        }
        
        if (tracker.hasVisited(searchCluster)) {
            return PermissionResult(PERM_DEFAULT, false, "Circular reference detected");
        }
        tracker.markVisited(searchCluster);
        
        uint64_t current = searchCluster;
        int chainLimit = 1000;
        
        while (current != 0 && current != LAT_END && chainLimit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type == TYPE_LEVELED_DIR || entries[idx].type == TYPE_LEVEL_MOUNT) {
                        uint64_t versionTableCluster = entries[idx].startCluster;
                        
                        uint64_t vCurrent = versionTableCluster;
                        int vChainLimit = 100;
                        
                        while (vCurrent != 0 && vCurrent != LAT_END && vChainLimit-- > 0) {
                            for (int vs = 0; vs < SECTORS_PER_CLUSTER; vs++) {
                                VersionEntry versionEntries[SECTOR_SIZE / sizeof(VersionEntry)];
                                
                                if (!disk.readSector(vCurrent * SECTORS_PER_CLUSTER + vs, versionEntries)) {
                                    continue;
                                }
                                
                                for (int vi = 0; vi < SECTOR_SIZE / sizeof(VersionEntry); vi++) {
                                    if (versionEntries[vi].isActive && versionEntries[vi].contentTableCluster != 0) {
                                        if (versionEntries[vi].contentTableCluster == targetCluster) {
                                            return PermissionResult(entries[idx].attributes, true, "");
                                        }
                                        
                                        PermissionResult childResult = searchFolderPermsInTree(
                                            versionEntries[vi].contentTableCluster,
                                            targetCluster,
                                            depth + 1,
                                            tracker
                                        );
                                        
                                        if (childResult.found) {
                                            return childResult;
                                        }
                                    }
                                }
                            }
                            
                            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
                            uint64_t labSector = vCurrent / LAB_ENTRIES_PER_CLUSTER;
                            uint64_t labOffset = vCurrent % LAB_ENTRIES_PER_CLUSTER;
                            disk.readSector(labSector, labBuffer);
                            LABEntry lab = labBuffer[labOffset];
                            
                            if (lab.nextCluster == LAT_END || lab.nextCluster == vCurrent) break;
                            vCurrent = lab.nextCluster;
                        }
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            disk.readSector(labSector, labBuffer);
            LABEntry lab = labBuffer[labOffset];
            
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        return PermissionResult(PERM_DEFAULT, false, "Folder not found in tree");
    }
    
    uint32_t getFolderPerms(uint64_t folderCluster, uint64_t currentContentCluster, uint32_t currentFolderPerms) {
        if (folderCluster == rootContentCluster) {
            return PERM_ROOT_DEFAULT;
        }
        
        if (folderCluster == currentContentCluster && currentFolderPerms != 0) {
            return currentFolderPerms;
        }
        
        ClusterVisitTracker tracker;
        PermissionResult result = searchFolderPermsInTree(rootContentCluster, folderCluster, 0, tracker);
        
        if (result.found) {
            return result.perms;
        }
        
        return PERM_DIR_DEFAULT;
    }
    
    PermissionResult resolvePathPermissions(const string& path, uint64_t rootCluster) {
        if (path.empty() || path == "/") {
            return PermissionResult(PERM_ROOT_DEFAULT, true, "");
        }
        
        PermissionResult cached = cache.get(path);
        if (cached.found) {
            return cached;
        }
        
        vector<string> parts;
        stringstream ss(path);
        string part;
        while (getline(ss, part, '/')) {
            if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
        }
        
        if (parts.empty()) {
            return PermissionResult(PERM_ROOT_DEFAULT, true, "");
        }
        
        uint64_t currentCluster = rootCluster;
        uint32_t cumulativePerms = PERM_ROOT_DEFAULT;
        
        for (size_t i = 0; i < parts.size(); i++) {
            const string& component = parts[i];
            
            PermissionResult entryResult = readEntryPerms(currentCluster, component, nullptr);
            if (!entryResult.found) {
                return PermissionResult(0, false, "Path component not found: " + component);
            }
            
            cumulativePerms &= entryResult.perms;
            
            if (i < parts.size() - 1) {
                if (!(entryResult.perms & PERM_EXEC)) {
                    return PermissionResult(0, false, "No execute permission to traverse: " + component);
                }
                if (!(entryResult.perms & PERM_READ)) {
                    return PermissionResult(0, false, "No read permission to access: " + component);
                }
            }
        }
        
        cache.add(path, cumulativePerms);
        return PermissionResult(cumulativePerms, true, "");
    }
};

class PermissionChecker {
public:
    static bool checkRead(uint32_t perms) {
        return (perms & PERM_READ) != 0;
    }
    
    static bool checkWrite(uint32_t perms) {
        return (perms & PERM_WRITE) != 0;
    }
    
    static bool checkExec(uint32_t perms) {
        return (perms & PERM_EXEC) != 0;
    }
    
    static bool checkReadWrite(uint32_t perms) {
        return checkRead(perms) && checkWrite(perms);
    }
    
    static bool checkAll(uint32_t perms) {
        return checkRead(perms) && checkWrite(perms) && checkExec(perms);
    }
    
    static bool isHidden(uint32_t attrs) {
        return (attrs & PERM_HIDDEN) != 0;
    }
    
    static bool isSystem(uint32_t attrs) {
        return (attrs & PERM_SYSTEM) != 0;
    }
    
    static bool isReadOnly(uint32_t attrs) {
        return (attrs & PERM_READONLY) != 0;
    }
    
    static bool isImmutable(uint32_t attrs) {
        return (attrs & FILE_FLAG_IMMUTABLE) != 0;
    }
    
    static bool isEncrypted(uint32_t attrs) {
        return (attrs & FILE_FLAG_ENCRYPTED) != 0;
    }
    
    static bool isCompressed(uint32_t attrs) {
        return (attrs & FILE_FLAG_COMPRESSED) != 0;
    }
    
    static string getPermsString(uint32_t attrs) {
        string s;
        s += (attrs & PERM_READ) ? 'r' : '-';
        s += (attrs & PERM_WRITE) ? 'w' : '-';
        s += (attrs & PERM_EXEC) ? 'x' : '-';
        return s;
    }
    
    static string getFullAttrString(uint32_t attrs) {
        string s = getPermsString(attrs);
        if (attrs & PERM_HIDDEN) s += " hidden";
        if (attrs & PERM_SYSTEM) s += " system";
        if (attrs & PERM_READONLY) s += " readonly";
        if (attrs & FILE_FLAG_IMMUTABLE) s += " immutable";
        if (attrs & FILE_FLAG_ENCRYPTED) s += " encrypted";
        if (attrs & FILE_FLAG_COMPRESSED) s += " compressed";
        return s;
    }
    
    static uint32_t parsePermString(const string& option, uint32_t currentPerms) {
        if (option == "+r") return currentPerms | PERM_READ;
        if (option == "-r") return currentPerms & ~PERM_READ;
        if (option == "+w") return currentPerms | PERM_WRITE;
        if (option == "-w") return currentPerms & ~PERM_WRITE;
        if (option == "+x") return currentPerms | PERM_EXEC;
        if (option == "-x") return currentPerms & ~PERM_EXEC;
        if (option == "+h") return currentPerms | PERM_HIDDEN;
        if (option == "-h") return currentPerms & ~PERM_HIDDEN;
        if (option == "+s") return currentPerms | PERM_SYSTEM;
        if (option == "-s") return currentPerms & ~PERM_SYSTEM;
        return currentPerms;
    }
    
    static bool isValidOption(const string& option) {
        return option == "+r" || option == "-r" ||
               option == "+w" || option == "-w" ||
               option == "+x" || option == "-x" ||
               option == "+h" || option == "-h" ||
               option == "+s" || option == "-s";
    }
};

#endif
