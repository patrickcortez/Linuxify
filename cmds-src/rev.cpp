// Compile: g++ -std=c++17 -static -o ../cmds/rev.exe rev.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>

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

    std::vector<std::string> lines;
    
    if (args.size() > 1) {
        std::ifstream file(resolvePath(args[1]));
        if (!file) {
            printError("rev: cannot open '" + args[1] + "'");
            return 1;
        }
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
    } else {
        std::string line;
        while (std::getline(std::cin, line)) {
            lines.push_back(line);
        }
    }
    
    for (const auto& line : lines) {
        std::string reversed(line.rbegin(), line.rend());
        std::cout << reversed << "\n";
    }
    return 0;
}
