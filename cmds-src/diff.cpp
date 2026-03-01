// Compile: g++ -std=c++17 -static -o ../cmds/diff.exe diff.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
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

    if (args.size() < 3) {
        printError("diff: missing file operands");
        std::cout << "Usage: diff file1 file2\n";
        return 1;
    }
    
    bool unified = false;
    std::string file1Path, file2Path;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-u") unified = true;
        else if (file1Path.empty()) file1Path = args[i];
        else if (file2Path.empty()) file2Path = args[i];
    }
    
    std::ifstream f1(resolvePath(file1Path));
    std::ifstream f2(resolvePath(file2Path));
    
    if (!f1) {
        printError("diff: cannot open '" + file1Path + "'");
        return 1;
    }
    if (!f2) {
        printError("diff: cannot open '" + file2Path + "'");
        return 1;
    }
    
    std::vector<std::string> lines1, lines2;
    std::string line;
    while (std::getline(f1, line)) lines1.push_back(line);
    while (std::getline(f2, line)) lines2.push_back(line);
    
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (unified) {
        std::cout << "--- " << file1Path << "\n";
        std::cout << "+++ " << file2Path << "\n";
    }
    
    size_t i = 0, j = 0;
    while (i < lines1.size() || j < lines2.size()) {
        if (i >= lines1.size()) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "+ " << lines2[j++] << "\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        } else if (j >= lines2.size()) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << "- " << lines1[i++] << "\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        } else if (lines1[i] == lines2[j]) {
            if (unified) {
                std::cout << "  " << lines1[i] << "\n";
            }
            i++; j++;
        } else {
            bool foundMatch = false;
            for (size_t look = 1; look < 5 && !foundMatch; ++look) {
                if (j + look < lines2.size() && lines1[i] == lines2[j + look]) {
                    for (size_t k = 0; k < look; ++k) {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                        std::cout << "+ " << lines2[j++] << "\n";
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    }
                    foundMatch = true;
                } else if (i + look < lines1.size() && lines1[i + look] == lines2[j]) {
                    for (size_t k = 0; k < look; ++k) {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                        std::cout << "- " << lines1[i++] << "\n";
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    }
                    foundMatch = true;
                }
            }
            if (!foundMatch) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::cout << "- " << lines1[i++] << "\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "+ " << lines2[j++] << "\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            }
        }
    }
    return 0;
}
