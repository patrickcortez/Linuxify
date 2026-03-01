// Compile: g++ -std=c++17 -static -o ../cmds/xargs.exe xargs.cpp
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    std::string command = "echo";
    bool verbose = false;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-t") verbose = true;
        else if (args[i][0] != '-') {
            command = args[i];
            for (size_t j = i + 1; j < args.size(); ++j) {
                command += " " + args[j];
            }
            break;
        }
    }
    
    // Read stdin
    std::ostringstream oss;
    char buf[4096];
    while (std::cin.read(buf, sizeof(buf)) || std::cin.gcount() > 0) {
        oss.write(buf, std::cin.gcount());
    }
    std::string pipedInput = oss.str();
    
    std::vector<std::string> inputArgs;
    std::istringstream iss(pipedInput);
    std::string arg;
    while (iss >> arg) {
        inputArgs.push_back(arg);
    }
    
    std::string cmdLine = command;
    for (const auto& a : inputArgs) {
        cmdLine += " \"" + a + "\"";
    }
    
    if (verbose) {
        std::cout << cmdLine << "\n";
    }
    
    int ret = std::system(cmdLine.c_str());
    return ret;
}
