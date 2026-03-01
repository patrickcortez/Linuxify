// g++ -O3 -o ../cmds/printf.exe printf.cpp
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

int unescapeChar(char c) {
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '\\': return '\\';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'v': return '\v';
        default: return c;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "printf: missing operand\n";
        return 1;
    }
    
    std::string format = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    
    size_t argIndex = 0;
    bool hasArgs = true;
    
    while (hasArgs) {
        bool formatUsed = false;
        for (size_t i = 0; i < format.length(); ++i) {
            if (format[i] == '\\' && i + 1 < format.length()) {
                putchar(unescapeChar(format[i + 1]));
                i++;
            } else if (format[i] == '%') {
                if (i + 1 < format.length() && format[i + 1] == '%') {
                    putchar('%');
                    i++;
                } else {
                    std::string currentFormat = "%";
                    i++;
                    while (i < format.length()) {
                        currentFormat += format[i];
                        if (format[i] == 'd' || format[i] == 'i' || format[i] == 'o' || format[i] == 'u' || format[i] == 'x' || format[i] == 'X' ||
                            format[i] == 'f' || format[i] == 'F' || format[i] == 'e' || format[i] == 'E' || format[i] == 'g' || format[i] == 'G' ||
                            format[i] == 'a' || format[i] == 'A' || format[i] == 'c' || format[i] == 's' || format[i] == 'p') {
                            break;
                        }
                        i++;
                    }
                    
                    formatUsed = true;
                    std::string argStr = (argIndex < args.size()) ? args[argIndex++] : "";
                    char specifier = currentFormat.back();
                    
                    try {
                        if (specifier == 's') {
                            printf(currentFormat.c_str(), argStr.c_str());
                        } else if (specifier == 'd' || specifier == 'i') {
                            int val = argStr.empty() ? 0 : std::stoi(argStr, nullptr, 0);
                            printf(currentFormat.c_str(), val);
                        } else if (specifier == 'u' || specifier == 'o' || specifier == 'x' || specifier == 'X') {
                            unsigned int val = argStr.empty() ? 0 : std::stoul(argStr, nullptr, 0);
                            printf(currentFormat.c_str(), val);
                        } else if (specifier == 'f' || specifier == 'F' || specifier == 'e' || specifier == 'E' || specifier == 'g' || specifier == 'G' || specifier == 'a' || specifier == 'A') {
                            double val = argStr.empty() ? 0.0 : std::stod(argStr);
                            printf(currentFormat.c_str(), val);
                        } else if (specifier == 'c') {
                            char val = argStr.empty() ? '\0' : argStr[0];
                            printf(currentFormat.c_str(), val);
                        } else {
                            printf("%s", currentFormat.c_str());
                        }
                    } catch (...) {
                        if (specifier == 's') printf(currentFormat.c_str(), "");
                        else if (specifier == 'f' || specifier == 'F' || specifier == 'e' || specifier == 'E' || specifier == 'g' || specifier == 'G' || specifier == 'a' || specifier == 'A') printf(currentFormat.c_str(), 0.0);
                        else printf(currentFormat.c_str(), 0);
                    }
                }
            } else {
                putchar(format[i]);
            }
        }
        
        if (!formatUsed || argIndex >= args.size()) {
            hasArgs = false;
        }
    }

    return 0;
}
