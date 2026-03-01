// g++ -O3 -o ../cmds/cal.exe cal.cpp
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <time.h>

int main(int argc, char* argv[]) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage: cal\n";
        std::cout << "Display a calendar of the current month.\n";
        return 0;
    }

    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    int current_day = timeinfo->tm_mday;
    int current_month = timeinfo->tm_mon;
    int current_year = timeinfo->tm_year + 1900;

    // Find first day of month
    timeinfo->tm_mday = 1;
    mktime(timeinfo);
    int first_wday = timeinfo->tm_wday; // 0 = Sunday

    const char* months[] = {"January", "February", "March", "April", "May", "June", 
                            "July", "August", "September", "October", "November", "December"};
    
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // Leap year check
    if ((current_year % 4 == 0 && current_year % 100 != 0) || (current_year % 400 == 0)) {
        days_in_month[1] = 29;
    }

    std::cout << "    " << months[current_month] << " " << current_year << "\n";
    std::cout << "Su Mo Tu We Th Fr Sa\n";

    for (int i = 0; i < first_wday; ++i) {
        std::cout << "   ";
    }

    for (int day = 1; day <= days_in_month[current_month]; ++day) {
        if (day == current_day) {
            // Highlight current day if terminal supports it (simple reverse video or just brackets)
            std::cout << "\033[7m" << std::setw(2) << day << "\033[0m ";
        } else {
            std::cout << std::setw(2) << day << " ";
        }
        
        if ((first_wday + day) % 7 == 0) {
            std::cout << "\n";
        }
    }
    std::cout << "\n";
    
    return 0;
}
