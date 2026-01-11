#ifndef LINUXIFY_SHELL_STREAMS_HPP
#define LINUXIFY_SHELL_STREAMS_HPP

#include <windows.h>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

namespace ShellIO {

    // Color Definitions
    enum class Color {
        Red, Green, Blue, Yellow, Cyan, Magenta, White, Gray,
        LightRed, LightGreen, LightBlue, LightYellow, LightCyan, LightMagenta, LightWhite,
        Reset, Bold, Faint
    };

    // Endl Token
    struct Endl {};
    inline Endl endl;

    // --- Output Stream ---
    class ShellOutStream {
    private:
        std::mutex outputMutex;
        HANDLE hOut;
        bool isErrorStream;
        bool isConsole; // True if output is a console, False if pipe/file
        std::atomic<bool> isPromptActive{false};
        std::function<void()> redrawCallback = nullptr;

        void writeRaw(const char* data, DWORD length) {
            DWORD written;
            if (isConsole) {
                WriteConsoleA(hOut, data, length, &written, NULL);
            } else {
                WriteFile(hOut, data, length, &written, NULL);
            }
        }

        void setAttributes(WORD attrs) {
            if (isConsole) SetConsoleTextAttribute(hOut, attrs);
        }

        void resetAttributes() {
            if (isConsole) SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        }

        void clearLine() {
            if (!isConsole) return;
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hOut, &csbi);
            COORD pos = {0, csbi.dwCursorPosition.Y};
            DWORD written;
            FillConsoleOutputCharacterA(hOut, ' ', csbi.dwSize.X, pos, &written);
            SetConsoleCursorPosition(hOut, pos);
        }

    public:
        ShellOutStream(bool error) : isErrorStream(error) {
            hOut = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
            DWORD mode;
            isConsole = GetConsoleMode(hOut, &mode);
        }

        // Re-check handle type (useful if redirection changes)
        void refreshHandle() {
             hOut = GetStdHandle(isErrorStream ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
             DWORD mode;
             isConsole = GetConsoleMode(hOut, &mode);
        }

        void registerPromptCallback(std::function<void()> callback) {
            std::lock_guard<std::mutex> lock(outputMutex);
            redrawCallback = callback;
        }

        void setPromptActive(bool active) {
            isPromptActive = active;
        }

        void print(const std::string& content) {
            std::lock_guard<std::mutex> lock(outputMutex);
            if (content.empty()) return;

            // 1. Clear Line (only if console and prompt active)
            if (isConsole && isPromptActive && redrawCallback) {
                clearLine();
            }

            // 2. Set Color
            if (isErrorStream) {
                setAttributes(FOREGROUND_RED | FOREGROUND_INTENSITY);
            }

            // 3. Write
            writeRaw(content.c_str(), (DWORD)content.length());

            // 4. Reset Color
            if (isErrorStream) {
                resetAttributes();
            }

            // 5. Restore Prompt
            if (isConsole && isPromptActive && redrawCallback) {
                if (content.back() != '\n') {
                    writeRaw("\n", 1);
                }
                redrawCallback();
            }
        }

        void printColor(Color c) {
            if (!isConsole) return; // Colors ignored on pipes
            std::lock_guard<std::mutex> lock(outputMutex);
            WORD attrs = 0;
            switch(c) {
                case Color::Red: attrs = FOREGROUND_RED; break;
                case Color::Green: attrs = FOREGROUND_GREEN; break;
                case Color::Blue: attrs = FOREGROUND_BLUE; break;
                case Color::Yellow: attrs = FOREGROUND_RED | FOREGROUND_GREEN; break;
                case Color::Cyan: attrs = FOREGROUND_GREEN | FOREGROUND_BLUE; break;
                case Color::Magenta: attrs = FOREGROUND_RED | FOREGROUND_BLUE; break;
                case Color::White: attrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
                case Color::Gray: attrs = FOREGROUND_INTENSITY; break;
                
                case Color::LightRed: attrs = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
                case Color::LightGreen: attrs = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
                case Color::LightBlue: attrs = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
                case Color::LightYellow: attrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
                case Color::LightCyan: attrs = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
                case Color::LightMagenta: attrs = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
                case Color::LightWhite: attrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
                
                case Color::Reset: attrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
                default: attrs = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
            }
            setAttributes(attrs);
        }

        // --- Operators ---
        ShellOutStream& operator<<(const std::string& val) { print(val); return *this; }
        ShellOutStream& operator<<(const char* val) { print(std::string(val)); return *this; }
        ShellOutStream& operator<<(char val) { print(std::string(1, val)); return *this; }
        ShellOutStream& operator<<(int val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(long val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(long long val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(unsigned int val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(unsigned long val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(unsigned long long val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(float val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(double val) { print(std::to_string(val)); return *this; }
        ShellOutStream& operator<<(bool val) { print(val ? "true" : "false"); return *this; }
        ShellOutStream& operator<<(Color c) { printColor(c); return *this; }
        ShellOutStream& operator<<(Endl) { print("\r\n"); return *this; }
        
        // Manual Flush (WriteConsole is usually immediate, but for pipes WriteFile is buffered by OS)
        void flush() {
            if (!isConsole) FlushFileBuffers(hOut);
        }
    };

    // --- Input Stream ---
    class ShellInStream {
    private:
        HANDLE hIn;
        bool isConsole;
        std::string buffer; // Internal buffer for tokenization

        bool readChunk() {
            const DWORD CHUNK_SIZE = 128;
            char chunk[CHUNK_SIZE];
            DWORD read;
            bool success;

            if (isConsole) {
                // ReadConsoleA reads logic lines (until enter)
                success = ReadConsoleA(hIn, chunk, CHUNK_SIZE - 1, &read, NULL);
            } else {
                // ReadFile reads raw bytes (pipe)
                 success = ReadFile(hIn, chunk, CHUNK_SIZE - 1, &read, NULL);
            }

            if (success && read > 0) {
                chunk[read] = '\0';
                buffer += chunk;
                return true;
            }
            return false;
        }

        // Skip whitespace
        void skipWhitespace() {
            while (true) {
                size_t firstNonSpace = buffer.find_first_not_of(" \t\r\n");
                if (firstNonSpace == std::string::npos) {
                    // All whitespace, clear and read more
                    buffer.clear();
                    if (!readChunk()) return; // EOF
                } else {
                    buffer.erase(0, firstNonSpace);
                    return;
                }
            }
        }

    public:
        ShellInStream() {
            hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode;
            isConsole = GetConsoleMode(hIn, &mode);
        }

        explicit ShellInStream(HANDLE handle) : hIn(handle) {
            DWORD mode;
            isConsole = GetConsoleMode(hIn, &mode);
        }

        ShellInStream& operator>>(std::string& out) {
            skipWhitespace();
            if (buffer.empty() && !readChunk()) return *this; // EOF

            size_t end = buffer.find_first_of(" \t\r\n");
            while (end == std::string::npos) {
                if (!readChunk()) { // End of stream
                    out = buffer;
                    buffer.clear();
                    return *this;
                }
                end = buffer.find_first_of(" \t\r\n");
            }
            
            out = buffer.substr(0, end);
            buffer.erase(0, end);
            return *this;
        }

        ShellInStream& operator>>(int& out) {
            std::string token;
            *this >> token;
            if (!token.empty()) out = std::stoi(token);
            return *this;
        }
        
         ShellInStream& operator>>(float& out) {
            std::string token;
            *this >> token;
            if (!token.empty()) out = std::stof(token);
            return *this;
        }
        
        // Helper to read entire line
        bool getline(std::string& line) {
            line.clear();
             // Consuming logic for whole line...
             // For now simple implementation:
             if (buffer.empty() && !readChunk()) return false;
             
             size_t newline = buffer.find('\n');
             while (newline == std::string::npos) {
                 if (!readChunk()) {
                     line = buffer;
                     buffer.clear();
                     return !line.empty();
                 }
                 newline = buffer.find('\n');
             }
             
             line = buffer.substr(0, newline);
             // Remove \r if present
             if (!line.empty() && line.back() == '\r') line.pop_back();
             
             buffer.erase(0, newline + 1);
             return true;
        }
    };

    // Singleton Instances
    inline ShellOutStream sout(false);
    inline ShellOutStream serr(true);
    inline ShellInStream sin;

} // namespace ShellIO

#endif // LINUXIFY_SHELL_STREAMS_HPP
