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
#define MAGIC 0x4C564C46 

using namespace std;

#pragma pack(push, 1)

struct SuperBlock {
    uint32_t magic;         
    uint64_t totalSectors;
    uint32_t clusterSize;   
    uint64_t rootDirCluster;
    uint64_t freeMapCluster;
    uint64_t freeMapSectors;
    char padding[472];
};

enum EntryType {
    TYPE_FREE = 0,
    TYPE_FILE = 1,
    TYPE_LEVELED_DIR = 2
};

struct DirEntry {
    char name[32];
    uint8_t type;
    uint64_t startCluster; 
    uint64_t size;         
    uint32_t attributes;
    char padding[12];
};

struct VersionEntry {
    char versionName[32];
    uint64_t contentTableCluster; 
    uint8_t isActive;
    char padding[23];
};

#pragma pack(pop)

class DiskDevice {
private:
    HANDLE hDevice;
    string currentDrive;

public:
    DiskDevice() : hDevice(INVALID_HANDLE_VALUE) {}

    ~DiskDevice() {
        close();
    }

    bool open(char driveLetter) {
        close();
        string path = "\\\\.\\";
        path += driveLetter;
        path += ":";
        
        hDevice = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) return false;

        currentDrive = string(1, driveLetter);
        DWORD bytesReturned;
        DeviceIoControl(hDevice, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        return true; // Ignore lock failure if system drive etc, but good to have
    }

    void close() {
        if (hDevice != INVALID_HANDLE_VALUE) {
            DeviceIoControl(hDevice, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, NULL, NULL);
            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
        }
    }

    uint64_t getDiskSize() {
        if (hDevice == INVALID_HANDLE_VALUE) return 0;
        GET_LENGTH_INFORMATION info = {0};
        DWORD bytesReturned;
        if (DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &info, sizeof(info), &bytesReturned, NULL)) {
            return (uint64_t)info.Length.QuadPart;
        }
        return 0;
    }

    bool readSector(uint64_t sectorIndex, void* buffer, uint32_t count = 1) {
        if (hDevice == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER offset;
        offset.QuadPart = sectorIndex * SECTOR_SIZE;
        if (!SetFilePointerEx(hDevice, offset, NULL, FILE_BEGIN)) return false;
        DWORD bytesRead;
        if (!ReadFile(hDevice, buffer, count * SECTOR_SIZE, &bytesRead, NULL)) return false;
        return bytesRead == count * SECTOR_SIZE;
    }

    bool writeSector(uint64_t sectorIndex, const void* buffer, uint32_t count = 1) {
        if (hDevice == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER offset;
        offset.QuadPart = sectorIndex * SECTOR_SIZE;
        if (!SetFilePointerEx(hDevice, offset, NULL, FILE_BEGIN)) return false;
        DWORD bytesWritten;
        if (!WriteFile(hDevice, buffer, count * SECTOR_SIZE, &bytesWritten, NULL)) return false;
        return bytesWritten == count * SECTOR_SIZE;
    }
    
    bool isOpen() const { return hDevice != INVALID_HANDLE_VALUE; }
    string getDrive() const { return currentDrive; }
};

#endif
