#pragma once
#include <windows.h>
#include <vector>
#include <cmath>
#include "map_gen.hpp"

struct RayResult {
    double perpWallDist;
    int wallType;
    int side;
    int mapX, mapY;
    double wallX;
};

struct Raycaster {
    int screenW, screenH;
    std::vector<double> zBuffer;

    void init(int w, int h) {
        screenW = w;
        screenH = h;
        zBuffer.resize(w, 1e30);
    }

    void render(UINT32* pixels, double posX, double posY, double dirX, double dirY,
                double planeX, double planeY, const std::vector<std::vector<int>>& grid,
                const Texture* wallTex, const Texture* doorTex) {

        int gridW = (int)grid.size();
        int gridH = gridW > 0 ? (int)grid[0].size() : 0;

        UINT32 ceilColor = (40 << 16) | (40 << 8) | 50;
        UINT32 floorColor = (60 << 16) | (60 << 8) | 70;
        int half = screenH / 2;
        for (int y = 0; y < half; y++)
            for (int x = 0; x < screenW; x++)
                pixels[y * screenW + x] = ceilColor;
        for (int y = half; y < screenH; y++)
            for (int x = 0; x < screenW; x++)
                pixels[y * screenW + x] = floorColor;

        for (int x = 0; x < screenW; x++) {
            double cameraX = 2.0 * x / (double)screenW - 1.0;
            double rayDirX = dirX + planeX * cameraX;
            double rayDirY = dirY + planeY * cameraX;

            int mapX = (int)posX;
            int mapY = (int)posY;

            double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1.0 / rayDirX);
            double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1.0 / rayDirY);

            double sideDistX, sideDistY;
            int stepX, stepY;

            if (rayDirX < 0) {
                stepX = -1;
                sideDistX = (posX - mapX) * deltaDistX;
            } else {
                stepX = 1;
                sideDistX = (mapX + 1.0 - posX) * deltaDistX;
            }
            if (rayDirY < 0) {
                stepY = -1;
                sideDistY = (posY - mapY) * deltaDistY;
            } else {
                stepY = 1;
                sideDistY = (mapY + 1.0 - posY) * deltaDistY;
            }

            int hit = 0;
            int side = 0;
            int cellType = 0;

            while (!hit) {
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }
                if (mapX < 0 || mapX >= gridW || mapY < 0 || mapY >= gridH) {
                    hit = 1;
                    cellType = 1;
                    break;
                }
                cellType = grid[mapX][mapY];
                if (cellType > 0) hit = 1;
            }

            double perpWallDist;
            if (side == 0)
                perpWallDist = (mapX - posX + (1 - stepX) / 2.0) / rayDirX;
            else
                perpWallDist = (mapY - posY + (1 - stepY) / 2.0) / rayDirY;

            if (perpWallDist < 0.001) perpWallDist = 0.001;
            zBuffer[x] = perpWallDist;

            int lineHeight = (int)(screenH / perpWallDist);
            int drawStart = -lineHeight / 2 + screenH / 2;
            int drawEnd = lineHeight / 2 + screenH / 2;
            if (drawStart < 0) drawStart = 0;
            if (drawEnd >= screenH) drawEnd = screenH - 1;

            double fogFactor = 1.0 / (1.0 + perpWallDist * 0.08);
            double sideDim = side == 1 ? 0.7 : 1.0;

            const Texture* tex = (cellType == 2 && doorTex && doorTex->w > 0) ? doorTex :
                                 (wallTex && wallTex->w > 0) ? wallTex : nullptr;

            if (tex) {
                double wallX;
                if (side == 0)
                    wallX = posY + perpWallDist * rayDirY;
                else
                    wallX = posX + perpWallDist * rayDirX;
                wallX -= floor(wallX);
                int texX = (int)(wallX * tex->w) % tex->w;
                if (texX < 0) texX += tex->w;
                for (int y = drawStart; y <= drawEnd; y++) {
                    int d = y * 256 - screenH * 128 + lineHeight * 128;
                    int texY = ((d * tex->h) / lineHeight) / 256;
                    if (texY < 0) texY = 0;
                    if (texY >= tex->h) texY = tex->h - 1;
                    UINT32 tc = tex->pixels[texY * tex->w + texX];
                    int r, g, b;
                    
                    // Check for transparency (alpha==0 or magic pink/black)
                    if (((tc >> 24) == 0 && (tc & 0xFFFFFF) == 0) || (tc & 0xFFFFFF) == 0xFF00FF || (tc & 0xFFFFFF) == 0x000000) {
                        r = cellType == 2 ? 50 : 130;
                        g = cellType == 2 ? 180 : 130;
                        b = cellType == 2 ? 50 : 150;
                    } else {
                        r = (tc >> 16) & 0xFF;
                        g = (tc >> 8) & 0xFF;
                        b = tc & 0xFF;
                    }

                    r = (int)(r * fogFactor * sideDim);
                    g = (int)(g * fogFactor * sideDim);
                    b = (int)(b * fogFactor * sideDim);
                    pixels[y * screenW + x] = (r << 16) | (g << 8) | b;
                }
            } else {
                int r, g, b;
                if (cellType == 2) {
                    r = 50; g = 180; b = 50;
                } else {
                    r = 130; g = 130; b = 150;
                }
                r = (int)(r * fogFactor * sideDim);
                g = (int)(g * fogFactor * sideDim);
                b = (int)(b * fogFactor * sideDim);
                UINT32 wallColor = (r << 16) | (g << 8) | b;
                for (int y = drawStart; y <= drawEnd; y++)
                    pixels[y * screenW + x] = wallColor;
            }
        }
    }
};
