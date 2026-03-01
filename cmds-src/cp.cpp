// Compile: g++ -std=c++17 -static -o ../cmds/cp.exe cp.cpp
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
    bool interactive = false;
    bool noClobber = false;
    bool update = false;
    bool verbose = false;
    std::vector<std::string> operands;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-r" || arg == "-R" || arg == "--recursive") recursive = true;
        else if (arg == "-i" || arg == "--interactive") interactive = true;
        else if (arg == "-n" || arg == "--no-clobber") noClobber = true;
        else if (arg == "-u" || arg == "--update") update = true;
        else if (arg == "-v" || arg == "--verbose") verbose = true;
        else if (arg.length() > 1 && arg[0] == '-') {
             for (size_t k = 1; k < arg.length(); ++k) {
                  if (arg[k] == 'r' || arg[k] == 'R') recursive = true;
                  else if (arg[k] == 'i') interactive = true;
                  else if (arg[k] == 'n') noClobber = true;
                  else if (arg[k] == 'u') update = true;
                  else if (arg[k] == 'v') verbose = true;
             }
        }
        else operands.push_back(arg);
    }
    
    if (operands.size() < 2) {
        printError("cp: missing operand");
        return 1;
    }
    
    int exitCode = 0;
    std::string destPath = resolvePath(operands.back());
    bool destIsDir = fs::exists(destPath) && fs::is_directory(destPath);
    
    if (operands.size() > 2 && !destIsDir) {
         printError("cp: target '" + operands.back() + "' is not a directory");
         return 1;
    }
    
    for (size_t i = 0; i < operands.size() - 1; ++i) {
         std::string sourcePath = resolvePath(operands[i]);
         if (!fs::exists(sourcePath)) {
              printError("cp: cannot stat '" + operands[i] + "': No such file or directory");
              exitCode = 1;
              continue;
         }
         
         if (fs::is_directory(sourcePath) && !recursive) {
              printError("cp: -r not specified; omitting directory '" + operands[i] + "'");
              exitCode = 1;
              continue;
         }
         
         std::string actualDest = destPath;
         if (destIsDir) {
              actualDest = (fs::path(destPath) / fs::path(sourcePath).filename()).string();
         }
         
         if (fs::exists(actualDest)) {
              if (noClobber) continue;
              if (update) {
                   try {
                        if (fs::last_write_time(sourcePath) <= fs::last_write_time(actualDest)) continue;
                   } catch(...) {}
              }
              if (interactive) {
                   std::string prompt = "cp: overwrite '" + actualDest + "'? ";
                   if (!confirm(prompt)) continue;
              }
         }
         
         try {
              auto options = fs::copy_options::overwrite_existing;
              if (recursive) options |= fs::copy_options::recursive;
              
              if (fs::is_directory(sourcePath)) {
                  fs::copy(sourcePath, actualDest, options);
              } else {
                  fs::copy_file(sourcePath, actualDest, options);
              }
              if (verbose) std::cout << "'" << operands[i] << "' -> '" << actualDest << "'" << std::endl;
         } catch (const std::exception& e) {
              printError("cp: cannot copy: " + std::string(e.what()));
              exitCode = 1;
         }
    }
    return exitCode;
}
