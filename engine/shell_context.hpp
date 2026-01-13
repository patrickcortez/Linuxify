#ifndef LINUXIFY_ENGINE_SHELL_CONTEXT_HPP
#define LINUXIFY_ENGINE_SHELL_CONTEXT_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include "../cmds-src/interpreter.hpp" // For Bash::Interpreter

namespace fs = std::filesystem;

/**
 * @brief The Data (The Persistent World)
 * 
 * Holds the state that must persist across different Continuations.
 * This effectively replaces the members of the old 'Linuxify' class.
 */
struct ShellContext {
    // Process State
    bool running = true;
    int lastExitCode = 0;
    bool isAdmin = false;
    
    // Environment
    std::string currentDir;
    std::vector<std::string> commandHistory;
    
    // Interpreter State
    Bash::Interpreter interpreter;
    
    // Variables
    std::map<std::string, std::string> sessionEnv;
    std::map<std::string, std::vector<std::string>> sessionArrayEnv;
    std::set<std::string> persistentVars;
    std::set<std::string> persistentArrayVars;
    
    // Spacing Helper
    // used by StatePrompt to determine if it should print a newline
    bool previousCommandWasEmpty = true; 

    ShellContext() {
        // Initialize CWD to startup directory
        try {
            currentDir = fs::current_path().string();
        } catch (...) {
            currentDir = "C:\\";
        }
    }
};

#endif // LINUXIFY_ENGINE_SHELL_CONTEXT_HPP
