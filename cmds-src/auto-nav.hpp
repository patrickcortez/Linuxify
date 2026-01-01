// Auto-Navigate for Linuxify 

#ifndef AUTO_NAV_HPP
#define AUTO_NAV_HPP

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

class AutoNav {
public:
    static bool isNavigablePath(const std::string& input, const std::string& currentDir) {
        if (input.empty()) return false;
        
        std::string trimmed = input;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
            trimmed.erase(0, 1);
        }
        
        if (trimmed.empty()) return false;
        
        // Check if it looks like a path
        bool looksLikePath = false;
        
        // Starts with / or ./ or ../ or ~/ or drive letter
        if (trimmed[0] == '/' || trimmed[0] == '\\') {
            looksLikePath = true;
        } else if (trimmed.length() >= 2 && trimmed[0] == '.' && (trimmed[1] == '/' || trimmed[1] == '\\')) {
            looksLikePath = true;
        } else if (trimmed.length() >= 3 && trimmed[0] == '.' && trimmed[1] == '.' && (trimmed[2] == '/' || trimmed[2] == '\\')) {
            looksLikePath = true;
        } else if (trimmed == "." || trimmed == "..") {
            looksLikePath = true;
        } else if (trimmed[0] == '~') {
            looksLikePath = true;
        } else if (trimmed.length() >= 2 && trimmed[1] == ':') {
            looksLikePath = true;
        }
        
        if (!looksLikePath) return false;
        
        // Resolve and check if it's a directory
        std::string resolvedPath = resolvePath(trimmed, currentDir);
        
        try {
            return fs::exists(resolvedPath) && fs::is_directory(resolvedPath);
        } catch (...) {
            return false;
        }
    }
    
    static std::string resolvePath(const std::string& path, const std::string& currentDir) {
        if (path.empty()) return currentDir;
        
        std::string expandedPath = path;
        
        // Handle ~ (home directory)
        if (!expandedPath.empty() && expandedPath[0] == '~') {
            char* home = getenv("USERPROFILE");
            if (home) {
                expandedPath = std::string(home) + expandedPath.substr(1);
            }
        }
        
        fs::path p(expandedPath);
        
        if (p.is_absolute()) {
            try {
                if (fs::exists(p)) {
                    return fs::canonical(p).string();
                }
                return p.string();
            } catch (...) {
                return p.string();
            }
        }
        
        fs::path fullPath = fs::path(currentDir) / expandedPath;
        try {
            if (fs::exists(fullPath)) {
                return fs::canonical(fullPath).string();
            }
            return fullPath.string();
        } catch (...) {
            return fullPath.string();
        }
    }
    
    static std::string getResolvedDirectory(const std::string& input, const std::string& currentDir) {
        std::string trimmed = input;
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
            trimmed.erase(0, 1);
        }
        
        return resolvePath(trimmed, currentDir);
    }
};

#endif
