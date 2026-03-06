// Compile: Part of Lino TUI - include from main.cpp

#ifndef DIALOGS_HPP
#define DIALOGS_HPP

#include "widgets.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace TUI {

class Dialog : public Widget {
public:
    std::string title;
    bool closed = false;
    int result = 0;
    
    Dialog(const std::string& t, int w, int h) : title(t) {
        bounds.width = w; bounds.height = h;
    }
    
    void center(int screenW, int screenH) {
        bounds.x = (screenW - bounds.width) / 2;
        bounds.y = (screenH - bounds.height) / 2;
    }
    
    void draw(Buffer& buf) override {
        buf.fill(bounds.x, bounds.y, bounds.width, bounds.height, ' ', Colors::BG_NORMAL);
        buf.drawBox(bounds.x, bounds.y, bounds.width, bounds.height, Colors::BORDER);
        
        std::string titleBar(bounds.width - 2, ' ');
        int titleStart = (bounds.width - 2 - title.size()) / 2;
        for (size_t i = 0; i < title.size(); i++)
            if (titleStart + i < titleBar.size()) titleBar[titleStart + i] = title[i];
        buf.write(bounds.x + 1, bounds.y, titleBar, Colors::BG_DARK | Colors::FG_HIGHLIGHT);
        
        Widget::draw(buf);
    }
    
    bool onKey(const KeyEvent& e) override {
        if (e.key == 27) { closed = true; result = 0; return true; }
        for (auto& c : children) if (c->focused && c->onKey(e)) return true;
        if (e.key == 9) { focusNext(); return true; }
        return false;
    }
    
    bool onMouse(const MouseEvent& e) override {
        for (auto& c : children) {
            if (c->bounds.contains(e.x, e.y)) {
                for (auto& ch : children) ch->focused = false;
                c->focused = true;
                return c->onMouse(e);
            }
        }
        return false;
    }
    
    void focusNext() {
        int cur = -1;
        for (int i = 0; i < (int)children.size(); i++) if (children[i]->focused) cur = i;
        for (auto& c : children) c->focused = false;
        children[(cur + 1) % children.size()]->focused = true;
    }
};

class MsgBox : public Dialog {
public:
    enum Type { OK, YES_NO_CANCEL };
    std::string message;
    std::shared_ptr<Button> okBtn;
    std::shared_ptr<Button> yesBtn;
    std::shared_ptr<Button> noBtn;
    std::shared_ptr<Button> cancelBtn;
    Type boxType;
    
    MsgBox(const std::string& title, const std::string& msg, Type type = OK) 
        : Dialog(title, std::max((int)msg.size() + 6, 40), 7), message(msg), boxType(type) {
        
        if (boxType == OK) {
            okBtn = std::make_shared<Button>("OK");
            okBtn->bounds.x = bounds.x + (bounds.width - okBtn->bounds.width) / 2;
            okBtn->bounds.y = bounds.y + bounds.height - 2;
            okBtn->focused = true;
            okBtn->onClick = [this]() { closed = true; result = 1; };
            addChild(okBtn);
        } else if (boxType == YES_NO_CANCEL) {
            yesBtn = std::make_shared<Button>("Save");
            noBtn = std::make_shared<Button>("OK");
            cancelBtn = std::make_shared<Button>("Cancel");
            
            int totalWidth = yesBtn->bounds.width + noBtn->bounds.width + cancelBtn->bounds.width + 4; // 2 spaces between buttons
            int startX = bounds.x + (bounds.width - totalWidth) / 2;
            
            yesBtn->bounds.x = startX;
            yesBtn->bounds.y = bounds.y + bounds.height - 2;
            yesBtn->focused = true;
            yesBtn->onClick = [this]() { closed = true; result = 2; }; // Save
            addChild(yesBtn);
            
            noBtn->bounds.x = startX + yesBtn->bounds.width + 2;
            noBtn->bounds.y = bounds.y + bounds.height - 2;
            noBtn->onClick = [this]() { closed = true; result = 1; }; // Discard (OK)
            addChild(noBtn);
            
            cancelBtn->bounds.x = noBtn->bounds.x + noBtn->bounds.width + 2;
            cancelBtn->bounds.y = bounds.y + bounds.height - 2;
            cancelBtn->onClick = [this]() { closed = true; result = 0; }; // Cancel
            addChild(cancelBtn);
        }
    }
    
    void draw(Buffer& buf) override {
        Dialog::draw(buf);
        buf.write(bounds.x + 3, bounds.y + 2, message, Colors::BG_NORMAL | Colors::FG_TEXT);
        
        if (boxType == OK && okBtn) {
            okBtn->bounds.x = bounds.x + (bounds.width - okBtn->bounds.width) / 2;
            okBtn->bounds.y = bounds.y + bounds.height - 2;
        } else if (boxType == YES_NO_CANCEL && yesBtn && noBtn && cancelBtn) {
            int totalWidth = yesBtn->bounds.width + noBtn->bounds.width + cancelBtn->bounds.width + 4;
            int startX = bounds.x + (bounds.width - totalWidth) / 2;
            
            yesBtn->bounds.x = startX;
            yesBtn->bounds.y = bounds.y + bounds.height - 2;
            
            noBtn->bounds.x = startX + yesBtn->bounds.width + 2;
            noBtn->bounds.y = bounds.y + bounds.height - 2;
            
            cancelBtn->bounds.x = noBtn->bounds.x + noBtn->bounds.width + 2;
            cancelBtn->bounds.y = bounds.y + bounds.height - 2;
        }
    }
};

class FileDialog : public Dialog {
public:
    std::shared_ptr<TextBox> filenameBox;
    std::shared_ptr<ListBox> fileList;
    std::shared_ptr<ListBox> dirList;
    std::shared_ptr<Button> okBtn;
    std::shared_ptr<Button> cancelBtn;
    fs::path currentPath;
    std::string selectedFile;
    bool isSave;
    
    FileDialog(bool save = false) : Dialog(save ? "Save" : "Open", 70, 20), isSave(save) {
        currentPath = fs::current_path();
        
        filenameBox = std::make_shared<TextBox>();
        filenameBox->bounds = {bounds.x + 12, bounds.y + 2, bounds.width - 14, 1};
        filenameBox->focused = true;
        addChild(filenameBox);
        
        fileList = std::make_shared<ListBox>();
        fileList->bounds = {bounds.x + 2, bounds.y + 5, 32, 11};
        fileList->onSelect = [this](int idx) {
            if (idx >= 0 && idx < (int)fileList->items.size())
                filenameBox->text = fileList->items[idx];
        };
        addChild(fileList);
        
        dirList = std::make_shared<ListBox>();
        dirList->bounds = {bounds.x + 36, bounds.y + 5, 32, 11};
        dirList->onSelect = [this](int idx) {
            if (idx >= 0 && idx < (int)dirList->items.size()) {
                std::string dir = dirList->items[idx];
                if (dir == "..") currentPath = currentPath.parent_path();
                else currentPath = currentPath / dir;
                loadDirectory();
            }
        };
        addChild(dirList);
        
        okBtn = std::make_shared<Button>(save ? "Save" : "Open");
        okBtn->bounds = {bounds.x + 2, bounds.y + bounds.height - 2, 10, 1};
        okBtn->onClick = [this]() {
            if (!filenameBox->text.empty()) {
                selectedFile = (currentPath / filenameBox->text).string();
                closed = true; result = 1;
            }
        };
        addChild(okBtn);
        
        cancelBtn = std::make_shared<Button>("Cancel");
        cancelBtn->bounds = {bounds.x + 14, bounds.y + bounds.height - 2, 10, 1};
        cancelBtn->onClick = [this]() { closed = true; result = 0; };
        addChild(cancelBtn);
        
        loadDirectory();
    }
    
    void loadDirectory() {
        fileList->items.clear();
        dirList->items.clear();
        dirList->items.push_back("..");
        
        try {
            for (auto& entry : fs::directory_iterator(currentPath)) {
                std::string name = entry.path().filename().string();
                if (entry.is_directory()) dirList->items.push_back(name);
                else fileList->items.push_back(name);
            }
        } catch (...) {}
        
        std::sort(fileList->items.begin(), fileList->items.end());
        std::sort(dirList->items.begin() + 1, dirList->items.end());
        fileList->selectedIndex = 0;
        fileList->scrollOffset = 0;
        dirList->selectedIndex = 0;
        dirList->scrollOffset = 0;
    }
    
    void draw(Buffer& buf) override {
        Dialog::draw(buf);
        buf.write(bounds.x + 2, bounds.y + 2, "File Name:", Colors::BG_NORMAL | Colors::FG_TEXT);
        buf.write(bounds.x + 2, bounds.y + 3, currentPath.string().substr(0, bounds.width - 4), 
                  Colors::BG_NORMAL | FOREGROUND_RED);
        buf.write(bounds.x + 2, bounds.y + 4, "Files:", Colors::BG_NORMAL | Colors::FG_TEXT);
        buf.write(bounds.x + 36, bounds.y + 4, "Directories:", Colors::BG_NORMAL | Colors::FG_TEXT);
        
        filenameBox->bounds.x = bounds.x + 12;
        filenameBox->bounds.y = bounds.y + 2;
        fileList->bounds.x = bounds.x + 2;
        fileList->bounds.y = bounds.y + 5;
        dirList->bounds.x = bounds.x + 36;
        dirList->bounds.y = bounds.y + 5;
        okBtn->bounds.x = bounds.x + 2;
        okBtn->bounds.y = bounds.y + bounds.height - 2;
        cancelBtn->bounds.x = bounds.x + 14;
        cancelBtn->bounds.y = bounds.y + bounds.height - 2;
    }
};

class InputDialog : public Dialog {
public:
    std::shared_ptr<TextBox> inputBox;
    std::shared_ptr<Button> okBtn;
    std::shared_ptr<Button> cancelBtn;
    std::string prompt;
    
    InputDialog(const std::string& title, const std::string& p, const std::string& defaultVal = "")
        : Dialog(title, 50, 7), prompt(p) {
        inputBox = std::make_shared<TextBox>();
        inputBox->text = defaultVal;
        inputBox->cursorPos = defaultVal.size();
        inputBox->bounds = {bounds.x + 3, bounds.y + 3, bounds.width - 6, 1};
        inputBox->focused = true;
        addChild(inputBox);
        
        okBtn = std::make_shared<Button>("OK");
        okBtn->bounds = {bounds.x + bounds.width / 2 - 12, bounds.y + 5, 8, 1};
        okBtn->onClick = [this]() { closed = true; result = 1; };
        addChild(okBtn);
        
        cancelBtn = std::make_shared<Button>("Cancel");
        cancelBtn->bounds = {bounds.x + bounds.width / 2 + 2, bounds.y + 5, 10, 1};
        cancelBtn->onClick = [this]() { closed = true; result = 0; };
        addChild(cancelBtn);
    }
    
    void draw(Buffer& buf) override {
        Dialog::draw(buf);
        buf.write(bounds.x + 3, bounds.y + 2, prompt, Colors::BG_NORMAL | Colors::FG_TEXT);
        inputBox->bounds.x = bounds.x + 3;
        inputBox->bounds.y = bounds.y + 3;
        okBtn->bounds.x = bounds.x + bounds.width / 2 - 12;
        okBtn->bounds.y = bounds.y + 5;
        cancelBtn->bounds.x = bounds.x + bounds.width / 2 + 2;
        cancelBtn->bounds.y = bounds.y + 5;
    }
    
    std::string getValue() const { return inputBox->text; }
};

class SearchDialog : public Dialog {
public:
    std::shared_ptr<TextBox> searchBox;
    std::shared_ptr<TextBox> replaceBox;
    std::shared_ptr<Button> findBtn;
    std::shared_ptr<Button> replaceBtn;
    std::shared_ptr<Button> replaceAllBtn;
    std::shared_ptr<Button> cancelBtn;
    
    std::string searchTerm;
    std::string replaceTerm;
    int action = 0;
    
    std::function<void(const std::string&)> onFind;
    std::function<void(const std::string&, const std::string&)> onReplace;
    std::function<void(const std::string&, const std::string&)> onReplaceAll;
    
    SearchDialog() : Dialog("Search & Replace", 70, 10) {
        searchBox = std::make_shared<TextBox>();
        searchBox->bounds = {bounds.x + 12, bounds.y + 2, 50, 1};
        searchBox->focused = true;
        addChild(searchBox);
        
        replaceBox = std::make_shared<TextBox>();
        replaceBox->bounds = {bounds.x + 12, bounds.y + 4, 50, 1};
        addChild(replaceBox);
        
        findBtn = std::make_shared<Button>("Find");
        findBtn->bounds = {bounds.x + 3, bounds.y + 7, 10, 1};
        findBtn->onClick = [this]() { 
            searchTerm = searchBox->text;
            replaceTerm = replaceBox->text;
            if (onFind && !searchTerm.empty()) onFind(searchTerm);
        };
        addChild(findBtn);
        
        replaceBtn = std::make_shared<Button>("Replace");
        replaceBtn->bounds = {bounds.x + 15, bounds.y + 7, 12, 1};
        replaceBtn->onClick = [this]() {
            searchTerm = searchBox->text;
            replaceTerm = replaceBox->text;
            if (onReplace && !searchTerm.empty()) onReplace(searchTerm, replaceTerm);
        };
        addChild(replaceBtn);
        
        replaceAllBtn = std::make_shared<Button>("Replace All");
        replaceAllBtn->bounds = {bounds.x + 29, bounds.y + 7, 15, 1};
        replaceAllBtn->onClick = [this]() {
            searchTerm = searchBox->text;
            replaceTerm = replaceBox->text;
            if (onReplaceAll && !searchTerm.empty()) {
                onReplaceAll(searchTerm, replaceTerm);
                closed = true; result = 1; action = 3;
            }
        };
        addChild(replaceAllBtn);
        
        cancelBtn = std::make_shared<Button>("Cancel");
        cancelBtn->bounds = {bounds.x + 50, bounds.y + 7, 10, 1};
        cancelBtn->onClick = [this]() { closed = true; result = 0; };
        addChild(cancelBtn);
    }
    
    void syncBounds() {
        searchBox->bounds = {bounds.x + 12, bounds.y + 2, 50, 1};
        replaceBox->bounds = {bounds.x + 12, bounds.y + 4, 50, 1};
        findBtn->bounds = {bounds.x + 3, bounds.y + 7, 10, 1};
        replaceBtn->bounds = {bounds.x + 15, bounds.y + 7, 12, 1};
        replaceAllBtn->bounds = {bounds.x + 29, bounds.y + 7, 15, 1};
        cancelBtn->bounds = {bounds.x + 50, bounds.y + 7, 10, 1};
    }
    
    bool onKey(const KeyEvent& e) override {
        if (e.key == 13 && (searchBox->focused || replaceBox->focused)) {
            searchTerm = searchBox->text;
            replaceTerm = replaceBox->text;
            if (onFind && !searchTerm.empty()) onFind(searchTerm);
            return true;
        }
        return Dialog::onKey(e);
    }
    
    bool onMouse(const MouseEvent& e) override {
        syncBounds();
        return Dialog::onMouse(e);
    }
    
    void draw(Buffer& buf) override {
        syncBounds();
        Dialog::draw(buf);
        buf.write(bounds.x + 3, bounds.y + 2, "Search:", Colors::BG_NORMAL | Colors::FG_TEXT);
        buf.write(bounds.x + 3, bounds.y + 4, "Replace:", Colors::BG_NORMAL | Colors::FG_TEXT);
    }
};

}
#endif
