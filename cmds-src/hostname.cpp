// g++ -O3 -o ../cmds/hostname.exe hostname.cpp
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
    if (argc > 1) {
        if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            std::cout << "Usage: hostname [NAME]\n";
            std::cout << "Show or set the system's host name.\n";
            return 0;
        }
        
        // Windows allows setting computer name, but requires reboot and admin privileges.
        // For standard hostname tool, if they pass an arg, try to set it.
        std::string newName = argv[1];
        if (!SetComputerNameA(newName.c_str())) {
            printError("hostname: you must be Administrator to change the hostname, and requires a reboot.");
            return 1;
        }
        std::cout << "Hostname changed to " << newName << " (Requires reboot to take full effect).\n";
        return 0;
    }

    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(computerName[0]);
    if (GetComputerNameA(computerName, &size)) {
        std::cout << computerName << "\n";
        return 0;
    } else {
        printError("hostname: failed to retrieve computer name");
        return 1;
    }
}
