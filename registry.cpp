// Linuxify Registry - External Package Management Implementation
// This module discovers and manages user-installed packages,
// allowing them to be executed directly from Linuxify shell.

#include "registry.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

// Global registry instance
LinuxifyRegistry g_registry;

LinuxifyRegistry::LinuxifyRegistry() {
    linuxdbPath = getLinuxdbPath();
    registryFilePath = getRegistryFilePath();
    
    // Initialize common commands list
    commonCommands = {
        // Version control
        "git", "svn", "hg",
        // Databases
        "mysql", "psql", "postgres", "mongod", "mongo", "mongosh", "redis-cli", "redis-server", "sqlite3",
        // Node/JavaScript
        "node", "npm", "npx", "yarn", "pnpm", "bun", "deno",
        // Python
        "python", "python3", "pip", "pip3", "conda", "pipenv", "poetry",
        // Ruby
        "ruby", "gem", "bundle", "bundler", "rails",
        // PHP
        "php", "composer",
        // Go
        "go", "gofmt",
        // Rust
        "rustc", "cargo", "rustup",
        // Java/JVM
        "java", "javac", "mvn", "gradle",
        // C/C++
        "gcc", "g++", "clang", "clang++", "make", "cmake", "ninja",
        // Cloud/DevOps
        "docker", "docker-compose", "kubectl", "helm", "terraform", "vagrant", "ansible",
        // Utilities
        "curl", "wget", "ssh", "scp", "rsync", "grep", "awk", "sed", "tar",
        "7z", "ffmpeg", "imagemagick", "convert", "pandoc",
        // Package managers
        "choco", "scoop", "winget",
        // Editors
        "vim", "nvim", "code", "subl",
        // Network
        "netstat", "ping", "tracert", "nslookup", "dig",
        // Misc
        "htop", "btop", "tree", "which", "whereis", "find", "locate",
        "jq", "yq", "rg", "ripgrep", "fd", "bat", "exa", "fzf"
    };
}

std::string LinuxifyRegistry::getLinuxdbPath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path dbPath = exeDir / "linuxdb";
    
    // Create directory if it doesn't exist
    if (!fs::exists(dbPath)) {
        fs::create_directories(dbPath);
    }
    
    return dbPath.string();
}

std::string LinuxifyRegistry::getRegistryFilePath() {
    return (fs::path(linuxdbPath) / "registry.lin").string();
}

std::string LinuxifyRegistry::getDbPath() {
    return linuxdbPath;
}

std::string LinuxifyRegistry::findInPath(const std::string& command) {
    char* pathEnv = nullptr;
    size_t pathLen = 0;
    _dupenv_s(&pathEnv, &pathLen, "PATH");
    
    if (!pathEnv) return "";
    
    std::string pathStr(pathEnv);
    free(pathEnv);
    
    std::vector<std::string> extensions = {".exe", ".cmd", ".bat", ".ps1", ".com", ""};
    std::stringstream ss(pathStr);
    std::string pathDir;
    
    while (std::getline(ss, pathDir, ';')) {
        if (pathDir.empty()) continue;
        
        for (const auto& ext : extensions) {
            fs::path fullPath = fs::path(pathDir) / (command + ext);
            try {
                if (fs::exists(fullPath) && fs::is_regular_file(fullPath)) {
                    return fullPath.string();
                }
            } catch (...) {
                continue;
            }
        }
    }
    
    return "";
}

std::string LinuxifyRegistry::findInCommonDirs(const std::string& command) {
    std::vector<std::string> commonDirs;
    
    // Get environment variables for common paths
    char* programFiles = nullptr;
    char* programFilesX86 = nullptr;
    char* localAppData = nullptr;
    char* appData = nullptr;
    char* userProfile = nullptr;
    size_t len;
    
    _dupenv_s(&programFiles, &len, "ProgramFiles");
    _dupenv_s(&programFilesX86, &len, "ProgramFiles(x86)");
    _dupenv_s(&localAppData, &len, "LOCALAPPDATA");
    _dupenv_s(&appData, &len, "APPDATA");
    _dupenv_s(&userProfile, &len, "USERPROFILE");
    
    if (programFiles) {
        commonDirs.push_back(std::string(programFiles) + "\\Git\\bin");
        commonDirs.push_back(std::string(programFiles) + "\\Git\\cmd");
        commonDirs.push_back(std::string(programFiles) + "\\nodejs");
        commonDirs.push_back(std::string(programFiles) + "\\MySQL\\MySQL Server 8.0\\bin");
        commonDirs.push_back(std::string(programFiles) + "\\PostgreSQL\\15\\bin");
        commonDirs.push_back(std::string(programFiles) + "\\Docker\\Docker\\resources\\bin");
        commonDirs.push_back(std::string(programFiles) + "\\Python312");
        commonDirs.push_back(std::string(programFiles) + "\\Python311");
        commonDirs.push_back(std::string(programFiles) + "\\Python310");
        free(programFiles);
    }
    
    if (programFilesX86) {
        commonDirs.push_back(std::string(programFilesX86) + "\\Git\\bin");
        free(programFilesX86);
    }
    
    if (localAppData) {
        commonDirs.push_back(std::string(localAppData) + "\\Programs\\Git\\bin");
        commonDirs.push_back(std::string(localAppData) + "\\Programs\\Python\\Python312");
        commonDirs.push_back(std::string(localAppData) + "\\Programs\\Microsoft VS Code\\bin");
        free(localAppData);
    }
    
    if (appData) {
        commonDirs.push_back(std::string(appData) + "\\npm");
        commonDirs.push_back(std::string(appData) + "\\Python\\Python312\\Scripts");
        free(appData);
    }
    
    if (userProfile) {
        commonDirs.push_back(std::string(userProfile) + "\\.cargo\\bin");
        commonDirs.push_back(std::string(userProfile) + "\\go\\bin");
        commonDirs.push_back(std::string(userProfile) + "\\scoop\\shims");
        free(userProfile);
    }
    
    std::vector<std::string> extensions = {".exe", ".cmd", ".bat", ""};
    
    for (const auto& dir : commonDirs) {
        for (const auto& ext : extensions) {
            fs::path fullPath = fs::path(dir) / (command + ext);
            try {
                if (fs::exists(fullPath) && fs::is_regular_file(fullPath)) {
                    return fullPath.string();
                }
            } catch (...) {
                continue;
            }
        }
    }
    
    return "";
}

void LinuxifyRegistry::loadRegistry() {
    if (isLoaded) return;
    
    std::ifstream file(registryFilePath);
    if (file) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string cmd = line.substr(0, pos);
                std::string path = line.substr(pos + 1);
                
                // Trim whitespace
                cmd.erase(0, cmd.find_first_not_of(" \t"));
                cmd.erase(cmd.find_last_not_of(" \t\r\n") + 1);
                path.erase(0, path.find_first_not_of(" \t"));
                path.erase(path.find_last_not_of(" \t\r\n") + 1);
                
                if (!cmd.empty() && !path.empty()) {
                    commandRegistry[cmd] = path;
                }
            }
        }
    }
    isLoaded = true;
}

void LinuxifyRegistry::saveRegistry() {
    std::ofstream file(registryFilePath);
    if (!file) return;
    
    file << "# Linuxify Command Registry\n";
    file << "# Auto-generated - Maps commands to executable paths\n";
    file << "# Stored in: linuxdb/registry.lin\n\n";
    
    for (const auto& pair : commandRegistry) {
        file << pair.first << "=" << pair.second << "\n";
    }
}

int LinuxifyRegistry::refreshRegistry() {
    commandRegistry.clear();
    int foundCount = 0;
    
    for (const auto& cmd : commonCommands) {
        // First try PATH
        std::string path = findInPath(cmd);
        
        // If not found, try common directories
        if (path.empty()) {
            path = findInCommonDirs(cmd);
        }
        
        if (!path.empty()) {
            commandRegistry[cmd] = path;
            foundCount++;
        }
    }
    
    isLoaded = true;
    saveRegistry();
    return foundCount;
}

bool LinuxifyRegistry::isRegistered(const std::string& command) {
    loadRegistry();
    return commandRegistry.find(command) != commandRegistry.end();
}

std::string LinuxifyRegistry::getExecutablePath(const std::string& command) {
    loadRegistry();
    
    auto it = commandRegistry.find(command);
    if (it != commandRegistry.end()) {
        // Verify the path still exists
        if (fs::exists(it->second)) {
            return it->second;
        }
        // Path no longer exists, try to find it again
        std::string newPath = findInPath(command);
        if (newPath.empty()) {
            newPath = findInCommonDirs(command);
        }
        if (!newPath.empty()) {
            commandRegistry[command] = newPath;
            saveRegistry();
            return newPath;
        }
    }
    
    // Not in registry, try to find it now
    std::string path = findInPath(command);
    if (path.empty()) {
        path = findInCommonDirs(command);
    }
    
    // If found, add to registry
    if (!path.empty()) {
        commandRegistry[command] = path;
        saveRegistry();
    }
    
    return path;
}

bool LinuxifyRegistry::executeRegisteredCommand(const std::string& command, const std::vector<std::string>& args, const std::string& currentDir) {
    std::string exePath = getExecutablePath(command);
    
    if (exePath.empty()) {
        return false;
    }
    
    // Check if it's a shell script (.sh file)
    std::string ext = fs::path(exePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    std::string cmdLine;
    
    if (ext == ".sh") {
        // Parse shebang to determine interpreter - REQUIRED
        std::string interpreterPath;
        std::string interpreterSpec;  // Could be registry name or absolute path
        bool hasShebang = false;
        
        std::ifstream scriptFile(exePath);
        if (scriptFile) {
            std::string firstLine;
            std::getline(scriptFile, firstLine);
            scriptFile.close();
            
            // Check for shebang #!
            if (firstLine.size() > 2 && firstLine[0] == '#' && firstLine[1] == '!') {
                hasShebang = true;
                std::string shebang = firstLine.substr(2);
                // Trim whitespace
                shebang.erase(0, shebang.find_first_not_of(" \t\r\n"));
                shebang.erase(shebang.find_last_not_of(" \t\r\n") + 1);
                
                // Get the interpreter spec (first word, rest are args)
                size_t spacePos = shebang.find(' ');
                interpreterSpec = (spacePos != std::string::npos) ? shebang.substr(0, spacePos) : shebang;
            }
        }
        
        // Shebang is required
        if (!hasShebang) {
            std::cerr << "Script missing shebang line: " << exePath << std::endl;
            std::cerr << "Add a shebang: #!<interpreter> (registry name or absolute path)" << std::endl;
            std::cerr << "Example: #!lish  or  #!C:\\path\\to\\interpreter.exe" << std::endl;
            return false;
        }
        
        if (interpreterSpec.empty()) {
            std::cerr << "Invalid shebang - no interpreter specified" << std::endl;
            return false;
        }
        
        // Check if it's an absolute path or a registry name
        fs::path specPath(interpreterSpec);
        if (specPath.is_absolute() && fs::exists(interpreterSpec)) {
            // Direct absolute path
            interpreterPath = interpreterSpec;
        } else {
            // Try to look up in registry
            std::string regPath = getExecutablePath(interpreterSpec);
            if (!regPath.empty() && fs::exists(regPath)) {
                interpreterPath = regPath;
            } else if (fs::exists(interpreterSpec)) {
                // Relative path that exists
                interpreterPath = fs::absolute(interpreterSpec).string();
            } else {
                std::cerr << "Interpreter not found: " << interpreterSpec << std::endl;
                std::cerr << "Either add it to registry: registry add " << interpreterSpec << " <path>" << std::endl;
                std::cerr << "Or use an absolute path in the shebang" << std::endl;
                return false;
            }
        }
        
        // Build command line with interpreter
        cmdLine = "\"" + interpreterPath + "\" \"" + exePath + "\"";
        for (size_t i = 1; i < args.size(); i++) {
            cmdLine += " \"" + args[i] + "\"";
        }
        
        // Use CreateProcessA for proper path handling
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        char cmdBuffer[4096];
        strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer));
        
        if (CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            currentDir.c_str(),
            &si,
            &pi
        )) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            DWORD err = GetLastError();
            std::cerr << "Failed to execute script (error " << err << ")" << std::endl;
            return false;
        }
    } else {
        // Regular executable - use CreateProcessA for better performance
        cmdLine = "\"" + exePath + "\"";
        for (size_t i = 1; i < args.size(); i++) {
            cmdLine += " \"" + args[i] + "\"";
        }
        
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        ZeroMemory(&pi, sizeof(pi));
        
        char cmdBuffer[4096];
        strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer));
        
        if (CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,   // Inherit handles for stdin/stdout
            0,
            NULL,
            currentDir.c_str(),
            &si,
            &pi
        )) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            DWORD err = GetLastError();
            std::cerr << "Failed to execute command (error " << err << ")" << std::endl;
            return false;
        }
    }
    
    return true;
}

const std::map<std::string, std::string>& LinuxifyRegistry::getAllCommands() {
    loadRegistry();
    return commandRegistry;
}

void LinuxifyRegistry::addCommand(const std::string& command, const std::string& path) {
    loadRegistry();
    commandRegistry[command] = path;
    saveRegistry();
}

void LinuxifyRegistry::removeCommand(const std::string& command) {
    loadRegistry();
    commandRegistry.erase(command);
    saveRegistry();
}
