/*
 * cat - Concatenate files and print on the standard output
 * Part of Linuxify Project
 * 
 * Usage: cat [OPTION]... [FILE]...
 * 
 * Compile: g++ -std=c++17 -static -o cat.exe cat.cpp
 */

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <windows.h>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << "\033[31m" << msg << "\033[0m" << std::endl;
}

int main(int argc, char* argv[]) {
    // Enable ANSI colors for Windows console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(hOut, dwMode);
    // Also enable for Stderr
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    GetConsoleMode(hErr, &dwMode);
    dwMode |= 0x0004;
    SetConsoleMode(hErr, dwMode);

    // CRITICAL: Ensure Input is in expected mode (Echo + Line Input)
    // The shell might have left it in raw mode for its own input handling.
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hIn, &dwMode);
        dwMode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT; 
        SetConsoleMode(hIn, dwMode);
    }

    bool showNumbers = false;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-n" || arg == "--number") {
            showNumbers = true;
        } else {
            files.push_back(arg);
        }
    }

    // If no files specified, read from stdin
    if (files.empty()) {
        files.push_back("-");
    }

    constexpr size_t BUFFER_SIZE = 65536; 
    std::vector<char> buffer(BUFFER_SIZE);
    long long lineNum = 1;
    int exitCode = 0;

    for (const auto& file : files) {
        std::istream* input = &std::cin;
        std::ifstream ifs;

        if (file != "-") {
            if (!fs::exists(file)) {
                printError("cat: " + file + ": No such file or directory");
                exitCode = 1;
                continue;
            }

            if (fs::is_directory(file)) {
                printError("cat: " + file + ": Is a directory");
                exitCode = 1;
                continue;
            }

            ifs.open(file, std::ios::binary);
            if (!ifs) {
                printError("cat: " + file + ": Cannot open file");
                exitCode = 1;
                continue;
            }
            input = &ifs;
        }

        if (!showNumbers) {
            while (input->read(buffer.data(), BUFFER_SIZE) || input->gcount() > 0) {
                std::cout.write(buffer.data(), input->gcount());
                if (!*input) break; 
            }
            std::cout.flush();
        } else {
            bool newLine = true; 
            while (input->read(buffer.data(), BUFFER_SIZE) || input->gcount() > 0) {
                std::streamsize count = input->gcount();
                for (std::streamsize i = 0; i < count; ++i) {
                    if (newLine) {
                        std::cout << std::setw(6) << lineNum << "  ";
                        lineNum++;
                        newLine = false;
                    }
                    char c = buffer[i];
                    std::cout.put(c);
                    if (c == '\n') {
                        newLine = true;
                    }
                }
            }
            std::cout.flush();
        }
    }

    return exitCode;
}
