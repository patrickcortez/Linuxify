// Compile: g++ -std=c++17 -static -o ../cmds/ls.exe ls.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <windows.h>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

std::string resolvePath(const std::string& path) {
    if (path.empty()) {
        return fs::current_path().string();
    }
    fs::path p(path);
    if (p.is_absolute()) {
        try {
            return fs::canonical(p).string();
        } catch (...) {
            return p.string();
        }
    }
    fs::path fullPath = fs::current_path() / path;
    try {
        return fs::canonical(fullPath).string();
    } catch (...) {
        return fullPath.string();
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    bool showAll = false;
    bool longFormat = false;
    bool recursive = false;
    bool humanReadable = false;
    bool reverse = false;
    bool timeSort = false;
    bool sizeSort = false;
    bool color = true;
    bool oneColumn = false;
    std::vector<std::string> paths;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-a" || arg == "--all") showAll = true;
        else if (arg == "-l") longFormat = true;
        else if (arg == "-R" || arg == "--recursive") recursive = true;
        else if (arg == "-h" || arg == "--human-readable") humanReadable = true;
        else if (arg == "-r" || arg == "--reverse") reverse = true;
        else if (arg == "-t") timeSort = true;
        else if (arg == "-S") sizeSort = true;
        else if (arg == "-1") oneColumn = true;
        else if (arg == "--color=never") color = false;
        else if (arg == "--color=auto" || arg == "--color=always") color = true;
        else if (arg.length() > 1 && arg[0] == '-') {
            for (size_t k = 1; k < arg.length(); ++k) {
                if (arg[k] == 'a') showAll = true;
                else if (arg[k] == 'l') longFormat = true;
                else if (arg[k] == 'R') recursive = true;
                else if (arg[k] == 'h') humanReadable = true;
                else if (arg[k] == 'r') reverse = true;
                else if (arg[k] == 't') timeSort = true;
                else if (arg[k] == 'S') sizeSort = true;
                else if (arg[k] == '1') oneColumn = true;
            }
        }
        else paths.push_back(arg);
    }

    if (paths.empty()) paths.push_back(fs::current_path().string());

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int termWidth = csbi.dwSize.X;
    if (termWidth <= 0) termWidth = 80;

    auto formatSize = [&](uintmax_t size) -> std::string {
        if (!humanReadable) return std::to_string(size);
        const char* units[] = {"B", "K", "M", "G", "T"};
        int unit = 0;
        double s = static_cast<double>(size);
        while (s >= 1024 && unit < 4) {
            s /= 1024;
            unit++;
        }
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << s << units[unit];
        return ss.str();
    };

    auto printEntryLong = [&](const fs::directory_entry& entry) {
         std::string perms;
         if (fs::is_directory(entry)) perms = "d";
         else if (fs::is_symlink(entry)) perms = "l";
         else perms = "-";
         
         try {
             auto status = fs::status(entry);
             auto p = status.permissions();
             perms += (p & fs::perms::owner_read) != fs::perms::none ? "r" : "-";
             perms += (p & fs::perms::owner_write) != fs::perms::none ? "w" : "-";
             perms += (p & fs::perms::owner_exec) != fs::perms::none ? "x" : "-";
             perms += (p & fs::perms::group_read) != fs::perms::none ? "r" : "-";
             perms += (p & fs::perms::group_write) != fs::perms::none ? "w" : "-";
             perms += (p & fs::perms::group_exec) != fs::perms::none ? "x" : "-";
             perms += (p & fs::perms::others_read) != fs::perms::none ? "r" : "-";
             perms += (p & fs::perms::others_write) != fs::perms::none ? "w" : "-";
             perms += (p & fs::perms::others_exec) != fs::perms::none ? "x" : "-";
         } catch(...) { perms += "---------"; }

         uintmax_t size = 0;
         if (!fs::is_directory(entry)) {
             try { size = fs::file_size(entry); } catch(...) {}
         }
         
         char timeBuf[64] = "Unknown";
         try {
             auto ftime = fs::last_write_time(entry);
             auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                 ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
             std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
             std::tm* tm = std::localtime(&cftime);
             if (tm) std::strftime(timeBuf, sizeof(timeBuf), "%b %d %H:%M", tm);
         } catch(...) {}

         std::cout << perms << " " << std::setw(humanReadable ? 6 : 10) << formatSize(size) << " " << timeBuf << " ";

         if (color) {
             if (fs::is_directory(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
             else if (fs::is_symlink(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
             else {
                 std::string ext = entry.path().extension().string();
                 std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                 if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".sh") 
                     SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                 else if (ext == ".zip" || ext == ".tar" || ext == ".gz")
                     SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                 else if (ext == ".jpg" || ext == ".png" || ext == ".bmp")
                     SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
             }
         }
         
         std::cout << entry.path().filename().string();
         if (color) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
         std::cout << std::endl;
    };

    auto printEntriesColumnar = [&](const std::vector<fs::directory_entry>& entries) {
        if (entries.empty()) return;

        size_t maxLen = 0;
        for (const auto& entry : entries) {
            size_t len = entry.path().filename().string().length();
            if (len > maxLen) maxLen = len;
        }
        
        int colWidth = (int)maxLen + 2;
        if (colWidth < 1) colWidth = 1;
        int numCols = termWidth / colWidth;
        if (numCols < 1) numCols = 1;

        int col = 0;
        for (const auto& entry : entries) {
            std::string name = entry.path().filename().string();
            
            if (color) {
                if (fs::is_directory(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                else if (fs::is_symlink(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                else {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".sh") 
                        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    else if (ext == ".zip" || ext == ".tar" || ext == ".gz")
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                    else if (ext == ".jpg" || ext == ".png" || ext == ".bmp")
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    else
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
            }
            
            std::cout << name;
            if (color) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            col++;
            if (col >= numCols) {
                std::cout << std::endl;
                col = 0;
            } else {
                int padding = colWidth - (int)name.length();
                for (int i = 0; i < padding; i++) std::cout << ' ';
            }
        }
        if (col != 0) std::cout << std::endl;
    };

    auto listDir = [&](auto&& self, const std::string& p) -> void {
         if (!fs::exists(p)) {
             printError("ls: cannot access '" + p + "': No such file or directory");
             return;
         }
         if (!fs::is_directory(p)) {
             fs::directory_entry entry(p);
             if (longFormat) {
                 printEntryLong(entry);
             } else {
                 std::vector<fs::directory_entry> single = {entry};
                 printEntriesColumnar(single);
             }
             return;
         }
         
         if (recursive && paths.size() > 1) std::cout << p << ":" << std::endl;
         
         std::vector<fs::directory_entry> entries;
         try {
             for (const auto& entry : fs::directory_iterator(p)) {
                 std::string name = entry.path().filename().string();
                 if (!showAll && name[0] == '.') continue;
                 entries.push_back(entry);
             }
         } catch (const std::exception& e) {
             printError("ls: " + std::string(e.what()));
             return;
         }
         
         std::sort(entries.begin(), entries.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
             if (timeSort) return fs::last_write_time(a) > fs::last_write_time(b);
             if (sizeSort) {
                 uintmax_t sa = 0, sb = 0;
                 if (!fs::is_directory(a)) sa = fs::file_size(a);
                 if (!fs::is_directory(b)) sb = fs::file_size(b);
                 return sa > sb;
             }
             return a.path().filename().string() < b.path().filename().string();
         });
         
         if (reverse) std::reverse(entries.begin(), entries.end());
         
         if (longFormat) {
             for (const auto& entry : entries) {
                 printEntryLong(entry);
             }
         } else if (oneColumn) {
             for (const auto& entry : entries) {
                 if (color) {
                     if (fs::is_directory(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                     else if (fs::is_symlink(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                     else {
                         std::string ext = entry.path().extension().string();
                         std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                         if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".sh") 
                             SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                         else if (ext == ".zip" || ext == ".tar" || ext == ".gz")
                             SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                         else if (ext == ".jpg" || ext == ".png" || ext == ".bmp")
                             SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                         else
                             SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                     }
                 }
                 std::cout << entry.path().filename().string();
                 if (color) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                 std::cout << std::endl;
             }
         } else {
             printEntriesColumnar(entries);
         }
         
         if (recursive) {
             for (const auto& entry : entries) {
                 if (fs::is_directory(entry)) {
                     std::string name = entry.path().filename().string();
                     if (name != "." && name != "..") {
                         std::cout << std::endl << entry.path().string() << ":" << std::endl;
                         self(self, entry.path().string());
                     }
                 }
             }
         }
    };

    for (const auto& path : paths) {
         listDir(listDir, resolvePath(path));
    }
    return 0;
}
