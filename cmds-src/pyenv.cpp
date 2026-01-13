#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

int spawn(std::string cmd) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        std::cerr << "Failed: " << GetLastError() << "\n";
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

std::string getRegValue(HKEY hKey, const char* subKey, const char* valueName) {
    HKEY hOpenedKey;
    if (RegOpenKeyExA(hKey, subKey, 0, KEY_READ, &hOpenedKey) != ERROR_SUCCESS) return "";
    
    char buffer[MAX_PATH];
    DWORD size = sizeof(buffer);
    std::string result = "";
    
    if (RegQueryValueExA(hOpenedKey, valueName, NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
        result = buffer;
    }
    RegCloseKey(hOpenedKey);
    return result;
}

std::string findPython() {
    const char* rootKeys[] = { 
        "Software\\Python\\PythonCore", 
        "Software\\Wow6432Node\\Python\\PythonCore" 
    };
    HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };

    for (HKEY root : roots) {
        for (const char* rootPath : rootKeys) {
            HKEY hKey;
            if (RegOpenKeyExA(root, rootPath, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hKey) == ERROR_SUCCESS) {
                char verBuf[256];
                DWORD verLen = sizeof(verBuf);
                DWORD index = 0;
                
                while (RegEnumKeyExA(hKey, index, verBuf, &verLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                    std::string installPathKey = std::string(rootPath) + "\\" + verBuf + "\\InstallPath";
                    std::string path = getRegValue(root, installPathKey.c_str(), NULL); 
                    
                    if (!path.empty()) {
                        std::string exe = path;
                        if (exe.back() != '\\') exe += "\\";
                        exe += "python.exe";
                        
                        if (GetFileAttributesA(exe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            RegCloseKey(hKey);
                            return exe;
                        }
                    }
                    index++;
                    verLen = sizeof(verBuf); 
                }
                RegCloseKey(hKey);
            }
        }
    }
    
    return "python"; 
}

void builtInLS() {
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile("*", &ffd);
    if (hFind == INVALID_HANDLE_VALUE) { return; }

    int count = 0;
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::cout << "\033[36m" << ffd.cFileName << "/\033[0m  ";
        } else {
            std::string name = ffd.cFileName;
            if (name.length() > 3 && name.substr(name.length()-3) == ".py") {
                 std::cout << "\033[32m" << name << "\033[0m  ";
            } else {
                 std::cout << name << "  ";
            }
        }
        count++;
        if (count % 4 == 0) std::cout << "\n";
    } while (FindNextFile(hFind, &ffd) != 0);
    std::cout << "\n";
    FindClose(hFind);
}

// Help command
void builtInHelp() {
    std::cout << "PyEnv - Python Environment Emulation\n";
    std::cout << "Commands:\n";
    std::cout << "  python <file>   Run python scripts (or ./file.py)\n";
    std::cout << "  cd <dir>        Change directory\n";
    std::cout << "  ls              List files (green=.py)\n";
    std::cout << "  help            Show this help\n";
    std::cout << "  exit            Exit to Linuxify\n\n";
}

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) != "init") {
        char* p = GetCommandLineA();
        bool inQuote = false;
        while (*p) {
            if (*p == '"') inQuote = !inQuote;
            else if (!inQuote && (*p == ' ' || *p == '\t')) { p++; break; }
            p++;
        }
        while (*p && (*p == ' ' || *p == '\t')) p++; 
        
        if (*p) {
             return spawn(p);
        }
    }

    std::cout << "[PyEnv] Initializing Environment...\n";

    std::string pythonExe = findPython();
    if (pythonExe == "python") {
        std::cout << "  Using System Python (from PATH)\n";
    } else {
        std::cout << "  Found Python: " << pythonExe << "\n";
    }
    
    SetEnvironmentVariableA("PYTHONIOENCODING", "utf-8");
    SetEnvironmentVariableA("TERM", "xterm-256color");

    std::cout << "\n[PyEnv] Environment Ready.\n";
    std::cout << "Type 'help' for commands, 'exit' to return.\n";

    std::string line;
    while (true) {
        std::cout << "(pyenv) \033[36m$ \033[0m";
        if (!std::getline(std::cin, line)) break;
        
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue; 
        size_t last = line.find_last_not_of(" \t");
        std::string trimmed = line.substr(first, (last - first + 1));

        if (trimmed == "exit" || trimmed == "quit") break;
        
        if (trimmed == "help") {
            builtInHelp();
            continue;
        }

        if (trimmed.rfind("cd ", 0) == 0 && trimmed.length() > 3) {
            std::string dir = trimmed.substr(3);
            if (!SetCurrentDirectoryA(dir.c_str())) {
                std::cerr << "cd: no such file or directory: " << dir << "\n";
            } else {
                char cwd[MAX_PATH];
                GetCurrentDirectoryA(MAX_PATH, cwd);
                std::cout << cwd << "\n";
            }
            continue;
        }
        
        if (trimmed == "ls") {
            builtInLS();
            continue;
        }
        
        std::string cmdName = trimmed;
        size_t spacePos = trimmed.find(' ');
        if (spacePos != std::string::npos) cmdName = trimmed.substr(0, spacePos);
        
        if (cmdName == "python" || cmdName == "python.exe") {
             spawn(trimmed);
             continue;
        }

        if (cmdName.length() > 3 && cmdName.substr(cmdName.length()-3) == ".py") {
             std::string newCmd = "\"" + pythonExe + "\" " + trimmed;
             spawn(newCmd);
             continue;
        }

        std::cerr << "pyenv: command not found: " << cmdName << "\n";
    }

    return 0;
}
