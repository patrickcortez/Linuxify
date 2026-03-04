// g++ -O3 -o ../cmds/cmatrix.exe cmatrix.cpp
#include <windows.h>
#include <conio.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>

static volatile bool g_running = true;

static BOOL WINAPI CtrlHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

struct Stream {
    int y;
    int length;
    int speed;
    int tick;
    bool active;
};

static const char* COLOR_TABLE[] = {
    "\033[32m",
    "\033[31m",
    "\033[34m",
    "\033[33m",
    "\033[36m",
    "\033[35m",
    "\033[37m"
};

enum ColorIndex { CLR_GREEN, CLR_RED, CLR_BLUE, CLR_YELLOW, CLR_CYAN, CLR_MAGENTA, CLR_WHITE };

static const char* BRIGHT_TABLE[] = {
    "\033[92m",
    "\033[91m",
    "\033[94m",
    "\033[93m",
    "\033[96m",
    "\033[95m",
    "\033[97m"
};

static char randomChar() {
    return (char)(33 + rand() % 94);
}

int main(int argc, char* argv[]) {
    int colorIdx = CLR_GREEN;
    bool bold = false;
    int speedDelay = 35;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: cmatrix [-s] [-b] [-C color]\n";
            std::cout << "Matrix digital rain animation.\n\n";
            std::cout << "Options:\n";
            std::cout << "  -s          Slow mode\n";
            std::cout << "  -b          Bold characters\n";
            std::cout << "  -C <color>  Set color: green (default), red, blue,\n";
            std::cout << "              yellow, cyan, magenta, white\n";
            std::cout << "  -h, --help  Show this help\n\n";
            std::cout << "Press 'q' or ESC to quit.\n";
            return 0;
        } else if (arg == "-s") {
            speedDelay = 70;
        } else if (arg == "-b") {
            bold = true;
        } else if (arg == "-C" && i + 1 < argc) {
            std::string c = argv[++i];
            if (c == "red") colorIdx = CLR_RED;
            else if (c == "blue") colorIdx = CLR_BLUE;
            else if (c == "yellow") colorIdx = CLR_YELLOW;
            else if (c == "cyan") colorIdx = CLR_CYAN;
            else if (c == "magenta") colorIdx = CLR_MAGENTA;
            else if (c == "white") colorIdx = CLR_WHITE;
            else colorIdx = CLR_GREEN;
        }
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    DWORD origOutMode = 0;
    GetConsoleMode(hOut, &origOutMode);
    SetConsoleMode(hOut, origOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);

    DWORD origInMode = 0;
    GetConsoleMode(hIn, &origInMode);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    std::cout << "\033[?25l";
    std::cout << "\033[2J\033[H";

    srand((unsigned int)time(NULL));

    Stream* streams = (Stream*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cols * sizeof(Stream));
    char* screen = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cols * rows);

    for (int x = 0; x < cols; x++) {
        streams[x].y = -(rand() % rows);
        streams[x].length = 4 + rand() % (rows / 2);
        streams[x].speed = 1 + rand() % 3;
        streams[x].tick = 0;
        streams[x].active = true;
    }

    memset(screen, 0, cols * rows);

    const char* dimColor = COLOR_TABLE[colorIdx];
    const char* brightColor = BRIGHT_TABLE[colorIdx];
    const char* boldSeq = bold ? "\033[1m" : "";

    char posBuf[32];
    DWORD written;
    char outBuf[65536];
    int bufPos = 0;

    auto flushBuf = [&]() {
        if (bufPos > 0) {
            WriteConsoleA(hOut, outBuf, bufPos, &written, NULL);
            bufPos = 0;
        }
    };

    auto appendStr = [&](const char* s) {
        int len = (int)strlen(s);
        if (bufPos + len >= (int)sizeof(outBuf)) flushBuf();
        memcpy(outBuf + bufPos, s, len);
        bufPos += len;
    };

    auto appendChar = [&](char c) {
        if (bufPos + 1 >= (int)sizeof(outBuf)) flushBuf();
        outBuf[bufPos++] = c;
    };

    while (g_running) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) break;
        }

        GetConsoleScreenBufferInfo(hOut, &csbi);
        int newCols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int newRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

        if (newCols != cols || newRows != rows) {
            HeapFree(GetProcessHeap(), 0, streams);
            HeapFree(GetProcessHeap(), 0, screen);
            cols = newCols;
            rows = newRows;
            streams = (Stream*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cols * sizeof(Stream));
            screen = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cols * rows);
            for (int x = 0; x < cols; x++) {
                streams[x].y = -(rand() % rows);
                streams[x].length = 4 + rand() % (rows / 2);
                streams[x].speed = 1 + rand() % 3;
                streams[x].tick = 0;
                streams[x].active = true;
            }
            memset(screen, 0, cols * rows);
            appendStr("\033[2J\033[H");
        }

        bufPos = 0;

        for (int x = 0; x < cols; x++) {
            Stream& s = streams[x];
            s.tick++;
            if (s.tick < s.speed) continue;
            s.tick = 0;
            s.y++;

            int headY = s.y;
            int tailY = s.y - s.length;

            if (headY >= 0 && headY < rows) {
                char ch = randomChar();
                screen[headY * cols + x] = ch;

                int posLen = snprintf(posBuf, sizeof(posBuf), "\033[%d;%dH", headY + 1, x + 1);
                if (bufPos + posLen >= (int)sizeof(outBuf)) flushBuf();
                memcpy(outBuf + bufPos, posBuf, posLen);
                bufPos += posLen;

                appendStr(boldSeq);
                appendStr(brightColor);
                appendChar(ch);
            }

            int prevY = headY - 1;
            if (prevY >= 0 && prevY < rows && screen[prevY * cols + x]) {
                char trailCh = randomChar();
                screen[prevY * cols + x] = trailCh;

                int posLen = snprintf(posBuf, sizeof(posBuf), "\033[%d;%dH", prevY + 1, x + 1);
                if (bufPos + posLen >= (int)sizeof(outBuf)) flushBuf();
                memcpy(outBuf + bufPos, posBuf, posLen);
                bufPos += posLen;

                if (bold) appendStr("\033[22m");
                appendStr(dimColor);
                appendChar(trailCh);
            }

            if (tailY >= 0 && tailY < rows) {
                screen[tailY * cols + x] = 0;

                int posLen = snprintf(posBuf, sizeof(posBuf), "\033[%d;%dH", tailY + 1, x + 1);
                if (bufPos + posLen >= (int)sizeof(outBuf)) flushBuf();
                memcpy(outBuf + bufPos, posBuf, posLen);
                bufPos += posLen;

                appendChar(' ');
            }

            if (tailY >= rows) {
                s.y = -(rand() % (rows / 2));
                s.length = 4 + rand() % (rows / 2);
                s.speed = 1 + rand() % 3;
                s.tick = 0;

                int scatter = rand() % 100;
                if (scatter < 15) {
                    s.y -= rand() % (rows / 3);
                }
            }
        }

        flushBuf();
        Sleep(speedDelay);
    }

    std::cout << "\033[0m";
    std::cout << "\033[2J\033[H";
    std::cout << "\033[?25h";
    std::cout.flush();

    SetConsoleMode(hOut, origOutMode);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);

    HeapFree(GetProcessHeap(), 0, streams);
    HeapFree(GetProcessHeap(), 0, screen);

    return 0;
}
