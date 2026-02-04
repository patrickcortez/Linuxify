// Compile: Part of Lino TUI - include from main.cpp

#ifndef WIDGETS_HPP
#define WIDGETS_HPP

#include "tui_core.hpp"
#include <deque>

namespace TUI {

class Label : public Widget {
public:
    std::string text;
    WORD color = Colors::BG_NORMAL | Colors::FG_TEXT;
    
    Label(const std::string& t = "") : text(t) { bounds.width = t.size(); bounds.height = 1; }
    void draw(Buffer& buf) override {
        buf.write(bounds.x, bounds.y, text, color);
    }
};

class Button : public Widget {
public:
    std::string label;
    bool isDefault = false;
    
    Button(const std::string& l = "Button") : label(l) { 
        bounds.width = l.size() + 4; bounds.height = 1; 
    }
    
    void draw(Buffer& buf) override {
        WORD attr = focused ? Colors::BUTTON_FOCUSED : Colors::BUTTON_NORMAL;
        std::string text = "[ " + label + " ]";
        buf.write(bounds.x, bounds.y, text, attr);
    }
    
    bool onMouse(const MouseEvent& e) override {
        if (e.evType == MouseEvent::M_CLICK && onClick) { onClick(); return true; }
        return false;
    }
    
    bool onKey(const KeyEvent& e) override {
        if ((e.key == 13 || e.key == ' ') && onClick) { onClick(); return true; }
        return false;
    }
};

class TextBox : public Widget {
public:
    std::string text;
    int cursorPos = 0;
    int scrollOffset = 0;
    
    TextBox() { bounds.width = 20; bounds.height = 1; }
    
    void draw(Buffer& buf) override {
        buf.fill(bounds.x, bounds.y, bounds.width, 1, ' ', Colors::TEXTBOX);
        int viewWidth = bounds.width - 1;
        std::string visible = text.substr(scrollOffset, viewWidth);
        buf.write(bounds.x, bounds.y, visible, Colors::TEXTBOX);
        if (focused) {
            int cx = bounds.x + cursorPos - scrollOffset;
            if (cx >= bounds.x && cx < bounds.x + bounds.width)
                buf.set(cx, bounds.y, '_', Colors::TEXTBOX | FOREGROUND_RED);
        }
    }
    
    bool onKey(const KeyEvent& e) override {
        if (e.key >= 32 && e.key < 127) {
            text.insert(cursorPos, 1, (char)e.key);
            cursorPos++;
            ensureVisible();
            return true;
        }
        if (e.key == 8 && cursorPos > 0) {
            text.erase(cursorPos - 1, 1);
            cursorPos--;
            ensureVisible();
            return true;
        }
        if (e.key == 256 + VK_LEFT && cursorPos > 0) { cursorPos--; ensureVisible(); return true; }
        if (e.key == 256 + VK_RIGHT && cursorPos < (int)text.size()) { cursorPos++; ensureVisible(); return true; }
        if (e.key == 256 + VK_HOME) { cursorPos = 0; ensureVisible(); return true; }
        if (e.key == 256 + VK_END) { cursorPos = text.size(); ensureVisible(); return true; }
        return false;
    }
    
    bool onMouse(const MouseEvent& e) override {
        if (e.evType == MouseEvent::M_CLICK) {
            cursorPos = std::min((int)text.size(), scrollOffset + e.x - bounds.x);
            return true;
        }
        return false;
    }
    
    void ensureVisible() {
        int viewWidth = bounds.width - 1;
        if (cursorPos < scrollOffset) scrollOffset = cursorPos;
        if (cursorPos >= scrollOffset + viewWidth) scrollOffset = cursorPos - viewWidth + 1;
    }
};

class ListBox : public Widget {
public:
    std::vector<std::string> items;
    int selectedIndex = 0;
    int scrollOffset = 0;
    std::function<void(int)> onSelect;
    
    ListBox() { bounds.width = 20; bounds.height = 10; }
    
    void draw(Buffer& buf) override {
        buf.drawBox(bounds.x, bounds.y, bounds.width, bounds.height, Colors::BORDER);
        int viewHeight = bounds.height - 2;
        for (int i = 0; i < viewHeight && scrollOffset + i < (int)items.size(); i++) {
            int idx = scrollOffset + i;
            WORD attr = (idx == selectedIndex) ? Colors::MENU_SELECTED : Colors::MENU_ITEM;
            std::string item = items[idx].substr(0, bounds.width - 3);
            item.resize(bounds.width - 3, ' ');
            buf.write(bounds.x + 1, bounds.y + 1 + i, item, attr);
        }
        drawScrollbar(buf);
    }
    
    void drawScrollbar(Buffer& buf) {
        if (items.empty()) return;
        int viewHeight = bounds.height - 2;
        int sbX = bounds.x + bounds.width - 1;
        int thumbPos = items.size() > (size_t)viewHeight ? 
            scrollOffset * (viewHeight - 1) / (items.size() - viewHeight) : 0;
        for (int i = 0; i < viewHeight; i++)
            buf.set(sbX, bounds.y + 1 + i, (i == thumbPos) ? '#' : ':', Colors::SCROLLBAR);
    }
    
    bool onKey(const KeyEvent& e) override {
        int viewHeight = bounds.height - 2;
        if (e.key == 256 + VK_UP && selectedIndex > 0) {
            selectedIndex--;
            if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
            if (onSelect) onSelect(selectedIndex);
            return true;
        }
        if (e.key == 256 + VK_DOWN && selectedIndex < (int)items.size() - 1) {
            selectedIndex++;
            if (selectedIndex >= scrollOffset + viewHeight) scrollOffset = selectedIndex - viewHeight + 1;
            if (onSelect) onSelect(selectedIndex);
            return true;
        }
        if (e.key == 13 && onSelect) { onSelect(selectedIndex); return true; }
        return false;
    }
    
    bool onMouse(const MouseEvent& e) override {
        if (e.evType == MouseEvent::M_CLICK) {
            int idx = scrollOffset + e.y - bounds.y - 1;
            if (idx >= 0 && idx < (int)items.size()) {
                selectedIndex = idx;
                if (onSelect) onSelect(selectedIndex);
                return true;
            }
        }
        if (e.evType == MouseEvent::M_SCROLL) {
            scrollOffset = std::clamp(scrollOffset + e.scrollDelta, 0, std::max(0, (int)items.size() - bounds.height + 2));
            return true;
        }
        return false;
    }
};

struct MenuItem {
    std::string label;
    std::string shortcut;
    std::function<void()> action;
    bool separator = false;
};

class DropdownMenu : public Widget {
public:
    std::vector<MenuItem> items;
    int selectedIndex = 0;
    
    void draw(Buffer& buf) override {
        buf.drawBox(bounds.x, bounds.y, bounds.width, bounds.height, Colors::BORDER);
        for (int i = 0; i < (int)items.size(); i++) {
            WORD attr = (i == selectedIndex) ? Colors::MENU_SELECTED : Colors::MENU_ITEM;
            if (items[i].separator) {
                std::string sep(bounds.width - 2, '-');
                buf.write(bounds.x + 1, bounds.y + 1 + i, sep, Colors::BORDER);
            } else {
                std::string line = " " + items[i].label;
                line.resize(bounds.width - 2 - items[i].shortcut.size(), ' ');
                line += items[i].shortcut + " ";
                buf.write(bounds.x + 1, bounds.y + 1 + i, line.substr(0, bounds.width - 2), attr);
            }
        }
    }
    
    bool onKey(const KeyEvent& e) override {
        if (e.key == 256 + VK_UP && selectedIndex > 0) { selectedIndex--; return true; }
        if (e.key == 256 + VK_DOWN && selectedIndex < (int)items.size() - 1) { selectedIndex++; return true; }
        if (e.key == 13 && items[selectedIndex].action) { items[selectedIndex].action(); return true; }
        return false;
    }
    
    bool onMouse(const MouseEvent& e) override {
        if (e.evType == MouseEvent::M_CLICK || e.evType == MouseEvent::M_MOVE) {
            int idx = e.y - bounds.y - 1;
            if (idx >= 0 && idx < (int)items.size() && !items[idx].separator) {
                selectedIndex = idx;
                if (e.evType == MouseEvent::M_CLICK && items[idx].action) items[idx].action();
                return true;
            }
        }
        return false;
    }
};

class MenuBar : public Widget {
public:
    struct Menu { std::string label; std::vector<MenuItem> items; int x; };
    std::vector<Menu> menus;
    int activeMenu = -1;
    std::shared_ptr<DropdownMenu> openDropdown;
    
    MenuBar() { bounds.height = 1; }
    
    void addMenu(const std::string& label, const std::vector<MenuItem>& items) {
        int x = 1;
        for (auto& m : menus) x += m.label.size() + 2;
        menus.push_back({label, items, x});
    }
    
    void draw(Buffer& buf) override {
        buf.fill(bounds.x, bounds.y, bounds.width, 1, ' ', Colors::MENU_BAR);
        for (int i = 0; i < (int)menus.size(); i++) {
            WORD attr = (i == activeMenu) ? Colors::MENU_SELECTED : Colors::MENU_BAR;
            buf.write(bounds.x + menus[i].x, bounds.y, " " + menus[i].label + " ", attr);
        }
        if (openDropdown) openDropdown->draw(buf);
    }
    
    bool onMouse(const MouseEvent& e) override {
        if (e.y == bounds.y && e.evType == MouseEvent::M_CLICK) {
            for (int i = 0; i < (int)menus.size(); i++) {
                int start = bounds.x + menus[i].x;
                int end = start + menus[i].label.size() + 2;
                if (e.x >= start && e.x < end) {
                    if (activeMenu == i) closeMenu();
                    else openMenu(i);
                    return true;
                }
            }
            closeMenu();
            return true;
        }
        if (openDropdown && openDropdown->bounds.contains(e.x, e.y))
            return openDropdown->onMouse(e);
        if (e.evType == MouseEvent::M_CLICK) closeMenu();
        return false;
    }
    
    bool onKey(const KeyEvent& e) override {
        if (openDropdown) {
            if (e.key == 27) { closeMenu(); return true; }
            if (e.key == 256 + VK_LEFT) { openMenu((activeMenu - 1 + menus.size()) % menus.size()); return true; }
            if (e.key == 256 + VK_RIGHT) { openMenu((activeMenu + 1) % menus.size()); return true; }
            return openDropdown->onKey(e);
        }
        if (e.alt && e.key >= 'a' && e.key <= 'z') {
            char c = e.key - 32;
            for (int i = 0; i < (int)menus.size(); i++)
                if (!menus[i].label.empty() && menus[i].label[0] == c) { openMenu(i); return true; }
        }
        return false;
    }
    
    void openMenu(int idx) {
        activeMenu = idx;
        auto& m = menus[idx];
        openDropdown = std::make_shared<DropdownMenu>();
        openDropdown->items = m.items;
        openDropdown->bounds = {bounds.x + m.x, bounds.y + 1, 25, (int)m.items.size() + 2};
    }
    
    void closeMenu() { activeMenu = -1; openDropdown = nullptr; }
};

}
#endif
