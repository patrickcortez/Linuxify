
// Compile: g++ -std=c++17 -static -o type.exe type.cpp -lshlwapi
 

#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <filesystem>
#include <algorithm>
#include <windows.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

// Dynamic Builtin Lookup - Single Source of Truth via Environment
std::set<std::string> getBuiltins() {
    std::set<std::string> builtins;
    const char* envBuf = getenv("LINUXIFY_BUILTINS");
    if (envBuf) {
        std::string s(envBuf);
        
        std::string delimiter = ";"; 
        if (s.find(',') != std::string::npos) delimiter = ",";
        
        size_t pos = 0;
        std::string token;
        while ((pos = s.find(delimiter)) != std::string::npos) {
            token = s.substr(0, pos);
            if (!token.empty()) builtins.insert(token);
            s.erase(0, pos + delimiter.length());
        }
        if (!s.empty()) builtins.insert(s);
    }
    
    if (builtins.empty()) {
        builtins = {
            "echo", "pwd", "cd", "ls", "dir", "type", "mkdir", "rm", "rmdir",
            "mv", "cp", "touch", "chmod", "chown", "clear", "env", "export",
            "which", "whoami", "ps", "kill", "history", "grep", "head", "tail", "wc",
            "sort", "uniq", "find", "cut", "tr", "sed", "awk", "diff", "tee", "xargs",
            "rev", "ln", "stat", "file", "readlink", "realpath", "basename", "dirname",
            "tree", "du", "lin", "top", "jobs", "fg", "bg", "less", "more", "uninstall",
            "setup", "alias", "unalias", "source", "read", "test", "true", "false",
            "exit", "help", "man", "date", "cal", "uname", "hostname", "uptime",
            "free", "df", "mount", "umount", "sleep", "printf", "seq", "yes",
            "fuzz"
        };
    }
    return builtins;
}

struct Options {
    bool showAll = false;
    bool typeOnly = false;
};

std::string getExecutablePath() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return std::string(path);
}

// Helper to check PATH for external commands
std::vector<std::string> findAllInPath(const std::string& cmd) {
    std::vector<std::string> results;
    std::string pathEnv_ = "";
    const char* pathBuf = getenv("PATH");
    if (pathBuf) {
        pathEnv_ = pathBuf;
    }

    std::string pathEnv = pathEnv_;
    size_t pos = 0;
    std::string token;
    
    while ((pos = pathEnv.find(';')) != std::string::npos) {
        token = pathEnv.substr(0, pos);
        if (!token.empty()) {
            if (token.back() != '\\') token += "\\";
            
            std::vector<std::string> exts = {".exe", ".bat", ".cmd", ".com", ""};
            for (const auto& ext : exts) {
                std::string fullPath = token + cmd + ext;
                if (fs::exists(fullPath) && !fs::is_directory(fullPath)) {
                    try {
                        fullPath = fs::canonical(fullPath).string();
                    } catch(...) {}
                    
                    bool found = false;
                    for (const auto& r : results) {
                        if (r == fullPath) { found = true; break; }
                    }
                    if (!found) results.push_back(fullPath);
                }
            }
        }
        pathEnv.erase(0, pos + 1);
    }
    return results;
}

void checkCommand(const std::string& cmd, const Options& opts) {
    bool foundAny = false;
    
    // Dynamic lookup
    std::set<std::string> builtins = getBuiltins();

    // 1. Check Built-in
    if (builtins.count(cmd)) {
        foundAny = true;
        if (opts.typeOnly) {
            std::cout << "builtin" << std::endl;
        } else if (!opts.pathOnly) {
             std::cout << cmd << " is a shell builtin" << std::endl;
        }
        if (!opts.showAll) return;
    }

    // 2. Check Internal (cmds/ folder)
    std::string selfPath = getExecutablePath();
    fs::path exeDir = fs::path(selfPath).parent_path();
    
    fs::path cmdsDir = exeDir;
    if (exeDir.filename() == "cmds") {
        cmdsDir = exeDir;
    } else if (fs::exists(exeDir / "cmds")) {
        cmdsDir = exeDir / "cmds";
    }

    std::vector<std::string> extensions = {".exe", ".bat", ".cmd", ""};
    for (const auto& ext : extensions) {
         fs::path checkPath = cmdsDir / (cmd + ext);
         if (fs::exists(checkPath) && !fs::is_directory(checkPath)) {
            foundAny = true;
            std::string fullPath = checkPath.string();
            try { fullPath = fs::canonical(checkPath).string(); } catch(...) {}

            if (opts.typeOnly) {
                std::cout << "file" << std::endl;
            } else {
                std::cout << cmd << " is an internal command" << std::endl;
            }
            
            if (!opts.showAll) return;
            break; 
         }
    }

    // 3. Check External (PATH)
    if (opts.showAll) {
         auto paths = findAllInPath(cmd);
         for (const auto& p : paths) {
             foundAny = true;
             if (opts.typeOnly) {
                 std::cout << "file" << std::endl;
             } else {
                 std::cout << cmd << " is an external command" << std::endl;
             }
         }
    } else {
         char buffer[MAX_PATH];
         char* filePart;
         if (SearchPathA(NULL, cmd.c_str(), ".exe", MAX_PATH, buffer, &filePart) ||
             SearchPathA(NULL, cmd.c_str(), NULL, MAX_PATH, buffer, &filePart)) {
             
             foundAny = true;
             if (opts.typeOnly) {
                 std::cout << "file" << std::endl;
             } else {
                 std::cout << cmd << " is an external command" << std::endl;
             }
         }
    }

    if (!foundAny) {
        std::cerr << cmd << ": not found" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 0; 
    }

    // Console formatting
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= 0x0004;
    SetConsoleMode(hOut, dwMode);

    Options opts;
    std::vector<std::string> commands;

        for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] == '-' && arg.length() > 1) {
            for (size_t j = 1; j < arg.length(); ++j) {
                if (arg[j] == 'a') opts.showAll = true;
                else if (arg[j] == 't') opts.typeOnly = true;
            }
        } else {
            commands.push_back(arg);
        }
    }

    for (const auto& cmd : commands) {
        checkCommand(cmd, opts);
    }

    return 0;
}
