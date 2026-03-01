// Compile: g++ -std=c++17 -static -o ../cmds/chmod.exe chmod.cpp
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

    if (args.size() < 3) {
        printError("chmod: missing operand");
        std::cout << "Usage: chmod [-R] <mode> <file>..." << std::endl;
        return 1;
    }

    bool recursive = false;
    bool verbose = false;
    std::string mode;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-R" || arg == "--recursive") recursive = true;
        else if (arg == "-v" || arg == "--verbose") verbose = true;
        else if (mode.empty() && arg[0] != '-') mode = arg;
        else if (arg[0] != '-') files.push_back(arg);
    }

    if (mode.empty() || files.empty()) {
         printError("chmod: missing mode or file operand");
         return 1;
    }

    int exitCode = 0;
    auto applyMode = [&](const std::string& path, int& result) {
         DWORD attrs = GetFileAttributesA(path.c_str());
         if (attrs == INVALID_FILE_ATTRIBUTES) return;
         
         DWORD newAttrs = attrs;
         
         if (isdigit(mode[0])) {
             int m = mode[0] - '0';
             if ((m & 2) == 0) newAttrs |= FILE_ATTRIBUTE_READONLY;
             else newAttrs &= ~FILE_ATTRIBUTE_READONLY;
         } else {
             bool add = false;
             bool remove = false;
             
             for (char c : mode) {
                 if (c == '+') { add = true; remove = false; }
                 else if (c == '-') { add = false; remove = true; }
                 else if (c == 'w') {
                     if (add) newAttrs &= ~FILE_ATTRIBUTE_READONLY;
                     if (remove) newAttrs |= FILE_ATTRIBUTE_READONLY;
                 }
                 else if (c == 'h') {
                     if (add) newAttrs |= FILE_ATTRIBUTE_HIDDEN;
                     if (remove) newAttrs &= ~FILE_ATTRIBUTE_HIDDEN;
                 }
             }
         }
         
         if (newAttrs != attrs) {
             if (SetFileAttributesA(path.c_str(), newAttrs)) {
                 if (verbose) std::cout << "mode of '" << path << "' changed" << std::endl;
             } else {
                 printError("chmod: changing permissions of '" + path + "': failed");
                 result = 1;
             }
         }
    };

    for (const auto& file : files) {
         std::string root = resolvePath(file);
         if (!fs::exists(root)) {
             printError("chmod: cannot access '" + file + "': No such file or directory");
             exitCode = 1;
             continue;
         }
         
         applyMode(root, exitCode);
         
         if (recursive && fs::is_directory(root)) {
             try {
                 for (const auto& entry : fs::recursive_directory_iterator(root)) {
                     applyMode(entry.path().string(), exitCode);
                 }
             } catch(...) {}
         }
    }
    return exitCode;
}
