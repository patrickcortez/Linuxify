// Linuxify System Information Module
// Provides system info commands: lsmem, lscpu, lshw, lsmount, lsblk, lsusb, lsnet

#ifndef LINUXIFY_SYSTEM_INFO_HPP
#define LINUXIFY_SYSTEM_INFO_HPP

#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>

// For drive info
#include <fileapi.h>

class SystemInfo {
public:
    // lsmem - Display memory information
    static void listMemory() {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        
        if (!GlobalMemoryStatusEx(&memInfo)) {
            std::cerr << "Error: Unable to retrieve memory information" << std::endl;
            return;
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Memory Information ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        DWORDLONG totalPhysMB = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG availPhysMB = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG usedPhysMB = totalPhysMB - availPhysMB;
        
        DWORDLONG totalVirtMB = memInfo.ullTotalVirtual / (1024 * 1024);
        DWORDLONG availVirtMB = memInfo.ullAvailVirtual / (1024 * 1024);
        
        DWORDLONG totalPageMB = memInfo.ullTotalPageFile / (1024 * 1024);
        DWORDLONG availPageMB = memInfo.ullAvailPageFile / (1024 * 1024);
        
        std::cout << std::endl;
        std::cout << "Physical Memory:" << std::endl;
        std::cout << "  Total:     " << std::setw(10) << totalPhysMB << " MB" << std::endl;
        std::cout << "  Used:      " << std::setw(10) << usedPhysMB << " MB (" << memInfo.dwMemoryLoad << "%)" << std::endl;
        std::cout << "  Available: " << std::setw(10) << availPhysMB << " MB" << std::endl;
        
        std::cout << std::endl;
        std::cout << "Virtual Memory:" << std::endl;
        std::cout << "  Total:     " << std::setw(10) << totalVirtMB << " MB" << std::endl;
        std::cout << "  Available: " << std::setw(10) << availVirtMB << " MB" << std::endl;
        
        std::cout << std::endl;
        std::cout << "Page File:" << std::endl;
        std::cout << "  Total:     " << std::setw(10) << totalPageMB << " MB" << std::endl;
        std::cout << "  Available: " << std::setw(10) << availPageMB << " MB" << std::endl;
        
        // Visual memory bar
        std::cout << std::endl;
        int barWidth = 50;
        int usedBars = (int)(memInfo.dwMemoryLoad * barWidth / 100);
        
        std::cout << "Memory Usage: [";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        for (int i = 0; i < usedBars; i++) std::cout << "#";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
        for (int i = usedBars; i < barWidth; i++) std::cout << "-";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "] " << memInfo.dwMemoryLoad << "%" << std::endl;
    }

    // lscpu - Display CPU information
    static void listCPU() {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== CPU Information ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        // Get processor name from registry
        HKEY hKey;
        char cpuName[256] = "Unknown";
        DWORD bufSize = sizeof(cpuName);
        
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)cpuName, &bufSize);
            RegCloseKey(hKey);
        }
        
        // Trim leading spaces from CPU name
        std::string cpuNameStr = cpuName;
        size_t start = cpuNameStr.find_first_not_of(" ");
        if (start != std::string::npos) {
            cpuNameStr = cpuNameStr.substr(start);
        }
        
        std::cout << "Processor:       " << cpuNameStr << std::endl;
        std::cout << "Architecture:    ";
        
        switch (sysInfo.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: std::cout << "x64 (AMD64)"; break;
            case PROCESSOR_ARCHITECTURE_ARM: std::cout << "ARM"; break;
            case PROCESSOR_ARCHITECTURE_ARM64: std::cout << "ARM64"; break;
            case PROCESSOR_ARCHITECTURE_INTEL: std::cout << "x86 (Intel)"; break;
            default: std::cout << "Unknown"; break;
        }
        std::cout << std::endl;
        
        std::cout << "Logical Cores:   " << sysInfo.dwNumberOfProcessors << std::endl;
        std::cout << "Page Size:       " << sysInfo.dwPageSize / 1024 << " KB" << std::endl;
        
        // Get CPU speed from registry
        DWORD cpuSpeed = 0;
        bufSize = sizeof(cpuSpeed);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&cpuSpeed, &bufSize);
            RegCloseKey(hKey);
        }
        
        if (cpuSpeed > 0) {
            std::cout << "Base Speed:      " << cpuSpeed << " MHz";
            if (cpuSpeed >= 1000) {
                std::cout << " (" << std::fixed << std::setprecision(2) << cpuSpeed / 1000.0 << " GHz)";
            }
            std::cout << std::endl;
        }
    }

    // lshw - Display hardware overview
    static void listHardware() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Hardware Information ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        // Computer name
        char computerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = sizeof(computerName);
        GetComputerNameA(computerName, &size);
        std::cout << "Computer Name:   " << computerName << std::endl;
        
        // Username
        char userName[256];
        size = sizeof(userName);
        GetUserNameA(userName, &size);
        std::cout << "User:            " << userName << std::endl;
        
        // OS Version
        std::cout << "OS:              Windows ";
        
        // Get Windows version
        OSVERSIONINFOEX osvi;
        ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
        
        // Use RtlGetVersion for accurate version
        typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            RtlGetVersionPtr rtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
            if (rtlGetVersion) {
                RTL_OSVERSIONINFOW rovi = { 0 };
                rovi.dwOSVersionInfoSize = sizeof(rovi);
                if (rtlGetVersion(&rovi) == 0) {
                    if (rovi.dwMajorVersion == 10 && rovi.dwBuildNumber >= 22000) {
                        std::cout << "11";
                    } else {
                        std::cout << rovi.dwMajorVersion;
                    }
                    std::cout << " (Build " << rovi.dwBuildNumber << ")";
                }
            }
        }
        std::cout << std::endl;
        
        // System uptime
        ULONGLONG uptime = GetTickCount64();
        ULONGLONG seconds = uptime / 1000;
        ULONGLONG minutes = seconds / 60;
        ULONGLONG hours = minutes / 60;
        ULONGLONG days = hours / 24;
        
        std::cout << "Uptime:          ";
        if (days > 0) std::cout << days << " days, ";
        std::cout << (hours % 24) << " hours, " << (minutes % 60) << " minutes" << std::endl;
        
        std::cout << std::endl;
        
        // Show CPU summary
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "--- CPU ---" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        listCPU();
        
        std::cout << std::endl;
        
        // Show Memory summary
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "--- Memory ---" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        listMemory();
    }

    // lsmount / lsblk - List mounted drives/block devices
    static void listMounts() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Mounted Drives ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        std::cout << std::setw(6) << "Drive" << " "
                  << std::setw(12) << "Type" << " "
                  << std::setw(12) << "Total" << " "
                  << std::setw(12) << "Used" << " "
                  << std::setw(12) << "Free" << " "
                  << std::setw(6) << "Use%" << " "
                  << "Label" << std::endl;
        std::cout << std::string(75, '-') << std::endl;
        
        DWORD drives = GetLogicalDrives();
        
        for (char letter = 'A'; letter <= 'Z'; letter++) {
            if (drives & (1 << (letter - 'A'))) {
                std::string drivePath = std::string(1, letter) + ":\\";
                
                UINT driveType = GetDriveTypeA(drivePath.c_str());
                std::string typeStr;
                
                switch (driveType) {
                    case DRIVE_REMOVABLE: typeStr = "Removable"; break;
                    case DRIVE_FIXED: typeStr = "Fixed"; break;
                    case DRIVE_REMOTE: typeStr = "Network"; break;
                    case DRIVE_CDROM: typeStr = "CD-ROM"; break;
                    case DRIVE_RAMDISK: typeStr = "RAM Disk"; break;
                    default: typeStr = "Unknown"; break;
                }
                
                ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
                char volumeName[MAX_PATH] = "";
                
                if (GetDiskFreeSpaceExA(drivePath.c_str(), &freeBytes, &totalBytes, &totalFreeBytes)) {
                    GetVolumeInformationA(drivePath.c_str(), volumeName, MAX_PATH, NULL, NULL, NULL, NULL, 0);
                    
                    ULONGLONG totalGB = totalBytes.QuadPart / (1024 * 1024 * 1024);
                    ULONGLONG freeGB = freeBytes.QuadPart / (1024 * 1024 * 1024);
                    ULONGLONG usedGB = totalGB - freeGB;
                    int usedPercent = totalBytes.QuadPart > 0 ? 
                        (int)((totalBytes.QuadPart - freeBytes.QuadPart) * 100 / totalBytes.QuadPart) : 0;
                    
                    std::cout << std::setw(6) << drivePath << " "
                              << std::setw(12) << typeStr << " "
                              << std::setw(10) << totalGB << " GB "
                              << std::setw(10) << usedGB << " GB "
                              << std::setw(10) << freeGB << " GB "
                              << std::setw(5) << usedPercent << "% "
                              << volumeName << std::endl;
                } else {
                    std::cout << std::setw(6) << drivePath << " "
                              << std::setw(12) << typeStr << " "
                              << "(not ready)" << std::endl;
                }
            }
        }
    }

    // lsusb - List USB devices via Registry
    static void listUSB() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== USB Devices ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\USB", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char vidPidBuffer[255];
            DWORD vidPidSize = 255;
            DWORD index = 0;
            
            while (RegEnumKeyExA(hKey, index, vidPidBuffer, &vidPidSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                // For each VID/PID, iterate instances
                HKEY hSubKey;
                std::string subKeyPath = std::string("SYSTEM\\CurrentControlSet\\Enum\\USB\\") + vidPidBuffer;
                
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKeyPath.c_str(), 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                    char instanceBuffer[255];
                    DWORD instanceSize = 255;
                    DWORD subIndex = 0;
                    
                    while (RegEnumKeyExA(hSubKey, subIndex, instanceBuffer, &instanceSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                         // Open instance key to get FriendlyName or DeviceDesc
                        std::string instancePath = subKeyPath + "\\" + instanceBuffer;
                        HKEY hInstanceKey;
                        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, instancePath.c_str(), 0, KEY_READ, &hInstanceKey) == ERROR_SUCCESS) {
                             char nameBuffer[256];
                             DWORD nameSize = sizeof(nameBuffer);
                             
                             // Try FriendlyName first, then DeviceDesc
                             if (RegQueryValueExA(hInstanceKey, "FriendlyName", NULL, NULL, (LPBYTE)nameBuffer, &nameSize) != ERROR_SUCCESS) {
                                  nameSize = sizeof(nameBuffer);
                                  RegQueryValueExA(hInstanceKey, "DeviceDesc", NULL, NULL, (LPBYTE)nameBuffer, &nameSize);
                             }
                             
                             // Clean up name (remove @oem...)
                             std::string nameStr = nameBuffer;
                             size_t splitPos = nameStr.find(';');
                             if (splitPos != std::string::npos) {
                                 nameStr = nameStr.substr(splitPos + 1);
                             }
                             
                             std::cout << "  - " << nameStr << " (" << vidPidBuffer << ")" << std::endl;
                             RegCloseKey(hInstanceKey);
                        }
                        instanceSize = 255;
                        subIndex++;
                    }
                    RegCloseKey(hSubKey);
                }
                vidPidSize = 255;
                index++;
            }
            RegCloseKey(hKey);
        } else {
            std::cout << "Failed to access registry for USB devices." << std::endl;
        }
    }

    // lsnet - List network interfaces via GetAdaptersInfo (Dynamic Linking)
    static void listNetwork() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Network Interfaces ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;

        // Define structures manually to avoid including iphlpapi.h which requires library linking
        // Or better, define types for pointers and LoadLibrary
        
        // Simplified approach: Parsing GetAdaptersInfo struct if we can define it, 
        // essentially we need to replicate the structs or just use the header but dynamic load the function
        // To be safe and avoid redefinition errors if headers are present, I will continue to use a lighter command approach
        // but one that yields cleaner output than raw ipconfig.
        // Actually, let's use the registry again for NICs, it's safer without libs.
        
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
             char idxBuffer[255];
             DWORD idxSize = 255;
             DWORD index = 0;
             
             while (RegEnumKeyExA(hKey, index, idxBuffer, &idxSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                  std::string cardKeyPath = std::string("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards\\") + idxBuffer;
                  HKEY hCardKey;
                  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, cardKeyPath.c_str(), 0, KEY_READ, &hCardKey) == ERROR_SUCCESS) {
                       char nameBuffer[256];
                       DWORD nameSize = sizeof(nameBuffer);
                       if (RegQueryValueExA(hCardKey, "Description", NULL, NULL, (LPBYTE)nameBuffer, &nameSize) == ERROR_SUCCESS) {
                            std::cout << "Interface: " << nameBuffer << std::endl;
                            // We could get IP addresses via Ws2_32.dll (GetAdaptersAddresses) but that requires linking too.
                            // For now, listing names is good.
                       }
                       RegCloseKey(hCardKey);
                  }
                  idxSize = 255;
                  index++;
             }
             RegCloseKey(hKey);
             
             std::cout << "\n(Use 'ipconfig' for detailed IP configuration)" << std::endl;
        }
    }

    // lsof - List open handles count
    static void listOpenFiles() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Process Handle Counts ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        DWORD count;
        if (GetProcessHandleCount(GetCurrentProcess(), &count)) {
            std::cout << "Current Shell Handles: " << count << std::endl << std::endl;
        }

        // To list top processes by handle count without PowerShell, we need Toolhelp32
        // We can iterate all processes and get their handle counts (if we have permission)
        
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            
            struct ProcInfo {
                std::string name;
                DWORD pid;
                DWORD handleCount;
            };
            std::vector<ProcInfo> procs;
            
            if (Process32First(hSnapshot, &pe32)) {
                do {
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe32.th32ProcessID);
                    DWORD hCount = 0;
                    if (hProcess) {
                        GetProcessHandleCount(hProcess, &hCount);
                        CloseHandle(hProcess);
                    }
                    
                    // Only list if we could get the count
                    if (hCount > 0) {
                        procs.push_back({pe32.szExeFile, pe32.th32ProcessID, hCount});
                    }
                } while (Process32Next(hSnapshot, &pe32));
            }
            CloseHandle(hSnapshot);
            
            // Sort by handle count descending
            std::sort(procs.begin(), procs.end(), [](const ProcInfo& a, const ProcInfo& b) {
                return a.handleCount > b.handleCount;
            });
            
            std::cout << std::setw(30) << std::left << "Process Name" 
                      << std::setw(10) << "PID" 
                      << "Handles" << std::endl;
            std::cout << std::string(50, '-') << std::endl;
            
            for (size_t i = 0; i < 10 && i < procs.size(); i++) {
                 std::cout << std::setw(30) << std::left << procs[i].name 
                           << std::setw(10) << procs[i].pid 
                           << procs[i].handleCount << std::endl;
            }
        }
    }
};

#endif // LINUXIFY_SYSTEM_INFO_HPP
