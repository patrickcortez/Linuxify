// Compile: g++ -std=c++17 -static -o ../cmds/tree.exe tree.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <iomanip>
#include <regex>
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

    std::string path = ".";
    int maxDepth = -1;
    bool dirsOnly = false;
    bool showHidden = false;
    bool showSize = false;
    bool humanReadable = false;
    bool showPermissions = false;
    bool fullPath = false;
    bool noReport = false;
    bool showDu = false;
    std::string ignorePattern;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-L" && i + 1 < args.size()) {
            try { maxDepth = std::stoi(args[++i]); } catch (...) {}
        } else if (args[i] == "-d") {
            dirsOnly = true;
        } else if (args[i] == "-a") {
            showHidden = true;
        } else if (args[i] == "-s") {
            showSize = true;
        } else if (args[i] == "-h") {
            humanReadable = true;
        } else if (args[i] == "-p") {
            showPermissions = true;
        } else if (args[i] == "-f") {
            fullPath = true;
        } else if (args[i] == "--noreport") {
            noReport = true;
        } else if (args[i] == "--du") {
            showDu = true;
            showSize = true;
        } else if ((args[i] == "-I" || args[i] == "--ignore") && i + 1 < args.size()) {
            ignorePattern = args[++i];
        } else if (args[i][0] != '-') {
            path = args[i];
        }
    }
    
    std::string rootPath = resolvePath(path);
    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        printError("tree: '" + path + "' is not a directory");
        return 1;
    }
    
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    std::cout << (fullPath ? rootPath : path) << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    
    int dirCount = 0, fileCount = 0;
    uintmax_t totalSize = 0;

    auto formatSize = [](uintmax_t bytes, bool human) -> std::string {
        if (!human) return std::to_string(bytes);
        const char* units[] = {"B", "K", "M", "G", "T"};
        int u = 0;
        double size = (double)bytes;
        while (size >= 1024 && u < 4) { size /= 1024; u++; }
        char buf[32];
        if (u == 0) snprintf(buf, sizeof(buf), "%4d%s", (int)size, units[u]);
        else snprintf(buf, sizeof(buf), "%4.1f%s", size, units[u]);
        return buf;
    };

    std::function<uintmax_t(const fs::path&, const std::string&, int)> printTree;
    printTree = [&](const fs::path& p, const std::string& prefix, int depth) -> uintmax_t {
        if (maxDepth >= 0 && depth >= maxDepth) return 0;
        
        std::vector<fs::directory_entry> entries;
        try {
            for (const auto& entry : fs::directory_iterator(p)) {
                std::string name = entry.path().filename().string();
                if (!showHidden && name[0] == '.') continue;
                if (!ignorePattern.empty()) {
                    try {
                        std::regex re(ignorePattern);
                        if (std::regex_search(name, re)) continue;
                    } catch (...) {
                        if (name.find(ignorePattern) != std::string::npos) continue;
                    }
                }
                if (dirsOnly && !entry.is_directory()) continue;
                entries.push_back(entry);
            }
        } catch (...) {
            return 0;
        }
        
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename() < b.path().filename();
        });
        
        uintmax_t dirSize = 0;
        
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            bool isLast = (i == entries.size() - 1);
            
            std::cout << prefix << (isLast ? "`-- " : "|-- ");
            
            uintmax_t entrySize = 0;
            if (entry.is_directory()) {
                if (showDu) {
                    entrySize = printTree(entry.path(), prefix + (isLast ? "    " : "|   "), depth + 1);
                }
            } else {
                try { entrySize = fs::file_size(entry.path()); } catch (...) {}
            }
            
            if (showPermissions) {
                DWORD attrs = GetFileAttributesA(entry.path().string().c_str());
                std::cout << "[" << (attrs & FILE_ATTRIBUTE_DIRECTORY ? "d" : "-")
                          << (attrs & FILE_ATTRIBUTE_READONLY ? "r-" : "rw")
                          << (attrs & FILE_ATTRIBUTE_HIDDEN ? "h" : "-") << "] ";
            }
            
            if (showSize) {
                std::cout << "[" << std::setw(8) << formatSize(entrySize, humanReadable) << "]  ";
            }
            
            if (entry.is_directory()) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                std::cout << (fullPath ? entry.path().string() : entry.path().filename().string()) << "\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                dirCount++;
                if (!showDu) {
                    printTree(entry.path(), prefix + (isLast ? "    " : "|   "), depth + 1);
                }
                dirSize += entrySize;
            } else {
                std::cout << (fullPath ? entry.path().string() : entry.path().filename().string()) << "\n";
                fileCount++;
                dirSize += entrySize;
                totalSize += entrySize;
            }
        }
        return dirSize;
    };
    
    printTree(rootPath, "", 0);
    
    if (!noReport) {
        std::cout << "\n" << dirCount << " directories";
        if (!dirsOnly) {
            std::cout << ", " << fileCount << " files";
            if (showSize) {
                std::cout << " (" << formatSize(totalSize, humanReadable) << " total)";
            }
        }
        std::cout << "\n";
    }
    return 0;
}
