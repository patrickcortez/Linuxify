// Linuxify Shell (lish) - Entry Point
// A shell script interpreter with bash-like syntax for Linuxify
// Compile: g++ -std=c++17 -static -o lish.exe lish.cpp

#include "lish.hpp"
#include <iostream>
#include <string>

void printUsage() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    
    SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << "Linuxify Shell (lish) v1.0\n";
    SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    std::cout << "A native shell script interpreter for Windows\n\n";
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    
    std::cout << "Usage:\n";
    std::cout << "  lish <script.sh>        Run a shell script\n";
    std::cout << "  lish -c \"<command>\"     Run a command\n";
    std::cout << "  lish                    Interactive mode\n";
    std::cout << "  lish --debug <script>   Run with debug output\n\n";
    
    std::cout << "Shebang Support:\n";
    std::cout << "  Scripts can specify interpreter with: #!/path/to/lish\n";
    std::cout << "  Or use: #!/bin/bash, #!/bin/sh (mapped to lish)\n\n";
    
    std::cout << "Supported Features:\n";
    std::cout << "  Variables:     NAME=\"value\", $NAME, ${NAME}\n";
    std::cout << "  Comments:      # This is a comment\n";
    std::cout << "  If/Else:       if [ condition ]; then ... fi\n";
    std::cout << "  For Loop:      for i in 1 2 3; do ... done\n";
    std::cout << "  While Loop:    while [ cond ]; do ... done\n";
    std::cout << "  Pipes:         cmd1 | cmd2\n";
    std::cout << "  Redirects:     cmd > file, cmd >> file\n";
    std::cout << "  Test:          [ -f file ], [ $a = $b ]\n";
    std::cout << "  Operators:     &&, ||, ;\n\n";
    
    std::cout << "Test Operators:\n";
    std::cout << "  -f FILE       File exists\n";
    std::cout << "  -d FILE       Directory exists\n";
    std::cout << "  -e FILE       Path exists\n";
    std::cout << "  -z STRING     String is empty\n";
    std::cout << "  -n STRING     String is not empty\n";
    std::cout << "  a = b         Strings equal\n";
    std::cout << "  a != b        Strings not equal\n";
    std::cout << "  a -eq b       Numbers equal\n";
    std::cout << "  a -lt b       Less than\n";
    std::cout << "  a -gt b       Greater than\n";
}

int main(int argc, char* argv[]) {
    Bash::Interpreter interpreter;
    
    if (argc < 2) {
        // Interactive mode
        interpreter.interactive();
        return 0;
    }
    
    std::string arg1 = argv[1];
    
    if (arg1 == "-h" || arg1 == "--help" || arg1 == "help") {
        printUsage();
        return 0;
    }
    
    if (arg1 == "--debug" || arg1 == "-d") {
        interpreter.setDebug(true);
        if (argc > 2) {
            return interpreter.runScript(argv[2]);
        } else {
            interpreter.interactive();
            return 0;
        }
    }
    
    if (arg1 == "-c") {
        if (argc > 2) {
            return interpreter.runCode(argv[2]);
        } else {
            std::cerr << "lish: -c requires an argument\n";
            return 1;
        }
    }
    
    // Run script file
    return interpreter.runScript(arg1);
}
