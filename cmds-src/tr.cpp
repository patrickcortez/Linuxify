// Compile: g++ -std=c++17 -static -o ../cmds/tr.exe tr.cpp
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    bool deleteMode = false;
    bool squeezeMode = false;
    bool complementMode = false;
    std::string set1, set2;
    
    size_t argIdx = 1;
    while (argIdx < args.size() && args[argIdx][0] == '-' && args[argIdx].length() > 1) {
        for (size_t k = 1; k < args[argIdx].length(); ++k) {
            char flag = args[argIdx][k];
            if (flag == 'd') deleteMode = true;
            else if (flag == 's') squeezeMode = true;
            else if (flag == 'c' || flag == 'C') complementMode = true;
        }
        argIdx++;
    }
    
    if (argIdx < args.size()) set1 = args[argIdx++];
    if (argIdx < args.size()) set2 = args[argIdx++];
    
    if (set1.empty()) {
        printError("tr: missing operand");
        return 1;
    }

    auto expandClass = [](const std::string& className) -> std::string {
        std::string result;
        if (className == "alpha" || className == "ALPHA") {
            for (char c = 'a'; c <= 'z'; ++c) result += c;
            for (char c = 'A'; c <= 'Z'; ++c) result += c;
        } else if (className == "digit" || className == "DIGIT") {
            for (char c = '0'; c <= '9'; ++c) result += c;
        } else if (className == "upper" || className == "UPPER") {
            for (char c = 'A'; c <= 'Z'; ++c) result += c;
        } else if (className == "lower" || className == "LOWER") {
            for (char c = 'a'; c <= 'z'; ++c) result += c;
        } else if (className == "alnum" || className == "ALNUM") {
            for (char c = 'a'; c <= 'z'; ++c) result += c;
            for (char c = 'A'; c <= 'Z'; ++c) result += c;
            for (char c = '0'; c <= '9'; ++c) result += c;
        } else if (className == "space" || className == "SPACE") {
            result = " \t\n\r\v\f";
        } else if (className == "blank" || className == "BLANK") {
            result = " \t";
        } else if (className == "punct" || className == "PUNCT") {
            result = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
        } else if (className == "xdigit" || className == "XDIGIT") {
            for (char c = '0'; c <= '9'; ++c) result += c;
            for (char c = 'a'; c <= 'f'; ++c) result += c;
            for (char c = 'A'; c <= 'F'; ++c) result += c;
        } else if (className == "cntrl" || className == "CNTRL") {
            for (char c = 0; c < 32; ++c) result += c;
            result += (char)127;
        } else if (className == "graph" || className == "GRAPH") {
            for (char c = 33; c < 127; ++c) result += c;
        } else if (className == "print" || className == "PRINT") {
            for (char c = 32; c < 127; ++c) result += c;
        }
        return result;
    };

    auto expandEscape = [](char c) -> char {
        switch (c) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case 'f': return '\f';
            case 'v': return '\v';
            case 'a': return '\a';
            case 'b': return '\b';
            case '\\': return '\\';
            default: return c;
        }
    };

    auto expandSet = [&](const std::string& set) -> std::string {
        std::string result;
        for (size_t i = 0; i < set.length(); ++i) {
            if (set[i] == '[' && i + 2 < set.length() && set[i + 1] == ':') {
                size_t endPos = set.find(":]", i + 2);
                if (endPos != std::string::npos) {
                    std::string className = set.substr(i + 2, endPos - i - 2);
                    result += expandClass(className);
                    i = endPos + 1;
                    continue;
                }
            }
            if (set[i] == '\\' && i + 1 < set.length()) {
                char next = set[i + 1];
                if (next >= '0' && next <= '7') {
                    int val = 0;
                    size_t j = i + 1;
                    while (j < set.length() && j < i + 4 && set[j] >= '0' && set[j] <= '7') {
                        val = val * 8 + (set[j] - '0');
                        j++;
                    }
                    result += (char)val;
                    i = j - 1;
                } else {
                    result += expandEscape(next);
                    i++;
                }
                continue;
            }
            if (i + 2 < set.length() && set[i + 1] == '-') {
                char start = set[i];
                char end = set[i + 2];
                if (start <= end) {
                    for (char c = start; c <= end; ++c) result += c;
                } else {
                    for (char c = start; c >= end; --c) result += c;
                }
                i += 2;
            } else {
                result += set[i];
            }
        }
        return result;
    };
    
    std::string expandedSet1 = expandSet(set1);
    std::string expandedSet2 = expandSet(set2);
    
    if (complementMode) {
        std::string complement;
        for (int c = 1; c < 256; ++c) {
            if (expandedSet1.find((char)c) == std::string::npos) {
                complement += (char)c;
            }
        }
        expandedSet1 = complement;
    }
    
    while (!deleteMode && !expandedSet2.empty() && expandedSet2.length() < expandedSet1.length()) {
        expandedSet2 += expandedSet2.back();
    }
    
    std::ostringstream oss;
    char buf[4096];
    while (std::cin.read(buf, sizeof(buf)) || std::cin.gcount() > 0) {
        oss.write(buf, std::cin.gcount());
    }
    std::string input = oss.str();
    
    std::string result;
    char lastChar = '\0';
    bool lastWasInSet1 = false;
    
    for (unsigned char c : input) {
        size_t pos = expandedSet1.find(c);
        if (deleteMode) {
            if (pos == std::string::npos) {
                if (!squeezeMode || c != lastChar) {
                    result += c;
                    lastChar = c;
                }
            }
        } else if (pos != std::string::npos) {
            char newChar = (pos < expandedSet2.length()) ? expandedSet2[pos] : c;
            if (!squeezeMode || newChar != lastChar || !lastWasInSet1) {
                result += newChar;
                lastChar = newChar;
            }
            lastWasInSet1 = true;
        } else {
            result += c;
            lastChar = c;
            lastWasInSet1 = false;
        }
    }
    
    std::cout << result;
    return 0;
}
