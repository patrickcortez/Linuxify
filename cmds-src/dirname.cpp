// Compile: g++ -std=c++17 -static -o ../cmds/dirname.exe dirname.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    if (args.size() < 2) {
        printError("dirname: missing operand");
        return 1;
    }
    
    int exitCode = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i][0] == '-') continue;
        
        fs::path p(args[i]);
        std::string dir = p.parent_path().string();
        if (dir.empty()) dir = ".";
        
        std::cout << dir << "\n";
    }
    return exitCode;
}
