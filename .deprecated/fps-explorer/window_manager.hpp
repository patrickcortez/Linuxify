#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <shobjidl.h>
#include <shlguid.h>
#include <objbase.h>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *FN_CreatePC)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void (WINAPI *FN_ClosePC)(void*);

static bool isGuiExe(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos;
    DWORD bytesRead;
    bool gui = false;
    if (ReadFile(hFile, &dos, sizeof(dos), &bytesRead, NULL) && dos.e_magic == IMAGE_DOS_SIGNATURE) {
        SetFilePointer(hFile, dos.e_lfanew, NULL, FILE_BEGIN);
        IMAGE_NT_HEADERS nt;
        if (ReadFile(hFile, &nt, sizeof(nt), &bytesRead, NULL) && nt.Signature == IMAGE_NT_SIGNATURE) {
            gui = (nt.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI);
        }
    }
    CloseHandle(hFile);
    return gui;
}

struct GameWindow {
    static const int TCOLS = 80;
    static const int TROWS = 24;

    int winX = 100, winY = 50;
    int winW = 660, winH = 460;
    std::wstring title;
    bool closed = false;
    bool active = false;

    wchar_t grid[TROWS][TCOLS];
    int curX = 0, curY = 0;
    bool gotESC = false, inCSI = false, inOSC = false;
    int csiParams[16];
    int csiParamCount = 0;
    bool csiPrivate = false;
    bool cursorVisible = true;

    HANDLE hPipeIn = NULL, hPipeOut = NULL;
    HANDLE hProcess = NULL;
    void* hPC = NULL;
    bool processRunning = false;
    bool isConsolePTY = false;

    HWND capturedHwnd = NULL;
    bool isGuiCapture = false;
    DWORD childPid = 0;

    int dragOffX = 0, dragOffY = 0;
    bool dragging = false;

    void clearGrid() {
        for (int r = 0; r < TROWS; r++)
            for (int c = 0; c < TCOLS; c++)
                grid[r][c] = L' ';
        curX = 0; curY = 0;
    }

    void launchConPTY(const std::wstring& exe, const std::wstring& arg,
                      FN_CreatePC fnCreate, FN_ClosePC fnClose) {
        clearGrid();
        gotESC = false; inCSI = false; inOSC = false;
        isConsolePTY = true;
        if (!fnCreate || !fnClose) return;

        HANDLE hPipePTYIn = NULL, hPipePTYOut = NULL;
        CreatePipe(&hPipePTYIn, &hPipeOut, NULL, 0);
        CreatePipe(&hPipeIn, &hPipePTYOut, NULL, 0);

        COORD size = {TCOLS, TROWS};
        HRESULT hr = fnCreate(size, hPipePTYIn, hPipePTYOut, 0, &hPC);
        CloseHandle(hPipePTYIn);
        CloseHandle(hPipePTYOut);

        if (FAILED(hr)) {
            if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
            if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
            return;
        }

        STARTUPINFOEXW siEx = {};
        siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
        siEx.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
        InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &attrSize);
        UpdateProcThreadAttribute(siEx.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(void*), NULL, NULL);

        PROCESS_INFORMATION pi = {};
        std::wstring cmd = L"\"" + exe + L"\"";
        if (!arg.empty()) cmd += L" " + arg;
        std::wstring parentDir;
        size_t lastSlash = exe.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) parentDir = exe.substr(0, lastSlash);
        BOOL ok = CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE,
            EXTENDED_STARTUPINFO_PRESENT, NULL,
            parentDir.empty() ? NULL : parentDir.c_str(),
            &siEx.StartupInfo, &pi);

        DeleteProcThreadAttributeList(siEx.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);

        if (ok) {
            hProcess = pi.hProcess;
            childPid = pi.dwProcessId;
            if (pi.hThread) CloseHandle(pi.hThread);
            processRunning = true;
        } else {
            if (hPC) { fnClose(hPC); hPC = NULL; }
            if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
            if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
        }
    }

    void launchGui(const std::wstring& exe, const std::wstring& arg) {
        isGuiCapture = true;
        isConsolePTY = false;

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::wstring cmd = L"\"" + exe + L"\"";
        if (!arg.empty()) cmd += L" " + arg;
        std::wstring parentDir;
        size_t lastSlash = exe.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) parentDir = exe.substr(0, lastSlash);
        BOOL ok = CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE,
            0, NULL, parentDir.empty() ? NULL : parentDir.c_str(), &si, &pi);
        if (ok) {
            hProcess = pi.hProcess;
            childPid = pi.dwProcessId;
            if (pi.hThread) CloseHandle(pi.hThread);
            processRunning = true;
        }
    }

    struct EnumData { DWORD pid; HWND result; };

    static BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
        EnumData* e = (EnumData*)lp;
        DWORD wPid;
        GetWindowThreadProcessId(hwnd, &wPid);
        if (wPid == e->pid && IsWindowVisible(hwnd)) {
            wchar_t cls[64];
            GetClassNameW(hwnd, cls, 64);
            if (wcscmp(cls, L"ConsoleWindowClass") != 0) {
                e->result = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }

    void findChildWindow() {
        if (capturedHwnd || !processRunning || childPid == 0) return;
        EnumData ed = {childPid, NULL};
        EnumWindows(enumProc, (LPARAM)&ed);
        if (ed.result) {
            capturedHwnd = ed.result;
            SetWindowPos(capturedHwnd, HWND_BOTTOM, -32000, -32000, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    void scrollUp() {
        for (int r = 0; r < TROWS - 1; r++)
            memcpy(grid[r], grid[r + 1], TCOLS * sizeof(wchar_t));
        for (int c = 0; c < TCOLS; c++) grid[TROWS - 1][c] = L' ';
    }

    void putChar(wchar_t ch) {
        if (curX >= TCOLS) { curX = 0; curY++; }
        if (curY >= TROWS) { scrollUp(); curY = TROWS - 1; }
        grid[curY][curX] = ch;
        curX++;
    }

    int csiParam(int idx, int def) {
        return (idx < csiParamCount && csiParams[idx] > 0) ? csiParams[idx] : def;
    }

    void executeCSI(char cmd) {
        int n;
        switch (cmd) {
        case 'A': curY -= csiParam(0, 1); if (curY < 0) curY = 0; break;
        case 'B': curY += csiParam(0, 1); if (curY >= TROWS) curY = TROWS - 1; break;
        case 'C': curX += csiParam(0, 1); if (curX >= TCOLS) curX = TCOLS - 1; break;
        case 'D': curX -= csiParam(0, 1); if (curX < 0) curX = 0; break;
        case 'H': case 'f':
            curY = csiParam(0, 1) - 1; curX = csiParam(1, 1) - 1;
            if (curY < 0) curY = 0; if (curY >= TROWS) curY = TROWS - 1;
            if (curX < 0) curX = 0; if (curX >= TCOLS) curX = TCOLS - 1;
            break;
        case 'J':
            n = csiParam(0, 0);
            if (n == 0) {
                for (int c = curX; c < TCOLS; c++) grid[curY][c] = L' ';
                for (int r = curY + 1; r < TROWS; r++) for (int c = 0; c < TCOLS; c++) grid[r][c] = L' ';
            } else if (n == 1) {
                for (int r = 0; r < curY; r++) for (int c = 0; c < TCOLS; c++) grid[r][c] = L' ';
                for (int c = 0; c <= curX && c < TCOLS; c++) grid[curY][c] = L' ';
            } else if (n >= 2) {
                for (int r = 0; r < TROWS; r++) for (int c = 0; c < TCOLS; c++) grid[r][c] = L' ';
                curX = 0; curY = 0;
            }
            break;
        case 'K':
            n = csiParam(0, 0);
            if (n == 0) { for (int c = curX; c < TCOLS; c++) grid[curY][c] = L' '; }
            else if (n == 1) { for (int c = 0; c <= curX && c < TCOLS; c++) grid[curY][c] = L' '; }
            else if (n == 2) { for (int c = 0; c < TCOLS; c++) grid[curY][c] = L' '; }
            break;
        case 'G': curX = csiParam(0, 1) - 1; if (curX < 0) curX = 0; if (curX >= TCOLS) curX = TCOLS - 1; break;
        case 'd': curY = csiParam(0, 1) - 1; if (curY < 0) curY = 0; if (curY >= TROWS) curY = TROWS - 1; break;
        case 'E': curX = 0; curY += csiParam(0, 1); if (curY >= TROWS) curY = TROWS - 1; break;
        case 'F': curX = 0; curY -= csiParam(0, 1); if (curY < 0) curY = 0; break;
        case 'S': for (int s = 0; s < csiParam(0, 1); s++) scrollUp(); break;
        }
    }

    void processByte(unsigned char ch) {
        if (inOSC) {
            if (ch == 0x07) inOSC = false;
            else if (ch == '\\' && gotESC) { inOSC = false; gotESC = false; }
            else gotESC = (ch == '\033');
            return;
        }
        if (inCSI) {
            if (ch >= '0' && ch <= '9') {
                if (csiParamCount == 0) csiParamCount = 1;
                csiParams[csiParamCount - 1] = csiParams[csiParamCount - 1] * 10 + (ch - '0');
            } else if (ch == ';') {
                if (csiParamCount < 16) csiParamCount++;
                csiParams[csiParamCount - 1] = 0;
            } else if (ch == '?') {
                csiPrivate = true;
            } else if (ch == '>' || ch == '!') {
            } else if (ch >= '@' && ch <= '~') {
                if (csiPrivate) {
                    if (ch == 'h' && csiParam(0, 0) == 25) cursorVisible = true;
                    if (ch == 'l' && csiParam(0, 0) == 25) cursorVisible = false;
                } else {
                    executeCSI((char)ch);
                }
                inCSI = false;
                csiPrivate = false;
            }
            return;
        }
        if (gotESC) {
            gotESC = false;
            if (ch == '[') { inCSI = true; csiParamCount = 0; csiPrivate = false; memset(csiParams, 0, sizeof(csiParams)); }
            else if (ch == ']') { inOSC = true; }
            return;
        }
        if (ch == '\033') { gotESC = true; return; }
        if (ch == '\r') { curX = 0; return; }
        if (ch == '\n') { curY++; if (curY >= TROWS) { scrollUp(); curY = TROWS - 1; } return; }
        if (ch == '\b') { if (curX > 0) curX--; return; }
        if (ch == '\t') { curX = (curX + 8) & ~7; if (curX >= TCOLS) curX = TCOLS - 1; return; }
        if (ch == 0x07 || ch < 32) return;
        putChar((wchar_t)ch);
    }

    void update(FN_ClosePC fnClose) {
        if (!processRunning) return;

        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            if (isConsolePTY) {
                if (hPC && fnClose) { fnClose(hPC); hPC = NULL; }
                if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
                if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
            }
            if (hProcess) { CloseHandle(hProcess); hProcess = NULL; }
            processRunning = false;
            capturedHwnd = NULL;
            return;
        }

        if (isConsolePTY && hPipeIn) {
            DWORD avail = 0;
            while (PeekNamedPipe(hPipeIn, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                char buf[4096];
                DWORD bytesRead = 0;
                DWORD toRead = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
                if (ReadFile(hPipeIn, buf, toRead, &bytesRead, NULL) && bytesRead > 0) {
                    for (DWORD b = 0; b < bytesRead; b++) processByte((unsigned char)buf[b]);
                } else break;
            }
        }

        if (isGuiCapture && !capturedHwnd) {
            findChildWindow();
        }
    }

    void sendChar(char c) {
        if (isConsolePTY && hPipeOut && processRunning) { DWORD w; WriteFile(hPipeOut, &c, 1, &w, NULL); }
        if (isGuiCapture && capturedHwnd) {
            PostMessageW(capturedHwnd, WM_CHAR, (WPARAM)(unsigned char)c, 0);
        }
    }

    void sendSeq(const char* seq, int n) {
        if (isConsolePTY && hPipeOut && processRunning) { DWORD w; WriteFile(hPipeOut, seq, n, &w, NULL); }
    }

    void sendKeyToGui(int vk, bool down) {
        if (!isGuiCapture || !capturedHwnd) return;
        PostMessageW(capturedHwnd, down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)vk,
            (LPARAM)(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16));
    }

    void sendMouseToGui(int mx, int my, UINT msg) {
        if (!isGuiCapture || !capturedHwnd) return;
        int contentX = winX + 4;
        int contentY = winY + TITLE_H + 2;
        int bodyW = winW - 8;
        int bodyH = winH - TITLE_H - 4;

        RECT wr;
        GetClientRect(capturedHwnd, &wr);
        int srcW = wr.right;
        int srcH = wr.bottom;
        if (srcW <= 0 || srcH <= 0) return;

        int relX = (int)((float)(mx - contentX) / bodyW * srcW);
        int relY = (int)((float)(my - contentY) / bodyH * srcH);
        if (relX < 0) relX = 0; if (relX >= srcW) relX = srcW - 1;
        if (relY < 0) relY = 0; if (relY >= srcH) relY = srcH - 1;
        PostMessageW(capturedHwnd, msg, (msg == WM_LBUTTONDOWN) ? MK_LBUTTON : 0, MAKELPARAM(relX, relY));
    }

    void destroy(FN_ClosePC fnClose) {
        if (capturedHwnd) { ShowWindow(capturedHwnd, SW_SHOW); capturedHwnd = NULL; }
        if (isConsolePTY) {
            if (hPC && fnClose) { fnClose(hPC); hPC = NULL; }
            if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
            if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
        }
        if (hProcess) { TerminateProcess(hProcess, 0); CloseHandle(hProcess); hProcess = NULL; }
        processRunning = false;
    }

    static const int TITLE_H = 24;

    bool hitTitleBar(int mx, int my) {
        return mx >= winX && mx < winX + winW && my >= winY && my < winY + TITLE_H;
    }

    bool hitClose(int mx, int my) {
        int bx = winX + winW - TITLE_H;
        return mx >= bx && mx < winX + winW && my >= winY && my < winY + TITLE_H;
    }

    bool hitBody(int mx, int my) {
        return mx >= winX && mx < winX + winW && my >= winY && my < winY + winH;
    }

    void renderConPTY(HDC hdc, HFONT font) {
        SetTextColor(hdc, RGB(180, 255, 180));
        int lineH = 16;
        int contentX = winX + 4;
        int contentY = winY + TITLE_H + 2;
        int contentW = winW - 8;
        int maxCols = contentW / 8;
        if (maxCols > TCOLS) maxCols = TCOLS;

        SIZE charSz;
        GetTextExtentPoint32W(hdc, L"A", 1, &charSz);
        int charW = charSz.cx;

        for (int r = 0; r < TROWS; r++) {
            int y = contentY + r * lineH;
            if (y + lineH > winY + winH) break;
            std::wstring line(grid[r], maxCols);
            size_t end = line.find_last_not_of(L' ');
            if (end != std::wstring::npos) {
                line.resize(end + 1);
                TextOutW(hdc, contentX, y, line.c_str(), (int)line.length());
            }
        }

        if (active && processRunning && cursorVisible && (GetTickCount() / 500) % 2 == 0) {
            int cxPx = contentX + curX * charW;
            int cyPx = contentY + curY * lineH;
            if (cyPx + lineH <= winY + winH) {
                HPEN curPen = CreatePen(PS_SOLID, 2, RGB(100, 255, 100));
                HPEN prevPen = (HPEN)SelectObject(hdc, curPen);
                MoveToEx(hdc, cxPx, cyPx, NULL);
                LineTo(hdc, cxPx, cyPx + lineH);
                SelectObject(hdc, prevPen);
                DeleteObject(curPen);
            }
        }
    }

    void renderGuiCapture(HDC hdc) {
        if (!capturedHwnd || !IsWindow(capturedHwnd)) return;

        int contentX = winX + 4;
        int contentY = winY + TITLE_H + 2;
        int bodyW = winW - 8;
        int bodyH = winH - TITLE_H - 4;

        SetWindowPos(capturedHwnd, HWND_BOTTOM, -32000, -32000, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

        RECT wr;
        GetClientRect(capturedHwnd, &wr);
        int srcW = wr.right; int srcH = wr.bottom;
        if (srcW <= 0 || srcH <= 0) return;

        HDC srcDC = CreateCompatibleDC(hdc);
        HBITMAP srcBmp = CreateCompatibleBitmap(hdc, srcW, srcH);
        HBITMAP oldBmp = (HBITMAP)SelectObject(srcDC, srcBmp);
        PrintWindow(capturedHwnd, srcDC, PW_CLIENTONLY);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, contentX, contentY, bodyW, bodyH,
                   srcDC, 0, 0, srcW, srcH, SRCCOPY);
        SelectObject(srcDC, oldBmp);
        DeleteObject(srcBmp);
        DeleteDC(srcDC);
    }

    void render(HDC hdc, HFONT font) {
        HBRUSH titleBr = CreateSolidBrush(active ? RGB(40, 120, 40) : RGB(30, 60, 30));
        RECT titleRect = {winX, winY, winX + winW, winY + TITLE_H};
        FillRect(hdc, &titleRect, titleBr);
        DeleteObject(titleBr);

        HBRUSH bodyBr = CreateSolidBrush(RGB(10, 10, 15));
        RECT bodyRect = {winX, winY + TITLE_H, winX + winW, winY + winH};
        FillRect(hdc, &bodyRect, bodyBr);
        DeleteObject(bodyBr);

        HPEN borderPen = CreatePen(PS_SOLID, 1, active ? RGB(80, 200, 80) : RGB(50, 100, 50));
        HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH hollow = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, hollow);
        Rectangle(hdc, winX, winY, winX + winW, winY + winH);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);

        HFONT oldFont = (HFONT)SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 255, 220));
        TextOutW(hdc, winX + 6, winY + 4, title.c_str(), (int)title.length());

        SetTextColor(hdc, RGB(255, 80, 80));
        TextOutW(hdc, winX + winW - TITLE_H + 8, winY + 4, L"X", 1);

        if (isGuiCapture) {
            renderGuiCapture(hdc);
        } else {
            renderConPTY(hdc, font);
        }

        SelectObject(hdc, oldFont);
    }
};

struct WindowManager {
    std::vector<GameWindow*> windows;
    int mouseX = 640, mouseY = 360;
    bool desktopMode = false;
    HFONT wmFont = NULL;

    FN_CreatePC pfnCreatePC = nullptr;
    FN_ClosePC pfnClosePC = nullptr;

    void init() {
        wmFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32) {
            pfnCreatePC = (FN_CreatePC)GetProcAddress(k32, "CreatePseudoConsole");
            pfnClosePC = (FN_ClosePC)GetProcAddress(k32, "ClosePseudoConsole");
        }
    }

    GameWindow* activeWindow() {
        for (int i = (int)windows.size() - 1; i >= 0; i--)
            if (!windows[i]->closed) return windows[i];
        return nullptr;
    }

    static std::wstring resolveLnk(const std::wstring& lnkPath) {
        std::wstring target;
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        IShellLinkW* psl = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                       IID_IShellLinkW, (void**)&psl))) {
            IPersistFile* ppf = NULL;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))) {
                if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                    wchar_t buf[MAX_PATH];
                    if (SUCCEEDED(psl->GetPath(buf, MAX_PATH, NULL, 0)))
                        target = buf;
                }
                ppf->Release();
            }
            psl->Release();
        }
        CoUninitialize();
        return target;
    }

    void openWindow(const std::wstring& exe, const std::wstring& arg, const std::wstring& windowTitle) {
        std::wstring actualExe = exe;
        std::wstring actualArg = arg;

        size_t dotPos = exe.find_last_of(L'.');
        std::wstring ext;
        if (dotPos != std::wstring::npos) {
            ext = exe.substr(dotPos);
            for (auto& c : ext) c = towlower(c);
        }

        if (ext == L".lnk") {
            std::wstring resolved = resolveLnk(exe);
            if (!resolved.empty()) {
                actualExe = resolved;
                dotPos = resolved.find_last_of(L'.');
                if (dotPos != std::wstring::npos) {
                    ext = resolved.substr(dotPos);
                    for (auto& c : ext) c = towlower(c);
                }
            }
        }

        bool gui = false;
        if (ext == L".exe") gui = isGuiExe(actualExe);

        GameWindow* gw = new GameWindow();
        gw->title = windowTitle;
        gw->winX = 80 + (int)(windows.size() % 5) * 30;
        gw->winY = 40 + (int)(windows.size() % 5) * 30;

        if (gui) {
            if (!actualArg.empty())
                gw->launchGui(actualExe, L"\"" + actualArg + L"\"");
            else
                gw->launchGui(actualExe, L"");
        } else {
            gw->launchConPTY(actualExe, actualArg.empty() ? L"" : L"\"" + actualArg + L"\"",
                             pfnCreatePC, pfnClosePC);
        }

        gw->active = true;
        for (auto* w : windows) w->active = false;
        windows.push_back(gw);
        if (!desktopMode) desktopMode = true;
    }

    void bringToFront(GameWindow* gw) {
        auto it = std::find(windows.begin(), windows.end(), gw);
        if (it != windows.end()) {
            windows.erase(it);
            windows.push_back(gw);
        }
        for (auto* w : windows) w->active = (w == gw);
    }

    GameWindow* hitTest(int mx, int my) {
        for (int i = (int)windows.size() - 1; i >= 0; i--) {
            if (!windows[i]->closed && windows[i]->hitBody(mx, my))
                return windows[i];
        }
        return nullptr;
    }

    void handleMouseDown(int mx, int my) {
        GameWindow* hit = hitTest(mx, my);
        if (!hit) return;
        bringToFront(hit);

        if (hit->hitClose(mx, my)) {
            hit->closed = true;
            hit->destroy(pfnClosePC);
            bool anyOpen = false;
            for (auto* w : windows) if (!w->closed) anyOpen = true;
            if (!anyOpen) desktopMode = false;
            return;
        }

        if (hit->hitTitleBar(mx, my)) {
            hit->dragging = true;
            hit->dragOffX = mx - hit->winX;
            hit->dragOffY = my - hit->winY;
            return;
        }

        if (hit->isGuiCapture) {
            hit->sendMouseToGui(mx, my, WM_LBUTTONDOWN);
        } else if (hit->processRunning && hit->hPipeOut) {
            int contentX = hit->winX + 4;
            int contentY = hit->winY + GameWindow::TITLE_H + 2;
            int col = (mx - contentX) / 8 + 1;
            int row = (my - contentY) / 16 + 1;
            if (col < 1) col = 1; if (col > GameWindow::TCOLS) col = GameWindow::TCOLS;
            if (row < 1) row = 1; if (row > GameWindow::TROWS) row = GameWindow::TROWS;
            char seq[32];
            int n = wsprintfA(seq, "\033[<%d;%d;%dM", 0, col, row);
            DWORD w; WriteFile(hit->hPipeOut, seq, n, &w, NULL);
        }
    }

    void handleMouseUp() {
        for (auto* w : windows) w->dragging = false;

        GameWindow* hit = hitTest(mouseX, mouseY);
        if (hit && !hit->hitTitleBar(mouseX, mouseY)) {
            if (hit->isGuiCapture) {
                hit->sendMouseToGui(mouseX, mouseY, WM_LBUTTONUP);
            } else if (hit->processRunning && hit->hPipeOut) {
                int contentX = hit->winX + 4;
                int contentY = hit->winY + GameWindow::TITLE_H + 2;
                int col = (mouseX - contentX) / 8 + 1;
                int row = (mouseY - contentY) / 16 + 1;
                if (col < 1) col = 1; if (col > GameWindow::TCOLS) col = GameWindow::TCOLS;
                if (row < 1) row = 1; if (row > GameWindow::TROWS) row = GameWindow::TROWS;
                char seq[32];
                int n = wsprintfA(seq, "\033[<%d;%d;%dm", 0, col, row);
                DWORD w; WriteFile(hit->hPipeOut, seq, n, &w, NULL);
            }
        }
    }

    void handleMouseMove(int mx, int my) {
        mouseX = mx; mouseY = my;
        for (auto* w : windows) {
            if (w->dragging) {
                w->winX = mx - w->dragOffX;
                w->winY = my - w->dragOffY;
            }
        }
    }

    void handleChar(wchar_t c) {
        GameWindow* aw = activeWindow();
        if (!aw || !aw->processRunning) return;
        if (aw->isGuiCapture) {
            if (aw->capturedHwnd)
                PostMessageW(aw->capturedHwnd, WM_CHAR, (WPARAM)c, 0);
            return;
        }
        char buf = (char)c;
        if (c == L'\r' || c == L'\n') buf = '\r';
        else if (c == L'\b') buf = '\x7f';
        else if (c < 32) return;
        else if (c >= 127) return;
        aw->sendChar(buf);
    }

    void handleKey(int vk) {
        GameWindow* aw = activeWindow();
        if (!aw || !aw->processRunning) return;
        if (aw->isGuiCapture) {
            aw->sendKeyToGui(vk, true);
            return;
        }
        char seq[8]; int n = 0;
        switch (vk) {
        case VK_UP:    seq[0]='\033'; seq[1]='['; seq[2]='A'; n=3; break;
        case VK_DOWN:  seq[0]='\033'; seq[1]='['; seq[2]='B'; n=3; break;
        case VK_RIGHT: seq[0]='\033'; seq[1]='['; seq[2]='C'; n=3; break;
        case VK_LEFT:  seq[0]='\033'; seq[1]='['; seq[2]='D'; n=3; break;
        case VK_HOME:  seq[0]='\033'; seq[1]='['; seq[2]='H'; n=3; break;
        case VK_END:   seq[0]='\033'; seq[1]='['; seq[2]='F'; n=3; break;
        case VK_DELETE:seq[0]='\033'; seq[1]='['; seq[2]='3'; seq[3]='~'; n=4; break;
        }
        if (n > 0) aw->sendSeq(seq, n);
    }

    void update() {
        for (auto* w : windows) {
            if (!w->closed) w->update(pfnClosePC);
        }
    }

    void render(HDC hdc, int screenW, int screenH) {
        if (!desktopMode) return;
        for (auto* w : windows) {
            if (!w->closed) w->render(hdc, wmFont);
        }
        HPEN curPen1 = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
        HPEN curPen2 = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HPEN oldPen = (HPEN)SelectObject(hdc, curPen1);
        MoveToEx(hdc, mouseX - 8, mouseY, NULL); LineTo(hdc, mouseX + 8, mouseY);
        MoveToEx(hdc, mouseX, mouseY - 8, NULL); LineTo(hdc, mouseX, mouseY + 8);
        SelectObject(hdc, curPen2);
        MoveToEx(hdc, mouseX - 7, mouseY, NULL); LineTo(hdc, mouseX + 7, mouseY);
        MoveToEx(hdc, mouseX, mouseY - 7, NULL); LineTo(hdc, mouseX, mouseY + 7);
        SelectObject(hdc, oldPen);
        DeleteObject(curPen1);
        DeleteObject(curPen2);
    }

    void cleanup() {
        for (auto* w : windows) {
            w->destroy(pfnClosePC);
            delete w;
        }
        windows.clear();
        if (wmFont) { DeleteObject(wmFont); wmFont = NULL; }
    }
};
