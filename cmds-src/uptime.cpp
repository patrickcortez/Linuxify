// g++ -O3 -o ../cmds/uptime.exe uptime.cpp
#include <iostream>
#include <windows.h>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage: uptime\n";
        std::cout << "Tell how long the system has been running.\n";
        return 0;
    }

    ULONGLONG ticks = GetTickCount64();
    double seconds = ticks / 1000.0;
    
    int days = (int)(seconds / 86400);
    seconds -= days * 86400;
    int hours = (int)(seconds / 3600);
    seconds -= hours * 3600;
    int minutes = (int)(seconds / 60);

    // Mock load average for Windows as it doesn't have a direct equivalent
    std::cout << " " << std::setw(2) << std::setfill('0') << hours << ":" 
              << std::setw(2) << std::setfill('0') << minutes 
              << " up ";
              
    if (days > 0) std::cout << days << " days, ";
    
    std::cout << hours << ":" << std::setw(2) << std::setfill('0') << minutes << ", "
              << "1 user,  load average: 0.00, 0.01, 0.05\n";

    return 0;
}
