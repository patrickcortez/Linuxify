// g++ -O3 -o ../cmds/free.exe free.cpp
#include <iostream>
#include <string>
#include <iomanip>
#include <windows.h>

int main(int argc, char* argv[]) {
    bool humanReadable = false;
    bool megaBytes = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-m" || arg == "--mebi") megaBytes = true;
        else if (arg == "-h" || arg == "--human") humanReadable = true;
        else if (arg == "--help") {
            std::cout << "Usage: free [options]\n";
            std::cout << "Display amount of free and used memory in the system\n";
            std::cout << "  -m, --mebi        show output in mebibytes\n";
            std::cout << "  -h, --human       show human-readable output\n";
            return 0;
        }
    }
    
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    
    double totalRaw = (double)memInfo.ullTotalPhys;
    double availRaw = (double)memInfo.ullAvailPhys;
    double usedRaw = totalRaw - availRaw;
    
    double swapTotalRaw = (double)memInfo.ullTotalPageFile;
    double swapAvailRaw = (double)memInfo.ullAvailPageFile;
    double swapUsedRaw = swapTotalRaw - swapAvailRaw;
    
    std::string unit = "bytes";
    double divisor = 1.0;
    
    if (humanReadable) {
        // Will format individually
    } else if (megaBytes) {
        divisor = 1024.0 * 1024.0;
    } else {
        // Default to KB to somewhat mimic Linux
        divisor = 1024.0;
    }
    
    auto formatMem = [&](double bytes) -> std::string {
        if (humanReadable) {
            const char* units[] = {"B", "K", "M", "G", "T"};
            int unitIdx = 0;
            while (bytes >= 1024.0 && unitIdx < 4) {
                bytes /= 1024.0;
                unitIdx++;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f%s", bytes, units[unitIdx]);
            return buf;
        } else {
            return std::to_string((unsigned long long)(bytes / divisor));
        }
    };
    
    std::cout << std::left << std::setw(15) << "" 
              << std::setw(15) << "total" 
              << std::setw(15) << "used" 
              << std::setw(15) << "free" 
              << std::setw(15) << "shared" 
              << std::setw(15) << "buff/cache" 
              << "available\n";
              
    std::cout << std::left << std::setw(15) << "Mem:" 
              << std::setw(15) << formatMem(totalRaw)
              << std::setw(15) << formatMem(usedRaw)
              << std::setw(15) << formatMem(availRaw)
              << std::setw(15) << formatMem(0) // Windows doesn't expose easy shared/cache
              << std::setw(15) << formatMem(0)
              << formatMem(availRaw) << "\n";
              
    std::cout << std::left << std::setw(15) << "Swap:" 
              << std::setw(15) << formatMem(swapTotalRaw)
              << std::setw(15) << formatMem(swapUsedRaw)
              << std::setw(15) << formatMem(swapAvailRaw)
              << "\n";

    return 0;
}
