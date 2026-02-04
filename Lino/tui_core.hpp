// Compile: g++ -std=c++17 -static -o Lino/lino.exe Lino/main.cpp

#ifndef TUI_CORE_HPP
#define TUI_CORE_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>

namespace TUI {

namespace Colors {
    inline WORD BG_NORMAL = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
    inline WORD BG_DARK = BACKGROUND_RED;
    inline WORD BG_HIGHLIGHT = BACKGROUND_RED | BACKGROUND_INTENSITY;
    inline WORD FG_NORMAL = 0;
    inline WORD FG_TITLE = FOREGROUND_RED | FOREGROUND_INTENSITY;
    inline WORD FG_TEXT = 0;
    inline WORD FG_HIGHLIGHT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    inline WORD MENU_BAR = BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    inline WORD MENU_ITEM = BG_NORMAL | FG_TEXT;
    inline WORD MENU_SELECTED = BG_HIGHLIGHT | FG_HIGHLIGHT;
    inline WORD BUTTON_NORMAL = BG_NORMAL | FG_TEXT;
    inline WORD BUTTON_FOCUSED = BG_DARK | FG_HIGHLIGHT;
    inline WORD TEXTBOX = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
    inline WORD EDIT_AREA = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
    inline WORD STATUS_BAR = BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    inline WORD BORDER = BG_NORMAL | FOREGROUND_RED;
    inline WORD SCROLLBAR = BG_NORMAL | FOREGROUND_RED;

    inline void setLightMode() {
        BG_NORMAL = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
        BG_DARK = BACKGROUND_RED;
        BG_HIGHLIGHT = BACKGROUND_RED | BACKGROUND_INTENSITY;
        FG_NORMAL = 0;
        FG_TITLE = FOREGROUND_RED | FOREGROUND_INTENSITY;
        FG_TEXT = 0;
        FG_HIGHLIGHT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        MENU_BAR = BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        MENU_ITEM = BG_NORMAL | FG_TEXT;
        MENU_SELECTED = BG_HIGHLIGHT | FG_HIGHLIGHT;
        BUTTON_NORMAL = BG_NORMAL | FG_TEXT;
        BUTTON_FOCUSED = BG_DARK | FG_HIGHLIGHT;
        TEXTBOX = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        EDIT_AREA = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        STATUS_BAR = BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        BORDER = BG_NORMAL | FOREGROUND_RED;
        SCROLLBAR = BG_NORMAL | FOREGROUND_RED;
    }

    inline void setDarkMode() {
        BG_NORMAL = 0; 
        FG_TEXT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        BG_DARK = BACKGROUND_RED; 
        BG_HIGHLIGHT = BACKGROUND_RED;
        FG_NORMAL = FG_TEXT;
        FG_TITLE = FOREGROUND_RED | FOREGROUND_INTENSITY;
        FG_HIGHLIGHT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        MENU_BAR = BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        MENU_ITEM = 0 | FG_TEXT;
        MENU_SELECTED = BACKGROUND_RED | FG_TEXT;
        BUTTON_NORMAL = 0 | FG_TEXT;
        BUTTON_FOCUSED = BACKGROUND_RED | FG_TEXT;
        TEXTBOX = 0 | FG_TEXT;
        EDIT_AREA = 0 | FG_TEXT;
        STATUS_BAR = BACKGROUND_RED | FG_TEXT;
        BORDER = 0 | FOREGROUND_RED;
        SCROLLBAR = 0 | FOREGROUND_RED;
    }
}

struct TRect {
    int x, y, width, height;
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct Cell {
    char ch = ' ';
    WORD attr = Colors::BG_NORMAL;
};

class Buffer {
public:
    int width, height;
    std::vector<Cell> cells;
    
    Buffer(int w = 80, int h = 25) : width(w), height(h), cells(w * h) {}
    
    void resize(int w, int h) {
        width = w; height = h;
        cells.resize(w * h);
        clear();
    }
    
    void clear(WORD attr = Colors::BG_NORMAL) {
        for (auto& c : cells) { c.ch = ' '; c.attr = attr; }
    }
    
    void set(int x, int y, char ch, WORD attr) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            cells[y * width + x] = {ch, attr};
    }
    
    void write(int x, int y, const std::string& text, WORD attr) {
        for (size_t i = 0; i < text.size() && x + (int)i < width; i++)
            set(x + i, y, text[i], attr);
    }
    
    void fill(int x, int y, int w, int h, char ch, WORD attr) {
        for (int row = y; row < y + h; row++)
            for (int col = x; col < x + w; col++)
                set(col, row, ch, attr);
    }
    
    void drawBox(int x, int y, int w, int h, WORD attr) {
        set(x, y, '+', attr);
        set(x + w - 1, y, '+', attr);
        set(x, y + h - 1, '+', attr);
        set(x + w - 1, y + h - 1, '+', attr);
        for (int i = 1; i < w - 1; i++) { set(x + i, y, '-', attr); set(x + i, y + h - 1, '-', attr); }
        for (int i = 1; i < h - 1; i++) { set(x, y + i, '|', attr); set(x + w - 1, y + i, '|', attr); }
    }
    
    void flush(HANDLE hConsole) {
        std::vector<CHAR_INFO> buf(width * height);
        for (int i = 0; i < width * height; i++) {
            buf[i].Char.AsciiChar = cells[i].ch;
            buf[i].Attributes = cells[i].attr;
        }
        COORD size = {(SHORT)width, (SHORT)height};
        COORD pos = {0, 0};
        SMALL_RECT rect = {0, 0, (SHORT)(width - 1), (SHORT)(height - 1)};
        WriteConsoleOutputA(hConsole, buf.data(), size, pos, &rect);
    }
};

struct MouseEvent {
    int x, y;
    enum EventType { M_NONE, M_CLICK, M_RELEASE, M_MOVE, M_SCROLL, M_DOUBLE_CLICK } evType = M_NONE;
    int button = 0;
    int scrollDelta = 0;
};

struct KeyEvent {
    int key = 0;
    bool ctrl = false, alt = false, shift = false;
};

class InputManager {
    HANDLE hInput;
    DWORD oldMode;
public:
    InputManager() {
        hInput = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hInput, &oldMode);
        SetConsoleMode(hInput, ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
    }
    
    ~InputManager() { SetConsoleMode(hInput, oldMode); }
    
    bool poll(KeyEvent& key, MouseEvent& mouse, bool& resized) {
        key = {}; mouse = {}; resized = false;
        DWORD count;
        INPUT_RECORD ir;
        if (!ReadConsoleInput(hInput, &ir, 1, &count) || count == 0) return false;
        
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            auto& k = ir.Event.KeyEvent;
            key.key = k.uChar.AsciiChar ? k.uChar.AsciiChar : k.wVirtualKeyCode + 256;
            key.ctrl = k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
            key.alt = k.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);
            key.shift = k.dwControlKeyState & SHIFT_PRESSED;
            return true;
        }
        
        if (ir.EventType == MOUSE_EVENT) {
            auto& m = ir.Event.MouseEvent;
            mouse.x = m.dwMousePosition.X;
            mouse.y = m.dwMousePosition.Y;
            if (m.dwEventFlags == 0) {
                mouse.evType = (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) ? MouseEvent::M_CLICK : MouseEvent::M_RELEASE;
                mouse.button = (m.dwButtonState & RIGHTMOST_BUTTON_PRESSED) ? 1 : 0;
            } else if (m.dwEventFlags == MOUSE_MOVED) {
                mouse.evType = MouseEvent::M_MOVE;
            } else if (m.dwEventFlags == MOUSE_WHEELED) {
                mouse.evType = MouseEvent::M_SCROLL;
                mouse.scrollDelta = (short)HIWORD(m.dwButtonState) > 0 ? -3 : 3;
            } else if (m.dwEventFlags == DOUBLE_CLICK) {
                mouse.evType = MouseEvent::M_DOUBLE_CLICK;
            }
            return true;
        }
        
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) { resized = true; return true; }
        return false;
    }
};

class Widget;
using WidgetPtr = std::shared_ptr<Widget>;

class Widget {
public:
    TRect bounds = {0, 0, 10, 1};
    Widget* parent = nullptr;
    std::vector<WidgetPtr> children;
    bool visible = true, enabled = true, focused = false;
    std::function<void()> onClick;
    
    virtual ~Widget() = default;
    virtual void draw(Buffer& buf) { for (auto& c : children) if (c->visible) c->draw(buf); }
    virtual bool onKey(const KeyEvent& e) { return false; }
    virtual bool onMouse(const MouseEvent& e) { return false; }
    
    void addChild(WidgetPtr w) { w->parent = this; children.push_back(w); }
    Widget* findAt(int x, int y) {
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if ((*it)->visible && (*it)->bounds.contains(x, y))
                return (*it)->findAt(x, y);
        return bounds.contains(x, y) ? this : nullptr;
    }
};

}
#endif
