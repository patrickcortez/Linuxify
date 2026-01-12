// Compile: This is a header-only library, included by main.cpp
#ifndef LINUXIFY_CHILD_HANDLER_HPP
#define LINUXIFY_CHILD_HANDLER_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include "../signal_handler.hpp"
#include "../process_manager.hpp"

class ChildHandler {
public:
    // Spawns a child process with proper console mode handling
    // Returns exit code of the child process
    static int spawn(const std::string& cmdLine, const std::string& workDir, bool wait = true) {
        // 1. Explicitly open fresh Console Handles
        // This bypasses any potential pipe redirection the shell might have.
        HANDLE hConIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE hConErr = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

        if (hConIn == INVALID_HANDLE_VALUE || hConOut == INVALID_HANDLE_VALUE) {
             std::cerr << "Failed to open console handles.\n";
             return -1;
        }

        // 2. Make them inheritable
        SetHandleInformation(hConIn, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(hConOut, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        SetHandleInformation(hConErr, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hConIn;
        si.hStdOutput = hConOut;
        si.hStdError = hConErr;
        
        ZeroMemory(&pi, sizeof(pi));

        char cmdBuffer[8192];
        strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);

        const char* dir = workDir.empty() ? nullptr : workDir.c_str();

        // 3. Set Console Mode on OUR fresh handle
        prepareConsoleForChild(hConIn);

        BOOL success = CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,   // Inherit handles (our fresh CONIN$)
            0,
            NULL,
            dir,
            &si,
            &pi
        );

        // Cleanup
        CloseHandle(hConIn);
        CloseHandle(hConOut);
        CloseHandle(hConErr);

        if (!success) {
            restoreConsoleForShell();
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

        restoreConsoleForShell();

        return exitCode;
    }

private:
    // Sets console to standard "cooked" mode using valid CONIN$ handle
    static void prepareConsoleForChild(HANDLE hConIn) {
         if (hConIn != INVALID_HANDLE_VALUE) {
             // Retrieve existing mode (likely raw from our shell)
             DWORD oldMode;
             GetConsoleMode(hConIn, &oldMode);

             // Force cooked mode: Echo on, Line input on, Processed input (Ctrl+C) on
             // Also enable generic editing features typically found in shells
             DWORD cookedMode = ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE | ENABLE_QUICK_EDIT_MODE;
             
             SetConsoleMode(hConIn, cookedMode);
             
             // Flush input to ensure child starts fresh
             FlushConsoleInputBuffer(hConIn);
             
             // We do NOT close handle here as it is owned by spawn()
         }
    }

    // Sets console back to "raw" mode for the shell
    static void restoreConsoleForShell() {
        // First try the standard dispatcher restore
        SignalHandler::InputDispatcher::getInstance().init();
        
        // Failsafe: Ensure CONIN$ is explicitly set to Raw mode
        // This handles cases where GetStdHandle might have been redirected
        HANDLE hConIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hConIn != INVALID_HANDLE_VALUE) {
             DWORD rawMode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
             // Remove PROCESSED, LINE, ECHO to ensure raw input
             SetConsoleMode(hConIn, rawMode);
             CloseHandle(hConIn);
        }
    }
};

#endif // LINUXIFY_CHILD_HANDLER_HPP
