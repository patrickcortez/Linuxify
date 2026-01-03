/*
 * LoneShooter - Open World 2.5D Raycaster
 * Compile: g++ -o LoneShooter.exe loneshooter.cpp -lgdi32 -mwindows -O2
 * Run: ./LoneShooter.exe
 * Controls: WASD=Move, Arrows=Look, ESC=Quit
 * By Patrick Andrew Cortez
 */

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include <process.h>
#include <mmsystem.h>

volatile bool musicRunning = true;
HMIDIOUT hMidiOut;

void MidiMsg(DWORD msg) {
    midiOutShortMsg(hMidiOut, msg);
}

void NoteOn(int ch, int note, int vel) {
    MidiMsg(0x90 | ch | (note << 8) | (vel << 16));
}

void NoteOff(int ch, int note) {
    MidiMsg(0x80 | ch | (note << 8));
}

void SetInstrument(int ch, int instr) {
    MidiMsg(0xC0 | ch | (instr << 8));
}

void SetVolume(int ch, int vol) {
    MidiMsg(0xB0 | ch | (7 << 8) | (vol << 16));
}

void InitAudio() {
    midiOutOpen(&hMidiOut, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    
    // Mix Volumes
    SetVolume(0, 85);  // Music Guitar (Lower)
    SetVolume(1, 100); // Music Bass
    SetVolume(2, 127); // GUN (Max)
    SetInstrument(2, 127); // Gunshot
    SetVolume(3, 127); // Score (Max)
    SetInstrument(3, 112); // Tinkle Bell (Better than Glock?)
    SetVolume(9, 127); // Drums (Max)
    
    SetInstrument(0, 30); // Distortion Guitar (More sustain)
    SetInstrument(1, 33); // Fingered Bass
}

void CleanupAudio() {
    midiOutReset(hMidiOut);
    midiOutClose(hMidiOut);
}

void PlayGunSound() {
    NoteOn(2, 45, 127); // Low Gunshot
    NoteOn(9, 36, 127); // Kick
    NoteOn(9, 57, 127); // Crash Cymbal (Explosive)
}

void PlayReloadSound(int stage) {
    if (stage == 0) NoteOn(9, 37, 100); // Side Stick (Click out)
    if (stage == 1) NoteOn(9, 75, 90);  // Claves (Click in)
    if (stage == 2) NoteOn(9, 39, 100); // Hand Clap (Slide/Slap)
}

void PlayStepSound() {
    NoteOn(9, 42, 40); // Quiet Hi-Hat
}

void PlayScoreSound() {
    NoteOn(3, 84, 127); // High Ding
}

void BackgroundMusic(void* arg) {
    // Wait for init? Assuming InitAudio called before thread start.
    
    // Doom-ish E1M1 Riff Pattern
    // E2 open string chugging with chromatic logic
    const int E2 = 40;
    const int E3 = 52; 
    const int D3 = 50;
    const int C3 = 48;
    const int B2 = 47;
    const int AS2 = 46;
    const int A2 = 45;

    while (musicRunning) {
        // Main Riff: E E E E E E D E E E E E C E E E A# E E B E D# E
        int riff[] = { E2, E3, E2, D3, E2, C3, E2, AS2, E2, B2, E2 };
        
        // Sustained Bass
        NoteOn(1, E2-12, 100);

        for (int i = 0; i < 11; i++) {
            if (!musicRunning) break;
            int note = riff[i];
            
            // Power chord
            NoteOn(0, note, 110);
            NoteOn(0, note + 7, 110);
            
            Sleep(150);
            
            // Release main chord but Bass can stay
            NoteOff(0, note);
            NoteOff(0, note + 7);
            
            // Chugging gap (Fast mute)
            if (i < 10) {
                 NoteOn(0, E2, 80);
                 NoteOn(0, E2+7, 80);
                 Sleep(150);
                 NoteOff(0, E2);
                 NoteOff(0, E2+7);
            }
        }
        
        // Drum Fill / Loop Turnaround
        NoteOn(9, 38, 127); // Snare
        Sleep(150);
        NoteOn(9, 38, 127); // Snare
        NoteOn(9, 49, 127); // Crash
        Sleep(150);
        
        // Restart loop immediately (no gap)
        // Bass continues or re-triggers
        NoteOff(1, E2-12);
    }
}

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const int MAP_WIDTH = 64;
const int MAP_HEIGHT = 64;
const float PI = 3.14159265f;
const float FOV = PI / 3.0f;

int worldMap[MAP_WIDTH][MAP_HEIGHT];

struct Player {
    float x, y;
    float angle;
    float pitch;
    int health;
};

struct Enemy {
    float x, y;
    float distance;
    bool active;
    float speed;
};

struct TreeSprite {
    float x, y;
    float distance;
};

struct Cloud {
    float x, y;
    float height;
    float speed;
};

struct Bullet {
    float x, y;
    float dirX, dirY;
    float speed;
    bool active;
};

Player player = {32.0f, 32.0f, 0.0f, 0.0f, 100};
std::vector<Enemy> enemies;
std::vector<TreeSprite> trees;
std::vector<Cloud> clouds;
std::vector<Bullet> bullets;
float* zBuffer = nullptr;

float gunSwayX = 0, gunSwayY = 0;
float gunSwayPhase = 0;
bool isFiring = false;
float fireTimer = 0;
bool isMoving = false;

int ammo = 8;
int maxAmmo = 8;
bool isReloading = false;
float reloadTimer = 0;
float reloadDuration = 3.0f;
float gunReloadOffset = 0;
int reloadStage = 0;

int score = 0;
float scoreTimer = 0;
wchar_t scoreMsg[64] = L"";
const wchar_t* praiseMsgs[] = {L"Nice Shot!", L"Damn Son", L"Daddy Chill"};

int highScore = 0;

void LoadHighScore() {
    FILE* f = fopen("highscore.dat", "rb");
    if (f) {
        fread(&highScore, sizeof(int), 1, f);
        fclose(f);
    }
}

void SaveHighScore() {
    FILE* f = fopen("highscore.dat", "wb");
    if (f) {
        fwrite(&highScore, sizeof(int), 1, f);
        fclose(f);
    }
}

HWND hMainWnd;
DWORD* backBufferPixels = NULL;

DWORD* grassPixels = NULL;
DWORD* npcPixels = NULL;
DWORD* treePixels = NULL;
DWORD* cloudPixels = NULL;
DWORD* gunPixels = NULL;
DWORD* gunfirePixels = NULL;
DWORD* bulletPixels = NULL;
DWORD* healthbarPixels[11] = {NULL};
int grassW = 0, grassH = 0;
int npcW = 0, npcH = 0;
int treeW = 0, treeH = 0;
int cloudW = 0, cloudH = 0;
int gunW = 0, gunH = 0;
int gunfireW = 0, gunfireH = 0;
int bulletW = 0, bulletH = 0;
int healthbarW = 0, healthbarH = 0;

bool keys[256] = {false};
wchar_t loadStatus[256] = L"Loading...";

DWORD* LoadBMPPixels(const wchar_t* filename, int* outW, int* outH) {
    HBITMAP hBmp = (HBITMAP)LoadImageW(NULL, filename, IMAGE_BITMAP, 0, 0, 
        LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!hBmp) return nullptr;
    
    BITMAP bm;
    GetObject(hBmp, sizeof(bm), &bm);
    *outW = bm.bmWidth;
    *outH = bm.bmHeight;
    
    DWORD* pixels = new DWORD[bm.bmWidth * bm.bmHeight];
    
    HDC hdc = GetDC(NULL);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    
    GetDIBits(hdc, hBmp, 0, bm.bmHeight, pixels, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);
    DeleteObject(hBmp);
    
    return pixels;
}

void TryLoadAssets() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastBackSlash = wcsrchr(exePath, L'\\');
    wchar_t* lastForwardSlash = wcsrchr(exePath, L'/');
    wchar_t* lastSlash = lastBackSlash;
    if (lastForwardSlash && (!lastSlash || lastForwardSlash > lastSlash)) lastSlash = lastForwardSlash;
    if (lastSlash) *lastSlash = L'\0';
    
    wchar_t path[MAX_PATH];
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\grass.bmp", exePath);
    grassPixels = LoadBMPPixels(path, &grassW, &grassH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\sprite2.bmp", exePath);
    npcPixels = LoadBMPPixels(path, &npcW, &npcH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\tree.bmp", exePath);
    treePixels = LoadBMPPixels(path, &treeW, &treeH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\cloud.bmp", exePath);
    cloudPixels = LoadBMPPixels(path, &cloudW, &cloudH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gun.bmp", exePath);
    gunPixels = LoadBMPPixels(path, &gunW, &gunH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gunfire.bmp", exePath);
    gunfirePixels = LoadBMPPixels(path, &gunfireW, &gunfireH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\bullet.bmp", exePath);
    bulletPixels = LoadBMPPixels(path, &bulletW, &bulletH);
    
    const wchar_t* healthbarNames[] = {L"healthbar_0.bmp", L"healthbar_10.bmp", L"healthbar_20.bmp", L"healthbar_30.bmp", L"healthbar_40.bmp", L"healthbar_50.bmp", L"healthbar_60.bmp", L"healthbar_70.bmp", L"healthbar_80.bmp", L"healthbar_90.bmp", L"healthbar_full.bmp"};
    for (int i = 0; i < 11; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\healthbar_UI\\%ls", exePath, healthbarNames[i]);
        healthbarPixels[i] = LoadBMPPixels(path, &healthbarW, &healthbarH);
    }
    
    swprintf(loadStatus, 256, L"G:%ls H:%ls", gunPixels ? L"OK" : L"X", healthbarPixels[10] ? L"OK" : L"X");
}

void GenerateWorld() {
    srand((unsigned)time(NULL));
    
    for (int x = 0; x < MAP_WIDTH; x++) {
        for (int y = 0; y < MAP_HEIGHT; y++) {
            if (x <= 3 || x >= MAP_WIDTH-4 || y <= 3 || y >= MAP_HEIGHT-4) {
                worldMap[x][y] = 3;
            } else {
                worldMap[x][y] = 0;
            }
        }
    }
    
    for (int i = 0; i < 600; i++) {
        int side = rand() % 4;
        float tx, ty;
        switch (side) {
            case 0: tx = -15.0f + (rand() % 180) / 10.0f; ty = -15.0f + (rand() % ((MAP_HEIGHT + 30) * 10)) / 10.0f; break;
            case 1: tx = MAP_WIDTH - 3.0f + (rand() % 180) / 10.0f; ty = -15.0f + (rand() % ((MAP_HEIGHT + 30) * 10)) / 10.0f; break;
            case 2: tx = -15.0f + (rand() % ((MAP_WIDTH + 30) * 10)) / 10.0f; ty = -15.0f + (rand() % 180) / 10.0f; break;
            case 3: tx = -15.0f + (rand() % ((MAP_WIDTH + 30) * 10)) / 10.0f; ty = MAP_HEIGHT - 3.0f + (rand() % 180) / 10.0f; break;
        }
        TreeSprite tree = {tx, ty, 0};
        trees.push_back(tree);
    }
    
    for (int i = 0; i < 200; i++) {
        int side = rand() % 4;
        float tx, ty;
        switch (side) {
            case 0: tx = 3.0f + (rand() % 30) / 10.0f; ty = 3.0f + (rand() % ((MAP_HEIGHT - 6) * 10)) / 10.0f; break;
            case 1: tx = MAP_WIDTH - 6.0f + (rand() % 30) / 10.0f; ty = 3.0f + (rand() % ((MAP_HEIGHT - 6) * 10)) / 10.0f; break;
            case 2: tx = 3.0f + (rand() % ((MAP_WIDTH - 6) * 10)) / 10.0f; ty = 3.0f + (rand() % 30) / 10.0f; break;
            case 3: tx = 3.0f + (rand() % ((MAP_WIDTH - 6) * 10)) / 10.0f; ty = MAP_HEIGHT - 6.0f + (rand() % 30) / 10.0f; break;
        }
        TreeSprite tree = {tx, ty, 0};
        trees.push_back(tree);
    }
    
    int numTrees = 40 + rand() % 30;
    for (int i = 0; i < numTrees; i++) {
        float tx = 8.0f + (rand() % ((MAP_WIDTH - 16) * 10)) / 10.0f;
        float ty = 8.0f + (rand() % ((MAP_HEIGHT - 16) * 10)) / 10.0f;
        float distToPlayer = sqrtf((tx - 32)*(tx - 32) + (ty - 32)*(ty - 32));
        if (distToPlayer > 10.0f) {
            TreeSprite tree = {tx, ty, 0};
            trees.push_back(tree);
        }
    }
    
    int clearX = (int)player.x;
    int clearY = (int)player.y;
    for (int dx = -4; dx <= 4; dx++) {
        for (int dy = -4; dy <= 4; dy++) {
            int cx = clearX + dx;
            int cy = clearY + dy;
            if (cx > 3 && cx < MAP_WIDTH-4 && cy > 3 && cy < MAP_HEIGHT-4) {
                worldMap[cx][cy] = 0;
            }
        }
    }
    
    for (int i = 0; i < 25; i++) {
        Cloud cloud;
        cloud.x = -50.0f + (rand() % 1500) / 10.0f;
        cloud.y = -50.0f + (rand() % 1500) / 10.0f;
        cloud.height = 15.0f + (rand() % 100) / 10.0f;
        cloud.speed = 0.5f + (rand() % 100) / 100.0f;
        clouds.push_back(cloud);
    }
}

void SpawnEnemies() {
    enemies.clear();
    for (int i = 0; i < 3; i++) {
        Enemy enemy;
        do {
            enemy.x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
            enemy.y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
        } while (worldMap[(int)enemy.x][(int)enemy.y] != 0 || 
                 sqrtf((enemy.x - player.x)*(enemy.x - player.x) + (enemy.y - player.y)*(enemy.y - player.y)) < 10.0f);
        enemy.active = true;
        enemy.speed = 1.5f + (rand() % 100) / 100.0f;
        enemy.distance = 0;
        enemies.push_back(enemy);
    }
}

inline DWORD MakeColor(int r, int g, int b) {
    return (r << 16) | (g << 8) | b;
}

void CastRays() {
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        float rayAngle = (player.angle - FOV / 2.0f) + ((float)x / SCREEN_WIDTH) * FOV;
        float rayDirX = cosf(rayAngle);
        float rayDirY = sinf(rayAngle);
        
        float distanceToWall = 0;
        bool hitWall = false;
        int wallType = 0;
        
        float stepSize = 0.02f;
        while (!hitWall && distanceToWall < 30.0f) {
            distanceToWall += stepSize;
            int testX = (int)(player.x + rayDirX * distanceToWall);
            int testY = (int)(player.y + rayDirY * distanceToWall);
            
            if (testX < 0 || testX >= MAP_WIDTH || testY < 0 || testY >= MAP_HEIGHT) {
                hitWall = true;
                distanceToWall = 30.0f;
                wallType = 3;
            } else if (worldMap[testX][testY] > 0) {
                hitWall = true;
                wallType = worldMap[testX][testY];
            }
        }
        
        float correctedDist = distanceToWall * cosf(rayAngle - player.angle);
        if (wallType == 3) {
            zBuffer[x] = 100.0f;
        } else {
            zBuffer[x] = correctedDist;
        }
        
        int ceiling, floorLine;
        if (wallType == 3) {
            ceiling = 0;
            floorLine = SCREEN_HEIGHT / 2 + (int)player.pitch;
        } else {
            ceiling = (int)((SCREEN_HEIGHT / 2.0f) - (SCREEN_HEIGHT / correctedDist) + player.pitch);
            floorLine = SCREEN_HEIGHT - ceiling;
        }
        
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            if (y <= SCREEN_HEIGHT / 2 + (int)player.pitch) {
                float skyGradient = (float)y / (SCREEN_HEIGHT / 2);
                int r = (int)(30 + 80 * (1 - skyGradient));
                int g = (int)(60 + 120 * (1 - skyGradient));
                int b = (int)(100 + 155 * (1 - skyGradient));
                backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(r, g, b);
            }
            
            if (y > SCREEN_HEIGHT / 2 + (int)player.pitch) {
                float rowDist = (SCREEN_HEIGHT / 2.0f) / (y - SCREEN_HEIGHT / 2.0f);
                float floorX = player.x + cosf(rayAngle) * rowDist;
                float floorY = player.y + sinf(rayAngle) * rowDist;
                if (grassPixels && grassW > 0) {
                    int texX = (int)(fmodf(floorX, 1.0f) * grassW);
                    int texY = (int)(fmodf(floorY, 1.0f) * grassH);
                    if (texX < 0) texX += grassW;
                    if (texY < 0) texY += grassH;
                    texX = texX % grassW;
                    texY = texY % grassH;
                    DWORD col = grassPixels[texY * grassW + texX];
                    int bb = (col >> 0) & 0xFF;
                    int gg = (col >> 8) & 0xFF;
                    int rr = (col >> 16) & 0xFF;
                    float shade = 1.0f - (rowDist / 20.0f);
                    if (shade < 0.15f) shade = 0.15f;
                    backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(
                        (int)(rr * shade), (int)(gg * shade), (int)(bb * shade));
                } else {
                    float shade = 1.0f - (rowDist / 20.0f);
                    if (shade < 0.15f) shade = 0.15f;
                    int c = (int)(80 * shade);
                    backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(c/2, c, c/2);
                }
            }
            
            if (wallType != 3 && y >= ceiling && y <= floorLine) {
                float shade = 1.0f - (correctedDist / 25.0f);
                if (shade < 0.1f) shade = 0.1f;
                int r, g, b;
                if (wallType == 2) {
                    r = (int)(60 * shade); g = (int)(100 * shade); b = (int)(40 * shade);
                } else {
                    r = (int)(140 * shade); g = (int)(100 * shade); b = (int)(60 * shade);
                }
                backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(r, g, b);
            }
        }
    }
}

void RenderSprite(DWORD* pixels, int pxW, int pxH, float sx, float sy, float dist) {
    if (dist < 0.5f || dist > 50.0f) return;
    
    float dx = sx - player.x;
    float dy = sy - player.y;
    float spriteAngle = atan2f(dy, dx) - player.angle;
    while (spriteAngle > PI) spriteAngle -= 2 * PI;
    while (spriteAngle < -PI) spriteAngle += 2 * PI;
    if (fabsf(spriteAngle) > FOV) return;
    
    float spriteScreenX = (0.5f + spriteAngle / FOV) * SCREEN_WIDTH;
    float spriteHeight = (SCREEN_HEIGHT / dist);
    float spriteWidth = spriteHeight;
    
    int floorLineAtDist = SCREEN_HEIGHT / 2 + (int)((SCREEN_HEIGHT / 2.0f) / dist) + (int)player.pitch;
    int drawEndY = floorLineAtDist;
    int drawStartY = (int)(drawEndY - spriteHeight);
    int drawStartX = (int)(spriteScreenX - spriteWidth / 2);
    int drawEndX = (int)(spriteScreenX + spriteWidth / 2);
    
    for (int x = drawStartX; x < drawEndX; x++) {
        if (x < 0 || x >= SCREEN_WIDTH) continue;
        if (dist > zBuffer[x]) continue;
        
        float texX = (float)(x - drawStartX) / spriteWidth;
        
        for (int y = drawStartY; y < drawEndY; y++) {
            if (y < 0 || y >= SCREEN_HEIGHT) continue;
            
            float texY = (float)(y - drawStartY) / spriteHeight;
            
            if (pixels && pxW > 0 && pxH > 0) {
                int tx = (int)(texX * pxW);
                int ty = (int)(texY * pxH);
                if (tx >= 0 && tx < pxW && ty >= 0 && ty < pxH) {
                    DWORD col = pixels[ty * pxW + tx];
                    int b = (col >> 0) & 0xFF;
                    int g = (col >> 8) & 0xFF;
                    int r = (col >> 16) & 0xFF;
                    int a = (col >> 24) & 0xFF;
                    if (a == 0) continue;
                    float shade = 1.0f - (dist / 40.0f);
                    if (shade < 0.15f) shade = 0.15f;
                    backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(
                        (int)(r * shade), (int)(g * shade), (int)(b * shade));
                }
            }
        }
    }
}

void RenderSprites() {
    struct SpriteRender { float x, y, dist; int type; };
    std::vector<SpriteRender> allSprites;
    
    for (auto& tree : trees) {
        float dx = tree.x - player.x;
        float dy = tree.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 50.0f) {
            allSprites.push_back({tree.x, tree.y, dist, 0});
        }
    }
    
    for (auto& enemy : enemies) {
        if (enemy.active) {
            float dx = enemy.x - player.x;
            float dy = enemy.y - player.y;
            float dist = sqrtf(dx*dx + dy*dy);
            allSprites.push_back({enemy.x, enemy.y, dist, 1});
        }
    }
    
    std::sort(allSprites.begin(), allSprites.end(), [](const SpriteRender& a, const SpriteRender& b) {
        return a.dist > b.dist;
    });
    
    for (auto& sp : allSprites) {
        if (sp.type == 0) {
            RenderSprite(treePixels, treeW, treeH, sp.x, sp.y, sp.dist);
        } else {
            RenderSprite(npcPixels, npcW, npcH, sp.x, sp.y, sp.dist);
        }
    }
}

void UpdateClouds(float deltaTime) {
    for (auto& cloud : clouds) {
        cloud.x += cloud.speed * deltaTime;
        if (cloud.x > 100.0f) cloud.x = -50.0f;
    }
}

void RenderClouds() {
    if (!cloudPixels || cloudW <= 0 || cloudH <= 0) return;
    
    for (auto& cloud : clouds) {
        float dx = cloud.x - player.x;
        float dy = cloud.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        
        if (dist < 5.0f || dist > 100.0f) continue;
        
        float cloudAngle = atan2f(dy, dx) - player.angle;
        while (cloudAngle > PI) cloudAngle -= 2 * PI;
        while (cloudAngle < -PI) cloudAngle += 2 * PI;
        if (fabsf(cloudAngle) > FOV) continue;
        
        float cloudScreenX = (0.5f + cloudAngle / FOV) * SCREEN_WIDTH;
        float cloudSize = (SCREEN_HEIGHT * 0.8f) / (dist * 0.08f);
        if (cloudSize > 350) cloudSize = 350;
        if (cloudSize < 30) continue;
        
        int horizon = SCREEN_HEIGHT / 2 + (int)player.pitch;
        int skyY = 60 + (int)((cloud.height - 15.0f) * 3.0f) + (int)player.pitch;
        if (skyY < 20) skyY = 20;
        if (skyY > horizon - 50) skyY = horizon - 50;
        
        int drawStartX = (int)(cloudScreenX - cloudSize / 2);
        int drawEndX = (int)(cloudScreenX + cloudSize / 2);
        int drawStartY = skyY;
        int drawEndY = (int)(skyY + cloudSize * 0.5f);
        if (drawEndY > horizon) drawEndY = horizon;
        
        for (int x = drawStartX; x < drawEndX; x++) {
            if (x < 0 || x >= SCREEN_WIDTH) continue;
            float texX = (float)(x - drawStartX) / (drawEndX - drawStartX);
            
            for (int y = drawStartY; y < drawEndY; y++) {
                if (y < 0 || y >= horizon) continue;
                float texY = (float)(y - drawStartY) / (drawEndY - drawStartY);
                
                int tx = (int)(texX * cloudW);
                int ty = (int)(texY * cloudH);
                if (tx < 0 || tx >= cloudW || ty < 0 || ty >= cloudH) continue;
                
                DWORD col = cloudPixels[ty * cloudW + tx];
                int b = (col >> 0) & 0xFF;
                int g = (col >> 8) & 0xFF;
                int r = (col >> 16) & 0xFF;
                int a = (col >> 24) & 0xFF;
                if (a == 0) continue;
                
                float fade = 1.0f - (dist / 100.0f);
                if (fade < 0.4f) fade = 0.4f;
                backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(
                    (int)(r * fade), (int)(g * fade), (int)(b * fade));
            }
        }
    }
}

void UpdateEnemies(float deltaTime) {
    for (auto& enemy : enemies) {
        if (!enemy.active) continue;
        
        float dx = player.x - enemy.x;
        float dy = player.y - enemy.y;
        float dist = sqrtf(dx*dx + dy*dy);
        
        if (dist > 0.5f) {
            float moveX = (dx / dist) * enemy.speed * deltaTime;
            float moveY = (dy / dist) * enemy.speed * deltaTime;
            
            float newX = enemy.x + moveX;
            float newY = enemy.y + moveY;
            
            if (worldMap[(int)newX][(int)enemy.y] == 0) enemy.x = newX;
            if (worldMap[(int)enemy.x][(int)newY] == 0) enemy.y = newY;
        }
        
        if (dist < 1.0f) {
            player.health -= 1;
            if (player.health <= 0) {
                score = 0;
                player.health = 100;
                player.x = 32.0f;
                player.y = 32.0f;
                SpawnEnemies();
            }
        }
        
        enemy.distance = dist;
    }
}

void UpdateGun(float deltaTime) {
    if (fireTimer > 0) fireTimer -= deltaTime;
    
    if (isMoving) {
        gunSwayPhase += deltaTime * 8.0f;
        gunSwayX = sinf(gunSwayPhase) * 15.0f;
        gunSwayY = fabsf(cosf(gunSwayPhase * 2.0f)) * 8.0f;
    } else {
        gunSwayX *= 0.9f;
        gunSwayY *= 0.9f;
        gunSwayPhase = 0;
    }
    
    if (isReloading) {
        reloadTimer += deltaTime;
        
        // Sound Logic
        if (reloadTimer > 0.1f && reloadStage == 0) { PlayReloadSound(0); reloadStage++; }
        if (reloadTimer > 1.4f && reloadStage == 1) { PlayReloadSound(1); reloadStage++; }
        if (reloadTimer > 2.2f && reloadStage == 2) { PlayReloadSound(2); reloadStage++; }
        
        if (reloadTimer < reloadDuration / 2) {
            gunReloadOffset = (reloadTimer / (reloadDuration / 2)) * 300;
        } else if (reloadTimer < reloadDuration) {
            gunReloadOffset = 300 - ((reloadTimer - reloadDuration / 2) / (reloadDuration / 2)) * 300;
        } else {
            isReloading = false;
            reloadTimer = 0;
            gunReloadOffset = 0;
            ammo = maxAmmo;
        }
    }
}

void StartReload() {
    if (isReloading || ammo == maxAmmo) return;
    isReloading = true;
    reloadTimer = 0;
    reloadStage = 0;
}

void ShootBullet() {
    if (fireTimer > 0 || isReloading || ammo <= 0) return;
    
    ammo--;
    
    Bullet b;
    b.x = player.x;
    b.y = player.y;
    b.dirX = cosf(player.angle);
    b.dirY = sinf(player.angle);
    b.speed = 20.0f;
    b.active = true;
    bullets.push_back(b);
    
    isFiring = true;
    fireTimer = 0.15f;
    
    PlayGunSound();
}

void UpdateBullets(float deltaTime) {
    if (isFiring && fireTimer < 0.1f) isFiring = false;
    
    for (auto& b : bullets) {
        if (!b.active) continue;
        
        b.x += b.dirX * b.speed * deltaTime;
        b.y += b.dirY * b.speed * deltaTime;
        
        int mx = (int)b.x;
        int my = (int)b.y;
        if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT || worldMap[mx][my] != 0) {
            b.active = false;
            continue;
        }
        
        for (auto& enemy : enemies) {
            if (!enemy.active) continue;
            float dx = b.x - enemy.x;
            float dy = b.y - enemy.y;
            if (sqrtf(dx*dx + dy*dy) < 0.8f) {
                enemy.active = false;
                b.active = false;
                
                score++;
                if (score > highScore) {
                    highScore = score;
                    SaveHighScore();
                }
                
                PlayScoreSound();
                
                scoreTimer = 3.0f;
                int msgIndex = rand() % 3;
                wcscpy(scoreMsg, praiseMsgs[msgIndex]);
                
                Enemy newEnemy;
                do {
                    newEnemy.x = 5.0f + (rand() % 540) / 10.0f;
                    newEnemy.y = 5.0f + (rand() % 540) / 10.0f;
                } while (sqrtf((newEnemy.x - player.x)*(newEnemy.x - player.x) + (newEnemy.y - player.y)*(newEnemy.y - player.y)) < 15.0f);
                newEnemy.active = true;
                newEnemy.speed = 1.5f + (rand() % 100) / 100.0f;
                newEnemy.distance = 0;
                enemies.push_back(newEnemy);
                break;
            }
        }
    }
    
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.active; }), bullets.end());
}

void DrawMinimap(HDC hdc) {
    int mapSize = 150;
    int cellSize = mapSize / MAP_WIDTH;
    if (cellSize < 1) cellSize = 1;
    int offsetX = SCREEN_WIDTH - mapSize - 10;
    int offsetY = 10;
    
    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 20));
    RECT bgRect = {offsetX - 2, offsetY - 2, offsetX + mapSize + 2, offsetY + mapSize + 2};
    FillRect(hdc, &bgRect, bgBrush);
    DeleteObject(bgBrush);
    
    int viewRange = 20;
    int startX = (int)player.x - viewRange/2;
    int startY = (int)player.y - viewRange/2;
    
    for (int dy = 0; dy < viewRange; dy++) {
        for (int dx = 0; dx < viewRange; dx++) {
            int mx = startX + dx;
            int my = startY + dy;
            if (mx >= 0 && mx < MAP_WIDTH && my >= 0 && my < MAP_HEIGHT) {
                RECT cell = {offsetX + dx * (mapSize/viewRange), offsetY + dy * (mapSize/viewRange),
                            offsetX + (dx + 1) * (mapSize/viewRange), offsetY + (dy + 1) * (mapSize/viewRange)};
                COLORREF color;
                if (worldMap[mx][my] == 2) color = RGB(0, 80, 0);
                else if (worldMap[mx][my] == 1) color = RGB(100, 60, 30);
                else color = RGB(40, 60, 30);
                HBRUSH brush = CreateSolidBrush(color);
                FillRect(hdc, &cell, brush);
                DeleteObject(brush);
            }
        }
    }
    
    int playerScreenX = offsetX + mapSize/2;
    int playerScreenY = offsetY + mapSize/2;
    HBRUSH playerBrush = CreateSolidBrush(RGB(0, 255, 0));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, playerBrush);
    Ellipse(hdc, playerScreenX - 3, playerScreenY - 3, playerScreenX + 3, playerScreenY + 3);
    SelectObject(hdc, oldBrush);
    DeleteObject(playerBrush);
    
    for (auto& enemy : enemies) {
        if (enemy.active) {
            int ex = offsetX + (int)((enemy.x - startX) * (mapSize/viewRange));
            int ey = offsetY + (int)((enemy.y - startY) * (mapSize/viewRange));
            if (ex > offsetX && ex < offsetX + mapSize && ey > offsetY && ey < offsetY + mapSize) {
                HBRUSH enemyBrush = CreateSolidBrush(RGB(255, 0, 0));
                oldBrush = (HBRUSH)SelectObject(hdc, enemyBrush);
                Ellipse(hdc, ex - 2, ey - 2, ex + 2, ey + 2);
                SelectObject(hdc, oldBrush);
                DeleteObject(enemyBrush);
            }
        }
    }
}

void UpdatePlayer(float deltaTime) {
    float moveSpeed = 4.0f * deltaTime;
    float rotSpeed = 2.5f * deltaTime;
    
    isMoving = false;
    
    if (keys['W'] || keys[VK_UP]) {
        float newX = player.x + cosf(player.angle) * moveSpeed;
        float newY = player.y + sinf(player.angle) * moveSpeed;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys['S'] || keys[VK_DOWN]) {
        float newX = player.x - cosf(player.angle) * moveSpeed;
        float newY = player.y - sinf(player.angle) * moveSpeed;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys['A']) {
        float strafeAngle = player.angle - PI / 2;
        float newX = player.x + cosf(strafeAngle) * moveSpeed;
        float newY = player.y + sinf(strafeAngle) * moveSpeed;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys['D']) {
        float strafeAngle = player.angle + PI / 2;
        float newX = player.x + cosf(strafeAngle) * moveSpeed;
        float newY = player.y + sinf(strafeAngle) * moveSpeed;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys[VK_LEFT]) player.angle -= rotSpeed;
    if (keys[VK_RIGHT]) player.angle += rotSpeed;
    
    if (keys[VK_SPACE] || keys[VK_LBUTTON]) ShootBullet();
    if (keys['R']) StartReload();
    
    static float stepTimer = 0;
    if (isMoving) {
        stepTimer -= deltaTime;
        if (stepTimer <= 0) {
            PlayStepSound();
            stepTimer = 0.4f;
        }
    } else {
        stepTimer = 0;
    }
}

void RenderGun() {
    if (!gunPixels || gunW <= 0 || gunH <= 0) return;
    
    int gunScale = 10;
    int gunDrawW = gunW * gunScale;
    int gunDrawH = gunH * gunScale;
    int gunX = SCREEN_WIDTH - gunDrawW + 20 + (int)gunSwayX;
    int gunY = SCREEN_HEIGHT - gunDrawH - 0 + (int)gunSwayY + (int)gunReloadOffset;
    
    DWORD* pixels = gunPixels;
    int srcW = gunW, srcH = gunH;
    
    if (isFiring && gunfirePixels && gunfireW > 0 && !isReloading) {
        pixels = gunfirePixels;
        srcW = gunfireW;
        srcH = gunfireH;
        gunDrawW = srcW * gunScale;
        gunDrawH = srcH * gunScale;
        gunX = SCREEN_WIDTH - gunDrawW + 20 + (int)gunSwayX;
        gunY = SCREEN_HEIGHT - gunDrawH - 0 + (int)gunSwayY + (int)gunReloadOffset;
    }
    
    for (int y = 0; y < gunDrawH; y++) {
        int screenY = gunY + y;
        if (screenY < 0 || screenY >= SCREEN_HEIGHT) continue;
        int srcY = y * srcH / gunDrawH;
        
        for (int x = 0; x < gunDrawW; x++) {
            int screenX = gunX + x;
            if (screenX < 0 || screenX >= SCREEN_WIDTH) continue;
            int srcX = x * srcW / gunDrawW;
            
            DWORD col = pixels[srcY * srcW + srcX];
            int a = (col >> 24) & 0xFF;
            if (a == 0) continue;
            
            int b = (col >> 0) & 0xFF;
            int g = (col >> 8) & 0xFF;
            int r = (col >> 16) & 0xFF;
            backBufferPixels[screenY * SCREEN_WIDTH + screenX] = MakeColor(r, g, b);
        }
    }
}

void RenderGame(HDC hdc) {
    CastRays();
    RenderClouds();
    RenderSprites();
    RenderGun();
    
    int hbIndex = player.health / 10;
    if (hbIndex > 10) hbIndex = 10;
    if (hbIndex < 0) hbIndex = 0;
    if (healthbarPixels[hbIndex] && healthbarW > 0 && healthbarH > 0) {
        int hbScale = 5;
        int hbDrawW = healthbarW * hbScale;
        int hbDrawH = healthbarH * hbScale;
        int hbX = 10;
        int hbY = 70;
        
        for (int y = 0; y < hbDrawH; y++) {
            int screenY = hbY + y;
            if (screenY < 0 || screenY >= SCREEN_HEIGHT) continue;
            int srcY = y * healthbarH / hbDrawH;
            
            for (int x = 0; x < hbDrawW; x++) {
                int screenX = hbX + x;
                if (screenX < 0 || screenX >= SCREEN_WIDTH) continue;
                int srcX = x * healthbarW / hbDrawW;
                
                DWORD col = healthbarPixels[hbIndex][srcY * healthbarW + srcX];
                int a = (col >> 24) & 0xFF;
                if (a == 0) continue;
                
                int b = (col >> 0) & 0xFF;
                int g = (col >> 8) & 0xFF;
                int r = (col >> 16) & 0xFF;
                backBufferPixels[screenY * SCREEN_WIDTH + screenX] = MakeColor(r, g, b);
            }
        }
    }
    
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = SCREEN_WIDTH;
    bi.bmiHeader.biHeight = -SCREEN_HEIGHT;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    SetDIBitsToDevice(hdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, 
        backBufferPixels, &bi, DIB_RGB_COLORS);
    
    DrawMinimap(hdc);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 0));
    TextOutW(hdc, 10, 10, loadStatus, (int)wcslen(loadStatus));
    
    wchar_t ammoText[64];
    if (isReloading) {
        swprintf(ammoText, 64, L"RELOADING...");
        SetTextColor(hdc, RGB(255, 255, 0));
    } else {
        swprintf(ammoText, 64, L"Ammo: %d/%d", ammo, maxAmmo);
        SetTextColor(hdc, ammo == 0 ? RGB(255, 0, 0) : RGB(255, 255, 255));
    }
    TextOutW(hdc, 10, 50, ammoText, (int)wcslen(ammoText));
    
    wchar_t scoreText[128];
    swprintf(scoreText, 128, L"Score: %d  High Score: %d", score, highScore);
    SetTextColor(hdc, RGB(255, 255, 255));
    TextOutW(hdc, 10, 90, scoreText, (int)wcslen(scoreText));
    
    if (scoreTimer > 0) {
        HFONT hFont = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        
        SetTextColor(hdc, RGB(255, 215, 0)); // Gold color
        SetBkMode(hdc, TRANSPARENT);
        
        wchar_t pointText[] = L"+1";
        SIZE size;
        GetTextExtentPoint32W(hdc, pointText, 2, &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, (SCREEN_HEIGHT - size.cy) / 2 - 40, pointText, 2);
        
        GetTextExtentPoint32W(hdc, scoreMsg, (int)wcslen(scoreMsg), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, (SCREEN_HEIGHT - size.cy) / 2 + 10, scoreMsg, (int)wcslen(scoreMsg));
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }

    
    SetTextColor(hdc, RGB(255, 255, 255));
    wchar_t info[128];
    swprintf(info, 128, L"WASD=Move | Arrows=Look | SPACE=Shoot | R=Reload | ESC=Quit");
    TextOutW(hdc, 10, SCREEN_HEIGHT - 25, info, (int)wcslen(info));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            backBufferPixels = new DWORD[SCREEN_WIDTH * SCREEN_HEIGHT];
            zBuffer = new float[SCREEN_WIDTH];
            SetTimer(hwnd, 1, 16, NULL);
            return 0;
        }
        case WM_TIMER: {
            static DWORD lastTime = GetTickCount();
            DWORD currentTime = GetTickCount();
            float deltaTime = (currentTime - lastTime) / 1000.0f;
            lastTime = currentTime;
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            
            if (scoreTimer > 0) scoreTimer -= deltaTime;
            
            UpdatePlayer(deltaTime);
            UpdateEnemies(deltaTime);
            UpdateClouds(deltaTime);
            UpdateGun(deltaTime);
            UpdateBullets(deltaTime);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderGame(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
            keys[wParam & 0xFF] = true;
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        case WM_KEYUP:
            keys[wParam & 0xFF] = false;
            return 0;
        case WM_DESTROY:
            musicRunning = false;
            CleanupAudio();
            KillTimer(hwnd, 1);
            delete[] backBufferPixels;
            delete[] zBuffer;
            if (grassPixels) delete[] grassPixels;
            if (npcPixels) delete[] npcPixels;
            if (treePixels) delete[] treePixels;
            if (cloudPixels) delete[] cloudPixels;
            if (gunPixels) delete[] gunPixels;
            if (gunfirePixels) delete[] gunfirePixels;
            if (bulletPixels) delete[] bulletPixels;
            for (int i = 0; i < 11; i++) if (healthbarPixels[i]) delete[] healthbarPixels[i];
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    
    LoadHighScore();
    TryLoadAssets();
    GenerateWorld();
    SpawnEnemies();
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"LoneShooterClass";
    RegisterClassExW(&wc);
    
    InitAudio();
    
    RECT windowRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
    
    hMainWnd = CreateWindowExW(0, L"LoneShooterClass", L"LoneShooter - Open World Survival",
        (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, 
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
        NULL, NULL, hInstance, NULL);
    
    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);
    
    _beginthread(BackgroundMusic, 0, NULL);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
