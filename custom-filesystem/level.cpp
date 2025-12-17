/*
 * Compile: g++ level.cpp -o level.exe
 */

#include "fs_common.hpp"
#include <conio.h>
#include <vector>
#include <algorithm>
#include <windows.h> // For console size and cursor visibility

// Key codes
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_ENTER 13
#define KEY_BACKSPACE 8
#define KEY_ESC 27

// ANSI Colors
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_REVERSE "\033[7m"
#define ANSI_BLUE "\033[34m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CYAN "\033[36m"
#define ANSI_WHITE "\033[37m"

struct FileEntry {
    string name;
    uint8_t type;
    uint64_t cluster; 
    uint64_t size;
    string extraInfo; 
};

class LevelExplorer {
    DiskDevice disk;
    SuperBlock sb;

    struct Context {
        uint64_t dirCluster;       
        uint64_t contentCluster;    
        string path;
        string version;             
    } ctx;

    vector<FileEntry> currentEntries;
    int selectionIndex = 0;
    int scrollOffset = 0;
    int maxRows = 20;

public:
    LevelExplorer() {
        memset(&ctx, 0, sizeof(ctx));
        ctx.path = "/";
        setupConsole();
    }
    
    ~LevelExplorer() {
        // Restore cursor
        cout << "\033[?25h"; 
    }

    void setupConsole() {
        // Enable ANSI processing on Windows
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
        
        // Hide cursor
        cout << "\033[?25l"; 
    }

    bool mount(string path) {
        bool res = false;
        if (path.length() == 1) res = disk.open(path[0]);
        else res = disk.openFile(path);

        if (!res) {
            cout << "Failed to open " << path << endl;
            return false;
        }

        if (!disk.readSector(0, &sb) || sb.magic != MAGIC) {
            cout << "Invalid filesystem." << endl;
            disk.close();
            return false;
        }

        ctx.dirCluster = sb.rootDirCluster;
        ctx.path = "/";
        
        if (!loadVersion("master")) {
             VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
             bool found = false;
             for (int i=0; i<8; i++) {
                 disk.readSector(ctx.dirCluster * 8 + i, vps);
                 for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                     if (vps[j].isActive) {
                         ctx.contentCluster = vps[j].contentTableCluster;
                         ctx.version = vps[j].versionName;
                         found = true;
                         break;
                     }
                 }
                 if(found) break;
             }
             if(!found) {
                 cout << "No active version found in root." << endl;
                 return false;
             }
        } else {
            ctx.version = "master";
        }

        refreshEntries();
        return true;
    }

    void run() {
        while (true) {
            render();
            int c = _getch();
            if (c == 224) { 
                c = _getch();
                if (c == KEY_UP) {
                    if (selectionIndex > 0) selectionIndex--;
                    if (selectionIndex < scrollOffset) scrollOffset = selectionIndex;
                } else if (c == KEY_DOWN) {
                    if (selectionIndex < currentEntries.size() - 1) selectionIndex++;
                    if (selectionIndex >= scrollOffset + maxRows) scrollOffset = selectionIndex - maxRows + 1;
                } else if (c == KEY_LEFT) {
                    navigateUp();
                }
            } else if (c == KEY_ENTER) {
                if (currentEntries.empty()) continue;
                handleEnter();
            } else if (c == KEY_BACKSPACE) {
                navigateUp();
            } else if (c == KEY_ESC || c == 'q') {
                break;
            }
        }
    }

private:
    uint64_t getLATEntry(uint64_t cluster) {
        uint64_t latOffset = cluster * sizeof(uint64_t);
        uint64_t sectorOffset = latOffset / SECTOR_SIZE;
        uint64_t entryOffset = latOffset % SECTOR_SIZE;
        uint64_t sectorIdx = (sb.latStartCluster * 8) + sectorOffset;
        uint8_t buffer[SECTOR_SIZE];
        if(!disk.readSector(sectorIdx, buffer)) return LAT_BAD;
        uint64_t* entry = (uint64_t*)(buffer + entryOffset);
        return *entry;
    }

    vector<uint64_t> getChain(uint64_t startCluster) {
        vector<uint64_t> chain;
        uint64_t current = startCluster;
        while (current != 0 && current != LAT_END && current != LAT_BAD && current < (sb.totalSectors/sb.clusterSize)) {
            chain.push_back(current);
            current = getLATEntry(current);
            if (find(chain.begin(), chain.end(), current) != chain.end()) break; 
            if (chain.size() > 1000000) break;
        }
        return chain;
    }

    bool loadVersion(const string& ver) {
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        for (int i=0; i<8; i++) {
             disk.readSector(ctx.dirCluster * 8 + i, vps);
             for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                 if (vps[j].isActive && string(vps[j].versionName) == ver) {
                     ctx.contentCluster = vps[j].contentTableCluster;
                     return true;
                 }
             }
        }
        return false;
    }

    void refreshEntries() {
        currentEntries.clear();
        selectionIndex = 0;
        scrollOffset = 0;
        
        DirEntry entries[SECTOR_SIZE/sizeof(DirEntry)];
        for (int i=0; i<8; i++) {
            disk.readSector(ctx.contentCluster * 8 + i, entries);
            for (int j=0; j<SECTOR_SIZE/sizeof(DirEntry); j++) {
                if (entries[j].type != TYPE_FREE) {
                    FileEntry fe;
                    entries[j].name[31] = '\0';
                    fe.name = entries[j].name;
                    fe.type = entries[j].type;
                    fe.cluster = entries[j].startCluster;
                    fe.size = entries[j].size;
                    
                    if (fe.type == TYPE_LEVELED_DIR) {
                        fe.extraInfo = "<L-DIR>";
                    } else {
                         fe.extraInfo = to_string(fe.size) + " B";
                    }
                    currentEntries.push_back(fe);
                }
            }
        }
        
        sort(currentEntries.begin(), currentEntries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.type != b.type) return a.type > b.type;
            return a.name < b.name;
        });
    }

    void drawBox(int x, int y, int w, int h) {
        // Top
        cout << "\033[" << y << ";" << x << "H" << "┌";
        for (int i=0; i<w-2; i++) cout << "─";
        cout << "┐";
        
        // Sides
        for (int i=1; i<h-1; i++) {
            cout << "\033[" << (y+i) << ";" << x << "H" << "│";
            cout << "\033[" << (y+i) << ";" << (x+w-1) << "H" << "│";
        }
        
        // Bottom
        cout << "\033[" << (y+h-1) << ";" << x << "H" << "└";
        for (int i=0; i<w-2; i++) cout << "─";
        cout << "┘";
    }

    void getConsoleSize(int& w, int& h) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    void render() {
        // Clear screen properly
        cout << "\033[2J";
        
        int width, height;
        getConsoleSize(width, height);
        
        // Ensure minimum size
        if (width < 40) width = 40;
        if (height < 10) height = 10;
        
        maxRows = height - 4; // Header + Footer + Borders

        drawBox(1, 1, width, height);

        // Header
        cout << "\033[2;3H" << ANSI_BOLD << ANSI_CYAN << " LevelFS Explorer " << ANSI_RESET;
        
        string pathStr = "Path: " + ctx.path;
        if (pathStr.length() > (width - 25)) pathStr = pathStr.substr(0, width - 28) + "...";
        cout << "\033[2;" << (width - pathStr.length() - 2) << "H" << pathStr;
        
        // Separator
        cout << "\033[3;2H";
        for (int i=0; i<width-2; i++) cout << "─";

        // Version pill
        string verStr = " " + ctx.version + " ";
        int verX = width/2 - verStr.length()/2;
        if (verX < 20) verX = 20; // Avoid overlapping title
        cout << "\033[2;" << verX << "H" << ANSI_REVERSE << verStr << ANSI_RESET;

        if (currentEntries.empty()) {
            cout << "\033[5;4H" << "(Empty directory)";
        } else {
            // Adjust scroll if window shrunk
             if (selectionIndex >= scrollOffset + maxRows) scrollOffset = selectionIndex - maxRows + 1;
             
            int endRow = min((int)currentEntries.size(), scrollOffset + maxRows);
            int row = 4;
            for (int i = scrollOffset; i < endRow; i++) {
                const auto& e = currentEntries[i];
                cout << "\033[" << row << ";3H"; // Position cursor
                
                if (i == selectionIndex) cout << ANSI_REVERSE << ANSI_BOLD;
                
                string icon = (e.type == TYPE_LEVELED_DIR) ? "[DIR] " : "      ";
                string displayText = icon + e.name;
                
                // Truncate text if too long
                int availableWidth = width - 16; 
                if (displayText.length() > availableWidth) {
                    displayText = displayText.substr(0, availableWidth - 3) + "...";
                }
                
                cout << setw(availableWidth) << left << displayText;
                cout << setw(10) << right << (e.type == TYPE_LEVELED_DIR ? "" : e.extraInfo);
                
                if (i == selectionIndex) cout << ANSI_RESET;
                row++;
            }
        }
        
        // Footer help
        string help = "ARROWS: Nav | ENTER: Open | BACKSPACE: Up | Q: Quit";
        if (help.length() > width - 4) help = "ARROWS: Nav | ENT: Open";
        cout << "\033[" << (height) << ";3H" << ANSI_CYAN << help << ANSI_RESET;
        cout << flush;
    }

    void handleEnter() {
        const auto& target = currentEntries[selectionIndex];
        
        if (target.type == TYPE_FILE) {
            viewFile(target);
        } else if (target.type == TYPE_LEVELED_DIR) {
            enterFolder(target);
        }
    }

    void viewFile(FileEntry file) {
        int w, h;
        getConsoleSize(w, h);
        
        cout << "\033[2J\033[H"; // Clear
        drawBox(1, 1, w, h);
        cout << "\033[2;3H" << "Viewing: " << file.name;
        cout << "\033[4;3H";
        
        vector<uint64_t> chain = getChain(file.cluster);
        uint64_t remaining = file.size;
        
        uint64_t printed = 0;
        uint64_t maxPrint = w * (h-6);
        
        for (uint64_t c : chain) {
            if (remaining == 0 || printed >= maxPrint) break;
            
            for (int i=0; i<8; i++) { // 8 sectors per cluster
                 if (remaining == 0 || printed >= maxPrint) break;
                 
                 uint8_t buffer[SECTOR_SIZE];
                 disk.readSector(c*8 + i, buffer);
                 
                 uint64_t chunkSize = std::min((uint64_t)SECTOR_SIZE, remaining);
                 
                 for (uint64_t k=0; k<chunkSize && printed < maxPrint; k++) {
                     char ch = (char)buffer[k];
                     if (ch == '\n') cout << "\033[E\033[2C";
                     else cout << ch;
                     printed++;
                 }
                 remaining -= chunkSize;
            }
        }
        cout << "\n\n\033[E\033[3H" << ANSI_REVERSE << " Press any key " << ANSI_RESET;
        _getch();
    }

    void enterFolder(FileEntry folder) {
        vector<string> levels;
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        
        for (int i=0; i<8; i++) {
            disk.readSector(folder.cluster * 8 + i, vps);
            for (int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                if (vps[j].isActive) {
                    levels.push_back(vps[j].versionName);
                }
            }
        }

        if (levels.empty()) return;

        int verSel = 0;
        while(true) {
            // Draw popup over existing
            int boxX = 15, boxY = 5, boxW = 30;
            int boxH = levels.size() + 4;
            drawBox(boxX, boxY, boxW, boxH);
            cout << "\033[" << (boxY+1) << ";" << (boxX+2) << "H" << "Select Level:";
            
            for(size_t i=0; i<levels.size(); i++) {
                cout << "\033[" << (boxY+3+i) << ";" << (boxX+2) << "H";
                if (i == verSel) cout << ANSI_REVERSE << " " << levels[i] << " " << ANSI_RESET;
                else cout << " " << levels[i] << " ";
            }

            int key = _getch();
            if (key == 224) {
                 key = _getch();
                 if (key == KEY_UP && verSel > 0) verSel--;
                 if (key == KEY_DOWN && verSel < levels.size()-1) verSel++;
            } else if (key == KEY_ENTER) {
                pushContext(folder.cluster, levels[verSel], folder.name);
                break;
            } else if (key == KEY_ESC) {
                break;
            }
        }
    }
    
    struct ContextState {
        uint64_t dirCluster;
        uint64_t contentCluster;
        string path;
        string version;
    };
    vector<ContextState> history;

    void pushContext(uint64_t newDirCluster, string newVer, string folderName) {
        ContextState state = {ctx.dirCluster, ctx.contentCluster, ctx.path, ctx.version};
        history.push_back(state);
        
        ctx.dirCluster = newDirCluster;
        VersionEntry vps[SECTOR_SIZE/sizeof(VersionEntry)];
        bool found = false;
        for (int i=0; i<8; i++) {
            disk.readSector(ctx.dirCluster * 8 + i, vps);
            for(int j=0; j<SECTOR_SIZE/sizeof(VersionEntry); j++) {
                if(vps[j].isActive && string(vps[j].versionName) == newVer) {
                    ctx.contentCluster = vps[j].contentTableCluster;
                    ctx.version = newVer;
                    found = true;
                    break;
                }
            }
            if(found) break;
        }
        
        if (ctx.path == "/") ctx.path += folderName;
        else ctx.path += "/" + folderName;
        
        refreshEntries();
    }

    void navigateUp() {
        if (history.empty()) return;
        ContextState back = history.back();
        history.pop_back();
        
        ctx.dirCluster = back.dirCluster;
        ctx.contentCluster = back.contentCluster;
        ctx.path = back.path;
        ctx.version = back.version;
        
        refreshEntries();
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "Usage: level.exe <image_path>" << endl;
        return 1;
    }

    LevelExplorer explorer;
    if (explorer.mount(argv[1])) {
        explorer.run();
    }
    return 0;
}
