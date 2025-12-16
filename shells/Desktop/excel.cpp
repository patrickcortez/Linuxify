// Compile: g++ -std=c++17 -static -I../src -o Excel.exe excel.cpp
#include "window.hpp"
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>

using namespace FunuxSys;

// Constants
const int CELL_WIDTH = 12;
const int ROW_HEADER_WIDTH = 5;

// Coordinate system for spreadsheet
struct CellCoord {
    int col;
    int row;
    bool operator<(const CellCoord& other) const {
        if (row != other.row) return row < other.row;
        return col < other.col;
    }
};

class ExcelApp : public GraphicsApp {
private:
    std::map<CellCoord, std::string> data;
    int cursorCol = 0;
    int cursorRow = 0;
    int scrollCol = 0;
    int scrollRow = 0;
    
    // Modes
    enum Mode { NAVIGATION, EDITING, COMMAND, SAVE_PROMPT, LOAD_PROMPT };
    Mode mode = NAVIGATION;
    
    std::string currentInput = "";
    std::string statusMessage = "Ready";
    std::string filename = "";
    
    // Helper to get column name (0 -> A, 1 -> B, ...)
    std::string getColName(int col) {
        std::string res = "";
        do {
            int rem = col % 26;
            res = (char)('A' + rem) + res;
            col = col / 26 - 1;
        } while (col >= 0);
        return res;
    }
    
    void saveData(const std::string& fname) {
        std::string path = "Sheets/" + fname;
        if (path.substr(path.length() - 4) != ".exc") path += ".exc";
        
        std::ofstream file(path);
        if (!file.is_open()) {
            statusMessage = "Error: Could not save file " + path;
            return;
        }
        
        // Simple CSV-like format: ROW,COL,CONTENT
        for (const auto& kv : data) {
            file << kv.first.row << "," << kv.first.col << "," << kv.second << "\n";
        }
        file.close();
        statusMessage = "Saved to " + path;
        filename = fname;
    }
    
    void loadData(const std::string& fname) {
        std::string path = "Sheets/" + fname;
        if (path.substr(path.length() - 4) != ".exc") path += ".exc";
        
        std::ifstream file(path);
        if (!file.is_open()) {
            statusMessage = "Error: Could not open file " + path;
            return;
        }
        
        data.clear();
        std::string line;
        while (std::getline(file, line)) {
            size_t comma1 = line.find(',');
            size_t comma2 = line.find(',', comma1 + 1);
            if (comma1 != std::string::npos && comma2 != std::string::npos) {
                int r = std::stoi(line.substr(0, comma1));
                int c = std::stoi(line.substr(comma1 + 1, comma2 - comma1 - 1));
                std::string content = line.substr(comma2 + 1);
                data[{c, r}] = content;
            }
        }
        file.close();
        statusMessage = "Loaded " + path;
        filename = fname;
    }

public:
    void onInit() override {
        // Initial setup
    }
    
    void onDraw() override {
        clear(GraphicsApp::BG_BLACK | GraphicsApp::FG_WHITE);
        
        int visibleCols = (termWidth - ROW_HEADER_WIDTH) / CELL_WIDTH;
        int visibleRows = termHeight - 2; // -1 for header, -1 for status
        
        // Draw Column Headers
        drawRect(0, 0, termWidth, 1, ' ', GraphicsApp::BG_GRAY | GraphicsApp::FG_BLACK);
        for (int c = 0; c < visibleCols; c++) {
            int colIdx = scrollCol + c;
            std::string label = getColName(colIdx);
            int x = ROW_HEADER_WIDTH + c * CELL_WIDTH;
            // Center align header
            int pad = (CELL_WIDTH - (int)label.size()) / 2;
            drawText(x + pad, 0, label, GraphicsApp::BG_GRAY | GraphicsApp::FG_BLACK);
            // Draw separator
            drawPixel(x + CELL_WIDTH - 1, 0, '|', GraphicsApp::FG_BLACK | GraphicsApp::BG_GRAY);
        }
        
        // Draw Rows
        for (int r = 0; r < visibleRows; r++) {
            int rowIdx = scrollRow + r;
            int y = r + 1;
            
            // Row Header
            std::string rowNum = std::to_string(rowIdx + 1);
            drawText(0, y, rowNum, GraphicsApp::FG_CYAN);
            drawPixel(ROW_HEADER_WIDTH - 1, y, '|', GraphicsApp::FG_GRAY);
            
            // Cells
            for (int c = 0; c < visibleCols; c++) {
                int colIdx = scrollCol + c;
                int x = ROW_HEADER_WIDTH + c * CELL_WIDTH;
                
                // Content
                std::string content = "";
                if (data.count({colIdx, rowIdx})) {
                    content = data[{colIdx, rowIdx}];
                }
                
                // Color
                WORD bg = GraphicsApp::BG_BLACK;
                WORD fg = GraphicsApp::FG_WHITE;
                
                bool isCursor = (colIdx == cursorCol && rowIdx == cursorRow);
                if (isCursor) {
                    bg = GraphicsApp::BG_BLUE | GraphicsApp::BG_INTENSE_BLUE;
                    fg = GraphicsApp::FG_WHITE | GraphicsApp::FG_INTENSE_WHITE;
                    if (mode == EDITING) {
                        bg = GraphicsApp::BG_WHITE;
                        fg = GraphicsApp::FG_BLACK;
                        content = currentInput; // Show what we are typing
                    }
                }
                
                drawRect(x, y, CELL_WIDTH - 1, 1, ' ', bg | fg);
                
                // Clip content
                if (content.size() > CELL_WIDTH - 1) {
                    content = content.substr(0, CELL_WIDTH - 2) + ">";
                }
                drawText(x, y, content, bg | fg);
                
                // Grid line
                drawPixel(x + CELL_WIDTH - 1, y, '|', GraphicsApp::FG_GRAY); // Vertical grid line
            }
        }
        
        // Status Bar
        std::string statusLine = "";
        std::string modeStr = "NAV";
        if (mode == EDITING) modeStr = "EDIT";
        else if (mode == SAVE_PROMPT) modeStr = "SAVE";
        else if (mode == LOAD_PROMPT) modeStr = "LOAD";
        
        statusLine = "[" + modeStr + "] " + getColName(cursorCol) + std::to_string(cursorRow + 1) + ": ";
        
        if (mode == EDITING) {
             // In edit mode, status shows original cell if we want, or just static
        } else if (data.count({cursorCol, cursorRow})) {
            statusLine += data[{cursorCol, cursorRow}];
        } else {
            statusLine += "<empty>";
        }
        
        // Right side: Message or filename
        std::string rightStatus = filename.empty() ? "Untitled" : filename;
        if (!statusMessage.empty()) rightStatus = statusMessage;
        
        if (mode == SAVE_PROMPT || mode == LOAD_PROMPT) {
             statusLine = (mode == SAVE_PROMPT ? "Save as: " : "Open: ") + currentInput;
        }

        drawRect(0, termHeight - 1, termWidth, 1, ' ', GraphicsApp::BG_WHITE | GraphicsApp::FG_BLACK);
        drawText(0, termHeight - 1, statusLine, GraphicsApp::BG_WHITE | GraphicsApp::FG_BLACK);
        drawText(termWidth - (int)rightStatus.size() - 1, termHeight - 1, rightStatus, GraphicsApp::BG_WHITE | GraphicsApp::FG_BLACK);
        
        present();
    }
    
    void onKey(int ch, int ext) override {
        statusMessage = ""; // Clear message on action
        
        if (mode == NAVIGATION) {
            if (ch == 0 || ch == 224) {
                if (ext == 72) cursorRow--; // Up
                if (ext == 80) cursorRow++; // Down
                if (ext == 75) cursorCol--; // Left
                if (ext == 77) cursorCol++; // Right
            } else if (ch == 13) { // Enter
                mode = EDITING;
                currentInput = data[{cursorCol, cursorRow}];
            } else if (ch == 19) { // Ctrl+S
                mode = SAVE_PROMPT;
                currentInput = filename;
            } else if (ch == 15) { // Ctrl+O (Open)
                mode = LOAD_PROMPT;
                currentInput = "";
            } else if (ch == 27) { // ESC
                // Maybe prompt input for command?
                quit();
            } else if (ch == 127 || ch == 8) { // Backspace/Del clears cell
                 data.erase({cursorCol, cursorRow});
            } else if (ch >= 32 && ch <= 126) {
                // Start typing immediately replaces cell
                mode = EDITING;
                currentInput = "";
                currentInput += (char)ch;
            }
            
            // Bounds and scrolling
            if (cursorCol < 0) cursorCol = 0;
            if (cursorRow < 0) cursorRow = 0;
            
            int visibleCols = (termWidth - ROW_HEADER_WIDTH) / CELL_WIDTH;
            int visibleRows = termHeight - 2;
            
            if (cursorCol < scrollCol) scrollCol = cursorCol;
            if (cursorCol >= scrollCol + visibleCols) scrollCol = cursorCol - visibleCols + 1;
            
            if (cursorRow < scrollRow) scrollRow = cursorRow;
            if (cursorRow >= scrollRow + visibleRows) scrollRow = cursorRow - visibleRows + 1;
        }
        else if (mode == EDITING) {
            if (ch == 13) { // Enter confirms
                data[{cursorCol, cursorRow}] = currentInput;
                mode = NAVIGATION;
                cursorRow++; // Auto-move down like Excel
            } else if (ch == 27) { // ESC cancels
                mode = NAVIGATION;
            } else if (ch == 8) { // Backspace
                if (!currentInput.empty()) currentInput.pop_back();
            } else if (ch >= 32 && ch <= 126) {
                currentInput += (char)ch;
            }
        }
        else if (mode == SAVE_PROMPT || mode == LOAD_PROMPT) {
            if (ch == 13) { // Enter
                if (mode == SAVE_PROMPT) saveData(currentInput);
                else loadData(currentInput);
                mode = NAVIGATION;
            } else if (ch == 27) {
                mode = NAVIGATION;
            } else if (ch == 8) {
                if (!currentInput.empty()) currentInput.pop_back();
            } else if (ch >= 32 && ch <= 126) {
                currentInput += (char)ch;
            }
        }
    }
    
    void onTick() override {
        // No animation needed mostly
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    ExcelApp app;
    app.run();
    return 0;
}
