// g++ -O3 -o ../cmds/whoami.exe whoami.cpp
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <lmcons.h>

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "Linuxify: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int main(int argc, char* argv[]) {
    // Basic argument parsing for help
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: whoami\n";
            std::cout << "Print the user name associated with the current effective user ID.\n";
            return 0;
        } else if (arg == "--version") {
            std::cout << "whoami (Linuxify) 1.0\n";
            return 0;
        }
    }

    if (argc > 1) {
        printError("whoami: extra operand '" + std::string(argv[1]) + "'");
        std::cerr << "Try 'whoami --help' for more information.\n";
        return 1;
    }

    char username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    if (GetUserNameA(username, &username_len)) {
        std::cout << username << "\n";
        return 0;
    } else {
        // Fallback to environment variable
        const char* envUsername = getenv("USERNAME");
        if (envUsername) {
            std::cout << envUsername << "\n";
            return 0;
        } else {
            printError("whoami: cannot find name for user ID");
            return 1;
        }
    }
}
