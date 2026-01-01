// TUI - Retro Terminal Emulator for Linuxify
// Compile: g++ -std=c++17 -static -o cmds\tui.exe cmds-src\tui.cpp -lgdi32 -lgdiplus -luser32 -mwindows

#ifndef TUI_HPP
#define TUI_HPP

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

class RetroTerminal {
public:
    static constexpr int DEFAULT_WIDTH = 1024;
    static constexpr int DEFAULT_HEIGHT = 768;
    static constexpr int FONT_SIZE = 16;
    static constexpr int LINE_HEIGHT = 20;
    static constexpr int PADDING = 40;
    static constexpr int MAX_LINES = 1000;
    
    static constexpr COLORREF BG_COLOR = RGB(8, 12, 8);

private:
    HWND hwnd;
    HANDLE hChildStdoutRead, hChildStdoutWrite;
    HANDLE hChildStdinRead, hChildStdinWrite;
    PROCESS_INFORMATION childProcess;
    
    std::deque<std::wstring> lines;
    std::wstring currentLine;
    int scrollOffset;
    int windowWidth, windowHeight;
    
    HFONT hFont;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    
    std::mutex lineMutex;
    std::atomic<bool> running;
    std::thread readerThread;
    
    HDC memDC;
    HBITMAP memBitmap;
    HBITMAP oldBitmap;
    
    bool cursorVisible;

public:
    RetroTerminal();
    ~RetroTerminal();
    
    bool initialize(HINSTANCE hInstance);
    void run();
    void cleanup();
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
private:
    bool createPipes();
    bool launchChild(const std::wstring& command);
    void readerLoop();
    void processOutput(const std::string& data);
    void sendInput(const std::string& text);
    
    void render(HDC hdc);
    void renderBackground(Gdiplus::Graphics& g, int width, int height);
    void renderScanlines(Gdiplus::Graphics& g, int width, int height);
    void renderText(Gdiplus::Graphics& g);
    void renderCursor(Gdiplus::Graphics& g);
    void renderVignette(Gdiplus::Graphics& g, int width, int height);
    
    void scrollToBottom();
    void handleMessage_KeyDown(WPARAM vk, bool ctrl);
    
    int getVisibleLines() const {
        return (windowHeight - PADDING * 2) / LINE_HEIGHT;
    }
    
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
};

#endif
