// g++ -std=c++17 main.cpp -o main.exe
#ifndef LINUXIFY_IO_HANDLER_HPP
#define LINUXIFY_IO_HANDLER_HPP

#include <windows.h>
#include <string>
#include <vector>

namespace IO {

    class Console {
    private:
        HANDLE hOut;
        CONSOLE_SCREEN_BUFFER_INFO csbi;

    public:
        // Colors corresponding to shell Usage
        static const WORD COLOR_COMMAND = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  
        static const WORD COLOR_ARG     = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; 
        static const WORD COLOR_STRING  = FOREGROUND_RED | FOREGROUND_INTENSITY;                     
        static const WORD COLOR_FLAG    = FOREGROUND_INTENSITY;                                      
        static const WORD COLOR_DEFAULT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;       
        static const WORD COLOR_FAINT   = FOREGROUND_INTENSITY;                                      

        Console() {
            hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            
            // Enable ANSI Escape Codes (Virtual Terminal Processing)
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
            
            updateInfo();
        }

        void updateInfo() {
            GetConsoleScreenBufferInfo(hOut, &csbi);
        }

        int getWidth() {
            updateInfo();
            return csbi.dwSize.X;
        }

        int getHeight() {
            updateInfo();
            return csbi.dwSize.Y;
        }

        COORD getCursorPos() {
            updateInfo();
            return csbi.dwCursorPosition;
        }

        void setCursorPos(SHORT x, SHORT y) {
            COORD c = { x, y };
            SetConsoleCursorPosition(hOut, c);
        }

        void setColor(WORD attrs) {
            SetConsoleTextAttribute(hOut, attrs);
        }

        void resetColor() {
            SetConsoleTextAttribute(hOut, COLOR_DEFAULT);
        }

        void write(const std::string& text) {
            DWORD written;
            WriteConsoleA(hOut, text.c_str(), (DWORD)text.length(), &written, NULL);
        }

        // Optimized screen clear (GUI + Buffer)
        void clearScreen() {
            DWORD dwMode = 0;
            
            if (GetConsoleMode(hOut, &dwMode) && (dwMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {

                write("\x1b[2J\x1b[3J\x1b[H");
            } else {
                // Legacy Fallback (for older conhost.exe without VT support)
                updateInfo();
                
                COORD topLeft = { 0, 0 };
                DWORD cCharsWritten;
                DWORD dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

                // Fill the entire screen with blanks
                FillConsoleOutputCharacterA(hOut, (TCHAR)' ', dwConSize, topLeft, &cCharsWritten);
                FillConsoleOutputAttribute(hOut, csbi.wAttributes, dwConSize, topLeft, &cCharsWritten);
                
                // Put the cursor at its home coordinates
                SetConsoleCursorPosition(hOut, topLeft);
                
                // Force the GUI viewport to reset to the top of the buffer
                SMALL_RECT scrollRect = csbi.srWindow;
                scrollRect.Bottom = scrollRect.Bottom - scrollRect.Top; // Maintain current height
                scrollRect.Top = 0;                                     // Move view to the top
                SetConsoleWindowInfo(hOut, TRUE, &scrollRect);
            }
        }
        
        void clearArea(int startRow, int numLines) {
            updateInfo();
            COORD clearPos;
            DWORD written;
            for (int i = 0; i < numLines + 2; i++) { // Clear a bit extra for safety
                clearPos.X = 0;
                clearPos.Y = (SHORT)(startRow + i);
                if (clearPos.Y < csbi.dwSize.Y) {
                    FillConsoleOutputCharacterA(hOut, ' ', csbi.dwSize.X, clearPos, &written);
                    FillConsoleOutputAttribute(hOut, csbi.wAttributes, csbi.dwSize.X, clearPos, &written);
                }
            }
        }

        void clearFromCursor() {
            updateInfo();
            DWORD written;
            DWORD cells = (csbi.dwSize.X - csbi.dwCursorPosition.X); 
            // Clear rest of the current line
            FillConsoleOutputCharacterA(hOut, ' ', cells, csbi.dwCursorPosition, &written);
            FillConsoleOutputAttribute(hOut, COLOR_DEFAULT, cells, csbi.dwCursorPosition, &written);
        }
    };

    // Singleton accessor for simple usage
    inline Console& get() {
        static Console instance;
        return instance;
    }

} 

#endif