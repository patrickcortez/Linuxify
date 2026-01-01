// TUI - Retro Terminal Emulator for Linuxify (ConPTY Version)
// Compile: g++ -std=c++17 -static -mwindows -o cmds\tui.exe cmds-src\tui.cpp -lgdi32 -lgdiplus -luser32

#define _WIN32_WINNT 0x0A00
#define NOMINMAX
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
#include <algorithm>
#include <cmath>

#include "conpty_defs.hpp"

namespace fs = std::filesystem;

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

const int DEFAULT_WIDTH = 1024;
const int DEFAULT_HEIGHT = 768;
const int FONT_SIZE = 16;
const int LINE_HEIGHT = 20;
const int PADDING = 40;
const int MAX_LINES = 2000;

const COLORREF BG_COLOR = RGB(8, 12, 8);
const COLORREF TEXT_COLOR = RGB(50, 255, 120);

struct Cell {
    char ch = ' ';
    COLORREF fg = TEXT_COLOR;
    COLORREF bg = BG_COLOR;
};

struct TerminalState {
    std::vector<std::vector<Cell>> grid;
    std::deque<std::vector<Cell>> history;
    int cursorRow = 0, cursorCol = 0;
    int rows = 40, cols = 120;
    int viewOffset = 0;
    COLORREF currentFg = TEXT_COLOR;
    COLORREF currentBg = BG_COLOR;
    
    std::string csiParams;
    int parseState = 0;
    
    void Resize(int r, int c) {
        rows = std::max(1, r);
        cols = std::max(1, c);
        grid.resize(rows);
        for (auto& row : grid) row.resize(cols, Cell{' ', TEXT_COLOR, BG_COLOR});
    }
    
    void Scroll() {
        if (!grid.empty()) {
            history.push_back(grid.front());
            if (history.size() > MAX_LINES) history.pop_front();
            grid.erase(grid.begin());
            grid.resize(rows);
            grid.back().resize(cols, Cell{' ', TEXT_COLOR, BG_COLOR});
        }
    }
};

ConPTYContext g_pty;
TerminalState g_term;
std::mutex g_mutex;
std::atomic<bool> g_running{false};
std::thread g_readerThread;

HWND g_hwnd = NULL;
HPCON g_hPC = NULL;
HANDLE g_hPipeIn = NULL;
HANDLE g_hPipeOut = NULL;
PROCESS_INFORMATION g_pi = {0};

HFONT g_hFont = NULL;
HDC g_memDC = NULL;
HBITMAP g_memBitmap = NULL;
HBITMAP g_oldBitmap = NULL;
bool g_cursorVisible = true;

Gdiplus::GdiplusStartupInput g_gdiplusStartupInput;
ULONG_PTR g_gdiplusToken;

void ProcessOutput(const char* buffer, DWORD bytes);
void Render(HDC hdc);
void SendInput(const std::string& text);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ProcessCSI(char cmd, const std::string& params) {
    std::vector<int> codes;
    std::string current;
    for (char c : params) {
        if (isdigit(c)) current += c;
        else if (c == ';') { codes.push_back(current.empty() ? 0 : std::stoi(current)); current = ""; }
    }
    if (!current.empty()) codes.push_back(std::stoi(current));
    if (codes.empty()) codes.push_back(0);
    
    switch (cmd) {
    case 'm':
        for (int c : codes) {
            if (c == 0) { g_term.currentFg = TEXT_COLOR; g_term.currentBg = BG_COLOR; }
            else if (c >= 30 && c <= 37) g_term.currentFg = RGB(0, 200, 80);
            else if (c >= 90 && c <= 97) g_term.currentFg = RGB(100, 255, 150);
            else if (c == 39) g_term.currentFg = TEXT_COLOR;
            else if (c == 49) g_term.currentBg = BG_COLOR;
        }
        break;
    case 'H': case 'f': {
        int row = (codes.size() >= 1 && codes[0] > 0) ? codes[0] - 1 : 0;
        int col = (codes.size() >= 2 && codes[1] > 0) ? codes[1] - 1 : 0;
        g_term.cursorRow = std::min(std::max(0, row), g_term.rows - 1);
        g_term.cursorCol = std::min(std::max(0, col), g_term.cols - 1);
        break;
    }
    case 'J':
        if (codes[0] == 2) {
            for (auto& r : g_term.grid) for (auto& c : r) c = Cell{' ', g_term.currentFg, g_term.currentBg};
        }
        break;
    case 'K':
        if (g_term.cursorRow < g_term.rows) {
            auto& row = g_term.grid[g_term.cursorRow];
            for (int i = g_term.cursorCol; i < g_term.cols; ++i) row[i] = Cell{' ', g_term.currentFg, g_term.currentBg};
        }
        break;
    case 'A': g_term.cursorRow = std::max(0, g_term.cursorRow - (codes[0] ? codes[0] : 1)); break;
    case 'B': g_term.cursorRow = std::min(g_term.rows - 1, g_term.cursorRow + (codes[0] ? codes[0] : 1)); break;
    case 'C': g_term.cursorCol = std::min(g_term.cols - 1, g_term.cursorCol + (codes[0] ? codes[0] : 1)); break;
    case 'D': g_term.cursorCol = std::max(0, g_term.cursorCol - (codes[0] ? codes[0] : 1)); break;
    case 'G': g_term.cursorCol = std::min(std::max(0, (codes[0] > 0 ? codes[0] - 1 : 0)), g_term.cols - 1); break;
    }
}

void ProcessOutput(const char* buffer, DWORD bytes) {
    std::lock_guard<std::mutex> lock(g_mutex);
    
    for (DWORD i = 0; i < bytes; ++i) {
        char c = buffer[i];
        
        if (g_term.parseState == 0) {
            if (c == '\x1b') { g_term.parseState = 1; }
            else if (c == '\r') { g_term.cursorCol = 0; }
            else if (c == '\n') {
                g_term.cursorRow++;
                if (g_term.cursorRow >= g_term.rows) { g_term.Scroll(); g_term.cursorRow = g_term.rows - 1; }
            }
            else if (c == '\b') { if (g_term.cursorCol > 0) g_term.cursorCol--; }
            else if (c >= 32) {
                if (g_term.cursorRow < g_term.rows && g_term.cursorCol < g_term.cols) {
                    g_term.grid[g_term.cursorRow][g_term.cursorCol] = Cell{c, g_term.currentFg, g_term.currentBg};
                    g_term.cursorCol++;
                    if (g_term.cursorCol >= g_term.cols) {
                        g_term.cursorCol = 0;
                        g_term.cursorRow++;
                        if (g_term.cursorRow >= g_term.rows) { g_term.Scroll(); g_term.cursorRow = g_term.rows - 1; }
                    }
                }
            }
        } else if (g_term.parseState == 1) {
            if (c == '[') { g_term.parseState = 2; g_term.csiParams = ""; }
            else g_term.parseState = 0;
        } else if (g_term.parseState == 2) {
            if (c >= 0x20 && c <= 0x3F) g_term.csiParams += c;
            else if (c >= 0x40 && c <= 0x7E) { ProcessCSI(c, g_term.csiParams); g_term.parseState = 0; }
            else g_term.parseState = 0;
        }
    }
    
    g_term.viewOffset = 0;
}

void ReaderThread() {
    char buffer[4096];
    DWORD bytesRead, bytesAvail;
    
    while (g_running) {
        if (PeekNamedPipe(g_hPipeOut, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(g_hPipeOut, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                ProcessOutput(buffer, bytesRead);
                InvalidateRect(g_hwnd, NULL, FALSE);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void SendInput(const std::string& text) {
    if (!g_hPipeIn) return;
    DWORD written;
    WriteFile(g_hPipeIn, text.c_str(), (DWORD)text.length(), &written, NULL);
}

void RenderWithGlow(Gdiplus::Graphics& g, const std::wstring& text, int x, int y, Gdiplus::Font& font) {
    if (text.empty()) return;
    Gdiplus::PointF pt((Gdiplus::REAL)x, (Gdiplus::REAL)y);
    
    // Single glow layer for performance
    Gdiplus::SolidBrush glow(Gdiplus::Color(40, 80, 255, 130));
    g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(pt.X + 1, pt.Y + 1), &glow);
    
    // Main text
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 50, 255, 120));
    g.DrawString(text.c_str(), -1, &font, pt, &textBrush);
}

void Render(HDC hdc) {
    RECT rect;
    GetClientRect(g_hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    
    Gdiplus::Graphics graphics(g_memDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighSpeed);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 8, 12, 8));
    graphics.FillRectangle(&bgBrush, 0, 0, width, height);
    
    Gdiplus::FontFamily fontFamily(L"Consolas");
    Gdiplus::Font font(&fontFamily, (Gdiplus::REAL)FONT_SIZE, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    
    std::lock_guard<std::mutex> lock(g_mutex);
    
    int visibleLines = (height - PADDING * 2) / LINE_HEIGHT;
    int historySize = (int)g_term.history.size();
    int totalLines = historySize + (int)g_term.grid.size();
    int startLine = std::max(0, totalLines - visibleLines - g_term.viewOffset);
    
    int y = PADDING;
    for (int i = 0; i < visibleLines && startLine + i < totalLines; ++i) {
        int lineIdx = startLine + i;
        const std::vector<Cell>* rowPtr = nullptr;
        if (lineIdx < historySize) rowPtr = &g_term.history[lineIdx];
        else rowPtr = &g_term.grid[lineIdx - historySize];
        
        if (rowPtr) {
            std::wstring line;
            for (const auto& cell : *rowPtr) line += (wchar_t)cell.ch;
            
            while (!line.empty() && line.back() == ' ') line.pop_back();
            
            if (!line.empty()) {
                RenderWithGlow(graphics, line, PADDING, y, font);
            }
        }
        y += LINE_HEIGHT;
    }
    
    if (g_cursorVisible) {
        int cursorY = PADDING + (g_term.cursorRow - (totalLines - historySize - g_term.rows - g_term.viewOffset)) * LINE_HEIGHT;
        int cursorX = PADDING + g_term.cursorCol * (int)(FONT_SIZE * 0.6);
        
        if (cursorY >= PADDING && cursorY < height - PADDING) {
            Gdiplus::SolidBrush glow(Gdiplus::Color(60, 100, 255, 150));
            graphics.FillRectangle(&glow, cursorX - 2, cursorY - 2, (int)(FONT_SIZE * 0.6) + 4, LINE_HEIGHT + 2);
            
            Gdiplus::SolidBrush cursor(Gdiplus::Color(200, 50, 255, 120));
            graphics.FillRectangle(&cursor, cursorX, cursorY, (int)(FONT_SIZE * 0.6), LINE_HEIGHT - 2);
        }
    }
    
    // Simple border instead of expensive vignette
    Gdiplus::Pen borderPen(Gdiplus::Color(60, 0, 0, 0), 3);
    graphics.DrawRectangle(&borderPen, 1, 1, width - 3, height - 3);
    
    BitBlt(hdc, 0, 0, width, height, g_memDC, 0, 0, SRCCOPY);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;
    case WM_CLOSE:
        g_running = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Render(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) SendInput("\r");
        else if (wParam == VK_BACK) SendInput("\b");
        else if (wParam == VK_UP) SendInput("\x1b[A");
        else if (wParam == VK_DOWN) SendInput("\x1b[B");
        else if (wParam == VK_LEFT) SendInput("\x1b[D");
        else if (wParam == VK_RIGHT) SendInput("\x1b[C");
        else if (wParam == VK_TAB) SendInput("\t");
        else if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) SendInput("\x03");
        else if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (OpenClipboard(hwnd)) {
                HANDLE hData = GetClipboardData(CF_TEXT);
                if (hData) {
                    char* text = (char*)GlobalLock(hData);
                    if (text) { SendInput(text); GlobalUnlock(hData); }
                }
                CloseClipboard();
            }
        }
        else if (wParam == VK_PRIOR) { g_term.viewOffset += 10; InvalidateRect(hwnd, NULL, FALSE); }
        else if (wParam == VK_NEXT) { g_term.viewOffset = std::max(0, g_term.viewOffset - 10); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    case WM_CHAR:
        if (wParam >= 32 && wParam < 127) {
            char c = (char)wParam;
            SendInput(std::string(1, c));
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) g_term.viewOffset += 3;
        else g_term.viewOffset = std::max(0, g_term.viewOffset - 3);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1) g_cursorVisible = !g_cursorVisible;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int cols = std::max(1, (int)(rc.right - PADDING * 2) / (int)(FONT_SIZE * 0.6));
        int rows = std::max(1, (int)(rc.bottom - PADDING * 2) / LINE_HEIGHT);
        g_term.Resize(rows, cols);
        if (g_hPC && g_pty.ResizePseudoConsole) {
            COORD size = {(SHORT)cols, (SHORT)rows};
            g_pty.ResizePseudoConsole(g_hPC, size);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    if (!g_pty.Init()) {
        MessageBoxW(NULL, L"ConPTY not available (requires Windows 10 1809+)", L"Error", MB_ICONERROR);
        return 1;
    }
    
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &g_gdiplusStartupInput, NULL);
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = CreateSolidBrush(BG_COLOR);
    wc.lpszClassName = L"LinuxifyTUI";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"LinuxifyTUI", L"LINUXIFY SHELL",
        WS_OVERLAPPEDWINDOW, (screenW - DEFAULT_WIDTH) / 2, (screenH - DEFAULT_HEIGHT) / 2,
        DEFAULT_WIDTH, DEFAULT_HEIGHT, NULL, NULL, hInstance, NULL);
    
    g_hFont = CreateFontW(FONT_SIZE, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    
    HDC hdc = GetDC(g_hwnd);
    g_memDC = CreateCompatibleDC(hdc);
    g_memBitmap = CreateCompatibleBitmap(hdc, DEFAULT_WIDTH * 2, DEFAULT_HEIGHT * 2);
    g_oldBitmap = (HBITMAP)SelectObject(g_memDC, g_memBitmap);
    ReleaseDC(g_hwnd, hdc);
    
    RECT rc; GetClientRect(g_hwnd, &rc);
    int cols = std::max(1, (int)(rc.right - PADDING * 2) / (int)(FONT_SIZE * 0.6));
    int rows = std::max(1, (int)(rc.bottom - PADDING * 2) / LINE_HEIGHT);
    g_term.Resize(rows, cols);
    
    HANDLE hPTYIn, hPTYOut;
    CreatePipe(&hPTYIn, &g_hPipeIn, NULL, 0);
    CreatePipe(&g_hPipeOut, &hPTYOut, NULL, 0);
    
    COORD size = {(SHORT)cols, (SHORT)rows};
    g_pty.CreatePseudoConsole(size, hPTYIn, hPTYOut, 0, &g_hPC);
    
    STARTUPINFOEXA siEx = {0};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXA);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    siEx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
    InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, g_hPC, sizeof(HPCON), NULL, NULL);
    
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    if (exeDir.filename() == "cmds") exeDir = exeDir.parent_path();
    std::string cmd = (exeDir / "linuxify.exe").string();
    
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT,
        NULL, NULL, &siEx.StartupInfo, &g_pi);
    
    CloseHandle(hPTYIn);
    CloseHandle(hPTYOut);
    DeleteProcThreadAttributeList(siEx.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
    
    g_running = true;
    g_readerThread = std::thread(ReaderThread);
    
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetTimer(g_hwnd, 1, 530, NULL);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    g_running = false;
    if (g_readerThread.joinable()) g_readerThread.join();
    
    if (g_pi.hProcess) { TerminateProcess(g_pi.hProcess, 0); CloseHandle(g_pi.hProcess); CloseHandle(g_pi.hThread); }
    if (g_hPC && g_pty.ClosePseudoConsole) g_pty.ClosePseudoConsole(g_hPC);
    if (g_hPipeIn) CloseHandle(g_hPipeIn);
    if (g_hPipeOut) CloseHandle(g_hPipeOut);
    
    if (g_oldBitmap && g_memDC) SelectObject(g_memDC, g_oldBitmap);
    if (g_memBitmap) DeleteObject(g_memBitmap);
    if (g_memDC) DeleteDC(g_memDC);
    if (g_hFont) DeleteObject(g_hFont);
    
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    
    return 0;
}
