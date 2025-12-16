// Funux Process Manager - Tracks and manages all Funux processes
// Usage: #include "process.hpp"
#ifndef FUNUX_PROCESS_HPP
#define FUNUX_PROCESS_HPP

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

namespace FunuxSys {

enum class ProcessState {
    RUNNING,
    SUSPENDED,
    TERMINATED,
    UNKNOWN
};

struct FunuxProcess {
    DWORD pid;
    std::string name;
    std::string path;
    ProcessState state;
    std::chrono::system_clock::time_point startTime;
    HANDLE handle;
    bool isFunuxApp;
    size_t memoryUsage;
    double cpuPercent;
    
    FunuxProcess() : pid(0), state(ProcessState::UNKNOWN), handle(nullptr), 
                     isFunuxApp(false), memoryUsage(0), cpuPercent(0.0) {}
};

class ProcessManager {
private:
    std::map<DWORD, FunuxProcess> processes;
    std::mutex mtx;
    DWORD funuxPid;
    
    static ProcessManager* instance;
    
    ProcessManager() {
        funuxPid = GetCurrentProcessId();
    }
    
public:
    static ProcessManager& get() {
        if (!instance) {
            instance = new ProcessManager();
        }
        return *instance;
    }
    
    DWORD spawn(const std::string& path, const std::string& args = "", const std::string& workDir = "") {
        std::lock_guard<std::mutex> lock(mtx);
        
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        std::string cmdLine = path;
        if (!args.empty()) cmdLine += " " + args;
        
        char cmd[MAX_PATH * 2];
        strncpy(cmd, cmdLine.c_str(), sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        
        const char* wd = workDir.empty() ? nullptr : workDir.c_str();
        
        if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, wd, &si, &pi)) {
            return 0;
        }
        
        FunuxProcess proc;
        proc.pid = pi.dwProcessId;
        proc.name = path.substr(path.find_last_of("\\/") + 1);
        proc.path = path;
        proc.state = ProcessState::RUNNING;
        proc.startTime = std::chrono::system_clock::now();
        proc.handle = pi.hProcess;
        proc.isFunuxApp = true;
        proc.memoryUsage = 0;
        proc.cpuPercent = 0.0;
        
        CloseHandle(pi.hThread);
        
        processes[proc.pid] = proc;
        return proc.pid;
    }
    
    DWORD spawnAndWait(const std::string& path, const std::string& args = "", const std::string& workDir = "") {
        DWORD pid = spawn(path, args, workDir);
        if (pid == 0) return 0;
        
        auto it = processes.find(pid);
        if (it != processes.end() && it->second.handle) {
            WaitForSingleObject(it->second.handle, INFINITE);
            
            DWORD exitCode;
            GetExitCodeProcess(it->second.handle, &exitCode);
            
            CloseHandle(it->second.handle);
            it->second.handle = nullptr;
            it->second.state = ProcessState::TERMINATED;
            
            return exitCode;
        }
        return 0;
    }
    
    bool kill(DWORD pid, UINT exitCode = 1) {
        std::lock_guard<std::mutex> lock(mtx);
        
        auto it = processes.find(pid);
        if (it != processes.end()) {
            if (it->second.handle) {
                TerminateProcess(it->second.handle, exitCode);
                CloseHandle(it->second.handle);
                it->second.handle = nullptr;
            }
            it->second.state = ProcessState::TERMINATED;
            return true;
        }
        
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) {
            bool result = TerminateProcess(h, exitCode) != 0;
            CloseHandle(h);
            return result;
        }
        return false;
    }
    
    bool suspend(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
        if (!h) return false;
        
        typedef LONG (NTAPI *NtSuspendProcess)(HANDLE);
        NtSuspendProcess pNtSuspend = (NtSuspendProcess)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtSuspendProcess");
        if (pNtSuspend) {
            pNtSuspend(h);
            auto it = processes.find(pid);
            if (it != processes.end()) it->second.state = ProcessState::SUSPENDED;
        }
        CloseHandle(h);
        return true;
    }
    
    bool resume(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
        if (!h) return false;
        
        typedef LONG (NTAPI *NtResumeProcess)(HANDLE);
        NtResumeProcess pNtResume = (NtResumeProcess)GetProcAddress(
            GetModuleHandleA("ntdll.dll"), "NtResumeProcess");
        if (pNtResume) {
            pNtResume(h);
            auto it = processes.find(pid);
            if (it != processes.end()) it->second.state = ProcessState::RUNNING;
        }
        CloseHandle(h);
        return true;
    }
    
    void update() {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (auto& kv : processes) {
            if (kv.second.handle && kv.second.state == ProcessState::RUNNING) {
                DWORD exitCode;
                if (GetExitCodeProcess(kv.second.handle, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        kv.second.state = ProcessState::TERMINATED;
                        CloseHandle(kv.second.handle);
                        kv.second.handle = nullptr;
                    }
                }
                
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(kv.second.handle, &pmc, sizeof(pmc))) {
                    kv.second.memoryUsage = pmc.WorkingSetSize;
                }
            }
        }
    }
    
    void cleanup() {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (auto it = processes.begin(); it != processes.end();) {
            if (it->second.state == ProcessState::TERMINATED) {
                it = processes.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    std::vector<FunuxProcess> listFunuxProcesses() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<FunuxProcess> result;
        for (const auto& kv : processes) {
            if (kv.second.isFunuxApp) {
                result.push_back(kv.second);
            }
        }
        return result;
    }
    
    std::vector<FunuxProcess> listAllProcesses() {
        std::vector<FunuxProcess> result;
        
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return result;
        
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        
        if (Process32First(snap, &pe)) {
            do {
                FunuxProcess proc;
                proc.pid = pe.th32ProcessID;
                proc.name = pe.szExeFile;
                proc.state = ProcessState::RUNNING;
                
                auto it = processes.find(proc.pid);
                if (it != processes.end()) {
                    proc.isFunuxApp = it->second.isFunuxApp;
                    proc.path = it->second.path;
                    proc.startTime = it->second.startTime;
                } else {
                    proc.isFunuxApp = false;
                }
                
                HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, proc.pid);
                if (h) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                        proc.memoryUsage = pmc.WorkingSetSize;
                    }
                    CloseHandle(h);
                }
                
                result.push_back(proc);
            } while (Process32Next(snap, &pe));
        }
        
        CloseHandle(snap);
        return result;
    }
    
    FunuxProcess* getByPid(DWORD pid) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = processes.find(pid);
        if (it != processes.end()) return &it->second;
        return nullptr;
    }
    
    size_t count() {
        std::lock_guard<std::mutex> lock(mtx);
        return processes.size();
    }
    
    DWORD getFunuxPid() const { return funuxPid; }
};

ProcessManager* ProcessManager::instance = nullptr;

}

#endif
