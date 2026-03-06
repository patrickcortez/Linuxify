// g++ -O3 -o ../cmds/calc.exe calc.cpp
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <windows.h>
#include <conio.h>
#include <vector>
#include <cstdlib>
#include "arith.hpp"

std::atomic<bool> g_sigstart(false);

void signalListener() {
    DWORD pid = GetCurrentProcessId();
    std::string pipeName = "\\\\.\\pipe\\linuxify_signals_" + std::to_string(pid);
    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            pipeName.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            1024 * 16,
            1024 * 16,
            0,
            NULL
        );
        if (hPipe != INVALID_HANDLE_VALUE) {
            if (ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
                char buffer[128];
                DWORD bytesRead;
                if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                    buffer[bytesRead] = '\0';
                    std::string sig(buffer);
                    if (sig == "SIGSTART") {
                        g_sigstart = true;
                    }
                }
            }
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
        }
        Sleep(100);
    }
}

const int WIDTH = 21;
const int HEIGHT = 11;
std::string map[HEIGHT] = {
    "#####################",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.###.#.###.####",
    "#.................#",
    "#.##.###.#.###.##.#",
    "#.................#",
    "#####################"
};

int px = 10, py = 7;
int gx[2] = {1, 19}, gy[2] = {1, 1};
int score = 0;

void drawGame() {
    std::cout << "\033[H";
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            if (x == px && y == py) {
                std::cout << "\033[33mC\033[0m";
            } else if ((x == gx[0] && y == gy[0]) || (x == gx[1] && y == gy[1])) {
                std::cout << "\033[31mG\033[0m";
            } else if (map[y][x] == '#') {
                std::cout << "\033[34m#\033[0m";
            } else if (map[y][x] == '.') {
                std::cout << ".";
            } else {
                std::cout << " ";
            }
        }
        std::cout << "\n";
    }
    std::cout << "Score: " << score << "\nPress 'q' to quit.\n";
}

void updateGhost(int idx) {
    int dir = rand() % 4;
    int nx = gx[idx], ny = gy[idx];
    if (dir == 0) nx++;
    else if (dir == 1) nx--;
    else if (dir == 2) ny++;
    else if (dir == 3) ny--;
    if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT && map[ny][nx] != '#') {
        gx[idx] = nx;
        gy[idx] = ny;
    }
}

void runPacman() {
    std::cout << "\033[2J";
    while (true) {
        drawGame();
        if (_kbhit()) {
            char ch = _getch();
            if (ch == 'q') break;
            int nx = px, ny = py;
            if (ch == 'w') ny--;
            else if (ch == 's') ny++;
            else if (ch == 'a') nx--;
            else if (ch == 'd') nx++;
            if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT && map[ny][nx] != '#') {
                px = nx;
                py = ny;
                if (map[py][px] == '.') {
                    map[py][px] = ' ';
                    score += 10;
                }
            }
        }
        for (int i = 0; i < 2; ++i) {
            updateGhost(i);
            if (gx[i] == px && gy[i] == py) {
                drawGame();
                std::cout << "GAME OVER!\n";
                return;
            }
        }
        Sleep(100);
    }
}

int main(int argc, char* argv[]) {
    std::thread t(signalListener);
    t.detach();
    std::string input;
    while (true) {
        if (g_sigstart) {
            runPacman();
            g_sigstart = false;
        }
        std::cout << "calc> ";
        if (!std::getline(std::cin, input)) break;
        if (g_sigstart) {
            runPacman();
            g_sigstart = false;
            continue;
        }
        if (input == "exit" || input == "quit") {
            break;
        }
        if (input.empty()) continue;
        try {
            std::string result = Arith::evaluate(input);
            std::cout << result << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n"; // Handled by standard io
        }
    }
    return 0;
}
