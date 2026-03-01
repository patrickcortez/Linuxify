// Compile: g++ -std=c++17 -static -o ../cmds/cut.exe cut.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <climits>

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

    char delimiter = '\t';
    std::string outputDelimiter;
    bool outputDelimiterSet = false;
    std::vector<std::pair<int, int>> ranges;
    std::vector<std::string> files;
    bool byByte = false;
    bool byChar = false;
    bool byField = false;
    bool complement = false;
    bool onlyDelimited = false;
    
    auto parseRange = [&](const std::string& spec) {
        std::istringstream ss(spec);
        std::string part;
        while (std::getline(ss, part, ',')) {
            size_t dashPos = part.find('-');
            if (dashPos == std::string::npos) {
                int val = std::stoi(part);
                ranges.push_back({val, val});
            } else if (dashPos == 0) {
                int end = std::stoi(part.substr(1));
                ranges.push_back({1, end});
            } else if (dashPos == part.length() - 1) {
                int start = std::stoi(part.substr(0, dashPos));
                ranges.push_back({start, INT_MAX});
            } else {
                int start = std::stoi(part.substr(0, dashPos));
                int end = std::stoi(part.substr(dashPos + 1));
                ranges.push_back({start, end});
            }
        }
    };
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-d" && i + 1 < args.size()) {
            std::string delim = args[++i];
            if (!delim.empty()) delimiter = delim[0];
        } else if (args[i].substr(0, 2) == "-d" && args[i].length() > 2) {
            delimiter = args[i][2];
        } else if (args[i] == "-f" && i + 1 < args.size()) {
            byField = true;
            parseRange(args[++i]);
        } else if (args[i].substr(0, 2) == "-f" && args[i].length() > 2) {
            byField = true;
            parseRange(args[i].substr(2));
        } else if (args[i] == "-c" && i + 1 < args.size()) {
            byChar = true;
            parseRange(args[++i]);
        } else if (args[i].substr(0, 2) == "-c" && args[i].length() > 2) {
            byChar = true;
            parseRange(args[i].substr(2));
        } else if (args[i] == "-b" && i + 1 < args.size()) {
            byByte = true;
            parseRange(args[++i]);
        } else if (args[i].substr(0, 2) == "-b" && args[i].length() > 2) {
            byByte = true;
            parseRange(args[i].substr(2));
        } else if (args[i] == "--complement") {
            complement = true;
        } else if (args[i] == "-s" || args[i] == "--only-delimited") {
            onlyDelimited = true;
        } else if (args[i] == "--output-delimiter" && i + 1 < args.size()) {
            outputDelimiter = args[++i];
            outputDelimiterSet = true;
        } else if (args[i].substr(0, 19) == "--output-delimiter=") {
            outputDelimiter = args[i].substr(19);
            outputDelimiterSet = true;
        } else if (args[i][0] != '-') {
            files.push_back(args[i]);
        }
    }
    
    if (ranges.empty()) {
        printError("cut: you must specify a list of bytes, characters, or fields");
        return 1;
    }

    if (!outputDelimiterSet) {
        outputDelimiter = std::string(1, delimiter);
    }

    std::sort(ranges.begin(), ranges.end());

    auto isInRange = [&](int pos) -> bool {
        for (const auto& r : ranges) {
            if (pos >= r.first && pos <= r.second) return !complement;
        }
        return complement;
    };

    auto processLine = [&](const std::string& line) {
        if (byByte || byChar) {
            std::string result;
            bool first = true;
            for (int pos = 1; pos <= (int)line.length(); ++pos) {
                if (isInRange(pos)) {
                    if (!first && outputDelimiterSet) result += outputDelimiter;
                    result += line[pos - 1];
                    first = false;
                }
            }
            std::cout << result << "\n";
        } else {
            if (onlyDelimited && line.find(delimiter) == std::string::npos) {
                return;
            }
            
            std::vector<std::string> tokens;
            size_t start = 0, end;
            while ((end = line.find(delimiter, start)) != std::string::npos) {
                tokens.push_back(line.substr(start, end - start));
                start = end + 1;
            }
            tokens.push_back(line.substr(start));
            
            std::string result;
            bool first = true;
            for (int pos = 1; pos <= (int)tokens.size(); ++pos) {
                if (isInRange(pos)) {
                    if (!first) result += outputDelimiter;
                    result += tokens[pos - 1];
                    first = false;
                }
            }
            std::cout << result << "\n";
        }
    };

    if (!files.empty()) {
        for (const auto& filePath : files) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("cut: cannot open '" + filePath + "'");
                continue;
            }
            std::string line;
            while (std::getline(file, line)) {
                processLine(line);
            }
        }
    } else {
        std::string line;
        while (std::getline(std::cin, line)) {
            processLine(line);
        }
    }
    return 0;
}
