// g++ -O3 -o ../cmds/df.exe df.cpp
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <iomanip>

int main(int argc, char* argv[]) {
    bool humanReadable = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--human-readable") {
            humanReadable = true;
        } else if (arg == "--help") {
            std::cout << "Usage: df [OPTION]...\n";
            std::cout << "Show information about the file system on which each FILE resides,\n";
            std::cout << "or all file systems by default.\n";
            std::cout << "  -h, --human-readable  print sizes in powers of 1024 (e.g., 1023M)\n";
            return 0;
        }
    }

    std::cout << std::left << std::setw(15) << "Filesystem" 
              << std::setw(15) << (humanReadable ? "Size" : "1K-blocks") 
              << std::setw(15) << "Used" 
              << std::setw(15) << "Avail" 
              << std::setw(10) << "Use%" 
              << "Mounted on\n";

    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            char driveName[] = { (char)('A' + i), ':', '\\', '\0' };
            UINT type = GetDriveTypeA(driveName);
            
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_REMOTE) {
                ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
                if (GetDiskFreeSpaceExA(driveName, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
                    
                    std::string fsName = std::string(1, (char)('A' + i)) + ":\\";
                    std::string mountPoint = "/" + std::string(1, (char)('A' + i));
                    
                    double total = (double)totalNumberOfBytes.QuadPart;
                    double _free = (double)totalNumberOfFreeBytes.QuadPart;
                    double used = total - _free;
                    double pct = (total > 0) ? (used / total) * 100.0 : 0.0;
                    
                    std::string szTotal, szUsed, szAvail;
                    
                    if (humanReadable) {
                        const char* units[] = {"B", "K", "M", "G", "T"};
                        auto formatSize = [&units](double bytes) -> std::string {
                            int unitIdx = 0;
                            while (bytes >= 1024.0 && unitIdx < 4) {
                                bytes /= 1024.0;
                                unitIdx++;
                            }
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%.1f%s", bytes, units[unitIdx]);
                            return buf;
                        };
                        szTotal = formatSize(total);
                        szUsed = formatSize(used);
                        szAvail = formatSize(_free);
                    } else {
                        szTotal = std::to_string((unsigned long long)(total / 1024.0));
                        szUsed = std::to_string((unsigned long long)(used / 1024.0));
                        szAvail = std::to_string((unsigned long long)(_free / 1024.0));
                    }
                    
                    char pctBuf[16];
                    snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", pct);
                    
                    std::cout << std::left << std::setw(15) << fsName
                              << std::setw(15) << szTotal
                              << std::setw(15) << szUsed
                              << std::setw(15) << szAvail
                              << std::setw(10) << pctBuf
                              << mountPoint << "\n";
                }
            }
        }
    }

    return 0;
}
