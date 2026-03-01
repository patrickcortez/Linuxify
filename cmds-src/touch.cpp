// Compile: g++ -std=c++17 -static -o ../cmds/touch.exe touch.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
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

    bool noCreate = false;
    bool updateAccess = false;
    bool updateMod = false;
    std::string refFile;
    std::string dateStr;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-c" || arg == "--no-create") noCreate = true;
        else if (arg == "-a") updateAccess = true;
        else if (arg == "-m") updateMod = true;
        else if (arg == "-r" && i + 1 < args.size()) refFile = args[++i];
        else if (arg == "-t" && i + 1 < args.size()) dateStr = args[++i];
        else if (arg[0] != '-') files.push_back(arg);
    }

    if (!updateAccess && !updateMod) {
        updateAccess = updateMod = true;
    }

    if (files.empty()) {
        printError("touch: missing file operand");
        return 1;
    }

    FILETIME ft = {0, 0};
    SYSTEMTIME st = {0};
    GetSystemTime(&st);
    
    if (!dateStr.empty()) {
        int year = st.wYear;
        int month = st.wMonth;
        int day = st.wDay;
        int hour = st.wHour;
        int min = st.wMinute;
        int sec = 0;
        
        size_t dotPos = dateStr.find('.');
        if (dotPos != std::string::npos) {
            if (dotPos + 1 < dateStr.length()) {
                try { sec = std::stoi(dateStr.substr(dotPos + 1)); } catch(...) {}
            }
            dateStr = dateStr.substr(0, dotPos);
        }
        
        bool valid = true;
        try {
            if (dateStr.length() == 8) {
                month = std::stoi(dateStr.substr(0, 2));
                day = std::stoi(dateStr.substr(2, 2));
                hour = std::stoi(dateStr.substr(4, 2));
                min = std::stoi(dateStr.substr(6, 2));
            } else if (dateStr.length() == 10) {
                int yy = std::stoi(dateStr.substr(0, 2));
                year = (yy < 69) ? (2000 + yy) : (1900 + yy); 
                month = std::stoi(dateStr.substr(2, 2));
                day = std::stoi(dateStr.substr(4, 2));
                hour = std::stoi(dateStr.substr(6, 2));
                min = std::stoi(dateStr.substr(8, 2));
            } else if (dateStr.length() == 12) {
                year = std::stoi(dateStr.substr(0, 4));
                month = std::stoi(dateStr.substr(4, 2));
                day = std::stoi(dateStr.substr(6, 2));
                hour = std::stoi(dateStr.substr(8, 2));
                min = std::stoi(dateStr.substr(10, 2));
            } else {
                valid = false;
            }
        } catch(...) { valid = false; }
        
        if (valid) {
            st.wYear = year;
            st.wMonth = month;
            st.wDay = day;
            st.wHour = hour;
            st.wMinute = min;
            st.wSecond = sec;
            st.wMilliseconds = 0;
            SystemTimeToFileTime(&st, &ft);
        } else {
            printError("touch: invalid date format '" + dateStr + "'");
            return 1;
        }
    } else if (!refFile.empty()) {
        HANDLE hRef = CreateFileA(resolvePath(refFile).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hRef != INVALID_HANDLE_VALUE) {
            GetFileTime(hRef, NULL, NULL, &ft);
            CloseHandle(hRef);
        } else {
            printError("touch: failed to get attributes of '" + refFile + "'");
            return 1;
        }
    } else {
        SystemTimeToFileTime(&st, &ft);
    }

    int exitCode = 0;
    for (const auto& file : files) {
        std::string fullPath = resolvePath(file);
        
        if (!fs::exists(fullPath)) {
            if (noCreate) continue;
            HANDLE h = CreateFileA(fullPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
            } else {
                printError("touch: cannot touch '" + file + "': " + std::to_string(GetLastError()));
                exitCode = 1;
                continue;
            }
        }

        HANDLE hFile = CreateFileA(fullPath.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            FILETIME* pAccess = updateAccess ? &ft : NULL;
            FILETIME* pWrite = updateMod ? &ft : NULL;
            if (!SetFileTime(hFile, NULL, pAccess, pWrite)) {
                 printError("touch: setting times of '" + file + "': " + std::to_string(GetLastError()));
                 exitCode = 1;
            }
            CloseHandle(hFile);
        } else {
            printError("touch: cannot touch '" + file + "'");
            exitCode = 1;
        }
    }
    return exitCode;
}
