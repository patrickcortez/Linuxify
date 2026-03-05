#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *FN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void (WINAPI *FN_ClosePseudoConsole)(void*);

struct GameConsole {
    static const int COLS = 120;
    static const int ROWS = 30;

    bool isOpen = false;
    wchar_t grid[ROWS][COLS];
    int curX = 0, curY = 0;
    std::vector<std::wstring> scrollback;
    int viewOffset = 0;
    int maxViewLines = 38;

    std::wstring inputLine;
    int inputCursorPos = 0;

    HANDLE hPipeIn = NULL;
    HANDLE hPipeOut = NULL;
    HANDLE hProcess = NULL;
    void* hPC = NULL;
    HFONT consoleFont = NULL;
    bool processRunning = false;

    FN_CreatePseudoConsole pfnCreatePC = nullptr;
    FN_ClosePseudoConsole pfnClosePC = nullptr;

    int csiParams[16];
    int csiParamCount = 0;
    bool inCSI = false;
    bool inOSC = false;
    bool gotESC = false;

    void init() {
        consoleFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32) {
            pfnCreatePC = (FN_CreatePseudoConsole)GetProcAddress(k32, "CreatePseudoConsole");
            pfnClosePC = (FN_ClosePseudoConsole)GetProcAddress(k32, "ClosePseudoConsole");
        }
        clearGrid();
    }

    void clearGrid() {
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                grid[r][c] = L' ';
        curX = 0;
        curY = 0;
    }

    void open(const std::wstring& exe, const std::wstring& workDir) {
        if (processRunning) { isOpen = true; return; }

        isOpen = true;
        clearGrid();
        scrollback.clear();
        inputLine.clear();
        inputCursorPos = 0;
        viewOffset = 0;
        gotESC = false;
        inCSI = false;
        inOSC = false;

        if (!pfnCreatePC || !pfnClosePC) return;

        HANDLE hPipePTYIn = NULL, hPipePTYOut = NULL;
        CreatePipe(&hPipePTYIn, &hPipeOut, NULL, 0);
        CreatePipe(&hPipeIn, &hPipePTYOut, NULL, 0);

        COORD size = {COLS, ROWS};
        HRESULT hr = pfnCreatePC(size, hPipePTYIn, hPipePTYOut, 0, &hPC);
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
        BOOL ok = CreateProcessW(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE,
            EXTENDED_STARTUPINFO_PRESENT, NULL, workDir.c_str(),
            &siEx.StartupInfo, &pi);

        DeleteProcThreadAttributeList(siEx.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, siEx.lpAttributeList);

        if (ok) {
            hProcess = pi.hProcess;
            if (pi.hThread) CloseHandle(pi.hThread);
            processRunning = true;
        } else {
            if (hPC) { pfnClosePC(hPC); hPC = NULL; }
            if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
            if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
        }
    }

    void close() {
        if (hPC && pfnClosePC) { pfnClosePC(hPC); hPC = NULL; }
        if (hProcess) { TerminateProcess(hProcess, 0); CloseHandle(hProcess); hProcess = NULL; }
        if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
        if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
        processRunning = false;
        isOpen = false;
    }

    void toggle(const std::wstring& exe, const std::wstring& workDir) {
        if (isOpen) isOpen = false;
        else open(exe, workDir);
    }

    void scrollUp() {
        std::wstring line(grid[0], COLS);
        size_t end = line.find_last_not_of(L' ');
        if (end != std::wstring::npos) line.resize(end + 1);
        else line.clear();
        scrollback.push_back(line);

        for (int r = 0; r < ROWS - 1; r++)
            memcpy(grid[r], grid[r + 1], COLS * sizeof(wchar_t));
        for (int c = 0; c < COLS; c++)
            grid[ROWS - 1][c] = L' ';
    }

    void putChar(wchar_t ch) {
        if (curX >= COLS) { curX = 0; curY++; }
        if (curY >= ROWS) { scrollUp(); curY = ROWS - 1; }
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
        case 'B': curY += csiParam(0, 1); if (curY >= ROWS) curY = ROWS - 1; break;
        case 'C': curX += csiParam(0, 1); if (curX >= COLS) curX = COLS - 1; break;
        case 'D': curX -= csiParam(0, 1); if (curX < 0) curX = 0; break;
        case 'H': case 'f':
            curY = csiParam(0, 1) - 1; curX = csiParam(1, 1) - 1;
            if (curY < 0) curY = 0; if (curY >= ROWS) curY = ROWS - 1;
            if (curX < 0) curX = 0; if (curX >= COLS) curX = COLS - 1;
            break;
        case 'J':
            n = csiParam(0, 0);
            if (n == 0) {
                for (int c = curX; c < COLS; c++) grid[curY][c] = L' ';
                for (int r = curY + 1; r < ROWS; r++) for (int c = 0; c < COLS; c++) grid[r][c] = L' ';
            } else if (n == 1) {
                for (int r = 0; r < curY; r++) for (int c = 0; c < COLS; c++) grid[r][c] = L' ';
                for (int c = 0; c <= curX && c < COLS; c++) grid[curY][c] = L' ';
            } else if (n == 2 || n == 3) {
                for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) grid[r][c] = L' ';
                curX = 0; curY = 0;
            }
            break;
        case 'K':
            n = csiParam(0, 0);
            if (n == 0) { for (int c = curX; c < COLS; c++) grid[curY][c] = L' '; }
            else if (n == 1) { for (int c = 0; c <= curX && c < COLS; c++) grid[curY][c] = L' '; }
            else if (n == 2) { for (int c = 0; c < COLS; c++) grid[curY][c] = L' '; }
            break;
        case 'G':
            curX = csiParam(0, 1) - 1;
            if (curX < 0) curX = 0; if (curX >= COLS) curX = COLS - 1;
            break;
        case 'd':
            curY = csiParam(0, 1) - 1;
            if (curY < 0) curY = 0; if (curY >= ROWS) curY = ROWS - 1;
            break;
        case 'E': curX = 0; curY += csiParam(0, 1); if (curY >= ROWS) curY = ROWS - 1; break;
        case 'F': curX = 0; curY -= csiParam(0, 1); if (curY < 0) curY = 0; break;
        case 'S':
            n = csiParam(0, 1);
            for (int s = 0; s < n; s++) scrollUp();
            break;
        case 'T':
            n = csiParam(0, 1);
            for (int s = 0; s < n; s++) {
                for (int r = ROWS - 1; r > 0; r--) memcpy(grid[r], grid[r - 1], COLS * sizeof(wchar_t));
                for (int c = 0; c < COLS; c++) grid[0][c] = L' ';
            }
            break;
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
            } else if (ch == '?' || ch == '>' || ch == '!') {
            } else if ((ch >= '@' && ch <= '~')) {
                executeCSI((char)ch);
                inCSI = false;
            }
            return;
        }

        if (gotESC) {
            gotESC = false;
            if (ch == '[') {
                inCSI = true;
                csiParamCount = 0;
                memset(csiParams, 0, sizeof(csiParams));
            } else if (ch == ']') {
                inOSC = true;
            }
            return;
        }

        if (ch == '\033') { gotESC = true; return; }
        if (ch == '\r') { curX = 0; return; }
        if (ch == '\n') {
            curY++;
            if (curY >= ROWS) { scrollUp(); curY = ROWS - 1; }
            return;
        }
        if (ch == '\b') { if (curX > 0) curX--; return; }
        if (ch == '\t') { curX = (curX + 8) & ~7; if (curX >= COLS) curX = COLS - 1; return; }
        if (ch == 0x07 || ch < 32) return;

        putChar((wchar_t)ch);
    }

    void update() {
        if (!processRunning || !hPipeIn) return;

        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            if (hPC && pfnClosePC) { pfnClosePC(hPC); hPC = NULL; }
            if (hProcess) { CloseHandle(hProcess); hProcess = NULL; }
            if (hPipeOut) { CloseHandle(hPipeOut); hPipeOut = NULL; }
            if (hPipeIn) { CloseHandle(hPipeIn); hPipeIn = NULL; }
            processRunning = false;
            return;
        }

        DWORD avail = 0;
        while (PeekNamedPipe(hPipeIn, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            char buf[4096];
            DWORD bytesRead = 0;
            DWORD toRead = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
            if (ReadFile(hPipeIn, buf, toRead, &bytesRead, NULL) && bytesRead > 0) {
                for (DWORD b = 0; b < bytesRead; b++)
                    processByte((unsigned char)buf[b]);
            } else break;
        }
    }

    void handleChar(wchar_t c) {
        if (!isOpen || !processRunning) return;
        if (c == L'`' || c == L'~') return;

        char buf[4];
        int n = 0;
        if (c == L'\r' || c == L'\n') {
            buf[0] = '\r'; n = 1;
            inputLine.clear();
            inputCursorPos = 0;
        } else if (c == L'\b') {
            buf[0] = '\x7f'; n = 1;
            if (inputCursorPos > 0) { inputLine.erase(inputCursorPos - 1, 1); inputCursorPos--; }
        } else if (c >= 32 && c < 127) {
            buf[0] = (char)c; n = 1;
            inputLine.insert(inputCursorPos, 1, c);
            inputCursorPos++;
        }

        if (n > 0 && hPipeOut) {
            DWORD written;
            WriteFile(hPipeOut, buf, n, &written, NULL);
        }
    }

    void handleKey(int vk) {
        if (!isOpen || !hPipeOut) return;
        char seq[8];
        int n = 0;
        switch (vk) {
        case VK_UP:    seq[0]='\033'; seq[1]='['; seq[2]='A'; n=3; break;
        case VK_DOWN:  seq[0]='\033'; seq[1]='['; seq[2]='B'; n=3; break;
        case VK_RIGHT: seq[0]='\033'; seq[1]='['; seq[2]='C'; n=3; break;
        case VK_LEFT:  seq[0]='\033'; seq[1]='['; seq[2]='D'; n=3; break;
        case VK_HOME:  seq[0]='\033'; seq[1]='['; seq[2]='H'; n=3; break;
        case VK_END:   seq[0]='\033'; seq[1]='['; seq[2]='F'; n=3; break;
        case VK_DELETE:seq[0]='\033'; seq[1]='['; seq[2]='3'; seq[3]='~'; n=4; break;
        case VK_PRIOR:
            viewOffset += maxViewLines / 2;
            if (viewOffset > (int)scrollback.size()) viewOffset = (int)scrollback.size();
            return;
        case VK_NEXT:
            viewOffset -= maxViewLines / 2;
            if (viewOffset < 0) viewOffset = 0;
            return;
        }
        if (n > 0 && processRunning) {
            DWORD written;
            WriteFile(hPipeOut, seq, n, &written, NULL);
        }
    }

    void render(HDC hdc, int screenW, int screenH) {
        if (!isOpen) return;

        HBRUSH bg = CreateSolidBrush(RGB(10, 10, 15));
        RECT bgRect = {0, 0, screenW, screenH};
        FillRect(hdc, &bgRect, bg);
        DeleteObject(bg);

        HFONT oldFont = (HFONT)SelectObject(hdc, consoleFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(180, 255, 180));

        int lineH = 18;
        int inputAreaH = lineH + 10;
        int outputAreaH = screenH - inputAreaH;
        maxViewLines = outputAreaH / lineH;

        int totalLines = (int)scrollback.size() + ROWS;
        int startIdx = totalLines - maxViewLines - viewOffset;
        if (startIdx < 0) startIdx = 0;

        int y = 2;
        for (int i = startIdx; i < totalLines && y + lineH <= outputAreaH; i++) {
            std::wstring line;
            if (i < (int)scrollback.size()) {
                line = scrollback[i];
            } else {
                int row = i - (int)scrollback.size();
                line = std::wstring(grid[row], COLS);
                size_t end = line.find_last_not_of(L' ');
                if (end != std::wstring::npos) line.resize(end + 1);
                else line.clear();
            }
            if (!line.empty())
                TextOutW(hdc, 6, y, line.c_str(), (int)line.length());
            y += lineH;
        }

        int inputY = screenH - inputAreaH;
        HBRUSH inputBg = CreateSolidBrush(RGB(20, 20, 30));
        RECT inputRect = {0, inputY, screenW, screenH};
        FillRect(hdc, &inputRect, inputBg);
        DeleteObject(inputBg);

        SetTextColor(hdc, RGB(100, 255, 100));
        std::wstring prompt = L"> " + inputLine;
        TextOutW(hdc, 6, inputY + 4, prompt.c_str(), (int)prompt.length());

        SIZE sz;
        std::wstring beforeCur = L"> " + inputLine.substr(0, inputCursorPos);
        GetTextExtentPoint32W(hdc, beforeCur.c_str(), (int)beforeCur.length(), &sz);
        if ((GetTickCount() / 500) % 2 == 0) {
            HPEN curPen = CreatePen(PS_SOLID, 2, RGB(100, 255, 100));
            HPEN oldPen = (HPEN)SelectObject(hdc, curPen);
            MoveToEx(hdc, 6 + sz.cx, inputY + 3, NULL);
            LineTo(hdc, 6 + sz.cx, inputY + 3 + lineH);
            SelectObject(hdc, oldPen);
            DeleteObject(curPen);
        }

        SelectObject(hdc, oldFont);
    }

    void cleanup() {
        close();
        if (consoleFont) { DeleteObject(consoleFont); consoleFont = NULL; }
    }
};
