/*
 * Journal Manager for LevelFS
 * Compile: Included in mount.cpp
 */

#ifndef JOURNAL_HPP
#define JOURNAL_HPP

#include "fs_common.hpp"
#include <vector>

// CRC64 polynomial (ECMA-182)
#define CRC64_POLY 0xC96C5795D7870F42ULL

class Journal {
private:
    DiskDevice* disk;
    SuperBlock* sb;
    uint64_t currentTxId;
    uint64_t journalHead;  // Write position in circular buffer
    
    // CRC64 calculation
    uint64_t calculateCRC64(const void* data, size_t length) {
        uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
        const uint8_t* ptr = (const uint8_t*)data;
        
        for (size_t i = 0; i < length; i++) {
            crc ^= (uint64_t)ptr[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) crc = (crc >> 1) ^ CRC64_POLY;
                else crc >>= 1;
            }
        }
        return crc ^ 0xFFFFFFFFFFFFFFFFULL;
    }
    
public:
    Journal(DiskDevice* d, SuperBlock* s) : disk(d), sb(s), currentTxId(s->lastTxId), journalHead(0) {}
    
    // Log an operation before executing it
    uint64_t logOperation(uint32_t opType, uint64_t targetCluster, const string& metadata) {
        currentTxId++;
        
        JournalEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.txId = currentTxId;
        entry.opType = opType;
        entry.status = J_PENDING;
        entry.targetCluster = targetCluster;
        entry.timestamp = time(0);
        strncpy(entry.metadata, metadata.c_str(), 31);
        
        // Calculate checksum (exclude checksum field itself)
        entry.checksum = calculateCRC64(&entry, sizeof(JournalEntry) - sizeof(uint64_t));
        
        // Write to journal (circular buffer)
        uint64_t entriesPerSector = SECTOR_SIZE / sizeof(JournalEntry);
        uint64_t journalSectorOffset = journalHead / entriesPerSector;
        uint64_t entryInSector = journalHead % entriesPerSector;
        
        // Read-Modify-Write sector
        JournalEntry buffer[entriesPerSector];
        uint64_t sectorIdx = (sb->journalStartCluster * 8) + journalSectorOffset;
        disk->readSector(sectorIdx, buffer);
        buffer[entryInSector] = entry;
        disk->writeSector(sectorIdx, buffer);
        
        // Advance head (circular)
        journalHead = (journalHead + 1) % (sb->journalSectors * entriesPerSector);
        
        return currentTxId;
    }
    
    // Mark operation as committed
    void commitOperation(uint64_t txId) {
        // Find entry and update status
        uint64_t entriesPerSector = SECTOR_SIZE / sizeof(JournalEntry);
        
        for (uint64_t i = 0; i < sb->journalSectors; i++) {
            JournalEntry buffer[entriesPerSector];
            uint64_t sectorIdx = (sb->journalStartCluster * 8) + i;
            disk->readSector(sectorIdx, buffer);
            
            for (uint64_t j = 0; j < entriesPerSector; j++) {
                if (buffer[j].txId == txId) {
                    buffer[j].status = J_COMMITTED;
                    disk->writeSector(sectorIdx, buffer);
                    
                    // Update SuperBlock's lastTxId
                    sb->lastTxId = txId;
                    disk->writeSector(0, sb);
                    return;
                }
            }
        }
    }
    
    // Replay journal on mount (crash recovery)
    void replayJournal() {
        cout << "[Journal] Replaying journal for crash recovery...\n";
        
        uint64_t entriesPerSector = SECTOR_SIZE / sizeof(JournalEntry);
        int pendingCount = 0;
        
        for (uint64_t i = 0; i < sb->journalSectors; i++) {
            JournalEntry buffer[entriesPerSector];
            uint64_t sectorIdx = (sb->journalStartCluster * 8) + i;
            disk->readSector(sectorIdx, buffer);
            
            for (uint64_t j = 0; j < entriesPerSector; j++) {
                JournalEntry& entry = buffer[j];
                
                // Skip empty or invalid entries
                if (entry.txId == 0) continue;
                
                // Verify checksum
                uint64_t expectedCRC = entry.checksum;
                uint64_t actualCRC = calculateCRC64(&entry, sizeof(JournalEntry) - sizeof(uint64_t));
                if (expectedCRC != actualCRC) {
                    cout << "[Journal] Corrupted entry detected (txId=" << entry.txId << "), skipping.\n";
                    continue;
                }
                
                // Replay pending operations
                if (entry.status == J_PENDING) {
                    cout << "[Journal] Replaying txId=" << entry.txId << " (op=" << entry.opType << ")\n";
                    
                    bool success = false;
                    switch (entry.opType) {
                        case OP_CREATE:
                            success = replayCreate(entry);
                            break;
                        case OP_WRITE:
                            success = replayWrite(entry);
                            break;
                        case OP_DELETE:
                            success = replayDelete(entry);
                            break;
                        case OP_UPDATE_DIR:
                            success = replayUpdateDir(entry);
                            break;
                        case OP_MKDIR:
                            success = replayMkdir(entry);
                            break;
                        default:
                            cout << "[Journal] Unknown operation type: " << entry.opType << "\n";
                            break;
                    }
                    
                    if (success) {
                        entry.status = J_COMMITTED;
                    } else {
                        entry.status = J_ABORTED;
                        cout << "[Journal] Operation aborted (txId=" << entry.txId << ")\n";
                    }
                    pendingCount++;
                }
            }
            
            // Write back updated statuses
            disk->writeSector(sectorIdx, buffer);
        }
        
        cout << "[Journal] Replay complete. Recovered " << pendingCount << " pending operations.\n";
    }
    
private:
    bool replayCreate(JournalEntry& entry) {
        // Parse metadata for filename
        string filename(entry.metadata);
        if (filename.empty()) return false;
        
        cout << "[Journal] Replaying CREATE: " << filename << " in cluster " << entry.targetCluster << "\n";
        
        // Check if file already exists in target cluster
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk->readSector(entry.targetCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_FILE && string(entries[j].name) == filename) {
                    cout << "[Journal] File already exists, skipping.\n";
                    return true;  // Already exists, idempotent
                }
            }
        }
        
        // File doesn't exist, operation was interrupted - mark as aborted
        cout << "[Journal] CREATE was interrupted, aborting.\n";
        return false;
    }
    
    bool replayWrite(JournalEntry& entry) {
        // Write operations use write-ahead logging
        // If we're replaying a WRITE, the data is already written or lost
        cout << "[Journal] Replaying WRITE: cluster " << entry.targetCluster << "\n";
        cout << "[Journal] WRITE operation committed (write-ahead logging).\n";
        return true;  // Safe to mark as committed
    }
    
    bool replayDelete(JournalEntry& entry) {
        string filename(entry.metadata);
        if (filename.empty()) return false;
        
        cout << "[Journal] Replaying DELETE: " << filename << " from cluster " << entry.targetCluster << "\n";
        
        // Search for entry and mark as free if found
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk->readSector(entry.targetCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type != TYPE_FREE && string(entries[j].name) == filename) {
                    entries[j].type = TYPE_FREE;
                    disk->writeSector(entry.targetCluster * 8 + i, entries);
                    cout << "[Journal] DELETE completed.\n";
                    return true;
                }
            }
        }
        
        cout << "[Journal] File not found, already deleted.\n";
        return true;  // Idempotent - if not found, already deleted
    }
    
    bool replayUpdateDir(JournalEntry& entry) {
        cout << "[Journal] Replaying UPDATE_DIR: cluster " << entry.targetCluster << "\n";
        
        // Verify directory cluster integrity
        uint8_t buffer[SECTOR_SIZE];
        for (int i=0; i<8; i++) {
            if (!disk->readSector(entry.targetCluster * 8 + i, buffer)) {
                cout << "[Journal] Directory cluster corrupted, aborting.\n";
                return false;
            }
        }
        
        cout << "[Journal] UPDATE_DIR verified.\n";
        return true;
    }
    
    bool replayMkdir(JournalEntry& entry) {
        string foldername(entry.metadata);
        if (foldername.empty()) return false;
        
        cout << "[Journal] Replaying MKDIR: " << foldername << " in cluster " << entry.targetCluster << "\n";
        
        // Check if folder already exists
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk->readSector(entry.targetCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type == TYPE_LEVELED_DIR && string(entries[j].name) == foldername) {
                    cout << "[Journal] Folder already exists, skipping.\n";
                    return true;  // Already exists, idempotent
                }
            }
        }
        
        cout << "[Journal] MKDIR was interrupted, aborting.\n";
        return false;
    }

public:
    // Clear old committed entries (garbage collection)
    void clearOldEntries(uint64_t olderThan) {
        uint64_t entriesPerSector = SECTOR_SIZE / sizeof(JournalEntry);
        
        for (uint64_t i = 0; i < sb->journalSectors; i++) {
            JournalEntry buffer[entriesPerSector];
            uint64_t sectorIdx = (sb->journalStartCluster * 8) + i;
            disk->readSector(sectorIdx, buffer);
            
            bool modified = false;
            for (uint64_t j = 0; j < entriesPerSector; j++) {
                if (buffer[j].status == J_COMMITTED && buffer[j].txId < olderThan) {
                    memset(&buffer[j], 0, sizeof(JournalEntry));
                    modified = true;
                }
            }
            
            if (modified) disk->writeSector(sectorIdx, buffer);
        }
    }
};

#endif
