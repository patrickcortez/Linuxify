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
        if (c == '*' || c == '?' || c == '[' || c == '{') return true;
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

inline std::vector<std::string> expandBraces(const std::string& input) {
    std::vector<std::string> results;
    
    size_t searchPos = 0;
    while (searchPos < input.length()) {
        size_t start = std::string::npos;
        for (size_t i = searchPos; i < input.length(); ++i) {
            if (input[i] == '\\' && i + 1 < input.length()) {
                i++;
                continue;
            }
            if (input[i] == '{') {
                start = i;
                break;
            }
        }
        
        if (start == std::string::npos) {
            break; 
        }
        
        size_t end = std::string::npos;
        int depth = 0;
        std::vector<size_t> commas;
        
        for (size_t i = start; i < input.length(); ++i) {
            if (input[i] == '\\' && i + 1 < input.length()) {
                i++;
                continue;
            }
            if (input[i] == '{') depth++;
            else if (input[i] == '}') {
                depth--;
                if (depth == 0) {
                    end = i;
                    break;
                }
            } else if (input[i] == ',' && depth == 1) {
                commas.push_back(i);
            }
        }
        
        if (end == std::string::npos) {
            searchPos = start + 1;
            continue;
        }
        
        std::string mid = input.substr(start + 1, end - start - 1);
        bool isRange = false;
        std::string rangeStart, rangeEnd;
        if (commas.empty()) {
            size_t dots = mid.find("..");
            if (dots != std::string::npos && dots > 0 && dots + 2 < mid.length()) {
                rangeStart = mid.substr(0, dots);
                rangeEnd = mid.substr(dots + 2);
                if (rangeStart.find("..") == std::string::npos && rangeEnd.find("..") == std::string::npos) {
                    auto isNum = [](const std::string& s) {
                        if (s.empty()) return false;
                        size_t i = 0;
                        if (s[i] == '-' || s[i] == '+') i++;
                        if (i == s.length()) return false;
                        for (; i < s.length(); i++) if (!isdigit(s[i])) return false;
                        return true;
                    };
                    auto isChar = [](const std::string& s) {
                        return s.length() == 1 && isalpha(s[0]);
                    };
                    if ((isNum(rangeStart) && isNum(rangeEnd)) || (isChar(rangeStart) && isChar(rangeEnd))) {
                        isRange = true;
                    }
                }
            }
        }
        
        if (!commas.empty() || isRange) {
            std::string pre = input.substr(0, start);
            std::string post = input.substr(end + 1);
            
            if (!commas.empty()) {
                std::vector<std::string> parts;
                size_t lastPos = start + 1;
                for (size_t c : commas) {
                    parts.push_back(input.substr(lastPos, c - lastPos));
                    lastPos = c + 1;
                }
                parts.push_back(input.substr(lastPos, end - lastPos));
                
                for (const auto& p : parts) {
                    std::string constructed = pre + p + post;
                    auto sub = expandBraces(constructed);
                    results.insert(results.end(), sub.begin(), sub.end());
                }
            } else if (isRange) {
                if (isdigit(rangeStart[0]) || rangeStart[0] == '-' || rangeStart[0] == '+') {
                    int s = std::stoi(rangeStart);
                    int e = std::stoi(rangeEnd);
                    int step = s <= e ? 1 : -1;
                    for (int i = s; s <= e ? i <= e : i >= e; i += step) {
                        std::string constructed = pre + std::to_string(i) + post;
                        auto sub = expandBraces(constructed);
                        results.insert(results.end(), sub.begin(), sub.end());
                    }
                } else {
                    char s = rangeStart[0];
                    char e = rangeEnd[0];
                    int step = s <= e ? 1 : -1;
                    for (char i = s; s <= e ? i <= e : i >= e; i += step) {
                        std::string constructed = pre + std::string(1, i) + post;
                        auto sub = expandBraces(constructed);
                        results.insert(results.end(), sub.begin(), sub.end());
                    }
                }
            }
            return results;
        } else {
            searchPos = start + 1;
        }
    }
    
    results.push_back(input);
    return results;
}

inline std::vector<std::string> expandGlobInternal(const std::string& pattern, const std::string& cwd) {
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
                            auto subResults = expandGlobInternal(subPattern, cwd);
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

inline std::vector<std::string> expandGlob(const std::string& pattern, const std::string& cwd) {
    std::vector<std::string> braced = expandBraces(pattern);
    if (braced.size() == 1 && braced[0] == pattern) {
        return expandGlobInternal(pattern, cwd);
    }
    
    std::vector<std::string> results;
    for (const auto& b : braced) {
        auto g = expandGlobInternal(b, cwd);
        if (g.size() == 1 && g[0] == b && g[0] != pattern) {
            results.push_back(g[0]);
        } else {
            results.insert(results.end(), g.begin(), g.end());
        }
    }
    return results;
}

}
