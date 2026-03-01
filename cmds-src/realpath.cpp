// Compile: g++ -std=c++17 -static -o ../cmds/realpath.exe realpath.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

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

    if (args.size() < 2) {
        printError("realpath: missing file operand");
        return 1;
    }
    
    int exitCode = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i][0] == '-') continue;
        
        std::string filePath = resolvePath(args[i]);
        
        try {
            if (fs::exists(filePath)) {
                std::cout << fs::canonical(filePath).string() << "\n";
            } else {
                std::cout << fs::absolute(filePath).string() << "\n";
                exitCode = 1;
            }
        } catch (const std::exception& e) {
            std::cout << fs::absolute(filePath).string() << "\n";
            exitCode = 1;
        }
    }
    return exitCode;
}
