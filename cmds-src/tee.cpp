// Compile: g++ -std=c++17 -static -o ../cmds/tee.exe tee.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
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

    bool appendMode = false;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-a") appendMode = true;
        else if (args[i][0] != '-') files.push_back(resolvePath(args[i]));
    }
    
    std::ostringstream oss;
    char buf[4096];
    while (std::cin.read(buf, sizeof(buf)) || std::cin.gcount() > 0) {
        oss.write(buf, std::cin.gcount());
    }
    std::string input = oss.str();
    
    std::cout << input;
    
    for (const auto& filePath : files) {
        std::ofstream file;
        if (appendMode) {
            file.open(filePath, std::ios::app);
        } else {
            file.open(filePath, std::ios::out);
        }
        if (file) {
            file << input;
        } else {
            printError("tee: cannot write to '" + filePath + "'");
        }
    }
    return 0;
}
