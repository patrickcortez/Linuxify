// Compiler Patch - Enforce No Comment Policy [RELEASE]
#include <string>
#include <vector>
#include <fstream>
#include <windows.h>
#include <iostream>

std::string getExtension(const std::string& path) {
    size_t dot = path.find_last_of(".");
    if (dot == std::string::npos) return "";
    return path.substr(dot);
}

std::string getDirectory(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}

bool hasComments(const std::string& filePath) {
    std::ifstream file(filePath.c_str());
    if (!file.is_open()) return false; 

    char c;
    bool inString = false;
    bool inChar = false;
    
    while (file.get(c)) {
        if (c == '"' && !inChar) {
            inString = !inString; 
        }
        else if (c == '\'' && !inString) {
            inChar = !inChar;
        }
        else if (!inString && !inChar && c == '/') {
            int next = file.peek();
            if (next == '/' || next == '*') {
                return true;
            }
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    // No debug output
    char selfPath[MAX_PATH];
    GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    std::string binDir = getDirectory(selfPath);
    std::string realCompiler = binDir + "\\g++.real.exe";

    if (GetFileAttributesA(realCompiler.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "CRITICAL ERROR: Real compiler (g++.real.exe) not found at " << realCompiler << std::endl;
        return 1;
    }

    std::vector<std::string> args;
    bool commentFound = false;
    std::string badFile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        args.push_back(arg);

        if (arg.size() > 2 && arg[0] != '-') {
            std::string ext = getExtension(arg);
            if (ext == ".cpp" || ext == ".c" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx") {
                if (hasComments(arg)) {
                    commentFound = true;
                    badFile = arg;
                    break;
                }
            }
        }
    }

    if (commentFound) {
        std::cerr << badFile << ":1:1: error: comments are STRICTLY FORBIDDEN by local policy." << std::endl;
        std::cerr << "       (remove all // and /* */ comments to compile)" << std::endl;
        return 1; 
    }

    std::string cmdLine = "\"" + realCompiler + "\"";
    for (size_t i = 0; i < args.size(); ++i) {
        cmdLine += " \"" + args[i] + "\"";
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    // Inherit handles
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, const_cast<char*>(cmdLine.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        std::cerr << "Error launching real compiler: " << GetLastError() << std::endl;
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode;
}
