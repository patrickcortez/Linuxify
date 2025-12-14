// Node - Graph-Based Virtual File System for Linuxify
// Entry point and CLI handler
// Compile: g++ -std=c++17 -static -o cmds\node.exe cmds-src\node.cpp

#include "node.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <conio.h> // For _getch() password input

namespace fs = std::filesystem;

// Get the centralized nodes directory - checks multiple locations for seamless install
std::string getNodesDir() {
    std::vector<fs::path> candidates;
    
    // 1. Check relative to executable (installed or development)
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    fs::path exePath(buffer);
    fs::path exeDir = exePath.parent_path();
    
    // Structure: <root>/cmds/node.exe -> <root>/linuxdb/nodes/
    fs::path rootFromExe = exeDir.parent_path();
    candidates.push_back(rootFromExe / "linuxdb" / "nodes");
    
    // Also check if exe is directly in root (alternative layout)
    candidates.push_back(exeDir / "linuxdb" / "nodes");
    
    // 2. Check LINUXIFY_HOME environment variable
    char* linuxifyHome = std::getenv("LINUXIFY_HOME");
    if (linuxifyHome && strlen(linuxifyHome) > 0) {
        candidates.push_back(fs::path(linuxifyHome) / "linuxdb" / "nodes");
    }
    
    // 3. Check common Windows install locations
    char* programFiles = std::getenv("ProgramFiles");
    if (programFiles) {
        candidates.push_back(fs::path(programFiles) / "Linuxify" / "linuxdb" / "nodes");
    }
    
    char* programFilesX86 = std::getenv("ProgramFiles(x86)");
    if (programFilesX86) {
        candidates.push_back(fs::path(programFilesX86) / "Linuxify" / "linuxdb" / "nodes");
    }
    
    // 4. Check user's home directory (fallback/portable)
    char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        candidates.push_back(fs::path(userProfile) / ".linuxify" / "nodes");
        candidates.push_back(fs::path(userProfile) / "Linuxify" / "linuxdb" / "nodes");
    }
    
    // Find first existing path, or first path where parent exists (can create)
    fs::path bestCandidate;
    bool foundExisting = false;
    
    for (const auto& candidate : candidates) {
        try {
            if (fs::exists(candidate)) {
                return candidate.string();
            }
            // Track first candidate where parent directory exists
            if (!foundExisting && bestCandidate.empty()) {
                fs::path parent = candidate.parent_path();
                if (fs::exists(parent) || (parent.parent_path().empty() == false && fs::exists(parent.parent_path()))) {
                    bestCandidate = candidate;
                }
            }
        } catch (...) {
            // Skip inaccessible paths
        }
    }
    
    // Return best candidate (will be created later)
    if (!bestCandidate.empty()) {
        return bestCandidate.string();
    }
    
    // Ultimate fallback: relative to exe
    return (rootFromExe / "linuxdb" / "nodes").string();
}

// Securely read password from stdin
std::string readPassword(const std::string& prompt) {
    std::cout << prompt;
    std::string password;
    char ch;
    
    while ((ch = _getch()) != '\r') {
        if (ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b";
            }
        } else {
            password += ch;
            std::cout << "*";
        }
    }
    std::cout << "\n";
    return password;
}

void printUsage() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    
    SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << "Node - Graph-Based Virtual File System v3.0\n";
    SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    std::cout << "A fully encrypted virtual file system stored in an image file\n\n";
    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    
    std::cout << "Usage:\n";
    std::cout << "  node init [options] <name>       Create new file system image\n";
    std::cout << "  node mount <name>                Mount image\n";
    std::cout << "  node list                        List available node images\n";
    std::cout << "  node remove <name>               Delete node image file\n";
    std::cout << "  node --help                      Show this help\n\n";
    
    std::cout << "Init Options:\n";
    std::cout << "  --size <MB>       Size in megabytes (default: 10)\n";
    std::cout << "  --password        Enable hardened encryption (PBKDF2 + AES-CTR style)\n";
    std::cout << "  --maxfile <KB>    Max file size in KB (0 = unlimited, default)\n\n";
    
    std::cout << "Security:\n";
    std::cout << "  - Encrypted files appear as random data to external tools\n";
    std::cout << "  - No readable magic numbers or headers\n";
    std::cout << "  - Salted key derivation (10,000 iterations)\n\n";
    
    std::cout << "Examples:\n";
    std::cout << "  node init --size 20 myfs             Create 20MB fs\n";
    std::cout << "  node init --password secure_fs       Create encrypted fs\n";
    std::cout << "  node init --password --maxfile 1024 limited_fs\n";
    std::cout << "  node mount secure_fs                 Mount (will ask password)\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 0;
    }
    
    std::string cmd = argv[1];
    
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        printUsage();
        return 0;
    }
    
    NodeFS nodeFs;
    std::string nodesDir = getNodesDir();
    
    // Ensure nodes directory exists
    try {
        if (!fs::exists(nodesDir)) {
            fs::path p(nodesDir);
            if (fs::exists(p.parent_path())) fs::create_directory(nodesDir);
            else fs::create_directories(nodesDir);
        }
    } catch (...) {}
    
    if (cmd == "init") {
        uint32_t sizeMB = 10;
        uint64_t maxFileSizeKB = 0; // 0 = unlimited
        bool usePassword = false;
        std::string name;
        
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if ((arg == "--size" || arg == "-s") && i + 1 < argc) {
                sizeMB = std::stoi(argv[++i]);
            } else if ((arg == "--maxfile" || arg == "-m") && i + 1 < argc) {
                maxFileSizeKB = std::stoull(argv[++i]);
            } else if (arg == "--password" || arg == "-p") {
                usePassword = true;
            } else if (arg[0] != '-') {
                name = arg;
            }
        }
        
        if (name.empty()) {
            std::cerr << "Error: File system name required\n";
            std::cerr << "Usage: node init [--size MB] [--password] [--maxfile KB] <name>\n";
            return 1;
        }
        
        std::string fullPath;
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            fullPath = name;
        } else {
            if (name.find('.') == std::string::npos) name += ".node";
            fullPath = nodesDir + "/" + name;
        }
        
        std::string password = "";
        if (usePassword) {
            std::cout << "Setting up encryption for " << name << "\n";
            std::cout << "(File will appear as random data to external tools)\n";
            password = readPassword("Enter password: ");
            std::string confirm = readPassword("Confirm password: ");
            
            if (password != confirm) {
                std::cerr << "Error: Passwords do not match!\n";
                return 1;
            }
        }
        
        std::cout << "Creating node filesystem at: " << fullPath << "\n";
        
        // Convert KB to bytes
        uint64_t maxFileSize = maxFileSizeKB * 1024;
        
        if (!nodeFs.format(fullPath, sizeMB, password, maxFileSize)) {
            return 1;
        }
        
        NodeShell shell(nodeFs);
        shell.run();
        
        nodeFs.unmount();
    }
    else if (cmd == "mount") {
        if (argc < 3) {
            std::cerr << "Error: File system name required\n";
            std::cerr << "Usage: node mount <name>\n";
            return 1;
        }
        
        std::string name = argv[2];
        std::string fullPath;
        
        // Path resolution logic
        if (fs::exists(name) && !fs::is_directory(name)) fullPath = name;
        else {
            std::string checkName = name;
            if (checkName.find('.') == std::string::npos) checkName += ".node";
            
            std::string tryPath = nodesDir + "/" + checkName;
            if (fs::exists(tryPath)) fullPath = tryPath;
            else if (fs::exists(checkName)) fullPath = checkName;
            else fullPath = name;
        }
        
        // Check if password required
        std::string password = "";
        if (nodeFs.requiresPassword(fullPath)) {
            std::cout << "This file system is password protected.\n";
            password = readPassword("Enter password: ");
        }
        
        if (!nodeFs.mount(fullPath, password)) {
            // Error message already printed by mount if password wrong, 
            // or if file not found
            if (!fs::exists(fullPath)) {
                 std::cerr << "Could not find node image: " << name << "\n";
                 std::cerr << "Try 'node list' to see available images.\n";
            }
            return 1;
        }
        
        NodeShell shell(nodeFs);
        shell.run();
        
        nodeFs.unmount();
    }
    else if (cmd == "list") {
        std::cout << "Available Node File Systems (" << nodesDir << "):\n";
        
        if (!fs::exists(nodesDir)) {
            std::cout << "  (none - directory not found)\n";
            return 0;
        }
        
        bool found = false;
        for (const auto& entry : fs::directory_iterator(nodesDir)) {
            if (entry.path().extension() == ".node") {
                found = true;
                uintmax_t size = fs::file_size(entry.path());
                
                // check if encrypted
                NodeFS checker;
                bool encrypted = checker.requiresPassword(entry.path().string());
                
                std::cout << "  - " << entry.path().stem().string();
                std::cout << " (" << (size / (1024*1024)) << " MB)";
                if (encrypted) std::cout << " [LOCKED]";
                std::cout << "\n";
            }
        }
        
        if (!found) {
            std::cout << "  (none)\n";
        }
    }
    else if (cmd == "remove" || cmd == "delete" || cmd == "rm") {
        if (argc < 3) {
            std::cerr << "Error: Node name required\n";
            std::cerr << "Usage: node remove <name>\n";
            return 1;
        }
        
        std::string name = argv[2];
        std::string fullPath;
        
        // Resolve path
        if (fs::exists(name) && !fs::is_directory(name)) {
            fullPath = name;
        } else {
            std::string checkName = name;
            if (checkName.find('.') == std::string::npos) checkName += ".node";
            std::string tryPath = nodesDir + "/" + checkName;
            if (fs::exists(tryPath)) fullPath = tryPath;
            else fullPath = name;
        }
        
        if (!fs::exists(fullPath)) {
            std::cerr << "Error: Node image not found: " << name << "\n";
            return 1;
        }
        
        std::cout << "Delete node image '" << name << "'? This cannot be undone. (yes/no): ";
        std::string confirm;
        std::getline(std::cin, confirm);
        
        if (confirm == "yes" || confirm == "y") {
            try {
                fs::remove(fullPath);
                std::cout << "Deleted: " << fullPath << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Error deleting file: " << e.what() << "\n";
                return 1;
            }
        } else {
            std::cout << "Cancelled.\n";
        }
    }
    else {
        std::cerr << "Error: Unknown command '" << cmd << "'\n";
        std::cerr << "Run 'node --help' for usage.\n";
        return 1;
    }
    
    return 0;
}
