// g++ -O3 -o ../cmds/sig.exe sig.cpp -lpsapi
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "sig: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int main(int argc, char* argv[]) {
    // sig <SIGNAL> <PID>
    if (argc < 3) {
        printError("usage: sig <SIGNAL_NAME> <PID>");
        return 1;
    }

    std::string signalName = argv[1];
    std::string pidStr = argv[2];
    
    // Convert PID to integer
    DWORD pid = 0;
    try {
        pid = std::stoul(pidStr);
    } catch (...) {
        printError("invalid PID format: " + pidStr);
        return 1;
    }

    // Direct termination for fatal standard signals
    if (signalName == "SIGKILL" || signalName == "KILL" || signalName == "9") {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess == NULL) {
            printError("(" + pidStr + ") - No such process or access denied");
            return 1;
        }
        if (!TerminateProcess(hProcess, 9)) {
            printError("(" + pidStr + ") - Failed to terminate process");
            CloseHandle(hProcess);
            return 1;
        }
        std::cout << "Sent SIGKILL to process " << pid << "\n";
        CloseHandle(hProcess);
        return 0;
    }

    // Try to send via IPC named pipe
    std::string pipeName = "\\\\.\\pipe\\linuxify_signals_" + std::to_string(pid);

    // Wait slightly if the pipe is busy
    if (!WaitNamedPipeA(pipeName.c_str(), 1000)) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            printError("Process " + pidStr + " is not accepting custom signals (pipe not found).");
        } else {
            printError("Failed to connect to process " + pidStr + " pipe (Error " + std::to_string(err) + ")");
        }
        return 1;
    }

    HANDLE hPipe = CreateFileA(
        pipeName.c_str(),
        GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        printError("Failed to open pipe for process " + pidStr + " (Error " + std::to_string(GetLastError()) + ")");
        return 1;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

    DWORD bytesWritten;
    // Add newline or padding if helpful, but standard string is fine
    if (!WriteFile(hPipe, signalName.c_str(), signalName.length(), &bytesWritten, NULL)) {
        printError("Failed to write signal to process " + pidStr);
        CloseHandle(hPipe);
        return 1;
    }

    std::cout << "Sent " << signalName << " to process " << pid << "\n";
    CloseHandle(hPipe);
    return 0;
}
