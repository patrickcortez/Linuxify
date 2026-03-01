// Compile: g++ -std=c++17 -static -o ../cmds/uniq.exe uniq.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <memory>

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

    bool countDupes = false;
    bool onlyDupes = false; 
    bool onlyUnique = false; 
    bool ignoreCase = false;
    int skipFields = 0;
    int skipChars = 0;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-c" || arg == "--count") countDupes = true;
        else if (arg == "-d" || arg == "--repeated") onlyDupes = true;
        else if (arg == "-u" || arg == "--unique") onlyUnique = true;
        else if (arg == "-i" || arg == "--ignore-case") ignoreCase = true;
        else if (arg.rfind("-f", 0) == 0) skipFields = std::stoi(arg.length() > 2 ? arg.substr(2) : args[++i]);
        else if (arg.rfind("-s", 0) == 0) skipChars = std::stoi(arg.length() > 2 ? arg.substr(2) : args[++i]);
        else if (arg[0] != '-') files.push_back(arg);
    }
    
    std::unique_ptr<std::istream> inputPtr;
    std::ifstream fileStream;
    
    if (!files.empty()) {
         fileStream.open(resolvePath(files[0])); 
         if (!fileStream) {
             printError("uniq: cannot open '" + files[0] + "'");
             return 1;
         }
    }
    
    std::istream& is = files.empty() ? std::cin : fileStream;
    
    std::ostream* os = &std::cout;
    std::ofstream outStream;
    if (files.size() > 1) {
         outStream.open(resolvePath(files[1]));
         if (outStream) os = &outStream;
    }

    std::string prevLine;
    std::string currentLine;
    int count = 0;
    bool first = true;

    auto linesMatch = [&](const std::string& a, const std::string& b) {
        std::string sa = a;
        std::string sb = b;
        
        if (skipFields > 0) {
             int skipped = 0;
             size_t posA = 0, posB = 0;
             while (skipped < skipFields) {
                 while (posA < sa.length() && !isspace((unsigned char)sa[posA])) posA++;
                 while (posA < sa.length() && isspace((unsigned char)sa[posA])) posA++;
                 while (posB < sb.length() && !isspace((unsigned char)sb[posB])) posB++;
                 while (posB < sb.length() && isspace((unsigned char)sb[posB])) posB++;
                 skipped++;
             }
             if (posA < sa.length()) sa = sa.substr(posA); else sa = "";
             if (posB < sb.length()) sb = sb.substr(posB); else sb = "";
        }
        
        if (skipChars > 0 && sa.length() > (size_t)skipChars) sa = sa.substr(skipChars);
        if (skipChars > 0 && sb.length() > (size_t)skipChars) sb = sb.substr(skipChars);
        
        if (ignoreCase) {
            std::transform(sa.begin(), sa.end(), sa.begin(), ::tolower);
            std::transform(sb.begin(), sb.end(), sb.begin(), ::tolower);
        }
        return sa == sb;
    };

    auto flush = [&](const std::string& line, int cnt) {
         if (cnt == 0) return;
         bool print = true;
         if (onlyDupes && cnt == 1) print = false;
         if (onlyUnique && cnt > 1) print = false;
         
         if (print) {
             if (countDupes) *os << std::setw(7) << cnt << " ";
             *os << line << std::endl;
         }
    };

    while (std::getline(is, currentLine)) {
        if (first) {
            prevLine = currentLine;
            count = 1;
            first = false;
            continue;
        }
        
        if (linesMatch(prevLine, currentLine)) {
            count++;
        } else {
            flush(prevLine, count);
            prevLine = currentLine;
            count = 1;
        }
    }
    if (!first) flush(prevLine, count);

    return 0;
}
