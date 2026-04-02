#ifndef LINUXIFY_ENGINE_SESSION_MANAGER_HPP
#define LINUXIFY_ENGINE_SESSION_MANAGER_HPP

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include "shell_context.hpp"

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include "shell_context.hpp"

namespace fs = std::filesystem;

class SessionManager {
public:
    struct SessionInfo {
        int id;
        DWORD pid;
        int parentId;
        std::string name;
    };

    int masterPid = 0;
    int currentSessionId = 0;
    std::string currentSessionName = "0";

private:
    std::string getStateFilePath() const {
        char* home = getenv("USERPROFILE");
        fs::path p = home ? fs::path(home) : fs::path("C:\\");
        p /= ".linuxify";
        if (!fs::exists(p)) fs::create_directory(p);
        p /= ("saao_tree_" + std::to_string(masterPid) + ".txt");
        return p.string();
    }

    HANDLE myWakeEvent = NULL;

    std::string getEventName(int sessionId) const {
        return "Global\\Linuxify_SAAO_Wake_" + std::to_string(masterPid) + "_" + std::to_string(sessionId);
    }

    void writeState(const std::vector<SessionInfo>& sessions) {
        std::ofstream out(getStateFilePath());
        for (const auto& s : sessions) {
            out << s.id << " " << s.pid << " " << s.parentId << " " << s.name << "\n";
        }
    }

public:
    SessionManager() {}

    ~SessionManager() {
        if (myWakeEvent) CloseHandle(myWakeEvent);
    }

    // Called on boot
    void initChild(int mPid, int sId) {
        masterPid = mPid;
        currentSessionId = sId;
        
        // Find my name from state file
        auto sessions = readState();
        for (const auto& s : sessions) {
            if (s.id == currentSessionId) {
                currentSessionName = s.name;
                break;
            }
        }
        
        myWakeEvent = CreateEventA(NULL, FALSE, FALSE, getEventName(currentSessionId).c_str());
        // Do not wait immediately, child starts active
    }

    void initMaster() {
        masterPid = GetCurrentProcessId();
        currentSessionId = 0;
        currentSessionName = "0";
        myWakeEvent = CreateEventA(NULL, FALSE, FALSE, getEventName(0).c_str());
        
        // Initialize state file
        std::vector<SessionInfo> State = { {0, (DWORD)masterPid, -1, "0"} };
        writeState(State);
    }
    
    std::vector<SessionInfo> readState() const {
        std::vector<SessionInfo> sessions;
        std::ifstream in(getStateFilePath());
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            SessionInfo s;
            if (iss >> s.id >> s.pid >> s.parentId >> s.name) {
                sessions.push_back(s);
            }
        }
        return sessions;
    }

    int getSessionIdByName(const std::string& name) const {
        auto sessions = readState();
        for (const auto& s : sessions) {
            if (s.name == name) return s.id;
        }
        return -1;
    }

    bool hasSession(int id) const {
        auto sessions = readState();
        for (const auto& s : sessions) {
            if (s.id == id) return true;
        }
        return false;
    }

    int addSession(int parentId, const std::string& name) {
        auto sessions = readState();
        int newId = 0;
        for (const auto& s : sessions) {
            if (s.id >= newId) newId = s.id + 1;
        }

        std::string finalName = name.empty() ? std::to_string(newId) : name;

        // Spawn child
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);

        std::string cmd = "\"" + std::string(exePath) + "\" --saao " + std::to_string(masterPid) + " " + std::to_string(newId);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        // Do NOT use CREATE_NO_WINDOW or detaching flags, they MUST share the same console instance.
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            std::cerr << "Failed to spawn session.\n";
            return -1;
        }

        sessions.push_back({newId, pi.dwProcessId, parentId, finalName});
        writeState(sessions);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Put myself to sleep, since the child will immediately take over foreground
        std::cout << "Switching to session [" << finalName << " | " << newId << "]...\n";
        suspendAndAwait();

        return newId;
    }

    void suspendAndAwait() {
        WaitForSingleObject(myWakeEvent, INFINITE);
        // We have awoken! Let the shell reprint prompt from where it was
    }

    bool canSwitchTo(int currentId, int targetId) const {
        if (!hasSession(targetId)) return false;
        
        auto sessions = readState();
        for (const auto& s : sessions) {
            if (s.id == currentId) {
                if (s.parentId == targetId) return true; // Target is parent
            }
            if (s.id == targetId) {
                if (s.parentId == currentId) return true; // Target is child
            }
        }
        return false;
    }

    void switchSession(int targetId) {
        HANDLE hTargetEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, getEventName(targetId).c_str());
        if (!hTargetEvent) {
             std::cerr << "Cannot open wake event for session " << targetId << ".\n";
             return;
        }
        
        SetEvent(hTargetEvent);
        CloseHandle(hTargetEvent);
        
        suspendAndAwait();
    }

    void recursivelyPop(int targetId, std::vector<SessionInfo>& sessions) {
        // Find children
        std::vector<int> kids;
        for (const auto& s : sessions) {
            if (s.parentId == targetId) kids.push_back(s.id);
        }

        // Depth first
        for (int k : kids) recursivelyPop(k, sessions);

        // Find PID and terminate
        for (auto it = sessions.begin(); it != sessions.end(); ) {
            if (it->id == targetId) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, it->pid);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool deleteSession(int targetId) {
        auto sessions = readState();
        
        bool isTargetActiveAncestor = false;
        int checkId = currentSessionId;
        while (checkId != -1) {
            if (checkId == targetId) {
                isTargetActiveAncestor = true;
                break;
            }
            bool found = false;
            for (const auto& s : sessions) {
                if (s.id == checkId) { checkId = s.parentId; found = true; break; }
            }
            if (!found) break;
        }

        if (isTargetActiveAncestor) {
            std::cerr << "Error: Cannot delete the active session or an ancestor of it.\n";
            return false;
        }

        recursivelyPop(targetId, sessions);
        writeState(sessions);
        return true;
    }
};

#endif // LINUXIFY_ENGINE_SESSION_MANAGER_HPP
