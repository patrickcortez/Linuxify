#pragma once
#include <cmath>
#include <vector>

struct Player {
    double posX, posY;
    double dirX, dirY;
    double planeX, planeY;
    double moveSpeed;
    double rotSpeed;

    void init(double x, double y) {
        posX = x;
        posY = y;
        dirX = -1.0;
        dirY = 0.0;
        planeX = 0.0;
        planeY = 0.66;
        moveSpeed = 3.0;
        rotSpeed = 2.0;
    }

    void rotate(double angle) {
        double oldDirX = dirX;
        dirX = dirX * cos(angle) - dirY * sin(angle);
        dirY = oldDirX * sin(angle) + dirY * cos(angle);
        double oldPlaneX = planeX;
        planeX = planeX * cos(angle) - planeY * sin(angle);
        planeY = oldPlaneX * sin(angle) + planeY * cos(angle);
    }

    void update(float dt, const bool keys[256], const std::vector<std::vector<int>>& grid) {
        double ms = moveSpeed * dt;
        double rs = rotSpeed * dt;
        double margin = 0.25;

        if (keys['W']) {
            double nx = posX + dirX * ms;
            double ny = posY + dirY * ms;
            if (grid[(int)(nx + dirX * margin)][(int)posY] == 0 || grid[(int)(nx + dirX * margin)][(int)posY] == 2)
                posX = nx;
            if (grid[(int)posX][(int)(ny + dirY * margin)] == 0 || grid[(int)posX][(int)(ny + dirY * margin)] == 2)
                posY = ny;
        }
        if (keys['S']) {
            double nx = posX - dirX * ms;
            double ny = posY - dirY * ms;
            if (grid[(int)(nx - dirX * margin)][(int)posY] == 0 || grid[(int)(nx - dirX * margin)][(int)posY] == 2)
                posX = nx;
            if (grid[(int)posX][(int)(ny - dirY * margin)] == 0 || grid[(int)posX][(int)(ny - dirY * margin)] == 2)
                posY = ny;
        }
        if (keys['A']) {
            double strafeX = dirY;
            double strafeY = -dirX;
            double nx = posX - strafeX * ms;
            double ny = posY - strafeY * ms;
            if (grid[(int)(nx - strafeX * margin)][(int)posY] == 0 || grid[(int)(nx - strafeX * margin)][(int)posY] == 2)
                posX = nx;
            if (grid[(int)posX][(int)(ny - strafeY * margin)] == 0 || grid[(int)posX][(int)(ny - strafeY * margin)] == 2)
                posY = ny;
        }
        if (keys['D']) {
            double strafeX = dirY;
            double strafeY = -dirX;
            double nx = posX + strafeX * ms;
            double ny = posY + strafeY * ms;
            if (grid[(int)(nx + strafeX * margin)][(int)posY] == 0 || grid[(int)(nx + strafeX * margin)][(int)posY] == 2)
                posX = nx;
            if (grid[(int)posX][(int)(ny + strafeY * margin)] == 0 || grid[(int)posX][(int)(ny + strafeY * margin)] == 2)
                posY = ny;
        }
        if (keys[VK_LEFT]) rotate(-rs);
        if (keys[VK_RIGHT]) rotate(rs);
    }

    int checkDoor(const std::vector<std::vector<int>>& grid) {
        double checkDist = 0.8;
        int gx = (int)(posX + dirX * checkDist);
        int gy = (int)(posY + dirY * checkDist);
        if (gx >= 0 && gy >= 0 && gx < (int)grid.size() && gy < (int)grid[0].size()) {
            if (grid[gx][gy] == 2) {
                return gx * 10000 + gy;
            }
        }
        return -1;
    }
};
