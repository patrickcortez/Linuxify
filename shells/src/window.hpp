// Funux Window System - Include in apps to use Funux TUI windowing
// Usage: #include "window.hpp"
#ifndef FUNUX_WINDOW_HPP
#define FUNUX_WINDOW_HPP

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <windows.h>
#include <conio.h>

namespace FunuxSys {

namespace ANSI {
    inline std::string moveTo(int row, int col) { return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H"; }
    inline std::string fg(int c) { return "\x1b[38;5;" + std::to_string(c) + "m"; }
    inline std::string bg(int c) { return "\x1b[48;5;" + std::to_string(c) + "m"; }
    const std::string RESET = "\x1b[0m";
    const std::string CLEAR = "\x1b[2J";
    const std::string HOME = "\x1b[H";
    const std::string HIDE_CURSOR = "\x1b[?25l";
    const std::string SHOW_CURSOR = "\x1b[?25h";
}

struct TermSize {
    int width, height;
};

inline TermSize getTermSize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return {csbi.srWindow.Right - csbi.srWindow.Left + 1, csbi.srWindow.Bottom - csbi.srWindow.Top + 1};
}

class Window {
protected:
    int x, y, width, height;
    std::string title;
    int borderColor = 34;
    int bgColor = 232;
    int titleColor = 46;
    bool needsRedraw = true;
    
public:
    Window(int x = 1, int y = 1, int w = 40, int h = 10, const std::string& t = "")
        : x(x), y(y), width(w), height(h), title(t) {}
    
    void setPosition(int newX, int newY) { x = newX; y = newY; needsRedraw = true; }
    void setSize(int w, int h) { width = w; height = h; needsRedraw = true; }
    void setTitle(const std::string& t) { title = t; needsRedraw = true; }
    void setColors(int border, int bg, int titleCol) { borderColor = border; bgColor = bg; titleColor = titleCol; needsRedraw = true; }
    void invalidate() { needsRedraw = true; }
    
    virtual void drawBorder() {
        std::cout << ANSI::fg(borderColor) << ANSI::bg(bgColor);
        
        std::cout << ANSI::moveTo(y, x) << "\xE2\x95\x94";
        for (int i = 0; i < width - 2; i++) std::cout << "\xE2\x95\x90";
        std::cout << "\xE2\x95\x97";
        
        if (!title.empty()) {
            int tStart = x + (width - (int)title.size() - 2) / 2;
            std::cout << ANSI::moveTo(y, tStart) << ANSI::fg(titleColor) << " " << title << " " << ANSI::fg(borderColor);
        }
        
        for (int row = 1; row < height - 1; row++) {
            std::cout << ANSI::moveTo(y + row, x) << "\xE2\x95\x91";
            std::cout << ANSI::moveTo(y + row, x + width - 1) << "\xE2\x95\x91";
        }
        
        std::cout << ANSI::moveTo(y + height - 1, x) << "\xE2\x95\x9A";
        for (int i = 0; i < width - 2; i++) std::cout << "\xE2\x95\x90";
        std::cout << "\xE2\x95\x9D";
    }
    
    virtual void clearContent() {
        std::cout << ANSI::bg(bgColor);
        for (int row = 1; row < height - 1; row++) {
            std::cout << ANSI::moveTo(y + row, x + 1);
            for (int i = 0; i < width - 2; i++) std::cout << " ";
        }
    }
    
    void print(int row, int col, const std::string& text, int fg = 250) {
        if (row < 1 || row >= height - 1) return;
        std::cout << ANSI::moveTo(y + row, x + col) << ANSI::fg(fg) << ANSI::bg(bgColor);
        std::string t = text;
        int maxLen = width - col - 1;
        if ((int)t.size() > maxLen) t = t.substr(0, maxLen);
        std::cout << t;
    }
    
    void printCentered(int row, const std::string& text, int fg = 250) {
        int col = (width - (int)text.size()) / 2;
        if (col < 1) col = 1;
        print(row, col, text, fg);
    }
    
    virtual void draw() {
        if (!needsRedraw) return;
        drawBorder();
        clearContent();
        needsRedraw = false;
    }
    
    int getContentWidth() const { return width - 2; }
    int getContentHeight() const { return height - 2; }
    int getX() const { return x; }
    int getY() const { return y; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

class Dialog : public Window {
private:
    std::vector<std::string> lines;
    std::vector<std::string> buttons;
    int selectedButton = 0;
    
public:
    Dialog(const std::string& title, int w = 50, int h = 10) : Window(0, 0, w, h, title) {
        auto sz = getTermSize();
        x = (sz.width - w) / 2;
        y = (sz.height - h) / 2;
    }
    
    void addLine(const std::string& line) { lines.push_back(line); needsRedraw = true; }
    void addButton(const std::string& btn) { buttons.push_back(btn); needsRedraw = true; }
    void clearLines() { lines.clear(); needsRedraw = true; }
    
    void draw() override {
        Window::draw();
        
        for (size_t i = 0; i < lines.size() && (int)i < height - 4; i++) {
            print(i + 1, 2, lines[i], 250);
        }
        
        if (!buttons.empty()) {
            int totalLen = 0;
            for (const auto& b : buttons) totalLen += (int)b.size() + 4;
            int startX = (width - totalLen) / 2;
            int bRow = height - 2;
            
            for (size_t i = 0; i < buttons.size(); i++) {
                bool sel = ((int)i == selectedButton);
                std::cout << ANSI::moveTo(y + bRow, x + startX);
                std::cout << (sel ? ANSI::bg(24) : ANSI::bg(236));
                std::cout << ANSI::fg(sel ? 255 : 250);
                std::cout << "[ " << buttons[i] << " ]";
                startX += (int)buttons[i].size() + 5;
            }
        }
        
        std::cout << ANSI::RESET;
        std::cout.flush();
    }
    
    int run() {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD oldMode;
        GetConsoleMode(hIn, &oldMode);
        SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
        
        std::cout << ANSI::HIDE_CURSOR;
        
        needsRedraw = true;
        while (true) {
            draw();
            
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 27) { SetConsoleMode(hIn, oldMode); std::cout << ANSI::SHOW_CURSOR; return -1; }
                if (ch == 13) { SetConsoleMode(hIn, oldMode); std::cout << ANSI::SHOW_CURSOR; return selectedButton; }
                if (ch == 0 || ch == 224) {
                    int ext = _getch();
                    if (ext == 75 && selectedButton > 0) { selectedButton--; needsRedraw = true; }
                    if (ext == 77 && selectedButton < (int)buttons.size() - 1) { selectedButton++; needsRedraw = true; }
                }
            }
            Sleep(30);
        }
    }
};

class InputDialog : public Window {
private:
    std::string prompt;
    std::string value;
    
public:
    InputDialog(const std::string& title, const std::string& p, int w = 50) 
        : Window(0, 0, w, 5, title), prompt(p) {
        auto sz = getTermSize();
        x = (sz.width - w) / 2;
        y = (sz.height - 5) / 2;
    }
    
    std::string run() {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD oldMode;
        GetConsoleMode(hIn, &oldMode);
        
        drawBorder();
        clearContent();
        print(1, 2, prompt, 250);
        
        std::cout << ANSI::moveTo(y + 2, x + 2) << ANSI::bg(236) << ANSI::fg(255);
        for (int i = 0; i < width - 4; i++) std::cout << " ";
        std::cout << ANSI::moveTo(y + 2, x + 2);
        std::cout << ANSI::SHOW_CURSOR;
        std::cout.flush();
        
        SetConsoleMode(hIn, oldMode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        std::getline(std::cin, value);
        SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
        
        std::cout << ANSI::HIDE_CURSOR;
        return value;
    }
};

class App {
protected:
    bool running = true;
    int termWidth = 80, termHeight = 24;
    bool needsRedraw = true;
    
    void updateTermSize() {
        auto sz = getTermSize();
        if (sz.width != termWidth || sz.height != termHeight) {
            termWidth = sz.width;
            termHeight = sz.height;
            needsRedraw = true;
        }
    }
    
    void setupConsole() {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hIn, &mode);
        SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
        
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        
        std::cout << ANSI::HIDE_CURSOR;
    }
    
    void cleanupConsole() {
        std::cout << ANSI::SHOW_CURSOR << ANSI::RESET << ANSI::CLEAR << ANSI::HOME;
    }
    
public:
    virtual void onInit() {}
    virtual void onDraw() = 0;
    virtual void onKey(int ch, int ext) {}
    virtual void onTick() {}
    
    void quit() { running = false; }
    void invalidate() { needsRedraw = true; }
    int getWidth() const { return termWidth; }
    int getHeight() const { return termHeight; }
    
    void run() {
        setupConsole();
        updateTermSize();
        onInit();
        
        while (running) {
            updateTermSize();
            
            if (needsRedraw) {
                onDraw();
                std::cout.flush();
                needsRedraw = false;
            }
            
            if (_kbhit()) {
                int ch = _getch();
                int ext = 0;
                if (ch == 0 || ch == 224) ext = _getch();
                onKey(ch, ext);
            }
            
            onTick();
            Sleep(16);
        }
        
        cleanupConsole();
    }
};

class StatusBar {
private:
    int y;
    int width;
    int bgColor = 235;
    int fgColor = 250;
    std::string left, right;
    
public:
    StatusBar(int row, int w) : y(row), width(w) {}
    
    void setPosition(int row, int w) { y = row; width = w; }
    void setLeft(const std::string& text) { left = text; }
    void setRight(const std::string& text) { right = text; }
    void setColors(int bg, int fg) { bgColor = bg; fgColor = fg; }
    
    void draw() {
        std::cout << ANSI::moveTo(y, 1) << ANSI::bg(bgColor) << ANSI::fg(fgColor);
        std::cout << " " << left;
        
        int pad = width - (int)left.size() - (int)right.size() - 2;
        for (int i = 0; i < pad; i++) std::cout << " ";
        
        std::cout << right << " ";
    }
};

class Menu {
private:
    std::vector<std::string> items;
    int selected = 0;
    int x, y, width;
    int bgColor = 236;
    int fgColor = 250;
    int selBg = 24;
    int selFg = 255;
    
public:
    Menu(int px, int py, int w) : x(px), y(py), width(w) {}
    
    void addItem(const std::string& item) { items.push_back(item); }
    void clear() { items.clear(); selected = 0; }
    int getSelected() const { return selected; }
    std::string getSelectedItem() const { return selected < (int)items.size() ? items[selected] : ""; }
    
    void up() { if (selected > 0) selected--; }
    void down() { if (selected < (int)items.size() - 1) selected++; }
    void home() { selected = 0; }
    void end() { selected = (int)items.size() - 1; }
    
    void draw() {
        for (size_t i = 0; i < items.size(); i++) {
            bool sel = ((int)i == selected);
            std::cout << ANSI::moveTo(y + i, x);
            std::cout << (sel ? ANSI::bg(selBg) : ANSI::bg(bgColor));
            std::cout << (sel ? ANSI::fg(selFg) : ANSI::fg(fgColor));
            
            std::string item = items[i];
            if ((int)item.size() > width) item = item.substr(0, width - 3) + "...";
            std::cout << " " << item;
            for (int j = (int)item.size() + 1; j < width; j++) std::cout << " ";
        }
    }
    
    int count() const { return (int)items.size(); }
};

// Graphics Extension
// Colors for direct buffer access
class GraphicsApp : public App {
public:
    enum Color {
        FG_BLACK = 0x0000,
        FG_BLUE = 0x0001,
        FG_GREEN = 0x0002,
        FG_CYAN = 0x0003,
        FG_RED = 0x0004,
        FG_MAGENTA = 0x0005,
        FG_YELLOW = 0x0006,
        FG_WHITE = 0x0007,
        FG_GRAY = 0x0008,
        FG_INTENSE_BLUE = 0x0009,
        FG_INTENSE_GREEN = 0x000A,
        FG_INTENSE_CYAN = 0x000B,
        FG_INTENSE_RED = 0x000C,
        FG_INTENSE_MAGENTA = 0x000D,
        FG_INTENSE_YELLOW = 0x000E,
        FG_INTENSE_WHITE = 0x000F,
        
        BG_BLACK = 0x0000,
        BG_BLUE = 0x0010,
        BG_GREEN = 0x0020,
        BG_CYAN = 0x0030,
        BG_RED = 0x0040,
        BG_MAGENTA = 0x0050,
        BG_YELLOW = 0x0060,
        BG_WHITE = 0x0070,
        BG_GRAY = 0x0080,
        BG_INTENSE_BLUE = 0x0090,
        BG_INTENSE_GREEN = 0x00A0,
        BG_INTENSE_CYAN = 0x00B0,
        BG_INTENSE_RED = 0x00C0,
        BG_INTENSE_MAGENTA = 0x00D0,
        BG_INTENSE_YELLOW = 0x00E0,
        BG_INTENSE_WHITE = 0x00F0
    };

protected:
    CHAR_INFO* buffer;
    HANDLE hConsole;
    SMALL_RECT rectWindow;
    COORD bufferSize;
    COORD bufferCoord;
    
public:
    GraphicsApp() : buffer(nullptr) {
        // Use CONOUT$ to bypass any redirection (pipes) and draw directly to the console buffer
        hConsole = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hConsole == INVALID_HANDLE_VALUE) {
            hConsole = GetStdHandle(STD_OUTPUT_HANDLE); // Fallback
        }
    }
    
    ~GraphicsApp() {
        if (buffer) delete[] buffer;
    }
    
    void initGraphics() {
        updateTermSize();
        bufferSize = { (SHORT)termWidth, (SHORT)termHeight };
        bufferCoord = { 0, 0 };
        rectWindow = { 0, 0, (SHORT)(termWidth - 1), (SHORT)(termHeight - 1) };
        
        if (buffer) delete[] buffer;
        buffer = new CHAR_INFO[termWidth * termHeight];
        
        // Hide cursor
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &cursorInfo);
    }
    
    void clear(WORD attributes = BG_BLACK | FG_WHITE) {
        for (int i = 0; i < termWidth * termHeight; i++) {
            buffer[i].Char.UnicodeChar = L' ';
            buffer[i].Attributes = attributes;
        }
    }
    
    void drawPixel(int x, int y, wchar_t ch, WORD attr) {
        if (x >= 0 && x < termWidth && y >= 0 && y < termHeight) {
            int idx = y * termWidth + x;
            buffer[idx].Char.UnicodeChar = ch;
            buffer[idx].Attributes = attr;
        }
    }
    
    void drawText(int x, int y, const std::string& text, WORD attr) {
        for (size_t i = 0; i < text.size(); i++) {
            drawPixel(x + i, y, (wchar_t)text[i], attr);
        }
    }
    
    void drawWText(int x, int y, const std::wstring& text, WORD attr) {
        for (size_t i = 0; i < text.size(); i++) {
            drawPixel(x + i, y, text[i], attr);
        }
    }
    
    void drawRect(int x, int y, int w, int h, wchar_t ch, WORD attr) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                drawPixel(x + j, y + i, ch, attr);
            }
        }
    }
    
    void present() {
        WriteConsoleOutputW(hConsole, buffer, bufferSize, bufferCoord, &rectWindow);
    }
    
    // Override run to use initGraphics
    void run() {
        setupConsole();
        initGraphics();
        onInit();
        
        while (running) {
            auto sz = getTermSize();
            if (sz.width != termWidth || sz.height != termHeight) {
                termWidth = sz.width;
                termHeight = sz.height;
                initGraphics(); 
            }
            
            onDraw(); 
            
            if (_kbhit()) {
                int ch = _getch();
                int ext = 0;
                if (ch == 0 || ch == 224) ext = _getch();
                onKey(ch, ext);
            }
            
            onTick();
        }
        
        cleanupConsole();
    }
};

}

#endif
