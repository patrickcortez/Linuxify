#include <iostream>
#include <vector>
#include <string>
#include <conio.h>
#include <windows.h>
#include <ctime>

using namespace std;

#define BLUE 1
#define GREEN 2
#define CYAN 3
#define RED 4
#define MAGENTA 5
#define BROWN 6
#define LIGHTGRAY 7
#define DARKGRAY 8
#define LIGHTBLUE 9
#define LIGHTGREEN 10
#define LIGHTCYAN 11
#define LIGHTRED 12
#define LIGHTMAGENTA 13
#define YELLOW 14
#define WHITE 15

const int WIDTH = 19;
const int HEIGHT = 19;

bool gameOver;
bool checkWin;
int score;

char map[HEIGHT][WIDTH + 1] = {
    "###################",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.### # ###.####",
    "   #.#       #.#   ",
    "####.# ##G## #.####",
    ".......#   #.......",
    "####.# ##### #.####",
    "   #.#       #.#   ",
    "####.# ##### #.####",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#..#...........#..#",
    "##.#.#.#####.#.#.##",
    "#....#...#...#....#",
    "###################"
};

struct Entity {
    int x, y;
    int dirX, dirY;
    char icon;
    int color;
    int startX, startY;
};

Entity pacman;
vector<Entity> ghosts;

void SetColor(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

void GotoXY(int x, int y) {
    COORD coord;
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void HideCursor() {
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(consoleHandle, &info);
}

void Setup() {
    gameOver = false;
    checkWin = false;
    score = 0;
    
    pacman.icon = 'C';
    pacman.color = YELLOW;
    pacman.dirX = 0;
    pacman.dirY = 0;

    ghosts.clear();
    int ghostColors[] = {LIGHTRED, LIGHTMAGENTA, LIGHTCYAN, LIGHTGREEN};
    
    pacman.x = 9;
    pacman.y = 15;
    pacman.startX = 9;
    pacman.startY = 15;

    for(int i=0; i<4; i++) {
        Entity g;
        g.x = 9;
        g.y = 9;
        g.startX = 9;
        g.startY = 9;
        g.dirX = 0;
        g.dirY = -1;
        g.icon = 'M';
        g.color = ghostColors[i];
        ghosts.push_back(g);
    }
}

void Draw() {
    GotoXY(0, 0);
    for (int i = 0; i < HEIGHT; i++) {
        for (int j = 0; j < WIDTH; j++) {
            bool drawn = false;
            
            if (j == pacman.x && i == pacman.y) {
                SetColor(pacman.color);
                cout << pacman.icon;
                drawn = true;
            }
            
            if (!drawn) {
                for(auto& g : ghosts) {
                    if (j == g.x && i == g.y) {
                        SetColor(g.color);
                        cout << g.icon;
                        drawn = true;
                        break;
                    }
                }
            }

            if (!drawn) {
                char tile = map[i][j];
                if (tile == '#') SetColor(BLUE);
                else if (tile == '.') SetColor(WHITE);
                else SetColor(LIGHTGRAY);
                cout << tile;
            }
        }
        cout << endl;
    }
    SetColor(WHITE);
    cout << "Score: " << score << endl;
}

void Input() {
    if (_kbhit()) {
        char current = _getch();
        if (current == -32) {
             current = _getch();
             switch(current) {
                 case 72: pacman.dirX = 0; pacman.dirY = -1; break;
                 case 80: pacman.dirX = 0; pacman.dirY = 1; break;
                 case 75: pacman.dirX = -1; pacman.dirY = 0; break;
                 case 77: pacman.dirX = 1; pacman.dirY = 0; break;
             }
        } else {
             switch(current) {
                 case 'w': pacman.dirX = 0; pacman.dirY = -1; break;
                 case 's': pacman.dirX = 0; pacman.dirY = 1; break;
                 case 'a': pacman.dirX = -1; pacman.dirY = 0; break;
                 case 'd': pacman.dirX = 1; pacman.dirY = 0; break;
             }
        }
    }
}

bool IsWalkable(int x, int y) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return false;
    return map[y][x] != '#';
}

void Logic() {
    int nextX = pacman.x + pacman.dirX;
    int nextY = pacman.y + pacman.dirY;

    if (nextX < 0) nextX = WIDTH - 1;
    if (nextX >= WIDTH) nextX = 0;

    if (IsWalkable(nextX, nextY)) {
        pacman.x = nextX;
        pacman.y = nextY;
    }

    if (map[pacman.y][pacman.x] == '.') {
        map[pacman.y][pacman.x] = ' ';
        score += 10;
    }

    int dots = 0;
    for (int i = 0; i < HEIGHT; i++)
        for (int j = 0; j < WIDTH; j++)
            if (map[i][j] == '.') dots++;
    
    if (dots == 0) {
        gameOver = true;
        checkWin = true;
    }

    for (auto& g : ghosts) {
        int possibleDirX[4] = {0, 0, -1, 1};
        int possibleDirY[4] = {-1, 1, 0, 0};
        
        int attempts = 0;
        int newDX, newDY;
        bool moved = false;
        
        bool track = (rand() % 100) < 20;

        if (track) {
            if (pacman.x > g.x) { newDX = 1; newDY = 0; }
            else if (pacman.x < g.x) { newDX = -1; newDY = 0; }
            else if (pacman.y > g.y) { newDX = 0; newDY = 1; }
            else { newDX = 0; newDY = -1; }
            
             int tryX = g.x + newDX;
             int tryY = g.y + newDY;
             if (IsWalkable(tryX, tryY)) {
                 g.x = tryX;
                 g.y = tryY;
                 moved = true;
             }
        } 
        
        if (!moved) {
             while(attempts < 10) {
                int r = rand() % 4;
                newDX = possibleDirX[r];
                newDY = possibleDirY[r];
                
                int tryX = g.x + newDX;
                int tryY = g.y + newDY;
                
                 if (tryX < 0) tryX = WIDTH - 1;
                 if (tryX >= WIDTH) tryX = 0;

                if (IsWalkable(tryX, tryY)) {
                    g.x = tryX;
                    g.y = tryY;
                    moved = true;
                    break;
                }
                attempts++;
            }
        }

        if (g.x == pacman.x && g.y == pacman.y) {
            gameOver = true;
        }
    }
}

int main() {
    srand(time(0));
    Setup();
    HideCursor();
    
    while (!gameOver) {
        Draw();
        Input();
        Logic();
        Sleep(100);
    }

    Draw();
    SetColor(WHITE);
    if (checkWin) cout << "\nYOU WIN!\n";
    else cout << "\nGAME OVER!\n";

    while (_kbhit()) _getch();
    _getch();
    
    return 0;
}
