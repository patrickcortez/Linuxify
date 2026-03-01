// Compile: g++ -std=c++17 -static -o ../cmds/du.exe du.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <functional>
#include <sstream>
#include <iomanip>
#include <regex>
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

    bool humanReadable = false;
    bool summary = false;
    bool showAll = false;
    bool showTotal = false;
    bool showTime = false;
    bool apparentSize = false;
    int maxDepth = -1;
    int blockSize = 1024;
    std::string excludePattern;
    std::vector<std::string> paths;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-h" || args[i] == "--human-readable") humanReadable = true;
        else if (args[i] == "-s" || args[i] == "--summarize") summary = true;
        else if (args[i] == "-a" || args[i] == "--all") showAll = true;
        else if (args[i] == "-c" || args[i] == "--total") showTotal = true;
        else if (args[i] == "-b" || args[i] == "--bytes") { blockSize = 1; apparentSize = true; }
        else if (args[i] == "-k") blockSize = 1024;
        else if (args[i] == "-m") blockSize = 1024 * 1024;
        else if (args[i] == "--time") showTime = true;
        else if (args[i] == "--apparent-size") apparentSize = true;
        else if ((args[i] == "-d" || args[i] == "--max-depth") && i + 1 < args.size()) {
            try { maxDepth = std::stoi(args[++i]); } catch (...) {}
        } else if (args[i].substr(0, 2) == "-d") {
            try { maxDepth = std::stoi(args[i].substr(2)); } catch (...) {}
        } else if ((args[i] == "--exclude") && i + 1 < args.size()) {
            excludePattern = args[++i];
        } else if (args[i][0] != '-') {
            paths.push_back(args[i]);
        }
    }
    
    if (paths.empty()) paths.push_back(".");
    
    auto formatSize = [humanReadable, blockSize](uintmax_t bytes) -> std::string {
        if (humanReadable) {
            const char* units[] = {"B", "K", "M", "G", "T"};
            int unit = 0;
            double size = (double)bytes;
            while (size >= 1024 && unit < 4) { size /= 1024; unit++; }
            std::ostringstream oss;
            if (unit == 0) oss << (int)size << units[unit];
            else oss << std::fixed << std::setprecision(1) << size << units[unit];
            return oss.str();
        }
        return std::to_string(bytes / blockSize);
    };
    
    uintmax_t grandTotal = 0;
    int exitCode = 0;
    
    for (const auto& path : paths) {
        std::string fullPath = resolvePath(path);
        
        if (!fs::exists(fullPath)) {
            printError("du: cannot access '" + path + "': No such file or directory");
            exitCode = 1;
            continue;
        }
        
        std::function<uintmax_t(const fs::path&, int)> calcSize;
        calcSize = [&](const fs::path& p, int depth) -> uintmax_t {
            uintmax_t total = 0;
            
            try {
                if (fs::is_regular_file(p)) {
                    uintmax_t fsize = fs::file_size(p);
                    if (showAll && !summary) {
                        std::cout << std::setw(8) << formatSize(fsize) << "\t" << p.string() << "\n";
                    }
                    return fsize;
                }
                
                for (const auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                    std::string name = entry.path().filename().string();
                    if (!excludePattern.empty()) {
                        try {
                            std::regex re(excludePattern);
                            if (std::regex_search(name, re)) continue;
                        } catch (...) {
                            if (name.find(excludePattern) != std::string::npos) continue;
                        }
                    }
                    
                    if (entry.is_directory()) {
                        uintmax_t dirSize = calcSize(entry.path(), depth + 1);
                        total += dirSize;
                        
                        if (!summary && (maxDepth < 0 || depth < maxDepth)) {
                            std::cout << std::setw(8) << formatSize(dirSize) << "\t";
                            if (showTime) {
                                auto lwt = fs::last_write_time(entry.path());
                                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                    lwt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                                auto time = std::chrono::system_clock::to_time_t(sctp);
                                char timeBuf[64];
                                strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&time));
                                std::cout << timeBuf << " ";
                            }
                            std::cout << entry.path().string() << "\n";
                        }
                    } else if (entry.is_regular_file()) {
                        uintmax_t fsize = fs::file_size(entry.path());
                        total += fsize;
                        if (showAll && !summary && (maxDepth < 0 || depth < maxDepth)) {
                            std::cout << std::setw(8) << formatSize(fsize) << "\t";
                            if (showTime) {
                                auto lwt = fs::last_write_time(entry.path());
                                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                    lwt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                                auto time = std::chrono::system_clock::to_time_t(sctp);
                                char timeBuf[64];
                                strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&time));
                                std::cout << timeBuf << " ";
                            }
                            std::cout << entry.path().string() << "\n";
                        }
                    }
                }
            } catch (...) {}
            
            return total;
        };
        
        uintmax_t totalSize = calcSize(fullPath, 0);
        std::cout << std::setw(8) << formatSize(totalSize) << "\t" << path << "\n";
        grandTotal += totalSize;
    }
    
    if (showTotal && paths.size() > 1) {
        std::cout << std::setw(8) << formatSize(grandTotal) << "\ttotal\n";
    }
    return exitCode;
}
