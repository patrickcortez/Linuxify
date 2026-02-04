// Compile: g++ -std=c++17 -static -o linuxify.exe main.cpp registry.cpp -lpsapi -lws2_32 -liphlpapi -lwininet -lwlanapi
#ifndef LINUXIFY_LINK_HANDLER_HPP
#define LINUXIFY_LINK_HANDLER_HPP

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>

namespace LinkHandler {

struct ScreenLink {
    std::string url;
    int row;
    int startCol;
    int endCol;
};

class LinkManager {
private:
    std::vector<ScreenLink> activeLinks;
    int hoveredLinkIndex = -1;
    std::mutex linkMutex;
    HANDLE hOut;
    bool enabled = true;

    int findCharType(char c) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
        if (c >= '0' && c <= '9') return 1;
        if (c == '-' || c == '_' || c == '.' || c == '~') return 1;
        if (c == ':' || c == '/' || c == '?' || c == '#' || c == '[' || c == ']' || c == '@') return 1;
        if (c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' || c == ')') return 1;
        if (c == '*' || c == '+' || c == ',' || c == ';' || c == '=' || c == '%') return 1;
        return 0;
    }

    bool isUrlChar(char c) {
        return findCharType(c) == 1;
    }

    std::vector<std::pair<size_t, size_t>> findUrls(const std::string& text) {
        std::vector<std::pair<size_t, size_t>> results;
        const char* prefixes[] = {"https://", "http://", "ftp://", "file:///"};
        const size_t prefixLens[] = {8, 7, 6, 8};
        
        for (int p = 0; p < 4; p++) {
            size_t pos = 0;
            while ((pos = text.find(prefixes[p], pos)) != std::string::npos) {
                size_t start = pos;
                size_t end = pos + prefixLens[p];
                
                while (end < text.length() && isUrlChar(text[end])) {
                    end++;
                }
                
                while (end > start + prefixLens[p] && (text[end-1] == '.' || text[end-1] == ',' || 
                       text[end-1] == ')' || text[end-1] == ']' || text[end-1] == '\'' || text[end-1] == '"')) {
                    end--;
                }
                
                if (end > start + prefixLens[p]) {
                    results.push_back({start, end});
                }
                pos = end;
            }
        }
        
        std::sort(results.begin(), results.end());
        return results;
    }

    void drawUnderline(int row, int startCol, int endCol, bool underline) {
        if (!hOut || hOut == INVALID_HANDLE_VALUE) return;
        
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return;
        
        COORD savedPos = csbi.dwCursorPosition;
        
        std::string escSeq;
        if (underline) {
            escSeq = "\033[4m";
        } else {
            escSeq = "\033[24m";
        }
        
        COORD pos = {(SHORT)startCol, (SHORT)row};
        SetConsoleCursorPosition(hOut, pos);
        
        std::vector<CHAR_INFO> lineBuffer(endCol - startCol);
        COORD bufSize = {(SHORT)(endCol - startCol), 1};
        COORD bufCoord = {0, 0};
        SMALL_RECT readRect = {(SHORT)startCol, (SHORT)row, (SHORT)(endCol - 1), (SHORT)row};
        
        if (ReadConsoleOutputA(hOut, lineBuffer.data(), bufSize, bufCoord, &readRect)) {
            for (auto& ci : lineBuffer) {
                if (underline) {
                    ci.Attributes |= COMMON_LVB_UNDERSCORE;
                } else {
                    ci.Attributes &= ~COMMON_LVB_UNDERSCORE;
                }
            }
            WriteConsoleOutputA(hOut, lineBuffer.data(), bufSize, bufCoord, &readRect);
        }
        
        SetConsoleCursorPosition(hOut, savedPos);
    }

public:
    static LinkManager& getInstance() {
        static LinkManager instance;
        return instance;
    }

    LinkManager() {
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() const { return enabled; }

    void detectUrls(const std::string& text, int baseRow, int baseCol) {
        if (!enabled) return;
        
        std::lock_guard<std::mutex> lock(linkMutex);
        
        auto urls = findUrls(text);
        int currentRow = baseRow;
        int currentCol = baseCol;
        
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int width = 120;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            width = csbi.dwSize.X;
        }
        
        for (const auto& urlPos : urls) {
            int urlColStart = baseCol;
            int urlRow = baseRow;
            
            for (size_t i = 0; i < urlPos.first; i++) {
                if (text[i] == '\n') {
                    urlRow++;
                    urlColStart = 0;
                } else if (text[i] == '\r') {
                    urlColStart = 0;
                } else {
                    urlColStart++;
                    if (urlColStart >= width) {
                        urlColStart = 0;
                        urlRow++;
                    }
                }
            }
            
            std::string url = text.substr(urlPos.first, urlPos.second - urlPos.first);
            int urlLen = (int)url.length();
            
            ScreenLink link;
            link.url = url;
            link.row = urlRow;
            link.startCol = urlColStart;
            link.endCol = urlColStart + urlLen;
            
            if (link.endCol > width) {
                link.endCol = width;
            }
            
            activeLinks.push_back(link);
        }
    }

    int getLinkAt(int row, int col) {
        std::lock_guard<std::mutex> lock(linkMutex);
        
        for (int i = 0; i < (int)activeLinks.size(); i++) {
            const auto& link = activeLinks[i];
            if (link.row == row && col >= link.startCol && col < link.endCol) {
                return i;
            }
        }
        return -1;
    }

    void setHover(int linkIndex) {
        if (!enabled) return;
        
        std::lock_guard<std::mutex> lock(linkMutex);
        
        if (hoveredLinkIndex == linkIndex) return;
        
        if (hoveredLinkIndex >= 0 && hoveredLinkIndex < (int)activeLinks.size()) {
            const auto& oldLink = activeLinks[hoveredLinkIndex];
            drawUnderline(oldLink.row, oldLink.startCol, oldLink.endCol, false);
        }
        
        hoveredLinkIndex = linkIndex;
        
        if (hoveredLinkIndex >= 0 && hoveredLinkIndex < (int)activeLinks.size()) {
            const auto& newLink = activeLinks[hoveredLinkIndex];
            drawUnderline(newLink.row, newLink.startCol, newLink.endCol, true);
        }
    }

    void openLink(int linkIndex) {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(linkMutex);
            if (linkIndex < 0 || linkIndex >= (int)activeLinks.size()) return;
            url = activeLinks[linkIndex].url;
        }
        
        ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }

    void clearRow(int row) {
        std::lock_guard<std::mutex> lock(linkMutex);
        
        activeLinks.erase(
            std::remove_if(activeLinks.begin(), activeLinks.end(),
                [row](const ScreenLink& link) { return link.row == row; }),
            activeLinks.end()
        );
        
        if (hoveredLinkIndex >= (int)activeLinks.size()) {
            hoveredLinkIndex = -1;
        }
    }

    void clearAllLinks() {
        std::lock_guard<std::mutex> lock(linkMutex);
        activeLinks.clear();
        hoveredLinkIndex = -1;
    }

    void shiftLinksUp(int amount) {
        std::lock_guard<std::mutex> lock(linkMutex);
        
        for (auto& link : activeLinks) {
            link.row -= amount;
        }
        
        activeLinks.erase(
            std::remove_if(activeLinks.begin(), activeLinks.end(),
                [](const ScreenLink& link) { return link.row < 0; }),
            activeLinks.end()
        );
        
        if (hoveredLinkIndex >= (int)activeLinks.size()) {
            hoveredLinkIndex = -1;
        }
    }

    void onMouseEvent(int x, int y, int eventType) {
        if (!enabled) return;
        
        int linkIdx = getLinkAt(y, x);
        
        if (eventType == 0) {
            setHover(linkIdx);
        } else if (eventType == 1 && linkIdx >= 0) {
            openLink(linkIdx);
        }
    }
};

inline LinkManager& get() {
    return LinkManager::getInstance();
}

}

#endif
