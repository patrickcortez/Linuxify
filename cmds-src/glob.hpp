// Glob expansion module for Linuxify shell
// Usage: Include this header and call Glob::expandGlob(pattern, cwd)

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace Glob {

inline bool containsGlob(const std::string& s) {
    for (char c : s) {
        if (c == '*' || c == '?' || c == '[') return true;
    }
    return false;
}

inline bool matchBracketClass(const std::string& pattern, size_t& patIdx, char c) {
    patIdx++;
    bool negate = false;
    if (patIdx < pattern.size() && (pattern[patIdx] == '!' || pattern[patIdx] == '^')) {
        negate = true;
        patIdx++;
    }
    
    bool matched = false;
    bool first = true;
    char lastChar = 0;
    
    while (patIdx < pattern.size() && (first || pattern[patIdx] != ']')) {
        first = false;
        char pc = pattern[patIdx];
        
        if (pc == '-' && lastChar != 0 && patIdx + 1 < pattern.size() && pattern[patIdx + 1] != ']') {
            patIdx++;
            char rangeEnd = pattern[patIdx];
            if (c >= lastChar && c <= rangeEnd) {
                matched = true;
            }
            lastChar = 0;
        } else {
            if (c == pc) {
                matched = true;
            }
            lastChar = pc;
        }
        patIdx++;
    }
    
    return negate ? !matched : matched;
}

inline bool globMatch(const std::string& pattern, const std::string& text, bool matchDot = false) {
    size_t patIdx = 0;
    size_t textIdx = 0;
    size_t starPatIdx = std::string::npos;
    size_t starTextIdx = std::string::npos;
    
    if (!matchDot && !text.empty() && text[0] == '.') {
        if (pattern.empty() || pattern[0] != '.') {
            return false;
        }
    }
    
    while (textIdx < text.size()) {
        if (patIdx < pattern.size()) {
            char p = pattern[patIdx];
            char t = text[textIdx];
            
            if (p == '*') {
                if (patIdx + 1 < pattern.size() && pattern[patIdx + 1] == '*') {
                    patIdx += 2;
                    if (patIdx >= pattern.size()) return true;
                    while (textIdx < text.size()) {
                        if (globMatch(pattern.substr(patIdx), text.substr(textIdx), true)) {
                            return true;
                        }
                        textIdx++;
                    }
                    return globMatch(pattern.substr(patIdx), "", true);
                }
                starPatIdx = patIdx;
                starTextIdx = textIdx;
                patIdx++;
                continue;
            }
            
            if (p == '?') {
                patIdx++;
                textIdx++;
                continue;
            }
            
            if (p == '[') {
                size_t bracketStart = patIdx;
                if (matchBracketClass(pattern, patIdx, t)) {
                    textIdx++;
                    continue;
                } else {
                    if (starPatIdx != std::string::npos) {
                        patIdx = starPatIdx + 1;
                        starTextIdx++;
                        textIdx = starTextIdx;
                        continue;
                    }
                    return false;
                }
            }
            
            if (p == t) {
                patIdx++;
                textIdx++;
                continue;
            }
        }
        
        if (starPatIdx != std::string::npos) {
            patIdx = starPatIdx + 1;
            starTextIdx++;
            textIdx = starTextIdx;
            continue;
        }
        
        return false;
    }
    
    while (patIdx < pattern.size() && pattern[patIdx] == '*') {
        patIdx++;
    }
    
    return patIdx >= pattern.size();
}

inline std::pair<std::string, std::string> splitPath(const std::string& pattern) {
    size_t lastSep = pattern.find_last_of("/\\");
    if (lastSep == std::string::npos) {
        return {".", pattern};
    }
    return {pattern.substr(0, lastSep), pattern.substr(lastSep + 1)};
}

inline bool hasRecursiveGlob(const std::string& pattern) {
    return pattern.find("**") != std::string::npos;
}

inline std::vector<std::string> expandRecursive(const std::string& basePath, const std::string& pattern, const std::string& cwd) {
    std::vector<std::string> results;
    
    size_t doubleStarPos = pattern.find("**");
    if (doubleStarPos == std::string::npos) {
        return results;
    }
    
    std::string prefix = pattern.substr(0, doubleStarPos);
    std::string suffix = pattern.substr(doubleStarPos + 2);
    
    if (!suffix.empty() && (suffix[0] == '/' || suffix[0] == '\\')) {
        suffix = suffix.substr(1);
    }
    
    fs::path searchBase;
    if (prefix.empty()) {
        searchBase = basePath.empty() ? fs::path(cwd) : fs::path(basePath);
    } else {
        if (prefix.back() == '/' || prefix.back() == '\\') {
            prefix.pop_back();
        }
        if (fs::path(prefix).is_absolute()) {
            searchBase = prefix;
        } else {
            searchBase = fs::path(cwd) / prefix;
        }
    }
    
    if (!fs::exists(searchBase) || !fs::is_directory(searchBase)) {
        return results;
    }
    
    try {
        for (auto& entry : fs::recursive_directory_iterator(searchBase, fs::directory_options::skip_permission_denied)) {
            std::string entryPath = entry.path().string();
            std::string filename = entry.path().filename().string();
            
            if (!filename.empty() && filename[0] == '.') {
                continue;
            }
            
            if (suffix.empty()) {
                results.push_back(entryPath);
            } else {
                if (globMatch(suffix, filename)) {
                    results.push_back(entryPath);
                }
            }
        }
    } catch (...) {}
    
    std::sort(results.begin(), results.end());
    return results;
}

inline std::vector<std::string> expandGlob(const std::string& pattern, const std::string& cwd) {
    std::vector<std::string> results;
    
    if (!containsGlob(pattern)) {
        results.push_back(pattern);
        return results;
    }
    
    if (hasRecursiveGlob(pattern)) {
        size_t doubleStarPos = pattern.find("**");
        std::string basePath = pattern.substr(0, doubleStarPos);
        if (!basePath.empty() && (basePath.back() == '/' || basePath.back() == '\\')) {
            basePath.pop_back();
        }
        
        results = expandRecursive(basePath, pattern, cwd);
        
        if (results.empty()) {
            results.push_back(pattern);
        }
        return results;
    }
    
    auto [dirPart, filePart] = splitPath(pattern);
    
    fs::path searchDir;
    if (fs::path(dirPart).is_absolute()) {
        searchDir = dirPart;
    } else if (dirPart == ".") {
        searchDir = cwd;
    } else {
        searchDir = fs::path(cwd) / dirPart;
    }
    
    if (containsGlob(dirPart)) {
        std::vector<std::string> dirMatches;
        
        std::string::size_type firstGlob = std::string::npos;
        for (size_t i = 0; i < dirPart.size(); i++) {
            if (dirPart[i] == '*' || dirPart[i] == '?' || dirPart[i] == '[') {
                firstGlob = i;
                break;
            }
        }
        
        std::string staticPrefix;
        std::string dynamicPart;
        
        if (firstGlob != std::string::npos) {
            size_t lastSep = dirPart.rfind('/', firstGlob);
            if (lastSep == std::string::npos) lastSep = dirPart.rfind('\\', firstGlob);
            
            if (lastSep != std::string::npos) {
                staticPrefix = dirPart.substr(0, lastSep);
                dynamicPart = dirPart.substr(lastSep + 1);
            } else {
                staticPrefix = ".";
                dynamicPart = dirPart;
            }
        }
        
        fs::path baseDir;
        if (fs::path(staticPrefix).is_absolute()) {
            baseDir = staticPrefix;
        } else if (staticPrefix == ".") {
            baseDir = cwd;
        } else {
            baseDir = fs::path(cwd) / staticPrefix;
        }
        
        if (fs::exists(baseDir) && fs::is_directory(baseDir)) {
            try {
                for (auto& entry : fs::directory_iterator(baseDir)) {
                    if (entry.is_directory()) {
                        std::string entryName = entry.path().filename().string();
                        if (!entryName.empty() && entryName[0] == '.') continue;
                        
                        if (globMatch(dynamicPart, entryName)) {
                            std::string subPattern = entry.path().string() + "/" + filePart;
                            auto subResults = expandGlob(subPattern, cwd);
                            for (const auto& r : subResults) {
                                if (r != subPattern || !containsGlob(r)) {
                                    results.push_back(r);
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
        }
        
        if (results.empty()) {
            results.push_back(pattern);
        }
        std::sort(results.begin(), results.end());
        return results;
    }
    
    if (!fs::exists(searchDir) || !fs::is_directory(searchDir)) {
        results.push_back(pattern);
        return results;
    }
    
    try {
        for (auto& entry : fs::directory_iterator(searchDir)) {
            std::string filename = entry.path().filename().string();
            
            if (!filename.empty() && filename[0] == '.' && (filePart.empty() || filePart[0] != '.')) {
                continue;
            }
            
            if (globMatch(filePart, filename)) {
                if (dirPart == ".") {
                    results.push_back(filename);
                } else {
                    results.push_back(entry.path().string());
                }
            }
        }
    } catch (...) {}
    
    std::sort(results.begin(), results.end());
    
    if (results.empty()) {
        results.push_back(pattern);
    }
    
    return results;
}

}
