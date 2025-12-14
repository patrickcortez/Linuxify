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
#include <functional>
#include <sys/utime.h>

#include "registry.hpp"
#include "process_manager.hpp"
#include "system_info.hpp"
#include "networking.hpp"
#include "cmds-src/interpreter.hpp"

// Global process manager instance
ProcessManager g_procMgr;

// IPC pipe name for crond communication
const char* CROND_PIPE_NAME = "\\\\.\\pipe\\LinuxifyCrond";

namespace fs = std::filesystem;

class Linuxify {
private:
    bool running;
    std::string currentDir;
    std::vector<std::string> commandHistory;
    std::map<std::string, std::string> sessionEnv;  // Session-specific environment variables
    Bash::Interpreter interpreter;

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

    // Execute a command using CreateProcessA (faster, more control than system())
    // Returns exit code, -1 on failure
    int runProcess(const std::string& cmdLine, const std::string& workDir = "", bool wait = true) {
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
        
        const char* dir = workDir.empty() ? currentDir.c_str() : workDir.c_str();
        
        if (!CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,   // Inherit handles for stdin/stdout
            0,
            NULL,
            dir,
            &si,
            &pi
        )) {
            return -1;
        }
        
        int exitCode = 0;
        if (wait) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code;
            GetExitCodeProcess(pi.hProcess, &code);
            exitCode = (int)code;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        return exitCode;
    }
    
    // Clear console screen (faster than system("cls"))
    void clearScreen() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        DWORD count;
        DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
        COORD homeCoords = {0, 0};
        FillConsoleOutputCharacterA(hConsole, ' ', cellCount, homeCoords, &count);
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count);
        SetConsoleCursorPosition(hConsole, homeCoords);
    }

    // Syntax highlighting colors
    static const WORD COLOR_COMMAND = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  // Bright Yellow
    static const WORD COLOR_ARG = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  // Cyan (arguments)
    static const WORD COLOR_STRING = FOREGROUND_RED | FOREGROUND_INTENSITY;  // Red (strings in quotes)
    static const WORD COLOR_FLAG = FOREGROUND_INTENSITY;  // Dark grey (flags like -la)
    static const WORD COLOR_DEFAULT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;  // White

    // Re-render input line with syntax highlighting
    // promptStartRow: the console row where the prompt started (tracked by caller)
    void renderInputWithHighlight(const std::string& input, int cursorPos, int promptStartRow) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        
        int consoleWidth = csbi.dwSize.X;
        int promptLen = 9 + (int)currentDir.length() + 2;  // "linuxify:" + currentDir + "$ "
        
        // Calculate how many lines the current content spans
        int totalLen = promptLen + (int)input.length();
        int numLines = (totalLen + consoleWidth - 1) / consoleWidth;
        if (numLines < 1) numLines = 1;
        
        // Use the provided start row (don't recalculate - avoids drift)
        int startRow = promptStartRow;
        if (startRow < 0) startRow = 0;
        
        // Clear all lines that could contain our input (clear a few extra to be safe)
        COORD clearPos;
        DWORD written;
        for (int i = 0; i < numLines + 2; i++) {
            clearPos.X = 0;
            clearPos.Y = (SHORT)(startRow + i);
            if (clearPos.Y < csbi.dwSize.Y) {  // Don't clear beyond buffer
                FillConsoleOutputCharacterA(hConsole, ' ', consoleWidth, clearPos, &written);
            }
        }
        
        // Move cursor to start of first line
        COORD startPos;
        startPos.X = 0;
        startPos.Y = (SHORT)startRow;
        SetConsoleCursorPosition(hConsole, startPos);
        
        // Reprint prompt
        printPrompt();
        
        if (input.empty()) {
            return;
        }
        
        // Parse and colorize
        bool inQuotes = false;
        char quoteChar = '\0';
        bool isFirstToken = true;
        size_t tokenStart = 0;
        
        for (size_t i = 0; i < input.length(); i++) {
            char c = input[i];
            
            // Handle quotes
            if ((c == '"' || c == '\'') && !inQuotes) {
                inQuotes = true;
                quoteChar = c;
                SetConsoleTextAttribute(hConsole, COLOR_STRING);
                std::cout << c;
                continue;
            }
            if (c == quoteChar && inQuotes) {
                std::cout << c;
                inQuotes = false;
                quoteChar = '\0';
                SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
                continue;
            }
            
            if (inQuotes) {
                std::cout << c;
                continue;
            }
            
            // Handle spaces (token separator)
            if (c == ' ') {
                SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
                std::cout << c;
                isFirstToken = false;
                tokenStart = i + 1;
                continue;
            }
            
            // Determine color based on context
            if (isFirstToken) {
                // Command (first token)
                SetConsoleTextAttribute(hConsole, COLOR_COMMAND);
            } else if (c == '-' && (i == tokenStart || (i > 0 && input[i-1] == ' '))) {
                // Flag starting with -
                SetConsoleTextAttribute(hConsole, COLOR_FLAG);
            } else if (i > 0 && input[i-1] == '-') {
                // Continue flag
                SetConsoleTextAttribute(hConsole, COLOR_FLAG);
            } else {
                // Check if we're in a flag token
                bool isInFlag = false;
                for (size_t j = tokenStart; j < i; j++) {
                    if (input[j] == '-') {
                        isInFlag = true;
                        break;
                    }
                }
                if (isInFlag) {
                    SetConsoleTextAttribute(hConsole, COLOR_FLAG);
                } else {
                    SetConsoleTextAttribute(hConsole, COLOR_ARG);
                }
            }
            
            std::cout << c;
        }
        
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        std::cout.flush();
        
        // Position cursor at the correct location (accounting for wrapping)
        int totalCursorPos = promptLen + cursorPos;
        COORD cursorCoord;
        cursorCoord.Y = (SHORT)(startRow + totalCursorPos / consoleWidth);
        cursorCoord.X = (SHORT)(totalCursorPos % consoleWidth);
        SetConsoleCursorPosition(hConsole, cursorCoord);
    }

    // Read input with syntax highlighting
    std::string readInputWithHighlight() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        
        std::string input;
        int cursorPos = 0;
        int historyIndex = -1;
        
        // Track the row where the prompt started (for consistent rendering)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        int promptStartRow = csbi.dwCursorPosition.Y;
        
        // Save original console mode
        DWORD originalMode;
        GetConsoleMode(hInput, &originalMode);
        
        // Enable raw input mode
        SetConsoleMode(hInput, ENABLE_PROCESSED_INPUT);
        
        while (true) {
            INPUT_RECORD ir;
            DWORD read;
            
            if (!ReadConsoleInputA(hInput, &ir, 1, &read) || read == 0) continue;
            
            if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;
            
            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            char ch = ir.Event.KeyEvent.uChar.AsciiChar;
            
            if (vk == VK_RETURN) {
                // Enter - submit
                std::cout << std::endl;
                break;
            } else if (vk == VK_BACK) {
                // Backspace
                if (cursorPos > 0) {
                    input.erase(cursorPos - 1, 1);
                    cursorPos--;
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                }
            } else if (vk == VK_DELETE) {
                // Delete
                if (cursorPos < (int)input.length()) {
                    input.erase(cursorPos, 1);
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                }
            } else if (vk == VK_LEFT) {
                // Left arrow
                if (cursorPos > 0) {
                    cursorPos--;
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                }
            } else if (vk == VK_RIGHT) {
                // Right arrow
                if (cursorPos < (int)input.length()) {
                    cursorPos++;
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                }
            } else if (vk == VK_UP) {
                // History up
                if (!commandHistory.empty() && historyIndex < (int)commandHistory.size() - 1) {
                    historyIndex++;
                    input = commandHistory[commandHistory.size() - 1 - historyIndex];
                    cursorPos = (int)input.length();
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                }
            } else if (vk == VK_DOWN) {
                // History down
                if (historyIndex > 0) {
                    historyIndex--;
                    input = commandHistory[commandHistory.size() - 1 - historyIndex];
                    cursorPos = (int)input.length();
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                } else if (historyIndex == 0) {
                    historyIndex = -1;
                    input.clear();
                    cursorPos = 0;
                    renderInputWithHighlight(input, cursorPos, promptStartRow);
                }
            } else if (vk == VK_HOME) {
                cursorPos = 0;
                renderInputWithHighlight(input, cursorPos, promptStartRow);
            } else if (vk == VK_END) {
                cursorPos = (int)input.length();
                renderInputWithHighlight(input, cursorPos, promptStartRow);
            } else if (vk == 'C' && (ir.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED)) {
                // Ctrl+C
                std::cout << "^C" << std::endl;
                input.clear();
                cursorPos = 0;
                break;
            } else if (ch >= 32 && ch < 127) {
                // Printable character
                input.insert(cursorPos, 1, ch);
                cursorPos++;
                renderInputWithHighlight(input, cursorPos, promptStartRow);
            }
        }
        
        // Restore console mode
        SetConsoleMode(hInput, originalMode);
        
        return input;
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
                    } else {
                        // Check for executable extensions
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || 
                            ext == ".com" || ext == ".sh" || ext == ".ps1") {
                            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                        }
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
                        // Check for executable extensions
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || 
                            ext == ".com" || ext == ".sh" || ext == ".ps1") {
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
        clearScreen();
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
        std::cout << "  - Windows Terminal integration" << std::endl;
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

    // ============================================================================
    // TEXT PROCESSING COMMANDS
    // ============================================================================

    // less/more - pager for viewing files
    void cmdLess(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (args.size() > 1) {
            std::string filePath = resolvePath(args[1]);
            std::ifstream file(filePath);
            if (!file) {
                printError("less: cannot open '" + args[1] + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("less: missing file operand");
            return;
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        int pageSize = csbi.srWindow.Bottom - csbi.srWindow.Top - 1;
        if (pageSize < 5) pageSize = 20;
        
        size_t currentLine = 0;
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        DWORD oldMode;
        GetConsoleMode(hInput, &oldMode);
        SetConsoleMode(hInput, 0);  // Raw input mode
        
        while (currentLine < lines.size()) {
            // Display a page
            for (int i = 0; i < pageSize && currentLine < lines.size(); ++i, ++currentLine) {
                std::cout << lines[currentLine] << "\n";
            }
            
            if (currentLine >= lines.size()) break;
            
            // Show prompt
            SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
            std::cout << "-- More -- (q to quit, Enter for next line, Space for next page)";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            // Wait for key
            INPUT_RECORD ir;
            DWORD read;
            while (true) {
                ReadConsoleInput(hInput, &ir, 1, &read);
                if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                    char ch = ir.Event.KeyEvent.uChar.AsciiChar;
                    if (ch == 'q' || ch == 'Q') {
                        std::cout << "\r                                                            \r";
                        SetConsoleMode(hInput, oldMode);
                        return;
                    } else if (ch == ' ') {
                        std::cout << "\r                                                            \r";
                        break;  // Next page
                    } else if (ch == '\r' || ch == '\n') {
                        std::cout << "\r                                                            \r";
                        currentLine--;  // Show one more line
                        break;
                    }
                }
            }
        }
        
        SetConsoleMode(hInput, oldMode);
    }

    // cut - extract columns from text
    void cmdCut(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        char delimiter = '\t';  // Default delimiter is tab
        std::vector<int> fields;
        std::string filePath;
        bool byChar = false;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-d" && i + 1 < args.size()) {
                std::string delim = args[++i];
                if (!delim.empty()) delimiter = delim[0];
            } else if (args[i] == "-f" && i + 1 < args.size()) {
                std::string fieldSpec = args[++i];
                // Parse field specification (e.g., "1,2,3" or "1-3")
                std::istringstream fss(fieldSpec);
                std::string part;
                while (std::getline(fss, part, ',')) {
                    size_t dashPos = part.find('-');
                    if (dashPos != std::string::npos) {
                        int start = std::stoi(part.substr(0, dashPos));
                        int end = std::stoi(part.substr(dashPos + 1));
                        for (int f = start; f <= end; ++f) {
                            fields.push_back(f);
                        }
                    } else {
                        fields.push_back(std::stoi(part));
                    }
                }
            } else if (args[i] == "-c" && i + 1 < args.size()) {
                byChar = true;
                std::string charSpec = args[++i];
                std::istringstream css(charSpec);
                std::string part;
                while (std::getline(css, part, ',')) {
                    size_t dashPos = part.find('-');
                    if (dashPos != std::string::npos) {
                        int start = std::stoi(part.substr(0, dashPos));
                        int end = std::stoi(part.substr(dashPos + 1));
                        for (int c = start; c <= end; ++c) {
                            fields.push_back(c);
                        }
                    } else {
                        fields.push_back(std::stoi(part));
                    }
                }
            } else if (args[i][0] != '-') {
                filePath = args[i];
            }
        }
        
        if (fields.empty()) {
            printError("cut: you must specify a list of bytes, characters, or fields");
            return;
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
                printError("cut: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("cut: missing file operand");
            return;
        }
        
        for (const auto& line : lines) {
            if (byChar) {
                // Cut by character position
                std::string result;
                for (int pos : fields) {
                    if (pos > 0 && pos <= (int)line.length()) {
                        result += line[pos - 1];
                    }
                }
                std::cout << result << "\n";
            } else {
                // Cut by field
                std::vector<std::string> tokens;
                std::string token;
                std::istringstream tss(line);
                while (std::getline(tss, token, delimiter)) {
                    tokens.push_back(token);
                }
                
                std::string result;
                for (size_t i = 0; i < fields.size(); ++i) {
                    int fieldNum = fields[i];
                    if (fieldNum > 0 && fieldNum <= (int)tokens.size()) {
                        if (!result.empty()) result += delimiter;
                        result += tokens[fieldNum - 1];
                    }
                }
                std::cout << result << "\n";
            }
        }
    }

    // tr - translate or delete characters
    void cmdTr(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool deleteMode = false;
        bool squeezeMode = false;
        std::string set1, set2;
        
        size_t argIdx = 1;
        while (argIdx < args.size() && args[argIdx][0] == '-') {
            if (args[argIdx] == "-d") deleteMode = true;
            else if (args[argIdx] == "-s") squeezeMode = true;
            argIdx++;
        }
        
        if (argIdx < args.size()) {
            set1 = args[argIdx++];
        }
        if (argIdx < args.size() && !deleteMode) {
            set2 = args[argIdx++];
        }
        
        if (set1.empty()) {
            printError("tr: missing operand");
            return;
        }
        
        // Expand character classes like a-z, A-Z, 0-9
        auto expandSet = [](const std::string& set) -> std::string {
            std::string result;
            for (size_t i = 0; i < set.length(); ++i) {
                if (i + 2 < set.length() && set[i + 1] == '-') {
                    char start = set[i];
                    char end = set[i + 2];
                    for (char c = start; c <= end; ++c) {
                        result += c;
                    }
                    i += 2;
                } else {
                    result += set[i];
                }
            }
            return result;
        };
        
        std::string expandedSet1 = expandSet(set1);
        std::string expandedSet2 = expandSet(set2);
        
        // Extend set2 if shorter than set1
        while (!deleteMode && expandedSet2.length() < expandedSet1.length() && !expandedSet2.empty()) {
            expandedSet2 += expandedSet2.back();
        }
        
        std::string input = pipedInput;
        if (input.empty()) {
            printError("tr: requires piped input");
            return;
        }
        
        std::string result;
        char lastChar = '\0';
        
        for (char c : input) {
            size_t pos = expandedSet1.find(c);
            if (deleteMode) {
                if (pos == std::string::npos) {
                    result += c;
                }
            } else if (pos != std::string::npos) {
                char newChar = (pos < expandedSet2.length()) ? expandedSet2[pos] : c;
                if (!squeezeMode || newChar != lastChar) {
                    result += newChar;
                    lastChar = newChar;
                }
            } else {
                if (!squeezeMode || c != lastChar) {
                    result += c;
                    lastChar = c;
                }
            }
        }
        
        std::cout << result;
    }

    // sed - stream editor (basic s/pattern/replacement/ support)
    void cmdSed(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (args.size() < 2) {
            printError("sed: missing script");
            std::cout << "Usage: sed 's/pattern/replacement/[g]' [file]\n";
            return;
        }
        
        std::string script = args[1];
        std::string filePath;
        bool inPlace = false;
        
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "-i") inPlace = true;
            else if (args[i][0] != '-') filePath = args[i];
        }
        
        // Parse s/pattern/replacement/flags
        if (script.length() < 4 || script[0] != 's') {
            printError("sed: only s/// substitution supported");
            return;
        }
        
        char delim = script[1];
        std::vector<std::string> parts;
        std::string current;
        bool escaped = false;
        
        for (size_t i = 2; i < script.length(); ++i) {
            if (escaped) {
                current += script[i];
                escaped = false;
            } else if (script[i] == '\\') {
                escaped = true;
            } else if (script[i] == delim) {
                parts.push_back(current);
                current.clear();
            } else {
                current += script[i];
            }
        }
        parts.push_back(current);
        
        if (parts.size() < 2) {
            printError("sed: invalid substitution syntax");
            return;
        }
        
        std::string pattern = parts[0];
        std::string replacement = parts[1];
        bool globalReplace = (parts.size() > 2 && parts[2].find('g') != std::string::npos);
        
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
                printError("sed: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("sed: missing file operand or piped input");
            return;
        }
        
        std::ostringstream output;
        for (const auto& line : lines) {
            std::string result = line;
            if (globalReplace) {
                size_t pos = 0;
                while ((pos = result.find(pattern, pos)) != std::string::npos) {
                    result.replace(pos, pattern.length(), replacement);
                    pos += replacement.length();
                }
            } else {
                size_t pos = result.find(pattern);
                if (pos != std::string::npos) {
                    result.replace(pos, pattern.length(), replacement);
                }
            }
            output << result << "\n";
        }
        
        if (inPlace && !filePath.empty()) {
            std::ofstream file(resolvePath(filePath));
            file << output.str();
        } else {
            std::cout << output.str();
        }
    }

    // awk - pattern scanning and processing (basic field extraction)
    void cmdAwk(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (args.size() < 2) {
            printError("awk: missing program");
            std::cout << "Usage: awk '{print $1}' [file] or awk -F: '{print $1}' [file]\n";
            return;
        }
        
        char fieldSep = ' ';
        std::string program;
        std::string filePath;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-F" && i + 1 < args.size()) {
                std::string fs = args[++i];
                if (!fs.empty()) fieldSep = fs[0];
            } else if (args[i].substr(0, 2) == "-F") {
                std::string fs = args[i].substr(2);
                if (!fs.empty()) fieldSep = fs[0];
            } else if (program.empty() && args[i][0] != '-') {
                program = args[i];
            } else if (args[i][0] != '-') {
                filePath = args[i];
            }
        }
        
        // Parse simple print command: {print $1, $2}
        std::vector<int> fieldsToPrint;
        std::string printSep = " ";
        
        if (program.find("print") != std::string::npos) {
            size_t printPos = program.find("print");
            std::string printPart = program.substr(printPos + 5);
            
            // Extract field numbers
            for (size_t i = 0; i < printPart.length(); ++i) {
                if (printPart[i] == '$') {
                    std::string numStr;
                    size_t j = i + 1;
                    while (j < printPart.length() && std::isdigit(printPart[j])) {
                        numStr += printPart[j++];
                    }
                    if (!numStr.empty()) {
                        fieldsToPrint.push_back(std::stoi(numStr));
                    }
                    i = j - 1;
                }
            }
        }
        
        if (fieldsToPrint.empty()) {
            fieldsToPrint.push_back(0);  // Print whole line
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
                printError("awk: cannot open '" + filePath + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("awk: missing file operand or piped input");
            return;
        }
        
        for (const auto& line : lines) {
            std::vector<std::string> fields;
            fields.push_back(line);  // $0 = whole line
            
            // Split by field separator (handling multiple separators as one)
            std::string token;
            bool inToken = false;
            for (char c : line) {
                if (c == fieldSep || (fieldSep == ' ' && std::isspace(c))) {
                    if (inToken) {
                        fields.push_back(token);
                        token.clear();
                        inToken = false;
                    }
                } else {
                    token += c;
                    inToken = true;
                }
            }
            if (inToken) {
                fields.push_back(token);
            }
            
            std::string output;
            for (size_t i = 0; i < fieldsToPrint.size(); ++i) {
                int fieldNum = fieldsToPrint[i];
                if (fieldNum >= 0 && fieldNum < (int)fields.size()) {
                    if (!output.empty()) output += printSep;
                    output += fields[fieldNum];
                }
            }
            std::cout << output << "\n";
        }
    }

    // diff - compare files line by line
    void cmdDiff(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("diff: missing file operands");
            std::cout << "Usage: diff file1 file2\n";
            return;
        }
        
        bool unified = false;
        std::string file1Path, file2Path;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-u") unified = true;
            else if (file1Path.empty()) file1Path = args[i];
            else if (file2Path.empty()) file2Path = args[i];
        }
        
        std::ifstream f1(resolvePath(file1Path));
        std::ifstream f2(resolvePath(file2Path));
        
        if (!f1) {
            printError("diff: cannot open '" + file1Path + "'");
            return;
        }
        if (!f2) {
            printError("diff: cannot open '" + file2Path + "'");
            return;
        }
        
        std::vector<std::string> lines1, lines2;
        std::string line;
        while (std::getline(f1, line)) lines1.push_back(line);
        while (std::getline(f2, line)) lines2.push_back(line);
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        if (unified) {
            std::cout << "--- " << file1Path << "\n";
            std::cout << "+++ " << file2Path << "\n";
        }
        
        // Simple diff algorithm (line-by-line comparison)
        size_t i = 0, j = 0;
        while (i < lines1.size() || j < lines2.size()) {
            if (i >= lines1.size()) {
                // Remaining lines only in file2
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "+ " << lines2[j++] << "\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            } else if (j >= lines2.size()) {
                // Remaining lines only in file1
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::cout << "- " << lines1[i++] << "\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            } else if (lines1[i] == lines2[j]) {
                // Lines match
                if (unified) {
                    std::cout << "  " << lines1[i] << "\n";
                }
                i++; j++;
            } else {
                // Lines differ - look ahead for resync
                bool foundMatch = false;
                for (size_t look = 1; look < 5 && !foundMatch; ++look) {
                    if (j + look < lines2.size() && lines1[i] == lines2[j + look]) {
                        // Extra lines in file2
                        for (size_t k = 0; k < look; ++k) {
                            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                            std::cout << "+ " << lines2[j++] << "\n";
                            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                        }
                        foundMatch = true;
                    } else if (i + look < lines1.size() && lines1[i + look] == lines2[j]) {
                        // Extra lines in file1
                        for (size_t k = 0; k < look; ++k) {
                            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                            std::cout << "- " << lines1[i++] << "\n";
                            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                        }
                        foundMatch = true;
                    }
                }
                if (!foundMatch) {
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                    std::cout << "- " << lines1[i++] << "\n";
                    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    std::cout << "+ " << lines2[j++] << "\n";
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
            }
        }
    }

    // tee - read from stdin and write to stdout and files
    void cmdTee(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (pipedInput.empty()) {
            printError("tee: requires piped input");
            return;
        }
        
        bool appendMode = false;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-a") appendMode = true;
            else if (args[i][0] != '-') files.push_back(resolvePath(args[i]));
        }
        
        // Write to stdout
        std::cout << pipedInput;
        
        // Write to files
        for (const auto& filePath : files) {
            std::ofstream file;
            if (appendMode) {
                file.open(filePath, std::ios::app);
            } else {
                file.open(filePath, std::ios::out);
            }
            if (file) {
                file << pipedInput;
            } else {
                printError("tee: cannot write to '" + filePath + "'");
            }
        }
    }

    // xargs - build and execute command lines from stdin
    void cmdXargs(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (pipedInput.empty()) {
            printError("xargs: requires piped input");
            return;
        }
        
        std::string command = "echo";  // Default command
        bool verbose = false;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-t") verbose = true;
            else if (args[i][0] != '-') {
                command = args[i];
                for (size_t j = i + 1; j < args.size(); ++j) {
                    command += " " + args[j];
                }
                break;
            }
        }
        
        // Collect arguments from input
        std::vector<std::string> inputArgs;
        std::istringstream iss(pipedInput);
        std::string arg;
        while (iss >> arg) {
            inputArgs.push_back(arg);
        }
        
        // Build command line
        std::string cmdLine = command;
        for (const auto& a : inputArgs) {
            cmdLine += " \"" + a + "\"";
        }
        
        if (verbose) {
            std::cout << cmdLine << "\n";
        }
        
        // Execute
        std::vector<std::string> tokens = tokenize(cmdLine);
        executeCommand(tokens);
    }

    // rev - reverse lines character-wise
    void cmdRev(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        std::vector<std::string> lines;
        
        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                lines.push_back(line);
            }
        } else if (args.size() > 1) {
            std::ifstream file(resolvePath(args[1]));
            if (!file) {
                printError("rev: cannot open '" + args[1] + "'");
                return;
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        } else {
            printError("rev: missing file operand or piped input");
            return;
        }
        
        for (const auto& line : lines) {
            std::string reversed(line.rbegin(), line.rend());
            std::cout << reversed << "\n";
        }
    }

    // ============================================================================
    // FILE OPERATIONS COMMANDS
    // ============================================================================

    // ln - create links (symbolic links on Windows)
    void cmdLn(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("ln: missing operands");
            std::cout << "Usage: ln [-s] target link_name\n";
            return;
        }
        
        bool symbolic = false;
        std::string target, linkName;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-s") symbolic = true;
            else if (target.empty()) target = args[i];
            else linkName = args[i];
        }
        
        if (target.empty() || linkName.empty()) {
            printError("ln: missing target or link name");
            return;
        }
        
        std::string targetPath = resolvePath(target);
        std::string linkPath = resolvePath(linkName);
        
        if (!symbolic) {
            // Hard links
            if (CreateHardLinkA(linkPath.c_str(), targetPath.c_str(), NULL)) {
                std::cout << "Created hard link: " << linkName << " -> " << target << "\n";
            } else {
                printError("ln: failed to create hard link (error " + std::to_string(GetLastError()) + ")");
            }
        } else {
            // Symbolic links (requires admin or developer mode on Windows)
            DWORD flags = fs::is_directory(targetPath) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
            flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            
            if (CreateSymbolicLinkA(linkPath.c_str(), targetPath.c_str(), flags)) {
                std::cout << "Created symbolic link: " << linkName << " -> " << target << "\n";
            } else {
                DWORD error = GetLastError();
                if (error == ERROR_PRIVILEGE_NOT_HELD) {
                    printError("ln: symbolic links require admin privileges or Developer Mode enabled");
                } else {
                    printError("ln: failed to create symbolic link (error " + std::to_string(error) + ")");
                }
            }
        }
    }

    // stat - display file status
    void cmdStat(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("stat: missing file operand");
            return;
        }
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-') continue;
            
            std::string filePath = resolvePath(args[i]);
            
            if (!fs::exists(filePath)) {
                printError("stat: cannot stat '" + args[i] + "': No such file or directory");
                continue;
            }
            
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "  File: ";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout << args[i] << "\n";
            
            try {
                auto status = fs::status(filePath);
                auto fileSize = fs::is_regular_file(filePath) ? fs::file_size(filePath) : 0;
                auto lastWrite = fs::last_write_time(filePath);
                
                std::string fileType;
                if (fs::is_regular_file(filePath)) fileType = "regular file";
                else if (fs::is_directory(filePath)) fileType = "directory";
                else if (fs::is_symlink(filePath)) fileType = "symbolic link";
                else if (fs::is_block_file(filePath)) fileType = "block device";
                else if (fs::is_character_file(filePath)) fileType = "character device";
                else fileType = "unknown";
                
                std::cout << "  Size: " << fileSize << " bytes\n";
                std::cout << "  Type: " << fileType << "\n";
                
                // Get file attributes
                DWORD attrs = GetFileAttributesA(filePath.c_str());
                std::string attrStr;
                if (attrs & FILE_ATTRIBUTE_READONLY) attrStr += "readonly ";
                if (attrs & FILE_ATTRIBUTE_HIDDEN) attrStr += "hidden ";
                if (attrs & FILE_ATTRIBUTE_SYSTEM) attrStr += "system ";
                if (attrs & FILE_ATTRIBUTE_ARCHIVE) attrStr += "archive ";
                if (attrStr.empty()) attrStr = "none";
                std::cout << " Attrs: " << attrStr << "\n";
                
                // Convert file time to readable format
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    lastWrite - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                auto time = std::chrono::system_clock::to_time_t(sctp);
                std::cout << "Modify: " << std::ctime(&time);
                
            } catch (const std::exception& e) {
                printError("stat: " + std::string(e.what()));
            }
            
            std::cout << "\n";
        }
    }

    // file - determine file type
    void cmdFile(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("file: missing file operand");
            return;
        }
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-') continue;
            
            std::string filePath = resolvePath(args[i]);
            
            if (!fs::exists(filePath)) {
                std::cout << args[i] << ": cannot open (No such file or directory)\n";
                continue;
            }
            
            std::cout << args[i] << ": ";
            
            if (fs::is_directory(filePath)) {
                std::cout << "directory\n";
                continue;
            }
            
            if (fs::is_symlink(filePath)) {
                std::cout << "symbolic link\n";
                continue;
            }
            
            // Read magic bytes
            std::ifstream file(filePath, std::ios::binary);
            if (!file) {
                std::cout << "cannot open\n";
                continue;
            }
            
            unsigned char magic[16] = {0};
            file.read(reinterpret_cast<char*>(magic), 16);
            size_t bytesRead = file.gcount();
            
            if (bytesRead == 0) {
                std::cout << "empty\n";
                continue;
            }
            
            // Detect file type by magic bytes
            if (magic[0] == 0x4D && magic[1] == 0x5A) {
                std::cout << "PE32 executable (Windows)\n";
            } else if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
                std::cout << "ELF executable (Linux)\n";
            } else if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') {
                std::cout << "PNG image data\n";
            } else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
                std::cout << "JPEG image data\n";
            } else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F') {
                std::cout << "GIF image data\n";
            } else if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
                std::cout << "Zip archive data\n";
            } else if (magic[0] == 0x1F && magic[1] == 0x8B) {
                std::cout << "gzip compressed data\n";
            } else if (magic[0] == '%' && magic[1] == 'P' && magic[2] == 'D' && magic[3] == 'F') {
                std::cout << "PDF document\n";
            } else if (magic[0] == 0xD0 && magic[1] == 0xCF) {
                std::cout << "Microsoft Office document\n";
            } else if (magic[0] == '<' && (magic[1] == '?' || magic[1] == '!' || magic[1] == 'h')) {
                std::cout << "HTML/XML document\n";
            } else if (magic[0] == '{' || magic[0] == '[') {
                std::cout << "JSON data\n";
            } else if (magic[0] == '#' && magic[1] == '!') {
                std::cout << "script, shebang executable\n";
            } else {
                // Check if it's text
                bool isText = true;
                for (size_t j = 0; j < bytesRead; ++j) {
                    if (magic[j] < 0x09 || (magic[j] > 0x0D && magic[j] < 0x20 && magic[j] != 0x1B)) {
                        if (magic[j] != 0) isText = false;
                    }
                }
                if (isText) {
                    std::cout << "ASCII text\n";
                } else {
                    std::cout << "data\n";
                }
            }
        }
    }

    // readlink - print resolved symbolic link
    void cmdReadlink(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("readlink: missing file operand");
            return;
        }
        
        bool canonicalize = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-f" || args[i] == "-e" || args[i] == "-m") {
                canonicalize = true;
                continue;
            }
            if (args[i][0] == '-') continue;
            
            std::string filePath = resolvePath(args[i]);
            
            if (canonicalize) {
                try {
                    std::cout << fs::canonical(filePath).string() << "\n";
                } catch (...) {
                    std::cout << fs::absolute(filePath).string() << "\n";
                }
            } else {
                if (fs::is_symlink(filePath)) {
                    try {
                        std::cout << fs::read_symlink(filePath).string() << "\n";
                    } catch (const std::exception& e) {
                        printError("readlink: " + std::string(e.what()));
                    }
                } else {
                    printError("readlink: '" + args[i] + "' is not a symbolic link");
                }
            }
        }
    }

    // realpath - print resolved absolute path
    void cmdRealpath(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("realpath: missing file operand");
            return;
        }
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-') continue;
            
            std::string filePath = resolvePath(args[i]);
            
            try {
                if (fs::exists(filePath)) {
                    std::cout << fs::canonical(filePath).string() << "\n";
                } else {
                    std::cout << fs::absolute(filePath).string() << "\n";
                }
            } catch (const std::exception& e) {
                std::cout << fs::absolute(filePath).string() << "\n";
            }
        }
    }

    // basename - strip directory from filename
    void cmdBasename(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("basename: missing operand");
            return;
        }
        
        std::string suffix;
        if (args.size() > 2 && args[args.size() - 2] == "-s") {
            suffix = args[args.size() - 1];
        }
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-a" || args[i] == "-s") continue;
            if (i > 1 && args[i - 1] == "-s") continue;
            
            fs::path p(args[i]);
            std::string name = p.filename().string();
            
            if (!suffix.empty() && name.length() > suffix.length()) {
                if (name.substr(name.length() - suffix.length()) == suffix) {
                    name = name.substr(0, name.length() - suffix.length());
                }
            }
            
            std::cout << name << "\n";
        }
    }

    // dirname - strip filename from path
    void cmdDirname(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("dirname: missing operand");
            return;
        }
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-') continue;
            
            fs::path p(args[i]);
            std::string dir = p.parent_path().string();
            if (dir.empty()) dir = ".";
            
            std::cout << dir << "\n";
        }
    }

    // tree - display directory tree
    void cmdTree(const std::vector<std::string>& args) {
        std::string path = ".";
        int maxDepth = -1;
        bool dirsOnly = false;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-L" && i + 1 < args.size()) {
                maxDepth = std::stoi(args[++i]);
            } else if (args[i] == "-d") {
                dirsOnly = true;
            } else if (args[i][0] != '-') {
                path = args[i];
            }
        }
        
        std::string fullPath = resolvePath(path);
        if (!fs::exists(fullPath) || !fs::is_directory(fullPath)) {
            printError("tree: '" + path + "' is not a directory");
            return;
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << path << "\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        int dirCount = 0, fileCount = 0;
        
        std::function<void(const fs::path&, const std::string&, int)> printTree;
        printTree = [&](const fs::path& p, const std::string& prefix, int depth) {
            if (maxDepth >= 0 && depth >= maxDepth) return;
            
            std::vector<fs::directory_entry> entries;
            try {
                for (const auto& entry : fs::directory_iterator(p)) {
                    if (dirsOnly && !entry.is_directory()) continue;
                    entries.push_back(entry);
                }
            } catch (...) {
                return;
            }
            
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.path().filename() < b.path().filename();
            });
            
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                bool isLast = (i == entries.size() - 1);
                
                std::cout << prefix;
                std::cout << (isLast ? "`-- " : "|-- ");
                
                if (entry.is_directory()) {
                    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    std::cout << entry.path().filename().string() << "\n";
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    dirCount++;
                    printTree(entry.path(), prefix + (isLast ? "    " : "|   "), depth + 1);
                } else {
                    std::cout << entry.path().filename().string() << "\n";
                    fileCount++;
                }
            }
        };
        
        printTree(fullPath, "", 0);
        
        std::cout << "\n" << dirCount << " directories";
        if (!dirsOnly) {
            std::cout << ", " << fileCount << " files";
        }
        std::cout << "\n";
    }

    // du - disk usage
    void cmdDu(const std::vector<std::string>& args) {
        bool humanReadable = false;
        bool summary = false;
        int maxDepth = -1;
        std::vector<std::string> paths;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-h") humanReadable = true;
            else if (args[i] == "-s") summary = true;
            else if (args[i] == "-d" && i + 1 < args.size()) {
                maxDepth = std::stoi(args[++i]);
            } else if (args[i].substr(0, 2) == "-d") {
                maxDepth = std::stoi(args[i].substr(2));
            } else if (args[i][0] != '-') {
                paths.push_back(args[i]);
            }
        }
        
        if (paths.empty()) paths.push_back(".");
        
        auto formatSize = [humanReadable](uintmax_t bytes) -> std::string {
            if (!humanReadable) {
                return std::to_string(bytes / 1024);  // KB
            }
            
            const char* units[] = {"B", "K", "M", "G", "T"};
            int unit = 0;
            double size = (double)bytes;
            while (size >= 1024 && unit < 4) {
                size /= 1024;
                unit++;
            }
            
            std::ostringstream oss;
            if (unit == 0) {
                oss << (int)size << units[unit];
            } else {
                oss << std::fixed << std::setprecision(1) << size << units[unit];
            }
            return oss.str();
        };
        
        for (const auto& path : paths) {
            std::string fullPath = resolvePath(path);
            
            if (!fs::exists(fullPath)) {
                printError("du: cannot access '" + path + "': No such file or directory");
                continue;
            }
            
            std::function<uintmax_t(const fs::path&, int)> calcSize;
            calcSize = [&](const fs::path& p, int depth) -> uintmax_t {
                uintmax_t total = 0;
                
                try {
                    if (fs::is_regular_file(p)) {
                        return fs::file_size(p);
                    }
                    
                    for (const auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                        if (entry.is_directory()) {
                            uintmax_t dirSize = calcSize(entry.path(), depth + 1);
                            total += dirSize;
                            
                            if (!summary && (maxDepth < 0 || depth < maxDepth)) {
                                std::cout << std::setw(8) << formatSize(dirSize) << "\t" 
                                         << entry.path().string() << "\n";
                            }
                        } else if (entry.is_regular_file()) {
                            total += fs::file_size(entry.path());
                        }
                    }
                } catch (...) {}
                
                return total;
            };
            
            uintmax_t totalSize = calcSize(fullPath, 0);
            std::cout << std::setw(8) << formatSize(totalSize) << "\t" << path << "\n";
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

        std::cout << "\n  Text Processing (Extended):\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  less/more";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "     Pager for viewing files (q to quit)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cut";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Extract columns (-d delim -f fields)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  tr";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            Translate characters (tr a-z A-Z)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  sed";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Stream editor (sed 's/old/new/g')\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  awk";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Field extraction (awk '{print $1}')\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  diff";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Compare files (diff file1 file2)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  tee";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Write to stdout and file (-a append)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  xargs";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "         Build command from stdin\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  rev";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Reverse lines character-wise\n";

        std::cout << "\n  File Operations:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  ln";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            Create links (-s for symbolic)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  stat";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Display file status/metadata\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Determine file type\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  readlink";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Print resolved symlink target\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  realpath";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Resolve to absolute path\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  basename";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Strip directory from path\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  dirname";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "       Strip filename from path\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  tree";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Directory tree view (-L depth)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  du";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "            Disk usage (-h human, -s summary)\n";

        std::cout << "\n  Redirection & Piping:\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd > file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Write stdout to file\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd >> file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Append stdout to file\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd 2> file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Write stderr to file\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd 2>&1";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Redirect stderr to stdout\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  cmd &> file";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Redirect both to file\n";

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
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Task Scheduler (Cron):\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  crontab -l";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      List scheduled tasks\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  crontab -e";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Edit crontab in nano\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  crontab -r";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Remove all scheduled tasks\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  setup cron";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Verify/fix cron daemon config\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  Cron runs at system boot. Format: min hour day month weekday cmd\n";
        std::cout << "  Special: @reboot @hourly @daily @weekly @monthly @yearly\n";

        std::cout << "\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  exit";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Exit the shell\n\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Setup:\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  Run 'setup install' to register .sh files with Windows.\n\n";
    }

    // Setup command - register file associations
    void cmdSetup(const std::vector<std::string>& args) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        if (args.size() < 2) {
            std::cout << "Setup Commands:\n";
            std::cout << "  setup install      Register .sh files with Windows (requires admin)\n";
            std::cout << "  setup uninstall    Remove .sh file association\n";
            std::cout << "  setup status       Check current file association\n";
            std::cout << "  setup admin        Enable sudo command for Windows (requires admin)\n";
            std::cout << "  setup cron         Configure cron daemon (auto-start at boot)\n";
            std::cout << "  setup windux       Add 'Open in Windux' to Explorer right-click menu\n";
            return;
        }
        
        std::string action = args[1];
        
        // Get path to lish.exe and current exe
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path lishPath = fs::path(exePath).parent_path() / "cmds" / "lish.exe";
        
        // Check if running as admin - ALL setup commands require admin
        {
            BOOL isAdmin = FALSE;
            PSID adminGroup = NULL;
            SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
            
            if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
                CheckTokenMembership(NULL, adminGroup, &isAdmin);
                FreeSid(adminGroup);
            }
            
            if (!isAdmin) {
                std::cout << "Administrator privileges required. Requesting elevation...\n";
                
                // Build command line for elevated process
                std::string params = "-c \"setup " + action + "\"";
                
                SHELLEXECUTEINFOA sei = { sizeof(sei) };
                sei.lpVerb = "runas";
                sei.lpFile = exePath;
                sei.lpParameters = params.c_str();
                sei.hwnd = NULL;
                sei.nShow = SW_SHOWNORMAL;
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                
                if (ShellExecuteExA(&sei)) {
                    // Wait for elevated process to complete
                    if (sei.hProcess) {
                        WaitForSingleObject(sei.hProcess, INFINITE);
                        CloseHandle(sei.hProcess);
                    }
                    std::cout << "\nSetup completed. You may need to restart your terminal.\n";
                } else {
                    printError("Failed to get administrator privileges.");
                }
                return;
            }
        }
        
        if (action == "install") {
            if (!fs::exists(lishPath)) {
                printError("lish.exe not found at: " + lishPath.string());
                return;
            }
            
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Registering .sh files with Linuxify Shell...\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            // Create file type using ftype and assoc commands
            std::string ftypeCmd = "ftype LishScript=\"" + lishPath.string() + "\" \"%1\" %*";
            std::string assocCmd = "assoc .sh=LishScript";
            
            // Run as admin commands
            int result1 = runProcess("cmd /c " + ftypeCmd);
            int result2 = runProcess("cmd /c " + assocCmd);
            
            // Add .SH to PATHEXT for PowerShell compatibility
            std::cout << "Adding .SH to PATHEXT for PowerShell...\n";
            
            // Check if .SH is already in PATHEXT
            char* pathext = nullptr;
            size_t pathextLen = 0;
            _dupenv_s(&pathext, &pathextLen, "PATHEXT");
            
            bool needsPathext = true;
            if (pathext) {
                std::string pathextStr = pathext;
                std::transform(pathextStr.begin(), pathextStr.end(), pathextStr.begin(), ::toupper);
                if (pathextStr.find(".SH") != std::string::npos) {
                    needsPathext = false;
                    std::cout << ".SH already in PATHEXT\n";
                }
                free(pathext);
            }
            
            int result3 = 0;
            if (needsPathext) {
                // Add .SH to system PATHEXT (requires admin)
                result3 = runProcess("cmd /c setx PATHEXT \"%PATHEXT%;.SH\" /M");
            }
            
            if (result1 == 0 && result2 == 0) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "\nSuccess! .sh files are now associated with lish.exe\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                std::cout << "\nHow to run .sh scripts:\n";
                std::cout << "  From cmd:         script.sh  or  .\\script.sh\n";
                std::cout << "  From PowerShell:  lish script.sh  or  cmd /c .\\script.sh\n";
                std::cout << "  Double-click:     Works in Explorer\n";
                std::cout << "  From Linuxify:    ./script.sh\n";
                std::cout << "\nNote: Scripts must have a shebang (e.g., #!lish)\n";
                if (needsPathext && result3 == 0) {
                    std::cout << "\nRestart your terminal for PATHEXT changes to take effect.\n";
                }
            } else {
                printError("Failed to register. Try running as Administrator.");
            }
            
        } else if (action == "uninstall") {
            std::cout << "Removing .sh file association...\n";
            
            runProcess("cmd /c assoc .sh=");
            runProcess("cmd /c ftype LishScript=");
            
            printSuccess("File association removed.");
            std::cout << "\nNote: .SH was added to PATHEXT. To remove it:\n";
            std::cout << "  1. Open System Properties > Environment Variables\n";
            std::cout << "  2. Edit PATHEXT and remove ;.SH\n";
            
        } else if (action == "status") {
            std::cout << "Checking .sh file association...\n\n";
            
            // Check current association
            runProcess("cmd /c assoc .sh 2>nul");
            runProcess("cmd /c ftype LishScript 2>nul");
            
            std::cout << "\nlish.exe location: " << lishPath.string() << "\n";
            std::cout << "Exists: " << (fs::exists(lishPath) ? "Yes" : "No") << "\n";
        } else if (action == "admin") {
            // Enable Windows 11 native sudo command
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Enabling Windows 11 native sudo...\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            // Check Windows version first
            std::cout << "Checking Windows version...\n";
            
            // Enable sudo in normal mode (runs in same terminal, allows input)
            // This requires Windows 11 24H2 or later
            std::cout << "Running: sudo config --enable normal\n";
            int result = runProcess("sudo config --enable normal");
            
            if (result == 0) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "\nSuccess! Windows sudo is now enabled.\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                std::cout << "\nThis means sudo commands run in the SAME terminal window!\n";
                std::cout << "\nUsage:\n";
                std::cout << "  From cmd:         sudo <command>\n";
                std::cout << "  From PowerShell:  sudo <command>\n";
                std::cout << "  From Linuxify:    sudo <command>\n";
                std::cout << "\nExamples:\n";
                std::cout << "  sudo notepad C:\\Windows\\System32\\drivers\\etc\\hosts\n";
                std::cout << "  sudo netsh wlan show profiles\n";
                std::cout << "  sudo ln -s source.txt link.txt\n";
            } else {
                printError("Failed to enable sudo.");
                std::cout << "\nPossible causes:\n";
                std::cout << "  1. Windows 11 version 24H2 or later is required\n";
                std::cout << "  2. You may need to update Windows\n";
                std::cout << "  3. Run 'winver' to check your Windows version\n";
                std::cout << "\nAlternatively, you can enable it manually:\n";
                std::cout << "  Settings > System > For Developers > Enable sudo\n";
            }
            
            
        } else if (action == "cron") {
            // Setup/verify cron daemon
            std::cout << "Checking cron daemon configuration...\n\n";
            
            // Get paths
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            fs::path crondPath = fs::path(exePath).parent_path() / "cmds" / "crond.exe";
            fs::path crontabPath = fs::path(exePath).parent_path() / "linuxdb" / "crontab";
            
            bool allGood = true;
            
            // 1. Check if crond.exe exists
            std::cout << "[1/4] Checking crond.exe... ";
            if (fs::exists(crondPath)) {
                std::cout << "\033[32mOK\033[0m\n";
            } else {
                std::cout << "\033[31mMISSING\033[0m\n";
                printError("crond.exe not found at: " + crondPath.string());
                allGood = false;
            }
            
            // 2. Check if crontab exists
            std::cout << "[2/4] Checking crontab file... ";
            if (fs::exists(crontabPath)) {
                std::cout << "\033[32mOK\033[0m\n";
            } else {
                std::cout << "\033[33mCREATING\033[0m\n";
                // Create default crontab
                fs::create_directories(crontabPath.parent_path());
                std::ofstream file(crontabPath);
                file << "# Linuxify Crontab\n";
                file << "# Format: min hour day month weekday command\n";
                file << "# Special: @reboot @hourly @daily @weekly @monthly @yearly\n\n";
                std::cout << "  Created: " << crontabPath.string() << "\n";
            }
            
            // 3. Check if crond is registered to start at system boot
            std::cout << "[3/4] Checking system startup registration... ";
            HKEY hKey;
            bool isRegistered = false;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                              "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                              0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                char value[MAX_PATH];
                DWORD size = sizeof(value);
                isRegistered = RegQueryValueExA(hKey, "LinuxifyCrond", NULL, NULL,
                                                (LPBYTE)value, &size) == ERROR_SUCCESS;
                RegCloseKey(hKey);
            }
            
            if (isRegistered) {
                std::cout << "\033[32mOK\033[0m (starts at system boot)\n";
            } else {
                std::cout << "\033[33mNOT REGISTERED\033[0m\n";
                std::cout << "  Installing crond to start at system boot...\n";
                
                // Register in startup (HKLM requires admin)
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                  "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                  0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                    std::string regValue = "\"" + crondPath.string() + "\"";
                    if (RegSetValueExA(hKey, "LinuxifyCrond", 0, REG_SZ,
                                       (const BYTE*)regValue.c_str(),
                                       (DWORD)(regValue.length() + 1)) == ERROR_SUCCESS) {
                        std::cout << "  \033[32mInstalled to system startup\033[0m\n";
                    } else {
                        printError("Failed to register crond (may need admin rights)");
                        allGood = false;
                    }
                    RegCloseKey(hKey);
                } else {
                    printError("Failed to access registry (run as admin)");
                    allGood = false;
                }
            }
            
            // 4. Check if crond is currently running
            std::cout << "[4/4] Checking if crond is running... ";
            HANDLE hPipe = CreateFileA(
                CROND_PIPE_NAME,
                GENERIC_READ | GENERIC_WRITE,
                0, NULL, OPEN_EXISTING, 0, NULL
            );
            
            if (hPipe != INVALID_HANDLE_VALUE) {
                // Send PING to verify
                const char* msg = "PING";
                DWORD bytesWritten;
                WriteFile(hPipe, msg, (DWORD)strlen(msg), &bytesWritten, NULL);
                
                char response[256];
                DWORD bytesRead;
                if (ReadFile(hPipe, response, sizeof(response) - 1, &bytesRead, NULL) && bytesRead > 0) {
                    std::cout << "\033[32mRUNNING\033[0m\n";
                } else {
                    std::cout << "\033[31mNOT RESPONDING\033[0m\n";
                    allGood = false;
                }
                CloseHandle(hPipe);
            } else {
                std::cout << "\033[33mNOT RUNNING\033[0m\n";
                std::cout << "  Starting crond daemon...\n";
                
                // Start crond in background
                STARTUPINFOA si;
                PROCESS_INFORMATION pi;
                ZeroMemory(&si, sizeof(si));
                si.cb = sizeof(si);
                ZeroMemory(&pi, sizeof(pi));
                
                std::string cmdLine = "\"" + crondPath.string() + "\"";
                char cmdBuffer[1024];
                strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);
                
                if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, FALSE,
                                   CREATE_NO_WINDOW | DETACHED_PROCESS,
                                   NULL, NULL, &si, &pi)) {
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    std::cout << "  \033[32mStarted crond daemon\033[0m\n";
                } else {
                    printError("Failed to start crond daemon");
                    allGood = false;
                }
            }
            
            std::cout << "\n";
            if (allGood) {
                printSuccess("Cron daemon is configured correctly!");
                std::cout << "Use 'crontab -e' to edit scheduled tasks.\n";
            } else {
                printError("Some issues found. Please fix them above.");
            }
            
        } else if (action == "windux") {
            // Add "Open in Windux" to Windows Explorer context menu
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            fs::path winduxPath = fs::path(exePath).parent_path() / "cmds" / "windux.exe";
            
            if (!fs::exists(winduxPath)) {
                printError("windux.exe not found at: " + winduxPath.string());
                return;
            }
            
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Adding 'Open in Windux' to Windows Explorer context menu...\n";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            std::string winduxCmd = "\"" + winduxPath.string() + "\"";
            bool success = true;
            HKEY hKey;
            
            // 1. Add to Directory Background (right-click in folder empty space)
            std::cout << "[1/3] Directory background context menu... ";
            std::string keyPath1 = "Directory\\Background\\shell\\Windux";
            if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath1.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)"Open in Windux", 15);
                RegSetValueExA(hKey, "Icon", 0, REG_SZ, (const BYTE*)winduxCmd.c_str(), (DWORD)(winduxCmd.length() + 1));
                RegCloseKey(hKey);
                
                std::string cmdKeyPath = keyPath1 + "\\command";
                if (RegCreateKeyExA(HKEY_CLASSES_ROOT, cmdKeyPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    std::string cmd = winduxCmd + " \"%V\"";
                    RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() + 1));
                    RegCloseKey(hKey);
                    std::cout << "\033[32mOK\033[0m\n";
                } else { std::cout << "\033[31mFAILED\033[0m\n"; success = false; }
            } else { std::cout << "\033[31mFAILED\033[0m\n"; success = false; }
            
            // 2. Add to Desktop Background (right-click on desktop)
            std::cout << "[2/3] Desktop background context menu... ";
            std::string keyPath2 = "DesktopBackground\\shell\\Windux";
            if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath2.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)"Open in Windux", 15);
                RegSetValueExA(hKey, "Icon", 0, REG_SZ, (const BYTE*)winduxCmd.c_str(), (DWORD)(winduxCmd.length() + 1));
                RegCloseKey(hKey);
                
                std::string cmdKeyPath = keyPath2 + "\\command";
                if (RegCreateKeyExA(HKEY_CLASSES_ROOT, cmdKeyPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    std::string cmd = winduxCmd + " \"%V\"";
                    RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() + 1));
                    RegCloseKey(hKey);
                    std::cout << "\033[32mOK\033[0m\n";
                } else { std::cout << "\033[31mFAILED\033[0m\n"; success = false; }
            } else { std::cout << "\033[31mFAILED\033[0m\n"; success = false; }
            
            // 3. Add to Folder context menu (right-click on a folder)
            std::cout << "[3/3] Folder context menu... ";
            std::string keyPath3 = "Directory\\shell\\Windux";
            if (RegCreateKeyExA(HKEY_CLASSES_ROOT, keyPath3.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)"Open in Windux", 15);
                RegSetValueExA(hKey, "Icon", 0, REG_SZ, (const BYTE*)winduxCmd.c_str(), (DWORD)(winduxCmd.length() + 1));
                RegCloseKey(hKey);
                
                std::string cmdKeyPath = keyPath3 + "\\command";
                if (RegCreateKeyExA(HKEY_CLASSES_ROOT, cmdKeyPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                    std::string cmd = winduxCmd + " \"%V\"";
                    RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() + 1));
                    RegCloseKey(hKey);
                    std::cout << "\033[32mOK\033[0m\n";
                } else { std::cout << "\033[31mFAILED\033[0m\n"; success = false; }
            } else { std::cout << "\033[31mFAILED\033[0m\n"; success = false; }
            
            std::cout << "\n";
            if (success) {
                printSuccess("'Open in Windux' added to Explorer context menu!");
                std::cout << "Right-click in Explorer to see the option.\n";
            } else {
                printError("Some entries failed. Try running as Administrator.");
            }
            
        } else {
            printError("Unknown setup action: " + action);
        }
    }

    void executeCommand(const std::vector<std::string>& tokens) {
        if (tokens.empty()) {
            return;
        }

        const std::string& cmd = tokens[0];

        // Only print newline if not running from a script (how to detect?)
        // For now, keep it potentially noisy or improve later.
        // std::cout << std::endl; 

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
            runProcess(nanoCmd);
        } else if (cmd == "lin") {
            cmdLin(tokens);
        } else if (cmd == "setup") {
            cmdSetup(tokens);
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
            } else if (tokens.size() > 2 && (tokens[1] == "delete" || tokens[1] == "remove" || tokens[1] == "rm")) {
                g_registry.removeCommand(tokens[2]);
                printSuccess("Removed: " + tokens[2]);
            } else {
                std::cout << "Registry Commands:\n";
                std::cout << "  registry refresh          Scan system for installed commands\n";
                std::cout << "  registry list             Show all registered commands\n";
                std::cout << "  registry add <cmd> <path> Add custom command\n";
                std::cout << "  registry delete <cmd>     Remove a command\n";
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
        // Text Processing Commands
        } else if (cmd == "less" || cmd == "more") {
            cmdLess(tokens);
        } else if (cmd == "cut") {
            cmdCut(tokens);
        } else if (cmd == "tr") {
            cmdTr(tokens);
        } else if (cmd == "sed") {
            cmdSed(tokens);
        } else if (cmd == "awk") {
            cmdAwk(tokens);
        } else if (cmd == "diff") {
            cmdDiff(tokens);
        } else if (cmd == "tee") {
            cmdTee(tokens);
        } else if (cmd == "xargs") {
            cmdXargs(tokens);
        } else if (cmd == "rev") {
            cmdRev(tokens);
        // File Operations Commands
        } else if (cmd == "ln") {
            cmdLn(tokens);
        } else if (cmd == "stat") {
            cmdStat(tokens);
        } else if (cmd == "file") {
            cmdFile(tokens);
        } else if (cmd == "readlink") {
            cmdReadlink(tokens);
        } else if (cmd == "realpath") {
            cmdRealpath(tokens);
        } else if (cmd == "basename") {
            cmdBasename(tokens);
        } else if (cmd == "dirname") {
            cmdDirname(tokens);
        } else if (cmd == "tree") {
            cmdTree(tokens);
        } else if (cmd == "du") {
            cmdDu(tokens);
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
        } else if (cmd == "gcc" || cmd == "g++" || cmd == "cc" || cmd == "c++" || 
                   cmd == "make" || cmd == "gdb" || cmd == "ar" || cmd == "ld" ||
                   cmd == "objdump" || cmd == "objcopy" || cmd == "strip" || cmd == "windres" ||
                   cmd == "as" || cmd == "nm" || cmd == "ranlib" || cmd == "size" ||
                   cmd == "strings" || cmd == "addr2line" || cmd == "c++filt") {
            // Handle bundled toolchain commands
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            fs::path toolchainBin = fs::path(exePath).parent_path() / "toolchain" / "compiler" / "mingw64" / "bin";
            
            // Map cc -> gcc, c++ -> g++
            std::string actualCmd = cmd;
            if (cmd == "cc") actualCmd = "gcc";
            else if (cmd == "c++") actualCmd = "g++";
            
            fs::path cmdPath = toolchainBin / (actualCmd + ".exe");
            
            if (fs::exists(cmdPath)) {
                std::string cmdLine = "\"" + cmdPath.string() + "\"";
                for (size_t i = 1; i < tokens.size(); i++) {
                    cmdLine += " \"" + tokens[i] + "\"";
                }
                
                STARTUPINFOA si;
                PROCESS_INFORMATION pi;
                ZeroMemory(&si, sizeof(si));
                si.cb = sizeof(si);
                ZeroMemory(&pi, sizeof(pi));
                
                char cmdBuffer[8192];
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
                    printError("Failed to execute: " + cmd);
                }
            } else {
                printError("Toolchain not found. Expected at: " + toolchainBin.string());
                printError("Please reinstall Linuxify or check toolchain installation.");
            }
        } else if (cmd == "sudo") {
            // Use Windows 11 native sudo
            if (tokens.size() < 2) {
                std::cout << "Usage: sudo <command> [arguments]\n";
                std::cout << "Run a command with administrator privileges.\n";
                std::cout << "\nNote: Requires Windows 11 24H2+ with sudo enabled.\n";
                std::cout << "Run 'setup admin' to enable sudo on your system.\n";
                std::cout << "\nExamples:\n";
                std::cout << "  sudo notepad C:\\Windows\\System32\\drivers\\etc\\hosts\n";
                std::cout << "  sudo netsh wlan show profiles\n";
                std::cout << "  sudo ln -s source.txt link.txt\n";
            } else {
                // Check if the command is a Linuxify builtin
                std::string targetCmd = tokens[1];
                bool isBuiltin = isBuiltinCommand(targetCmd);
                
                // Also check if it's in our cmds folder
                char exePath[MAX_PATH];
                GetModuleFileNameA(NULL, exePath, MAX_PATH);
                fs::path cmdsDir = fs::path(exePath).parent_path() / "cmds";
                bool isInCmds = false;
                std::vector<std::string> exts = {".exe", ".cmd", ".bat", ""};
                for (const auto& ext : exts) {
                    if (fs::exists(cmdsDir / (targetCmd + ext))) {
                        isInCmds = true;
                        break;
                    }
                }
                
                std::string sudoCmd;
                if (isBuiltin || isInCmds) {
                    // For Linuxify commands, wrap with: sudo linuxify.exe -c "command args"
                    sudoCmd = "sudo \"" + std::string(exePath) + "\" -c \"";
                    for (size_t i = 1; i < tokens.size(); i++) {
                        if (i > 1) sudoCmd += " ";
                        // Escape quotes in arguments
                        std::string arg = tokens[i];
                        if (arg.find(' ') != std::string::npos || arg.find('"') != std::string::npos) {
                            // Simple escaping for nested quotes
                            sudoCmd += "'" + arg + "'";
                        } else {
                            sudoCmd += arg;
                        }
                    }
                    sudoCmd += "\"";
                } else {
                    // For system commands, pass directly to Windows sudo
                    sudoCmd = "sudo";
                    for (size_t i = 1; i < tokens.size(); i++) {
                        sudoCmd += " ";
                        if (tokens[i].find(' ') != std::string::npos) {
                            sudoCmd += "\"" + tokens[i] + "\"";
                        } else {
                            sudoCmd += tokens[i];
                        }
                    }
                }
                
                // Run the sudo command
                int result = runProcess(sudoCmd);
                if (result != 0) {
                    printError("sudo command failed. Make sure sudo is enabled:");
                    std::cout << "  Run 'setup admin' or enable in Settings > For Developers\n";
                }
            }
        } else if (cmd == "crontab") {
            // Crontab - edit scheduled tasks (communicates with crond daemon via IPC)
            auto sendToCrond = [](const char* command) -> std::string {
                HANDLE hPipe = CreateFileA(
                    CROND_PIPE_NAME,
                    GENERIC_READ | GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL
                );
                
                if (hPipe == INVALID_HANDLE_VALUE) {
                    return "";  // Daemon not running
                }
                
                DWORD bytesWritten;
                WriteFile(hPipe, command, (DWORD)strlen(command), &bytesWritten, NULL);
                
                char response[8192];
                DWORD bytesRead;
                std::string result;
                if (ReadFile(hPipe, response, sizeof(response) - 1, &bytesRead, NULL)) {
                    response[bytesRead] = '\0';
                    result = response;
                }
                
                CloseHandle(hPipe);
                return result;
            };
            
            auto getCrontabPath = []() -> std::string {
                char exePath[MAX_PATH];
                GetModuleFileNameA(NULL, exePath, MAX_PATH);
                return (fs::path(exePath).parent_path() / "linuxdb" / "crontab").string();
            };
            
            if (tokens.size() < 2) {
                std::cout << "Usage: crontab [-l | -e | -r]\n";
                std::cout << "  -l    List crontab entries\n";
                std::cout << "  -e    Edit crontab in nano\n";
                std::cout << "  -r    Remove all entries\n";
                std::cout << "\nCrontab format: min hour day month weekday command\n";
                std::cout << "Special: @reboot @hourly @daily @weekly @monthly @yearly\n";
                std::cout << "\nExample:\n";
                std::cout << "  */5 * * * * ping google.com\n";
                std::cout << "  @daily C:\\backup\\daily.bat\n";
                std::cout << "  @reboot echo System started\n";
            } else {
                std::string arg = tokens[1];
                std::string crontabPath = getCrontabPath();
                
                if (arg == "-l") {
                    // List crontab
                    std::ifstream file(crontabPath);
                    if (file) {
                        std::string line;
                        bool hasJobs = false;
                        while (std::getline(file, line)) {
                            // Skip comments but show for context
                            std::cout << line << "\n";
                            if (!line.empty() && line[0] != '#') {
                                hasJobs = true;
                            }
                        }
                        if (!hasJobs) {
                            std::cout << "\n(No active jobs)\n";
                        }
                    } else {
                        std::cout << "No crontab file. Use 'crontab -e' to create one.\n";
                    }
                    
                } else if (arg == "-e") {
                    // Edit crontab with nano
                    char exePath[MAX_PATH];
                    GetModuleFileNameA(NULL, exePath, MAX_PATH);
                    fs::path nanoPath = fs::path(exePath).parent_path() / "nano.exe";
                    
                    // Create crontab if it doesn't exist
                    if (!fs::exists(crontabPath)) {
                        std::ofstream file(crontabPath);
                        file << "# Linuxify Crontab - Edit with crontab -e\n";
                        file << "# Format: min hour day month weekday command\n";
                        file << "# Example: */5 * * * * ping google.com\n\n";
                    }
                    
                    if (fs::exists(nanoPath)) {
                        std::string cmdLine = "\"" + nanoPath.string() + "\" \"" + crontabPath + "\"";
                        runProcess(cmdLine);
                        
                        // Tell crond to reload
                        std::string response = sendToCrond("RELOAD");
                        if (response.empty()) {
                            std::cout << "Crontab saved. Note: crond is not running.\n";
                            std::cout << "Start it with: crond (or crond --install for auto-start)\n";
                        } else {
                            printSuccess("Crontab saved and reloaded.");
                        }
                    } else {
                        printError("nano not found. Edit manually: " + crontabPath);
                    }
                    
                } else if (arg == "-r") {
                    std::cout << "Remove all cron jobs? (y/n): ";
                    char c;
                    std::cin >> c;
                    std::cin.ignore(10000, '\n');
                    
                    if (c == 'y' || c == 'Y') {
                        std::ofstream file(crontabPath);
                        file << "# Linuxify Crontab - Empty\n";
                        sendToCrond("RELOAD");
                        printSuccess("All cron jobs removed.");
                    } else {
                        std::cout << "Cancelled.\n";
                    }
                    
                } else {
                    printError("Unknown option: " + arg);
                    std::cout << "Use: crontab -l | -e | -r\n";
                }
            }
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
        
        std::string cmdLine;
        
        // Check if it's a shell script (.sh file)
        std::string ext = fs::path(fullPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".sh") {
            // Parse shebang to determine interpreter - REQUIRED
            std::string interpreterPath;
            std::string interpreterSpec;  // Could be registry name or absolute path
            bool hasShebang = false;
            
            std::ifstream scriptFile(fullPath);
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
                printError("Script missing shebang line: " + path);
                printError("Add a shebang: #!<interpreter> (registry name or absolute path)");
                printError("Example: #!lish  or  #!C:\\path\\to\\interpreter.exe");
                return;
            }
            
            if (interpreterSpec.empty()) {
                printError("Invalid shebang - no interpreter specified");
                return;
            }
            
            // Normalize interpreter name for comparison
            std::string interpreterName = interpreterSpec;
            // Remove .exe if present
            if (interpreterName.size() > 4 && interpreterName.substr(interpreterName.size() - 4) == ".exe") {
                interpreterName = interpreterName.substr(0, interpreterName.size() - 4);
            }
            // Get basename (remove path)
            size_t lastSlash = interpreterName.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                interpreterName = interpreterName.substr(lastSlash + 1);
            }
            // Convert to lowercase for comparison
            std::transform(interpreterName.begin(), interpreterName.end(), interpreterName.begin(), ::tolower);
            
            // Check for internal interpreters (default, lish, bash, sh)
            // These should be handled by the internal interpreter, not looked up externally
            if (interpreterName == "default" || interpreterName == "lish" || 
                interpreterName == "bash" || interpreterName == "sh") {
                // Use the internal interpreter directly - run the script!
                runScript(fullPath);
                return;
            }
            
            // Check if it's an absolute path or a registry name
            fs::path specPath(interpreterSpec);
            if (specPath.is_absolute() && fs::exists(interpreterSpec)) {
                // Direct absolute path
                interpreterPath = interpreterSpec;
            } else {
                // Try to look up in registry
                std::string regPath = g_registry.getExecutablePath(interpreterSpec);
                if (!regPath.empty() && fs::exists(regPath)) {
                    interpreterPath = regPath;
                } else if (fs::exists(interpreterSpec)) {
                    // Relative path that exists
                    interpreterPath = fs::absolute(interpreterSpec).string();
                } else {
                    printError("Interpreter not found: " + interpreterSpec);
                    printError("Either add it to registry: registry add " + interpreterSpec + " <path>");
                    printError("Or use an absolute path in the shebang");
                    return;
                }
            }
            
            cmdLine = "\"" + interpreterPath + "\" \"" + fullPath + "\"";
            // Add any additional arguments after the script name
            for (size_t i = 1; i < args.size(); i++) {
                cmdLine += " \"" + args[i] + "\"";
            }
        } else {
            // Regular executable
            cmdLine = "\"" + fullPath + "\"";
            for (size_t i = 1; i < args.size(); i++) {
                cmdLine += " \"" + args[i] + "\"";
            }
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

        // Link interpreter to this shell
        interpreter.getExecutor().setFallbackHandler([this](const std::vector<std::string>& args) {
            this->executeCommand(args);
            return 0;
        });
    }



    // Check if a command is a built-in Linuxify command
    bool isBuiltinCommand(const std::string& cmd) {
        static std::vector<std::string> builtins = {
            "pwd", "cd", "ls", "dir", "mkdir", "rm", "rmdir", "mv", "cp", "copy", 
            "cat", "type", "touch", "chmod", "chown", "clear", "cls", "help", 
            "nano", "lin", "registry", "history", "whoami", "echo", "env", 
            "printenv", "export", "which", "ps", "kill", "top", "htop", "jobs", "fg",
            "grep", "head", "tail", "wc", "sort", "uniq", "find",
            // Text processing commands
            "less", "more", "cut", "tr", "sed", "awk", "diff", "tee", "xargs", "rev",
            // File operations commands
            "ln", "stat", "file", "readlink", "realpath", "basename", "dirname", "tree", "du",
            "lsmem", "free", "lscpu", "lshw", "sysinfo", "lsmount", "lsblk", "df",
            "lsusb", "lsnet", "lsof", "ip", "ping", "traceroute", "tracert",
            "nslookup", "dig", "host", "curl", "wget", "net", "netstat", "ifconfig", "ipconfig",
            // Toolchain commands
            "gcc", "g++", "cc", "c++", "make", "gdb", "ar", "ld", "objdump", "objcopy",
            "strip", "windres", "as", "nm", "ranlib", "size", "strings", "addr2line", "c++filt",
            // Admin commands
            "sudo", "setup", "uninstall",
            // Scheduler commands
            "crontab"
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

    // Parse and handle output redirection (>, >>, 2>, 2>&1, &>) and pipes (|)
    bool handleRedirection(const std::string& input) {
        // Check for stderr redirections first
        size_t stderrToStdout = input.find("2>&1");
        size_t stderrAppend = input.find("2>>");
        size_t stderrWrite = input.find("2>");
        size_t bothAppend = input.find("&>>");
        size_t bothWrite = input.find("&>");
        
        // Handle 2>&1 - redirect stderr to stdout (strip it and let external command handle it)
        std::string processedInput = input;
        bool mergeStderr = false;
        
        if (stderrToStdout != std::string::npos) {
            // Remove 2>&1 from input and set flag
            processedInput = input.substr(0, stderrToStdout);
            if (stderrToStdout + 4 < input.length()) {
                processedInput += input.substr(stderrToStdout + 4);
            }
            // Trim
            processedInput.erase(processedInput.find_last_not_of(" \t") + 1);
            mergeStderr = true;
        }
        
        // Handle &> or &>> (redirect both stdout and stderr to file)
        if (bothWrite != std::string::npos || bothAppend != std::string::npos) {
            bool append = (bothAppend != std::string::npos && (bothWrite == std::string::npos || bothAppend < bothWrite));
            size_t pos = append ? bothAppend : bothWrite;
            size_t skip = append ? 3 : 2;
            
            std::string cmdPart = input.substr(0, pos);
            std::string filePart = input.substr(pos + skip);
            
            cmdPart.erase(cmdPart.find_last_not_of(" \t") + 1);
            filePart.erase(0, filePart.find_first_not_of(" \t"));
            filePart.erase(filePart.find_last_not_of(" \t") + 1);
            
            if (filePart.empty()) {
                printError("Syntax error: missing filename after redirect");
                return true;
            }
            
            std::string outputFile = resolvePath(filePart);
            
            // Execute with stderr merged
            std::vector<std::string> tokens = tokenize(cmdPart);
            if (tokens.empty()) return true;
            
            // For external commands, use 2>&1
            std::string fullCmd = "cd /d \"" + currentDir + "\" && " + cmdPart + " 2>&1";
            FILE* pipe = _popen(fullCmd.c_str(), "r");
            std::string output;
            if (pipe) {
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    output += buffer;
                }
                _pclose(pipe);
            }
            
            std::ofstream file;
            if (append) {
                file.open(outputFile, std::ios::app);
            } else {
                file.open(outputFile, std::ios::out);
            }
            
            if (file) {
                file << output;
            } else {
                printError("Cannot open file: " + filePart);
            }
            
            return true;
        }
        
        // Handle 2> or 2>> (redirect only stderr to file) - but NOT if 2>&1 was detected
        if (stderrToStdout == std::string::npos && 
            ((stderrWrite != std::string::npos && stderrWrite != stderrAppend - 1) || stderrAppend != std::string::npos)) {
            bool append = (stderrAppend != std::string::npos);
            size_t pos = append ? stderrAppend : stderrWrite;
            size_t skip = append ? 3 : 2;
            
            std::string cmdPart = input.substr(0, pos);
            std::string filePart = input.substr(pos + skip);
            
            cmdPart.erase(cmdPart.find_last_not_of(" \t") + 1);
            filePart.erase(0, filePart.find_first_not_of(" \t"));
            filePart.erase(filePart.find_last_not_of(" \t") + 1);
            
            if (filePart.empty()) {
                printError("Syntax error: missing filename after redirect");
                return true;
            }
            
            std::string stderrFile = resolvePath(filePart);
            
            // Execute command, capture stderr separately
            std::string fullCmd = "cd /d \"" + currentDir + "\" && " + cmdPart + " 2>\"" + stderrFile + "\"";
            int result = system(fullCmd.c_str());
            (void)result;  // Suppress warning
            
            return true;
        }
        
        // Check for output redirection first (> or >>)
        size_t appendPos = processedInput.find(">>");
        size_t writePos = processedInput.find(">");
        size_t pipePos = processedInput.find("|");
        
        // Handle output redirection
        if (appendPos != std::string::npos || (writePos != std::string::npos && (appendPos == std::string::npos || writePos < appendPos))) {
            bool append = (appendPos != std::string::npos && (writePos == std::string::npos || appendPos <= writePos));
            size_t pos = append ? appendPos : writePos;
            size_t skip = append ? 2 : 1;
            
            std::string cmdPart = processedInput.substr(0, pos);
            std::string filePart = processedInput.substr(pos + skip);
            
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
            std::string output;
            if (mergeStderr) {
                // For merged stderr, use _popen with 2>&1
                std::string fullCmd = "cd /d \"" + currentDir + "\" && " + cmdPart + " 2>&1";
                FILE* pipe = _popen(fullCmd.c_str(), "r");
                if (pipe) {
                    char buffer[256];
                    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                        output += buffer;
                    }
                    _pclose(pipe);
                }
            } else {
                output = executeAndCapture(cmdPart);
            }
            
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
                    } else if (cmd == "cut") {
                        cmdCut(tokens, pipedOutput);
                    } else if (cmd == "tr") {
                        cmdTr(tokens, pipedOutput);
                    } else if (cmd == "sed") {
                        cmdSed(tokens, pipedOutput);
                    } else if (cmd == "awk") {
                        cmdAwk(tokens, pipedOutput);
                    } else if (cmd == "tee") {
                        cmdTee(tokens, pipedOutput);
                    } else if (cmd == "xargs") {
                        cmdXargs(tokens, pipedOutput);
                    } else if (cmd == "rev") {
                        cmdRev(tokens, pipedOutput);
                    } else if (cmd == "less" || cmd == "more") {
                        cmdLess(tokens, pipedOutput);
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
        
        clearScreen();
        
        // Print Tux penguin with colors (yellow body, white eyes)
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "         .---.         ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "  _     _                  _  __\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "        /     \\        ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << " | |   (_)_ __  _   ___  _(_)/ _|_   _\n";
        
        // Eyes line - split for white eyes
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "        \\.";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); // White
        std::cout << "@-@";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "./        ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << " | |   | | '_ \\| | | \\ \\/ / | |_| | | |\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "        /`\\_/`\\        ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << " | |___| | | | | |_| |>  <| |  _| |_| |\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "       //  _  \\\\       ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << " |_____|_|_| |_|\\__,_/_/\\_\\_|_|  \\__, |\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "      | \\     )|_      ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "                                 |___/\n";
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); // Yellow
        std::cout << "     /`\\_`>  <_/ \\     \n";
        std::cout << "     \\__/'---'\\__/     ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "                             By Cortez\n" << std::endl;
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  Linux Commands for Windows - Type 'help' for commands\n";
        std::cout << "  Licensed under GPLv3 - Free Software Foundation\n" << std::endl;

        // Silently start crond if not running (auto-setup cron on first run)
        {
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            fs::path crondPath = fs::path(exePath).parent_path() / "cmds" / "crond.exe";
            fs::path crontabPath = fs::path(exePath).parent_path() / "linuxdb" / "crontab";
            
            if (fs::exists(crondPath)) {
                // Check if crond is running via IPC
                HANDLE hPipe = CreateFileA(
                    CROND_PIPE_NAME,
                    GENERIC_READ | GENERIC_WRITE,
                    0, NULL, OPEN_EXISTING, 0, NULL
                );
                
                bool crondRunning = (hPipe != INVALID_HANDLE_VALUE);
                if (crondRunning) CloseHandle(hPipe);
                
                if (!crondRunning) {
                    // Ensure crontab exists
                    if (!fs::exists(crontabPath)) {
                        fs::create_directories(crontabPath.parent_path());
                        std::ofstream file(crontabPath);
                        file << "# Linuxify Crontab\n";
                        file << "# Format: min hour day month weekday command\n";
                        file << "# Edit with: crontab -e\n\n";
                    }
                    
                    // Start crond silently in background
                    STARTUPINFOA si;
                    PROCESS_INFORMATION pi;
                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    ZeroMemory(&pi, sizeof(pi));
                    
                    std::string cmdLine = "\"" + crondPath.string() + "\"";
                    char cmdBuffer[1024];
                    strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);
                    
                    if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, FALSE,
                                       CREATE_NO_WINDOW | DETACHED_PROCESS,
                                       NULL, NULL, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                    
                    // Register in system startup if not already (first run - needs admin)
                    HKEY hKey;
                    bool isRegistered = false;
                    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                      "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                        char val[MAX_PATH];
                        DWORD sz = sizeof(val);
                        isRegistered = RegQueryValueExA(hKey, "LinuxifyCrond", NULL, NULL, (LPBYTE)val, &sz) == ERROR_SUCCESS;
                        RegCloseKey(hKey);
                    }
                    
                    // Only try to register if not already done (installer should have done this)
                    // Silent fail is OK here - installer handles this with admin rights
                    if (!isRegistered) {
                        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                          "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                          0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                            std::string regValue = "\"" + crondPath.string() + "\"";
                            RegSetValueExA(hKey, "LinuxifyCrond", 0, REG_SZ,
                                           (const BYTE*)regValue.c_str(), (DWORD)(regValue.length() + 1));
                            RegCloseKey(hKey);
                        }
                    }
                }
            }
    }

        std::string input;
        
        while (running) {
            printPrompt();
            
            // Use syntax-highlighted input
            input = readInputWithHighlight();

            input.erase(0, input.find_first_not_of(" \t"));
            if (!input.empty()) {
                input.erase(input.find_last_not_of(" \t") + 1);
            }

            if (input.empty()) {
                continue;
            }

            // History expansion: !!, !n, !-n, !string
            if (input[0] == '!' && input.length() > 1) {
                std::string expandedInput;
                bool expanded = false;
                
                if (input == "!!") {
                    // !! - repeat last command
                    if (!commandHistory.empty()) {
                        expandedInput = commandHistory.back();
                        expanded = true;
                    } else {
                        printError("!!: event not found");
                        continue;
                    }
                } else if (input[1] == '-' && input.length() > 2) {
                    // !-n - run n-th previous command
                    try {
                        int n = std::stoi(input.substr(2));
                        if (n > 0 && n <= (int)commandHistory.size()) {
                            expandedInput = commandHistory[commandHistory.size() - n];
                            expanded = true;
                        } else {
                            printError("!" + input.substr(1) + ": event not found");
                            continue;
                        }
                    } catch (...) {
                        printError("!" + input.substr(1) + ": event not found");
                        continue;
                    }
                } else if (std::isdigit(input[1])) {
                    // !n - run command n from history
                    try {
                        int n = std::stoi(input.substr(1));
                        if (n > 0 && n <= (int)commandHistory.size()) {
                            expandedInput = commandHistory[n - 1];
                            expanded = true;
                        } else {
                            printError("!" + input.substr(1) + ": event not found");
                            continue;
                        }
                    } catch (...) {
                        printError("!" + input.substr(1) + ": event not found");
                        continue;
                    }
                } else {
                    // !string - run most recent command starting with string
                    std::string prefix = input.substr(1);
                    for (int i = (int)commandHistory.size() - 1; i >= 0; --i) {
                        if (commandHistory[i].substr(0, prefix.length()) == prefix) {
                            expandedInput = commandHistory[i];
                            expanded = true;
                            break;
                        }
                    }
                    if (!expanded) {
                        printError("!" + prefix + ": event not found");
                        continue;
                    }
                }
                
                if (expanded) {
                    // Show the expanded command
                    std::cout << expandedInput << std::endl;
                    input = expandedInput;
                }
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

    }




    int runScript(const std::string& filename, const std::vector<std::string>& args = {}) {
        std::ifstream file(filename);
        if (!file) {
            printError("Script not found: " + filename);
            return 1;
        }
        
        // Skip shebang if present
        std::string line;
        std::getline(file, line);
        if (line.size() > 2 && line[0] == '#' && line[1] == '!') {
            // Check for recursive lish execution
            std::string shebang = line.substr(2);
            if (shebang.find("lish") != std::string::npos) {
                printError("No Goofy shebangs allowed ;), thats basically inception. Sooooo...... Yeah");
                return 1;
            }
        } else {
            // Not a shebang, rewind
            file.clear();
            file.seekg(0);
        }
        
        // Set up script arguments ($0, $1, $2, etc.)
        std::vector<std::string> scriptArgs;
        scriptArgs.push_back(filename);  // $0 is script name
        for (const auto& arg : args) {
            scriptArgs.push_back(arg);
        }
        interpreter.setScriptArgs(scriptArgs);
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        int result = interpreter.runCode(buffer.str());
        
        interpreter.clearScriptArgs();
        return result;
    }
    
    // Run a single command string (e.g. from -c)
    int runCommand(const std::string& command) {
        return interpreter.runCode(command);
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

int main(int argc, char* argv[]) {
    // Set up Ctrl+C handler to prevent shell from exiting
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    
    Linuxify shell;
    
    // Handle -c flag: execute command and exit
    if (argc >= 3 && std::string(argv[1]) == "-c") {
        std::string command;
        for (int i = 2; i < argc; i++) {
            if (i > 2) command += " ";
            command += argv[i];
        }
        shell.runCommand(command);
        return 0;
    }
    
    // Handle script file execution
    if (argc >= 2 && argv[1][0] != '-') {
        // Collect script arguments
        std::vector<std::string> scriptArgs;
        for (int i = 2; i < argc; i++) {
            scriptArgs.push_back(argv[i]);
        }
        return shell.runScript(argv[1], scriptArgs);
    }
    
    shell.run();
    return 0;
}

