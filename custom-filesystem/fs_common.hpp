/*
 * Leveled File System (LFS) v2 Common Header
 * Architecture: True Level-First Design with 2-Level Radix Tree HLAT
 * 
 * Disk Layout:
 *   Sector 0: SuperBlock
 *   Cluster 1+: Level Index Table (LIT) - Sparse pointers to LABs
 *   Cluster N+: Level Allocation Blocks (LABs) - Actual allocation metadata
 *   Cluster M+: Level Registry - Global level catalog
 *   Cluster J+: Journal
 *   Cluster R+: Root Directory
 *   Cluster B: Backup SuperBlock
 */

#ifndef FS_COMMON_HPP
#define FS_COMMON_HPP

#include <windows.h>
#include <winioctl.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <cstdint>

#define SECTOR_SIZE 512
#define CLUSTER_SIZE 4096 
#define SECTORS_PER_CLUSTER 8
#define LFS_MAGIC 0x4C465332
#define LFS_VERSION 2
#define MAGIC 0x4C465332

#define LAT_FREE 0x0000000000000000
#define LAT_END  0xFFFFFFFFFFFFFFFF
#define LAT_BAD  0xFFFFFFFFFFFFFFFE
#define LIT_EMPTY 0x0000000000000000

#define LAB_ENTRIES_PER_CLUSTER 256
#define LIT_ENTRIES_PER_CLUSTER 512
#define CLUSTERS_PER_LIT_ENTRY 256

#define OP_CREATE     1
#define OP_WRITE      2
#define OP_DELETE     3
#define OP_UPDATE_DIR 4
#define OP_MKDIR      5
#define OP_LEVEL_CREATE 6
#define OP_LEVEL_LINK   7
#define OP_LAB_ALLOC    8

#define J_PENDING   0
#define J_COMMITTED 1
#define J_ABORTED   2

#define LEVEL_FLAG_ACTIVE   0x0001
#define LEVEL_FLAG_LOCKED   0x0002
#define LEVEL_FLAG_SNAPSHOT 0x0004
#define LEVEL_FLAG_SHARED   0x0008
#define LEVEL_FLAG_DERIVED  0x0010

#define LAT_FLAG_USED       0x0001
#define LAT_FLAG_RESERVED   0x0002
#define LAT_FLAG_CHAIN_END  0x0004

#define LEVEL_ID_NONE   0
#define LEVEL_ID_MASTER 1

using namespace std;

#pragma pack(push, 1)

struct SuperBlock {
    uint32_t magic;
    uint32_t version;
    uint64_t totalSectors;
    uint32_t clusterSize;
    uint64_t totalClusters;
    
    uint64_t litStartCluster;
    uint64_t litClusters;
    
    uint64_t labPoolStart;
    uint64_t labPoolClusters;
    uint64_t nextFreeLAB;
    
    uint64_t levelRegistryCluster;
    uint64_t levelRegistryClusters;
    
    uint64_t journalStartCluster;
    uint64_t journalSectors;
    uint64_t lastTxId;
    
    uint64_t nextLevelID;
    uint64_t totalLevels;
    uint64_t rootLevelID;
    
    uint64_t rootDirCluster;
    
    uint64_t backupSBCluster;
    
    uint64_t freeClusterHint;
    uint64_t totalFreeClusters;
    
    uint64_t latStartCluster;
    uint64_t latSectors;
    
    char volumeName[32];
    char padding[312];
};

struct LITEntry {
    uint64_t labCluster;
    uint64_t baseCluster;
    uint32_t allocatedCount;
    uint32_t flags;
};

struct LABEntry {
    uint64_t nextCluster;
    uint32_t levelID;
    uint16_t flags;
    uint16_t refCount;
};

struct LevelDescriptor {
    char name[32];
    uint64_t levelID;
    uint64_t parentLevelID;
    uint64_t rootContentCluster;
    uint64_t createTime;
    uint64_t modTime;
    uint32_t flags;
    uint32_t refCount;
    uint64_t childCount;
    uint64_t totalSize;
    char padding[8];
};

enum EntryType {
    TYPE_FREE = 0,
    TYPE_FILE = 1,
    TYPE_LEVELED_DIR = 2,
    TYPE_SYMLINK = 3,
    TYPE_HARDLINK = 4,
    TYPE_LEVEL_MOUNT = 5
};

struct DirEntry {
    char name[32];
    uint8_t type;
    uint64_t startCluster;
    uint64_t size;
    uint32_t attributes;
    uint32_t createTime;
    uint32_t modTime;
    char padding[3];
};

struct LeveledDirEntry {
    char name[32];
    uint8_t type;
    uint64_t levelID;
    uint64_t size;
    uint32_t permissions;
    uint32_t createTime;
    uint32_t modTime;
    uint8_t flags;
    char padding[2];
};

struct JournalEntry {
    uint64_t txId;
    uint32_t opType;
    uint32_t status;
    uint64_t targetCluster;
    uint64_t levelID;
    uint64_t timestamp;
    char metadata[24];
    uint64_t checksum;
};

struct VersionEntry {
    char versionName[32];
    uint64_t contentTableCluster;
    uint64_t levelID;
    uint64_t parentLevelID;
    uint32_t flags;
    uint8_t isActive;
    char padding[7];
};

#pragma pack(pop)

class DiskDevice {
private:
    HANDLE hDevice;
    fstream imageFile;
    string currentPath;
    uint64_t baseOffset;
    bool verbose;
    bool isImageFile;

public:
    DiskDevice() : hDevice(INVALID_HANDLE_VALUE), baseOffset(0), verbose(false), isImageFile(false) {}

    void setVerbose(bool v) { verbose = v; }

    ~DiskDevice() {
        close();
    }

    bool open(char driveLetter) {
        close();
        string path = "\\\\.\\";
        path += driveLetter;
        path += ":";
        return open(path, 0);
    }

    bool open(string path, uint64_t offsetBytes = 0) {
        close();
        isImageFile = false;
        
        hDevice = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) return false;

        currentPath = path;
        baseOffset = offsetBytes;
        
        DWORD bytesReturned;
        DeviceIoControl(hDevice, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        return true;
    }

    bool openFile(const string& filePath) {
        close();
        isImageFile = true;
        
        imageFile.open(filePath, ios::in | ios::out | ios::binary);
        if (!imageFile.is_open()) {
            isImageFile = false;
            return false;
        }
        currentPath = filePath;
        baseOffset = 0;
        return true;
    }

    void close() {
        if (isImageFile) {
            if (imageFile.is_open()) {
                imageFile.flush();
                imageFile.close();
            }
        } else {
            if (hDevice != INVALID_HANDLE_VALUE) {
                DeviceIoControl(hDevice, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, NULL, NULL);
                CloseHandle(hDevice);
                hDevice = INVALID_HANDLE_VALUE;
            }
        }
        baseOffset = 0;
        isImageFile = false;
    }

    uint64_t getDiskSize() {
        if (isImageFile) {
            return getFileSizeFromHandle();
        }
        if (hDevice == INVALID_HANDLE_VALUE) return 0;
        GET_LENGTH_INFORMATION info = {0};
        DWORD bytesReturned;
        if (DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &info, sizeof(info), &bytesReturned, NULL)) {
            return (uint64_t)info.Length.QuadPart;
        }
        return 0;
    }

    bool readSector(uint64_t sectorIndex, void* buffer, uint32_t count = 1) {
        bool success = false;
        if (isImageFile) {
            if (!imageFile.is_open()) return false;
            imageFile.clear();
            size_t offset = baseOffset + (sectorIndex * SECTOR_SIZE);
            imageFile.seekg(offset, ios::beg);
            if (imageFile.fail() || imageFile.bad()) {
                imageFile.clear();
                return false;
            }
            memset(buffer, 0, count * SECTOR_SIZE);
            imageFile.read(reinterpret_cast<char*>(buffer), count * SECTOR_SIZE);
            success = !imageFile.fail() && (imageFile.gcount() == (streamsize)(count * SECTOR_SIZE));
            imageFile.clear();
        } else {
            if (hDevice == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER offset;
            offset.QuadPart = baseOffset + (sectorIndex * SECTOR_SIZE);
            if (!SetFilePointerEx(hDevice, offset, NULL, FILE_BEGIN)) return false;
            DWORD bytesRead;
            if (!ReadFile(hDevice, buffer, count * SECTOR_SIZE, &bytesRead, NULL)) return false;
            success = bytesRead == count * SECTOR_SIZE;
        }

        if (verbose) {
            if (success) {
                cout << "\n----------------------------------------------------------------\n";
                cout << "[DISK READ]  Sector: " << setw(8) << left << sectorIndex 
                     << " Offset: 0x" << hex << setw(8) << setfill('0') << (baseOffset + (sectorIndex * SECTOR_SIZE)) << dec << setfill(' ')
                     << " Size: " << (count * SECTOR_SIZE) << "\n";
                cout << "----------------------------------------------------------------\n";
                
                unsigned char* p = (unsigned char*)buffer;
                cout << "DATA: ";
                for(int i=0; i<16 && i < (int)(count * SECTOR_SIZE); ++i) 
                    cout << hex << setw(2) << setfill('0') << (int)p[i] << " ";
                cout << dec << setfill(' ') << "\n";
                cout << "----------------------------------------------------------------\n";
            } else {
                 cout << "[DISK] READ FAILED | Sector: " << sectorIndex << endl;
            }
        }
        return success;
    }

    bool writeSector(uint64_t sectorIndex, const void* buffer, uint32_t count = 1) {
        if (verbose) {
            cout << "\n----------------------------------------------------------------\n";
            cout << "[DISK WRITE] Sector: " << setw(8) << left << sectorIndex 
                 << " Offset: 0x" << hex << setw(8) << setfill('0') << (baseOffset + (sectorIndex * SECTOR_SIZE)) << dec << setfill(' ')
                 << " Size: " << (count * SECTOR_SIZE) << "\n";
            cout << "----------------------------------------------------------------\n";
                 
            unsigned char* p = (unsigned char*)buffer;
            cout << "DATA: ";
            for(int i=0; i<16 && i < (int)(count * SECTOR_SIZE); ++i) 
                cout << hex << setw(2) << setfill('0') << (int)p[i] << " ";
            cout << dec << setfill(' ') << "\n";
            cout << "----------------------------------------------------------------\n";
        }

        if (isImageFile) {
            if (!imageFile.is_open()) return false;
            imageFile.clear();
            size_t offset = baseOffset + (sectorIndex * SECTOR_SIZE);
            imageFile.seekp(offset, ios::beg);
            if (imageFile.fail() || imageFile.bad()) {
                imageFile.clear();
                return false;
            }
            imageFile.write(reinterpret_cast<const char*>(buffer), count * SECTOR_SIZE);
            imageFile.flush();
            bool success = !imageFile.fail();
            imageFile.clear();
            return success;
        } else {
            if (hDevice == INVALID_HANDLE_VALUE) return false;
            LARGE_INTEGER offset;
            offset.QuadPart = baseOffset + (sectorIndex * SECTOR_SIZE);
            if (!SetFilePointerEx(hDevice, offset, NULL, FILE_BEGIN)) return false;
            DWORD bytesWritten;
            if (!WriteFile(hDevice, buffer, count * SECTOR_SIZE, &bytesWritten, NULL)) return false;
            if (!FlushFileBuffers(hDevice)) {
                 DWORD err = GetLastError();
                 cout << "[DISK] CRITICAL ERROR: FlushFileBuffers failed. Error Code: " << err << endl;
                 return false;
            }
            return bytesWritten == count * SECTOR_SIZE;
        }
    }
    
    uint64_t getFileSizeFromHandle() {
        if (isImageFile) {
            if (!imageFile.is_open()) return 0;
            auto pos = imageFile.tellg();
            imageFile.seekg(0, ios::end);
            auto size = imageFile.tellg();
            imageFile.seekg(pos);
            return size;
        }
        if (hDevice == INVALID_HANDLE_VALUE) return 0;
        LARGE_INTEGER size;
        if (GetFileSizeEx(hDevice, &size)) {
            return (uint64_t)size.QuadPart;
        }
        return 0;
    }
    
    bool isOpen() const { 
        if (isImageFile) return imageFile.is_open();
        return hDevice != INVALID_HANDLE_VALUE; 
    }
    string getPath() const { return currentPath; }
};

#endif
