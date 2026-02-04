// LinMake - Linuxify Build System
// Compile: g++ -std=c++17 -static -o cmds\linmake.exe cmds-src\linmake.cpp

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <windows.h>

namespace fs = std::filesystem;

const std::string CONFIG_FILE = "LMake";
const std::string BUILD_DIR = "build";

struct Config {
    std::string project = "app";
    std::string type = "executable";
    std::string version = "1.0.0";
    std::vector<std::string> sources;
    std::vector<std::string> libraries;
    std::vector<std::string> includeDirs;
    std::string standard = "c++17";
    int optimize = 0;
    bool staticLink = false;
    bool warnings = true;
    bool debug = false;
};

const std::map<std::string, std::string> LIBRARY_MAP = {
    {"z", "-lz"},
    {"zlib", "-lz"},
    {"ssl", "-lssl -lcrypto"},
    {"openssl", "-lssl -lcrypto"},
    {"crypto", "-lcrypto"},
    {"curl", "-lcurl"},
    {"libcurl", "-lcurl"},
    {"png", "-lpng -lz"},
    {"libpng", "-lpng -lz"},
    {"sqlite", "-lsqlite3"},
    {"sqlite3", "-lsqlite3"},
    {"curses", "-lpdcurses"},
    {"pdcurses", "-lpdcurses"},
    {"ncurses", "-lpdcurses"},
    {"ws2_32", "-lws2_32"},
    {"winsock", "-lws2_32"},
    {"gdi32", "-lgdi32"},
    {"user32", "-luser32"},
    {"kernel32", "-lkernel32"},
    {"shell32", "-lshell32"},
    {"dwmapi", "-ldwmapi"}
};

void setColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

void printStatus(const std::string& msg) {
    setColor(11);
    std::cout << "[LinMake] ";
    setColor(7);
    std::cout << msg << std::endl;
}

void printSuccess(const std::string& msg) {
    setColor(10);
    std::cout << "[LinMake] ";
    setColor(7);
    std::cout << msg << std::endl;
}

void printError(const std::string& msg) {
    setColor(12);
    std::cerr << "[Error] ";
    setColor(7);
    std::cerr << msg << std::endl;
}

void printProgress(int current, int total, const std::string& file) {
    setColor(14);
    std::cout << "[" << current << "/" << total << "] ";
    setColor(7);
    std::cout << "Compiling " << file << "..." << std::endl;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

bool parseConfig(Config& config) {
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    std::string currentSection;
    
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }
        
        if (currentSection.empty()) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(line.substr(0, eq));
                std::string value = trim(line.substr(eq + 1));
                
                if (key == "project") config.project = value;
                else if (key == "type") config.type = value;
                else if (key == "version") config.version = value;
            }
        } else if (currentSection == "sources") {
            config.sources.push_back(line);
        } else if (currentSection == "libraries") {
            config.libraries.push_back(line);
        } else if (currentSection == "include") {
            config.includeDirs.push_back(line);
        } else if (currentSection == "flags") {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(line.substr(0, eq));
                std::string value = trim(line.substr(eq + 1));
                
                if (key == "std") config.standard = value;
                else if (key == "optimize") config.optimize = std::stoi(value);
                else if (key == "static") config.staticLink = (value == "true" || value == "1");
                else if (key == "warnings") config.warnings = (value == "all" || value == "true");
                else if (key == "debug") config.debug = (value == "true" || value == "1");
            }
        }
    }
    
    return true;
}

std::vector<std::string> expandGlob(const std::string& pattern) {
    std::vector<std::string> result;
    
    size_t starPos = pattern.find('*');
    if (starPos == std::string::npos) {
        if (fs::exists(pattern)) {
            result.push_back(pattern);
        }
        return result;
    }
    
    std::string dir = ".";
    std::string ext = "";
    
    size_t lastSlash = pattern.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        dir = pattern.substr(0, lastSlash);
    }
    
    size_t dotPos = pattern.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > starPos) {
        ext = pattern.substr(dotPos);
    }
    
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().string();
                if (ext.empty() || entry.path().extension() == ext) {
                    result.push_back(filename);
                }
            }
        }
    } catch (...) {}
    
    return result;
}

std::vector<std::string> findSources(const Config& config) {
    std::vector<std::string> sources;
    
    if (config.sources.empty()) {
        for (const auto& entry : fs::recursive_directory_iterator(".")) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
                    std::string path = entry.path().string();
                    if (path.find("build") == std::string::npos) {
                        sources.push_back(path);
                    }
                }
            }
        }
    } else {
        for (const auto& pattern : config.sources) {
            auto files = expandGlob(pattern);
            sources.insert(sources.end(), files.begin(), files.end());
        }
    }
    
    return sources;
}

std::string getObjectFile(const std::string& source) {
    fs::path p(source);
    return BUILD_DIR + "/" + p.stem().string() + ".o";
}

bool needsRecompile(const std::string& source, const std::string& object) {
    if (!fs::exists(object)) return true;
    
    auto srcTime = fs::last_write_time(source);
    auto objTime = fs::last_write_time(object);
    
    return srcTime > objTime;
}

std::string buildCompilerFlags(const Config& config) {
    std::stringstream flags;
    
    flags << "-std=" << config.standard << " ";
    
    if (config.optimize > 0) {
        flags << "-O" << config.optimize << " ";
    }
    
    if (config.debug) {
        flags << "-g ";
    }
    
    if (config.warnings) {
        flags << "-Wall ";
    }
    
    if (config.staticLink) {
        flags << "-static ";
    }
    
    for (const auto& inc : config.includeDirs) {
        flags << "-I" << inc << " ";
    }
    
    return flags.str();
}

std::string buildLibraryFlags(const Config& config) {
    std::stringstream flags;
    std::set<std::string> added;
    
    for (const auto& lib : config.libraries) {
        auto it = LIBRARY_MAP.find(lib);
        if (it != LIBRARY_MAP.end()) {
            if (added.find(it->second) == added.end()) {
                flags << it->second << " ";
                added.insert(it->second);
            }
        } else {
            std::string flag = "-l" + lib;
            if (added.find(flag) == added.end()) {
                flags << flag << " ";
                added.insert(flag);
            }
        }
    }
    
    return flags.str();
}

int runCommand(const std::string& cmd) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    // Inherit standard handles so output goes to console
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    
    ZeroMemory(&pi, sizeof(pi));
    
    
    std::vector<char> cmdBuffer(cmd.length() + 1);
    strcpy(cmdBuffer.data(), cmd.c_str());

    if (!CreateProcessA(NULL, cmdBuffer.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return (int)exitCode;
}

int cmdInit() {
    if (fs::exists(CONFIG_FILE)) {
        printError(CONFIG_FILE + " already exists");
        return 1;
    }
    
    std::ofstream file(CONFIG_FILE);
    file << "# LinMake Configuration\n";
    file << "project = myapp\n";
    file << "type = executable\n";
    file << "version = 1.0.0\n";
    file << "\n";
    file << "[sources]\n";
    file << "# Add source files or patterns\n";
    file << "# src/*.cpp\n";
    file << "# main.c\n";
    file << "\n";
    file << "[libraries]\n";
    file << "# Available: z, ssl, curl, png, sqlite3, curses\n";
    file << "# z\n";
    file << "# curl\n";
    file << "\n";
    file << "[include]\n";
    file << "# include/\n";
    file << "\n";
    file << "[flags]\n";
    file << "std = c++17\n";
    file << "optimize = 0\n";
    file << "static = false\n";
    file << "warnings = true\n";
    file << "debug = false\n";
    
    printSuccess("Created " + CONFIG_FILE);
    return 0;
}

int cmdBuild(bool release, bool debug) {
    Config config;
    if (!parseConfig(config)) {
        printError("No " + CONFIG_FILE + " found. Run 'linmake init' first.");
        return 1;
    }
    
    if (release) {
        config.optimize = 2;
        config.debug = false;
    }
    if (debug) {
        config.debug = true;
        config.optimize = 0;
    }
    
    printStatus("Project: " + config.project + " v" + config.version);
    
    auto sources = findSources(config);
    if (sources.empty()) {
        printError("No source files found");
        return 1;
    }
    
    fs::create_directories(BUILD_DIR);
    
    std::string compilerFlags = buildCompilerFlags(config);
    std::vector<std::string> objects;
    int compiled = 0;
    int total = 0;
    
    for (const auto& src : sources) {
        std::string obj = getObjectFile(src);
        objects.push_back(obj);
        if (needsRecompile(src, obj)) {
            total++;
        }
    }
    
    if (total == 0) {
        printSuccess("Nothing to compile (up to date)");
    } else {
        for (const auto& src : sources) {
            std::string obj = getObjectFile(src);
            if (needsRecompile(src, obj)) {
                compiled++;
                printProgress(compiled, total, fs::path(src).filename().string());
                
                bool isCpp = fs::path(src).extension() == ".cpp" || 
                             fs::path(src).extension() == ".cc" ||
                             fs::path(src).extension() == ".cxx";
                std::string compiler = isCpp ? "g++" : "gcc";
                
                std::string cmd = compiler + " " + compilerFlags + "-c " + src + " -o " + obj;
                int result = runCommand(cmd);
                if (result != 0) {
                    printError("Compilation failed for " + src);
                    return 1;
                }
            }
        }
    }
    
    std::string output = BUILD_DIR + "/" + config.project;
    if (config.type == "executable") {
        output += ".exe";
    } else if (config.type == "static") {
        output = BUILD_DIR + "/lib" + config.project + ".a";
    } else if (config.type == "shared") {
        output += ".dll";
    }
    
    bool needsLink = !fs::exists(output);
    if (!needsLink && total > 0) {
        needsLink = true;
    }
    
    if (needsLink) {
        setColor(14);
        std::cout << "[" << (compiled > 0 ? compiled + 1 : 1) << "/" << (total > 0 ? total + 1 : 1) << "] ";
        setColor(7);
        std::cout << "Linking " << fs::path(output).filename().string() << "..." << std::endl;
        
        std::stringstream cmd;
        
        if (config.type == "static") {
            cmd << "ar rcs " << output;
            for (const auto& obj : objects) {
                cmd << " " << obj;
            }
        } else {
            bool hasCpp = false;
            for (const auto& src : sources) {
                std::string ext = fs::path(src).extension().string();
                if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
                    hasCpp = true;
                    break;
                }
            }
            
            cmd << (hasCpp ? "g++" : "gcc") << " " << compilerFlags;
            for (const auto& obj : objects) {
                cmd << obj << " ";
            }
            cmd << "-o " << output << " ";
            cmd << buildLibraryFlags(config);
            
            if (config.type == "shared") {
                cmd << "-shared ";
            }
        }
        
        int result = runCommand(cmd.str());
        if (result != 0) {
            printError("Linking failed");
            return 1;
        }
    }
    
    printSuccess("Build complete: " + output);
    return 0;
}

int cmdClean() {
    Config config;
    parseConfig(config);
    
    if (fs::exists(BUILD_DIR)) {
        fs::remove_all(BUILD_DIR);
        printSuccess("Cleaned build directory");
    } else {
        printStatus("Nothing to clean");
    }
    
    return 0;
}

int cmdRun(bool release, bool debug) {
    int result = cmdBuild(release, debug);
    if (result != 0) return result;
    
    Config config;
    parseConfig(config);
    
    if (config.type != "executable") {
        printError("Cannot run a library project");
        return 1;
    }
    
    std::string exe = BUILD_DIR + "/" + config.project + ".exe";
    if (!fs::exists(exe)) {
        printError("Executable not found: " + exe);
        return 1;
    }
    
    printStatus("Running " + exe + "...");
    std::cout << std::endl;
    
    return runCommand(exe);
}

void printUsage() {
    std::cout << "LinMake - Linuxify Build System\n\n";
    std::cout << "Usage: linmake <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  init         Create a new LinMake.lin config file\n";
    std::cout << "  build        Compile the project\n";
    std::cout << "  clean        Remove build artifacts\n";
    std::cout << "  run          Build and run the project\n";
    std::cout << "  help         Show this help message\n\n";
    std::cout << "Options:\n";
    std::cout << "  --release    Build with optimizations (-O2)\n";
    std::cout << "  --debug      Build with debug symbols (-g)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  linmake init           Create LinMake.lin template\n";
    std::cout << "  linmake build          Compile project\n";
    std::cout << "  linmake build --release  Optimized build\n";
    std::cout << "  linmake run            Build and execute\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 0;
    }
    
    std::string cmd = argv[1];
    bool release = false;
    bool debug = false;
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--release" || arg == "-r") release = true;
        if (arg == "--debug" || arg == "-d") debug = true;
    }
    
    if (cmd == "init") {
        return cmdInit();
    } else if (cmd == "build") {
        return cmdBuild(release, debug);
    } else if (cmd == "clean") {
        return cmdClean();
    } else if (cmd == "run") {
        return cmdRun(release, debug);
    } else if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        printUsage();
        return 0;
    } else {
        printError("Unknown command: " + cmd);
        printUsage();
        return 1;
    }
}
