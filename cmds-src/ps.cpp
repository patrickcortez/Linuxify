// g++ -O3 -o ../cmds/ps.exe ps.cpp -lpsapi
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <wtypes.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "Linuxify: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
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

int main(int argc, char* argv[]) {
    bool showAll = false;
    bool showFull = false;
    bool showJobs = false; // Note: Jobs logic handled separately in shell

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-e" || arg == "-A" || arg == "aux" || arg == "-ef") showAll = true;
        if (arg == "-f" || arg == "aux" || arg == "-ef") showFull = true;
        if (arg == "-j") showJobs = true;
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ps [options]\n";
            std::cout << "Report a snapshot of the current processes.\n\n";
            std::cout << "Options:\n";
            std::cout << "  -e, -A   Show all processes\n";
            std::cout << "  -f       Full format listing\n";
            std::cout << "  aux      Show all processes with full info (BSD style)\n";
            std::cout << "  -j       Show shell job definitions (not supported here, use 'jobs' builtin)\n";
            return 0;
        }
    }

    if (showJobs) {
        printError("Job control is handled natively by the Linuxify shell block. Run 'jobs' from the shell directly.");
        return 1;
    }

    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        printError("Failed to take process snapshot");
        return 1;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hProcessSnap, &pe32)) {
        printError("Failed to get process info");
        CloseHandle(hProcessSnap);
        return 1;
    }

    DWORD currentSessionId;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId);

    if (showFull) {
        std::cout << std::left 
                  << std::setw(15) << "USER" 
                  << std::setw(8) << "PID" 
                  << std::setw(8) << "PPID" 
                  << std::setw(10) << "MEM"
                  << std::setw(8) << "THRDS"
                  << "CMD\n";
    } else {
        std::cout << std::left 
                  << std::setw(8) << "PID" 
                  << std::setw(12) << "TTY" 
                  << std::setw(14) << "TIME" 
                  << "CMD\n";
    }

    do {
        DWORD processSessionId;
        if (ProcessIdToSessionId(pe32.th32ProcessID, &processSessionId)) {
            if (!showAll && processSessionId != currentSessionId) {
                continue;
            }
        }

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
        std::string user = "unknown";
        std::string mem = "0K";
        
        if (hProc) {
            if (showFull) {
                user = getProcessUser(hProc);
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    mem = formatMemory(pmc.WorkingSetSize);
                }
            }
            CloseHandle(hProc);
        }

        if (showFull) {
            std::cout << std::left 
                      << std::setw(15) << user.substr(0, 14) 
                      << std::setw(8) << pe32.th32ProcessID 
                      << std::setw(8) << pe32.th32ParentProcessID 
                      << std::setw(10) << mem
                      << std::setw(8) << pe32.cntThreads
                      << pe32.szExeFile << "\n";
        } else {
            std::cout << std::left 
                      << std::setw(8) << pe32.th32ProcessID 
                      << std::setw(12) << "?" 
                      << std::setw(14) << "00:00:00" 
                      << pe32.szExeFile << "\n";
        }
    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    return 0;
}
