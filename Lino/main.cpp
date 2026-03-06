// Compile: g++ -std=c++17 -static -o Lino/lino.exe Lino/main.cpp

#include "dialogs.hpp"
#include "widgets.hpp"
#include <fstream>
#include <sstream>
#include <deque>
#include <filesystem>
#include <processenv.h>

namespace fs = std::filesystem;

namespace TUI {

#pragma pack(push, 1)
struct DiskLineRecord {
    uint8_t src; // 0=none, 1=orig, 2=cache
    uint64_t offset;
    uint32_t length;
};
#pragma pack(pop)

class DiskIndexGapBuffer {
private:
    std::fstream file;
    std::string tmppath;
    std::string expandTmpPath;
    size_t gapStart;
    size_t gapEnd;
    size_t cap;

    void expand() {
        size_t newCap = cap * 2;
        std::fstream newFile(expandTmpPath, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
        
        DiskLineRecord rec;
        file.clear(); file.seekg(0, std::ios::beg);
        for (size_t i = 0; i < gapStart; i++) {
            file.read((char*)&rec, sizeof(rec));
            newFile.write((char*)&rec, sizeof(rec));
        }
        
        size_t newGapEnd = newCap - (cap - gapEnd);
        
        DiskLineRecord blank = {0,0,0};
        for(size_t i=gapStart; i<newGapEnd; i++) {
            newFile.write((char*)&blank, sizeof(blank));
        }
        
        file.clear();
        file.seekg(gapEnd * sizeof(rec), std::ios::beg);
        for(size_t i=0; i < (cap - gapEnd); i++) {
            file.read((char*)&rec, sizeof(rec));
            newFile.write((char*)&rec, sizeof(rec));
        }
        
        file.close();
        std::remove(tmppath.c_str());
        std::rename(expandTmpPath.c_str(), tmppath.c_str());
        file.open(tmppath, std::ios::in | std::ios::out | std::ios::binary);
        
        gapEnd = newGapEnd;
        cap = newCap;
    }

public:
    DiskIndexGapBuffer() : tmppath(""), expandTmpPath(""), gapStart(0), gapEnd(1024), cap(1024) {}

    void setup(const std::string& path, const std::string& expandPath, size_t initialCapacity = 1024) {
        tmppath = path;
        expandTmpPath = expandPath;
        gapStart = 0;
        gapEnd = initialCapacity;
        cap = initialCapacity;
        
        file.open(tmppath, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
        DiskLineRecord blank = {0,0,0};
        for(size_t i=0; i<cap; i++) file.write((char*)&blank, sizeof(blank));
    }
    
    ~DiskIndexGapBuffer() {
        if (file.is_open()) file.close();
        std::remove(tmppath.c_str());
    }

    void moveGap(size_t pos) {
        size_t sz = size();
        if (pos > sz) pos = sz;
        if (pos == gapStart) return;
        
        DiskLineRecord rec;
        if (pos < gapStart) {
            size_t count = gapStart - pos;
            for (size_t i = 0; i < count; i++) {
                file.clear(); file.seekg((gapStart - 1 - i) * sizeof(rec), std::ios::beg);
                file.read((char*)&rec, sizeof(rec));
                file.clear(); file.seekp((gapEnd - 1 - i) * sizeof(rec), std::ios::beg);
                file.write((char*)&rec, sizeof(rec));
            }
            gapStart -= count;
            gapEnd -= count;
        } else {
            size_t count = pos - gapStart;
            for (size_t i = 0; i < count; i++) {
                file.clear(); file.seekg((gapEnd + i) * sizeof(rec), std::ios::beg);
                file.read((char*)&rec, sizeof(rec));
                file.clear(); file.seekp((gapStart + i) * sizeof(rec), std::ios::beg);
                file.write((char*)&rec, sizeof(rec));
            }
            gapStart += count;
            gapEnd += count;
        }
    }

    void insert(size_t pos, const DiskLineRecord& item) {
        if (gapStart == gapEnd) expand();
        moveGap(pos);
        file.clear(); file.seekp(gapStart * sizeof(item), std::ios::beg);
        file.write((char*)&item, sizeof(item));
        gapStart++;
    }

    void remove(size_t pos) {
        moveGap(pos + 1);
        if (gapStart > 0) gapStart--;
    }

    DiskLineRecord operator[](size_t idx) {
        DiskLineRecord rec = {0,0,0};
        file.clear();
        if (idx < gapStart) {
            file.seekg(idx * sizeof(rec), std::ios::beg);
        } else {
            file.seekg((idx + (gapEnd - gapStart)) * sizeof(rec), std::ios::beg);
        }
        file.read((char*)&rec, sizeof(rec));
        return rec;
    }
    
    void set(size_t idx, const DiskLineRecord& rec) {
        file.clear();
        if (idx < gapStart) {
            file.seekp(idx * sizeof(rec), std::ios::beg);
        } else {
            file.seekp((idx + (gapEnd - gapStart)) * sizeof(rec), std::ios::beg);
        }
        file.write((char*)&rec, sizeof(rec));
    }

    size_t size() const {
        return cap - (gapEnd - gapStart); // Elements count, not bytes
    }

    void clear() {
        if (file.is_open()) file.close();
        std::remove(tmppath.c_str());
        gapStart = 0;
        gapEnd = cap;
    }
    
    bool isOpen() const { return file.is_open(); }
    void close() {
        if (file.is_open()) file.close();
        std::remove(tmppath.c_str());
    }
};

struct LineDiff {
    enum Type { MODIFY, INSERT, REMOVE };
    Type type;
    int lineIdx;
    DiskLineRecord oldRecord;
};

struct UndoEntry {
    std::vector<LineDiff> diffs;
    int cursorX, cursorY;
};

struct UndoManager {
    std::deque<UndoEntry> undoStack;
    std::deque<UndoEntry> redoStack;
    static const int MAX_HISTORY = 512;

    void record(UndoEntry entry) {
        if (undoStack.size() >= MAX_HISTORY) undoStack.pop_front();
        undoStack.push_back(std::move(entry));
        redoStack.clear();
    }

    void clear() {
        undoStack.clear();
        redoStack.clear();
    }
};

struct Document {
    DiskIndexGapBuffer lines;
    std::fstream origFile;
    std::fstream cacheFile;
    std::string cachePath;
    std::string filename;
    bool modified = false;
    int cursorX = 0, cursorY = 0;
    int scrollX = 0, scrollY = 0;
    UndoManager undoMgr;
    
    Document() {
        DWORD pid = GetCurrentProcessId();
        std::string tmpDir = fs::temp_directory_path().string();
        cachePath = tmpDir + "/lino_chars_" + std::to_string(pid) + ".tmp";
        
        // Reinitialize the lines buffer with explicit temp paths 
        std::string idxPath = tmpDir + "/lino_idx_" + std::to_string(pid) + ".tmp";
        std::string idxExpand = tmpDir + "/lino_idx2_" + std::to_string(pid) + ".tmp";
        lines.setup(idxPath, idxExpand);
        
        cacheFile.open(cachePath, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
        uint8_t zero = 0; cacheFile.write((char*)&zero, 1);
        insertLine(0, ""); 
    }
    
    ~Document() {
        if (origFile.is_open()) origFile.close();
        if (cacheFile.is_open()) cacheFile.close();
        std::remove(cachePath.c_str());
    }
    
    void clear() {
        if (origFile.is_open()) origFile.close();
        
        DWORD pid = GetCurrentProcessId();
        std::string tmpDir = fs::temp_directory_path().string();
        std::string idxPath = tmpDir + "/lino_idx_" + std::to_string(pid) + ".tmp";
        std::string idxExpand = tmpDir + "/lino_idx2_" + std::to_string(pid) + ".tmp";
        if (lines.isOpen()) lines.close();
        lines.setup(idxPath, idxExpand);
        
        cacheFile.close();
        cacheFile.open(cachePath, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
        uint8_t zero = 0; cacheFile.write((char*)&zero, 1);
        insertLine(0, "");
        filename = "";
        modified = false;
        cursorX = cursorY = scrollX = scrollY = 0;
        undoMgr.clear();
    }
    
    bool load(const std::string& path) {
        if (origFile.is_open()) origFile.close();
        origFile.open(path, std::ios::in | std::ios::binary);
        if (!origFile) return false;
        
        DWORD pid = GetCurrentProcessId();
        std::string tmpDir = fs::temp_directory_path().string();
        std::string idxPath = tmpDir + "/lino_idx_" + std::to_string(pid) + ".tmp";
        std::string idxExpand = tmpDir + "/lino_idx2_" + std::to_string(pid) + ".tmp";
        if (lines.isOpen()) lines.close();
        lines.setup(idxPath, idxExpand);
        
        cacheFile.close(); 
        cacheFile.open(cachePath, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
        uint8_t zero = 0; cacheFile.write((char*)&zero, 1);
        
        origFile.clear(); origFile.seekg(0, std::ios::end);
        uint64_t fileSize = origFile.tellg();
        origFile.clear(); origFile.seekg(0, std::ios::beg);
        
        uint64_t currentOffset = 0;
        char buffer[8192];
        uint64_t startOfLine = 0;
        
        while (currentOffset < fileSize) {
            origFile.read(buffer, sizeof(buffer));
            int bytesRead = origFile.gcount();
            
            for (int i = 0; i < bytesRead; i++) {
                if (buffer[i] == '\n') {
                    uint64_t absolutePos = currentOffset + i;
                    uint64_t len = absolutePos - startOfLine;
                    if (len > 0) {
                        origFile.clear(); origFile.seekg(absolutePos - 1, std::ios::beg);
                        char prev; origFile.read(&prev, 1);
                        if (prev == '\r') len--;
                        origFile.clear(); origFile.seekg(currentOffset + bytesRead, std::ios::beg);
                    }
                    DiskLineRecord rec = {1, startOfLine, (uint32_t)len};
                    lines.insert(lines.size(), rec);
                    startOfLine = absolutePos + 1;
                }
            }
            currentOffset += bytesRead;
        }
        
        if (startOfLine < fileSize) {
            DiskLineRecord rec = {1, startOfLine, (uint32_t)(fileSize - startOfLine)};
            lines.insert(lines.size(), rec);
        }
        
        if (lines.size() == 0) insertLine(0, "");
        
        filename = path;
        modified = false;
        cursorX = cursorY = scrollX = scrollY = 0;
        undoMgr.clear();
        return true;
    }
    
    bool save(const std::string& path = "") {
        std::string savePath = path.empty() ? filename : path;
        if (savePath.empty()) return false;
        
        std::string tmpSave = savePath + ".saving";
        std::ofstream out(tmpSave, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        
        for (int i = 0; i < lines.size(); i++) {
            std::string content = getLine(i);
            if (content.size() > 0) out.write(content.c_str(), content.size());
            if (i < lines.size() - 1) out.write("\n", 1);
        }
        out.close();
        
        if (origFile.is_open()) origFile.close();
        
#ifdef _WIN32
        DeleteFileA(savePath.c_str());
        MoveFileA(tmpSave.c_str(), savePath.c_str());
#else
        std::rename(tmpSave.c_str(), savePath.c_str());
#endif
        
        // Let's reload to reset mapping properly onto the freshly saved clean file.
        load(savePath);
        return true;
    }
    
    std::string getLine(int idx) {
        if (idx < 0 || idx >= lines.size()) return "";
        DiskLineRecord rec = lines[idx];
        if (rec.length == 0 || rec.src == 0) return "";
        
        std::string res(rec.length, '\0');
        if (rec.src == 1) {
            origFile.clear();
            origFile.seekg(rec.offset, std::ios::beg);
            origFile.read(&res[0], rec.length);
        } else if (rec.src == 2) {
            cacheFile.clear();
            cacheFile.seekg(rec.offset, std::ios::beg);
            cacheFile.read(&res[0], rec.length);
        }
        return res;
    }
    
    DiskLineRecord cacheString(const std::string& str) {
        cacheFile.clear();
        cacheFile.seekp(0, std::ios::end);
        uint64_t offset = cacheFile.tellp();
        if (str.size() > 0) cacheFile.write(str.c_str(), str.size());
        return { 2, offset, (uint32_t)str.size() };
    }
    
    void setLine(int idx, const std::string& content) {
        if (idx < 0 || idx >= lines.size()) return;
        DiskLineRecord rec = cacheString(content);
        lines.set(idx, rec);
    }
    
    void insertLine(int idx, const std::string& content) {
        DiskLineRecord rec = cacheString(content);
        lines.insert(idx, rec);
    }
    
    int lineCount() const { return lines.size(); }
};

class EditWidget : public Widget {
public:
    Document* doc = nullptr;
    int lineNumWidth = 5;
    int tabWidth = 4;
    std::string searchTerm;
    
    // UTF-8 Helpers
    int utf8_prev(const std::string& str, int pos) {
        while (pos > 0) {
            pos--;
            if ((str[pos] & 0xC0) != 0x80) break;
        }
        return pos;
    }

    int utf8_next(const std::string& str, int pos) {
        if (pos >= str.length()) return pos;
        if ((str[pos] & 0x80) == 0) return pos + 1; // ASCII
        
        int len = 1;
        while (pos + len < str.length() && (str[pos + len] & 0xC0) == 0x80) {
            len++;
        }
        return pos + len;
    }

    void setSearchTerm(const std::string& term) { searchTerm = term; }
    void clearSearch() { searchTerm.clear(); }
    
    void draw(Buffer& buf) override {
        if (!doc) return;
        buf.fill(bounds.x, bounds.y, bounds.width, bounds.height, ' ', Colors::EDIT_AREA);
        
        int viewHeight = bounds.height;
        int viewWidth = bounds.width - lineNumWidth - 1;
        
        for (int i = 0; i < viewHeight; i++) {
            int lineIdx = doc->scrollY + i;
            if (lineIdx >= doc->lineCount()) break;
            
            char lineNum[16];
            snprintf(lineNum, sizeof(lineNum), "%4d ", lineIdx + 1);
            buf.write(bounds.x, bounds.y + i, lineNum, Colors::BG_NORMAL | FOREGROUND_RED);
            
            std::string line = doc->getLine(lineIdx);
            int start = doc->scrollX;
            std::string visible = (start < (int)line.size()) ? line.substr(start, viewWidth) : "";
            buf.write(bounds.x + lineNumWidth, bounds.y + i, visible, Colors::EDIT_AREA);
            
            if (!searchTerm.empty() && !line.empty()) {
                size_t searchLen = searchTerm.size();
                size_t pos = 0;
                while ((pos = line.find(searchTerm, pos)) != std::string::npos) {
                    for (size_t c = 0; c < searchLen; c++) {
                        int screenCol = (int)(pos + c) - doc->scrollX;
                        if (screenCol >= 0 && screenCol < viewWidth) {
                            buf.set(bounds.x + lineNumWidth + screenCol, bounds.y + i,
                                    line[pos + c], Colors::SEARCH_HIGHLIGHT);
                        }
                    }
                    pos++;
                }
            }
        }
        
        int cursorScreenX = bounds.x + lineNumWidth + (doc->cursorX - doc->scrollX);
        int cursorScreenY = bounds.y + (doc->cursorY - doc->scrollY);
        if (cursorScreenX >= bounds.x + lineNumWidth && cursorScreenX < bounds.x + bounds.width &&
            cursorScreenY >= bounds.y && cursorScreenY < bounds.y + bounds.height) {
            std::string currentLine = doc->getLine(doc->cursorY);
            char ch = (doc->cursorX < (int)currentLine.size()) ? currentLine[doc->cursorX] : ' ';
            buf.set(cursorScreenX, cursorScreenY, ch, BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        }
    }
    
    bool onKey(const KeyEvent& e) override {
        if (!doc) return false;
        
        if (e.key >= 32 && e.key < 127 && !e.ctrl && !e.alt) {
            std::string curLine = doc->getLine(doc->cursorY);
            UndoEntry ue;
            ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
            ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY, doc->lines[doc->cursorY]});
            doc->undoMgr.record(std::move(ue));
            curLine.insert(doc->cursorX, 1, (char)e.key);
            doc->setLine(doc->cursorY, curLine);
            doc->cursorX = utf8_next(curLine, doc->cursorX);
            doc->modified = true;
            ensureVisible();
            return true;
        }
        
        switch (e.key) {
            case 13: {
                std::string curLine = doc->getLine(doc->cursorY);
                UndoEntry ue;
                ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
                ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY, doc->lines[doc->cursorY]});
                std::string rest = curLine.substr(doc->cursorX);
                curLine = curLine.substr(0, doc->cursorX);
                
                doc->setLine(doc->cursorY, curLine);
                doc->insertLine(doc->cursorY + 1, rest);
                
                ue.diffs.push_back({LineDiff::INSERT, doc->cursorY + 1, {0,0,0}});
                doc->undoMgr.record(std::move(ue));
                
                doc->cursorY++;
                doc->cursorX = 0;
                doc->modified = true;
                ensureVisible();
                return true;
            }
            case 8:
                if (doc->cursorX > 0) {
                    std::string curLine = doc->getLine(doc->cursorY);
                    UndoEntry ue;
                    ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
                    ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY, doc->lines[doc->cursorY]});
                    doc->undoMgr.record(std::move(ue));
                    
                    int prevX = utf8_prev(curLine, doc->cursorX);
                    int charLen = doc->cursorX - prevX;
                    
                    curLine.erase(prevX, charLen);
                    doc->setLine(doc->cursorY, curLine);
                    doc->cursorX = prevX;
                    doc->modified = true;
                } else if (doc->cursorY > 0) {
                    std::string prevLine = doc->getLine(doc->cursorY - 1);
                    std::string curLine = doc->getLine(doc->cursorY);
                    UndoEntry ue;
                    ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
                    ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY - 1, doc->lines[doc->cursorY - 1]});
                    ue.diffs.push_back({LineDiff::REMOVE, doc->cursorY, doc->lines[doc->cursorY]});
                    doc->undoMgr.record(std::move(ue));
                    doc->cursorX = prevLine.size();
                    prevLine += curLine;
                    doc->setLine(doc->cursorY - 1, prevLine);
                    doc->lines.remove(doc->cursorY);
                    doc->cursorY--;
                    doc->modified = true;
                }
                ensureVisible();
                return true;
            case 9: {
                std::string curLine = doc->getLine(doc->cursorY);
                UndoEntry ue;
                ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
                ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY, doc->lines[doc->cursorY]});
                doc->undoMgr.record(std::move(ue));
                std::string spaces(tabWidth, ' ');
                curLine.insert(doc->cursorX, spaces);
                doc->setLine(doc->cursorY, curLine);
                doc->cursorX += tabWidth;
                doc->modified = true;
                return true;
            }
            case 256 + VK_UP:
                if (doc->cursorY > 0) doc->cursorY--;
                doc->cursorX = std::min(doc->cursorX, (int)doc->getLine(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_DOWN:
                if (doc->cursorY < doc->lineCount() - 1) doc->cursorY++;
                doc->cursorX = std::min(doc->cursorX, (int)doc->getLine(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_LEFT:
                if (doc->cursorX > 0) doc->cursorX = utf8_prev(doc->getLine(doc->cursorY), doc->cursorX);
                else if (doc->cursorY > 0) { doc->cursorY--; doc->cursorX = doc->getLine(doc->cursorY).size(); }
                ensureVisible();
                return true;
            case 256 + VK_RIGHT:
                if (doc->cursorX < (int)doc->getLine(doc->cursorY).size()) doc->cursorX = utf8_next(doc->getLine(doc->cursorY), doc->cursorX);
                else if (doc->cursorY < doc->lineCount() - 1) { doc->cursorY++; doc->cursorX = 0; }
                ensureVisible();
                return true;
            case 256 + VK_HOME:
                doc->cursorX = 0;
                ensureVisible();
                return true;
            case 256 + VK_END:
                doc->cursorX = doc->getLine(doc->cursorY).size();
                ensureVisible();
                return true;
            case 256 + VK_PRIOR:
                doc->cursorY = std::max(0, doc->cursorY - bounds.height);
                doc->cursorX = std::min(doc->cursorX, (int)doc->getLine(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_NEXT:
                doc->cursorY = std::min(doc->lineCount() - 1, doc->cursorY + bounds.height);
                doc->cursorX = std::min(doc->cursorX, (int)doc->getLine(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_DELETE:
                std::string curLine = doc->getLine(doc->cursorY);
                if (doc->cursorX < (int)curLine.size()) {
                    UndoEntry ue;
                    ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
                    ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY, doc->lines[doc->cursorY]});
                    doc->undoMgr.record(std::move(ue));
                    
                    int nextX = utf8_next(curLine, doc->cursorX);
                    int charLen = nextX - doc->cursorX;
                    
                    curLine.erase(doc->cursorX, charLen);
                    doc->setLine(doc->cursorY, curLine);
                    doc->modified = true;
                } else if (doc->cursorY < doc->lineCount() - 1) {
                    std::string nextLine = doc->getLine(doc->cursorY + 1);
                    UndoEntry ue;
                    ue.cursorX = doc->cursorX; ue.cursorY = doc->cursorY;
                    ue.diffs.push_back({LineDiff::MODIFY, doc->cursorY, doc->lines[doc->cursorY]});
                    ue.diffs.push_back({LineDiff::REMOVE, doc->cursorY + 1, doc->lines[doc->cursorY + 1]});
                    doc->undoMgr.record(std::move(ue));
                    curLine += nextLine;
                    doc->setLine(doc->cursorY, curLine);
                    doc->lines.remove(doc->cursorY + 1);
                    doc->modified = true;
                }
                return true;
        }
        return false;
    }
    
    bool onMouse(const MouseEvent& e) override {
        if (!doc) return false;
        
        if (e.evType == MouseEvent::M_CLICK) {
            int clickX = e.x - bounds.x - lineNumWidth + doc->scrollX;
            int clickY = e.y - bounds.y + doc->scrollY;
            if (clickY >= 0 && clickY < doc->lineCount()) {
                doc->cursorY = clickY;
                doc->cursorX = std::min(std::max(0, clickX), (int)doc->getLine(clickY).size());
            }
            return true;
        }
        
        if (e.evType == MouseEvent::M_SCROLL) {
            doc->scrollY = std::clamp(doc->scrollY + e.scrollDelta, 0, std::max(0, doc->lineCount() - bounds.height));
            return true;
        }
        
        return false;
    }
    
    void ensureVisible() {
        if (!doc) return;
        int viewHeight = bounds.height;
        int viewWidth = bounds.width - lineNumWidth - 1;
        if (doc->cursorY < doc->scrollY) doc->scrollY = doc->cursorY;
        if (doc->cursorY >= doc->scrollY + viewHeight) doc->scrollY = doc->cursorY - viewHeight + 1;
        if (doc->cursorX < doc->scrollX) doc->scrollX = doc->cursorX;
        if (doc->cursorX >= doc->scrollX + viewWidth) doc->scrollX = doc->cursorX - viewWidth + 1;
    }
};

}

using namespace TUI;

class LinoApp {
    HANDLE hConsole;
    InputManager input;
    Buffer buffer;
    Document doc;
    
    std::shared_ptr<MenuBar> menuBar;
    std::shared_ptr<EditWidget> editor;
    std::shared_ptr<Dialog> activeDialog;
    
    int width, height;
    bool running = true;
    std::string statusMsg;
    std::string lastSearchTerm;
    
public:
    LinoApp() {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        DWORD outMode = 0;
        GetConsoleMode(hConsole, &outMode);
        SetConsoleMode(hConsole, outMode | 0x0004);
        
        DWORD written;
        WriteConsoleA(hConsole, "\x1b[?1049h", 8, &written, NULL);
        
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        buffer.resize(width, height);
        
        CONSOLE_CURSOR_INFO cci = {1, FALSE};
        SetConsoleCursorInfo(hConsole, &cci);
        
        setupUI();
    }
    
    ~LinoApp() {
        DWORD written;
        WriteConsoleA(hConsole, "\x1b[?1049l", 8, &written, NULL);
    }
    
    void setupUI() {
        menuBar = std::make_shared<MenuBar>();
        menuBar->bounds = {0, 0, width, 1};
        
        menuBar->addMenu("File", {
            {"New", "Ctrl+N", [this]() { newFile(); }},
            {"Open", "Ctrl+O", [this]() { openFile(); }},
            {"Save", "Ctrl+S", [this]() { saveFile(); }},
            {"Save As", "", [this]() { saveFileAs(); }},
            {"", "", nullptr, true},
            {"Exit", "Ctrl+Q", [this]() { quit(); }}
        });
        
        menuBar->addMenu("Edit", {
            {"Undo", "Ctrl+Z", [this]() {
                if (doc.undoMgr.undoStack.empty()) { statusMsg = "Nothing to undo"; return; }
                UndoEntry& entry = doc.undoMgr.undoStack.back();
                UndoEntry redo;
                redo.cursorX = doc.cursorX; redo.cursorY = doc.cursorY;
                for (int i = (int)entry.diffs.size() - 1; i >= 0; i--) {
                    LineDiff& d = entry.diffs[i];
                    switch (d.type) {
                        case LineDiff::MODIFY:
                            redo.diffs.push_back({LineDiff::MODIFY, d.lineIdx, doc.lines[d.lineIdx]});
                            doc.lines.set(d.lineIdx, d.oldRecord);
                            break;
                        case LineDiff::INSERT:
                            redo.diffs.push_back({LineDiff::REMOVE, d.lineIdx, doc.lines[d.lineIdx]});
                            doc.lines.remove(d.lineIdx); 
                            break;
                        case LineDiff::REMOVE:
                            doc.lines.insert(d.lineIdx, d.oldRecord); 
                            redo.diffs.push_back({LineDiff::INSERT, d.lineIdx, {0,0,0}});
                            break;  
                    }
                }
                doc.cursorX = entry.cursorX; doc.cursorY = entry.cursorY;
                doc.undoMgr.undoStack.pop_back();
                if (doc.undoMgr.redoStack.size() >= UndoManager::MAX_HISTORY) doc.undoMgr.redoStack.pop_front();
                doc.undoMgr.redoStack.push_back(std::move(redo));
                doc.modified = true;
                editor->ensureVisible();
                statusMsg = "Undo";
            }},
            {"Redo", "Ctrl+Y", [this]() {
                if (doc.undoMgr.redoStack.empty()) { statusMsg = "Nothing to redo"; return; }
                UndoEntry& entry = doc.undoMgr.redoStack.back();
                UndoEntry undo;
                undo.cursorX = doc.cursorX; undo.cursorY = doc.cursorY;
                for (int i = (int)entry.diffs.size() - 1; i >= 0; i--) {
                    LineDiff& d = entry.diffs[i];
                    switch (d.type) {
                        case LineDiff::MODIFY:
                            undo.diffs.push_back({LineDiff::MODIFY, d.lineIdx, doc.lines[d.lineIdx]});
                            doc.lines.set(d.lineIdx, d.oldRecord);
                            break;
                        case LineDiff::INSERT:
                            undo.diffs.push_back({LineDiff::REMOVE, d.lineIdx, doc.lines[d.lineIdx]});
                            doc.lines.remove(d.lineIdx);
                            break;
                        case LineDiff::REMOVE:
                            doc.lines.insert(d.lineIdx, d.oldRecord);
                            undo.diffs.push_back({LineDiff::INSERT, d.lineIdx, {0,0,0}});
                            break;
                    }
                }
                doc.cursorX = entry.cursorX; doc.cursorY = entry.cursorY;
                doc.undoMgr.redoStack.pop_back();
                if (doc.undoMgr.undoStack.size() >= UndoManager::MAX_HISTORY) doc.undoMgr.undoStack.pop_front();
                doc.undoMgr.undoStack.push_back(std::move(undo));
                doc.modified = true;
                editor->ensureVisible();
                statusMsg = "Redo";
            }},
            {"", "", nullptr, true},
            {"Cut", "Ctrl+X", [this]() { statusMsg = "Cut"; }},
            {"Copy", "Ctrl+C", [this]() { statusMsg = "Copy"; }},
            {"Paste", "Ctrl+V", [this]() { statusMsg = "Paste"; }}
        });
        
        menuBar->addMenu("Search", {
            {"Find", "Ctrl+F", [this]() { showSearch(); }},
            {"Find Next", "F3", [this]() { findNext(); }},
            {"Go to Line", "Ctrl+G", [this]() { gotoLine(); }}
        });
        
        menuBar->addMenu("Themes", {
            {"Light", "", [this]() { TUI::Colors::setLightMode(); render(); }},
            {"Dark", "", [this]() { TUI::Colors::setDarkMode(); render(); }}
        });

        menuBar->addMenu("Help", {
            {"About", "", [this]() { showAbout(); }}
        });
        
        editor = std::make_shared<EditWidget>();
        editor->bounds = {0, 1, width, height - 2};
        editor->doc = &doc;
    }
    
    void newFile() {
        doc.clear();
        statusMsg = "New file";
        menuBar->closeMenu();
    }
    
    void openFile() {
        menuBar->closeMenu();
        auto dlg = std::make_shared<FileDialog>(false);
        dlg->center(width, height);
        activeDialog = dlg;
        
        while (!dlg->closed) {
            render();
            processInput();
        }
        activeDialog = nullptr;
        
        if (dlg->result && !dlg->selectedFile.empty()) {
            if (doc.load(dlg->selectedFile))
                statusMsg = "Opened: " + dlg->selectedFile;
            else
                statusMsg = "Failed to open file";
        }
    }
    
    void saveFile() {
        menuBar->closeMenu();
        if (doc.filename.empty()) { saveFileAs(); return; }
        if (doc.save()) statusMsg = "Saved: " + doc.filename;
        else statusMsg = "Failed to save";
    }
    
    void saveFileAs() {
        menuBar->closeMenu();
        auto dlg = std::make_shared<FileDialog>(true);
        dlg->center(width, height);
        activeDialog = dlg;
        
        while (!dlg->closed) {
            render();
            processInput();
        }
        activeDialog = nullptr;
        
        if (dlg->result && !dlg->selectedFile.empty()) {
            if (doc.save(dlg->selectedFile))
                statusMsg = "Saved: " + dlg->selectedFile;
            else
                statusMsg = "Failed to save";
        }
    }
    
    void findFirst(const std::string& term) {
        if (term.empty()) return;
        lastSearchTerm = term;
        editor->setSearchTerm(term);
        for (int i = 0; i < doc.lineCount(); i++) {
            size_t pos = doc.getLine(i).find(term, 0);
            if (pos != std::string::npos) {
                doc.cursorY = i;
                doc.cursorX = pos;
                statusMsg = "Found";
                editor->ensureVisible();
                return;
            }
        }
        statusMsg = "Not found";
    }
    
    void findNext(bool skipCurrent = true) {
        if (lastSearchTerm.empty()) return;
        editor->setSearchTerm(lastSearchTerm);
        bool found = false;
        int startOffset = skipCurrent ? 1 : 0;
        for (int i = doc.cursorY; i < doc.lineCount() && !found; i++) {
            size_t startPos = (i == doc.cursorY) ? doc.cursorX + startOffset : 0;
            size_t pos = doc.getLine(i).find(lastSearchTerm, startPos);
            if (pos != std::string::npos) {
                doc.cursorY = i;
                doc.cursorX = pos;
                found = true;
            }
        }
        if (!found) {
            for (int i = 0; i < doc.lineCount() && !found; i++) {
                size_t pos = doc.getLine(i).find(lastSearchTerm, 0);
                if (pos != std::string::npos) {
                    doc.cursorY = i;
                    doc.cursorX = pos;
                    found = true;
                }
            }
        }
        statusMsg = found ? "Found" : "Not found";
        editor->ensureVisible();
    }
    
    void showSearch() {
        menuBar->closeMenu();
        auto dlg = std::make_shared<SearchDialog>();
        dlg->center(width, height);
        dlg->syncBounds();
        activeDialog = dlg;
        
        dlg->onFind = [this](const std::string& term) {
            findFirst(term);
        };
        
        dlg->onReplace = [this](const std::string& term, const std::string& rep) {
            for (int i = 0; i < doc.lineCount(); i++) {
                std::string curLine = doc.getLine(i);
                size_t pos = curLine.find(term, 0);
                if (pos != std::string::npos) {
                    UndoEntry ue;
                    ue.cursorX = doc.cursorX; ue.cursorY = doc.cursorY;
                    ue.diffs.push_back({LineDiff::MODIFY, i, doc.lines[i]});
                    doc.undoMgr.record(std::move(ue));
                    curLine.erase(pos, term.size());
                    curLine.insert(pos, rep);
                    
                    doc.setLine(i, curLine);
                    doc.cursorY = i;
                    doc.cursorX = pos;
                    doc.modified = true;
                    statusMsg = "Replaced";
                    editor->ensureVisible();
                    lastSearchTerm = term;
                    editor->setSearchTerm(term);
                    return;
                }
            }
            statusMsg = "Not found";
        };
        
        dlg->onReplaceAll = [this](const std::string& term, const std::string& rep) {
            UndoEntry ue;
            ue.cursorX = doc.cursorX; ue.cursorY = doc.cursorY;
            int count = 0;
            for (int i = 0; i < doc.lineCount(); i++) {
                std::string curLine = doc.getLine(i);
                size_t pos = curLine.find(term, 0);
                if (pos != std::string::npos) {
                    ue.diffs.push_back({LineDiff::MODIFY, i, doc.lines[i]});
                    while ((pos = curLine.find(term, pos)) != std::string::npos) {
                        curLine.erase(pos, term.size());
                        curLine.insert(pos, rep);
                        pos += rep.size();
                        count++;
                    }
                    doc.setLine(i, curLine);
                }
            }
            if (count > 0) {
                doc.undoMgr.record(std::move(ue));
                doc.modified = true;
            }
            statusMsg = "Replaced " + std::to_string(count) + " occurrence(s)";
            editor->clearSearch();
            lastSearchTerm.clear();
        };
        
        while (!dlg->closed) {
            render();
            processInput();
        }
        activeDialog = nullptr;
        
        if (!dlg->result) {
            editor->clearSearch();
        }
    }
    
    void gotoLine() {
        menuBar->closeMenu();
        auto dlg = std::make_shared<InputDialog>("Go to Line", "Line:");
        dlg->center(width, height);
        activeDialog = dlg;
        
        while (!dlg->closed) {
            render();
            processInput();
        }
        activeDialog = nullptr;
        
        if (dlg->result) {
            try {
                int line = std::stoi(dlg->getValue()) - 1;
                if (line >= 0 && line < doc.lineCount()) {
                    doc.cursorY = line;
                    doc.cursorX = 0;
                    editor->ensureVisible();
                    statusMsg = "Line " + std::to_string(line + 1);
                }
            } catch (...) {}
        }
    }
    
    void showAbout() {
        menuBar->closeMenu();
        auto dlg = std::make_shared<MsgBox>("About", "Lino Editor v2.1 Disk-Backed");
        dlg->center(width, height);
        activeDialog = dlg;
        
        while (!dlg->closed) {
            render();
            processInput();
        }
        activeDialog = nullptr;
    }
    
    void quit() {
        if (doc.modified) {
            auto dlg = std::make_shared<MsgBox>("Warning", "Unsaved changes!", MsgBox::YES_NO_CANCEL);
            dlg->center(width, height);
            activeDialog = dlg;
            while (!dlg->closed) { render(); processInput(); }
            int res = dlg->result;
            activeDialog = nullptr;
            
            if (res == 2) {
                // Save and quit
                saveFile();
                if (doc.modified) return; // If save failed or was cancelled, don't quit
            } else if (res == 0) {
                // Cancel quit
                return;
            }
            // If res == 1 (OK/Discard), proceed to quit without saving
        }
        running = false;
    }
    
    void render() {
        buffer.clear(Colors::EDIT_AREA);
        editor->draw(buffer);
        menuBar->draw(buffer);
        
        buffer.fill(0, height - 1, width, 1, ' ', Colors::STATUS_BAR);
        std::string status = doc.filename.empty() ? "[New File]" : doc.filename;
        if (doc.modified) status += " *";
        status += "  Ln " + std::to_string(doc.cursorY + 1) + ", Col " + std::to_string(doc.cursorX + 1);
        if (!statusMsg.empty()) status += " | " + statusMsg;
        buffer.write(1, height - 1, status.substr(0, width - 2), Colors::STATUS_BAR);
        
        if (activeDialog) activeDialog->draw(buffer);
        
        buffer.flush(hConsole);
    }
    
    void processInput() {
        KeyEvent key;
        MouseEvent mouse;
        bool resized;
        
        if (!input.poll(key, mouse, resized)) return;
        
        if (resized) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hConsole, &csbi);
            width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            buffer.resize(width, height);
            menuBar->bounds.width = width;
            editor->bounds = {0, 1, width, height - 2};
            return;
        }
        
        if (activeDialog) {
            if (key.key) activeDialog->onKey(key);
            if (mouse.evType != MouseEvent::M_NONE) activeDialog->onMouse(mouse);
            return;
        }
        
        if (!key.ctrl && !key.alt && key.key == 256 + VK_F3) {
            findNext();
            return;
        }
        
        if (key.ctrl) {
            switch (key.key) {
                case 14: newFile(); return;
                case 15: openFile(); return;
                case 19: saveFile(); return;
                case 17: quit(); return;
                case 6: showSearch(); return;
                case 7: gotoLine(); return;
                case 26: {
                    auto& editMenu = menuBar->menus[1];
                    if (editMenu.items.size() > 0 && editMenu.items[0].action) editMenu.items[0].action();
                    return;
                }
                case 25: {
                    auto& editMenu = menuBar->menus[1];
                    if (editMenu.items.size() > 1 && editMenu.items[1].action) editMenu.items[1].action();
                    return;
                }
            }
        }
        
        if (menuBar->openDropdown || (key.alt && key.key)) {
            if (menuBar->onKey(key)) return;
        }
        
        if (mouse.evType != MouseEvent::M_NONE) {
            if (mouse.y == 0) { menuBar->onMouse(mouse); return; }
            if (menuBar->openDropdown && menuBar->openDropdown->bounds.contains(mouse.x, mouse.y)) {
                menuBar->onMouse(mouse); return;
            }
            if (mouse.evType == MouseEvent::M_CLICK && menuBar->openDropdown) {
                menuBar->closeMenu();
            }
            editor->onMouse(mouse);
            return;
        }
        
        if (key.key) editor->onKey(key);
    }
    
    void run(const std::string& filepath = "") {
        if (!filepath.empty()) doc.load(filepath);
        
        while (running) {
            render();
            processInput();
        }
    }
};

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    LinoApp app;
    app.run(argc > 1 ? argv[1] : "");
    
    return 0;
}
