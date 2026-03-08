// Auto-Suggest for Linuxify Shell
// Include this header and use AutoSuggest::getSuggestions() for Tab completion

#ifndef AUTO_SUGGEST_HPP
#define AUTO_SUGGEST_HPP

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <windows.h>
#include <set>
#include "../registry.hpp"

namespace fs = std::filesystem;

class AutoSuggest {
public:
    static const std::vector<std::string>& getBuiltinCommands() {
        static std::vector<std::string> commands = {
            "echo", "pwd", "cd", "ls", "dir", "type", "mkdir", "rm", "rmdir",
            "mv", "cp", "touch", "chmod", "chown", "clear", "env", "export",
            "which", "whoami", "ps", "kill", "history", "grep", "head", "tail", "wc",
            "sort", "uniq", "find", "cut", "tr", "sed", "awk", "diff", "tee", "xargs",
            "rev", "ln", "stat", "file", "readlink", "realpath", "basename", "dirname",
            "tree", "du", "lin", "top", "jobs", "fg", "bg", "less", "more", "uninstall",
            "setup", "alias", "unalias", "source", "read", "test", "true", "false",
            "exit", "help", "man", "date", "cal", "uname", "hostname", "uptime",
            "free", "df", "mount", "umount", "sleep", "printf", "seq", "yes",
            "fuzz", "registry"
        };
        return commands;
    }
    
    static const std::vector<std::string>& getShellSyntaxKeywords() {
        static std::vector<std::string> keywords = {
            "if", "for", "while", "case", "function", "declare", "typeset", "local",
            "read", "source", ".", "[[", "((", "select", "until", "elif", "fi", "done", "esac",
            "do", "then", "else", "{", "}"
        };
        return keywords;
    }
    
    static std::vector<std::string> getExternalCommands() {
        static std::vector<std::string> cachedCommands;
        static bool cachePopulated = false;
        
        if (cachePopulated) return cachedCommands;

        std::set<std::string> uniqueCmds;
        
        // 1. Scan local 'cmds' folder (as before)
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path cmdsDir = exeDir / "cmds";
        
        try {
            if (fs::exists(cmdsDir) && fs::is_directory(cmdsDir)) {
                for (const auto& entry : fs::directory_iterator(cmdsDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".exe" || ext == ".bat" || ext == ".cmd") {
                            uniqueCmds.insert(entry.path().stem().string());
                        }
                    }
                }
            }
        } catch (...) {}
        
        // 2. Scan PATH
        char* pathEnv = getenv("PATH");
        if (pathEnv) {
            std::string pathStr = pathEnv;
            std::string delimiter = ";";
            size_t pos = 0;
            std::string token;
            while ((pos = pathStr.find(delimiter)) != std::string::npos) {
                token = pathStr.substr(0, pos);
                try {
                    if (!token.empty() && fs::exists(token) && fs::is_directory(token)) {
                        for (const auto& entry : fs::directory_iterator(token)) {
                             // Only grab .exe/bat/cmd
                             if (entry.is_regular_file()) {
                                 std::string ext = entry.path().extension().string();
                                 std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                 if (ext == ".exe" || ext == ".bat" || ext == ".cmd") {
                                      uniqueCmds.insert(entry.path().stem().string());
                                 }
                             }
                        }
                    }
                } catch (...) {} // Ignore permission errors etc
                pathStr.erase(0, pos + delimiter.length());
            }
            // Check last token
             try {
                if (!pathStr.empty() && fs::exists(pathStr) && fs::is_directory(pathStr)) {
                    for (const auto& entry : fs::directory_iterator(pathStr)) {
                         if (entry.is_regular_file()) {
                             std::string ext = entry.path().extension().string();
                             std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                             if (ext == ".exe" || ext == ".bat" || ext == ".cmd") {
                                  uniqueCmds.insert(entry.path().stem().string());
                             }
                         }
                    }
                }
            } catch (...) {}
        }

        // 3. Scan Registry (registry.lin)
        for (const auto& pair : g_registry.getAllCommands()) {
            uniqueCmds.insert(pair.first);
        }

        cachedCommands.assign(uniqueCmds.begin(), uniqueCmds.end());
        cachePopulated = true;
        return cachedCommands;
    }

    static std::vector<std::string> getCommandSuggestions(const std::string& prefix) {
        std::vector<std::string> suggestions;
        std::set<std::string> seen;
        std::string lowerPrefix = toLower(prefix);
        
        for (const auto& cmd : getBuiltinCommands()) {
            if (cmd.length() >= prefix.length() && 
                toLower(cmd.substr(0, prefix.length())) == lowerPrefix) {
                if (seen.insert(cmd).second) suggestions.push_back(cmd);
            }
        }
        
        for (const auto& cmd : getExternalCommands()) {
            if (cmd.length() >= prefix.length() && 
                toLower(cmd.substr(0, prefix.length())) == lowerPrefix) {
                if (seen.insert(cmd).second) suggestions.push_back(cmd);
            }
        }
        
        for (const auto& kw : getShellSyntaxKeywords()) {
            if (kw.length() >= prefix.length() && 
                toLower(kw.substr(0, prefix.length())) == lowerPrefix) {
                if (seen.insert(kw).second) suggestions.push_back(kw);
            }
        }
        
        std::sort(suggestions.begin(), suggestions.end());
        return suggestions;
    }

    static std::vector<std::string> getPathSuggestions(const std::string& partialPath, const std::string& currentDir) {
        std::vector<std::string> suggestions;
        
        std::string searchDir;
        std::string prefix;
        
        fs::path inputPath(partialPath);
        
        if (partialPath.empty()) {
            searchDir = currentDir;
            prefix = "";
        } else if (partialPath.back() == '/' || partialPath.back() == '\\') {
            if (inputPath.is_absolute()) {
                searchDir = partialPath;
            } else {
                searchDir = (fs::path(currentDir) / partialPath).string();
            }
            prefix = "";
        } else {
            fs::path parentPath = inputPath.parent_path();
            if (parentPath.empty()) {
                searchDir = currentDir;
            } else if (inputPath.is_absolute()) {
                searchDir = parentPath.string();
            } else {
                searchDir = (fs::path(currentDir) / parentPath).string();
            }
            prefix = inputPath.filename().string();
        }
        
        try {
            if (!fs::exists(searchDir) || !fs::is_directory(searchDir)) {
                return suggestions;
            }
            
            std::string lowerPrefix = toLower(prefix);
            
            for (const auto& entry : fs::directory_iterator(searchDir)) {
                std::string name = entry.path().filename().string();
                std::string lowerName = toLower(name);
                
                if (prefix.empty() || lowerName.substr(0, lowerPrefix.length()) == lowerPrefix) {
                    std::string suggestion = name;
                    if (fs::is_directory(entry)) {
                        suggestion += "/";
                    }
                    suggestions.push_back(suggestion);
                }
            }
        } catch (...) {
        }
        
        std::sort(suggestions.begin(), suggestions.end());
        return suggestions;
    }

    static std::string findCommonPrefix(const std::vector<std::string>& suggestions) {
        if (suggestions.empty()) return "";
        if (suggestions.size() == 1) return suggestions[0];
        
        std::string prefix = suggestions[0];
        for (size_t i = 1; i < suggestions.size(); i++) {
            size_t j = 0;
            while (j < prefix.length() && j < suggestions[i].length() &&
                   toLower(prefix[j]) == toLower(suggestions[i][j])) {
                j++;
            }
            prefix = prefix.substr(0, j);
            if (prefix.empty()) break;
        }
        return prefix;
    }

    struct SuggestionResult {
        std::vector<std::string> suggestions;
        std::string completionText;
        size_t replaceStart;
        size_t replaceLength;
        bool isPath;
    };

    static SuggestionResult getSuggestions(const std::string& input, int cursorPos, const std::string& currentDir) {
        SuggestionResult result;
        result.replaceStart = 0;
        result.replaceLength = 0;
        result.isPath = false;
        
        if (input.empty() || cursorPos == 0) {
            result.suggestions = getCommandSuggestions("");
            result.completionText = findCommonPrefix(result.suggestions);
            return result;
        }
        
        std::string relevantInput = input.substr(0, cursorPos);
        
        size_t lastSpace = relevantInput.rfind(' ');
        
        if (lastSpace == std::string::npos) {
            result.replaceStart = 0;
            result.replaceLength = cursorPos;
            
            // Check if input looks like a path for auto-nav
            bool looksLikePath = false;
            if (relevantInput.find('/') != std::string::npos ||
                relevantInput.find('\\') != std::string::npos ||
                (relevantInput.length() >= 1 && relevantInput[0] == '.') ||
                (relevantInput.length() >= 1 && relevantInput[0] == '~') ||
                (relevantInput.length() >= 2 && relevantInput[1] == ':')) {
                looksLikePath = true;
            }
            
            if (looksLikePath) {
                result.isPath = true;
                result.suggestions = getPathSuggestions(relevantInput, currentDir);
                
                if (!result.suggestions.empty()) {
                    std::string common = findCommonPrefix(result.suggestions);
                    
                    fs::path tokenPath(relevantInput);
                    std::string parentPart;
                    if (!relevantInput.empty() && relevantInput.back() != '/' && relevantInput.back() != '\\') {
                        fs::path parent = tokenPath.parent_path();
                        if (!parent.empty()) {
                            parentPart = parent.string();
                            if (parentPart.back() != '/' && parentPart.back() != '\\') {
                                parentPart += "/";
                            }
                        }
                    } else {
                        parentPart = relevantInput;
                    }
                    
                    result.completionText = parentPart + common;
                }
            } else {
                result.isPath = false;
                result.suggestions = getCommandSuggestions(relevantInput);
                result.completionText = findCommonPrefix(result.suggestions);
            }
        } else {
            std::string currentToken = relevantInput.substr(lastSpace + 1);
            result.replaceStart = lastSpace + 1;
            result.replaceLength = currentToken.length();
            
            // Only suggest paths if token looks like a path (contains / or ./ or .. or is absolute)
            bool looksLikePath = false;
            if (currentToken.find('/') != std::string::npos ||
                currentToken.find('\\') != std::string::npos ||
                (currentToken.length() >= 1 && currentToken[0] == '.') ||
                (currentToken.length() >= 2 && currentToken[1] == ':')) {
                looksLikePath = true;
            }
            
            if (looksLikePath) {
                result.isPath = true;
                result.suggestions = getPathSuggestions(currentToken, currentDir);
                
                if (!result.suggestions.empty()) {
                    std::string common = findCommonPrefix(result.suggestions);
                    
                    fs::path tokenPath(currentToken);
                    std::string parentPart;
                    if (!currentToken.empty() && currentToken.back() != '/' && currentToken.back() != '\\') {
                        fs::path parent = tokenPath.parent_path();
                        if (!parent.empty()) {
                            parentPart = parent.string();
                            if (parentPart.back() != '/' && parentPart.back() != '\\') {
                                parentPart += "/";
                            }
                        }
                    } else {
                        parentPart = currentToken;
                    }
                    
                    result.completionText = parentPart + common;
                }
            } else {
                // Not a path - no suggestions for plain arguments
                result.isPath = false;
            }
        }
        
        return result;
    }

private:
    static std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    static char toLower(char c) {
        return static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
};

#endif
