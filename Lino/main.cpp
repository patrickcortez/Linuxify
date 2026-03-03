// Compile: g++ -std=c++17 -static -o Lino/lino.exe Lino/main.cpp

#include "dialogs.hpp"
#include <fstream>
#include <sstream>

namespace TUI {

struct Document {
    std::vector<std::string> lines;
    std::string filename;
    bool modified = false;
    int cursorX = 0, cursorY = 0;
    int scrollX = 0, scrollY = 0;
    
    Document() { lines.push_back(""); }
    
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file) return false;
        lines.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (lines.empty()) lines.push_back("");
        filename = path;
        modified = false;
        cursorX = cursorY = scrollX = scrollY = 0;
        return true;
    }
    
    bool save(const std::string& path = "") {
        std::string savePath = path.empty() ? filename : path;
        if (savePath.empty()) return false;
        std::ofstream file(savePath);
        if (!file) return false;
        for (size_t i = 0; i < lines.size(); i++) {
            file << lines[i];
            if (i < lines.size() - 1) file << '\n';
        }
        filename = savePath;
        modified = false;
        return true;
    }
    
    std::string& line(int idx) {
        static std::string empty;
        if (idx < 0 || idx >= (int)lines.size()) return empty;
        return lines[idx];
    }
    
    int lineCount() const { return lines.size(); }
};

class EditWidget : public Widget {
public:
    Document* doc = nullptr;
    int lineNumWidth = 5;
    std::string searchTerm;
    
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
            
            std::string& line = doc->line(lineIdx);
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
            char ch = (doc->cursorX < (int)doc->line(doc->cursorY).size()) ? 
                      doc->line(doc->cursorY)[doc->cursorX] : ' ';
            buf.set(cursorScreenX, cursorScreenY, ch, BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        }
    }
    
    bool onKey(const KeyEvent& e) override {
        if (!doc) return false;
        
        if (e.key >= 32 && e.key < 127 && !e.ctrl && !e.alt) {
            doc->line(doc->cursorY).insert(doc->cursorX, 1, (char)e.key);
            doc->cursorX++;
            doc->modified = true;
            ensureVisible();
            return true;
        }
        
        switch (e.key) {
            case 13: {
                std::string& cur = doc->line(doc->cursorY);
                std::string rest = cur.substr(doc->cursorX);
                cur = cur.substr(0, doc->cursorX);
                doc->lines.insert(doc->lines.begin() + doc->cursorY + 1, rest);
                doc->cursorY++;
                doc->cursorX = 0;
                doc->modified = true;
                ensureVisible();
                return true;
            }
            case 8:
                if (doc->cursorX > 0) {
                    doc->line(doc->cursorY).erase(doc->cursorX - 1, 1);
                    doc->cursorX--;
                    doc->modified = true;
                } else if (doc->cursorY > 0) {
                    doc->cursorX = doc->line(doc->cursorY - 1).size();
                    doc->line(doc->cursorY - 1) += doc->line(doc->cursorY);
                    doc->lines.erase(doc->lines.begin() + doc->cursorY);
                    doc->cursorY--;
                    doc->modified = true;
                }
                ensureVisible();
                return true;
            case 9:
                doc->line(doc->cursorY).insert(doc->cursorX, "    ");
                doc->cursorX += 4;
                doc->modified = true;
                return true;
            case 256 + VK_UP:
                if (doc->cursorY > 0) doc->cursorY--;
                doc->cursorX = std::min(doc->cursorX, (int)doc->line(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_DOWN:
                if (doc->cursorY < doc->lineCount() - 1) doc->cursorY++;
                doc->cursorX = std::min(doc->cursorX, (int)doc->line(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_LEFT:
                if (doc->cursorX > 0) doc->cursorX--;
                else if (doc->cursorY > 0) { doc->cursorY--; doc->cursorX = doc->line(doc->cursorY).size(); }
                ensureVisible();
                return true;
            case 256 + VK_RIGHT:
                if (doc->cursorX < (int)doc->line(doc->cursorY).size()) doc->cursorX++;
                else if (doc->cursorY < doc->lineCount() - 1) { doc->cursorY++; doc->cursorX = 0; }
                ensureVisible();
                return true;
            case 256 + VK_HOME:
                doc->cursorX = 0;
                ensureVisible();
                return true;
            case 256 + VK_END:
                doc->cursorX = doc->line(doc->cursorY).size();
                ensureVisible();
                return true;
            case 256 + VK_PRIOR:
                doc->cursorY = std::max(0, doc->cursorY - bounds.height);
                doc->cursorX = std::min(doc->cursorX, (int)doc->line(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_NEXT:
                doc->cursorY = std::min(doc->lineCount() - 1, doc->cursorY + bounds.height);
                doc->cursorX = std::min(doc->cursorX, (int)doc->line(doc->cursorY).size());
                ensureVisible();
                return true;
            case 256 + VK_DELETE:
                if (doc->cursorX < (int)doc->line(doc->cursorY).size()) {
                    doc->line(doc->cursorY).erase(doc->cursorX, 1);
                    doc->modified = true;
                } else if (doc->cursorY < doc->lineCount() - 1) {
                    doc->line(doc->cursorY) += doc->line(doc->cursorY + 1);
                    doc->lines.erase(doc->lines.begin() + doc->cursorY + 1);
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
                doc->cursorX = std::min(std::max(0, clickX), (int)doc->line(clickY).size());
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
    HANDLE hOriginalConsole;
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
        hOriginalConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        hConsole = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
        SetConsoleActiveScreenBuffer(hConsole);
        
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
        SetConsoleActiveScreenBuffer(hOriginalConsole);
        CloseHandle(hConsole);
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
        doc = Document();
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
            size_t pos = doc.lines[i].find(term, 0);
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
            size_t pos = doc.lines[i].find(lastSearchTerm, startPos);
            if (pos != std::string::npos) {
                doc.cursorY = i;
                doc.cursorX = pos;
                found = true;
            }
        }
        if (!found) {
            for (int i = 0; i < doc.lineCount() && !found; i++) {
                size_t pos = doc.lines[i].find(lastSearchTerm, 0);
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
                size_t pos = doc.lines[i].find(term, 0);
                if (pos != std::string::npos) {
                    doc.lines[i].erase(pos, term.size());
                    doc.lines[i].insert(pos, rep);
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
            int count = 0;
            for (int i = 0; i < doc.lineCount(); i++) {
                size_t pos = 0;
                while ((pos = doc.lines[i].find(term, pos)) != std::string::npos) {
                    doc.lines[i].erase(pos, term.size());
                    doc.lines[i].insert(pos, rep);
                    pos += rep.size();
                    count++;
                }
            }
            if (count > 0) doc.modified = true;
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
        auto dlg = std::make_shared<MsgBox>("About", "Lino Editor v2.0");
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
            auto dlg = std::make_shared<MsgBox>("Warning", "Unsaved changes!");
            dlg->center(width, height);
            activeDialog = dlg;
            while (!dlg->closed) { render(); processInput(); }
            activeDialog = nullptr;
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
