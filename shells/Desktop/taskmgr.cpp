// Compile: g++ -std=c++17 -static -I../src -o TaskMgr.exe taskmgr.cpp -lpsapi
#include "window.hpp"
#include "process.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace FunuxSys;

class TaskManagerApp : public App {
private:
    std::vector<FunuxProcess> processes;
    int selected = 0;
    int scrollOffset = 0;
    bool showAllProcesses = false;
    std::chrono::system_clock::time_point lastUpdate;
    
    std::string formatMemory(size_t bytes) {
        if (bytes >= 1024 * 1024 * 1024) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
            return oss.str();
        } else if (bytes >= 1024 * 1024) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
            return oss.str();
        } else if (bytes >= 1024) {
            return std::to_string(bytes / 1024) + " KB";
        }
        return std::to_string(bytes) + " B";
    }
    
    void refreshProcesses() {
        processes = ProcessManager::get().listAllProcesses();
        
        if (!showAllProcesses) {
            processes.erase(
                std::remove_if(processes.begin(), processes.end(),
                    [](const FunuxProcess& p) { return !p.isFunuxApp; }),
                processes.end());
        }
        
        std::sort(processes.begin(), processes.end(),
            [](const FunuxProcess& a, const FunuxProcess& b) {
                return a.memoryUsage > b.memoryUsage;
            });
        
        if (selected >= (int)processes.size()) {
            selected = std::max(0, (int)processes.size() - 1);
        }
        
        invalidate();
    }
    
public:
    void onInit() override {
        lastUpdate = std::chrono::system_clock::now();
        refreshProcesses();
    }
    
    void onDraw() override {
        std::cout << ANSI::bg(17) << ANSI::CLEAR << ANSI::HOME;
        
        std::cout << ANSI::bg(18) << ANSI::fg(46);
        std::cout << " FUNUX TASK MANAGER";
        for (int i = 19; i < termWidth - 20; i++) std::cout << " ";
        std::cout << ANSI::fg(240) << (showAllProcesses ? "[All Processes]" : "[Funux Apps]   ");
        std::cout << ANSI::fg(250) << " " << processes.size() << " procs ";
        
        std::cout << ANSI::moveTo(2, 1) << ANSI::bg(236) << ANSI::fg(250);
        std::cout << std::setw(8) << "PID" << "  ";
        std::cout << std::setw(25) << std::left << "Name" << std::right;
        std::cout << std::setw(10) << "Memory";
        std::cout << std::setw(10) << "Status";
        std::cout << "  Type";
        for (int i = 65; i < termWidth; i++) std::cout << " ";
        
        int visibleRows = termHeight - 4;
        
        if (selected < scrollOffset) scrollOffset = selected;
        if (selected >= scrollOffset + visibleRows) scrollOffset = selected - visibleRows + 1;
        
        for (int i = 0; i < visibleRows; i++) {
            int idx = scrollOffset + i;
            std::cout << ANSI::moveTo(3 + i, 1);
            
            if (idx < (int)processes.size()) {
                const FunuxProcess& p = processes[idx];
                bool sel = (idx == selected);
                
                std::cout << (sel ? ANSI::bg(24) : ANSI::bg(17));
                
                std::cout << ANSI::fg(240) << std::setw(8) << p.pid << "  ";
                
                std::string name = p.name;
                if (name.size() > 24) name = name.substr(0, 21) + "...";
                std::cout << ANSI::fg(sel ? 255 : 250) << std::setw(25) << std::left << name << std::right;
                
                std::cout << ANSI::fg(226) << std::setw(10) << formatMemory(p.memoryUsage);
                
                std::string state;
                int stateColor = 250;
                switch (p.state) {
                    case ProcessState::RUNNING: state = "Running"; stateColor = 46; break;
                    case ProcessState::SUSPENDED: state = "Paused"; stateColor = 208; break;
                    case ProcessState::TERMINATED: state = "Stopped"; stateColor = 196; break;
                    default: state = "Unknown"; stateColor = 240; break;
                }
                std::cout << ANSI::fg(stateColor) << std::setw(10) << state;
                
                std::cout << ANSI::fg(p.isFunuxApp ? 46 : 240);
                std::cout << (p.isFunuxApp ? "  [FNX]" : "  [SYS]");
                
                for (int j = 65; j < termWidth; j++) std::cout << " ";
            } else {
                std::cout << ANSI::bg(17);
                for (int j = 0; j < termWidth; j++) std::cout << " ";
            }
        }
        
        std::cout << ANSI::moveTo(termHeight, 1) << ANSI::bg(235) << ANSI::fg(250);
        std::cout << " K:Kill  P:Pause  R:Resume  Tab:Toggle View  F5:Refresh  Esc:Exit ";
        for (int i = 67; i < termWidth; i++) std::cout << " ";
        
        std::cout << ANSI::RESET;
        std::cout.flush();
    }
    
    void onKey(int ch, int ext) override {
        if (ch == 27) {
            quit();
        } else if (ch == 9) {
            showAllProcesses = !showAllProcesses;
            refreshProcesses();
        } else if (ch == 'k' || ch == 'K') {
            if (!processes.empty() && selected < (int)processes.size()) {
                DWORD pid = processes[selected].pid;
                if (pid != GetCurrentProcessId() && pid != ProcessManager::get().getFunuxPid()) {
                    Dialog dlg("Kill Process", 45, 7);
                    dlg.addLine("Terminate " + processes[selected].name + "?");
                    dlg.addLine("PID: " + std::to_string(pid));
                    dlg.addButton("Yes");
                    dlg.addButton("No");
                    if (dlg.run() == 0) {
                        ProcessManager::get().kill(pid);
                        refreshProcesses();
                    }
                    invalidate();
                }
            }
        } else if (ch == 'p' || ch == 'P') {
            if (!processes.empty() && selected < (int)processes.size()) {
                DWORD pid = processes[selected].pid;
                if (pid != GetCurrentProcessId()) {
                    ProcessManager::get().suspend(pid);
                    refreshProcesses();
                }
            }
        } else if (ch == 'r' || ch == 'R') {
            if (!processes.empty() && selected < (int)processes.size()) {
                DWORD pid = processes[selected].pid;
                ProcessManager::get().resume(pid);
                refreshProcesses();
            }
        } else if (ext == 63) {
            refreshProcesses();
        } else if (ext == 72 && selected > 0) {
            selected--;
            invalidate();
        } else if (ext == 80 && selected < (int)processes.size() - 1) {
            selected++;
            invalidate();
        } else if (ext == 73) {
            selected = std::max(0, selected - (termHeight - 4));
            invalidate();
        } else if (ext == 81) {
            selected = std::min((int)processes.size() - 1, selected + (termHeight - 4));
            invalidate();
        } else if (ext == 71) {
            selected = 0;
            invalidate();
        } else if (ext == 79) {
            selected = (int)processes.size() - 1;
            invalidate();
        }
    }
    
    void onTick() override {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate);
        
        if (elapsed.count() >= 2) {
            refreshProcesses();
            lastUpdate = now;
        }
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    TaskManagerApp app;
    app.run();
    return 0;
}
