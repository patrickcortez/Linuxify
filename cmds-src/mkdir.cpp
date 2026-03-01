// Compile: g++ -std=c++17 -static -o ../cmds/mkdir.exe mkdir.cpp
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

    bool parents = false;
    bool verbose = false;
    std::vector<std::string> dirs;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-p" || args[i] == "--parents") parents = true;
        else if (args[i] == "-v" || args[i] == "--verbose") verbose = true;
        else if (args[i][0] != '-') dirs.push_back(args[i]);
    }
    
    if (dirs.empty()) {
        printError("mkdir: missing operand");
        return 1;
    }

    int exitCode = 0;
    for (const auto& dir : dirs) {
        try {
            std::string fullPath = resolvePath(dir);
            bool created = false;
            if (parents) created = fs::create_directories(fullPath);
            else created = fs::create_directory(fullPath);
            
            if (verbose && created) std::cout << "mkdir: created directory '" << dir << "'" << std::endl;
        } catch (const std::exception& e) {
            printError("mkdir: cannot create directory '" + dir + "': " + e.what());
            exitCode = 1;
        }
    }
    return exitCode;
}
