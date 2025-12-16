// Compile: g++ -std=c++17 -static -I../src -o Maze3D.exe maze3d.cpp
#include "window.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <stack>
#include <ctime>

using namespace FunuxSys;

const int MAP_WIDTH = 33; // Must be odd for this algo
const int MAP_HEIGHT = 33;
const double FOV = 3.14159 / 3.0;
const double DEPTH = 16.0;

// The map needs to be mutable for generation
std::wstring mapStr; 

class Maze3D : public GraphicsApp {
private:
    double playerX = 1.5;
    double playerY = 1.5;
    double playerA = 0.0; 
    
    double enemyX = 1.5;
    double enemyY = 1.5;
    double enemySpeed = 1.2; 
    
    bool gameOver = false;
    bool victory = false;
    
    std::chrono::system_clock::time_point lastFrame;
    
    // Maze Generation Helper
    struct Cell { int x, y; };
    
    void generateMaze() {
        // Initialize full walls
        mapStr.resize(MAP_WIDTH * MAP_HEIGHT);
        for(int i=0; i<MAP_WIDTH*MAP_HEIGHT; i++) mapStr[i] = '#';
        
        std::stack<Cell> stack;
        // Start at 1,1
        Cell start = {1, 1};
        mapStr[1 * MAP_WIDTH + 1] = '.';
        stack.push(start);
        
        std::srand(std::time(nullptr));
        
        while(!stack.empty()) {
            Cell current = stack.top();
            std::vector<Cell> neighbors;
            
            // Check neighbors 2 steps away
            int dirs[4][2] = {{0,-2}, {0,2}, {-2,0}, {2,0}}; // Up, Down, Left, Right
            
            for(auto& d : dirs) {
                int nx = current.x + d[0];
                int ny = current.y + d[1];
                
                if(nx > 0 && nx < MAP_WIDTH-1 && ny > 0 && ny < MAP_HEIGHT-1) {
                    if(mapStr[ny * MAP_WIDTH + nx] == '#') {
                        neighbors.push_back({nx, ny});
                    }
                }
            }
            
            if(!neighbors.empty()) {
                Cell next = neighbors[std::rand() % neighbors.size()];
                
                // Remove wall between
                int wallX = current.x + (next.x - current.x)/2;
                int wallY = current.y + (next.y - current.y)/2;
                mapStr[wallY * MAP_WIDTH + wallX] = '.';
                
                mapStr[next.y * MAP_WIDTH + next.x] = '.';
                stack.push(next);
            } else {
                stack.pop();
            }
        }
        
        // Ensure player start is clear
        mapStr[1 * MAP_WIDTH + 1] = '.';
        playerX = 1.5; playerY = 1.5;
        
        // Place enemy far away
        while(true) {
            int ex = std::rand() % (MAP_WIDTH-2) + 1;
            int ey = std::rand() % (MAP_HEIGHT-2) + 1;
            if(mapStr[ey * MAP_WIDTH + ex] == '.') {
                double dist = sqrt(pow(ex - playerX, 2) + pow(ey - playerY, 2));
                if(dist > 20.0) { // Far away
                    enemyX = ex + 0.5;
                    enemyY = ey + 0.5;
                    break;
                }
            }
        }
    }

public:
    void onInit() override {
        generateMaze();
        lastFrame = std::chrono::system_clock::now();
    }
    
    void onDraw() override {
        clear(GraphicsApp::BG_BLACK);
        
        if (gameOver) {
            std::string msg = victory ? "VICTORY! YOU ESCAPED!" : "GAME OVER";
            WORD color = victory ? GraphicsApp::FG_INTENSE_GREEN : GraphicsApp::FG_INTENSE_RED;
            drawText((termWidth - (int)msg.size())/2, termHeight/2, msg, color);
            
            msg = "Press SPACE to restart, ESC to exit";
            drawText((termWidth - (int)msg.size())/2, termHeight/2 + 2, msg, GraphicsApp::FG_GRAY);
            present();
            return;
        }

        // Raycasting Loop
        for (int x = 0; x < termWidth; x++) {
            double rayAngle = (playerA - FOV / 2.0) + ((double)x / (double)termWidth) * FOV;
            
            double distanceToWall = 0;
            bool hitWall = false;
            bool hitBoundary = false;
            
            double eyeX = sin(rayAngle);
            double eyeY = cos(rayAngle);
            
            while (!hitWall && distanceToWall < DEPTH) {
                distanceToWall += 0.1;
                int testX = (int)(playerX + eyeX * distanceToWall);
                int testY = (int)(playerY + eyeY * distanceToWall);
                
                if (testX < 0 || testX >= MAP_WIDTH || testY < 0 || testY >= MAP_HEIGHT) {
                    hitWall = true;
                    distanceToWall = DEPTH;
                } else {
                    if (mapStr[testY * MAP_WIDTH + testX] == '#') {
                        hitWall = true;
                        
                        std::vector<std::pair<double, double>> p; 
                        for (int tx = 0; tx < 2; tx++)
                            for (int ty = 0; ty < 2; ty++) {
                                double vy = (double)testY + ty - playerY;
                                double vx = (double)testX + tx - playerX;
                                double d = sqrt(vx*vx + vy*vy);
                                double dot = (eyeX * vx / d) + (eyeY * vy / d);
                                p.push_back(std::make_pair(d, dot));
                            }
                        
                        std::sort(p.begin(), p.end(), [](const std::pair<double, double> &left, const std::pair<double, double> &right) {
                            return left.first < right.first;
                        });
                        
                        double bound = 0.01;
                        if (acos(p.at(0).second) < bound) hitBoundary = true;
                        if (acos(p.at(1).second) < bound) hitBoundary = true;
                    }
                }
            }
            
            int ceiling = (double)(termHeight / 2.0) - termHeight / ((double)distanceToWall);
            int floor = termHeight - ceiling;
            
            // Wall colors - Blue/Purple theme for "Cyber" look? Or just Stone grey
            WORD wallColor = GraphicsApp::FG_WHITE;
            wchar_t wallChar = 0x2588;
            
            if (distanceToWall <= DEPTH / 4.0)      { wallChar = 0x2588; wallColor = GraphicsApp::FG_INTENSE_WHITE; } 
            else if (distanceToWall < DEPTH / 3.0)  { wallChar = 0x2593; wallColor = GraphicsApp::FG_WHITE; }
            else if (distanceToWall < DEPTH / 2.0)  { wallChar = 0x2592; wallColor = GraphicsApp::FG_GRAY; }
            else if (distanceToWall < DEPTH)        { wallChar = 0x2591; wallColor = GraphicsApp::FG_BLACK | GraphicsApp::FG_GRAY; } 
            else                                    { wallChar = ' ';    wallColor = 0; }
            
            if (hitBoundary) wallColor = 0; 
            
            for (int y = 0; y < termHeight; y++) {
                if (y < ceiling) {
                    drawPixel(x, y, ' ', GraphicsApp::BG_BLACK);
                } else if (y > ceiling && y <= floor) {
                    drawPixel(x, y, wallChar, wallColor);
                } else {
                    double b = 1.0 - (((double)y - termHeight / 2.0) / ((double)termHeight / 2.0));
                    wchar_t floorShade = ' ';
                    WORD floorColor = GraphicsApp::FG_GRAY;
                    if (b < 0.25) floorShade = '#';
                    else if (b < 0.5) floorShade = 'x';
                    else if (b < 0.75) floorShade = '.';
                    drawPixel(x, y, floorShade, floorColor); 
                }
            }
        }
        
        // Entity Rendering (Blocky Person)
        double vecX = enemyX - playerX;
        double vecY = enemyY - playerY;
        double distToEnemy = sqrt(vecX*vecX + vecY*vecY);
        
        double eyeX = sin(playerA);
        double eyeY = cos(playerA);
        
        double objectAngle = atan2(eyeY, eyeX) - atan2(vecY, vecX);
        while (objectAngle < -3.14159) objectAngle += 2 * 3.14159;
        while (objectAngle > 3.14159) objectAngle -= 2 * 3.14159;
        
        bool inFOV = fabs(objectAngle) < FOV / 2.0; // Standardize FOV check
        
        if (inFOV && distToEnemy > 0.5 && distToEnemy < DEPTH) {
            int entityH = termHeight / distToEnemy;
            int entityW = entityH / 2; // Making it narrower like a person
            
            int entityCol = (0.5 * (objectAngle / (FOV / 2.0)) + 0.5) * termWidth;
            
            for (int y = 0; y < entityH; y++) {
                for (int x = 0; x < entityW; x++) {
                    int screenX = entityCol - entityW/2 + x;
                    int screenY = termHeight/2 - entityH/2 + y;
                    
                    if (screenX >= 0 && screenX < termWidth && screenY >= 0 && screenY < termHeight) {
                        float fy = (float)y / entityH;
                        float fx = (float)x / entityW;
                        
                        // Pixel Art Logic for Blocky Person
                        // Head: Top 15%
                        bool isHead = (fy < 0.15);
                        // Body: 15% to 60%
                        bool isBody = (fy >= 0.15 && fy < 0.6);
                        // Legs: 60% to 100%
                        bool isLegs = (fy >= 0.6);
                        
                        WORD color = 0;
                        wchar_t ch = 0x2588;
                        
                        // Simple "Steve" like colors
                        if (isHead) {
                            // Face color (Peach-ish approximation -> Yellow/Red mix or just White/Red)
                            color = GraphicsApp::FG_INTENSE_YELLOW; // Close enough to skin in 16 colors
                            
                            // Eyes
                            if (fy > 0.05 && fy < 0.1) {
                                if ((fx > 0.2 && fx < 0.4) || (fx > 0.6 && fx < 0.8)) {
                                    color = GraphicsApp::FG_WHITE; // Whites of eyes
                                    // Pupils?
                                }
                            }
                        } else if (isBody) {
                             // Cyan shirt
                             color = GraphicsApp::FG_CYAN | GraphicsApp::FG_INTENSE_CYAN;
                             
                             // Arms?
                             if (fx < 0.2 || fx > 0.8) {
                                 // Skin arms? Or sleeves?
                                 color = GraphicsApp::FG_INTENSE_YELLOW;
                             }
                        } else if (isLegs) {
                             // Blue pants
                             color = GraphicsApp::FG_BLUE | GraphicsApp::FG_INTENSE_BLUE;
                             // Separation
                             if (fx > 0.45 && fx < 0.55 && fy > 0.7) {
                                 color = 0; // Gap between legs
                             }
                        }
                        
                        if (color != 0)
                            drawPixel(screenX, screenY, ch, color);
                    }
                }
            }
        }
        
        // HUD
        std::string hud = "POS: " + std::to_string((int)playerX) + "," + std::to_string((int)playerY);
        drawText(1, 1, hud, GraphicsApp::FG_INTENSE_WHITE);
        
        present();
    }
    
    void onKey(int ch, int ext) override {
        if (ch == 27) quit();
        if (gameOver && ch == ' ') {
            generateMaze(); // New maze!
            gameOver = false;
            return;
        }
        
        if (gameOver) return;
        
        double moveSpeed = 0.2; 
        double rotSpeed = 0.1;
        
        double oldX = playerX;
        double oldY = playerY;
        
        if (ch == 'w' || ch == 'W') {
            playerX += sin(playerA) * moveSpeed;
            playerY += cos(playerA) * moveSpeed;
        } else if (ch == 's' || ch == 'S') {
            playerX -= sin(playerA) * moveSpeed;
            playerY -= cos(playerA) * moveSpeed;
        }
        
        // Wall collision for player
        if (mapStr[(int)playerY * MAP_WIDTH + (int)playerX] == '#') {
            playerX = oldX; 
            playerY = oldY;
        }
        
        if (ch == 'a' || ch == 'A') playerA -= rotSpeed;
        if (ch == 'd' || ch == 'D') playerA += rotSpeed;
    }
    
    void onTick() override {
        if (gameOver) return;
        
        auto now = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed = now - lastFrame;
        lastFrame = now;
        double dt = elapsed.count();
        if (dt > 0.1) dt = 0.1; // Cap delta time
        
        // Enemy AI with Wall Collision
        double dx = playerX - enemyX;
        double dy = playerY - enemyY;
        double dist = sqrt(dx*dx + dy*dy);
        
        if (dist > 0.5) {
            double dirX = dx / dist;
            double dirY = dy / dist;
            
            double nextX = enemyX + dirX * enemySpeed * dt;
            double nextY = enemyY + dirY * enemySpeed * dt;
            
            // Check X axis
            if (mapStr[(int)enemyY * MAP_WIDTH + (int)nextX] != '#') {
                enemyX = nextX;
            }
            // Check Y axis
            if (mapStr[(int)nextY * MAP_WIDTH + (int)enemyX] != '#') {
                enemyY = nextY;
            }
        }
        
        if (dist < 0.8) {
            gameOver = true;
        }
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    Maze3D app;
    app.run();
    return 0;
}
