// Compile: g++ -std=c++17 -static -o ../cmds/tail.exe tail.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <chrono>

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
    bool follow = false;
    bool quiet = false;
    bool verbose = false;
    std::vector<std::string> files;
    int sleepInterval = 1000;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "--help") {
            std::cout << "Usage: tail [OPTION]... [FILE]...\n";
            std::cout << "Print the last 10 lines of each FILE to standard output.\n\n";
            std::cout << "Options:\n";
            std::cout << "  -n NUM        Print the last NUM lines instead of 10\n";
            std::cout << "  -c NUM        Print the last NUM bytes\n";
            std::cout << "  -f, --follow  Output appended data as the file grows\n";
            std::cout << "  -s NUM        Sleep for NUM seconds between follow iterations\n";
            std::cout << "  -q            Never print headers giving file names\n";
            std::cout << "  -v            Always print headers giving file names\n";
            std::cout << "  -NUM          Shorthand for -n NUM\n";
            std::cout << "      --help    Display this help and exit\n";
            return 0;
        }
        else if (arg == "-n" && i + 1 < args.size()) count = std::stoll(args[++i]);
        else if (arg == "-c" && i + 1 < args.size()) { count = std::stoll(args[++i]); useBytes = true; }
        else if (arg == "-f" || arg == "--follow") follow = true;
        else if (arg == "-q") quiet = true;
        else if (arg == "-v") verbose = true;
        else if (arg[0] == '-' && isdigit(arg[1])) count = std::abs(std::stoll(arg));
        else if (arg == "-s" && i + 1 < args.size()) sleepInterval = std::stoi(args[++i]) * 1000;
        else files.push_back(arg);
    }

    auto tailFile = [&](const std::string& path, bool showHeader) {
         std::ifstream file(path, std::ios::binary);
         if (!file) {
             printError("tail: cannot open '" + path + "'");
             return;
         }
         if (showHeader) std::cout << "==> " << path << " <==\n";

         if (useBytes) {
             file.seekg(0, std::ios::end);
             long long fileSize = file.tellg();
             long long startPos = (std::max)(0LL, fileSize - count);
             file.seekg(startPos);
             std::cout << file.rdbuf();
         } else {
             file.seekg(0, std::ios::end);
             long long fileSize = file.tellg();
             if (fileSize == 0) return;

             long long pos = fileSize;
             long long linesFound = 0;
             const int CHUNK = 4096;
             char buffer[CHUNK];
             
             if (fileSize < CHUNK * 2 && count > 100) {
                 file.seekg(0);
                 std::vector<std::string> buf;
                 std::string l;
                 while(std::getline(file, l)) {
                     buf.push_back(l);
                     if (buf.size() > count) buf.erase(buf.begin());
                 }
                 for(const auto& s : buf) std::cout << s << "\n";
                 return;
             }

             while (pos > 0 && linesFound <= count) {
                  long long toRead = (std::min)((long long)CHUNK, pos);
                  pos -= toRead;
                  file.seekg(pos);
                  file.read(buffer, toRead);
                  for (long long k = toRead - 1; k >= 0; --k) {
                      if (buffer[k] == '\n') {
                          linesFound++;
                          if (linesFound > count) {
                              pos += k + 1;
                              goto found_start;
                          }
                      }
                  }
             }
             pos = 0;
             
             found_start:
             file.seekg(pos);
             std::cout << file.rdbuf(); 
         }
         if (showHeader) std::cout << "\n";

         if (follow) {
             file.clear(); // Clear EOF
             std::streampos lastPos = file.tellg();
             while (true) {
                 file.seekg(0, std::ios::end);
                 std::streampos currentPos = file.tellg();
                 if (currentPos > lastPos) {
                     file.seekg(lastPos);
                     std::cout << file.rdbuf();
                     lastPos = currentPos;
                     std::cout.flush();
                 } else if (currentPos < lastPos) {
                     // File truncated
                     lastPos = 0;
                 }
                 std::this_thread::sleep_for(std::chrono::milliseconds(sleepInterval));
             }
         }
    };

    if (files.empty()) {
         printError("tail: missing file operand");
         return 1;
    } else {
         bool showHeader = (files.size() > 1 && !quiet) || verbose;
         for (size_t i = 0; i < files.size(); ++i) {
             tailFile(resolvePath(files[i]), showHeader);
             showHeader = (files.size() > 1 && !quiet);
         }
    }
    return 0;
}
