// Compile: g++ -std=c++17 -static -o ../cmds/mv.exe mv.cpp
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

    bool interactive = false;
    bool noClobber = false; 
    bool update = false;
    bool verbose = false;
    std::vector<std::string> operands;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-i" || arg == "--interactive") interactive = true;
        else if (arg == "-n" || arg == "--no-clobber") noClobber = true;
        else if (arg == "-u" || arg == "--update") update = true;
        else if (arg == "-v" || arg == "--verbose") verbose = true;
        else if (arg[0] != '-') operands.push_back(arg);
    }
    
    if (operands.size() < 2) {
        printError("mv: missing operand");
        return 1;
    }
    
    int exitCode = 0;
    std::string destPath = resolvePath(operands.back());
    bool destIsDir = fs::exists(destPath) && fs::is_directory(destPath);
    
    if (operands.size() > 2 && !destIsDir) {
         printError("mv: target '" + operands.back() + "' is not a directory");
         return 1;
    }
    
    for (size_t i = 0; i < operands.size() - 1; ++i) {
         std::string sourcePath = resolvePath(operands[i]);
         if (!fs::exists(sourcePath)) {
             printError("mv: cannot stat '" + operands[i] + "': No such file or directory");
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
                  std::string prompt = "mv: overwrite '" + actualDest + "'? ";
                  if (!confirm(prompt)) continue;
              }
              fs::remove_all(actualDest); 
         }
         
         try {
             fs::rename(sourcePath, actualDest);
             if (verbose) std::cout << "renamed '" << operands[i] << "' -> '" << actualDest << "'" << std::endl;
         } catch (const std::exception& e) {
             printError("mv: cannot move '" + operands[i] + "' to '" + actualDest + "': " + e.what());
             exitCode = 1;
         }
    }
    return exitCode;
}
