/*
 * Compile: g++ fs.cpp -o fs.exe -lole32 -lsetupapi
 */

#include "fs_common.hpp"
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <ntddstor.h>

struct PartitionResult {
    int diskIndex;
    uint64_t offset;
    uint64_t size;
    bool success;
};

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

        STORAGE_PROPERTY_QUERY query;
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

    static bool clearDisk(int diskIndex) {
        if (isSystemDisk(diskIndex)) return false;

        string path = "\\\\.\\PhysicalDrive" + to_string(diskIndex);
        HANDLE hDevice = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) return false;

        DWORD bytes;
        const int layoutSize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + sizeof(PARTITION_INFORMATION_EX) * 4;
        vector<char> layoutBuf(layoutSize);
        DRIVE_LAYOUT_INFORMATION_EX* layout = (DRIVE_LAYOUT_INFORMATION_EX*)layoutBuf.data();
        layout->PartitionStyle = PARTITION_STYLE_GPT;
        layout->PartitionCount = 0;
        layout->Gpt.DiskId = {0};
        CoCreateGuid(&layout->Gpt.DiskId);
        layout->Gpt.StartingUsableOffset.QuadPart = 1024*1024;
        
        vector<HANDLE> locks = lockVolumesOnDisk(diskIndex);
        bool result = DeviceIoControl(hDevice, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, layout, layoutSize, NULL, 0, &bytes, NULL);
        if (result) DeviceIoControl(hDevice, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &bytes, NULL);
        
        for(HANDLE h : locks) {
             DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytes, NULL);
             CloseHandle(h);
        }
        CloseHandle(hDevice);
        return result;
    }

    static bool setDriveLetter(int diskIndex, uint64_t offset, char letter) {
        char volumeName[MAX_PATH];
        bool found = false;
        
        for (int retry = 0; retry < 15; retry++) {
            HANDLE hFind = FindFirstVolumeA(volumeName, MAX_PATH);
            if (hFind == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }

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
            
            if (found) break;
            cout << "Waiting for volume to appear... (" << (retry+1) << "/15)\n";
            Sleep(1000);
        }

        if (!found) return false; 

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
        
        cout << "Windows did not assign a drive letter (Normal for RAW filesystems).\n";
        cout << "Don't worry! Use 'mount auto' to access the partition directly.\n";
        return true;
    }

    static PartitionResult createPartitionInteractive() {
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
            if (override != "I") return {0, 0, 0, false}; 
             string rest; getline(cin, rest);  
        }

        auto chunks = getUnallocatedSpace(diskIndex);
        if (chunks.empty()) { 
             cout << "No unallocated space found on Disk " << diskIndex << ".\n";
             if (isSystemDisk(diskIndex)) {
                 cout << "This is a System Disk. Cannot wipe safely.\n";
                 return {0, 0, 0, false};
             }
             
             cout << "Would you like to WIPE the disk and clear all partitions? (Required for new FS)\n";
             cout << "Type 'WIPE' to confirm (ALL DATA WILL BE LOST): ";
             string wipeConf;
             cin >> wipeConf;
             if (wipeConf == "WIPE") {
                 cout << "Wiping Disk " << diskIndex << "...\n";
                 if (clearDisk(diskIndex)) {
                     cout << "Disk Wiped. Re-scanning...\n";
                     Sleep(2000);
                     chunks = getUnallocatedSpace(diskIndex);
                     if (chunks.empty()) { cout << "Still no space? Try removing getting a new disk.\n"; return {0, 0, 0, false}; }
                 } else {
                     cout << "Failed to wipe disk.\n";
                     return {0, 0, 0, false};
                 }
             } else {
                 return {0, 0, 0, false};
             }
        }
        
        int chunkIndex;
        cout << "Select Chunk Index: ";
        cin >> chunkIndex;
        if (chunkIndex < 0 || chunkIndex >= (int)chunks.size()) return {0, 0, 0, false};
        
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
        transform(confirm.begin(), confirm.end(), confirm.begin(), ::toupper);
        if (confirm != "YES") return {0, 0, 0, false};
        
        uint64_t partOffset = chunks[chunkIndex].offset;
        uint64_t partSize = sizeMB * 1024 * 1024;
        if (createPartition(diskIndex, partOffset, partSize, driveLetter)) {
            PartitionResult result = {diskIndex, partOffset, partSize, true};
            return result;
        }
        return {0, 0, 0, false};
    }
};

class Formatter {
    DiskDevice disk;
    SuperBlock sb;

public:
    bool formatDirect(int diskIndex, uint64_t partitionOffset, uint64_t partitionSize) {
        string path = "\\\\.\\PhysicalDrive" + to_string(diskIndex);
        if (!disk.open(path, partitionOffset)) {
            cout << "Failed to open PhysicalDrive" << diskIndex << " at offset " << partitionOffset << "\n";
            return false;
        }
        cout << "Formatting partition on Disk " << diskIndex << " (offset " << partitionOffset << ", size " << (partitionSize/1024/1024) << " MB)...\n";

        return performFormat(partitionSize);
    }

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
        return performFormat(diskSizeBytes);
    }

    bool performFormat(uint64_t diskSizeBytes) {
        memset(&sb, 0, sizeof(sb));
        sb.magic = MAGIC;
        sb.version = LFS_VERSION;
        sb.clusterSize = SECTORS_PER_CLUSTER;
        sb.totalSectors = diskSizeBytes / SECTOR_SIZE;
        sb.totalClusters = sb.totalSectors / sb.clusterSize;
        
        sb.backupSBCluster = sb.totalClusters - 1;
        
        uint64_t litEntriesNeeded = (sb.totalClusters + CLUSTERS_PER_LIT_ENTRY - 1) / CLUSTERS_PER_LIT_ENTRY;
        uint64_t litBytesNeeded = litEntriesNeeded * sizeof(LITEntry);
        sb.litClusters = (litBytesNeeded + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
        sb.litStartCluster = 1;
        
        uint64_t maxLABsNeeded = litEntriesNeeded;
        sb.labPoolStart = sb.litStartCluster + sb.litClusters;
        sb.labPoolClusters = maxLABsNeeded;
        sb.nextFreeLAB = 0;
        
        sb.levelRegistryCluster = sb.labPoolStart + sb.labPoolClusters;
        sb.levelRegistryClusters = 2;
        
        uint64_t journalEntries = 1024;
        uint64_t journalBytes = journalEntries * sizeof(JournalEntry);
        sb.journalSectors = (journalBytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        sb.journalStartCluster = sb.levelRegistryCluster + sb.levelRegistryClusters;
        uint64_t journalClusters = (sb.journalSectors + SECTORS_PER_CLUSTER - 1) / SECTORS_PER_CLUSTER;
        
        sb.rootDirCluster = sb.journalStartCluster + journalClusters;
        sb.lastTxId = 0;
        
        sb.nextLevelID = 2;
        sb.totalLevels = 1;
        sb.rootLevelID = LEVEL_ID_MASTER;
        
        uint64_t reservedClusters = sb.rootDirCluster + 2;
        sb.freeClusterHint = reservedClusters;
        sb.totalFreeClusters = sb.totalClusters - reservedClusters - 1;
        
        sb.latStartCluster = sb.litStartCluster;
        sb.latSectors = sb.litClusters * SECTORS_PER_CLUSTER;
        
        cout << "=== LFS v2 Hierarchical Format ===\n";
        cout << "  Total Clusters: " << sb.totalClusters << "\n";
        cout << "  LIT: Cluster " << sb.litStartCluster << " (" << sb.litClusters << " clusters, " << litEntriesNeeded << " entries)\n";
        cout << "  LAB Pool: Cluster " << sb.labPoolStart << " (" << sb.labPoolClusters << " blocks)\n";
        cout << "  Level Registry: Cluster " << sb.levelRegistryCluster << "\n";
        cout << "  Journal: Cluster " << sb.journalStartCluster << "\n";
        cout << "  Root Dir: Cluster " << sb.rootDirCluster << "\n\n";
        
        if (!disk.writeSector(0, &sb)) { cout << "Failed to write SuperBlock.\n"; return false; }
        if (!disk.writeSector(sb.backupSBCluster * SECTORS_PER_CLUSTER, &sb)) { 
            cout << "Warning: Failed to write backup SuperBlock.\n"; 
        }
        
        cout << "Initializing Level Index Table (LIT)...\n";
        LITEntry emptyLIT[CLUSTER_SIZE / sizeof(LITEntry)];
        memset(emptyLIT, 0, sizeof(emptyLIT));
        
        for (uint64_t c = 0; c < sb.litClusters; c++) {
            for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                disk.writeSector((sb.litStartCluster + c) * SECTORS_PER_CLUSTER + s, 
                    ((char*)emptyLIT) + s * SECTOR_SIZE);
            }
        }
        cout << "  LIT initialized (" << sb.litClusters << " clusters, sparse allocation ready)\n";
        
        cout << "Initializing LAB Pool...\n";
        LABEntry emptyLAB[LAB_ENTRIES_PER_CLUSTER];
        memset(emptyLAB, 0, sizeof(emptyLAB));
        for (int i = 0; i < LAB_ENTRIES_PER_CLUSTER; i++) {
            emptyLAB[i].nextCluster = LAT_FREE;
            emptyLAB[i].levelID = LEVEL_ID_NONE;
            emptyLAB[i].flags = 0;
            emptyLAB[i].refCount = 0;
        }
        
        for (uint64_t c = 0; c < min(sb.labPoolClusters, (uint64_t)16); c++) {
            for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
                disk.writeSector((sb.labPoolStart + c) * SECTORS_PER_CLUSTER + s,
                    ((char*)emptyLAB) + s * SECTOR_SIZE);
            }
        }
        cout << "  LAB Pool initialized (" << sb.labPoolClusters << " blocks available)\n";
        
        cout << "Initializing Journal...\n";
        JournalEntry emptyJournal[SECTOR_SIZE / sizeof(JournalEntry)];
        memset(emptyJournal, 0, sizeof(emptyJournal));
        for (uint64_t i = 0; i < sb.journalSectors; i++) {
            disk.writeSector((sb.journalStartCluster * SECTORS_PER_CLUSTER) + i, emptyJournal);
        }
        cout << "  Journal initialized (" << journalEntries << " entries)\n";
        
        cout << "Initializing Global Level Registry...\n";
        LevelDescriptor levelRegistry[CLUSTER_SIZE / sizeof(LevelDescriptor)];
        memset(levelRegistry, 0, sizeof(levelRegistry));
        
        SYSTEMTIME st;
        GetSystemTime(&st);
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        uint64_t timestamp = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        
        strcpy(levelRegistry[0].name, "master");
        levelRegistry[0].levelID = LEVEL_ID_MASTER;
        levelRegistry[0].parentLevelID = LEVEL_ID_NONE;
        levelRegistry[0].rootContentCluster = sb.rootDirCluster + 1;
        levelRegistry[0].createTime = timestamp;
        levelRegistry[0].modTime = timestamp;
        levelRegistry[0].flags = LEVEL_FLAG_ACTIVE;
        levelRegistry[0].refCount = 1;
        levelRegistry[0].childCount = 0;
        levelRegistry[0].totalSize = 0;
        
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.writeSector(sb.levelRegistryCluster * SECTORS_PER_CLUSTER + s,
                ((char*)levelRegistry) + s * SECTOR_SIZE);
        }
        char zeros[SECTOR_SIZE] = {0};
        for (int s = SECTORS_PER_CLUSTER; s < SECTORS_PER_CLUSTER * 2; s++) {
            disk.writeSector(sb.levelRegistryCluster * SECTORS_PER_CLUSTER + s, zeros);
        }
        cout << "  Level Registry initialized with 'master' (ID: 1)\n";
        
        cout << "Initializing Root Directory...\n";
        VersionEntry vTable[CLUSTER_SIZE / sizeof(VersionEntry)];
        memset(vTable, 0, sizeof(vTable));
        strcpy(vTable[0].versionName, "master");
        vTable[0].isActive = 1;
        vTable[0].contentTableCluster = sb.rootDirCluster + 1;
        vTable[0].levelID = LEVEL_ID_MASTER;
        vTable[0].parentLevelID = LEVEL_ID_NONE;
        vTable[0].flags = LEVEL_FLAG_ACTIVE;
        vTable[0].permissions = PERM_ROOT_DEFAULT;
        vTable[0].createTime = (uint32_t)(timestamp & 0xFFFFFFFF);
        vTable[0].modTime = (uint32_t)(timestamp & 0xFFFFFFFF);
        vTable[0].isLocked = 0;
        vTable[0].isSnapshot = 0;
        
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.writeSector(sb.rootDirCluster * SECTORS_PER_CLUSTER + s,
                ((char*)vTable) + s * SECTOR_SIZE);
        }
        
        DirEntry content[CLUSTER_SIZE / sizeof(DirEntry)];
        memset(content, 0, sizeof(content));
        
        strcpy(content[0].name, ".");
        content[0].type = TYPE_LEVELED_DIR;
        content[0].startCluster = sb.rootDirCluster;
        content[0].size = 0;
        content[0].attributes = PERM_ROOT_DEFAULT;
        content[0].createTime = (uint32_t)(timestamp & 0xFFFFFFFF);
        content[0].modTime = (uint32_t)(timestamp & 0xFFFFFFFF);
        
        for (int i = 1; i < CLUSTER_SIZE / sizeof(DirEntry); i++) {
            content[i].type = TYPE_FREE;
            content[i].attributes = 0;
        }
        
        for (int s = 0; s < SECTORS_PER_CLUSTER; s++) {
            disk.writeSector((sb.rootDirCluster + 1) * SECTORS_PER_CLUSTER + s,
                ((char*)content) + s * SECTOR_SIZE);
        }
        cout << "  Root directory initialized with perms: rwx\n";
        
        disk.close();
        cout << "\n=== LFS v2 Format Complete ===\n";
        cout << "  Architecture: 2-Level Radix Tree HLAT\n";
        cout << "  Total Clusters: " << sb.totalClusters << "\n";
        cout << "  Free Clusters: " << sb.totalFreeClusters << "\n";
        cout << "  Root Level: master (ID: 1)\n";
        return true;
    }

    static bool createImageFile(const string& filePath, uint64_t sizeMB) {
        HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            cout << "Failed to create file: " << filePath << "\n";
            return false;
        }
        
        uint64_t sizeBytes = sizeMB * 1024 * 1024;
        LARGE_INTEGER newSize;
        newSize.QuadPart = sizeBytes;
        
        if (!SetFilePointerEx(hFile, newSize, NULL, FILE_BEGIN) || !SetEndOfFile(hFile)) {
            cout << "Failed to set file size.\n";
            CloseHandle(hFile);
            return false;
        }
        
        SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
        char zero[4096] = {0};
        DWORD written;
        uint64_t toWrite = min(sizeBytes, (uint64_t)(1024 * 1024));
        for (uint64_t i = 0; i < toWrite; i += 4096) {
            WriteFile(hFile, zero, 4096, &written, NULL);
        }
        
        CloseHandle(hFile);
        cout << "Created image file: " << filePath << " (" << sizeMB << " MB)\n";
        return true;
    }

    bool formatImage(const string& filePath) {
        if (!disk.openFile(filePath)) {
            cout << "Failed to open image file: " << filePath << "\n";
            return false;
        }
        
        uint64_t diskSizeBytes = disk.getFileSizeFromHandle();
        if (diskSizeBytes == 0) {
            cout << "Error: Could not determine file size.\n";
            disk.close();
            return false;
        }
        
        cout << "Formatting image file: " << filePath << " (" << (diskSizeBytes/1024/1024) << " MB)...\n";
        return performFormat(diskSizeBytes);
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
        if (cmd == "list") {
            DiskPartitionManager::listDisks();
        }
        else if (cmd == "format") {
            PartitionResult res = DiskPartitionManager::createPartitionInteractive();
            if (res.success) {
                if (fmt.formatDirect(res.diskIndex, res.offset, res.size)) {
                    cout << "Format Success. Use 'mount auto' to access.\n";
                } else {
                    cout << "Format Failed.\n";
                }
            } else {
                cout << "Partition creation aborted or failed.\n";
            }
        }
        else if (cmd == "createimg") {
            string filePath;
            uint64_t sizeMB = 0;
            ss >> filePath >> sizeMB;
            if (filePath.empty() || sizeMB == 0) {
                cout << "Usage: createimg <filename.img> <size_mb>\n";
                cout << "Example: createimg myfs.img 200\n";
            } else {
                if (filePath.find('.') == string::npos) {
                    filePath += ".img";
                }
                Formatter::createImageFile(filePath, sizeMB);
            }
        }
        else if (cmd == "formatimg") {
            string filePath;
            ss >> filePath;
            if (filePath.empty()) {
                cout << "Usage: formatimg <filepath.img>\n";
            } else {
                fmt.formatImage(filePath);
            }
        }
        else if (cmd == "help") {
            cout << "list      - List all physical disks.\n";
            cout << "format    - Create partition and format on physical disk.\n";
            cout << "createimg - Create empty image file: createimg <path> <size_mb>\n";
            cout << "formatimg - Format an image file: formatimg <path>\n";
            cout << "exit      - Quit.\n";
        }
        else cout << "Unknown command.\n";
    }
    return 0;
}
