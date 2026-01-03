/*
 * LoneShooter - Open World 2.5D Raycaster
 * Compile: g++ -o cmds/LoneShooter.exe loneshooter.cpp -lgdi32 -lwinmm -mwindows -O2
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
#include <string>
#include <algorithm>
#include <process.h>
#include <mmsystem.h>

volatile bool musicRunning = true;
extern bool bossActive;
extern bool preBossPhase;
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
    NoteOn(3, 84, 127);
}

void PlaySlamSound() {
    static wchar_t slamPath[MAX_PATH] = {0};
    if (slamPath[0] == 0) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        swprintf(slamPath, MAX_PATH, L"\"%ls\\assets\\sound-effects\\claw-impact.mp3\"", exePath);
    }
    
    wchar_t cmd[512];
    mciSendStringW(L"close slamsfx", NULL, 0, NULL);
    swprintf(cmd, 512, L"open %ls type mpegvideo alias slamsfx", slamPath);
    mciSendStringW(cmd, NULL, 0, NULL);
    mciSendStringW(L"setaudio slamsfx volume to 1000", NULL, 0, NULL);
    mciSendStringW(L"play slamsfx from 0", NULL, 0, NULL);
}

void BackgroundMusic(void* arg) {
    const int E2 = 40;
    const int E3 = 52; 
    const int D3 = 50;
    const int C3 = 48;
    const int B2 = 47;
    const int AS2 = 46;
    const int A2 = 45;

    while (musicRunning) {
        if (preBossPhase) {
            // Silence during buildup
            Sleep(100);
            continue;
        }
        if (bossActive) {
            // Scary Boss Music: Low drones, dissonant chords, fast tempo
            SetInstrument(0, 30); // Distortion Guitar
            SetInstrument(1, 32); // Acoustic Bass
            
            // Minor 2nd drone - very unsettling
            NoteOn(1, 28, 100); // E1 low drone
            NoteOn(1, 29, 80);  // F1 - dissonant with E
            
            for (int i = 0; i < 8 && musicRunning && bossActive; i++) {
                // Staccato power chords descending chromatically
                int note = E3 - i;
                NoteOn(0, note, 120);
                NoteOn(0, note + 6, 120); // Tritone - devil's interval
                Sleep(100);
                NoteOff(0, note);
                NoteOff(0, note + 6);
                
                // Drum hits
                NoteOn(9, 36, 127); // Kick
                Sleep(100);
            }
            
            NoteOff(1, 28);
            NoteOff(1, 29);
            
            // Crash and rebuild tension
            NoteOn(9, 49, 127); // Crash
            NoteOn(9, 38, 127); // Snare
            Sleep(200);
        } else {
            // Normal Action Music
            int riff[] = { E2, E3, E2, D3, E2, C3, E2, AS2, E2, B2, E2 };
            
            NoteOn(1, E2-12, 100);

            for (int i = 0; i < 11; i++) {
                if (!musicRunning || bossActive) break;
                int note = riff[i];
                
                NoteOn(0, note, 110);
                NoteOn(0, note + 7, 110);
                
                Sleep(150);
                
                NoteOff(0, note);
                NoteOff(0, note + 7);
                
                if (i < 10) {
                     NoteOn(0, E2, 80);
                     NoteOn(0, E2+7, 80);
                     Sleep(150);
                     NoteOff(0, E2);
                     NoteOff(0, E2+7);
                }
            }
            
            NoteOn(9, 38, 127);
            Sleep(150);
            NoteOn(9, 38, 127);
            NoteOn(9, 49, 127);
            Sleep(150);
            
            NoteOff(1, E2-12);
        }
    }
}

const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 768;
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
    int spriteIndex;
    int health;
    float hurtTimer;
    bool isShooter;
    float fireTimer;
    float firingTimer;
};

struct EnemyBullet {
    float x, y;
    float dirX, dirY;
    float speed;
    bool active;
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

enum ClawState { CLAW_DORMANT, CLAW_IDLE, CLAW_CHASING, CLAW_SLAMMING, CLAW_RISING, CLAW_RETURNING };

struct Claw {
    float x, y;
    float homeX, homeY;
    float groundY;
    ClawState state;
    float timer;
    int index;
    bool dealtDamage;
};

// --- 3D Engine Structs (Ported from LoneMaker) ---
struct Vec3 { float x, y, z; };
struct Mat4 { float m[4][4]; };
struct Vertex { Vec3 pos; };
struct Triangle { int p1, p2, p3; DWORD color; bool selected; };
struct Object3D {
    Vec3 pos;
    Vec3 rot;
    std::vector<Vertex> verts;
    std::vector<Triangle> tris;
};

// --- 3D Math ---
Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 Mul(Vec3 v, float s) { return {v.x*s, v.y*s, v.z*s}; }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
float Length(Vec3 v) { return sqrtf(Dot(v, v)); }
Vec3 Normalize(Vec3 v) { float l = Length(v); if(l==0) return {0,0,0}; return Mul(v, 1.0f/l); }

Mat4 MatrixIdentity() {
    Mat4 mat = {0};
    mat.m[0][0] = 1; mat.m[1][1] = 1; mat.m[2][2] = 1; mat.m[3][3] = 1;
    return mat;
}
Mat4 MatrixRotationY(float angle) {
    Mat4 mat = MatrixIdentity();
    mat.m[0][0] = cosf(angle); mat.m[0][2] = -sinf(angle);
    mat.m[2][0] = sinf(angle); mat.m[2][2] = cosf(angle);
    return mat;
}
Mat4 MatrixRotationX(float angle) {
    Mat4 mat = MatrixIdentity();
    mat.m[1][1] = cosf(angle); mat.m[1][2] = -sinf(angle);
    mat.m[2][1] = sinf(angle); mat.m[2][2] = cosf(angle);
    return mat;
}
Mat4 MatrixTranslation(float x, float y, float z) {
    Mat4 mat = MatrixIdentity();
    mat.m[3][0] = x; mat.m[3][1] = y; mat.m[3][2] = z;
    return mat;
}
Mat4 MatrixPerspective(float fov, float aspect, float znear, float zfar) {
    Mat4 mat = {0};
    float tanHalf = tanf(fov / 2.0f);
    mat.m[0][0] = 1.0f / (aspect * tanHalf);
    mat.m[1][1] = 1.0f / tanHalf;
    mat.m[2][2] = zfar / (zfar - znear);
    mat.m[2][3] = 1.0f;
    mat.m[3][2] = (-zfar * znear) / (zfar - znear);
    return mat;
}
Mat4 MatrixMultiply(Mat4 a, Mat4 b) {
    Mat4 c = {0};
    for(int i=0; i<4; i++) for(int j=0; j<4; j++) for(int k=0; k<4; k++)
        c.m[i][j] += a.m[i][k] * b.m[k][j];
    return c;
}
Vec3 TransformPoint(Mat4 m, Vec3 i) {
    Vec3 o;
    o.x = i.x * m.m[0][0] + i.y * m.m[1][0] + i.z * m.m[2][0] + m.m[3][0];
    o.y = i.x * m.m[0][1] + i.y * m.m[1][1] + i.z * m.m[2][1] + m.m[3][1];
    o.z = i.x * m.m[0][2] + i.y * m.m[1][2] + i.z * m.m[2][2] + m.m[3][2];
    float w = i.x * m.m[0][3] + i.y * m.m[1][3] + i.z * m.m[2][3] + m.m[3][3];
    if (w != 0.0f) { o.x /= w; o.y /= w; o.z /= w; }
    return o;
}
float EdgeFunc(int x1, int y1, int x2, int y2, int px, int py) {
    return (float)((px - x1) * (y2 - y1) - (py - y1) * (x2 - x1));
}

// Spawn Player at (10, 32) facing East (0.0) towards center (32, 32)

struct Fireball {
    float x, y;
    float dirX, dirY;
    float speed;
    bool active;
};

struct Medkit {
    float x, y;
    bool active;
    float respawnTimer;
    static const float RESPAWN_TIME;
    static const int HEAL_AMOUNT = 20;
};
const float Medkit::RESPAWN_TIME = 20.0f;

Player player = {10.0f, 32.0f, 0.0f, 0.0f, 100};
std::vector<Enemy> enemies;
std::vector<TreeSprite> trees;
std::vector<Cloud> clouds;
std::vector<Bullet> bullets;
std::vector<Fireball> fireballs;
std::vector<EnemyBullet> enemyBullets;
Medkit medkit = {0, 0, false, 0};

bool bossActive = false;
bool preBossPhase = false;
float preBossTimer = 0;
float bossEventTimer = 0;
float fireballSpawnTimer = 0;
int bossHealth = 200;
float bossHurtTimer = 0;
float playerHurtTimer = 0;
bool bossDead = false;
bool victoryScreen = false;
float screenShakeTimer = 0;
float screenShakeIntensity = 0;
float shooterSpawnTimer = 3.0f;

bool consoleActive = false;
std::wstring consoleBuffer = L"";

std::vector<Object3D> scene3D;
float* zBuffer = nullptr;

// Prototypes
void LoadModelCurrentDir(const wchar_t* filename, float x, float z);
void Render3DScene();

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

void GetHighScorePath(wchar_t* path) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastBackSlash = wcsrchr(exePath, L'\\');
    wchar_t* lastForwardSlash = wcsrchr(exePath, L'/');
    wchar_t* lastSlash = lastBackSlash;
    if (lastForwardSlash && (!lastSlash || lastForwardSlash > lastSlash)) lastSlash = lastForwardSlash;
    if (lastSlash) *lastSlash = L'\0';
    swprintf(path, MAX_PATH, L"%ls\\highscore.dat", exePath);
}

void LoadHighScore() {
    wchar_t path[MAX_PATH];
    GetHighScorePath(path);
    FILE* f = _wfopen(path, L"rb");
    if (f) {
        fread(&highScore, sizeof(int), 1, f);
        fclose(f);
    }
}

void SaveHighScore() {
    wchar_t path[MAX_PATH];
    GetHighScorePath(path);
    FILE* f = _wfopen(path, L"wb");
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
DWORD* enemyPixels[5] = {NULL};
int enemyW[5] = {0};
int enemyH[5] = {0};
DWORD* enemy5HurtPixels = NULL;
int enemy5HurtW = 0, enemy5HurtH = 0;
DWORD* gunnerPixels = NULL;
int gunnerW = 0, gunnerH = 0;
DWORD* gunnerFiringPixels = NULL;
int gunnerFiringW = 0, gunnerFiringH = 0;
int treeW = 0, treeH = 0;
int cloudW = 0, cloudH = 0;
int gunW = 0, gunH = 0;
int gunfireW = 0, gunfireH = 0;
int bulletW = 0, bulletH = 0;
int healthbarW = 0, healthbarH = 0;
DWORD* spirePixels = NULL;
int spireW = 0, spireH = 0;
DWORD* spireAwakePixels = NULL;
int spireAwakeW = 0, spireAwakeH = 0;
DWORD* spireHurtPixels = NULL;
int spireHurtW = 0, spireHurtH = 0;
DWORD* spireDeathPixels = NULL;
int spireDeathW = 0, spireDeathH = 0;
DWORD* fireballPixels = NULL;
int fireballW = 0, fireballH = 0;
DWORD* medkitPixels = NULL;
int medkitW = 0, medkitH = 0;

DWORD* clawDormantPixels = NULL;
int clawDormantW = 0, clawDormantH = 0;
DWORD* clawActivePixels = NULL;
int clawActiveW = 0, clawActiveH = 0;

Claw claws[6];
int activeClawIndex = 0;
float clawReturnSpeed = 3.0f;

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
    
    for(int i=0; i<5; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\enemy%d.bmp", exePath, i+1);
        enemyPixels[i] = LoadBMPPixels(path, &enemyW[i], &enemyH[i]);
    }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\enemy5_hurt.bmp", exePath);
    enemy5HurtPixels = LoadBMPPixels(path, &enemy5HurtW, &enemy5HurtH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gunner.bmp", exePath);
    gunnerPixels = LoadBMPPixels(path, &gunnerW, &gunnerH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gunner_firing.bmp", exePath);
    gunnerFiringPixels = LoadBMPPixels(path, &gunnerFiringW, &gunnerFiringH);
    
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
    
    // Load Spire Sprite
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\spire_resting.bmp", exePath);
    spirePixels = LoadBMPPixels(path, &spireW, &spireH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\spire_awake.bmp", exePath);
    spireAwakePixels = LoadBMPPixels(path, &spireAwakeW, &spireAwakeH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\Spire_hurt.bmp", exePath);
    spireHurtPixels = LoadBMPPixels(path, &spireHurtW, &spireHurtH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\Spire_Death.bmp", exePath);
    spireDeathPixels = LoadBMPPixels(path, &spireDeathW, &spireDeathH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\fireball.bmp", exePath);
    fireballPixels = LoadBMPPixels(path, &fireballW, &fireballH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\items\\Medkit.bmp", exePath);
    medkitPixels = LoadBMPPixels(path, &medkitW, &medkitH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_dormant.bmp", exePath);
    clawDormantPixels = LoadBMPPixels(path, &clawDormantW, &clawDormantH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_active.bmp", exePath);
    clawActivePixels = LoadBMPPixels(path, &clawActiveW, &clawActiveH);

    swprintf(loadStatus, 256, L"G:%ls S:%ls A:%ls H:%ls D:%ls F:%ls M:%ls C:%ls", gunPixels?L"OK":L"X", spirePixels?L"OK":L"X", spireAwakePixels?L"OK":L"X", spireHurtPixels?L"OK":L"X", spireDeathPixels?L"OK":L"X", fireballPixels?L"OK":L"X", medkitPixels?L"OK":L"X", clawDormantPixels?L"OK":L"X");
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

void SpawnMedkit() {
    do {
        medkit.x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
        medkit.y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
    } while (worldMap[(int)medkit.x][(int)medkit.y] != 0 || 
             sqrtf((medkit.x - 32)*(medkit.x - 32) + (medkit.y - 32)*(medkit.y - 32)) < 5.0f);
    medkit.active = true;
    medkit.respawnTimer = 0;
}

void InitClaws() {
    for(int i = 0; i < 6; i++) {
        float angle = (i * 60.0f) * (PI / 180.0f);
        claws[i].homeX = 32.0f + cosf(angle) * 16.0f;
        claws[i].homeY = 32.0f + sinf(angle) * 16.0f;
        claws[i].x = claws[i].homeX;
        claws[i].y = claws[i].homeY;
        claws[i].groundY = claws[i].homeY;
        claws[i].state = CLAW_DORMANT;
        claws[i].timer = 0;
        claws[i].index = i;
        claws[i].dealtDamage = false;
    }
    activeClawIndex = 0;
}

void SpawnEnemies() {
    enemies.clear();
    enemyBullets.clear();
    
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
        enemy.spriteIndex = rand() % 5;
        if (enemy.spriteIndex == 4) {
            enemy.health = 4;
        } else {
            enemy.health = 1;
        }
        enemy.hurtTimer = 0;
        enemy.isShooter = false;
        enemy.fireTimer = 0;
        enemy.firingTimer = 0;
        enemies.push_back(enemy);
    }
}

inline DWORD MakeColor(int r, int g, int b) {
    return (r << 16) | (g << 8) | b;
}

inline DWORD BlendWithFog(int r, int g, int b, float dist, float fogStart, float fogEnd) {
    // Fog color (gray-blue for normal, dark red for boss)
    int fogR = bossActive ? 40 : 80;
    int fogG = bossActive ? 20 : 85;
    int fogB = bossActive ? 20 : 90;
    
    float fogFactor = (dist - fogStart) / (fogEnd - fogStart);
    if (fogFactor < 0) fogFactor = 0;
    if (fogFactor > 1) fogFactor = 1;
    
    r = (int)(r * (1 - fogFactor) + fogR * fogFactor);
    g = (int)(g * (1 - fogFactor) + fogG * fogFactor);
    b = (int)(b * (1 - fogFactor) + fogB * fogFactor);
    
    return MakeColor(r, g, b);
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
        while (!hitWall && distanceToWall < 90.0f) {
            distanceToWall += stepSize;
            int testX = (int)(player.x + rayDirX * distanceToWall);
            int testY = (int)(player.y + rayDirY * distanceToWall);
            
            if (testX < 0 || testX >= MAP_WIDTH || testY < 0 || testY >= MAP_HEIGHT) {
                hitWall = true;
                distanceToWall = 90.0f;
                wallType = 3;
            } else if (worldMap[testX][testY] > 0) {
                hitWall = true;
                wallType = worldMap[testX][testY];
            }
        }
        
        float correctedDist = distanceToWall * cosf(rayAngle - player.angle);
        
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
                // Sky
                float skyGradient = (float)y / (SCREEN_HEIGHT / 2);
                int r, g, b;
                
                if (bossActive) {
                    // Red Sky
                    r = (int)(150 + 100 * (1 - skyGradient));
                    g = (int)(20 * (1 - skyGradient));
                    b = (int)(20 * (1 - skyGradient));
                } else {
                    r = (int)(30 + 80 * (1 - skyGradient));
                    g = (int)(60 + 120 * (1 - skyGradient));
                    b = (int)(100 + 155 * (1 - skyGradient));
                }
                
                backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(r, g, b);
                zBuffer[y * SCREEN_WIDTH + x] = 1000.0f; // Infinite depth
            }
            
            if (y > SCREEN_HEIGHT / 2 + (int)player.pitch) {
                // Floor
                float rowDist = (SCREEN_HEIGHT / 2.0f) / (y - SCREEN_HEIGHT / 2.0f);
                float floorX = player.x + cosf(rayAngle) * rowDist;
                float floorY = player.y + sinf(rayAngle) * rowDist;
                
                if (grassPixels && grassW > 0) {
                    int texX = (int)(fmodf(floorX, 1.0f) * grassW);
                    int texY = (int)(fmodf(floorY, 1.0f) * grassH);
                    if (texX < 0) texX += grassW;
                    if (texY < 0) texY += grassH;
                    texX %= grassW; texY %= grassH;
                    DWORD col = grassPixels[texY * grassW + texX];
                    int bb = (col >> 0) & 0xFF;
                    int gg = (col >> 8) & 0xFF;
                    int rr = (col >> 16) & 0xFF;
                    float shade = 1.0f - (rowDist / 20.0f);
                    if (shade < 0.15f) shade = 0.15f;
                    backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(
                        (int)(rr * shade), (int)(gg * shade), (int)(bb * shade));
                } else {
                    float shade = 1.0f - (rowDist / 40.0f);
                    if (shade < 0.1f) shade = 0.1f;
                    int c = (int)(80 * shade);
                    backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(c/2, c, c/2);
                }
                
                zBuffer[y * SCREEN_WIDTH + x] = rowDist;
            }
            
            if (wallType != 3 && y >= ceiling && y <= floorLine) {
                // Wall
                float shade = 1.0f - (correctedDist / 50.0f);
                if (shade < 0.1f) shade = 0.1f;
                int r, g, b;
                if (wallType == 2) {
                    r = (int)(60 * shade); g = (int)(100 * shade); b = (int)(40 * shade);
                } else {
                    r = (int)(140 * shade); g = (int)(100 * shade); b = (int)(60 * shade);
                }
                backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(r, g, b);
                
                zBuffer[y * SCREEN_WIDTH + x] = correctedDist;
            }
        }
    }
}

// --- 3D Rasterizer ---
void RasterizeTri(Vec3 v1, Vec3 v2, Vec3 v3, DWORD color) {
    int x1 = (int)((v1.x + 1) * 0.5f * SCREEN_WIDTH);
    int y1 = (int)((1 - v1.y) * 0.5f * SCREEN_HEIGHT);
    int x2 = (int)((v2.x + 1) * 0.5f * SCREEN_WIDTH);
    int y2 = (int)((1 - v2.y) * 0.5f * SCREEN_HEIGHT);
    int x3 = (int)((v3.x + 1) * 0.5f * SCREEN_WIDTH);
    int y3 = (int)((1 - v3.y) * 0.5f * SCREEN_HEIGHT);
    
    int minX = std::max(0, std::min(x1, std::min(x2, x3)));
    int minY = std::max(0, std::min(y1, std::min(y2, y3)));
    int maxX = std::min(SCREEN_WIDTH-1, std::max(x1, std::max(x2, x3)));
    int maxY = std::min(SCREEN_HEIGHT-1, std::max(y1, std::max(y2, y3)));
    
    float area = EdgeFunc(x1, y1, x2, y2, x3, y3);
    if(area == 0) return;
    
    for(int y=minY; y<=maxY; y++) {
        for(int x=minX; x<=maxX; x++) {
            float w0 = EdgeFunc(x2, y2, x3, y3, x, y);
            float w1 = EdgeFunc(x3, y3, x1, y1, x, y);
            float w2 = EdgeFunc(x1, y1, x2, y2, x, y);
            
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            
            if(inside) {
                w0/=area; w1/=area; w2/=area;
                float z = 1.0f / (w0/v1.z + w1/v2.z + w2/v3.z);
                
                // Z-Buffer Check
                if(z < zBuffer[y * SCREEN_WIDTH + x]) {
                    zBuffer[y * SCREEN_WIDTH + x] = z;
                    backBufferPixels[y * SCREEN_WIDTH + x] = color;
                }
            }
        }
    }
}

void LoadModelCurrentDir(const wchar_t* filename, float x, float z) {
    // Helper to find file in current dir or assets
    FILE* f = _wfopen(filename, L"rb");
    if(!f) return;
    
    int magic, objCount;
    fread(&magic, 4, 1, f);
    fread(&objCount, 4, 1, f);
    for(int i=0; i<objCount; i++) {
        Object3D obj;
        fread(&obj.pos, sizeof(Vec3), 1, f);
        fread(&obj.rot, sizeof(Vec3), 1, f);
        
        // Offset & Scale
        float scale = 5.0f;
        obj.pos.x = (obj.pos.x * scale) + x;
        obj.pos.y = (obj.pos.y * scale); // Ground level
        obj.pos.z = (obj.pos.z * scale) + z;
        
        for(auto& v : obj.verts) {
            v.pos = Mul(v.pos, scale);
        }
        
        int vCount, tCount;
        fread(&vCount, 4, 1, f);
        fread(&tCount, 4, 1, f);
        obj.verts.resize(vCount);
        obj.tris.resize(tCount);
        fread(obj.verts.data(), sizeof(Vertex), vCount, f);
        fread(obj.tris.data(), sizeof(Triangle), tCount, f);
        scene3D.push_back(obj);
    }
    fclose(f);
}

void Render3DScene() {
    Vec3 lightDir = Normalize({0.5f, 1.0f, -0.5f});
    
    Mat4 matTrans = MatrixTranslation(-player.x, -2.0f, -player.y);
    
    Mat4 matRotY = MatrixRotationY(-player.angle + PI/2);
    Mat4 matRotX = MatrixRotationX(-player.pitch/100.0f);
    Mat4 matProj = MatrixPerspective(FOV, (float)SCREEN_WIDTH/SCREEN_HEIGHT, 0.1f, 100.0f);
    
    Mat4 matView = MatrixMultiply(matRotX, MatrixMultiply(matRotY, matTrans));
    // Actually MatrixMultiply order: M = R * T
    
    for(auto& obj : scene3D) {
        Mat4 modelMat = MatrixMultiply(MatrixRotationY(obj.rot.y), MatrixTranslation(obj.pos.x, obj.pos.y, obj.pos.z));
        // We accumulate transformation
        
        for(auto& tri : obj.tris) {
            Vec3 v1 = TransformPoint(modelMat, obj.verts[tri.p1].pos);
            Vec3 v2 = TransformPoint(modelMat, obj.verts[tri.p2].pos);
            Vec3 v3 = TransformPoint(modelMat, obj.verts[tri.p3].pos);
            
            // Lighting
            Vec3 normal = Normalize(Cross(Sub(v2,v1), Sub(v3,v1)));
            float intensity = Dot(normal, lightDir);
            if(intensity < 0.2f) intensity = 0.2f;
            
            // View Points
            Vec3 tv1 = TransformPoint(matView, v1);
            Vec3 tv2 = TransformPoint(matView, v2);
            Vec3 tv3 = TransformPoint(matView, v3);
            
            // Clip
            if(tv1.z < 0.1f || tv2.z < 0.1f || tv3.z < 0.1f) continue;
            
            // Project
            Vec3 p1 = TransformPoint(matProj, tv1);
            Vec3 p2 = TransformPoint(matProj, tv2);
            Vec3 p3 = TransformPoint(matProj, tv3);
            
            DWORD c = tri.color;
            int r = (c >> 16) & 0xFF; int g = (c >> 8) & 0xFF; int b = (c) & 0xFF;
            r*=intensity; g*=intensity; b*=intensity;
            DWORD litColor = (r<<16)|(g<<8)|b;
            
            RasterizeTri(p1, p2, p3, litColor);
        }
    }
}

void RenderSprite(DWORD* pixels, int pxW, int pxH, float sx, float sy, float dist, float scale, float heightOffset = 0.0f) {
    if (dist < 0.5f || dist > 50.0f) return;
    
    float dx = sx - player.x;
    float dy = sy - player.y;
    float spriteAngle = atan2f(dy, dx) - player.angle;
    while (spriteAngle > PI) spriteAngle -= 2 * PI;
    while (spriteAngle < -PI) spriteAngle += 2 * PI;
    if (fabsf(spriteAngle) > FOV) return;
    
    float spriteScreenX = (0.5f + spriteAngle / FOV) * SCREEN_WIDTH;
    float spriteHeight = (SCREEN_HEIGHT / dist) * scale;
    float spriteWidth = spriteHeight;
    
    int floorLineAtDist = SCREEN_HEIGHT / 2 + (int)((SCREEN_HEIGHT / 2.0f) / dist) + (int)player.pitch;
    int verticalOffset = (int)((heightOffset * SCREEN_HEIGHT) / dist);
    int drawEndY = floorLineAtDist - verticalOffset;
    int drawStartY = (int)(drawEndY - spriteHeight);
    int drawStartX = (int)(spriteScreenX - spriteWidth / 2);
    int drawEndX = (int)(spriteScreenX + spriteWidth / 2);
    
    for (int x = drawStartX; x < drawEndX; x++) {
        if (x < 0 || x >= SCREEN_WIDTH) continue;
        
        float texX = (float)(x - drawStartX) / spriteWidth;
        
        for (int y = drawStartY; y < drawEndY; y++) {
            if (y < 0 || y >= SCREEN_HEIGHT) continue;
            
            if (dist > zBuffer[y * SCREEN_WIDTH + x]) continue;
            
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
    struct SpriteRender { float x, y, dist; int type; float scale; int variant; bool isHurt; float height; bool isFiring; };
    std::vector<SpriteRender> allSprites;
    
    DWORD* sPix;
    int sW, sH;
    if (bossDead) {
        sPix = spireDeathPixels;
        sW = spireDeathW;
        sH = spireDeathH;
    } else if (bossHurtTimer > 0 && bossActive) {
        sPix = spireHurtPixels;
        sW = spireHurtW;
        sH = spireHurtH;
    } else if (bossActive) {
        sPix = spireAwakePixels;
        sW = spireAwakeW;
        sH = spireAwakeH;
    } else {
        sPix = spirePixels;
        sW = spireW;
        sH = spireH;
    }
    
    float dx = 32.0f - player.x;
    float dy = 32.0f - player.y;
    float dist = sqrtf(dx*dx + dy*dy);
    allSprites.push_back({32.0f, 32.0f, dist, 2, 8.0f, 0, false, 0.0f, false});

    for(auto& fb : fireballs) {
        if(!fb.active) continue;
        float fdx = fb.x - player.x;
        float fdy = fb.y - player.y;
        float fdist = sqrtf(fdx*fdx + fdy*fdy);
        allSprites.push_back({fb.x, fb.y, fdist, 3, 2.0f, 0, false, 0.0f, false});
    }
    
    if (medkit.active) {
        float mdx = medkit.x - player.x;
        float mdy = medkit.y - player.y;
        float mdist = sqrtf(mdx*mdx + mdy*mdy);
        allSprites.push_back({medkit.x, medkit.y, mdist, 4, 0.8f, 0, false, 0.0f, false});
    }

    for (auto& tree : trees) {
        float dx = tree.x - player.x;
        float dy = tree.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 50.0f) {
            allSprites.push_back({tree.x, tree.y, dist, 0, 1.0f, 0, false, 0.0f, false});
        }
    }
    
    for (auto& enemy : enemies) {
        if (enemy.active) {
            float dx = enemy.x - player.x;
            float dy = enemy.y - player.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (enemy.isShooter) {
                allSprites.push_back({enemy.x, enemy.y, dist, 6, 1.0f, 0, false, 0.0f, enemy.firingTimer > 0});
            } else {
                allSprites.push_back({enemy.x, enemy.y, dist, 1, 1.0f, enemy.spriteIndex, (enemy.spriteIndex == 4 && enemy.hurtTimer > 0), 0.0f, false});
            }
        }
    }
    
    for (auto& eb : enemyBullets) {
        if (eb.active) {
            float dx = eb.x - player.x;
            float dy = eb.y - player.y;
            float dist = sqrtf(dx*dx + dy*dy);
            allSprites.push_back({eb.x, eb.y, dist, 7, 0.5f, 0, false, 0.0f, false});
        }
    }
    
    for(int i = 0; i < 6; i++) {
        float cdx = claws[i].x - player.x;
        float cdy = claws[i].y - player.y;
        float cdist = sqrtf(cdx*cdx + cdy*cdy);
        int clawVariant = (claws[i].state == CLAW_DORMANT || bossDead) ? 0 : 1;
        
        float clawHeight = 3.0f;
        if (claws[i].state == CLAW_SLAMMING) {
            float progress = 1.0f - (claws[i].timer / 0.5f);
            if (progress < 0) progress = 0;
            if (progress > 1) progress = 1;
            clawHeight = 3.0f * (1.0f - progress);
        } else if (claws[i].state == CLAW_RISING) {
            float progress = 1.0f - (claws[i].timer / 1.0f);
            if (progress < 0) progress = 0;
            if (progress > 1) progress = 1;
            clawHeight = 3.0f * progress;
        } else if (claws[i].state == CLAW_RETURNING) {
            clawHeight = 3.0f;
        }
        
        allSprites.push_back({claws[i].x, claws[i].y, cdist, 5, 4.0f, clawVariant, false, clawHeight, false});
    }
    
    std::sort(allSprites.begin(), allSprites.end(), [](const SpriteRender& a, const SpriteRender& b) {
        return a.dist > b.dist;
    });
    
    for (auto& sp : allSprites) {
        if (sp.type == 0) {
            RenderSprite(treePixels, treeW, treeH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
        } else if (sp.type == 1) {
            if (sp.isHurt) {
                RenderSprite(enemy5HurtPixels, enemy5HurtW, enemy5HurtH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else {
                int idx = sp.variant;
                if (idx < 0) idx = 0; if (idx > 4) idx = 4;
                RenderSprite(enemyPixels[idx], enemyW[idx], enemyH[idx], sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 2) {
            RenderSprite(sPix, sW, sH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
        } else if (sp.type == 3) {
            RenderSprite(fireballPixels, fireballW, fireballH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
        } else if (sp.type == 4) {
            RenderSprite(medkitPixels, medkitW, medkitH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
        } else if (sp.type == 5) {
            if (sp.variant == 0) {
                RenderSprite(clawDormantPixels, clawDormantW, clawDormantH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else {
                RenderSprite(clawActivePixels, clawActiveW, clawActiveH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 6) {
            if (sp.isFiring) {
                RenderSprite(gunnerFiringPixels, gunnerFiringW, gunnerFiringH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else {
                RenderSprite(gunnerPixels, gunnerW, gunnerH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 7) {
            RenderSprite(bulletPixels, bulletW, bulletH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
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
        
        if (enemy.isShooter) {
            if (enemy.firingTimer > 0) enemy.firingTimer -= deltaTime;
            
            if (dist <= 16.0f && dist > 1.0f) {
                enemy.fireTimer -= deltaTime;
                if (enemy.fireTimer <= 0) {
                    EnemyBullet eb;
                    eb.x = enemy.x;
                    eb.y = enemy.y;
                    float edx = player.x - enemy.x;
                    float edy = player.y - enemy.y;
                    float edist = sqrtf(edx*edx + edy*edy);
                    eb.dirX = edx / edist;
                    eb.dirY = edy / edist;
                    eb.speed = 8.0f;
                    eb.active = true;
                    enemyBullets.push_back(eb);
                    
                    enemy.fireTimer = 2.0f;
                    enemy.firingTimer = 0.5f;
                }
            } else if (dist > 16.0f) {
                float moveX = (dx / dist) * enemy.speed * deltaTime;
                float moveY = (dy / dist) * enemy.speed * deltaTime;
                float newX = enemy.x + moveX;
                float newY = enemy.y + moveY;
                if (worldMap[(int)newX][(int)enemy.y] == 0) enemy.x = newX;
                if (worldMap[(int)enemy.x][(int)newY] == 0) enemy.y = newY;
            }
        } else {
            if (dist > 0.5f) {
                float moveX = (dx / dist) * enemy.speed * deltaTime;
                float moveY = (dy / dist) * enemy.speed * deltaTime;
                float newX = enemy.x + moveX;
                float newY = enemy.y + moveY;
                if (worldMap[(int)newX][(int)enemy.y] == 0) enemy.x = newX;
                if (worldMap[(int)enemy.x][(int)newY] == 0) enemy.y = newY;
            }
            
            if (dist < 1.0f) {
                if (enemy.spriteIndex == 4) player.health -= 3;
                else player.health -= 1;
                
                playerHurtTimer = 0.3f;
                if (player.health <= 0) {
                    score = 0;
                    player.health = 100;
                    player.x = 10.0f;
                    player.y = 32.0f;
                    
                    if (bossActive) {
                        bossActive = false;
                        preBossPhase = false;
                        bossHealth = 200;
                        enemies.clear();
                        fireballs.clear();
                        InitClaws();
                    }
                    SpawnEnemies();
                }
            }
        }
        
        if (enemy.hurtTimer > 0) enemy.hurtTimer -= deltaTime;
        
        enemy.distance = dist;
    }
    
    for (auto& eb : enemyBullets) {
        if (!eb.active) continue;
        
        eb.x += eb.dirX * eb.speed * deltaTime;
        eb.y += eb.dirY * eb.speed * deltaTime;
        
        if (eb.x < 0 || eb.x > MAP_WIDTH || eb.y < 0 || eb.y > MAP_HEIGHT) {
            eb.active = false;
            continue;
        }
        
        if (worldMap[(int)eb.x][(int)eb.y] != 0) {
            eb.active = false;
            continue;
        }
        
        float pdx = player.x - eb.x;
        float pdy = player.y - eb.y;
        if (sqrtf(pdx*pdx + pdy*pdy) < 0.5f) {
            player.health -= 5;
            playerHurtTimer = 0.3f;
            eb.active = false;
            
            if (player.health <= 0) {
                score = 0;
                player.health = 100;
                player.x = 10.0f;
                player.y = 32.0f;
                
                if (bossActive) {
                    bossActive = false;
                    preBossPhase = false;
                    bossHealth = 200;
                    enemies.clear();
                    fireballs.clear();
                    InitClaws();
                }
                SpawnEnemies();
            }
        }
    }
    
    // Prevent spawning and clear enemies during pre-boss phase
    if (preBossPhase) {
        enemies.clear();
    } else {
        shooterSpawnTimer -= deltaTime;
        if (shooterSpawnTimer <= 0) {
            shooterSpawnTimer = 3.0f;
            
            int meleeCount = 0;
            int shooterCount = 0;
            for (auto& e : enemies) {
                if (e.active) {
                    if (e.isShooter) shooterCount++;
                    else meleeCount++;
                }
            }
            
            if (meleeCount < 3) {
                int meleeToSpawn = 3 - meleeCount;
                for (int i = 0; i < meleeToSpawn; i++) {
                    Enemy enemy;
                    do {
                        enemy.x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
                        enemy.y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
                    } while (worldMap[(int)enemy.x][(int)enemy.y] != 0 || 
                             sqrtf((enemy.x - player.x)*(enemy.x - player.x) + (enemy.y - player.y)*(enemy.y - player.y)) < 15.0f);
                    enemy.active = true;
                    enemy.speed = 1.5f + (rand() % 100) / 100.0f;
                    enemy.distance = 0;
                    enemy.spriteIndex = rand() % 5;
                    if (enemy.spriteIndex == 4) { enemy.health = 4; } else { enemy.health = 1; }
                    enemy.hurtTimer = 0;
                    enemy.isShooter = false;
                    enemy.fireTimer = 0;
                    enemy.firingTimer = 0;
                    enemies.push_back(enemy);
                    meleeCount++;
                }
            }
            
            int neededShooters = meleeCount / 3;
            int shootersToSpawn = neededShooters - shooterCount;
            
            for (int i = 0; i < shootersToSpawn; i++) {
                Enemy shooter;
                do {
                    shooter.x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
                    shooter.y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
                } while (worldMap[(int)shooter.x][(int)shooter.y] != 0 || 
                         sqrtf((shooter.x - player.x)*(shooter.x - player.x) + (shooter.y - player.y)*(shooter.y - player.y)) < 15.0f);
                shooter.active = true;
                shooter.speed = 1.2f;
                shooter.distance = 0;
                shooter.spriteIndex = 0;
                shooter.health = 2;
                shooter.hurtTimer = 0;
                shooter.isShooter = true;
                shooter.fireTimer = 2.0f;
                shooter.firingTimer = 0;
                enemies.push_back(shooter);
            }
        }
    }
    
    if (preBossPhase && !bossActive) {
        preBossTimer -= deltaTime;
        if (preBossTimer <= 0) {
            preBossPhase = false;
            bossActive = true;
            bossEventTimer = 3.0f;
            
            for(int i=0; i<6; i++) {
                claws[i].state = CLAW_IDLE;
            }
            activeClawIndex = 0;
            claws[0].state = CLAW_CHASING;
            claws[0].timer = 4.0f;
            
            // Spawn 15 enemies for boss fight
            for(int i=0; i<15; i++) {
                Enemy enemy;
                enemy.x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
                enemy.y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
                if(sqrtf((enemy.x - player.x)*(enemy.x - player.x) + (enemy.y - player.y)*(enemy.y - player.y)) < 10.0f) {
                    enemy.x = 32; enemy.y = 5;
                }
                enemy.active = true;
                enemy.speed = 1.5f + (rand() % 100) / 100.0f;
                enemy.distance = 0;
                enemy.spriteIndex = rand() % 5;
                if (enemy.spriteIndex == 4) { enemy.health = 4; } else { enemy.health = 1; }
                enemy.hurtTimer = 0;
                enemy.isShooter = false;
                enemy.fireTimer = 0;
                enemy.firingTimer = 0;
                enemies.push_back(enemy);
            }
        }
    }
    
    // Boss Logic
    if (bossActive) { 
        if (bossEventTimer > 0) bossEventTimer -= deltaTime;
        
        fireballSpawnTimer -= deltaTime;
        if (fireballSpawnTimer <= 0) {
            Fireball fb;
            fb.x = 32.0f;
            fb.y = 32.0f;
            float dx = player.x - 32.0f;
            float dy = player.y - 32.0f;
            float dist = sqrtf(dx*dx + dy*dy);
            if(dist > 0) {
                fb.dirX = dx/dist;
                fb.dirY = dy/dist;
            } else {
                fb.dirX = 1; fb.dirY = 0;
            }
            fb.speed = 5.0f; // Slow, dodgeable
            fb.active = true;
            fireballs.push_back(fb);
            
            fireballSpawnTimer = 2.0f; // Every 2 seconds
        }
        
        for(int i = 0; i < 6; i++) {
            Claw& claw = claws[i];
            
            if(claw.state == CLAW_CHASING) {
                float dx = player.x - claw.x;
                float dy = player.y - claw.y;
                float dist = sqrtf(dx*dx + dy*dy);
                if(dist > 0.5f) {
                    claw.x += (dx/dist) * 8.0f * deltaTime;
                    claw.y += (dy/dist) * 8.0f * deltaTime;
                }
                claw.timer -= deltaTime;
                if(claw.timer <= 0) {
                    claw.state = CLAW_SLAMMING;
                    claw.timer = 0.5f;
                    claw.groundY = claw.y;
                    claw.dealtDamage = false;
                }
            }
            else if(claw.state == CLAW_SLAMMING) {
                claw.timer -= deltaTime;
                if(claw.timer <= 0 && !claw.dealtDamage) {
                    float dx = player.x - claw.x;
                    float dy = player.y - claw.y;
                    float dist = sqrtf(dx*dx + dy*dy);
                    float aoeRadius = 4.0f + (rand() % 5);
                    if(dist < aoeRadius) {
                        player.health -= 10;
                        playerHurtTimer = 0.3f;
                        if(player.health <= 0) {
                            score = 0;
                            player.health = 100;
                            player.x = 10.0f;
                            player.y = 32.0f;
                            
                            bossActive = false;
                            preBossPhase = false;
                            bossHealth = 200;
                            enemies.clear();
                            fireballs.clear();
                            InitClaws();
                            SpawnEnemies();
                        }
                    }
                    claw.dealtDamage = true;
                    claw.state = CLAW_RISING;
                    claw.timer = 1.0f;
                    PlaySlamSound();
                    screenShakeTimer = 1.0f;
                    screenShakeIntensity = 50.0f;
                }
            }
            else if(claw.state == CLAW_RISING) {
                claw.timer -= deltaTime;
                if(claw.timer <= 0) {
                    claw.state = CLAW_RETURNING;
                }
            }
            else if(claw.state == CLAW_RETURNING) {
                float dx = claw.homeX - claw.x;
                float dy = claw.homeY - claw.y;
                float dist = sqrtf(dx*dx + dy*dy);
                if(dist > 0.5f) {
                    claw.x += (dx/dist) * clawReturnSpeed * deltaTime;
                    claw.y += (dy/dist) * clawReturnSpeed * deltaTime;
                } else {
                    claw.x = claw.homeX;
                    claw.y = claw.homeY;
                    claw.state = CLAW_IDLE;
                    
                    activeClawIndex = (activeClawIndex + 1) % 6;
                    claws[activeClawIndex].state = CLAW_CHASING;
                    claws[activeClawIndex].timer = 4.0f;
                }
            }
        }
    }
    
    // Update Fireballs
    for(auto& fb : fireballs) {
        if(!fb.active) continue;
        
        fb.x += fb.dirX * fb.speed * deltaTime;
        fb.y += fb.dirY * fb.speed * deltaTime;
        
        float dx = player.x - fb.x;
        float dy = player.y - fb.y;
        if (sqrtf(dx*dx + dy*dy) < 0.5f) {
            player.health -= 10;
            playerHurtTimer = 0.3f;
            fb.active = false;
            
            if (player.health <= 0) {
                score = 0;
                player.health = 100;
                player.x = 10.0f;
                player.y = 32.0f;
                
                if (bossActive) {
                    bossActive = false;
                    preBossPhase = false;
                    bossHealth = 200;
                    enemies.clear();
                    fireballs.clear();
                    InitClaws();
                }
                SpawnEnemies();
            }
        }
        
        if(fb.x < 0 || fb.x > MAP_WIDTH || fb.y < 0 || fb.y > MAP_HEIGHT) fb.active = false;
    }
    
    if (!medkit.active) {
        medkit.respawnTimer -= deltaTime;
        if (medkit.respawnTimer <= 0) {
            SpawnMedkit();
        }
    } else {
        float mdx = player.x - medkit.x;
        float mdy = player.y - medkit.y;
        float mdist = sqrtf(mdx*mdx + mdy*mdy);
        if (mdist < 1.0f) {
            player.health += Medkit::HEAL_AMOUNT;
            if (player.health > 100) player.health = 100;
            medkit.active = false;
            medkit.respawnTimer = Medkit::RESPAWN_TIME;
        }
    }
    
    if (bossHurtTimer > 0) bossHurtTimer -= deltaTime;
    if (playerHurtTimer > 0) playerHurtTimer -= deltaTime;
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
    fireTimer = 0.30f;
    
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
            float edx = b.x - enemy.x;
            float edy = b.y - enemy.y;
            if (sqrtf(edx*edx + edy*edy) < 1.0f) {
                b.active = false;
                
                enemy.health--;
                if (enemy.spriteIndex == 4) enemy.hurtTimer = 0.3f;
                
                if (enemy.health <= 0) {
                    enemy.active = false;
                    score++;
                    PlayScoreSound();
                    
                    if (score > highScore) {
                        highScore = score;
                        SaveHighScore();
                    }

                    // Check Score for Boss Trigger
                    if (score >= 100 && !bossActive && !preBossPhase) {
                        preBossPhase = true;
                        preBossTimer = 30.0f; // 30 seconds of silence
                        
                        // Despawn all enemies
                        for (auto& e : enemies) e.active = false;
                        enemies.clear();
                        
                        scoreTimer = 0; // Clear score text
                    }
                    
                    scoreTimer = 3.0f;
                    int msgIndex = rand() % 3;
                    wcscpy(scoreMsg, praiseMsgs[msgIndex]);
                    
                    // Only spawn replacement enemy if not in pre-boss phase
                    if (!preBossPhase) {
                        Enemy newEnemy;
                        do {
                            newEnemy.x = 5.0f + (rand() % 540) / 10.0f;
                            newEnemy.y = 5.0f + (rand() % 540) / 10.0f;
                        } while (sqrtf((newEnemy.x - player.x)*(newEnemy.x - player.x) + (newEnemy.y - player.y)*(newEnemy.y - player.y)) < 15.0f);
                        newEnemy.active = true;
                        newEnemy.speed = 1.5f + (rand() % 100) / 100.0f;
                        newEnemy.distance = 0;
                        newEnemy.spriteIndex = rand() % 5;
                        if (newEnemy.spriteIndex == 4) { newEnemy.health = 4; } else { newEnemy.health = 1; }
                        newEnemy.hurtTimer = 0;
                        enemies.push_back(newEnemy);
                    }
                }
                break;
            }
        }
        
        if (bossActive && bossHealth > 0) {
            float bdx = b.x - 32.0f;
            float bdy = b.y - 32.0f;
            if (sqrtf(bdx*bdx + bdy*bdy) < 2.5f) {
                bossHealth--;
                bossHurtTimer = 2.0f;
                b.active = false;
                PlayScoreSound();
                
                if (bossHealth <= 0) {
                    bossActive = false;
                    bossDead = true;
                    victoryScreen = true;
                    musicRunning = false;
                    score += 50;
                    if (score > highScore) {
                        highScore = score;
                        SaveHighScore();
                    }
                    
                    for (auto& e : enemies) e.active = false;
                    enemies.clear();
                    fireballs.clear();
                    
                    for(int i = 0; i < 6; i++) {
                        claws[i].state = CLAW_DORMANT;
                        claws[i].x = claws[i].homeX;
                        claws[i].y = claws[i].homeY;
                    }
                }
            }
        }
    }
    
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.active; }), bullets.end());
}

void DrawMinimap(HDC hdc) {
    int cellSize = 3;
    int mapDrawWidth = MAP_WIDTH * cellSize;
    int mapDrawHeight = MAP_HEIGHT * cellSize;
    
    int offsetX = SCREEN_WIDTH - mapDrawWidth - 10;
    int offsetY = 10;
    
    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 20));
    RECT bgRect = {offsetX - 3, offsetY - 3, offsetX + mapDrawWidth + 3, offsetY + mapDrawHeight + 3};
    FillRect(hdc, &bgRect, bgBrush);
    DeleteObject(bgBrush);
    
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (worldMap[x][y] > 0) {
                RECT cell = {
                    offsetX + x * cellSize, 
                    offsetY + y * cellSize,
                    offsetX + (x + 1) * cellSize, 
                    offsetY + (y + 1) * cellSize
                };
                COLORREF color;
                if (worldMap[x][y] == 2) color = RGB(0, 80, 0);
                else if (worldMap[x][y] == 1) color = RGB(100, 60, 30);
                else color = RGB(40, 60, 30);
                
                HBRUSH brush = CreateSolidBrush(color);
                FillRect(hdc, &cell, brush);
                DeleteObject(brush);
            }
        }
    }
    
    int playerScreenX = offsetX + (int)(player.x * cellSize);
    int playerScreenY = offsetY + (int)(player.y * cellSize);
    
    float triSize = 10.0f;
    POINT tri[3];
    tri[0].x = playerScreenX + (int)(cosf(player.angle) * triSize);
    tri[0].y = playerScreenY + (int)(sinf(player.angle) * triSize);
    tri[1].x = playerScreenX + (int)(cosf(player.angle + 2.4f) * triSize * 0.5f);
    tri[1].y = playerScreenY + (int)(sinf(player.angle + 2.4f) * triSize * 0.5f);
    tri[2].x = playerScreenX + (int)(cosf(player.angle - 2.4f) * triSize * 0.5f);
    tri[2].y = playerScreenY + (int)(sinf(player.angle - 2.4f) * triSize * 0.5f);
    
    HPEN greenPen = CreatePen(PS_SOLID, 2, RGB(0, 255, 0));
    HBRUSH playerBrush = CreateSolidBrush(RGB(0, 255, 0));
    HPEN oldPen = (HPEN)SelectObject(hdc, greenPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, playerBrush);
    Polygon(hdc, tri, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(greenPen);
    DeleteObject(playerBrush);
    
    MoveToEx(hdc, playerScreenX, playerScreenY, NULL);
    HPEN fovPen = CreatePen(PS_SOLID, 1, RGB(0, 200, 0));
    SelectObject(hdc, fovPen);
    int fovLen = 20;
    LineTo(hdc, playerScreenX + (int)(cosf(player.angle) * fovLen), playerScreenY + (int)(sinf(player.angle) * fovLen));
    SelectObject(hdc, oldPen);
    DeleteObject(fovPen);
    
    int spireScreenX = offsetX + (int)(32 * cellSize);
    int spireScreenY = offsetY + (int)(32 * cellSize);
    HBRUSH spireBrush = CreateSolidBrush(RGB(255, 165, 0));
    oldBrush = (HBRUSH)SelectObject(hdc, spireBrush);
    Ellipse(hdc, spireScreenX - 6, spireScreenY - 6, spireScreenX + 6, spireScreenY + 6);
    SelectObject(hdc, oldBrush);
    DeleteObject(spireBrush);
    
    if (medkit.active) {
        int medkitScreenX = offsetX + (int)(medkit.x * cellSize);
        int medkitScreenY = offsetY + (int)(medkit.y * cellSize);
        HBRUSH medkitBrush = CreateSolidBrush(RGB(0, 150, 255));
        oldBrush = (HBRUSH)SelectObject(hdc, medkitBrush);
        Ellipse(hdc, medkitScreenX - 4, medkitScreenY - 4, medkitScreenX + 4, medkitScreenY + 4);
        SelectObject(hdc, oldBrush);
        DeleteObject(medkitBrush);
    }
    
    for (auto& enemy : enemies) {
        if (enemy.active) {
            int ex = offsetX + (int)(enemy.x * cellSize);
            int ey = offsetY + (int)(enemy.y * cellSize);
            
            if (ex >= offsetX && ex < offsetX + mapDrawWidth && ey >= offsetY && ey < offsetY + mapDrawHeight) {
                HBRUSH enemyBrush = CreateSolidBrush(RGB(255, 0, 0));
                oldBrush = (HBRUSH)SelectObject(hdc, enemyBrush);
                Ellipse(hdc, ex - 3, ey - 3, ex + 3, ey + 3);
                SelectObject(hdc, oldBrush);
                DeleteObject(enemyBrush);
            }
        }
    }
    
    for (int i = 0; i < 6; i++) {
        int cx = offsetX + (int)(claws[i].x * cellSize);
        int cy = offsetY + (int)(claws[i].y * cellSize);
        
        HBRUSH clawBrush = CreateSolidBrush(RGB(255, 0, 255)); // Bright Magenta
        oldBrush = (HBRUSH)SelectObject(hdc, clawBrush);
        Rectangle(hdc, cx - 4, cy - 4, cx + 4, cy + 4);
        SelectObject(hdc, oldBrush);
        DeleteObject(clawBrush);
    }
}

void UpdatePlayer(float deltaTime) {
    float moveSpeed = 4.0f * deltaTime;
    float rotSpeed = 2.5f * deltaTime;
    
    isMoving = false;
    
    if (keys['W'] || keys[VK_UP]) {
        float newX = player.x + cosf(player.angle) * moveSpeed;
        float newY = player.y + sinf(player.angle) * moveSpeed;
        if ((newX-32)*(newX-32) + (player.y-32)*(player.y-32) < 4.0f) newX = player.x;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        
        if ((player.x-32)*(player.x-32) + (newY-32)*(newY-32) < 4.0f) newY = player.y;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys['S'] || keys[VK_DOWN]) {
        float newX = player.x - cosf(player.angle) * moveSpeed;
        float newY = player.y - sinf(player.angle) * moveSpeed;
        if ((newX-32)*(newX-32) + (player.y-32)*(player.y-32) < 4.0f) newX = player.x;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        
        if ((player.x-32)*(player.x-32) + (newY-32)*(newY-32) < 4.0f) newY = player.y;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys['A']) {
        float strafeAngle = player.angle - PI / 2;
        float newX = player.x + cosf(strafeAngle) * moveSpeed;
        float newY = player.y + sinf(strafeAngle) * moveSpeed;
        if ((newX-32)*(newX-32) + (player.y-32)*(player.y-32) < 4.0f) newX = player.x;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        
        if ((player.x-32)*(player.x-32) + (newY-32)*(newY-32) < 4.0f) newY = player.y;
        if (worldMap[(int)player.x][(int)newY] == 0) player.y = newY;
        isMoving = true;
    }
    if (keys['D']) {
        float strafeAngle = player.angle + PI / 2;
        float newX = player.x + cosf(strafeAngle) * moveSpeed;
        float newY = player.y + sinf(strafeAngle) * moveSpeed;
        if ((newX-32)*(newX-32) + (player.y-32)*(player.y-32) < 4.0f) newX = player.x;
        if (worldMap[(int)newX][(int)player.y] == 0) player.x = newX;
        
        if ((player.x-32)*(player.x-32) + (newY-32)*(newY-32) < 4.0f) newY = player.y;
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
    // Render3DScene(); // Disabled
    RenderClouds();
    RenderSprites();
    RenderGun();
    
    if (playerHurtTimer > 0) {
        float intensity = playerHurtTimer / 0.3f;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            DWORD col = backBufferPixels[i];
            int r = (col >> 16) & 0xFF;
            int g = (col >> 8) & 0xFF;
            int b = col & 0xFF;
            r = (int)(r + (255 - r) * intensity * 0.5f);
            g = (int)(g * (1.0f - intensity * 0.5f));
            b = (int)(b * (1.0f - intensity * 0.5f));
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            backBufferPixels[i] = MakeColor(r, g, b);
        }
    }
    
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
    
    int shakeX = 0, shakeY = 0;
    if (screenShakeTimer > 0) {
        float shakeFactor = screenShakeTimer / 1.0f;
        shakeX = (int)((rand() % (int)(screenShakeIntensity * 2 + 1) - screenShakeIntensity) * shakeFactor);
        shakeY = (int)((rand() % (int)(screenShakeIntensity * 2 + 1) - screenShakeIntensity) * shakeFactor);
    }
    
    SetDIBitsToDevice(hdc, shakeX, shakeY, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, 
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
    
    // Boss Health Bar
    if (bossActive && bossHealth > 0) {
        int barW = 400;
        int barH = 20;
        int barX = (SCREEN_WIDTH - barW) / 2;
        int barY = 40;
        
        // Background (Black)
        RECT bgRect = {barX - 2, barY - 2, barX + barW + 2, barY + barH + 2};
        HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &bgRect, bgBrush);
        DeleteObject(bgBrush);
        
        // Health (Red)
        float healthPct = (float)bossHealth / 200.0f;
        if (healthPct < 0) healthPct = 0;
        int hpW = (int)(barW * healthPct);
        RECT hpRect = {barX, barY, barX + hpW, barY + barH};
        HBRUSH hpBrush = CreateSolidBrush(RGB(200, 0, 0));
        FillRect(hdc, &hpRect, hpBrush);
        DeleteObject(hpBrush);
        
        // Text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        TextOutW(hdc, barX, barY - 20, L"THE SPIRE", 9);
    }
    
    // Countdown Timer during Pre-Boss Phase
    if (preBossPhase) {
        wchar_t bossTimerMsg[64];
        swprintf(bossTimerMsg, 64, L"BOSS IN: %.0f", preBossTimer);
        
        HFONT hFont = CreateFontW(50, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        
        SetTextColor(hdc, RGB(255, 0, 0)); // Red countdown
        SetBkMode(hdc, TRANSPARENT);
        
        SIZE size;
        GetTextExtentPoint32W(hdc, bossTimerMsg, (int)wcslen(bossTimerMsg), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, SCREEN_HEIGHT / 2 - 50, bossTimerMsg, (int)wcslen(bossTimerMsg));
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }

    // Pre-Boss Phase: Shaking "God has awoken" text
    if (bossActive && bossEventTimer > 0) {
        HFONT hFont = CreateFontW(60, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetTextColor(hdc, RGB(255, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        
        int shakeX = (rand() % 10) - 5;
        int shakeY = (rand() % 10) - 5;
        
        TextOutW(hdc, SCREEN_WIDTH/2 - 200 + shakeX, SCREEN_HEIGHT/2 - 100 + shakeY, L"God has awoken", 14);
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        
        SetTextColor(hdc, RGB(255, 255, 255));
    }

    
    SetTextColor(hdc, RGB(255, 255, 255));
    wchar_t info[128];
    swprintf(info, 128, L"WASD=Move | Arrows=Look | SPACE=Shoot | R=Reload | ESC=Quit");
    TextOutW(hdc, 10, SCREEN_HEIGHT - 25, info, (int)wcslen(info));
    
    if (victoryScreen) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            DWORD col = backBufferPixels[i];
            int r = (col >> 16) & 0xFF;
            int g = (col >> 8) & 0xFF;
            int b = col & 0xFF;
            r = (int)(r * 0.3f + 255 * 0.7f);
            g = (int)(g * 0.3f + 255 * 0.7f);
            b = (int)(b * 0.3f + 255 * 0.7f);
            backBufferPixels[i] = MakeColor(r, g, b);
        }
        
        BITMAPINFO bi2 = {};
        bi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi2.bmiHeader.biWidth = SCREEN_WIDTH;
        bi2.bmiHeader.biHeight = -SCREEN_HEIGHT;
        bi2.bmiHeader.biPlanes = 1;
        bi2.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(hdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, 
            backBufferPixels, &bi2, DIB_RGB_COLORS);
        
        HFONT hBigFont = CreateFontW(72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hMedFont = CreateFontW(36, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hBtnFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        
        SetBkMode(hdc, TRANSPARENT);
        
        HFONT hOldFont = (HFONT)SelectObject(hdc, hBigFont);
        SetTextColor(hdc, RGB(0, 150, 0));
        const wchar_t* wonText = L"You Won!";
        SIZE size;
        GetTextExtentPoint32W(hdc, wonText, (int)wcslen(wonText), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, 150, wonText, (int)wcslen(wonText));
        
        SelectObject(hdc, hMedFont);
        SetTextColor(hdc, RGB(50, 50, 50));
        wchar_t hsText[128];
        swprintf(hsText, 128, L"Final Score: %d", score);
        GetTextExtentPoint32W(hdc, hsText, (int)wcslen(hsText), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, 240, hsText, (int)wcslen(hsText));
        
        swprintf(hsText, 128, L"High Score: %d", highScore);
        GetTextExtentPoint32W(hdc, hsText, (int)wcslen(hsText), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, 290, hsText, (int)wcslen(hsText));
        
        SelectObject(hdc, hBtnFont);
        
        RECT playAgainBtn = {SCREEN_WIDTH/2 - 120, 380, SCREEN_WIDTH/2 + 120, 430};
        RECT exitBtn = {SCREEN_WIDTH/2 - 120, 450, SCREEN_WIDTH/2 + 120, 500};
        
        HBRUSH greenBrush = CreateSolidBrush(RGB(0, 180, 0));
        HBRUSH redBrush = CreateSolidBrush(RGB(180, 0, 0));
        FillRect(hdc, &playAgainBtn, greenBrush);
        FillRect(hdc, &exitBtn, redBrush);
        DeleteObject(greenBrush);
        DeleteObject(redBrush);
        
        SetTextColor(hdc, RGB(255, 255, 255));
        const wchar_t* playText = L"Play Again";
        GetTextExtentPoint32W(hdc, playText, (int)wcslen(playText), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, 392, playText, (int)wcslen(playText));
        
        const wchar_t* exitText = L"Exit";
        GetTextExtentPoint32W(hdc, exitText, (int)wcslen(exitText), &size);
        TextOutW(hdc, (SCREEN_WIDTH - size.cx) / 2, 462, exitText, (int)wcslen(exitText));
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hBigFont);
        DeleteObject(hMedFont);
        DeleteObject(hBtnFont);
    }
    
    // Debug Console
    if (consoleActive) {
        RECT consoleRect = {0, 0, SCREEN_WIDTH, 200};
        HBRUSH consoleBrush = CreateSolidBrush(RGB(50, 50, 50)); // Dark Gray
        FillRect(hdc, &consoleRect, consoleBrush);
        DeleteObject(consoleBrush);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hConsFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        HFONT hOldConsFont = (HFONT)SelectObject(hdc, hConsFont);
        
        TextOutW(hdc, 10, 10, L"DEBUG CONSOLE (type 'exit' to close)", 36);
        TextOutW(hdc, 10, 35, L">", 1);
        TextOutW(hdc, 25, 35, consoleBuffer.c_str(), (int)consoleBuffer.length());
        
        // Cursor
        if ((int)(GetTickCount() / 500) % 2 == 0) {
            SIZE size;
            GetTextExtentPoint32W(hdc, consoleBuffer.c_str(), (int)consoleBuffer.length(), &size);
            TextOutW(hdc, 25 + size.cx, 35, L"_", 1);
        }
        
        SelectObject(hdc, hOldConsFont);
        DeleteObject(hConsFont);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            backBufferPixels = new DWORD[SCREEN_WIDTH * SCREEN_HEIGHT];
            zBuffer = new float[SCREEN_WIDTH * SCREEN_HEIGHT];
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
            if (screenShakeTimer > 0) screenShakeTimer -= deltaTime;
            
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
        case WM_CHAR: {
            if (consoleActive) {
                if (wParam == VK_BACK) {
                    if (consoleBuffer.length() > 0) consoleBuffer.pop_back();
                } else if (wParam == VK_RETURN) {
                    if (consoleBuffer == L"exit") {
                        consoleActive = false;
                        consoleBuffer = L"";
                    } else if (consoleBuffer.find(L"score") == 0) {
                        // Parse "score = 100" or "score 100"
                        size_t eqPos = consoleBuffer.find(L'=');
                        if (eqPos != std::wstring::npos) {
                            std::wstring numStr = consoleBuffer.substr(eqPos + 1);
                            score = _wtoi(numStr.c_str());
                        } else {
                            // Try "score 100" format
                            size_t spacePos = consoleBuffer.find(L' ');
                            if (spacePos != std::wstring::npos) {
                                std::wstring numStr = consoleBuffer.substr(spacePos + 1);
                                score = _wtoi(numStr.c_str());
                            }
                        }
                        consoleBuffer = L"";
                    } else {
                        consoleBuffer = L""; // Clear unknown command
                    }
                } else {
                    consoleBuffer += (wchar_t)wParam;
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_OEM_3) { // Tilde key
                consoleActive = !consoleActive;
                return 0;
            }
            if (consoleActive) return 0; // Block game input
            
            if (victoryScreen) return 0;
            keys[wParam & 0xFF] = true;
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        case WM_KEYUP:
            if (victoryScreen) return 0;
            keys[wParam & 0xFF] = false;
            return 0;
        case WM_LBUTTONDOWN: {
            if (victoryScreen) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);
                RECT playAgainBtn = {SCREEN_WIDTH/2 - 120, 380, SCREEN_WIDTH/2 + 120, 430};
                RECT exitBtn = {SCREEN_WIDTH/2 - 120, 450, SCREEN_WIDTH/2 + 120, 500};
                
                if (mx >= playAgainBtn.left && mx <= playAgainBtn.right && my >= playAgainBtn.top && my <= playAgainBtn.bottom) {
                    victoryScreen = false;
                    bossDead = false;
                    bossActive = false;
                    preBossPhase = false;
                    bossHealth = 200;
                    score = 0;
                    player.health = 100;
                    player.x = 10.0f;
                    player.y = 32.0f;
                    player.angle = 0.0f;
                    ammo = maxAmmo;
                    enemies.clear();
                    fireballs.clear();
                    bullets.clear();
                    SpawnEnemies();
                    SpawnMedkit();
                    InitClaws();
                    musicRunning = true;
                    _beginthread(BackgroundMusic, 0, NULL);
                }
                
                if (mx >= exitBtn.left && mx <= exitBtn.right && my >= exitBtn.top && my <= exitBtn.bottom) {
                    PostQuitMessage(0);
                }
            }
            return 0;
        }
        case WM_DESTROY:
            musicRunning = false;
            CleanupAudio();
            KillTimer(hwnd, 1);
            delete[] backBufferPixels;
            delete[] zBuffer;
            if (grassPixels) delete[] grassPixels;
            for(int i=0; i<5; i++) if (enemyPixels[i]) delete[] enemyPixels[i];
            if (enemy5HurtPixels) delete[] enemy5HurtPixels;
            if (treePixels) delete[] treePixels;
            if (cloudPixels) delete[] cloudPixels;
            if (gunPixels) delete[] gunPixels;
            if (gunfirePixels) delete[] gunfirePixels;
            if (bulletPixels) delete[] bulletPixels;
            if (medkitPixels) delete[] medkitPixels;
            if (clawDormantPixels) delete[] clawDormantPixels;
            if (clawActivePixels) delete[] clawActivePixels;
            if (gunnerPixels) delete[] gunnerPixels;
            if (gunnerFiringPixels) delete[] gunnerFiringPixels;
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
    SpawnMedkit();
    InitClaws();
    
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
