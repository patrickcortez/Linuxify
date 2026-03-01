// g++ -O3 -o ../cmds/top.exe top.cpp -lpsapi
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <conio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

struct ProcInfo {
    DWORD pid;
    std::string name;
    SIZE_T mem;
    DWORD threads;
    std::string user;
    double cpu; // Approximation
};

void clearScreen() {
    std::cout << "\033[2J\033[H";
}

std::string getProcessUser(HANDLE hProcess) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) return "unknown";

    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseHandle(hToken);
        return "unknown";
    }

    PTOKEN_USER pTokenUser = (PTOKEN_USER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
    if (!pTokenUser) {
        CloseHandle(hToken);
        return "unknown";
    }

    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
        HeapFree(GetProcessHeap(), 0, pTokenUser);
        CloseHandle(hToken);
        return "unknown";
    }

    SID_NAME_USE sidType;
    char name[256];
    DWORD cbName = sizeof(name);
    char domainName[256];
    DWORD cbDomainName = sizeof(domainName);

    std::string user = "unknown";
    if (LookupAccountSidA(NULL, pTokenUser->User.Sid, name, &cbName, domainName, &cbDomainName, &sidType)) {
        user = name;
    }

    HeapFree(GetProcessHeap(), 0, pTokenUser);
    CloseHandle(hToken);
    return user;
}

std::string formatMemory(SIZE_T bytes) {
    double kb = bytes / 1024.0;
    if (kb > 1024) {
        double mb = kb / 1024.0;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fM", mb);
        return buf;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0fK", kb);
    return buf;
}

void getSystemMetrics(SIZE_T& totalMem, SIZE_T& availMem, DWORD& runningProcs) {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    totalMem = memInfo.ullTotalPhys;
    availMem = memInfo.ullAvailPhys;
    
    runningProcs = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnap, &pe32)) {
            do { runningProcs++; } while (Process32Next(hSnap, &pe32));
        }
        CloseHandle(hSnap);
    }
}

int main(int argc, char* argv[]) {
    // top/htop terminal UI approximation
    bool htopMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: top\n";
            std::cout << "Display Linuxify tasks\n";
            return 0;
        }
    }
    
    // Check if originally called as htop
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeName = exePath;
    if (exeName.find("htop.exe") != std::string::npos) {
        htopMode = true; 
        // Just slightly different UI if desired
    }
    
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    while (true) {
        clearScreen();
        
        SIZE_T totalMem, availMem;
        DWORD runningProcs;
        getSystemMetrics(totalMem, availMem, runningProcs);
        
        if (htopMode) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "top - Linuxify Shell\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

        std::cout << "Tasks: " << runningProcs << " total, 1 running\n";
        
        double usedMemMb = (totalMem - availMem) / (1024.0 * 1024.0);
        double totalMemMb = totalMem / (1024.0 * 1024.0);
        std::cout << "KiB Mem : " << std::fixed << std::setprecision(1) << totalMemMb << " MiB total, " 
                  << std::setprecision(1) << (availMem / (1024.0 * 1024.0)) << " MiB free, "
                  << std::setprecision(1) << usedMemMb << " MiB used\n\n";
                  
        // Column Headers
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | BACKGROUND_BLUE);
        std::cout << std::left 
                  << std::setw(8) << "PID" 
                  << std::setw(15) << "USER" 
                  << std::setw(8) << "PR" 
                  << std::setw(6) << "NI" 
                  << std::setw(10) << "RES"
                  << std::setw(8) << "SHR"
                  << std::setw(6) << "S"
                  << std::setw(8) << "%CPU"
                  << std::setw(8) << "%MEM"
                  << "COMMAND";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "\n";
        
        
        // Collect process info
        std::vector<ProcInfo> procs;
        HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hProcessSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            if (Process32First(hProcessSnap, &pe32)) {
                do {
                    ProcInfo info;
                    info.pid = pe32.th32ProcessID;
                    info.name = pe32.szExeFile;
                    info.threads = pe32.cntThreads;
                    info.mem = 0;
                    info.user = "unknown";
                    info.cpu = 0.0;
                    
                    if (pe32.th32ProcessID != 0) {
                        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                        if (hProc) {
                            info.user = getProcessUser(hProc);
                            PROCESS_MEMORY_COUNTERS pmc;
                            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                                info.mem = pmc.WorkingSetSize;
                            }
                            CloseHandle(hProc);
                        }
                    }
                    procs.push_back(info);
                } while (Process32Next(hProcessSnap, &pe32));
            }
            CloseHandle(hProcessSnap);
        }
        
        // Sort by memory usage as an approximation of activity
        std::sort(procs.begin(), procs.end(), [](const ProcInfo& a, const ProcInfo& b) {
            return a.mem > b.mem;
        });
        
        // Print top 20 or console height
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int rows = 20;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            rows = csbi.srWindow.Bottom - csbi.srWindow.Top - 7;
        }
        
        for (int i = 0; i < std::min((int)procs.size(), rows); i++) {
            const auto& p = procs[i];
            
            // Calc approximate memory %
            double memPct = (p.mem / (double)totalMem) * 100.0;
            
            // Format CPU arbitrarily for now (Windows CPU calculation requires WMI or PDH over time)
            std::string cpuStr = "0.0";
            if (p.pid == GetCurrentProcessId()) cpuStr = "1.5";
            
            std::cout << std::left 
                      << std::setw(8) << p.pid 
                      << std::setw(15) << p.user.substr(0, 14)
                      << std::setw(8) << "20" 
                      << std::setw(6) << "0" 
                      << std::setw(10) << formatMemory(p.mem)
                      << std::setw(8) << "0"
                      << std::setw(6) << "S"
                      << std::setw(8) << cpuStr
                      << std::fixed << std::setprecision(1) << std::setw(8) << memPct
                      << p.name << "\n";
        }
        
        // Check for exit
        for (int i = 0; i < 20; i++) {
            if (_kbhit()) {
                char ch = _getch();
                if (ch == 'q' || ch == 'Q' || ch == 3) { // q or Ctrl+C
                    return 0;
                }
            }
            Sleep(100); // Wait total 2 seconds per tick
        }
    }

    return 0;
}
