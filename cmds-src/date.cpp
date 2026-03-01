// g++ -O3 -o ../cmds/date.exe date.cpp
#include <iostream>
#include <string>
#include <windows.h>
#include <time.h>
#include <iomanip>
#include <sstream>

int main(int argc, char* argv[]) {
    // Handling simple display of current date/time
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: date [OPTION]... [+FORMAT]\n";
            std::cout << "Display the current time in the given FORMAT.\n";
            return 0;
        }
    }

    time_t now = time(0);
    struct tm* tstruct = localtime(&now);

    // Default output format
    // e.g. Wed Mar  5 14:35:00 UTC 2025
    char buf[80];
    strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Z %Y", tstruct);

    std::cout << buf << "\n";
    return 0;
}
