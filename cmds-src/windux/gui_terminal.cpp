// Compile: g++ -std=c++17 -static -mwindows -o cmds\terminal.exe cmds-src\windux\gui_terminal.cpp cmds-src\windux\windux.res -lgdi32 -luser32 -ldwmapi -lshell32

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
#include <fstream>
#include <sstream>
#include <shellapi.h>

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
    RGB(15, 17, 20), RGB(204, 62, 68), RGB(38, 180, 58), RGB(215, 175, 40),
    RGB(55, 120, 230), RGB(160, 60, 176), RGB(68, 170, 220), RGB(210, 210, 210),
    RGB(100, 105, 115), RGB(240, 85, 90), RGB(50, 215, 60), RGB(250, 240, 150),
    RGB(86, 156, 255), RGB(190, 60, 175), RGB(90, 220, 220), RGB(240, 242, 245)
};

const COLORREF DEFAULT_BG = RGB(15, 17, 20);
const COLORREF DEFAULT_FG = RGB(210, 215, 220);
const COLORREF TAB_BG = RGB(20, 22, 28);
const COLORREF TAB_ACTIVE_BG = RGB(32, 36, 46);
const COLORREF ACCENT = RGB(86, 156, 255);
const COLORREF SELECTION_BG = RGB(55, 70, 110);
const int TAB_HEIGHT = 34;

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

// ============================================================================
// Shell Profiles
// ============================================================================

struct ShellProfile {
    std::string name;
    std::string path;
    std::string sdir; // Starting directory
    std::string iconPath;
    bool isDefault = false;
    HICON hIcon = NULL;
};

std::vector<ShellProfile> g_profiles;
std::string g_shellsProfPath;

struct Settings {
    std::string fontName = "Consolas";
    int fontSize = 16;
    int opacity = 240;
    std::string cursorStyle = "underline";
};

Settings g_settings;
std::string g_settingsProfPath;

struct UrlSpan {
    int startCol;
    int endCol;
    std::string url;
};

void LoadProfiles() {
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    // Adjusted: if running from cmds/ and executable is in cmds/, 
    // we want shells/ at same level as executable or parent? 
    // User said "windux on startup doesnt detect any shells folder it'll create the folder"
    // Usually relative to CWD or EXE. Let's use EXE dir.
    
    fs::path shellsDir = exeDir / "shells";
    if (!fs::exists(shellsDir)) {
        fs::create_directory(shellsDir);
    }
    
    fs::path profPath = shellsDir / "shells.prof";
    g_shellsProfPath = profPath.string();
    if (!fs::exists(profPath)) {
        std::ofstream out(profPath);
        out << "[Linuxify]\n";
        out << "    path: \"linuxify.exe\"\n";
        out << "    sdir: \"home\"\n";
        out << "    default: true\n";
        out << "    icon: \"../../assets/linux-logo.ico\"\n";
        out.close();
    }
    
    // Parse
    std::ifstream file(profPath);
    std::string line;
    ShellProfile current;
    bool inSection = false;
    
    while (std::getline(file, line)) {
        // Trim
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t");
        std::string trimmed = line.substr(first, last - first + 1);
        
        // Remove comments
        size_t comment = trimmed.find("//");
        if (comment != std::string::npos) trimmed = trimmed.substr(0, comment);
        // Retrim
        last = trimmed.find_last_not_of(" \t");
        if (last == std::string::npos) continue;
        trimmed = trimmed.substr(0, last + 1);
        
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            if (inSection) {
                if (!current.iconPath.empty()) {
                    fs::path resolvedIcon = fs::weakly_canonical(shellsDir / current.iconPath);
                    if (fs::exists(resolvedIcon)) {
                        current.iconPath = resolvedIcon.string();
                        current.hIcon = (HICON)LoadImageA(NULL, current.iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
                    }
                }
                g_profiles.push_back(current);
            }
            current = ShellProfile();
            current.name = trimmed.substr(1, trimmed.size() - 2);
            inSection = true;
        } else if (inSection) {
            size_t colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string key = trimmed.substr(0, colon);
                std::string val = trimmed.substr(colon + 1);
                
                // key trim
                last = key.find_last_not_of(" \t");
                if (last != std::string::npos) key = key.substr(0, last + 1);
                
                // val trim & remove quotes
                first = val.find_first_not_of(" \t");
                if (first != std::string::npos) val = val.substr(first);
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                    val = val.substr(1, val.size() - 2);
                }
                
                if (key == "path") current.path = val;
                else if (key == "sdir") current.sdir = val;
                else if (key == "icon") current.iconPath = val;
                else if (key == "default") current.isDefault = (val == "true");
            }
        }
    }
    if (inSection) {
        if (!current.iconPath.empty()) {
            fs::path resolvedIcon = fs::weakly_canonical(shellsDir / current.iconPath);
            if (fs::exists(resolvedIcon)) {
                current.iconPath = resolvedIcon.string();
                current.hIcon = (HICON)LoadImageA(NULL, current.iconPath.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
            }
        }
        g_profiles.push_back(current);
    }
    
    // Fallback if empty
    if (g_profiles.empty()) {
        char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        if (exeDir.filename() == "cmds") exeDir = exeDir.parent_path();
        std::string defaultShell = (exeDir / "linuxify.exe").string();
        
        g_profiles.push_back({"Linuxify", defaultShell, "home", "home", true, NULL});
    }
}

void LoadSettings() {
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path shellsDir = exeDir / "shells";
    if (!fs::exists(shellsDir)) fs::create_directory(shellsDir);
    fs::path settPath = shellsDir / "settings.prof";
    g_settingsProfPath = settPath.string();
    if (!fs::exists(settPath)) {
        std::ofstream out(settPath);
        out << "[Settings]\n";
        out << "    font: \"Consolas\"\n";
        out << "    fontSize: 16\n";
        out << "    opacity: 240\n";
        out << "    cursorStyle: \"underline\"\n";
        out.close();
    }
    std::ifstream file(settPath);
    std::string line;
    bool inSection = false;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(first, last - first + 1);
        if (trimmed.front() == '[' && trimmed.back() == ']') { inSection = true; continue; }
        if (!inSection) continue;
        size_t colon = trimmed.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trimmed.substr(0, colon);
        std::string val = trimmed.substr(colon + 1);
        last = key.find_last_not_of(" \t");
        if (last != std::string::npos) key = key.substr(0, last + 1);
        first = val.find_first_not_of(" \t");
        if (first != std::string::npos) val = val.substr(first);
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
        if (key == "font") g_settings.fontName = val;
        else if (key == "fontSize") g_settings.fontSize = std::max(8, std::min(72, std::stoi(val)));
        else if (key == "opacity") g_settings.opacity = std::max(50, std::min(255, std::stoi(val)));
        else if (key == "cursorStyle") g_settings.cursorStyle = val;
    }
}

void SaveSettings() {
    std::ofstream out(g_settingsProfPath);
    out << "[Settings]\n";
    out << "    font: \"" << g_settings.fontName << "\"\n";
    out << "    fontSize: " << g_settings.fontSize << "\n";
    out << "    opacity: " << g_settings.opacity << "\n";
    out << "    cursorStyle: \"" << g_settings.cursorStyle << "\"\n";
    out.close();
}

// ============================================================================
// Data Structures
// ============================================================================

struct Cell {
    char ch = ' ';
    COLORREF fg = DEFAULT_FG;
    COLORREF bg = DEFAULT_BG;
};

std::vector<UrlSpan> FindUrlsInRow(const std::vector<Cell>& row) {
    std::vector<UrlSpan> urls;
    std::string text;
    text.reserve(row.size());
    for (const auto& c : row) text += c.ch;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t httpPos = text.find("http://", pos);
        size_t httpsPos = text.find("https://", pos);
        size_t urlStart = std::min(httpPos, httpsPos);
        if (urlStart == std::string::npos) break;
        size_t urlEnd = urlStart;
        while (urlEnd < text.size() && text[urlEnd] > 32 && text[urlEnd] != '"' && text[urlEnd] != '<' && text[urlEnd] != '>') urlEnd++;
        while (urlEnd > urlStart && (text[urlEnd-1] == '.' || text[urlEnd-1] == ',' || text[urlEnd-1] == ')' || text[urlEnd-1] == ']' || text[urlEnd-1] == ';')) urlEnd--;
        if (urlEnd > urlStart + 7) {
            urls.push_back({(int)urlStart, (int)urlEnd - 1, text.substr(urlStart, urlEnd - urlStart)});
        }
        pos = urlEnd;
    }
    return urls;
}

struct Session;
struct ShellProfile;
void ProcessOutput(Session* session, const char* buffer, DWORD bytes);
void CreateNewSession(const ShellProfile* prof = nullptr);

enum ParseState { STATE_TEXT, STATE_ESCAPE, STATE_CSI, STATE_OSC };

struct Session {
    int id;
    std::string name = "Terminal";
    std::mutex mutex;
    std::vector<std::vector<Cell>> grid;
    std::deque<std::vector<Cell>> history;
    
    // Mouse Tracking Modes
    bool mouseMode = false;      // Any mouse reporting enabled
    bool sgrMouseMode = false;   // SGR extended mode (1006)
    bool clickMode = false;      // Click only (1000)
    bool dragMode = false;       // Drag support (1002)
    int viewOffset = 0; 
    
    // TUI Support
    bool inAltBuffer = false; 
    std::vector<std::vector<Cell>> savedGrid; // Backup for main buffer if needed, usually ConPTY handles restore content, but we might need to restore our specific history context if we cleared grid.
    // Actually, ConPTY sends the logic to restore. We just need to track mode to disable scrollbar.

    int cursorRow = 0; int cursorCol = 0;
    bool wrapPending = false;  // Deferred wrap: cursor at last col, wrap on next char
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
    HICON hIcon = NULL;

    void Resize(int r, int c) {
        std::lock_guard<std::mutex> lock(mutex);
        int oldRows = rows;
        rows = std::max(1, r); cols = std::max(1, c);
        
        // Resize current grid
        grid.resize(rows); 
        for (auto& row : grid) {
            row.resize(cols, Cell{' ', DEFAULT_FG, DEFAULT_BG});
        }
        
        // Resize saved grid if exists (to prevent restoring wrong size)
        if (!savedGrid.empty()) {
            savedGrid.resize(rows);
            for (auto& row : savedGrid) {
                row.resize(cols, Cell{' ', DEFAULT_FG, DEFAULT_BG});
            }
        }
        
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

// ============================================================================
// Global State & Selection
// ============================================================================

bool g_selecting = false;
int g_selStartRow = -1, g_selStartCol = -1;
int g_selEndRow = -1, g_selEndCol = -1;

// UI State
bool g_hoverPlus = false;
bool g_hoverDown = false;
HWND g_hMenuWnd = NULL;
int g_menuHoverIndex = -1;
std::string g_startDir;

float g_scrollCurrent = 0.0f;
int g_scrollTarget = 0;
bool g_scrollAnimating = false;
#define SCROLL_TIMER_ID 1

int g_draggingTab = -1;
int g_dragStartX = 0;

int g_hoverUrlRow = -1;
int g_hoverUrlStartCol = -1;
int g_hoverUrlEndCol = -1;
std::string g_hoverUrlText;

HWND g_hSettingsWnd = NULL;

// Custom Menu Window
LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const int itemHeight = 28;
    const int separatorHeight = 9;
    int editShellsY = 1 + (int)g_profiles.size() * itemHeight + separatorHeight;
    int settingsY = editShellsY + itemHeight;
    int totalMenuItems = (int)g_profiles.size() + 2;

    switch(msg) {
        case WM_PAINT:
        {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);

            HBRUSH hBg = CreateSolidBrush(RGB(22, 24, 30));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);

            HBRUSH hBorder = CreateSolidBrush(RGB(55, 60, 75));
            FrameRect(hdc, &rc, hBorder);
            DeleteObject(hBorder);

            SelectObject(hdc, g_hFont);
            SetBkMode(hdc, TRANSPARENT);

            for (size_t i = 0; i < g_profiles.size(); i++) {
                RECT rcItem = {1, 1 + (LONG)i * itemHeight, rc.right - 1, 1 + (LONG)(i+1) * itemHeight};

                if ((int)i == g_menuHoverIndex) {
                    HBRUSH hHover = CreateSolidBrush(RGB(40, 44, 56));
                    FillRect(hdc, &rcItem, hHover);
                    DeleteObject(hHover);
                }

                SetTextColor(hdc, RGB(210, 215, 220));

                if (g_profiles[i].hIcon) {
                    DrawIconEx(hdc, rcItem.left + 8, rcItem.top + 6, g_profiles[i].hIcon, 16, 16, 0, NULL, DI_NORMAL);
                }

                RECT rcText = rcItem; rcText.left += 30;
                DrawTextA(hdc, g_profiles[i].name.c_str(), -1, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            int sepY = 1 + (int)g_profiles.size() * itemHeight + 4;
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(55, 60, 75));
            HPEN hOld = (HPEN)SelectObject(hdc, hPen);
            MoveToEx(hdc, 8, sepY, NULL);
            LineTo(hdc, rc.right - 8, sepY);
            SelectObject(hdc, hOld);
            DeleteObject(hPen);

            RECT rcEdit = {1, (LONG)editShellsY, rc.right - 1, (LONG)(editShellsY + itemHeight)};
            if (g_menuHoverIndex == (int)g_profiles.size()) {
                HBRUSH hHover = CreateSolidBrush(RGB(40, 44, 56));
                FillRect(hdc, &rcEdit, hHover);
                DeleteObject(hHover);
            }
            SetTextColor(hdc, RGB(170, 175, 185));
            RECT rcEditText = rcEdit; rcEditText.left += 12;
            DrawTextA(hdc, "Edit Shells...", -1, &rcEditText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT rcSettings = {1, (LONG)settingsY, rc.right - 1, (LONG)(settingsY + itemHeight)};
            if (g_menuHoverIndex == (int)g_profiles.size() + 1) {
                HBRUSH hHover2 = CreateSolidBrush(RGB(40, 44, 56));
                FillRect(hdc, &rcSettings, hHover2);
                DeleteObject(hHover2);
            }
            SetTextColor(hdc, RGB(170, 175, 185));
            RECT rcSettText = rcSettings; rcSettText.left += 12;
            DrawTextA(hdc, "Settings...", -1, &rcSettText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            int y = HIWORD(lParam);
            int newIndex = -1;
            if (y >= 1 && y < 1 + (int)g_profiles.size() * itemHeight) {
                newIndex = (y - 1) / itemHeight;
                if (newIndex >= (int)g_profiles.size()) newIndex = -1;
            } else if (y >= editShellsY && y < editShellsY + itemHeight) {
                newIndex = (int)g_profiles.size();
            } else if (y >= settingsY && y < settingsY + itemHeight) {
                newIndex = (int)g_profiles.size() + 1;
            }

            if (newIndex != g_menuHoverIndex) {
                g_menuHoverIndex = newIndex;
                InvalidateRect(hwnd, NULL, FALSE);

                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            g_menuHoverIndex = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_LBUTTONUP:
        {
            int y = HIWORD(lParam);
            int index = -1;
            if (y >= 1 && y < 1 + (int)g_profiles.size() * itemHeight) {
                index = (y - 1) / itemHeight;
            } else if (y >= editShellsY && y < editShellsY + itemHeight) {
                index = (int)g_profiles.size();
            } else if (y >= settingsY && y < settingsY + itemHeight) {
                index = (int)g_profiles.size() + 1;
            }

            if (index >= 0 && index < (int)g_profiles.size()) {
                CreateNewSession(&g_profiles[index]);
            } else if (index == (int)g_profiles.size()) {
                ShellExecuteA(NULL, "open", g_shellsProfPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            } else if (index == (int)g_profiles.size() + 1) {
                PostMessage(g_hwnd, WM_USER + 100, 0, 0);
            }
            ShowWindow(hwnd, SW_HIDE);
            DestroyWindow(hwnd);
            g_hMenuWnd = NULL;
            return 0;
        }

        case WM_KILLFOCUS:
            DestroyWindow(hwnd);
            g_hMenuWnd = NULL;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void RegisterMenuClass() {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = MenuWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "LinuxifyMenuClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_DROPSHADOW; // Add drop shadow
    RegisterClassExA(&wc);
}

void ApplySettings();
void ShowSettingsDialog();

#define IDC_SFONT 201
#define IDC_SFONTSIZE 202
#define IDC_SOPACITY 203
#define IDC_SCURSOR 204
#define IDC_SAPPLY 205

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
        {
            HFONT hUiFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            auto MakeLabel = [&](const char* text, int x, int y, int w, int h) {
                HWND lbl = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, hwnd, NULL, GetModuleHandle(NULL), NULL);
                SendMessage(lbl, WM_SETFONT, (WPARAM)hUiFont, TRUE);
            };

            MakeLabel("Font:", 15, 22, 90, 20);
            HWND hFont = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", g_settings.fontName.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 120, 20, 200, 24, hwnd, (HMENU)IDC_SFONT, GetModuleHandle(NULL), NULL);
            SendMessage(hFont, WM_SETFONT, (WPARAM)hUiFont, TRUE);

            MakeLabel("Font Size:", 15, 56, 90, 20);
            HWND hSize = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", std::to_string(g_settings.fontSize).c_str(), WS_CHILD | WS_VISIBLE | ES_NUMBER, 120, 54, 70, 24, hwnd, (HMENU)IDC_SFONTSIZE, GetModuleHandle(NULL), NULL);
            SendMessage(hSize, WM_SETFONT, (WPARAM)hUiFont, TRUE);

            MakeLabel("Opacity (50-255):", 15, 90, 100, 20);
            HWND hOpac = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", std::to_string(g_settings.opacity).c_str(), WS_CHILD | WS_VISIBLE | ES_NUMBER, 120, 88, 70, 24, hwnd, (HMENU)IDC_SOPACITY, GetModuleHandle(NULL), NULL);
            SendMessage(hOpac, WM_SETFONT, (WPARAM)hUiFont, TRUE);

            MakeLabel("Cursor Style:", 15, 124, 90, 20);
            HWND hCursor = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 120, 122, 140, 120, hwnd, (HMENU)IDC_SCURSOR, GetModuleHandle(NULL), NULL);
            SendMessage(hCursor, WM_SETFONT, (WPARAM)hUiFont, TRUE);
            SendMessageA(hCursor, CB_ADDSTRING, 0, (LPARAM)"Underline");
            SendMessageA(hCursor, CB_ADDSTRING, 0, (LPARAM)"Block");
            SendMessageA(hCursor, CB_ADDSTRING, 0, (LPARAM)"Bar");
            int sel = 0;
            if (g_settings.cursorStyle == "block") sel = 1;
            else if (g_settings.cursorStyle == "bar") sel = 2;
            SendMessage(hCursor, CB_SETCURSEL, sel, 0);

            HWND hApply = CreateWindowExA(0, "BUTTON", "Apply", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 120, 168, 100, 32, hwnd, (HMENU)IDC_SAPPLY, GetModuleHandle(NULL), NULL);
            SendMessage(hApply, WM_SETFONT, (WPARAM)hUiFont, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_SAPPLY && HIWORD(wParam) == BN_CLICKED) {
                char buf[256];
                GetDlgItemTextA(hwnd, IDC_SFONT, buf, sizeof(buf));
                g_settings.fontName = buf;

                GetDlgItemTextA(hwnd, IDC_SFONTSIZE, buf, sizeof(buf));
                g_settings.fontSize = std::max(8, std::min(72, atoi(buf)));

                GetDlgItemTextA(hwnd, IDC_SOPACITY, buf, sizeof(buf));
                g_settings.opacity = std::max(50, std::min(255, atoi(buf)));

                int csIdx = (int)SendDlgItemMessageA(hwnd, IDC_SCURSOR, CB_GETCURSEL, 0, 0);
                if (csIdx == 0) g_settings.cursorStyle = "underline";
                else if (csIdx == 1) g_settings.cursorStyle = "block";
                else if (csIdx == 2) g_settings.cursorStyle = "bar";

                SaveSettings();
                ApplySettings();
                DestroyWindow(hwnd);
                g_hSettingsWnd = NULL;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            g_hSettingsWnd = NULL;
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        {
            HDC hdcCtrl = (HDC)wParam;
            SetTextColor(hdcCtrl, RGB(210, 215, 220));
            SetBkColor(hdcCtrl, RGB(30, 33, 42));
            static HBRUSH hEditBg = CreateSolidBrush(RGB(30, 33, 42));
            return (LRESULT)hEditBg;
        }
        case WM_CTLCOLORBTN:
        {
            static HBRUSH hBtnBg = CreateSolidBrush(RGB(40, 44, 56));
            return (LRESULT)hBtnBg;
        }
        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH hBg = CreateSolidBrush(RGB(22, 24, 30));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);
            return 1;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void RegisterSettingsClass() {
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "LinuxifySettingsClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExA(&wc);
}

void ShowSettingsDialog() {
    if (g_hSettingsWnd) { SetForegroundWindow(g_hSettingsWnd); return; }
    RECT rcMain; GetWindowRect(g_hwnd, &rcMain);
    int x = rcMain.left + 100;
    int y = rcMain.top + 80;
    g_hSettingsWnd = CreateWindowExA(WS_EX_TOOLWINDOW, "LinuxifySettingsClass", "Windux Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 350, 240, g_hwnd, NULL, GetModuleHandle(NULL), NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hSettingsWnd, 20, &dark, sizeof(dark));
    ShowWindow(g_hSettingsWnd, SW_SHOW);
    SetForegroundWindow(g_hSettingsWnd);
}

void ApplySettings() {
    if (g_hFont) DeleteObject(g_hFont);
    g_hFont = CreateFontA(g_settings.fontSize, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, g_settings.fontName.c_str());
    HDC hdc = GetDC(g_hwnd);
    SelectObject(hdc, g_hFont);
    TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
    g_fontWidth = tm.tmAveCharWidth;
    g_fontHeight = tm.tmHeight;
    ReleaseDC(g_hwnd, hdc);
    SetLayeredWindowAttributes(g_hwnd, 0, (BYTE)g_settings.opacity, LWA_ALPHA);
    RECT rc; GetClientRect(g_hwnd, &rc);
    int termHeight = rc.bottom - TAB_HEIGHT;
    int cols = std::max(20, (int)((rc.right - 20 - SCROLLBAR_WIDTH) / g_fontWidth));
    int rows = std::max(5, (int)((termHeight - 20) / g_fontHeight));
    for (Session* s : g_sessions) {
        if (s && s->hPC) {
            g_pty.ResizePseudoConsole(s->hPC, {(SHORT)cols, (SHORT)rows});
            s->Resize(rows, cols);
        }
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
}

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
    
    for (size_t i = 0; i < params.length(); i++) {
        char c = params[i];
        if (isdigit(c)) {
            current += c;
        }
        else if (c == ';') { 
            codes.push_back(current.empty() ? 0 : std::stoi(current)); 
            current = ""; 
        }
        // Ignore other chars like '?' so they don't block number parsing
    }
    if (!current.empty()) codes.push_back(std::stoi(current));
    if (codes.empty()) codes.push_back(0);

    switch (cmd) {
    case 'm': 
        for (size_t i = 0; i < codes.size(); ++i) {
            int c = codes[i];
            if (c == 0) { s->currentFg = DEFAULT_FG; s->currentBg = DEFAULT_BG; }
            else if (c == 1) { if (s->currentFg == DEFAULT_FG) s->currentFg = PALETTE[15]; }
            else if (c == 7) { std::swap(s->currentFg, s->currentBg); } // Reverse video
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
        
    // Erase in Display (ED)
    case 'J': 
        {
            int mode = codes[0];
            if (mode == 0) {
                // Erase from cursor to end of screen
                if (s->cursorRow < s->rows) {
                    auto& row = s->grid[s->cursorRow];
                    for (int i = s->cursorCol; i < s->cols; ++i) row[i] = Cell{' ', s->currentFg, s->currentBg};
                }
                for (int r = s->cursorRow + 1; r < s->rows; ++r) {
                    for (auto& c : s->grid[r]) c = Cell{' ', s->currentFg, s->currentBg};
                }
            } else if (mode == 1) {
                // Erase from start of screen to cursor
                for (int r = 0; r < s->cursorRow; ++r) {
                    for (auto& c : s->grid[r]) c = Cell{' ', s->currentFg, s->currentBg};
                }
                if (s->cursorRow < s->rows) {
                    auto& row = s->grid[s->cursorRow];
                    for (int i = 0; i <= s->cursorCol && i < s->cols; ++i) row[i] = Cell{' ', s->currentFg, s->currentBg};
                }
            } else if (mode == 2 || mode == 3) {
                // Erase entire screen
                for (auto& r : s->grid) for (auto& c : r) c = Cell{' ', s->currentFg, s->currentBg};
            }
        }
        break;
        
    // Erase in Line (EL)
    case 'K':
        if (s->cursorRow < s->rows && s->cursorRow >= 0) {
            auto& row = s->grid[s->cursorRow];
            int mode = codes[0];
            if (mode == 0) {
                // Erase from cursor to end of line
                for (int i = s->cursorCol; i < s->cols; ++i) row[i] = Cell{' ', s->currentFg, s->currentBg};
            } else if (mode == 1) {
                // Erase from start of line to cursor
                for (int i = 0; i <= s->cursorCol && i < s->cols; ++i) row[i] = Cell{' ', s->currentFg, s->currentBg};
            } else if (mode == 2) {
                // Erase entire line
                for (auto& c : row) c = Cell{' ', s->currentFg, s->currentBg};
            }
        }
        break;
        
    // Cursor Position (CUP) / Horizontal and Vertical Position (HVP)
    case 'H':
    case 'f':
        {
            int row = (codes.size() >= 1 && codes[0] > 0) ? codes[0] - 1 : 0;
            int col = (codes.size() >= 2 && codes[1] > 0) ? codes[1] - 1 : 0;
            s->cursorRow = std::min(std::max(0, row), s->rows - 1);
            s->cursorCol = std::min(std::max(0, col), s->cols - 1);
        }
        break;
        
    // Cursor Horizontal Absolute (CHA) - move to column N
    case 'G':
        {
            int col = (codes[0] > 0) ? codes[0] - 1 : 0;
            s->cursorCol = std::min(std::max(0, col), s->cols - 1);
        }
        break;
        
    // Vertical Position Absolute (VPA) - move to row N
    case 'd':
        {
            int row = (codes[0] > 0) ? codes[0] - 1 : 0;
            s->cursorRow = std::min(std::max(0, row), s->rows - 1);
        }
        break;
    
    // Cursor Up (CUU)
    case 'A': 
        s->cursorRow = std::max(0, s->cursorRow - (codes[0] ? codes[0] : 1)); 
        break;
        
    // Cursor Down (CUD)
    case 'B': 
        s->cursorRow = std::min(s->rows - 1, s->cursorRow + (codes[0] ? codes[0] : 1)); 
        break;
        
    // Cursor Forward (CUF)
    case 'C': 
        s->cursorCol = std::min(s->cols - 1, s->cursorCol + (codes[0] ? codes[0] : 1)); 
        break;
        
    // Cursor Back (CUB)
    case 'D': 
        s->cursorCol = std::max(0, s->cursorCol - (codes[0] ? codes[0] : 1)); 
        break;
        
    // Cursor Next Line (CNL)
    case 'E':
        s->cursorRow = std::min(s->rows - 1, s->cursorRow + (codes[0] ? codes[0] : 1));
        s->cursorCol = 0;
        break;
        
    // Cursor Previous Line (CPL)
    case 'F':
        s->cursorRow = std::max(0, s->cursorRow - (codes[0] ? codes[0] : 1));
        s->cursorCol = 0;
        break;
        
    // Insert Line (IL)
    case 'L':
        {
            int count = codes[0] ? codes[0] : 1;
            for (int n = 0; n < count && s->cursorRow < s->rows; ++n) {
                // Shift lines down from cursor
                for (int r = s->rows - 1; r > s->cursorRow; --r) {
                    s->grid[r] = s->grid[r - 1];
                }
                // Clear current line
                s->grid[s->cursorRow].assign(s->cols, Cell{' ', s->currentFg, s->currentBg});
            }
        }
        break;
        
    // Delete Line (DL)
    case 'M':
        {
            int count = codes[0] ? codes[0] : 1;
            for (int n = 0; n < count && s->cursorRow < s->rows; ++n) {
                // Shift lines up from cursor
                for (int r = s->cursorRow; r < s->rows - 1; ++r) {
                    s->grid[r] = s->grid[r + 1];
                }
                // Clear bottom line
                s->grid[s->rows - 1].assign(s->cols, Cell{' ', s->currentFg, s->currentBg});
            }
        }
        break;

        break;
    
    // Insert Character (ICH)
    case '@':
        if (s->cursorRow < s->rows) {
            int count = codes[0] ? codes[0] : 1;
            auto& row = s->grid[s->cursorRow];
            // Shift characters right from cursor
            for (int i = s->cols - 1; i >= s->cursorCol + count; --i) {
                row[i] = row[i - count];
            }
            // Clear inserted positions
            for (int i = s->cursorCol; i < s->cursorCol + count && i < s->cols; ++i) {
                row[i] = Cell{' ', s->currentFg, s->currentBg};
            }
        }
        break;
        
    // Delete Character (DCH)
    case 'P':
        if (s->cursorRow < s->rows) {
            int count = codes[0] ? codes[0] : 1;
            auto& row = s->grid[s->cursorRow];
            // Shift characters left from cursor
            for (int i = s->cursorCol; i < s->cols - count; ++i) {
                row[i] = row[i + count];
            }
            // Clear trailing positions
            for (int i = s->cols - count; i < s->cols; ++i) {
                if (i >= 0) row[i] = Cell{' ', s->currentFg, s->currentBg};
            }
        }
        break;
        
    // Erase Character (ECH)
    case 'X':
        if (s->cursorRow < s->rows) {
            int count = codes[0] ? codes[0] : 1;
            auto& row = s->grid[s->cursorRow];
            for (int i = s->cursorCol; i < s->cursorCol + count && i < s->cols; ++i) {
                row[i] = Cell{' ', s->currentFg, s->currentBg};
            }
        }
        break;
        
    // Scroll Up (SU)
    case 'S':
        {
            int count = codes[0] ? codes[0] : 1;
            for (int n = 0; n < count; ++n) {
                s->Scroll();
            }
        }
        break;
        
    // Scroll Down (SD)
    case 'T':
        {
            int count = codes[0] ? codes[0] : 1;
            for (int n = 0; n < count; ++n) {
                // Insert line at top, shift everything down
                for (int r = s->rows - 1; r > 0; --r) {
                    s->grid[r] = s->grid[r - 1];
                }
                s->grid[0].assign(s->cols, Cell{' ', s->currentFg, s->currentBg});
            }
        }
        break;
        
    // Set Scrolling Region (DECSTBM) - we don't track regions, but handle sequence
    case 'r':
        // Currently ignoring scroll region - full screen scrolling only
        break;
    
    // Private Modes
    case 'h': // SM (Set Mode)
        if (privateMode) {
            for (int code : codes) {
                if (code == 1049 || code == 47 || code == 1047) { 
                    // Switch to alternate screen buffer
                    s->savedGrid = s->grid;
                    s->inAltBuffer = true; 
                    s->viewOffset = 0;
                    // Clear the alternate buffer
                    for (auto& r : s->grid) for (auto& c : r) c = Cell{' ', DEFAULT_FG, DEFAULT_BG};
                } else if (code == 25) {
                    // Show cursor (DECTCEM)
                } else if (code == 1000) { s->mouseMode = true; s->clickMode = true; s->dragMode = false; }
                else if (code == 1002) { s->mouseMode = true; s->clickMode = true; s->dragMode = true; }
                else if (code == 1006) { s->sgrMouseMode = true; }
            }
        }
        break;
    case 'l': // RM (Reset Mode)
        if (privateMode) {
            for (int code : codes) {
                if (code == 1049 || code == 47 || code == 1047) { 
                    // Switch back to main screen buffer
                    s->inAltBuffer = false;
                    if (!s->savedGrid.empty()) {
                        s->grid = s->savedGrid;
                        s->savedGrid.clear();
                    }
                } else if (code == 25) {
                    // Hide cursor (DECTCEM)
                } else if (code == 1000) { s->mouseMode = false; s->clickMode = false; }
                else if (code == 1002) { s->mouseMode = false; s->dragMode = false; }
                else if (code == 1006) { s->sgrMouseMode = false; }
            }
        }
        break;
    }
    
    // Clamp cursor position after any operation
    if (s->cursorRow < 0) s->cursorRow = 0;
    if (s->cursorRow >= s->rows) s->cursorRow = s->rows - 1;
    if (s->cursorCol < 0) s->cursorCol = 0;
    if (s->cursorCol >= s->cols) s->cursorCol = s->cols - 1;
    
    // Reset wrapPending ONLY when cursor is explicitly moved
    switch (cmd) {
        case 'H': case 'f': case 'G': case 'd': case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            s->wrapPending = false;
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
            if (c == '\x1b') {
                s->parseState = STATE_ESCAPE;
            }
            else if (c == '\r') {
                s->cursorCol = 0;
                s->wrapPending = false;
            }
            else if (c == '\n') {
                s->wrapPending = false;
                s->cursorRow++;
                if (s->cursorRow >= s->rows) { s->Scroll(); s->cursorRow = s->rows - 1; }
            }
            else if (c == '\b') {
                s->wrapPending = false;
                if (s->cursorCol > 0) s->cursorCol--;
            }
            else if (c == '\a') {}
            else if (c >= 32 || c == '\t') {
                if (c == '\t') c = ' ';
                
                // If wrap was pending from previous char at end of line, now actually wrap
                if (s->wrapPending) {
                    s->wrapPending = false;
                    s->cursorCol = 0;
                    s->cursorRow++;
                    if (s->cursorRow >= s->rows) { s->Scroll(); s->cursorRow = s->rows - 1; }
                }
                
                if (s->cursorRow < s->rows && s->cursorCol < s->cols) {
                    s->grid[s->cursorRow][s->cursorCol] = Cell{c, s->currentFg, s->currentBg};
                    s->cursorCol++;
                    
                    // If we just wrote to the last column, set wrap pending (deferred wrap)
                    if (s->cursorCol >= s->cols) {
                        s->cursorCol = s->cols - 1;  // Keep cursor at last column
                        s->wrapPending = true;       // Mark that next char should wrap
                    }
                }
            }
            break;
        case STATE_ESCAPE:
            if (c == '[') { s->parseState = STATE_CSI; s->csiParams = ""; }
            else if (c == ']') { s->parseState = STATE_OSC; s->csiParams = ""; }
            else if (c == 'M') {
                // Reverse Index (RI) - move cursor up, scroll down if needed
                if (s->cursorRow > 0) {
                    s->cursorRow--;
                } else {
                    // Scroll down - insert line at top
                    for (int r = s->rows - 1; r > 0; --r) {
                        s->grid[r] = s->grid[r - 1];
                    }
                    s->grid[0].assign(s->cols, Cell{' ', s->currentFg, s->currentBg});
                }
                s->wrapPending = false;
                s->parseState = STATE_TEXT;
            }
            else if (c == 'D') {
                // Index (IND) - move cursor down, scroll up if needed
                s->cursorRow++;
                if (s->cursorRow >= s->rows) { s->Scroll(); s->cursorRow = s->rows - 1; }
                s->wrapPending = false;
                s->parseState = STATE_TEXT;
            }
            else if (c == 'E') {
                // Next Line (NEL) - move to start of next line
                s->cursorCol = 0;
                s->cursorRow++;
                if (s->cursorRow >= s->rows) { s->Scroll(); s->cursorRow = s->rows - 1; }
                s->wrapPending = false;
                s->parseState = STATE_TEXT;
            }
            else if (c == '7') {
                // Save cursor (DECSC) - ignoring for now
                s->parseState = STATE_TEXT;
            }
            else if (c == '8') {
                // Restore cursor (DECRC) - ignoring for now
                s->parseState = STATE_TEXT;
            }
            else {
                s->parseState = STATE_TEXT;
            }
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

void CreateNewSession(const ShellProfile* prof) {
    // Load profiles if empty
    if (g_profiles.empty()) LoadProfiles();
    
    // Find default if none specified
    if (!prof) {
        for (const auto& p : g_profiles) {
            if (p.isDefault) {
                prof = &p;
                break;
            }
        }
        // Fallback to first if no default marked
        if (!prof && !g_profiles.empty()) prof = &g_profiles[0];
    }

    Session* s = new Session();
    s->id = g_sessions.size() + 1;
    s->name = prof ? prof->name : "Terminal";
    s->hIcon = prof ? prof->hIcon : NULL;
    
    RECT rc; GetClientRect(g_hwnd, &rc);
    int termHeight = rc.bottom - TAB_HEIGHT;
    // FIX: Enforce minimum dimensions
    int cols = std::max(20, (int)((rc.right - 20 - SCROLLBAR_WIDTH) / g_fontWidth));
    int rows = std::max(5, (int)((termHeight - 20) / g_fontHeight));
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

    std::string cmd;
    std::string cwd = ""; 

    if (prof) {
        cmd = prof->path;
        if (cmd.find(':') == std::string::npos && cmd.find('/') == std::string::npos && cmd.find('\\') == std::string::npos) {
             char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
             fs::path exeDir = fs::path(exePath).parent_path();
             if (exeDir.filename() == "cmds" && cmd == "linuxify.exe") exeDir = exeDir.parent_path();
             cmd = (exeDir / cmd).string();
        }
        
        if (!g_startDir.empty()) {
            cwd = g_startDir;
            g_startDir.clear();
        } else if (!prof->sdir.empty()) {
            if (prof->sdir == "home") {
                const char* home = getenv("USERPROFILE");
                if (home) cwd = std::string(home);
            } else {
                 char expandBuf[MAX_PATH];
                 ExpandEnvironmentStringsA(prof->sdir.c_str(), expandBuf, MAX_PATH);
                 cwd = std::string(expandBuf);
            }
        }
    } else {
        // Fallback safety
        char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        if (exeDir.filename() == "cmds") exeDir = exeDir.parent_path();
        cmd = (exeDir / "linuxify.exe").string();
    }

    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, cwd.empty() ? NULL : (LPSTR)cwd.c_str(), &siEx.StartupInfo, &s->pi);

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

    RECT rcTabSep = {0, TAB_HEIGHT - 1, rc.right, TAB_HEIGHT};
    HBRUSH hTabSep = CreateSolidBrush(RGB(40, 44, 56));
    FillRect(hdcMem, &rcTabSep, hTabSep);
    DeleteObject(hTabSep);

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
        
        Session* s = g_sessions[i];
        if (s->hIcon) {
             DrawIconEx(hdcMem, rcItem.left + 8, 8, s->hIcon, 16, 16, 0, NULL, DI_NORMAL);
        }

        std::string title = s->name; // Use custom name
        RECT rcText = rcItem; 
        rcText.left += 24; // Space for icon
        rcText.right -= 20; 
        DrawTextA(hdcMem, title.c_str(), -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        RECT rcClose = {rcItem.right - 20, rcItem.top, rcItem.right - 5, rcItem.bottom};
        SetTextColor(hdcMem, RGB(160, 80, 80));
        DrawTextA(hdcMem, "x", 1, &rcClose, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (i == g_activeSessionIndex) {
            RECT rcLine = {rcItem.left, TAB_HEIGHT - 3, rcItem.right, TAB_HEIGHT - 1};
            HBRUSH hLine = CreateSolidBrush(ACCENT);
            FillRect(hdcMem, &rcLine, hLine);
            DeleteObject(hLine);
        }
    }
    
    // Draw + button
    RECT rcPlus = { (LONG)g_sessions.size() * tabWidth, 0, (LONG)g_sessions.size() * tabWidth + 30, TAB_HEIGHT };
    if (g_hoverPlus) {
        HBRUSH hHover = CreateSolidBrush(RGB(45, 48, 58));
        FillRect(hdcMem, &rcPlus, hHover);
        DeleteObject(hHover);
    }
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(200, 200, 200));
    DrawTextA(hdcMem, "+", 1, &rcPlus, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Separator Line
    RECT rcSep = { rcPlus.right, 4, rcPlus.right + 1, TAB_HEIGHT - 4 };
    HBRUSH hSep = CreateSolidBrush(RGB(50, 55, 65));
    FillRect(hdcMem, &rcSep, hSep);
    DeleteObject(hSep);

    // Draw v dropdown
    RECT rcDown = { rcPlus.right + 1, 0, rcPlus.right + 31, TAB_HEIGHT };
    if (g_hoverDown) {
        HBRUSH hHover = CreateSolidBrush(RGB(45, 48, 58));
        FillRect(hdcMem, &rcDown, hHover);
        DeleteObject(hHover);
    }
    DrawTextA(hdcMem, "v", 1, &rcDown, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (g_activeSessionIndex >= 0 && g_activeSessionIndex < g_sessions.size()) {
        Session* s = g_sessions[g_activeSessionIndex];
        std::lock_guard<std::mutex> lock(s->mutex);
        
        SetBkMode(hdcMem, OPAQUE); // Reset for grid drawing
        
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

                int r1, c1, r2, c2;
                bool hasSel = HasSelection();
                if (hasSel) GetOrderedSelection(r1, c1, r2, c2);

                std::vector<UrlSpan> rowUrls = FindUrlsInRow(row);

                for (int c = 0; c < (int)row.size(); ++c) {
                    const Cell& cell = row[c];
                    int x = termX + c * g_fontWidth;
                    int y = termY + i * g_fontHeight;

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

                    bool isHoveredUrl = false;
                    bool isAnyUrl = false;
                    for (const auto& u : rowUrls) {
                        if (c >= u.startCol && c <= u.endCol) {
                            isAnyUrl = true;
                            if (i == g_hoverUrlRow && u.startCol == g_hoverUrlStartCol && u.endCol == g_hoverUrlEndCol) {
                                isHoveredUrl = true;
                            }
                            break;
                        }
                    }

                    if (isSelected) {
                        SetTextColor(hdcMem, cell.fg);
                        SetBkColor(hdcMem, SELECTION_BG);
                    } else if (isHoveredUrl) {
                        SetTextColor(hdcMem, ACCENT);
                        SetBkColor(hdcMem, cell.bg);
                    } else {
                        SetTextColor(hdcMem, cell.fg);
                        SetBkColor(hdcMem, cell.bg);
                    }
                    TextOutA(hdcMem, x, y, &cell.ch, 1);

                    if (isHoveredUrl) {
                        HPEN hULine = CreatePen(PS_SOLID, 1, ACCENT);
                        HPEN hOldPen = (HPEN)SelectObject(hdcMem, hULine);
                        MoveToEx(hdcMem, x, y + g_fontHeight - 1, NULL);
                        LineTo(hdcMem, x + g_fontWidth, y + g_fontHeight - 1);
                        SelectObject(hdcMem, hOldPen);
                        DeleteObject(hULine);
                    }
                }
            }
        }
        
        if (s->viewOffset == 0) {
            int visualRow = (historySize + s->cursorRow) - startLine;
            if (visualRow >= 0 && visualRow < maxVisible) {
                int cx = termX + s->cursorCol * g_fontWidth;
                int cy = termY + visualRow * g_fontHeight;
                HBRUSH hCaret = CreateSolidBrush(ACCENT);
                RECT rcCaret;
                if (g_settings.cursorStyle == "block") {
                    rcCaret = {cx, cy, cx + g_fontWidth, cy + g_fontHeight};
                } else if (g_settings.cursorStyle == "bar") {
                    rcCaret = {cx, cy, cx + 2, cy + g_fontHeight};
                } else {
                    rcCaret = {cx, cy + g_fontHeight - 2, cx + g_fontWidth, cy + g_fontHeight};
                }
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
             HBRUSH hSbTrack = CreateSolidBrush(RGB(20, 22, 28));
             FillRect(hdcMem, &rcSb, hSbTrack);
             DeleteObject(hSbTrack);

             float ratio = (float)s->rows / (float)totalRows;
             if (ratio > 1.0f) ratio = 1.0f;
             int thumbH = std::max(24, (int)(sbH * ratio));

             int maxStart = historySize;
             if (maxStart > 0) {
                 float posRatio = (float)startLine / (float)maxStart;
                 int thumbY = sbY + (int)((sbH - thumbH) * posRatio);
                 RECT rcThumb = {sbX + 3, thumbY, rc.right - 3, thumbY + thumbH};
                 HBRUSH hThumb = CreateSolidBrush(RGB(80, 85, 95));
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
        LoadSettings();
        g_hFont = CreateFontA(g_settings.fontSize, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, g_settings.fontName.c_str());
        {
            HDC hdc = GetDC(hwnd);
            SelectObject(hdc, g_hFont);
            TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
            g_fontWidth = tm.tmAveCharWidth;
            g_fontHeight = tm.tmHeight;
            ReleaseDC(hwnd, hdc);
        }
        LoadProfiles();
        if (!g_pty.Init()) MessageBoxA(NULL, "Failed to init ConPTY", "Error", MB_OK);
        else CreateNewSession();
        return 0;
        
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (g_pty.ResizePseudoConsole) {
             RECT rc; GetClientRect(hwnd, &rc);
             int termHeight = rc.bottom - TAB_HEIGHT;
             
             // FIX: Enforce minimum dimensions to prevent "1 row" collapse
             int cols = std::max(20, (int)((rc.right - 20 - SCROLLBAR_WIDTH) / g_fontWidth));
             int rows = std::max(5, (int)((termHeight - 20) / g_fontHeight)); // Minimum 5 rows

             for (Session* s : g_sessions) {
                 if (s && s->hPC) {
                     g_pty.ResizePseudoConsole(s->hPC, {(SHORT)cols, (SHORT)rows});
                     s->Resize(rows, cols);
                 }
             }

             // FIX: Update window title with debug info
             std::string title = std::string(WINDOW_TITLE); // + " [" + std::to_string(cols) + "x" + std::to_string(rows) + "]";
             SetWindowTextA(hwnd, title.c_str());
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            if (pt.y < TAB_HEIGHT) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            } else if (g_hoverUrlRow >= 0) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
            } else {
                SetCursor(LoadCursor(NULL, IDC_IBEAM));
            }
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wParam == SCROLL_TIMER_ID && g_activeSessionIndex >= 0) {
            Session* s = g_sessions[g_activeSessionIndex];
            std::lock_guard<std::mutex> lock(s->mutex);
            float diff = (float)g_scrollTarget - g_scrollCurrent;
            if (std::abs(diff) < 0.5f) {
                g_scrollCurrent = (float)g_scrollTarget;
                s->viewOffset = g_scrollTarget;
                g_scrollAnimating = false;
                KillTimer(hwnd, SCROLL_TIMER_ID);
            } else {
                g_scrollCurrent += diff * 0.3f;
                s->viewOffset = (int)(g_scrollCurrent + 0.5f);
            }
            if (s->viewOffset < 0) s->viewOffset = 0;
            if (s->viewOffset > (int)s->history.size()) s->viewOffset = (int)s->history.size();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        {
            int x = LOWORD(lParam); int y = HIWORD(lParam);
            
            // Mouse Tracking for TUI
            if (y >= TAB_HEIGHT && g_activeSessionIndex >= 0) {
                 Session* s = g_sessions[g_activeSessionIndex];
                 if (s->mouseMode) {
                     int row, col;
                     ScreenToCell(x, y, row, col);
                     int vtCol = col + 1;
                     int vtRow = row + 1;
                     
                     if (s->sgrMouseMode) {
                         std::string seq = "\033[<0;" + std::to_string(vtCol) + ";" + std::to_string(vtRow) + "M";
                         WriteFile(s->hPipeIn, seq.c_str(), seq.length(), NULL, NULL);
                     }
                     return 0; // Block selection
                 }
            }

            if (y < TAB_HEIGHT) {
                int tabWidth = 140;
                int clickedIndex = x / tabWidth;
                if (clickedIndex < (int)g_sessions.size()) {
                    int tabLeft = clickedIndex * tabWidth;
                    if (x > tabLeft + tabWidth - 25) {
                        CloseSession(clickedIndex);
                    } else {
                        SwitchTab(clickedIndex);
                        g_draggingTab = clickedIndex;
                        g_dragStartX = x;
                        SetCapture(hwnd);
                    }
                } else if (clickedIndex == (int)g_sessions.size()) {
                    // Check if clicked the + button (approx 0-30px relative to start)
                    int startX = g_sessions.size() * tabWidth;
                    int localX = x - startX;
                    if (localX < 30) {
                         CreateNewSession();
                    } else if (localX >= 31 && localX < 61) {
                        // Dropdown menu - Custom Window
                        if (g_hMenuWnd) { DestroyWindow(g_hMenuWnd); g_hMenuWnd = NULL; return 0; }
                        
                        POINT pt = {startX + 31, TAB_HEIGHT};
                        ClientToScreen(hwnd, &pt);

                        int w = 220;
                        int h = 2 + ((int)g_profiles.size() * 28) + 9 + 28 + 28;

                        g_hMenuWnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "LinuxifyMenuClass", "",
                                       WS_POPUP, pt.x, pt.y, w, h, hwnd, NULL, GetModuleHandle(NULL), NULL);
                                       
                        AnimateWindow(g_hMenuWnd, 120, AW_SLIDE | AW_VER_POSITIVE | AW_ACTIVATE);
                        SetForegroundWindow(g_hMenuWnd);
                        SetFocus(g_hMenuWnd);
                    }
                }
            } else {
                if ((GetKeyState(VK_CONTROL) & 0x8000) && g_hoverUrlRow >= 0 && !g_hoverUrlText.empty()) {
                    ShellExecuteA(NULL, "open", g_hoverUrlText.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    return 0;
                }
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
        {
            int x = LOWORD(lParam); int y = HIWORD(lParam);

            if (g_draggingTab >= 0 && y < TAB_HEIGHT) {
                int tabWidth = 140;
                int targetIndex = x / tabWidth;
                if (targetIndex >= 0 && targetIndex < (int)g_sessions.size() && targetIndex != g_draggingTab) {
                    std::swap(g_sessions[g_draggingTab], g_sessions[targetIndex]);
                    if (g_activeSessionIndex == g_draggingTab) g_activeSessionIndex = targetIndex;
                    else if (g_activeSessionIndex == targetIndex) g_activeSessionIndex = g_draggingTab;
                    g_draggingTab = targetIndex;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            if (y < TAB_HEIGHT) {
                int tabWidth = 140;
                int startX = (int)g_sessions.size() * tabWidth;
                int localX = x - startX;

                bool oldPlus = g_hoverPlus;
                bool oldDown = g_hoverDown;

                int clickedIndex = x / tabWidth;
                if (clickedIndex == (int)g_sessions.size()) {
                    g_hoverPlus = (localX >= 0 && localX < 30);
                    g_hoverDown = (localX >= 31 && localX < 61);
                } else {
                    g_hoverPlus = false;
                    g_hoverDown = false;
                }

                if (oldPlus != g_hoverPlus || oldDown != g_hoverDown) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    TRACKMOUSEEVENT tme;
                    tme.cbSize = sizeof(TRACKMOUSEEVENT);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                }
                g_hoverUrlRow = -1;
                g_hoverUrlText.clear();
            } else if (g_selecting) {
                ScreenToCell(x, y, g_selEndRow, g_selEndCol);
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                if (g_hoverPlus || g_hoverDown) {
                    g_hoverPlus = false; g_hoverDown = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                int oldHoverRow = g_hoverUrlRow;
                int oldHoverStart = g_hoverUrlStartCol;
                g_hoverUrlRow = -1;
                g_hoverUrlStartCol = -1;
                g_hoverUrlEndCol = -1;
                g_hoverUrlText.clear();
                if (g_activeSessionIndex >= 0 && y >= TAB_HEIGHT) {
                    Session* s = g_sessions[g_activeSessionIndex];
                    std::lock_guard<std::mutex> lock(s->mutex);
                    int row, col;
                    ScreenToCell(x, y, row, col);
                    int historySize = s->inAltBuffer ? 0 : (int)s->history.size();
                    int totalRows = historySize + (int)s->grid.size();
                    int startLine = totalRows - s->rows - s->viewOffset;
                    int lineIdx = startLine + row;
                    const std::vector<Cell>* rowPtr = nullptr;
                    if (lineIdx >= 0 && lineIdx < historySize) rowPtr = &s->history[lineIdx];
                    else if (lineIdx >= historySize && lineIdx < totalRows) rowPtr = &s->grid[lineIdx - historySize];
                    if (rowPtr) {
                        std::vector<UrlSpan> urls = FindUrlsInRow(*rowPtr);
                        for (const auto& u : urls) {
                            if (col >= u.startCol && col <= u.endCol) {
                                g_hoverUrlRow = row;
                                g_hoverUrlStartCol = u.startCol;
                                g_hoverUrlEndCol = u.endCol;
                                g_hoverUrlText = u.url;
                                break;
                            }
                        }
                    }
                }
                if (oldHoverRow != g_hoverUrlRow || oldHoverStart != g_hoverUrlStartCol) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        g_hoverPlus = false;
        g_hoverDown = false;
        g_hoverUrlRow = -1;
        g_hoverUrlText.clear();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (g_draggingTab >= 0) {
            g_draggingTab = -1;
            ReleaseCapture();
            return 0;
        }
        if (g_activeSessionIndex >= 0) {
             Session* s = g_sessions[g_activeSessionIndex];
             if (s->mouseMode) {
                 int x = LOWORD(lParam); int y = HIWORD(lParam);
                 int row, col; ScreenToCell(x, y, row, col);
                 if (s->sgrMouseMode) {
                     std::string seq = "\033[<0;" + std::to_string(col+1) + ";" + std::to_string(row+1) + "m";
                     WriteFile(s->hPipeIn, seq.c_str(), seq.length(), NULL, NULL);
                 }
             }
        }
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
            if (s->inAltBuffer) return 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int lines = delta / WHEEL_DELTA * 3;
            g_scrollTarget += lines;
            if (g_scrollTarget < 0) g_scrollTarget = 0;
            if (g_scrollTarget > (int)s->history.size()) g_scrollTarget = (int)s->history.size();
            if (!g_scrollAnimating) {
                g_scrollCurrent = (float)s->viewOffset;
                g_scrollAnimating = true;
                SetTimer(hwnd, SCROLL_TIMER_ID, 16, NULL);
            }
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
            if (c == '\b') return 0;
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

    case WM_USER + 100:
        ShowSettingsDialog();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (lpCmdLine && lpCmdLine[0]) {
        g_startDir = lpCmdLine;
        if (g_startDir.size() >= 2 && g_startDir.front() == '"' && g_startDir.back() == '"') {
            g_startDir = g_startDir.substr(1, g_startDir.size() - 2);
        }
        if (!g_startDir.empty() && !fs::is_directory(g_startDir)) {
            g_startDir.clear();
        }
    }

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconA(hInstance, "id");
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    RegisterClassExA(&wc);
    
    RegisterMenuClass();
    RegisterSettingsClass();

    HWND hwnd = CreateWindowExA(WS_EX_LAYERED, CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, NULL, NULL, hInstance, NULL);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)g_settings.opacity, LWA_ALPHA);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}