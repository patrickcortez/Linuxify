// Compile: g++ -std=c++17 -static -o ../cmds/stat.exe stat.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <windows.h>

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

    if (args.size() < 2) {
        printError("stat: missing file operand");
        return 1;
    }
    
    std::string format;
    bool followSymlinks = false;
    bool terse = false;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if ((args[i] == "-c" || args[i] == "--format") && i + 1 < args.size()) {
            format = args[++i];
        } else if (args[i].substr(0, 10) == "--format=") {
            format = args[i].substr(10);
        } else if (args[i] == "-L" || args[i] == "--dereference") {
            followSymlinks = true;
        } else if (args[i] == "-t" || args[i] == "--terse") {
            terse = true;
        } else if (args[i][0] != '-') {
            files.push_back(args[i]);
        }
    }
    
    int exitCode = 0;
    for (const auto& arg : files) {
        std::string filePath = resolvePath(arg);
        
        if (followSymlinks && fs::is_symlink(filePath)) {
            try { filePath = fs::canonical(filePath).string(); } catch (...) {}
        }
        
        if (!fs::exists(filePath)) {
            printError("stat: cannot stat '" + arg + "': No such file or directory");
            exitCode = 1;
            continue;
        }
        
        try {
            auto fileSize = fs::is_regular_file(filePath) ? fs::file_size(filePath) : 0;
            auto lastWrite = fs::last_write_time(filePath);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                lastWrite - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            auto modTime = std::chrono::system_clock::to_time_t(sctp);
            
            std::string fileType;
            if (fs::is_regular_file(filePath)) fileType = "regular file";
            else if (fs::is_directory(filePath)) fileType = "directory";
            else if (fs::is_symlink(filePath)) fileType = "symbolic link";
            else fileType = "unknown";
            
            DWORD attrs = GetFileAttributesA(filePath.c_str());
            std::string attrStr;
            if (attrs & FILE_ATTRIBUTE_READONLY) attrStr += "r";
            else attrStr += "-";
            attrStr += "w";
            if (filePath.find(".exe") != std::string::npos || filePath.find(".bat") != std::string::npos)
                attrStr += "x";
            else attrStr += "-";
            
            BY_HANDLE_FILE_INFORMATION fileInfo;
            HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            DWORD nLinks = 1;
            DWORD fileIndex = 0;
            if (hFile != INVALID_HANDLE_VALUE) {
                if (GetFileInformationByHandle(hFile, &fileInfo)) {
                    nLinks = fileInfo.nNumberOfLinks;
                    fileIndex = fileInfo.nFileIndexLow;
                }
                CloseHandle(hFile);
            }
            
            if (!format.empty()) {
                std::string output = format;
                size_t pos;
                while ((pos = output.find("%n")) != std::string::npos)
                    output.replace(pos, 2, arg);
                while ((pos = output.find("%N")) != std::string::npos)
                    output.replace(pos, 2, "'" + arg + "'");
                while ((pos = output.find("%s")) != std::string::npos)
                    output.replace(pos, 2, std::to_string(fileSize));
                while ((pos = output.find("%F")) != std::string::npos)
                    output.replace(pos, 2, fileType);
                while ((pos = output.find("%A")) != std::string::npos)
                    output.replace(pos, 2, attrStr);
                while ((pos = output.find("%h")) != std::string::npos)
                    output.replace(pos, 2, std::to_string(nLinks));
                while ((pos = output.find("%i")) != std::string::npos)
                    output.replace(pos, 2, std::to_string(fileIndex));
                while ((pos = output.find("%Y")) != std::string::npos)
                    output.replace(pos, 2, std::to_string(modTime));
                while ((pos = output.find("%y")) != std::string::npos) {
                    char timeBuf[64];
                    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&modTime));
                    output.replace(pos, 2, timeBuf);
                }
                while ((pos = output.find("\\n")) != std::string::npos)
                    output.replace(pos, 2, "\n");
                std::cout << output << "\n";
            } else if (terse) {
                std::cout << arg << " " << fileSize << " " << nLinks << " " << modTime << "\n";
            } else {
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "  File: ";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                std::cout << arg << "\n";
                std::cout << "  Size: " << fileSize << "       \tBlocks: " << (fileSize / 512 + 1) 
                          << "     \tLinks: " << nLinks << "\n";
                std::cout << "  Type: " << fileType << "\n";
                std::cout << " Attrs: ";
                if (attrs & FILE_ATTRIBUTE_READONLY) std::cout << "readonly ";
                if (attrs & FILE_ATTRIBUTE_HIDDEN) std::cout << "hidden ";
                if (attrs & FILE_ATTRIBUTE_SYSTEM) std::cout << "system ";
                if (attrs & FILE_ATTRIBUTE_ARCHIVE) std::cout << "archive ";
                if (attrs & FILE_ATTRIBUTE_DIRECTORY) std::cout << "directory ";
                std::cout << "\n";
                std::cout << "Modify: " << std::ctime(&modTime);
                std::cout << "\n";
            }
        } catch (const std::exception& e) {
            printError("stat: " + std::string(e.what()));
            exitCode = 1;
        }
    }
    return exitCode;
}
