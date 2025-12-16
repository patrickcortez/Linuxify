// Compile: g++ -std=c++17 -static -I../src -o SnakeGame.exe snake.cpp
#include "window.hpp"
#include <deque>
#include <random>
#include <ctime>
#include <chrono>

using namespace FunuxSys;

struct Point {
    int x, y;
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

class SnakeGame : public GraphicsApp {
private:
    std::deque<Point> snake;
    Point food;
    Point dir;
    int score = 0;
    bool gameOver = false;
    std::mt19937 rng;
    int speed = 100; // ms per frame
    std::chrono::system_clock::time_point lastMove;
    
    void spawnFood() {
        if (termWidth <= 2 || termHeight <= 2) return;
        std::uniform_int_distribution<int> distX(1, termWidth - 2);
        std::uniform_int_distribution<int> distY(1, termHeight - 2);
        
        int attempts = 0;
        while (attempts < 100) {
            food = {distX(rng), distY(rng)};
            
            bool onSnake = false;
            for (const auto& p : snake) {
                if (p == food) { onSnake = true; break; }
            }
            if (!onSnake) break;
            attempts++;
        }
    }
    
public:
    void onInit() override {
        rng.seed(std::time(nullptr));
        resetGame();
    }
    
    void resetGame() {
        snake.clear();
        int cx = termWidth / 2;
        int cy = termHeight / 2;
        if (cx < 3) cx = 3; 
        if (cy < 3) cy = 3;
        snake.push_back({cx, cy});
        snake.push_back({cx-1, cy});
        snake.push_back({cx-2, cy});
        dir = {1, 0}; 
        score = 0;
        gameOver = false;
        speed = 100;
        spawnFood();
        lastMove = std::chrono::system_clock::now();
    }
    
    void onDraw() override {
        clear(GraphicsApp::BG_BLACK | GraphicsApp::FG_WHITE);
        
        // Draw border
        for (int i = 0; i < termWidth; i++) {
            drawPixel(i, 0, '#', GraphicsApp::FG_GRAY);
            drawPixel(i, termHeight - 1, '#', GraphicsApp::FG_GRAY);
        }
        for (int i = 0; i < termHeight; i++) {
            drawPixel(0, i, '#', GraphicsApp::FG_GRAY);
            drawPixel(termWidth - 1, i, '#', GraphicsApp::FG_GRAY);
        }
        
        if (gameOver) {
            std::string msg = "GAME OVER";
            drawText((termWidth - (int)msg.size())/2, termHeight/2, msg, GraphicsApp::FG_RED | GraphicsApp::FG_INTENSE_RED); // Using intense red directly if available, or just red
            msg = "Score: " + std::to_string(score);
            drawText((termWidth - (int)msg.size())/2, termHeight/2 + 1, msg, GraphicsApp::FG_WHITE | GraphicsApp::FG_INTENSE_WHITE);
            msg = "Press SPACE to restart, ESC to exit";
            drawText((termWidth - (int)msg.size())/2, termHeight/2 + 2, msg, GraphicsApp::FG_GRAY);
            present();
            return;
        }
        
        // Draw Food
        drawPixel(food.x, food.y, 'O', GraphicsApp::FG_RED | GraphicsApp::FG_INTENSE_RED);
        
        // Draw Snake
        for (size_t i = 0; i < snake.size(); i++) {
            const auto& p = snake[i];
            WORD color = GraphicsApp::FG_GREEN;
            if (i == 0) color = GraphicsApp::FG_GREEN | GraphicsApp::FG_INTENSE_GREEN; 
            drawPixel(p.x, p.y, i==0 ? '@' : 'o', color);
        }
        
        // Draw Score
        std::string scoreText = "Score: " + std::to_string(score);
        drawText(2, 0, scoreText, GraphicsApp::FG_WHITE | GraphicsApp::FG_INTENSE_WHITE);
        
        present();
    }
    
    void onKey(int ch, int ext) override {
        if (ch == 27) { // ESC
            quit();
        }
        else if (gameOver && ch == ' ') {
            resetGame();
        }
        else if (!gameOver) {
            if (ch == 0 || ch == 224) {
                if (ext == 72 && dir.y == 0) dir = {0, -1}; // Up
                else if (ext == 80 && dir.y == 0) dir = {0, 1}; // Down
                else if (ext == 75 && dir.x == 0) dir = {-1, 0}; // Left
                else if (ext == 77 && dir.x == 0) dir = {1, 0}; // Right
            }
            else if ((ch == 'w' || ch == 'W') && dir.y == 0) dir = {0, -1};
            else if ((ch == 's' || ch == 'S') && dir.y == 0) dir = {0, 1};
            else if ((ch == 'a' || ch == 'A') && dir.x == 0) dir = {-1, 0};
            else if ((ch == 'd' || ch == 'D') && dir.x == 0) dir = {1, 0};
        }
    }
    
    void onTick() override {
        if (gameOver) return;
        
        auto now = std::chrono::system_clock::now();
        int ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMove).count();
        
        if (ms >= speed) {
            Point head = snake.front();
            Point newHead = {head.x + dir.x, head.y + dir.y};
            
            // Check collisions with wall
            if (newHead.x <= 0 || newHead.x >= termWidth - 1 || newHead.y <= 0 || newHead.y >= termHeight - 1) {
                gameOver = true;
                return;
            }
            
            // Check self collision
            for (const auto& p : snake) {
                if (p == newHead) {
                    gameOver = true;
                    return;
                }
            }
            
            snake.push_front(newHead);
            
            if (newHead == food) {
                score += 10;
                if (speed > 30) speed -= 2; 
                spawnFood();
            } else {
                snake.pop_back();
            }
            
            lastMove = now;
        }
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SnakeGame game;
    game.run();
    return 0;
}
