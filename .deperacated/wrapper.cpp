#include <windows.h>
#include <iostream>
#include <string>

// linuxify_wrapper.exe <command_line>
// This micro-shell ensures a clean, standard Windows Console environment for the child.
int main(int argc, char* argv[]) {
    // We need at least the executable and one argument (the target command)
    // Actually, check raw command line effectively.
    
    // 1. Reconstruct Command Line (Robust)
    // We use GetCommandLineA() to preserve quotes and spacing exactly as passed.
    char* fullCmd = GetCommandLineA();
    
    // Skip the first token (the wrapper executable itself)
    bool inQuote = false;
    char* targetCmd = fullCmd;
    
    while (*targetCmd) {
        if (*targetCmd == '"') {
            inQuote = !inQuote;
        } else if (!inQuote && (*targetCmd == ' ' || *targetCmd == '\t')) {
            targetCmd++; // Skip the space/tab
            break; 
        }
        targetCmd++;
    }
    
    // Skip any leading whitespace before the actual args
    while (*targetCmd && (*targetCmd == ' ' || *targetCmd == '\t')) {
        targetCmd++;
    }

    if (*targetCmd == '\0') {
        // No arguments passed
        return 0;
    }

    // 2. Spawn the Target Process with "Clean" Inheritance
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // CRITICAL: We pass bInheritHandles = TRUE.
    // Since wrapper.exe was launched by Linuxify (which fixed the handles),
    // wrapper.exe has VALID, inheritable standard handles (stdin/out/err).
    // By invoking CreateProcess with TRUE and DEFAULT StartupInfo,
    // Windows automatically duplicates these valid handles to the child.
    // This creates the most "Native" environment possible, identical to cmd.exe launching a process.
    
    if (!CreateProcessA(
        NULL, 
        targetCmd,   // The raw command line we extracted
        NULL, 
        NULL, 
        TRUE,        // Inherit Handles!
        0,           // No special flags (Let it be a normal console process)
        NULL,        // Inherit Env
        NULL,        // Inherit CWD
        &si, 
        &pi
    )) {
        std::cerr << "[Wrapper] Failed to launch: " << targetCmd << " Error: " << GetLastError() << "\n";
        return 1;
    }

    // Wait for child to finish
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)exitCode;
}
