// Compile: This is a header-only library, included by main.cpp
#ifndef LINUXIFY_CHILD_HANDLER_HPP
#define LINUXIFY_CHILD_HANDLER_HPP

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <iostream>
#include "../signal_handler.hpp"
#include "../process_manager.hpp"

class ChildHandler {
public:

    static int spawn(const std::string& cmdLine, const std::string& workDir, bool wait = true) {
 
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE hIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        HANDLE hOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        HANDLE hErr = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

        if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) {
             std::cerr << "[ChildHandler] Error: Failed to open console handles. Error: " << GetLastError() << "\n";
             return -1;
        }

        // 2. Configure Console Input (Standard Cooked Mode - Fixes Keys)
        // ENABLE_VIRTUAL_TERMINAL_INPUT removed to restore Arrow/Backspace in Cooked Mode.
        DWORD inputMode = ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | 
                          ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE;
        if (!SetConsoleMode(hIn, inputMode)) {
            std::cerr << "[ChildHandler] Warning: SetConsoleMode failed for Stdin. Error: " << GetLastError() << "\n";
        }

        // 3. Configure Console Output (VT Processing - Fixes Lag)
        // Enabling VT Processing allows fast ANSI rendering.
        DWORD outputMode = 0;
        if (GetConsoleMode(hOut, &outputMode)) {
            outputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
            SetConsoleMode(hOut, outputMode);
        }
        FlushConsoleInputBuffer(hIn);

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hIn;
        si.hStdOutput = hOut;
        si.hStdError = hErr;

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        // 3. Remove Shell's signal handler
        auto& signalHandler = SignalHandler::InputDispatcher::getInstance();
        SetConsoleCtrlHandler(SignalHandler::ConsoleCtrlHandler, FALSE);

        char cmdBuffer[8192];
        strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);

        const char* dir = workDir.empty() ? nullptr : workDir.c_str();

        // 4. Input Sanitization (Global "Dead Key" Fix) - MOVED TO INDIVIDUAL TOOLS (Lino/Funux)
        
        // Native CreateProcess call

        BOOL success = CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            dir,
            &si,
            &pi
        );
        
        CloseHandle(hIn);
        CloseHandle(hOut);
        CloseHandle(hErr);

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

        // 5. Restore Shell State
        SetConsoleCtrlHandler(SignalHandler::ConsoleCtrlHandler, TRUE);
        signalHandler.init(); // Restore Raw Mode

        return exitCode;
    }
  
};


#endif // LINUXIFY_CHILD_HANDLER_HPP
