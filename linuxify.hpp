// Linuxify Shell Client Library
// Include this header to execute commands through the Linuxify shell
// This header is installed to system include directories during Linuxify installation

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <sstream>

namespace Linuxify {

const char* PIPE_NAME = "\\\\.\\pipe\\LinuxifyShell";
const DWORD BUFFER_SIZE = 65536;

struct Result {
    int exitCode;
    std::string output;
    bool success() const { return exitCode == 0; }
    operator std::string() const { return output; }
    operator bool() const { return exitCode == 0; }
};

class Shell {
private:
    HANDLE hPipe;
    bool connected;
    
    bool ensureConnection() {
        if (connected && hPipe != INVALID_HANDLE_VALUE) return true;
        
        hPipe = CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL
        );
        
        connected = (hPipe != INVALID_HANDLE_VALUE);
        return connected;
    }
    
    std::string sendRequest(const std::string& request) {
        HANDLE hPipe = CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) return "";
        
        DWORD bytesWritten;
        if (!WriteFile(hPipe, request.c_str(), (DWORD)request.length(), &bytesWritten, NULL)) {
            CloseHandle(hPipe);
            return "";
        }
        
        char buffer[BUFFER_SIZE];
        DWORD bytesRead;
        std::string response;
        
        if (ReadFile(hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            response = buffer;
        }
        
        CloseHandle(hPipe);
        return response;
    }

public:
    Shell() : hPipe(INVALID_HANDLE_VALUE), connected(false) {}
    ~Shell() { disconnect(); }
    
    void disconnect() {
        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
            connected = false;
        }
    }
    
    bool isConnected() const { return connected; }
    
    Result exec(const std::string& command) {
        Result result = {-1, ""};
        
        std::string response = sendRequest("EXEC " + command);
        if (response.empty()) {
            result.output = "Error: Cannot connect to Linuxify shell";
            return result;
        }
        
        size_t newline = response.find('\n');
        if (newline != std::string::npos) {
            result.exitCode = std::stoi(response.substr(0, newline));
            result.output = response.substr(newline + 1);
        } else {
            result.exitCode = 0;
            result.output = response;
        }
        
        return result;
    }
    
    std::string operator()(const std::string& command) {
        return exec(command).output;
    }
    
    bool ping() {
        return sendRequest("PING") == "PONG";
    }
    
    std::string status() {
        return sendRequest("STATUS");
    }
};

static Shell defaultShell;

inline Result exec(const std::string& command) {
    return defaultShell.exec(command);
}

inline std::string operator""_sh(const char* cmd, size_t) {
    return defaultShell.exec(cmd).output;
}

inline bool isRunning() {
    return defaultShell.ping();
}

inline std::string pwd() { return defaultShell.exec("pwd").output; }
inline std::string ls(const std::string& path = "") { 
    return defaultShell.exec("ls " + path).output; 
}
inline std::string cat(const std::string& file) { 
    return defaultShell.exec("cat " + file).output; 
}
inline std::string echo(const std::string& msg) { 
    return defaultShell.exec("echo " + msg).output; 
}

}

inline Linuxify::Result linuxify(const std::string& command) {
    return Linuxify::exec(command);
}
