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

namespace fs = std::filesystem;

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

    void printPrompt() {
        IO::get().setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        IO::get().write("linuxify");
        
        IO::get().setColor(IO::Console::COLOR_DEFAULT);
        IO::get().write(":");
        
        IO::get().setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        IO::get().write(currentDir);
        
        IO::get().setColor(IO::Console::COLOR_DEFAULT);
        IO::get().write("$ ");
    }

    void render() {
        IO::Console& io = IO::get();
        int width = io.getWidth();
        int height = io.getHeight();
        
        // Calculate dimensions
        int promptLen = 9 + (int)currentDir.length() + 2; 
        int totalLen = promptLen + (int)inputBuffer.length();
        int numLines = (totalLen + width - 1) / width;
        if (numLines < 1) numLines = 1;

        // Handle Scrolling
        int startRow = promptStartRow;
        if (startRow < 0) startRow = 0;
        
        int linesNeeded = startRow + numLines;
        if (linesNeeded > height) {
            int scrollAmount = linesNeeded - height;
            startRow = std::max(0, startRow - scrollAmount);
            promptStartRow = startRow; 
        }

        // Clear and Reset
        // NO - causes flicker: io.clearArea(startRow, numLines);
        io.setCursorPos(0, (SHORT)startRow);
        printPrompt();

        // if (inputBuffer.empty()) return; - REMOVED to ensure clearing happens

        // Syntax Highlighting Loop
        bool inQuotes = false;
        char quoteChar = '\0';
        bool isFirstToken = true;
        size_t tokenStart = 0;

        for (size_t i = 0; i < inputBuffer.length(); i++) {
            char c = inputBuffer[i];
            
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
                io.setColor(IO::Console::COLOR_COMMAND);
            } else if (c == '-') {
                 io.setColor(IO::Console::COLOR_FLAG);
            } else {
                 // Simple heuristic: if previous char was flag color, keep flag color? 
                 // Sticking to robust logic from main.cpp: check if we are in a token starting with -
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

        // Autosuggest Faint Text
        if (!inputBuffer.empty()) {
            auto result = AutoSuggest::getSuggestions(inputBuffer, (int)inputBuffer.length(), currentDir);
            if (!result.suggestions.empty()) {
                std::string best = result.suggestions[0];
                std::string suffix;
                
                if (result.isPath) {
                    std::string currentToken = inputBuffer.substr(result.replaceStart, result.replaceLength);
                    fs::path tokenPath(currentToken);
                    std::string prefix = tokenPath.filename().string();
                     if (best.length() > prefix.length() && 
                            best.substr(0, prefix.length()) == prefix) { 
                            suffix = best.substr(prefix.length());
                    }
                } else {
                     if (best.length() > inputBuffer.length() && 
                        best.substr(0, inputBuffer.length()) == inputBuffer) {
                        suffix = best.substr(inputBuffer.length());
                    }
                }
                
                if (!suffix.empty()) {
                    io.setColor(IO::Console::COLOR_FAINT);
                    io.write(suffix);
                    io.resetColor();
                }
            }
        }
        
        // 4. Clear Remainder of Line
        io.clearFromCursor();

        // 5. Clear extra lines if we shrank
        if (lastNumLines > numLines) {
           io.clearArea(startRow + numLines, lastNumLines - numLines);
        }
        lastNumLines = numLines;

        // Set Cursor
        int totalCursor = promptLen + cursorPos;
        SHORT cx = (SHORT)(totalCursor % width);
        SHORT cy = (SHORT)(startRow + totalCursor / width);
        io.setCursorPos(cx, cy);
    }

public:
    InputHandler(const std::string& cwd, const std::vector<std::string>& hist) 
        : currentDir(cwd), history(hist), historyIndex(-1), cursorPos(0), lastNumLines(1) {
        // Init row
        promptStartRow = IO::get().getCursorPos().Y;
    }

    std::string readLine() {
        // Initial Render
        render();

        // Register callback for prompt stomping protection
        ShellIO::sout.registerPromptCallback([this](){ 
            // Save cursor is handled by render logic mostly, but we might need to ensure connection
            this->render(); 
        });
        ShellIO::sout.setPromptActive(true);

        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD originalMode;
        GetConsoleMode(hIn, &originalMode);
        SetConsoleMode(hIn, ENABLE_PROCESSED_INPUT); // Raw-ish mode

        // Double-tap state
        char lastCharInput = 0;
        auto lastTimeInput = std::chrono::steady_clock::now();

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
                ShellIO::sout.setPromptActive(false);
                ShellIO::sout << ShellIO::endl; 
                break;
            } 
            else if (vk == VK_BACK) {
                lastCharInput = 0; // Reset double tap
                if (cursorPos > 0) {
                    inputBuffer.erase(cursorPos - 1, 1);
                    cursorPos--;
                    render();
                }
            }
            else if (vk == VK_DELETE) {
                lastCharInput = 0; // Reset double tap
                if (cursorPos < (int)inputBuffer.length()) {
                    inputBuffer.erase(cursorPos, 1);
                    render();
                }
            }
            else if (vk == VK_LEFT) {
                lastCharInput = 0; // Reset double tap
                if (cursorPos > 0) { cursorPos--; render(); }
            }
            else if (vk == VK_RIGHT) {
                lastCharInput = 0; // Reset double tap
                if (cursorPos < (int)inputBuffer.length()) {
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
                    render();
                }
            }
            else if (vk == VK_DOWN) {
                lastCharInput = 0;
                if (historyIndex > 0) {
                    historyIndex--;
                    inputBuffer = history[history.size() - 1 - historyIndex];
                    cursorPos = (int)inputBuffer.length();
                    render();
                } else if (historyIndex == 0) {
                    historyIndex = -1;
                    inputBuffer.clear(); cursorPos = 0; render();
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
                std::cout << "^C\n";
                inputBuffer.clear();
                ShellIO::sout.setPromptActive(false);
                SetConsoleMode(hIn, originalMode);
                return ""; // Cancelled
            }
            else if (ch >= 32 && ch < 127) {
                // Double tap detection
                auto now = std::chrono::steady_clock::now();
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTimeInput).count();
                
                bool handled = false;
                if (ch == lastCharInput && diff < 500) { // 500ms window
                    char closing = 0;
                    if (ch == '"') closing = '"';
                    else if (ch == '\'') closing = '\'';
                    else if (ch == '(') closing = ')';
                    else if (ch == '[') closing = ']';
                    else if (ch == '{') closing = '}';
                    
                    if (closing != 0) {
                        // Insert closing char at cursor (cursor is essentially between the pair now)
                        inputBuffer.insert(cursorPos, 1, closing);
                        // Do NOT advance cursor, so it stays inside
                        render();
                        lastCharInput = 0; // Prevent triple-tap issues
                        handled = true;
                    }
                }
                
                if (!handled) {
                    inputBuffer.insert(cursorPos, 1, ch);
                    cursorPos++;
                    render();
                    lastCharInput = ch;
                    lastTimeInput = now;
                }
            }
        }

        ShellIO::sout.setPromptActive(false);
        SetConsoleMode(hIn, originalMode);
        return inputBuffer;
    }
    
    // Static helper to just read a line
    static std::string read(const std::string& cwd, const std::vector<std::string>& hist) {
        InputHandler handler(cwd, hist);
        return handler.readLine();
    }

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
