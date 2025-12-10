// Compile: cl /EHsc /std:c++17 nano.cpp /Fe:nano.exe
// Alternate compile: g++ c++17 -o nano nano.cpp

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <windows.h>
#include <conio.h>

class NanoEditor {
private:
    std::vector<std::string> lines;
    std::string filename;
    int cursorX;
    int cursorY;
    int scrollOffsetY;
    int screenWidth;
    int screenHeight;
    bool modified;
    bool running;
    HANDLE hConsole;
    std::string statusMessage;
    std::string cutBuffer;

    void getTerminalSize() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            screenWidth = csbi.dwSize.X;
            screenHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        } else {
            screenWidth = 80;
            screenHeight = 25;
        }
        if (screenHeight < 10) screenHeight = 25;
    }

    void setCursorPosition(int x, int y) {
        COORD pos;
        pos.X = (SHORT)x;
        pos.Y = (SHORT)y;
        SetConsoleCursorPosition(hConsole, pos);
    }

    void hideCursor() {
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 25;
        info.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &info);
    }

    void showCursor() {
        CONSOLE_CURSOR_INFO info;
        info.dwSize = 25;
        info.bVisible = TRUE;
        SetConsoleCursorInfo(hConsole, &info);
    }

    void clearLine(int y) {
        setCursorPosition(0, y);
        for (int i = 0; i < screenWidth; i++) {
            std::cout << ' ';
        }
    }

    void setColor(WORD color) {
        SetConsoleTextAttribute(hConsole, color);
    }

    void resetColor() {
        setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    void drawHeader() {
        setCursorPosition(0, 0);
        setColor(BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED | FOREGROUND_BLUE);
        
        std::string title = " NANO ";
        if (filename.empty()) {
            title += "[New Buffer]";
        } else {
            title += filename;
        }
        if (modified) {
            title += " *";
        }
        
        std::cout << title;
        int remaining = screenWidth - (int)title.length();
        for (int i = 0; i < remaining; i++) {
            std::cout << ' ';
        }
        
        resetColor();
    }

    void drawFooter() {
        int line1 = screenHeight - 2;
        int line2 = screenHeight - 1;
        
        WORD bgWhite = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        WORD keyColor = bgWhite | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD textColor = bgWhite;
        
        setCursorPosition(0, line1);
        setColor(keyColor);
        std::cout << "^X";
        setColor(textColor);
        std::cout << " Exit  ";
        setColor(keyColor);
        std::cout << "^O";
        setColor(textColor);
        std::cout << " Save  ";
        setColor(keyColor);
        std::cout << "^G";
        setColor(textColor);
        std::cout << " Help  ";
        setColor(keyColor);
        std::cout << "^K";
        setColor(textColor);
        std::cout << " Cut   ";
        setColor(keyColor);
        std::cout << "^U";
        setColor(textColor);
        std::cout << " Paste ";
        
        int pos = 48;
        while (pos < screenWidth) {
            std::cout << ' ';
            pos++;
        }
        
        setCursorPosition(0, line2);
        setColor(keyColor);
        std::cout << "^W";
        setColor(textColor);
        std::cout << " Search  ";
        
        std::string posInfo = "L:" + std::to_string(cursorY + 1) + "/" + 
                              std::to_string(lines.size()) + " C:" + 
                              std::to_string(cursorX + 1);
        
        int infoStart = screenWidth - (int)posInfo.length() - 2;
        int currentPos = 11;
        
        if (!statusMessage.empty()) {
            setColor(bgWhite | FOREGROUND_RED | FOREGROUND_BLUE);
            std::cout << statusMessage;
            currentPos += (int)statusMessage.length();
        }
        
        while (currentPos < infoStart) {
            std::cout << ' ';
            currentPos++;
        }
        
        setColor(bgWhite | FOREGROUND_BLUE);
        std::cout << posInfo;
        
        while (currentPos + (int)posInfo.length() < screenWidth) {
            std::cout << ' ';
            currentPos++;
        }
        
        resetColor();
    }

    void drawContent() {
        int contentStart = 1;
        int contentEnd = screenHeight - 3;
        int contentHeight = contentEnd - contentStart + 1;
        
        for (int y = 0; y < contentHeight; y++) {
            int screenY = contentStart + y;
            int lineIdx = scrollOffsetY + y;
            
            setCursorPosition(0, screenY);
            
            if (lineIdx < (int)lines.size()) {
                std::string& line = lines[lineIdx];
                int printLen = (int)line.length();
                if (printLen > screenWidth) {
                    printLen = screenWidth;
                }
                for (int i = 0; i < printLen; i++) {
                    std::cout << line[i];
                }
                for (int i = printLen; i < screenWidth; i++) {
                    std::cout << ' ';
                }
            } else {
                setColor(FOREGROUND_BLUE);
                std::cout << '~';
                resetColor();
                for (int i = 1; i < screenWidth; i++) {
                    std::cout << ' ';
                }
            }
        }
    }

    void refreshScreen() {
        hideCursor();
        getTerminalSize();
        
        drawHeader();
        drawContent();
        drawFooter();
        
        int contentStart = 1;
        int displayY = contentStart + (cursorY - scrollOffsetY);
        int displayX = cursorX;
        if (displayX >= screenWidth) displayX = screenWidth - 1;
        
        setCursorPosition(displayX, displayY);
        showCursor();
    }

    void ensureCursorVisible() {
        int contentHeight = screenHeight - 4;
        if (contentHeight < 1) contentHeight = 1;
        
        if (cursorY < scrollOffsetY) {
            scrollOffsetY = cursorY;
        }
        if (cursorY >= scrollOffsetY + contentHeight) {
            scrollOffsetY = cursorY - contentHeight + 1;
        }
        if (scrollOffsetY < 0) {
            scrollOffsetY = 0;
        }
    }

    void moveCursorUp() {
        if (cursorY > 0) {
            cursorY--;
            int lineLen = (int)lines[cursorY].length();
            if (cursorX > lineLen) cursorX = lineLen;
            ensureCursorVisible();
        }
    }

    void moveCursorDown() {
        if (cursorY < (int)lines.size() - 1) {
            cursorY++;
            int lineLen = (int)lines[cursorY].length();
            if (cursorX > lineLen) cursorX = lineLen;
            ensureCursorVisible();
        }
    }

    void moveCursorLeft() {
        if (cursorX > 0) {
            cursorX--;
        } else if (cursorY > 0) {
            cursorY--;
            cursorX = (int)lines[cursorY].length();
            ensureCursorVisible();
        }
    }

    void moveCursorRight() {
        int lineLen = (int)lines[cursorY].length();
        if (cursorX < lineLen) {
            cursorX++;
        } else if (cursorY < (int)lines.size() - 1) {
            cursorY++;
            cursorX = 0;
            ensureCursorVisible();
        }
    }

    void insertChar(char c) {
        if (lines.empty()) lines.push_back("");
        lines[cursorY].insert(cursorX, 1, c);
        cursorX++;
        modified = true;
    }

    void insertNewLine() {
        if (lines.empty()) lines.push_back("");
        std::string rest = lines[cursorY].substr(cursorX);
        lines[cursorY] = lines[cursorY].substr(0, cursorX);
        lines.insert(lines.begin() + cursorY + 1, rest);
        cursorY++;
        cursorX = 0;
        modified = true;
        ensureCursorVisible();
    }

    void deleteChar() {
        if (cursorX > 0) {
            lines[cursorY].erase(cursorX - 1, 1);
            cursorX--;
            modified = true;
        } else if (cursorY > 0) {
            cursorX = (int)lines[cursorY - 1].length();
            lines[cursorY - 1] += lines[cursorY];
            lines.erase(lines.begin() + cursorY);
            cursorY--;
            modified = true;
            ensureCursorVisible();
        }
    }

    void deleteCharForward() {
        int lineLen = (int)lines[cursorY].length();
        if (cursorX < lineLen) {
            lines[cursorY].erase(cursorX, 1);
            modified = true;
        } else if (cursorY < (int)lines.size() - 1) {
            lines[cursorY] += lines[cursorY + 1];
            lines.erase(lines.begin() + cursorY + 1);
            modified = true;
        }
    }

    void cutLine() {
        if (lines.empty()) return;
        cutBuffer = lines[cursorY];
        lines.erase(lines.begin() + cursorY);
        if (lines.empty()) lines.push_back("");
        if (cursorY >= (int)lines.size()) cursorY = (int)lines.size() - 1;
        cursorX = 0;
        modified = true;
        statusMessage = "Cut line";
        ensureCursorVisible();
    }

    void pasteLine() {
        if (cutBuffer.empty()) {
            statusMessage = "Buffer empty";
            return;
        }
        lines.insert(lines.begin() + cursorY, cutBuffer);
        cursorX = 0;
        modified = true;
        statusMessage = "Pasted";
    }

    std::string promptInput(const std::string& prompt) {
        int promptY = screenHeight - 2;
        setCursorPosition(0, promptY);
        setColor(BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED);
        std::cout << ' ' << prompt << ' ';
        int inputStart = (int)prompt.length() + 2;
        for (int i = inputStart; i < screenWidth; i++) std::cout << ' ';
        
        setCursorPosition(inputStart, promptY);
        showCursor();
        
        std::string input;
        while (true) {
            int ch = _getch();
            if (ch == 13) break;
            if (ch == 27) { resetColor(); return ""; }
            if (ch == 8 && !input.empty()) {
                input.pop_back();
                std::cout << "\b \b";
            } else if (ch >= 32 && ch < 127) {
                input += (char)ch;
                std::cout << (char)ch;
            }
        }
        resetColor();
        return input;
    }

    void saveFile() {
        std::string saveName = filename;
        if (saveName.empty()) {
            saveName = promptInput("Filename:");
            if (saveName.empty()) {
                statusMessage = "Cancelled";
                return;
            }
        }
        
        std::ofstream ofs(saveName);
        if (!ofs) {
            statusMessage = "Error saving!";
            return;
        }
        
        for (size_t i = 0; i < lines.size(); i++) {
            ofs << lines[i];
            if (i < lines.size() - 1) ofs << '\n';
        }
        
        filename = saveName;
        modified = false;
        statusMessage = "Saved";
    }

    void search() {
        std::string query = promptInput("Search:");
        if (query.empty()) {
            statusMessage = "";
            return;
        }
        
        for (int y = cursorY; y < (int)lines.size(); y++) {
            size_t start = (y == cursorY) ? cursorX + 1 : 0;
            size_t pos = lines[y].find(query, start);
            if (pos != std::string::npos) {
                cursorY = y;
                cursorX = (int)pos;
                ensureCursorVisible();
                statusMessage = "Found";
                return;
            }
        }
        for (int y = 0; y <= cursorY; y++) {
            size_t pos = lines[y].find(query);
            if (pos != std::string::npos) {
                cursorY = y;
                cursorX = (int)pos;
                ensureCursorVisible();
                statusMessage = "Found (wrapped)";
                return;
            }
        }
        statusMessage = "Not found";
    }

    void showHelp() {
        system("cls");
        std::cout << "\n  NANO HELP\n";
        std::cout << "  =========\n\n";
        std::cout << "  Arrow Keys  - Move cursor\n";
        std::cout << "  Home/End    - Start/end of line\n";
        std::cout << "  Page Up/Dn  - Scroll page\n";
        std::cout << "  Backspace   - Delete before cursor\n";
        std::cout << "  Delete      - Delete at cursor\n";
        std::cout << "  Enter       - New line\n\n";
        std::cout << "  Ctrl+X      - Exit\n";
        std::cout << "  Ctrl+O      - Save\n";
        std::cout << "  Ctrl+K      - Cut line\n";
        std::cout << "  Ctrl+U      - Paste line\n";
        std::cout << "  Ctrl+W      - Search\n";
        std::cout << "  Ctrl+G      - This help\n\n";
        std::cout << "  Press any key to continue...";
        _getch();
    }

    bool confirmExit() {
        if (!modified) return true;
        statusMessage = "Save? (Y/N/C)";
        refreshScreen();
        
        while (true) {
            int ch = _getch();
            if (ch == 'y' || ch == 'Y') { saveFile(); return true; }
            if (ch == 'n' || ch == 'N') return true;
            if (ch == 'c' || ch == 'C' || ch == 27) { statusMessage = ""; return false; }
        }
    }

    void processInput() {
        statusMessage = "";
        int ch = _getch();
        
        if (ch == 0 || ch == 224) {
            ch = _getch();
            switch (ch) {
                case 72: moveCursorUp(); break;
                case 80: moveCursorDown(); break;
                case 75: moveCursorLeft(); break;
                case 77: moveCursorRight(); break;
                case 71: cursorX = 0; break;
                case 79: cursorX = (int)lines[cursorY].length(); break;
                case 73: for (int i = 0; i < screenHeight - 4; i++) moveCursorUp(); break;
                case 81: for (int i = 0; i < screenHeight - 4; i++) moveCursorDown(); break;
                case 83: deleteCharForward(); break;
            }
        } else if (ch == 24) {
            if (confirmExit()) running = false;
        } else if (ch == 15) {
            saveFile();
        } else if (ch == 7) {
            showHelp();
        } else if (ch == 11) {
            cutLine();
        } else if (ch == 21) {
            pasteLine();
        } else if (ch == 23) {
            search();
        } else if (ch == 8) {
            deleteChar();
        } else if (ch == 13) {
            insertNewLine();
        } else if (ch == 9) {
            for (int i = 0; i < 4; i++) insertChar(' ');
        } else if (ch >= 32 && ch < 127) {
            insertChar((char)ch);
        }
    }

    void loadFile(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs) {
            lines.push_back("");
            statusMessage = "New file";
            return;
        }
        
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        if (lines.empty()) lines.push_back("");
        statusMessage = "Loaded " + std::to_string(lines.size()) + " lines";
    }

public:
    NanoEditor(const std::string& filepath = "") 
        : filename(filepath), cursorX(0), cursorY(0), scrollOffsetY(0),
          screenWidth(80), screenHeight(25), modified(false), running(true) {
        
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        getTerminalSize();
        
        if (!filepath.empty()) {
            loadFile(filepath);
        } else {
            lines.push_back("");
            statusMessage = "New buffer";
        }
    }

    void run() {
        system("cls");
        
        while (running) {
            refreshScreen();
            processInput();
        }
        
        system("cls");
    }
};

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    std::string filename;
    if (argc > 1) filename = argv[1];
    
    NanoEditor editor(filename);
    editor.run();
    
    return 0;
}
