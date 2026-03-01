// Compile: g++ -std=c++17 -static -o ../cmds/wc.exe wc.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <iomanip>

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

    bool lines = false, words = false, chars = false, bytes = false;
    bool maxLine = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-l" || arg == "--lines") lines = true;
        else if (arg == "-w" || arg == "--words") words = true;
        else if (arg == "-m" || arg == "--chars") chars = true;
        else if (arg == "-c" || arg == "--bytes") bytes = true;
        else if (arg == "-L" || arg == "--max-line-length") maxLine = true;
        else files.push_back(arg);
    }

    if (!lines && !words && !chars && !bytes && !maxLine) {
        lines = words = bytes = true;
    }

    auto countFile = [&](std::istream& is, const std::string& name) {
         long long l = 0, w = 0, c = 0, b = 0, L = 0;
         long long currentL = 0;
         bool inWord = false;
         char buf[8192];
         
         while (is.read(buf, sizeof(buf)) || is.gcount() > 0) {
             size_t n = is.gcount();
             b += n;

             for (size_t i = 0; i < n; ++i) {
                 unsigned char ch = (unsigned char)buf[i];
                 if (ch == '\n') {
                     l++;
                     if (currentL > L) L = currentL;
                     currentL = 0;
                 } else {
                     if (chars) {
                          if ((ch & 0xC0) != 0x80) c++;
                     } else {
                          c++; 
                     }
                     if ((ch & 0xC0) != 0x80) currentL++;
                 }

                 if (isspace(ch)) {
                     inWord = false;
                 } else if (!inWord) {
                     inWord = true;
                     w++;
                 }
             }
             if (is.eof()) break;
         }
         if (currentL > L) L = currentL;

         if (lines) std::cout << std::setw(4) << l << " ";
         if (words) std::cout << std::setw(4) << w << " ";
         if (bytes) std::cout << std::setw(4) << b << " ";
         if (chars) std::cout << std::setw(4) << c << " ";
         if (maxLine) std::cout << std::setw(4) << L << " ";
         if (!name.empty()) std::cout << name;
         std::cout << std::endl;
         return std::make_tuple(l, w, b, c, L);
    };

    if (files.empty()) {
         countFile(std::cin, "");
    } else {
         long long tl=0, tw=0, tb=0, tc=0, tL=0;
         for (const auto& file : files) {
              std::ifstream ifs(resolvePath(file), std::ios::binary);
              if (!ifs) {
                  printError("wc: " + file + ": No such file or directory");
                  continue;
              }
              auto [l, w, b, c, L] = countFile(ifs, file);
              tl += l; tw += w; tb += b; tc += c; tL = (std::max)(tL, L);
         }
         if (files.size() > 1) {
             if (lines) std::cout << std::setw(4) << tl << " ";
             if (words) std::cout << std::setw(4) << tw << " ";
             if (bytes) std::cout << std::setw(4) << tb << " ";
             if (chars) std::cout << std::setw(4) << tc << " ";
             if (maxLine) std::cout << std::setw(4) << tL << " ";
             std::cout << "total" << std::endl;
         }
    }
    return 0;
}
