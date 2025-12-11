// Linuxify Process Manager - Background Job Tracking
// Manages background processes and provides process utilities

#ifndef LINUXIFY_PROCESS_MANAGER_HPP
#define LINUXIFY_PROCESS_MANAGER_HPP

#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <iomanip>
#include <psapi.h>

struct BackgroundJob {
    int jobId;
    DWORD pid;
    HANDLE hProcess;
    std::string command;
    bool running;
    DWORD startTime;
};

class ProcessManager {
private:
    std::vector<BackgroundJob> jobs;
    int nextJobId = 1;

public:
    // Add a new background job
    int addJob(HANDLE hProcess, DWORD pid, const std::string& cmd) {
        BackgroundJob job;
        job.jobId = nextJobId++;
        job.pid = pid;
        job.hProcess = hProcess;
        job.command = cmd;
        job.running = true;
        job.startTime = GetTickCount();
        jobs.push_back(job);
        return job.jobId;
    }

    // Update status of all jobs
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

    // List all jobs
    void listJobs() {
        updateJobStatus();
        
        bool hasJobs = false;
        for (const auto& job : jobs) {
            if (job.running) {
                hasJobs = true;
                std::cout << "[" << job.jobId << "] " 
                          << "Running    PID:" << job.pid << "  " 
                          << job.command << std::endl;
            }
        }
        
        if (!hasJobs) {
            std::cout << "No background jobs." << std::endl;
        }
    }

    // Kill a job by job ID
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

    // Kill by PID
    bool killByPid(DWORD pid) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess != NULL) {
            bool result = TerminateProcess(hProcess, 1) != 0;
            CloseHandle(hProcess);
            
            // Update our job list if this was one of ours
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

    // Get job by ID
    BackgroundJob* getJob(int jobId) {
        for (auto& job : jobs) {
            if (job.jobId == jobId) {
                return &job;
            }
        }
        return nullptr;
    }

    // Wait for a job to complete (bring to foreground)
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

    // Clean up completed jobs
    void cleanupCompletedJobs() {
        updateJobStatus();
        jobs.erase(
            std::remove_if(jobs.begin(), jobs.end(), 
                [](const BackgroundJob& j) { return !j.running; }),
            jobs.end()
        );
    }

    // List all system processes (ps command)
    static void listProcesses() {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create process snapshot" << std::endl;
            return;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        std::cout << std::setw(8) << "PID" << "  "
                  << std::setw(8) << "PPID" << "  "
                  << std::setw(10) << "THREADS" << "  "
                  << "NAME" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        if (Process32First(hSnapshot, &pe32)) {
            do {
                std::cout << std::setw(8) << pe32.th32ProcessID << "  "
                          << std::setw(8) << pe32.th32ParentProcessID << "  "
                          << std::setw(10) << pe32.cntThreads << "  "
                          << pe32.szExeFile << std::endl;
            } while (Process32Next(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
    }

    // List processes with memory info (detailed ps)
    static void listProcessesDetailed() {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create process snapshot" << std::endl;
            return;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        std::cout << std::setw(8) << "PID" << "  "
                  << std::setw(10) << "MEM(KB)" << "  "
                  << std::setw(8) << "THREADS" << "  "
                  << "NAME" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        if (Process32First(hSnapshot, &pe32)) {
            do {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, 
                                              FALSE, pe32.th32ProcessID);
                SIZE_T memUsage = 0;
                
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        memUsage = pmc.WorkingSetSize / 1024;
                    }
                    CloseHandle(hProcess);
                }

                std::cout << std::setw(8) << pe32.th32ProcessID << "  "
                          << std::setw(10) << memUsage << "  "
                          << std::setw(8) << pe32.cntThreads << "  "
                          << pe32.szExeFile << std::endl;
            } while (Process32Next(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
    }

    // Simple top-like display
    static void topView() {
        std::cout << "Press 'q' to quit, any other key to refresh...\n\n";
        
        while (true) {
            system("cls");
            
            // Get system info
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            GlobalMemoryStatusEx(&memInfo);
            
            std::cout << "=== Linuxify Top ===" << std::endl;
            std::cout << "Memory: " << (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024*1024) 
                      << " MB used / " << memInfo.ullTotalPhys / (1024*1024) << " MB total ("
                      << memInfo.dwMemoryLoad << "% used)" << std::endl;
            std::cout << std::endl;
            
            listProcessesDetailed();
            
            std::cout << "\nPress 'q' to quit..." << std::endl;
            
            // Check for keypress
            if (_kbhit()) {
                char c = _getch();
                if (c == 'q' || c == 'Q') {
                    break;
                }
            }
            
            Sleep(2000);  // Refresh every 2 seconds
        }
    }
};

// Global process manager
extern ProcessManager g_procMgr;

#endif // LINUXIFY_PROCESS_MANAGER_HPP
