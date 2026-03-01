// Compile: g++ -std=c++17 -static -o ../cmds/chown.exe chown.cpp -ladvapi32
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <windows.h>
#include <accctrl.h>
#include <aclapi.h>

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

    if (args.size() < 3) {
        printError("chown: missing operand");
        std::cout << "Usage: chown [-R] <owner> <file>..." << std::endl;
        return 1;
    }

    bool recursive = false;
    bool verbose = false;
    std::string owner;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg == "-R" || arg == "--recursive") recursive = true;
        else if (arg == "-v" || arg == "--verbose") verbose = true;
        else if (owner.empty() && arg[0] != '-') owner = arg;
        else if (arg[0] != '-') files.push_back(arg);
    }

    if (owner.empty() || files.empty()) {
         printError("chown: missing owner or file operand");
         return 1;
    }

    size_t colon = owner.find(':');
    if (colon != std::string::npos) {
        owner = owner.substr(0, colon);
    }

    int exitCode = 0;
    for (const auto& file : files) {
         std::string root = resolvePath(file);
         if (!fs::exists(root)) {
             printError("chown: cannot access '" + file + "': No such file or directory");
             exitCode = 1;
             continue;
         }
         
         PSID pSid = NULL;
         SID_NAME_USE sidType;
         DWORD sidSize = 0, domainSize = 0;
         char domain[256];
         
         LookupAccountNameA(NULL, owner.c_str(), NULL, &sidSize, domain, &domainSize, &sidType);
         pSid = (PSID)LocalAlloc(LMEM_FIXED, sidSize);
         domainSize = sizeof(domain);
         
         bool success = false;
         if (LookupAccountNameA(NULL, owner.c_str(), pSid, &sidSize, domain, &domainSize, &sidType)) {
             DWORD result = SetNamedSecurityInfoA(
                 (LPSTR)root.c_str(),
                 SE_FILE_OBJECT,
                 OWNER_SECURITY_INFORMATION,
                 pSid,
                 NULL, NULL, NULL
             );
             success = (result == ERROR_SUCCESS);
             
             if (success && recursive && fs::is_directory(root)) {
                 for (const auto& entry : fs::recursive_directory_iterator(root)) {
                     SetNamedSecurityInfoA(
                         (LPSTR)entry.path().string().c_str(),
                         SE_FILE_OBJECT,
                         OWNER_SECURITY_INFORMATION,
                         pSid,
                         NULL, NULL, NULL
                     );
                 }
             }
         }
         
         if (pSid) LocalFree(pSid);
         
         if (success) {
             if (verbose) std::cout << "ownership of '" + file + "' retained as " + owner << std::endl;
         } else {
             printError("chown: changing ownership of '" + file + "': Operation not permitted (or user invalid)");
             exitCode = 1;
         }
    }
    return exitCode;
}
