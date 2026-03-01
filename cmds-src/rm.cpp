// Compile: g++ -std=c++17 -static -o ../cmds/rm.exe rm.cpp
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

bool confirm(const std::string& prompt) {
    std::cout << prompt;
    std::string ans;
    std::getline(std::cin, ans);
    return (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y'));
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    bool recursive = false;
    bool force = false; 
    bool interactive = false; 
    bool verbose = false; 
    std::vector<std::string> targets;

    for (size_t i = 1; i < args.size(); ++i) {
         std::string arg = args[i];
         if (arg == "-r" || arg == "-R" || arg == "--recursive") recursive = true;
         else if (arg == "-f" || arg == "--force") force = true;
         else if (arg == "-i" || arg == "--interactive") interactive = true;
         else if (arg == "-v" || arg == "--verbose") verbose = true;
         else if (arg.length() > 1 && arg[0] == '-') {
              for (size_t k = 1; k < arg.length(); ++k) {
                   if (arg[k] == 'r' || arg[k] == 'R') recursive = true;
                   else if (arg[k] == 'f') force = true;
                   else if (arg[k] == 'i') interactive = true;
                   else if (arg[k] == 'v') verbose = true;
              }
         }
         else targets.push_back(args[i]);
    }
    
    if (targets.empty()) {
         printError("rm: missing operand");
         return 1;
    }

    int exitCode = 0;
    for (const auto& target : targets) {
         std::string fullPath = resolvePath(target);
         if (!fs::exists(fullPath)) {
             if (!force) {
                 printError("rm: cannot remove '" + target + "': No such file or directory");
                 exitCode = 1;
             }
             continue;
         }
         
         if (interactive) {
             std::string prompt = "rm: remove " + std::string(fs::is_directory(fullPath) ? "directory" : "regular file") + " '" + target + "'? ";
             if (!confirm(prompt)) continue;
         }

         try {
             if (fs::is_directory(fullPath)) {
                 if (!recursive) {
                     printError("rm: cannot remove '" + target + "': Is a directory");
                     exitCode = 1;
                     continue;
                 }
                 fs::remove_all(fullPath);
                 if (verbose) std::cout << "removed directory '" << target << "'" << std::endl;
             } else {
                 fs::remove(fullPath);
                 if (verbose) std::cout << "removed '" << target << "'" << std::endl;
             }
         } catch (const fs::filesystem_error& e) {
             printError("rm: cannot remove '" + target + "': " + e.code().message());
             exitCode = 1;
         } catch (const std::exception& e) {
             printError("rm: cannot remove '" + target + "': " + std::string(e.what()));
             exitCode = 1;
         }
    }
    return exitCode;
}
