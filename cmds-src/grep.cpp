
#include "../shell_streams.hpp"
#include <string>
#include <vector>
#include <deque>
#include <algorithm>
#include <regex>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

struct GrepOptions {
    bool ignoreCase = false;
    bool lineNumbers = false;
    bool invertMatch = false;
    bool countOnly = false;
    bool recursive = false;
    bool useRegex = false;
    bool fixedStrings = false;
    bool wordMatch = false;
    bool lineMatch = false;
    bool showFilename = false;
    bool noFilename = false;
    bool binaryFilesText = false;
    int afterContext = 0;
    int beforeContext = 0;
    int maxCount = -1;
    bool color = true;
};

GrepOptions opts;

void printError(const std::string& msg) {
    ShellIO::serr << ShellIO::Color::LightRed << "grep: " << msg << ShellIO::Color::Reset << ShellIO::endl;
}

bool isBinaryData(const std::string& s) {
    if (opts.binaryFilesText) return false;
    return s.find('\0') != std::string::npos;
}

bool isMatch(const std::string& line, const std::regex& re, const std::string& pattern, 
             std::vector<std::pair<size_t, size_t>>& matchRanges) {
    matchRanges.clear();
    
    if (opts.useRegex) {
        try {
            std::sregex_iterator begin(line.begin(), line.end(), re);
            std::sregex_iterator end;
            
            if (begin == end) return opts.invertMatch;
            
            bool found = false;
            for (auto i = begin; i != end; ++i) {
                std::smatch match = *i;
                if (opts.lineMatch) {
                    if (match.position() == 0 && match.length() == line.length()) {
                        matchRanges.push_back({(size_t)match.position(), (size_t)match.length()});
                        found = true;
                    } 
                } 
                else if (opts.wordMatch) {
                    size_t pos = match.position();
                    size_t len = match.length();
                    bool startOk = (pos == 0) || (!isalnum(line[pos-1]) && line[pos-1] != '_');
                    bool endOk = (pos + len == line.length()) || (!isalnum(line[pos+len]) && line[pos+len] != '_');
                    if (startOk && endOk) {
                        matchRanges.push_back({pos, len});
                        found = true;
                    }
                } else {
                    matchRanges.push_back({(size_t)match.position(), (size_t)match.length()});
                    found = true;
                }
            }
            return opts.invertMatch ? !found : found;
        } catch (...) { return opts.invertMatch; }
    } else {
        std::string searchLine = line;
        std::string searchPat = pattern;
        
        if (opts.ignoreCase) {
            std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(), ::tolower);
            std::transform(searchPat.begin(), searchPat.end(), searchPat.begin(), ::tolower);
        }
        
        if (opts.lineMatch) {
            bool eq = (searchLine == searchPat);
            if (eq) matchRanges.push_back({0, line.length()});
            return opts.invertMatch ? !eq : eq;    
        }
        
        size_t pos = 0;
        bool found = false;
        while ((pos = searchLine.find(searchPat, pos)) != std::string::npos) {
            bool valid = true;
            if (opts.wordMatch) {
                bool startOk = (pos == 0) || (!isalnum(searchLine[pos-1]) && searchLine[pos-1] != '_');
                bool endOk = (pos + searchPat.length() == searchLine.length()) || (!isalnum(searchLine[pos+searchPat.length()]) && searchLine[pos+searchPat.length()] != '_');
                if (!startOk || !endOk) valid = false;
            }
            
            if (valid) {
                matchRanges.push_back({pos, searchPat.length()});
                found = true;
            }
            pos += 1;
            if (pos >= searchLine.length()) break;
        }
        return opts.invertMatch ? !found : found;
    }
}

void printLine(const std::string& filename, int lineNum, const std::string& line, 
               const std::vector<std::pair<size_t, size_t>>& matches, bool isContext, char separator) {
    if (!opts.noFilename && opts.showFilename) {
        ShellIO::sout << ShellIO::Color::Magenta << filename << ShellIO::Color::Blue << separator << ShellIO::Color::Reset;
    }
    
    if (opts.lineNumbers) {
        ShellIO::sout << ShellIO::Color::Green << lineNum << ShellIO::Color::Blue << separator << ShellIO::Color::Reset;
    }
    
    if (matches.empty() || isContext || opts.invertMatch || !opts.color) {
        ShellIO::sout << line << ShellIO::endl;
        return;
    }
    
    size_t lastPos = 0;
    for (const auto& mr : matches) {
        size_t start = mr.first;
        size_t len = mr.second;
        
        if (start > lastPos) {
            ShellIO::sout << line.substr(lastPos, start - lastPos);
        }
        
        ShellIO::sout << ShellIO::Color::LightRed << line.substr(start, len) << ShellIO::Color::Reset;
        
        lastPos = start + len;
    }
    if (lastPos < line.length()) {
        ShellIO::sout << line.substr(lastPos);
    }
    ShellIO::sout << ShellIO::endl;
}

int processFile(ShellIO::ShellInStream& is, const std::string& filename, const std::string& pattern, const std::regex& re) {
    std::string line;
    int lineNum = 0;
    int matchCount = 0;
    int afterContextCounter = 0;
    std::deque<std::pair<int, std::string>> beforeBuffer;
    std::vector<std::pair<size_t, size_t>> matches;
    
    while (is.getline(line)) {
        lineNum++;
        
        if (lineNum == 1 && isBinaryData(line)) {
            if (!opts.countOnly) {
                ShellIO::sout << "Binary file " << filename << " matches" << ShellIO::endl;
            }
            return 1;
        }

        bool matched = isMatch(line, re, pattern, matches);
        
        if (matched) {
            matchCount++;
            if (opts.maxCount != -1 && matchCount > opts.maxCount) break;
            if (opts.countOnly) continue;
            
            if (!beforeBuffer.empty()) {
                if (afterContextCounter == 0 && beforeBuffer.front().first > 1 && opts.beforeContext > 0) {
                     ShellIO::sout << "--" << ShellIO::endl;
                }
                while (!beforeBuffer.empty()) {
                     printLine(filename, beforeBuffer.front().first, beforeBuffer.front().second, {}, true, '-');
                     beforeBuffer.pop_front();
                }
            }
            
            if (afterContextCounter == 0 && opts.afterContext > 0 && matchCount > 1) {
                 ShellIO::sout << "--" << ShellIO::endl;
            }

            printLine(filename, lineNum, line, matches, false, ':');
            afterContextCounter = opts.afterContext;
        } else {
            if (afterContextCounter > 0) {
                printLine(filename, lineNum, line, {}, true, '-');
                afterContextCounter--;
            } else if (opts.beforeContext > 0) {
                beforeBuffer.push_back({lineNum, line});
                if (beforeBuffer.size() > (size_t)opts.beforeContext) {
                    beforeBuffer.pop_front();
                }
            }
        }
    }
    
    if (opts.countOnly) {
        if (!opts.noFilename && opts.showFilename) {
            ShellIO::sout << ShellIO::Color::Magenta << filename << ShellIO::Color::Blue << ":" << ShellIO::Color::Reset;
        }
        ShellIO::sout << matchCount << ShellIO::endl;
    }
    
    return (matchCount > 0);
}

void processPath(const std::string& path, const std::string& pattern, const std::regex& re, int& totalMatches) {
    WIN32_FIND_DATAA findData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!opts.noFilename) printError(path + ": No such file or directory");
        return;
    }
    
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (opts.recursive) {
             std::string searchPath = path + "\\*";
             hFind = FindFirstFileA(searchPath.c_str(), &findData);
             if (hFind != INVALID_HANDLE_VALUE) {
                 do {
                     std::string name = findData.cFileName;
                     if (name != "." && name != "..") {
                         processPath(path + "\\" + name, pattern, re, totalMatches);
                     }
                 } while (FindNextFileA(hFind, &findData));
                 FindClose(hFind);
             }
        } else {
            printError(path + ": Is a directory");
        }
    } else {
        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
             if (!opts.noFilename) printError(path + ": Permission denied (or not found)");
        } else {
             ShellIO::ShellInStream fis(hFile);
             totalMatches += processFile(fis, path, pattern, re);
             CloseHandle(hFile);
        }
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    
    for(int i=0; i<argc; ++i) args.push_back(argv[i]);
    
    std::string pattern;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg[0] == '-') {
            if (arg == "--") {
                for (size_t k = i + 1; k < args.size(); ++k) {
                    if (pattern.empty()) pattern = args[k]; else files.push_back(args[k]);
                }
                break;
            }
            
            if (arg.find("--") == 0) {
                if (arg == "--ignore-case") opts.ignoreCase = true;
                else if (arg == "--line-number") opts.lineNumbers = true;
                else if (arg == "--invert-match") opts.invertMatch = true;
                else if (arg == "--count") opts.countOnly = true;
                else if (arg == "--recursive") opts.recursive = true;
                else if (arg == "--extended-regexp") opts.useRegex = true;
                else if (arg == "--fixed-strings") opts.fixedStrings = true;
                else if (arg == "--word-regexp") opts.wordMatch = true;
                else if (arg == "--line-regexp") opts.lineMatch = true;
                else if (arg == "--with-filename") opts.showFilename = true;
                else if (arg == "--no-filename") opts.noFilename = true;
                else if (arg == "--text" || arg == "-a") opts.binaryFilesText = true;
                else if (arg == "--help") {
                    ShellIO::sout << "Usage: grep [OPTIONS] PATTERN [FILE...]" << ShellIO::endl;
                    return 0;
                }
                else if (arg.find("--context=") == 0) { size_t eq=arg.find('='); opts.afterContext = opts.beforeContext = std::stoi(arg.substr(eq+1)); }
                else if (arg.find("--after-context=") == 0) { size_t eq=arg.find('='); opts.afterContext = std::stoi(arg.substr(eq+1)); }
                else if (arg.find("--before-context=") == 0) { size_t eq=arg.find('='); opts.beforeContext = std::stoi(arg.substr(eq+1)); }
                else if (arg.find("--color") == 0) {
                    size_t eq = arg.find('=');
                    if (eq != std::string::npos) {
                        std::string mode = arg.substr(eq+1);
                        if (mode == "never" || mode == "no") opts.color = false;
                        else opts.color = true;
                    } else opts.color = true;
                }
            } else {
                for (size_t j = 1; j < arg.length(); ++j) {
                     char c = arg[j];
                     if (c == 'i') opts.ignoreCase = true;
                     else if (c == 'n') opts.lineNumbers = true;
                     else if (c == 'v') opts.invertMatch = true;
                     else if (c == 'c') opts.countOnly = true;
                     else if (c == 'r' || c == 'R') opts.recursive = true;
                     else if (c == 'E') opts.useRegex = true;
                     else if (c == 'F') opts.fixedStrings = true;
                     else if (c == 'w') opts.wordMatch = true;
                     else if (c == 'x') opts.lineMatch = true;
                     else if (c == 'H') opts.showFilename = true;
                     else if (c == 'h') opts.noFilename = true;
                     else if (c == 'a') opts.binaryFilesText = true;
                     else if (c == 'A') {
                         if (j+1 < arg.length()) { opts.afterContext = std::stoi(arg.substr(j+1)); break; }
                         else if (i+1 < args.size()) { opts.afterContext = std::stoi(args[++i]); break; }
                     }
                     else if (c == 'B') {
                         if (j+1 < arg.length()) { opts.beforeContext = std::stoi(arg.substr(j+1)); break; }
                         else if (i+1 < args.size()) { opts.beforeContext = std::stoi(args[++i]); break; }
                     }
                     else if (c == 'C') {
                         int val = 2;
                         if (j+1 < arg.length()) { val = std::stoi(arg.substr(j+1)); }
                         else if (i+1 < args.size()) { val = std::stoi(args[++i]); }
                         opts.afterContext = opts.beforeContext = val;
                         break;
                     }
                     else if (c == 'm') {
                         if (j+1 < arg.length()) { opts.maxCount = std::stoi(arg.substr(j+1)); break; }
                         else if (i+1 < args.size()) { opts.maxCount = std::stoi(args[++i]); break; }
                     }
                     else if (c == 'e') {
                         if (i+1 < args.size()) { pattern = args[++i]; break; }
                     }
                }
            }
        } else {
            if (pattern.empty()) pattern = arg;
            else files.push_back(arg);
        }
    }
    
    if (pattern.empty()) {
        printError("Usage: grep [OPTIONS] PATTERN [FILE...]");
        return 2;
    }
    
    std::regex re;
    try {
        if (opts.useRegex || (!opts.fixedStrings)) {
            auto flags = std::regex::ECMAScript; 
            if (opts.ignoreCase) flags |= std::regex::icase;
            std::string finalPat = pattern;
            re = std::regex(finalPat, flags);
            opts.useRegex = true;
        }
    } catch (const std::regex_error& e) {
        printError("Regex error: " + std::string(e.what()));
        return 2;
    }
    
    if (files.empty() && opts.recursive) files.push_back(".");
    if (files.size() > 1 || opts.recursive) opts.showFilename = true;
    if (opts.noFilename) opts.showFilename = false;
    
    int totalMatches = 0;
    
    if (files.empty()) {
        totalMatches += processFile(ShellIO::sin, "(standard input)", pattern, re);
    } else {
        for (const auto& f : files) {
            processPath(f, pattern, re, totalMatches);
        }
    }
    
    return (totalMatches > 0) ? 0 : 1;
}
