// LinPTY - Custom Pseudo-Terminal Implementation for Linuxify

#ifndef LINPTY_HPP
#define LINPTY_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class LinPTY {
private:
    // Pipe handles
    HANDLE hStdinRead = NULL;
    HANDLE hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL;
    HANDLE hStdoutWrite = NULL;
    
    // Process
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    
    // Reader thread
    std::thread readerThread;
    std::atomic<bool> running{false};
    
    // Terminal size
    int cols = 120;
    int rows = 30;
    
    // Callback for output
    std::function<void(const char*, int)> onOutput;
    
    // Line buffer for input (line discipline simulation)
    std::string lineBuffer;
    std::mutex lineMutex;
    
public:
    LinPTY() = default;
    ~LinPTY() { close(); }
    
    // Set output callback
    void setOutputCallback(std::function<void(const char*, int)> callback) {
        onOutput = callback;
    }
    
    // Start child process
    bool start(const std::wstring& command, int c, int r) {
        cols = c;
        rows = r;
        
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;
        
        // Create stdin pipe
        if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) {
            return false;
        }
        SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
        
        // Create stdout pipe  
        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
            CloseHandle(hStdinRead);
            CloseHandle(hStdinWrite);
            return false;
        }
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
        
        // Create child process
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdInput = hStdinRead;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStdoutWrite;
        si.wShowWindow = SW_HIDE;
        
        PROCESS_INFORMATION pi = {};
        std::wstring cmdLine = command;
        
        // Set environment variable to indicate PTY mode
        SetEnvironmentVariableW(L"LINPTY", L"1");
        SetEnvironmentVariableW(L"TERM", L"xterm-256color");
        
        // Set terminal size environment
        wchar_t colsStr[16], rowsStr[16];
        swprintf_s(colsStr, L"%d", cols);
        swprintf_s(rowsStr, L"%d", rows);
        SetEnvironmentVariableW(L"COLUMNS", colsStr);
        SetEnvironmentVariableW(L"LINES", rowsStr);
        
        if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, TRUE,
                            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hStdinRead);
            CloseHandle(hStdinWrite);
            CloseHandle(hStdoutRead);
            CloseHandle(hStdoutWrite);
            return false;
        }
        
        // Close child's ends
        CloseHandle(hStdinRead);
        CloseHandle(hStdoutWrite);
        hStdinRead = NULL;
        hStdoutWrite = NULL;
        
        hProcess = pi.hProcess;
        hThread = pi.hThread;
        running = true;
        
        // Start reader thread
        readerThread = std::thread([this]() {
            char buffer[4096];
            DWORD bytesRead;
            
            while (running) {
                if (ReadFile(hStdoutRead, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                    if (onOutput) {
                        onOutput(buffer, bytesRead);
                    }
                } else {
                    break;
                }
            }
            running = false;
        });
        
        return true;
    }
    
    // Write raw data to child stdin
    bool writeRaw(const char* data, int len) {
        if (!hStdinWrite || !running) return false;
        
        DWORD bytesWritten;
        return WriteFile(hStdinWrite, data, len, &bytesWritten, NULL) && (int)bytesWritten == len;
    }
    
    // Write a single character
    bool writeChar(char c) {
        return writeRaw(&c, 1);
    }
    
    // Write string
    bool writeString(const std::string& str) {
        return writeRaw(str.c_str(), (int)str.length());
    }
    
    // Send special key sequences
    void sendUp()     { writeRaw("\x1b[A", 3); }
    void sendDown()   { writeRaw("\x1b[B", 3); }
    void sendRight()  { writeRaw("\x1b[C", 3); }
    void sendLeft()   { writeRaw("\x1b[D", 3); }
    void sendHome()   { writeRaw("\x1b[H", 3); }
    void sendEnd()    { writeRaw("\x1b[F", 3); }
    void sendDelete() { writeRaw("\x1b[3~", 4); }
    void sendBackspace() { writeRaw("\x7f", 1); }
    void sendTab()    { writeRaw("\t", 1); }
    void sendEnter()  { writeRaw("\r", 1); }
    void sendCtrlC()  { writeRaw("\x03", 1); }
    void sendCtrlD()  { writeRaw("\x04", 1); }
    void sendCtrlZ()  { writeRaw("\x1a", 1); }
    
    // Resize
    void resize(int c, int r) {
        cols = c;
        rows = r;
        
        // Send SIGWINCH equivalent - update environment
        wchar_t colsStr[16], rowsStr[16];
        swprintf_s(colsStr, L"%d", cols);
        swprintf_s(rowsStr, L"%d", rows);
        SetEnvironmentVariableW(L"COLUMNS", colsStr);
        SetEnvironmentVariableW(L"LINES", rowsStr);
    }
    
    // Check if running
    bool isRunning() const {
        return running;
    }
    
    // Get exit code
    DWORD getExitCode() {
        DWORD exitCode = 0;
        if (hProcess) {
            GetExitCodeProcess(hProcess, &exitCode);
        }
        return exitCode;
    }
    
    // Close
    void close() {
        running = false;
        
        if (hStdinWrite) {
            CloseHandle(hStdinWrite);
            hStdinWrite = NULL;
        }
        
        if (hStdoutRead) {
            CloseHandle(hStdoutRead);
            hStdoutRead = NULL;
        }
        
        if (readerThread.joinable()) {
            readerThread.join();
        }
        
        if (hProcess) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
            hProcess = NULL;
        }
        
        if (hThread) {
            CloseHandle(hThread);
            hThread = NULL;
        }
    }
};

#endif // LINPTY_HPP
