/*
 * BitPainter - Pixel Sprite Editor
 * Compile: g++ -o bitpainter.exe bitpainter.cpp -lgdi32 -lcomdlg32 -mwindows
 * Run: ./bitpainter.exe
 */

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <stack>

#define IDM_NEW         1001
#define IDM_OPEN        1002
#define IDM_SAVE        1003
#define IDM_SAVEAS      1004
#define IDM_EXIT        1005
#define IDM_UNDO        1006
#define IDM_REDO        1007
#define IDM_RESIZE      1008
#define IDM_GRID        1009
#define IDM_TRANSPARENT_BG 1010
#define IDM_TOOL_PENCIL 2001
#define IDM_TOOL_ERASER 2002
#define IDM_TOOL_FILL   2003
#define IDM_ZOOM_IN     3001
#define IDM_ZOOM_OUT    3002
#define IDM_ZOOM_MODE   3003
#define IDC_CANVAS      4001

#define TRANSPARENT_COLOR 0xFFFFFFFF

const COLORREF BASIC_COLORS[16] = {
    RGB(0, 0, 0),       RGB(128, 128, 128), RGB(128, 0, 0),     RGB(128, 128, 0),
    RGB(0, 128, 0),     RGB(0, 128, 128),   RGB(0, 0, 128),     RGB(128, 0, 128),
    RGB(255, 255, 255), RGB(192, 192, 192), RGB(255, 0, 0),     RGB(255, 255, 0),
    RGB(0, 255, 0),     RGB(0, 255, 255),   RGB(0, 0, 255),     RGB(255, 0, 255)
};

enum Tool { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL };

struct CanvasState {
    std::vector<COLORREF> pixels;
    int width, height;
};

class Canvas {
public:
    int width = 32;
    int height = 32;
    std::vector<COLORREF> pixels;
    std::stack<CanvasState> undoStack;
    std::stack<CanvasState> redoStack;

    Canvas() { clear(); }

    void resize(int w, int h, bool transparent = false) {
        COLORREF fillColor = transparent ? TRANSPARENT_COLOR : RGB(255, 255, 255);
        std::vector<COLORREF> newPixels(w * h, fillColor);
        for (int y = 0; y < (height < h ? height : h); y++) {
            for (int x = 0; x < (width < w ? width : w); x++) {
                newPixels[y * w + x] = pixels[y * width + x];
            }
        }
        width = w;
        height = h;
        pixels = newPixels;
    }

    void clear(bool transparent = false) {
        COLORREF bgColor = transparent ? TRANSPARENT_COLOR : RGB(255, 255, 255);
        pixels.assign(width * height, bgColor);
        while (!undoStack.empty()) undoStack.pop();
        while (!redoStack.empty()) redoStack.pop();
    }

    void clearTransparent() {
        pixels.assign(width * height, TRANSPARENT_COLOR);
        while (!undoStack.empty()) undoStack.pop();
        while (!redoStack.empty()) redoStack.pop();
    }

    void saveState() {
        CanvasState state = {pixels, width, height};
        undoStack.push(state);
        while (!redoStack.empty()) redoStack.pop();
        if (undoStack.size() > 50) {
            std::stack<CanvasState> temp;
            while (undoStack.size() > 1) {
                temp.push(undoStack.top());
                undoStack.pop();
            }
            undoStack.pop();
            while (!temp.empty()) {
                undoStack.push(temp.top());
                temp.pop();
            }
        }
    }

    bool undo() {
        if (undoStack.empty()) return false;
        CanvasState current = {pixels, width, height};
        redoStack.push(current);
        CanvasState state = undoStack.top();
        undoStack.pop();
        pixels = state.pixels;
        width = state.width;
        height = state.height;
        return true;
    }

    bool redo() {
        if (redoStack.empty()) return false;
        CanvasState current = {pixels, width, height};
        undoStack.push(current);
        CanvasState state = redoStack.top();
        redoStack.pop();
        pixels = state.pixels;
        width = state.width;
        height = state.height;
        return true;
    }

    void setPixel(int x, int y, COLORREF color) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            pixels[y * width + x] = color;
    }

    COLORREF getPixel(int x, int y) const {
        if (x >= 0 && x < width && y >= 0 && y < height)
            return pixels[y * width + x];
        return RGB(255, 255, 255);
    }

    void floodFill(int x, int y, COLORREF newColor) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        COLORREF oldColor = getPixel(x, y);
        if (oldColor == newColor) return;
        std::vector<std::pair<int, int>> stack;
        stack.push_back({x, y});
        while (!stack.empty()) {
            auto [cx, cy] = stack.back();
            stack.pop_back();
            if (cx < 0 || cx >= width || cy < 0 || cy >= height) continue;
            if (getPixel(cx, cy) != oldColor) continue;
            setPixel(cx, cy, newColor);
            stack.push_back({cx + 1, cy});
            stack.push_back({cx - 1, cy});
            stack.push_back({cx, cy + 1});
            stack.push_back({cx, cy - 1});
        }
    }
};

Canvas canvas;
Tool currentTool = TOOL_PENCIL;
COLORREF currentColor = RGB(0, 0, 0);
bool showGrid = false;
bool useTransparentBg = false;
int zoomLevel = 10;
bool isDrawing = false;
wchar_t currentFilePath[MAX_PATH] = {0};
bool isModified = false;
int paletteHeight = 40;
int toolbarHeight = 30;
HWND hMainWnd;
int scrollX = 0;
int scrollY = 0;
bool zoomMode = false;

void UpdateScrollbars() {
    RECT clientRect;
    GetClientRect(hMainWnd, &clientRect);
    int canvasAreaTop = toolbarHeight;
    int canvasAreaBottom = clientRect.bottom - paletteHeight;
    int canvasAreaHeight = canvasAreaBottom - canvasAreaTop;
    int canvasAreaWidth = clientRect.right;
    int pixelSize = zoomLevel;
    int canvasWidth = canvas.width * pixelSize;
    int canvasHeight = canvas.height * pixelSize;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (canvasWidth > canvasAreaWidth) {
        si.nMin = 0;
        si.nMax = canvasWidth;
        si.nPage = canvasAreaWidth;
        if (scrollX > canvasWidth - canvasAreaWidth) scrollX = canvasWidth - canvasAreaWidth;
        if (scrollX < 0) scrollX = 0;
        si.nPos = scrollX;
        SetScrollInfo(hMainWnd, SB_HORZ, &si, TRUE);
        EnableScrollBar(hMainWnd, SB_HORZ, ESB_ENABLE_BOTH);
    } else {
        scrollX = 0;
        si.nMin = 0;
        si.nMax = 0;
        si.nPage = 0;
        si.nPos = 0;
        SetScrollInfo(hMainWnd, SB_HORZ, &si, TRUE);
        EnableScrollBar(hMainWnd, SB_HORZ, ESB_DISABLE_BOTH);
    }
    if (canvasHeight > canvasAreaHeight) {
        si.nMin = 0;
        si.nMax = canvasHeight;
        si.nPage = canvasAreaHeight;
        if (scrollY > canvasHeight - canvasAreaHeight) scrollY = canvasHeight - canvasAreaHeight;
        if (scrollY < 0) scrollY = 0;
        si.nPos = scrollY;
        SetScrollInfo(hMainWnd, SB_VERT, &si, TRUE);
        EnableScrollBar(hMainWnd, SB_VERT, ESB_ENABLE_BOTH);
    } else {
        scrollY = 0;
        si.nMin = 0;
        si.nMax = 0;
        si.nPage = 0;
        si.nPos = 0;
        SetScrollInfo(hMainWnd, SB_VERT, &si, TRUE);
        EnableScrollBar(hMainWnd, SB_VERT, ESB_DISABLE_BOTH);
    }
}

void UpdateTitle() {
    wchar_t title[MAX_PATH + 50];
    const wchar_t* filename = currentFilePath[0] ? wcsrchr(currentFilePath, L'\\') : nullptr;
    if (filename) filename++; else filename = L"Untitled";
    swprintf(title, MAX_PATH + 50, L"BitPainter - %s%s", filename, isModified ? L" *" : L"");
    SetWindowTextW(hMainWnd, title);
}

int selectedColorIndex = -1;

void DrawPalette(HDC hdc, RECT& clientRect) {
    int colorWidth = 28;
    int startX = 10;
    int startY = clientRect.bottom - paletteHeight + 5;
    for (int i = 0; i < 16; i++) {
        RECT r = {startX + i * (colorWidth + 3), startY, startX + i * (colorWidth + 3) + colorWidth, startY + 28};
        bool isSelected = (BASIC_COLORS[i] == currentColor);
        if (isSelected) {
            HPEN glowPen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
            HPEN oldPen = (HPEN)SelectObject(hdc, glowPen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, r.left - 4, r.top - 4, r.right + 4, r.bottom + 4);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(glowPen);
            HPEN accentPen = CreatePen(PS_SOLID, 2, RGB(0, 200, 255));
            oldPen = (HPEN)SelectObject(hdc, accentPen);
            oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, r.left - 2, r.top - 2, r.right + 2, r.bottom + 2);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(accentPen);
            selectedColorIndex = i;
        }
        HBRUSH brush = CreateSolidBrush(BASIC_COLORS[i]);
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
        HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(borderPen);
    }
    RECT customBtn = {startX + 16 * (colorWidth + 3) + 15, startY, startX + 16 * (colorWidth + 3) + 90, startY + 28};
    HBRUSH customBrush = CreateSolidBrush(currentColor);
    FillRect(hdc, &customBtn, customBrush);
    DeleteObject(customBrush);
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, customBtn.left, customBtn.top, customBtn.right, customBtn.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(borderPen);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, (GetRValue(currentColor) + GetGValue(currentColor) + GetBValue(currentColor)) / 3 > 128 ? RGB(0,0,0) : RGB(255,255,255));
    DrawTextW(hdc, L"Custom", -1, &customBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawToolbar(HDC hdc, RECT& clientRect) {
    const wchar_t* tools[] = {L"Pencil", L"Eraser", L"Fill"};
    int toolWidth = 60;
    int startX = 10;
    int startY = 5;
    for (int i = 0; i < 3; i++) {
        RECT r = {startX + i * (toolWidth + 5), startY, startX + i * (toolWidth + 5) + toolWidth, startY + 22};
        HBRUSH brush = CreateSolidBrush((i == (int)currentTool) ? RGB(200, 220, 255) : RGB(240, 240, 240));
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
        FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        DrawTextW(hdc, tools[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    wchar_t zoomText[32];
    swprintf(zoomText, 32, L"Zoom: %dx", zoomLevel);
    RECT zoomRect = {startX + 3 * (toolWidth + 5) + 20, startY, startX + 3 * (toolWidth + 5) + 120, startY + 22};
    SetTextColor(hdc, RGB(0, 0, 0));
    DrawTextW(hdc, zoomText, -1, &zoomRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    wchar_t sizeText[32];
    swprintf(sizeText, 32, L"Size: %dx%d", canvas.width, canvas.height);
    RECT sizeRect = {startX + 3 * (toolWidth + 5) + 130, startY, startX + 3 * (toolWidth + 5) + 250, startY + 22};
    DrawTextW(hdc, sizeText, -1, &sizeRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void DrawCheckerboard(HDC hdc, int x, int y, int size) {
    int checkSize = size / 2;
    if (checkSize < 2) checkSize = 2;
    COLORREF light = RGB(255, 255, 255);
    COLORREF dark = RGB(200, 200, 200);
    HBRUSH lightBrush = CreateSolidBrush(light);
    HBRUSH darkBrush = CreateSolidBrush(dark);
    RECT r1 = {x, y, x + checkSize, y + checkSize};
    RECT r2 = {x + checkSize, y, x + size, y + checkSize};
    RECT r3 = {x, y + checkSize, x + checkSize, y + size};
    RECT r4 = {x + checkSize, y + checkSize, x + size, y + size};
    FillRect(hdc, &r1, lightBrush);
    FillRect(hdc, &r2, darkBrush);
    FillRect(hdc, &r3, darkBrush);
    FillRect(hdc, &r4, lightBrush);
    DeleteObject(lightBrush);
    DeleteObject(darkBrush);
}

void DrawCanvas(HDC hdc, RECT& clientRect) {
    int canvasAreaTop = toolbarHeight;
    int canvasAreaBottom = clientRect.bottom - paletteHeight;
    int canvasAreaHeight = canvasAreaBottom - canvasAreaTop;
    int canvasAreaWidth = clientRect.right;
    int pixelSize = zoomLevel;
    int canvasWidth = canvas.width * pixelSize;
    int canvasHeight = canvas.height * pixelSize;
    int offsetX = (canvasAreaWidth - canvasWidth) / 2;
    int offsetY = canvasAreaTop + (canvasAreaHeight - canvasHeight) / 2;
    if (canvasWidth > canvasAreaWidth) {
        offsetX = -scrollX;
    }
    if (canvasHeight > canvasAreaHeight) {
        offsetY = canvasAreaTop - scrollY;
    }
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    HBRUSH bgBrush = CreateSolidBrush(RGB(64, 64, 64));
    RECT canvasArea = {0, canvasAreaTop, clientRect.right, canvasAreaBottom};
    FillRect(memDC, &canvasArea, bgBrush);
    DeleteObject(bgBrush);
    for (int y = 0; y < canvas.height; y++) {
        for (int x = 0; x < canvas.width; x++) {
            COLORREF pixel = canvas.getPixel(x, y);
            int px = offsetX + x * pixelSize;
            int py = offsetY + y * pixelSize;
            if (pixel == TRANSPARENT_COLOR) {
                DrawCheckerboard(memDC, px, py, pixelSize);
            } else {
                RECT r = {px, py, px + pixelSize, py + pixelSize};
                HBRUSH brush = CreateSolidBrush(pixel);
                FillRect(memDC, &r, brush);
                DeleteObject(brush);
            }
        }
    }
    if (showGrid && pixelSize >= 4) {
        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
        HPEN oldPen = (HPEN)SelectObject(memDC, gridPen);
        for (int x = 0; x <= canvas.width; x++) {
            MoveToEx(memDC, offsetX + x * pixelSize, offsetY, NULL);
            LineTo(memDC, offsetX + x * pixelSize, offsetY + canvasHeight);
        }
        for (int y = 0; y <= canvas.height; y++) {
            MoveToEx(memDC, offsetX, offsetY + y * pixelSize, NULL);
            LineTo(memDC, offsetX + canvasWidth, offsetY + y * pixelSize);
        }
        SelectObject(memDC, oldPen);
        DeleteObject(gridPen);
    }
    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, offsetX - 1, offsetY - 1, offsetX + canvasWidth + 1, offsetY + canvasHeight + 1);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(borderPen);
    BitBlt(hdc, 0, canvasAreaTop, clientRect.right, canvasAreaHeight, memDC, 0, canvasAreaTop, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

bool GetCanvasCoords(LPARAM lParam, RECT& clientRect, int& outX, int& outY) {
    int mx = LOWORD(lParam);
    int my = HIWORD(lParam);
    int canvasAreaTop = toolbarHeight;
    int canvasAreaBottom = clientRect.bottom - paletteHeight;
    int canvasAreaHeight = canvasAreaBottom - canvasAreaTop;
    int canvasAreaWidth = clientRect.right;
    int pixelSize = zoomLevel;
    int canvasWidth = canvas.width * pixelSize;
    int canvasHeight = canvas.height * pixelSize;
    int offsetX = (canvasAreaWidth - canvasWidth) / 2;
    int offsetY = canvasAreaTop + (canvasAreaHeight - canvasHeight) / 2;
    if (canvasWidth > canvasAreaWidth) {
        offsetX = -scrollX;
    }
    if (canvasHeight > canvasAreaHeight) {
        offsetY = canvasAreaTop - scrollY;
    }
    outX = (mx - offsetX) / pixelSize;
    outY = (my - offsetY) / pixelSize;
    return outX >= 0 && outX < canvas.width && outY >= 0 && outY < canvas.height;
}

int GetPaletteColorIndex(LPARAM lParam, RECT& clientRect) {
    int mx = LOWORD(lParam);
    int my = HIWORD(lParam);
    int colorWidth = 28;
    int startX = 10;
    int startY = clientRect.bottom - paletteHeight + 5;
    if (my >= startY && my <= startY + 28) {
        for (int i = 0; i < 16; i++) {
            int left = startX + i * (colorWidth + 3);
            int right = left + colorWidth;
            if (mx >= left && mx <= right) return i;
        }
        int customLeft = startX + 16 * (colorWidth + 3) + 15;
        int customRight = customLeft + 75;
        if (mx >= customLeft && mx <= customRight) return 16;
    }
    return -1;
}

int GetToolbarToolIndex(LPARAM lParam) {
    int mx = LOWORD(lParam);
    int my = HIWORD(lParam);
    int toolWidth = 60;
    int startX = 10;
    int startY = 5;
    if (my >= startY && my <= startY + 22) {
        for (int i = 0; i < 3; i++) {
            int left = startX + i * (toolWidth + 5);
            int right = left + toolWidth;
            if (mx >= left && mx <= right) return i;
        }
    }
    return -1;
}

bool SaveBMP(const wchar_t* filename) {
    FILE* f = _wfopen(filename, L"wb");
    if (!f) return false;
    bool hasTransparency = false;
    for (int i = 0; i < (int)canvas.pixels.size(); i++) {
        if (canvas.pixels[i] == TRANSPARENT_COLOR) { hasTransparency = true; break; }
    }
    if (hasTransparency) {
        int rowSize = canvas.width * 4;
        int dataSize = rowSize * canvas.height;
        BITMAPFILEHEADER bfh = {};
        bfh.bfType = 0x4D42;
        bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dataSize;
        bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        BITMAPINFOHEADER bih = {};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = canvas.width;
        bih.biHeight = canvas.height;
        bih.biPlanes = 1;
        bih.biBitCount = 32;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = dataSize;
        fwrite(&bfh, sizeof(bfh), 1, f);
        fwrite(&bih, sizeof(bih), 1, f);
        std::vector<unsigned char> row(rowSize);
        for (int y = canvas.height - 1; y >= 0; y--) {
            for (int x = 0; x < canvas.width; x++) {
                COLORREF c = canvas.getPixel(x, y);
                if (c == TRANSPARENT_COLOR) {
                    row[x * 4 + 0] = 0;
                    row[x * 4 + 1] = 0;
                    row[x * 4 + 2] = 0;
                    row[x * 4 + 3] = 0;
                } else {
                    row[x * 4 + 0] = GetBValue(c);
                    row[x * 4 + 1] = GetGValue(c);
                    row[x * 4 + 2] = GetRValue(c);
                    row[x * 4 + 3] = 255;
                }
            }
            fwrite(row.data(), 1, rowSize, f);
        }
    } else {
        int rowSize = ((canvas.width * 3 + 3) / 4) * 4;
        int dataSize = rowSize * canvas.height;
        BITMAPFILEHEADER bfh = {};
        bfh.bfType = 0x4D42;
        bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dataSize;
        bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        BITMAPINFOHEADER bih = {};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = canvas.width;
        bih.biHeight = canvas.height;
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = dataSize;
        fwrite(&bfh, sizeof(bfh), 1, f);
        fwrite(&bih, sizeof(bih), 1, f);
        std::vector<unsigned char> row(rowSize);
        for (int y = canvas.height - 1; y >= 0; y--) {
            for (int x = 0; x < canvas.width; x++) {
                COLORREF c = canvas.getPixel(x, y);
                row[x * 3 + 0] = GetBValue(c);
                row[x * 3 + 1] = GetGValue(c);
                row[x * 3 + 2] = GetRValue(c);
            }
            fwrite(row.data(), 1, rowSize, f);
        }
    }
    fclose(f);
    return true;
}

bool LoadBMP(const wchar_t* filename) {
    FILE* f = _wfopen(filename, L"rb");
    if (!f) return false;
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    if (fread(&bfh, sizeof(bfh), 1, f) != 1 || bfh.bfType != 0x4D42) { fclose(f); return false; }
    if (fread(&bih, sizeof(bih), 1, f) != 1) { fclose(f); return false; }
    if (bih.biCompression != BI_RGB) { fclose(f); return false; }
    if (bih.biBitCount != 24 && bih.biBitCount != 32) { fclose(f); return false; }
    
    canvas.width = bih.biWidth;
    canvas.height = bih.biHeight < 0 ? -bih.biHeight : bih.biHeight;
    canvas.pixels.resize(canvas.width * canvas.height);
    
    bool is32bit = (bih.biBitCount == 32);
    int bytesPerPixel = is32bit ? 4 : 3;
    int rowSize = is32bit ? (canvas.width * 4) : (((canvas.width * 3 + 3) / 4) * 4);
    std::vector<unsigned char> row(rowSize);
    fseek(f, bfh.bfOffBits, SEEK_SET);
    bool bottomUp = bih.biHeight > 0;
    
    for (int i = 0; i < canvas.height; i++) {
        int y = bottomUp ? (canvas.height - 1 - i) : i;
        if (fread(row.data(), 1, rowSize, f) != (size_t)rowSize) break;
        for (int x = 0; x < canvas.width; x++) {
            int offset = x * bytesPerPixel;
            unsigned char b = row[offset + 0];
            unsigned char g = row[offset + 1];
            unsigned char r = row[offset + 2];
            unsigned char a = is32bit ? row[offset + 3] : 255;
            if (a == 0) {
                canvas.setPixel(x, y, TRANSPARENT_COLOR);
            } else {
                canvas.setPixel(x, y, RGB(r, g, b));
            }
        }
    }
    fclose(f);
    while (!canvas.undoStack.empty()) canvas.undoStack.pop();
    while (!canvas.redoStack.empty()) canvas.redoStack.pop();
    return true;
}

bool PromptSaveChanges() {
    if (!isModified) return true;
    int result = MessageBoxW(hMainWnd, L"Save changes to current file?", L"BitPainter", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (result == IDCANCEL) return false;
    if (result == IDYES) {
        if (currentFilePath[0]) {
            if (!SaveBMP(currentFilePath)) {
                MessageBoxW(hMainWnd, L"Failed to save file!", L"Error", MB_OK | MB_ICONERROR);
                return false;
            }
        } else {
            OPENFILENAMEW ofn = {};
            wchar_t filename[MAX_PATH] = L"sprite.bmp";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hMainWnd;
            ofn.lpstrFilter = L"BMP Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"bmp";
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (!GetSaveFileNameW(&ofn)) return false;
            if (!SaveBMP(filename)) {
                MessageBoxW(hMainWnd, L"Failed to save file!", L"Error", MB_OK | MB_ICONERROR);
                return false;
            }
            wcscpy(currentFilePath, filename);
        }
        isModified = false;
        UpdateTitle();
    }
    return true;
}

INT_PTR CALLBACK ResizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int* pWidth;
    static int* pHeight;
    switch (msg) {
        case WM_INITDIALOG: {
            int* sizes = (int*)lParam;
            pWidth = &sizes[0];
            pHeight = &sizes[1];
            SetDlgItemInt(hDlg, 101, *pWidth, FALSE);
            SetDlgItemInt(hDlg, 102, *pHeight, FALSE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                *pWidth = GetDlgItemInt(hDlg, 101, NULL, FALSE);
                *pHeight = GetDlgItemInt(hDlg, 102, NULL, FALSE);
                if (*pWidth < 1) *pWidth = 1;
                if (*pHeight < 1) *pHeight = 1;
                if (*pWidth > 256) *pWidth = 256;
                if (*pHeight > 256) *pHeight = 256;
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

void ShowResizeDialog(HWND hwnd) {
    const char* dlgTemplate =
        "\x01\x00\xFF\xFF\x00\x00\x00\x00\x00\x00\x00\x00"
        "\xC8\x00\xC8\x80"
        "\x05\x00"
        "\x0A\x00\x0A\x00\x96\x00\x50\x00"
        "\x00\x00\x00\x00"
        "R\x00""e\x00s\x00i\x00z\x00""e\x00 \x00""C\x00""a\x00n\x00v\x00""a\x00s\x00\x00\x00"
        "\x00\x00\x82\x50\x00\x00\x00\x00"
        "\x0A\x00\x14\x00\x28\x00\x0E\x00"
        "\xFF\xFF\x00\x00\x00\x00"
        "W\x00i\x00""d\x00t\x00h\x00:\x00\x00\x00"
        "\x00\x00\x80\x00\x81\x50\x00\x00\x00\x00"
        "\x32\x00\x12\x00\x32\x00\x0E\x00"
        "\x65\x00\x00\x00\x00\x00"
        "\x00\x00\x82\x50\x00\x00\x00\x00"
        "\x0A\x00\x2A\x00\x28\x00\x0E\x00"
        "\xFF\xFF\x00\x00\x00\x00"
        "H\x00""e\x00i\x00g\x00h\x00t\x00:\x00\x00\x00"
        "\x00\x00\x80\x00\x81\x50\x00\x00\x00\x00"
        "\x32\x00\x28\x00\x32\x00\x0E\x00"
        "\x66\x00\x00\x00\x00\x00"
        "\x00\x00\x01\x50\x00\x00\x00\x00"
        "\x50\x00\x40\x00\x32\x00\x0E\x00"
        "\x01\x00\x00\x00\x00\x00"
        "O\x00K\x00\x00\x00";
    int sizes[2] = {canvas.width, canvas.height};
    HWND hDlg = CreateDialogIndirectParamW(NULL, (LPCDLGTEMPLATEW)dlgTemplate, hwnd, NULL, 0);
    if (!hDlg) {
        wchar_t input[64];
        swprintf(input, 64, L"%d", canvas.width);
        RECT r = {0, 0, 200, 100};
        int newWidth = canvas.width, newHeight = canvas.height;
        wchar_t msg[128];
        swprintf(msg, 128, L"Enter new width (current: %d):", canvas.width);
        if (MessageBoxW(hwnd, msg, L"Resize Canvas", MB_OKCANCEL) == IDOK) {
            newWidth = 32;
        }
        swprintf(msg, 128, L"Enter new height (current: %d):", canvas.height);
        if (MessageBoxW(hwnd, msg, L"Resize Canvas", MB_OKCANCEL) == IDOK) {
            newHeight = 32;
        }
        if (newWidth >= 1 && newWidth <= 256 && newHeight >= 1 && newHeight <= 256) {
            canvas.saveState();
            canvas.resize(newWidth, newHeight, useTransparentBg);
            isModified = true;
            UpdateTitle();
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return;
    }
}

HWND hWidthEdit = NULL;
HWND hHeightEdit = NULL;
HWND hResizeDlg = NULL;

LRESULT CALLBACK ResizeInputDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowW(L"STATIC", L"Width (1-512):", WS_CHILD | WS_VISIBLE, 20, 20, 100, 20, hwnd, NULL, NULL, NULL);
            hWidthEdit = CreateWindowW(L"EDIT", L"32", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 130, 18, 80, 24, hwnd, (HMENU)101, NULL, NULL);
            CreateWindowW(L"STATIC", L"Height (1-512):", WS_CHILD | WS_VISIBLE, 20, 55, 100, 20, hwnd, NULL, NULL, NULL);
            hHeightEdit = CreateWindowW(L"EDIT", L"32", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 130, 53, 80, 24, hwnd, (HMENU)102, NULL, NULL);
            CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 50, 95, 80, 28, hwnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 140, 95, 80, 28, hwnd, (HMENU)IDCANCEL, NULL, NULL);
            wchar_t buf[16];
            swprintf(buf, 16, L"%d", canvas.width);
            SetWindowTextW(hWidthEdit, buf);
            swprintf(buf, 16, L"%d", canvas.height);
            SetWindowTextW(hHeightEdit, buf);
            SetFocus(hWidthEdit);
            SendMessage(hWidthEdit, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDOK) {
                wchar_t buf[16];
                GetWindowTextW(hWidthEdit, buf, 16);
                int newWidth = _wtoi(buf);
                GetWindowTextW(hHeightEdit, buf, 16);
                int newHeight = _wtoi(buf);
                if (newWidth < 1) newWidth = 1;
                if (newHeight < 1) newHeight = 1;
                if (newWidth > 512) newWidth = 512;
                if (newHeight > 512) newHeight = 512;
                if (newWidth != canvas.width || newHeight != canvas.height) {
                    canvas.saveState();
                    canvas.resize(newWidth, newHeight, useTransparentBg);
                    isModified = true;
                    UpdateTitle();
                    InvalidateRect(hMainWnd, NULL, TRUE);
                }
                DestroyWindow(hwnd);
                hResizeDlg = NULL;
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                hResizeDlg = NULL;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            hResizeDlg = NULL;
            return 0;
        case WM_DESTROY:
            hResizeDlg = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void DoResize(HWND hwnd) {
    if (hResizeDlg) { SetForegroundWindow(hResizeDlg); return; }
    WNDCLASSEXW wcResize = {};
    wcResize.cbSize = sizeof(wcResize);
    wcResize.lpfnWndProc = ResizeInputDlgProc;
    wcResize.hInstance = GetModuleHandle(NULL);
    wcResize.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcResize.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcResize.lpszClassName = L"ResizeDlgClass";
    RegisterClassExW(&wcResize);
    RECT parentRect;
    GetWindowRect(hwnd, &parentRect);
    int dlgWidth = 270;
    int dlgHeight = 170;
    int dlgX = parentRect.left + (parentRect.right - parentRect.left - dlgWidth) / 2;
    int dlgY = parentRect.top + (parentRect.bottom - parentRect.top - dlgHeight) / 2;
    hResizeDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ResizeDlgClass", L"Resize Canvas",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, dlgX, dlgY, dlgWidth, dlgHeight,
        hwnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(hResizeDlg, SW_SHOW);
    UpdateWindow(hResizeDlg);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    switch (msg) {
        case WM_CREATE: {
            HMENU hMenuBar = CreateMenu();
            HMENU hFileMenu = CreatePopupMenu();
            AppendMenuW(hFileMenu, MF_STRING, IDM_NEW, L"&New\tCtrl+N");
            AppendMenuW(hFileMenu, MF_STRING, IDM_OPEN, L"&Open...\tCtrl+O");
            AppendMenuW(hFileMenu, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");
            AppendMenuW(hFileMenu, MF_STRING, IDM_SAVEAS, L"Save &As...");
            AppendMenuW(hFileMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hFileMenu, MF_STRING, IDM_EXIT, L"E&xit");
            HMENU hEditMenu = CreatePopupMenu();
            AppendMenuW(hEditMenu, MF_STRING, IDM_UNDO, L"&Undo\tCtrl+Z");
            AppendMenuW(hEditMenu, MF_STRING, IDM_REDO, L"&Redo\tCtrl+Y");
            AppendMenuW(hEditMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hEditMenu, MF_STRING, IDM_RESIZE, L"&Resize Canvas...");
            HMENU hViewMenu = CreatePopupMenu();
            AppendMenuW(hViewMenu, MF_STRING, IDM_GRID, L"Show &Grid\tG");
            AppendMenuW(hViewMenu, MF_STRING, IDM_TRANSPARENT_BG, L"&Transparent Background\tT");
            AppendMenuW(hViewMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_MODE, L"&Zoom Mode\tM");
            AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_IN, L"Zoom &In\t+");
            AppendMenuW(hViewMenu, MF_STRING, IDM_ZOOM_OUT, L"Zoom &Out\t-");
            HMENU hToolMenu = CreatePopupMenu();
            AppendMenuW(hToolMenu, MF_STRING, IDM_TOOL_PENCIL, L"&Pencil\t1");
            AppendMenuW(hToolMenu, MF_STRING, IDM_TOOL_ERASER, L"&Eraser\t2");
            AppendMenuW(hToolMenu, MF_STRING, IDM_TOOL_FILL, L"&Fill\t3");
            AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"&File");
            AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hEditMenu, L"&Edit");
            AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hViewMenu, L"&View");
            AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hToolMenu, L"&Tools");
            SetMenu(hwnd, hMenuBar);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HBRUSH toolbarBrush = CreateSolidBrush(RGB(240, 240, 240));
            RECT toolbarRect = {0, 0, clientRect.right, toolbarHeight};
            FillRect(hdc, &toolbarRect, toolbarBrush);
            DeleteObject(toolbarBrush);
            HBRUSH paletteBrush = CreateSolidBrush(RGB(50, 50, 50));
            RECT paletteRect = {0, clientRect.bottom - paletteHeight, clientRect.right, clientRect.bottom};
            FillRect(hdc, &paletteRect, paletteBrush);
            DeleteObject(paletteBrush);
            DrawToolbar(hdc, clientRect);
            DrawCanvas(hdc, clientRect);
            DrawPalette(hdc, clientRect);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int colorIdx = GetPaletteColorIndex(lParam, clientRect);
            if (colorIdx >= 0 && colorIdx < 16) {
                currentColor = BASIC_COLORS[colorIdx];
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (colorIdx == 16) {
                CHOOSECOLORW cc = {};
                static COLORREF customColors[16] = {0};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = currentColor;
                cc.lpCustColors = customColors;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    currentColor = cc.rgbResult;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
            int toolIdx = GetToolbarToolIndex(lParam);
            if (toolIdx >= 0) {
                currentTool = (Tool)toolIdx;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            int cx, cy;
            if (GetCanvasCoords(lParam, clientRect, cx, cy)) {
                canvas.saveState();
                isDrawing = true;
                SetCapture(hwnd);
                if (currentTool == TOOL_PENCIL) {
                    canvas.setPixel(cx, cy, currentColor);
                    isModified = true;
                } else if (currentTool == TOOL_ERASER) {
                    if (canvas.getPixel(cx, cy) != TRANSPARENT_COLOR) {
                        canvas.setPixel(cx, cy, useTransparentBg ? TRANSPARENT_COLOR : RGB(255, 255, 255));
                        isModified = true;
                    }
                } else if (currentTool == TOOL_FILL) {
                    canvas.floodFill(cx, cy, currentColor);
                    isModified = true;
                    isDrawing = false;
                    ReleaseCapture();
                }
                UpdateTitle();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (isDrawing && (wParam & MK_LBUTTON)) {
                int cx, cy;
                if (GetCanvasCoords(lParam, clientRect, cx, cy)) {
                    if (currentTool == TOOL_PENCIL) {
                        canvas.setPixel(cx, cy, currentColor);
                        isModified = true;
                    } else if (currentTool == TOOL_ERASER) {
                        if (canvas.getPixel(cx, cy) != TRANSPARENT_COLOR) {
                            canvas.setPixel(cx, cy, useTransparentBg ? TRANSPARENT_COLOR : RGB(255, 255, 255));
                            isModified = true;
                        }
                    }
                    UpdateTitle();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (isDrawing) {
                isDrawing = false;
                ReleaseCapture();
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            int cx, cy;
            if (GetCanvasCoords(lParam, clientRect, cx, cy)) {
                currentColor = canvas.getPixel(cx, cy);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            bool shift = GetKeyState(VK_SHIFT) & 0x8000;
            if (zoomMode) {
                if (delta > 0 && zoomLevel < 50) zoomLevel += 2;
                if (delta < 0 && zoomLevel > 2) zoomLevel -= 2;
                UpdateScrollbars();
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (shift) {
                int scrollAmount = 30;
                if (delta > 0) scrollX -= scrollAmount;
                if (delta < 0) scrollX += scrollAmount;
                UpdateScrollbars();
                InvalidateRect(hwnd, NULL, TRUE);
            } else {
                int scrollAmount = 30;
                if (delta > 0) scrollY -= scrollAmount;
                if (delta < 0) scrollY += scrollAmount;
                UpdateScrollbars();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
        }
        case WM_HSCROLL: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_HORZ, &si);
            int oldPos = scrollX;
            switch (LOWORD(wParam)) {
                case SB_LINELEFT: scrollX -= 10; break;
                case SB_LINERIGHT: scrollX += 10; break;
                case SB_PAGELEFT: scrollX -= si.nPage; break;
                case SB_PAGERIGHT: scrollX += si.nPage; break;
                case SB_THUMBTRACK: scrollX = si.nTrackPos; break;
                case SB_THUMBPOSITION: scrollX = si.nTrackPos; break;
            }
            UpdateScrollbars();
            if (scrollX != oldPos) InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int oldPos = scrollY;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: scrollY -= 10; break;
                case SB_LINEDOWN: scrollY += 10; break;
                case SB_PAGEUP: scrollY -= si.nPage; break;
                case SB_PAGEDOWN: scrollY += si.nPage; break;
                case SB_THUMBTRACK: scrollY = si.nTrackPos; break;
                case SB_THUMBPOSITION: scrollY = si.nTrackPos; break;
            }
            UpdateScrollbars();
            if (scrollY != oldPos) InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_KEYDOWN: {
            bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
            if (ctrl && wParam == 'N') { SendMessage(hwnd, WM_COMMAND, IDM_NEW, 0); return 0; }
            if (ctrl && wParam == 'O') { SendMessage(hwnd, WM_COMMAND, IDM_OPEN, 0); return 0; }
            if (ctrl && wParam == 'S') { SendMessage(hwnd, WM_COMMAND, IDM_SAVE, 0); return 0; }
            if (ctrl && wParam == 'Z') { SendMessage(hwnd, WM_COMMAND, IDM_UNDO, 0); return 0; }
            if (ctrl && wParam == 'Y') { SendMessage(hwnd, WM_COMMAND, IDM_REDO, 0); return 0; }
            if (wParam == '1') { currentTool = TOOL_PENCIL; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (wParam == '2') { currentTool = TOOL_ERASER; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (wParam == '3') { currentTool = TOOL_FILL; InvalidateRect(hwnd, NULL, FALSE); return 0; }
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD) { SendMessage(hwnd, WM_COMMAND, IDM_ZOOM_IN, 0); return 0; }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) { SendMessage(hwnd, WM_COMMAND, IDM_ZOOM_OUT, 0); return 0; }
            if (wParam == 'G') { SendMessage(hwnd, WM_COMMAND, IDM_GRID, 0); return 0; }
            if (wParam == 'T') { SendMessage(hwnd, WM_COMMAND, IDM_TRANSPARENT_BG, 0); return 0; }
            if (wParam == 'M') { SendMessage(hwnd, WM_COMMAND, IDM_ZOOM_MODE, 0); return 0; }
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_NEW:
                    if (PromptSaveChanges()) {
                        canvas.clear(useTransparentBg);
                        currentFilePath[0] = 0;
                        isModified = false;
                        scrollX = 0;
                        scrollY = 0;
                        UpdateTitle();
                        UpdateScrollbars();
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                    break;
                case IDM_OPEN: {
                    if (!PromptSaveChanges()) break;
                    OPENFILENAMEW ofn = {};
                    wchar_t filename[MAX_PATH] = L"";
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"BMP Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = filename;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        if (LoadBMP(filename)) {
                            wcscpy(currentFilePath, filename);
                            isModified = false;
                            scrollX = 0;
                            scrollY = 0;
                            UpdateTitle();
                            UpdateScrollbars();
                            InvalidateRect(hwnd, NULL, TRUE);
                        } else {
                            MessageBoxW(hwnd, L"Failed to open file!\nOnly 24-bit BMP files are supported.", L"Error", MB_OK | MB_ICONERROR);
                        }
                    }
                    break;
                }
                case IDM_SAVE:
                    if (currentFilePath[0]) {
                        if (SaveBMP(currentFilePath)) {
                            isModified = false;
                            UpdateTitle();
                        } else {
                            MessageBoxW(hwnd, L"Failed to save file!", L"Error", MB_OK | MB_ICONERROR);
                        }
                    } else {
                        SendMessage(hwnd, WM_COMMAND, IDM_SAVEAS, 0);
                    }
                    break;
                case IDM_SAVEAS: {
                    OPENFILENAMEW ofn = {};
                    wchar_t filename[MAX_PATH] = L"sprite.bmp";
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"BMP Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = filename;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrDefExt = L"bmp";
                    ofn.Flags = OFN_OVERWRITEPROMPT;
                    if (GetSaveFileNameW(&ofn)) {
                        if (SaveBMP(filename)) {
                            wcscpy(currentFilePath, filename);
                            isModified = false;
                            UpdateTitle();
                        } else {
                            MessageBoxW(hwnd, L"Failed to save file!", L"Error", MB_OK | MB_ICONERROR);
                        }
                    }
                    break;
                }
                case IDM_EXIT:
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;
                case IDM_UNDO:
                    if (canvas.undo()) {
                        isModified = true;
                        UpdateTitle();
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                    break;
                case IDM_REDO:
                    if (canvas.redo()) {
                        isModified = true;
                        UpdateTitle();
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                    break;
                case IDM_RESIZE:
                    DoResize(hwnd);
                    break;
                case IDM_GRID:
                    showGrid = !showGrid;
                    CheckMenuItem(GetMenu(hwnd), IDM_GRID, showGrid ? MF_CHECKED : MF_UNCHECKED);
                    InvalidateRect(hwnd, NULL, TRUE);
                    break;
                case IDM_TRANSPARENT_BG:
                    useTransparentBg = !useTransparentBg;
                    CheckMenuItem(GetMenu(hwnd), IDM_TRANSPARENT_BG, useTransparentBg ? MF_CHECKED : MF_UNCHECKED);
                    break;
                case IDM_ZOOM_IN:
                    if (zoomLevel < 50) { zoomLevel += 2; UpdateScrollbars(); InvalidateRect(hwnd, NULL, TRUE); }
                    break;
                case IDM_ZOOM_OUT:
                    if (zoomLevel > 2) { zoomLevel -= 2; UpdateScrollbars(); InvalidateRect(hwnd, NULL, TRUE); }
                    break;
                case IDM_ZOOM_MODE:
                    zoomMode = !zoomMode;
                    CheckMenuItem(GetMenu(hwnd), IDM_ZOOM_MODE, zoomMode ? MF_CHECKED : MF_UNCHECKED);
                    break;
                case IDM_TOOL_PENCIL:
                    currentTool = TOOL_PENCIL;
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case IDM_TOOL_ERASER:
                    currentTool = TOOL_ERASER;
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                case IDM_TOOL_FILL:
                    currentTool = TOOL_FILL;
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
            }
            return 0;
        }
        case WM_SIZE:
            UpdateScrollbars();
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case WM_CLOSE:
            if (PromptSaveChanges()) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"BitPainterClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);
    hMainWnd = CreateWindowExW(0, L"BitPainterClass", L"BitPainter - Untitled",
        WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);
    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
