// Compile: g++ -std=c++17 -static -o ../cmds/find.exe find.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>

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

    std::vector<std::string> paths;
    std::string namePattern;
    std::string typeFilter; 
    long long sizeMin = -1, sizeMax = -1;
    int maxDepth = -1;
    bool exec = false;
    std::vector<std::string> execCommand;

    size_t i = 1;
    while (i < args.size() && args[i][0] != '-') {
        paths.push_back(args[i++]);
    }
    if (paths.empty()) paths.push_back(".");

    for (; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-name" && i + 1 < args.size()) namePattern = args[++i];
        else if (arg == "-type" && i + 1 < args.size()) typeFilter = args[++i];
        else if (arg == "-size" && i + 1 < args.size()) {
            std::string s = args[++i];
            bool gt = (s[0] == '+');
            bool lt = (s[0] == '-');
            if (gt || lt) s = s.substr(1);
            long long val = 0;
            try { 
                 size_t suffixPos = 0;
                 val = std::stoll(s, &suffixPos);
                 if (suffixPos < s.length()) {
                     char suf = s[suffixPos];
                     if (suf == 'k' || suf == 'K') val *= 1024;
                     else if (suf == 'M') val *= 1024*1024;
                     else if (suf == 'G') val *= 1024*1024*1024;
                 }
            } catch(...) {}
            
            if (gt) sizeMin = val + 1;
            else if (lt) sizeMax = val - 1;
            else { sizeMin = val; sizeMax = val; } 
        }
        else if (arg == "-maxdepth" && i + 1 < args.size()) maxDepth = std::stoi(args[++i]);
        else if (arg == "-exec") {
            exec = true;
            i++;
            while (i < args.size()) {
                if (args[i] == ";") break;
                execCommand.push_back(args[i]);
                i++;
            }
        }
    }

    int exitCode = 0;
    for (const auto& path : paths) {
        std::string root = resolvePath(path);
        if (!fs::exists(root)) {
            printError("find: '" + path + "': No such file or directory");
            exitCode = 1;
            continue;
        }

        try {
            auto walker = [&](auto&& self, const fs::path& p, int depth) -> void {
                if (maxDepth != -1 && depth > maxDepth) return;
                
                bool match = true;
                std::string filename = p.filename().string();
                
                if (!namePattern.empty()) {
                     if (namePattern.front() == '*' && namePattern.back() == '*') {
                         std::string sub = namePattern.substr(1, namePattern.length()-2);
                         if (filename.find(sub) == std::string::npos) match = false;
                     } else if (namePattern.front() == '*') {
                         std::string suf = namePattern.substr(1);
                         if (filename.length() < suf.length() || filename.substr(filename.length()-suf.length()) != suf) match=false;
                     } else if (namePattern.back() == '*') {
                         std::string pre = namePattern.substr(0, namePattern.length()-1);
                         if (filename.substr(0, pre.length()) != pre) match=false;
                     } else {
                         if (filename != namePattern) match = false;
                     }
                }

                if (match && !typeFilter.empty()) {
                    if (typeFilter == "f" && !fs::is_regular_file(p)) match = false;
                    else if (typeFilter == "d" && !fs::is_directory(p)) match = false;
                }

                if (match && (sizeMin != -1 || sizeMax != -1)) {
                     if (fs::is_regular_file(p)) {
                         uintmax_t sz = fs::file_size(p);
                         if (sizeMin != -1 && sz < (uintmax_t)sizeMin) match = false;
                         if (sizeMax != -1 && sz > (uintmax_t)sizeMax) match = false;
                     } else {
                         match = false;
                     }
                }

                if (match) {
                    if (exec) {
                        std::string cmd;
                        for (const auto& part : execCommand) {
                            if (part == "{}") cmd += "\"" + p.string() + "\" ";
                            else cmd += part + " ";
                        }
                        std::system(cmd.c_str());
                    } else {
                        std::cout << p.string() << std::endl;
                    }
                }

                if (fs::is_directory(p)) {
                    for (const auto& entry : fs::directory_iterator(p)) {
                         self(self, entry.path(), depth + 1);
                    }
                }
            };
            
            walker(walker, root, 0);

        } catch (const std::exception& e) {
            printError("find: " + std::string(e.what()));
            exitCode = 1;
        }
    }
    return exitCode;
}
