#ifndef LINUXIFY_ENGINE_SHELL_CONTEXT_HPP
#define LINUXIFY_ENGINE_SHELL_CONTEXT_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include "../cmds-src/interpreter.hpp" // For Bash::Interpreter
#include "../cmds-src/cmds.hpp" // For AliasManager

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
    
    // Alias Manager
    AliasManager aliasInContext;
    
    // Interpreter State
    Bash::Interpreter interpreter;
    
    // Variables
    std::map<std::string, std::string> sessionEnv;
    std::map<std::string, std::vector<std::string>> sessionArrayEnv;
    std::set<std::string> persistentVars;
    std::set<std::string> persistentArrayVars;
    
    std::string promptFirstColor = "\033[92m";
    std::string promptSecondColor = "\033[94m";
    std::string promptResetColor = "\033[0m";
    
    bool previousCommandWasEmpty = true; 

    ShellContext() {
        try {
            currentDir = fs::current_path().string();
            std::string lower = currentDir;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("\\system32") != std::string::npos || 
                lower.find("\\syswow64") != std::string::npos) {
                char* home = getenv("USERPROFILE");
                if (home) {
                    currentDir = home;
                }
            }
        } catch (...) {
            char* home = getenv("USERPROFILE");
            currentDir = home ? home : "C:\\";
        }
    }
};

#endif // LINUXIFY_ENGINE_SHELL_CONTEXT_HPP
