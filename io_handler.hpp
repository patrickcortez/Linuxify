// g++ -std=c++17 main.cpp -o main.exe
#ifndef LINUXIFY_IO_HANDLER_HPP
#define LINUXIFY_IO_HANDLER_HPP

#include <windows.h>
#include <string>
#include <iostream>
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

        // Optimized screen clear
        void clearScreen() {
            updateInfo();
            DWORD count;
            DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
            COORD homeCoords = { 0, 0 };
            FillConsoleOutputCharacterA(hOut, ' ', cellCount, homeCoords, &count);
            FillConsoleOutputAttribute(hOut, csbi.wAttributes, cellCount, homeCoords, &count);
            setCursorPos(0, 0);
        }

        // Robust Area Clear - Clears from specific row to end of current view
        // Used for clean repainting of multi-line prompts
        void clearArea(int startRow, int numLines) {
            updateInfo();
            COORD clearPos;
            DWORD written;
            for (int i = 0; i < numLines + 2; i++) { // Clear a bit extra for safety
                clearPos.X = 0;
                clearPos.Y = (SHORT)(startRow + i);
                if (clearPos.Y < csbi.dwSize.Y) {
                    FillConsoleOutputCharacterA(hOut, ' ', csbi.dwSize.X, clearPos, &written);
                }
            }
        }
    };

    // Singleton accessor for simple usage
    inline Console& get() {
        static Console instance;
        return instance;
    }

} 

#endif 
