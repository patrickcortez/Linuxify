// Compile: This is a header-only library, included by main.cpp
#ifndef LINUXIFY_CHILD_HANDLER_HPP
#define LINUXIFY_CHILD_HANDLER_HPP

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include "../signal_handler.hpp"
#include "../process_manager.hpp"

class ChildHandler {
public:

    static int spawn(const std::string& cmdLine, const std::string& workDir, bool wait = true) {
        std::string finalCmdLine = cmdLine;
        
        {
            std::string firstToken;
            size_t start = 0;
            if (!cmdLine.empty() && cmdLine[0] == '"') {
                size_t end = cmdLine.find('"', 1);
                if (end != std::string::npos) {
                    firstToken = cmdLine.substr(1, end - 1);
                    start = end + 1;
                }
            } else {
                size_t space = cmdLine.find(' ');
                if (space != std::string::npos) {
                    firstToken = cmdLine.substr(0, space);
                    start = space;
                } else {
                    firstToken = cmdLine;
                    start = cmdLine.length();
                }
            }
            
            
            std::string ext;
            size_t dotPos = firstToken.rfind('.');
            if (dotPos != std::string::npos) {
                ext = firstToken.substr(dotPos);
                for (auto& c : ext) c = (char)tolower((unsigned char)c);
            }
            
            std::string args = (start < cmdLine.length()) ? cmdLine.substr(start) : "";
            
            if (ext == ".ps1") {
                std::filesystem::path scriptPath(firstToken);
                if (!scriptPath.is_absolute() && !workDir.empty()) {
                    scriptPath = std::filesystem::path(workDir) / scriptPath;
                }
                try {
                    if (std::filesystem::exists(scriptPath)) {
                        scriptPath = std::filesystem::canonical(scriptPath);
                        finalCmdLine = "powershell.exe -ExecutionPolicy Bypass -File \"" + scriptPath.string() + "\"" + args;
                    }
                } catch (...) {}
            } else if (ext == ".bat" || ext == ".cmd") {
                std::filesystem::path scriptPath(firstToken);
                if (!scriptPath.is_absolute() && !workDir.empty()) {
                    scriptPath = std::filesystem::path(workDir) / scriptPath;
                }
                try {
                    if (std::filesystem::exists(scriptPath)) {
                        scriptPath = std::filesystem::canonical(scriptPath);
                        finalCmdLine = "cmd.exe /c \"" + scriptPath.string() + "\"" + args;
                    }
                } catch (...) {}
            }
        }

        auto& signalHandler = SignalHandler::InputDispatcher::getInstance();
        SetConsoleCtrlHandler(SignalHandler::ConsoleCtrlHandler, FALSE);
        signalHandler.restore();

        char cmdBuffer[8192];
        strncpy_s(cmdBuffer, finalCmdLine.c_str(), sizeof(cmdBuffer) - 1);
        const char* dir = workDir.empty() ? nullptr : workDir.c_str();

        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

        DWORD inputMode = ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
                          ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT;
        if (!SetConsoleMode(hStdin, inputMode)) {
            inputMode = ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
                        ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE;
            SetConsoleMode(hStdin, inputMode);
        }

        DWORD outputMode = 0;
        if (GetConsoleMode(hStdout, &outputMode)) {
            outputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
            SetConsoleMode(hStdout, outputMode);
        }
        FlushConsoleInputBuffer(hStdin);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        BOOL success = CreateProcessA(NULL, cmdBuffer, NULL, NULL, TRUE, 0, NULL, dir, &si, &pi);

        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_ELEVATION_REQUIRED) {
                SetConsoleCtrlHandler(SignalHandler::ConsoleCtrlHandler, TRUE);
                signalHandler.init();

                std::string executable;
                std::string arguments;
                if (!cmdLine.empty() && cmdLine[0] == '"') {
                    size_t closeQuote = cmdLine.find('"', 1);
                    if (closeQuote != std::string::npos) {
                        executable = cmdLine.substr(1, closeQuote - 1);
                        if (closeQuote + 1 < cmdLine.length()) {
                            arguments = cmdLine.substr(closeQuote + 1);
                            size_t argStart = arguments.find_first_not_of(' ');
                            if (argStart != std::string::npos) {
                                arguments = arguments.substr(argStart);
                            } else {
                                arguments.clear();
                            }
                        }
                    } else {
                        executable = cmdLine;
                    }
                } else {
                    size_t firstSpace = cmdLine.find(' ');
                    if (firstSpace != std::string::npos) {
                        executable = cmdLine.substr(0, firstSpace);
                        arguments = cmdLine.substr(firstSpace + 1);
                    } else {
                        executable = cmdLine;
                    }
                }

                SHELLEXECUTEINFOA sei = {0};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb = "runas";
                sei.lpFile = executable.c_str();
                sei.lpParameters = arguments.empty() ? NULL : arguments.c_str();
                sei.lpDirectory = dir;
                sei.nShow = SW_SHOWNORMAL;

                if (ShellExecuteExA(&sei)) {
                    if (wait && sei.hProcess) {
                        WaitForSingleObject(sei.hProcess, INFINITE);
                        DWORD code;
                        GetExitCodeProcess(sei.hProcess, &code);
                        CloseHandle(sei.hProcess);
                        return (int)code;
                    }
                    if (sei.hProcess) CloseHandle(sei.hProcess);
                    return 0;
                }
                std::cerr << "[ChildHandler] Elevation failed. Error: " << GetLastError() << "\n";
                return -1;
            }
            std::cerr << "[ChildHandler] Failed to create process: " << cmdLine << " Error: " << err << "\n";
            SetConsoleCtrlHandler(SignalHandler::ConsoleCtrlHandler, TRUE);
            signalHandler.init();
            return -1;
        }

        int exitCode = 0;
        if (wait) {
            g_procMgr.setForegroundPid(pi.dwProcessId);
            WaitForSingleObject(pi.hProcess, INFINITE);
            g_procMgr.clearForegroundPid();
            DWORD code;
            GetExitCodeProcess(pi.hProcess, &code);
            exitCode = (int)code;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        SetConsoleCtrlHandler(SignalHandler::ConsoleCtrlHandler, TRUE);
        signalHandler.init();

        return exitCode;
    }
  
};


#endif // LINUXIFY_CHILD_HANDLER_HPP
