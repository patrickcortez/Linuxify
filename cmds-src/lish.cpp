// Linuxify Shell Resolver (lish)
// Compile: g++ -std=c++17 -static -o cmds\lish.exe cmds-src\lish.cpp


#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <filesystem>
#include <windows.h>
#include <algorithm>

namespace fs = std::filesystem;

// Helper to get linuxify.exe path (parent of cmds dir)
std::string getLinuxifyPath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    return (fs::path(exePath).parent_path().parent_path() / "linuxify.exe").string();
}

// execute a command and wait for it
int runProcess(const std::string& cmdLine, const std::string& currentDir = "") {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    ZeroMemory(&pi, sizeof(pi));
    
    char cmdBuffer[8192];
    strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);
    
    if (!CreateProcessA(
        NULL,
        cmdBuffer,
        NULL,
        NULL,
        TRUE,   // Inherit handles
        0,
        NULL,
        currentDir.empty() ? NULL : currentDir.c_str(),
        &si,
        &pi
    )) {
        std::cerr << "lish: process launch failed (error " << GetLastError() << ")\n";
        return 127;
    }
    
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return (int)exitCode;
}

int main(int argc, char* argv[]) {
    std::string linuxifyExe = getLinuxifyPath();
    
    if (!fs::exists(linuxifyExe)) {
        // Fallback if directory structure is different (e.g. dev/flat)
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        linuxifyExe = (fs::path(exePath).parent_path() / "linuxify.exe").string();
        
        if (!fs::exists(linuxifyExe)) {
            std::cerr << "lish: fatal: could not locate linuxify.exe\n";
            return 1;
        }
    }

    if (argc < 2) {
        // Interactive mode: launch linuxify
        return runProcess("\"" + linuxifyExe + "\"");
    }
    
    std::string arg1 = argv[1];
    
    // Pass-through flags to linuxify
    if (arg1 == "-c" || arg1 == "--help" || arg1 == "-h" || arg1 == "--version") {
        std::string cmd = "\"" + linuxifyExe + "\"";
        for (int i = 1; i < argc; i++) {
            cmd += " \"" + std::string(argv[i]) + "\"";
        }
        return runProcess(cmd);
    }
    
    // Script execution
    std::string scriptPath = arg1;
    if (!fs::exists(scriptPath)) {
        std::cerr << "lish: " << scriptPath << ": No such file\n";
        return 1;
    }
    
    // Read shebang
    std::ifstream file(scriptPath);
    std::string firstLine;
    std::getline(file, firstLine);
    
    bool useDefaultInfo = true;
    std::string interpreterCmd;
    
    if (firstLine.size() > 2 && firstLine[0] == '#' && firstLine[1] == '!') {
        std::string shebang = firstLine.substr(2);
        // Trim whitespace
        shebang.erase(0, shebang.find_first_not_of(" \t\r\n"));
        shebang.erase(shebang.find_last_not_of(" \t\r\n") + 1);
        
        // Parse interpreter
        size_t spacePos = shebang.find(' ');
        std::string interpreterSpec = (spacePos != std::string::npos) ? shebang.substr(0, spacePos) : shebang;
        
        // Normalize name
        std::string interpreterName = interpreterSpec;
        if (interpreterName.size() > 4 && interpreterName.substr(interpreterName.size() - 4) == ".exe") {
            interpreterName = interpreterName.substr(0, interpreterName.size() - 4);
        }
        size_t lastSlash = interpreterName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            interpreterName = interpreterName.substr(lastSlash + 1);
        }
        std::transform(interpreterName.begin(), interpreterName.end(), interpreterName.begin(), ::tolower);
        
        if (interpreterName == "default" || interpreterName == "lish" || interpreterName == "bash" || interpreterName == "sh") {
            useDefaultInfo = true;
        } else {
            // External interpreter!
            useDefaultInfo = false;
            
            // If absolute path, use it directly
            // If simple name (python), use SearchPath or Registry resolution (which we might delegate to system?)
            // Simple approach: Use SearchPathA
            
            std::string resolvedPath = interpreterSpec;
             if (GetFileAttributesA(interpreterSpec.c_str()) == INVALID_FILE_ATTRIBUTES) {
                char pathBuf[MAX_PATH];
                if (SearchPathA(NULL, interpreterSpec.c_str(), ".exe", MAX_PATH, pathBuf, NULL)) {
                    resolvedPath = pathBuf;
                } else {
                     // Try adding .exe
                    if (SearchPathA(NULL, (interpreterSpec + ".exe").c_str(), NULL, MAX_PATH, pathBuf, NULL)) {
                        resolvedPath = pathBuf;
                    }
                }
            }
            
            // Build command: "interpreter" "script" [args...]
            interpreterCmd = "\"" + resolvedPath + "\" \"" + scriptPath + "\"";
            for (int i = 2; i < argc; i++) {
                interpreterCmd += " \"" + std::string(argv[i]) + "\"";
            }
        }
    }
    
    if (useDefaultInfo) {
        // Execute with main shell: linuxify script.sh [args]
        // Linuxify main needs to handle reading the file itself.
        // We just pass it as an argument.
        std::string cmd = "\"" + linuxifyExe + "\" \"" + scriptPath + "\"";
        for (int i = 2; i < argc; i++) {
            cmd += " \"" + std::string(argv[i]) + "\"";
        }
        return runProcess(cmd);
    } else {
        return runProcess(interpreterCmd);
    }
}
