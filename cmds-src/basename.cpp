// Compile: g++ -std=c++17 -static -o ../cmds/basename.exe basename.cpp
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
        printError("basename: missing operand");
        return 1;
    }
    
    std::string suffix;
    if (args.size() > 2 && args[args.size() - 2] == "-s") {
        suffix = args[args.size() - 1];
    }
    
    int exitCode = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-a" || args[i] == "-s") continue;
        if (i > 1 && args[i - 1] == "-s") continue;
        
        fs::path p(args[i]);
        std::string name = p.filename().string();
        
        if (!suffix.empty() && name.length() > suffix.length()) {
            if (name.substr(name.length() - suffix.length()) == suffix) {
                name = name.substr(0, name.length() - suffix.length());
            }
        }
        
        std::cout << name << "\n";
    }
    return exitCode;
}
