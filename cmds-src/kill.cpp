// g++ -O3 -o ../cmds/kill.exe kill.cpp -lpsapi
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <windows.h>

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "Linuxify: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int main(int argc, char* argv[]) {
    // Basic argument parsing
    if (argc < 2) {
        printError("kill: usage: kill [-s sigspec | -n signum | -sigspec] pid | jobspec ... or kill -l [sigspec]");
        return 1;
    }

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    if (args[0] == "-l" || args[0] == "--list") {
        std::cout << " 1) SIGHUP   2) SIGINT   3) SIGQUIT  4) SIGILL   5) SIGTRAP\n";
        std::cout << " 6) SIGABRT  7) SIGBUS   8) SIGFPE   9) SIGKILL 10) SIGUSR1\n";
        std::cout << "11) SIGSEGV 12) SIGUSR2 13) SIGPIPE 14) SIGALRM 15) SIGTERM\n";
        return 0;
    }

    std::string signalStr = "-TERM";
    std::vector<std::string> targetPids;

    for (const auto& arg : args) {
        if (arg[0] == '-') {
            if (arg == "-9" || arg == "-KILL" || arg == "-s KILL") {
                signalStr = "-KILL";
            } else if (arg == "-15" || arg == "-TERM") {
                signalStr = "-TERM";
            } else if (arg == "-2" || arg == "-INT") {
                signalStr = "-INT";
            } else {
                signalStr = arg; // Capture other signals even if we don't handle them specifically
            }
        } else {
            targetPids.push_back(arg);
        }
    }

    if (targetPids.empty()) {
        printError("kill: usage: kill [-s sigspec | -n signum | -sigspec] pid | jobspec ... or kill -l [sigspec]");
        return 1;
    }

    int exitCode = 0;
    for (const auto& target : targetPids) {
        if (target[0] == '%') {
            printError("kill: job control syntax ('%') is handled natively by the shell. Use direct PID or execute 'kill' from within Linuxify.");
            exitCode = 1;
            continue;
        }

        try {
            DWORD pid = std::stoul(target);
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProcess == NULL) {
                printError("kill: (" + target + ") - No such process or access denied");
                exitCode = 1;
                continue;
            }

            // In Windows, we generally just terminate for most signals unless we inject threads
            // For now, TerminateProcess handles most "kill" requests
            UINT uExitCode = (signalStr == "-KILL" || signalStr == "-9") ? 9 : 15;
            if (!TerminateProcess(hProcess, uExitCode)) {
                printError("kill: (" + target + ") - Failed to terminate process");
                exitCode = 1;
            } else {
                // Determine signal name for output
                std::string sigName = "SIGTERM";
                if (signalStr == "-KILL" || signalStr == "-9") sigName = "SIGKILL";
                else if (signalStr == "-INT" || signalStr == "-2") sigName = "SIGINT";
                else sigName = signalStr.length() > 1 ? "SIG" + signalStr.substr(1) : "SIGTERM";
                
                std::cout << "Sent " << sigName << " to process " << pid << "\n";
            }
            CloseHandle(hProcess);

        } catch (...) {
            printError("kill: " + target + ": arguments must be process or job IDs");
            exitCode = 1;
        }
    }

    return exitCode;
}
