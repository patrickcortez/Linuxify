#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

std::string getExtension(const std::string& path) {
    size_t dot = path.find_last_of(".");
    if (dot == std::string::npos) return "";
    return path.substr(dot);
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

void CheckPolicy() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeName = exePath;
    size_t slash = exeName.find_last_of("\\/");
    if (slash != std::string::npos) exeName = exeName.substr(slash + 1);
    
    bool isCompiler = (exeName.find("g++") != std::string::npos) || 
                      (exeName.find("gcc") != std::string::npos) || 
                      (exeName.find("cpp") != std::string::npos) || 
                      (exeName.find("cc1plus") != std::string::npos);

    if (!isCompiler) return;

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    for (int i = 1; i < argc; i++) {
        std::wstring warg = argv[i];
        std::string arg(warg.begin(), warg.end());
        
        if (arg.empty() || arg[0] == '-') continue;
        
        std::string ext = getExtension(arg);
        if (ext == ".cpp" || ext == ".c" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx") {
            if (hasComments(arg)) {
                
                HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
                std::string msg = "\n\n"
                    "===========================================================\n"
                    "FATAL ERROR: COMMENTS DETECTED IN " + arg + "\n"
                    "The compiler refused to process this file due to Strict\n"
                    "No-Comment Policy. Remove all // and /* */ comments.\n"
                    "===========================================================\n\n";
                
                DWORD written;
                WriteFile(hStdErr, msg.c_str(), msg.length(), &written, NULL);

                TerminateProcess(GetCurrentProcess(), 1);
            }
        }
    }
    LocalFree(argv);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CheckPolicy();
    }
    return TRUE;
}
