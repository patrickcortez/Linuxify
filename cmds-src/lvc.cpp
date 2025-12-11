// Linuxify Version Control (LVC) - Production Grade
// A sophisticated git-like version control system
// Compile: g++ -std=c++17 -static -o lvc.exe lvc.cpp

#include "lvc.hpp"
#include <iostream>
#include <vector>
#include <string>

void printUsage() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    
    SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << "LVC - Linuxify Version Control v2.0\n";
    SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    std::cout << "A sophisticated git-like version control with delta compression\n\n";
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    
    std::cout << "Getting Started:\n";
    std::cout << "  lvc init                      Initialize repository\n";
    std::cout << "  lvc add <files> | .           Stage files\n";
    std::cout << "  lvc commit -v <ver> -m <msg>  Create version\n\n";
    
    std::cout << "History & Diff:\n";
    std::cout << "  lvc log                       Show commit history\n";
    std::cout << "  lvc diff                      Show uncommitted changes\n";
    std::cout << "  lvc diff <v1> <v2>            Compare versions\n";
    std::cout << "  lvc show <version>            Show version details\n";
    std::cout << "  lvc blame <file>              Show line-by-line history\n\n";
    
    std::cout << "Version Management:\n";
    std::cout << "  lvc versions                  List all versions\n";
    std::cout << "  lvc rebuild <version>         Restore to version\n";
    std::cout << "  lvc checkout <ver|branch>     Switch version/branch\n\n";
    
    std::cout << "Branches:\n";
    std::cout << "  lvc branch                    List branches\n";
    std::cout << "  lvc branch <name>             Create branch\n";
    std::cout << "  lvc branch -d <name>          Delete branch\n\n";
    
    std::cout << "Stash:\n";
    std::cout << "  lvc stash                     Save staged changes\n";
    std::cout << "  lvc stash pop                 Restore stash\n";
    std::cout << "  lvc stash list                Show stashes\n\n";
    
    std::cout << "Other:\n";
    std::cout << "  lvc status                    Show status\n";
    std::cout << "  lvc reset [--hard]            Reset index\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 0;
    }
    
    std::string cmd = argv[1];
    LVC lvc(".");
    
    if (cmd == "init") {
        lvc.init();
    }
    else if (cmd == "add") {
        if (argc < 3) {
            std::cerr << "error: nothing specified to add\n";
            return 1;
        }
        std::vector<std::string> paths;
        for (int i = 2; i < argc; i++) paths.push_back(argv[i]);
        lvc.add(paths);
    }
    else if (cmd == "commit") {
        std::string version, message;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if ((arg == "-v" || arg == "--version") && i + 1 < argc) {
                version = argv[++i];
            } else if ((arg == "-m" || arg == "--message") && i + 1 < argc) {
                message = argv[++i];
            }
        }
        if (version.empty()) {
            std::cerr << "error: version required. Use 'lvc commit -v <version>'\n";
            return 1;
        }
        lvc.commit(version, message);
    }
    else if (cmd == "diff") {
        if (argc == 2) lvc.diff();
        else if (argc == 3) lvc.diff(argv[2]);
        else lvc.diff(argv[2], argv[3]);
    }
    else if (cmd == "log") {
        int count = (argc > 2) ? std::stoi(argv[2]) : 10;
        lvc.log(count);
    }
    else if (cmd == "status" || cmd == "st") {
        lvc.status();
    }
    else if (cmd == "rebuild" || cmd == "restore") {
        if (argc < 3) {
            std::cerr << "error: version required\n";
            return 1;
        }
        lvc.rebuild(argv[2]);
    }
    else if (cmd == "versions" || cmd == "ls") {
        lvc.versions();
    }
    else if (cmd == "show") {
        if (argc < 3) {
            std::cerr << "error: version required\n";
            return 1;
        }
        lvc.show(argv[2]);
    }
    else if (cmd == "branch") {
        if (argc == 2) {
            lvc.branch();
        } else if (argc == 3 && std::string(argv[2]) != "-d") {
            lvc.branch(argv[2]);
        } else if (argc == 4 && std::string(argv[2]) == "-d") {
            lvc.branch(argv[3], true);
        }
    }
    else if (cmd == "checkout" || cmd == "co") {
        if (argc < 3) {
            std::cerr << "error: target required\n";
            return 1;
        }
        lvc.checkout(argv[2]);
    }
    else if (cmd == "blame") {
        if (argc < 3) {
            std::cerr << "error: file required\n";
            return 1;
        }
        lvc.blame(argv[2]);
    }
    else if (cmd == "stash") {
        std::string action = (argc > 2) ? argv[2] : "push";
        lvc.stash(action);
    }
    else if (cmd == "reset") {
        std::string mode = (argc > 2) ? argv[2] : "--soft";
        lvc.reset(mode);
    }
    else if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        printUsage();
    }
    else {
        std::cerr << "error: unknown command '" << cmd << "'\n";
        std::cerr << "Run 'lvc help' for usage.\n";
        return 1;
    }
    
    return 0;
}
