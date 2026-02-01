// Compile Instructions: This is a header file, included by main.cpp and interpreter.hpp
// No standalone compilation.

#ifndef LINUXIFY_CMDS_HPP
#define LINUXIFY_CMDS_HPP

#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <iostream>
#include <algorithm>

class AliasManager {
private:
    std::map<std::string, std::string> aliases;

public:
    void addAlias(const std::string& name, const std::string& command) {
        aliases[name] = command;
    }

    void removeAlias(const std::string& name) {
        aliases.erase(name);
    }

    std::string getAlias(const std::string& name) const {
        auto it = aliases.find(name);
        if (it != aliases.end()) {
            return it->second;
        }
        return "";
    }

    bool hasAlias(const std::string& name) const {
        return aliases.find(name) != aliases.end();
    }

    const std::map<std::string, std::string>& getAllAliases() const {
        return aliases;
    }

    void clear() {
        aliases.clear();
    }

    // Resolves an alias recursively with depth limit
    std::string resolve(const std::string& command, int depth = 0) const {
        if (depth > 50) return command; 

        std::string firstWord;
        std::string rest;
        
        // Skip leading whitespace
        size_t startPos = command.find_first_not_of(" \t");
        if (startPos == std::string::npos) {
            return command; // All whitespace or empty
        }
        
        size_t spacePos = command.find_first_of(" \t", startPos);
        if (spacePos == std::string::npos) {
            firstWord = command.substr(startPos);
        } else {
            firstWord = command.substr(startPos, spacePos - startPos);
            rest = command.substr(spacePos);
        }

        auto it = aliases.find(firstWord);
        if (it != aliases.end()) {
            std::string aliasBody = it->second;
            std::string expanded = aliasBody + rest;
            
            // Prevent immediate recursion if the alias expands to itself (e.g., alias ls='ls -F')
            std::string expandedFirstWord;
            size_t expSpacePos = aliasBody.find_first_of(" \t");
            
            if (expSpacePos == std::string::npos) {
                expandedFirstWord = aliasBody;
            } else {
                expandedFirstWord = aliasBody.substr(0, expSpacePos);
            }

            if (expandedFirstWord == firstWord) {
                return expanded;
            }
            
            return resolve(expanded, depth + 1);
        }

        return command;
    }
};

#endif // LINUXIFY_CMDS_HPP
