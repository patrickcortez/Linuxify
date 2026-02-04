/*
 * LoneMaker - 3D Level Editor
 * Compile: g++ -o LoneMaker.exe lonemaker.cpp -lgdi32 -mwindows -O2
 * Run: ./LoneMaker.exe
 */

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <cmath>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <string>
#include <cwchar>

// --- Constants ---
const int SCREEN_WIDTH = 1000; // Wider for sidebar
const int SCREEN_HEIGHT = 600;
const int SIDEBAR_WIDTH = 150;
const float PI = 3.14159265f;

// --- Math ---
struct Vec3 { float x, y, z; };
struct Mat4 { float m[4][4]; };

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
Mat4 MatrixRotationZ(float angle) {
    Mat4 mat = MatrixIdentity();
    mat.m[0][0] = cosf(angle); mat.m[0][1] = sinf(angle);
    mat.m[1][0] = -sinf(angle); mat.m[1][1] = cosf(angle);
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
    for(int i=0; i<4; i++)
        for(int j=0; j<4; j++)
            for(int k=0; k<4; k++)
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

// --- Data ---
struct Vertex { Vec3 pos; };
struct Triangle { int p1, p2, p3; DWORD color; bool selected; };
struct Object {
    Vec3 pos;
    Vec3 rot; // Euler Angles
    std::vector<Vertex> verts; // Local Space
    std::vector<Triangle> tris;
    bool selected;
};

std::vector<Object> scene;
int selectedObjIndex = -1;

DWORD* backBufferPixels = NULL;
float* zBuffer = NULL;
HWND hMainWnd;

// Camera
float camYaw = 0.5f, camPitch = -0.5f;
float camDist = 15.0f; // Zoomed out a bit
Vec3 camTarget = {0,0,0}; // Target for translation

// --- Globals ---
enum ToolMode { MODE_VIEW, MODE_EDIT };
ToolMode appMode = MODE_EDIT;
bool isDragging = false;
int dragStartX, dragStartY;

// Global UI State
wchar_t lastStatus[64] = L"Ready";
int hoverButtonId = -1;

// R-Drag State
bool isRDragging = false;
int rDragStartX, rDragStartY;

// --- Generation ---
void AddCube() {
    Object obj;
    // Random offset to avoid z-fighting/overlap on multiple adds
    float ox = (rand()%10)/100.0f; 
    float oy = (rand()%10)/100.0f;
    obj.pos = {ox,oy,0}; obj.rot = {0,0,0}; obj.selected = true; // Auto-select new
    // Deselect others
    for(auto& o : scene) o.selected = false;
    selectedObjIndex = scene.size(); // Set to this new index (will be pushed back next)
    
    float s = 1.0f;
    obj.verts = {
        {-s,-s,-s}, {s,-s,-s}, {s,s,-s}, {-s,s,-s},
        {-s,-s,s}, {s,-s,s}, {s,s,s}, {-s,s,s}
    };
    DWORD c = 0xFFCCCCCC; // Grey
    obj.tris = {
        {0,2,1,c}, {0,3,2,c}, {1,6,5,c}, {1,2,6,c}, {5,6,7,c}, {5,7,4,c},
        {4,7,3,c}, {4,3,0,c}, {3,7,6,c}, {3,6,2,c}, {4,0,1,c}, {4,1,5,c}
    };
    scene.push_back(obj);
}

void AddCone() {
    Object obj;
    obj.pos = {0,0,0}; obj.rot = {0,0,0}; obj.selected = false;
    float r = 1.0f, h = 2.0f;
    obj.verts.push_back({0, h/2, 0}); // Tip
    for(int i=0; i<8; i++) {
        float a = i * (2*PI/8);
        obj.verts.push_back({cosf(a)*r, -h/2, sinf(a)*r});
    }
    obj.verts.push_back({0, -h/2, 0}); // 9: Base Center
    DWORD c = 0xFF88CC88; // Greenish
    for(int i=0; i<8; i++) {
        int next = (i+1)%8;
        obj.tris.push_back({0, 1+next, 1+i, c}); // Side
        obj.tris.push_back({9, 1+i, 1+next, c}); // Base (Fixed index 10->9)
    }
    scene.push_back(obj);
}

void AddSphere() {
    Object obj;
    obj.pos = {0,0,0}; obj.rot = {0,0,0}; obj.selected = false;
    float r = 1.0f;
    int rings = 8, sectors = 8;
    float R = 1.0f/(float)(rings-1);
    float S = 1.0f/(float)(sectors-1);
    
    for(int rs=0; rs<rings; rs++) for(int s=0; s<sectors; s++) {
        float y = sinf(-PI/2 + PI * rs * R);
        float x = cosf(2*PI * s * S) * sinf(PI * rs * R);
        float z = sinf(2*PI * s * S) * sinf(PI * rs * R);
        obj.verts.push_back({x*r, y*r, z*r});
    }
    
    DWORD c = 0xFF8888CC; // Blueish
    for(int r=0; r<rings-1; r++) for(int s=0; s<sectors-1; s++) {
        int cur = r * sectors + s;
        int next = (r+1) * sectors + s;
        obj.tris.push_back({cur, next, cur+1, c});
        obj.tris.push_back({cur+1, next, next+1, c});
    }
    scene.push_back(obj);
}

// --- IO ---
void SaveModel() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = L"Lone Models (*.lone)\0*.lone\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"lone";
    
    if(GetSaveFileName(&ofn)) {
        FILE* f = _wfopen(filename, L"wb");
        if(!f) return;
        int magic = 0x3D3D3D3D;
        int objCount = scene.size();
        fwrite(&magic, 4, 1, f);
        fwrite(&objCount, 4, 1, f);
        for(const auto& val : scene) {
            fwrite(&val.pos, sizeof(Vec3), 1, f);
            fwrite(&val.rot, sizeof(Vec3), 1, f);
            int vCount = val.verts.size();
            int tCount = val.tris.size();
            fwrite(&vCount, 4, 1, f);
            fwrite(&tCount, 4, 1, f);
            fwrite(val.verts.data(), sizeof(Vertex), vCount, f);
            fwrite(val.tris.data(), sizeof(Triangle), tCount, f);
        }
        fclose(f);
        lstrcpyW(lastStatus, L"Saved Model");
    }
}

void LoadModel() {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWnd;
    ofn.lpstrFilter = L"Lone Models (*.lone)\0*.lone\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt = L"lone";

    if(GetOpenFileName(&ofn)) {
        FILE* f = _wfopen(filename, L"rb");
        if(!f) return;
        int magic, objCount;
        fread(&magic, 4, 1, f);
        fread(&objCount, 4, 1, f);
        scene.clear();
        for(int i=0; i<objCount; i++) {
            Object obj;
            fread(&obj.pos, sizeof(Vec3), 1, f);
            fread(&obj.rot, sizeof(Vec3), 1, f);
            int vCount, tCount;
            fread(&vCount, 4, 1, f);
            fread(&tCount, 4, 1, f);
            obj.verts.resize(vCount);
            obj.tris.resize(tCount);
            fread(obj.verts.data(), sizeof(Vertex), vCount, f);
            fread(obj.tris.data(), sizeof(Triangle), tCount, f);
            obj.selected = false;
            scene.push_back(obj);
        }
        fclose(f);
        lstrcpyW(lastStatus, L"Loaded Model");
    }
}

// --- Context Menu & Logic ---
Object clipboardObj;
bool hasClipboard = false;

void CopyObject() {
    if(selectedObjIndex != -1 && selectedObjIndex < (int)scene.size()) {
        const Object& src = scene[selectedObjIndex];
        clipboardObj.pos = src.pos;
        clipboardObj.rot = src.rot;
        clipboardObj.verts = src.verts;
        clipboardObj.tris = src.tris;
        clipboardObj.selected = false;
        hasClipboard = true;
        lstrcpyW(lastStatus, L"Copied Object");
    }
}

void PasteObject() {
    if(hasClipboard) {
        Object newObj;
        newObj.pos = clipboardObj.pos;
        newObj.rot = clipboardObj.rot;
        newObj.verts = clipboardObj.verts;
        newObj.tris = clipboardObj.tris;
        
        newObj.pos.x += 1.0f; // Offset slightly
        newObj.pos.z += 1.0f;
        newObj.selected = true;
        for(auto& o : scene) o.selected = false;
        scene.push_back(newObj);
        selectedObjIndex = (int)scene.size()-1;
        lstrcpyW(lastStatus, L"Pasted Object");
    }
}

void DeleteSelected() {
    if(selectedObjIndex != -1 && selectedObjIndex < scene.size()) {
        scene.erase(scene.begin() + selectedObjIndex);
        selectedObjIndex = -1;
        lstrcpyW(lastStatus, L"Deleted Object");
    }
}

void ColorObject() {
    if(selectedObjIndex != -1 && selectedObjIndex < (int)scene.size()) {
        CHOOSECOLOR cc;
        static COLORREF acrCustClr[16];
        ZeroMemory(&cc, sizeof(cc));
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = hMainWnd;
        cc.lpCustColors = (LPDWORD)acrCustClr;
        cc.rgbResult = RGB(255, 0, 0);
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        
        if (ChooseColor(&cc)) {
            DWORD r = GetRValue(cc.rgbResult);
            DWORD g = GetGValue(cc.rgbResult);
            DWORD b = GetBValue(cc.rgbResult);
            DWORD newColor = (r << 16) | (g << 8) | b;
            
            for(auto& t : scene[selectedObjIndex].tris) t.color = newColor;
            lstrcpyW(lastStatus, L"Changed Color");
        }
    }
}

void ShowContextMenu(int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    
    if(selectedObjIndex != -1) {
        AppendMenu(hMenu, MF_STRING, 1001, L"Delete Object");
        AppendMenu(hMenu, MF_STRING, 1002, L"Copy Object");
        AppendMenu(hMenu, MF_STRING, 1004, L"Color Object...");
    }
    
    if(hasClipboard) {
        AppendMenu(hMenu, MF_STRING, 1003, L"Paste Object");
    }
    
    POINT pt = {x, y};
    ClientToScreen(hMainWnd, &pt);
    
    int result = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0, hMainWnd, NULL);
    
    if(result == 1001) DeleteSelected();
    if(result == 1002) CopyObject();
    if(result == 1003) PasteObject();
    if(result == 1004) ColorObject();
    
    DestroyMenu(hMenu);
}

// --- Rendering ---
void DrawRect(int x, int y, int w, int h, DWORD c) {
    for(int iy=y; iy<y+h; iy++) for(int ix=x; ix<x+w; ix++) {
        if(ix>=0 && ix<SCREEN_WIDTH && iy>=0 && iy<SCREEN_HEIGHT)
            backBufferPixels[ix + iy*SCREEN_WIDTH] = c;
    }
}

void DrawPixelZ(int x, int y, float z, DWORD c) {
    if(x>=SIDEBAR_WIDTH && x<SCREEN_WIDTH && y>=0 && y<SCREEN_HEIGHT) { // Respect Sidebar
        if(z < zBuffer[x + y*SCREEN_WIDTH]) {
            zBuffer[x + y*SCREEN_WIDTH] = z;
            backBufferPixels[x + y*SCREEN_WIDTH] = c;
        }
    }
}

float EdgeFunc(int x1, int y1, int x2, int y2, int px, int py) {
    return (float)((px - x1) * (y2 - y1) - (py - y1) * (x2 - x1));
}

void RasterizeTri(Vec3 v1, Vec3 v2, Vec3 v3, DWORD color) {
    int x1 = (int)((v1.x + 1) * 0.5f * (SCREEN_WIDTH - SIDEBAR_WIDTH) + SIDEBAR_WIDTH); // Viewport Offset
    int y1 = (int)((1 - v1.y) * 0.5f * SCREEN_HEIGHT);
    int x2 = (int)((v2.x + 1) * 0.5f * (SCREEN_WIDTH - SIDEBAR_WIDTH) + SIDEBAR_WIDTH);
    int y2 = (int)((1 - v2.y) * 0.5f * SCREEN_HEIGHT);
    int x3 = (int)((v3.x + 1) * 0.5f * (SCREEN_WIDTH - SIDEBAR_WIDTH) + SIDEBAR_WIDTH);
    int y3 = (int)((1 - v3.y) * 0.5f * SCREEN_HEIGHT);
    
    int minX = std::max(SIDEBAR_WIDTH, std::min(x1, std::min(x2, x3)));
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
            
            // Accept both winding orders (Double Sided) to fix missing faces
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            
            if(inside) {
                w0/=area; w1/=area; w2/=area;
                float z = 1.0f / (w0/v1.z + w1/v2.z + w2/v3.z);
                DrawPixelZ(x, y, z, color);
            }
        }
    }
}

void DrawLine(int x1, int y1, int x2, int y2, DWORD color) {
    int dx = abs(x2-x1), sx = x1<x2 ? 1 : -1;
    int dy = -abs(y2-y1), sy = y1<y2 ? 1 : -1; 
    int err = dx+dy, e2;
    while(1) {
        if(x1>=SIDEBAR_WIDTH && x1<SCREEN_WIDTH && y1>=0 && y1<SCREEN_HEIGHT)
            backBufferPixels[x1 + y1*SCREEN_WIDTH] = color;
        if(x1==x2 && y1==y2) break;
        e2 = 2*err;
        if(e2 >= dy) { err += dy; x1 += sx; }
        if(e2 <= dx) { err += dx; y1 += sy; }
    }
}

void RenderScene() {
    // Clear
    for(int i=0; i<SCREEN_WIDTH*SCREEN_HEIGHT; i++) {
        backBufferPixels[i] = 0xFF202020; // Dark Grey Background
        zBuffer[i] = 1e9f;
    }
    
    // Grid
    Mat4 matRotY = MatrixRotationY(camYaw);
    Mat4 matRotX = MatrixRotationX(camPitch);
    Mat4 matTrans = MatrixTranslation(0, 0, camDist); // Positive Z to put objects in front of camera (since w=z)
    Mat4 matView = MatrixMultiply(matRotY, MatrixMultiply(matRotX, matTrans));
    Mat4 matProj = MatrixPerspective(PI/3.0f, (float)(SCREEN_WIDTH-SIDEBAR_WIDTH)/SCREEN_HEIGHT, 0.1f, 100.0f);
    
    auto Project = [&](Vec3 v) {
        Vec3 t = TransformPoint(matView, v);
        return TransformPoint(matProj, t);
    };
    
    // Draw Grid Lines (Floor at Y = -1)
    auto ScreenX = [&](float x) { return (int)((x + 1) * 0.5f * (SCREEN_WIDTH - SIDEBAR_WIDTH) + SIDEBAR_WIDTH); };
    auto ScreenY = [&](float y) { return (int)((1 - y) * 0.5f * SCREEN_HEIGHT); };
    
    for(int i=-10; i<=10; i++) {
        // Z-axis lines
        Vec3 p1 = {(float)i, -1.0f, -10.0f}; Vec3 p2 = {(float)i, -1.0f, 10.0f};
        Vec3 t1 = Project(p1); Vec3 t2 = Project(p2);
        // Simple clip check before drawing
        if(t1.z > 0 && t1.z < 100 && t2.z > 0 && t2.z < 100) { 
             DrawLine(ScreenX(t1.x), ScreenY(t1.y), ScreenX(t2.x), ScreenY(t2.y), 0xFF666666);
        }
        
        // X-axis lines
        p1 = {-10.0f, -1.0f, (float)i}; p2 = {10.0f, -1.0f, (float)i};
        t1 = Project(p1); t2 = Project(p2);
        if(t1.z > 0 && t1.z < 100 && t2.z > 0 && t2.z < 100) {
             DrawLine(ScreenX(t1.x), ScreenY(t1.y), ScreenX(t2.x), ScreenY(t2.y), 0xFF666666);
        }
    }
    
    Vec3 lightDir = Normalize({0.5f, 1.0f, -0.5f});
    
    for(int i=0; i<scene.size(); i++) {
        Object& obj = scene[i];
        
        Mat4 modelMat = MatrixMultiply(MatrixRotationY(obj.rot.y), MatrixTranslation(obj.pos.x, obj.pos.y, obj.pos.z));
        
        for(int t=0; t<obj.tris.size(); t++) {
            Triangle& tri = obj.tris[t];
            Vec3 v1 = TransformPoint(modelMat, obj.verts[tri.p1].pos);
            Vec3 v2 = TransformPoint(modelMat, obj.verts[tri.p2].pos);
            Vec3 v3 = TransformPoint(modelMat, obj.verts[tri.p3].pos);
            
            // View Transform
            Vec3 tv1 = TransformPoint(matView, v1);
            Vec3 tv2 = TransformPoint(matView, v2);
            Vec3 tv3 = TransformPoint(matView, v3);
            
            // Lighting (World Space Normal)
            Vec3 normal = Normalize(Cross(Sub(v2,v1), Sub(v3,v1)));
            float intensity = Dot(normal, lightDir);
            if(intensity < 0.2f) intensity = 0.2f;
            
            DWORD c = tri.color;
            if(obj.selected) c = 0xFFFFFF00; // Yellow highlight
            
            int r = (c >> 16) & 0xFF;
            int g = (c >> 8) & 0xFF;
            int b = (c) & 0xFF;
            r = (int)(r * intensity); g = (int)(g * intensity); b = (int)(b * intensity);
            DWORD litColor = (r << 16) | (g << 8) | b;
            
            // Projection
            Vec3 p1 = TransformPoint(matProj, tv1);
            Vec3 p2 = TransformPoint(matProj, tv2);
            Vec3 p3 = TransformPoint(matProj, tv3);
            
            // Clip? Simple Z check
            if(tv1.z > 0.1f && tv2.z > 0.1f && tv3.z > 0.1f)
                RasterizeTri(p1, p2, p3, litColor);
        }
    }
    
    // Sidebar UI Overlay
    DrawRect(0, 0, SIDEBAR_WIDTH, SCREEN_HEIGHT, 0xFF404040);
}

// --- UI Logic ---
struct Button { int id; const wchar_t* label; int y; };
Button buttons[] = {
    {1, L"Add Cube", 10},
    {2, L"Add Sphere", 50},
    {3, L"Add Cone", 90}, 
    {4, L"Clear All", 130},
    {5, L"Save", 170},
    {6, L"Load", 210},
    {7, L"Fuse Selected", 250},
    {10, L"MODE: VIEW", 300},
    {11, L"MODE: EDIT", 340}
};

// Moved lastStatus/hoverButtonId to top

void HandleClick(int x, int y) {
    if(x < SIDEBAR_WIDTH) {
        for(auto& b : buttons) {
            if(y >= b.y && y < b.y + 30) {
                if(b.id == 1) { if(appMode==MODE_EDIT) { AddCube(); lstrcpyW(lastStatus, L"Added Cube"); } }
                if(b.id == 2) { if(appMode==MODE_EDIT) { AddSphere(); lstrcpyW(lastStatus, L"Added Sphere"); } }
                if(b.id == 3) { if(appMode==MODE_EDIT) { AddCone(); lstrcpyW(lastStatus, L"Added Cone"); } }
                if(b.id == 4) { scene.clear(); lstrcpyW(lastStatus, L"Cleared Scene"); }
                if(b.id == 5) { SaveModel(); lstrcpyW(lastStatus, L"Saved Model"); }
                if(b.id == 6) { LoadModel(); lstrcpyW(lastStatus, L"Loaded Model"); }
                if(b.id == 7) { lstrcpyW(lastStatus, L"Fused Objects"); }
                if(b.id == 10) { appMode = MODE_VIEW; lstrcpyW(lastStatus, L"Switched to VIEW Mode"); }
                if(b.id == 11) { appMode = MODE_EDIT; lstrcpyW(lastStatus, L"Switched to EDIT Mode"); }
                return;
            }
        }
    } else {
        // 3D Viewport Click
        if(appMode == MODE_EDIT) {
             // Selection logic (Cycle select)
            if(scene.size() > 0) {
                selectedObjIndex = (selectedObjIndex + 1) % scene.size();
                for(int i=0; i<scene.size(); i++) scene[i].selected = (i == selectedObjIndex);
                lstrcpyW(lastStatus, L"Selected Object");
            }
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderScene();
            
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = SCREEN_WIDTH;
            bi.bmiHeader.biHeight = -SCREEN_HEIGHT;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(hdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, backBufferPixels, &bi, DIB_RGB_COLORS, SRCCOPY);
            
            // Draw UI Text on top
            SetBkMode(hdc, TRANSPARENT);
            
            // Status Bar
            SetTextColor(hdc, RGB(200, 200, 200));
            TextOutW(hdc, 10, SCREEN_HEIGHT - 20, lastStatus, wcslen(lastStatus));
            
            for(auto& b : buttons) {
                RECT r = {10, b.y + 5, SIDEBAR_WIDTH, b.y + 30};
                
                // Button Background
                RECT border = {5, b.y, SIDEBAR_WIDTH - 5, b.y + 30};
                HBRUSH bgBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
                if (b.id == hoverButtonId) {
                    bgBrush = CreateSolidBrush(RGB(80, 80, 80));
                    FillRect(hdc, &border, bgBrush);
                    DeleteObject(bgBrush);
                }
                
                SetTextColor(hdc, RGB(255, 255, 255));
                DrawTextW(hdc, b.label, -1, &r, DT_LEFT);
                
                // Button Border
                FrameRect(hdc, &border, (HBRUSH)GetStockObject(LTGRAY_BRUSH));
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            dragStartX = LOWORD(lParam);
            dragStartY = HIWORD(lParam);
            isDragging = true;
            HandleClick(dragStartX, dragStartY);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
            
        case WM_LBUTTONUP:
            isDragging = false;
            return 0;
            
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            
            if (isDragging) {
                int dx = x - dragStartX;
                int dy = y - dragStartY;
                
                if (appMode == MODE_VIEW) {
                     // Rotate Camera (Left Click Drag usually Pans, but let's keep it simple)
                     // If User wants to rotate camera in View Mode, they can use Right Click too.
                     // Let's use Left Drag for Pan in View Mode perhaps?
                     // Current code used L-Drag for Cam Rotate in View Mode.
                     camYaw -= dx * 0.01f;
                     camPitch -= dy * 0.01f;
                } else if (appMode == MODE_EDIT) {
                     // Move Object (XZ Plane)
                     if(x > SIDEBAR_WIDTH && selectedObjIndex != -1 && selectedObjIndex < scene.size()) {
                         float moveScale = 0.02f;
                         float worldDX = dx * moveScale;
                         float worldDZ = -dy * moveScale; 
                         
                         float cosY = cosf(camYaw);
                         float sinY = sinf(camYaw);
                         
                         scene[selectedObjIndex].pos.x += worldDX * cosY - worldDZ * sinY;
                         scene[selectedObjIndex].pos.z += worldDZ * cosY + worldDX * sinY;
                         
                         lstrcpyW(lastStatus, L"Moving Object");
                     }
                }
                
                dragStartX = x;
                dragStartY = y;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            
            // Right Click Drag Logic
            int dx = x - dragStartX;
            
            if(msg == WM_RBUTTONDOWN) {
                rDragStartX = x; rDragStartY = y;
                isRDragging = false; // Reset
            }
            if(msg == WM_MOUSEMOVE && (wParam & MK_RBUTTON)) {
                 if (abs(x - rDragStartX) > 2 || abs(y - rDragStartY) > 2) isRDragging = true;
                 
                 // Drag Logic
                 if(isRDragging) {
                     int rdx = x - rDragStartX;
                     int rdy = y - rDragStartY;
                     // ... View/Edit Rotate logic from before ...
                     if (appMode == MODE_VIEW) {
                        camYaw -= rdx * 0.01f;
                        camPitch -= rdy * 0.01f;
                     } else if (appMode == MODE_EDIT) {
                        if(selectedObjIndex != -1 && selectedObjIndex < scene.size()) {
                            scene[selectedObjIndex].rot.y += rdx * 0.01f;
                            scene[selectedObjIndex].rot.x += rdy * 0.01f;
                            lstrcpyW(lastStatus, L"Rotating Object");
                        }
                     }
                     rDragStartX = x; rDragStartY = y;
                     InvalidateRect(hwnd, NULL, FALSE);
                 }
            }
            // L-Drag Logic (Existing)
            if (isDragging) {
                 // Copy existing logic for L-Drag
                 int dx = x - dragStartX;
                 int dy = y - dragStartY;
                 if (appMode == MODE_VIEW) {
                     camYaw -= dx * 0.01f;
                     camPitch -= dy * 0.01f;
                 } else if (appMode == MODE_EDIT) {
                     if(x > SIDEBAR_WIDTH && selectedObjIndex != -1 && selectedObjIndex < scene.size()) {
                         float moveScale = 0.02f;
                         float worldDX = dx * moveScale;
                         float worldDZ = -dy * moveScale; 
                         float cosY = cosf(camYaw); float sinY = sinf(camYaw);
                         scene[selectedObjIndex].pos.x += worldDX * cosY - worldDZ * sinY;
                         scene[selectedObjIndex].pos.z += worldDZ * cosY + worldDX * sinY;
                         lstrcpyW(lastStatus, L"Moving Object");
                     }
                 }
                 dragStartX = x; dragStartY = y;
                 InvalidateRect(hwnd, NULL, FALSE);
            }
            
            // UI Hover Logic
            hoverButtonId = -1;
            if(x < SIDEBAR_WIDTH) {
                for(auto& b : buttons) {
                    if(y >= b.y && y < b.y + 30) {
                        hoverButtonId = b.id;
                        break;
                    }
                }
                if(hoverButtonId != -1) InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_RBUTTONDOWN:
            rDragStartX = LOWORD(lParam);
            rDragStartY = HIWORD(lParam);
            isRDragging = false;
            return 0;

        case WM_RBUTTONUP: {
             if (!isRDragging) {
                 ShowContextMenu(LOWORD(lParam), HIWORD(lParam));
                 InvalidateRect(hwnd, NULL, FALSE);
             }
             isRDragging = false;
             return 0;
        }
        case WM_KEYDOWN:
            // Gizmo: Move Selected
            if(selectedObjIndex != -1 && selectedObjIndex < scene.size()) {
                float speed = 0.2f;
                // Planar Move
                if(wParam == 'W') scene[selectedObjIndex].pos.z += speed; 
                if(wParam == 'S') scene[selectedObjIndex].pos.z -= speed;
                if(wParam == 'A') scene[selectedObjIndex].pos.x -= speed;
                if(wParam == 'D') scene[selectedObjIndex].pos.x += speed;
                
                // Vertical Move (Up/Down Arrows)
                if(wParam == VK_UP) scene[selectedObjIndex].pos.y += speed;
                if(wParam == VK_DOWN) scene[selectedObjIndex].pos.y -= speed;
                
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if(wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    backBufferPixels = new DWORD[SCREEN_WIDTH * SCREEN_HEIGHT];
    zBuffer = new float[SCREEN_WIDTH * SCREEN_HEIGHT];
    
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"LoneMakerV2Class";
    RegisterClassEx(&wc);
    
    // Add default cube so screen isn't empty
    AddCube();
    lstrcpyW(lastStatus, L"Ready (Default Cube Added)");
    
    hMainWnd = CreateWindowEx(0, L"LoneMakerV2Class", L"LoneMaker 3D Studio",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, SCREEN_WIDTH+16, SCREEN_HEIGHT+39,
        NULL, NULL, hInstance, NULL);
        
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
