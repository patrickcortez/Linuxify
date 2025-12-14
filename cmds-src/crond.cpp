// Linuxify Cron Daemon (crond)
// A standalone daemon that runs scheduled tasks based on crontab
// Communicates with linuxify shell via named pipes (IPC)
//
// Compile: g++ -std=c++17 -static -o crond.exe crond.cpp -lws2_32

#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <ctime>
#include <filesystem>
#include <thread>
#include <atomic>
#include <iomanip>

namespace fs = std::filesystem;

// ============================================================================
// Constants
// ============================================================================

const char* PIPE_NAME = "\\\\.\\pipe\\LinuxifyCrond";
const char* CROND_MUTEX = "Global\\LinuxifyCrondMutex";
const int POLL_INTERVAL_SECS = 60;

// ============================================================================
// Cron Job Structure
// ============================================================================

struct CronField {
    bool isWildcard = false;
    std::set<int> values;
    
    bool matches(int value) const {
        if (isWildcard) return true;
        return values.count(value) > 0;
    }
};

struct CronJob {
    CronField minute;      // 0-59
    CronField hour;        // 0-23
    CronField dayOfMonth;  // 1-31
    CronField month;       // 1-12
    CronField dayOfWeek;   // 0-6 (Sunday = 0)
    std::string command;
    bool isReboot = false;  // @reboot job
    std::string rawLine;    // Original line for display
};

// ============================================================================
// Global State
// ============================================================================

std::vector<CronJob> g_jobs;
std::atomic<bool> g_running{true};
std::string g_linuxdbPath;
std::string g_crontabPath;
std::string g_logPath;
HANDLE g_mutex = NULL;

// ============================================================================
// Utility Functions
// ============================================================================

std::string getLinuxdbPath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    
    // If in cmds folder, go up one level
    if (exeDir.filename() == "cmds") {
        exeDir = exeDir.parent_path();
    }
    
    return (exeDir / "linuxdb").string();
}

void logMessage(const std::string& msg) {
    std::ofstream log(g_logPath, std::ios::app);
    if (log) {
        time_t now = time(nullptr);
        std::tm tm;
        localtime_s(&tm, &now);
        log << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [crond] " << msg << "\n";
    }
}

void printUsage() {
    std::cout << "Linuxify Cron Daemon (crond)\n\n";
    std::cout << "Usage: crond [command]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  (none)      Run daemon in foreground\n";
    std::cout << "  --install   Install crond to start at Windows login\n";
    std::cout << "  --uninstall Remove crond from Windows startup\n";
    std::cout << "  --status    Check if crond is running\n";
    std::cout << "  --help      Show this help\n";
}

// ============================================================================
// Crontab Parser
// ============================================================================

CronField parseField(const std::string& field, int minVal, int maxVal) {
    CronField result;
    
    if (field == "*") {
        result.isWildcard = true;
        return result;
    }
    
    // Handle step syntax: */5 or 1-10/2
    size_t stepPos = field.find('/');
    int step = 1;
    std::string baseField = field;
    
    if (stepPos != std::string::npos) {
        step = std::stoi(field.substr(stepPos + 1));
        baseField = field.substr(0, stepPos);
    }
    
    if (baseField == "*") {
        for (int i = minVal; i <= maxVal; i += step) {
            result.values.insert(i);
        }
        return result;
    }
    
    // Handle lists: 1,3,5
    std::stringstream ss(baseField);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        // Handle range: 1-5
        size_t rangePos = item.find('-');
        if (rangePos != std::string::npos) {
            int start = std::stoi(item.substr(0, rangePos));
            int end = std::stoi(item.substr(rangePos + 1));
            for (int i = start; i <= end; i += step) {
                if (i >= minVal && i <= maxVal) {
                    result.values.insert(i);
                }
            }
        } else {
            int val = std::stoi(item);
            if (val >= minVal && val <= maxVal) {
                result.values.insert(val);
            }
        }
    }
    
    return result;
}

bool parseCronLine(const std::string& line, CronJob& job) {
    std::string trimmed = line;
    
    // Trim leading/trailing whitespace
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    trimmed = trimmed.substr(start);
    size_t end = trimmed.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) trimmed = trimmed.substr(0, end + 1);
    
    // Skip comments and empty lines
    if (trimmed.empty() || trimmed[0] == '#') return false;
    
    job.rawLine = trimmed;
    
    // Check for special strings
    if (trimmed[0] == '@') {
        size_t spacePos = trimmed.find(' ');
        if (spacePos == std::string::npos) return false;
        
        std::string special = trimmed.substr(0, spacePos);
        job.command = trimmed.substr(spacePos + 1);
        
        // Trim command
        start = job.command.find_first_not_of(" \t");
        if (start != std::string::npos) job.command = job.command.substr(start);
        
        if (special == "@reboot") {
            job.isReboot = true;
            return true;
        } else if (special == "@hourly") {
            job.minute = parseField("0", 0, 59);
            job.hour.isWildcard = true;
            job.dayOfMonth.isWildcard = true;
            job.month.isWildcard = true;
            job.dayOfWeek.isWildcard = true;
        } else if (special == "@daily" || special == "@midnight") {
            job.minute = parseField("0", 0, 59);
            job.hour = parseField("0", 0, 23);
            job.dayOfMonth.isWildcard = true;
            job.month.isWildcard = true;
            job.dayOfWeek.isWildcard = true;
        } else if (special == "@weekly") {
            job.minute = parseField("0", 0, 59);
            job.hour = parseField("0", 0, 23);
            job.dayOfMonth.isWildcard = true;
            job.month.isWildcard = true;
            job.dayOfWeek = parseField("0", 0, 6);  // Sunday
        } else if (special == "@monthly") {
            job.minute = parseField("0", 0, 59);
            job.hour = parseField("0", 0, 23);
            job.dayOfMonth = parseField("1", 1, 31);
            job.month.isWildcard = true;
            job.dayOfWeek.isWildcard = true;
        } else if (special == "@yearly" || special == "@annually") {
            job.minute = parseField("0", 0, 59);
            job.hour = parseField("0", 0, 23);
            job.dayOfMonth = parseField("1", 1, 31);
            job.month = parseField("1", 1, 12);
            job.dayOfWeek.isWildcard = true;
        } else {
            return false;  // Unknown special string
        }
        return true;
    }
    
    // Standard 5-field format
    std::istringstream iss(trimmed);
    std::string minute, hour, dom, month, dow;
    
    if (!(iss >> minute >> hour >> dom >> month >> dow)) {
        return false;
    }
    
    // Rest of line is the command
    std::getline(iss, job.command);
    start = job.command.find_first_not_of(" \t");
    if (start == std::string::npos) return false;
    job.command = job.command.substr(start);
    
    if (job.command.empty()) return false;
    
    try {
        job.minute = parseField(minute, 0, 59);
        job.hour = parseField(hour, 0, 23);
        job.dayOfMonth = parseField(dom, 1, 31);
        job.month = parseField(month, 1, 12);
        job.dayOfWeek = parseField(dow, 0, 6);
        return true;
    } catch (...) {
        return false;
    }
}

void loadCrontab() {
    g_jobs.clear();
    
    std::ifstream file(g_crontabPath);
    if (!file) {
        logMessage("No crontab file found");
        return;
    }
    
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        CronJob job;
        if (parseCronLine(line, job)) {
            g_jobs.push_back(job);
        }
    }
    
    logMessage("Loaded " + std::to_string(g_jobs.size()) + " jobs from crontab");
}

// ============================================================================
// Job Execution
// ============================================================================

bool shouldRunNow(const CronJob& job, const std::tm& tm) {
    if (job.isReboot) return false;  // Handled separately
    
    return job.minute.matches(tm.tm_min) &&
           job.hour.matches(tm.tm_hour) &&
           job.dayOfMonth.matches(tm.tm_mday) &&
           job.month.matches(tm.tm_mon + 1) &&
           job.dayOfWeek.matches(tm.tm_wday);
}

void executeJob(const CronJob& job) {
    logMessage("Executing: " + job.command);
    
    // Get linuxify.exe path
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    
    if (exeDir.filename() == "cmds") {
        exeDir = exeDir.parent_path();
    }
    
    fs::path linuxifyPath = exeDir / "linuxify.exe";
    
    std::string cmdLine;
    
    // Check if it's an executable path or needs linuxify
    if (job.command.find(".exe") != std::string::npos ||
        job.command.find(".bat") != std::string::npos ||
        job.command.find(".cmd") != std::string::npos ||
        job.command.find(".ps1") != std::string::npos ||
        job.command.find(":\\") != std::string::npos) {
        // Direct executable
        cmdLine = "cmd /c " + job.command;
    } else {
        // Run via linuxify
        cmdLine = "\"" + linuxifyPath.string() + "\" -c \"" + job.command + "\"";
    }
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    char cmdBuffer[4096];
    strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);
    
    if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, FALSE, 
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        logMessage("Job started: " + job.command);
    } else {
        logMessage("Failed to execute: " + job.command + " (error " + std::to_string(GetLastError()) + ")");
    }
}

void runRebootJobs() {
    for (const auto& job : g_jobs) {
        if (job.isReboot) {
            logMessage("Running @reboot job: " + job.command);
            executeJob(job);
        }
    }
}

// ============================================================================
// IPC Server (Named Pipe)
// ============================================================================

void handleIPCRequest(const std::string& request, std::string& response) {
    if (request == "LIST") {
        // Return crontab contents
        std::ifstream file(g_crontabPath);
        if (file) {
            std::ostringstream oss;
            oss << file.rdbuf();
            response = oss.str();
            if (response.empty()) {
                response = "# No crontab entries\n";
            }
        } else {
            response = "# No crontab file\n";
        }
    } else if (request == "RELOAD") {
        loadCrontab();
        response = "OK: Reloaded " + std::to_string(g_jobs.size()) + " jobs";
    } else if (request == "STATUS") {
        response = "RUNNING: " + std::to_string(g_jobs.size()) + " jobs loaded";
    } else if (request == "PING") {
        response = "PONG";
    } else {
        response = "ERROR: Unknown command";
    }
}

void ipcServerLoop() {
    while (g_running) {
        HANDLE hPipe = CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            NULL
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        
        // Wait for client connection
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char buffer[4096];
            DWORD bytesRead;
            
            if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                std::string request(buffer);
                std::string response;
                
                handleIPCRequest(request, response);
                
                DWORD bytesWritten;
                WriteFile(hPipe, response.c_str(), (DWORD)response.length(), &bytesWritten, NULL);
            }
            
            DisconnectNamedPipe(hPipe);
        }
        
        CloseHandle(hPipe);
    }
}

// ============================================================================
// Daemon Main Loop
// ============================================================================

void daemonLoop() {
    logMessage("Cron daemon started");
    
    // Load crontab
    loadCrontab();
    
    // Run @reboot jobs
    runRebootJobs();
    
    // Start IPC server in separate thread
    std::thread ipcThread(ipcServerLoop);
    ipcThread.detach();
    
    time_t lastCheck = 0;
    
    while (g_running) {
        time_t now = time(nullptr);
        
        // Check once per minute (at the start of each minute)
        if (now / 60 != lastCheck / 60) {
            lastCheck = now;
            
            std::tm tm;
            localtime_s(&tm, &now);
            
            // Reload crontab to pick up changes
            loadCrontab();
            
            // Check each job
            for (const auto& job : g_jobs) {
                if (shouldRunNow(job, tm)) {
                    executeJob(job);
                }
            }
        }
        
        Sleep(1000);  // Check every second for stop signal
    }
    
    logMessage("Cron daemon stopped");
}

// ============================================================================
// Install/Uninstall to Windows Startup
// ============================================================================

bool installToStartup() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, 
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    
    std::string value = std::string("\"") + exePath + "\"";
    
    bool success = RegSetValueExA(hKey, "LinuxifyCrond", 0, REG_SZ,
                                  (const BYTE*)value.c_str(), 
                                  (DWORD)(value.length() + 1)) == ERROR_SUCCESS;
    
    RegCloseKey(hKey);
    
    if (success) {
        logMessage("Installed to Windows startup");
    }
    
    return success;
}

bool uninstallFromStartup() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    bool success = RegDeleteValueA(hKey, "LinuxifyCrond") == ERROR_SUCCESS;
    
    RegCloseKey(hKey);
    
    if (success) {
        logMessage("Removed from Windows startup");
    }
    
    return success;
}

bool isInstalled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    char value[MAX_PATH];
    DWORD size = sizeof(value);
    bool exists = RegQueryValueExA(hKey, "LinuxifyCrond", NULL, NULL,
                                   (LPBYTE)value, &size) == ERROR_SUCCESS;
    
    RegCloseKey(hKey);
    return exists;
}

bool isDaemonRunning() {
    // Try to connect to the pipe
    HANDLE hPipe = CreateFileA(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    // Send PING
    const char* msg = "PING";
    DWORD bytesWritten;
    WriteFile(hPipe, msg, (DWORD)strlen(msg), &bytesWritten, NULL);
    
    char response[256];
    DWORD bytesRead;
    bool running = ReadFile(hPipe, response, sizeof(response) - 1, &bytesRead, NULL) &&
                   bytesRead > 0;
    
    CloseHandle(hPipe);
    return running;
}

// ============================================================================
// Main
// ============================================================================

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char* argv[]) {
    // Initialize paths
    g_linuxdbPath = getLinuxdbPath();
    g_crontabPath = g_linuxdbPath + "\\crontab";
    g_logPath = g_linuxdbPath + "\\cron.log";
    
    // Ensure linuxdb directory exists
    fs::create_directories(g_linuxdbPath);
    
    // Handle command line arguments
    if (argc > 1) {
        std::string arg = argv[1];
        
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--install") {
            if (installToStartup()) {
                std::cout << "Crond installed to Windows startup.\n";
                std::cout << "It will start automatically when you log in.\n";
                return 0;
            } else {
                std::cerr << "Failed to install crond.\n";
                return 1;
            }
        } else if (arg == "--uninstall") {
            if (uninstallFromStartup()) {
                std::cout << "Crond removed from Windows startup.\n";
                return 0;
            } else {
                std::cerr << "Failed to uninstall crond.\n";
                return 1;
            }
        } else if (arg == "--status") {
            bool installed = isInstalled();
            bool running = isDaemonRunning();
            
            std::cout << "Installed: " << (installed ? "yes" : "no") << "\n";
            std::cout << "Running:   " << (running ? "yes" : "no") << "\n";
            
            if (running) {
                // Get job count
                HANDLE hPipe = CreateFileA(PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                                           0, NULL, OPEN_EXISTING, 0, NULL);
                if (hPipe != INVALID_HANDLE_VALUE) {
                    const char* msg = "STATUS";
                    DWORD bytesWritten;
                    WriteFile(hPipe, msg, (DWORD)strlen(msg), &bytesWritten, NULL);
                    
                    char response[256];
                    DWORD bytesRead;
                    if (ReadFile(hPipe, response, sizeof(response) - 1, &bytesRead, NULL)) {
                        response[bytesRead] = '\0';
                        std::cout << "Status:    " << response << "\n";
                    }
                    CloseHandle(hPipe);
                }
            }
            return 0;
        }
    }
    
    // Check if already running
    g_mutex = CreateMutexA(NULL, TRUE, CROND_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cout << "Crond is already running.\n";
        return 1;
    }
    
    // Set up console handler
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    
    std::cout << "Linuxify Cron Daemon starting...\n";
    std::cout << "Crontab: " << g_crontabPath << "\n";
    std::cout << "Log: " << g_logPath << "\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
    // Run daemon
    daemonLoop();
    
    // Cleanup
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
    }
    
    return 0;
}
