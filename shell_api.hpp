// Linuxify Shell API - Named Pipe IPC for external command delegation
// Compile: g++ -std=c++17 -static -o linuxify.exe main.cpp registry.cpp -lpsapi -lws2_32 -liphlpapi -lwininet -lwlanapi 2>&1

#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

namespace ShellAPI {

const char* PIPE_NAME = "\\\\.\\pipe\\LinuxifyShell";
const DWORD BUFFER_SIZE = 65536;

static std::atomic<bool> g_serverRunning{false};
static std::thread g_serverThread;

using CommandHandler = std::function<std::string(const std::string&)>;
static CommandHandler g_commandHandler = nullptr;

// Input API - Forward declarations to implementation in main/input_handler
using InputProvider = std::function<std::string(const std::string& prompt, bool isPassword)>;
using ConfirmationProvider = std::function<bool(const std::string& prompt)>;

static InputProvider g_inputProvider = nullptr;
static ConfirmationProvider g_confirmationProvider = nullptr;

inline void setInputProviders(InputProvider input, ConfirmationProvider confirm) {
    g_inputProvider = input;
    g_confirmationProvider = confirm;
}

inline std::string readLine(const std::string& prompt = "", bool isPassword = false) {
    if (g_inputProvider) return g_inputProvider(prompt, isPassword);
    return "";
}

inline bool confirm(const std::string& prompt) {
    if (g_confirmationProvider) return g_confirmationProvider(prompt);
    return false;
}

inline void setCommandHandler(CommandHandler handler) {
    g_commandHandler = handler;
}

inline void handleClient(HANDLE hPipe) {
    char buffer[BUFFER_SIZE];
    DWORD bytesRead, bytesWritten;
    
    // Read request
    if (ReadFile(hPipe, buffer, BUFFER_SIZE - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        std::string request(buffer);
        
        std::string response;
        
        if (request.substr(0, 5) == "EXEC ") {
            std::string command = request.substr(5);
            
            if (g_commandHandler) {
                try {
                    std::string output = g_commandHandler(command);
                    response = "0\n" + output;
                } catch (const std::exception& e) {
                    response = "1\nError: " + std::string(e.what());
                }
            } else {
                response = "1\nNo command handler registered";
            }
        } else if (request == "PING") {
            response = "PONG";
        } else if (request == "STATUS") {
            response = "OK\nLinuxify Shell API v1.0";
        } else {
            response = "1\nUnknown command. Use: EXEC <command>, PING, STATUS";
        }
        
        WriteFile(hPipe, response.c_str(), (DWORD)response.length(), &bytesWritten, NULL);
        FlushFileBuffers(hPipe);
    }
    
    // Clean up
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
}

inline void serverLoop() {
    while (g_serverRunning) {
        HANDLE hPipe = CreateNamedPipeA(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE,
            BUFFER_SIZE,
            0,
            NULL
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }
        
        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            std::thread clientThread(handleClient, hPipe);
            clientThread.detach();
        } else {
            CloseHandle(hPipe);
        }
    }
}

inline void startServer() {
    if (g_serverRunning) return;
    
    g_serverRunning = true;
    g_serverThread = std::thread(serverLoop);
}

inline void stopServer() {
    g_serverRunning = false;
    
    HANDLE hPipe = CreateFileA(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }
    
    if (g_serverThread.joinable()) {
        g_serverThread.join();
    }
}

inline bool isRunning() {
    return g_serverRunning;
}

}
