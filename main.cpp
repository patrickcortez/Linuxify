// Compile: cl /EHsc /std:c++17 main.cpp registry.cpp /Fe:linuxify.exe
// Alternate compile: g++ -std=c++17 -static -o linuxify main.cpp registry.cpp

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <conio.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <map>
#include <sys/utime.h>

#include "registry.hpp"
#include "process_manager.hpp"
#include "system_info.hpp"
#include "networking.hpp"

// Global process manager instance
ProcessManager g_procMgr;

namespace fs = std::filesystem;

class Linuxify {
private:
    bool running;
    std::string currentDir;
    std::vector<std::string> commandHistory;
    std::map<std::string, std::string> sessionEnv;  // Session-specific environment variables

    std::vector<std::string> tokenize(const std::string& input) {
        std::vector<std::string> tokens;
        std::string token;
        bool inQuotes = false;
        char quoteChar = '\0';

        for (size_t i = 0; i < input.length(); ++i) {
            char c = input[i];

            if ((c == '"' || c == '\'') && !inQuotes) {
                inQuotes = true;
                quoteChar = c;
            } else if (c == quoteChar && inQuotes) {
                inQuotes = false;
                quoteChar = '\0';
            } else if (c == ' ' && !inQuotes) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += c;
            }
        }

        if (!token.empty()) {
            tokens.push_back(token);
        }

        return tokens;
    }

    void printPrompt() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "linuxify";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << ":";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << currentDir;
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "$ ";
    }

    void printError(const std::string& message) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cerr << "Error: " << message << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    void printSuccess(const std::string& message) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << message << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    std::string resolvePath(const std::string& path) {
        if (path.empty()) {
            return currentDir;
        }
        
        fs::path p(path);
        if (p.is_absolute()) {
            return fs::canonical(p).string();
        }
        
        fs::path fullPath = fs::path(currentDir) / path;
        try {
            return fs::canonical(fullPath).string();
        } catch (...) {
            return fullPath.string();
        }
    }

    void cmdPwd(const std::vector<std::string>& args) {
        std::cout << currentDir << std::endl;
    }

    void cmdCd(const std::vector<std::string>& args) {
        std::string targetDir;
        
        if (args.size() < 2) {
            char* homeDir = getenv("USERPROFILE");
            if (homeDir) {
                targetDir = homeDir;
            } else {
                printError("Could not find home directory");
                return;
            }
        } else if (args[1] == "-") {
            printError("Previous directory tracking not implemented");
            return;
        } else if (args[1] == "..") {
            fs::path parent = fs::path(currentDir).parent_path();
            targetDir = parent.string();
        } else if (args[1] == "~") {
            char* homeDir = getenv("USERPROFILE");
            if (homeDir) {
                targetDir = homeDir;
            } else {
                printError("Could not find home directory");
                return;
            }
        } else {
            targetDir = resolvePath(args[1]);
        }

        try {
            if (fs::exists(targetDir) && fs::is_directory(targetDir)) {
                currentDir = fs::canonical(targetDir).string();
            } else {
                printError("cd: " + args[1] + ": No such directory");
            }
        } catch (const std::exception& e) {
            printError("cd: " + std::string(e.what()));
        }
    }

    void cmdLs(const std::vector<std::string>& args) {
        bool showAll = false;
        bool longFormat = false;
        std::string targetPath = currentDir;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-a" || args[i] == "--all") {
                showAll = true;
            } else if (args[i] == "-l") {
                longFormat = true;
            } else if (args[i] == "-la" || args[i] == "-al") {
                showAll = true;
                longFormat = true;
            } else if (args[i][0] != '-') {
                targetPath = resolvePath(args[i]);
            }
        }

        try {
            if (!fs::exists(targetPath)) {
                printError("ls: cannot access '" + targetPath + "': No such file or directory");
                return;
            }

            if (!fs::is_directory(targetPath)) {
                std::cout << targetPath << std::endl;
                return;
            }

            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(targetPath)) {
                std::string filename = entry.path().filename().string();
                if (!showAll && filename[0] == '.') {
                    continue;
                }
                entries.push_back(entry);
            }

            std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                return a.path().filename().string() < b.path().filename().string();
            });

            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

            if (longFormat) {
                for (const auto& entry : entries) {
                    std::string perms;
                    if (fs::is_directory(entry)) {
                        perms = "d";
                    } else if (fs::is_symlink(entry)) {
                        perms = "l";
                    } else {
                        perms = "-";
                    }
                    
                    auto status = fs::status(entry);
                    auto p = status.permissions();
                    perms += (p & fs::perms::owner_read) != fs::perms::none ? "r" : "-";
                    perms += (p & fs::perms::owner_write) != fs::perms::none ? "w" : "-";
                    perms += (p & fs::perms::owner_exec) != fs::perms::none ? "x" : "-";
                    perms += (p & fs::perms::group_read) != fs::perms::none ? "r" : "-";
                    perms += (p & fs::perms::group_write) != fs::perms::none ? "w" : "-";
                    perms += (p & fs::perms::group_exec) != fs::perms::none ? "x" : "-";
                    perms += (p & fs::perms::others_read) != fs::perms::none ? "r" : "-";
                    perms += (p & fs::perms::others_write) != fs::perms::none ? "w" : "-";
                    perms += (p & fs::perms::others_exec) != fs::perms::none ? "x" : "-";

                    uintmax_t size = 0;
                    if (fs::is_regular_file(entry)) {
                        size = fs::file_size(entry);
                    }

                    auto ftime = fs::last_write_time(entry);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                    std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                    std::tm* tm = std::localtime(&cftime);
                    char timeBuf[64];
                    std::strftime(timeBuf, sizeof(timeBuf), "%b %d %H:%M", tm);

                    std::cout << perms << " ";
                    std::cout << std::setw(10) << size << " ";
                    std::cout << timeBuf << " ";

                    if (fs::is_directory(entry)) {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    } else if ((p & fs::perms::owner_exec) != fs::perms::none) {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    }

                    std::cout << entry.path().filename().string();
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    std::cout << std::endl;
                }
            } else {
                CONSOLE_SCREEN_BUFFER_INFO csbi;
                int termWidth = 80;
                if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
                    termWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
                }

                size_t maxLen = 0;
                for (const auto& entry : entries) {
                    size_t len = entry.path().filename().string().length();
                    if (len > maxLen) maxLen = len;
                }
                
                int colWidth = static_cast<int>(maxLen) + 2;
                int numCols = (std::max)(1, termWidth / colWidth);

                int count = 0;
                for (const auto& entry : entries) {
                    if (fs::is_directory(entry)) {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    } else {
                        auto status = fs::status(entry);
                        auto p = status.permissions();
                        if ((p & fs::perms::owner_exec) != fs::perms::none) {
                            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                        }
                    }
                    
                    std::cout << std::setw(colWidth) << std::left << entry.path().filename().string();
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    
                    count++;
                    if (count % numCols == 0) {
                        std::cout << std::endl;
                    }
                }
                if (count % numCols != 0) {
                    std::cout << std::endl;
                }
            }
        } catch (const std::exception& e) {
            printError("ls: " + std::string(e.what()));
        }
    }

    void cmdMkdir(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("mkdir: missing operand");
            return;
        }

        bool parents = false;
        std::vector<std::string> dirs;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-p" || args[i] == "--parents") {
                parents = true;
            } else {
                dirs.push_back(args[i]);
            }
        }

        for (const auto& dir : dirs) {
            try {
                std::string fullPath = resolvePath(dir);
                if (parents) {
                    fs::create_directories(fullPath);
                } else {
                    fs::create_directory(fullPath);
                }
                printSuccess("Created directory: " + dir);
            } catch (const std::exception& e) {
                printError("mkdir: cannot create directory '" + dir + "': " + e.what());
            }
        }
    }

    void cmdRm(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("rm: missing operand");
            return;
        }

        bool recursive = false;
        bool force = false;
        std::vector<std::string> targets;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-r" || args[i] == "-R" || args[i] == "--recursive") {
                recursive = true;
            } else if (args[i] == "-f" || args[i] == "--force") {
                force = true;
            } else if (args[i] == "-rf" || args[i] == "-fr") {
                recursive = true;
                force = true;
            } else {
                targets.push_back(args[i]);
            }
        }

        for (const auto& target : targets) {
            try {
                std::string fullPath = resolvePath(target);
                
                if (!fs::exists(fullPath)) {
                    if (!force) {
                        printError("rm: cannot remove '" + target + "': No such file or directory");
                    }
                    continue;
                }

                if (fs::is_directory(fullPath)) {
                    if (!recursive) {
                        printError("rm: cannot remove '" + target + "': Is a directory");
                        continue;
                    }
                    fs::remove_all(fullPath);
                } else {
                    fs::remove(fullPath);
                }
            } catch (const std::exception& e) {
                if (!force) {
                    printError("rm: cannot remove '" + target + "': " + e.what());
                }
            }
        }
    }

    void cmdMv(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("mv: missing operand");
            std::cout << "Usage: mv <source> <destination>" << std::endl;
            return;
        }

        try {
            std::string source = resolvePath(args[1]);
            std::string dest = resolvePath(args[2]);

            if (!fs::exists(source)) {
                printError("mv: cannot stat '" + args[1] + "': No such file or directory");
                return;
            }

            if (fs::is_directory(dest)) {
                dest = (fs::path(dest) / fs::path(source).filename()).string();
            }

            fs::rename(source, dest);
        } catch (const std::exception& e) {
            printError("mv: " + std::string(e.what()));
        }
    }

    void cmdCp(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("cp: missing operand");
            std::cout << "Usage: cp [-r] <source> <destination>" << std::endl;
            return;
        }

        bool recursive = false;
        std::vector<std::string> operands;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-r" || args[i] == "-R" || args[i] == "--recursive") {
                recursive = true;
            } else {
                operands.push_back(args[i]);
            }
        }

        if (operands.size() < 2) {
            printError("cp: missing destination operand");
            return;
        }

        try {
            std::string source = resolvePath(operands[0]);
            std::string dest = resolvePath(operands[1]);

            if (!fs::exists(source)) {
                printError("cp: cannot stat '" + operands[0] + "': No such file or directory");
                return;
            }

            if (fs::is_directory(source)) {
                if (!recursive) {
                    printError("cp: -r not specified; omitting directory '" + operands[0] + "'");
                    return;
                }
                
                fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            } else {
                if (fs::is_directory(dest)) {
                    dest = (fs::path(dest) / fs::path(source).filename()).string();
                }
                fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
            }
        } catch (const std::exception& e) {
            printError("cp: " + std::string(e.what()));
        }
    }

    void cmdCat(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("cat: missing operand");
            return;
        }

        bool showNumbers = false;
        std::vector<std::string> files;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-n" || args[i] == "--number") {
                showNumbers = true;
            } else {
                files.push_back(args[i]);
            }
        }

        for (const auto& file : files) {
            try {
                std::string fullPath = resolvePath(file);

                if (!fs::exists(fullPath)) {
                    printError("cat: " + file + ": No such file or directory");
                    continue;
                }

                if (fs::is_directory(fullPath)) {
                    printError("cat: " + file + ": Is a directory");
                    continue;
                }

                std::ifstream ifs(fullPath);
                if (!ifs) {
                    printError("cat: " + file + ": Cannot open file");
                    continue;
                }

                std::string line;
                int lineNum = 1;
                while (std::getline(ifs, line)) {
                    if (showNumbers) {
                        std::cout << std::setw(6) << lineNum << "  ";
                    }
                    std::cout << line << std::endl;
                    lineNum++;
                }
            } catch (const std::exception& e) {
                printError("cat: " + std::string(e.what()));
            }
        }
    }

    void cmdClear(const std::vector<std::string>& args) {
        system("cls");
    }

    // touch - create files or update timestamps
    void cmdTouch(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("touch: missing file operand");
            return;
        }

        for (size_t i = 1; i < args.size(); ++i) {
            try {
                std::string fullPath = resolvePath(args[i]);
                
                if (fs::exists(fullPath)) {
                    // Update modification time using _utime
                    if (_utime(fullPath.c_str(), nullptr) == 0) {
                        printSuccess("Updated: " + args[i]);
                    } else {
                        printError("touch: cannot touch '" + args[i] + "'");
                    }
                } else {
                    // Create new empty file
                    std::ofstream file(fullPath);
                    if (file) {
                        file.close();
                        printSuccess("Created: " + args[i]);
                    } else {
                        printError("touch: cannot create '" + args[i] + "'");
                    }
                }
            } catch (const std::exception& e) {
                printError("touch: " + std::string(e.what()));
            }
        }
    }

    // chmod - change file permissions (Windows adaptation)
    void cmdChmod(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("chmod: missing operand");
            std::cout << "Usage: chmod <mode> <file>..." << std::endl;
            std::cout << "  Modes: +x (executable), -x (not executable)" << std::endl;
            std::cout << "         +w (writable), -w (read-only)" << std::endl;
            std::cout << "         +r (readable), -r (hidden)" << std::endl;
            return;
        }

        std::string mode = args[1];
        
        for (size_t i = 2; i < args.size(); ++i) {
            try {
                std::string fullPath = resolvePath(args[i]);
                
                if (!fs::exists(fullPath)) {
                    printError("chmod: cannot access '" + args[i] + "': No such file or directory");
                    continue;
                }

                DWORD attrs = GetFileAttributesA(fullPath.c_str());
                if (attrs == INVALID_FILE_ATTRIBUTES) {
                    printError("chmod: cannot access '" + args[i] + "'");
                    continue;
                }

                bool success = false;
                
                if (mode == "+w") {
                    // Remove read-only attribute
                    attrs &= ~FILE_ATTRIBUTE_READONLY;
                    success = SetFileAttributesA(fullPath.c_str(), attrs);
                    if (success) printSuccess("Made writable: " + args[i]);
                } else if (mode == "-w") {
                    // Set read-only attribute
                    attrs |= FILE_ATTRIBUTE_READONLY;
                    success = SetFileAttributesA(fullPath.c_str(), attrs);
                    if (success) printSuccess("Made read-only: " + args[i]);
                } else if (mode == "+r") {
                    // Remove hidden attribute (make visible/readable)
                    attrs &= ~FILE_ATTRIBUTE_HIDDEN;
                    success = SetFileAttributesA(fullPath.c_str(), attrs);
                    if (success) printSuccess("Made visible: " + args[i]);
                } else if (mode == "-r") {
                    // Set hidden attribute
                    attrs |= FILE_ATTRIBUTE_HIDDEN;
                    success = SetFileAttributesA(fullPath.c_str(), attrs);
                    if (success) printSuccess("Made hidden: " + args[i]);
                } else if (mode == "+x") {
                    // Windows doesn't have execute bit, but we can note it
                    // Check if it's a valid executable extension
                    std::string ext = fs::path(fullPath).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".exe" || ext == ".cmd" || ext == ".bat" || ext == ".ps1") {
                        printSuccess("File is already executable: " + args[i]);
                    } else {
                        std::cout << "Note: Windows executability is determined by file extension (.exe, .cmd, .bat)" << std::endl;
                        printSuccess("Marked as executable: " + args[i]);
                    }
                    success = true;
                } else if (mode == "-x") {
                    std::cout << "Note: Windows executability is determined by file extension" << std::endl;
                    success = true;
                } else if (mode.length() == 3 && std::all_of(mode.begin(), mode.end(), ::isdigit)) {
                    // Numeric mode like 755, 644
                    std::cout << "Note: Numeric permissions (" << mode << ") mapped to Windows attributes" << std::endl;
                    int ownerPerms = mode[0] - '0';
                    if ((ownerPerms & 2) == 0) {
                        // No write permission - make read-only
                        attrs |= FILE_ATTRIBUTE_READONLY;
                    } else {
                        attrs &= ~FILE_ATTRIBUTE_READONLY;
                    }
                    success = SetFileAttributesA(fullPath.c_str(), attrs);
                    if (success) printSuccess("Applied permissions to: " + args[i]);
                } else {
                    printError("chmod: invalid mode '" + mode + "'");
                    continue;
                }

                if (!success && mode != "+x" && mode != "-x") {
                    printError("chmod: failed to change permissions for '" + args[i] + "'");
                }
            } catch (const std::exception& e) {
                printError("chmod: " + std::string(e.what()));
            }
        }
    }

    // chown - change file ownership (Windows adaptation)
    void cmdChown(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("chown: missing operand");
            std::cout << "Usage: chown <owner> <file>..." << std::endl;
            return;
        }

        std::string owner = args[1];
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Note: Windows file ownership changes require elevated privileges." << std::endl;
        std::cout << "      Attempting to use icacls to grant permissions..." << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        for (size_t i = 2; i < args.size(); ++i) {
            try {
                std::string fullPath = resolvePath(args[i]);
                
                if (!fs::exists(fullPath)) {
                    printError("chown: cannot access '" + args[i] + "': No such file or directory");
                    continue;
                }

                // Use icacls to grant full control to the specified user
                std::string cmd = "icacls \"" + fullPath + "\" /grant " + owner + ":F 2>nul";
                int result = system(cmd.c_str());
                
                if (result == 0) {
                    printSuccess("Granted permissions to " + owner + " for: " + args[i]);
                } else {
                    printError("chown: permission change failed for '" + args[i] + "' (may need admin rights)");
                }
            } catch (const std::exception& e) {
                printError("chown: " + std::string(e.what()));
            }
        }
    }

    // Get history file path
    std::string getHistoryFilePath() {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path linuxdbDir = exeDir / "linuxdb";
        
        if (!fs::exists(linuxdbDir)) {
            fs::create_directories(linuxdbDir);
        }
        
        return (linuxdbDir / "history.lin").string();
    }

    // Load history from file
    void loadHistory() {
        std::ifstream file(getHistoryFilePath());
        if (file) {
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    commandHistory.push_back(line);
                }
            }
        }
    }

    // Save command to history
    void saveToHistory(const std::string& cmd) {
        commandHistory.push_back(cmd);
        
        std::ofstream file(getHistoryFilePath(), std::ios::app);
        if (file) {
            file << cmd << "\n";
        }
    }

    // history - show command history
    void cmdHistory(const std::vector<std::string>& args) {
        bool showNumbers = true;
        int limit = -1;  // -1 means show all
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-c" || args[i] == "--clear") {
                // Clear history
                commandHistory.clear();
                std::ofstream file(getHistoryFilePath(), std::ios::trunc);
                printSuccess("History cleared.");
                return;
            } else {
                try {
                    limit = std::stoi(args[i]);
                } catch (...) {}
            }
        }
        
        size_t start = 0;
        if (limit > 0 && limit < (int)commandHistory.size()) {
            start = commandHistory.size() - limit;
        }
        
        for (size_t i = start; i < commandHistory.size(); ++i) {
            if (showNumbers) {
                std::cout << std::setw(5) << (i + 1) << "  ";
            }
            std::cout << commandHistory[i] << std::endl;
        }
    }

    // whoami - show current user
    void cmdWhoami(const std::vector<std::string>& args) {
        char username[256];
        DWORD size = sizeof(username);
        
        if (GetUserNameA(username, &size)) {
            std::cout << username << std::endl;
        } else {
            // Fallback to environment variable
            char* user = nullptr;
            size_t len;
            _dupenv_s(&user, &len, "USERNAME");
            if (user) {
                std::cout << user << std::endl;
                free(user);
            } else {
                printError("whoami: cannot determine username");
            }
        }
    }

    // echo - print text
    void cmdEcho(const std::vector<std::string>& args) {
        bool newline = true;
        size_t start = 1;
        
        if (args.size() > 1 && args[1] == "-n") {
            newline = false;
            start = 2;
        }
        
        for (size_t i = start; i < args.size(); ++i) {
            std::string text = args[i];
            
            // Expand environment variables ($VAR or ${VAR})
            size_t pos = 0;
            while ((pos = text.find('$', pos)) != std::string::npos) {
                size_t end = pos + 1;
                bool braced = false;
                
                if (end < text.length() && text[end] == '{') {
                    braced = true;
                    end++;
                    size_t close = text.find('}', end);
                    if (close != std::string::npos) {
                        std::string varName = text.substr(end, close - end);
                        
                        // Check session env first, then system
                        std::string value;
                        auto it = sessionEnv.find(varName);
                        if (it != sessionEnv.end()) {
                            value = it->second;
                        } else {
                            char* envVal = nullptr;
                            size_t len;
                            _dupenv_s(&envVal, &len, varName.c_str());
                            if (envVal) {
                                value = envVal;
                                free(envVal);
                            }
                        }
                        
                        text = text.substr(0, pos) + value + text.substr(close + 1);
                    }
                } else {
                    while (end < text.length() && (isalnum(text[end]) || text[end] == '_')) {
                        end++;
                    }
                    std::string varName = text.substr(pos + 1, end - pos - 1);
                    
                    std::string value;
                    auto it = sessionEnv.find(varName);
                    if (it != sessionEnv.end()) {
                        value = it->second;
                    } else {
                        char* envVal = nullptr;
                        size_t len;
                        _dupenv_s(&envVal, &len, varName.c_str());
                        if (envVal) {
                            value = envVal;
                            free(envVal);
                        }
                    }
                    
                    text = text.substr(0, pos) + value + text.substr(end);
                }
                pos++;
            }
            
            if (i > start) std::cout << " ";
            std::cout << text;
        }
        
        if (newline) std::cout << std::endl;
    }

    // env/printenv - show environment variables
    void cmdEnv(const std::vector<std::string>& args) {
        // If specific variable requested
        if (args.size() > 1) {
            std::string varName = args[1];
            
            // Check session env first
            auto it = sessionEnv.find(varName);
            if (it != sessionEnv.end()) {
                std::cout << it->second << std::endl;
                return;
            }
            
            char* value = nullptr;
            size_t len;
            _dupenv_s(&value, &len, varName.c_str());
            if (value) {
                std::cout << value << std::endl;
                free(value);
            }
            return;
        }
        
        // Show all environment variables
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        // First show session variables
        if (!sessionEnv.empty()) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "# Session Variables:\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            for (const auto& pair : sessionEnv) {
                std::cout << pair.first << "=" << pair.second << std::endl;
            }
            std::cout << std::endl;
        }
        
        // Then system variables
        char* envStrings = GetEnvironmentStringsA();
        if (envStrings) {
            char* current = envStrings;
            while (*current) {
                std::cout << current << std::endl;
                current += strlen(current) + 1;
            }
            FreeEnvironmentStringsA(envStrings);
        }
    }

    // export - set environment variable
    void cmdExport(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            // Show exported session variables
            for (const auto& pair : sessionEnv) {
                std::cout << "export " << pair.first << "=\"" << pair.second << "\"" << std::endl;
            }
            return;
        }
        
        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            size_t eqPos = arg.find('=');
            
            if (eqPos != std::string::npos) {
                std::string name = arg.substr(0, eqPos);
                std::string value = arg.substr(eqPos + 1);
                
                // Remove quotes if present
                if (value.length() >= 2 && 
                    ((value.front() == '"' && value.back() == '"') ||
                     (value.front() == '\'' && value.back() == '\''))) {
                    value = value.substr(1, value.length() - 2);
                }
                
                // Set in session and system environment
                sessionEnv[name] = value;
                SetEnvironmentVariableA(name.c_str(), value.c_str());
                printSuccess("Exported: " + name + "=" + value);
            } else {
                printError("export: invalid format. Use: export NAME=value");
            }
        }
    }

    // which - find command location
    void cmdWhich(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("which: missing argument");
            std::cout << "Usage: which <command>" << std::endl;
            return;
        }
        
        std::string cmd = args[1];
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        // Check built-in commands first
        std::vector<std::string> builtins = {
            "pwd", "cd", "ls", "mkdir", "rm", "mv", "cp", "cat", "touch", 
            "chmod", "chown", "clear", "help", "nano", "lin", "registry",
            "history", "whoami", "echo", "env", "printenv", "export", "which", "exit"
        };
        
        for (const auto& builtin : builtins) {
            if (cmd == builtin) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << cmd << ": shell built-in command" << std::endl;
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                return;
            }
        }
        
        // Check cmds folder
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path cmdsDir = fs::path(exePath).parent_path() / "cmds";
        
        std::vector<std::string> extensions = {".exe", ".cmd", ".bat", ""};
        for (const auto& ext : extensions) {
            fs::path cmdPath = cmdsDir / (cmd + ext);
            if (fs::exists(cmdPath)) {
                std::cout << cmdPath.string() << std::endl;
                return;
            }
        }
        
        // Check registry
        std::string regPath = g_registry.getExecutablePath(cmd);
        if (!regPath.empty()) {
            std::cout << regPath << std::endl;
            return;
        }
        
        printError("which: " + cmd + " not found");
    }

    // uninstall - Remove Linuxify from system
    void cmdUninstall(const std::vector<std::string>& args) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cout << "\n========================================" << std::endl;
        std::cout << "    LINUXIFY UNINSTALLER" << std::endl;
        std::cout << "========================================\n" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        std::cout << "This will remove Linuxify from your system." << std::endl;
        std::cout << "The following will be removed:" << std::endl;
        std::cout << "  - Linuxify executable and related files" << std::endl;
        std::cout << "  - Linuxify from your system PATH" << std::endl;
        std::cout << "  - Windows Terminal integration (if installed)" << std::endl;
        std::cout << std::endl;
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Are you sure you want to uninstall Linuxify? (yes/no): ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        std::string response;
        std::getline(std::cin, response);
        
        // Normalize response
        std::transform(response.begin(), response.end(), response.begin(), ::tolower);
        response.erase(0, response.find_first_not_of(" \t"));
        response.erase(response.find_last_not_of(" \t") + 1);
        
        if (response != "yes" && response != "y") {
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "\nUninstall cancelled." << std::endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            return;
        }
        
        std::cout << std::endl;
        
        // Get the installation directory
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path installDir = fs::path(exePath).parent_path();
        
        std::cout << "Removing Linuxify from: " << installDir.string() << std::endl;
        
        // Try to remove from PATH using PowerShell
        std::cout << "Removing from system PATH..." << std::endl;
        std::string removePathCmd = "powershell -Command \"$path = [Environment]::GetEnvironmentVariable('PATH', 'User'); $newPath = ($path -split ';' | Where-Object { $_ -notlike '*Linuxify*' }) -join ';'; [Environment]::SetEnvironmentVariable('PATH', $newPath, 'User')\" 2>nul";
        system(removePathCmd.c_str());
        
        // Create a batch script to delete the folder after we exit
        std::string tempPath = std::getenv("TEMP") ? std::string(std::getenv("TEMP")) : "C:\\Windows\\Temp";
        std::string batchFile = tempPath + "\\linuxify_uninstall.bat";
        
        std::ofstream batch(batchFile);
        if (batch) {
            batch << "@echo off\n";
            batch << "echo Completing Linuxify uninstallation...\n";
            batch << "timeout /t 2 /nobreak > nul\n";  // Wait for the exe to close
            batch << "rd /s /q \"" << installDir.string() << "\" 2>nul\n";
            batch << "echo Linuxify has been completely removed.\n";
            batch << "echo.\n";
            batch << "del \"%~f0\"\n";  // Delete the batch file itself
            batch.close();
            
            // Start the batch file in a new window
            std::string startCmd = "start \"\" cmd /c \"" + batchFile + "\"";
            system(startCmd.c_str());
        }
        
        std::cout << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "========================================" << std::endl;
        std::cout << "  Thank you for using Linuxify!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::endl;
        std::cout << "Goodbye! :)" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        running = false;
    }

    // ps - list processes
    void cmdPs(const std::vector<std::string>& args) {
        bool detailed = false;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-l" || args[i] == "--long" || args[i] == "-aux") {
                detailed = true;
            }
        }
        
        if (detailed) {
            ProcessManager::listProcessesDetailed();
        } else {
            ProcessManager::listProcesses();
        }
    }

    // kill - terminate a process
    void cmdKill(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("kill: missing PID or job ID");
            std::cout << "Usage: kill <PID> or kill %<job_id>" << std::endl;
            return;
        }
        
        std::string target = args[1];
        
        // Check if it's a job ID (%n)
        if (target[0] == '%') {
            try {
                int jobId = std::stoi(target.substr(1));
                if (g_procMgr.killJob(jobId)) {
                    printSuccess("Job " + std::to_string(jobId) + " terminated.");
                } else {
                    printError("kill: no such job: " + target);
                }
            } catch (...) {
                printError("kill: invalid job ID: " + target);
            }
        } else {
            // It's a PID
            try {
                DWORD pid = std::stoul(target);
                if (g_procMgr.killByPid(pid)) {
                    printSuccess("Process " + target + " terminated.");
                } else {
                    printError("kill: failed to terminate process " + target);
                }
            } catch (...) {
                printError("kill: invalid PID: " + target);
            }
        }
    }

    // top - live process monitor
    void cmdTop(const std::vector<std::string>& args) {
        ProcessManager::topView();
    }

    // jobs - list background jobs
    void cmdJobs(const std::vector<std::string>& args) {
        g_procMgr.listJobs();
    }

    // fg - bring job to foreground
    void cmdFg(const std::vector<std::string>& args) {
        int jobId = 1;  // Default to job 1
        
        if (args.size() > 1) {
            std::string target = args[1];
            if (target[0] == '%') {
                target = target.substr(1);
            }
            try {
                jobId = std::stoi(target);
            } catch (...) {
                printError("fg: invalid job ID");
                return;
            }
        }
        
        BackgroundJob* job = g_procMgr.getJob(jobId);
        if (job && job->running) {
            std::cout << job->command << std::endl;
            g_procMgr.waitForJob(jobId);
        } else {
            printError("fg: no such job");
        }
    }

    // Run a command in background
    bool runInBackground(const std::string& cmdLine, const std::string& displayCmd) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;  // Hide the window
        ZeroMemory(&pi, sizeof(pi));
        
        char cmdBuffer[4096];
        strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer));
        
        // CREATE_NEW_CONSOLE creates a separate console for the process
        // DETACHED_PROCESS would have no console at all
        DWORD creationFlags = CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE;
        
        if (CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            FALSE,
            creationFlags,
            NULL,
            currentDir.c_str(),
            &si,
            &pi
        )) {
            int jobId = g_procMgr.addJob(pi.hProcess, pi.dwProcessId, displayCmd);
            CloseHandle(pi.hThread);
            
            std::cout << "[" << jobId << "] " << pi.dwProcessId << std::endl;
            return true;
        }
        
        return false;
    }

    // grep - search for pattern in file or input
    void cmdGrep(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (args.size() < 2) {
            printError("grep: missing pattern");
            std::cout << "Usage: grep <pattern> [file]" << std::endl;
            return;
        }
        
        std::string pattern = args[1];
        bool ignoreCase = false;
        bool lineNumbers = false;
        bool invertMatch = false;
        size_t argStart = 2;
        
        // Parse flags
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-i") { ignoreCase = true; argStart++; }
            else if (args[i] == "-n") { lineNumbers = true; argStart++; }
            else if (args[i] == "-v") { invertMatch = true; argStart++; }
            else if (args[i][0] != '-') { pattern = args[i]; argStart = i + 1; break; }
        }
        
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            // Use piped input
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (argStart < args.size()) {
            // Read from file
            std::string filePath = resolvePath(args[argStart]);
            std::ifstream file(filePath);
            if (!file) {
                printError("grep: cannot open '" + args[argStart] + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("grep: missing file operand");
            return;
        }
        
        std::string searchPattern = ignoreCase ? pattern : pattern;
        
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string line = lines[i];
            std::string searchLine = ignoreCase ? line : line;
            
            if (ignoreCase) {
                std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(), ::tolower);
                std::transform(searchPattern.begin(), searchPattern.end(), searchPattern.begin(), ::tolower);
            }
            
            bool found = searchLine.find(searchPattern) != std::string::npos;
            if (invertMatch) found = !found;
            
            if (found) {
                if (lineNumbers) {
                    std::cout << (i + 1) << ":";
                }
                // Highlight the match
                size_t pos = 0;
                std::string output = line;
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                
                while ((pos = searchLine.find(searchPattern, pos)) != std::string::npos) {
                    std::cout << line.substr(0, pos);
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                    std::cout << line.substr(pos, pattern.length());
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    line = line.substr(pos + pattern.length());
                    searchLine = searchLine.substr(pos + searchPattern.length());
                    pos = 0;
                }
                std::cout << line << std::endl;
            }
        }
    }

    // head - show first N lines
    void cmdHead(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        int numLines = 10;
        std::string filePath;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-n" && i + 1 < args.size()) {
                numLines = std::stoi(args[++i]);
            } else if (args[i][0] == '-' && isdigit(args[i][1])) {
                numLines = std::stoi(args[i].substr(1));
            } else if (args[i][0] != '-') {
                filePath = args[i];
            }
        }
        
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (!filePath.empty()) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("head: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("head: missing file operand");
            return;
        }
        
        for (int i = 0; i < numLines && i < (int)lines.size(); ++i) {
            std::cout << lines[i] << std::endl;
        }
    }

    // tail - show last N lines
    void cmdTail(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        int numLines = 10;
        std::string filePath;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-n" && i + 1 < args.size()) {
                numLines = std::stoi(args[++i]);
            } else if (args[i][0] == '-' && isdigit(args[i][1])) {
                numLines = std::stoi(args[i].substr(1));
            } else if (args[i][0] != '-') {
                filePath = args[i];
            }
        }
        
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (!filePath.empty()) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("tail: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("tail: missing file operand");
            return;
        }
        
        int start = std::max(0, (int)lines.size() - numLines);
        for (int i = start; i < (int)lines.size(); ++i) {
            std::cout << lines[i] << std::endl;
        }
    }

    // wc - word/line/char count
    void cmdWc(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool countLines = false, countWords = false, countChars = false;
        std::string filePath;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-l") countLines = true;
            else if (args[i] == "-w") countWords = true;
            else if (args[i] == "-c" || args[i] == "-m") countChars = true;
            else if (args[i][0] != '-') filePath = args[i];
        }
        
        // Default: show all
        if (!countLines && !countWords && !countChars) {
            countLines = countWords = countChars = true;
        }
        
        std::string content;
        
        if (!pipedInput.empty()) {
            content = pipedInput;
        } else if (!filePath.empty()) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("wc: cannot open '" + filePath + "'");
                return;
            }
            std::ostringstream oss;
            oss << file.rdbuf();
            content = oss.str();
        } else {
            printError("wc: missing file operand");
            return;
        }
        
        int lines = 0, words = 0, chars = (int)content.length();
        bool inWord = false;
        
        for (char c : content) {
            if (c == '\n') lines++;
            if (isspace(c)) {
                inWord = false;
            } else if (!inWord) {
                inWord = true;
                words++;
            }
        }
        
        if (countLines) std::cout << std::setw(8) << lines;
        if (countWords) std::cout << std::setw(8) << words;
        if (countChars) std::cout << std::setw(8) << chars;
        if (!filePath.empty()) std::cout << " " << filePath;
        std::cout << std::endl;
    }

    // sort - sort lines
    void cmdSort(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool reverse = false;
        bool numeric = false;
        bool unique = false;
        std::string filePath;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-r") reverse = true;
            else if (args[i] == "-n") numeric = true;
            else if (args[i] == "-u") unique = true;
            else if (args[i][0] != '-') filePath = args[i];
        }
        
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (!filePath.empty()) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("sort: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("sort: missing file operand");
            return;
        }
        
        if (numeric) {
            std::sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) {
                try { return std::stod(a) < std::stod(b); }
                catch (...) { return a < b; }
            });
        } else {
            std::sort(lines.begin(), lines.end());
        }
        
        if (reverse) {
            std::reverse(lines.begin(), lines.end());
        }
        
        std::string prev;
        for (const auto& line : lines) {
            if (!unique || line != prev) {
                std::cout << line << std::endl;
                prev = line;
            }
        }
    }

    // uniq - remove duplicate consecutive lines
    void cmdUniq(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool countDupes = false;
        bool onlyDupes = false;
        std::string filePath;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-c") countDupes = true;
            else if (args[i] == "-d") onlyDupes = true;
            else if (args[i][0] != '-') filePath = args[i];
        }
        
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (!filePath.empty()) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("uniq: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("uniq: missing file operand");
            return;
        }
        
        std::string prev;
        int count = 0;
        
        for (size_t i = 0; i <= lines.size(); ++i) {
            if (i < lines.size() && lines[i] == prev) {
                count++;
            } else {
                if (!prev.empty()) {
                    if (!onlyDupes || count > 1) {
                        if (countDupes) {
                            std::cout << std::setw(7) << count << " ";
                        }
                        std::cout << prev << std::endl;
                    }
                }
                if (i < lines.size()) {
                    prev = lines[i];
                    count = 1;
                }
            }
        }
    }

    // find - search for files
    void cmdFind(const std::vector<std::string>& args) {
        std::string searchPath = ".";
        std::string namePattern;
        std::string typeFilter;  // "f" for file, "d" for directory
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-name" && i + 1 < args.size()) {
                namePattern = args[++i];
            } else if (args[i] == "-type" && i + 1 < args.size()) {
                typeFilter = args[++i];
            } else if (args[i][0] != '-') {
                searchPath = args[i];
            }
        }
        
        std::string fullPath = resolvePath(searchPath);
        
        if (!fs::exists(fullPath)) {
            printError("find: '" + searchPath + "': No such file or directory");
            return;
        }
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(fullPath, fs::directory_options::skip_permission_denied)) {
                std::string filename = entry.path().filename().string();
                
                // Type filter
                if (typeFilter == "f" && !entry.is_regular_file()) continue;
                if (typeFilter == "d" && !entry.is_directory()) continue;
                
                // Name pattern matching (simple wildcard support)
                if (!namePattern.empty()) {
                    bool match = false;
                    if (namePattern[0] == '*') {
                        // Suffix match
                        std::string suffix = namePattern.substr(1);
                        match = filename.length() >= suffix.length() &&
                                filename.substr(filename.length() - suffix.length()) == suffix;
                    } else if (namePattern.back() == '*') {
                        // Prefix match
                        std::string prefix = namePattern.substr(0, namePattern.length() - 1);
                        match = filename.substr(0, prefix.length()) == prefix;
                    } else {
                        match = filename == namePattern;
                    }
                    if (!match) continue;
                }
                
                std::cout << entry.path().string() << std::endl;
            }
        } catch (const std::exception& e) {
            printError("find: " + std::string(e.what()));
        }
    }

    std::string getPackagesFilePath() {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path linuxdbDir = exeDir / "linuxdb";
        
        // Create linuxdb directory if it doesn't exist
        if (!fs::exists(linuxdbDir)) {
            fs::create_directories(linuxdbDir);
        }
        
        return (linuxdbDir / "packages.lin").string();
    }

    std::map<std::string, std::string> loadPackageAliases() {
        std::map<std::string, std::string> aliases;
        std::ifstream file(getPackagesFilePath());
        
        if (!file) {
            return aliases;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                if (!key.empty() && !value.empty()) {
                    aliases[key] = value;
                }
            }
        }
        
        return aliases;
    }

    std::string resolvePackageName(const std::string& name) {
        auto aliases = loadPackageAliases();
        auto it = aliases.find(name);
        if (it != aliases.end()) {
            return it->second;
        }
        return name;
    }

    void cmdLin(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Lin Package Manager";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout << " - Linux-style wrapper for winget\n\n";
            
            std::cout << "Usage:\n";
            std::cout << "  lin get <package>      Install a package\n";
            std::cout << "  lin remove <package>   Uninstall a package\n";
            std::cout << "  lin search <query>     Search packages (auto-syncs)\n";
            std::cout << "  lin update             Check for updates\n";
            std::cout << "  lin upgrade            Upgrade all packages\n";
            std::cout << "  lin list               List installed packages\n";
            std::cout << "  lin info <package>     Show package info\n";
            std::cout << "  lin alias              Show all package aliases\n";
            std::cout << "  lin add <name> <id>    Add custom alias\n";
            return;
        }
        
        std::string subcmd = args[1];
        
        if (subcmd == "get" || subcmd == "install") {
            if (args.size() < 3) {
                printError("Usage: lin get <package>");
                return;
            }
            
            std::string package = resolvePackageName(args[2]);
            
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Installing: ";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout << package << "\n\n";
            
            std::string cmd = "winget install --accept-package-agreements --accept-source-agreements " + package;
            system(cmd.c_str());
            
        } else if (subcmd == "remove" || subcmd == "uninstall") {
            if (args.size() < 3) {
                printError("Usage: lin remove <package>");
                return;
            }
            
            std::string package = resolvePackageName(args[2]);
            
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
            std::cout << "Removing: ";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout << package << "\n\n";
            
            std::string cmd = "winget uninstall " + package;
            system(cmd.c_str());
            
        } else if (subcmd == "search" || subcmd == "find") {
            if (args.size() < 3) {
                printError("Usage: lin search <query>");
                return;
            }
            
            std::string query = args[2];
            std::string tempFile = getPackagesFilePath() + ".tmp";
            
            std::string cmd = "winget search " + query + " --accept-source-agreements";
            system(cmd.c_str());
            
            std::cout << "\n";
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Syncing found packages to aliases...";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            std::string captureCmd = "winget search " + query + " --accept-source-agreements > \"" + tempFile + "\" 2>nul";
            system(captureCmd.c_str());
            
            auto existingAliases = loadPackageAliases();
            int addedCount = 0;
            
            std::ifstream ifs(tempFile);
            if (ifs) {
                std::string line;
                int lineNum = 0;
                
                while (std::getline(ifs, line)) {
                    lineNum++;
                    if (lineNum <= 2) continue;
                    if (line.empty() || line[0] == '-') continue;
                    if (line.find("Name") != std::string::npos && line.find("Id") != std::string::npos) continue;
                    
                    std::string name;
                    std::string packageId;
                    
                    size_t pos = 0;
                    while (pos < line.length() && line[pos] != ' ') {
                        name += line[pos];
                        pos++;
                    }
                    
                    while (pos < line.length() && line[pos] == ' ') pos++;
                    
                    while (pos < line.length() && line[pos] != ' ') {
                        packageId += line[pos];
                        pos++;
                    }
                    
                    if (packageId.length() > 3 && packageId.find('.') != std::string::npos) {
                        std::string alias = name;
                        std::transform(alias.begin(), alias.end(), alias.begin(), ::tolower);
                        alias.erase(std::remove_if(alias.begin(), alias.end(), 
                            [](char c) { return !isalnum(c) && c != '-' && c != '_'; }), alias.end());
                        
                        if (alias.length() >= 2 && alias.length() <= 30) {
                            if (existingAliases.find(alias) == existingAliases.end()) {
                                std::ofstream outFile(getPackagesFilePath(), std::ios::app);
                                if (outFile) {
                                    outFile << alias << "=" << packageId << "\n";
                                    outFile.close();
                                    existingAliases[alias] = packageId;
                                    addedCount++;
                                }
                            }
                        }
                    }
                }
                ifs.close();
            }
            
            fs::remove(tempFile);
            
            if (addedCount > 0) {
                std::cout << " added " << addedCount << " new aliases!\n";
            } else {
                std::cout << " (all packages already known)\n";
            }
            
        } else if (subcmd == "update") {
            std::cout << "Checking for updates...\n\n";
            system("winget upgrade");
            
        } else if (subcmd == "upgrade") {
            std::cout << "Upgrading all packages...\n\n";
            system("winget upgrade --all --accept-package-agreements --accept-source-agreements");
            
        } else if (subcmd == "list") {
            system("winget list");
            
        } else if (subcmd == "info" || subcmd == "show") {
            if (args.size() < 3) {
                printError("Usage: lin info <package>");
                return;
            }
            
            std::string package = resolvePackageName(args[2]);
            std::string cmd = "winget show " + package;
            system(cmd.c_str());
            
        } else if (subcmd == "alias" || subcmd == "aliases") {
            auto aliases = loadPackageAliases();
            
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Package Aliases";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout << " (" << aliases.size() << " total)\n\n";
            
            for (const auto& pair : aliases) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                std::cout << std::setw(15) << std::left << pair.first;
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                std::cout << " -> " << pair.second << "\n";
            }
            
        } else if (subcmd == "add" || subcmd == "add-alias") {
            if (args.size() < 4) {
                printError("Usage: lin add <alias-name> <winget-id>");
                return;
            }
            
            std::string aliasName = args[2];
            std::string wingetId = args[3];
            
            std::ofstream file(getPackagesFilePath(), std::ios::app);
            if (!file) {
                printError("Cannot write to packages.lin");
                return;
            }
            
            file << "\n" << aliasName << "=" << wingetId;
            file.close();
            
            printSuccess("Added alias: " + aliasName + " -> " + wingetId);
            
        } else {
            printError("Unknown lin command: " + subcmd);
            std::cout << "Type 'lin' for usage\n";
        }
    }

    void cmdHelp(const std::vector<std::string>& args) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "\n  Linuxify Shell - Linux Commands for Windows\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  ============================================\n\n";

        std::cout << "  File System Commands:\n\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  pwd";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Print working directory\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cd <dir>";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Change directory\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  ls [-la]";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      List directory contents\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  mkdir [-p]";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Create directories\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  rm [-rf]";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Remove files or directories\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  mv";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            Move or rename files\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cp [-r]";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "       Copy files or directories\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cat [-n]";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Display file contents\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  touch";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Create files or update timestamps\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  chmod";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Change file permissions (+w/-w/+r/-r)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  chown";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Change file ownership (uses icacls)\n";

        std::cout << "\n  Utilities:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  clear";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Clear the screen\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  echo";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Print text (supports $VAR expansion)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  history";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "       Show command history (-c to clear)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  whoami";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "        Show current username\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  env/printenv";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  Show environment variables\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  export";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "        Set environment variable (NAME=value)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  which";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Find command location\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  help";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Show this help message\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  nano";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Text editor\n";

        std::cout << "\n  Package Management:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lin";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Package manager (lin get, lin remove, ...)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  registry";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      External command registry (refresh, list)\n";

        std::cout << "\n  External Commands:\n\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  Installed tools like git, node, python, mysql, etc.\n";
        std::cout << "  Run 'registry refresh' to scan for installed commands.\n";

        std::cout << "\n  Process Management:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  ps";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            List running processes (-aux for details)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  kill";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Terminate process (kill <PID> or kill %<job>)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  top";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Live process monitor (press 'q' to quit)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  jobs";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          List background jobs\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  fg";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            Bring job to foreground (fg %<job>)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  <cmd> &";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "       Run command in background\n";

        std::cout << "\n  Text Processing:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  grep";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Search for pattern (-i -n -v)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  head";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Show first N lines (head -n 10)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  tail";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Show last N lines (tail -n 10)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  wc";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            Count lines/words/chars (-l -w -c)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  sort";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Sort lines (-r reverse, -n numeric)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  uniq";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Remove duplicate lines (-c count)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  find";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Find files (find . -name \"*.txt\")\n";

        std::cout << "\n  Redirection & Piping:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd > file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Write output to file\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd >> file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Append output to file\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd | cmd";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "     Pipe output to next command\n";

        std::cout << "\n  System Information:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lsmem";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Memory information\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lscpu";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         CPU information\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lshw";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Hardware overview (sysinfo)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  df/lsblk";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Disk usage and mounts\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lsusb";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         USB devices\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lsnet";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Network interfaces\n";

        std::cout << "\n  Networking:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  ping";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Ping a host\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  traceroute";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Trace route to host\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  curl/wget";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "     HTTP requests / download\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  nslookup";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      DNS lookup\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  net show";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Show WiFi networks\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  net connect";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Connect to WiFi (-p password)\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "\n  External tools: git, node, python, mysql, etc.\n";
        std::cout << "  Run 'registry refresh' to scan for installed commands.\n";

        std::cout << "\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  exit";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Exit the shell\n\n";
    }

    void executeCommand(const std::vector<std::string>& tokens) {
        if (tokens.empty()) {
            return;
        }

        const std::string& cmd = tokens[0];

        std::cout << std::endl;

        try {
        if (cmd == "pwd") {
            cmdPwd(tokens);
        } else if (cmd == "cd") {
            cmdCd(tokens);
        } else if (cmd == "ls" || cmd == "dir") {
            cmdLs(tokens);
        } else if (cmd == "mkdir") {
            cmdMkdir(tokens);
        } else if (cmd == "rm" || cmd == "rmdir") {
            cmdRm(tokens);
        } else if (cmd == "mv") {
            cmdMv(tokens);
        } else if (cmd == "cp" || cmd == "copy") {
            cmdCp(tokens);
        } else if (cmd == "cat" || cmd == "type") {
            cmdCat(tokens);
        } else if (cmd == "touch") {
            cmdTouch(tokens);
        } else if (cmd == "chmod") {
            cmdChmod(tokens);
        } else if (cmd == "chown") {
            cmdChown(tokens);
        } else if (cmd == "clear" || cmd == "cls") {
            cmdClear(tokens);
        } else if (cmd == "help") {
            cmdHelp(tokens);
        } else if (cmd == "nano") {
            std::string nanoCmd = "nano.exe";
            if (tokens.size() > 1) {
                nanoCmd += " \"" + resolvePath(tokens[1]) + "\"";
            }
            system(nanoCmd.c_str());
        } else if (cmd == "lin") {
            cmdLin(tokens);
        } else if (cmd == "registry") {
            // Registry management commands
            if (tokens.size() > 1 && tokens[1] == "refresh") {
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "Scanning for installed commands...";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                
                int found = g_registry.refreshRegistry();
                std::cout << " found " << found << " commands.\n";
                printSuccess("Registry updated! Use 'registry list' to see all commands.");
            } else if (tokens.size() > 1 && tokens[1] == "list") {
                const auto& commands = g_registry.getAllCommands();
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "Registered External Commands";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                std::cout << " (" << commands.size() << " total)\n\n";
                
                for (const auto& pair : commands) {
                    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    std::cout << std::setw(15) << std::left << pair.first;
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    std::cout << " -> " << pair.second << "\n";
                }
            } else if (tokens.size() > 3 && tokens[1] == "add") {
                g_registry.addCommand(tokens[2], tokens[3]);
                printSuccess("Added: " + tokens[2] + " -> " + tokens[3]);
                std::cout << "Saved to: " << g_registry.getDbPath() << std::endl;
            } else {
                std::cout << "Registry Commands:\n";
                std::cout << "  registry refresh      Scan system for installed commands\n";
                std::cout << "  registry list         Show all registered commands\n";
                std::cout << "  registry add <cmd> <path>  Add custom command\n";
            }
        } else if (cmd == "history") {
            cmdHistory(tokens);
        } else if (cmd == "whoami") {
            cmdWhoami(tokens);
        } else if (cmd == "echo") {
            cmdEcho(tokens);
        } else if (cmd == "env" || cmd == "printenv") {
            cmdEnv(tokens);
        } else if (cmd == "export") {
            cmdExport(tokens);
        } else if (cmd == "which" || cmd == "where") {
            cmdWhich(tokens);
        } else if (cmd == "ps") {
            cmdPs(tokens);
        } else if (cmd == "kill") {
            cmdKill(tokens);
        } else if (cmd == "top" || cmd == "htop") {
            cmdTop(tokens);
        } else if (cmd == "jobs") {
            cmdJobs(tokens);
        } else if (cmd == "fg") {
            cmdFg(tokens);
        } else if (cmd == "grep") {
            cmdGrep(tokens);
        } else if (cmd == "head") {
            cmdHead(tokens);
        } else if (cmd == "tail") {
            cmdTail(tokens);
        } else if (cmd == "wc") {
            cmdWc(tokens);
        } else if (cmd == "sort") {
            cmdSort(tokens);
        } else if (cmd == "uniq") {
            cmdUniq(tokens);
        } else if (cmd == "find") {
            cmdFind(tokens);
        } else if (cmd == "lsmem" || cmd == "free") {
            SystemInfo::listMemory();
        } else if (cmd == "lscpu") {
            SystemInfo::listCPU();
        } else if (cmd == "lshw" || cmd == "sysinfo") {
            SystemInfo::listHardware();
        } else if (cmd == "lsmount" || cmd == "lsblk" || cmd == "df") {
            SystemInfo::listMounts();
        } else if (cmd == "lsusb") {
            SystemInfo::listUSB();
        } else if (cmd == "lsnet") {
            SystemInfo::listNetwork();
        } else if (cmd == "lsof") {
            SystemInfo::listOpenFiles();
        } else if (cmd == "ip") {
            Networking::showIP(tokens);
        } else if (cmd == "ping") {
            Networking::ping(tokens);
        } else if (cmd == "traceroute" || cmd == "tracert") {
            Networking::traceroute(tokens);
        } else if (cmd == "nslookup") {
            Networking::nslookup(tokens);
        } else if (cmd == "dig" || cmd == "host") {
            Networking::dig(tokens);
        } else if (cmd == "curl") {
            Networking::curl(tokens);
        } else if (cmd == "wget") {
            Networking::wget(tokens, currentDir);
        } else if (cmd == "net") {
            Networking::netCommand(tokens);
        } else if (cmd == "netstat") {
            Networking::netstat(tokens);
        } else if (cmd == "ifconfig" || cmd == "ipconfig") {
            Networking::showIP(tokens);
        } else if (cmd == "uninstall") {
            cmdUninstall(tokens);
        } else if (cmd == "exit" || cmd == "quit") {
            running = false;
        } else {
            // 1. First check the cmds folder for a binary
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            fs::path cmdsDir = fs::path(exePath).parent_path() / "cmds";
            
            std::vector<std::string> extensions = {".exe", ".cmd", ".bat", ""};
            bool foundInCmds = false;
            
            for (const auto& ext : extensions) {
                fs::path cmdPath = cmdsDir / (cmd + ext);
                if (fs::exists(cmdPath)) {
                    // Execute from cmds folder using CreateProcessA with proper working directory
                    std::string cmdLine = "\"" + cmdPath.string() + "\"";
                    for (size_t i = 1; i < tokens.size(); i++) {
                        cmdLine += " \"" + tokens[i] + "\"";
                    }
                    
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
                        currentDir.c_str(),  // Execute in shell's current directory
                        &si,
                        &pi
                    )) {
                        WaitForSingleObject(pi.hProcess, INFINITE);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                    
                    foundInCmds = true;
                    break;
                }
            }
            
            if (!foundInCmds) {
                // 2. Try to find and execute as registered external command
                if (!g_registry.executeRegisteredCommand(cmd, tokens, currentDir)) {
                    // 3. Command not found anywhere
                    printError("Command not found: " + cmd + ". Type 'help' for available commands.");
                }
            }
        }

        } catch (const std::filesystem::filesystem_error& e) {
            // Handle file system errors
            std::string msg = e.what();
            if (msg.find("permission denied") != std::string::npos || 
                msg.find("Access is denied") != std::string::npos) {
                printError("Permission denied: " + e.path1().string());
            } else if (msg.find("no such file") != std::string::npos || 
                       msg.find("cannot find") != std::string::npos) {
                printError("No such file or directory: " + e.path1().string());
            } else if (msg.find("directory not empty") != std::string::npos) {
                printError("Directory not empty: " + e.path1().string() + " (use rm -r for recursive delete)");
            } else {
                printError("File system error: " + std::string(e.what()));
            }
        } catch (const std::invalid_argument& e) {
            printError("Invalid argument: " + std::string(e.what()));
        } catch (const std::out_of_range& e) {
            printError("Value out of range: " + std::string(e.what()));
        } catch (const std::runtime_error& e) {
            printError("Runtime error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            printError("Error: " + std::string(e.what()));
        } catch (...) {
            printError("An unexpected error occurred.");
        }

        if (cmd != "clear" && cmd != "cls" && running) {
            std::cout << std::endl;
        }
    }

    void runExecutable(const std::string& path, const std::vector<std::string>& args) {
        std::string fullPath = resolvePath(path);
        
        if (!fs::exists(fullPath)) {
            printError("Cannot find: " + path);
            return;
        }
        
        std::string cmdLine = "\"" + fullPath + "\"";
        for (size_t i = 1; i < args.size(); i++) {
            cmdLine += " \"" + args[i] + "\"";
        }
        
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
            
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            DWORD err = GetLastError();
            printError("Failed to execute (error " + std::to_string(err) + ")");
        }
    }

public:
    Linuxify() : running(true) {
        currentDir = fs::current_path().string();
        
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        SetEnvironmentVariableA("SHELL", exePath);
        SetEnvironmentVariableA("LINUXIFY", "1");
        SetEnvironmentVariableA("LINUXIFY_VERSION", "1.0");
    }

    // Check if a command is a built-in Linuxify command
    bool isBuiltinCommand(const std::string& cmd) {
        static std::vector<std::string> builtins = {
            "pwd", "cd", "ls", "dir", "mkdir", "rm", "rmdir", "mv", "cp", "copy", 
            "cat", "type", "touch", "chmod", "chown", "clear", "cls", "help", 
            "nano", "lin", "registry", "history", "whoami", "echo", "env", 
            "printenv", "export", "which", "ps", "kill", "top", "htop", "jobs", "fg",
            "grep", "head", "tail", "wc", "sort", "uniq", "find",
            "lsmem", "free", "lscpu", "lshw", "sysinfo", "lsmount", "lsblk", "df",
            "lsusb", "lsnet", "lsof", "ip", "ping", "traceroute", "tracert",
            "nslookup", "dig", "host", "curl", "wget", "net", "netstat", "ifconfig", "ipconfig"
        };
        for (const auto& builtin : builtins) {
            if (cmd == builtin) return true;
        }
        return false;
    }

    // Execute a command and capture its output
    std::string executeAndCapture(const std::string& cmdStr) {
        std::string output;
        
        // Tokenize to get the command name
        std::vector<std::string> tokens = tokenize(cmdStr);
        if (tokens.empty()) return "";
        
        std::string cmd = tokens[0];
        
        // Check if it's a built-in command
        if (isBuiltinCommand(cmd)) {
            // Capture stdout for built-in commands
            std::ostringstream capturedOutput;
            std::streambuf* oldCout = std::cout.rdbuf();
            std::cout.rdbuf(capturedOutput.rdbuf());
            
            // Execute the built-in command
            executeCommand(tokens);
            
            // Restore stdout
            std::cout.rdbuf(oldCout);
            output = capturedOutput.str();
        } else {
            // External command - use _popen
            std::string fullCmd = "cd /d \"" + currentDir + "\" && " + cmdStr + " 2>&1";
            
            FILE* pipe = _popen(fullCmd.c_str(), "r");
            if (pipe) {
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    output += buffer;
                }
                _pclose(pipe);
            }
        }
        
        return output;
    }

    // Parse and handle output redirection (>, >>) and pipes (|)
    bool handleRedirection(const std::string& input) {
        // Check for output redirection first (> or >>)
        size_t appendPos = input.find(">>");
        size_t writePos = input.find(">");
        size_t pipePos = input.find("|");
        
        // Handle output redirection
        if (appendPos != std::string::npos || (writePos != std::string::npos && (appendPos == std::string::npos || writePos < appendPos))) {
            bool append = (appendPos != std::string::npos && (writePos == std::string::npos || appendPos <= writePos));
            size_t pos = append ? appendPos : writePos;
            size_t skip = append ? 2 : 1;
            
            std::string cmdPart = input.substr(0, pos);
            std::string filePart = input.substr(pos + skip);
            
            // Trim
            cmdPart.erase(cmdPart.find_last_not_of(" \t") + 1);
            filePart.erase(0, filePart.find_first_not_of(" \t"));
            filePart.erase(filePart.find_last_not_of(" \t") + 1);
            
            if (filePart.empty()) {
                printError("Syntax error: missing filename after redirect");
                return true;
            }
            
            std::string outputFile = resolvePath(filePart);
            
            // Use executeAndCapture to get output from any command
            std::string output = executeAndCapture(cmdPart);
            
            // Write to file
            std::ofstream file;
            if (append) {
                file.open(outputFile, std::ios::app);
            } else {
                file.open(outputFile, std::ios::out);
            }
            
            if (!file) {
                printError("Cannot open file: " + filePart);
                return true;
            }
            
            file << output;
            file.close();
            
            return true;
        }
        
        // Handle pipes
        if (pipePos != std::string::npos) {
            std::vector<std::string> commands;
            std::string remaining = input;
            
            while ((pipePos = remaining.find("|")) != std::string::npos) {
                std::string cmd = remaining.substr(0, pipePos);
                cmd.erase(0, cmd.find_first_not_of(" \t"));
                cmd.erase(cmd.find_last_not_of(" \t") + 1);
                commands.push_back(cmd);
                remaining = remaining.substr(pipePos + 1);
            }
            remaining.erase(0, remaining.find_first_not_of(" \t"));
            remaining.erase(remaining.find_last_not_of(" \t") + 1);
            commands.push_back(remaining);
            
            // Execute pipeline
            std::string pipedOutput;
            
            for (size_t i = 0; i < commands.size(); ++i) {
                std::vector<std::string> tokens = tokenize(commands[i]);
                if (tokens.empty()) continue;
                
                std::string& cmd = tokens[0];
                
                // For the first command or external commands, use executeAndCapture
                // For subsequent commands, use built-in text processing with piped input
                if (i == 0) {
                    // First command - capture output using _popen
                    pipedOutput = executeAndCapture(commands[i]);
                } else {
                    // Subsequent commands - use built-in text processing with piped input
                    std::ostringstream capturedOutput;
                    std::streambuf* oldCout = std::cout.rdbuf();
                    std::cout.rdbuf(capturedOutput.rdbuf());
                    
                    if (cmd == "grep") {
                        cmdGrep(tokens, pipedOutput);
                    } else if (cmd == "head") {
                        cmdHead(tokens, pipedOutput);
                    } else if (cmd == "tail") {
                        cmdTail(tokens, pipedOutput);
                    } else if (cmd == "wc") {
                        cmdWc(tokens, pipedOutput);
                    } else if (cmd == "sort") {
                        cmdSort(tokens, pipedOutput);
                    } else if (cmd == "uniq") {
                        cmdUniq(tokens, pipedOutput);
                    } else {
                        // For other commands, just print the piped input
                        std::cout << pipedOutput;
                    }
                    
                    std::cout.rdbuf(oldCout);
                    pipedOutput = capturedOutput.str();
                }
            }
            
            // Output final result with spacing
            std::cout << std::endl << pipedOutput << std::endl;
            return true;
        }
        
        return false;
    }

    void run() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTitleA("Linuxify Shell");
        
        // Load command history
        loadHistory();
        
        system("cls");
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << R"(
  _     _                  _  __       
 | |   (_)_ __  _   ___  _(_)/ _|_   _ 
 | |   | | '_ \| | | \ \/ / | |_| | | |
 | |___| | | | | |_| |>  <| |  _| |_| |
 |_____|_|_| |_|\__,_/_/\_\_|_|  \__, |
                                 |___/ 
)" << std::endl;
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "                              By Cortez\n" << std::endl;
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  Linux Commands for Windows - Type 'help' for commands\n" << std::endl;

        std::string input;
        
        while (running) {
            printPrompt();
            
            if (!std::getline(std::cin, input)) {
                // Check if this was Ctrl+C (cin would be in fail state)
                if (std::cin.fail()) {
                    std::cin.clear();  // Clear error state
                    std::cin.ignore(10000, '\n');  // Clear input buffer
                    continue;  // Continue the loop instead of breaking
                }
                break;  // Only break on actual EOF
            }

            input.erase(0, input.find_first_not_of(" \t"));
            input.erase(input.find_last_not_of(" \t") + 1);

            if (input.empty()) {
                continue;
            }

            // Save command to history (except for history command itself to avoid clutter)
            if (input.substr(0, 7) != "history") {
                saveToHistory(input);
            }

            // Check for redirection or pipes first
            if (handleRedirection(input)) {
                continue;  // Redirection handled the command
            }

            std::vector<std::string> tokens = tokenize(input);
            
            if (!tokens.empty()) {
                // Check for background execution (&)
                bool runBackground = false;
                if (!tokens.empty() && tokens.back() == "&") {
                    runBackground = true;
                    tokens.pop_back();
                    if (tokens.empty()) continue;
                }
                
                std::string& cmd = tokens[0];
                
                if (cmd.substr(0, 2) == "./" || cmd.substr(0, 2) == ".\\" ||
                    cmd.find('/') != std::string::npos || cmd.find('\\') != std::string::npos ||
                    (cmd.length() > 4 && cmd.substr(cmd.length() - 4) == ".exe")) {
                    
                    std::string execPath = cmd;
                    if (cmd.substr(0, 2) == "./" || cmd.substr(0, 2) == ".\\" ) {
                        execPath = cmd.substr(2);
                    }
                    
                    std::cout << std::endl;
                    
                    if (runBackground) {
                        // Build full command line for background execution
                        std::string fullPath = resolvePath(execPath);
                        std::string cmdLine = "\"" + fullPath + "\"";
                        for (size_t i = 1; i < tokens.size(); i++) {
                            cmdLine += " \"" + tokens[i] + "\"";
                        }
                        runInBackground(cmdLine, input);
                    } else {
                        runExecutable(execPath, tokens);
                    }
                    std::cout << std::endl;
                } else if (runBackground) {
                    // Try to run built-in or registry command in background
                    std::string exePath = g_registry.getExecutablePath(cmd);
                    if (!exePath.empty()) {
                        std::string cmdLine = "\"" + exePath + "\"";
                        for (size_t i = 1; i < tokens.size(); i++) {
                            cmdLine += " \"" + tokens[i] + "\"";
                        }
                        std::cout << std::endl;
                        runInBackground(cmdLine, input);
                        std::cout << std::endl;
                    } else {
                        printError("Cannot run in background: " + cmd);
                    }
                } else {
                    executeCommand(tokens);
                }
            }
        }

        std::cout << "\nGoodbye!\n";
    }
};

// Ctrl+C handler - prevents shell from exiting
BOOL WINAPI CtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
            // Just print a new line and return TRUE to indicate we handled it
            // This prevents the shell from exiting
            std::cout << "^C" << std::endl;
            return TRUE;
        case CTRL_BREAK_EVENT:
            return TRUE;
        default:
            return FALSE;
    }
}

int main() {
    // Set up Ctrl+C handler to prevent shell from exiting
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    
    Linuxify shell;
    shell.run();
    return 0;
}

