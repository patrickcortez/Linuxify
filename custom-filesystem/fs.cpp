/*
 * Compile: g++ fs.cpp -o fs.exe -lole32 -lsetupapi
 */

#include "fs_common.hpp"
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <ntddstor.h>

class DiskPartitionManager {
public:
    struct UnallocatedChunk {
        uint64_t offset;
        uint64_t length;
    };

    static vector<UnallocatedChunk> getUnallocatedSpace(int diskIndex) {
        vector<UnallocatedChunk> chunks;
        string path = "\\\\.\\PhysicalDrive" + to_string(diskIndex);
        HANDLE hDevice = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) return chunks;

        GET_LENGTH_INFORMATION sizeInfo = {0};
        DWORD bytesRet;
        if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &sizeInfo, sizeof(sizeInfo), &bytesRet, NULL)) {
            CloseHandle(hDevice);
            return chunks;
        }
        uint64_t diskSize = sizeInfo.Length.QuadPart;

        const int layoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + sizeof(PARTITION_INFORMATION_EX) * 128;
        vector<char> layoutBuf(layoutSize);
        DRIVE_LAYOUT_INFORMATION_EX* layout = (DRIVE_LAYOUT_INFORMATION_EX*)layoutBuf.data();

        if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, layoutSize, &bytesRet, NULL)) {
            CloseHandle(hDevice);
            return chunks;
        }

        vector<pair<uint64_t, uint64_t>> used; 
        for (DWORD i = 0; i < layout->PartitionCount; i++) {
            PARTITION_INFORMATION_EX* p = &layout->PartitionEntry[i];
            if (p->PartitionLength.QuadPart > 0) {
                uint64_t start = p->StartingOffset.QuadPart;
                uint64_t end = start + p->PartitionLength.QuadPart;
                used.push_back({start, end});
            }
        }
        sort(used.begin(), used.end());

        uint64_t current = 1024*1024; 
        for (auto& interval : used) {
            if (interval.first > current) {
                if (interval.first - current > 1024*1024) { 
                    chunks.push_back({current, interval.first - current});
                }
            }
            if (interval.second > current) current = interval.second;
        }
        if (diskSize > current + 1024*1024) {
             chunks.push_back({current, diskSize - current - 1024*1024});
        }
        CloseHandle(hDevice);
        return chunks;
    }

    static string getDiskModel(int diskIndex) {
        string path = "\\\\.\\PhysicalDrive" + to_string(diskIndex);
        HANDLE hDevice = CreateFileA(path.c_str(), 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) return "Unknown";

        STORAGE_PROPERTY_QUERY query; // = {0} removed to avoid enum conversion error
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        
        char buffer[1024] = {0};
        DWORD bytes;
        string model = "Unknown";
        if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &bytes, NULL)) {
            STORAGE_DEVICE_DESCRIPTOR* desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;
            if (desc->ProductIdOffset) {
                model = string(buffer + desc->ProductIdOffset);
            }
        }
        // Trim spaces
        size_t first = model.find_first_not_of(" ");
        if (string::npos != first) {
            size_t last = model.find_last_not_of(" ");
            model = model.substr(first, (last - first + 1));
        }
        CloseHandle(hDevice);
        return model;
    }

    static void listDisks() {
        cout << "\n--- Physical Disks ---\n";
        for (int i=0; i<16; i++) {
             string path = "\\\\.\\PhysicalDrive" + to_string(i);
             // Must use GENERIC_READ for IOCTL_DISK_GET_LENGTH_INFO
             HANDLE hDevice = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
             if (hDevice == INVALID_HANDLE_VALUE) continue;
             
             GET_LENGTH_INFORMATION sizeInfo = {0};
             DWORD bytes;
             uint64_t size = 0;
             if (DeviceIoControl(hDevice, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &sizeInfo, sizeof(sizeInfo), &bytes, NULL)) {
                 size = sizeInfo.Length.QuadPart;
             }
             CloseHandle(hDevice);

             string model = getDiskModel(i);
             bool sys = isSystemDisk(i);
             
             double sizeGB = (double)size / (1024.0 * 1024.0 * 1024.0);
             cout << "Disk " << i << ": " << model << " (" << fixed << setprecision(2) << sizeGB << " GB)";
             if (sys) cout << " [SYSTEM]";
             cout << "\n";
        }
        cout << "\n";
    }

    static bool isSystemDisk(int diskIndex) {
        HANDLE hVol = CreateFileA("\\\\.\\C:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hVol == INVALID_HANDLE_VALUE) return false; 
        
        VOLUME_DISK_EXTENTS extents = {0};
        struct { DWORD Count; DISK_EXTENT Extents[4]; } buf = {0};
        DWORD bytes;
        if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &buf, sizeof(buf), &bytes, NULL)) {
            for (DWORD i = 0; i < buf.Count; i++) {
                if (buf.Extents[i].DiskNumber == diskIndex) {
                    CloseHandle(hVol);
                    return true;
                }
            }
        }
        CloseHandle(hVol);
        return false;
    }

    static vector<HANDLE> lockVolumesOnDisk(int diskIndex) {
        vector<HANDLE> lockedHandles;
        char driveLetter = 'A';
        DWORD bytesReturned;
        for (; driveLetter <= 'Z'; driveLetter++) {
            string path = "\\\\.\\";
            path += driveLetter;
            path += ":";
            HANDLE hVol = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hVol == INVALID_HANDLE_VALUE) continue;
            VOLUME_DISK_EXTENTS extents = {0};
            struct { DWORD Count; DISK_EXTENT Extents[4]; } buf = {0};
            if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &buf, sizeof(buf), &bytesReturned, NULL)) {
                bool onDisk = false;
                for (DWORD i = 0; i < buf.Count; i++) if (buf.Extents[i].DiskNumber == diskIndex) onDisk = true;
                if (onDisk) {
                    if (DeviceIoControl(hVol, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
                        DeviceIoControl(hVol, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
                        lockedHandles.push_back(hVol);
                    } else CloseHandle(hVol);
                } else CloseHandle(hVol);
            } else CloseHandle(hVol);
        }
        return lockedHandles;
    }

    static bool setDriveLetter(int diskIndex, uint64_t offset, char letter) {
        char volumeName[MAX_PATH];
        HANDLE hFind = FindFirstVolumeA(volumeName, MAX_PATH);
        if (hFind == INVALID_HANDLE_VALUE) return false;

        bool found = false;
        do {
            size_t len = strlen(volumeName);
            if (len > 0 && volumeName[len-1] == '\\') volumeName[len-1] = '\0'; 

            HANDLE hVol = CreateFileA(volumeName, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if(hVol != INVALID_HANDLE_VALUE) {
                struct { DWORD Count; DISK_EXTENT Extents[8]; } buf = {0};
                DWORD bytes;
                if(DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &buf, sizeof(buf), &bytes, NULL)) {
                    for(DWORD i=0; i<buf.Count; i++) {
                        if(buf.Extents[i].DiskNumber == diskIndex && buf.Extents[i].StartingOffset.QuadPart == offset) {
                            found = true;
                            strcat(volumeName, "\\");
                            break;
                        }
                    }
                }
                CloseHandle(hVol);
            }
            if(found) break;
            
        } while (FindNextVolumeA(hFind, volumeName, MAX_PATH));
        FindVolumeClose(hFind);

        if (!found) {
            Sleep(2000);
            return false; 
        }

        string mountPoint = string(1, letter) + ":\\";
        return SetVolumeMountPointA(mountPoint.c_str(), volumeName);
    }

    static bool createPartition(int diskIndex, uint64_t offset, uint64_t size, char driveLetter) {
        const uint64_t ONE_MB = 1024 * 1024;
        if (offset % ONE_MB != 0) offset = (offset + ONE_MB - 1) & ~(ONE_MB - 1);
        size = size & ~(ONE_MB - 1);
        if (size < ONE_MB) return false;

        string path = "\\\\.\\PhysicalDrive" + to_string(diskIndex);
        HANDLE hDevice = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) return false;

        const int maxPartitions = 128;
        const int layoutBufferSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + maxPartitions * sizeof(PARTITION_INFORMATION_EX);
        vector<char> oldLayoutBuf(layoutBufferSize);
        DRIVE_LAYOUT_INFORMATION_EX* oldLayout = (DRIVE_LAYOUT_INFORMATION_EX*)oldLayoutBuf.data();
        DWORD bytesRet;

        if (!DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, oldLayout, layoutBufferSize, &bytesRet, NULL)) {
            CloseHandle(hDevice);
            return false;
        }

        if (oldLayout->PartitionStyle == PARTITION_STYLE_MBR) {
             CloseHandle(hDevice);
             cout << "Error: MBR disks are not supported. Convert to GPT.\n";
             return false;
        }

        vector<char> newLayoutBuf(layoutBufferSize);
        memset(newLayoutBuf.data(), 0, layoutBufferSize);
        DRIVE_LAYOUT_INFORMATION_EX* newLayout = (DRIVE_LAYOUT_INFORMATION_EX*)newLayoutBuf.data();

        newLayout->PartitionStyle = PARTITION_STYLE_GPT;
        newLayout->Gpt = oldLayout->Gpt;

        int newIdx = 0;
        for (DWORD i = 0; i < oldLayout->PartitionCount; i++) {
            if (oldLayout->PartitionEntry[i].PartitionLength.QuadPart > 0) {
                 if (newIdx >= maxPartitions) break;
                 newLayout->PartitionEntry[newIdx] = oldLayout->PartitionEntry[i];
                 newLayout->PartitionEntry[newIdx].RewritePartition = FALSE; 
                 newIdx++;
            }
        }
        if (newIdx >= maxPartitions) { CloseHandle(hDevice); return false; }

        PARTITION_INFORMATION_EX* p = &newLayout->PartitionEntry[newIdx];
        p->StartingOffset.QuadPart = offset;
        p->PartitionLength.QuadPart = size;
        p->RewritePartition = TRUE;
        p->PartitionStyle = PARTITION_STYLE_GPT;
        memset(&p->Gpt.PartitionType, 0, sizeof(GUID));
        CLSIDFromString(L"{EBD0A0A2-B9E5-4433-87C0-68B6B72699C7}", &p->Gpt.PartitionType);
        CoCreateGuid(&p->Gpt.PartitionId);
        p->Gpt.Attributes = 0;
        wcscpy(p->Gpt.Name, L"Linuxify FS");

        newLayout->PartitionCount = newIdx + 1;

        vector<HANDLE> volumeLocks = lockVolumesOnDisk(diskIndex);

        bool result = DeviceIoControl(hDevice, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, newLayout, layoutBufferSize, NULL, 0, &bytesRet, NULL);
        if (result) DeviceIoControl(hDevice, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &bytesRet, NULL);

        for (HANDLE h : volumeLocks) {
             DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL);
             CloseHandle(h);
        }
        CloseHandle(hDevice);

        if (!result) return false;

        cout << "Partition created. Waiting for volume manager...\n";
        Sleep(3000); 

        if (setDriveLetter(diskIndex, offset, driveLetter)) {
            cout << "Assigned " << driveLetter << ": using SetVolumeMountPoint.\n";
            return true;
        } 
        
        cout << "Could not auto-assign drive letter via Mount Point APIs.\n";
        cout << "Please map the volume manually or try again.\n";
        return false;
    }

    static char createPartitionInteractive() {
        cout << "\n--- NATIVE PARTITION MANAGER ---\n";
        int bestDisk = -1;
        uint64_t maxFree = 0;
        
        for (int i=0; i<16; i++) {
             auto chunks = getUnallocatedSpace(i);
             if (chunks.empty()) continue; 

             bool system = isSystemDisk(i);
             cout << "Disk " << i << (system ? " [SYSTEM/BOOT]" : "") << ":\n";
             
             if (system) {
                 cout << "  (Protecting System Disk - Read Only logic recommended)\n";
             }

             for (size_t c = 0; c < chunks.size(); c++) {
                 cout << "  [" << c << "] Start: " << (chunks[c].offset/1024/1024) << " MB, Length: " << (chunks[c].length/1024/1024) << " MB\n";
                 if (!system && chunks[c].length > maxFree) { 
                     maxFree = chunks[c].length;
                     bestDisk = i;
                 }
             }
        }
        
        int diskIndex;
        cout << "\nEnter Disk Number: ";
        cin >> diskIndex;
        
        if (isSystemDisk(diskIndex)) {
            string override;
            cout << "WARNING: Disk " << diskIndex << " appears to be the SYSTEM DISK.\n";
            cout << "Modifying it could render your OS unbootable.\n";
            cout << "Type 'I UNDERSTAND' to proceed: ";
            cin >> override; 
            if (override != "I") return 0; 
             string rest; getline(cin, rest);  
        }

        auto chunks = getUnallocatedSpace(diskIndex);
        if (chunks.empty()) { cout << "No space or invalid disk.\n"; return 0; }
        
        int chunkIndex;
        cout << "Select Chunk Index: ";
        cin >> chunkIndex;
        if (chunkIndex < 0 || chunkIndex >= chunks.size()) return 0;
        
        uint64_t availMB = chunks[chunkIndex].length / 1024 / 1024;
        uint64_t sizeMB;
        cout << "Enter Size (MB) [Max " << availMB << "]: ";
        cin >> sizeMB;
        if (sizeMB > availMB) sizeMB = availMB;
        
        char driveLetter;
        cout << "Enter Drive Letter (e.g. Z): ";
        cin >> driveLetter;
        driveLetter = toupper(driveLetter);
        
        string confirm;
        cout << "Type 'YES' to write changes to Disk " << diskIndex << ": ";
        cin >> confirm;
        if (confirm != "YES") return 0;
        
        if (createPartition(diskIndex, chunks[chunkIndex].offset, sizeMB * 1024 * 1024, driveLetter)) {
            return driveLetter;
        }
        return 0;
    }
};

class Formatter {
    DiskDevice disk;
    SuperBlock sb;

public:
    bool format(char driveLetter) {
        if (!disk.open(driveLetter)) {
            cout << "Failed to open volume " << driveLetter << ":\n";
            return false;
        }
        cout << "Formatting " << driveLetter << ": ...\n";

        uint64_t diskSizeBytes = disk.getDiskSize();
        if (diskSizeBytes == 0) {
            cout << "Error: Could not determine volume size. Aborting.\n";
            return false;
        }
        
        sb.magic = MAGIC;
        sb.clusterSize = 8; 
        sb.totalSectors = diskSizeBytes / SECTOR_SIZE;
        
        uint64_t totalClusters = sb.totalSectors / sb.clusterSize;
        uint64_t bitmapBytes = (totalClusters + 7) / 8;
        sb.freeMapSectors = (bitmapBytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        
        sb.freeMapCluster = 1; 
        uint64_t bitmapClusters = (sb.freeMapSectors * SECTOR_SIZE + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
        
        sb.rootDirCluster = sb.freeMapCluster + bitmapClusters; 
        
        if (!disk.writeSector(0, &sb)) return false;

        vector<uint8_t> initialBitmap(sb.freeMapSectors * SECTOR_SIZE, 0);
        
        initialBitmap[0] |= 1; 
        
        for(uint64_t i=0; i<bitmapClusters; i++) {
            uint64_t c = sb.freeMapCluster + i;
            initialBitmap[c/8] |= (1 << (c%8));
        }
        
        uint64_t r = sb.rootDirCluster;
        initialBitmap[r/8] |= (1 << (r%8));
        
        for (uint64_t i=0; i<sb.freeMapSectors; i++) {
             disk.writeSector((sb.freeMapCluster * 8) + i, &initialBitmap[i * SECTOR_SIZE]); 
        }

        VersionEntry vTable[SECTOR_SIZE/sizeof(VersionEntry)];
        memset(vTable, 0, sizeof(vTable));
        strcpy(vTable[0].versionName, "master");
        vTable[0].isActive = 1;
        vTable[0].contentTableCluster = sb.rootDirCluster + 8; 
        
        uint64_t c = vTable[0].contentTableCluster;
        initialBitmap[c/8] |= (1 << (c%8));
        
        for (uint64_t i=0; i<sb.freeMapSectors; i++) {
             disk.writeSector((sb.freeMapCluster * 8) + i, &initialBitmap[i * SECTOR_SIZE]); 
        }

        disk.writeSector(sb.rootDirCluster * 8, vTable, 8); 
        
        DirEntry content[SECTOR_SIZE/sizeof(DirEntry)];
        memset(content, 0, sizeof(content));
        disk.writeSector((sb.rootDirCluster + 8) * 8, content, 8);
        return true;
    }
};

int main(int argc, char** argv) {
    Formatter fmt;
    string input;
    
    cout << "--- Leveled FS Formatter ---\n";
    cout << "Commands: format, help, exit\n\n";

    while (true) {
        cout << "fs> ";
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        stringstream ss(input);
        string cmd;
        ss >> cmd;

        if (cmd == "exit") break;
        if (cmd == "exit") break;
        if (cmd == "list") {
            DiskPartitionManager::listDisks();
        }
        else if (cmd == "format") {
            char drv = DiskPartitionManager::createPartitionInteractive();
            if (drv != 0) {
                if (fmt.format(drv)) {
                    cout << "Format Success. Use 'mount.exe " << drv << "' to access.\n";
                } else {
                    cout << "Format Failed.\n";
                }
            } else {
                cout << "Partition creation aborted or failed.\n";
            }
        }
        else if (cmd == "help") {
            cout << "list   - List all physical disks.\n";
            cout << "format - Create partition and format.\n";
            cout << "exit   - Quit.\n";
        }
        else cout << "Unknown command.\n";
    }
    return 0;
}
