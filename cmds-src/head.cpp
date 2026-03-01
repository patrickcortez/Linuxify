// Compile: g++ -std=c++17 -static -o ../cmds/head.exe head.cpp
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

    long long count = 10;
    bool useBytes = false;
    bool quiet = false;
    bool verbose = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "--help") {
            std::cout << "Usage: head [OPTION]... [FILE]...\n";
            std::cout << "Print the first 10 lines of each FILE to standard output.\n\n";
            std::cout << "Options:\n";
            std::cout << "  -n NUM        Print the first NUM lines instead of 10\n";
            std::cout << "  -c NUM        Print the first NUM bytes\n";
            std::cout << "  -q, --quiet   Never print headers giving file names\n";
            std::cout << "  -v, --verbose Always print headers giving file names\n";
            std::cout << "  -NUM          Shorthand for -n NUM\n";
            std::cout << "      --help    Display this help and exit\n";
            return 0;
        }
        else if (arg == "-n" && i + 1 < args.size()) count = std::stoll(args[++i]);
        else if (arg == "-c" && i + 1 < args.size()) { count = std::stoll(args[++i]); useBytes = true; }
        else if (arg == "-q" || arg == "--quiet" || arg == "--silent") quiet = true;
        else if (arg == "-v" || arg == "--verbose") verbose = true;
        else if (arg[0] == '-' && isdigit(arg[1])) count = std::abs(std::stoll(arg));
        else files.push_back(arg);
    }

    auto process = [&](std::istream& is, const std::string& name, bool showHeader) {
        if (showHeader) {
            std::cout << "==> " << name << " <==\n";
        }
        if (useBytes) {
            char buf[4096];
            long long remaining = count;
            while (remaining > 0 && is) {
                long long toRead = (std::min)((long long)sizeof(buf), remaining);
                is.read(buf, toRead);
                std::cout.write(buf, is.gcount());
                remaining -= is.gcount();
            }
        } else {
            std::string line;
            long long remaining = count;
            while (remaining > 0 && std::getline(is, line)) {
                std::cout << line << "\n";
                remaining--;
            }
        }
        if (showHeader) std::cout << "\n";
    };

    if (files.empty()) {
        process(std::cin, "", false);
    } else {
         bool showHeader = (files.size() > 1 && !quiet) || verbose;
         for (const auto& file : files) {
             std::ifstream ifs(resolvePath(file), std::ios::binary);
             if (!ifs) {
                 printError("head: cannot open '" + file + "'");
                 continue;
             }
             process(ifs, file, showHeader);
             showHeader = (files.size() > 1 && !quiet);
         }
    }
    return 0;
}
