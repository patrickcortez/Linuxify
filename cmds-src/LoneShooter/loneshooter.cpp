/*
 * LoneShooter - Open World 2.5D Raycaster
 * Compile: g++ -o cmds/LoneShooter.exe loneshooter.cpp -lgdi32 -lwinmm -mwindows -O2
 * Run: ./LoneShooter.exe
 * Controls: WASD=Move, Mouse=Look, ESC=Quit
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

void PlayHealSound() {
    NoteOn(3, 72, 127);
    NoteOn(3, 76, 127);
    NoteOn(3, 79, 127);
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

void PlayMarshallAttackSound() {
    static wchar_t mashPath[MAX_PATH] = {0};
    if (mashPath[0] == 0) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        swprintf(mashPath, MAX_PATH, L"\"%ls\\assets\\sound-effects\\hammer-effect.mp3\"", exePath);
    }
    
    wchar_t cmd[512];
    mciSendStringW(L"close mashsfx", NULL, 0, NULL);
    swprintf(cmd, 512, L"open %ls type mpegvideo alias mashsfx", mashPath);
    mciSendStringW(cmd, NULL, 0, NULL);
    mciSendStringW(L"setaudio mashsfx volume to 1000", NULL, 0, NULL);
    mciSendStringW(L"play mashsfx from 0", NULL, 0, NULL);
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

const int TRIG_TABLE_SIZE = 4096;
float sinTable[TRIG_TABLE_SIZE];
float cosTable[TRIG_TABLE_SIZE];

void InitTrigTables() {
    for (int i = 0; i < TRIG_TABLE_SIZE; i++) {
        float angle = (float)i / TRIG_TABLE_SIZE * 2.0f * PI;
        sinTable[i] = sinf(angle);
        cosTable[i] = cosf(angle);
    }
}

inline float FastSin(float angle) {
    while (angle < 0) angle += 2.0f * PI;
    while (angle >= 2.0f * PI) angle -= 2.0f * PI;
    int index = (int)(angle / (2.0f * PI) * TRIG_TABLE_SIZE) % TRIG_TABLE_SIZE;
    return sinTable[index];
}

inline float FastCos(float angle) {
    while (angle < 0) angle += 2.0f * PI;
    while (angle >= 2.0f * PI) angle -= 2.0f * PI;
    int index = (int)(angle / (2.0f * PI) * TRIG_TABLE_SIZE) % TRIG_TABLE_SIZE;
    return cosTable[index];
}

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
    bool isMarshall;
    int state; // 0=Seek, 1=Chase, 2=Retreat
    float healTimer;
    float summonTimer;
    float attackTimer;
};

struct EnemyBullet {
    float x, y;
    float dirX, dirY;
    float speed;
    bool active;
    bool isLaser; // true = claw laser (10 dmg), false = enemy bullet (5 dmg)
};

struct TreeSprite {
    float x, y;
    float distance;
};

struct GrassSprite {
    float x, y;
};

struct RockSprite {
    float x, y;
    int variant;
};

struct BushSprite {
    float x, y;
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

enum ClawState { CLAW_DORMANT, CLAW_IDLE, CLAW_CHASING, CLAW_SLAMMING, CLAW_RISING, CLAW_RETURNING, 
                 CLAW_PH2_AWAKEN, CLAW_PH2_DROPPING, CLAW_PH2_ANCHORED, CLAW_PH2_DEAD, CLAW_PH2_RISING };

struct Claw {
    float x, y;
    float homeX, homeY;
    float groundY;
    ClawState state;
    float timer;
    int index;
    bool dealtDamage;
    // Phase 2
    int health;
    int animFrame;
    float animTimer;
    bool hurt;
    float hurtTimer;
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
    static const int HEAL_AMOUNT = 25;
};
const float Medkit::RESPAWN_TIME = 10.0f;

Player player = {10.0f, 32.0f, 0.0f, 0.0f, 100};
std::vector<Enemy> enemies;
std::vector<TreeSprite> trees;
std::vector<GrassSprite> grasses;
std::vector<RockSprite> rocks;
std::vector<BushSprite> bushes;
std::vector<Cloud> clouds;
std::vector<Bullet> bullets;
std::vector<Fireball> fireballs;
std::vector<EnemyBullet> enemyBullets;
Medkit medkits[3] = {{0, 0, false, 0}, {0, 0, false, 0}, {0, 0, false, 0}};
float healFlashTimer = 0;

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
float bossSpawnTimer = 0;

int maxMeleeSpawn = 3;
int maxShooterSpawn = 1;
float spawnCapTimer = 20.0f;
const int MELEE_CAP = 15;
const int SHOOTER_CAP = 5;

bool phase2Active = false;
bool forceFieldActive = false;
bool enragedMode = false;
int phase2BossFrame = 0;
float phase2BossAnimTimer = 0;
DWORD* spirePhase2Pixels[3];
int spirePhase2W[3], spirePhase2H[3];

DWORD* clawPhase2Pixels[4];
int clawPhase2W[4], clawPhase2H[4];
DWORD* clawHurtPixels = nullptr;
int clawHurtW = 0, clawHurtH = 0;

int activeLaserClaw = -1;
int lastActiveClaw = 5; // For sequential ordering
float laserTimer = 0;

DWORD* laserPixels = nullptr;
int laserW = 0, laserH = 0;

int playerDamage = 1;
bool godMode = false;
bool marshallSpawned = false;
DWORD* marshallPixels = nullptr;
int marshallW = 0, marshallH = 0;
DWORD* marshallHurtPixels = nullptr;
int marshallHurtW = 0, marshallHurtH = 0;
// Marshall UI
bool marshallHealthBarActive = false;
int marshallHP = 0;
int marshallMaxHP = 35;

struct Paragon {
    float x, y;
    float speed;
    int health;
    bool active;
    float hurtTimer;
    float targetX, targetY;
    bool hunting;
    int targetEnemyIndex;
    int targetClawIndex;
};

std::vector<Paragon> paragons;
bool marshallKilled = false;
bool paragonsUnlocked = false;
float paragonMessageTimer = 0;
DWORD* paragonPixels = nullptr;
int paragonW = 0, paragonH = 0;
DWORD* paragonHurtPixels = nullptr;
int paragonHurtW = 0, paragonHurtH = 0;
float paragonSummonCooldown = 0;

bool consoleActive = false;
std::wstring consoleBuffer = L"";
bool showStats = false;
int fpsCounter = 0;
int currentFPS = 0;
DWORD fpsLastTime = 0;

wchar_t errorMessage[256] = L"";
float errorTimer = 0;
wchar_t consoleError[128] = L"";
std::vector<std::wstring> missingAssets;
bool assetsFolderMissing = false;

std::vector<Object3D> scene3D;
float* zBuffer = nullptr;

// Prototypes
void LoadModelCurrentDir(const wchar_t* filename, float x, float z);
void Render3DScene();

void ShowError(const wchar_t* msg) {
    wcscpy(errorMessage, msg);
    errorTimer = 3.0f;
}

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
DWORD* gunnerHurtPixels = NULL;
int gunnerHurtW = 0, gunnerHurtH = 0;
DWORD* grassPlantPixels = NULL;
int grassPlantW = 0, grassPlantH = 0;
DWORD* rockPixels[3] = {NULL};
int rockW[3] = {0}, rockH[3] = {0};
DWORD* bushPixels = NULL;
int bushW = 0, bushH = 0;
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
DWORD* clawActivatingPixels = NULL;
int clawActivatingW = 0, clawActivatingH = 0;
float preBossPulseTimer = 0;
bool preBossPulseFrame = false;

Claw claws[6];
int activeClawIndex = 0;
float clawReturnSpeed = 3.0f;

DWORD* errorPixels = NULL;
int errorW = 0, errorH = 0;

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
    missingAssets.clear();
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\error.bmp", exePath);
    errorPixels = LoadBMPPixels(path, &errorW, &errorH);
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\grass.bmp", exePath);
    grassPixels = LoadBMPPixels(path, &grassW, &grassH);
    if (!grassPixels) { 
        missingAssets.push_back(L"grass.bmp");
        if (errorPixels) { grassPixels = errorPixels; grassW = errorW; grassH = errorH; } 
    }
    
    for(int i=0; i<5; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\enemy%d.bmp", exePath, i+1);
        enemyPixels[i] = LoadBMPPixels(path, &enemyW[i], &enemyH[i]);
        if (!enemyPixels[i]) { wchar_t name[32]; swprintf(name, 32, L"enemy%d.bmp", i+1); missingAssets.push_back(name); if (errorPixels) { enemyPixels[i] = errorPixels; enemyW[i] = errorW; enemyH[i] = errorH; } }
    }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\enemy5_hurt.bmp", exePath);
    enemy5HurtPixels = LoadBMPPixels(path, &enemy5HurtW, &enemy5HurtH);
    if (!enemy5HurtPixels) { missingAssets.push_back(L"enemy5_hurt.bmp"); if (errorPixels) { enemy5HurtPixels = errorPixels; enemy5HurtW = errorW; enemy5HurtH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gunner.bmp", exePath);
    gunnerPixels = LoadBMPPixels(path, &gunnerW, &gunnerH);
    if (!gunnerPixels) { missingAssets.push_back(L"gunner.bmp"); if (errorPixels) { gunnerPixels = errorPixels; gunnerW = errorW; gunnerH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gunner_firing.bmp", exePath);
    gunnerFiringPixels = LoadBMPPixels(path, &gunnerFiringW, &gunnerFiringH);
    if (!gunnerFiringPixels) { missingAssets.push_back(L"gunner_firing.bmp"); if (errorPixels) { gunnerFiringPixels = errorPixels; gunnerFiringW = errorW; gunnerFiringH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\tree.bmp", exePath);
    treePixels = LoadBMPPixels(path, &treeW, &treeH);
    if (!treePixels) { missingAssets.push_back(L"tree.bmp"); if (errorPixels) { treePixels = errorPixels; treeW = errorW; treeH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\cloud.bmp", exePath);
    cloudPixels = LoadBMPPixels(path, &cloudW, &cloudH);
    if (!cloudPixels) { missingAssets.push_back(L"cloud.bmp"); if (errorPixels) { cloudPixels = errorPixels; cloudW = errorW; cloudH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gun.bmp", exePath);
    gunPixels = LoadBMPPixels(path, &gunW, &gunH);
    if (!gunPixels) { missingAssets.push_back(L"gun.bmp"); if (errorPixels) { gunPixels = errorPixels; gunW = errorW; gunH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\gunfire.bmp", exePath);
    gunfirePixels = LoadBMPPixels(path, &gunfireW, &gunfireH);
    if (!gunfirePixels) { missingAssets.push_back(L"gunfire.bmp"); if (errorPixels) { gunfirePixels = errorPixels; gunfireW = errorW; gunfireH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\bullet.bmp", exePath);
    bulletPixels = LoadBMPPixels(path, &bulletW, &bulletH);
    if (!bulletPixels) { missingAssets.push_back(L"bullet.bmp"); if (errorPixels) { bulletPixels = errorPixels; bulletW = errorW; bulletH = errorH; } }
    
    const wchar_t* healthbarNames[] = {L"healthbar_0.bmp", L"healthbar_10.bmp", L"healthbar_20.bmp", L"healthbar_30.bmp", L"healthbar_40.bmp", L"healthbar_50.bmp", L"healthbar_60.bmp", L"healthbar_70.bmp", L"healthbar_80.bmp", L"healthbar_90.bmp", L"healthbar_full.bmp"};
    for (int i = 0; i < 11; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\healthbar_UI\\%ls", exePath, healthbarNames[i]);
        healthbarPixels[i] = LoadBMPPixels(path, &healthbarW, &healthbarH);
        if (!healthbarPixels[i]) { missingAssets.push_back(healthbarNames[i]); if (errorPixels) { healthbarPixels[i] = errorPixels; healthbarW = errorW; healthbarH = errorH; } }
    }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\spire_resting.bmp", exePath);
    spirePixels = LoadBMPPixels(path, &spireW, &spireH);
    if (!spirePixels) { missingAssets.push_back(L"spire_resting.bmp"); if (errorPixels) { spirePixels = errorPixels; spireW = errorW; spireH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\spire_awake.bmp", exePath);
    spireAwakePixels = LoadBMPPixels(path, &spireAwakeW, &spireAwakeH);
    if (!spireAwakePixels) { missingAssets.push_back(L"spire_awake.bmp"); if (errorPixels) { spireAwakePixels = errorPixels; spireAwakeW = errorW; spireAwakeH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\Spire_hurt.bmp", exePath);
    spireHurtPixels = LoadBMPPixels(path, &spireHurtW, &spireHurtH);
    if (!spireHurtPixels) { missingAssets.push_back(L"Spire_hurt.bmp"); if (errorPixels) { spireHurtPixels = errorPixels; spireHurtW = errorW; spireHurtH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\Spire_Death.bmp", exePath);
    spireDeathPixels = LoadBMPPixels(path, &spireDeathW, &spireDeathH);
    if (!spireDeathPixels) { missingAssets.push_back(L"Spire_Death.bmp"); if (errorPixels) { spireDeathPixels = errorPixels; spireDeathW = errorW; spireDeathH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\fireball.bmp", exePath);
    fireballPixels = LoadBMPPixels(path, &fireballW, &fireballH);
    if (!fireballPixels) { missingAssets.push_back(L"fireball.bmp"); if (errorPixels) { fireballPixels = errorPixels; fireballW = errorW; fireballH = errorH; } }
    
    for(int i=0; i<3; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\spire_phase2\\spire_frame%d.bmp", exePath, i+1);
        spirePhase2Pixels[i] = LoadBMPPixels(path, &spirePhase2W[i], &spirePhase2H[i]);
        if(!spirePhase2Pixels[i]) {
            wchar_t name[64]; swprintf(name, 64, L"spire_frame%d.bmp", i+1); missingAssets.push_back(name);
            if (errorPixels) { spirePhase2Pixels[i] = errorPixels; spirePhase2W[i] = errorW; spirePhase2H[i] = errorH; }
        }
    }
    
    for(int i=0; i<4; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_awaken\\claw_frame%d.bmp", exePath, i+1);
        clawPhase2Pixels[i] = LoadBMPPixels(path, &clawPhase2W[i], &clawPhase2H[i]);
        if(!clawPhase2Pixels[i]) {
             wchar_t name[64]; swprintf(name, 64, L"claw_frame%d.bmp", i+1); missingAssets.push_back(name);
             if (errorPixels) { clawPhase2Pixels[i] = errorPixels; clawPhase2W[i] = errorW; clawPhase2H[i] = errorH; }
        }
    }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_awaken\\claw_hurt.bmp", exePath);
    clawHurtPixels = LoadBMPPixels(path, &clawHurtW, &clawHurtH);
    if (!clawHurtPixels) { missingAssets.push_back(L"claw_hurt.bmp"); if (errorPixels) { clawHurtPixels = errorPixels; clawHurtW = errorW; clawHurtH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_attack\\laser.bmp", exePath);
    laserPixels = LoadBMPPixels(path, &laserW, &laserH);
    if (!laserPixels) { missingAssets.push_back(L"laser.bmp"); if (errorPixels) { laserPixels = errorPixels; laserW = errorW; laserH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\items\\Medkit.bmp", exePath);
    medkitPixels = LoadBMPPixels(path, &medkitW, &medkitH);
    if (!medkitPixels) { missingAssets.push_back(L"Medkit.bmp"); if (errorPixels) { medkitPixels = errorPixels; medkitW = errorW; medkitH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_dormant.bmp", exePath);
    clawDormantPixels = LoadBMPPixels(path, &clawDormantW, &clawDormantH);
    if (!clawDormantPixels) { missingAssets.push_back(L"claw_dormant.bmp"); if (errorPixels) { clawDormantPixels = errorPixels; clawDormantW = errorW; clawDormantH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_active.bmp", exePath);
    clawActivePixels = LoadBMPPixels(path, &clawActiveW, &clawActiveH);
    if (!clawActivePixels) { missingAssets.push_back(L"claw_active.bmp"); if (errorPixels) { clawActivePixels = errorPixels; clawActiveW = errorW; clawActiveH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\spire\\claw_activating.bmp", exePath);
    clawActivatingPixels = LoadBMPPixels(path, &clawActivatingW, &clawActivatingH);
    if (!clawActivatingPixels) { missingAssets.push_back(L"claw_activating.bmp"); if (errorPixels) { clawActivatingPixels = errorPixels; clawActivatingW = errorW; clawActivatingH = errorH; } }

    swprintf(path, MAX_PATH, L"%ls\\assets\\Marshall\\marshall.bmp", exePath);
    marshallPixels = LoadBMPPixels(path, &marshallW, &marshallH);
    if (!marshallPixels) { missingAssets.push_back(L"marshall.bmp"); if (errorPixels) { marshallPixels = errorPixels; marshallW = errorW; marshallH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\Marshall\\marshall_hurt.bmp", exePath);
    marshallHurtPixels = LoadBMPPixels(path, &marshallHurtW, &marshallHurtH);
    if (!marshallHurtPixels) { missingAssets.push_back(L"marshall_hurt.bmp"); if (errorPixels) { marshallHurtPixels = errorPixels; marshallHurtW = errorW; marshallHurtH = errorH; } }

    swprintf(path, MAX_PATH, L"%ls\\assets\\gunner_hurt.bmp", exePath);
    gunnerHurtPixels = LoadBMPPixels(path, &gunnerHurtW, &gunnerHurtH);
    if (!gunnerHurtPixels) { missingAssets.push_back(L"gunner_hurt.bmp"); if (errorPixels) { gunnerHurtPixels = errorPixels; gunnerHurtW = errorW; gunnerHurtH = errorH; } }

    swprintf(path, MAX_PATH, L"%ls\\assets\\Viper\\Viper.bmp", exePath);
    paragonPixels = LoadBMPPixels(path, &paragonW, &paragonH);
    if (!paragonPixels) { missingAssets.push_back(L"Viper.bmp"); if (errorPixels) { paragonPixels = errorPixels; paragonW = errorW; paragonH = errorH; } }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\Viper\\Viper_hurt.bmp", exePath);
    paragonHurtPixels = LoadBMPPixels(path, &paragonHurtW, &paragonHurtH);
    if (!paragonHurtPixels) { missingAssets.push_back(L"Viper_hurt.bmp"); if (errorPixels) { paragonHurtPixels = errorPixels; paragonHurtW = errorW; paragonHurtH = errorH; } }

    swprintf(path, MAX_PATH, L"%ls\\assets\\environment\\plants\\grass_plant.bmp", exePath);
    grassPlantPixels = LoadBMPPixels(path, &grassPlantW, &grassPlantH);
    if (!grassPlantPixels) { missingAssets.push_back(L"grass_plant.bmp"); if (errorPixels) { grassPlantPixels = errorPixels; grassPlantW = errorW; grassPlantH = errorH; } }
    
    for (int i = 0; i < 3; i++) {
        swprintf(path, MAX_PATH, L"%ls\\assets\\environment\\small_rocks\\rock%d.bmp", exePath, i + 1);
        rockPixels[i] = LoadBMPPixels(path, &rockW[i], &rockH[i]);
        if (!rockPixels[i]) { wchar_t name[32]; swprintf(name, 32, L"rock%d.bmp", i+1); missingAssets.push_back(name); if (errorPixels) { rockPixels[i] = errorPixels; rockW[i] = errorW; rockH[i] = errorH; } }
    }
    
    swprintf(path, MAX_PATH, L"%ls\\assets\\environment\\plants\\bush.bmp", exePath);
    bushPixels = LoadBMPPixels(path, &bushW, &bushH);
    if (!bushPixels) { missingAssets.push_back(L"bush.bmp"); if (errorPixels) { bushPixels = errorPixels; bushW = errorW; bushH = errorH; } }

    swprintf(loadStatus, 256, L"G:%ls S:%ls A:%ls H:%ls D:%ls F:%ls M:%ls C:%ls", gunPixels?L"OK":L"X", spirePixels?L"OK":L"X", spireAwakePixels?L"OK":L"X", spireHurtPixels?L"OK":L"X", spireDeathPixels?L"OK":L"X", fireballPixels?L"OK":L"X", medkitPixels?L"OK":L"X", clawDormantPixels?L"OK":L"X");
    
    if (!errorPixels && !gunPixels && !spirePixels && !treePixels && !grassPixels) {
        assetsFolderMissing = true;
    }
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
    
    for (int i = 0; i < 400; i++) {
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
    
    int numTrees = 250 + rand() % 50;
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
    
    for (int i = 0; i < 50; i++) {
        Cloud cloud;
        cloud.x = -50.0f + (rand() % 1500) / 10.0f;
        cloud.y = -50.0f + (rand() % 1500) / 10.0f;
        cloud.height = 15.0f + (rand() % 100) / 10.0f;
        cloud.speed = 0.5f + (rand() % 100) / 100.0f;
        clouds.push_back(cloud);
    }
    
    int rockVariant = 0;
    for (int i = 0; i < 5000; i++) {
        float gx = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
        float gy = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
        float distToCenter = sqrtf((gx - 32)*(gx - 32) + (gy - 32)*(gy - 32));
        if (distToCenter > 6.0f && worldMap[(int)gx][(int)gy] == 0) {
            GrassSprite grass = {gx, gy};
            grasses.push_back(grass);
        }
    }
    
    for (int i = 0; i < 350; i++) {
        float rx = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
        float ry = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
        float distToCenter = sqrtf((rx - 32)*(rx - 32) + (ry - 32)*(ry - 32));
        if (distToCenter > 6.0f && worldMap[(int)rx][(int)ry] == 0) {
            RockSprite rock = {rx, ry, rockVariant};
            rocks.push_back(rock);
            rockVariant = (rockVariant + 1) % 3;
        }
    }
    
    for (int i = 0; i < 80; i++) {
        float bx = 6.0f + (rand() % ((MAP_WIDTH - 12) * 10)) / 10.0f;
        float by = 6.0f + (rand() % ((MAP_HEIGHT - 12) * 10)) / 10.0f;
        float distToCenter = sqrtf((bx - 32)*(bx - 32) + (by - 32)*(by - 32));
        if (distToCenter > 8.0f && worldMap[(int)bx][(int)by] == 0) {
            BushSprite bush = {bx, by};
            bushes.push_back(bush);
        }
    }
}

void SpawnMedkit() {
    for (int i = 0; i < 3; i++) {
        do {
            medkits[i].x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
            medkits[i].y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
        } while (worldMap[(int)medkits[i].x][(int)medkits[i].y] != 0 || 
                 sqrtf((medkits[i].x - 32)*(medkits[i].x - 32) + (medkits[i].y - 32)*(medkits[i].y - 32)) < 5.0f);
        medkits[i].active = true;
        medkits[i].respawnTimer = 0;
    }
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
        enemy.isMarshall = false; // Fix uninitialized
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

struct RaycastParams {
    int startX;
    int endX;
};

unsigned __stdcall RaycastWorker(void* param) {
    RaycastParams* rp = (RaycastParams*)param;
    
    for (int x = rp->startX; x < rp->endX; x++) {
        float rayAngle = (player.angle - FOV / 2.0f) + ((float)x / SCREEN_WIDTH) * FOV;
        float rayDirX = FastCos(rayAngle);
        float rayDirY = FastSin(rayAngle);
        
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
                float skyGradient = (float)y / (SCREEN_HEIGHT / 2);
                int r, g, b;
                
                if (bossActive) {
                    r = (int)(150 + 100 * (1 - skyGradient));
                    g = (int)(20 * (1 - skyGradient));
                    b = (int)(20 * (1 - skyGradient));
                } else {
                    r = (int)(30 + 80 * (1 - skyGradient));
                    g = (int)(60 + 120 * (1 - skyGradient));
                    b = (int)(100 + 155 * (1 - skyGradient));
                }
                
                backBufferPixels[y * SCREEN_WIDTH + x] = MakeColor(r, g, b);
                zBuffer[y * SCREEN_WIDTH + x] = 1000.0f;
            }
            
            if (y > SCREEN_HEIGHT / 2 + (int)player.pitch) {
                float rowDist = (SCREEN_HEIGHT / 2.0f) / (y - SCREEN_HEIGHT / 2.0f);
                float floorX = player.x + FastCos(rayAngle) * rowDist;
                float floorY = player.y + FastSin(rayAngle) * rowDist;
                
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
    
    return 0;
}

void CastRays() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numThreads = sysInfo.dwNumberOfProcessors;
    if (numThreads < 1) numThreads = 1;
    if (numThreads > 32) numThreads = 32;
    
    HANDLE* threads = new HANDLE[numThreads];
    RaycastParams* params = new RaycastParams[numThreads];
    
    int columnsPerThread = SCREEN_WIDTH / numThreads;
    
    for (int i = 0; i < numThreads; i++) {
        params[i].startX = i * columnsPerThread;
        params[i].endX = (i == numThreads - 1) ? SCREEN_WIDTH : (i + 1) * columnsPerThread;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, RaycastWorker, &params[i], 0, NULL);
    }
    
    WaitForMultipleObjects(numThreads, threads, TRUE, INFINITE);
    
    for (int i = 0; i < numThreads; i++) {
        CloseHandle(threads[i]);
    }
    
    delete[] threads;
    delete[] params;
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
    
    DWORD* sPix = NULL;
    int sW, sH;
    if (bossDead) {
        sPix = spireDeathPixels;
        sW = spireDeathW;
        sH = spireDeathH;
    } else if (bossHurtTimer > 0 && bossActive) {
        sPix = spireHurtPixels;
        sW = spireHurtW;
        sH = spireHurtH;
    }
    if (sPix == NULL) {
        if (phase2Active && !enragedMode) {
            sPix = spirePhase2Pixels[phase2BossFrame];
            sW = spirePhase2W[phase2BossFrame];
            sH = spirePhase2H[phase2BossFrame];
        } else if (bossActive && !bossDead) {
            sPix = spireAwakePixels;
            sW = spireAwakeW;
            sH = spireAwakeH;
        } else {
            sPix = spirePixels;
            sW = spireW;
            sH = spireH;
        }
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
    
    for (int i = 0; i < 3; i++) {
        if (medkits[i].active) {
            float mdx = medkits[i].x - player.x;
            float mdy = medkits[i].y - player.y;
            float mdist = sqrtf(mdx*mdx + mdy*mdy);
            allSprites.push_back({medkits[i].x, medkits[i].y, mdist, 4, 0.8f, 0, false, 0.0f, false});
        }
    }

    for (auto& tree : trees) {
        float dx = tree.x - player.x;
        float dy = tree.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 50.0f) {
            allSprites.push_back({tree.x, tree.y, dist, 0, 1.0f, 0, false, 0.0f, false});
        }
    }
    
    for (auto& grass : grasses) {
        float dx = grass.x - player.x;
        float dy = grass.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 30.0f) {
            allSprites.push_back({grass.x, grass.y, dist, 11, 0.3f, 0, false, 0.0f, false});
        }
    }
    
    for (auto& rock : rocks) {
        float dx = rock.x - player.x;
        float dy = rock.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 30.0f) {
            allSprites.push_back({rock.x, rock.y, dist, 12, 0.3f, rock.variant, false, 0.0f, false});
        }
    }
    
    for (auto& bush : bushes) {
        float dx = bush.x - player.x;
        float dy = bush.y - player.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < 40.0f) {
            allSprites.push_back({bush.x, bush.y, dist, 13, 0.6f, 0, false, 0.0f, false});
        }
    }
    
    for (auto& enemy : enemies) {
        if (enemy.active) {
            float dx = enemy.x - player.x;
            float dy = enemy.y - player.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (enemy.isMarshall) {
                allSprites.push_back({enemy.x, enemy.y, dist, 9, 2.5f, (enemy.hurtTimer > 0 ? 1 : 0), false, 0.0f, false});
            } else if (enemy.isShooter) {
                allSprites.push_back({enemy.x, enemy.y, dist, 6, 1.0f, 0, (enemy.hurtTimer > 0), 0.0f, enemy.firingTimer > 0});
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
            int bulletType = eb.isLaser ? 8 : 7;
            float scale = eb.isLaser ? 1.5f : 0.5f;
            float height = eb.isLaser ? 1.0f : 0.0f;
            allSprites.push_back({eb.x, eb.y, dist, bulletType, scale, 0, false, height, false});
        }
    }
    
    for (auto& p : paragons) {
        if (p.active) {
            float dx = p.x - player.x;
            float dy = p.y - player.y;
            float dist = sqrtf(dx*dx + dy*dy);
            allSprites.push_back({p.x, p.y, dist, 10, 1.0f, 0, (p.hurtTimer > 0), 0.0f, false});
        }
    }
    
    for(int i = 0; i < 6; i++) {
        float cdx = claws[i].x - player.x;
        float cdy = claws[i].y - player.y;
        float cdist = sqrtf(cdx*cdx + cdy*cdy);
        int clawVariant = 0;
        bool isClawHurt = false;
        
        if (phase2Active && !enragedMode) {
            if (claws[i].state == CLAW_PH2_DEAD) {
                clawVariant = -1;
            } else {
                clawVariant = claws[i].animFrame;
                isClawHurt = (claws[i].hurtTimer > 0);
            }
        } else if (preBossPhase) {
            clawVariant = 3;
        } else if (!bossActive && !bossDead) {
            int activatedClaws = score / 50;
            if (activatedClaws > 6) activatedClaws = 6;
            if (i < activatedClaws) {
                clawVariant = 2;
            } else {
                clawVariant = 0;
            }
        } else {
            clawVariant = (claws[i].state == CLAW_DORMANT || bossDead) ? 0 : 1;
        }
        
        float clawHeight = 6.0f;
        if (claws[i].state == CLAW_PH2_ANCHORED) {
            clawHeight = 0.5f; // Ground level
        } else if (claws[i].state == CLAW_PH2_DROPPING) {
            float progress = 1.0f - (claws[i].timer / 2.0f); // 0 to 1
            if (progress < 0) progress = 0; if (progress > 1) progress = 1;
            // Lerp from 6.0 to 0.5
            clawHeight = 6.0f * (1.0f - progress) + 0.5f * progress;
        } else if (claws[i].state == CLAW_SLAMMING) {
            float progress = 1.0f - (claws[i].timer / 0.5f);
            if (progress < 0) progress = 0;
            if (progress > 1) progress = 1;
            clawHeight = 6.0f * (1.0f - progress);
        } else if (claws[i].state == CLAW_RISING) {
            float progress = 1.0f - (claws[i].timer / 1.0f);
            if (progress < 0) progress = 0;
            if (progress > 1) progress = 1;
            clawHeight = 6.0f * progress;
        } else if (claws[i].state == CLAW_RETURNING) {
            clawHeight = 6.0f;
        } else if (claws[i].state == CLAW_PH2_RISING) {
            float progress = 1.0f - (claws[i].timer / 2.0f); // 0 to 1 over 2s
            if (progress < 0) progress = 0; if (progress > 1) progress = 1;
            clawHeight = 0.5f * (1.0f - progress) + 6.0f * progress; // 0.5 to 6.0
        } else if (claws[i].state == CLAW_PH2_DEAD) {
            clawHeight = 6.0f;
        }
        
        allSprites.push_back({claws[i].x, claws[i].y, cdist, 5, 8.0f, clawVariant, isClawHurt, clawHeight, false});
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
            if (phase2Active && sp.variant >= 0 && !enragedMode) {
                if (sp.isHurt) {
                    RenderSprite(clawHurtPixels, clawHurtW, clawHurtH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                } else {
                    int idx = sp.variant;
                    if (idx < 0) idx = 0; if (idx > 3) idx = 3;
                    RenderSprite(clawPhase2Pixels[idx], clawPhase2W[idx], clawPhase2H[idx], sp.x, sp.y, sp.dist, sp.scale, sp.height);
                }
            } else {
                if (sp.variant == 0 || sp.variant == -1) {
                    RenderSprite(clawDormantPixels, clawDormantW, clawDormantH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                } else if (sp.variant == 2) {
                    if (clawActivatingPixels) {
                        RenderSprite(clawActivatingPixels, clawActivatingW, clawActivatingH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                    } else {
                        RenderSprite(clawActivePixels, clawActiveW, clawActiveH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                    }
                } else if (sp.variant == 3) {
                    if (preBossPulseFrame && clawActivatingPixels) {
                        RenderSprite(clawActivatingPixels, clawActivatingW, clawActivatingH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                    } else {
                        RenderSprite(clawActivePixels, clawActiveW, clawActiveH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                    }
                } else {
                    RenderSprite(clawActivePixels, clawActiveW, clawActiveH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
                }
            }
        } else if (sp.type == 6) {
            if (sp.isHurt && gunnerHurtPixels) {
                RenderSprite(gunnerHurtPixels, gunnerHurtW, gunnerHurtH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else if (sp.isFiring) {
                RenderSprite(gunnerFiringPixels, gunnerFiringW, gunnerFiringH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else {
                RenderSprite(gunnerPixels, gunnerW, gunnerH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 9) { // Marshall
            if (sp.variant == 1) {
                RenderSprite(marshallHurtPixels, marshallHurtW, marshallHurtH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else {
                RenderSprite(marshallPixels, marshallW, marshallH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 7) {
            RenderSprite(bulletPixels, bulletW, bulletH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
        } else if (sp.type == 8) {
            if (laserPixels) {
                RenderSprite(laserPixels, laserW, laserH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else {
                RenderSprite(bulletPixels, bulletW, bulletH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 10) {
            if (sp.isHurt && paragonHurtPixels) {
                RenderSprite(paragonHurtPixels, paragonHurtW, paragonHurtH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            } else if (paragonPixels) {
                RenderSprite(paragonPixels, paragonW, paragonH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 11) {
            if (grassPlantPixels) {
                RenderSprite(grassPlantPixels, grassPlantW, grassPlantH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 12) {
            int v = sp.variant;
            if (v < 0 || v > 2) v = 0;
            if (rockPixels[v]) {
                RenderSprite(rockPixels[v], rockW[v], rockH[v], sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
        } else if (sp.type == 13) {
            if (bushPixels) {
                RenderSprite(bushPixels, bushW, bushH, sp.x, sp.y, sp.dist, sp.scale, sp.height);
            }
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
    marshallHealthBarActive = false; // Reset frame flag
    for (auto& enemy : enemies) {
        if (!enemy.active) continue;
        
        // Marshall UI Sync
        if (enemy.isMarshall) {
             marshallHealthBarActive = true;
             marshallHP = enemy.health;
        }
        
        // Marshall AI
        if (enemy.isMarshall) {
             // Heal Logic: Retreat if HP < 5, Return to Chase if HP >= 35
             if (enemy.health < 5 && enemy.state != 2) {
                 enemy.state = 2; // Retreat
             }
             if (enemy.health >= 35 && enemy.state == 2) {
                 enemy.state = 1; // Return to Chase
             }
             
             if (enemy.state == 2) { // Retreat & Heal
                 // Move AWAY from player
                 float dx = enemy.x - player.x; 
                 float dy = enemy.y - player.y;
                 float dist = sqrtf(dx*dx + dy*dy);
                 if (dist > 0) {
                     float retreatSpeed = 7.5f; // Player Sprint (6.5) + 1
                     float mx = (dx/dist) * retreatSpeed * deltaTime;
                     float my = (dy/dist) * retreatSpeed * deltaTime;
                     
                     bool moved = false;
                     // Try standard retreat (sliding allowed) with Boundary Check (4 unit padding)
                     float nextX = enemy.x + mx;
                     float nextY = enemy.y + my;
                     
                     if (nextX >= 4.0f && nextX <= MAP_WIDTH - 4.0f && worldMap[(int)nextX][(int)enemy.y] == 0) {
                          enemy.x = nextX; moved = true; 
                     }
                     if (nextY >= 4.0f && nextY <= MAP_HEIGHT - 4.0f && worldMap[(int)enemy.x][(int)nextY] == 0) { 
                          enemy.y = nextY; moved = true; 
                     }
                     
                     // Corner Escape: If stuck, try moving perpendicular
                     if (!moved) {
                         // Try Left Perpendicular (-y, x)
                         float px = -my;
                         float py = mx;
                         if (worldMap[(int)(enemy.x + px)][(int)(enemy.y + py)] == 0) {
                             enemy.x += px; enemy.y += py;
                         } else {
                             // Try Right Perpendicular (y, -x)
                             px = my; py = -mx;
                             if (worldMap[(int)(enemy.x + px)][(int)(enemy.y + py)] == 0) {
                                 enemy.x += px; enemy.y += py;
                             }
                         }
                     }
                 }
                 
                 enemy.healTimer += deltaTime;
                 if (enemy.healTimer >= 2.0f) {
                     enemy.health++;
                     enemy.healTimer = 0;
                 }
                 continue; // Skip standard logic
             } else if (enemy.state == 0) { // Seek Horde
                 // Find average position of nearby enemies
                 float avgX = 0, avgY = 0;
                 int count = 0;
                 for(auto& other : enemies) {
                     if (!other.active || other.isMarshall) continue;
                     float d = sqrtf((other.x - enemy.x)*(other.x - enemy.x) + (other.y - enemy.y)*(other.y - enemy.y));
                     if (d < 10.0f) {
                         avgX += other.x;
                         avgY += other.y;
                         count++;
                     }
                 }
                 
                 if (count > 2) {
                     avgX /= count;
                     avgY /= count;
                     float dx = avgX - enemy.x;
                     float dy = avgY - enemy.y;
                     float dist = sqrtf(dx*dx + dy*dy);
                     
                     if (dist > 1.0f) {
                         // Move to horde
                         float mx = (dx/dist) * enemy.speed * deltaTime;
                         float my = (dy/dist) * enemy.speed * deltaTime;
                         if (worldMap[(int)(enemy.x + mx)][(int)enemy.y] == 0) enemy.x += mx;
                         if (worldMap[(int)enemy.x][(int)(enemy.y + my)] == 0) enemy.y += my;
                     } else {
                         enemy.state = 1; // Blended in, now chase
                     }
                 } else {
                     enemy.state = 1; // No horde found, chase
                 }
                 continue; // Skip standard logic
             } else { // Chase (State 1) - Falls through to standard move, but handle Attacks
                 float dx = player.x - enemy.x;
                 float dy = player.y - enemy.y;
                 float dist = sqrtf(dx*dx + dy*dy);
                 
                 if (dist < 3.0f && enemy.attackTimer <= 0) {
                     // AOE Attack
                     if (!godMode) player.health -= 20;
                     PlayMarshallAttackSound();
                     screenShakeTimer = 1.0f; // Violent shake
                     playerHurtTimer = 0.5f;
                     
                     // Knockback
                     float kx = (player.x - enemy.x) / dist;
                     float ky = (player.y - enemy.y) / dist;
                     player.x += kx * 2.0f;
                     player.y += ky * 2.0f;
                     
                     enemy.attackTimer = 2.0f; // Cooldown
                 }
                 if (enemy.attackTimer > 0) enemy.attackTimer -= deltaTime;
                 
                 // Summoning
                 enemy.summonTimer -= deltaTime;
                 if (enemy.summonTimer <= 0) {
                     enemy.summonTimer = 10.0f;
                     for (int k=0; k<5; k++) {
                         Enemy s;
                         s.x = enemy.x + (rand()%200 - 100)/50.0f;
                         s.y = enemy.y + (rand()%200 - 100)/50.0f;
                         if (worldMap[(int)s.x][(int)s.y] == 0) {
                             s.active = true; s.health = 1; s.speed = 3.0f; s.spriteIndex = rand()%4; // Melee
                             s.isShooter = false; s.isMarshall = false;
                             enemies.push_back(s);
                         }
                     }
                 }
             }
        }

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
                    eb.isLaser = false;
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
                if (!godMode) {
                    if (enemy.spriteIndex == 4) player.health -= 3;
                    else player.health -= 1;
                }
                
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
                        phase2Active = false;
                        enragedMode = false;
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
            int dmg = eb.isLaser ? 10 : 5;
            if (!godMode) player.health -= dmg;
            playerHurtTimer = 0.3f;
            eb.active = false;
            
            if (player.health <= 0) {
                score = 0;
                player.health = 100;
                player.x = 10.0f;
                player.y = 32.0f;
                
                // Reset Boss & Game State
                bossActive = false;
                preBossPhase = false;
                preBossTimer = 0;
                preBossPulseTimer = 0;
                bossHealth = 200;
                phase2Active = false;
                enragedMode = false;
                enemies.clear();
                fireballs.clear();
                enemies.clear();
                fireballs.clear();
                InitClaws();
                marshallSpawned = false; // Reset Marshall
                SpawnEnemies();
            }
        }
    }
    
    // Prevent spawning and clear enemies during pre-boss phase
    if (preBossPhase) {
        enemies.clear();
    } else if (!bossActive) {
        // Progressive spawn cap increase (pre-boss only)
        spawnCapTimer -= deltaTime;
        if (spawnCapTimer <= 0) {
            spawnCapTimer = 20.0f;
            if (maxMeleeSpawn < MELEE_CAP) maxMeleeSpawn += 3;
            if (maxMeleeSpawn > MELEE_CAP) maxMeleeSpawn = MELEE_CAP;
            if (maxShooterSpawn < SHOOTER_CAP) maxShooterSpawn += 1;
            if (maxShooterSpawn > SHOOTER_CAP) maxShooterSpawn = SHOOTER_CAP;
        }
        
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
            
            // Spawn melee up to current max
            if (meleeCount < maxMeleeSpawn) {
                int meleeToSpawn = maxMeleeSpawn - meleeCount;
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
                    enemy.isMarshall = false; // Fix uninitialized
                    enemies.push_back(enemy);
                }
            }
            
            // Spawn shooters up to current max
            if (shooterCount < maxShooterSpawn) {
                int shootersToSpawn = maxShooterSpawn - shooterCount;
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
                    shooter.isMarshall = false; // Fix uninitialized
                    enemies.push_back(shooter);
                }
            }
        }
    }
    
    if (preBossPhase && !bossActive) {
        preBossTimer -= deltaTime;
        
        preBossPulseTimer += deltaTime;
        if (preBossPulseTimer >= 1.0f) {
            preBossPulseTimer = 0;
            preBossPulseFrame = !preBossPulseFrame;
        }
        
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
        // Trigger Phase 2
        if (!phase2Active && bossHealth <= 100) {
            phase2Active = true;
            forceFieldActive = true;
            enemies.clear(); // Kill all minions
            fireballs.clear();
            
            // Setup Claws for Phase 2
            for(int i=0; i<6; i++) {
                claws[i].state = CLAW_PH2_AWAKEN;
                claws[i].animFrame = 0;
                claws[i].animTimer = 0;
                claws[i].health = 50;
                claws[i].x = claws[i].homeX;
                claws[i].y = claws[i].homeY;
                claws[i].hurt = false;
                claws[i].hurtTimer = 0;
            }
            activeClawIndex = -1; // Reset active claw
            lastActiveClaw = 5;
        }

        if (phase2Active) {
            // Phase 2 Logic
            phase2BossAnimTimer += deltaTime;
            if (phase2BossAnimTimer >= 0.5f) {
                phase2BossFrame = (phase2BossFrame + 1) % 3;
                phase2BossAnimTimer = 0;
            }
            
            int livingClaws = 0;
            for(int i=0; i<6; i++) {
                Claw& c = claws[i];
                if (c.state != CLAW_PH2_DEAD) livingClaws++;
                
                if (c.hurtTimer > 0) c.hurtTimer -= deltaTime;
                
                if (c.state == CLAW_PH2_AWAKEN) {
                    c.animTimer += deltaTime;
                    if (c.animTimer >= 0.5f) {
                        c.animFrame++;
                        c.animTimer = 0;
                        if (c.animFrame >= 4) {
                            c.animFrame = 3; // Stay on last frame
                            c.state = CLAW_IDLE; // Wait for selection
                        }
                    }
                } else if (c.state == CLAW_PH2_DROPPING) {
                    c.timer -= deltaTime;
                    if (c.timer <= 0) {
                        c.state = CLAW_PH2_ANCHORED;
                        c.timer = 10.0f; // Anchor time
                    }
                } else if (c.state == CLAW_PH2_ANCHORED) {
                    c.timer -= deltaTime;
                    laserTimer += deltaTime;
                    if (laserTimer >= 0.5f) { // Rapid burst every 0.5s
                        // Fire laser projectile at player
                        float dx = player.x - c.x;
                        float dy = player.y - c.y;
                        float dist = sqrtf(dx*dx + dy*dy);
                        if (dist > 0.1f) {
                            EnemyBullet laser;
                            laser.x = c.x;
                            laser.y = c.y;
                            laser.dirX = dx / dist;
                            laser.dirY = dy / dist;
                            laser.speed = 15.0f; // Fast laser
                            laser.active = true;
                            laser.isLaser = true;
                            enemyBullets.push_back(laser);
                        }
                        laserTimer = 0;
                    }
                    if (c.timer <= 0) {
                        c.state = CLAW_PH2_RISING; // Rising animation
                        c.timer = 2.0f; // 2s rise
                        c.x = c.homeX;
                        c.y = c.homeY;
                        activeLaserClaw = -1;
                    }
                } else if (c.state == CLAW_PH2_RISING) {
                    c.timer -= deltaTime;
                    float progress = c.timer / 2.0f;
                    if (progress < 0) progress = 0;
                    c.x = c.homeX;
                    c.y = c.homeY;
                    if (c.timer <= 0) {
                        if (c.health <= 0) {
                            c.state = CLAW_PH2_DEAD;
                        } else {
                            c.state = CLAW_IDLE;
                        }
                    }
                }
            }
            
            if (livingClaws == 0 && !enragedMode) {
                enragedMode = true;
                forceFieldActive = false;
                
                for(int i = 0; i < 6; i++) {
                    claws[i].state = CLAW_IDLE;
                    claws[i].health = 999;
                    claws[i].x = claws[i].homeX;
                    claws[i].y = claws[i].homeY;
                }
            }
            
            if (enragedMode) {
                forceFieldActive = false;
                
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
                    fb.speed = 8.0f;
                    fb.active = true;
                    fireballs.push_back(fb);
                    
                    fireballSpawnTimer = 0.8f;
                }
                
                for(int i = 0; i < 6; i++) {
                    Claw& claw = claws[i];
                    
                    if(claw.state == CLAW_IDLE) {
                        claw.state = CLAW_CHASING;
                        claw.timer = 2.0f;
                    }
                    else if(claw.state == CLAW_CHASING) {
                        float dx = player.x - claw.x;
                        float dy = player.y - claw.y;
                        float dist = sqrtf(dx*dx + dy*dy);
                        if(dist > 0.5f) {
                            claw.x += (dx/dist) * 12.0f * deltaTime;
                            claw.y += (dy/dist) * 12.0f * deltaTime;
                        }
                        claw.timer -= deltaTime;
                        if(claw.timer <= 0) {
                            claw.state = CLAW_SLAMMING;
                            claw.timer = 0.3f;
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
                            float aoeRadius = 5.0f + (rand() % 5);
                            if(dist < aoeRadius) {
                                if (!godMode) player.health -= 15;
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
                                    phase2Active = false;
                                    forceFieldActive = false;
                                    enragedMode = false;
                                }
                            }
                            claw.dealtDamage = true;
                            claw.state = CLAW_RISING;
                            claw.timer = 0.5f;
                            PlaySlamSound();
                            screenShakeTimer = 1.0f;
                            screenShakeIntensity = 60.0f;
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
                            claw.x += (dx/dist) * 15.0f * deltaTime;
                            claw.y += (dy/dist) * 15.0f * deltaTime;
                        } else {
                            claw.state = CLAW_IDLE;
                        }
                    }
                }
            } else if (livingClaws > 0) {
                forceFieldActive = true;
            }
            
            // Pick new claw to anchor sequentially (only if force field still active)
            if (activeLaserClaw == -1 && livingClaws > 0 && forceFieldActive) {
                int attempts = 0;
                int idx = (lastActiveClaw + 1) % 6;
                bool found = false;
                
                // Find next living claw that is ready (CLAW_IDLE)
                for(int i=0; i<6; i++) {
                    if (claws[idx].state == CLAW_PH2_DEAD) {
                        idx = (idx + 1) % 6;
                        continue; // Skip dead claws
                    }
                    if (claws[idx].state == CLAW_IDLE) {
                        found = true;
                        break;
                    }
                    idx = (idx + 1) % 6;
                }
                
                if (found) {
                    activeLaserClaw = idx;
                    lastActiveClaw = idx;
                    claws[idx].state = CLAW_PH2_DROPPING;
                    claws[idx].timer = 2.0f; // Drop time
                }
            }

            
        } else {
            // Phase 1 Logic
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
                fb.speed = 5.0f; 
                fb.active = true;
                fireballs.push_back(fb);
                
                fireballSpawnTimer = 2.0f; 
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
                            if (!godMode) player.health -= 10;
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
                                phase2Active = false;
                                enragedMode = false;
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
    }
    
    // Update Fireballs
    for(auto& fb : fireballs) {
        if(!fb.active) continue;
        
        fb.x += fb.dirX * fb.speed * deltaTime;
        fb.y += fb.dirY * fb.speed * deltaTime;
        
        float dx = player.x - fb.x;
        float dy = player.y - fb.y;
        if (sqrtf(dx*dx + dy*dy) < 0.5f) {
            if (!godMode) player.health -= 10;
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
                    phase2Active = false;
                    enragedMode = false;
                    enemies.clear();
                    fireballs.clear();
                    InitClaws();
                }
                SpawnEnemies();
            }
        }
        
        if(fb.x < 0 || fb.x > MAP_WIDTH || fb.y < 0 || fb.y > MAP_HEIGHT) fb.active = false;
    }
    
    
    if (bossHurtTimer > 0) bossHurtTimer -= deltaTime;
    if (playerHurtTimer > 0) playerHurtTimer -= deltaTime;
    
    if (bossActive && !bossDead && !phase2Active) {
        bossSpawnTimer -= deltaTime;
        if (bossSpawnTimer <= 0) {
            bossSpawnTimer = 2.0f;
            
            int meleeCount = 0, shooterCount = 0;
            for (auto& e : enemies) {
                if (e.active) {
                    if (e.isShooter) shooterCount++;
                    else meleeCount++;
                }
            }
            
            const int BOSS_MELEE_CAP = 30;
            const int BOSS_SHOOTER_CAP = 10;
            
            if (meleeCount < BOSS_MELEE_CAP) {
                Enemy e;
                int attempts = 0;
                do {
                    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
                    float dist = 8.0f + (float)(rand() % 20);
                    e.x = player.x + cosf(angle) * dist;
                    e.y = player.y + sinf(angle) * dist;
                    if (e.x < 1.5f) e.x = 1.5f; if (e.x >= MAP_WIDTH - 1.5f) e.x = (float)(MAP_WIDTH - 2);
                    if (e.y < 1.5f) e.y = 1.5f; if (e.y >= MAP_HEIGHT - 1.5f) e.y = (float)(MAP_HEIGHT - 2);
                    attempts++;
                } while (worldMap[(int)e.x][(int)e.y] != 0 && attempts < 10);
                
                if (worldMap[(int)e.x][(int)e.y] == 0) {
                    e.active = true;
                    e.health = 1; // standard
                    e.speed = 3.0f; // Slower than player (4.0)
                    e.spriteIndex = rand() % 5;
                    e.hurtTimer = 0;
                    e.isShooter = false;
                    e.fireTimer = 0;
                    e.firingTimer = 0;
                    e.isMarshall = false; // Fix uninitialized
                    enemies.push_back(e);
                }
            }
            
            if (shooterCount < BOSS_SHOOTER_CAP) {
                Enemy e;
                int attempts = 0;
                do {
                    float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
                    float dist = 10.0f + (float)(rand() % 15);
                    e.x = player.x + cosf(angle) * dist;
                    e.y = player.y + sinf(angle) * dist;
                    if (e.x < 1.5f) e.x = 1.5f; if (e.x >= MAP_WIDTH - 1.5f) e.x = (float)(MAP_WIDTH - 2);
                    if (e.y < 1.5f) e.y = 1.5f; if (e.y >= MAP_HEIGHT - 1.5f) e.y = (float)(MAP_HEIGHT - 2);
                    attempts++;
                } while (worldMap[(int)e.x][(int)e.y] != 0 && attempts < 10);
                
                if (worldMap[(int)e.x][(int)e.y] == 0) {
                    e.active = true;
                    e.health = 1; // standard
                    e.speed = 2.0f; 
                    e.spriteIndex = 0;
                    e.hurtTimer = 0;
                    e.isShooter = true;
                    e.fireTimer = 2.0f + (float)(rand() % 20) / 10.0f;
                    e.firingTimer = 0;
                    e.isMarshall = false; // Fix uninitialized
                    enemies.push_back(e);
                }
            }
        }
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
    fireTimer = 0.30f;
    
    PlayGunSound();
}

void UpdateBullets(float deltaTime) {
    bool shouldClearEnemies = false;
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
                
                enemy.health -= playerDamage;
                if (enemy.spriteIndex == 4 || enemy.isShooter) enemy.hurtTimer = 0.5f;
                
                if (enemy.health <= 0) {
                    enemy.active = false;
                    if (enemy.isMarshall) marshallKilled = true;
                    score++;
                    PlayScoreSound();
                    
                    if (score > highScore) {
                        highScore = score;
                        SaveHighScore();
                    }

                    // Check Score for Boss Trigger
                    if (score >= 300 && !bossActive && !preBossPhase) {
                        preBossPhase = true;
                        preBossTimer = 30.0f;
                        
                        // Despawn all enemies
                        // for (auto& e : enemies) e.active = false;
                        // enemies.clear(); // Unsafe in loop
                        shouldClearEnemies = true;
                        
                        scoreTimer = 0; // Clear score text
                    }
                    
                    // Marshall Spawn Trigger
                    if (score >= 50 && !marshallSpawned) {
                        Enemy marshall;
                        int attempts = 0;
                        do {
                            float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
                            float dist = 10.0f + (float)(rand() % 15);
                            marshall.x = player.x + cosf(angle) * dist;
                            marshall.y = player.y + sinf(angle) * dist;
                            if (marshall.x < 1.5f) marshall.x = 1.5f; if (marshall.x >= MAP_WIDTH - 1.5f) marshall.x = (float)(MAP_WIDTH - 2);
                            if (marshall.y < 1.5f) marshall.y = 1.5f; if (marshall.y >= MAP_HEIGHT - 1.5f) marshall.y = (float)(MAP_HEIGHT - 2);
                            attempts++;
                        } while (worldMap[(int)marshall.x][(int)marshall.y] != 0 && attempts < 10);
                        
                        if (worldMap[(int)marshall.x][(int)marshall.y] == 0) {
                            marshall.active = true;
                            marshall.health = 35; 
                            marshall.speed = 2.5f;
                            marshall.spriteIndex = 4; // Use elite/red imp base but override render
                            marshall.hurtTimer = 0;
                            marshall.isShooter = false;
                            marshall.fireTimer = 0;
                            marshall.firingTimer = 0;
                            // Marshall Specifics
                            marshall.isMarshall = true;
                            marshall.state = 0; // Seek Horde
                            marshall.healTimer = 0;
                            marshall.summonTimer = 10.0f; // Initial delay
                            marshall.attackTimer = 0;
                            
                            enemies.push_back(marshall);
                            marshallSpawned = true;
                            
                            // Spawn 10 minions to follow him
                            for (int k=0; k<10; k++) {
                                Enemy s;
                                s.x = marshall.x + (rand()%200 - 100)/50.0f; 
                                s.y = marshall.y + (rand()%200 - 100)/50.0f;
                                if (s.x < 1.5f) s.x = 1.5f; if (s.x >= MAP_WIDTH - 1.5f) s.x = (float)(MAP_WIDTH - 2);
                                if (s.y < 1.5f) s.y = 1.5f; if (s.y >= MAP_HEIGHT - 1.5f) s.y = (float)(MAP_HEIGHT - 2);
                                
                                if (worldMap[(int)s.x][(int)s.y] == 0) {
                                    s.active = true; s.health = 1; s.speed = 3.0f; s.spriteIndex = rand()%4; 
                                    s.isShooter = false; s.isMarshall = false; 
                                    enemies.push_back(s);
                                }
                            }
                        }
                    }
                    
                    scoreTimer = 3.0f;
                    int msgIndex = rand() % 3;
                    wcscpy(scoreMsg, praiseMsgs[msgIndex]);
                }
                break;
            }
        }
        
        if (bossActive && bossHealth > 0) {
            bool hitHit = false;
            
            // Check Boss Hit (if no Force Field)
            if (phase2Active && forceFieldActive) {
                float bdx = b.x - 32.0f;
                float bdy = b.y - 32.0f;
                if (sqrtf(bdx*bdx + bdy*bdy) < 3.5f) {
                     b.active = false; // Deflected
                     hitHit = true; 
                }
            } else {
                float bdx = b.x - 32.0f;
                float bdy = b.y - 32.0f;
                if (sqrtf(bdx*bdx + bdy*bdy) < 2.5f) {
                    bossHealth -= playerDamage;
                    bossHurtTimer = 2.0f;
                    b.active = false;
                    PlayScoreSound();
                    hitHit = true;
                    
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
                        
                        phase2Active = false;
                        forceFieldActive = false;
                        activeLaserClaw = -1;
                    }
                }
            }
            
            // Check Claws Hit (Phase 2)
            if (!hitHit && phase2Active) {
                for(int i=0; i<6; i++) {
                    if (claws[i].state == CLAW_PH2_DEAD) continue;
                    
                    float cdx = b.x - claws[i].x;
                    float cdy = b.y - claws[i].y;
                    if (sqrtf(cdx*cdx + cdy*cdy) < 2.0f) {
                        b.active = false;
                        claws[i].health -= playerDamage; 
                        claws[i].hurtTimer = 0.2f;
                        if (claws[i].health <= 0) {
                            claws[i].state = CLAW_PH2_RISING;
                            claws[i].timer = 2.0f;
                            PlayScoreSound();
                            if (activeLaserClaw == i) activeLaserClaw = -1;
                        }
                        break;
                    }
                }
            }
        }
    }
    
    if (shouldClearEnemies) {
        enemies.clear();
        // fireballs.clear(); // Maybe? Pre-boss clears everything.
        // Logic in UpdateEnemies says: enemies.clear().
        // Here we just clear enemies.
    }
    
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.active; }), bullets.end());
}

int GetAliveParagonCount() {
    int count = 0;
    for (auto& p : paragons) { if (p.active) count++; }
    return count;
}

void UpdateParagons(float deltaTime) {
    if (!paragonsUnlocked && score >= 200 && marshallKilled) {
        paragonsUnlocked = true;
        paragonMessageTimer = 3.0f;
        for (int i = 0; i < 2; i++) {
            Paragon p;
            float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
            p.x = player.x + cosf(angle) * 1.5f;
            p.y = player.y + sinf(angle) * 1.5f;
            p.speed = 4.5f;
            p.health = 10;
            p.active = true;
            p.hurtTimer = 0;
            p.targetX = 0; p.targetY = 0;
            p.hunting = false;
            p.targetEnemyIndex = -1;
            p.targetClawIndex = -1;
            paragons.push_back(p);
        }
    }
    
    if (paragonMessageTimer > 0) paragonMessageTimer -= deltaTime;
    if (paragonSummonCooldown > 0) paragonSummonCooldown -= deltaTime;
    
    for (auto& p : paragons) {
        if (!p.active) continue;
        
        if (p.hurtTimer > 0) p.hurtTimer -= deltaTime;
        
        float distToPlayer = sqrtf((p.x - player.x)*(p.x - player.x) + (p.y - player.y)*(p.y - player.y));
        
        int nearestEnemyIdx = -1;
        float nearestEnemyDist = 6.0f;
        for (size_t i = 0; i < enemies.size(); i++) {
            if (!enemies[i].active) continue;
            float dx = enemies[i].x - p.x;
            float dy = enemies[i].y - p.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < nearestEnemyDist) {
                nearestEnemyDist = dist;
                nearestEnemyIdx = (int)i;
            }
        }
        
        int nearestClawIdx = -1;
        float nearestClawDist = 6.0f;
        if (phase2Active) {
            for (int i = 0; i < 6; i++) {
                if (claws[i].state == CLAW_PH2_DEAD) continue;
                float dx = claws[i].x - p.x;
                float dy = claws[i].y - p.y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist < nearestClawDist) {
                    nearestClawDist = dist;
                    nearestClawIdx = i;
                }
            }
        }
        
        float evadeX = 0, evadeY = 0;
        
        for (auto& eb : enemyBullets) {
            if (!eb.active) continue;
            float dx = p.x - eb.x;
            float dy = p.y - eb.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < 4.0f && dist > 0.1f) {
                float dotProduct = dx * eb.dirX + dy * eb.dirY;
                if (dotProduct > 0) {
                    float perpX = -eb.dirY;
                    float perpY = eb.dirX;
                    evadeX += perpX * (4.0f - dist);
                    evadeY += perpY * (4.0f - dist);
                }
            }
        }
        
        for (auto& fb : fireballs) {
            if (!fb.active) continue;
            float dx = p.x - fb.x;
            float dy = p.y - fb.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < 4.0f && dist > 0.1f) {
                float dotProduct = dx * fb.dirX + dy * fb.dirY;
                if (dotProduct > 0) {
                    float perpX = -fb.dirY;
                    float perpY = fb.dirX;
                    evadeX += perpX * (4.0f - dist);
                    evadeY += perpY * (4.0f - dist);
                }
            }
        }
        
        for (int i = 0; i < 6; i++) {
            if (claws[i].state == CLAW_SLAMMING || claws[i].state == CLAW_CHASING) {
                float dx = p.x - claws[i].x;
                float dy = p.y - claws[i].y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist < 6.0f && dist > 0.1f) {
                    evadeX += (dx / dist) * (6.0f - dist) * 2.0f;
                    evadeY += (dy / dist) * (6.0f - dist) * 2.0f;
                }
            }
            if (claws[i].state == CLAW_PH2_ANCHORED && activeLaserClaw == i) {
                float dx = p.x - claws[i].x;
                float dy = p.y - claws[i].y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist < 5.0f && dist > 0.1f) {
                    float perpX = -dy / dist;
                    float perpY = dx / dist;
                    evadeX += perpX * (5.0f - dist);
                    evadeY += perpY * (5.0f - dist);
                }
            }
        }
        
        if (evadeX != 0 || evadeY != 0) {
            float evadeDist = sqrtf(evadeX*evadeX + evadeY*evadeY);
            if (evadeDist > 0.1f) {
                p.x += (evadeX / evadeDist) * p.speed * 1.5f * deltaTime;
                p.y += (evadeY / evadeDist) * p.speed * 1.5f * deltaTime;
            }
        }
        
        if (distToPlayer > 16.0f) {
            float dx = player.x - p.x;
            float dy = player.y - p.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist > 0.1f) {
                p.x += (dx / dist) * p.speed * deltaTime;
                p.y += (dy / dist) * p.speed * deltaTime;
            }
            p.hunting = false;
        } else if (nearestEnemyIdx != -1) {
            p.hunting = true;
            p.targetEnemyIndex = nearestEnemyIdx;
            float dx = enemies[nearestEnemyIdx].x - p.x;
            float dy = enemies[nearestEnemyIdx].y - p.y;
            float dist = sqrtf(dx*dx + dy*dy);
            
            float repelX = 0, repelY = 0;
            for (auto& other : paragons) {
                if (&other == &p || !other.active) continue;
                float ox = p.x - other.x;
                float oy = p.y - other.y;
                float odist = sqrtf(ox*ox + oy*oy);
                if (odist < 1.2f && odist > 0.01f) {
                    repelX += (ox / odist) * (1.2f - odist);
                    repelY += (oy / odist) * (1.2f - odist);
                }
            }
            
            if (dist > 0.5f) {
                p.x += (dx / dist) * p.speed * deltaTime;
                p.y += (dy / dist) * p.speed * deltaTime;
            }
            if (repelX != 0 || repelY != 0) {
                float repelDist = sqrtf(repelX*repelX + repelY*repelY);
                if (repelDist > 0.01f) {
                    p.x += (repelX / repelDist) * p.speed * 0.3f * deltaTime;
                    p.y += (repelY / repelDist) * p.speed * 0.3f * deltaTime;
                }
            }
            if (dist < 1.0f) {
                enemies[nearestEnemyIdx].health -= 2;
                if (enemies[nearestEnemyIdx].health <= 0) {
                    enemies[nearestEnemyIdx].active = false;
                    if (enemies[nearestEnemyIdx].isMarshall) marshallKilled = true;
                    score++;
                    PlayScoreSound();
                    if (score > highScore) { highScore = score; SaveHighScore(); }
                }
            }
        } else if (nearestClawIdx != -1) {
            p.hunting = true;
            p.targetClawIndex = nearestClawIdx;
            float dx = claws[nearestClawIdx].x - p.x;
            float dy = claws[nearestClawIdx].y - p.y;
            float dist = sqrtf(dx*dx + dy*dy);
            
            float repelX = 0, repelY = 0;
            for (auto& other : paragons) {
                if (&other == &p || !other.active) continue;
                float ox = p.x - other.x;
                float oy = p.y - other.y;
                float odist = sqrtf(ox*ox + oy*oy);
                if (odist < 1.2f && odist > 0.01f) {
                    repelX += (ox / odist) * (1.2f - odist);
                    repelY += (oy / odist) * (1.2f - odist);
                }
            }
            
            if (dist > 0.5f) {
                p.x += (dx / dist) * p.speed * deltaTime;
                p.y += (dy / dist) * p.speed * deltaTime;
            }
            if (repelX != 0 || repelY != 0) {
                float repelDist = sqrtf(repelX*repelX + repelY*repelY);
                if (repelDist > 0.01f) {
                    p.x += (repelX / repelDist) * p.speed * 0.3f * deltaTime;
                    p.y += (repelY / repelDist) * p.speed * 0.3f * deltaTime;
                }
            }
            if (dist < 1.5f) {
                claws[nearestClawIdx].health -= 2;
                claws[nearestClawIdx].hurtTimer = 0.2f;
                if (claws[nearestClawIdx].health <= 0) {
                    claws[nearestClawIdx].state = CLAW_PH2_RISING;
                    claws[nearestClawIdx].timer = 2.0f;
                    PlayScoreSound();
                    if (activeLaserClaw == nearestClawIdx) activeLaserClaw = -1;
                }
            }
        } else {
            p.hunting = false;
            if (distToPlayer > 2.0f) {
                float dx = player.x - p.x;
                float dy = player.y - p.y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist > 0.1f) {
                    p.x += (dx / dist) * p.speed * deltaTime;
                    p.y += (dy / dist) * p.speed * deltaTime;
                }
            } else {
                float repelX = 0, repelY = 0;
                for (auto& other : paragons) {
                    if (&other == &p || !other.active) continue;
                    float ox = p.x - other.x;
                    float oy = p.y - other.y;
                    float odist = sqrtf(ox*ox + oy*oy);
                    if (odist < 1.5f && odist > 0.01f) {
                        repelX += (ox / odist) * (1.5f - odist);
                        repelY += (oy / odist) * (1.5f - odist);
                    }
                }
                if (repelX != 0 || repelY != 0) {
                    float repelDist = sqrtf(repelX*repelX + repelY*repelY);
                    if (repelDist > 0.01f) {
                        p.x += (repelX / repelDist) * p.speed * 0.5f * deltaTime;
                        p.y += (repelY / repelDist) * p.speed * 0.5f * deltaTime;
                    }
                }
            }
        }
        
        if (p.x < 1.5f) p.x = 1.5f; if (p.x >= MAP_WIDTH - 1.5f) p.x = (float)(MAP_WIDTH - 2);
        if (p.y < 1.5f) p.y = 1.5f; if (p.y >= MAP_HEIGHT - 1.5f) p.y = (float)(MAP_HEIGHT - 2);
    }
    
    for (auto& eb : enemyBullets) {
        if (!eb.active) continue;
        for (auto& p : paragons) {
            if (!p.active) continue;
            float dx = eb.x - p.x;
            float dy = eb.y - p.y;
            if (sqrtf(dx*dx + dy*dy) < 1.0f) {
                eb.active = false;
                p.health -= (eb.isLaser ? 10 : 5);
                p.hurtTimer = 1.0f;
                if (p.health <= 0) p.active = false;
                break;
            }
        }
    }
    
    for (auto& fb : fireballs) {
        if (!fb.active) continue;
        for (auto& p : paragons) {
            if (!p.active) continue;
            float dx = fb.x - p.x;
            float dy = fb.y - p.y;
            if (sqrtf(dx*dx + dy*dy) < 0.5f) {
                fb.active = false;
                p.health -= 10;
                p.hurtTimer = 1.0f;
                if (p.health <= 0) p.active = false;
                break;
            }
        }
    }
    
    for (auto& e : enemies) {
        if (!e.active || e.isShooter || e.isMarshall) continue;
        for (auto& p : paragons) {
            if (!p.active) continue;
            float dx = p.x - e.x;
            float dy = p.y - e.y;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < 1.0f) {
                p.health -= 1;
                p.hurtTimer = 1.0f;
                if (p.health <= 0) p.active = false;
                break;
            }
        }
    }
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
    
    for (int i = 0; i < 3; i++) {
        if (medkits[i].active) {
            int medkitScreenX = offsetX + (int)(medkits[i].x * cellSize);
            int medkitScreenY = offsetY + (int)(medkits[i].y * cellSize);
            HBRUSH medkitBrush = CreateSolidBrush(RGB(0, 150, 255));
            oldBrush = (HBRUSH)SelectObject(hdc, medkitBrush);
            Ellipse(hdc, medkitScreenX - 4, medkitScreenY - 4, medkitScreenX + 4, medkitScreenY + 4);
            SelectObject(hdc, oldBrush);
            DeleteObject(medkitBrush);
        }
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
    
    // Marshall Health Bar
    if (marshallHealthBarActive) {
        int barW = 300;
        int barH = 15;
        int barX = (SCREEN_WIDTH - barW) / 2;
        int barY = 40; 
        
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        RECT border = {barX - 2, barY - 2, barX + barW + 2, barY + barH + 2};
        FillRect(hdc, &border, blackBrush); 
        DeleteObject(blackBrush);
        
        if (marshallHP > 0) {
            int fillW = (int)((float)marshallHP / marshallMaxHP * barW);
            HBRUSH redBrush = CreateSolidBrush(RGB(200, 0, 0));
            RECT fill = {barX, barY, barX + fillW, barY + barH};
            FillRect(hdc, &fill, redBrush); 
            DeleteObject(redBrush);
        }
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        TextOutA(hdc, barX, barY - 15, "MARSHALL", 8);
    }
}

void UpdatePlayer(float deltaTime) {
    bool isSprinting = keys[VK_LSHIFT] || keys[VK_SHIFT];
    float sprintSpeed = enragedMode ? 13.0f : 6.5f;
    float baseSpeed = isSprinting ? sprintSpeed : 4.0f;
    float moveSpeed = baseSpeed * deltaTime;
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
    
    if (healFlashTimer > 0) healFlashTimer -= deltaTime;
    
    for (int i = 0; i < 3; i++) {
        if (medkits[i].active) {
            float dx = player.x - medkits[i].x;
            float dy = player.y - medkits[i].y;
            if (sqrtf(dx*dx + dy*dy) < 1.5f) {
                player.health += Medkit::HEAL_AMOUNT;
                if (player.health > 100) player.health = 100;
                medkits[i].active = false;
                medkits[i].respawnTimer = Medkit::RESPAWN_TIME;
                healFlashTimer = 1.0f;
                PlayHealSound();
            }
        } else {
            medkits[i].respawnTimer -= deltaTime;
            if (medkits[i].respawnTimer <= 0) {
                do {
                    medkits[i].x = 5.0f + (rand() % ((MAP_WIDTH - 10) * 10)) / 10.0f;
                    medkits[i].y = 5.0f + (rand() % ((MAP_HEIGHT - 10) * 10)) / 10.0f;
                } while (worldMap[(int)medkits[i].x][(int)medkits[i].y] != 0 || 
                         sqrtf((medkits[i].x - 32)*(medkits[i].x - 32) + (medkits[i].y - 32)*(medkits[i].y - 32)) < 5.0f);
                medkits[i].active = true;
            }
        }
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
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, SCREEN_WIDTH, SCREEN_HEIGHT);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    SetDIBitsToDevice(memDC, shakeX, shakeY, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, 
        backBufferPixels, &bi, DIB_RGB_COLORS);
    
    // Phase 2 Visuals (Force Field + Lasers)
    if (phase2Active) {
        // Force Field
        if (forceFieldActive) {
             float dx = 32.0f - player.x; // Boss X
             float dy = 32.0f - player.y; // Boss Y
             float dist = sqrtf(dx*dx + dy*dy);
             
             float spriteAngle = atan2f(dy, dx) - player.angle;
             while (spriteAngle > PI) spriteAngle -= 2 * PI;
             while (spriteAngle < -PI) spriteAngle += 2 * PI;
             
             if (fabsf(spriteAngle) < FOV && dist > 0.5f) {
                 float screenX = (0.5f + spriteAngle / FOV) * SCREEN_WIDTH;
                 float spriteHeight = (SCREEN_HEIGHT / dist) * 8.0f; // Boss Scale 8.0
                 int radius = (int)(spriteHeight / 2.0f * 0.8f); // Slightly smaller than full sprite width
                 
                 // Center Y calculation matching RenderSprite
                 int centerY = (SCREEN_HEIGHT / 2 + (int)((SCREEN_HEIGHT / 2.0f) / dist) + (int)player.pitch) - (int)(spriteHeight / 2.0f);

                 HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
                 HPEN oldPen = (HPEN)SelectObject(memDC, hPen);
                 HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                 Ellipse(memDC, (int)(screenX - radius), centerY - radius, (int)(screenX + radius), centerY + radius);
                 SelectObject(memDC, oldPen);
                 SelectObject(memDC, oldBrush);
                 DeleteObject(hPen);
             }
        }
        
        // Laser
        if (activeLaserClaw != -1 && claws[activeLaserClaw].state == CLAW_PH2_ANCHORED) {
             float dx = claws[activeLaserClaw].x - player.x;
             float dy = claws[activeLaserClaw].y - player.y;
             float dist = sqrtf(dx*dx + dy*dy);
             
             float spriteAngle = atan2f(dy, dx) - player.angle;
             while (spriteAngle > PI) spriteAngle -= 2 * PI;
             while (spriteAngle < -PI) spriteAngle += 2 * PI;
             
             if (fabsf(spriteAngle) < FOV && dist > 0.5f) {
                 float screenX = (0.5f + spriteAngle / FOV) * SCREEN_WIDTH;
                 float spriteHeight = (SCREEN_HEIGHT / dist) * 8.0f; // Claw Scale 8.0
                 
                 int centerY = (SCREEN_HEIGHT / 2 + (int)((SCREEN_HEIGHT / 2.0f) / dist) + (int)player.pitch) - (int)(spriteHeight / 2.0f);
                 
                 HPEN laserPen = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
                 HPEN oldPen = (HPEN)SelectObject(memDC, laserPen);
                 MoveToEx(memDC, (int)screenX, centerY, NULL);
                 LineTo(memDC, SCREEN_WIDTH / 2, SCREEN_HEIGHT); // To weapon
                 SelectObject(memDC, oldPen);
                 DeleteObject(laserPen);
             }
        }
    }
    
    int cx = SCREEN_WIDTH / 2;
    int cy = SCREEN_HEIGHT / 2;
    int reticleSize = 12;
    int reticleGap = 4;
    HPEN reticlePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HPEN oldPen = (HPEN)SelectObject(memDC, reticlePen);
    MoveToEx(memDC, cx - reticleSize, cy, NULL);
    LineTo(memDC, cx - reticleGap, cy);
    MoveToEx(memDC, cx + reticleGap, cy, NULL);
    LineTo(memDC, cx + reticleSize, cy);
    MoveToEx(memDC, cx, cy - reticleSize, NULL);
    LineTo(memDC, cx, cy - reticleGap);
    MoveToEx(memDC, cx, cy + reticleGap, NULL);
    LineTo(memDC, cx, cy + reticleSize);
    SelectObject(memDC, oldPen);
    DeleteObject(reticlePen);
    
    DrawMinimap(memDC);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 0));
    TextOutW(memDC, 10, 10, loadStatus, (int)wcslen(loadStatus));
    
    if (!missingAssets.empty()) {
        SetTextColor(memDC, RGB(255, 80, 80));
        int yPos = 30;
        TextOutA(memDC, 10, yPos, "MISSING ASSETS:", 15);
        yPos += 15;
        for (size_t i = 0; i < missingAssets.size() && i < 10; i++) {
            TextOutW(memDC, 20, yPos, missingAssets[i].c_str(), (int)missingAssets[i].length());
            yPos += 15;
        }
        if (missingAssets.size() > 10) {
            wchar_t moreText[64];
            swprintf(moreText, 64, L"... and %zu more", missingAssets.size() - 10);
            TextOutW(memDC, 20, yPos, moreText, (int)wcslen(moreText));
        }
    }
    
    wchar_t ammoText[64];
    if (isReloading) {
        swprintf(ammoText, 64, L"RELOADING...");
        SetTextColor(memDC, RGB(255, 255, 0));
    } else {
        swprintf(ammoText, 64, L"Ammo: %d/%d", ammo, maxAmmo);
        SetTextColor(memDC, ammo == 0 ? RGB(255, 0, 0) : RGB(255, 255, 255));
    }
    TextOutW(memDC, 10, 50, ammoText, (int)wcslen(ammoText));
    
    wchar_t scoreText[128];
    swprintf(scoreText, 128, L"Score: %d  High Score: %d", score, highScore);
    SetTextColor(memDC, RGB(255, 255, 255));
    TextOutW(memDC, 10, 90, scoreText, (int)wcslen(scoreText));
    
    if (paragonsUnlocked && paragonSummonCooldown > 0) {
        wchar_t cdText[64];
        swprintf(cdText, 64, L"Summon: %.1fs", paragonSummonCooldown);
        SetTextColor(memDC, RGB(147, 112, 219));
        TextOutW(memDC, 10, 130, cdText, (int)wcslen(cdText));
        
        int barW = 100;
        int barH = 8;
        int barX = 10;
        int barY = 155;
        RECT bgRect = {barX, barY, barX + barW, barY + barH};
        HBRUSH bgB = CreateSolidBrush(RGB(50, 50, 50));
        FillRect(memDC, &bgRect, bgB);
        DeleteObject(bgB);
        
        float pct = paragonSummonCooldown / 3.0f;
        if (pct > 1.0f) pct = 1.0f;
        int fillW = (int)(barW * (1.0f - pct));
        RECT fillRect = {barX, barY, barX + fillW, barY + barH};
        HBRUSH fillB = CreateSolidBrush(RGB(147, 112, 219));
        FillRect(memDC, &fillRect, fillB);
        DeleteObject(fillB);
    }
    
    if (scoreTimer > 0) {
        HFONT hFont = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);
        
        SetTextColor(memDC, RGB(255, 215, 0));
        SetBkMode(memDC, TRANSPARENT);
        
        wchar_t pointText[] = L"+1";
        SIZE size;
        GetTextExtentPoint32W(memDC, pointText, 2, &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, (SCREEN_HEIGHT - size.cy) / 2 - 40, pointText, 2);
        
        GetTextExtentPoint32W(memDC, scoreMsg, (int)wcslen(scoreMsg), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, (SCREEN_HEIGHT - size.cy) / 2 + 10, scoreMsg, (int)wcslen(scoreMsg));
        
        SelectObject(memDC, hOldFont);
        DeleteObject(hFont);
    }
    
    if (paragonMessageTimer > 0) {
        HFONT hPFont = CreateFontW(40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hOldPFont = (HFONT)SelectObject(memDC, hPFont);
        
        int alpha = (int)((paragonMessageTimer / 3.0f) * 255.0f);
        if (alpha > 255) alpha = 255;
        if (alpha < 0) alpha = 0;
        SetTextColor(memDC, RGB(147, 112, 219));
        SetBkMode(memDC, TRANSPARENT);
        
        const wchar_t* msg = L"The Brotherhood has deemed you worthy";
        SIZE sz;
        GetTextExtentPoint32W(memDC, msg, (int)wcslen(msg), &sz);
        TextOutW(memDC, (SCREEN_WIDTH - sz.cx) / 2, SCREEN_HEIGHT / 3, msg, (int)wcslen(msg));
        
        SelectObject(memDC, hOldPFont);
        DeleteObject(hPFont);
    }
    
    // Boss Health Bar
    if (bossActive && bossHealth > 0) {
        int barW = 400;
        int barH = 20;
        int barX = (SCREEN_WIDTH - barW) / 2;
        int barY = 40;
        
        RECT bgRect = {barX - 2, barY - 2, barX + barW + 2, barY + barH + 2};
        HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &bgRect, bgBrush);
        DeleteObject(bgBrush);
        
        float healthPct = (float)bossHealth / 200.0f;
        if (healthPct < 0) healthPct = 0;
        int hpW = (int)(barW * healthPct);
        RECT hpRect = {barX, barY, barX + hpW, barY + barH};
        HBRUSH hpBrush = CreateSolidBrush(RGB(200, 0, 0));
        FillRect(memDC, &hpRect, hpBrush);
        DeleteObject(hpBrush);
        
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutW(memDC, barX, barY - 20, L"THE SPIRE", 9);
    }
    
    // Countdown Timer during Pre-Boss Phase
    if (preBossPhase) {
        wchar_t bossTimerMsg[64];
        swprintf(bossTimerMsg, 64, L"BOSS IN: %.0f", preBossTimer);
        
        HFONT hFont = CreateFontW(50, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);
        
        SetTextColor(memDC, RGB(255, 0, 0));
        SetBkMode(memDC, TRANSPARENT);
        
        SIZE size;
        GetTextExtentPoint32W(memDC, bossTimerMsg, (int)wcslen(bossTimerMsg), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, SCREEN_HEIGHT / 2 - 50, bossTimerMsg, (int)wcslen(bossTimerMsg));
        
        SelectObject(memDC, hOldFont);
        DeleteObject(hFont);
    }

    // Pre-Boss Phase: Shaking "God has awoken" text
    if (bossActive && bossEventTimer > 0) {
        HFONT hFont = CreateFontW(60, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);
        SetTextColor(memDC, RGB(255, 0, 0));
        SetBkMode(memDC, TRANSPARENT);
        
        int shakeX = (rand() % 10) - 5;
        int shakeY = (rand() % 10) - 5;
        
        TextOutW(memDC, SCREEN_WIDTH/2 - 200 + shakeX, SCREEN_HEIGHT/2 - 100 + shakeY, L"God has awoken", 14);
        SelectObject(memDC, hOldFont);
        DeleteObject(hFont);
        
        SetTextColor(memDC, RGB(255, 255, 255));
    }

    
    SetTextColor(memDC, RGB(255, 255, 255));
    wchar_t info[128];
    swprintf(info, 128, L"WASD=Move | Mouse=Look | LClick=Shoot | R=Reload | ESC=Quit");
    TextOutW(memDC, 10, SCREEN_HEIGHT - 25, info, (int)wcslen(info));
    
    if (healFlashTimer > 0) {
        float intensity = healFlashTimer / 1.0f;
        if (intensity > 1.0f) intensity = 1.0f;
        int alpha = (int)(intensity * 80);
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            DWORD col = backBufferPixels[i];
            int r = (col >> 16) & 0xFF;
            int g = (col >> 8) & 0xFF;
            int b = col & 0xFF;
            g = g + alpha; if (g > 255) g = 255;
            backBufferPixels[i] = MakeColor(r, g, b);
        }
        
        BITMAPINFO biHeal = {};
        biHeal.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        biHeal.bmiHeader.biWidth = SCREEN_WIDTH;
        biHeal.bmiHeader.biHeight = -SCREEN_HEIGHT;
        biHeal.bmiHeader.biPlanes = 1;
        biHeal.bmiHeader.biBitCount = 32;
        SetDIBitsToDevice(memDC, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, 
            backBufferPixels, &biHeal, DIB_RGB_COLORS);
    }
    
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
        SetDIBitsToDevice(memDC, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, SCREEN_HEIGHT, 
            backBufferPixels, &bi2, DIB_RGB_COLORS);
        
        HFONT hBigFont = CreateFontW(72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hMedFont = CreateFontW(36, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hBtnFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        
        SetBkMode(memDC, TRANSPARENT);
        
        HFONT hOldFont = (HFONT)SelectObject(memDC, hBigFont);
        SetTextColor(memDC, RGB(0, 150, 0));
        const wchar_t* wonText = L"You Won!";
        SIZE size;
        GetTextExtentPoint32W(memDC, wonText, (int)wcslen(wonText), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, 150, wonText, (int)wcslen(wonText));
        
        SelectObject(memDC, hMedFont);
        SetTextColor(memDC, RGB(50, 50, 50));
        wchar_t hsText[128];
        swprintf(hsText, 128, L"Final Score: %d", score);
        GetTextExtentPoint32W(memDC, hsText, (int)wcslen(hsText), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, 240, hsText, (int)wcslen(hsText));
        
        swprintf(hsText, 128, L"High Score: %d", highScore);
        GetTextExtentPoint32W(memDC, hsText, (int)wcslen(hsText), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, 290, hsText, (int)wcslen(hsText));
        
        SelectObject(memDC, hBtnFont);
        
        RECT playAgainBtn = {SCREEN_WIDTH/2 - 120, 380, SCREEN_WIDTH/2 + 120, 430};
        RECT exitBtn = {SCREEN_WIDTH/2 - 120, 450, SCREEN_WIDTH/2 + 120, 500};
        
        HBRUSH greenBrush = CreateSolidBrush(RGB(0, 180, 0));
        HBRUSH redBrush = CreateSolidBrush(RGB(180, 0, 0));
        FillRect(memDC, &playAgainBtn, greenBrush);
        FillRect(memDC, &exitBtn, redBrush);
        DeleteObject(greenBrush);
        DeleteObject(redBrush);
        
        SetTextColor(memDC, RGB(255, 255, 255));
        const wchar_t* playText = L"Play Again";
        GetTextExtentPoint32W(memDC, playText, (int)wcslen(playText), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, 392, playText, (int)wcslen(playText));
        
        const wchar_t* exitText = L"Exit";
        GetTextExtentPoint32W(memDC, exitText, (int)wcslen(exitText), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, 462, exitText, (int)wcslen(exitText));
        
        SelectObject(memDC, hOldFont);
        DeleteObject(hBigFont);
        DeleteObject(hMedFont);
        DeleteObject(hBtnFont);
    }
    
    // Debug Console
    if (consoleActive) {
        RECT consoleRect = {0, 0, SCREEN_WIDTH, 200};
        HBRUSH consoleBrush = CreateSolidBrush(RGB(50, 50, 50)); // Dark Gray
        FillRect(memDC, &consoleRect, consoleBrush);
        DeleteObject(consoleBrush);
        
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255, 255, 255));
        HFONT hConsFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        HFONT hOldConsFont = (HFONT)SelectObject(memDC, hConsFont);
        
        TextOutW(memDC, 10, 10, L"DEBUG CONSOLE (type 'exit' to close)", 36);
        TextOutW(memDC, 10, 35, L">", 1);
        TextOutW(memDC, 25, 35, consoleBuffer.c_str(), (int)consoleBuffer.length());
        
        // Cursor
        if ((int)(GetTickCount() / 500) % 2 == 0) {
            SIZE size;
            GetTextExtentPoint32W(memDC, consoleBuffer.c_str(), (int)consoleBuffer.length(), &size);
            TextOutW(memDC, 25 + size.cx, 35, L"_", 1);
        }
        
        if (wcslen(consoleError) > 0) {
            SetTextColor(memDC, RGB(255, 80, 80));
            TextOutW(memDC, 10, 60, consoleError, (int)wcslen(consoleError));
            SetTextColor(memDC, RGB(255, 255, 255));
        }
        
        SelectObject(memDC, hOldConsFont);
        DeleteObject(hConsFont);
    }
    
    // Stats Display
    if (showStats) {
        int meleeCount = 0, shooterCount = 0;
        for (auto& e : enemies) {
            if (e.active) {
                if (e.isShooter) shooterCount++;
                else meleeCount++;
            }
        }
        int totalEnemies = meleeCount + shooterCount;
        int paragonCount = GetAliveParagonCount();
        
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(0, 0, 0));
        wchar_t statText[512];
        swprintf(statText, 512, L"FPS: %d  |  Enemies: %d (Melee: %d/%d, Shooters: %d/%d)  |  Paragons: %d/8  |  Pos: (%.1f, %.1f)  |  Cap Timer: %.1f", 
                 currentFPS, totalEnemies, meleeCount, maxMeleeSpawn, shooterCount, maxShooterSpawn, paragonCount, player.x, player.y, spawnCapTimer);
        TextOutW(memDC, 10, SCREEN_HEIGHT - 50, statText, (int)wcslen(statText));
    }
    
    if (errorTimer > 0 && wcslen(errorMessage) > 0) {
        HFONT hErrFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
        HFONT hOldErrFont = (HFONT)SelectObject(memDC, hErrFont);
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255, 50, 50));
        SIZE size;
        GetTextExtentPoint32W(memDC, errorMessage, (int)wcslen(errorMessage), &size);
        TextOutW(memDC, (SCREEN_WIDTH - size.cx) / 2, SCREEN_HEIGHT - 100, errorMessage, (int)wcslen(errorMessage));
        SelectObject(memDC, hOldErrFont);
        DeleteObject(hErrFont);
    }
    
    BitBlt(hdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            backBufferPixels = new DWORD[SCREEN_WIDTH * SCREEN_HEIGHT];
            zBuffer = new float[SCREEN_WIDTH * SCREEN_HEIGHT];
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
                    } else if (consoleBuffer == L"stat on") {
                        showStats = true;
                        consoleBuffer = L"";
                    } else if (consoleBuffer == L"stat off") {
                        showStats = false;
                        consoleBuffer = L"";
                    } else if (consoleBuffer == L"reset cam") {
                        player.pitch = 0.0f;
                        consoleBuffer = L"";
                    } else if (consoleBuffer.find(L"player.dmg") == 0) {
                        size_t eqPos = consoleBuffer.find(L'=');
                        if (eqPos != std::wstring::npos) {
                            std::wstring numStr = consoleBuffer.substr(eqPos + 1);
                            playerDamage = _wtoi(numStr.c_str());
                        } else {
                            size_t spacePos = consoleBuffer.find(L' ');
                             if (spacePos != std::wstring::npos) {
                                std::wstring numStr = consoleBuffer.substr(spacePos + 1);
                                playerDamage = _wtoi(numStr.c_str());
                            }
                        }
                        if (playerDamage < 1) playerDamage = 1;
                        consoleBuffer = L"";
                    } else if (consoleBuffer.find(L"player.gmode") == 0) {
                        if (consoleBuffer.find(L"true") != std::wstring::npos) godMode = true;
                        else if (consoleBuffer.find(L"false") != std::wstring::npos) godMode = false;
                        consoleBuffer = L"";
                    } else if (consoleBuffer == L"help") {
                        wcscpy(consoleError, L"Commands: score=N, stat on/off, reset cam, help, exit");
                        consoleBuffer = L"";
                    } else {
                        wcscpy(consoleError, L"Unknown command");
                        consoleBuffer = L"";
                    }
                } else {
                    consoleError[0] = L'\0';
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
            if (consoleActive) return 0;
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
                    phase2Active = false;
                    enragedMode = false;
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
            } else {
                ShootBullet();
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            if (consoleActive || victoryScreen) return 0;
            if (paragonsUnlocked && GetAliveParagonCount() < 8 && paragonSummonCooldown <= 0) {
                Paragon p;
                p.x = player.x;
                p.y = player.y;
                p.speed = 4.5f;
                p.health = 10;
                p.active = true;
                p.hurtTimer = 0;
                p.targetX = 0; p.targetY = 0;
                p.hunting = false;
                p.targetEnemyIndex = -1;
                p.targetClawIndex = -1;
                paragons.push_back(p);
                paragonSummonCooldown = 3.0f;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (consoleActive || victoryScreen) return 0;
            
            static int lastMouseX = SCREEN_WIDTH / 2;
            int mx = LOWORD(lParam);
            int deltaX = mx - lastMouseX;
            
            float sensitivity = 0.003f;
            player.angle += deltaX * sensitivity;
            
            POINT center = {SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2};
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
            lastMouseX = SCREEN_WIDTH / 2;
            
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
            
            for(int i=0; i<3; i++) if(spirePhase2Pixels[i] && spirePhase2Pixels[i] != spirePixels) delete[] spirePhase2Pixels[i];
            for(int i=0; i<4; i++) if(clawPhase2Pixels[i] && clawPhase2Pixels[i] != clawDormantPixels) delete[] clawPhase2Pixels[i];
            if(clawHurtPixels) delete[] clawHurtPixels;
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
    InitTrigTables();
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
    ShowCursor(FALSE);
    
    if (assetsFolderMissing) {
        MessageBoxW(hMainWnd, L"CRITICAL ERROR: Assets folder is missing or empty!\n\nThe game cannot start without assets.\nPlease ensure the 'assets' folder exists and contains the required files.", L"LoneShooter - Asset Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    _beginthread(BackgroundMusic, 0, NULL);
    
    MSG msg;
    DWORD lastTime = GetTickCount();
    
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return (int)msg.wParam;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        DWORD currentTime = GetTickCount();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        if (deltaTime > 0.1f) deltaTime = 0.1f;
        if (deltaTime < 0.001f) deltaTime = 0.001f;
        
        if (scoreTimer > 0) scoreTimer -= deltaTime;
        if (screenShakeTimer > 0) screenShakeTimer -= deltaTime;
        if (errorTimer > 0) errorTimer -= deltaTime;
        
        fpsCounter++;
        if (currentTime - fpsLastTime >= 1000) {
            currentFPS = fpsCounter;
            fpsCounter = 0;
            fpsLastTime = currentTime;
        }
        
        UpdatePlayer(deltaTime);
        UpdateEnemies(deltaTime);
        UpdateClouds(deltaTime);
        UpdateGun(deltaTime);
        UpdateBullets(deltaTime);
        UpdateParagons(deltaTime);
        
        HDC hdc = GetDC(hMainWnd);
        RenderGame(hdc);
        ReleaseDC(hMainWnd, hdc);
    }
    return 0;
}
