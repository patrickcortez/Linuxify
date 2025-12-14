// Linuxify GUI Terminal (Tabs + ConPTY + Pixel Font + Polish + Scrollback + TUI Support)
// Compile: g++ -std=c++17 -static -mwindows -o cmds\terminal.exe cmds-src\gui_terminal.cpp cmds-src\terminal.res -lgdi32 -luser32 -ldwmapi

#define _WIN32_WINNT 0x0A00 
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>

#include "conpty_defs.hpp"

namespace fs = std::filesystem;

// ============================================================================
// Constants & Configuration
// ============================================================================

const char* CLASS_NAME = "LinuxifyTerminalClass";
const char* WINDOW_TITLE = "Windux";
const int SCROLLBAR_WIDTH = 12;

// Colors
const COLORREF PALETTE[] = {
    RGB(12, 12, 12), RGB(197, 15, 31), RGB(19, 161, 14), RGB(193, 156, 0),
    RGB(0, 55, 218), RGB(136, 23, 152), RGB(58, 150, 221), RGB(204, 204, 204),
    RGB(118, 118, 118), RGB(231, 72, 86), RGB(22, 198, 12), RGB(249, 241, 165),
    RGB(59, 120, 255), RGB(180, 0, 158), RGB(97, 214, 214), RGB(242, 242, 242)
};

const COLORREF DEFAULT_BG = RGB(10, 10, 10);
const COLORREF DEFAULT_FG = RGB(220, 220, 220);
const COLORREF TAB_BG = RGB(30, 30, 30);
const COLORREF TAB_ACTIVE_BG = RGB(50, 50, 50);
const int TAB_HEIGHT = 32;

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

// ============================================================================
// Data Structures
// ============================================================================

struct Cell {
    char ch = ' ';
    COLORREF fg = DEFAULT_FG;
    COLORREF bg = DEFAULT_BG;
};

// Forward decl
struct Session;
void ProcessOutput(Session* session, const char* buffer, DWORD bytes);

enum ParseState { STATE_TEXT, STATE_ESCAPE, STATE_CSI, STATE_OSC };

struct Session {
    int id;
    std::mutex mutex;
    std::vector<std::vector<Cell>> grid;
    std::deque<std::vector<Cell>> history;
    int viewOffset = 0; 
    
    // TUI Support
    bool inAltBuffer = false; 
    std::vector<std::vector<Cell>> savedGrid; // Backup for main buffer if needed, usually ConPTY handles restore content, but we might need to restore our specific history context if we cleared grid.
    // Actually, ConPTY sends the logic to restore. We just need to track mode to disable scrollbar.

    int cursorRow = 0; int cursorCol = 0;
    int rows = 25; int cols = 80;
    
    ParseState parseState = STATE_TEXT;
    std::string csiParams;
    COLORREF currentFg = DEFAULT_FG;
    COLORREF currentBg = DEFAULT_BG;

    HPCON hPC;
    HANDLE hPipeIn = NULL;
    HANDLE hPipeOut = NULL;
    PROCESS_INFORMATION pi = {0};
    bool active = true;
    std::thread readerThread;

    void Resize(int r, int c) {
        std::lock_guard<std::mutex> lock(mutex);
        rows = std::max(1, r); cols = std::max(1, c);
        grid.resize(rows);
        for (auto& row : grid) row.resize(cols);
        if (cursorRow >= rows) cursorRow = rows - 1;
        if (cursorCol >= cols) cursorCol = cols - 1;
        viewOffset = 0;
    }

    void Scroll() {
        if (!inAltBuffer) { // Only save history in non-alt buffer
            if (!grid.empty()) {
                history.push_back(grid.front());
                if (history.size() > 2000) history.pop_front();
            }
        }
        if (!grid.empty()) {
            grid.erase(grid.begin());
            grid.resize(rows);
            grid.back().resize(cols, Cell{' ', DEFAULT_FG, DEFAULT_BG});
        }
    }
    
    void Close() {
        active = false;
        if (hPipeIn) CloseHandle(hPipeIn);
        if (hPipeOut) CloseHandle(hPipeOut);
        if (pi.hProcess) { TerminateProcess(pi.hProcess, 0); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
};

ConPTYContext g_pty;
std::vector<Session*> g_sessions;
int g_activeSessionIndex = -1;
HFONT g_hFont = NULL;
int g_fontWidth = 8;
int g_fontHeight = 16;
HWND g_hwnd = NULL;

bool g_selecting = false;
int g_selStartRow = -1, g_selStartCol = -1;
int g_selEndRow = -1, g_selEndCol = -1;

void ScreenToCell(int screenX, int screenY, int& row, int& col) {
    int padding = 10;
    int termY = TAB_HEIGHT + padding;
    int termX = padding;
    
    col = (screenX - termX) / g_fontWidth;
    row = (screenY - termY) / g_fontHeight;
    if (col < 0) col = 0;
    if (row < 0) row = 0;
}

void ClearSelection() {
    g_selecting = false;
    g_selStartRow = g_selStartCol = g_selEndRow = g_selEndCol = -1;
}

bool HasSelection() {
    return g_selStartRow >= 0 && g_selEndRow >= 0;
}

void GetOrderedSelection(int& r1, int& c1, int& r2, int& c2) {
    if (g_selStartRow < g_selEndRow || (g_selStartRow == g_selEndRow && g_selStartCol <= g_selEndCol)) {
        r1 = g_selStartRow; c1 = g_selStartCol;
        r2 = g_selEndRow; c2 = g_selEndCol;
    } else {
        r1 = g_selEndRow; c1 = g_selEndCol;
        r2 = g_selStartRow; c2 = g_selStartCol;
    }
}

std::string GetSelectedText() {
    if (!HasSelection() || g_activeSessionIndex < 0) return "";
    Session* s = g_sessions[g_activeSessionIndex];
    std::lock_guard<std::mutex> lock(s->mutex);
    
    int r1, c1, r2, c2;
    GetOrderedSelection(r1, c1, r2, c2);
    
    std::string text;
    int historySize = s->inAltBuffer ? 0 : s->history.size();
    int totalRows = historySize + s->grid.size();
    int startLine = totalRows - s->rows - s->viewOffset;
    
    for (int i = r1; i <= r2 && i < s->rows; i++) {
        int lineIdx = startLine + i;
        if (lineIdx < 0 || lineIdx >= totalRows) continue;
        
        const std::vector<Cell>* rowPtr = nullptr;
        if (lineIdx < historySize) rowPtr = &s->history[lineIdx];
        else rowPtr = &s->grid[lineIdx - historySize];
        
        if (!rowPtr) continue;
        
        int startCol = (i == r1) ? c1 : 0;
        int endCol = (i == r2) ? c2 : (int)rowPtr->size() - 1;
        
        for (int c = startCol; c <= endCol && c < (int)rowPtr->size(); c++) {
            text += (*rowPtr)[c].ch;
        }
        if (i < r2) text += "\r\n";
    }
    
    // Trim trailing spaces from each line
    return text;
}

COLORREF GetXtermColor(int index) {
    if (index < 16) return PALETTE[index];
    if (index < 232) {
        int i = index - 16;
        int r = (i / 36) % 6; int g = (i / 6) % 6; int b = i % 6;
        return RGB(r ? r * 40 + 55 : 0, g ? g * 40 + 55 : 0, b ? b * 40 + 55 : 0);
    }
    int gray = (index - 232) * 10 + 8;
    return RGB(gray, gray, gray);
}

void ApplyCSI(Session* s, char cmd, const std::string& params) {
    std::vector<int> codes;
    std::string current;
    bool privateMode = (!params.empty() && params[0] == '?');
    
    for (char c : params) {
        if (isdigit(c)) current += c;
        else if (c == ';') { codes.push_back(current.empty() ? 0 : std::stoi(current)); current = ""; }
    }
    if (!current.empty()) codes.push_back(std::stoi(current));
    if (codes.empty()) codes.push_back(0);

    switch (cmd) {
    case 'm': 
        for (size_t i = 0; i < codes.size(); ++i) {
            int c = codes[i];
            if (c == 0) { s->currentFg = DEFAULT_FG; s->currentBg = DEFAULT_BG; }
            else if (c == 1) { if (s->currentFg == DEFAULT_FG) s->currentFg = PALETTE[15]; }
            else if (c >= 30 && c <= 37) s->currentFg = PALETTE[c - 30];
            else if (c >= 40 && c <= 47) s->currentBg = PALETTE[c - 40];
            else if (c >= 90 && c <= 97) s->currentFg = PALETTE[c - 90 + 8];
            else if (c >= 100 && c <= 107) s->currentBg = PALETTE[c - 100 + 8];
            else if (c == 39) s->currentFg = DEFAULT_FG;
            else if (c == 49) s->currentBg = DEFAULT_BG;
            else if ((c == 38 || c == 48) && i + 2 < codes.size()) {
                COLORREF color;
                if (codes[i+1] == 5) { color = GetXtermColor(codes[i+2]); i+=2; }
                else if (codes[i+1] == 2 && i+4 < codes.size()) { color = RGB(codes[i+2], codes[i+3], codes[i+4]); i+=4; }
                else continue;
                if (c == 38) s->currentFg = color; else s->currentBg = color;
            }
        }
        break;
    case 'J': 
        if (codes[0] == 2) {
             for(auto& r : s->grid) for(auto& c : r) c = Cell{' ', s->currentFg, s->currentBg};
             s->cursorRow = 0; s->cursorCol = 0;
        }
        break;
    case 'K':
        if (s->cursorRow < s->rows) {
            auto& row = s->grid[s->cursorRow];
            for(int i=s->cursorCol; i<s->cols; ++i) row[i] = Cell{' ', s->currentFg, s->currentBg};
        }
        break;
    case 'H':
        if (codes.size() >= 2) { s->cursorRow = std::max(0, codes[0]-1); s->cursorCol = std::max(0, codes[1]-1); }
        else { s->cursorRow = 0; s->cursorCol = 0; }
        break;
    case 'A': s->cursorRow = std::max(0, s->cursorRow - (codes[0]?codes[0]:1)); break;
    case 'B': s->cursorRow = std::min(s->rows-1, s->cursorRow + (codes[0]?codes[0]:1)); break;
    case 'C': s->cursorCol = std::min(s->cols-1, s->cursorCol + (codes[0]?codes[0]:1)); break;
    case 'D': s->cursorCol = std::max(0, s->cursorCol - (codes[0]?codes[0]:1)); break;
    
    // Private Modes
    case 'h': // SM
        if (privateMode) {
            for (int code : codes) {
                if (code == 1049) { 
                    s->inAltBuffer = true; 
                    s->viewOffset = 0; 
                    // Optional: Save cursor?
                }
            }
        }
        break;
    case 'l': // RM
        if (privateMode) {
            for (int code : codes) {
                if (code == 1049) { 
                    s->inAltBuffer = false; 
                    // Optional: Restore cursor?
                }
            }
        }
        break;
    }
}

void ApplyOSC(Session* s, const std::string& params) {}

void ProcessOutput(Session* s, const char* buffer, DWORD bytes) {
    if (!s->active) return;
    std::lock_guard<std::mutex> lock(s->mutex);
    
    for (DWORD i = 0; i < bytes; ++i) {
        char c = buffer[i];
        switch (s->parseState) {
        case STATE_TEXT:
            if (c == '\x1b') s->parseState = STATE_ESCAPE;
            else if (c == '\r') s->cursorCol = 0;
            else if (c == '\n') {
                s->cursorRow++;
                if (s->cursorRow >= s->rows) { s->Scroll(); s->cursorRow = s->rows - 1; }
            }
            else if (c == '\b') { if (s->cursorCol > 0) s->cursorCol--; }
            else if (c == '\a') {}
            else if (c >= 32 || c == '\t') {
                if (c == '\t') c = ' ';
                if (s->cursorRow < s->rows && s->cursorCol < s->cols) {
                    s->grid[s->cursorRow][s->cursorCol] = Cell{c, s->currentFg, s->currentBg};
                    s->cursorCol++;
                    if (s->cursorCol >= s->cols) {
                        s->cursorCol = 0; s->cursorRow++;
                        if (s->cursorRow >= s->rows) { s->Scroll(); s->cursorRow = s->rows - 1; }
                    }
                }
            }
            break;
        case STATE_ESCAPE:
            if (c == '[') { s->parseState = STATE_CSI; s->csiParams = ""; }
            else if (c == ']') { s->parseState = STATE_OSC; s->csiParams = ""; }
            else s->parseState = STATE_TEXT;
            break;
        case STATE_CSI:
            if (c >= 0x20 && c <= 0x3F) s->csiParams += c;
            else if (c >= 0x40 && c <= 0x7E) {
                ApplyCSI(s, c, s->csiParams);
                s->parseState = STATE_TEXT;
            } else s->parseState = STATE_TEXT;
            break;
        case STATE_OSC:
            if (c == '\a') { ApplyOSC(s, s->csiParams); s->parseState = STATE_TEXT; }
            else if (c == '\x1b') s->parseState = STATE_ESCAPE; 
            else s->csiParams += c;
            break;
        }
    }
}

void ReaderThread(Session* s) {
    char buffer[1024];
    DWORD bytesRead;
    DWORD bytesAvail;
    
    while (s->active) {
        if (PeekNamedPipe(s->hPipeOut, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            if (ReadFile(s->hPipeOut, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                ProcessOutput(s, buffer, bytesRead);
                InvalidateRect(g_hwnd, NULL, FALSE);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void CreateNewSession() {
    Session* s = new Session();
    s->id = g_sessions.size() + 1;
    
    RECT rc; GetClientRect(g_hwnd, &rc);
    int termHeight = rc.bottom - TAB_HEIGHT;
    int cols = std::max(1, (int)((rc.right - 20) / g_fontWidth));
    int rows = std::max(1, (int)((termHeight - 20) / g_fontHeight));
    s->Resize(rows, cols);

    HANDLE hPTYIn, hPTYOut;
    CreatePipe(&hPTYIn, &s->hPipeIn, NULL, 0);
    CreatePipe(&s->hPipeOut, &hPTYOut, NULL, 0);

    COORD size = {(SHORT)cols, (SHORT)rows};
    g_pty.CreatePseudoConsole(size, hPTYIn, hPTYOut, 0, &s->hPC);

    STARTUPINFOEXA siEx = {0};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXA);
    siEx.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
    siEx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
    InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, s->hPC, sizeof(HPCON), NULL, NULL);

    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    if (exeDir.filename() == "cmds") exeDir = exeDir.parent_path();
    std::string cmd = (exeDir / "linuxify.exe").string();

    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &siEx.StartupInfo, &s->pi);

    CloseHandle(hPTYIn); CloseHandle(hPTYOut);
    DeleteProcThreadAttributeList(siEx.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);
    
    g_sessions.push_back(s);
    g_activeSessionIndex = g_sessions.size() - 1;
    
    s->readerThread = std::thread(ReaderThread, s);
}

void SwitchTab(int index) {
    if (index >= 0 && index < g_sessions.size()) {
        g_activeSessionIndex = index;
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

void CloseSession(int index) {
    if (index >= 0 && index < g_sessions.size()) {
        Session* s = g_sessions[index];
        s->Close(); 
        if (s->readerThread.joinable()) s->readerThread.join(); 
        if (s->hPC) g_pty.ClosePseudoConsole(s->hPC);
        delete s;
        g_sessions.erase(g_sessions.begin() + index);
        
        if (g_sessions.empty()) PostQuitMessage(0);
        else {
            if (g_activeSessionIndex >= g_sessions.size()) g_activeSessionIndex = g_sessions.size() - 1;
            InvalidateRect(g_hwnd, NULL, FALSE);
        }
    }
}

// ============================================================================
// Drawing & Window
// ============================================================================

void PaintWindow(HWND hwnd, HDC hdc) {
    RECT rc; GetClientRect(hwnd, &rc);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    SelectObject(hdcMem, hbmMem);

    HBRUSH hBg = CreateSolidBrush(DEFAULT_BG);
    FillRect(hdcMem, &rc, hBg);
    DeleteObject(hBg);

    RECT rcTab = {0, 0, rc.right, TAB_HEIGHT};
    HBRUSH hTabBg = CreateSolidBrush(TAB_BG);
    FillRect(hdcMem, &rcTab, hTabBg);
    DeleteObject(hTabBg);

    SelectObject(hdcMem, g_hFont);
    SetBkMode(hdcMem, OPAQUE);
    
    int tabWidth = 140; 
    for (size_t i = 0; i < g_sessions.size(); ++i) {
        RECT rcItem = { (LONG)i * tabWidth, 0, (LONG)(i+1) * tabWidth, TAB_HEIGHT };
        COLORREF bg = (i == g_activeSessionIndex) ? TAB_ACTIVE_BG : TAB_BG;
        
        SetBkColor(hdcMem, bg);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        HBRUSH hItemBg = CreateSolidBrush(bg);
        FillRect(hdcMem, &rcItem, hItemBg);
        DeleteObject(hItemBg);
        
        std::string title = " Terminal " + std::to_string(i+1);
        RECT rcText = rcItem; rcText.right -= 20; 
        DrawTextA(hdcMem, title.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        RECT rcClose = {rcItem.right - 20, rcItem.top, rcItem.right - 5, rcItem.bottom};
        SetTextColor(hdcMem, RGB(200, 100, 100)); 
        DrawTextA(hdcMem, "x", 1, &rcClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (i == g_activeSessionIndex) {
            RECT rcLine = {rcItem.left, TAB_HEIGHT-2, rcItem.right, TAB_HEIGHT};
            HBRUSH hLine = CreateSolidBrush(RGB(60, 160, 255));
            FillRect(hdcMem, &rcLine, hLine);
            DeleteObject(hLine);
        }
    }
    
    RECT rcPlus = { (LONG)g_sessions.size() * tabWidth, 0, (LONG)g_sessions.size() * tabWidth + 40, TAB_HEIGHT };
    SetBkColor(hdcMem, TAB_BG);
    SetTextColor(hdcMem, RGB(200, 200, 200));
    DrawTextA(hdcMem, "+", 1, &rcPlus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (g_activeSessionIndex >= 0 && g_activeSessionIndex < g_sessions.size()) {
        Session* s = g_sessions[g_activeSessionIndex];
        std::lock_guard<std::mutex> lock(s->mutex);
        
        int padding = 10;
        int termY = TAB_HEIGHT + padding;
        int termX = padding;
        int termH = rc.bottom - termY;
        int maxVisible = termH / g_fontHeight;
        
        // Use history only if NOT in Alt Buffer
        int historySize = s->inAltBuffer ? 0 : s->history.size();
        int gridRows = s->grid.size(); 
        int totalRows = historySize + gridRows;
        
        if (s->viewOffset < 0) s->viewOffset = 0;
        if (s->viewOffset > historySize) s->viewOffset = historySize;
        
        int startLine = totalRows - s->rows - s->viewOffset;
        
        for (int i = 0; i < maxVisible; ++i) {
            int lineIdx = startLine + i;
            if (lineIdx >= totalRows) break;
            
            const std::vector<Cell>* rowPtr = nullptr;
            if (lineIdx < historySize) rowPtr = &s->history[lineIdx];
            else if (lineIdx < totalRows) rowPtr = &s->grid[lineIdx - historySize];
            
            if (rowPtr) {
                const auto& row = *rowPtr;
                
                // Get selection bounds
                int r1, c1, r2, c2;
                bool hasSel = HasSelection();
                if (hasSel) GetOrderedSelection(r1, c1, r2, c2);
                
                for (int c = 0; c < row.size(); ++c) {
                    const Cell& cell = row[c];
                    int x = termX + c * g_fontWidth;
                    int y = termY + i * g_fontHeight;
                    
                    // Check if this cell is selected
                    bool isSelected = false;
                    if (hasSel && i >= r1 && i <= r2) {
                        if (r1 == r2) {
                            isSelected = (c >= c1 && c <= c2);
                        } else if (i == r1) {
                            isSelected = (c >= c1);
                        } else if (i == r2) {
                            isSelected = (c <= c2);
                        } else {
                            isSelected = true;
                        }
                    }
                    
                    if (isSelected) {
                        // Highlight: invert colors (white bg, dark text)
                        SetTextColor(hdcMem, RGB(0, 0, 0));
                        SetBkColor(hdcMem, RGB(100, 149, 237)); // Cornflower blue selection
                    } else {
                        SetTextColor(hdcMem, cell.fg);
                        SetBkColor(hdcMem, cell.bg);
                    }
                    TextOutA(hdcMem, x, y, &cell.ch, 1);
                }
            }
        }
        
        if (s->viewOffset == 0) {
            int visualRow = (historySize + s->cursorRow) - startLine;
            if (visualRow >= 0 && visualRow < maxVisible) {
                int cx = termX + s->cursorCol * g_fontWidth;
                int cy = termY + visualRow * g_fontHeight;
                HBRUSH hCaret = CreateSolidBrush(RGB(200, 200, 200));
                RECT rcCaret = {cx, cy + g_fontHeight - 2, cx + g_fontWidth, cy + g_fontHeight};
                FillRect(hdcMem, &rcCaret, hCaret);
                DeleteObject(hCaret);
            }
        }
        
        // Scrollbar (Only show if NOT in Alt Buffer and overflow exists)
        if (!s->inAltBuffer && totalRows > s->rows) { 
             int sbX = rc.right - SCROLLBAR_WIDTH;
             int sbY = TAB_HEIGHT;
             int sbH = rc.bottom - sbY;
             RECT rcSb = {sbX, sbY, rc.right, rc.bottom};
             FillRect(hdcMem, &rcSb, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
             
             float ratio = (float)s->rows / (float)totalRows;
             if (ratio > 1.0f) ratio = 1.0f;
             int thumbH = std::max(20, (int)(sbH * ratio));
             
             int maxStart = historySize; 
             if (maxStart > 0) {
                 float posRatio = (float)startLine / (float)maxStart;
                 int thumbY = sbY + (int)((sbH - thumbH) * posRatio);
                 RECT rcThumb = {sbX + 2, thumbY, rc.right - 2, thumbY + thumbH};
                 HBRUSH hThumb = CreateSolidBrush(RGB(150, 150, 150));
                 FillRect(hdcMem, &rcThumb, hThumb);
                 DeleteObject(hThumb);
             }
        }
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
    DeleteDC(hdcMem);
    DeleteObject(hbmMem);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET, 0,0,0, FIXED_PITCH, "Fixedsys"); 
        if (!g_pty.Init()) MessageBoxA(NULL, "Failed to init ConPTY", "Error", MB_OK);
        else CreateNewSession();
        return 0;
        
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (g_activeSessionIndex >= 0 && g_pty.ResizePseudoConsole) {
             RECT rc; GetClientRect(hwnd, &rc);
             int termHeight = rc.bottom - TAB_HEIGHT;
             int cols = std::max(1, (int)((rc.right - 20) / g_fontWidth));
             int rows = std::max(1, (int)((termHeight - 20) / g_fontHeight));
             if (g_activeSessionIndex < g_sessions.size()) {
                 Session* s = g_sessions[g_activeSessionIndex];
                 g_pty.ResizePseudoConsole(s->hPC, {(SHORT)cols, (SHORT)rows});
                 s->Resize(rows, cols);
             }
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            // Always use arrow cursor in terminal (like PowerShell)
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        {
            int x = LOWORD(lParam); int y = HIWORD(lParam);
            if (y < TAB_HEIGHT) {
                int tabWidth = 140;
                int clickedIndex = x / tabWidth;
                if (clickedIndex < g_sessions.size()) {
                    int tabLeft = clickedIndex * tabWidth;
                    if (x > tabLeft + tabWidth - 25) CloseSession(clickedIndex);
                    else SwitchTab(clickedIndex);
                } else if (clickedIndex == g_sessions.size() && x < ((g_sessions.size()*tabWidth)+40)) CreateNewSession();
            } else {
                // Start text selection in terminal area
                ScreenToCell(x, y, g_selStartRow, g_selStartCol);
                g_selEndRow = g_selStartRow;
                g_selEndCol = g_selStartCol;
                g_selecting = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (g_selecting) {
            int x = LOWORD(lParam); int y = HIWORD(lParam);
            ScreenToCell(x, y, g_selEndRow, g_selEndCol);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_selecting) {
            int x = LOWORD(lParam); int y = HIWORD(lParam);
            ScreenToCell(x, y, g_selEndRow, g_selEndCol);
            g_selecting = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            
            // If no actual selection (same start/end), clear
            if (g_selStartRow == g_selEndRow && g_selStartCol == g_selEndCol) {
                ClearSelection();
            }
        }
        return 0;

    case WM_MBUTTONUP:
        {
            int x = LOWORD(lParam); int y = HIWORD(lParam);
            if (y < TAB_HEIGHT) {
                int clickedIndex = x / 140;
                if (clickedIndex < g_sessions.size()) CloseSession(clickedIndex);
            }
        }
        return 0;

    case WM_RBUTTONUP:
        // Right-click: copy if selection, paste if no selection
        if (g_activeSessionIndex >= 0) {
            Session* s = g_sessions[g_activeSessionIndex];
            int y = HIWORD(lParam);
            if (y >= TAB_HEIGHT) {  // Only in terminal area
                if (HasSelection()) {
                    // Copy selection to clipboard
                    std::string text = GetSelectedText();
                    if (!text.empty() && OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
                        if (hMem) {
                            memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_TEXT, hMem);
                        }
                        CloseClipboard();
                        ClearSelection();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else {
                    // Paste from clipboard
                    if (OpenClipboard(hwnd)) {
                        HANDLE hData = GetClipboardData(CF_TEXT);
                        if (hData) {
                            char* text = (char*)GlobalLock(hData);
                            if (text) {
                                WriteFile(s->hPipeIn, text, strlen(text), NULL, NULL);
                                GlobalUnlock(hData);
                            }
                        }
                        CloseClipboard();
                    }
                }
            }
        }
        return 0;
        
    case WM_MOUSEWHEEL:
        if (g_activeSessionIndex >= 0) {
            Session* s = g_sessions[g_activeSessionIndex];
            std::lock_guard<std::mutex> lock(s->mutex);
            if (s->inAltBuffer) return 0; // Disable scroll in Alt Buffer

            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int lines = delta / WHEEL_DELTA * 3;
            s->viewOffset += lines;
            if (s->viewOffset < 0) s->viewOffset = 0;
            if (s->viewOffset > s->history.size()) s->viewOffset = s->history.size();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            PaintWindow(hwnd, hdc); EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_CHAR:
        if (g_activeSessionIndex >= 0) {
            if (GetKeyState(VK_CONTROL) & 0x8000) return 0; // Ignore Ctrl here, handled in KEYDOWN
            Session* s = g_sessions[g_activeSessionIndex];
            if (!s->inAltBuffer) { // Snap to bottom only if not in Alt
                std::lock_guard<std::mutex> lock(s->mutex); s->viewOffset = 0; InvalidateRect(hwnd, NULL, FALSE);
            }
            char c = (char)wParam;
            WriteFile(s->hPipeIn, &c, 1, NULL, NULL);
        }
        return 0;
        
    case WM_KEYDOWN:
        if (g_activeSessionIndex >= 0) {
            Session* s = g_sessions[g_activeSessionIndex];

            // Handle Ctrl Shortcuts
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                bool hasShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                
                if (wParam == 'T') { CreateNewSession(); return 0; }
                if (wParam == 'W') { CloseSession(g_activeSessionIndex); return 0; }
                
                // Ctrl+Shift+C - Copy selection to clipboard
                if (wParam == 'C' && hasShift) {
                    if (HasSelection()) {
                        std::string text = GetSelectedText();
                        if (!text.empty() && OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
                            if (hMem) {
                                memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_TEXT, hMem);
                            }
                            CloseClipboard();
                            ClearSelection();
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    return 0;
                }
                
                // Ctrl+Shift+V - Paste from clipboard
                if (wParam == 'V' && hasShift) {
                    if (OpenClipboard(hwnd)) {
                        HANDLE hData = GetClipboardData(CF_TEXT);
                        if (hData) {
                            char* text = (char*)GlobalLock(hData);
                            if (text) {
                                WriteFile(s->hPipeIn, text, strlen(text), NULL, NULL);
                                GlobalUnlock(hData);
                            }
                        }
                        CloseClipboard();
                    }
                    return 0;
                }
                
                // Generic Ctrl+A to Ctrl+Z mapping (send control char to terminal)
                if (wParam >= 'A' && wParam <= 'Z' && !hasShift) {
                    char c = (char)(wParam - 'A' + 1);
                    WriteFile(s->hPipeIn, &c, 1, NULL, NULL);
                    return 0;
                }
            }

            // Snap to bottom
            if (!s->inAltBuffer) {
                std::lock_guard<std::mutex> lock(s->mutex); s->viewOffset = 0; InvalidateRect(hwnd, NULL, FALSE);
            }
            
            const char* seq = nullptr;
            switch (wParam) {
                 case VK_UP: seq = "\x1b[A"; break;
                 case VK_DOWN: seq = "\x1b[B"; break;
                 case VK_RIGHT: seq = "\x1b[C"; break;
                 case VK_LEFT: seq = "\x1b[D"; break;
                 case VK_BACK: seq = "\x7f"; break; // DEL for Backspace
                 case VK_DELETE: seq = "\x1b[3~"; break;
                 case VK_HOME: seq = "\x1b[H"; break;
                 case VK_END: seq = "\x1b[F"; break;
                 case VK_PRIOR: seq = "\x1b[5~"; break; // PgUp
                 case VK_NEXT: seq = "\x1b[6~"; break; // PgDn
            }
            if (seq) WriteFile(s->hPipeIn, seq, strlen(seq), NULL, NULL);
        }
        return 0;
        
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconA(hInstance, "id");
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, NULL, NULL, hInstance, NULL);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}
