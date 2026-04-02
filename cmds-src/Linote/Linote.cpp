/*
 * Compilation code (MinGW/GCC):
 * g++ Linote.cpp -o linote.exe -mwindows -lcomdlg32 -lgdi32 -ldwmapi -luxtheme -static
 *
 * Compilation code (MSVC/cl.exe from Developer Command Prompt):
 * cl Linote.cpp user32.lib gdi32.lib comdlg32.lib dwmapi.lib uxtheme.lib
 */

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// --- Menu IDs ---
#define IDM_FILE_NEW      1001
#define IDM_FILE_OPEN     1002
#define IDM_FILE_SAVE     1003
#define IDM_FILE_SAVEAS   1004
#define IDM_FILE_EXIT     1005
#define IDM_EDIT_UNDO     2001
#define IDM_EDIT_CUT      2002
#define IDM_EDIT_COPY     2003
#define IDM_EDIT_PASTE    2004
#define IDM_EDIT_DELETE   2005
#define IDM_EDIT_SELALL   2006
#define IDM_EDIT_REDO     2007
#define IDM_VIEW_LIGHT    4001
#define IDM_VIEW_DARK     4002
#define IDM_HELP_ABOUT    3001

// --- Globals ---
HINSTANCE hInst;
HWND hMainWnd, hEditWnd, hMarginWnd;
HFONT hFont;
WNDPROC wpOrigEditProc;

char szFilePath[MAX_PATH] = "";
char szFileName[MAX_PATH] = "Untitled";

const int MARGIN_WIDTH = 55;

// Theme Globals
bool bDarkMode = true;
COLORREF colBg, colText, colMarginBg, colMarginText, colMarginBorder;
HBRUSH hbrEditBg = NULL;

// Sophisticated Undo/Redo Engine
struct EditorState {
    std::string text;
    DWORD selStart;
    DWORD selEnd;
};
std::vector<EditorState> undoStack;
std::vector<EditorState> redoStack;
EditorState currentState;
bool bIsProgrammaticChange = false;

// --- Function Prototypes ---
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MarginProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM);
void CreateMainMenu(HWND hwnd);
void UpdateTitle(HWND hwnd);
bool SaveFile(HWND hwnd, bool saveAs);
bool OpenFileDlg(HWND hwnd);
bool CheckSave(HWND hwnd);
void ApplyTheme(HWND hwnd);

// Undo/Redo Prototypes
EditorState GetCurrentEditorState();
void SnapshotCurrentState();
void RestoreState(const EditorState& st);
void DoUndo();
void DoRedo();
void ResetUndoRedo();
void UpdateCurrentStateCursor();

// --- Entry Point ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;
    
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "Win32NotepadClass";
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExA(&wc)) return 0;

    WNDCLASSEXA wcm = { sizeof(WNDCLASSEXA) };
    wcm.style         = CS_HREDRAW | CS_VREDRAW;
    wcm.lpfnWndProc   = MarginProc;
    wcm.hInstance     = hInstance;
    wcm.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcm.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wcm.lpszClassName = "MarginClass";
    if (!RegisterClassExA(&wcm)) return 0;

    hMainWnd = CreateWindowExA(0, "Win32NotepadClass", "Untitled - Linote",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        NULL, NULL, hInstance, NULL);

    if (!hMainWnd) return 0;

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

// --- Undo/Redo Engine Implementation ---
EditorState GetCurrentEditorState() {
    EditorState st;
    int len = GetWindowTextLengthA(hEditWnd);
    st.text.resize(len);
    GetWindowTextA(hEditWnd, &st.text[0], len + 1);
    st.text.resize(len); // remove null terminator from string object
    SendMessage(hEditWnd, EM_GETSEL, (WPARAM)&st.selStart, (LPARAM)&st.selEnd);
    return st;
}

void SnapshotCurrentState() {
    EditorState st = GetCurrentEditorState();
    if (st.text != currentState.text) {
        undoStack.push_back(currentState);
        currentState = st;
        redoStack.clear();
    } else {
        currentState.selStart = st.selStart;
        currentState.selEnd = st.selEnd;
    }
}

void RestoreState(const EditorState& st) {
    bIsProgrammaticChange = true;
    SetWindowTextA(hEditWnd, st.text.c_str());
    SendMessage(hEditWnd, EM_SETSEL, st.selStart, st.selEnd);
    SendMessage(hEditWnd, EM_SCROLLCARET, 0, 0);
    bIsProgrammaticChange = false;
}

void DoUndo() {
    EditorState currentEdit = GetCurrentEditorState();
    if (currentEdit.text != currentState.text) {
        undoStack.push_back(currentState);
        currentState = currentEdit;
    }

    if (undoStack.empty()) return;

    redoStack.push_back(currentState);
    currentState = undoStack.back();
    undoStack.pop_back();

    RestoreState(currentState);
}

void DoRedo() {
    if (redoStack.empty()) return;

    undoStack.push_back(currentState);
    currentState = redoStack.back();
    redoStack.pop_back();

    RestoreState(currentState);
}

void ResetUndoRedo() {
    undoStack.clear();
    redoStack.clear();
    currentState = GetCurrentEditorState();
}

void UpdateCurrentStateCursor() {
    SendMessage(hEditWnd, EM_GETSEL, (WPARAM)&currentState.selStart, (LPARAM)&currentState.selEnd);
}

// --- Theme Management ---
void UpdateColors() {
    if (hbrEditBg) DeleteObject(hbrEditBg);
    
    if (bDarkMode) {
        colBg           = RGB(39, 41, 44);
        colText         = RGB(220, 220, 220);
        colMarginBg     = RGB(30, 31, 34);
        colMarginText   = RGB(120, 130, 140);
        colMarginBorder = RGB(60, 62, 66);
    } else {
        colBg           = RGB(255, 255, 255);
        colText         = RGB(0, 0, 0);
        colMarginBg     = RGB(245, 245, 245);
        colMarginText   = RGB(130, 130, 130);
        colMarginBorder = RGB(210, 210, 210);
    }
    hbrEditBg = CreateSolidBrush(colBg);
}

void ApplyTheme(HWND hwnd) {
    UpdateColors();
    
    // 1. Dark Title Bar
    BOOL useDark = bDarkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));

    // 2. Dark Scrollbars (Requires uxtheme.h)
    SetWindowTheme(hEditWnd, bDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);

    // 3. Dark Dropdown Menus (Undocumented UXTheme APIs)
    HMODULE hUxtheme = LoadLibraryA("uxtheme.dll");
    if (hUxtheme) {
        // SetPreferredAppMode
        using fnSetPreferredAppMode = int (WINAPI*)(int);
        fnSetPreferredAppMode setAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (setAppMode) setAppMode(bDarkMode ? 2 : 1); // 2 = ForceDark, 1 = AllowDark

        // AllowDarkModeForWindow
        using fnAllowDarkModeForWindow = bool (WINAPI*)(HWND, bool);
        fnAllowDarkModeForWindow allowDark = (fnAllowDarkModeForWindow)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
        if (allowDark) allowDark(hwnd, bDarkMode);

        // FlushMenuThemes to apply immediately
        using fnFlushMenuThemes = void (WINAPI*)();
        fnFlushMenuThemes flush = (fnFlushMenuThemes)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));
        if (flush) flush();

        FreeLibrary(hUxtheme);
    }
    DrawMenuBar(hwnd); // Force standard menu bar redraw

    // Update Menu Checks
    HMENU hMenu = GetMenu(hwnd);
    CheckMenuItem(hMenu, IDM_VIEW_LIGHT, MF_BYCOMMAND | (bDarkMode ? MF_UNCHECKED : MF_CHECKED));
    CheckMenuItem(hMenu, IDM_VIEW_DARK,  MF_BYCOMMAND | (bDarkMode ? MF_CHECKED : MF_UNCHECKED));

    // Force Repaint
    if (hEditWnd) {
        InvalidateRect(hEditWnd, NULL, TRUE);
        UpdateWindow(hEditWnd);
    }
    if (hMarginWnd) {
        InvalidateRect(hMarginWnd, NULL, TRUE);
        UpdateWindow(hMarginWnd);
    }
}

// --- Main Window Procedure ---
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateMainMenu(hwnd);

            hMarginWnd = CreateWindowExA(0, "MarginClass", NULL,
                WS_CHILD | WS_VISIBLE, 0, 0, MARGIN_WIDTH, 0,
                hwnd, (HMENU)2, hInst, NULL);

            hEditWnd = CreateWindowExA(0, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | 
                ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL,
                MARGIN_WIDTH, 0, 0, 0,
                hwnd, (HMENU)1, hInst, NULL);

            hFont = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            
            SendMessage(hEditWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
            wpOrigEditProc = (WNDPROC)SetWindowLongPtr(hEditWnd, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
            
            ApplyTheme(hwnd);
            ResetUndoRedo();
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, colBg);
            SetTextColor(hdc, colText);
            return (LRESULT)hbrEditBg;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            
            MoveWindow(hMarginWnd, 0, 0, MARGIN_WIDTH, height, TRUE);
            MoveWindow(hEditWnd, MARGIN_WIDTH, 0, width - MARGIN_WIDTH, height, TRUE);

            RECT rcEdit;
            rcEdit.left = 10;
            rcEdit.top = 10;
            rcEdit.right = (width - MARGIN_WIDTH) - 10;
            rcEdit.bottom = height - 10;
            SendMessage(hEditWnd, EM_SETRECT, 0, (LPARAM)&rcEdit);
            break;
        }

        case WM_TIMER: {
            if (wParam == 2) {
                KillTimer(hwnd, 2);
                SnapshotCurrentState(); // Commit text history after user pauses typing
            }
            break;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == 1) { 
                if (HIWORD(wParam) == EN_CHANGE) {
                    if (!bIsProgrammaticChange) {
                        SetTimer(hwnd, 2, 400, NULL); // Debounce typing history
                    }
                    InvalidateRect(hMarginWnd, NULL, TRUE);
                } else if (HIWORD(wParam) == EN_UPDATE || HIWORD(wParam) == EN_VSCROLL) {
                    InvalidateRect(hMarginWnd, NULL, TRUE);
                }
                return 0;
            }

            switch (LOWORD(wParam)) {
                case IDM_FILE_NEW:
                    if (CheckSave(hwnd)) {
                        SetWindowTextA(hEditWnd, "");
                        szFilePath[0] = '\0';
                        strcpy(szFileName, "Untitled");
                        SendMessage(hEditWnd, EM_SETMODIFY, FALSE, 0);
                        UpdateTitle(hwnd);
                        InvalidateRect(hMarginWnd, NULL, TRUE);
                        ResetUndoRedo();
                    }
                    break;
                case IDM_FILE_OPEN: if (CheckSave(hwnd)) OpenFileDlg(hwnd); break;
                case IDM_FILE_SAVE: SaveFile(hwnd, false); break;
                case IDM_FILE_SAVEAS: SaveFile(hwnd, true); break;
                case IDM_FILE_EXIT: PostMessage(hwnd, WM_CLOSE, 0, 0); break;

                case IDM_EDIT_UNDO:   DoUndo(); break;
                case IDM_EDIT_REDO:   DoRedo(); break;
                case IDM_EDIT_CUT:    SendMessage(hEditWnd, WM_CUT, 0, 0); break;
                case IDM_EDIT_COPY:   SendMessage(hEditWnd, WM_COPY, 0, 0); break;
                case IDM_EDIT_PASTE:  SendMessage(hEditWnd, WM_PASTE, 0, 0); break;
                case IDM_EDIT_DELETE: SendMessage(hEditWnd, WM_CLEAR, 0, 0); break;
                case IDM_EDIT_SELALL: SendMessage(hEditWnd, EM_SETSEL, 0, -1); break;

                case IDM_VIEW_LIGHT:
                    if (bDarkMode) { bDarkMode = false; ApplyTheme(hwnd); }
                    break;
                case IDM_VIEW_DARK:
                    if (!bDarkMode) { bDarkMode = true; ApplyTheme(hwnd); }
                    break;

                case IDM_HELP_ABOUT:
                    MessageBoxA(hwnd, "Modern Win32 C++ Notepad\nWith Multi-Level Undo & True Dark Themes", "About", MB_OK | MB_ICONINFORMATION);
                    break;
            }
            break;
        }

        case WM_CLOSE:
            if (CheckSave(hwnd)) DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            DeleteObject(hFont);
            if (hbrEditBg) DeleteObject(hbrEditBg);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Margin Window Procedure (Draws Line Numbers) ---
LRESULT CALLBACK MarginProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            HBRUSH hbg = CreateSolidBrush(colMarginBg);
            FillRect(hdc, &rcClient, hbg);
            DeleteObject(hbg);

            HPEN hPen = CreatePen(PS_SOLID, 1, colMarginBorder);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            MoveToEx(hdc, rcClient.right - 1, 0, NULL);
            LineTo(hdc, rcClient.right - 1, rcClient.bottom);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            if (!hEditWnd) {
                EndPaint(hwnd, &ps);
                return 0;
            }

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, colMarginText);
            SelectObject(hdc, hFont);

            int firstLine = (int)SendMessage(hEditWnd, EM_GETFIRSTVISIBLELINE, 0, 0);
            int lineCount = (int)SendMessage(hEditWnd, EM_GETLINECOUNT, 0, 0);

            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            int lineHeight = tm.tmHeight;

            RECT editRect;
            SendMessage(hEditWnd, EM_GETRECT, 0, (LPARAM)&editRect);
            int y = editRect.top;

            char numBuf[32];
            for (int i = firstLine; i < lineCount && y < rcClient.bottom; ++i) {
                sprintf(numBuf, "%d", i + 1);
                RECT textRect = { 0, y, rcClient.right - 10, y + lineHeight }; 
                DrawTextA(hdc, numBuf, -1, &textRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);
                y += lineHeight;
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- Edit Control Subclass ---
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        // Intercept native shortcuts to use our custom memory buffers
        if (ctrlDown && wParam == 'Z') {
            PostMessage(hMainWnd, WM_COMMAND, IDM_EDIT_UNDO, 0);
            return 0;
        }
        if (ctrlDown && wParam == 'Y') {
            PostMessage(hMainWnd, WM_COMMAND, IDM_EDIT_REDO, 0);
            return 0;
        }
        // Save state immediately before destructive keystrokes
        if (wParam == VK_RETURN || wParam == VK_SPACE || wParam == VK_BACK || wParam == VK_DELETE) {
            SnapshotCurrentState();
        }
    } else if (msg == WM_CUT || msg == WM_PASTE || msg == WM_CLEAR) {
        SnapshotCurrentState();
    }

    LRESULT res = CallWindowProc(wpOrigEditProc, hwnd, msg, wParam, lParam);
    
    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL || msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
        if (msg == WM_KEYUP || msg == WM_LBUTTONUP) UpdateCurrentStateCursor(); // Sync cursor
        InvalidateRect(hMarginWnd, NULL, TRUE);
        UpdateWindow(hMarginWnd);
    }
    return res;
}

// --- Helper Functions ---
void CreateMainMenu(HWND hwnd) {
    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenu(hFile, MF_STRING, IDM_FILE_NEW, "&New\tCtrl+N");
    AppendMenu(hFile, MF_STRING, IDM_FILE_OPEN, "&Open...\tCtrl+O");
    AppendMenu(hFile, MF_STRING, IDM_FILE_SAVE, "&Save\tCtrl+S");
    AppendMenu(hFile, MF_STRING, IDM_FILE_SAVEAS, "Save &As...");
    AppendMenu(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFile, MF_STRING, IDM_FILE_EXIT, "E&xit");

    HMENU hEdit = CreatePopupMenu();
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_UNDO, "&Undo\tCtrl+Z");
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_REDO, "&Redo\tCtrl+Y");
    AppendMenu(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_CUT, "Cu&t\tCtrl+X");
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_COPY, "&Copy\tCtrl+C");
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_PASTE, "&Paste\tCtrl+V");
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_DELETE, "De&lete\tDel");
    AppendMenu(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenu(hEdit, MF_STRING, IDM_EDIT_SELALL, "Select &All\tCtrl+A");

    HMENU hView = CreatePopupMenu();
    AppendMenu(hView, MF_STRING, IDM_VIEW_LIGHT, "&Light Theme");
    AppendMenu(hView, MF_STRING, IDM_VIEW_DARK, "&Dark Theme");

    HMENU hHelp = CreatePopupMenu();
    AppendMenu(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About");

    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hEdit, "&Edit");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hView, "&View");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");

    SetMenu(hwnd, hMenu);
}

void UpdateTitle(HWND hwnd) {
    char title[MAX_PATH + 32];
    sprintf(title, "%s - Linote", szFileName);
    SetWindowTextA(hwnd, title);
}

bool CheckSave(HWND hwnd) {
    if (SendMessage(hEditWnd, EM_GETMODIFY, 0, 0)) {
        int res = MessageBoxA(hwnd, "Do you want to save changes?", "Linote", MB_YESNOCANCEL | MB_ICONWARNING);
        if (res == IDYES) return SaveFile(hwnd, false);
        if (res == IDCANCEL) return false;
    }
    return true;
}

bool OpenFileDlg(HWND hwnd) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        HANDLE hFile = CreateFileA(ofn.lpstrFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD fileSize = GetFileSize(hFile, NULL);
            char* buffer = new char[fileSize + 1];
            DWORD bytesRead;
            if (ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                SetWindowTextA(hEditWnd, buffer);
                
                strcpy(szFilePath, ofn.lpstrFile);
                char* p = strrchr(szFilePath, '\\');
                strcpy(szFileName, p ? p + 1 : szFilePath);
                
                UpdateTitle(hwnd);
                SendMessage(hEditWnd, EM_SETMODIFY, FALSE, 0);
                InvalidateRect(hMarginWnd, NULL, TRUE);
                ResetUndoRedo();
            }
            delete[] buffer;
            CloseHandle(hFile);
            return true;
        }
    }
    return false;
}

bool SaveFile(HWND hwnd, bool saveAs) {
    if (saveAs || strlen(szFilePath) == 0) {
        OPENFILENAMEA ofn;
        char szFile[MAX_PATH] = "";
        if (strlen(szFilePath) > 0) strcpy(szFile, szFilePath);

        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = "txt";

        if (!GetSaveFileNameA(&ofn)) return false;
        strcpy(szFilePath, ofn.lpstrFile);
        
        char* p = strrchr(szFilePath, '\\');
        strcpy(szFileName, p ? p + 1 : szFilePath);
    }

    HANDLE hFile = CreateFileA(szFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        int length = GetWindowTextLengthA(hEditWnd);
        char* buffer = new char[length + 1];
        GetWindowTextA(hEditWnd, buffer, length + 1);

        DWORD bytesWritten;
        WriteFile(hFile, buffer, length, &bytesWritten, NULL);

        delete[] buffer;
        CloseHandle(hFile);

        SendMessage(hEditWnd, EM_SETMODIFY, FALSE, 0);
        UpdateTitle(hwnd);
        return true;
    }
    return false;
}