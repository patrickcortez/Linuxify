// Compile: cl /EHsc /std:c++17 main.cpp /Fe:linuxify.exe

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <map>

namespace fs = std::filesystem;

class Linuxify {
private:
    bool running;
    std::string currentDir;

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

    std::string getPackagesFilePath() {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        return (exeDir / "packages.lin").string();
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

        std::cout << "  Available Commands:\n\n";
        
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
        std::cout << "  clear";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Clear the screen\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  help";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Show this help message\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  nano";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Text editor\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lin";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Package manager (lin get, lin remove, ...)\n";

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
        } else if (cmd == "exit" || cmd == "quit") {
            running = false;
        } else {
            printError("Command not found: " + cmd + ". Type 'help' for available commands.");
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

    void run() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTitleA("Linuxify Shell");
        
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
                break;
            }

            input.erase(0, input.find_first_not_of(" \t"));
            input.erase(input.find_last_not_of(" \t") + 1);

            if (input.empty()) {
                continue;
            }

            std::vector<std::string> tokens = tokenize(input);
            
            if (!tokens.empty()) {
                std::string& cmd = tokens[0];
                
                if (cmd.substr(0, 2) == "./" || cmd.substr(0, 2) == ".\\" ||
                    cmd.find('/') != std::string::npos || cmd.find('\\') != std::string::npos ||
                    (cmd.length() > 4 && cmd.substr(cmd.length() - 4) == ".exe")) {
                    
                    std::string execPath = cmd;
                    if (cmd.substr(0, 2) == "./" || cmd.substr(0, 2) == ".\\" ) {
                        execPath = cmd.substr(2);
                    }
                    
                    std::cout << std::endl;
                    runExecutable(execPath, tokens);
                    std::cout << std::endl;
                } else {
                    executeCommand(tokens);
                }
            }
        }

        std::cout << "\nGoodbye!\n";
    }
};

int main() {
    Linuxify shell;
    shell.run();
    return 0;
}
