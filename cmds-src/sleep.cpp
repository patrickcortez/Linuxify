// g++ -O3 -o ../cmds/sleep.exe sleep.cpp
#include <iostream>
#include <string>
#include <windows.h>

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "Linuxify: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printError("sleep: missing operand");
        std::cerr << "Try 'sleep --help' for more information.\n";
        return 1;
    }

    if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        std::cout << "Usage: sleep NUMBER[SUFFIX]...\n";
        std::cout << "Pause for NUMBER seconds.  SUFFIX may be 's' for seconds (the default),\n";
        std::cout << "'m' for minutes, 'h' for hours or 'd' for days.\n";
        return 0;
    }

    double totalSeconds = 0.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        double num = 0.0;
        char suffix = 's'; // Default

        size_t lastDigitPos = arg.find_last_of("0123456789.");
        if (lastDigitPos != std::string::npos && lastDigitPos < arg.length() - 1) {
            suffix = arg[arg.length() - 1];
            arg = arg.substr(0, lastDigitPos + 1);
        }

        try {
            num = std::stod(arg);
        } catch (...) {
            printError("sleep: invalid time interval '" + std::string(argv[i]) + "'");
            return 1;
        }

        switch (suffix) {
            case 's': totalSeconds += num; break;
            case 'm': totalSeconds += num * 60; break;
            case 'h': totalSeconds += num * 3600; break;
            case 'd': totalSeconds += num * 86400; break;
            default:
                printError("sleep: invalid time interval '" + std::string(argv[i]) + "'");
                return 1;
        }
    }


    Sleep(static_cast<DWORD>(totalSeconds * 1000.0));

    return 0;
}
