// Compile: g++ -std=c++17 -static -o cmds/Lino.exe Lino.cpp

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
#include <deque>

namespace fs = std::filesystem;

// Syntax highlighting rule types
enum class RuleType { KEYWORD, PREPROCESSOR, SPECIAL_CHAR, COMMENT };

struct SyntaxRule {
    RuleType type;
    std::string pattern;
    WORD color;
};

struct PatternRule {
    std::string name;
    std::vector<char> triggers;
    std::string format;  // e.g., "%s@%w.%w"
    WORD color;
};

struct ContextPatternGroup {
    std::string name;  // e.g., "singles", "sections"
    std::vector<std::string> patterns;  // Each pattern like "out(<freetext>);"
};

struct ContextRule {
    std::string name;
    WORD errorColor;
    std::map<std::string, std::vector<std::string>> valueGroups;
    std::vector<std::string> keywords;
    std::vector<std::string> specials;
    std::vector<ContextPatternGroup> patternGroups;  // Named pattern groups
};

struct LanguageSyntax {
    std::string extension;
    std::vector<SyntaxRule> rules;
    std::map<std::string, WORD> specialChars;
    std::string commentPattern;
    WORD commentColor = FOREGROUND_INTENSITY;
    std::string multiLineStart;
    std::string multiLineEnd;
    WORD multiLineColor = FOREGROUND_INTENSITY;
    std::vector<PatternRule> patterns;
    std::vector<ContextRule> contexts;
};

// Delta-based undo/redo - stores only what changed
struct EditDelta {
    enum Type { INSERT_CHAR, DELETE_CHAR, INSERT_LINE, DELETE_LINE, MODIFY_LINE };
    Type type;
    int lineNum;
    int charPos;
    std::string oldContent;  // For undo
    std::string newContent;  // For redo
    int cursorX, cursorY;    // Cursor position before change
};

// Line cache entry
struct LineCacheEntry {
    std::string content;
    bool dirty;  // Modified but not saved to disk
};

class LinoEditor {
private:
    HANDLE hIn;
    std::deque<int> inputQueue;

    // Emulate _getch but using ReadConsoleInput for efficiency
    int waitForInput() {
        if (!inputQueue.empty()) {
            int ch = inputQueue.front();
            inputQueue.pop_front();
            return ch;
        }

        DWORD count;
        INPUT_RECORD ir;

        while (true) {
            // Wait for event
            if (!ReadConsoleInput(hIn, &ir, 1, &count)) return 0;

            if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                needsFullRedraw = true;
                return 0; // Return to allow main loop to handle redraw
            }

            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                KEY_EVENT_RECORD& ker = ir.Event.KeyEvent;
                
                if (ker.uChar.AsciiChar != 0) {
                    return (unsigned char)ker.uChar.AsciiChar;
                } else {
                    // Map Virtual Key to BIOS Scan Code equivalent
                    int scanCode = 0;
                    switch (ker.wVirtualKeyCode) {
                        case VK_UP:    scanCode = 72; break;
                        case VK_DOWN:  scanCode = 80; break;
                        case VK_LEFT:  scanCode = 75; break;
                        case VK_RIGHT: scanCode = 77; break;
                        case VK_HOME:  scanCode = 71; break;
                        case VK_END:   scanCode = 79; break;
                        case VK_PRIOR: scanCode = 73; break; // PgUp
                        case VK_NEXT:  scanCode = 81; break; // PgDn
                        case VK_DELETE: scanCode = 83; break;
                        case VK_INSERT: scanCode = 82; break;
                    }

                    if (scanCode != 0) {
                        inputQueue.push_back(scanCode);
                        return 224; // Extended key prefix
                    }
                }
            }
        }
    }

    // Memory-efficient file storage
    std::vector<std::streamoff> lineOffsets;  // Byte position of each line start
    std::fstream fileHandle;                   // Keep file open for reading
    std::string filename;
    std::string fileExtension;
    size_t totalLineCount = 0;
    
    // Line cache - only keeps lines in/near view
    std::map<int, LineCacheEntry> lineCache;
    static const int CACHE_SIZE = 500;  // Max lines to cache
    
    // Dirty lines that need to be written on save
    std::set<int> dirtyLines;
    std::map<int, std::string> insertedLines;  // New lines not yet on disk
    std::set<int> deletedLines;                 // Lines marked for deletion
    
    int cursorX;
    int cursorY;
    int scrollOffsetY;
    int scrollOffsetX;
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
    std::map<std::string, LanguageSyntax> syntaxPlugins;
    LanguageSyntax* currentSyntax = nullptr;
    std::set<std::string> keywords;

    std::set<std::string> preprocessors;

    enum EditorMode { NORMAL, SEARCH };
    EditorMode currentMode = NORMAL;
    std::string highlightTerm;
    std::vector<std::pair<int, int>> searchResults;
    int searchIdx = 0;

    enum AppState { MENU, FILE_BROWSER, EDITOR };
    AppState appState;

    // Menu State
    int menuIndex = 0;
    std::vector<std::string> menuOptions = {"New File", "Open File"};

    // Browser State
    int browserIndex = 0;
    std::vector<fs::directory_entry> browserFiles;
    fs::path currentBrowserPath;

    // File-based Undo/Redo - stores edits in temp file instead of memory
    std::string undoTempFile;
    std::string redoTempFile;
    int undoCount = 0;
    int redoCount = 0;

    // Syntax State
    std::vector<bool> lineStartsInComment;

    // Build line offset index for a file (fast scan)
    void buildLineIndex(const std::string& filepath) {
        lineOffsets.clear();
        lineCache.clear();
        dirtyLines.clear();
        insertedLines.clear();
        deletedLines.clear();
        
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            totalLineCount = 1;
            lineOffsets.push_back(0);
            return;
        }
        
        lineOffsets.push_back(0);  // First line starts at byte 0
        char c;
        std::streamoff pos = 0;
        while (file.get(c)) {
            pos++;
            if (c == '\n') {
                lineOffsets.push_back(pos);
            }
        }
        totalLineCount = lineOffsets.size();
        file.close();
    }
    
    // Get effective line count (original + insertions - deletions)
    size_t getLineCount() {
        return totalLineCount + insertedLines.size() - deletedLines.size();
    }
    
    // Get a line by number (from cache or disk)
    std::string getLine(int lineNum) {
        if (lineNum < 0) return "";
        
        // Check if this is an inserted line
        auto itInsert = insertedLines.find(lineNum);
        if (itInsert != insertedLines.end()) {
            return itInsert->second;
        }
        

        
        // Adjust for virtual line numbers after insertions/deletions
        int realLine = lineNum;
        for (const auto& ins : insertedLines) {
            if (ins.first <= lineNum) realLine--;
        }
        for (int del : deletedLines) {
            if (del <= realLine) realLine++;
        }
        
        if (realLine < 0 || realLine >= (int)lineOffsets.size()) return "";
        
        // Check cache
        auto it = lineCache.find(lineNum);
        if (it != lineCache.end()) {
            return it->second.content;
        }
        
        // Read from file
        if (!fileHandle.is_open() && !filename.empty()) {
            fileHandle.open(filename, std::ios::in | std::ios::binary);
        }
        
        if (!fileHandle.is_open() || realLine >= (int)lineOffsets.size()) {
            return "";
        }
        
        fileHandle.clear();
        fileHandle.seekg(lineOffsets[realLine]);
        std::string line;
        std::getline(fileHandle, line);
        
        // Remove \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Add to cache
        lineCache[lineNum] = {line, false};
        
        // Evict old entries if cache too large
        if ((int)lineCache.size() > CACHE_SIZE) {
            evictCache();
        }
        
        return line;
    }
    
    // Set/modify a line (marks it dirty)
    void setLine(int lineNum, const std::string& content) {
        // If this line is in insertedLines, update it there (since getLine checks insertedLines first)
        auto it = insertedLines.find(lineNum);
        if (it != insertedLines.end()) {
            it->second = content;
        }
        // Always update the cache too
        lineCache[lineNum] = {content, true};
        dirtyLines.insert(lineNum);
        modified = true;
    }
    
    // Evict least recently used cache entries
    void evictCache() {
        // Keep lines near cursor, evict far ones
        std::vector<int> toEvict;
        for (const auto& pair : lineCache) {
            int dist = std::abs(pair.first - cursorY);
            if (dist > CACHE_SIZE / 2 && !pair.second.dirty) {
                toEvict.push_back(pair.first);
            }
        }
        for (int ln : toEvict) {
            lineCache.erase(ln);
        }
    }
    
    // Shift all lines >= lineNum down by 1 (used after deleting a line)
    void shiftLinesDown(int deletedLine) {
        // Rebuild insertedLines with shifted keys
        std::map<int, std::string> newInserted;
        for (auto& pair : insertedLines) {
            if (pair.first < deletedLine) {
                newInserted[pair.first] = pair.second;
            } else if (pair.first > deletedLine) {
                newInserted[pair.first - 1] = pair.second;
            }
            // Skip the deleted line itself
        }
        insertedLines = std::move(newInserted);
        
        // Also shift lineCache
        std::map<int, LineCacheEntry> newCache;
        for (auto& pair : lineCache) {
            if (pair.first < deletedLine) {
                newCache[pair.first] = pair.second;
            } else if (pair.first > deletedLine) {
                newCache[pair.first - 1] = pair.second;
            }
        }
        lineCache = std::move(newCache);
        
        // Shift dirtyLines
        std::set<int> newDirty;
        for (int ln : dirtyLines) {
            if (ln < deletedLine) newDirty.insert(ln);
            else if (ln > deletedLine) newDirty.insert(ln - 1);
        }
        dirtyLines = std::move(newDirty);
    }
    
    // Shift all lines >= lineNum up by 1 (used after inserting a line)
    void shiftLinesUp(int insertedLine) {
        // Rebuild insertedLines with shifted keys
        std::map<int, std::string> newInserted;
        for (auto& pair : insertedLines) {
            if (pair.first < insertedLine) {
                newInserted[pair.first] = pair.second;
            } else {
                newInserted[pair.first + 1] = pair.second;
            }
        }
        insertedLines = std::move(newInserted);
        
        // Also shift lineCache
        std::map<int, LineCacheEntry> newCache;
        for (auto& pair : lineCache) {
            if (pair.first < insertedLine) {
                newCache[pair.first] = pair.second;
            } else {
                newCache[pair.first + 1] = pair.second;
            }
        }
        lineCache = std::move(newCache);
        
        // Shift dirtyLines
        std::set<int> newDirty;
        for (int ln : dirtyLines) {
            if (ln < insertedLine) newDirty.insert(ln);
            else newDirty.insert(ln + 1);
        }
        dirtyLines = std::move(newDirty);
    }
    
    // Initialize temp files for undo/redo
    void initUndoFiles() {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        undoTempFile = std::string(tempPath) + "lino_undo_" + std::to_string(GetCurrentProcessId()) + ".tmp";
        redoTempFile = std::string(tempPath) + "lino_redo_" + std::to_string(GetCurrentProcessId()) + ".tmp";
        // Clear files
        std::ofstream(undoTempFile, std::ios::trunc).close();
        std::ofstream(redoTempFile, std::ios::trunc).close();
        undoCount = 0;
        redoCount = 0;
    }
    
    // Cleanup temp files
    void cleanupUndoFiles() {
        if (!undoTempFile.empty()) {
            fs::remove(undoTempFile);
        }
        if (!redoTempFile.empty()) {
            fs::remove(redoTempFile);
        }
    }
    
    // Write a delta to temp file (append mode)
    void writeDeltaToFile(const std::string& filepath, const EditDelta& delta) {
        std::ofstream file(filepath, std::ios::app | std::ios::binary);
        if (!file) return;
        
        // Format: type|lineNum|charPos|cursorX|cursorY|oldLen|oldContent|newLen|newContent\n
        file << (int)delta.type << "|" 
             << delta.lineNum << "|" 
             << delta.charPos << "|"
             << delta.cursorX << "|"
             << delta.cursorY << "|"
             << delta.oldContent.length() << "|" << delta.oldContent << "|"
             << delta.newContent.length() << "|" << delta.newContent << "\n";
    }
    
    // Read last delta from temp file and remove it
    bool readLastDeltaFromFile(const std::string& filepath, EditDelta& delta, int& count) {
        if (count <= 0) return false;
        
        std::ifstream file(filepath, std::ios::binary);
        if (!file) return false;
        
        // Read all lines
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) lines.push_back(line);
        }
        file.close();
        
        if (lines.empty()) return false;
        
        // Parse last line
        std::string lastLine = lines.back();
        lines.pop_back();
        
        // Parse format: type|lineNum|charPos|cursorX|cursorY|oldLen|oldContent|newLen|newContent
        size_t pos = 0;
        auto nextField = [&]() -> std::string {
            size_t end = lastLine.find('|', pos);
            if (end == std::string::npos) end = lastLine.length();
            std::string field = lastLine.substr(pos, end - pos);
            pos = end + 1;
            return field;
        };
        
        delta.type = (EditDelta::Type)std::stoi(nextField());
        delta.lineNum = std::stoi(nextField());
        delta.charPos = std::stoi(nextField());
        delta.cursorX = std::stoi(nextField());
        delta.cursorY = std::stoi(nextField());
        
        int oldLen = std::stoi(nextField());
        delta.oldContent = lastLine.substr(pos, oldLen);
        pos += oldLen + 1;
        
        int newLen = std::stoi(nextField());
        delta.newContent = lastLine.substr(pos, newLen);
        
        // Rewrite file without last line
        std::ofstream outFile(filepath, std::ios::trunc | std::ios::binary);
        for (const auto& l : lines) {
            outFile << l << "\n";
        }
        
        count--;
        return true;
    }
    
    // Save delta for undo (writes to temp file)
    void saveDelta(EditDelta::Type type, int lineNum, int charPos, 
                   const std::string& oldContent, const std::string& newContent) {
        EditDelta delta = {type, lineNum, charPos, oldContent, newContent, cursorX, cursorY};
        writeDeltaToFile(undoTempFile, delta);
        undoCount++;
        
        // Clear redo file when new edit is made
        std::ofstream(redoTempFile, std::ios::trunc).close();
        redoCount = 0;
    }

    void undo() {
        EditDelta delta;
        if (!readLastDeltaFromFile(undoTempFile, delta, undoCount)) {
            statusMessage = "Nothing to undo";
            return;
        }
        
        // Apply reverse operation
        switch (delta.type) {
            case EditDelta::MODIFY_LINE:
                setLine(delta.lineNum, delta.oldContent);
                break;
            case EditDelta::INSERT_LINE:
                deletedLines.insert(delta.lineNum);
                insertedLines.erase(delta.lineNum);
                if (totalLineCount > 0) totalLineCount--;
                break;
            case EditDelta::DELETE_LINE:
                deletedLines.erase(delta.lineNum);
                insertedLines[delta.lineNum] = delta.oldContent;
                totalLineCount++;
                break;
            case EditDelta::INSERT_CHAR:
            case EditDelta::DELETE_CHAR:
                setLine(delta.lineNum, delta.oldContent);
                break;
        }
        
        cursorX = delta.cursorX;
        cursorY = delta.cursorY;
        ensureCursorVisible();
        
        // Push to redo file
        writeDeltaToFile(redoTempFile, delta);
        redoCount++;
        statusMessage = "Undid change";
    }

    void redo() {
        EditDelta delta;
        if (!readLastDeltaFromFile(redoTempFile, delta, redoCount)) {
            statusMessage = "Nothing to redo";
            return;
        }
        
        // Apply forward operation
        switch (delta.type) {
            case EditDelta::MODIFY_LINE:
                setLine(delta.lineNum, delta.newContent);
                break;
            case EditDelta::INSERT_LINE:
                insertedLines[delta.lineNum] = delta.newContent;
                deletedLines.erase(delta.lineNum);
                totalLineCount++;
                break;
            case EditDelta::DELETE_LINE:
                deletedLines.insert(delta.lineNum);
                insertedLines.erase(delta.lineNum);
                if (totalLineCount > 0) totalLineCount--;
                break;
            case EditDelta::INSERT_CHAR:
            case EditDelta::DELETE_CHAR:
                setLine(delta.lineNum, delta.newContent);
                break;
        }
        
        ensureCursorVisible();
        
        // Push back to undo file
        writeDeltaToFile(undoTempFile, delta);
        undoCount++;
        statusMessage = "Redid change";
    }

    void updateSyntaxState() {
        size_t lineCount = getLineCount();
        lineStartsInComment.assign(lineCount + 1, false);
        if (!currentSyntax || currentSyntax->multiLineStart.empty()) return;

        // Only update for visible range + buffer
        int startLine = std::max(0, scrollOffsetY - 50);
        int endLine = std::min((int)lineCount, scrollOffsetY + screenHeight + 50);
        
        bool inComment = false;
        for (int i = startLine; i < endLine; i++) {
            lineStartsInComment[i] = inComment;
            std::string line = getLine(i);
            size_t pos = 0;
            while (pos < line.length()) {
                if (!inComment) {
                    size_t start = line.find(currentSyntax->multiLineStart, pos);
                    if (start == std::string::npos) break;
                    inComment = true;
                    pos = start + currentSyntax->multiLineStart.length();
                } else {
                    size_t end = line.find(currentSyntax->multiLineEnd, pos);
                    if (end == std::string::npos) break;
                    inComment = false;
                    pos = end + currentSyntax->multiLineEnd.length();
                }
            }
        }
        lineStartsInComment[lineCount] = inComment;
    }

    
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
    
    // Parse a single .Lino plugin file
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
                        
                        // Check for specific rule types
                        if (ruleType == "comments" || ruleType == "comment") {
                            currentLang.commentPattern = word;
                            currentLang.commentColor = color;
                        } else if (ruleType == "multiline_comment") {
                             // Format: start,end,color
                             // "word" holds "start" from the comma split inside the generic loop
                             // but we need 3 parts. Let's re-parse 'rest' properly here.
                             // The generic loop split 'rest' by first comma.
                             // 'word' = start
                             // 'colorStr' = "end,color"
                             size_t comma2 = colorStr.find(',');
                             if (comma2 != std::string::npos) {
                                 std::string endPat = trim(colorStr.substr(0, comma2));
                                 std::string finalColor = trim(colorStr.substr(comma2 + 1));
                                 currentLang.multiLineStart = word;
                                 currentLang.multiLineEnd = endPat;
                                 currentLang.multiLineColor = parseColor(finalColor);
                             }
                        } else {
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
            
            // Handle Pattern [Name] { block
            if (lowerLine.substr(0, 7) == "pattern") {
                size_t bracket1 = trimmed.find('[');
                size_t bracket2 = trimmed.find(']');
                if (bracket1 != std::string::npos && bracket2 != std::string::npos) {
                    PatternRule pattern;
                    pattern.name = trimmed.substr(bracket1 + 1, bracket2 - bracket1 - 1);
                    
                    // Read pattern block until }
                    while (std::getline(file, line)) {
                        std::string pLine = trim(line);
                        if (pLine.empty() || pLine[0] == '#') continue;
                        if (pLine[0] == '}') break;
                        
                        size_t colonPos = pLine.find(':');
                        if (colonPos == std::string::npos) continue;
                        
                        std::string key = trim(pLine.substr(0, colonPos));
                        std::string val = trim(pLine.substr(colonPos + 1));
                        // Remove trailing semicolon if present
                        if (!val.empty() && val.back() == ';') val.pop_back();
                        val = trim(val);
                        
                        std::string lowerKey = key;
                        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
                        
                        if (lowerKey == "triggers" || lowerKey == "trigger") {
                            // Parse comma-separated chars like '@', '.'
                            std::stringstream ss(val);
                            std::string token;
                            while (std::getline(ss, token, ',')) {
                                token = trim(token);
                                // Remove quotes if present: '@' -> @
                                if (token.size() >= 3 && token[0] == '\'' && token[token.size()-1] == '\'') {
                                    pattern.triggers.push_back(token[1]);
                                } else if (!token.empty()) {
                                    pattern.triggers.push_back(token[0]);
                                }
                            }
                        } else if (lowerKey == "format") {
                            pattern.format = val;
                        } else if (lowerKey == "color") {
                            pattern.color = parseColor(val);
                        }
                    }
                    
                    if (!pattern.format.empty()) {
                        currentLang.patterns.push_back(pattern);
                    }
                }
                continue;
            }
            
            // Handle Context [name](color) { block
            if (lowerLine.substr(0, 7) == "context") {
                size_t bracket1 = trimmed.find('[');
                size_t bracket2 = trimmed.find(']');
                size_t paren1 = trimmed.find('(');
                size_t paren2 = trimmed.find(')');
                
                if (bracket1 != std::string::npos && bracket2 != std::string::npos) {
                    ContextRule ctx;
                    ctx.name = trimmed.substr(bracket1 + 1, bracket2 - bracket1 - 1);
                    
                    // Parse error color from (color)
                    if (paren1 != std::string::npos && paren2 != std::string::npos) {
                        std::string colorStr = trimmed.substr(paren1 + 1, paren2 - paren1 - 1);
                        ctx.errorColor = parseColor(trim(colorStr));
                    } else {
                        ctx.errorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
                    }
                    
                    // Read context block until }
                    bool inPattern = false;
                    std::string patternContent;
                    
                    while (std::getline(file, line)) {
                        std::string cLine = trim(line);
                        if (cLine.empty() || cLine[0] == '#') continue;
                        
                        // Check for end of context
                        if (cLine[0] == '}' && !inPattern) break;
                        
                        // Handle Pattern[name] block
                        if (cLine.find("Pattern") != std::string::npos && cLine.find('{') != std::string::npos) {
                            // Extract pattern group name from Pattern[name]
                            ContextPatternGroup group;
                            size_t bracketStart = cLine.find('[');
                            size_t bracketEnd = cLine.find(']');
                            if (bracketStart != std::string::npos && bracketEnd != std::string::npos) {
                                group.name = cLine.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
                            } else {
                                group.name = "default";
                            }
                            
                            // Read patterns until }
                            while (std::getline(file, line)) {
                                std::string pLine = trim(line);
                                if (pLine.empty() || pLine[0] == '#') continue;
                                if (pLine[0] == '}') break;
                                
                                // Try to extract pattern from {pattern},
                                size_t braceStart = pLine.find('{');
                                size_t braceEnd = pLine.rfind('}');
                                if (braceStart != std::string::npos && braceEnd != std::string::npos && braceEnd > braceStart) {
                                    std::string pattern = pLine.substr(braceStart + 1, braceEnd - braceStart - 1);
                                    group.patterns.push_back(trim(pattern));
                                } else {
                                    // No braces - use the line directly as a pattern
                                    // Remove trailing comma/semicolon if present
                                    if (!pLine.empty() && (pLine.back() == ',' || pLine.back() == ';')) {
                                        pLine.pop_back();
                                    }
                                    if (!trim(pLine).empty()) {
                                        group.patterns.push_back(trim(pLine));
                                    }
                                }
                            }
                            
                            if (!group.patterns.empty()) {
                                ctx.patternGroups.push_back(group);
                            }
                            continue;
                        }
                        
                        size_t colonPos = cLine.find(':');
                        if (colonPos == std::string::npos) continue;
                        
                        std::string key = trim(cLine.substr(0, colonPos));
                        std::string val = trim(cLine.substr(colonPos + 1));
                        if (!val.empty() && val.back() == ';') val.pop_back();
                        val = trim(val);
                        
                        std::string lowerKey = key;
                        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
                        
                        if (lowerKey == "values" || lowerKey.find("values") == 0) {
                            // Parse "GroupName: val1, val2, val3"
                            // If key is just "Values", use "Values" as group name
                            std::string groupName = key;
                            std::vector<std::string> vals;
                            std::stringstream ss(val);
                            std::string token;
                            while (std::getline(ss, token, ',')) {
                                vals.push_back(trim(token));
                            }
                            ctx.valueGroups[groupName] = vals;
                        } else if (lowerKey == "keyword" || lowerKey == "keywords") {
                            std::stringstream ss(val);
                            std::string token;
                            while (std::getline(ss, token, ',')) {
                                ctx.keywords.push_back(trim(token));
                            }
                        } else if (lowerKey == "special" || lowerKey == "specials") {
                            std::stringstream ss(val);
                            std::string token;
                            while (std::getline(ss, token, ',')) {
                                token = trim(token);
                                if (token.size() >= 3 && token[0] == '\'' && token[token.size()-1] == '\'') {
                                    ctx.specials.push_back(std::string(1, token[1]));
                                } else {
                                    ctx.specials.push_back(token);
                                }
                            }
                        }
                    }
                    
                    currentLang.contexts.push_back(ctx);
                }
                continue;
            }
        }
        
        // Handle unclosed section
        if (inSection && !currentExtension.empty()) {
            syntaxPlugins[currentExtension] = currentLang;
        }
    }
    
    // Load all .Lino plugins from plugins folder
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
    
    // Returns length of match, or 0 if no match
    // %s = letters only, %d = digits only, %w = alphanumeric, %c = any char
    // (...) = repeat group zero or more times
    int matchPattern(const std::string& text, int pos, const std::string& format) {
        int textPos = pos;
        int fmtPos = 0;
        int textLen = (int)text.length();
        int fmtLen = (int)format.length();
        
        while (fmtPos < fmtLen && textPos <= textLen) {
            // Check for repeat group (...)
            if (format[fmtPos] == '(') {
                // Find matching closing paren
                int parenEnd = fmtPos + 1;
                int depth = 1;
                while (parenEnd < fmtLen && depth > 0) {
                    if (format[parenEnd] == '(') depth++;
                    else if (format[parenEnd] == ')') depth--;
                    parenEnd++;
                }
                if (depth != 0) return 0; // Unmatched paren
                
                // Extract the group pattern (without parens)
                std::string groupFmt = format.substr(fmtPos + 1, parenEnd - fmtPos - 2);
                fmtPos = parenEnd; // Move past the group
                
                // Try to match the group repeatedly
                while (textPos < textLen) {
                    int groupLen = matchPattern(text, textPos, groupFmt);
                    if (groupLen == 0) break; // No more matches
                    textPos += groupLen;
                }
                continue;
            }
            
            if (textPos >= textLen) break;
            
            if (format[fmtPos] == '%' && fmtPos + 1 < fmtLen) {
                char spec = format[fmtPos + 1];
                fmtPos += 2;
                
                int matchStart = textPos;
                
                if (spec == 's') {
                    // Match letters only (a-z, A-Z)
                    while (textPos < textLen && std::isalpha(text[textPos])) textPos++;
                } else if (spec == 'd') {
                    // Match digits only (0-9)
                    while (textPos < textLen && std::isdigit(text[textPos])) textPos++;
                } else if (spec == 'w') {
                    // Match word chars (alphanumeric + underscore)
                    while (textPos < textLen && (std::isalnum(text[textPos]) || text[textPos] == '_')) textPos++;
                } else if (spec == 'c') {
                    // Match any single char
                    textPos++;
                }
                
                // Must match at least 1 char for %s, %d, %w
                if (spec != 'c' && textPos == matchStart) return 0;
            } else {
                // Literal character match
                if (text[textPos] != format[fmtPos]) return 0;
                textPos++;
                fmtPos++;
            }
        }
        
        // Format must be fully consumed
        if (fmtPos < fmtLen) return 0;
        
        return textPos - pos;
    }
    
    // Check patterns for current position, returns {length, color} or {0, 0} if no match
    // This checks if a pattern STARTS at position pos
    std::pair<int, WORD> checkPatterns(const std::string& line, int pos) {
        if (!currentSyntax) return {0, 0};
        
        for (const auto& pattern : currentSyntax->patterns) {
            // Try to match this pattern starting at pos
            int len = matchPattern(line, pos, pattern.format);
            if (len > 0) {
                return {len, pattern.color};
            }
        }
        return {0, 0};
    }
    
    // Validate a token against Context rules
    // Returns error color if token is invalid, 0 if valid or no context
    WORD validateToken(const std::string& token) {
        if (!currentSyntax || currentSyntax->contexts.empty()) return 0;
        
        for (const auto& ctx : currentSyntax->contexts) {
            // Check if token is in any value group
            for (const auto& group : ctx.valueGroups) {
                for (const auto& val : group.second) {
                    if (token == val) return 0; // Valid
                }
            }
            
            // Check if token is a keyword
            for (const auto& kw : ctx.keywords) {
                if (token == kw) return 0; // Valid
            }
            
            // Check if token is a special
            for (const auto& sp : ctx.specials) {
                if (token == sp) return 0; // Valid
            }
            
            // Check if it's a number (always valid)
            bool isNumber = !token.empty();
            for (char c : token) {
                if (!std::isdigit(c) && c != '.' && c != ',') {
                    isNumber = false;
                    break;
                }
            }
            if (isNumber) return 0;
            
            // Check if it's a string literal (always valid)
            if (token.size() >= 2 && (token[0] == '"' || token[0] == '\'')) return 0;
        }
        
        return 0; // Default: don't flag as error (freetext allowed)
    }
    
    // Match a line against a Context pattern like "out(<freetext>);"
    // Returns true if the line matches the pattern
    bool matchContextPattern(const std::string& line, const std::string& pattern, const ContextRule& ctx) {
        int linePos = 0;
        int patPos = 0;
        int lineLen = (int)line.length();
        int patLen = (int)pattern.length();
        
        // Skip leading whitespace in line
        while (linePos < lineLen && std::isspace(line[linePos])) linePos++;
        
        while (patPos < patLen && linePos <= lineLen) {
            // Check for <placeholder>
            if (pattern[patPos] == '<') {
                size_t closeAngle = pattern.find('>', patPos);
                if (closeAngle != std::string::npos) {
                    std::string placeholder = pattern.substr(patPos + 1, closeAngle - patPos - 1);
                    patPos = (int)closeAngle + 1;
                    
                    if (placeholder == "freetext") {
                        // Match anything until next pattern char or end
                        char nextPat = (patPos < patLen) ? pattern[patPos] : '\0';
                        while (linePos < lineLen) {
                            if (nextPat != '\0' && line[linePos] == nextPat) break;
                            linePos++;
                        }
                    } else if (placeholder == "Values") {
                        // Match any value from the Values group
                        bool found = false;
                        for (const auto& group : ctx.valueGroups) {
                            for (const auto& val : group.second) {
                                if (linePos + val.length() <= lineLen &&
                                    line.substr(linePos, val.length()) == val) {
                                    linePos += (int)val.length();
                                    found = true;
                                    break;
                                }
                            }
                            if (found) break;
                        }
                        if (!found) return false;
                    }
                    continue;
                }
            }
            
            // Check for (...) continuation pattern
            if (pattern[patPos] == '(' && patPos + 4 < patLen && 
                pattern.substr(patPos, 5) == "(...)") {
                patPos += 5; // Skip past (...)
                
                // Get next pattern char after (...) to know when to stop
                char terminator = (patPos < patLen) ? pattern[patPos] : '\0';
                
                // Keep matching: special_char + freetext repeatedly
                while (linePos < lineLen) {
                    // Skip whitespace
                    while (linePos < lineLen && std::isspace(line[linePos])) linePos++;
                    
                    if (linePos >= lineLen) break;
                    
                    // Check for terminator
                    if (terminator != '\0' && line[linePos] == terminator) break;
                    
                    // Try to match any special character from context
                    bool matchedSpecial = false;
                    for (const auto& sp : ctx.specials) {
                        if (linePos + sp.length() <= lineLen &&
                            line.substr(linePos, sp.length()) == sp) {
                            linePos += (int)sp.length();
                            matchedSpecial = true;
                            break;
                        }
                    }
                    
                    if (!matchedSpecial) break;
                    
                    // Skip whitespace after special
                    while (linePos < lineLen && std::isspace(line[linePos])) linePos++;
                    
                    // Match freetext until next special or terminator
                    while (linePos < lineLen) {
                        if (terminator != '\0' && line[linePos] == terminator) break;
                        
                        // Check if next chars are a special
                        bool isSpecial = false;
                        for (const auto& sp : ctx.specials) {
                            if (linePos + sp.length() <= lineLen &&
                                line.substr(linePos, sp.length()) == sp) {
                                isSpecial = true;
                                break;
                            }
                        }
                        if (isSpecial) break;
                        
                        linePos++;
                    }
                }
                continue;
            }
            
            if (linePos >= lineLen) break;
            
            // Skip whitespace in both
            if (std::isspace(pattern[patPos])) {
                while (patPos < patLen && std::isspace(pattern[patPos])) patPos++;
                while (linePos < lineLen && std::isspace(line[linePos])) linePos++;
                continue;
            }
            
            // Literal match
            if (line[linePos] != pattern[patPos]) return false;
            linePos++;
            patPos++;
        }
        
        // Skip trailing whitespace
        while (linePos < lineLen && std::isspace(line[linePos])) linePos++;
        while (patPos < patLen && std::isspace(pattern[patPos])) patPos++;
        
        return (patPos >= patLen);
    }
    
    // Check if a line matches any Context pattern
    bool validateLineAgainstContext(const std::string& line) {
        if (!currentSyntax || currentSyntax->contexts.empty()) return true;
        if (trim(line).empty()) return true; // Empty lines are valid
        
        // Check if ANY patterns are defined
        bool hasPatterns = false;
        for (const auto& ctx : currentSyntax->contexts) {
            for (const auto& group : ctx.patternGroups) {
                if (!group.patterns.empty()) {
                    hasPatterns = true;
                    break;
                }
            }
            if (hasPatterns) break;
        }
        
        // If no patterns defined, line is valid (no Context validation)
        if (!hasPatterns) return true;
        
        // Try to match against defined patterns
        for (const auto& ctx : currentSyntax->contexts) {
            for (const auto& group : ctx.patternGroups) {
                for (const auto& pattern : group.patterns) {
                    if (matchContextPattern(line, pattern, ctx)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    
    void getTerminalSize() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            screenWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            screenHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        } else {
            screenWidth = 80;
            screenHeight = 25;
        }
        if (screenHeight < 10) screenHeight = 25;
    }

    void setCursorPosition(int x, int y) {
        std::cout << "\x1b[" << (y + 1) << ";" << (x + 1) << "H" << std::flush;
    }

    void hideCursor() {
        std::cout << "\x1b[?25l" << std::flush;
    }

    void showCursor() {
        std::cout << "\x1b[?25h" << std::flush;
    }

    std::string wordToAnsi(WORD attr) {
        int fg = 37, bg = 40;
        bool fgBright = (attr & FOREGROUND_INTENSITY) != 0;
        bool bgBright = (attr & BACKGROUND_INTENSITY) != 0;
        
        int fgColor = (attr & (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE));
        int bgColor = (attr & (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)) >> 4;
        
        switch (fgColor) {
            case 0: fg = fgBright ? 90 : 30; break;
            case FOREGROUND_RED: fg = fgBright ? 91 : 31; break;
            case FOREGROUND_GREEN: fg = fgBright ? 92 : 32; break;
            case FOREGROUND_RED | FOREGROUND_GREEN: fg = fgBright ? 93 : 33; break;
            case FOREGROUND_BLUE: fg = fgBright ? 94 : 34; break;
            case FOREGROUND_RED | FOREGROUND_BLUE: fg = fgBright ? 95 : 35; break;
            case FOREGROUND_GREEN | FOREGROUND_BLUE: fg = fgBright ? 96 : 36; break;
            case FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE: fg = fgBright ? 97 : 37; break;
        }
        
        switch (bgColor) {
            case 0: bg = bgBright ? 100 : 40; break;
            case 1: bg = bgBright ? 101 : 41; break;
            case 2: bg = bgBright ? 102 : 42; break;
            case 3: bg = bgBright ? 103 : 43; break;
            case 4: bg = bgBright ? 104 : 44; break;
            case 5: bg = bgBright ? 105 : 45; break;
            case 6: bg = bgBright ? 106 : 46; break;
            case 7: bg = bgBright ? 107 : 47; break;
        }
        
        return "\x1b[" + std::to_string(fg) + ";" + std::to_string(bg) + "m";
    }

    void setColor(WORD color) {
        std::cout << wordToAnsi(color) << std::flush;
    }

    void resetColor() {
        std::cout << "\x1b[0m" << std::flush;
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
    
    void flushBuffer() {
        if (screenBuffer.empty()) return;
        
        COORD bufferSize = { (SHORT)screenWidth, (SHORT)screenHeight };
        COORD bufferCoord = { 0, 0 };
        SMALL_RECT writeRegion = { 0, 0, (SHORT)(screenWidth - 1), (SHORT)(screenHeight - 1) };
        
        WriteConsoleOutputA(hConsole, screenBuffer.data(), bufferSize, bufferCoord, &writeRegion);
    }

    void drawHeader() {
        // Red Aesthetic: Red Background, White Text
        WORD headerAttr = BACKGROUND_RED | BACKGROUND_INTENSITY | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY;
        
        std::string title = " Lino ";
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
        
        // Red Aesthetic: Red Background like Header/Prompt
        WORD bgRed = BACKGROUND_RED | BACKGROUND_INTENSITY;
        WORD keyColor = bgRed | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY; // Keys pop in White
        WORD textColor = bgRed | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); // Desc in Grey/White
        
        bufferFill(0, line1, screenWidth, ' ', textColor);
        bufferFill(0, line2, screenWidth, ' ', textColor);
        
        if (currentMode == SEARCH) {
            // Search Mode Footer
            int pos = 0;
            bufferWrite(pos, line1, "^X", keyColor); pos += 2;
            bufferWrite(pos, line1, " Exit Search  ", textColor); pos += 14;
            bufferWrite(pos, line1, "Arrows", keyColor); pos += 6;
            bufferWrite(pos, line1, " NavigateMatches  ", textColor);
            
            std::string status = "Match " + std::to_string(searchIdx + 1) + "/" + std::to_string(searchResults.size());
            if (searchResults.empty()) status = "No Matches";
            bufferWrite(0, line2, status, textColor);
        } else {
            // Normal Footer
            // Line 1
            int pos = 0;
            bufferWrite(pos, line1, "^X", keyColor); pos += 2; bufferWrite(pos, line1, " Exit ", textColor); pos += 6;
            bufferWrite(pos, line1, "^O", keyColor); pos += 2; bufferWrite(pos, line1, " Save ", textColor); pos += 6;
            bufferWrite(pos, line1, "^Z", keyColor); pos += 2; bufferWrite(pos, line1, " Undo ", textColor); pos += 6;
            bufferWrite(pos, line1, "^Y", keyColor); pos += 2; bufferWrite(pos, line1, " Redo ", textColor); pos += 6;
            bufferWrite(pos, line1, "^F", keyColor); pos += 2; bufferWrite(pos, line1, " Find ", textColor); pos += 6;

            // Line 2
            pos = 0;
            bufferWrite(pos, line2, "^R", keyColor); pos += 2; bufferWrite(pos, line2, " Repl ", textColor); pos += 6;
            bufferWrite(pos, line2, "^K", keyColor); pos += 2; bufferWrite(pos, line2, " Cut  ", textColor); pos += 6;
            bufferWrite(pos, line2, "^U", keyColor); pos += 2; bufferWrite(pos, line2, " Paste", textColor); pos += 6;
            bufferWrite(pos, line2, "^G", keyColor); pos += 2; bufferWrite(pos, line2, " Help ", textColor); pos += 6;
            
            if (!statusMessage.empty()) {
                int msgStart = 45; // Arbitrary gap
                if (msgStart < pos) msgStart = pos + 1;
                bufferWrite(msgStart, line2, statusMessage, bgRed | FOREGROUND_RED | FOREGROUND_BLUE);
            }
            
            std::string posInfo = "L:" + std::to_string(cursorY + 1) + "/" + 
                                  std::to_string(getLineCount()) + " C:" + 
                                  std::to_string(cursorX + 1);
            int infoStart = screenWidth - (int)posInfo.length() - 1;
            bufferWrite(infoStart, line2, posInfo, bgRed | FOREGROUND_BLUE);
        }
    }

    void drawContent() {
        int contentStart = 1;
        int contentEnd = screenHeight - 3;
        int contentHeight = contentEnd - contentStart + 1;
        
        WORD normalAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        WORD tildeAttr = FOREGROUND_BLUE;
        WORD stringAttr = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD numberAttr = FOREGROUND_RED | FOREGROUND_GREEN;
        WORD gutterAttr = FOREGROUND_BLUE | FOREGROUND_INTENSITY;

        // Calculate gutter width
        std::string maxLineNum = std::to_string(getLineCount());
        int gutterWidth = (int)maxLineNum.length() + 1; // +1 padding
        if (gutterWidth < 3) gutterWidth = 3;

        int viewWidth = screenWidth - gutterWidth;
        
        // Update coloring state if needed
        if (lineStartsInComment.size() != getLineCount() + 1) updateSyntaxState();

        for (int y = 0; y < contentHeight; y++) {
            int screenY = contentStart + y;
            int lineIdx = scrollOffsetY + y;
            
            // Draw gutter
            bufferFill(0, screenY, gutterWidth, ' ', gutterAttr); 
            if (lineIdx < (int)getLineCount()) {
                std::string lineNum = std::to_string(lineIdx + 1);
                bufferWrite(0, screenY, lineNum, gutterAttr);
            }

            int screenX = gutterWidth;
            
            if (lineIdx < (int)getLineCount()) {
                std::string line = getLine(lineIdx);
                bool inMultiLine = (lineIdx < (int)lineStartsInComment.size()) ? lineStartsInComment[lineIdx] : false;
                
                // Horizontal scrolling applied to local "visible part" logic
                int startChar = scrollOffsetX;
                int fullLen = (int)line.length();

                // Clear line area
                bufferFill(screenX, screenY, viewWidth, ' ', normalAttr);
                
                // Check if line matches any Context pattern
                bool lineIsError = false;
                WORD lineErrorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
                if (currentSyntax && !currentSyntax->contexts.empty()) {
                    if (!validateLineAgainstContext(line)) {
                        lineIsError = true;
                        for (const auto& ctx : currentSyntax->contexts) {
                            lineErrorColor = ctx.errorColor;
                            break;
                        }
                    }
                }

                if (!currentSyntax) {
                    // Simple render without syntax
                    if (startChar < fullLen) {
                         int maxX = startChar + viewWidth;
                         if (maxX > fullLen) maxX = fullLen;
                         std::string sub = line.substr(startChar, maxX - startChar);
                         bufferWrite(screenX, screenY, sub, normalAttr);
                    }
                } else if (lineIsError) {
                    // Render entire line in error color
                    if (startChar < fullLen) {
                         int maxX = startChar + viewWidth;
                         if (maxX > fullLen) maxX = fullLen;
                         std::string sub = line.substr(startChar, maxX - startChar);
                         bufferWrite(screenX, screenY, sub, lineErrorColor);
                    }
                } else {
                    // Logic with syntax
                    int x = 0;
                    bool inString = false;
                    char stringChar = '\0';
                    
                    while (x < fullLen) {
                         // Determine if this char is visible
                         int drawX = screenX + (x - startChar);
                         bool isVisible = (drawX >= screenX && drawX < screenWidth);

                         // Multiline Logic
                         if (inMultiLine) {
                            if (isVisible) bufferWriteChar(drawX, screenY, line[x], currentSyntax->multiLineColor);
                            
                            // Check for end
                            if (!currentSyntax->multiLineEnd.empty() && 
                                x + currentSyntax->multiLineEnd.length() <= fullLen && 
                                line.substr(x, currentSyntax->multiLineEnd.length()) == currentSyntax->multiLineEnd) {
                                
                                // Draw the rest of the end marker
                                for (size_t k = 1; k < currentSyntax->multiLineEnd.length(); k++) {
                                    int dx = drawX + (int)k;
                                    if (dx >= screenX && dx < screenWidth) 
                                        bufferWriteChar(dx, screenY, line[x+k], currentSyntax->multiLineColor);
                                }
                                x += (int)currentSyntax->multiLineEnd.length();
                                inMultiLine = false;
                                continue;
                            }
                            x++;
                            continue;
                        }

                        // Check for start of multiline
                        if (!inString && !currentSyntax->multiLineStart.empty() &&
                            x + currentSyntax->multiLineStart.length() <= fullLen &&
                            line.substr(x, currentSyntax->multiLineStart.length()) == currentSyntax->multiLineStart) {
                            
                            inMultiLine = true;
                            // Draw start
                            for (size_t k = 0; k < currentSyntax->multiLineStart.length(); k++) {
                                int dx = screenX + (x + (int)k - startChar);
                                if (dx >= screenX && dx < screenWidth)
                                    bufferWriteChar(dx, screenY, line[x+k], currentSyntax->multiLineColor);
                            }
                            x += (int)currentSyntax->multiLineStart.length();
                            continue;
                        }

                        // Check for comments
                        if (!inString && 
                            !currentSyntax->commentPattern.empty() && 
                            x + currentSyntax->commentPattern.length() <= fullLen && 
                            line.substr(x, currentSyntax->commentPattern.length()) == currentSyntax->commentPattern) {
                            
                            // Rest of line is comment
                            for (int i = x; i < fullLen; i++) {
                                int sx = screenX + (i - startChar);
                                if (sx >= screenX && sx < screenWidth) {
                                    bufferWriteChar(sx, screenY, line[i], currentSyntax->commentColor);
                                }
                            }
                            break;
                        }
                        
                        // Check for # at start of line (preprocessor)
                         if (x == 0 && line[x] == '#' && currentSyntax->commentPattern != "#") {
                            WORD preprocColor = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                            for (int i = 0; i < fullLen; i++) {
                                int sx = screenX + (i - startChar);
                                if (sx >= screenX && sx < screenWidth) {
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
                                if (isVisible) bufferWriteChar(drawX, screenY, line[x], stringAttr);
                                x++;
                                continue;
                            } else if (line[x] == stringChar) {
                                if (isVisible) bufferWriteChar(drawX, screenY, line[x], stringAttr);
                                inString = false;
                                stringChar = '\0';
                                x++;
                                continue;
                            }
                        }
                        
                        if (inString) {
                            if (isVisible) bufferWriteChar(drawX, screenY, line[x], stringAttr);
                            x++;
                            continue;
                        }

                        // CHECK CUSTOM PATTERNS (e.g., emails, floats, money)
                        auto [patLen, patColor] = checkPatterns(line, x);
                        if (patLen > 0) {
                            for (int k = 0; k < patLen; k++) {
                                int dx = screenX + (x + k - startChar);
                                if (dx >= screenX && dx < screenWidth)
                                    bufferWriteChar(dx, screenY, line[x+k], patColor);
                            }
                            x += patLen;
                            continue;
                        }

                        // HIGHLIGHT SEARCH TERMS
                        // Do this check before keywords/numbers to overlay/override
                        if (!highlightTerm.empty() && 
                            x + highlightTerm.length() <= fullLen && 
                            line.substr(x, highlightTerm.length()) == highlightTerm) {
                            
                            // Highlight: Bright Yellow Background, Black Text
                            WORD highlightColor = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_INTENSITY; 
                            
                            // Check if this is the CURRENT match
                            if (currentMode == SEARCH && !searchResults.empty() && 
                                searchIdx < searchResults.size() && 
                                searchResults[searchIdx].first == lineIdx && 
                                searchResults[searchIdx].second == x) {
                                // Current: Bright Cyan Background, Bright White Text
                                highlightColor = BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                            }

                            for (size_t k = 0; k < highlightTerm.length(); k++) {
                                int dx = screenX + (x + (int)k - startChar);
                                if (dx >= screenX && dx < screenWidth)
                                    bufferWriteChar(dx, screenY, line[x+k], highlightColor);
                            }
                            x += (int)highlightTerm.length();
                            continue;
                        }
                        
                        // Check for numbers
                        if (std::isdigit(line[x])) {
                            if (isVisible) bufferWriteChar(drawX, screenY, line[x], numberAttr);
                            x++;
                            continue;
                        }
                        
                        // Check for special characters
                        bool foundSpecial = false;
                        for (const auto& pair : currentSyntax->specialChars) {
                            if (pair.first.find(line[x]) != std::string::npos) {
                                if (isVisible) bufferWriteChar(drawX, screenY, line[x], pair.second);
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
                            int wordStart = x;
                            while (x < fullLen && isWordChar(line[x])) x++;
                            std::string word = line.substr(wordStart, x - wordStart);
                            
                            WORD wordColor = normalAttr;
                            bool isKeyword = keywords.count(word) > 0;
                            bool isPreproc = preprocessors.count(word) > 0;
                            
                            if (isKeyword) {
                                wordColor = getWordColor(word);
                            } else if (isPreproc) {
                                wordColor = getWordColor(word);
                            } else if (currentSyntax && !currentSyntax->contexts.empty()) {
                                // Context validation - strict check
                                bool isContextValid = false;
                                WORD errorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
                                
                                for (const auto& ctx : currentSyntax->contexts) {
                                    errorColor = ctx.errorColor;
                                    
                                    // Check value groups (exact match only)
                                    for (const auto& group : ctx.valueGroups) {
                                        for (const auto& val : group.second) {
                                            if (word == val) { isContextValid = true; break; }
                                        }
                                        if (isContextValid) break;
                                    }
                                    
                                    // Check context keywords (exact match only)
                                    if (!isContextValid) {
                                        for (const auto& kw : ctx.keywords) {
                                            if (word == kw) { isContextValid = true; break; }
                                        }
                                    }
                                }
                                
                                // Check if followed by ( - must be a valid function
                                int afterWord = x;
                                while (afterWord < fullLen && line[afterWord] == ' ') afterWord++;
                                
                                if (afterWord < fullLen && line[afterWord] == '(') {
                                    // This is a function call - MUST be valid
                                    if (!isContextValid) {
                                        wordColor = errorColor;
                                    }
                                }
                            }
                            
                            for (int i = wordStart; i < x; i++) {
                                int sx = screenX + (i - startChar);
                                if (sx >= screenX && sx < screenWidth) {
                                    bufferWriteChar(sx, screenY, line[i], wordColor);
                                }
                            }
                            continue;
                        }
                        
                        // Check for invalid syntax: ( after ) is an error
                        if (currentSyntax && !currentSyntax->contexts.empty() && line[x] == '(') {
                            // Check if previous non-space char was ) - that's an error
                            int prev = x - 1;
                            while (prev >= 0 && line[prev] == ' ') prev--;
                            if (prev >= 0 && line[prev] == ')') {
                                // Error: ()() pattern
                                WORD errorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
                                for (const auto& ctx : currentSyntax->contexts) {
                                    errorColor = ctx.errorColor;
                                    break;
                                }
                                if (isVisible) bufferWriteChar(drawX, screenY, line[x], errorColor);
                                x++;
                                continue;
                            }
                        }
                        
                        // Default
                        if (isVisible) bufferWriteChar(drawX, screenY, line[x], normalAttr);
                        x++;
                    }
                }
            } else {
                // Empty line (no tilde)
                bufferFill(screenX, screenY, viewWidth, ' ', normalAttr);
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
        
        if (appState == MENU) {
            drawMenu();
        } else if (appState == FILE_BROWSER) {
            drawBrowser();
        } else {
            drawHeader();
            drawContent();
            drawFooter();
        }
        
        // Flush entire buffer at once (very fast!)
        flushBuffer();
        
        if (appState == EDITOR) {
            // Calculate gutter width for cursor offset
            std::string maxLineNum = std::to_string(getLineCount());
            int gutterWidth = (int)maxLineNum.length() + 1; // +1 padding
            if (gutterWidth < 3) gutterWidth = 3;

            int contentStart = 1;
            int displayY = contentStart + (cursorY - scrollOffsetY);
            int displayX = gutterWidth + (cursorX - scrollOffsetX);  // Offset by gutter
            
            if (displayX < gutterWidth) displayX = gutterWidth; 
            if (displayX >= screenWidth) displayX = screenWidth - 1;
            
            setCursorPosition(displayX, displayY);
            showCursor();
        } else {
            setCursorPosition(0, 0); // Hide mostly
        }
    }

    void drawMenu() {
        // Red Aesthetic: Black Background, Red Highlights
        WORD bg = 0; 
        WORD textAttr = FOREGROUND_RED | FOREGROUND_INTENSITY;
        WORD highlightAttr = BACKGROUND_RED | BACKGROUND_INTENSITY | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY;

        bufferFill(0, 0, screenWidth * screenHeight, ' ', bg);
        
        int centerY = screenHeight / 3;
        
        std::string title = "L I N O   v 2 . 0";
        std::string subtitle = "Linuxified Lino Editor";
        std::string credit = "Created by Cortez";

        int titleX = (screenWidth - (int)title.length()) / 2;
        int subX = (screenWidth - (int)subtitle.length()) / 2;
        int credX = (screenWidth - (int)credit.length()) / 2;

        bufferWrite(titleX, centerY, title, BACKGROUND_RED | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY);
        bufferWrite(subX, centerY + 2, subtitle, textAttr);
        bufferWrite(credX, centerY + 3, credit, textAttr);

        int menuY = centerY + 6;
        for (int i = 0; i < (int)menuOptions.size(); i++) {
            std::string opt = "   " + menuOptions[i] + "   ";
            int optX = (screenWidth - (int)opt.length()) / 2;
            
            WORD attr = (i == menuIndex) ? highlightAttr : textAttr;
            bufferWrite(optX, menuY + i * 2, opt, attr);
        }
    }

    void drawBrowser() {
        WORD bg = 0; // Black
        WORD textAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // White
        WORD dirAttr = FOREGROUND_RED | FOREGROUND_INTENSITY; // Red Dirs
        WORD fileAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Grey Files
        WORD highlightAttr = BACKGROUND_RED | BACKGROUND_INTENSITY | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY;
        // Explicitly clear buffer using std::fill for robustness
        CHAR_INFO empty;
        empty.Char.AsciiChar = ' ';
        empty.Attributes = bg;
        std::fill(screenBuffer.begin(), screenBuffer.end(), empty);

        std::string pathStr = "Path: " + currentBrowserPath.string();
        bufferWrite(2, 1, pathStr, BACKGROUND_RED | BACKGROUND_INTENSITY | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY);

        int startY = 3;
        int maxItems = screenHeight - 5;
        
        
        // Map logical index (-1 to N-1) to visual index (0 to N)
        int logicalSize = (int)browserFiles.size() + 1; // +1 for ".."
        int visualIndex = browserIndex + 1; // 0 for "..", 1+ for files
        
        int startVisual = 0;
        if (visualIndex > maxItems / 2) {
            startVisual = visualIndex - maxItems / 2;
        }
        if (startVisual + maxItems > logicalSize) {
            startVisual = logicalSize - maxItems;
        }
        if (startVisual < 0) startVisual = 0;

        // Render Loop
        for (int i = 0; i < maxItems; i++) {
            int currentVisual = startVisual + i;
            if (currentVisual >= logicalSize) break; // No more items

            // Map back to logical
            int logicalIdx = currentVisual - 1; 

            int drawY = startY + i;
            
            std::string label;
            WORD itemAttr;
            
            if (logicalIdx == -1) {
                label = "[..] Parent Directory";
                itemAttr = dirAttr;
            } else {
                const auto& ent = browserFiles[logicalIdx];
                if (ent.is_directory()) {
                    label = "[" + ent.path().filename().string() + "]";
                    itemAttr = dirAttr;
                } else {
                    label = ent.path().filename().string();
                    itemAttr = fileAttr;
                }
            }

            bool selected = (logicalIdx == browserIndex);
            if (selected) itemAttr = highlightAttr;

            // Draw selection bar background
            bufferFill(2, drawY, screenWidth - 4, ' ', selected ? highlightAttr : bg);
            // Draw text
            bufferWrite(4, drawY, label, itemAttr);
        }
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
        
        // Calculate gutter width
        std::string maxLineNum = std::to_string(getLineCount());
        int gutterWidth = (int)maxLineNum.length() + 1;
        if (gutterWidth < 3) gutterWidth = 3;

        // Horizontal scrolling
        int margin = 5;
        int viewWidth = screenWidth - gutterWidth - margin; // Reduce by gutter
        if (viewWidth < 10) viewWidth = screenWidth - gutterWidth; // Fallback
        
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
            int lineLen = (int)getLine(cursorY).length();
            if (cursorX > lineLen) cursorX = lineLen;
            ensureCursorVisible();
        }
    }

    void moveCursorDown() {
        if (cursorY < (int)getLineCount() - 1) {
            cursorY++;
            int lineLen = (int)getLine(cursorY).length();
            if (cursorX > lineLen) cursorX = lineLen;
            ensureCursorVisible();
        }
    }

    void moveCursorLeft() {
        if (cursorX > 0) {
            cursorX--;
        } else if (cursorY > 0) {
            cursorY--;
            cursorX = (int)getLine(cursorY).length();
            ensureCursorVisible();
        }
    }

    void moveCursorRight() {
        int lineLen = (int)getLine(cursorY).length();
        if (cursorX < lineLen) {
            cursorX++;
        } else if (cursorY < (int)getLineCount() - 1) {
            cursorY++;
            cursorX = 0;
            ensureCursorVisible();
        }
    }

    void insertChar(char c) {
        std::string oldLine = getLine(cursorY);
        
        // Clamp cursorX to valid range for this line
        if (cursorX > (int)oldLine.length()) cursorX = (int)oldLine.length();
        if (cursorX < 0) cursorX = 0;
        
        // Auto-wrap
        if (cursorX >= screenWidth - 1) {
            std::string rest = (cursorX < (int)oldLine.length()) ? oldLine.substr(cursorX) : "";
            std::string beforeRest = oldLine.substr(0, cursorX);
            
            saveDelta(EditDelta::MODIFY_LINE, cursorY, cursorX, oldLine, beforeRest);
            setLine(cursorY, beforeRest);
            
            // Insert new line - shift existing lines up first, then add new line
            shiftLinesUp(cursorY + 1);  // Make room for the new line
            insertedLines[cursorY + 1] = rest;
            saveDelta(EditDelta::INSERT_LINE, cursorY + 1, 0, "", rest);
            
            cursorY++;
            cursorX = 0;
            scrollOffsetX = 0; 
            ensureCursorVisible();
        }
        
        std::string currentLine = getLine(cursorY);
        
        // Clamp cursorX again after potential line change
        if (cursorX > (int)currentLine.length()) cursorX = (int)currentLine.length();
        if (cursorX < 0) cursorX = 0;
        
        std::string newLine = currentLine.substr(0, cursorX) + c + currentLine.substr(cursorX);
        saveDelta(EditDelta::INSERT_CHAR, cursorY, cursorX, currentLine, newLine);
        setLine(cursorY, newLine);
        cursorX++;
        updateSyntaxState();
    }

    void insertNewLine() {
        std::string oldLine = getLine(cursorY);
        
        // Clamp cursorX to valid range
        if (cursorX > (int)oldLine.length()) cursorX = (int)oldLine.length();
        if (cursorX < 0) cursorX = 0;
        
        std::string rest = (cursorX < (int)oldLine.length()) ? oldLine.substr(cursorX) : "";
        std::string beforeCursor = oldLine.substr(0, cursorX);
        
        // Modify current line
        saveDelta(EditDelta::MODIFY_LINE, cursorY, cursorX, oldLine, beforeCursor);
        setLine(cursorY, beforeCursor);
        
        // Insert new line - shift existing lines up first, then add new line
        shiftLinesUp(cursorY + 1);  // Make room for the new line
        insertedLines[cursorY + 1] = rest;
        saveDelta(EditDelta::INSERT_LINE, cursorY + 1, 0, "", rest);
        
        cursorY++;
        cursorX = 0;
        ensureCursorVisible();
        updateSyntaxState();
    }

    void deleteChar() {
        std::string currentLine = getLine(cursorY);
        
        // Clamp cursorX to valid range
        if (cursorX > (int)currentLine.length()) cursorX = (int)currentLine.length();
        if (cursorX < 0) cursorX = 0;
        
        if (cursorX > 0) {
            std::string newLine = currentLine.substr(0, cursorX - 1) + currentLine.substr(cursorX);
            saveDelta(EditDelta::DELETE_CHAR, cursorY, cursorX, currentLine, newLine);
            setLine(cursorY, newLine);
            cursorX--;
            if (cursorX < screenWidth - 5) {
                scrollOffsetX = 0;
            }
        } else if (cursorY > 0) {
            // Merge with previous line
            std::string prevLine = getLine(cursorY - 1);
            int prevLineLen = (int)prevLine.length();
            std::string mergedLine = prevLine + currentLine;
            
            saveDelta(EditDelta::MODIFY_LINE, cursorY - 1, prevLineLen, prevLine, mergedLine);
            setLine(cursorY - 1, mergedLine);
            
            // Delete current line and shift subsequent lines down
            saveDelta(EditDelta::DELETE_LINE, cursorY, 0, currentLine, "");
            int deletedLineNum = cursorY;
            // Check if line was in insertedLines or original file
            auto itInsert = insertedLines.find(cursorY);
            if (itInsert != insertedLines.end()) {
                insertedLines.erase(itInsert);  // Just remove from inserted
            } else {
                // Calculate real line index to delete
                int realLine = cursorY;
                for (const auto& ins : insertedLines) {
                    if (ins.first <= cursorY) realLine--;
                }
                for (int del : deletedLines) {
                    if (del <= realLine) realLine++;
                }
                deletedLines.insert(realLine);  // Mark original line as deleted
            }
            lineCache.erase(cursorY);
            shiftLinesDown(deletedLineNum);  // Renumber remaining lines
            
            cursorY--;
            cursorX = prevLineLen;
            scrollOffsetX = 0;
            if (cursorX >= screenWidth - 5) {
                scrollOffsetX = cursorX - screenWidth + 10;
                if (scrollOffsetX < 0) scrollOffsetX = 0;
            }
            ensureCursorVisible();
        }
        updateSyntaxState();
    }

    void deleteCharForward() {
        std::string currentLine = getLine(cursorY);
        int lineLen = (int)currentLine.length();
        
        if (cursorX < lineLen) {
            std::string newLine = currentLine.substr(0, cursorX) + currentLine.substr(cursorX + 1);
            saveDelta(EditDelta::DELETE_CHAR, cursorY, cursorX, currentLine, newLine);
            setLine(cursorY, newLine);
        } else if (cursorY < (int)getLineCount() - 1) {
            // Merge with next line
            std::string nextLine = getLine(cursorY + 1);
            std::string mergedLine = currentLine + nextLine;
            
            saveDelta(EditDelta::MODIFY_LINE, cursorY, lineLen, currentLine, mergedLine);
            setLine(cursorY, mergedLine);
            
            saveDelta(EditDelta::DELETE_LINE, cursorY + 1, 0, nextLine, "");
            int deletedLineNum = cursorY + 1;
            // Check if line was in insertedLines or original file
            auto itInsert = insertedLines.find(cursorY + 1);
            if (itInsert != insertedLines.end()) {
                insertedLines.erase(itInsert);  // Just remove from inserted
            } else {
                // Calculate real line index to delete
                int realLine = cursorY + 1;
                for (const auto& ins : insertedLines) {
                    if (ins.first <= cursorY + 1) realLine--;
                }
                for (int del : deletedLines) {
                    if (del <= realLine) realLine++;
                }
                deletedLines.insert(realLine);  // Mark original line as deleted
            }
            lineCache.erase(cursorY + 1);
            shiftLinesDown(deletedLineNum);  // Renumber remaining lines
        }
        updateSyntaxState();
    }

    void cutLine() {
        if (getLineCount() == 0) return;
        
        std::string currentLine = getLine(cursorY);
        cutBuffer = currentLine;
        
        saveDelta(EditDelta::DELETE_LINE, cursorY, 0, currentLine, "");
        int deletedLineNum = cursorY;
        // Check if line was in insertedLines or original file
        auto itInsert = insertedLines.find(cursorY);
        if (itInsert != insertedLines.end()) {
            insertedLines.erase(itInsert);  // Just remove from inserted
        } else {
            // Calculate real line index to delete
            int realLine = cursorY;
            for (const auto& ins : insertedLines) {
                if (ins.first <= cursorY) realLine--;
            }
            for (int del : deletedLines) {
                if (del <= realLine) realLine++;
            }
            deletedLines.insert(realLine);  // Mark original line as deleted
        }
        lineCache.erase(cursorY);
        shiftLinesDown(deletedLineNum);  // Renumber remaining lines
        
        if (getLineCount() == 0) {
            insertedLines[0] = "";  // Ensure at least one line
        }
        
        if (cursorY >= (int)getLineCount()) cursorY = (int)getLineCount() - 1;
        if (cursorY < 0) cursorY = 0;
        cursorX = 0;
        statusMessage = "Cut line";
        ensureCursorVisible();
        updateSyntaxState();
    }

    void pasteLine() {
        if (cutBuffer.empty()) {
            statusMessage = "Buffer empty";
            return;
        }
        
        // Insert the cut buffer as a new line - shift existing lines up first
        shiftLinesUp(cursorY);  // Make room for the new line
        saveDelta(EditDelta::INSERT_LINE, cursorY, 0, "", cutBuffer);
        insertedLines[cursorY] = cutBuffer;
        
        cursorX = 0;
        statusMessage = "Pasted";
        updateSyntaxState();
    }

    std::string promptInput(const std::string& prompt, const std::vector<std::pair<std::string, std::string>>& shortcuts = {}, std::string prefill = "") {
        std::string input = prefill;
        int promptY = screenHeight - 3; // Nano style: 3rd line from bottom
        
        while (true) {
            // 1. Draw Background for Footer Area
            // Red Aesthetic: Bright Red Background
            WORD promptAttr = BACKGROUND_RED | BACKGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; 
            WORD defaultAttr = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED; // Normal text
            
            // Clear prompt line
            bufferFill(0, promptY, screenWidth, ' ', promptAttr);
            bufferWrite(0, promptY, " " + prompt + " " + input, promptAttr);
            
            // Show cursor emulation
            int cursorVisX = (int)prompt.length() + 2 + (int)input.length();
            if (cursorVisX < screenWidth) {
                bufferWriteChar(cursorVisX, promptY, '_', promptAttr);
            }
            
            // Clear shortcut lines to black
            bufferFill(0, screenHeight - 2, screenWidth, ' ', 0);
            bufferFill(0, screenHeight - 1, screenWidth, ' ', 0);
            
            // Draw hints/shortcuts
            int x = 0;
            int y = screenHeight - 2;
            for (const auto& sc : shortcuts) {
                int width = (int)sc.first.length() + (int)sc.second.length() + 3; 
                if (x + width > screenWidth && y == screenHeight - 2) {
                     x = 0;
                     y = screenHeight - 1;
                }
                
                // Draw Key: Red Text on Black (to match aesthetic?) or Inverted Red?
                // Let's go with White Text on Red Background for the Key to pop, normal for desc.
                // Or: Red Text for key.
                // User said "red aesthetic". 
                bufferWrite(x, y, sc.first, FOREGROUND_RED | FOREGROUND_INTENSITY); // Bright Red Text
                x += (int)sc.first.length();
                bufferWrite(x, y, " " + sc.second + "  ", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                x += (int)sc.second.length() + 3;
            }
            
            flushBuffer();
            
            int ch = waitForInput();
            
            if (ch == 13) break; // Enter
            if (ch == 27 || ch == 3) { // Esc or Ctrl+C
                statusMessage = "Cancelled";
                return ""; 
            } 
            if (ch == 8) { // Backspace
                if (!input.empty()) input.pop_back();
            } else if (ch >= 32 && ch < 127) {
                input += (char)ch;
            }
        }
        return input;
    }

    void saveFile() {
        std::string saveName = promptInput("Filename to Write", {{"^G", "Get Help"}, {"^C", "Cancel"}}, filename);
        if (saveName.empty()) {
             statusMessage = "Cancelled";
             return;
        }
        
        std::ofstream ofs(saveName);
        if (!ofs) {
            statusMessage = "Error saving!";
            return;
        }
        
        // Close file handle temporarily for writing
        if (fileHandle.is_open()) {
            fileHandle.close();
        }
        
        size_t lineCount = getLineCount();
        for (size_t i = 0; i < lineCount; i++) {
            ofs << getLine((int)i);
            if (i < lineCount - 1) ofs << '\n';
        }
        ofs.close();
        
        // Clear dirty state
        dirtyLines.clear();
        
        // Rebuild index for new file
        filename = saveName;
        buildLineIndex(saveName);
        fileHandle.open(saveName, std::ios::in | std::ios::binary);
        
        modified = false;
        statusMessage = "Saved " + std::to_string(lineCount) + " lines";
        
        // Update extension and syntax on first save
        fs::path p(filename);
        if (p.has_extension()) {
            fileExtension = p.extension().string();
            selectSyntax();
        }
    }

    void updateSearchResults() {
        searchResults.clear();
        searchIdx = 0;
        if (highlightTerm.empty()) return;
        
        for (int y = 0; y < (int)getLineCount(); y++) {
            size_t pos = 0;
            std::string line = getLine(y);
            while ((pos = line.find(highlightTerm, pos)) != std::string::npos) {
                searchResults.push_back({y, (int)pos});
                pos += highlightTerm.length();
            }
        }
        
        // Find closest match to current cursor
        if (!searchResults.empty()) {
            for (size_t i = 0; i < searchResults.size(); i++) {
                if (searchResults[i].first > cursorY || (searchResults[i].first == cursorY && searchResults[i].second >= cursorX)) {
                    searchIdx = (int)i;
                    break;
                }
            }
            // Move cursor to it
            cursorY = searchResults[searchIdx].first;
            cursorX = searchResults[searchIdx].second;
            ensureCursorVisible();
        }
    }

    void runSearch() {
        std::string query = promptInput("Search", {{"^C", "Cancel"}});
        if (query.empty()) return;
        
        highlightTerm = query;
        updateSearchResults();
        
        if (searchResults.empty()) {
            statusMessage = "Not found";
            highlightTerm.clear();
            return;
        }
        
        currentMode = SEARCH;
        statusMessage = "Search Mode";
    }

    void runReplace() {
        std::string find = promptInput("Find to Replace", {{"^C", "Cancel"}});
        if (find.empty()) return;

        std::string rep = promptInput("Replace with", {{"^C", "Cancel"}});
    
    // Custom footer prompt for confirmation
    while(true) {
        WORD promptAttr = BACKGROUND_RED | BACKGROUND_INTENSITY | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY; 
        int promptY = screenHeight - 3;
        bufferFill(0, promptY, screenWidth, ' ', promptAttr);
        bufferWrite(0, promptY, " Replace matches?", promptAttr);
        
        bufferFill(0, screenHeight - 2, screenWidth, ' ', 0);
        bufferFill(0, screenHeight - 1, screenWidth, ' ', 0);
        
        std::vector<std::pair<std::string, std::string>> options = {{"Y", "All"}, {"N", "Cancel"}};
        int x = 0;
        int y = screenHeight - 2;
        for (const auto& sc : options) {
            bufferWrite(x, y, sc.first, FOREGROUND_RED | FOREGROUND_INTENSITY); 
            x += (int)sc.first.length();
            bufferWrite(x, y, " " + sc.second + "  ", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            x += (int)sc.second.length() + 3;
        }
        flushBuffer();
        
        int ch = waitForInput();
        if (ch == 'y' || ch == 'Y') break; 
        if (ch == 'n' || ch == 'N' || ch == 27 || ch == 3) {
            statusMessage = "Cancelled";
            return;
        }
    }
    
    int count = 0;
        for (int i = 0; i < (int)getLineCount(); i++) {
            std::string line = getLine(i);
            std::string originalLine = line;
            size_t pos = 0;
            bool changed = false;
            while ((pos = line.find(find, pos)) != std::string::npos) {
                line.replace(pos, find.length(), rep);
                pos += rep.length();
                count++;
                changed = true;
            }
            if (changed) {
                saveDelta(EditDelta::MODIFY_LINE, i, 0, originalLine, line);
                setLine(i, line);
            }
        }
        statusMessage = "Replaced " + std::to_string(count) + " occurrences";
        updateSyntaxState();
    }

    void runGotoLine() {
        std::string numStr = promptInput("Go to Line", {{"^C", "Cancel"}});
        if (numStr.empty()) return;
        
        try {
            int lineNum = std::stoi(numStr);
            if (lineNum < 1) lineNum = 1;
            if (lineNum > getLineCount()) lineNum = getLineCount();
            
            cursorY = lineNum - 1;
            cursorX = 0;
            ensureCursorVisible();
        } catch (...) {
            statusMessage = "Invalid Line Number";
        }
    }

    void showHelp() {
        system("cls");
        std::cout << "\n  Lino HELP\n";
        std::cout << "  =========\n\n";
        std::cout << "  Arrow Keys  - Move cursor\n";
        std::cout << "  Enter       - New line\n\n";
        std::cout << "  Ctrl+X      - Exit\n";
        std::cout << "  Ctrl+O      - Save\n";
        std::cout << "  Ctrl+Z      - Undo\n";
        std::cout << "  Ctrl+Y      - Redo\n";
        std::cout << "  Ctrl+F      - Interactive Search\n";
        std::cout << "  Ctrl+R      - Replace\n";
        std::cout << "\n  SEARCH MODE:\n";
        std::cout << "  Arrows      - Navigate matches\n";
        std::cout << "  Ctrl+X      - Exit Search\n";
        std::cout << "\n  Press any key to continue...";
        waitForInput();
    }

    bool confirmExit() {
        if (!modified) return true;
        
        while (true) {
             // 1. Draw Background for Footer Area
            // Red Aesthetic
            WORD promptAttr = BACKGROUND_RED | BACKGROUND_INTENSITY | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) | FOREGROUND_INTENSITY; 
            WORD defaultAttr = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
            
            int promptY = screenHeight - 3;
            
            // Clear prompt line
            bufferFill(0, promptY, screenWidth, ' ', promptAttr);
            std::string prompt = "Save modified buffer? (Answering \"No\" will DISCARD changes)";
            bufferWrite(0, promptY, " " + prompt, promptAttr);
            
             // Clear shortcut lines to black
            bufferFill(0, screenHeight - 2, screenWidth, ' ', 0);
            bufferFill(0, screenHeight - 1, screenWidth, ' ', 0);
            
            // Draw options
            std::vector<std::pair<std::string, std::string>> options = {
                {"Y", "Yes"}, {"N", "No"}, {"^C", "Cancel"}
            };
            
            int x = 0;
            int y = screenHeight - 2;
            for (const auto& sc : options) {
                bufferWrite(x, y, sc.first, FOREGROUND_RED | FOREGROUND_INTENSITY); 
                x += (int)sc.first.length();
                bufferWrite(x, y, " " + sc.second + "  ", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                x += (int)sc.second.length() + 3;
            }
            
            flushBuffer();
            
            int ch = waitForInput();
            if (ch == 'y' || ch == 'Y') { 
                saveFile(); 
                return true; 
            }
            if (ch == 'n' || ch == 'N') {
                return true;
            }
            if (ch == 'c' || ch == 'C' || ch == 27) { 
                statusMessage = ""; 
                return false; 
            }
        }
    }

    // PROCESS INPUT DISPATCHER
    bool processSingleInput(int ch) {
        if (appState == MENU) {
            return processMenuInput(ch);
        } else if (appState == FILE_BROWSER) {
            return processBrowserInput(ch);
        } else {
            return processEditorInput(ch);
        }
    }

    bool processMenuInput(int ch) {
        if (ch == 0 || ch == 224) {
            ch = waitForInput();
            if (ch == 72) { // Up
                menuIndex--;
                if (menuIndex < 0) menuIndex = (int)menuOptions.size() - 1;
            } else if (ch == 80) { // Down
                menuIndex++;
                if (menuIndex >= (int)menuOptions.size()) menuIndex = 0;
            }
        } else if (ch == 13) { // Enter
            if (menuIndex == 0) { // New File
                appState = EDITOR;
                // Reset to empty file
                lineOffsets.clear();
                totalLineCount = 0;  // Set to 0 since insertedLines[0] will represent the first line
                lineCache.clear();
                insertedLines.clear();
                insertedLines[0] = "";
                deletedLines.clear();
                filename = "";
                fileExtension = "";
                cursorX = 0;
                cursorY = 0;
                scrollOffsetX = 0;
                scrollOffsetY = 0;
                modified = false;
                initUndoFiles();  // Reset undo/redo state
                statusMessage = "New File";
            } else if (menuIndex == 1) { // Open File
                appState = FILE_BROWSER;
                loadBrowserFiles();
            }
        } else if (ch == 27) { // Esc
             // Exit?
        }
        return true;
    }

    void loadBrowserFiles() {
        browserFiles.clear();
        browserIndex = 0;
        try {
            // Add parent dir ".."
             browserFiles.push_back(fs::directory_entry(currentBrowserPath.parent_path())); // Placeholder logic
             // Actually, the directory_entry for ".." is hard to forge cleanly, 
             // let's just create entries from iteration.
             
             // We'll treat index 0 as ".." if not root
             
             for (const auto& entry : fs::directory_iterator(currentBrowserPath)) {
                 browserFiles.push_back(entry);
             }
             // Sort: Dirs first, then files
             std::sort(browserFiles.begin(), browserFiles.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                 if (a.is_directory() && !b.is_directory()) return true;
                 if (!a.is_directory() && b.is_directory()) return false;
                 return a.path().filename().string() < b.path().filename().string();
             });
        } catch (...) {}
    }

    bool processBrowserInput(int ch) {
        if (ch == 0 || ch == 224) {
            ch = waitForInput();
             if (ch == 72) { // Up
                browserIndex--;
                if (browserIndex < -1) browserIndex = (int)browserFiles.size() - 1; 
                // index -1 is ".."
            } else if (ch == 80) { // Down
                browserIndex++;
                if (browserIndex >= (int)browserFiles.size()) browserIndex = -1;
            }
        } else if (ch == 13) { // Enter
            if (browserIndex == -1) {
                // Go up
                if (currentBrowserPath.has_parent_path()) {
                    currentBrowserPath = currentBrowserPath.parent_path();
                    loadBrowserFiles();
                }
            } else if (browserIndex >= 0 && browserIndex < (int)browserFiles.size()) {
                const auto& entry = browserFiles[browserIndex];
                if (entry.is_directory()) {
                    currentBrowserPath = entry.path();
                    loadBrowserFiles();
                } else {
                    loadFile(entry.path().string());
                    filename = entry.path().string();
                    fs::path p(filename);
                    if (p.has_extension()) fileExtension = p.extension().string();
                    selectSyntax();
                    appState = EDITOR;
                }
            }
        } else if (ch == 27) { // Esc
            appState = MENU;
        }
        return true;
    }

    bool processEditorInput(int ch) {
        statusMessage = "";
        
        if (currentMode == SEARCH) {
            if (ch == 0 || ch == 224) {
                ch = waitForInput();
                switch (ch) {
                    case 77: // Right - Next Match
                    case 80: // Down
                        if (!searchResults.empty()) {
                            searchIdx = (searchIdx + 1) % searchResults.size();
                            cursorY = searchResults[searchIdx].first;
                            cursorX = searchResults[searchIdx].second;
                            ensureCursorVisible();
                        }
                        break;
                    case 75: // Left - Prev Match
                    case 72: // Up
                        if (!searchResults.empty()) {
                            searchIdx = (searchIdx - 1 + (int)searchResults.size()) % searchResults.size();
                            cursorY = searchResults[searchIdx].first;
                            cursorX = searchResults[searchIdx].second;
                            ensureCursorVisible();
                        }
                        break;
                }
            } else if (ch == 24) { // Ctrl+X
                currentMode = NORMAL;
                highlightTerm.clear();
                searchResults.clear();
                statusMessage = "Exited Search";
            }
            return true;
        }

        // NORMAL MODE
        if (ch == 0 || ch == 224) {
            ch = waitForInput();
            switch (ch) {
                case 72: moveCursorUp(); break;
                case 80: moveCursorDown(); break;
                case 75: moveCursorLeft(); break;
                case 77: moveCursorRight(); break;
                case 71: cursorX = 0; break;
                case 79: cursorX = (int)getLine(cursorY).length(); break;
                case 73: for (int i = 0; i < screenHeight - 4; i++) moveCursorUp(); break;
                case 81: for (int i = 0; i < screenHeight - 4; i++) moveCursorDown(); break;
                case 83: deleteCharForward(); break;
            }
        } else if (ch == 26) { // Ctrl+Z
            undo();
        } else if (ch == 25) { // Ctrl+Y
            redo();
        } else if (ch == 6) { // Ctrl+F
            runSearch();
        } else if (ch == 18) { // Ctrl+R
            runReplace();
        } else if (ch == 24) {
            if (confirmExit()) {
                cleanupUndoFiles();
                running = false;
            }
        } else if (ch == 15) {
            saveFile();
        } else if (ch == 7) {
            showHelp();
            return false; // Force full redraw after help
        } else if (ch == 11) {
            cutLine();
        } else if (ch == 21) {
            pasteLine();
        } else if (ch == 20) { // Ctrl+T
            runGotoLine();
        } else if (ch == 23) {
            // Ctrl+W legacy support
            runSearch();
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
        // Close any existing file handle
        if (fileHandle.is_open()) {
            fileHandle.close();
        }
        
        // Clear all state
        lineCache.clear();
        dirtyLines.clear();
        insertedLines.clear();
        deletedLines.clear();
        initUndoFiles();  // Reset undo/redo temp files
        
        // Check if file exists
        if (!fs::exists(path)) {
            // New file
            totalLineCount = 1;
            lineOffsets.clear();
            lineOffsets.push_back(0);
            insertedLines[0] = "";
            statusMessage = "New file";
            return;
        }
        
        // Build line index (fast scan, doesn't load content)
        buildLineIndex(path);
        
        // Open file handle for reading
        fileHandle.open(path, std::ios::in | std::ios::binary);
        
        if (totalLineCount == 0) {
            totalLineCount = 1;
            insertedLines[0] = "";
        }
        
        statusMessage = "Loaded " + std::to_string(totalLineCount) + " lines (lazy)";
    }

public:
    LinoEditor(const std::string& filepath = "") 
        : filename(filepath), cursorX(0), cursorY(0), scrollOffsetY(0), scrollOffsetX(0),
          screenWidth(80), screenHeight(25), modified(false), running(true),
          needsFullRedraw(true) {
        
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        hIn = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

        getTerminalSize();
        screenBuffer.resize(screenWidth * screenHeight);
        
        // Load syntax plugins
        // Load syntax plugins
        loadPlugins();
        
        // Initialize undo/redo temp files
        initUndoFiles();
        
        // Extract file extension
        if (!filepath.empty()) {
            appState = EDITOR;
            fs::path p(filepath);
            if (p.has_extension()) {
                fileExtension = p.extension().string();
            }
            loadFile(filepath);
        } else {
            appState = MENU;
            currentBrowserPath = fs::current_path();
            // No file specified - start with empty buffer
            totalLineCount = 0;  // Set to 0 since insertedLines[0] will represent the first line
            insertedLines[0] = "";
            statusMessage = "Welcome to Lino";
        }
        
        // Select syntax based on extension
        selectSyntax();
    }

    void run() {
        std::cout << "\x1b[?1049h";
        std::cout << "\x1b[2J\x1b[H" << std::flush;
        
        refreshScreen();
        
        while (running) {
             int ch = waitForInput();
             
             if (needsFullRedraw) {
                 getTerminalSize(); // Update dimensions
                 screenBuffer.resize(screenWidth * screenHeight);
                 refreshScreen();
                 needsFullRedraw = false;
             }

             if (ch != 0) {
                 if (!processSingleInput(ch)) {
                     break;
                 }
                 refreshScreen();
             }
        }
        
        std::cout << "\x1b[?1049l" << std::flush;
    }
};

int main(int argc, char* argv[]) {
    // Force US Standard Layout to fix "Dead Keys" (double press for quotes)
    HKL hUsLayout = LoadKeyboardLayoutA("00000409", KLF_ACTIVATE | KLF_SUBSTITUTE_OK);
    if (hUsLayout) {
        ActivateKeyboardLayout(hUsLayout, KLF_SETFORPROCESS);
    }

    SetConsoleOutputCP(CP_UTF8);
    
    std::string filename;
    if (argc > 1) filename = argv[1];
    
    LinoEditor editor(filename);
    editor.run();
    
    return 0;
}
