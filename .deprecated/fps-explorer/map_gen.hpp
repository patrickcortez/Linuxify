#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include "file_entity.hpp"

namespace fs = std::filesystem;

struct Texture {
    std::vector<UINT32> pixels;
    int w, h;
    Texture() : w(0), h(0) {}
};

inline bool loadBmpTexture(const std::wstring& path, Texture& tex) {
    HBITMAP hbmp = (HBITMAP)LoadImageW(NULL, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!hbmp) return false;
    BITMAP bm;
    GetObject(hbmp, sizeof(BITMAP), &bm);
    tex.w = bm.bmWidth;
    tex.h = bm.bmHeight;
    tex.pixels.resize(tex.w * tex.h);

    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tex.w;
    bmi.bmiHeader.biHeight = -tex.h; // Negative for top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    GetDIBits(memDC, hbmp, 0, tex.h, tex.pixels.data(), &bmi, DIB_RGB_COLORS);

    // Swap R and B channels since DIBs are BGRA, and we want 0x00RRGGBB
    for (size_t i = 0; i < tex.pixels.size(); i++) {
        UINT32 p = tex.pixels[i];
        tex.pixels[i] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
    }

    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
    DeleteObject(hbmp);
    return true;
}

struct DoorInfo {
    int gridX, gridY;
    std::wstring targetPath;
};

struct RoomMap {
    std::vector<std::vector<int>> grid;
    std::vector<FileEntity> entities;
    std::vector<DoorInfo> doors;
    std::wstring currentPath;
    double spawnX, spawnY;

    void cleanup() {
        for (auto& e : entities) e.cleanup();
        entities.clear();
        doors.clear();
    }
};

inline RoomMap generateMap(const std::wstring& dirPath) {
    RoomMap room;
    room.currentPath = dirPath;

    std::vector<std::wstring> folderNames;
    std::vector<std::wstring> folderPaths;
    std::vector<std::wstring> fileNames;
    std::vector<std::wstring> filePaths;

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec)) {
        std::wstring fname = entry.path().filename().wstring();
        if (fname.empty() || fname[0] == L'.') continue;
        if (entry.is_directory(ec)) {
            folderNames.push_back(fname);
            folderPaths.push_back(entry.path().wstring());
        } else {
            fileNames.push_back(fname);
            filePaths.push_back(entry.path().wstring());
        }
    }

    int fileCount = (int)fileNames.size();
    int folderCount = (int)folderNames.size();
    int totalDoors = folderCount + 1; // +1 for parent

    int topDoors = totalDoors / 3 + (totalDoors % 3 > 0 ? 1 : 0);
    int leftDoors = totalDoors / 3 + (totalDoors % 3 > 1 ? 1 : 0);
    int rightDoors = totalDoors / 3;

    int innerW = 6;
    int innerH = 6;
    if (fileCount > 0) {
        int cols = (int)ceil(sqrt((double)fileCount));
        int rows = (fileCount + cols - 1) / cols;
        innerW = std::max(6, cols * 2 + 2);
        innerH = std::max(6, rows * 2 + 2);
    }
    innerW = std::max(innerW, topDoors * 3 + 2);
    innerH = std::max(innerH, std::max(leftDoors, rightDoors) * 3 + 2);

    int gridW = innerW + 2;
    int gridH = innerH + 2;

    room.grid.assign(gridW, std::vector<int>(gridH, 0));

    for (int x = 0; x < gridW; x++) {
        room.grid[x][0] = 1;
        room.grid[x][gridH - 1] = 1;
    }
    for (int y = 0; y < gridH; y++) {
        room.grid[0][y] = 1;
        room.grid[gridW - 1][y] = 1;
    }

    std::wstring parentPath;
    fs::path pp = fs::path(dirPath).parent_path();
    if (!pp.empty() && pp != fs::path(dirPath)) {
        parentPath = pp.wstring();
    }

    int spaceTop = std::max(3, gridW / (topDoors + 1));
    int spaceLeft = std::max(3, gridH / (leftDoors + 1));
    int spaceRight = std::max(3, gridH / (rightDoors + 1));

    int topIdx = 0, leftIdx = 0, rightIdx = 0;

    if (!parentPath.empty()) {
        int dx = std::min(spaceTop * (topIdx + 1), gridW - 2);
        dx = std::max(1, dx);
        room.grid[dx][0] = 2;
        DoorInfo di; di.gridX = dx; di.gridY = 0; di.targetPath = parentPath;
        room.doors.push_back(di);
        topIdx++;
    }

    for (int i = 0; i < folderCount; i++) {
        int dx, dy;
        // Distribute round-robin to walls that still have capacity
        if (topIdx < topDoors && (topIdx <= leftIdx || leftIdx >= leftDoors) && (topIdx <= rightIdx || rightIdx >= rightDoors)) {
            dx = std::min(spaceTop * (topIdx + 1), gridW - 2);
            dx = std::max(1, dx);
            dy = 0;
            topIdx++;
        } else if (leftIdx < leftDoors && (leftIdx <= rightIdx || rightIdx >= rightDoors)) {
            dx = 0;
            dy = std::min(spaceLeft * (leftIdx + 1), gridH - 2);
            dy = std::max(1, dy);
            leftIdx++;
        } else {
            dx = gridW - 1;
            dy = std::min(spaceRight * (rightIdx + 1), gridH - 2);
            dy = std::max(1, dy);
            rightIdx++;
        }
        
        // Ensure no overlapping on corners
        if (dx == 0 && dy == 0) { dx++; }
        if (dx == 0 && dy == gridH - 1) { dx++; }
        if (dx == gridW - 1 && dy == 0) { dx--; }
        if (dx == gridW - 1 && dy == gridH - 1) { dx--; }

        room.grid[dx][dy] = 2;
        DoorInfo di;
        di.gridX = dx;
        di.gridY = dy;
        di.targetPath = folderPaths[i];
        room.doors.push_back(di);
    }

    int cols = std::max(1, (int)ceil(sqrt((double)fileCount)));
    for (int i = 0; i < fileCount; i++) {
        int col = i % cols;
        int row = i / cols;
        FileEntity fe;
        fe.x = 2.0 + col * 2.0;
        fe.y = 2.0 + row * 2.0;
        if (fe.x >= gridW - 1) fe.x = gridW - 2.0;
        if (fe.y >= gridH - 1) fe.y = gridH - 2.0;
        fe.name = fileNames[i];
        fe.fullPath = filePaths[i];
        fe.isDirectory = false;
        fe.extractIcon(filePaths[i]);
        room.entities.push_back(fe);
    }

    for (int i = 0; i < folderCount; i++) {
        for (auto& d : room.doors) {
            if (d.targetPath == folderPaths[i]) {
                FileEntity fe;
                double fx = d.gridX;
                double fy = d.gridY;
                if (d.gridY == 0) fy = 1.5;
                else if (d.gridY == gridH - 1) fy = gridH - 2.5;
                if (d.gridX == 0) fx = 1.5;
                else if (d.gridX == gridW - 1) fx = gridW - 2.5;
                fe.x = fx;
                fe.y = fy;
                fe.name = folderNames[i];
                fe.fullPath = folderPaths[i];
                fe.isDirectory = true;
                fe.extractIcon(folderPaths[i]);
                room.entities.push_back(fe);
                break;
            }
        }
    }

    room.spawnX = gridW / 2.0;
    room.spawnY = gridH / 2.0;

    return room;
}
