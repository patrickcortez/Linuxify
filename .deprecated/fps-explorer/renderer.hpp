#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include "file_entity.hpp"

struct SpriteOrder {
    int index;
    double dist;
};

inline bool spriteCmp(const SpriteOrder& a, const SpriteOrder& b) {
    return a.dist > b.dist;
}

struct Renderer {
    int screenW, screenH;
    HFONT nameFont;
    HFONT hudFont;
    HFONT pathFont;

    void init(int w, int h) {
        screenW = w;
        screenH = h;
        nameFont = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Consolas");
        hudFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Consolas");
        pathFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Consolas");
    }

    void cleanup() {
        if (nameFont) DeleteObject(nameFont);
        if (hudFont) DeleteObject(hudFont);
        if (pathFont) DeleteObject(pathFont);
    }

    void drawSprites(HDC hdc, HDC memDC, const std::vector<FileEntity>& entities,
                     double posX, double posY, double dirX, double dirY,
                     double planeX, double planeY, const std::vector<double>& zBuffer) {

        std::vector<SpriteOrder> order(entities.size());
        for (int i = 0; i < (int)entities.size(); i++) {
            order[i].index = i;
            double dx = entities[i].x - posX;
            double dy = entities[i].y - posY;
            order[i].dist = dx * dx + dy * dy;
        }
        std::sort(order.begin(), order.end(), spriteCmp);

        for (auto& so : order) {
            const FileEntity& ent = entities[so.index];
            double spriteX = ent.x - posX;
            double spriteY = ent.y - posY;

            double invDet = 1.0 / (planeX * dirY - dirX * planeY);
            double transformX = invDet * (dirY * spriteX - dirX * spriteY);
            double transformY = invDet * (-planeY * spriteX + planeX * spriteY);

            if (transformY <= 0.1) continue;

            int spriteScreenX = (int)((screenW / 2.0) * (1.0 + transformX / transformY));

            int spriteSize = abs((int)(screenH / transformY));
            if (spriteSize > screenH * 2) spriteSize = screenH * 2;
            int iconSize = spriteSize / 2;
            if (iconSize < 8) iconSize = 8;
            if (iconSize > 256) iconSize = 256;

            int drawStartY = -iconSize / 2 + screenH / 2;
            int drawStartX = spriteScreenX - iconSize / 2;

            if (drawStartX + iconSize < 0 || drawStartX >= screenW) continue;

            int xStart = std::max(0, drawStartX);
            int xEnd = std::min(screenW - 1, drawStartX + iconSize);

            bool visible = false;
            for (int stripe = xStart; stripe <= xEnd; stripe++) {
                if (stripe >= 0 && stripe < (int)zBuffer.size() && transformY < zBuffer[stripe]) {
                    visible = true;
                    break;
                }
            }
            if (!visible) continue;

            if (ent.iconBitmap) {
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, ent.iconBitmap);
                BLENDFUNCTION bf;
                bf.BlendOp = AC_SRC_OVER;
                bf.BlendFlags = 0;
                bf.SourceConstantAlpha = 255;
                bf.AlphaFormat = AC_SRC_ALPHA;

                AlphaBlend(hdc, drawStartX, drawStartY, iconSize, iconSize,
                           memDC, 0, 0, ent.iconW, ent.iconH, bf);
                SelectObject(memDC, oldBmp);
            } else {
                COLORREF fallback = ent.isDirectory ? RGB(255, 200, 50) : RGB(180, 180, 220);
                HBRUSH br = CreateSolidBrush(fallback);
                RECT rc = {drawStartX, drawStartY, drawStartX + iconSize, drawStartY + iconSize};
                FillRect(hdc, &rc, br);
                DeleteObject(br);
            }

            HFONT oldFont = (HFONT)SelectObject(hdc, nameFont);
            SetBkMode(hdc, TRANSPARENT);

            int textY = drawStartY - 18;
            if (textY < 0) textY = 0;

            std::wstring displayName = ent.name;
            if ((int)displayName.length() > 20) {
                displayName = displayName.substr(0, 17) + L"...";
            }

            SIZE textSize;
            GetTextExtentPoint32W(hdc, displayName.c_str(), (int)displayName.length(), &textSize);
            int textX = spriteScreenX - textSize.cx / 2;

            SetTextColor(hdc, RGB(0, 0, 0));
            TextOutW(hdc, textX + 1, textY + 1, displayName.c_str(), (int)displayName.length());

            COLORREF nameColor = ent.isDirectory ? RGB(100, 255, 100) : RGB(255, 255, 255);
            SetTextColor(hdc, nameColor);
            TextOutW(hdc, textX, textY, displayName.c_str(), (int)displayName.length());

            SelectObject(hdc, oldFont);
        }
    }

    void drawHUD(HDC hdc, const std::wstring& currentPath, bool nearDoor, const std::wstring& doorLabel,
                 bool nearFile, const std::wstring& fileLabel) {
        SetBkMode(hdc, TRANSPARENT);

        HPEN crossPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HPEN oldPen = (HPEN)SelectObject(hdc, crossPen);
        int cx = screenW / 2;
        int cy = screenH / 2;
        MoveToEx(hdc, cx - 10, cy, NULL);
        LineTo(hdc, cx + 10, cy);
        MoveToEx(hdc, cx, cy - 10, NULL);
        LineTo(hdc, cx, cy + 10);
        SelectObject(hdc, oldPen);
        DeleteObject(crossPen);

        HFONT oldFont = (HFONT)SelectObject(hdc, pathFont);
        SetTextColor(hdc, RGB(200, 200, 200));
        std::wstring pathDisplay = L"  " + currentPath;
        TextOutW(hdc, 5, 5, pathDisplay.c_str(), (int)pathDisplay.length());

        SelectObject(hdc, hudFont);

        if (nearDoor) {
            SetTextColor(hdc, RGB(100, 255, 100));
            std::wstring prompt = L"[E] Enter: " + doorLabel;
            SIZE sz;
            GetTextExtentPoint32W(hdc, prompt.c_str(), (int)prompt.length(), &sz);
            TextOutW(hdc, cx - sz.cx / 2, screenH - 60, prompt.c_str(), (int)prompt.length());
        } else if (nearFile) {
            SetTextColor(hdc, RGB(255, 255, 100));
            std::wstring prompt = L"[E] Open: " + fileLabel;
            SIZE sz;
            GetTextExtentPoint32W(hdc, prompt.c_str(), (int)prompt.length(), &sz);
            TextOutW(hdc, cx - sz.cx / 2, screenH - 60, prompt.c_str(), (int)prompt.length());
        }

        SetTextColor(hdc, RGB(150, 150, 150));
        std::wstring back = L"[BACKSPACE] Go Back    [ESC] Quit";
        SIZE sz;
        GetTextExtentPoint32W(hdc, back.c_str(), (int)back.length(), &sz);
        TextOutW(hdc, cx - sz.cx / 2, screenH - 30, back.c_str(), (int)back.length());

        SetTextColor(hdc, RGB(120, 120, 120));
        std::wstring controls = L"WASD: Move  |  Mouse: Look";
        GetTextExtentPoint32W(hdc, controls.c_str(), (int)controls.length(), &sz);
        TextOutW(hdc, cx - sz.cx / 2, screenH - 90, controls.c_str(), (int)controls.length());

        SelectObject(hdc, oldFont);
    }
};
