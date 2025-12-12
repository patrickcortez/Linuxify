// Compile: cl /EHsc /std:c++17 nano.cpp /Fe:nano.exe
// Alternate compile: g++ -std=c++17 -o nano nano.cpp

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <set>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <windows.h>
#include <conio.h>

namespace fs = std::filesystem;

// Syntax highlighting rule types
enum class RuleType { KEYWORD, PREPROCESSOR, SPECIAL_CHAR };

struct SyntaxRule {
    RuleType type;
    std::string pattern;
    WORD color;
};

struct LanguageSyntax {
    std::string extension;
    std::vector<SyntaxRule> rules;
    std::map<std::string, WORD> specialChars;  // e.g., "" -> yellow, {} -> blue
};

class NanoEditor {
private:
    std::vector<std::string> lines;
    std::string filename;
    std::string fileExtension;
    int cursorX;
    int cursorY;
    int scrollOffsetY;
    int scrollOffsetX;  // Horizontal scroll offset
    int screenWidth;
    int screenHeight;
    bool modified;
    bool running;
    HANDLE hConsole;
    std::string statusMessage;
    std::string cutBuffer;
    bool needsFullRedraw;
    
    // Screen buffer for efficient rendering
    std::vector<CHAR_INFO> screenBuffer;
    
    // Syntax highlighting
    std::map<std::string, LanguageSyntax> syntaxPlugins;  // extension -> syntax
    LanguageSyntax* currentSyntax = nullptr;
    std::set<std::string> keywords;  // Fast keyword lookup
    std::set<std::string> preprocessors;
    
    // Color name to Windows attribute mapping
    WORD parseColor(const std::string& colorName) {
        std::string lower = colorName;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        // Trim whitespace
        while (!lower.empty() && (lower.front() == ' ' || lower.front() == '\t')) lower.erase(0, 1);
        while (!lower.empty() && (lower.back() == ' ' || lower.back() == '\t' || lower.back() == ';')) lower.pop_back();
        
        if (lower == "red") return FOREGROUND_RED | FOREGROUND_INTENSITY;
        if (lower == "green") return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        if (lower == "blue") return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        if (lower == "yellow") return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        if (lower == "magenta" || lower == "purple") return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        if (lower == "cyan") return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        if (lower == "white") return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        if (lower == "gray" || lower == "grey") return FOREGROUND_INTENSITY;
        if (lower == "orange") return FOREGROUND_RED | FOREGROUND_GREEN;
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;  // Default white
    }
    
    // Trim whitespace from string
    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n;");
        return s.substr(start, end - start + 1);
    }
    
    // Parse a single .nano plugin file
    void parsePlugin(const std::string& path) {
        std::ifstream file(path);
        if (!file) return;
        
        std::string currentExtension;
        LanguageSyntax currentLang;
        bool inSection = false;
        std::map<std::string, WORD> globalSpecialChars;
        
        std::string line;
        while (std::getline(file, line)) {
            // Remove carriage return if present
            if (!line.empty() && line.back() == '\r') line.pop_back();
            
            std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;  // Skip empty/comments
            
            // Handle global "set `chars` = color;" statements
            if (trimmed.substr(0, 3) == "set" || trimmed.substr(0, 3) == "SET") {
                size_t tick1 = trimmed.find('`');
                size_t tick2 = trimmed.find('`', tick1 + 1);
                size_t eq = trimmed.find('=');
                if (tick1 != std::string::npos && tick2 != std::string::npos && eq != std::string::npos) {
                    std::string chars = trimmed.substr(tick1 + 1, tick2 - tick1 - 1);
                    std::string colorStr = trim(trimmed.substr(eq + 1));
                    WORD color = parseColor(colorStr);
                    globalSpecialChars[chars] = color;
                }
                continue;
            }
            
            // Handle Section [.ext]{ or section [.ext]{
            std::string lowerLine = trimmed;
            std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);
            if (lowerLine.substr(0, 7) == "section") {
                size_t bracket1 = trimmed.find('[');
                size_t bracket2 = trimmed.find(']');
                if (bracket1 != std::string::npos && bracket2 != std::string::npos) {
                    currentExtension = trimmed.substr(bracket1 + 1, bracket2 - bracket1 - 1);
                    currentLang = LanguageSyntax();
                    currentLang.extension = currentExtension;
                    currentLang.specialChars = globalSpecialChars;
                    inSection = true;
                }
                continue;
            }
            
            // Handle closing brace
            if (trimmed[0] == '}') {
                if (inSection && !currentExtension.empty()) {
                    syntaxPlugins[currentExtension] = currentLang;
                }
                inSection = false;
                currentExtension.clear();
                continue;
            }
            
            // Inside section - parse rules
            if (inSection) {
                size_t colonPos = trimmed.find(':');
                if (colonPos != std::string::npos) {
                    std::string ruleType = trim(trimmed.substr(0, colonPos));
                    std::string rest = trim(trimmed.substr(colonPos + 1));
                    
                    std::transform(ruleType.begin(), ruleType.end(), ruleType.begin(), ::tolower);
                    
                    // Parse "word, color" or "word,color"
                    size_t comma = rest.find(',');
                    if (comma != std::string::npos) {
                        std::string word = trim(rest.substr(0, comma));
                        std::string colorStr = trim(rest.substr(comma + 1));
                        WORD color = parseColor(colorStr);
                        
                        SyntaxRule rule;
                        rule.pattern = word;
                        rule.color = color;
                        
                        if (ruleType == "keyword") {
                            rule.type = RuleType::KEYWORD;
                        } else if (ruleType == "preprocessor") {
                            rule.type = RuleType::PREPROCESSOR;
                        } else {
                            rule.type = RuleType::KEYWORD;  // Default
                        }
                        
                        currentLang.rules.push_back(rule);
                    }
                }
            }
        }
        
        // Handle unclosed section
        if (inSection && !currentExtension.empty()) {
            syntaxPlugins[currentExtension] = currentLang;
        }
    }
    
    // Load all .nano plugins from plugins folder
    void loadPlugins() {
        // Find plugins directory relative to executable
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path pluginsDir = fs::path(exePath).parent_path() / "plugins";
        
        // Also check current directory
        fs::path localPlugins = fs::current_path() / "plugins";
        
        std::vector<fs::path> searchPaths = { pluginsDir, localPlugins };
        
        for (const auto& dir : searchPaths) {
            if (!fs::exists(dir)) continue;
            
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".nano") {
                        parsePlugin(entry.path().string());
                    }
                }
            }
        }
    }
    
    // Select syntax based on current file extension
    void selectSyntax() {
        currentSyntax = nullptr;
        keywords.clear();
        preprocessors.clear();
        
        if (fileExtension.empty()) return;
        
        auto it = syntaxPlugins.find(fileExtension);
        if (it != syntaxPlugins.end()) {
            currentSyntax = &it->second;
            // Build fast lookup sets
            for (const auto& rule : currentSyntax->rules) {
                if (rule.type == RuleType::KEYWORD) {
                    keywords.insert(rule.pattern);
                } else if (rule.type == RuleType::PREPROCESSOR) {
                    preprocessors.insert(rule.pattern);
                }
            }
            statusMessage = "Syntax: " + fileExtension;
        }
    }
    
    // Get color for a word based on syntax rules
    WORD getWordColor(const std::string& word) {
        if (!currentSyntax) return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        
        for (const auto& rule : currentSyntax->rules) {
            if (rule.pattern == word) {
                return rule.color;
            }
        }
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
    
    // Check if character is part of a word
    bool isWordChar(char c) {
        return std::isalnum(c) || c == '_' || c == '#';
    }
    
    void getTerminalSize() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            screenWidth = csbi.dwSize.X;
            screenHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        } else {
            screenWidth = 80;
            screenHeight = 25;
        }
        if (screenHeight < 10) screenHeight = 25;
    }

    void setCursorPosition(int x, int y) {
        COORD pos;
        pos.X = (SHORT)x;
        pos.Y = (SHORT)y;
        SetConsoleCursorPosition(hConsole, pos);
    }

    void hideCursor() {
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 25;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &info);
    }

    void showCursor() {
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 25;
        info.bVisible = TRUE;
        SetConsoleCursorInfo(hConsole, &info);
    }

    void setColor(WORD color) {
        SetConsoleTextAttribute(hConsole, color);
    }

    void resetColor() {
        setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    
    // Write a single character to screen buffer
    void bufferWriteChar(int x, int y, char c, WORD attr) {
        if (y < 0 || y >= screenHeight || x < 0 || x >= screenWidth) return;
        int idx = y * screenWidth + x;
        if (idx < (int)screenBuffer.size()) {
            screenBuffer[idx].Char.AsciiChar = c;
            screenBuffer[idx].Attributes = attr;
        }
    }
    
    // Write a string to the screen buffer at position
    void bufferWrite(int x, int y, const std::string& text, WORD attr) {
        if (y < 0 || y >= screenHeight) return;
        int idx = y * screenWidth + x;
        for (size_t i = 0; i < text.length() && x + (int)i < screenWidth; i++) {
            if (idx + (int)i < (int)screenBuffer.size()) {
                screenBuffer[idx + i].Char.AsciiChar = text[i];
                screenBuffer[idx + i].Attributes = attr;
            }
        }
    }
    
    // Fill a region with a character
    void bufferFill(int x, int y, int width, char c, WORD attr) {
        if (y < 0 || y >= screenHeight) return;
        int idx = y * screenWidth + x;
        for (int i = 0; i < width && x + i < screenWidth; i++) {
            if (idx + i < (int)screenBuffer.size()) {
                screenBuffer[idx + i].Char.AsciiChar = c;
                screenBuffer[idx + i].Attributes = attr;
            }
        }
    }
    
    // Flush the screen buffer to console (fast!)
    void flushBuffer() {
        COORD bufferSize = { (SHORT)screenWidth, (SHORT)screenHeight };
        COORD bufferCoord = { 0, 0 };
        SMALL_RECT writeRegion = { 0, 0, (SHORT)(screenWidth - 1), (SHORT)(screenHeight - 1) };
        WriteConsoleOutputA(hConsole, screenBuffer.data(), bufferSize, bufferCoord, &writeRegion);
    }

    void drawHeader() {
        WORD headerAttr = BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED | FOREGROUND_BLUE;
        
        std::string title = " NANO ";
        if (filename.empty()) {
            title += "[New Buffer]";
        } else {
            title += filename;
        }
        if (modified) {
            title += " *";
        }
        if (currentSyntax) {
            title += " [" + fileExtension + "]";
        }
        
        bufferFill(0, 0, screenWidth, ' ', headerAttr);
        bufferWrite(0, 0, title, headerAttr);
    }

    void drawFooter() {
        int line1 = screenHeight - 2;
        int line2 = screenHeight - 1;
        
        WORD bgWhite = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        WORD keyColor = bgWhite | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD textColor = bgWhite;
        
        // Line 1
        bufferFill(0, line1, screenWidth, ' ', textColor);
        int pos = 0;
        bufferWrite(pos, line1, "^X", keyColor); pos += 2;
        bufferWrite(pos, line1, " Exit  ", textColor); pos += 7;
        bufferWrite(pos, line1, "^O", keyColor); pos += 2;
        bufferWrite(pos, line1, " Save  ", textColor); pos += 7;
        bufferWrite(pos, line1, "^G", keyColor); pos += 2;
        bufferWrite(pos, line1, " Help  ", textColor); pos += 7;
        bufferWrite(pos, line1, "^K", keyColor); pos += 2;
        bufferWrite(pos, line1, " Cut   ", textColor); pos += 7;
        bufferWrite(pos, line1, "^U", keyColor); pos += 2;
        bufferWrite(pos, line1, " Paste ", textColor);
        
        // Line 2
        bufferFill(0, line2, screenWidth, ' ', textColor);
        pos = 0;
        bufferWrite(pos, line2, "^W", keyColor); pos += 2;
        bufferWrite(pos, line2, " Search  ", textColor); pos += 9;
        
        if (!statusMessage.empty()) {
            bufferWrite(pos, line2, statusMessage, bgWhite | FOREGROUND_RED | FOREGROUND_BLUE);
        }
        
        std::string posInfo = "L:" + std::to_string(cursorY + 1) + "/" + 
                              std::to_string(lines.size()) + " C:" + 
                              std::to_string(cursorX + 1);
        int infoStart = screenWidth - (int)posInfo.length() - 1;
        bufferWrite(infoStart, line2, posInfo, bgWhite | FOREGROUND_BLUE);
    }

    void drawContent() {
        int contentStart = 1;
        int contentEnd = screenHeight - 3;
        int contentHeight = contentEnd - contentStart + 1;
        
        WORD normalAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        WORD tildeAttr = FOREGROUND_BLUE;
        WORD stringAttr = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD commentAttr = FOREGROUND_INTENSITY;
        WORD numberAttr = FOREGROUND_RED | FOREGROUND_GREEN;
        
        for (int y = 0; y < contentHeight; y++) {
            int screenY = contentStart + y;
            int lineIdx = scrollOffsetY + y;
            
            if (lineIdx < (int)lines.size()) {
                const std::string& line = lines[lineIdx];
                
                // Apply horizontal scroll offset
                int startX = scrollOffsetX;
                int lineLen = (int)line.length();
                
                // Get visible portion of line
                std::string visiblePart;
                if (startX < lineLen) {
                    int endX = startX + screenWidth;
                    if (endX > lineLen) endX = lineLen;
                    visiblePart = line.substr(startX, endX - startX);
                }
                int printLen = (int)visiblePart.length();
                
                if (!currentSyntax) {
                    // No syntax highlighting
                    bufferWrite(0, screenY, visiblePart, normalAttr);
                    bufferFill(printLen, screenY, screenWidth - printLen, ' ', normalAttr);
                } else {
                    // Apply syntax highlighting to visible portion
                    // First, clear the line
                    bufferFill(0, screenY, screenWidth, ' ', normalAttr);
                    
                    // We process the FULL line to get correct syntax state, but only render visible part
                    int x = 0;  // Position in full line
                    bool inString = false;
                    char stringChar = '\0';
                    int fullLen = (int)line.length();
                    
                    while (x < fullLen) {
                        // Calculate screen position (only render if visible)
                        int screenX = x - scrollOffsetX;
                        bool isVisible = (screenX >= 0 && screenX < screenWidth);
                        
                        // Check for line comment (// or #)
                        if (!inString && x + 1 < fullLen && line[x] == '/' && line[x+1] == '/') {
                            // Rest of line is comment
                            for (int i = x; i < fullLen; i++) {
                                int sx = i - scrollOffsetX;
                                if (sx >= 0 && sx < screenWidth) {
                                    bufferWriteChar(sx, screenY, line[i], commentAttr);
                                }
                            }
                            break;
                        }
                        
                        // Check for # at start of line (preprocessor)
                        if (x == 0 && line[x] == '#') {
                            WORD preprocColor = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                            for (int i = 0; i < fullLen; i++) {
                                int sx = i - scrollOffsetX;
                                if (sx >= 0 && sx < screenWidth) {
                                    bufferWriteChar(sx, screenY, line[i], preprocColor);
                                }
                            }
                            break;
                        }
                        
                        // Check for string start/end
                        if ((line[x] == '"' || line[x] == '\'') && (x == 0 || line[x-1] != '\\')) {
                            if (!inString) {
                                inString = true;
                                stringChar = line[x];
                                if (isVisible) bufferWriteChar(screenX, screenY, line[x], stringAttr);
                                x++;
                                continue;
                            } else if (line[x] == stringChar) {
                                if (isVisible) bufferWriteChar(screenX, screenY, line[x], stringAttr);
                                inString = false;
                                stringChar = '\0';
                                x++;
                                continue;
                            }
                        }
                        
                        if (inString) {
                            if (isVisible) bufferWriteChar(screenX, screenY, line[x], stringAttr);
                            x++;
                            continue;
                        }
                        
                        // Check for numbers
                        if (std::isdigit(line[x])) {
                            if (isVisible) bufferWriteChar(screenX, screenY, line[x], numberAttr);
                            x++;
                            continue;
                        }
                        
                        // Check for special characters from plugin
                        bool foundSpecial = false;
                        for (const auto& pair : currentSyntax->specialChars) {
                            if (pair.first.find(line[x]) != std::string::npos) {
                                if (isVisible) bufferWriteChar(screenX, screenY, line[x], pair.second);
                                foundSpecial = true;
                                break;
                            }
                        }
                        if (foundSpecial) {
                            x++;
                            continue;
                        }
                        
                        // Check for keywords
                        if (isWordChar(line[x]) && (x == 0 || !isWordChar(line[x-1]))) {
                            // Extract word
                            int wordStart = x;
                            while (x < fullLen && isWordChar(line[x])) x++;
                            std::string word = line.substr(wordStart, x - wordStart);
                            
                            // Check if it's a keyword
                            WORD wordColor = normalAttr;
                            if (keywords.count(word) > 0) {
                                wordColor = getWordColor(word);
                            } else if (preprocessors.count(word) > 0) {
                                wordColor = getWordColor(word);
                            }
                            
                            // Write word (only visible portion)
                            for (int i = wordStart; i < x; i++) {
                                int sx = i - scrollOffsetX;
                                if (sx >= 0 && sx < screenWidth) {
                                    bufferWriteChar(sx, screenY, line[i], wordColor);
                                }
                            }
                            continue;
                        }
                        
                        // Default: normal character
                        if (isVisible) bufferWriteChar(screenX, screenY, line[x], normalAttr);
                        x++;
                    }
                }
                
                // Fill rest with spaces (already done by bufferFill at start for syntax, or here for non-syntax)
            } else {
                // Empty line with tilde
                bufferWrite(0, screenY, "~", tildeAttr);
                bufferFill(1, screenY, screenWidth - 1, ' ', normalAttr);
            }
        }
    }

    void refreshScreen() {
        hideCursor();
        getTerminalSize();
        
        // Resize buffer if needed
        int bufferSize = screenWidth * screenHeight;
        if ((int)screenBuffer.size() != bufferSize) {
            screenBuffer.resize(bufferSize);
            needsFullRedraw = true;
        }
        
        drawHeader();
        drawContent();
        drawFooter();
        
        // Flush entire buffer at once (very fast!)
        flushBuffer();
        
        int contentStart = 1;
        int displayY = contentStart + (cursorY - scrollOffsetY);
        int displayX = cursorX - scrollOffsetX;  // Account for horizontal scroll
        if (displayX < 0) displayX = 0;
        if (displayX >= screenWidth) displayX = screenWidth - 1;
        
        setCursorPosition(displayX, displayY);
        showCursor();
    }

    void ensureCursorVisible() {
        int contentHeight = screenHeight - 4;
        if (contentHeight < 1) contentHeight = 1;
        
        // Vertical scrolling
        if (cursorY < scrollOffsetY) {
            scrollOffsetY = cursorY;
        }
        if (cursorY >= scrollOffsetY + contentHeight) {
            scrollOffsetY = cursorY - contentHeight + 1;
        }
        if (scrollOffsetY < 0) {
            scrollOffsetY = 0;
        }
        
        // Horizontal scrolling - keep cursor 5 chars from edge when possible
        int margin = 5;
        int viewWidth = screenWidth - margin;
        if (viewWidth < 10) viewWidth = screenWidth;
        
        if (cursorX < scrollOffsetX) {
            scrollOffsetX = cursorX;
        }
        if (cursorX >= scrollOffsetX + viewWidth) {
            scrollOffsetX = cursorX - viewWidth + 1;
        }
        if (scrollOffsetX < 0) {
            scrollOffsetX = 0;
        }
    }

    void moveCursorUp() {
        if (cursorY > 0) {
            cursorY--;
            int lineLen = (int)lines[cursorY].length();
            if (cursorX > lineLen) cursorX = lineLen;
            ensureCursorVisible();
        }
    }

    void moveCursorDown() {
        if (cursorY < (int)lines.size() - 1) {
            cursorY++;
            int lineLen = (int)lines[cursorY].length();
            if (cursorX > lineLen) cursorX = lineLen;
            ensureCursorVisible();
        }
    }

    void moveCursorLeft() {
        if (cursorX > 0) {
            cursorX--;
        } else if (cursorY > 0) {
            cursorY--;
            cursorX = (int)lines[cursorY].length();
            ensureCursorVisible();
        }
    }

    void moveCursorRight() {
        int lineLen = (int)lines[cursorY].length();
        if (cursorX < lineLen) {
            cursorX++;
        } else if (cursorY < (int)lines.size() - 1) {
            cursorY++;
            cursorX = 0;
            ensureCursorVisible();
        }
    }

    void insertChar(char c) {
        if (lines.empty()) lines.push_back("");
        
        // Auto-wrap: if at screen edge, create new line first
        if (cursorX >= screenWidth - 1) {
            // Move cursor to next line (like pressing Enter)
            std::string rest = lines[cursorY].substr(cursorX);
            lines[cursorY] = lines[cursorY].substr(0, cursorX);
            lines.insert(lines.begin() + cursorY + 1, rest);
            cursorY++;
            cursorX = 0;
            scrollOffsetX = 0;  // Reset horizontal scroll
            ensureCursorVisible();
        }
        
        lines[cursorY].insert(cursorX, 1, c);
        cursorX++;
        modified = true;
    }

    void insertNewLine() {
        if (lines.empty()) lines.push_back("");
        std::string rest = lines[cursorY].substr(cursorX);
        lines[cursorY] = lines[cursorY].substr(0, cursorX);
        lines.insert(lines.begin() + cursorY + 1, rest);
        cursorY++;
        cursorX = 0;
        modified = true;
        ensureCursorVisible();
    }

    void deleteChar() {
        if (cursorX > 0) {
            lines[cursorY].erase(cursorX - 1, 1);
            cursorX--;
            modified = true;
            // Reset horizontal scroll if cursor is now visible without it
            if (cursorX < screenWidth - 5) {
                scrollOffsetX = 0;
            }
        } else if (cursorY > 0) {
            // Merging with previous line
            int prevLineLen = (int)lines[cursorY - 1].length();
            lines[cursorY - 1] += lines[cursorY];
            lines.erase(lines.begin() + cursorY);
            cursorY--;
            cursorX = prevLineLen;
            modified = true;
            
            // Reset horizontal scroll - we want cursor visible
            scrollOffsetX = 0;
            if (cursorX >= screenWidth - 5) {
                // Cursor is beyond screen, need to scroll
                scrollOffsetX = cursorX - screenWidth + 10;
                if (scrollOffsetX < 0) scrollOffsetX = 0;
            }
            ensureCursorVisible();
        }
    }

    void deleteCharForward() {
        int lineLen = (int)lines[cursorY].length();
        if (cursorX < lineLen) {
            lines[cursorY].erase(cursorX, 1);
            modified = true;
        } else if (cursorY < (int)lines.size() - 1) {
            lines[cursorY] += lines[cursorY + 1];
            lines.erase(lines.begin() + cursorY + 1);
            modified = true;
        }
    }

    void cutLine() {
        if (lines.empty()) return;
        cutBuffer = lines[cursorY];
        lines.erase(lines.begin() + cursorY);
        if (lines.empty()) lines.push_back("");
        if (cursorY >= (int)lines.size()) cursorY = (int)lines.size() - 1;
        cursorX = 0;
        modified = true;
        statusMessage = "Cut line";
        ensureCursorVisible();
    }

    void pasteLine() {
        if (cutBuffer.empty()) {
            statusMessage = "Buffer empty";
            return;
        }
        lines.insert(lines.begin() + cursorY, cutBuffer);
        cursorX = 0;
        modified = true;
        statusMessage = "Pasted";
    }

    std::string promptInput(const std::string& prompt) {
        int promptY = screenHeight - 2;
        setCursorPosition(0, promptY);
        setColor(BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED);
        std::cout << ' ' << prompt << ' ';
        int inputStart = (int)prompt.length() + 2;
        for (int i = inputStart; i < screenWidth; i++) std::cout << ' ';
        
        setCursorPosition(inputStart, promptY);
        showCursor();
        
        std::string input;
        while (true) {
            int ch = _getch();
            if (ch == 13) break;
            if (ch == 27) { resetColor(); return ""; }
            if (ch == 8 && !input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
            } else if (ch >= 32 && ch < 127) {
                input += (char)ch;
                std::cout << (char)ch;
            }
        }
        resetColor();
        return input;
    }

    void saveFile() {
        std::string saveName = filename;
        if (saveName.empty()) {
            saveName = promptInput("Filename:");
            if (saveName.empty()) {
                statusMessage = "Cancelled";
                return;
            }
        }
        
        std::ofstream ofs(saveName);
        if (!ofs) {
            statusMessage = "Error saving!";
            return;
        }
        
        for (size_t i = 0; i < lines.size(); i++) {
            ofs << lines[i];
            if (i < lines.size() - 1) ofs << '\n';
        }
        
        filename = saveName;
        modified = false;
        statusMessage = "Saved";
        
        // Update extension and syntax on first save
        fs::path p(filename);
        if (p.has_extension()) {
            fileExtension = p.extension().string();
            selectSyntax();
        }
    }

    void search() {
        std::string query = promptInput("Search:");
        if (query.empty()) {
            statusMessage = "";
            return;
        }
        
        for (int y = cursorY; y < (int)lines.size(); y++) {
            size_t start = (y == cursorY) ? cursorX + 1 : 0;
            size_t pos = lines[y].find(query, start);
            if (pos != std::string::npos) {
                cursorY = y;
                cursorX = (int)pos;
                ensureCursorVisible();
                statusMessage = "Found";
                return;
            }
        }
        for (int y = 0; y <= cursorY; y++) {
            size_t pos = lines[y].find(query);
            if (pos != std::string::npos) {
                cursorY = y;
                cursorX = (int)pos;
                ensureCursorVisible();
                statusMessage = "Found (wrapped)";
                return;
            }
        }
        statusMessage = "Not found";
    }

    void showHelp() {
        system("cls");
        std::cout << "\n  NANO HELP\n";
        std::cout << "  =========\n\n";
        std::cout << "  Arrow Keys  - Move cursor\n";
        std::cout << "  Home/End    - Start/end of line\n";
        std::cout << "  Page Up/Dn  - Scroll page\n";
        std::cout << "  Backspace   - Delete before cursor\n";
        std::cout << "  Delete      - Delete at cursor\n";
        std::cout << "  Enter       - New line\n\n";
        std::cout << "  Ctrl+X      - Exit\n";
        std::cout << "  Ctrl+O      - Save\n";
        std::cout << "  Ctrl+K      - Cut line\n";
        std::cout << "  Ctrl+U      - Paste line\n";
        std::cout << "  Ctrl+W      - Search\n";
        std::cout << "  Ctrl+G      - This help\n\n";
        std::cout << "  SYNTAX HIGHLIGHTING\n";
        std::cout << "  Place .nano files in plugins/ folder\n\n";
        std::cout << "  Press any key to continue...";
        _getch();
    }

    bool confirmExit() {
        if (!modified) return true;
        statusMessage = "Save? (Y/N/C)";
        refreshScreen();
        
        while (true) {
            int ch = _getch();
            if (ch == 'y' || ch == 'Y') { saveFile(); return true; }
            if (ch == 'n' || ch == 'N') return true;
            if (ch == 'c' || ch == 'C' || ch == 27) { statusMessage = ""; return false; }
        }
    }

    // Process a single input character (returns true if should continue processing)
    bool processSingleInput(int ch) {
        statusMessage = "";
        
        if (ch == 0 || ch == 224) {
            ch = _getch();
            switch (ch) {
                case 72: moveCursorUp(); break;
                case 80: moveCursorDown(); break;
                case 75: moveCursorLeft(); break;
                case 77: moveCursorRight(); break;
                case 71: cursorX = 0; break;
                case 79: cursorX = (int)lines[cursorY].length(); break;
                case 73: for (int i = 0; i < screenHeight - 4; i++) moveCursorUp(); break;
                case 81: for (int i = 0; i < screenHeight - 4; i++) moveCursorDown(); break;
                case 83: deleteCharForward(); break;
            }
        } else if (ch == 24) {
            if (confirmExit()) running = false;
        } else if (ch == 15) {
            saveFile();
        } else if (ch == 7) {
            showHelp();
            return false; // Force full redraw after help
        } else if (ch == 11) {
            cutLine();
        } else if (ch == 21) {
            pasteLine();
        } else if (ch == 23) {
            search();
        } else if (ch == 8) {
            deleteChar();
        } else if (ch == 13) {
            insertNewLine();
        } else if (ch == 9) {
            for (int i = 0; i < 4; i++) insertChar(' ');
        } else if (ch >= 32 && ch < 127) {
            insertChar((char)ch);
        }
        return true;
    }

    void loadFile(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs) {
            lines.push_back("");
            statusMessage = "New file";
            return;
        }
        
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (lines.empty()) lines.push_back("");
        statusMessage = "Loaded " + std::to_string(lines.size()) + " lines";
    }

public:
    NanoEditor(const std::string& filepath = "") 
        : filename(filepath), cursorX(0), cursorY(0), scrollOffsetY(0), scrollOffsetX(0),
          screenWidth(80), screenHeight(25), modified(false), running(true),
          needsFullRedraw(true) {
        
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        getTerminalSize();
        screenBuffer.resize(screenWidth * screenHeight);
        
        // Load syntax plugins
        loadPlugins();
        
        // Extract file extension
        if (!filepath.empty()) {
            fs::path p(filepath);
            if (p.has_extension()) {
                fileExtension = p.extension().string();
            }
            loadFile(filepath);
        } else {
            lines.push_back("");
            statusMessage = "New buffer";
        }
        
        // Select syntax based on extension
        selectSyntax();
    }

    void run() {
        system("cls");
        
        // Initial screen refresh before waiting for input
        refreshScreen();
        
        while (running) {
            // Process ALL pending input before refreshing screen
            // This handles paste operations as a batch
            bool hasInput = _kbhit();
            
            if (hasInput) {
                // Process all available input without refreshing
                while (_kbhit()) {
                    int ch = _getch();
                    if (!processSingleInput(ch)) {
                        break; // Force refresh (e.g., after help)
                    }
                }
            } else {
                // No input pending, wait for input
                int ch = _getch();
                processSingleInput(ch);
            }
            
            // Refresh screen after processing all pending input
            refreshScreen();
        }
        
        system("cls");
    }
};

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    std::string filename;
    if (argc > 1) filename = argv[1];
    
    NanoEditor editor(filename);
    editor.run();
    
    return 0;
}
