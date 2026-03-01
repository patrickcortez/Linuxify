// Compile: g++ -std=c++17 -static -o ../cmds/sort.exe sort.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

std::string resolvePath(const std::string& path) {
    if (path.empty()) {
        return fs::current_path().string();
    }
    fs::path p(path);
    if (p.is_absolute()) {
        try {
            return fs::canonical(p).string();
        } catch (...) {
            return p.string();
        }
    }
    fs::path fullPath = fs::current_path() / path;
    try {
        return fs::canonical(fullPath).string();
    } catch (...) {
        return fullPath.string();
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    bool reverse = false;
    bool numeric = false;
    bool unique = false;
    bool ignoreCase = false;
    bool check = false;
    int keyStart = 0; 
    int keyEnd = 0;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-r" || arg == "--reverse") reverse = true;
        else if (arg == "-n" || arg == "--numeric-sort") numeric = true;
        else if (arg == "-u" || arg == "--unique") unique = true;
        else if (arg == "-f" || arg == "--ignore-case") ignoreCase = true;
        else if (arg == "-c" || arg == "--check") check = true;
        else if (arg == "-k" && i + 1 < args.size()) {
            std::string kdef = args[++i];
            size_t comma = kdef.find(',');
            if (comma != std::string::npos) {
                keyStart = std::stoi(kdef.substr(0, comma));
                keyEnd = std::stoi(kdef.substr(comma + 1));
            } else {
                keyStart = std::stoi(kdef);
                keyEnd = 0; 
            }
        }
        else if (arg[0] != '-') files.push_back(arg);
    }
    
    std::vector<std::string> lines;
    
    auto readLines = [&](std::istream& is) {
        std::string line;
        while (std::getline(is, line)) {
            lines.push_back(line);
        }
    };

    if (files.empty()) {
         readLines(std::cin);
    } else {
         for (const auto& file : files) {
             std::ifstream ifs(resolvePath(file));
             if (!ifs) {
                 printError("sort: cannot open '" + file + "'");
                 return 1;
             }
             readLines(ifs);
         }
    }

    auto extractKey = [&](const std::string& s) {
        if (keyStart == 0) return s; 
        
        std::istringstream iss(s);
        std::string token;
        std::string keyStr;
        int col = 1;
        while (iss >> token) {
            if (col >= keyStart) {
                if (!keyStr.empty()) keyStr += " ";
                keyStr += token;
            }
            if (keyEnd > 0 && col >= keyEnd) break;
            col++;
        }
        return keyStr;
    };

    auto compare = [&](const std::string& a, const std::string& b) {
         std::string ka = extractKey(a);
         std::string kb = extractKey(b);

         if (numeric) {
             try {
                 double da = std::stod(ka);
                 double db = std::stod(kb);
                 return da < db;
             } catch (...) {
                 return ka < kb;
             }
         }
         
         if (ignoreCase) {
             std::transform(ka.begin(), ka.end(), ka.begin(), ::tolower);
             std::transform(kb.begin(), kb.end(), kb.begin(), ::tolower);
         }
         
         return ka < kb;
    };

    if (check) {
        for (size_t i = 1; i < lines.size(); ++i) {
            bool ordered = !reverse ? !compare(lines[i], lines[i-1]) : !compare(lines[i-1], lines[i]);
            if (reverse) {
                if (compare(lines[i-1], lines[i])) { 
                    std::cout << "sort: disorder: " << lines[i] << std::endl;
                    return 1;
                }
            } else {
                 if (compare(lines[i], lines[i-1])) {
                     std::cout << "sort: disorder: " << lines[i] << std::endl;
                     return 1;
                 }
            }
        }
        return 0;
    }

    std::sort(lines.begin(), lines.end(), compare);

    if (reverse) {
        std::reverse(lines.begin(), lines.end());
    }
    
    if (unique) {
        auto last = std::unique(lines.begin(), lines.end(), [&](const std::string& a, const std::string& b){
             std::string ka = extractKey(a);
             std::string kb = extractKey(b);
             if (ignoreCase) {
                  std::transform(ka.begin(), ka.end(), ka.begin(), ::tolower);
                  std::transform(kb.begin(), kb.end(), kb.begin(), ::tolower);
             }
             return ka == kb;
        });
        lines.erase(last, lines.end());
    }
    
    for (const auto& line : lines) {
        std::cout << line << std::endl;
    }
    return 0;
}
