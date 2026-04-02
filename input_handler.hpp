// g++ -std=c++17 main.cpp -o main.exe
#ifndef LINUXIFY_INPUT_HANDLER_HPP
#define LINUXIFY_INPUT_HANDLER_HPP

#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include "io_handler.hpp"
#include "signal_handler.hpp"
#include "cmds-src/auto-suggest.hpp"
#include "shell_streams.hpp"
#include <map>
#include <unordered_map>

namespace fs = std::filesystem;

namespace PromptConfig {
    inline std::string firstColor = "\033[92m";
    inline std::string secondColor = "\033[94m";
    inline std::string resetColor = "\033[0m";
}

class InputHandler {
private:
    std::string currentDir;
    std::vector<std::string> history;
    int historyIndex;
    
    // Internal State
    std::string inputBuffer;
    int cursorPos;
    int promptStartRow;
    int lastNumLines; // Track previous height to clear garbage only when shrinking
    int selectionAnchor; // -1 if no selection, otherwise index where selection started
    
    // Key Handling State
    char lastCharInput;
    std::chrono::time_point<std::chrono::steady_clock> lastTimeInput;
    bool initialized;
    bool isAdmin;
    std::function<bool(const std::string&)> commandValidator;

    // Auto-Suggestion State
    struct SuggestionEntry {
        std::string cmd;
        int frequency;
        int lastIndex; // Recency tie-breaker
        
        // For binary search comparison
        bool operator<(const SuggestionEntry& other) const {
            return cmd < other.cmd;
        }
        bool operator<(const std::string& otherCmd) const {
            return cmd < otherCmd;
        }
    };
    std::vector<SuggestionEntry> frequencyIndex;
    std::string currentSuggestion;
    
    enum GitStatus { GIT_NONE, GIT_CLEAN, GIT_STAGED, GIT_UNSTAGED };
    GitStatus currentGitStatus;

    GitStatus checkGitStatus() {
        // Fast check using git status --porcelain
        // We create a pipe to capture output
        SECURITY_ATTRIBUTES saAttr; 
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
        saAttr.bInheritHandle = TRUE; 
        saAttr.lpSecurityDescriptor = NULL; 

        HANDLE hChildStd_OUT_Rd = NULL;
        HANDLE hChildStd_OUT_Wr = NULL;

        if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) return GIT_CLEAN; // Asume clean on fail
        SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError = NULL; // Ignore stderr
        si.hStdOutput = hChildStd_OUT_Wr;
        si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        ZeroMemory(&pi, sizeof(pi));
        
        // Command: git status --porcelain
        std::string cmd = "git status --porcelain";
        char cmdBuf[128];
        strcpy(cmdBuf, cmd.c_str());

        // Create the child process. 
        if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, currentDir.c_str(), &si, &pi)) {
             CloseHandle(hChildStd_OUT_Wr);
             CloseHandle(hChildStd_OUT_Rd);
             return GIT_CLEAN;
        }

        CloseHandle(hChildStd_OUT_Wr); // Close write end so read ends

        // Read output
        std::string output;
        DWORD bytesRead;
        CHAR buffer[256];
        bool hasStaged = false;
        bool hasUnstaged = false;

        while (ReadFile(hChildStd_OUT_Rd, buffer, 255, &bytesRead, NULL) && bytesRead != 0) {
            output.append(buffer, bytesRead);
            if (output.length() > 2048) break; // Limit read
        }
        
        WaitForSingleObject(pi.hProcess, 100); // Wait bit
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hChildStd_OUT_Rd);

        if (output.empty()) return GIT_CLEAN;

        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.length() < 2) continue;
            char x = line[0];
            char y = line[1];
            
            if (x == '?' && y == '?') hasUnstaged = true; // Untracked
            else {
                if (x != ' ' && x != '?') hasStaged = true;
                if (y != ' ' && y != '?') hasUnstaged = true;
            }
        }

        if (hasUnstaged) return GIT_UNSTAGED;
        if (hasStaged) return GIT_STAGED;
        return GIT_CLEAN;
    }

    void rebuildSuggestions() {
        if (history.empty()) return;

        std::unordered_map<std::string, std::pair<int, int>> stats; // cmd -> {freq, lastIndex}
        for (int i = 0; i < (int)history.size(); ++i) {
            const std::string& cmd = history[i];
            if (cmd.empty()) continue;
            stats[cmd].first++;
            stats[cmd].second = i;
        }

        frequencyIndex.clear();
        frequencyIndex.reserve(stats.size());
        for (const auto& kv : stats) {
            frequencyIndex.push_back({kv.first, kv.second.first, kv.second.second});
        }
        
        // Sort alphabetically to allow binary search by prefix
        std::sort(frequencyIndex.begin(), frequencyIndex.end());
    }

    void updateSuggestion() {
        currentSuggestion.clear();
        if (inputBuffer.empty()) return;

        // Binary search for first command starting with inputBuffer
        auto it = std::lower_bound(frequencyIndex.begin(), frequencyIndex.end(), inputBuffer);
        
        int bestFreq = -1;
        int bestRecency = -1;
        const SuggestionEntry* bestMatch = nullptr;

        // Iterate while the command still starts with inputBuffer
        while (it != frequencyIndex.end()) {
            // Check prefix
            if (it->cmd.length() < inputBuffer.length() || 
                it->cmd.compare(0, inputBuffer.length(), inputBuffer) != 0) {
                break;
            }

            // Candidate found. Check if it's "better" (higher freq, or same freq & more recent)
            if (it->cmd != inputBuffer) { // Don't suggest what is already fully typed
                if (it->frequency > bestFreq || (it->frequency == bestFreq && it->lastIndex > bestRecency)) {
                    bestFreq = it->frequency;
                    bestRecency = it->lastIndex;
                    bestMatch = &(*it);
                }
            }
            ++it;
        }

        if (bestMatch) {
            currentSuggestion = bestMatch->cmd;
        } else {
             // Fallback: AutoSuggest (Filesystem)
             auto result = AutoSuggest::getSuggestions(inputBuffer, (int)inputBuffer.length(), currentDir);
             if (!result.suggestions.empty()) {
                 std::string best = result.suggestions[0];
                 
                 // Reconstruct full command line from suggestion
                 std::string prefix = inputBuffer.substr(0, result.replaceStart);
                 std::string token = inputBuffer.substr(result.replaceStart);
                 
                 if (result.isPath) {
                     // Token might be partial path, best is filename
                     // We need to keep the folder part of the token
                     fs::path p(token);
                     std::string parent = p.parent_path().string();
                     
                     // Add separator if parent exists and lacks it
                     if (!parent.empty()) {
                         // Windows/Linux separator check
                         char last = parent.back();
                         if (last != '/' && last != '\\') parent += "/";
                     } else if (token == "/" || token == "\\") {
                         // Root handling
                         parent = token;
                     } else if (token.find('/') != std::string::npos || token.find('\\') != std::string::npos) {
                          // Handle cases like "./" where parent_path returns "."
                          if (token.back() == '/' || token.back() == '\\') parent = token;
                     }
                     
                     currentSuggestion = prefix + parent + best;
                 } else {
                     // Command 
                     currentSuggestion = prefix + best;
                 }
             }
        }
    }

public:
    enum class PollResult {
        Continue,
        LineReady,
        Cancelled
    };

private:

    void copyToClipboard(const std::string& text) {
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (hg) {
                memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
                GlobalUnlock(hg);
                SetClipboardData(CF_TEXT, hg);
            }
            CloseClipboard();
        }
    }

    std::string getClipboardText() {
        if (!OpenClipboard(NULL)) return "";
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData == NULL) {
            CloseClipboard();
            return "";
        }
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText == NULL) {
            CloseClipboard();
            return "";
        }
        std::string text(pszText);
        GlobalUnlock(hData);
        CloseClipboard();
        
        // Remove carriage returns to keep single-line if mostly used for commands, 
        // or handle them. Shell usually expects single line or specific handling.
        // For now, let's strip \r to play nice with linux-style line endings if needed,
        // or just paste as is.
        // Actually, simple paste is fine. shell might handle \n later or we strip it.
        // Most shells strip newlines or execute immediately. 
        // Let's strip newlines for safety in this simple shell to avoid multi-command injection weirdness
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
        
        return text;
    }

    void deleteSelection() {
        if (selectionAnchor == -1) return;
        int start = std::min(selectionAnchor, cursorPos);
        int end = std::max(selectionAnchor, cursorPos);
        if (start != end) {
            inputBuffer.erase(start, end - start);
            cursorPos = start;
        }
        selectionAnchor = -1;
    }

    std::string getDisplayPath() {
        char* home = getenv("USERPROFILE");
        if (!home) return currentDir;
        
        std::string homePath = home;
        std::string dirLower = currentDir;
        std::string homeLower = homePath;
        std::transform(dirLower.begin(), dirLower.end(), dirLower.begin(), ::tolower);
        std::transform(homeLower.begin(), homeLower.end(), homeLower.begin(), ::tolower);
        
        if (dirLower == homeLower) {
            return "~";
        }
        
        if (homeLower.back() != '\\' && homeLower.back() != '/') {
            homeLower += '\\';
        }
        
        if (dirLower.length() > homeLower.length() && 
            dirLower.substr(0, homeLower.length()) == homeLower) {
            std::string relativePath = currentDir.substr(homePath.length());
            if (relativePath.front() == '\\' || relativePath.front() == '/') {
                relativePath = relativePath.substr(1);
            }
            for (char& c : relativePath) {
                if (c == '\\') c = '/';
            }
            return "~/" + relativePath;
        }
        
        return currentDir;
    }

    std::string getGitBranch() {
        namespace fs = std::filesystem;
        try {
            fs::path current(currentDir);
            while (true) {
                fs::path gitDir = current / ".git";
                if (fs::exists(gitDir) && fs::is_directory(gitDir)) {
                    fs::path headPath = gitDir / "HEAD";
                    std::ifstream headFile(headPath);
                    if (headFile.is_open()) {
                        std::string line;
                        std::getline(headFile, line);
                        if (line.substr(0, 5) == "ref: ") {
                            std::string ref = line.substr(5);
                            size_t lastSlash = ref.find_last_of('/');
                            if (lastSlash != std::string::npos) {
                                return ref.substr(lastSlash + 1);
                            }
                            return ref;
                        } else {
                            return line.substr(0, 7);
                        }
                    }
                }
                if (current.has_parent_path() && current.parent_path() != current) {
                    current = current.parent_path();
                } else {
                    break;
                }
            }
        } catch (...) {}
        return "";
    }

    void printPrompt() {
        std::string displayPath = getDisplayPath();
        
        IO::get().write(PromptConfig::firstColor);
        IO::get().write("linuxify[");
        IO::get().write(sessionName);
        IO::get().write("]");
        
        IO::get().write(PromptConfig::resetColor);
        IO::get().write(":");
        
        std::string branch = getGitBranch();
        if (!branch.empty()) {
             IO::get().write("\033[95m");
             IO::get().write(displayPath);
             
             IO::get().write(PromptConfig::resetColor);
             IO::get().write("@");
             
             std::string statusColor = "\033[93m";
             if (currentGitStatus == GIT_UNSTAGED) {
                 statusColor = "\033[91m";
             } else if (currentGitStatus == GIT_STAGED) {
                 statusColor = "\033[92m";
             }
             
             IO::get().write(statusColor);
             IO::get().write(branch);
        } else {
             IO::get().write(PromptConfig::secondColor);
             IO::get().write(displayPath);
        }
        
        IO::get().write(PromptConfig::resetColor);
        if (isAdmin) {
            IO::get().write("# ");
        } else {
            IO::get().write("$ ");
        }
    }

    void render() {
        IO::Console& io = IO::get();
        int width = io.getWidth();
        int height = io.getHeight();
        
        std::string displayPath = getDisplayPath();
        // Base length: "linuxify[" (9) + sessionName.length() + "]:" (2) + displayPath + "$ " (2)
        int promptLen = 11 + (int)sessionName.length() + (int)displayPath.length() + 2; 
        
        std::string branch = getGitBranch();
        if (!branch.empty()) {
            promptLen += 1 + (int)branch.length();
        }
        
        // Handle Scrolling (pre-calc)
        int startRow = promptStartRow;
        if (startRow < 0) startRow = 0;

        int cx = promptLen;
        int cy = startRow;
        for (size_t i = 0; i < inputBuffer.length(); i++) {
            if (inputBuffer[i] == '\n') {
                cx = 2; // Length of "> "
                cy++;
            } else {
                cx++;
                if (cx >= width) { cx = 0; cy++; }
            }
        }
        int numLines = cy - startRow + 1;

        int linesNeeded = startRow + numLines;
        if (linesNeeded > height) {
            int scrollAmount = linesNeeded - height;
            
            io.setColor(IO::Console::COLOR_DEFAULT);
            io.setCursorPos(0, (SHORT)(height - 1));
            for (int i = 0; i < scrollAmount; i++) {
                io.write("\n");
            }
            
            startRow = std::max(0, startRow - scrollAmount);
            promptStartRow = startRow; 
        }

        // Clear and Reset
        io.setCursorPos(0, (SHORT)startRow);
        printPrompt();

        // Syntax Highlighting Loop
        bool inQuotes = false;
        char quoteChar = '\0';
        bool isFirstToken = true;
        size_t tokenStart = 0;

        int selStart = -1, selEnd = -1;
        if (selectionAnchor != -1) {
            selStart = std::min(selectionAnchor, cursorPos);
            selEnd = std::max(selectionAnchor, cursorPos);
        }

        std::string firstTokenStr;
        if (!inputBuffer.empty()) {
            size_t firstSpace = inputBuffer.find(' ');
            if (firstSpace != std::string::npos) {
                firstTokenStr = inputBuffer.substr(0, firstSpace);
            } else {
                firstTokenStr = inputBuffer;
            }
        }
        
        bool isValidCommand = commandValidator ? commandValidator(firstTokenStr) : true;

        for (size_t i = 0; i < inputBuffer.length(); i++) {
            char c = inputBuffer[i];
            
            // Selection Highlight Override
            if (selStart != -1 && (int)i >= selStart && (int)i < selEnd) {
                io.setColor(BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                std::string s(1, c); io.write(s);
                continue; // Skip syntax highlighting for selected text
            }

            if (c == '\n') {
                io.clearFromCursor(); 
                io.setColor(IO::Console::COLOR_DEFAULT);
                io.write("\n> ");
                isFirstToken = true;
                if (inQuotes) io.setColor(IO::Console::COLOR_STRING);
                continue;
            }

            if ((c == '"' || c == '\'') && !inQuotes) {
                inQuotes = true;
                quoteChar = c;
                io.setColor(IO::Console::COLOR_STRING);
                std::string s(1, c); io.write(s);
                continue;
            }
            if (c == quoteChar && inQuotes) {
                std::string s(1, c); io.write(s);
                inQuotes = false;
                quoteChar = '\0';
                io.setColor(IO::Console::COLOR_DEFAULT);
                continue;
            }
            if (inQuotes) {
                std::string s(1, c); io.write(s);
                continue;
            }
            
            if (c == ' ') {
                io.setColor(IO::Console::COLOR_DEFAULT);
                io.write(" ");
                isFirstToken = false;
                tokenStart = i + 1;
                continue;
            }

            // Color Logic
            if (isFirstToken) {
                if (!isValidCommand) {
                    io.setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
                } else {
                    io.setColor(IO::Console::COLOR_COMMAND);
                }
            } else if (c == '-') {
                 io.setColor(IO::Console::COLOR_FLAG);
            } else {
                 bool isInFlag = false;
                 for (size_t j = tokenStart; j < i; j++) {
                     if (inputBuffer[j] == '-') { isInFlag = true; break; }
                 }
                 if (isInFlag) io.setColor(IO::Console::COLOR_FLAG);
                 else io.setColor(IO::Console::COLOR_ARG);
            }
            std::string s(1, c); io.write(s);
        }

        io.resetColor();

        int endCx = promptLen;
        int endCy = startRow;
        for (size_t i = 0; i < inputBuffer.length(); i++) {
            if (inputBuffer[i] == '\n') {
                endCx = 2;
                endCy++;
            } else {
                endCx++;
                if (endCx >= width) { endCx = 0; endCy++; }
            }
        }

        int ghostLines = 0;
        if (!currentSuggestion.empty() && currentSuggestion.length() > inputBuffer.length()) {
             std::string bufLower = inputBuffer;
             std::string sugLower = currentSuggestion.substr(0, inputBuffer.length());
             std::transform(bufLower.begin(), bufLower.end(), bufLower.begin(), ::tolower);
             std::transform(sugLower.begin(), sugLower.end(), sugLower.begin(), ::tolower);

             if (bufLower == sugLower) {
                 std::string suffix = currentSuggestion.substr(inputBuffer.length());
                 int remaining = width - endCx - 1; // Leave 1 char margin to prevent auto-scrolling
                 if (remaining <= 0) remaining = 0;
                 if ((int)suffix.length() > remaining) {
                     suffix = suffix.substr(0, remaining);
                 }
                 if (!suffix.empty()) {
                     io.setColor(IO::Console::COLOR_FAINT);
                     io.write(suffix);
                     io.resetColor();
                 }
             }
        }
        
        io.clearFromCursor();

        int totalVisualLines = numLines + ghostLines;
        if (lastNumLines > totalVisualLines) {
           io.clearArea(startRow + totalVisualLines, lastNumLines - totalVisualLines);
        }
        lastNumLines = totalVisualLines;

        // Set Cursor
        cx = promptLen;
        cy = startRow;
        for (int i = 0; i < cursorPos; i++) {
            if (inputBuffer[i] == '\n') {
                cx = 2; // Length of "> "
                cy++;
            } else {
                cx++;
                if (cx >= width) { cx = 0; cy++; }
            }
        }
        io.setCursorPos((SHORT)cx, (SHORT)cy);
    }
    std::string sessionName;

public:
    InputHandler(const std::string& cwd, const std::vector<std::string>& hist, bool admin = false, const std::string& sessName = "0") 
        : currentDir(cwd), history(hist), historyIndex(-1), cursorPos(0), lastNumLines(1), selectionAnchor(-1),
          lastCharInput(0), initialized(false), isAdmin(admin), commandValidator(nullptr), sessionName(sessName) {
        // Init row
        promptStartRow = IO::get().getCursorPos().Y;
        
        // Check Git Status once on init
        currentGitStatus = GIT_NONE;
        if (!getGitBranch().empty()) {
            currentGitStatus = checkGitStatus();
        }
        
        rebuildSuggestions(); // Build the frequency index logic
    }

    void setCommandValidator(std::function<bool(const std::string&)> validator) {
        commandValidator = validator;
    }

    std::string getInputBuffer() const { return inputBuffer; }

    PollResult poll() {
        if (!initialized) {
             // Initial Render
            render();
            ShellIO::sout.registerPromptCallback([this](){ 
                this->render(); 
            });
            ShellIO::sout.setPromptActive(true);
            
             // REMOVE ENABLE_PROCESSED_INPUT manually just in case
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode;
            GetConsoleMode(hIn, &mode);
            // We assume InputDispatcher has already set raw mode or equivalent
            
            lastTimeInput = std::chrono::steady_clock::now();
            initialized = true;
        }

        SignalHandler::signalHeartbeat();
        SignalHandler::poll();

        INPUT_RECORD ir;
        if (!SignalHandler::InputDispatcher::getInstance().getNextBufferedEvent(ir)) {
            Sleep(1); // Yield CPU (Reduced latency)
            return PollResult::Continue;
        }

        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) return PollResult::Continue;

        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
        char ch = ir.Event.KeyEvent.uChar.AsciiChar;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        
        // Unconditional Dead Key / Quote Fix
        // Check if the key maps to a quote character physically
        UINT mapped = MapVirtualKeyA(vk, 2) & 0xFF; // MAPVK_VK_TO_CHAR
        
        // Check for single quote (') or backtick (`)
        // We force the character if the key *physically* maps to it, ignoring dead state.
        if (mapped == '\'' || mapped == '"') { 
            ch = (ctrl & SHIFT_PRESSED) ? '"' : '\'';
        } 
        else if (mapped == '`' || mapped == '~') {
            ch = (ctrl & SHIFT_PRESSED) ? '~' : '`';
        }
        else if (mapped == 0 && (vk == VK_OEM_7 || vk == VK_OEM_3)) {
             // Fallback for layouts where MapVirtualKey might fail on dead keys
             if (vk == VK_OEM_7) ch = (ctrl & SHIFT_PRESSED) ? '"' : '\'';
             else if (vk == VK_OEM_3) ch = (ctrl & SHIFT_PRESSED) ? '~' : '`';
        }

        if (vk == VK_RETURN) {
            // Fix: Clear any ghost text/suggestions before moving to next line
            currentSuggestion.clear();
            
            // History Expansion: !! and !$ (only handle this if we're not inside a multiline string from the start)
            if (!history.empty() && !inputBuffer.empty()) {
                std::string expanded;
                size_t i = 0;
                bool changed = false;
                while (i < inputBuffer.length()) {
                    if (inputBuffer[i] == '!' && i + 1 < inputBuffer.length()) {
                        if (inputBuffer[i + 1] == '!') {
                            expanded += history.back();
                            changed = true;
                            i += 2;
                            continue;
                        } else if (inputBuffer[i + 1] == '$') {
                            std::string lastCmd = history.back();
                            // Find last token (space separated)
                            size_t lastSpace = lastCmd.find_last_of(" \t");
                            if (lastSpace != std::string::npos) {
                                expanded += lastCmd.substr(lastSpace + 1);
                            } else {
                                expanded += lastCmd;
                            }
                            changed = true;
                            i += 2;
                            continue;
                        }
                    }
                    expanded += inputBuffer[i];
                    i++;
                }
                
                if (changed) {
                    inputBuffer = expanded;
                }
            }

            // Check for unclosed quotes
            bool openSingle = false;
            bool openDouble = false;
            for (char c : inputBuffer) {
                if (c == '\'' && !openDouble) openSingle = !openSingle;
                else if (c == '"' && !openSingle) openDouble = !openDouble;
            }

            if (openSingle || openDouble) {
                inputBuffer += '\n'; // Add newline and continue accepting input
                cursorPos++;
                
                render(); 
                return PollResult::Continue;
            }

            render();
            
            ShellIO::sout.setPromptActive(false);
            ShellIO::sout << ShellIO::endl; 
            return PollResult::LineReady;
        } 
            else if (vk == 'A' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                selectionAnchor = 0;
                cursorPos = (int)inputBuffer.length();
                render();
            }
            else if (vk == 'L' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                IO::get().clearScreen();
                promptStartRow = 0;
                render();
            }
            else if (vk == VK_BACK) {
                lastCharInput = 0; 
                if (selectionAnchor != -1) {
                    deleteSelection();
                    updateSuggestion();
                    render();
                }
                else if ((ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && cursorPos > 0) {
                    // Ctrl+Backspace: Delete word left
                    int i = cursorPos - 1;
                    while (i > 0 && inputBuffer[i] == ' ') i--; // Skip trailing spaces
                    while (i > 0 && inputBuffer[i] != ' ') i--; // Skip word characters
                    if (inputBuffer[i] == ' ') i++; // Keep the boundary space
                    
                    int len = cursorPos - i;
                    inputBuffer.erase(i, len);
                    cursorPos = i;
                    updateSuggestion();
                    render();
                } 
                else if (cursorPos > 0) {
                    // Smart Backspace: Delete matched empty pair
                    if (cursorPos < (int)inputBuffer.length()) {
                        char prev = inputBuffer[cursorPos - 1];
                        char next = inputBuffer[cursorPos];
                        bool isPair = (prev == '(' && next == ')') ||
                                      (prev == '[' && next == ']') ||
                                      (prev == '{' && next == '}') ||
                                      (prev == '"' && next == '"') ||
                                      (prev == '\'' && next == '\'') ||
                                      (prev == '`' && next == '`');
                        if (isPair) {
                            inputBuffer.erase(cursorPos - 1, 2);
                            cursorPos--;
                            updateSuggestion();
                            render();
                            return PollResult::Continue; // Event handled
                        }
                    }

                    inputBuffer.erase(cursorPos - 1, 1);
                    cursorPos--;
                    updateSuggestion();
                    render();
                }
            }
            else if (vk == VK_DELETE) {
                lastCharInput = 0; 
                if (selectionAnchor != -1) {
                    deleteSelection();
                    updateSuggestion();
                    render();
                } else if (cursorPos < (int)inputBuffer.length()) {
                    inputBuffer.erase(cursorPos, 1);
                    updateSuggestion();
                    render();
                }
            }
            else if (vk == VK_HOME) {
                lastCharInput = 0;
                if (selectionAnchor != -1) selectionAnchor = -1;
                cursorPos = 0;
                render();
            }
            else if (vk == VK_END) {
                lastCharInput = 0;
                if (selectionAnchor != -1) selectionAnchor = -1;
                cursorPos = (int)inputBuffer.length();
                render();
            }
            else if (vk == VK_LEFT) {
                lastCharInput = 0; 
                if (selectionAnchor != -1) {
                    // Normalize cursor to start of selection
                    cursorPos = std::min(selectionAnchor, cursorPos);
                    selectionAnchor = -1;
                    render();
                } 
                else if (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
                    // Word Jump Left
                    if (cursorPos > 0) {
                        int i = cursorPos - 1;
                        while (i > 0 && inputBuffer[i] == ' ') i--; // Skip trailing spaces
                        while (i > 0 && inputBuffer[i] != ' ') i--; // Skip word characters
                        if (inputBuffer[i] == ' ') i++; // Stop at boundary
                        cursorPos = i;
                        render();
                    }
                }
                else if (cursorPos > 0) { 
                    cursorPos--; render(); 
                }
            }
            else if (vk == VK_RIGHT) {
                lastCharInput = 0; 
                // Accept Ghost Suggestion if at end of line and suggestion exists
                if (cursorPos == inputBuffer.length() && !currentSuggestion.empty()) {
                    inputBuffer = currentSuggestion;
                    cursorPos = (int)inputBuffer.length();
                    updateSuggestion(); // Likely clears it or finds next
                    render();
                }
                else if (selectionAnchor != -1) {
                    // Normalize cursor to end of selection
                    cursorPos = std::max(selectionAnchor, cursorPos);
                    selectionAnchor = -1;
                    render();
                } 
                else if (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
                    // Word Jump Right
                    if (cursorPos < (int)inputBuffer.length()) {
                        int i = cursorPos;
                        while (i < (int)inputBuffer.length() && inputBuffer[i] != ' ') i++; // Skip word chars
                        while (i < (int)inputBuffer.length() && inputBuffer[i] == ' ') i++; // Skip spaces
                        cursorPos = i;
                        render();
                    }
                }
                else if (cursorPos < (int)inputBuffer.length()) {
                     cursorPos++; render(); 
                } else {
                    // Accept Autosuggest
                    auto result = AutoSuggest::getSuggestions(inputBuffer, (int)inputBuffer.length(), currentDir);
                    if (!result.suggestions.empty()) {
                        std::string bestMatch = result.suggestions[0];
                        std::string suggestionSuffix;
                        
                        if (result.isPath) {
                            // For paths, get the completion part
                            std::string currentToken = inputBuffer.substr(result.replaceStart, result.replaceLength);
                            fs::path tokenPath(currentToken);
                            std::string prefix = tokenPath.filename().string();
                            std::string lowerPrefix = prefix;
                            std::string lowerBest = bestMatch;
                            std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
                            std::transform(lowerBest.begin(), lowerBest.end(), lowerBest.begin(), ::tolower);
                            
                            if (bestMatch.length() > prefix.length() && 
                                lowerBest.substr(0, prefix.length()) == lowerPrefix) {
                                suggestionSuffix = bestMatch.substr(prefix.length());
                            }
                        } else {
                            // For commands
                            std::string lowerInput = inputBuffer;
                            std::string lowerBest = bestMatch;
                            std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
                            std::transform(lowerBest.begin(), lowerBest.end(), lowerBest.begin(), ::tolower);
                            
                            if (bestMatch.length() > inputBuffer.length() && 
                                lowerBest.substr(0, inputBuffer.length()) == lowerInput) {
                                suggestionSuffix = bestMatch.substr(inputBuffer.length());
                            }
                        }
                        
                        if (!suggestionSuffix.empty()) {
                            inputBuffer += suggestionSuffix;
                            cursorPos = (int)inputBuffer.length();
                            render();
                        }
                    }
                }
            }
            else if (vk == VK_UP) {
                lastCharInput = 0;
                if (!history.empty() && historyIndex < (int)history.size() - 1) {
                    historyIndex++;
                    inputBuffer = history[history.size() - 1 - historyIndex];
                    cursorPos = (int)inputBuffer.length();
                    updateSuggestion();
                    render();
                }
            }
            else if (vk == VK_DOWN) {
                lastCharInput = 0;
                if (historyIndex > 0) {
                    historyIndex--;
                    inputBuffer = history[history.size() - 1 - historyIndex];
                    cursorPos = (int)inputBuffer.length();
                    updateSuggestion();
                    render();
                } else if (historyIndex == 0) {
                    historyIndex = -1;
                    inputBuffer.clear(); cursorPos = 0; 
                    updateSuggestion();
                    render();
                }
            }
            else if (vk == VK_TAB) {
                lastCharInput = 0;
                // Tab - auto-complete
                auto result = AutoSuggest::getSuggestions(inputBuffer, cursorPos, currentDir);
                
                if (!result.suggestions.empty()) {
                    if (result.suggestions.size() == 1) {
                        // Single match - complete it
                        std::string completion = result.suggestions[0];
                        if (result.isPath) {
                            // Replace from replaceStart to cursorPos
                            std::string before = inputBuffer.substr(0, result.replaceStart);
                            std::string after = (cursorPos < (int)inputBuffer.length()) ? inputBuffer.substr(cursorPos) : "";
                            
                            // Get parent path if any
                            std::string currentToken = inputBuffer.substr(result.replaceStart, result.replaceLength);
                            fs::path tokenPath(currentToken);
                            std::string parentPart;
                            if (!currentToken.empty() && currentToken.back() != '/' && currentToken.back() != '\\') {
                                fs::path parent = tokenPath.parent_path();
                                if (!parent.empty()) {
                                    parentPart = parent.string();
                                    if (parentPart.back() != '/' && parentPart.back() != '\\') {
                                        parentPart += "/";
                                    }
                                }
                            } else {
                                parentPart = currentToken;
                            }
                            
                            inputBuffer = before + parentPart + completion + after;
                            cursorPos = (int)(before.length() + parentPart.length() + completion.length());
                        } else {
                            // Command completion
                            std::string after = (cursorPos < (int)inputBuffer.length()) ? inputBuffer.substr(cursorPos) : "";
                            inputBuffer = completion + " " + after;
                            cursorPos = (int)completion.length() + 1;
                        }
                        render();
                    } else {
                        // Multiple matches - show them and complete to common prefix
                        std::cout << std::endl;
                        
                        // Display suggestions in columns
                        IO::Console& io = IO::get();
                        int termWidth = io.getWidth();
                        
                        size_t maxLen = 0;
                        for (const auto& s : result.suggestions) {
                            if (s.length() > maxLen) maxLen = s.length();
                        }
                        int colWidth = (int)maxLen + 2;
                        int numCols = std::max(1, termWidth / colWidth);
                        
                        int col = 0;
                        for (const auto& s : result.suggestions) {
                            io.setColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                            io.write(s);
                            io.resetColor();
                            
                            col++;
                            if (col >= numCols) {
                                io.write("\n");
                                col = 0;
                            } else {
                                int padding = colWidth - (int)s.length();
                                io.write(std::string(padding, ' '));
                            }
                        }
                        if (col != 0) io.write("\n");
                        
                        // Complete to common prefix
                        if (!result.completionText.empty() && result.completionText.length() > result.replaceLength) {
                            std::string before = inputBuffer.substr(0, result.replaceStart);
                            std::string after = (cursorPos < (int)inputBuffer.length()) ? inputBuffer.substr(cursorPos) : "";
                            inputBuffer = before + result.completionText + after;
                            cursorPos = (int)(before.length() + result.completionText.length());
                        }
                        
                        // Get new start row and re-render
                        promptStartRow = io.getCursorPos().Y;
                        printPrompt();
                        render();
                    }
                }
            }
            else if (vk == 'C' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                if (selectionAnchor != -1 && selectionAnchor != cursorPos) {
                    // Copy
                    int start = std::min(selectionAnchor, cursorPos);
                    int end = std::max(selectionAnchor, cursorPos);
                    copyToClipboard(inputBuffer.substr(start, end - start));
                } else {
                    currentSuggestion.clear();
                    render();
                    ShellIO::sout.setPromptActive(false);
                    ShellIO::sout << "^C" << ShellIO::endl;
                    inputBuffer.clear();
                    return PollResult::Cancelled;
                }
            }
            else if (vk == 'X' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                // Cut
                 if (selectionAnchor != -1 && selectionAnchor != cursorPos) {
                    int start = std::min(selectionAnchor, cursorPos);
                    int end = std::max(selectionAnchor, cursorPos);
                    copyToClipboard(inputBuffer.substr(start, end - start));
                    deleteSelection();
                    render();
                }
            }
            else if (vk == 'V' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                // Paste
                std::string text = getClipboardText();
                if (!text.empty()) {
                    if (selectionAnchor != -1) deleteSelection();
                    inputBuffer.insert(cursorPos, text);
                    cursorPos += (int)text.length();
                    updateSuggestion();
                    render();
                }
            }
            else if (ch >= 32 && ch < 127) {
                auto now = std::chrono::steady_clock::now();
                
                // If text selected, handle wrapping or deletion
                if (selectionAnchor != -1) {
                    char open = ch;
                    char close = 0;
                    if (open == '(') close = ')';
                    else if (open == '[') close = ']';
                    else if (open == '{') close = '}';
                    else if (open == '<') close = '>';
                    else if (open == '"') close = '"';
                    else if (open == '\'') close = '\'';
                    else if (open == '`') close = '`';
                    
                    if (close != 0) {
                        // WRAP Selection
                        int start = std::min(selectionAnchor, cursorPos);
                        int end = std::max(selectionAnchor, cursorPos);
                        int len = end - start;
                        
                        inputBuffer.insert(start, 1, open);
                        inputBuffer.insert(start + len + 1, 1, close);
                        
                        // Move cursor to end of wrap
                        cursorPos = start + len + 2; 
                        selectionAnchor = -1;
                        updateSuggestion();
                        render();
                        return PollResult::Continue;
                    }

                    deleteSelection();
                }

                bool handled = false;

                // 1. Type-Over (Skip) Logic
                // If typing a closer/quote that is already at cursor, just move past it.
                if (cursorPos < (int)inputBuffer.length()) {
                    char nextChar = inputBuffer[cursorPos];
                    if (nextChar == ch && (ch == ')' || ch == ']' || ch == '}' || 
                                           ch == '"' || ch == '\'' || ch == '`')) {
                        cursorPos++;
                        render();
                        handled = true;
                    }
                }

                if (!handled) {
                    // 2. Auto-Pairing Logic
                    char closing = 0;
                    if (ch == '(') closing = ')';
                    else if (ch == '[') closing = ']';
                    else if (ch == '{') closing = '}';
                    else if (ch == '"') closing = '"';
                    else if (ch == '\'') closing = '\'';
                    else if (ch == '`') closing = '`';

                    bool shouldPair = false;
                    if (closing != 0) {
                        // Quote Heuristic: Pair unless we are seemingly inside a word (e.g. don't)
                        bool isQuote = (ch == '"' || ch == '\'' || ch == '`');
                        if (!isQuote) {
                            shouldPair = true; // Brackets ALWAYS pair
                        } else {
                            // Default to pairing
                            shouldPair = true;
                            
                            // Don't pair if immediately following an alphanumeric char (e.g. "don't")
                            if (cursorPos > 0 && isalnum(inputBuffer[cursorPos-1])) {
                                shouldPair = false;
                            }
                            // Don't pair if immediately before an alphanumeric char (e.g. insert quote in "text")
                            if (cursorPos < (int)inputBuffer.length() && isalnum(inputBuffer[cursorPos])) {
                                shouldPair = false;
                            }
                        }
                    }

                    if (shouldPair) {
                        inputBuffer.insert(cursorPos, 1, ch);
                        inputBuffer.insert(cursorPos + 1, 1, closing);
                        cursorPos++; 
                    } else {
                        inputBuffer.insert(cursorPos, 1, ch);
                        cursorPos++;
                    }
                    
                    updateSuggestion();
                    render();
                    lastCharInput = ch;
                }
                lastTimeInput = now;
            }
        return PollResult::Continue;
    }
    
    // Static helper to just read a line


    // Static helper for simple input (prompts, password input) using the unified event loop
    static std::string readSimpleLine(const std::string& prompt = "", bool isPassword = false) {
        if (!prompt.empty()) {
            ShellIO::sout << prompt;
        }

        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD originalMode;
        GetConsoleMode(hIn, &originalMode);
        SetConsoleMode(hIn, ENABLE_PROCESSED_INPUT); 

        std::string buffer;
        int cursorPos = 0;

        while (true) {
            SignalHandler::signalHeartbeat();
            SignalHandler::poll();

            INPUT_RECORD ir;
            if (!SignalHandler::InputDispatcher::getInstance().getNextBufferedEvent(ir)) {
                Sleep(10);
                continue;
            }

            if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) continue;

            WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
            char ch = ir.Event.KeyEvent.uChar.AsciiChar;
            DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;

            if (vk == VK_RETURN) {
                ShellIO::sout << ShellIO::endl;
                break;
            }
            else if (vk == VK_BACK) {
                if (cursorPos > 0) {
                    if (!isPassword) {
                        ShellIO::sout << "\b \b";
                    }
                    buffer.erase(cursorPos - 1, 1);
                    cursorPos--;
                }
            }
            else if (vk == 'C' && (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))) {
                ShellIO::sout << "^C" << ShellIO::endl;
                SetConsoleMode(hIn, originalMode);
                SignalHandler::handleInterrupt(); // Propagate interrupt
                return ""; 
            }
            else if (ch >= 32 && ch < 127) {
                buffer.insert(cursorPos, 1, ch);
                cursorPos++;
                if (isPassword) {
                    ShellIO::sout << "*";
                } else {
                    ShellIO::sout << ch;
                }
            }
        }

        SetConsoleMode(hIn, originalMode);
        return buffer;
    }
};

#endif
