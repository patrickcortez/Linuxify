// g++ -O3 -o ../cmds/yes.exe yes.cpp
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "Linuxify: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int main(int argc, char* argv[]) {
    std::string text = "y";
    
    if (argc > 1) {
        if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            std::cout << "Usage: yes [STRING]...\n";
            std::cout << "Repeatedly output a line with all specified STRING(s), or 'y'.\n";
            return 0;
        }
        
        text = "";
        for (int i = 1; i < argc; ++i) {
            if (i > 1) text += " ";
            text += argv[i];
        }
    }

    // Fast output loop. Rely on standard CTRL+C handling (SIGINT) to terminate.
    while (true) {
        std::cout << text << "\n";
        // Optionally yield slightly if we want to avoid 100% CPU pinning
        // Sleep(1); 
    }

    return 0;
}
