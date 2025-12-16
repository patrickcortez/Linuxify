// Compile: cl /EHsc /std:c++17 main.cpp registry.cpp /Fe:linuxify.exe
// Alternate compile: g++ -std=c++17 -static -o linuxify main.cpp registry.cpp -lpsapi -lws2_32 -liphlpapi -lwininet

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
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
#include <regex>
#include <deque>
#include <thread>
#include <chrono>
#include <list>

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
    std::map<std::string, std::string> sessionEnv;
    Bash::Interpreter interpreter;
    int lastExitCode = 0;

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
        bool recursive = false;
        bool humanReadable = false;
        bool reverse = false;
        bool timeSort = false;
        bool sizeSort = false;
        bool color = true;
        bool oneColumn = false;
        std::vector<std::string> paths;

        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-a" || arg == "--all") showAll = true;
            else if (arg == "-l") longFormat = true;
            else if (arg == "-R" || arg == "--recursive") recursive = true;
            else if (arg == "-h" || arg == "--human-readable") humanReadable = true;
            else if (arg == "-r" || arg == "--reverse") reverse = true;
            else if (arg == "-t") timeSort = true;
            else if (arg == "-S") sizeSort = true;
            else if (arg == "-1") oneColumn = true;
            else if (arg == "--color=never") color = false;
            else if (arg == "--color=auto" || arg == "--color=always") color = true;
            else if (arg.length() > 1 && arg[0] == '-') {
                for (size_t k = 1; k < arg.length(); ++k) {
                    if (arg[k] == 'a') showAll = true;
                    else if (arg[k] == 'l') longFormat = true;
                    else if (arg[k] == 'R') recursive = true;
                    else if (arg[k] == 'h') humanReadable = true;
                    else if (arg[k] == 'r') reverse = true;
                    else if (arg[k] == 't') timeSort = true;
                    else if (arg[k] == 'S') sizeSort = true;
                    else if (arg[k] == '1') oneColumn = true;
                }
            }
            else paths.push_back(arg);
        }

        if (paths.empty()) paths.push_back(currentDir);

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        int termWidth = csbi.dwSize.X;
        if (termWidth <= 0) termWidth = 80;

        auto formatSize = [&](uintmax_t size) -> std::string {
            if (!humanReadable) return std::to_string(size);
            const char* units[] = {"B", "K", "M", "G", "T"};
            int unit = 0;
            double s = static_cast<double>(size);
            while (s >= 1024 && unit < 4) {
                s /= 1024;
                unit++;
            }
            std::stringstream ss;
            ss << std::fixed << std::setprecision(1) << s << units[unit];
            return ss.str();
        };

        auto printEntryLong = [&](const fs::directory_entry& entry) {
             std::string perms;
             if (fs::is_directory(entry)) perms = "d";
             else if (fs::is_symlink(entry)) perms = "l";
             else perms = "-";
             
             try {
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
             } catch(...) { perms += "---------"; }

             uintmax_t size = 0;
             if (!fs::is_directory(entry)) {
                 try { size = fs::file_size(entry); } catch(...) {}
             }
             
             char timeBuf[64] = "Unknown";
             try {
                 auto ftime = fs::last_write_time(entry);
                 auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                     ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                 std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                 std::tm* tm = std::localtime(&cftime);
                 if (tm) std::strftime(timeBuf, sizeof(timeBuf), "%b %d %H:%M", tm);
             } catch(...) {}

             std::cout << perms << " " << std::setw(humanReadable ? 6 : 10) << formatSize(size) << " " << timeBuf << " ";

             if (color) {
                 if (fs::is_directory(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                 else if (fs::is_symlink(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                 else {
                     std::string ext = entry.path().extension().string();
                     std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                     if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".sh") 
                         SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                     else if (ext == ".zip" || ext == ".tar" || ext == ".gz")
                         SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                     else if (ext == ".jpg" || ext == ".png" || ext == ".bmp")
                         SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                 }
             }
             
             std::cout << entry.path().filename().string();
             if (color) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
             std::cout << std::endl;
        };

        auto printEntriesColumnar = [&](const std::vector<fs::directory_entry>& entries) {
            if (entries.empty()) return;

            size_t maxLen = 0;
            for (const auto& entry : entries) {
                size_t len = entry.path().filename().string().length();
                if (len > maxLen) maxLen = len;
            }
            
            int colWidth = (int)maxLen + 2;
            if (colWidth < 1) colWidth = 1;
            int numCols = termWidth / colWidth;
            if (numCols < 1) numCols = 1;

            int col = 0;
            for (const auto& entry : entries) {
                std::string name = entry.path().filename().string();
                
                if (color) {
                    if (fs::is_directory(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    else if (fs::is_symlink(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    else {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".sh") 
                            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                        else if (ext == ".zip" || ext == ".tar" || ext == ".gz")
                            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                        else if (ext == ".jpg" || ext == ".png" || ext == ".bmp")
                            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                        else
                            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    }
                }
                
                std::cout << name;
                if (color) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                
                col++;
                if (col >= numCols) {
                    std::cout << std::endl;
                    col = 0;
                } else {
                    int padding = colWidth - (int)name.length();
                    for (int i = 0; i < padding; i++) std::cout << ' ';
                }
            }
            if (col != 0) std::cout << std::endl;
        };

        auto listDir = [&](auto&& self, const std::string& p) -> void {
             if (!fs::exists(p)) {
                 printError("ls: cannot access '" + p + "': No such file or directory");
                 return;
             }
             if (!fs::is_directory(p)) {
                 fs::directory_entry entry(p);
                 if (longFormat) {
                     printEntryLong(entry);
                 } else {
                     std::vector<fs::directory_entry> single = {entry};
                     printEntriesColumnar(single);
                 }
                 return;
             }
             
             if (recursive && paths.size() > 1) std::cout << p << ":" << std::endl;
             
             std::vector<fs::directory_entry> entries;
             try {
                 for (const auto& entry : fs::directory_iterator(p)) {
                     std::string name = entry.path().filename().string();
                     if (!showAll && name[0] == '.') continue;
                     entries.push_back(entry);
                 }
             } catch (const std::exception& e) {
                 printError("ls: " + std::string(e.what()));
                 return;
             }
             
             std::sort(entries.begin(), entries.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
                 if (timeSort) return fs::last_write_time(a) > fs::last_write_time(b);
                 if (sizeSort) {
                     uintmax_t sa = 0, sb = 0;
                     if (!fs::is_directory(a)) sa = fs::file_size(a);
                     if (!fs::is_directory(b)) sb = fs::file_size(b);
                     return sa > sb;
                 }
                 return a.path().filename().string() < b.path().filename().string();
             });
             
             if (reverse) std::reverse(entries.begin(), entries.end());
             
             if (longFormat) {
                 for (const auto& entry : entries) {
                     printEntryLong(entry);
                 }
             } else if (oneColumn) {
                 for (const auto& entry : entries) {
                     if (color) {
                         if (fs::is_directory(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                         else if (fs::is_symlink(entry)) SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                         else {
                             std::string ext = entry.path().extension().string();
                             std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                             if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".sh") 
                                 SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                             else if (ext == ".zip" || ext == ".tar" || ext == ".gz")
                                 SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                             else if (ext == ".jpg" || ext == ".png" || ext == ".bmp")
                                 SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                             else
                                 SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                         }
                     }
                     std::cout << entry.path().filename().string();
                     if (color) SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                     std::cout << std::endl;
                 }
             } else {
                 printEntriesColumnar(entries);
             }
             
             if (recursive) {
                 for (const auto& entry : entries) {
                     if (fs::is_directory(entry)) {
                         std::string name = entry.path().filename().string();
                         if (name != "." && name != "..") {
                             std::cout << std::endl << entry.path().string() << ":" << std::endl;
                             self(self, entry.path().string());
                         }
                     }
                 }
             }
        };

        for (const auto& path : paths) {
             listDir(listDir, resolvePath(path));
        }
    }

    void cmdMkdir(const std::vector<std::string>& args) {
        bool parents = false;
        bool verbose = false;
        std::vector<std::string> dirs;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-p" || args[i] == "--parents") parents = true;
            else if (args[i] == "-v" || args[i] == "--verbose") verbose = true;
            else if (args[i][0] != '-') dirs.push_back(args[i]);
        }
        
        if (dirs.empty()) {
            printError("mkdir: missing operand");
            return;
        }

        for (const auto& dir : dirs) {
            try {
                std::string fullPath = resolvePath(dir);
                bool created = false;
                if (parents) created = fs::create_directories(fullPath);
                else created = fs::create_directory(fullPath);
                
                if (verbose && created) std::cout << "mkdir: created directory '" << dir << "'" << std::endl;
            } catch (const std::exception& e) {
                printError("mkdir: cannot create directory '" + dir + "': " + e.what());
            }
        }
    }

    void cmdRm(const std::vector<std::string>& args) {
        bool recursive = false;
        bool force = false; 
        bool interactive = false; 
        bool verbose = false; 
        std::vector<std::string> targets;

        for (size_t i = 1; i < args.size(); ++i) {
             std::string arg = args[i];
             if (arg == "-r" || arg == "-R" || arg == "--recursive") recursive = true;
             else if (arg == "-f" || arg == "--force") force = true;
             else if (arg == "-i" || arg == "--interactive") interactive = true;
             else if (arg == "-v" || arg == "--verbose") verbose = true;
             else if (arg.length() > 1 && arg[0] == '-') {
                  for (size_t k = 1; k < arg.length(); ++k) {
                       if (arg[k] == 'r' || arg[k] == 'R') recursive = true;
                       else if (arg[k] == 'f') force = true;
                       else if (arg[k] == 'i') interactive = true;
                       else if (arg[k] == 'v') verbose = true;
                  }
             }
             else targets.push_back(args[i]);
        }
        
        if (targets.empty()) {
             printError("rm: missing operand");
             return;
        }

        for (const auto& target : targets) {
             std::string fullPath = resolvePath(target);
             if (!fs::exists(fullPath)) {
                 if (!force) printError("rm: cannot remove '" + target + "': No such file or directory");
                 continue;
             }
             
             if (interactive) {
                 std::cout << "rm: remove " << (fs::is_directory(fullPath) ? "directory" : "regular file") << " '" << target << "'? ";
                 std::string answer;
                 std::getline(std::cin, answer);
                 if (answer.empty() || (answer[0] != 'y' && answer[0] != 'Y')) continue;
             }

             try {
                 if (fs::is_directory(fullPath)) {
                     if (!recursive) {
                         printError("rm: cannot remove '" + target + "': Is a directory");
                         continue;
                     }
                     fs::remove_all(fullPath);
                     if (verbose) std::cout << "removed directory '" << target << "'" << std::endl;
                 } else {
                     fs::remove(fullPath);
                     if (verbose) std::cout << "removed '" << target << "'" << std::endl;
                 }
             } catch (const std::exception& e) {
                 if (!force) printError("rm: cannot remove '" + target + "': " + e.what());
             }
        }
    }

    void cmdMv(const std::vector<std::string>& args) {
         bool interactive = false;
         bool noClobber = false; 
         bool update = false;
         bool verbose = false;
         std::vector<std::string> operands;
         
         for (size_t i = 1; i < args.size(); ++i) {
             std::string arg = args[i];
             if (arg == "-i" || arg == "--interactive") interactive = true;
             else if (arg == "-n" || arg == "--no-clobber") noClobber = true;
             else if (arg == "-u" || arg == "--update") update = true;
             else if (arg == "-v" || arg == "--verbose") verbose = true;
             else if (arg[0] != '-') operands.push_back(arg);
         }
         
         if (operands.size() < 2) {
             printError("mv: missing operand");
             return;
         }
         
         std::string destPath = resolvePath(operands.back());
         bool destIsDir = fs::exists(destPath) && fs::is_directory(destPath);
         
         if (operands.size() > 2 && !destIsDir) {
              printError("mv: target '" + operands.back() + "' is not a directory");
              return;
         }
         
         for (size_t i = 0; i < operands.size() - 1; ++i) {
              std::string sourcePath = resolvePath(operands[i]);
              if (!fs::exists(sourcePath)) {
                  printError("mv: cannot stat '" + operands[i] + "': No such file or directory");
                  continue;
              }
              
              std::string actualDest = destPath;
              if (destIsDir) {
                  actualDest = (fs::path(destPath) / fs::path(sourcePath).filename()).string();
              }
              
              if (fs::exists(actualDest)) {
                   if (noClobber) continue;
                   if (update) {
                       try {
                           if (fs::last_write_time(sourcePath) <= fs::last_write_time(actualDest)) continue;
                       } catch(...) {}
                   }
                   if (interactive) {
                       std::cout << "mv: overwrite '" << actualDest << "'? ";
                       std::string ans;
                       std::getline(std::cin, ans);
                       if (ans.empty() || (ans[0] != 'y' && ans[0] != 'Y')) continue;
                   }
                   fs::remove_all(actualDest); 
              }
              
              try {
                  fs::rename(sourcePath, actualDest);
                  if (verbose) std::cout << "renamed '" << operands[i] << "' -> '" << actualDest << "'" << std::endl;
              } catch (const std::exception& e) {
                  printError("mv: cannot move '" + operands[i] + "' to '" + actualDest + "': " + e.what());
              }
         }
    }

    void cmdCp(const std::vector<std::string>& args) {
         bool recursive = false;
         bool interactive = false;
         bool noClobber = false;
         bool update = false;
         bool verbose = false;
         std::vector<std::string> operands;
         
         for (size_t i = 1; i < args.size(); ++i) {
             std::string arg = args[i];
             if (arg == "-r" || arg == "-R" || arg == "--recursive") recursive = true;
             else if (arg == "-i" || arg == "--interactive") interactive = true;
             else if (arg == "-n" || arg == "--no-clobber") noClobber = true;
             else if (arg == "-u" || arg == "--update") update = true;
             else if (arg == "-v" || arg == "--verbose") verbose = true;
             else if (arg.length() > 1 && arg[0] == '-') {
                  for (size_t k = 1; k < arg.length(); ++k) {
                       if (arg[k] == 'r' || arg[k] == 'R') recursive = true;
                       else if (arg[k] == 'i') interactive = true;
                       else if (arg[k] == 'n') noClobber = true;
                       else if (arg[k] == 'u') update = true;
                       else if (arg[k] == 'v') verbose = true;
                  }
             }
             else operands.push_back(arg);
         }
         
         if (operands.size() < 2) {
             printError("cp: missing operand");
             return;
         }
         
         std::string destPath = resolvePath(operands.back());
         bool destIsDir = fs::exists(destPath) && fs::is_directory(destPath);
         
         if (operands.size() > 2 && !destIsDir) {
              printError("cp: target '" + operands.back() + "' is not a directory");
              return;
         }
         
         for (size_t i = 0; i < operands.size() - 1; ++i) {
              std::string sourcePath = resolvePath(operands[i]);
              if (!fs::exists(sourcePath)) {
                   printError("cp: cannot stat '" + operands[i] + "': No such file or directory");
                   continue;
              }
              
              if (fs::is_directory(sourcePath) && !recursive) {
                   printError("cp: -r not specified; omitting directory '" + operands[i] + "'");
                   continue;
              }
              
              std::string actualDest = destPath;
              if (destIsDir) {
                   actualDest = (fs::path(destPath) / fs::path(sourcePath).filename()).string();
              }
              
              if (fs::exists(actualDest)) {
                   if (noClobber) continue;
                   if (update) {
                        try {
                             if (fs::last_write_time(sourcePath) <= fs::last_write_time(actualDest)) continue;
                        } catch(...) {}
                   }
                   if (interactive) {
                        std::cout << "cp: overwrite '" << actualDest << "'? ";
                        std::string ans;
                        std::getline(std::cin, ans);
                        if (ans.empty() || (ans[0] != 'y' && ans[0] != 'Y')) continue;
                   }
              }
              
              try {
                   auto options = fs::copy_options::overwrite_existing;
                   if (recursive) options |= fs::copy_options::recursive;
                   
                   if (fs::is_directory(sourcePath)) {
                       fs::copy(sourcePath, actualDest, options);
                   } else {
                       fs::copy_file(sourcePath, actualDest, options);
                   }
                   if (verbose) std::cout << "'" << operands[i] << "' -> '" << actualDest << "'" << std::endl;
              } catch (const std::exception& e) {
                   printError("cp: cannot copy: " + std::string(e.what()));
              }
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

        constexpr size_t BUFFER_SIZE = 65536; 
        std::vector<char> buffer(BUFFER_SIZE);

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

                std::ifstream ifs(fullPath, std::ios::binary);
                if (!ifs) {
                    printError("cat: " + file + ": Cannot open file");
                    continue;
                }

                if (!showNumbers) {
                    while (ifs.read(buffer.data(), BUFFER_SIZE) || ifs.gcount() > 0) {
                        std::cout.write(buffer.data(), ifs.gcount());
                        if (!ifs) break; 
                    }
                    std::cout.flush();
                } else {
                    long long lineNum = 1;
                    bool newLine = true; 

                    while (ifs.read(buffer.data(), BUFFER_SIZE) || ifs.gcount() > 0) {
                        std::streamsize count = ifs.gcount();
                        for (std::streamsize i = 0; i < count; ++i) {
                            if (newLine) {
                                std::cout << std::setw(6) << lineNum << "  ";
                                lineNum++;
                                newLine = false;
                            }
                            char c = buffer[i];
                            std::cout.put(c);
                            if (c == '\n') {
                                newLine = true;
                            }
                        }
                        if (!ifs) break;
                    }
                    if (newLine && lineNum > 1) { 
                    } else if (!newLine) {
                        std::cout << std::endl; 
                    }
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
        bool noCreate = false;
        bool updateAccess = false;
        bool updateMod = false;
        std::string refFile;
        std::string dateStr;
        std::vector<std::string> files;

        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-c" || arg == "--no-create") noCreate = true;
            else if (arg == "-a") updateAccess = true;
            else if (arg == "-m") updateMod = true;
            else if (arg == "-r" && i + 1 < args.size()) refFile = args[++i];
            else if (arg == "-t" && i + 1 < args.size()) dateStr = args[++i];
            else if (arg[0] != '-') files.push_back(arg);
        }

        if (!updateAccess && !updateMod) {
            updateAccess = updateMod = true;
        }

        if (files.empty()) {
            printError("touch: missing file operand");
            return;
        }

        FILETIME ft = {0, 0};
        SYSTEMTIME st = {0};
        GetSystemTime(&st);
        
        if (!dateStr.empty()) {
            int year = st.wYear;
            int month = st.wMonth;
            int day = st.wDay;
            int hour = st.wHour;
            int min = st.wMinute;
            int sec = 0;
            
            size_t dotPos = dateStr.find('.');
            if (dotPos != std::string::npos) {
                if (dotPos + 1 < dateStr.length()) {
                    try { sec = std::stoi(dateStr.substr(dotPos + 1)); } catch(...) {}
                }
                dateStr = dateStr.substr(0, dotPos);
            }
            
            bool valid = true;
            try {
                if (dateStr.length() == 8) {
                    month = std::stoi(dateStr.substr(0, 2));
                    day = std::stoi(dateStr.substr(2, 2));
                    hour = std::stoi(dateStr.substr(4, 2));
                    min = std::stoi(dateStr.substr(6, 2));
                } else if (dateStr.length() == 10) {
                    int yy = std::stoi(dateStr.substr(0, 2));
                    year = (yy < 69) ? (2000 + yy) : (1900 + yy); 
                    month = std::stoi(dateStr.substr(2, 2));
                    day = std::stoi(dateStr.substr(4, 2));
                    hour = std::stoi(dateStr.substr(6, 2));
                    min = std::stoi(dateStr.substr(8, 2));
                } else if (dateStr.length() == 12) {
                    year = std::stoi(dateStr.substr(0, 4));
                    month = std::stoi(dateStr.substr(4, 2));
                    day = std::stoi(dateStr.substr(6, 2));
                    hour = std::stoi(dateStr.substr(8, 2));
                    min = std::stoi(dateStr.substr(10, 2));
                } else {
                    valid = false;
                }
            } catch(...) { valid = false; }
            
            if (valid) {
                st.wYear = year;
                st.wMonth = month;
                st.wDay = day;
                st.wHour = hour;
                st.wMinute = min;
                st.wSecond = sec;
                st.wMilliseconds = 0;
                SystemTimeToFileTime(&st, &ft);
            } else {
                printError("touch: invalid date format '" + dateStr + "'");
                return;
            }
        } else if (!refFile.empty()) {
            HANDLE hRef = CreateFileA(resolvePath(refFile).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hRef != INVALID_HANDLE_VALUE) {
                GetFileTime(hRef, NULL, NULL, &ft);
                CloseHandle(hRef);
            } else {
                printError("touch: failed to get attributes of '" + refFile + "'");
                return;
            }
        } else {
            SystemTimeToFileTime(&st, &ft);
        }

        for (const auto& file : files) {
            std::string fullPath = resolvePath(file);
            
            if (!fs::exists(fullPath)) {
                if (noCreate) continue;
                HANDLE h = CreateFileA(fullPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    CloseHandle(h);
                } else {
                    printError("touch: cannot touch '" + file + "': " + std::to_string(GetLastError()));
                    continue;
                }
            }

            HANDLE hFile = CreateFileA(fullPath.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                FILETIME* pAccess = updateAccess ? &ft : NULL;
                FILETIME* pWrite = updateMod ? &ft : NULL;
                if (!SetFileTime(hFile, NULL, pAccess, pWrite)) {
                     printError("touch: setting times of '" + file + "': " + std::to_string(GetLastError()));
                }
                CloseHandle(hFile);
            } else {
                printError("touch: cannot touch '" + file + "'");
            }
        }
    }

    void cmdChmod(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("chmod: missing operand");
            std::cout << "Usage: chmod [-R] <mode> <file>..." << std::endl;
            return;
        }

        bool recursive = false;
        bool verbose = false;
        std::string mode;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-R" || arg == "--recursive") recursive = true;
            else if (arg == "-v" || arg == "--verbose") verbose = true;
            else if (mode.empty() && arg[0] != '-') mode = arg;
            else if (arg[0] != '-') files.push_back(arg);
        }

        if (mode.empty() || files.empty()) {
             printError("chmod: missing mode or file operand");
             return;
        }

        auto applyMode = [&](const std::string& path) {
             DWORD attrs = GetFileAttributesA(path.c_str());
             if (attrs == INVALID_FILE_ATTRIBUTES) return;
             
             DWORD newAttrs = attrs;
             
             if (isdigit(mode[0])) {
                 int m = mode[0] - '0';
                 if ((m & 2) == 0) newAttrs |= FILE_ATTRIBUTE_READONLY;
                 else newAttrs &= ~FILE_ATTRIBUTE_READONLY;
             } else {
                 bool add = false;
                 bool remove = false;
                 
                 for (char c : mode) {
                     if (c == '+') { add = true; remove = false; }
                     else if (c == '-') { add = false; remove = true; }
                     else if (c == 'w') {
                         if (add) newAttrs &= ~FILE_ATTRIBUTE_READONLY;
                         if (remove) newAttrs |= FILE_ATTRIBUTE_READONLY;
                     }
                     else if (c == 'h') {
                         if (add) newAttrs |= FILE_ATTRIBUTE_HIDDEN;
                         if (remove) newAttrs &= ~FILE_ATTRIBUTE_HIDDEN;
                     }
                 }
             }
             
             if (newAttrs != attrs) {
                 if (SetFileAttributesA(path.c_str(), newAttrs)) {
                     if (verbose) std::cout << "mode of '" << path << "' changed" << std::endl;
                 } else {
                     printError("chmod: changing permissions of '" + path + "': failed");
                 }
             }
        };

        for (const auto& file : files) {
             std::string root = resolvePath(file);
             if (!fs::exists(root)) {
                 printError("chmod: cannot access '" + file + "': No such file or directory");
                 continue;
             }
             
             applyMode(root);
             
             if (recursive && fs::is_directory(root)) {
                 try {
                     for (const auto& entry : fs::recursive_directory_iterator(root)) {
                         applyMode(entry.path().string());
                     }
                 } catch(...) {}
             }
        }
    }

    void cmdChown(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("chown: missing operand");
            std::cout << "Usage: chown [-R] <owner> <file>..." << std::endl;
            return;
        }

        bool recursive = false;
        bool verbose = false;
        std::string owner;
        std::vector<std::string> files;

        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-R" || arg == "--recursive") recursive = true;
            else if (arg == "-v" || arg == "--verbose") verbose = true;
            else if (owner.empty() && arg[0] != '-') owner = arg;
            else if (arg[0] != '-') files.push_back(arg);
        }

        if (owner.empty() || files.empty()) {
             printError("chown: missing owner or file operand");
             return;
        }

        size_t colon = owner.find(':');
        if (colon != std::string::npos) {
            owner = owner.substr(0, colon);
        }

        for (const auto& file : files) {
             std::string root = resolvePath(file);
             if (!fs::exists(root)) {
                 printError("chown: cannot access '" + file + "': No such file or directory");
                 continue;
             }
             
             std::string cmd = "cmd /c icacls \"" + root + "\" /setowner " + owner + (recursive ? " /T" : "") + " /C /Q >nul 2>&1";
             int res = runProcess(cmd);
             
             if (res == 0) {
                 if (verbose) std::cout << "ownership of '" + file + "' retained as " + owner << std::endl;
             } else {
                 printError("chown: changing ownership of '" + file + "': Operation not permitted (or user invalid)");
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

    void cmdEcho(const std::vector<std::string>& args) {
        bool newline = true;
        bool interpretEscapes = false;
        size_t start = 1;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-n") { newline = false; start = i + 1; }
            else if (args[i] == "-e") { interpretEscapes = true; start = i + 1; }
            else if (args[i] == "-E") { interpretEscapes = false; start = i + 1; }
            else if (args[i] == "-ne" || args[i] == "-en") { newline = false; interpretEscapes = true; start = i + 1; }
            else break;
        }
        
        auto processEscapes = [](const std::string& input) -> std::string {
            std::string result;
            for (size_t i = 0; i < input.length(); ++i) {
                if (input[i] == '\\' && i + 1 < input.length()) {
                    char next = input[i + 1];
                    if (next == 'n') { result += '\n'; i++; }
                    else if (next == 't') { result += '\t'; i++; }
                    else if (next == 'r') { result += '\r'; i++; }
                    else if (next == 'a') { result += '\a'; i++; }
                    else if (next == 'b') { result += '\b'; i++; }
                    else if (next == 'v') { result += '\v'; i++; }
                    else if (next == 'f') { result += '\f'; i++; }
                    else if (next == '\\') { result += '\\'; i++; }
                    else if (next == '0') {
                        int val = 0;
                        size_t j = i + 2;
                        while (j < input.length() && j < i + 5 && input[j] >= '0' && input[j] <= '7') {
                            val = val * 8 + (input[j] - '0');
                            j++;
                        }
                        result += (char)val;
                        i = j - 1;
                    }
                    else if (next == 'x') {
                        int val = 0;
                        size_t j = i + 2;
                        while (j < input.length() && j < i + 4) {
                            char c = input[j];
                            if (c >= '0' && c <= '9') val = val * 16 + (c - '0');
                            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                            else break;
                            j++;
                        }
                        result += (char)val;
                        i = j - 1;
                    }
                    else if (next == 'e' || next == 'E') { result += '\033'; i++; }
                    else if (next == 'c') { return result; }
                    else { result += input[i]; }
                } else {
                    result += input[i];
                }
            }
            return result;
        };
        
        for (size_t i = start; i < args.size(); ++i) {
            std::string text = args[i];
            
            size_t pos = 0;
            while ((pos = text.find('$', pos)) != std::string::npos) {
                size_t end = pos + 1;
                
                if (end < text.length() && text[end] == '{') {
                    size_t close = text.find('}', end + 1);
                    if (close != std::string::npos) {
                        std::string varName = text.substr(end + 1, close - end - 1);
                        std::string value;
                        auto it = sessionEnv.find(varName);
                        if (it != sessionEnv.end()) {
                            value = it->second;
                        } else {
                            char* envVal = nullptr;
                            size_t len;
                            _dupenv_s(&envVal, &len, varName.c_str());
                            if (envVal) { value = envVal; free(envVal); }
                        }
                        text = text.substr(0, pos) + value + text.substr(close + 1);
                        continue;
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
                        if (envVal) { value = envVal; free(envVal); }
                    }
                    text = text.substr(0, pos) + value + text.substr(end);
                    continue;
                }
                pos++;
            }
            
            if (interpretEscapes) {
                text = processEscapes(text);
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
            "chmod", "chown", "clear", "help", "lino", "lin", "registry",
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
        std::string removePathCmd = "cmd /c powershell -Command \"$path = [Environment]::GetEnvironmentVariable('PATH', 'User'); $newPath = ($path -split ';' | Where-Object { $_ -notlike '*Linuxify*' }) -join ';'; [Environment]::SetEnvironmentVariable('PATH', $newPath, 'User')\" 2>nul";
        runProcess(removePathCmd);
        
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
            std::string startCmd = "cmd /c start \"\" cmd /c \"" + batchFile + "\"";
            runProcess(startCmd, "", false);
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

    void cmdPs(const std::vector<std::string>& args) {
        bool allProcesses = false;
        bool fullFormat = false;
        bool extendedFormat = false;
        std::string filterUser;
        DWORD filterPid = 0;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-e" || args[i] == "-A" || args[i] == "aux" || args[i] == "-aux") {
                allProcesses = true;
                fullFormat = true;
            } else if (args[i] == "-f" || args[i] == "--full") {
                fullFormat = true;
            } else if (args[i] == "-l" || args[i] == "--long") {
                extendedFormat = true;
            } else if ((args[i] == "-u" || args[i] == "-U") && i + 1 < args.size()) {
                filterUser = args[++i];
                allProcesses = true;
            } else if (args[i] == "-p" && i + 1 < args.size()) {
                try { filterPid = std::stoul(args[++i]); } catch (...) {}
            }
        }

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) {
            printError("ps: failed to enumerate processes");
            return;
        }

        struct ProcInfo {
            DWORD pid;
            DWORD ppid;
            std::string name;
            size_t memoryKB;
            DWORD threads;
        };
        std::vector<ProcInfo> procs;

        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                if (filterPid != 0 && pe.th32ProcessID != filterPid) continue;
                
                ProcInfo info;
                info.pid = pe.th32ProcessID;
                info.ppid = pe.th32ParentProcessID;
                info.name = pe.szExeFile;
                info.threads = pe.cntThreads;
                info.memoryKB = 0;

                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        info.memoryKB = pmc.WorkingSetSize / 1024;
                    }
                    CloseHandle(hProc);
                }
                
                procs.push_back(info);
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);

        if (extendedFormat || fullFormat) {
            std::cout << std::left << std::setw(8) << "PID" 
                      << std::setw(8) << "PPID"
                      << std::setw(8) << "THR"
                      << std::setw(12) << "RSS(KB)"
                      << "COMMAND" << std::endl;
            
            for (const auto& p : procs) {
                std::cout << std::left << std::setw(8) << p.pid
                          << std::setw(8) << p.ppid
                          << std::setw(8) << p.threads
                          << std::setw(12) << p.memoryKB
                          << p.name << std::endl;
            }
        } else {
            std::cout << std::left << std::setw(8) << "PID" << "COMMAND" << std::endl;
            for (const auto& p : procs) {
                if (!allProcesses && p.pid == 0) continue;
                std::cout << std::left << std::setw(8) << p.pid << p.name << std::endl;
            }
        }
    }

    void cmdKill(const std::vector<std::string>& args) {
        static const std::map<std::string, int> signalMap = {
            {"SIGHUP", 1}, {"HUP", 1}, {"1", 1},
            {"SIGINT", 2}, {"INT", 2}, {"2", 2},
            {"SIGQUIT", 3}, {"QUIT", 3}, {"3", 3},
            {"SIGKILL", 9}, {"KILL", 9}, {"9", 9},
            {"SIGTERM", 15}, {"TERM", 15}, {"15", 15},
            {"SIGSTOP", 17}, {"STOP", 17}, {"17", 17},
            {"SIGCONT", 19}, {"CONT", 19}, {"19", 19}
        };

        if (args.size() >= 2 && (args[1] == "-l" || args[1] == "--list")) {
            std::cout << " 1) SIGHUP     2) SIGINT     3) SIGQUIT    9) SIGKILL\n";
            std::cout << "15) SIGTERM   17) SIGSTOP   19) SIGCONT\n";
            return;
        }

        if (args.size() < 2) {
            printError("kill: missing PID");
            return;
        }

        int signal = 15;
        std::vector<std::string> targets;

        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-s" && i + 1 < args.size()) {
                std::string sigName = args[++i];
                std::transform(sigName.begin(), sigName.end(), sigName.begin(), ::toupper);
                auto it = signalMap.find(sigName);
                if (it != signalMap.end()) {
                    signal = it->second;
                } else {
                    try { signal = std::stoi(sigName); } catch (...) {
                        printError("kill: invalid signal: " + sigName);
                        return;
                    }
                }
            } else if (args[i][0] == '-' && args[i].length() > 1 && std::isdigit(args[i][1])) {
                try { signal = std::stoi(args[i].substr(1)); } catch (...) {}
            } else if (args[i][0] == '-' && args[i].length() > 1 && std::isalpha(args[i][1])) {
                std::string sigName = args[i].substr(1);
                std::transform(sigName.begin(), sigName.end(), sigName.begin(), ::toupper);
                auto it = signalMap.find(sigName);
                if (it != signalMap.end()) signal = it->second;
            } else {
                targets.push_back(args[i]);
            }
        }

        for (const auto& target : targets) {
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
                try {
                    DWORD pid = std::stoul(target);
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                    if (hProc) {
                        if (TerminateProcess(hProc, signal)) {
                            printSuccess("Process " + target + " killed with signal " + std::to_string(signal));
                        } else {
                            printError("kill: failed to terminate process " + target);
                        }
                        CloseHandle(hProc);
                    } else {
                        printError("kill: (" + target + ") - No such process or access denied");
                    }
                } catch (...) {
                    printError("kill: invalid PID: " + target);
                }
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

    // grep - search for pattern in file or input using buffered reading and regex
    void cmdGrep(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (args.size() < 2) {
            printError("grep: missing pattern");
            std::cout << "Usage: grep [OPTIONS] PATTERN [FILE...]" << std::endl;
            return;
        }

        struct GrepOptions {
             bool ignoreCase = false;
             bool lineNumbers = false;
             bool invertMatch = false;
             bool countOnly = false;
             bool recursive = false;
             bool useRegex = false;
             bool showFilename = false; // Implicit if multiple files
             int context = 0;
        } opts;

        std::string pattern;
        std::vector<std::string> files;
        size_t argIdx = 1;

        // Argument Parsing
        for (; argIdx < args.size(); ++argIdx) {
            std::string arg = args[argIdx];
            if (arg[0] != '-') {
                 if (pattern.empty()) pattern = arg;
                 else files.push_back(arg);
            } else {
                 if (arg == "-i" || arg == "--ignore-case") opts.ignoreCase = true;
                 else if (arg == "-n" || arg == "--line-number") opts.lineNumbers = true;
                 else if (arg == "-v" || arg == "--invert-match") opts.invertMatch = true;
                 else if (arg == "-c" || arg == "--count") opts.countOnly = true;
                 else if (arg == "-r" || arg == "-R" || arg == "--recursive") opts.recursive = true;
                 else if (arg == "-E" || arg == "--extended-regexp") opts.useRegex = true;
                 else if (arg == "-h" || arg == "--no-filename") opts.showFilename = false;
                 else if (arg == "-H" || arg == "--with-filename") opts.showFilename = true;
                 else if (arg.rfind("-C", 0) == 0 && arg.length() > 2) opts.context = std::stoi(arg.substr(2));
                 else if (arg == "-C" && argIdx + 1 < args.size()) opts.context = std::stoi(args[++argIdx]);
                 else if (pattern.empty()) pattern = arg; // Handle negative pattern?? No, usually flags first
            }
        }
        
        if (pattern.empty()) {
             printError("grep: missing pattern");
             return;
        }

        if (files.empty() && opts.recursive) files.push_back(".");
        
        bool multipleFiles = files.size() > 1 || opts.recursive;
        if (opts.recursive) multipleFiles = true;
        int totalMatches = 0;

        std::regex regexPattern;
        try {
            if (opts.useRegex) {
                regexPattern = std::regex(pattern, opts.ignoreCase ? std::regex::icase : std::regex::ECMAScript);
            }
        } catch (...) {
            printError("grep: invalid regular expression");
            return;
        }

        auto performGrep = [&](std::istream& is, const std::string& filename) {
             std::string line;
             int lineNum = 0;
             int matches = 0;
             std::deque<std::string> contextBuffer; // For leading context
             int contextCountdown = 0; // For trailing context

             while (std::getline(is, line)) {
                 lineNum++;
                 bool found = false;
                 std::smatch sm;

                 if (opts.useRegex) {
                     found = std::regex_search(line, sm, regexPattern);
                 } else {
                     std::string searchLine = line;
                     std::string searchPat = pattern;
                     if (opts.ignoreCase) {
                         std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(), ::tolower);
                         std::transform(searchPat.begin(), searchPat.end(), searchPat.begin(), ::tolower);
                     }
                     found = searchLine.find(searchPat) != std::string::npos;
                 }

                 if (opts.invertMatch) found = !found;

                 if (found) {
                     matches++;
                     if (opts.countOnly) continue;
                     
                     // Print Leading Context
                     if (opts.context > 0 && !contextBuffer.empty()) {
                          int cLine = lineNum - (int)contextBuffer.size();
                          for (const auto& cL : contextBuffer) {
                              if (multipleFiles || opts.showFilename) std::cout << filename << "-";
                              if (opts.lineNumbers) std::cout << cLine << "-";
                              std::cout << cL << std::endl;
                              cLine++;
                          }
                          contextBuffer.clear();
                     }

                     // Print Match
                     if (multipleFiles || opts.showFilename) {
                         SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                         std::cout << filename << ":";
                         SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                     }
                     if (opts.lineNumbers) {
                         SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                         std::cout << lineNum << ":";
                         SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                     }

                     // Highlight Pattern if not inverted
                     if (!opts.invertMatch && !opts.useRegex) {
                         // Basic highlight
                         size_t pos = 0;
                         std::string temp = line;
                         std::string searchTemp = temp;
                         if (opts.ignoreCase) std::transform(searchTemp.begin(), searchTemp.end(), searchTemp.begin(), ::tolower);
                         std::string searchPat = pattern;
                         if (opts.ignoreCase) std::transform(searchPat.begin(), searchPat.end(), searchPat.begin(), ::tolower);

                         while ((pos = searchTemp.find(searchPat)) != std::string::npos) {
                             std::cout << temp.substr(0, pos);
                             SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
                             std::cout << temp.substr(pos, pattern.length());
                             SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                             temp = temp.substr(pos + pattern.length());
                             searchTemp = searchTemp.substr(pos + searchPat.length());
                         }
                         std::cout << temp << std::endl;
                     } else {
                         std::cout << line << std::endl;
                     }
                     
                     contextCountdown = opts.context;
                 } else {
                     // Not found
                     if (contextCountdown > 0 && !opts.countOnly) {
                         if (multipleFiles || opts.showFilename) std::cout << filename << "-";
                         if (opts.lineNumbers) std::cout << lineNum << "-";
                         std::cout << line << std::endl;
                         contextCountdown--;
                     } else if (opts.context > 0) {
                         contextBuffer.push_back(line);
                         if (contextBuffer.size() > (size_t)opts.context) contextBuffer.pop_front();
                     }
                 }
             }

             if (opts.countOnly) {
                 if (multipleFiles || opts.showFilename) std::cout << filename << ":";
                 std::cout << matches << std::endl;
             }
             return matches;
        };

        if (files.empty()) {
            std::istringstream iss(pipedInput);
            totalMatches += performGrep(iss, "(standard input)");
        } else {
            for (const auto& file : files) {
                if (opts.recursive && fs::is_directory(file)) {
                     try {
                         for (const auto& entry : fs::recursive_directory_iterator(file)) {
                             if (entry.is_regular_file()) {
                                 std::ifstream ifs(entry.path());
                                 if (ifs) totalMatches += performGrep(ifs, entry.path().string());
                             }
                         }
                     } catch (...) {}
                } else {
                     std::ifstream ifs(resolvePath(file));
                     if (!ifs) {
                         if (!opts.recursive) printError("grep: " + file + ": No such file or directory");
                         lastExitCode = 2;
                     } else {
                         totalMatches += performGrep(ifs, file);
                     }
                }
            }
        }
        
        lastExitCode = (totalMatches > 0) ? 0 : 1;
    }

    // head - output first part of files
    void cmdHead(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        long long count = 10;
        bool useBytes = false;
        bool quiet = false;
        bool verbose = false;
        std::vector<std::string> files;

        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-n" && i + 1 < args.size()) count = std::stoll(args[++i]);
            else if (arg == "-c" && i + 1 < args.size()) { count = std::stoll(args[++i]); useBytes = true; }
            else if (arg == "-q" || arg == "--quiet" || arg == "--silent") quiet = true;
            else if (arg == "-v" || arg == "--verbose") verbose = true;
            else if (arg[0] == '-' && isdigit(arg[1])) count = std::abs(std::stoll(arg)); // handle -5
            else files.push_back(arg);
        }

        auto process = [&](std::istream& is, const std::string& name, bool showHeader) {
            if (showHeader) {
                std::cout << "==> " << name << " <==\n";
            }
            if (useBytes) {
                char buf[4096];
                long long remaining = count;
                while (remaining > 0 && is) {
                    long long toRead = (std::min)((long long)sizeof(buf), remaining);
                    is.read(buf, toRead);
                    std::cout.write(buf, is.gcount());
                    remaining -= is.gcount();
                }
            } else {
                std::string line;
                long long remaining = count;
                while (remaining > 0 && std::getline(is, line)) {
                    std::cout << line << "\n";
                    remaining--;
                }
            }
            if (showHeader) std::cout << "\n";
        };

        if (files.empty() && !pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            process(iss, "", false);
        } else if (files.empty()) {
             printError("head: missing file operand");
        } else {
             bool showHeader = (files.size() > 1 && !quiet) || verbose;
             for (const auto& file : files) {
                 std::ifstream ifs(resolvePath(file), std::ios::binary);
                 if (!ifs) {
                     printError("head: cannot open '" + file + "'");
                     continue;
                 }
                 process(ifs, file, showHeader);
                 showHeader = (files.size() > 1 && !quiet); // Show separator for subsequent
             }
        }
    }

    // tail - output last part of files
    void cmdTail(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        long long count = 10;
        bool useBytes = false;
        bool follow = false;
        bool quiet = false;
        bool verbose = false;
        std::vector<std::string> files;
        int sleepInterval = 1000; // ms

        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-n" && i + 1 < args.size()) count = std::stoll(args[++i]);
            else if (arg == "-c" && i + 1 < args.size()) { count = std::stoll(args[++i]); useBytes = true; }
            else if (arg == "-f" || arg == "--follow") follow = true;
            else if (arg == "-q") quiet = true;
            else if (arg == "-v") verbose = true;
            else if (arg[0] == '-' && isdigit(arg[1])) count = std::abs(std::stoll(arg)); 
            else if (arg == "-s" && i + 1 < args.size()) sleepInterval = std::stoi(args[++i]) * 1000;
            else files.push_back(arg);
        }

        auto tailFile = [&](const std::string& path, bool showHeader) {
            std::ifstream file(path, std::ios::binary);
             if (!file) {
                 printError("tail: cannot open '" + path + "'");
                 return;
             }
             if (showHeader) std::cout << "==> " << path << " <==\n";

             if (useBytes) {
                 // Seek to end - count
                 file.seekg(0, std::ios::end);
                 long long fileSize = file.tellg();
                 long long startPos = (std::max)(0LL, fileSize - count);
                 file.seekg(startPos);
                 std::cout << file.rdbuf();
             } else {
                 // Efficient backwards reading for lines
                 file.seekg(0, std::ios::end);
                 long long fileSize = file.tellg();
                 if (fileSize == 0) return;

                 long long pos = fileSize;
                 long long linesFound = 0;
                 // Optimization: Reading from end
                 // Note: This is simpler than full buffer management but effective
                 // We step back 4KB at a time
                 const int CHUNK = 4096;
                 char buffer[CHUNK];
                 
                 // If file is small, just read it all
                 if (fileSize < CHUNK * 2 && count > 100) {
                     file.seekg(0);
                     // Fallback to forward read for small files/huge N
                     std::vector<std::string> buf;
                     std::string l;
                     while(std::getline(file, l)) {
                         buf.push_back(l);
                         if (buf.size() > count) buf.erase(buf.begin());
                     }
                     for(const auto& s : buf) std::cout << s << "\n";
                     return;
                 }

                 while (pos > 0 && linesFound <= count) {
                      long long toRead = (std::min)((long long)CHUNK, pos);
                      pos -= toRead;
                      file.seekg(pos);
                      file.read(buffer, toRead);
                      for (long long k = toRead - 1; k >= 0; --k) {
                          if (buffer[k] == '\n') {
                              linesFound++;
                              if (linesFound > count) {
                                  pos += k + 1; // Point to char after newline
                                  goto found_start;
                              }
                          }
                      }
                 }
                 pos = 0; // Read from start if we didn't find N newlines
                 
                 found_start:
                 file.seekg(pos);
                 // Dump from pos
                 std::cout << file.rdbuf(); 
                 // Note: rdbuf dumping might lose the last newline if not present? usually ok
             }
             if (showHeader) std::cout << "\n";

             if (follow) {
                 file.clear(); // Clear EOF
                 std::streampos lastPos = file.tellg();
                 while (running) {
                     // Check for new data
                     // In real implementation we should use generic filesystem watcher
                     // Polling here:
                     std::this_thread::sleep_for(std::chrono::milliseconds(sleepInterval));
                     fs::path p(path);
                     if (fs::exists(p) && fs::file_size(p) > lastPos) {
                         file.seekg(lastPos);
                         std::string line;
                         while (std::getline(file, line)) { // Or read block
                             std::cout << line << std::endl;
                         }
                         if (!file.eof()) {
                             // Block read might leave partial line? std::getline handles up to delim
                         }
                         file.clear();
                         lastPos = file.tellg();
                     } else if (fs::file_size(p) < lastPos) {
                         std::cerr << "tail: " << path << ": file truncated" << std::endl;
                         lastPos = 0;
                         file.seekg(0);
                     }
                 }
             }
        };

        if (files.empty() && !pipedInput.empty()) {
            // Cannot seek on pipe, must use buffer
            std::istringstream iss(pipedInput);
            std::deque<std::string> ring;
            if (useBytes) {
                // ... byte ring buffer is harder, just dump last N chars? 
                // String supports it
                if (pipedInput.size() > count) 
                    std::cout << pipedInput.substr(pipedInput.size() - count);
                else 
                    std::cout << pipedInput;
            } else {
                std::string line;
                while (std::getline(iss, line)) {
                    ring.push_back(line);
                    if (ring.size() > count) ring.pop_front();
                }
                for (const auto& l : ring) std::cout << l << "\n";
            }
        } else if (!files.empty()) {
            bool showHeader = (files.size() > 1 && !quiet) || verbose;
            for (const auto& f : files) {
                tailFile(resolvePath(f), showHeader);
                showHeader = (files.size() > 1);
            }
        } else {
             printError("tail: missing file operand");
        }
    }

    // wc - print newline, word, and byte counts
    void cmdWc(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool lines = false, words = false, chars = false, bytes = false;
        bool maxLine = false;
        std::vector<std::string> files;

        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-l" || arg == "--lines") lines = true;
            else if (arg == "-w" || arg == "--words") words = true;
            else if (arg == "-m" || arg == "--chars") chars = true;
            else if (arg == "-c" || arg == "--bytes") bytes = true;
            else if (arg == "-L" || arg == "--max-line-length") maxLine = true;
            else files.push_back(arg);
        }

        if (!lines && !words && !chars && !bytes && !maxLine) {
            lines = words = bytes = true;
        }

        auto countFile = [&](std::istream& is, const std::string& name) {
             long long l = 0, w = 0, c = 0, b = 0, L = 0;
             long long currentL = 0;
             bool inWord = false;
             char buf[8192];
             
             // Optimized stream reading
             while (is.read(buf, sizeof(buf)) || is.gcount() > 0) {
                 size_t n = is.gcount();
                 b += n;

                 for (size_t i = 0; i < n; ++i) {
                     unsigned char ch = (unsigned char)buf[i];
                     if (ch == '\n') {
                         l++;
                         if (currentL > L) L = currentL;
                         currentL = 0;
                     } else {
                         // UTF-8 char counting logic
                         if (chars) {
                              // If using specific char count, skip continuation bytes
                              if ((ch & 0xC0) != 0x80) c++;
                         } else {
                              c++; 
                         }
                         if ((ch & 0xC0) != 0x80) currentL++;
                     }

                     if (isspace(ch)) {
                         inWord = false;
                     } else if (!inWord) {
                         inWord = true;
                         w++;
                     }
                 }
                 if (is.eof()) break;
             }
             if (currentL > L) L = currentL;

             if (lines) std::cout << std::setw(4) << l << " ";
             if (words) std::cout << std::setw(4) << w << " ";
             if (bytes) std::cout << std::setw(4) << b << " ";
             if (chars) std::cout << std::setw(4) << c << " ";
             if (maxLine) std::cout << std::setw(4) << L << " ";
             if (!name.empty()) std::cout << name;
             std::cout << std::endl;
             return std::make_tuple(l, w, b, c, L);
        };

        if (files.empty()) {
             std::istringstream iss(pipedInput);
             countFile(iss, "");
        } else {
             long long tl=0, tw=0, tb=0, tc=0, tL=0;
             for (const auto& file : files) {
                  std::ifstream ifs(resolvePath(file), std::ios::binary);
                  if (!ifs) {
                      printError("wc: " + file + ": No such file or directory");
                      continue;
                  }
                  auto [l, w, b, c, L] = countFile(ifs, file);
                  tl += l; tw += w; tb += b; tc += c; tL = (std::max)(tL, L);
             }
             if (files.size() > 1) {
                 if (lines) std::cout << std::setw(4) << tl << " ";
                 if (words) std::cout << std::setw(4) << tw << " ";
                 if (bytes) std::cout << std::setw(4) << tb << " ";
                 if (chars) std::cout << std::setw(4) << tc << " ";
                 if (maxLine) std::cout << std::setw(4) << tL << " ";
                 std::cout << "total" << std::endl;
             }
        }
    }

    // sort - sort lines of text files
    void cmdSort(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool reverse = false;
        bool numeric = false;
        bool unique = false;
        bool ignoreCase = false;
        bool check = false;
        int keyStart = 0; 
        int keyEnd = 0;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-r" || arg == "--reverse") reverse = true;
            else if (arg == "-n" || arg == "--numeric-sort") numeric = true;
            else if (arg == "-u" || arg == "--unique") unique = true;
            else if (arg == "-f" || arg == "--ignore-case") ignoreCase = true;
            else if (arg == "-c" || arg == "--check") check = true;
            else if (arg == "-k" && i + 1 < args.size()) {
                std::string kdef = args[++i];
                size_t comma = kdef.find(',');
                if (comma != std::string::npos) {
                    keyStart = std::stoi(kdef.substr(0, comma));
                    keyEnd = std::stoi(kdef.substr(comma + 1));
                } else {
                    keyStart = std::stoi(kdef);
                    keyEnd = 0; 
                }
            }
            else if (arg[0] != '-') files.push_back(arg);
        }
        
        std::vector<std::string> lines;
        
        auto readLines = [&](std::istream& is) {
            std::string line;
            while (std::getline(is, line)) {
                lines.push_back(line);
            }
        };

        if (files.empty() && !pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            readLines(iss);
        } else if (files.empty()) {
             printError("sort: missing file operand");
             return;
        } else {
             for (const auto& file : files) {
                 std::ifstream ifs(resolvePath(file));
                 if (!ifs) {
                     printError("sort: cannot open '" + file + "'");
                     return;
                 }
                 readLines(ifs);
             }
        }

        auto extractKey = [&](const std::string& s) {
            if (keyStart == 0) return s; 
            
            std::istringstream iss(s);
            std::string token;
            std::string keyStr;
            int col = 1;
            while (iss >> token) {
                if (col >= keyStart) {
                    if (!keyStr.empty()) keyStr += " ";
                    keyStr += token;
                }
                if (keyEnd > 0 && col >= keyEnd) break;
                col++;
            }
            return keyStr;
        };

        auto compare = [&](const std::string& a, const std::string& b) {
             std::string ka = extractKey(a);
             std::string kb = extractKey(b);

             if (numeric) {
                 try {
                     double da = std::stod(ka);
                     double db = std::stod(kb);
                     return da < db;
                 } catch (...) {
                     return ka < kb;
                 }
             }
             
             if (ignoreCase) {
                 std::transform(ka.begin(), ka.end(), ka.begin(), ::tolower);
                 std::transform(kb.begin(), kb.end(), kb.begin(), ::tolower);
             }
             
             return ka < kb;
        };

        if (check) {
            for (size_t i = 1; i < lines.size(); ++i) {
                bool ordered = !reverse ? !compare(lines[i], lines[i-1]) : !compare(lines[i-1], lines[i]);
                if (reverse) {
                    if (compare(lines[i-1], lines[i])) { 
                        std::cout << "sort: disorder: " << lines[i] << std::endl;
                        return;
                    }
                } else {
                     if (compare(lines[i], lines[i-1])) {
                         std::cout << "sort: disorder: " << lines[i] << std::endl;
                         return;
                     }
                }
            }
            return;
        }

        std::sort(lines.begin(), lines.end(), compare);

        if (reverse) {
            std::reverse(lines.begin(), lines.end());
        }
        
        if (unique) {
            auto last = std::unique(lines.begin(), lines.end(), [&](const std::string& a, const std::string& b){
                 std::string ka = extractKey(a);
                 std::string kb = extractKey(b);
                 if (ignoreCase) {
                      std::transform(ka.begin(), ka.end(), ka.begin(), ::tolower);
                      std::transform(kb.begin(), kb.end(), kb.begin(), ::tolower);
                 }
                 return ka == kb;
            });
            lines.erase(last, lines.end());
        }
        
        for (const auto& line : lines) {
            std::cout << line << std::endl;
        }
    }

    // uniq - report or omit repeated lines
    void cmdUniq(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool countDupes = false;
        bool onlyDupes = false; 
        bool onlyUnique = false; 
        bool ignoreCase = false;
        int skipFields = 0;
        int skipChars = 0;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-c" || arg == "--count") countDupes = true;
            else if (arg == "-d" || arg == "--repeated") onlyDupes = true;
            else if (arg == "-u" || arg == "--unique") onlyUnique = true;
            else if (arg == "-i" || arg == "--ignore-case") ignoreCase = true;
            else if (arg.rfind("-f", 0) == 0) skipFields = std::stoi(arg.length() > 2 ? arg.substr(2) : args[++i]);
            else if (arg.rfind("-s", 0) == 0) skipChars = std::stoi(arg.length() > 2 ? arg.substr(2) : args[++i]);
            else if (arg[0] != '-') files.push_back(arg);
        }
        
        std::unique_ptr<std::istream> inputPtr;
        std::ifstream fileStream;
        
        if (!pipedInput.empty()) {
             inputPtr = std::make_unique<std::istringstream>(pipedInput);
        } else if (!files.empty()) {
             fileStream.open(resolvePath(files[0])); 
             if (!fileStream) {
                 printError("uniq: cannot open '" + files[0] + "'");
                 return;
             }
        } else {
             printError("uniq: missing file operand");
             return;
        }
        
        std::istream& is = (!pipedInput.empty()) ? (std::istream&)*inputPtr : fileStream;
        
        std::ostream* os = &std::cout;
        std::ofstream outStream;
        if (files.size() > 1) {
             outStream.open(resolvePath(files[1]));
             if (outStream) os = &outStream;
        }

        std::string prevLine;
        std::string currentLine;
        int count = 0;
        bool first = true;

        auto linesMatch = [&](const std::string& a, const std::string& b) {
            std::string sa = a;
            std::string sb = b;
            
            if (skipFields > 0) {
                 int skipped = 0;
                 size_t posA = 0, posB = 0;
                 while (skipped < skipFields) {
                     while (posA < sa.length() && !isspace((unsigned char)sa[posA])) posA++;
                     while (posA < sa.length() && isspace((unsigned char)sa[posA])) posA++;
                     while (posB < sb.length() && !isspace((unsigned char)sb[posB])) posB++;
                     while (posB < sb.length() && isspace((unsigned char)sb[posB])) posB++;
                     skipped++;
                 }
                 if (posA < sa.length()) sa = sa.substr(posA); else sa = "";
                 if (posB < sb.length()) sb = sb.substr(posB); else sb = "";
            }
            
            if (skipChars > 0 && sa.length() > (size_t)skipChars) sa = sa.substr(skipChars);
            if (skipChars > 0 && sb.length() > (size_t)skipChars) sb = sb.substr(skipChars);
            
            if (ignoreCase) {
                std::transform(sa.begin(), sa.end(), sa.begin(), ::tolower);
                std::transform(sb.begin(), sb.end(), sb.begin(), ::tolower);
            }
            return sa == sb;
        };

        auto flush = [&](const std::string& line, int cnt) {
             if (cnt == 0) return;
             bool print = true;
             if (onlyDupes && cnt == 1) print = false;
             if (onlyUnique && cnt > 1) print = false;
             
             if (print) {
                 if (countDupes) *os << std::setw(7) << cnt << " ";
                 *os << line << std::endl;
             }
        };

        while (std::getline(is, currentLine)) {
            if (first) {
                prevLine = currentLine;
                count = 1;
                first = false;
                continue;
            }
            
            if (linesMatch(prevLine, currentLine)) {
                count++;
            } else {
                flush(prevLine, count);
                prevLine = currentLine;
                count = 1;
            }
        }
        if (!first) flush(prevLine, count);
    }

    // find - search for files in a directory hierarchy
    void cmdFind(const std::vector<std::string>& args) {
        std::vector<std::string> paths;
        std::string namePattern;
        std::string typeFilter; 
        long long sizeMin = -1, sizeMax = -1;
        int maxDepth = -1;
        bool exec = false;
        std::vector<std::string> execCommand;

        size_t i = 1;
        while (i < args.size() && args[i][0] != '-') {
            paths.push_back(args[i++]);
        }
        if (paths.empty()) paths.push_back(".");

        for (; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "-name" && i + 1 < args.size()) namePattern = args[++i];
            else if (arg == "-type" && i + 1 < args.size()) typeFilter = args[++i];
            else if (arg == "-size" && i + 1 < args.size()) {
                std::string s = args[++i];
                bool gt = (s[0] == '+');
                bool lt = (s[0] == '-');
                if (gt || lt) s = s.substr(1);
                long long val = 0;
                try { 
                     size_t suffixPos = 0;
                     val = std::stoll(s, &suffixPos);
                     if (suffixPos < s.length()) {
                         char suf = s[suffixPos];
                         if (suf == 'k' || suf == 'K') val *= 1024;
                         else if (suf == 'M') val *= 1024*1024;
                         else if (suf == 'G') val *= 1024*1024*1024;
                     }
                } catch(...) {}
                
                if (gt) sizeMin = val + 1;
                else if (lt) sizeMax = val - 1;
                else { sizeMin = val; sizeMax = val; } 
            }
            else if (arg == "-maxdepth" && i + 1 < args.size()) maxDepth = std::stoi(args[++i]);
            else if (arg == "-exec") {
                exec = true;
                i++;
                while (i < args.size()) {
                    if (args[i] == ";") break;
                    execCommand.push_back(args[i]);
                    i++;
                }
            }
        }

        for (const auto& path : paths) {
            std::string root = resolvePath(path);
            if (!fs::exists(root)) {
                printError("find: '" + path + "': No such file or directory");
                continue;
            }

            try {
                auto walker = [&](auto&& self, const fs::path& p, int depth) -> void {
                    if (maxDepth != -1 && depth > maxDepth) return;
                    
                    bool match = true;
                    std::string filename = p.filename().string();
                    
                    if (!namePattern.empty()) {
                         if (namePattern.front() == '*' && namePattern.back() == '*') {
                             std::string sub = namePattern.substr(1, namePattern.length()-2);
                             if (filename.find(sub) == std::string::npos) match = false;
                         } else if (namePattern.front() == '*') {
                             std::string suf = namePattern.substr(1);
                             if (filename.length() < suf.length() || filename.substr(filename.length()-suf.length()) != suf) match=false;
                         } else if (namePattern.back() == '*') {
                             std::string pre = namePattern.substr(0, namePattern.length()-1);
                             if (filename.substr(0, pre.length()) != pre) match=false;
                         } else {
                             if (filename != namePattern) match = false;
                         }
                    }

                    if (match && !typeFilter.empty()) {
                        if (typeFilter == "f" && !fs::is_regular_file(p)) match = false;
                        else if (typeFilter == "d" && !fs::is_directory(p)) match = false;
                    }

                    if (match && (sizeMin != -1 || sizeMax != -1)) {
                         if (fs::is_regular_file(p)) {
                             uintmax_t sz = fs::file_size(p);
                             if (sizeMin != -1 && sz < (uintmax_t)sizeMin) match = false;
                             if (sizeMax != -1 && sz > (uintmax_t)sizeMax) match = false;
                         } else {
                             match = false;
                         }
                    }

                    if (match) {
                        if (exec) {
                            std::string cmd;
                            for (const auto& part : execCommand) {
                                if (part == "{}") cmd += "\"" + p.string() + "\" ";
                                else cmd += part + " ";
                            }
                            runProcess("cmd /c " + cmd);
                        } else {
                            std::cout << p.string() << std::endl;
                        }
                    }

                    if (fs::is_directory(p)) {
                        for (const auto& entry : fs::directory_iterator(p)) {
                             self(self, entry.path(), depth + 1);
                        }
                    }
                };
                
                walker(walker, root, 0);

            } catch (const std::exception& e) {
                printError("find: " + std::string(e.what()));
            }
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

    void cmdCut(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        char delimiter = '\t';
        std::string outputDelimiter;
        bool outputDelimiterSet = false;
        std::vector<std::pair<int, int>> ranges;
        std::vector<std::string> files;
        bool byByte = false;
        bool byChar = false;
        bool byField = false;
        bool complement = false;
        bool onlyDelimited = false;
        
        auto parseRange = [&](const std::string& spec) {
            std::istringstream ss(spec);
            std::string part;
            while (std::getline(ss, part, ',')) {
                size_t dashPos = part.find('-');
                if (dashPos == std::string::npos) {
                    int val = std::stoi(part);
                    ranges.push_back({val, val});
                } else if (dashPos == 0) {
                    int end = std::stoi(part.substr(1));
                    ranges.push_back({1, end});
                } else if (dashPos == part.length() - 1) {
                    int start = std::stoi(part.substr(0, dashPos));
                    ranges.push_back({start, INT_MAX});
                } else {
                    int start = std::stoi(part.substr(0, dashPos));
                    int end = std::stoi(part.substr(dashPos + 1));
                    ranges.push_back({start, end});
                }
            }
        };
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-d" && i + 1 < args.size()) {
                std::string delim = args[++i];
                if (!delim.empty()) delimiter = delim[0];
            } else if (args[i].substr(0, 2) == "-d" && args[i].length() > 2) {
                delimiter = args[i][2];
            } else if (args[i] == "-f" && i + 1 < args.size()) {
                byField = true;
                parseRange(args[++i]);
            } else if (args[i].substr(0, 2) == "-f" && args[i].length() > 2) {
                byField = true;
                parseRange(args[i].substr(2));
            } else if (args[i] == "-c" && i + 1 < args.size()) {
                byChar = true;
                parseRange(args[++i]);
            } else if (args[i].substr(0, 2) == "-c" && args[i].length() > 2) {
                byChar = true;
                parseRange(args[i].substr(2));
            } else if (args[i] == "-b" && i + 1 < args.size()) {
                byByte = true;
                parseRange(args[++i]);
            } else if (args[i].substr(0, 2) == "-b" && args[i].length() > 2) {
                byByte = true;
                parseRange(args[i].substr(2));
            } else if (args[i] == "--complement") {
                complement = true;
            } else if (args[i] == "-s" || args[i] == "--only-delimited") {
                onlyDelimited = true;
            } else if (args[i] == "--output-delimiter" && i + 1 < args.size()) {
                outputDelimiter = args[++i];
                outputDelimiterSet = true;
            } else if (args[i].substr(0, 19) == "--output-delimiter=") {
                outputDelimiter = args[i].substr(19);
                outputDelimiterSet = true;
            } else if (args[i][0] != '-') {
                files.push_back(args[i]);
            }
        }
        
        if (ranges.empty()) {
            printError("cut: you must specify a list of bytes, characters, or fields");
            return;
        }

        if (!outputDelimiterSet) {
            outputDelimiter = std::string(1, delimiter);
        }

        std::sort(ranges.begin(), ranges.end());

        auto isInRange = [&](int pos) -> bool {
            for (const auto& r : ranges) {
                if (pos >= r.first && pos <= r.second) return !complement;
            }
            return complement;
        };

        auto processLine = [&](const std::string& line) {
            if (byByte || byChar) {
                std::string result;
                bool first = true;
                for (int pos = 1; pos <= (int)line.length(); ++pos) {
                    if (isInRange(pos)) {
                        if (!first && outputDelimiterSet) result += outputDelimiter;
                        result += line[pos - 1];
                        first = false;
                    }
                }
                std::cout << result << "\n";
            } else {
                if (onlyDelimited && line.find(delimiter) == std::string::npos) {
                    return;
                }
                
                std::vector<std::string> tokens;
                size_t start = 0, end;
                while ((end = line.find(delimiter, start)) != std::string::npos) {
                    tokens.push_back(line.substr(start, end - start));
                    start = end + 1;
                }
                tokens.push_back(line.substr(start));
                
                std::string result;
                bool first = true;
                for (int pos = 1; pos <= (int)tokens.size(); ++pos) {
                    if (isInRange(pos)) {
                        if (!first) result += outputDelimiter;
                        result += tokens[pos - 1];
                        first = false;
                    }
                }
                std::cout << result << "\n";
            }
        };

        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                processLine(line);
            }
        } else if (!files.empty()) {
            for (const auto& filePath : files) {
                std::ifstream file(resolvePath(filePath));
                if (!file) {
                    printError("cut: cannot open '" + filePath + "'");
                    continue;
                }
                std::string line;
                while (std::getline(file, line)) {
                    processLine(line);
                }
            }
        } else {
            std::string line;
            while (std::getline(std::cin, line)) {
                processLine(line);
            }
        }
    }

    void cmdTr(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        bool deleteMode = false;
        bool squeezeMode = false;
        bool complementMode = false;
        std::string set1, set2;
        
        size_t argIdx = 1;
        while (argIdx < args.size() && args[argIdx][0] == '-' && args[argIdx].length() > 1) {
            for (size_t k = 1; k < args[argIdx].length(); ++k) {
                char flag = args[argIdx][k];
                if (flag == 'd') deleteMode = true;
                else if (flag == 's') squeezeMode = true;
                else if (flag == 'c' || flag == 'C') complementMode = true;
            }
            argIdx++;
        }
        
        if (argIdx < args.size()) set1 = args[argIdx++];
        if (argIdx < args.size()) set2 = args[argIdx++];
        
        if (set1.empty()) {
            printError("tr: missing operand");
            return;
        }

        auto expandClass = [](const std::string& className) -> std::string {
            std::string result;
            if (className == "alpha" || className == "ALPHA") {
                for (char c = 'a'; c <= 'z'; ++c) result += c;
                for (char c = 'A'; c <= 'Z'; ++c) result += c;
            } else if (className == "digit" || className == "DIGIT") {
                for (char c = '0'; c <= '9'; ++c) result += c;
            } else if (className == "upper" || className == "UPPER") {
                for (char c = 'A'; c <= 'Z'; ++c) result += c;
            } else if (className == "lower" || className == "LOWER") {
                for (char c = 'a'; c <= 'z'; ++c) result += c;
            } else if (className == "alnum" || className == "ALNUM") {
                for (char c = 'a'; c <= 'z'; ++c) result += c;
                for (char c = 'A'; c <= 'Z'; ++c) result += c;
                for (char c = '0'; c <= '9'; ++c) result += c;
            } else if (className == "space" || className == "SPACE") {
                result = " \t\n\r\v\f";
            } else if (className == "blank" || className == "BLANK") {
                result = " \t";
            } else if (className == "punct" || className == "PUNCT") {
                result = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
            } else if (className == "xdigit" || className == "XDIGIT") {
                for (char c = '0'; c <= '9'; ++c) result += c;
                for (char c = 'a'; c <= 'f'; ++c) result += c;
                for (char c = 'A'; c <= 'F'; ++c) result += c;
            } else if (className == "cntrl" || className == "CNTRL") {
                for (char c = 0; c < 32; ++c) result += c;
                result += (char)127;
            } else if (className == "graph" || className == "GRAPH") {
                for (char c = 33; c < 127; ++c) result += c;
            } else if (className == "print" || className == "PRINT") {
                for (char c = 32; c < 127; ++c) result += c;
            }
            return result;
        };

        auto expandEscape = [](char c) -> char {
            switch (c) {
                case 'n': return '\n';
                case 't': return '\t';
                case 'r': return '\r';
                case 'f': return '\f';
                case 'v': return '\v';
                case 'a': return '\a';
                case 'b': return '\b';
                case '\\': return '\\';
                default: return c;
            }
        };

        auto expandSet = [&](const std::string& set) -> std::string {
            std::string result;
            for (size_t i = 0; i < set.length(); ++i) {
                if (set[i] == '[' && i + 2 < set.length() && set[i + 1] == ':') {
                    size_t endPos = set.find(":]", i + 2);
                    if (endPos != std::string::npos) {
                        std::string className = set.substr(i + 2, endPos - i - 2);
                        result += expandClass(className);
                        i = endPos + 1;
                        continue;
                    }
                }
                if (set[i] == '\\' && i + 1 < set.length()) {
                    char next = set[i + 1];
                    if (next >= '0' && next <= '7') {
                        int val = 0;
                        size_t j = i + 1;
                        while (j < set.length() && j < i + 4 && set[j] >= '0' && set[j] <= '7') {
                            val = val * 8 + (set[j] - '0');
                            j++;
                        }
                        result += (char)val;
                        i = j - 1;
                    } else {
                        result += expandEscape(next);
                        i++;
                    }
                    continue;
                }
                if (i + 2 < set.length() && set[i + 1] == '-') {
                    char start = set[i];
                    char end = set[i + 2];
                    if (start <= end) {
                        for (char c = start; c <= end; ++c) result += c;
                    } else {
                        for (char c = start; c >= end; --c) result += c;
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
        
        if (complementMode) {
            std::string complement;
            for (int c = 1; c < 256; ++c) {
                if (expandedSet1.find((char)c) == std::string::npos) {
                    complement += (char)c;
                }
            }
            expandedSet1 = complement;
        }
        
        while (!deleteMode && !expandedSet2.empty() && expandedSet2.length() < expandedSet1.length()) {
            expandedSet2 += expandedSet2.back();
        }
        
        std::string input = pipedInput;
        if (input.empty()) {
            std::ostringstream oss;
            char buf[4096];
            while (std::cin.read(buf, sizeof(buf)) || std::cin.gcount() > 0) {
                oss.write(buf, std::cin.gcount());
            }
            input = oss.str();
        }
        
        std::string result;
        char lastChar = '\0';
        bool lastWasInSet1 = false;
        
        for (unsigned char c : input) {
            size_t pos = expandedSet1.find(c);
            if (deleteMode) {
                if (pos == std::string::npos) {
                    if (!squeezeMode || c != lastChar) {
                        result += c;
                        lastChar = c;
                    }
                }
            } else if (pos != std::string::npos) {
                char newChar = (pos < expandedSet2.length()) ? expandedSet2[pos] : c;
                if (!squeezeMode || newChar != lastChar || !lastWasInSet1) {
                    result += newChar;
                    lastChar = newChar;
                }
                lastWasInSet1 = true;
            } else {
                result += c;
                lastChar = c;
                lastWasInSet1 = false;
            }
        }
        
        std::cout << result;
    }

    void cmdSed(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (args.size() < 2) {
            printError("sed: missing script");
            return;
        }
        
        std::vector<std::string> scripts;
        std::vector<std::string> files;
        bool inPlace = false;
        std::string inPlaceSuffix;
        bool quietMode = false;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-n" || args[i] == "--quiet" || args[i] == "--silent") {
                quietMode = true;
            } else if (args[i] == "-e" && i + 1 < args.size()) {
                scripts.push_back(args[++i]);
            } else if (args[i] == "-i" || args[i].substr(0, 2) == "-i") {
                inPlace = true;
                if (args[i].length() > 2) inPlaceSuffix = args[i].substr(2);
            } else if (args[i][0] == '-') {
            } else if (scripts.empty()) {
                scripts.push_back(args[i]);
            } else {
                files.push_back(args[i]);
            }
        }
        
        if (scripts.empty()) {
            printError("sed: missing script");
            return;
        }

        struct SedCommand {
            std::string addr1;
            std::string addr2;
            char cmd;
            std::string arg1;
            std::string arg2;
            bool globalFlag;
            bool printFlag;
        };

        auto parseScript = [&](const std::string& script) -> std::vector<SedCommand> {
            std::vector<SedCommand> commands;
            size_t pos = 0;
            
            while (pos < script.length()) {
                while (pos < script.length() && (script[pos] == ' ' || script[pos] == '\t' || script[pos] == ';')) pos++;
                if (pos >= script.length()) break;
                
                SedCommand cmd = {"", "", '\0', "", "", false, false};
                
                if (std::isdigit(script[pos]) || script[pos] == '$') {
                    while (pos < script.length() && (std::isdigit(script[pos]) || script[pos] == '$')) {
                        cmd.addr1 += script[pos++];
                    }
                    if (pos < script.length() && script[pos] == ',') {
                        pos++;
                        while (pos < script.length() && (std::isdigit(script[pos]) || script[pos] == '$')) {
                            cmd.addr2 += script[pos++];
                        }
                    }
                } else if (script[pos] == '/') {
                    pos++;
                    while (pos < script.length() && script[pos] != '/') {
                        if (script[pos] == '\\' && pos + 1 < script.length()) {
                            cmd.addr1 += script[pos++];
                        }
                        cmd.addr1 += script[pos++];
                    }
                    if (pos < script.length()) pos++;
                }
                
                while (pos < script.length() && (script[pos] == ' ' || script[pos] == '\t')) pos++;
                if (pos >= script.length()) break;
                
                cmd.cmd = script[pos++];
                
                if (cmd.cmd == 's' && pos < script.length()) {
                    char delim = script[pos++];
                    bool escaped = false;
                    while (pos < script.length()) {
                        if (escaped) {
                            cmd.arg1 += script[pos++];
                            escaped = false;
                        } else if (script[pos] == '\\') {
                            escaped = true;
                            cmd.arg1 += script[pos++];
                        } else if (script[pos] == delim) {
                            pos++;
                            break;
                        } else {
                            cmd.arg1 += script[pos++];
                        }
                    }
                    escaped = false;
                    while (pos < script.length()) {
                        if (escaped) {
                            cmd.arg2 += script[pos++];
                            escaped = false;
                        } else if (script[pos] == '\\') {
                            escaped = true;
                            cmd.arg2 += script[pos++];
                        } else if (script[pos] == delim) {
                            pos++;
                            break;
                        } else {
                            cmd.arg2 += script[pos++];
                        }
                    }
                    while (pos < script.length() && script[pos] != ';' && script[pos] != '\n') {
                        if (script[pos] == 'g') cmd.globalFlag = true;
                        else if (script[pos] == 'p') cmd.printFlag = true;
                        pos++;
                    }
                } else if (cmd.cmd == 'y' && pos < script.length()) {
                    char delim = script[pos++];
                    while (pos < script.length() && script[pos] != delim) cmd.arg1 += script[pos++];
                    if (pos < script.length()) pos++;
                    while (pos < script.length() && script[pos] != delim) cmd.arg2 += script[pos++];
                    if (pos < script.length()) pos++;
                }
                
                commands.push_back(cmd);
            }
            return commands;
        };

        std::vector<SedCommand> allCommands;
        for (const auto& script : scripts) {
            auto cmds = parseScript(script);
            allCommands.insert(allCommands.end(), cmds.begin(), cmds.end());
        }

        auto matchAddress = [](const std::string& addr, int lineNum, int lastLine, const std::string& line) -> bool {
            if (addr.empty()) return true;
            if (addr == "$") return lineNum == lastLine;
            if (std::isdigit(addr[0])) return lineNum == std::stoi(addr);
            try {
                std::regex re(addr);
                return std::regex_search(line, re);
            } catch (...) {
                return line.find(addr) != std::string::npos;
            }
        };

        auto processLines = [&](std::vector<std::string>& lines) -> std::string {
            std::ostringstream output;
            int lastLine = (int)lines.size();
            std::map<int, bool> inRange;
            
            for (int lineNum = 1; lineNum <= (int)lines.size(); ++lineNum) {
                std::string line = lines[lineNum - 1];
                bool deleted = false;
                bool printed = false;
                
                for (size_t ci = 0; ci < allCommands.size(); ++ci) {
                    const auto& cmd = allCommands[ci];
                    
                    bool inAddr = false;
                    if (cmd.addr1.empty() && cmd.addr2.empty()) {
                        inAddr = true;
                    } else if (cmd.addr2.empty()) {
                        inAddr = matchAddress(cmd.addr1, lineNum, lastLine, line);
                    } else {
                        if (!inRange[ci] && matchAddress(cmd.addr1, lineNum, lastLine, line)) {
                            inRange[ci] = true;
                        }
                        if (inRange[ci]) {
                            inAddr = true;
                            if (matchAddress(cmd.addr2, lineNum, lastLine, line)) {
                                inRange[ci] = false;
                            }
                        }
                    }
                    
                    if (!inAddr) continue;
                    
                    switch (cmd.cmd) {
                        case 'd':
                            deleted = true;
                            break;
                        case 'p':
                            output << line << "\n";
                            printed = true;
                            break;
                        case 'q':
                            if (!quietMode && !deleted) output << line << "\n";
                            return output.str();
                        case 's': {
                            try {
                                std::regex re(cmd.arg1);
                                std::string repl = cmd.arg2;
                                if (cmd.globalFlag) {
                                    line = std::regex_replace(line, re, repl);
                                } else {
                                    line = std::regex_replace(line, re, repl, std::regex_constants::format_first_only);
                                }
                                if (cmd.printFlag) {
                                    output << line << "\n";
                                    printed = true;
                                }
                            } catch (...) {
                                if (cmd.globalFlag) {
                                    size_t pos = 0;
                                    while ((pos = line.find(cmd.arg1, pos)) != std::string::npos) {
                                        line.replace(pos, cmd.arg1.length(), cmd.arg2);
                                        pos += cmd.arg2.length();
                                    }
                                } else {
                                    size_t pos = line.find(cmd.arg1);
                                    if (pos != std::string::npos) {
                                        line.replace(pos, cmd.arg1.length(), cmd.arg2);
                                    }
                                }
                            }
                            break;
                        }
                        case 'y': {
                            for (char& c : line) {
                                size_t idx = cmd.arg1.find(c);
                                if (idx != std::string::npos && idx < cmd.arg2.length()) {
                                    c = cmd.arg2[idx];
                                }
                            }
                            break;
                        }
                    }
                    
                    if (deleted) break;
                }
                
                if (!deleted && !quietMode) {
                    output << line << "\n";
                }
            }
            return output.str();
        };

        if (!pipedInput.empty()) {
            std::vector<std::string> lines;
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) lines.push_back(line);
            std::cout << processLines(lines);
        } else if (!files.empty()) {
            for (const auto& filePath : files) {
                std::vector<std::string> lines;
                std::ifstream file(resolvePath(filePath));
                if (!file) {
                    printError("sed: cannot open '" + filePath + "'");
                    continue;
                }
                std::string line;
                while (std::getline(file, line)) lines.push_back(line);
                file.close();
                
                std::string result = processLines(lines);
                
                if (inPlace) {
                    if (!inPlaceSuffix.empty()) {
                        fs::copy_file(resolvePath(filePath), resolvePath(filePath) + inPlaceSuffix, fs::copy_options::overwrite_existing);
                    }
                    std::ofstream out(resolvePath(filePath));
                    out << result;
                } else {
                    std::cout << result;
                }
            }
        } else {
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(std::cin, line)) lines.push_back(line);
            std::cout << processLines(lines);
        }
    }

    void cmdAwk(const std::vector<std::string>& args, const std::string& pipedInput = "") {
        if (args.size() < 2) {
            printError("awk: missing program");
            return;
        }
        
        std::string fieldSepStr = " ";
        std::string program;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-F" && i + 1 < args.size()) {
                fieldSepStr = args[++i];
            } else if (args[i].substr(0, 2) == "-F") {
                fieldSepStr = args[i].substr(2);
            } else if (program.empty() && (args[i][0] == '{' || args[i][0] == '\'')) {
                program = args[i];
            } else if (program.empty() && args[i][0] != '-') {
                program = args[i];
            } else if (args[i][0] != '-') {
                files.push_back(args[i]);
            }
        }

        std::map<std::string, std::string> vars;
        vars["FS"] = fieldSepStr;
        vars["OFS"] = " ";
        vars["ORS"] = "\n";
        vars["NR"] = "0";
        vars["NF"] = "0";
        vars["FILENAME"] = "";

        auto splitFields = [&](const std::string& line) -> std::vector<std::string> {
            std::vector<std::string> fields;
            fields.push_back(line);
            std::string fs = vars["FS"];
            
            if (fs == " ") {
                std::string token;
                bool inToken = false;
                for (char c : line) {
                    if (std::isspace(c)) {
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
                if (inToken) fields.push_back(token);
            } else if (fs.length() == 1) {
                std::istringstream iss(line);
                std::string token;
                while (std::getline(iss, token, fs[0])) {
                    fields.push_back(token);
                }
            } else {
                size_t pos = 0, found;
                while ((found = line.find(fs, pos)) != std::string::npos) {
                    fields.push_back(line.substr(pos, found - pos));
                    pos = found + fs.length();
                }
                fields.push_back(line.substr(pos));
            }
            
            vars["NF"] = std::to_string(fields.size() - 1);
            return fields;
        };

        auto evalExpr = [&](const std::string& expr, const std::vector<std::string>& fields) -> std::string {
            std::string result = expr;
            
            for (int i = 9; i >= 0; --i) {
                std::string placeholder = "$" + std::to_string(i);
                size_t pos;
                while ((pos = result.find(placeholder)) != std::string::npos) {
                    std::string val = (i < (int)fields.size()) ? fields[i] : "";
                    result.replace(pos, placeholder.length(), val);
                }
            }
            
            for (const auto& kv : vars) {
                size_t pos;
                while ((pos = result.find(kv.first)) != std::string::npos) {
                    bool validBoundary = true;
                    if (pos > 0 && (std::isalnum(result[pos - 1]) || result[pos - 1] == '_')) validBoundary = false;
                    size_t endPos = pos + kv.first.length();
                    if (endPos < result.length() && (std::isalnum(result[endPos]) || result[endPos] == '_')) validBoundary = false;
                    if (validBoundary) {
                        result.replace(pos, kv.first.length(), kv.second);
                    } else {
                        break;
                    }
                }
            }
            
            size_t lenPos;
            while ((lenPos = result.find("length(")) != std::string::npos) {
                size_t endParen = result.find(")", lenPos);
                if (endParen != std::string::npos) {
                    std::string arg = result.substr(lenPos + 7, endParen - lenPos - 7);
                    result.replace(lenPos, endParen - lenPos + 1, std::to_string(arg.length()));
                } else break;
            }
            
            while ((lenPos = result.find("toupper(")) != std::string::npos) {
                size_t endParen = result.find(")", lenPos);
                if (endParen != std::string::npos) {
                    std::string arg = result.substr(lenPos + 8, endParen - lenPos - 8);
                    std::transform(arg.begin(), arg.end(), arg.begin(), ::toupper);
                    result.replace(lenPos, endParen - lenPos + 1, arg);
                } else break;
            }
            
            while ((lenPos = result.find("tolower(")) != std::string::npos) {
                size_t endParen = result.find(")", lenPos);
                if (endParen != std::string::npos) {
                    std::string arg = result.substr(lenPos + 8, endParen - lenPos - 8);
                    std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);
                    result.replace(lenPos, endParen - lenPos + 1, arg);
                } else break;
            }
            
            return result;
        };

        auto parseAction = [&](const std::string& action, const std::vector<std::string>& fields) {
            std::string act = action;
            while (!act.empty() && (act.front() == '{' || act.front() == ' ')) act.erase(0, 1);
            while (!act.empty() && (act.back() == '}' || act.back() == ' ')) act.pop_back();
            
            std::istringstream iss(act);
            std::string stmt;
            while (std::getline(iss, stmt, ';')) {
                while (!stmt.empty() && stmt.front() == ' ') stmt.erase(0, 1);
                while (!stmt.empty() && stmt.back() == ' ') stmt.pop_back();
                if (stmt.empty()) continue;
                
                if (stmt.substr(0, 5) == "print") {
                    std::string printArgs = stmt.substr(5);
                    while (!printArgs.empty() && printArgs.front() == ' ') printArgs.erase(0, 1);
                    
                    if (printArgs.empty()) {
                        std::cout << fields[0] << vars["ORS"];
                    } else {
                        std::vector<std::string> parts;
                        std::string current;
                        bool inQuote = false;
                        for (size_t i = 0; i < printArgs.length(); ++i) {
                            char c = printArgs[i];
                            if (c == '"') {
                                inQuote = !inQuote;
                            } else if ((c == ',' || c == ' ') && !inQuote) {
                                if (!current.empty()) {
                                    parts.push_back(current);
                                    current.clear();
                                }
                            } else {
                                current += c;
                            }
                        }
                        if (!current.empty()) parts.push_back(current);
                        
                        std::string output;
                        for (size_t i = 0; i < parts.size(); ++i) {
                            if (i > 0) output += vars["OFS"];
                            output += evalExpr(parts[i], fields);
                        }
                        std::cout << output << vars["ORS"];
                    }
                } else if (stmt.substr(0, 6) == "printf") {
                    std::string printfArgs = stmt.substr(6);
                    while (!printfArgs.empty() && printfArgs.front() == ' ') printfArgs.erase(0, 1);
                    
                    size_t firstQuote = printfArgs.find('"');
                    size_t lastQuote = printfArgs.rfind('"');
                    if (firstQuote != std::string::npos && lastQuote > firstQuote) {
                        std::string fmt = printfArgs.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                        std::string argsStr = printfArgs.substr(lastQuote + 1);
                        
                        std::vector<std::string> printfVals;
                        std::istringstream argStream(argsStr);
                        std::string arg;
                        while (std::getline(argStream, arg, ',')) {
                            while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
                            printfVals.push_back(evalExpr(arg, fields));
                        }
                        
                        std::string output;
                        size_t valIdx = 0;
                        for (size_t i = 0; i < fmt.length(); ++i) {
                            if (fmt[i] == '%' && i + 1 < fmt.length()) {
                                char spec = fmt[i + 1];
                                if (spec == 's' && valIdx < printfVals.size()) {
                                    output += printfVals[valIdx++];
                                    i++;
                                } else if (spec == 'd' && valIdx < printfVals.size()) {
                                    try { output += std::to_string(std::stoi(printfVals[valIdx++])); } catch (...) { output += "0"; }
                                    i++;
                                } else if (spec == '%') {
                                    output += '%';
                                    i++;
                                } else {
                                    output += fmt[i];
                                }
                            } else if (fmt[i] == '\\' && i + 1 < fmt.length()) {
                                char esc = fmt[i + 1];
                                if (esc == 'n') output += '\n';
                                else if (esc == 't') output += '\t';
                                else output += esc;
                                i++;
                            } else {
                                output += fmt[i];
                            }
                        }
                        std::cout << output;
                    }
                }
            }
        };

        std::string beginBlock, endBlock, mainBlock;
        size_t beginPos = program.find("BEGIN");
        size_t endPos = program.find("END");
        
        if (beginPos != std::string::npos) {
            size_t braceStart = program.find('{', beginPos);
            if (braceStart != std::string::npos) {
                int braceCount = 1;
                size_t braceEnd = braceStart + 1;
                while (braceEnd < program.length() && braceCount > 0) {
                    if (program[braceEnd] == '{') braceCount++;
                    else if (program[braceEnd] == '}') braceCount--;
                    braceEnd++;
                }
                beginBlock = program.substr(braceStart, braceEnd - braceStart);
            }
        }
        
        if (endPos != std::string::npos) {
            size_t braceStart = program.find('{', endPos);
            if (braceStart != std::string::npos) {
                int braceCount = 1;
                size_t braceEnd = braceStart + 1;
                while (braceEnd < program.length() && braceCount > 0) {
                    if (program[braceEnd] == '{') braceCount++;
                    else if (program[braceEnd] == '}') braceCount--;
                    braceEnd++;
                }
                endBlock = program.substr(braceStart, braceEnd - braceStart);
            }
        }
        
        size_t mainStart = program.find('{');
        if (mainStart != std::string::npos) {
            if (beginPos != std::string::npos && mainStart > beginPos && mainStart < beginPos + 10) {
                mainStart = program.find('}', mainStart);
                if (mainStart != std::string::npos) mainStart = program.find('{', mainStart);
            }
            if (mainStart != std::string::npos && endPos != std::string::npos && mainStart > endPos) {
                mainStart = std::string::npos;
            }
            if (mainStart != std::string::npos) {
                int braceCount = 1;
                size_t braceEnd = mainStart + 1;
                while (braceEnd < program.length() && braceCount > 0) {
                    if (program[braceEnd] == '{') braceCount++;
                    else if (program[braceEnd] == '}') braceCount--;
                    braceEnd++;
                }
                mainBlock = program.substr(mainStart, braceEnd - mainStart);
            }
        }
        
        if (mainBlock.empty() && beginBlock.empty() && endBlock.empty()) {
            mainBlock = program;
        }

        if (!beginBlock.empty()) {
            std::vector<std::string> emptyFields = {""};
            parseAction(beginBlock, emptyFields);
        }

        auto processLine = [&](const std::string& line, const std::string& filename) {
            int nr = std::stoi(vars["NR"]) + 1;
            vars["NR"] = std::to_string(nr);
            vars["FILENAME"] = filename;
            
            std::vector<std::string> fields = splitFields(line);
            
            if (!mainBlock.empty()) {
                parseAction(mainBlock, fields);
            }
        };

        if (!pipedInput.empty()) {
            std::istringstream iss(pipedInput);
            std::string line;
            while (std::getline(iss, line)) {
                processLine(line, "");
            }
        } else if (!files.empty()) {
            for (const auto& filePath : files) {
                std::ifstream file(resolvePath(filePath));
                if (!file) {
                    printError("awk: cannot open '" + filePath + "'");
                    continue;
                }
                std::string line;
                while (std::getline(file, line)) {
                    processLine(line, filePath);
                }
            }
        } else {
            std::string line;
            while (std::getline(std::cin, line)) {
                processLine(line, "");
            }
        }

        if (!endBlock.empty()) {
            std::vector<std::string> emptyFields = {""};
            parseAction(endBlock, emptyFields);
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

    void cmdLn(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("ln: missing operand");
            return;
        }
        
        bool symbolic = false;
        bool force = false;
        bool noDereference = false;
        bool verbose = false;
        bool relative = false;
        std::vector<std::string> targets;
        std::string linkName;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] == '-' && args[i].length() > 1) {
                for (size_t j = 1; j < args[i].length(); ++j) {
                    char flag = args[i][j];
                    if (flag == 's') symbolic = true;
                    else if (flag == 'f') force = true;
                    else if (flag == 'n') noDereference = true;
                    else if (flag == 'v') verbose = true;
                    else if (flag == 'r') relative = true;
                }
            } else {
                if (linkName.empty()) {
                    targets.push_back(args[i]);
                } else {
                    targets.push_back(linkName);
                    linkName = args[i];
                }
                if (targets.size() == 1 && i == args.size() - 1) {
                    linkName = targets[0];
                    targets.clear();
                } else if (i == args.size() - 1) {
                    linkName = args[i];
                    targets.pop_back();
                }
            }
        }
        
        if (targets.empty() && !linkName.empty()) {
            targets.push_back(linkName);
            linkName = fs::path(linkName).filename().string();
        }

        for (const auto& target : targets) {
            std::string targetPath = resolvePath(target);
            std::string actualLinkPath;
            
            if (fs::is_directory(resolvePath(linkName))) {
                actualLinkPath = resolvePath(linkName) + "\\" + fs::path(target).filename().string();
            } else {
                actualLinkPath = resolvePath(linkName);
            }
            
            if (force && fs::exists(actualLinkPath)) {
                try { fs::remove(actualLinkPath); } catch (...) {}
            }
            
            if (fs::exists(actualLinkPath) && !noDereference) {
                printError("ln: failed to create link '" + actualLinkPath + "': File exists");
                continue;
            }
            
            std::string linkTarget = targetPath;
            if (relative && symbolic) {
                fs::path linkDir = fs::path(actualLinkPath).parent_path();
                linkTarget = fs::relative(targetPath, linkDir).string();
            }
            
            if (!symbolic) {
                if (CreateHardLinkA(actualLinkPath.c_str(), targetPath.c_str(), NULL)) {
                    if (verbose) std::cout << "'" << actualLinkPath << "' => '" << target << "'\n";
                } else {
                    printError("ln: failed to create hard link (error " + std::to_string(GetLastError()) + ")");
                }
            } else {
                DWORD flags = 0;
                try { if (fs::is_directory(targetPath)) flags = SYMBOLIC_LINK_FLAG_DIRECTORY; } catch (...) {}
                flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
                
                if (CreateSymbolicLinkA(actualLinkPath.c_str(), linkTarget.c_str(), flags)) {
                    if (verbose) std::cout << "'" << actualLinkPath << "' -> '" << linkTarget << "'\n";
                } else {
                    DWORD error = GetLastError();
                    if (error == ERROR_PRIVILEGE_NOT_HELD) {
                        printError("ln: symbolic links require admin privileges or Developer Mode");
                    } else {
                        printError("ln: failed to create symbolic link (error " + std::to_string(error) + ")");
                    }
                }
            }
        }
    }

    void cmdStat(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("stat: missing file operand");
            return;
        }
        
        std::string format;
        bool followSymlinks = false;
        bool terse = false;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i] == "-c" || args[i] == "--format") && i + 1 < args.size()) {
                format = args[++i];
            } else if (args[i].substr(0, 10) == "--format=") {
                format = args[i].substr(10);
            } else if (args[i] == "-L" || args[i] == "--dereference") {
                followSymlinks = true;
            } else if (args[i] == "-t" || args[i] == "--terse") {
                terse = true;
            } else if (args[i][0] != '-') {
                files.push_back(args[i]);
            }
        }
        
        for (const auto& arg : files) {
            std::string filePath = resolvePath(arg);
            
            if (followSymlinks && fs::is_symlink(filePath)) {
                try { filePath = fs::canonical(filePath).string(); } catch (...) {}
            }
            
            if (!fs::exists(filePath)) {
                printError("stat: cannot stat '" + arg + "': No such file or directory");
                continue;
            }
            
            try {
                auto fileSize = fs::is_regular_file(filePath) ? fs::file_size(filePath) : 0;
                auto lastWrite = fs::last_write_time(filePath);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    lastWrite - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                auto modTime = std::chrono::system_clock::to_time_t(sctp);
                
                std::string fileType;
                if (fs::is_regular_file(filePath)) fileType = "regular file";
                else if (fs::is_directory(filePath)) fileType = "directory";
                else if (fs::is_symlink(filePath)) fileType = "symbolic link";
                else fileType = "unknown";
                
                DWORD attrs = GetFileAttributesA(filePath.c_str());
                std::string attrStr;
                if (attrs & FILE_ATTRIBUTE_READONLY) attrStr += "r";
                else attrStr += "-";
                attrStr += "w";
                if (filePath.find(".exe") != std::string::npos || filePath.find(".bat") != std::string::npos)
                    attrStr += "x";
                else attrStr += "-";
                
                BY_HANDLE_FILE_INFORMATION fileInfo;
                HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                DWORD nLinks = 1;
                DWORD fileIndex = 0;
                if (hFile != INVALID_HANDLE_VALUE) {
                    if (GetFileInformationByHandle(hFile, &fileInfo)) {
                        nLinks = fileInfo.nNumberOfLinks;
                        fileIndex = fileInfo.nFileIndexLow;
                    }
                    CloseHandle(hFile);
                }
                
                if (!format.empty()) {
                    std::string output = format;
                    size_t pos;
                    while ((pos = output.find("%n")) != std::string::npos)
                        output.replace(pos, 2, arg);
                    while ((pos = output.find("%N")) != std::string::npos)
                        output.replace(pos, 2, "'" + arg + "'");
                    while ((pos = output.find("%s")) != std::string::npos)
                        output.replace(pos, 2, std::to_string(fileSize));
                    while ((pos = output.find("%F")) != std::string::npos)
                        output.replace(pos, 2, fileType);
                    while ((pos = output.find("%A")) != std::string::npos)
                        output.replace(pos, 2, attrStr);
                    while ((pos = output.find("%h")) != std::string::npos)
                        output.replace(pos, 2, std::to_string(nLinks));
                    while ((pos = output.find("%i")) != std::string::npos)
                        output.replace(pos, 2, std::to_string(fileIndex));
                    while ((pos = output.find("%Y")) != std::string::npos)
                        output.replace(pos, 2, std::to_string(modTime));
                    while ((pos = output.find("%y")) != std::string::npos) {
                        char timeBuf[64];
                        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&modTime));
                        output.replace(pos, 2, timeBuf);
                    }
                    while ((pos = output.find("\\n")) != std::string::npos)
                        output.replace(pos, 2, "\n");
                    std::cout << output << "\n";
                } else if (terse) {
                    std::cout << arg << " " << fileSize << " " << nLinks << " " << modTime << "\n";
                } else {
                    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    std::cout << "  File: ";
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    std::cout << arg << "\n";
                    std::cout << "  Size: " << fileSize << "       \tBlocks: " << (fileSize / 512 + 1) 
                              << "     \tLinks: " << nLinks << "\n";
                    std::cout << "  Type: " << fileType << "\n";
                    std::cout << " Attrs: ";
                    if (attrs & FILE_ATTRIBUTE_READONLY) std::cout << "readonly ";
                    if (attrs & FILE_ATTRIBUTE_HIDDEN) std::cout << "hidden ";
                    if (attrs & FILE_ATTRIBUTE_SYSTEM) std::cout << "system ";
                    if (attrs & FILE_ATTRIBUTE_ARCHIVE) std::cout << "archive ";
                    if (attrs & FILE_ATTRIBUTE_DIRECTORY) std::cout << "directory ";
                    std::cout << "\n";
                    std::cout << "Modify: " << std::ctime(&modTime);
                    std::cout << "\n";
                }
            } catch (const std::exception& e) {
                printError("stat: " + std::string(e.what()));
            }
        }
    }

    void cmdFile(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("file: missing file operand");
            return;
        }
        
        bool brief = false;
        bool mimeType = false;
        std::vector<std::string> files;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-b" || args[i] == "--brief") brief = true;
            else if (args[i] == "-i" || args[i] == "--mime-type") mimeType = true;
            else if (args[i][0] != '-') files.push_back(args[i]);
        }
        
        for (const auto& arg : files) {
            std::string filePath = resolvePath(arg);
            
            if (!fs::exists(filePath)) {
                if (!brief) std::cout << arg << ": ";
                std::cout << "cannot open (No such file or directory)\n";
                continue;
            }
            
            if (!brief) std::cout << arg << ": ";
            
            if (fs::is_directory(filePath)) {
                std::cout << (mimeType ? "inode/directory" : "directory") << "\n";
                continue;
            }
            
            if (fs::is_symlink(filePath)) {
                std::cout << (mimeType ? "inode/symlink" : "symbolic link") << "\n";
                continue;
            }
            
            std::ifstream file(filePath, std::ios::binary);
            if (!file) {
                std::cout << "cannot open\n";
                continue;
            }
            
            unsigned char magic[32] = {0};
            file.read(reinterpret_cast<char*>(magic), 32);
            size_t bytesRead = file.gcount();
            
            if (bytesRead == 0) {
                std::cout << (mimeType ? "inode/x-empty" : "empty") << "\n";
                continue;
            }
            
            if (magic[0] == 0x4D && magic[1] == 0x5A) {
                std::cout << (mimeType ? "application/x-dosexec" : "PE32 executable (Windows)") << "\n";
            } else if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
                std::cout << (mimeType ? "application/x-executable" : "ELF executable") << "\n";
            } else if (magic[0] == 0xCA && magic[1] == 0xFE && magic[2] == 0xBA && magic[3] == 0xBE) {
                std::cout << (mimeType ? "application/x-mach-binary" : "Mach-O universal binary") << "\n";
            } else if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') {
                std::cout << (mimeType ? "image/png" : "PNG image data") << "\n";
            } else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
                std::cout << (mimeType ? "image/jpeg" : "JPEG image data") << "\n";
            } else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8') {
                std::cout << (mimeType ? "image/gif" : "GIF image data") << "\n";
            } else if (magic[0] == 'B' && magic[1] == 'M') {
                std::cout << (mimeType ? "image/bmp" : "BMP image data") << "\n";
            } else if (magic[0] == 0x00 && magic[1] == 0x00 && magic[2] == 0x01 && magic[3] == 0x00) {
                std::cout << (mimeType ? "image/x-icon" : "ICO image data") << "\n";
            } else if (magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F') {
                if (magic[8] == 'W' && magic[9] == 'A' && magic[10] == 'V' && magic[11] == 'E') {
                    std::cout << (mimeType ? "audio/wav" : "WAV audio") << "\n";
                } else if (magic[8] == 'A' && magic[9] == 'V' && magic[10] == 'I') {
                    std::cout << (mimeType ? "video/avi" : "AVI video") << "\n";
                } else if (magic[8] == 'W' && magic[9] == 'E' && magic[10] == 'B' && magic[11] == 'P') {
                    std::cout << (mimeType ? "image/webp" : "WebP image") << "\n";
                } else {
                    std::cout << (mimeType ? "application/octet-stream" : "RIFF data") << "\n";
                }
            } else if (magic[0] == 'O' && magic[1] == 'g' && magic[2] == 'g' && magic[3] == 'S') {
                std::cout << (mimeType ? "application/ogg" : "Ogg data") << "\n";
            } else if (magic[0] == 'f' && magic[1] == 'L' && magic[2] == 'a' && magic[3] == 'C') {
                std::cout << (mimeType ? "audio/flac" : "FLAC audio") << "\n";
            } else if (magic[0] == 0xFF && (magic[1] & 0xE0) == 0xE0) {
                std::cout << (mimeType ? "audio/mpeg" : "MP3 audio") << "\n";
            } else if (magic[0] == 'I' && magic[1] == 'D' && magic[2] == '3') {
                std::cout << (mimeType ? "audio/mpeg" : "MP3 audio (ID3 tag)") << "\n";
            } else if (magic[4] == 'f' && magic[5] == 't' && magic[6] == 'y' && magic[7] == 'p') {
                std::cout << (mimeType ? "video/mp4" : "MP4/M4A media") << "\n";
            } else if (magic[0] == 0x1A && magic[1] == 0x45 && magic[2] == 0xDF && magic[3] == 0xA3) {
                std::cout << (mimeType ? "video/webm" : "WebM/MKV video") << "\n";
            } else if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
                file.seekg(30);
                char nameTest[8] = {0};
                file.read(nameTest, 8);
                if (strncmp(nameTest, "word/", 5) == 0) {
                    std::cout << (mimeType ? "application/vnd.openxmlformats-officedocument.wordprocessingml.document" : "Microsoft Word 2007+ document") << "\n";
                } else if (strncmp(nameTest, "xl/", 3) == 0) {
                    std::cout << (mimeType ? "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" : "Microsoft Excel 2007+ spreadsheet") << "\n";
                } else if (strncmp(nameTest, "ppt/", 4) == 0) {
                    std::cout << (mimeType ? "application/vnd.openxmlformats-officedocument.presentationml.presentation" : "Microsoft PowerPoint 2007+ presentation") << "\n";
                } else {
                    std::cout << (mimeType ? "application/zip" : "Zip archive data") << "\n";
                }
            } else if (magic[0] == 0x1F && magic[1] == 0x8B) {
                std::cout << (mimeType ? "application/gzip" : "gzip compressed data") << "\n";
            } else if (magic[0] == 0x42 && magic[1] == 0x5A && magic[2] == 0x68) {
                std::cout << (mimeType ? "application/x-bzip2" : "bzip2 compressed data") << "\n";
            } else if (magic[0] == 0xFD && magic[1] == 0x37 && magic[2] == 0x7A && magic[3] == 0x58 && magic[4] == 0x5A) {
                std::cout << (mimeType ? "application/x-xz" : "XZ compressed data") << "\n";
            } else if (magic[0] == 0x37 && magic[1] == 0x7A && magic[2] == 0xBC && magic[3] == 0xAF) {
                std::cout << (mimeType ? "application/x-7z-compressed" : "7-zip archive data") << "\n";
            } else if (magic[0] == 0x52 && magic[1] == 0x61 && magic[2] == 0x72 && magic[3] == 0x21) {
                std::cout << (mimeType ? "application/x-rar" : "RAR archive data") << "\n";
            } else if (magic[0] == '%' && magic[1] == 'P' && magic[2] == 'D' && magic[3] == 'F') {
                std::cout << (mimeType ? "application/pdf" : "PDF document") << "\n";
            } else if (magic[0] == 0xD0 && magic[1] == 0xCF && magic[2] == 0x11 && magic[3] == 0xE0) {
                std::cout << (mimeType ? "application/msword" : "Microsoft Office document (OLE)") << "\n";
            } else if (magic[0] == 0x25 && magic[1] == 0x21 && magic[2] == 0x50 && magic[3] == 0x53) {
                std::cout << (mimeType ? "application/postscript" : "PostScript document") << "\n";
            } else if (magic[0] == 0xEF && magic[1] == 0xBB && magic[2] == 0xBF) {
                std::cout << (mimeType ? "text/plain; charset=utf-8" : "UTF-8 Unicode text (with BOM)") << "\n";
            } else if (magic[0] == 0xFE && magic[1] == 0xFF) {
                std::cout << (mimeType ? "text/plain; charset=utf-16be" : "UTF-16 BE Unicode text") << "\n";
            } else if (magic[0] == 0xFF && magic[1] == 0xFE) {
                std::cout << (mimeType ? "text/plain; charset=utf-16le" : "UTF-16 LE Unicode text") << "\n";
            } else if (magic[0] == '<' && magic[1] == '?') {
                if (magic[2] == 'x' && magic[3] == 'm' && magic[4] == 'l') {
                    std::cout << (mimeType ? "application/xml" : "XML document") << "\n";
                } else {
                    std::cout << (mimeType ? "text/x-php" : "PHP script") << "\n";
                }
            } else if (magic[0] == '<' && magic[1] == '!' && magic[2] == 'D') {
                std::cout << (mimeType ? "text/html" : "HTML document") << "\n";
            } else if (magic[0] == '<' && (magic[1] == 'h' || magic[1] == 'H')) {
                std::cout << (mimeType ? "text/html" : "HTML document") << "\n";
            } else if (magic[0] == '{' || magic[0] == '[') {
                std::cout << (mimeType ? "application/json" : "JSON data") << "\n";
            } else if (magic[0] == '#' && magic[1] == '!') {
                std::cout << (mimeType ? "text/x-shellscript" : "script, shebang executable") << "\n";
            } else if (magic[0] == 0x00 && magic[1] == 0x00 && magic[2] == 0x00) {
                std::cout << (mimeType ? "application/octet-stream" : "binary data") << "\n";
            } else {
                bool isText = true;
                for (size_t j = 0; j < bytesRead; ++j) {
                    if (magic[j] < 0x09 || (magic[j] > 0x0D && magic[j] < 0x20 && magic[j] != 0x1B)) {
                        if (magic[j] != 0) isText = false;
                    }
                }
                if (isText) {
                    std::cout << (mimeType ? "text/plain" : "ASCII text") << "\n";
                } else {
                    std::cout << (mimeType ? "application/octet-stream" : "data") << "\n";
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

    void cmdTree(const std::vector<std::string>& args) {
        std::string path = ".";
        int maxDepth = -1;
        bool dirsOnly = false;
        bool showHidden = false;
        bool showSize = false;
        bool humanReadable = false;
        bool showPermissions = false;
        bool fullPath = false;
        bool noReport = false;
        bool showDu = false;
        std::string ignorePattern;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-L" && i + 1 < args.size()) {
                try { maxDepth = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i] == "-d") {
                dirsOnly = true;
            } else if (args[i] == "-a") {
                showHidden = true;
            } else if (args[i] == "-s") {
                showSize = true;
            } else if (args[i] == "-h") {
                humanReadable = true;
            } else if (args[i] == "-p") {
                showPermissions = true;
            } else if (args[i] == "-f") {
                fullPath = true;
            } else if (args[i] == "--noreport") {
                noReport = true;
            } else if (args[i] == "--du") {
                showDu = true;
                showSize = true;
            } else if ((args[i] == "-I" || args[i] == "--ignore") && i + 1 < args.size()) {
                ignorePattern = args[++i];
            } else if (args[i][0] != '-') {
                path = args[i];
            }
        }
        
        std::string rootPath = resolvePath(path);
        if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
            printError("tree: '" + path + "' is not a directory");
            return;
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << (fullPath ? rootPath : path) << "\n";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        int dirCount = 0, fileCount = 0;
        uintmax_t totalSize = 0;

        auto formatSize = [](uintmax_t bytes, bool human) -> std::string {
            if (!human) return std::to_string(bytes);
            const char* units[] = {"B", "K", "M", "G", "T"};
            int u = 0;
            double size = (double)bytes;
            while (size >= 1024 && u < 4) { size /= 1024; u++; }
            char buf[32];
            if (u == 0) snprintf(buf, sizeof(buf), "%4d%s", (int)size, units[u]);
            else snprintf(buf, sizeof(buf), "%4.1f%s", size, units[u]);
            return buf;
        };

        std::function<uintmax_t(const fs::path&, const std::string&, int)> printTree;
        printTree = [&](const fs::path& p, const std::string& prefix, int depth) -> uintmax_t {
            if (maxDepth >= 0 && depth >= maxDepth) return 0;
            
            std::vector<fs::directory_entry> entries;
            try {
                for (const auto& entry : fs::directory_iterator(p)) {
                    std::string name = entry.path().filename().string();
                    if (!showHidden && name[0] == '.') continue;
                    if (!ignorePattern.empty()) {
                        try {
                            std::regex re(ignorePattern);
                            if (std::regex_search(name, re)) continue;
                        } catch (...) {
                            if (name.find(ignorePattern) != std::string::npos) continue;
                        }
                    }
                    if (dirsOnly && !entry.is_directory()) continue;
                    entries.push_back(entry);
                }
            } catch (...) {
                return 0;
            }
            
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.path().filename() < b.path().filename();
            });
            
            uintmax_t dirSize = 0;
            
            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                bool isLast = (i == entries.size() - 1);
                
                std::cout << prefix << (isLast ? "`-- " : "|-- ");
                
                uintmax_t entrySize = 0;
                if (entry.is_directory()) {
                    if (showDu) {
                        entrySize = printTree(entry.path(), prefix + (isLast ? "    " : "|   "), depth + 1);
                    }
                } else {
                    try { entrySize = fs::file_size(entry.path()); } catch (...) {}
                }
                
                if (showPermissions) {
                    DWORD attrs = GetFileAttributesA(entry.path().string().c_str());
                    std::cout << "[" << (attrs & FILE_ATTRIBUTE_DIRECTORY ? "d" : "-")
                              << (attrs & FILE_ATTRIBUTE_READONLY ? "r-" : "rw")
                              << (attrs & FILE_ATTRIBUTE_HIDDEN ? "h" : "-") << "] ";
                }
                
                if (showSize) {
                    std::cout << "[" << std::setw(8) << formatSize(entrySize, humanReadable) << "]  ";
                }
                
                if (entry.is_directory()) {
                    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    std::cout << (fullPath ? entry.path().string() : entry.path().filename().string()) << "\n";
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    dirCount++;
                    if (!showDu) {
                        printTree(entry.path(), prefix + (isLast ? "    " : "|   "), depth + 1);
                    }
                    dirSize += entrySize;
                } else {
                    std::cout << (fullPath ? entry.path().string() : entry.path().filename().string()) << "\n";
                    fileCount++;
                    dirSize += entrySize;
                    totalSize += entrySize;
                }
            }
            return dirSize;
        };
        
        printTree(rootPath, "", 0);
        
        if (!noReport) {
            std::cout << "\n" << dirCount << " directories";
            if (!dirsOnly) {
                std::cout << ", " << fileCount << " files";
                if (showSize) {
                    std::cout << " (" << formatSize(totalSize, humanReadable) << " total)";
                }
            }
            std::cout << "\n";
        }
    }

    void cmdDu(const std::vector<std::string>& args) {
        bool humanReadable = false;
        bool summary = false;
        bool showAll = false;
        bool showTotal = false;
        bool showTime = false;
        bool apparentSize = false;
        int maxDepth = -1;
        int blockSize = 1024;
        std::string excludePattern;
        std::vector<std::string> paths;
        
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-h" || args[i] == "--human-readable") humanReadable = true;
            else if (args[i] == "-s" || args[i] == "--summarize") summary = true;
            else if (args[i] == "-a" || args[i] == "--all") showAll = true;
            else if (args[i] == "-c" || args[i] == "--total") showTotal = true;
            else if (args[i] == "-b" || args[i] == "--bytes") { blockSize = 1; apparentSize = true; }
            else if (args[i] == "-k") blockSize = 1024;
            else if (args[i] == "-m") blockSize = 1024 * 1024;
            else if (args[i] == "--time") showTime = true;
            else if (args[i] == "--apparent-size") apparentSize = true;
            else if ((args[i] == "-d" || args[i] == "--max-depth") && i + 1 < args.size()) {
                try { maxDepth = std::stoi(args[++i]); } catch (...) {}
            } else if (args[i].substr(0, 2) == "-d") {
                try { maxDepth = std::stoi(args[i].substr(2)); } catch (...) {}
            } else if ((args[i] == "--exclude") && i + 1 < args.size()) {
                excludePattern = args[++i];
            } else if (args[i][0] != '-') {
                paths.push_back(args[i]);
            }
        }
        
        if (paths.empty()) paths.push_back(".");
        
        auto formatSize = [humanReadable, blockSize](uintmax_t bytes) -> std::string {
            if (humanReadable) {
                const char* units[] = {"B", "K", "M", "G", "T"};
                int unit = 0;
                double size = (double)bytes;
                while (size >= 1024 && unit < 4) { size /= 1024; unit++; }
                std::ostringstream oss;
                if (unit == 0) oss << (int)size << units[unit];
                else oss << std::fixed << std::setprecision(1) << size << units[unit];
                return oss.str();
            }
            return std::to_string(bytes / blockSize);
        };
        
        uintmax_t grandTotal = 0;
        
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
                        uintmax_t fsize = fs::file_size(p);
                        if (showAll && !summary) {
                            std::cout << std::setw(8) << formatSize(fsize) << "\t" << p.string() << "\n";
                        }
                        return fsize;
                    }
                    
                    for (const auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied)) {
                        std::string name = entry.path().filename().string();
                        if (!excludePattern.empty()) {
                            try {
                                std::regex re(excludePattern);
                                if (std::regex_search(name, re)) continue;
                            } catch (...) {
                                if (name.find(excludePattern) != std::string::npos) continue;
                            }
                        }
                        
                        if (entry.is_directory()) {
                            uintmax_t dirSize = calcSize(entry.path(), depth + 1);
                            total += dirSize;
                            
                            if (!summary && (maxDepth < 0 || depth < maxDepth)) {
                                std::cout << std::setw(8) << formatSize(dirSize) << "\t";
                                if (showTime) {
                                    auto lwt = fs::last_write_time(entry.path());
                                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                        lwt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                                    auto time = std::chrono::system_clock::to_time_t(sctp);
                                    char timeBuf[64];
                                    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&time));
                                    std::cout << timeBuf << " ";
                                }
                                std::cout << entry.path().string() << "\n";
                            }
                        } else if (entry.is_regular_file()) {
                            uintmax_t fsize = fs::file_size(entry.path());
                            total += fsize;
                            if (showAll && !summary && (maxDepth < 0 || depth < maxDepth)) {
                                std::cout << std::setw(8) << formatSize(fsize) << "\t";
                                if (showTime) {
                                    auto lwt = fs::last_write_time(entry.path());
                                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                        lwt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                                    auto time = std::chrono::system_clock::to_time_t(sctp);
                                    char timeBuf[64];
                                    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&time));
                                    std::cout << timeBuf << " ";
                                }
                                std::cout << entry.path().string() << "\n";
                            }
                        }
                    }
                } catch (...) {}
                
                return total;
            };
            
            uintmax_t totalSize = calcSize(fullPath, 0);
            std::cout << std::setw(8) << formatSize(totalSize) << "\t" << path << "\n";
            grandTotal += totalSize;
        }
        
        if (showTotal && paths.size() > 1) {
            std::cout << std::setw(8) << formatSize(grandTotal) << "\ttotal\n";
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
            runProcess(cmd);
            
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
            runProcess(cmd);
            
        } else if (subcmd == "search" || subcmd == "find") {
            if (args.size() < 3) {
                printError("Usage: lin search <query>");
                return;
            }
            
            std::string query = args[2];
            std::string tempFile = getPackagesFilePath() + ".tmp";
            
            std::string cmd = "winget search " + query + " --accept-source-agreements";
            runProcess(cmd);
            
            std::cout << "\n";
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Syncing found packages to aliases...";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            std::string captureCmd = "cmd /c winget search " + query + " --accept-source-agreements > \"" + tempFile + "\" 2>nul";
            runProcess(captureCmd);
            
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
            runProcess("winget upgrade");
            
        } else if (subcmd == "upgrade") {
            std::cout << "Upgrading all packages...\n\n";
            runProcess("winget upgrade --all --accept-package-agreements --accept-source-agreements");
            
        } else if (subcmd == "list") {
            runProcess("winget list");
            
        } else if (subcmd == "info" || subcmd == "show") {
            if (args.size() < 3) {
                printError("Usage: lin info <package>");
                return;
            }
            
            std::string package = resolvePackageName(args[2]);
            std::string cmd = "winget show " + package;
            runProcess(cmd);
            
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
        std::cout << "  lino";
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
        std::cout << "  ifconfig/ip";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Show network interfaces\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  hostname";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Show hostname (-i for IP)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  ping";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          Ping a host (-c count)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  traceroute";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Trace route to host\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  nslookup/dig";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "  DNS lookup with server info\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  curl/wget";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "     HTTP requests / download files\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  arp";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "           Show ARP table\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  ss/netstat";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "    Socket statistics (-t tcp, -u udp)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  nc/netcat";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "     TCP client/server (-l listen)\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  net show";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "      Scan WiFi networks\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  net connect";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Connect to WiFi network\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  net disconnect";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "Disconnect from WiFi\n";

        std::cout << "\n  Process Management (Extended):\n\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  pstree";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "        Show process tree\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  renice/nice";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "   Change process priority\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "  lsof";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "          List open files/handles\n";

        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << "\n  External tools: git, node, python, gcc, g++, make, etc.\n";
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
        std::cout << "      Edit crontab in lino\n";
        
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
        lastExitCode = 0;

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
        } else if (cmd == "lino") {
            std::string linoCmd = "lino.exe";
            if (tokens.size() > 1) {
                linoCmd += " \"" + resolvePath(tokens[1]) + "\"";
            }
            runProcess(linoCmd);
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
            Networking::ifconfig(tokens);
        } else if (cmd == "ss") {
            Networking::ss(tokens);
        } else if (cmd == "hostname") {
            Networking::hostname(tokens);
        } else if (cmd == "arp") {
            Networking::arp(tokens);
        } else if (cmd == "nc" || cmd == "netcat") {
            Networking::nc(tokens);
        } else if (cmd == "pstree") {
            DWORD pid = 0;
            if (tokens.size() > 1) { try { pid = std::stoul(tokens[1]); } catch (...) {} }
            ProcessManager::pstree(pid);
        } else if (cmd == "renice" || cmd == "nice") {
            if (tokens.size() < 3) {
                printError("Usage: renice <priority> -p <pid>");
            } else {
                int priority = 0;
                DWORD pid = 0;
                for (size_t i = 1; i < tokens.size(); i++) {
                    if (tokens[i] == "-p" && i + 1 < tokens.size()) {
                        try { pid = std::stoul(tokens[++i]); } catch (...) {}
                    } else if (tokens[i] == "-n" && i + 1 < tokens.size()) {
                        try { priority = std::stoi(tokens[++i]); } catch (...) {}
                    } else {
                        try { priority = std::stoi(tokens[i]); } catch (...) {}
                    }
                }
                if (pid > 0 && ProcessManager::setProcessPriority(pid, priority)) {
                    std::cout << "Priority of PID " << pid << " set to " << priority << std::endl;
                } else {
                    printError("Failed to set priority");
                }
            }
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
                std::cout << "  -e    Edit crontab in lino\n";
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
                    // Edit crontab with lino
                    char exePath[MAX_PATH];
                    GetModuleFileNameA(NULL, exePath, MAX_PATH);
                    fs::path linoPath = fs::path(exePath).parent_path() / "lino.exe";
                    
                    // Make sure crontab exists
                    if (!fs::exists(crontabPath)) {
                        std::ofstream ofs(crontabPath);
                        ofs << "# Linuxify crontab - edit scheduled tasks\n";
                        ofs << "# Format: min hour day month weekday command\n";
                        ofs << "# Example: 0 12 * * * echo Hello World\n";
                    }
                    
                    if (fs::exists(linoPath)) {
                        std::string cmdLine = "\"" + linoPath.string() + "\" \"" + crontabPath + "\"";
                        STARTUPINFOA si = {sizeof(si)};
                        PROCESS_INFORMATION pi;
                        if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                            WaitForSingleObject(pi.hProcess, INFINITE);
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);
                        }
                        
                        // Tell crond to reload
                        std::string response = sendToCrond("RELOAD");
                        if (response.empty()) {
                            std::cout << "Crontab saved. Note: crond is not running.\n";
                            std::cout << "Start it with: crond (or crond --install for auto-start)\n";
                        } else {
                            printSuccess("Crontab saved and reloaded.");
                        }
                    } else {
                        printError("lino not found. Edit manually: " + crontabPath);
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
            lastExitCode = 1;
        } catch (const std::invalid_argument& e) {
            printError("Invalid argument: " + std::string(e.what()));
            lastExitCode = 1;
        } catch (const std::out_of_range& e) {
            printError("Value out of range: " + std::string(e.what()));
            lastExitCode = 1;
        } catch (const std::runtime_error& e) {
            printError("Runtime error: " + std::string(e.what()));
            lastExitCode = 1;
        } catch (const std::exception& e) {
            printError("Error: " + std::string(e.what()));
            lastExitCode = 1;
        } catch (...) {
            printError("An unexpected error occurred.");
            lastExitCode = 1;
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
            lastExitCode = (int)exitCode;
            
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            DWORD err = GetLastError();
            printError("Failed to execute (error " + std::to_string(err) + ")");
            lastExitCode = 1;
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
            "lino", "lin", "registry", "history", "whoami", "echo", "env", 
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

    size_t findSinglePipe(const std::string& str, size_t start = 0) {
        for (size_t i = start; i < str.length(); i++) {
            if (str[i] == '|') {
                if (i + 1 < str.length() && str[i + 1] == '|') {
                    i++;
                    continue;
                }
                if (i > 0 && str[i - 1] == '|') {
                    continue;
                }
                return i;
            }
        }
        return std::string::npos;
    }

    std::string readHeredoc(const std::string& delimiter) {
        std::string content;
        std::string line;
        
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) break;
            
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (!trimmed.empty()) trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
            
            if (trimmed == delimiter) break;
            
            content += line + "\n";
        }
        return content;
    }

    int executeWithStdin(const std::string& cmdPart, const std::string& stdinContent) {
        char tempPath[MAX_PATH];
        char tempFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        GetTempFileNameA(tempPath, "lin", 0, tempFile);
        
        std::ofstream tf(tempFile);
        tf << stdinContent;
        tf.close();
        
        std::string fullCmd = "cmd /c \"cd /d \"" + currentDir + "\" && " + cmdPart + " < \"" + tempFile + "\"\"";
        
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
        strncpy_s(cmdBuffer, fullCmd.c_str(), sizeof(cmdBuffer) - 1);
        
        int exitCode = 0;
        if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code;
            GetExitCodeProcess(pi.hProcess, &code);
            exitCode = (int)code;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            exitCode = 1;
        }
        
        DeleteFileA(tempFile);
        return exitCode;
    }

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
            int result = runProcess("cmd /c " + fullCmd);
            (void)result;  // Suppress warning
            
            return true;
        }
        
        size_t hereStringPos = processedInput.find("<<<");
        size_t heredocPos = processedInput.find("<<");
        size_t inputRedirPos = processedInput.find("<");
        
        if (heredocPos != std::string::npos && heredocPos == hereStringPos) {
            heredocPos = std::string::npos;
        }
        if (inputRedirPos != std::string::npos && (inputRedirPos == heredocPos || inputRedirPos == hereStringPos || inputRedirPos + 1 == heredocPos)) {
            inputRedirPos = std::string::npos;
        }
        
        if (hereStringPos != std::string::npos) {
            std::string cmdPart = processedInput.substr(0, hereStringPos);
            std::string stringPart = processedInput.substr(hereStringPos + 3);
            
            cmdPart.erase(cmdPart.find_last_not_of(" \t") + 1);
            stringPart.erase(0, stringPart.find_first_not_of(" \t"));
            
            if (stringPart.front() == '"' || stringPart.front() == '\'') {
                char quote = stringPart.front();
                stringPart = stringPart.substr(1);
                size_t endQuote = stringPart.find(quote);
                if (endQuote != std::string::npos) {
                    stringPart = stringPart.substr(0, endQuote);
                }
            } else {
                size_t endSpace = stringPart.find_first_of(" \t");
                if (endSpace != std::string::npos) {
                    stringPart = stringPart.substr(0, endSpace);
                }
            }
            
            std::vector<std::string> tokens = tokenize(cmdPart);
            if (!tokens.empty()) {
                std::string& cmd = tokens[0];
                std::istringstream iss(stringPart);
                
                std::ostringstream capturedOutput;
                std::streambuf* oldCout = std::cout.rdbuf();
                std::cout.rdbuf(capturedOutput.rdbuf());
                
                if (cmd == "grep") cmdGrep(tokens, stringPart);
                else if (cmd == "wc") cmdWc(tokens, stringPart);
                else if (cmd == "head") cmdHead(tokens, stringPart);
                else if (cmd == "tail") cmdTail(tokens, stringPart);
                else if (cmd == "sort") cmdSort(tokens, stringPart);
                else if (cmd == "uniq") cmdUniq(tokens, stringPart);
                else if (cmd == "cut") cmdCut(tokens, stringPart);
                else if (cmd == "tr") cmdTr(tokens, stringPart);
                else if (cmd == "cat") {
                    std::cout << stringPart;
                    if (!stringPart.empty() && stringPart.back() != '\n') std::cout << '\n';
                }
                else {
                    std::cout.rdbuf(oldCout);
                    lastExitCode = executeWithStdin(cmdPart, stringPart + "\n");
                    return true;
                }
                
                std::cout.rdbuf(oldCout);
                std::cout << capturedOutput.str();
            }
            return true;
        }
        
        if (heredocPos != std::string::npos && (hereStringPos == std::string::npos || heredocPos < hereStringPos)) {
            std::string cmdPart = processedInput.substr(0, heredocPos);
            std::string delimPart = processedInput.substr(heredocPos + 2);
            
            cmdPart.erase(cmdPart.find_last_not_of(" \t") + 1);
            delimPart.erase(0, delimPart.find_first_not_of(" \t"));
            delimPart.erase(delimPart.find_last_not_of(" \t\r\n") + 1);
            
            if (delimPart.empty()) {
                printError("Syntax error: missing delimiter after <<");
                return true;
            }
            
            std::string heredocContent = readHeredoc(delimPart);
            
            std::vector<std::string> tokens = tokenize(cmdPart);
            if (!tokens.empty()) {
                std::string& cmd = tokens[0];
                
                if (cmd == "grep") cmdGrep(tokens, heredocContent);
                else if (cmd == "wc") cmdWc(tokens, heredocContent);
                else if (cmd == "cat") {
                    std::cout << heredocContent;
                }
                else {
                    lastExitCode = executeWithStdin(cmdPart, heredocContent);
                }
            }
            return true;
        }
        
        if (inputRedirPos != std::string::npos && inputRedirPos > 0) {
            bool isStderrRedir = (inputRedirPos > 0 && processedInput[inputRedirPos - 1] == '2');
            bool isAmpRedir = (inputRedirPos > 0 && processedInput[inputRedirPos - 1] == '&');
            
            if (!isStderrRedir && !isAmpRedir) {
                std::string cmdPart = processedInput.substr(0, inputRedirPos);
                std::string filePart = processedInput.substr(inputRedirPos + 1);
                
                cmdPart.erase(cmdPart.find_last_not_of(" \t") + 1);
                filePart.erase(0, filePart.find_first_not_of(" \t"));
                filePart.erase(filePart.find_last_not_of(" \t") + 1);
                
                if (filePart.empty()) {
                    printError("Syntax error: missing filename after <");
                    return true;
                }
                
                std::string inputFile = resolvePath(filePart);
                if (!fs::exists(inputFile)) {
                    printError("No such file: " + filePart);
                    lastExitCode = 1;
                    return true;
                }
                
                std::ifstream ifs(inputFile);
                std::string fileContent((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                ifs.close();
                
                std::vector<std::string> tokens = tokenize(cmdPart);
                if (!tokens.empty()) {
                    std::string& cmd = tokens[0];
                    
                    if (cmd == "grep") cmdGrep(tokens, fileContent);
                    else if (cmd == "wc") cmdWc(tokens, fileContent);
                    else if (cmd == "cat") {
                        std::cout << fileContent;
                    }
                    else {
                        lastExitCode = executeWithStdin(cmdPart, fileContent);
                    }
                }
                return true;
            }
        }
        
        size_t appendPos = processedInput.find(">>");
        size_t writePos = processedInput.find(">");
        size_t pipePos = findSinglePipe(processedInput);
        
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
            
            while ((pipePos = findSinglePipe(remaining)) != std::string::npos) {
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
                
                if (i == 0) {
                    std::string cmdToRun = commands[i];
                    if (cmd == "ls" || cmd == "dir") {
                        if (cmdToRun.find("-1") == std::string::npos && cmdToRun.find(" -l") == std::string::npos) {
                            cmdToRun = cmd + " -1";
                            for (size_t j = 1; j < tokens.size(); j++) {
                                cmdToRun += " " + tokens[j];
                            }
                        }
                    }
                    pipedOutput = executeAndCapture(cmdToRun);
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
            
            std::cout << std::endl << pipedOutput << std::endl;
            return true;
        }
        
        return false;
    }

    int executeCommandLine(const std::string& cmdLine) {
        std::string trimmed = cmdLine;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (!trimmed.empty()) trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        if (trimmed.empty()) return 0;

        if (handleRedirection(trimmed)) {
            return lastExitCode;
        }

        std::vector<std::string> tokens = tokenize(trimmed);
        if (tokens.empty()) return 0;

        std::string& cmd = tokens[0];

        if (cmd.substr(0, 2) == "./" || cmd.substr(0, 2) == ".\\" ||
            cmd.find('/') != std::string::npos || cmd.find('\\') != std::string::npos ||
            (cmd.length() > 4 && cmd.substr(cmd.length() - 4) == ".exe")) {
            std::string execPath = cmd;
            if (cmd.substr(0, 2) == "./" || cmd.substr(0, 2) == ".\\") {
                execPath = cmd.substr(2);
            }
            runExecutable(execPath, tokens);
        } else {
            executeCommand(tokens);
        }
        return lastExitCode;
    }

    std::string expandHistoryInString(const std::string& input) {
        std::string result = input;
        size_t pos = 0;
        while ((pos = result.find("!!", pos)) != std::string::npos) {
            if (!commandHistory.empty()) {
                result.replace(pos, 2, commandHistory.back());
                pos += commandHistory.back().length();
            } else {
                pos += 2;
            }
        }
        return result;
    }

    bool handleChainedCommands(const std::string& input) {
        size_t orPos = input.find("||");
        size_t andPos = input.find("&&");
        
        if (orPos == std::string::npos && andPos == std::string::npos) {
            return false;
        }
        
        struct ChainPart {
            std::string cmd;
            int opType;
        };
        std::vector<ChainPart> parts;
        std::string remaining = input;
        
        while (true) {
            orPos = remaining.find("||");
            andPos = remaining.find("&&");
            
            size_t minPos = std::string::npos;
            int opType = 0;
            
            if (orPos != std::string::npos && (andPos == std::string::npos || orPos < andPos)) {
                minPos = orPos;
                opType = 1;
            } else if (andPos != std::string::npos) {
                minPos = andPos;
                opType = 2;
            }
            
            if (minPos == std::string::npos) {
                std::string cmd = remaining;
                cmd.erase(0, cmd.find_first_not_of(" \t"));
                if (!cmd.empty()) cmd.erase(cmd.find_last_not_of(" \t") + 1);
                parts.push_back({cmd, 0});
                break;
            }
            
            std::string cmd = remaining.substr(0, minPos);
            cmd.erase(0, cmd.find_first_not_of(" \t"));
            if (!cmd.empty()) cmd.erase(cmd.find_last_not_of(" \t") + 1);
            parts.push_back({cmd, opType});
            remaining = remaining.substr(minPos + 2);
        }
        
        if (parts.size() <= 1 && parts[0].opType == 0) {
            return false;
        }
        
        int exitCode = 0;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i == 0) {
                exitCode = executeCommandLine(parts[i].cmd);
            } else {
                int prevOp = parts[i - 1].opType;
                if (prevOp == 1 && exitCode != 0) {
                    exitCode = executeCommandLine(parts[i].cmd);
                } else if (prevOp == 2 && exitCode == 0) {
                    exitCode = executeCommandLine(parts[i].cmd);
                } else if (prevOp == 1 && exitCode == 0) {
                } else if (prevOp == 2 && exitCode != 0) {
                }
            }
        }
        
        return true;
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

            input = expandHistoryInString(input);

            if (input[0] == '!' && input.length() > 1 && input[1] != '!') {
                std::string expandedInput;
                bool expanded = false;
                
                if (input[1] == '-' && input.length() > 2) {
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
                    std::string prefix = input.substr(1);
                    for (auto it = commandHistory.rbegin(); it != commandHistory.rend(); ++it) {
                        if (it->substr(0, prefix.length()) == prefix) {
                            expandedInput = *it;
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
                    std::cout << expandedInput << std::endl;
                    input = expandedInput;
                }
            }

            // Save command to history (except for history command itself to avoid clutter)
            if (input.substr(0, 7) != "history") {
                saveToHistory(input);
            }

            if (handleChainedCommands(input)) {
                continue;
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

