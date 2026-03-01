// Compile: g++ -std=c++17 -static -o ../cmds/ln.exe ln.cpp
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

    if (args.size() < 2) {
        printError("ln: missing operand");
        return 1;
    }
    
    bool symbolic = false;
    bool force = false;
    bool noDereference = false;
    bool verbose = false;
    bool relative = false;
    std::vector<std::string> targets;
    std::string linkName;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i][0] == '-' && args[i].length() > 1) {
            for (size_t j = 1; j < args[i].length(); ++j) {
                char flag = args[i][j];
                if (flag == 's') symbolic = true;
                else if (flag == 'f') force = true;
                else if (flag == 'n') noDereference = true;
                else if (flag == 'v') verbose = true;
                else if (flag == 'r') relative = true;
            }
        } else {
            if (linkName.empty()) {
                targets.push_back(args[i]);
            } else {
                targets.push_back(linkName);
                linkName = args[i];
            }
            if (targets.size() == 1 && i == args.size() - 1) {
                linkName = targets[0];
                targets.clear();
            } else if (i == args.size() - 1) {
                linkName = args[i];
                targets.pop_back();
            }
        }
    }
    
    if (targets.empty() && !linkName.empty()) {
        targets.push_back(linkName);
        linkName = fs::path(linkName).filename().string();
    }

    int exitCode = 0;
    for (const auto& target : targets) {
        std::string targetPath = resolvePath(target);
        std::string actualLinkPath;
        
        if (fs::is_directory(resolvePath(linkName))) {
            actualLinkPath = resolvePath(linkName) + "\\" + fs::path(target).filename().string();
        } else {
            actualLinkPath = resolvePath(linkName);
        }
        
        if (force && fs::exists(actualLinkPath)) {
            try { fs::remove(actualLinkPath); } catch (...) {}
        }
        
        if (fs::exists(actualLinkPath) && !noDereference) {
            printError("ln: failed to create link '" + actualLinkPath + "': File exists");
            exitCode = 1;
            continue;
        }
        
        std::string linkTarget = targetPath;
        if (relative && symbolic) {
            fs::path linkDir = fs::path(actualLinkPath).parent_path();
            linkTarget = fs::relative(targetPath, linkDir).string();
        }
        
        if (!symbolic) {
            if (CreateHardLinkA(actualLinkPath.c_str(), targetPath.c_str(), NULL)) {
                if (verbose) std::cout << "'" << actualLinkPath << "' => '" << target << "'\n";
            } else {
                printError("ln: failed to create hard link (error " + std::to_string(GetLastError()) + ")");
                exitCode = 1;
            }
        } else {
            DWORD flags = 0;
            try { if (fs::is_directory(targetPath)) flags = SYMBOLIC_LINK_FLAG_DIRECTORY; } catch (...) {}
            flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            
            if (CreateSymbolicLinkA(actualLinkPath.c_str(), linkTarget.c_str(), flags)) {
                if (verbose) std::cout << "'" << actualLinkPath << "' -> '" << linkTarget << "'\n";
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_PRIVILEGE_NOT_HELD) {
                    printError("ln: symbolic links require admin privileges or Developer Mode");
                } else {
                    printError("ln: failed to create symbolic link (error " + std::to_string(error) + ")");
                }
                exitCode = 1;
            }
        }
    }
    return exitCode;
}
