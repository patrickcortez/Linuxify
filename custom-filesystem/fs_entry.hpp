/*
 * fs_entry.hpp - DirEntry and VersionEntry Operations for LevelFS
 * 
 * Compile: Include in mount.cpp with #include "fs_entry.hpp"
 */

#ifndef FS_ENTRY_HPP
#define FS_ENTRY_HPP

#include "fs_common.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <ctime>

using namespace std;

struct EntryLocation {
    uint64_t cluster;
    int sector;
    int index;
    bool found;
    
    EntryLocation() : cluster(0), sector(-1), index(-1), found(false) {}
    EntryLocation(uint64_t c, int s, int i) : cluster(c), sector(s), index(i), found(true) {}
};

struct FindResult {
    DirEntry entry;
    EntryLocation location;
    bool found;
    string errorMessage;
    
    FindResult() : found(false), errorMessage("Not found") {}
};

class EntryReader {
private:
    DiskDevice& disk;

public:
    EntryReader(DiskDevice& d) : disk(d) {}
    
    vector<DirEntry> readAllEntries(uint64_t contentCluster, 
                                     uint64_t (*getChainFunc)(uint64_t, void*), void* context) {
        vector<DirEntry> result;
        
        if (!disk.isOpen() || contentCluster == 0) {
            return result;
        }
        
        vector<uint64_t> chain;
        uint64_t current = contentCluster;
        int limit = 1000;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            chain.push_back(current);
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        for (uint64_t c : chain) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(c * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type != TYPE_FREE) {
                        result.push_back(entries[idx]);
                    }
                }
            }
        }
        
        return result;
    }
    
    vector<DirEntry> readEntriesFromCluster(uint64_t cluster) {
        vector<DirEntry> result;
        
        if (!disk.isOpen() || cluster == 0) {
            return result;
        }
        
        for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
            DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
            
            if (!disk.readSector(cluster * SECTORS_PER_CLUSTER + sector, entries)) {
                continue;
            }
            
            for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                if (entries[idx].type != TYPE_FREE) {
                    result.push_back(entries[idx]);
                }
            }
        }
        
        return result;
    }
    
    bool readEntry(uint64_t cluster, int sector, int index, DirEntry& outEntry) {
        if (!disk.isOpen()) return false;
        
        DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
        
        if (!disk.readSector(cluster * SECTORS_PER_CLUSTER + sector, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(DirEntry)) {
            return false;
        }
        
        outEntry = entries[index];
        return true;
    }
    
    vector<VersionEntry> readVersionEntries(uint64_t versionTableCluster) {
        vector<VersionEntry> result;
        
        if (!disk.isOpen() || versionTableCluster == 0) {
            return result;
        }
        
        uint64_t current = versionTableCluster;
        int limit = 100;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                VersionEntry entries[SECTOR_SIZE / sizeof(VersionEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(VersionEntry); idx++) {
                    if (entries[idx].isActive) {
                        result.push_back(entries[idx]);
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        return result;
    }
};

class EntryWriter {
private:
    DiskDevice& disk;

public:
    EntryWriter(DiskDevice& d) : disk(d) {}
    
    bool writeEntry(uint64_t cluster, int sector, int index, const DirEntry& entry) {
        if (!disk.isOpen()) return false;
        
        DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
        uint64_t sectorNum = cluster * SECTORS_PER_CLUSTER + sector;
        
        if (!disk.readSector(sectorNum, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(DirEntry)) {
            return false;
        }
        
        entries[index] = entry;
        
        return disk.writeSector(sectorNum, entries);
    }
    
    bool updateEntryTimestamp(uint64_t cluster, int sector, int index) {
        if (!disk.isOpen()) return false;
        
        DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
        uint64_t sectorNum = cluster * SECTORS_PER_CLUSTER + sector;
        
        if (!disk.readSector(sectorNum, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(DirEntry)) {
            return false;
        }
        
        entries[index].modTime = time(nullptr);
        
        return disk.writeSector(sectorNum, entries);
    }
    
    bool updateEntryAttributes(uint64_t cluster, int sector, int index, uint32_t newAttrs) {
        if (!disk.isOpen()) return false;
        
        DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
        uint64_t sectorNum = cluster * SECTORS_PER_CLUSTER + sector;
        
        if (!disk.readSector(sectorNum, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(DirEntry)) {
            return false;
        }
        
        entries[index].attributes = newAttrs;
        entries[index].modTime = time(nullptr);
        
        return disk.writeSector(sectorNum, entries);
    }
    
    bool updateEntrySize(uint64_t cluster, int sector, int index, uint64_t newSize) {
        if (!disk.isOpen()) return false;
        
        DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
        uint64_t sectorNum = cluster * SECTORS_PER_CLUSTER + sector;
        
        if (!disk.readSector(sectorNum, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(DirEntry)) {
            return false;
        }
        
        entries[index].size = newSize;
        entries[index].modTime = time(nullptr);
        
        return disk.writeSector(sectorNum, entries);
    }
    
    bool deleteEntry(uint64_t cluster, int sector, int index) {
        if (!disk.isOpen()) return false;
        
        DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
        uint64_t sectorNum = cluster * SECTORS_PER_CLUSTER + sector;
        
        if (!disk.readSector(sectorNum, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(DirEntry)) {
            return false;
        }
        
        memset(&entries[index], 0, sizeof(DirEntry));
        entries[index].type = TYPE_FREE;
        
        return disk.writeSector(sectorNum, entries);
    }
    
    bool writeVersionEntry(uint64_t cluster, int sector, int index, const VersionEntry& entry) {
        if (!disk.isOpen()) return false;
        
        VersionEntry entries[SECTOR_SIZE / sizeof(VersionEntry)];
        uint64_t sectorNum = cluster * SECTORS_PER_CLUSTER + sector;
        
        if (!disk.readSector(sectorNum, entries)) {
            return false;
        }
        
        if (index < 0 || index >= SECTOR_SIZE / sizeof(VersionEntry)) {
            return false;
        }
        
        entries[index] = entry;
        
        return disk.writeSector(sectorNum, entries);
    }
};

class EntryFinder {
private:
    DiskDevice& disk;

public:
    EntryFinder(DiskDevice& d) : disk(d) {}
    
    FindResult findByName(uint64_t contentCluster, const string& name) {
        FindResult result;
        
        if (!disk.isOpen() || contentCluster == 0) {
            result.errorMessage = "Invalid parameters";
            return result;
        }
        
        uint64_t current = contentCluster;
        int limit = 1000;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type != TYPE_FREE) {
                        char nameBuf[25];
                        memcpy(nameBuf, entries[idx].name, 24);
                        nameBuf[24] = '\0';
                        
                        if (string(nameBuf) == name) {
                            result.entry = entries[idx];
                            result.location = EntryLocation(current, sector, idx);
                            result.found = true;
                            result.errorMessage = "";
                            return result;
                        }
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        result.errorMessage = "Entry not found: " + name;
        return result;
    }
    
    FindResult findByCluster(uint64_t contentCluster, uint64_t targetStartCluster) {
        FindResult result;
        
        if (!disk.isOpen() || contentCluster == 0) {
            result.errorMessage = "Invalid parameters";
            return result;
        }
        
        uint64_t current = contentCluster;
        int limit = 1000;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type != TYPE_FREE && entries[idx].startCluster == targetStartCluster) {
                        result.entry = entries[idx];
                        result.location = EntryLocation(current, sector, idx);
                        result.found = true;
                        result.errorMessage = "";
                        return result;
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        result.errorMessage = "Entry with cluster not found";
        return result;
    }
    
    FindResult findByType(uint64_t contentCluster, uint8_t type) {
        FindResult result;
        
        if (!disk.isOpen() || contentCluster == 0) {
            result.errorMessage = "Invalid parameters";
            return result;
        }
        
        uint64_t current = contentCluster;
        int limit = 1000;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type == type) {
                        result.entry = entries[idx];
                        result.location = EntryLocation(current, sector, idx);
                        result.found = true;
                        result.errorMessage = "";
                        return result;
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        result.errorMessage = "Entry with type not found";
        return result;
    }
    
    vector<FindResult> findAllByType(uint64_t contentCluster, uint8_t type) {
        vector<FindResult> results;
        
        if (!disk.isOpen() || contentCluster == 0) {
            return results;
        }
        
        uint64_t current = contentCluster;
        int limit = 1000;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type == type) {
                        FindResult r;
                        r.entry = entries[idx];
                        r.location = EntryLocation(current, sector, idx);
                        r.found = true;
                        results.push_back(r);
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        return results;
    }
    
    EntryLocation findFreeSlot(uint64_t contentCluster) {
        if (!disk.isOpen() || contentCluster == 0) {
            return EntryLocation();
        }
        
        uint64_t current = contentCluster;
        int limit = 1000;
        
        while (current != 0 && current != LAT_END && limit-- > 0) {
            for (int sector = 0; sector < SECTORS_PER_CLUSTER; sector++) {
                DirEntry entries[SECTOR_SIZE / sizeof(DirEntry)];
                
                if (!disk.readSector(current * SECTORS_PER_CLUSTER + sector, entries)) {
                    continue;
                }
                
                for (int idx = 0; idx < SECTOR_SIZE / sizeof(DirEntry); idx++) {
                    if (entries[idx].type == TYPE_FREE) {
                        return EntryLocation(current, sector, idx);
                    }
                }
            }
            
            LABEntry labBuffer[LAB_ENTRIES_PER_CLUSTER];
            uint64_t labSector = current / LAB_ENTRIES_PER_CLUSTER;
            uint64_t labOffset = current % LAB_ENTRIES_PER_CLUSTER;
            
            if (!disk.readSector(labSector, labBuffer)) break;
            
            LABEntry lab = labBuffer[labOffset];
            if (lab.nextCluster == LAT_END || lab.nextCluster == current) break;
            current = lab.nextCluster;
        }
        
        return EntryLocation();
    }
};

#endif
