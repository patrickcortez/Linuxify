// g++ -std=c++17 -static -o linuxify main.cpp registry.cpp -lpsapi

#ifndef LINUXIFY_PROCESS_MANAGER_HPP
#define LINUXIFY_PROCESS_MANAGER_HPP

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include "shell_streams.hpp"
#include <psapi.h>
#include <algorithm>
#include <conio.h>
#include <atomic>

struct BackgroundJob {
    int jobId;
    DWORD pid;
    HANDLE hProcess;
    std::string command;
    bool running;
    DWORD startTime;
    int priority;
};

class ProcessManager {
private:
    std::vector<BackgroundJob> jobs;
    int nextJobId = 1;
    std::atomic<DWORD> foregroundPid{0};

public:
    int addJob(HANDLE hProcess, DWORD pid, const std::string& cmd) {
        BackgroundJob job;
        job.jobId = nextJobId++;
        job.pid = pid;
        job.hProcess = hProcess;
        job.command = cmd;
        job.running = true;
        job.startTime = GetTickCount();
        job.priority = 0;
        jobs.push_back(job);
        return job.jobId;
    }

    void updateJobStatus() {
        for (auto& job : jobs) {
            if (job.running && job.hProcess != NULL) {
                DWORD exitCode;
                if (GetExitCodeProcess(job.hProcess, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        job.running = false;
                        CloseHandle(job.hProcess);
                        job.hProcess = NULL;
                    }
                }
            }
        }
    }

    void listJobs() {
        updateJobStatus();
        
        bool hasJobs = false;
        for (const auto& job : jobs) {
            if (job.running) {
                hasJobs = true;
                DWORD elapsed = (GetTickCount() - job.startTime) / 1000;
                ShellIO::sout << "[" << job.jobId << "] Running    PID:" << job.pid 
                              << "  " << elapsed << "s  " << job.command << ShellIO::endl;
            }
        }
        
        for (const auto& job : jobs) {
            if (!job.running) {
                ShellIO::sout << "[" << job.jobId << "] Done       " << job.command << ShellIO::endl;
            }
        }
        
        if (!hasJobs && jobs.empty()) {
            ShellIO::sout << "No background jobs." << ShellIO::endl;
        }
    }

    bool killJob(int jobId) {
        for (auto& job : jobs) {
            if (job.jobId == jobId && job.running) {
                if (TerminateProcess(job.hProcess, 1)) {
                    job.running = false;
                    CloseHandle(job.hProcess);
                    job.hProcess = NULL;
                    return true;
                }
            }
        }
        return false;
    }

    bool killByPid(DWORD pid, int signal = 15) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess != NULL) {
            bool result = TerminateProcess(hProcess, signal) != 0;
            CloseHandle(hProcess);
            
            for (auto& job : jobs) {
                if (job.pid == pid) {
                    job.running = false;
                    if (job.hProcess) {
                        CloseHandle(job.hProcess);
                        job.hProcess = NULL;
                    }
                }
            }
            return result;
        }
        return false;
    }

    BackgroundJob* getJob(int jobId) {
        for (auto& job : jobs) {
            if (job.jobId == jobId) {
                return &job;
            }
        }
        return nullptr;
    }

    bool waitForJob(int jobId) {
        BackgroundJob* job = getJob(jobId);
        if (job && job->running && job->hProcess) {
            WaitForSingleObject(job->hProcess, INFINITE);
            job->running = false;
            CloseHandle(job->hProcess);
            job->hProcess = NULL;
            return true;
        }
        return false;
    }

    void killAll() {
        for (auto& job : jobs) {
            if (job.running && job.hProcess) {
                TerminateProcess(job.hProcess, 1);
                CloseHandle(job.hProcess);
                job.hProcess = NULL;
                job.running = false;
            }
        }
        ShellIO::sout << "[ProcessManager] All background jobs terminated." << ShellIO::endl;
    }

    void cleanupCompletedJobs() {
        updateJobStatus();
        jobs.erase(
            std::remove_if(jobs.begin(), jobs.end(), 
                [](const BackgroundJob& j) { return !j.running; }),
            jobs.end()
        );
    }
    
    // --- MANUAL JOB CONTROL (Signal Propagation) ---
    
    void setForegroundPid(DWORD pid) {
        foregroundPid.store(pid);
    }

    void clearForegroundPid() {
        foregroundPid.store(0);
    }

    // Returns true if a foreground process was killed, false otherwise
    bool killForeground() {
        DWORD pid = foregroundPid.load();
        if (pid != 0) {
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc) {
                TerminateProcess(hProc, 1);
                CloseHandle(hProc);
                // We don't clear pid here immediately; main loop will clear it after wait
                return true;
            }
        }
        return false;
    }

    static bool setProcessPriority(DWORD pid, int niceValue) {
        HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (hProcess == NULL) return false;
        
        DWORD priorityClass;
        if (niceValue <= -15) priorityClass = REALTIME_PRIORITY_CLASS;
        else if (niceValue <= -10) priorityClass = HIGH_PRIORITY_CLASS;
        else if (niceValue <= -5) priorityClass = ABOVE_NORMAL_PRIORITY_CLASS;
        else if (niceValue < 5) priorityClass = NORMAL_PRIORITY_CLASS;
        else if (niceValue < 10) priorityClass = BELOW_NORMAL_PRIORITY_CLASS;
        else priorityClass = IDLE_PRIORITY_CLASS;
        
        bool result = SetPriorityClass(hProcess, priorityClass) != 0;
        CloseHandle(hProcess);
        return result;
    }

    static std::string getPriorityString(DWORD priorityClass) {
        switch (priorityClass) {
            case REALTIME_PRIORITY_CLASS: return "RT";
            case HIGH_PRIORITY_CLASS: return "high";
            case ABOVE_NORMAL_PRIORITY_CLASS: return "above";
            case NORMAL_PRIORITY_CLASS: return "normal";
            case BELOW_NORMAL_PRIORITY_CLASS: return "below";
            case IDLE_PRIORITY_CLASS: return "idle";
            default: return "?";
        }
    }

    static void listProcesses() {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            ShellIO::serr << "Failed to create process snapshot" << ShellIO::endl;
            return;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        ShellIO::sout << "PID       PPID      THR     NAME" << ShellIO::endl;
        ShellIO::sout << "--------------------------------------------------" << ShellIO::endl;

        if (Process32First(hSnapshot, &pe32)) {
            do {
                // Manual formatting since we removed iomanip
                char buffer[128];
                wsprintfA(buffer, "%-8lu  %-8lu  %-6lu  %s", 
                    pe32.th32ProcessID, pe32.th32ParentProcessID, pe32.cntThreads, pe32.szExeFile);
                ShellIO::sout << buffer << ShellIO::endl;
            } while (Process32Next(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
    }

    static void listProcessesDetailed() {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            ShellIO::serr << "Failed to create process snapshot" << ShellIO::endl;
            return;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        ShellIO::sout << "PID       PPID      RSS(KB)   THR     PRI     NAME" << ShellIO::endl;
        ShellIO::sout << "----------------------------------------------------------------------" << ShellIO::endl;

        if (Process32First(hSnapshot, &pe32)) {
            do {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 
                                              FALSE, pe32.th32ProcessID);
                SIZE_T memUsage = 0;
                std::string priority = "?";
                
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        memUsage = pmc.WorkingSetSize / 1024;
                    }
                    priority = getPriorityString(GetPriorityClass(hProcess));
                    CloseHandle(hProcess);
                }

                char buffer[256];
                // Using snprintf for safe formatting if available, or just string composition
                // Using standard string manipulation for independence
                std::string line = std::to_string(pe32.th32ProcessID);
                while(line.length() < 10) line += " ";
                
                std::string ppidStr = std::to_string(pe32.th32ParentProcessID);
                while(ppidStr.length() < 10) ppidStr += " ";
                line += ppidStr;
                
                std::string memStr = std::to_string(memUsage);
                while(memStr.length() < 10) memStr += " ";
                line += memStr;
                
                std::string thrStr = std::to_string(pe32.cntThreads);
                while(thrStr.length() < 8) thrStr += " ";
                line += thrStr;
                
                while(priority.length() < 8) priority += " ";
                line += priority;
                
                line += "  ";
                line += pe32.szExeFile;

                ShellIO::sout << line << ShellIO::endl;
            } while (Process32Next(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
    }

    static void pstree(DWORD rootPid = 0) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return;

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        std::map<DWORD, std::vector<DWORD>> children;
        std::map<DWORD, std::string> names;
        std::map<DWORD, DWORD> parents;

        if (Process32First(hSnapshot, &pe32)) {
            do {
                DWORD pid = pe32.th32ProcessID;
                DWORD ppid = pe32.th32ParentProcessID;
                
                if (pid != ppid) {
                    children[ppid].push_back(pid);
                }
                names[pid] = pe32.szExeFile;
                parents[pid] = ppid;
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);

        for (auto& pair : children) {
            std::sort(pair.second.begin(), pair.second.end(), [&](DWORD a, DWORD b) {
                return names[a] < names[b];
            });
        }

        std::set<DWORD> visited;
        std::function<void(DWORD, const std::string&, bool, int)> printTree;
        printTree = [&](DWORD pid, const std::string& prefix, bool isLast, int depth) {
            if (visited.count(pid)) return;
            visited.insert(pid);
            
            auto it = names.find(pid);
            if (it == names.end()) return;

            std::string name = it->second;
            if (name.length() > 4 && name.substr(name.length() - 4) == ".exe") {
                name = name.substr(0, name.length() - 4);
            }

            if (depth == 0) {
                ShellIO::sout << name << "(" << pid << ")" << ShellIO::endl;
            } else {
                ShellIO::sout << prefix << (isLast ? "`-" : "|-") << name << "(" << pid << ")" << ShellIO::endl;
            }

            auto cit = children.find(pid);
            if (cit != children.end()) {
                std::string newPrefix;
                if (depth == 0) {
                    newPrefix = "";
                } else {
                    newPrefix = prefix + (isLast ? "  " : "| ");
                }
                
                for (size_t i = 0; i < cit->second.size(); i++) {
                    bool last = (i == cit->second.size() - 1);
                    printTree(cit->second[i], newPrefix, last, depth + 1);
                }
            }
        };

        if (rootPid != 0) {
            printTree(rootPid, "", true, 0);
        } else {
            std::set<DWORD> roots;
            for (const auto& pair : names) {
                DWORD pid = pair.first;
                DWORD ppid = parents[pid];
                
                if (names.find(ppid) == names.end() || ppid == 0 || ppid == pid) {
                    roots.insert(pid);
                }
            }

            for (DWORD root : roots) {
                printTree(root, "", true, 0);
            }
        }
    }

    // Helper for pstree printing
    static void printTreeLine(const std::string& line) {
         ShellIO::sout << line << ShellIO::endl;
    }

    static void topView() {
        ShellIO::sout << "Press 'q' to quit, any other key to refresh...\n" << ShellIO::endl;
        
        while (true) {
            HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
            COORD topLeft = {0, 0};
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            DWORD written;
            GetConsoleScreenBufferInfo(hCon, &csbi);
            DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
            FillConsoleOutputCharacterA(hCon, ' ', size, topLeft, &written);
            FillConsoleOutputAttribute(hCon, csbi.wAttributes, size, topLeft, &written);
            SetConsoleCursorPosition(hCon, topLeft);
            
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&memInfo);
            
            FILETIME idleTime, kernelTime, userTime;
            GetSystemTimes(&idleTime, &kernelTime, &userTime);
            
            ShellIO::sout << ShellIO::Color::Green << "=== Linuxify Top ===" << ShellIO::Color::Reset << ShellIO::endl;
            
            ShellIO::sout << "Memory: " << (unsigned long long)((memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024*1024)) 
                      << " MB used / " << (unsigned long long)(memInfo.ullTotalPhys / (1024*1024)) << " MB total ("
                      << memInfo.dwMemoryLoad << "% used)" << ShellIO::endl;
            
            HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32 pe32;
                pe32.dwSize = sizeof(PROCESSENTRY32);
                int procCount = 0;
                if (Process32First(hSnapshot, &pe32)) {
                    do { procCount++; } while (Process32Next(hSnapshot, &pe32));
                }
                CloseHandle(hSnapshot);
                ShellIO::sout << "Processes: " << procCount << ShellIO::endl;
            }
            
            ShellIO::sout << ShellIO::endl;
            listProcessesDetailed();
            
            ShellIO::sout << "\nPress 'q' to quit..." << ShellIO::endl;
            
            if (_kbhit()) {
                char c = _getch();
                if (c == 'q' || c == 'Q') break;
            }
            
            Sleep(2000);
        }
    }
};

extern ProcessManager g_procMgr;

#endif

