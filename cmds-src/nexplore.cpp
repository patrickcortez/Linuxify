// Nexplore - Node File System Explorer
// A Win32 GUI tool to browse .node file contents with Explorer-style interface
// Compile: g++ -std=c++17 -static -mwindows -o cmds\nexplore.exe cmds-src\nexplore.cpp -lgdi32 -luser32 -lcomdlg32 -ldwmapi -lshell32

#define _WIN32_WINNT 0x0A00
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stack>

constexpr uint32_t NODE_MAGIC = 0x4E4F4445;
constexpr uint32_t NODE_VERSION = 3;
constexpr uint32_t DEFAULT_BLOCK_SIZE = 4096;
constexpr uint32_t MAX_NAME_LEN = 63;
constexpr uint32_t DATA_BLOCKS_COUNT = 10;
constexpr uint32_t EDGE_BLOCKS_COUNT = 4;
constexpr uint32_t SUPERBLOCK_SIZE = 512;
constexpr uint32_t SALT_SIZE = 16;
constexpr uint32_t VERIFY_TAG_SIZE = 32;
constexpr uint32_t KDF_ITERATIONS = 10000;

class SHA256 {
private:
    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    static uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    static uint32_t ep0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    static uint32_t ep1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
public:
    static std::string hash(const std::string& data) {
        uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        std::vector<uint8_t> msg(data.begin(), data.end());
        size_t origLen = msg.size();
        msg.push_back(0x80);
        while ((msg.size() + 8) % 64 != 0) msg.push_back(0);
        uint64_t bitLen = origLen * 8;
        for (int i = 7; i >= 0; i--) msg.push_back((bitLen >> (i * 8)) & 0xFF);
        for (size_t i = 0; i < msg.size(); i += 64) {
            uint32_t w[64];
            for (int j = 0; j < 16; j++) w[j] = (msg[i+j*4]<<24)|(msg[i+j*4+1]<<16)|(msg[i+j*4+2]<<8)|msg[i+j*4+3];
            for (int j = 16; j < 64; j++) w[j] = ep1(w[j-2]) + w[j-7] + ep0(w[j-15]) + w[j-16];
            uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
            for (int j = 0; j < 64; j++) {
                uint32_t t1 = hh + sig1(e) + ch(e,f,g) + K[j] + w[j];
                uint32_t t2 = sig0(a) + maj(a,b,c);
                hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
            }
            h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        }
        std::ostringstream oss;
        for (int i = 0; i < 8; i++) oss << std::hex << std::setfill('0') << std::setw(8) << h[i];
        return oss.str();
    }
};
constexpr uint32_t SHA256::K[64];

std::string deriveKey(const std::string& password, const uint8_t* salt) {
    std::string input = password;
    for (size_t i = 0; i < SALT_SIZE; i++) input += static_cast<char>(salt[i]);
    std::string key = SHA256::hash(input);
    for (uint32_t i = 1; i < KDF_ITERATIONS; i++) key = SHA256::hash(key + input);
    return key;
}

void generateEncryptedMagic(uint8_t* dest, const std::string& key) {
    std::string pattern = SHA256::hash(key + "MAGIC_OBFUSCATE");
    uint32_t magicData[2] = { NODE_MAGIC, NODE_VERSION };
    for (size_t i = 0; i < 8; i++) dest[i] = reinterpret_cast<uint8_t*>(magicData)[i] ^ static_cast<uint8_t>(pattern[i]);
}

bool verifyEncryptedMagic(const uint8_t* magic, const std::string& key) {
    uint8_t expected[8];
    generateEncryptedMagic(expected, key);
    return memcmp(magic, expected, 8) == 0;
}

void xorData(char* data, size_t size, size_t fileOffset, const std::string& encryptionKey) {
    if (encryptionKey.empty()) return;
    const size_t ENCRYPTED_START = 8 + SALT_SIZE + VERIFY_TAG_SIZE;
    if (fileOffset < ENCRYPTED_START) {
        size_t skip = ENCRYPTED_START - fileOffset;
        if (skip >= size) return;
        data += skip; size -= skip; fileOffset = ENCRYPTED_START;
    }
    const size_t CHUNK_SIZE = 64;
    size_t lastChunkIdx = SIZE_MAX;
    std::string lastPad;
    for (size_t i = 0; i < size; i++) {
        size_t globalPos = fileOffset + i;
        size_t chunkIdx = globalPos / CHUNK_SIZE;
        size_t byteIdx = globalPos % CHUNK_SIZE;
        if (chunkIdx != lastChunkIdx) {
            lastChunkIdx = chunkIdx;
            std::string material = encryptionKey + std::to_string(chunkIdx);
            lastPad = SHA256::hash(SHA256::hash(material) + material);
        }
        data[i] ^= lastPad[byteIdx];
    }
}

#pragma pack(push, 1)
struct Superblock {
    uint8_t encryptedMagic[8]; uint8_t salt[SALT_SIZE]; uint8_t verifyTag[VERIFY_TAG_SIZE];
    uint32_t version; uint32_t blockSize; uint32_t totalBlocks; uint32_t totalNodes;
    uint32_t freeBlocks; uint32_t freeNodes; uint32_t rootNode; uint32_t nodeBitmapBlock;
    uint32_t blockBitmapBlock; uint32_t nodeTableBlock; uint32_t dataBlockStart;
    uint64_t maxFileSize; uint32_t flags; uint8_t padding[SUPERBLOCK_SIZE - 8 - 48 - 52];
};
struct GraphNode {
    uint32_t id; uint32_t size; uint32_t dataBlockCount; uint32_t dataBlocks[DATA_BLOCKS_COUNT];
    uint32_t edgeCount; uint32_t edgeBlockCount; uint32_t edgeBlocks[EDGE_BLOCKS_COUNT];
    uint32_t refCount; int64_t created; int64_t modified; uint8_t padding[36];
};
struct LinkEntry { uint32_t targetNodeId; char name[MAX_NAME_LEN + 1]; };
#pragma pack(pop)

const int TOOLBAR_HEIGHT = 40;
const int ICON_SIZE = 64;
const int ICON_SPACING = 20;
const int ITEM_WIDTH = 90;
const int ITEM_HEIGHT = 90;
const COLORREF BG_COLOR = RGB(30, 30, 30);
const COLORREF TOOLBAR_COLOR = RGB(45, 45, 45);
const COLORREF TEXT_COLOR = RGB(220, 220, 220);
const COLORREF SELECT_COLOR = RGB(60, 100, 180);
const COLORREF FOLDER_COLOR = RGB(255, 200, 80);
const COLORREF FILE_COLOR = RGB(100, 180, 255);
const COLORREF HOVER_COLOR = RGB(50, 50, 50);

HWND g_hwnd = NULL;
HFONT g_hFont = NULL, g_hFontSmall = NULL;
bool g_mounted = false;
std::string g_imagePath;
Superblock g_superblock;
std::vector<GraphNode> g_nodes;
std::vector<uint8_t> g_nodeBitmap, g_blockBitmap;
std::fstream g_imageFile;
bool g_isEncrypted = false;
std::string g_encryptionKey;

uint32_t g_currentNode = 0;
std::vector<LinkEntry> g_currentLinks;
int g_selectedIndex = -1;
int g_hoverIndex = -1;
int g_scrollY = 0;
std::stack<uint32_t> g_history;
std::string g_currentPath = "/";

size_t getNodeBitmapOffset() { return SUPERBLOCK_SIZE; }
size_t getBlockBitmapOffset() { return getNodeBitmapOffset() + (g_superblock.totalNodes + 7) / 8; }
size_t getNodeTableOffset() {
    size_t offset = getBlockBitmapOffset() + (g_superblock.totalBlocks + 7) / 8;
    return ((offset + g_superblock.blockSize - 1) / g_superblock.blockSize) * g_superblock.blockSize;
}
size_t getDataOffset() {
    size_t offset = getNodeTableOffset() + g_superblock.totalNodes * sizeof(GraphNode);
    return ((offset + g_superblock.blockSize - 1) / g_superblock.blockSize) * g_superblock.blockSize;
}

std::vector<uint8_t> readBlock(uint32_t blockId) {
    std::vector<uint8_t> data(g_superblock.blockSize, 0);
    size_t offset = getDataOffset() + blockId * g_superblock.blockSize;
    g_imageFile.seekg(offset);
    g_imageFile.read(reinterpret_cast<char*>(data.data()), g_superblock.blockSize);
    if (g_isEncrypted) xorData(reinterpret_cast<char*>(data.data()), g_superblock.blockSize, offset, g_encryptionKey);
    return data;
}

std::vector<LinkEntry> readLinks(const GraphNode& node) {
    std::vector<LinkEntry> links;
    for (uint32_t i = 0; i < node.edgeBlockCount && i < EDGE_BLOCKS_COUNT; i++) {
        auto block = readBlock(node.edgeBlocks[i]);
        size_t offset = 0;
        while (offset + sizeof(LinkEntry) <= block.size()) {
            LinkEntry entry;
            memcpy(&entry, block.data() + offset, sizeof(LinkEntry));
            if (entry.targetNodeId != 0 || strlen(entry.name) > 0) links.push_back(entry);
            offset += sizeof(LinkEntry);
        }
    }
    return links;
}

bool openNodeFile(const std::string& path, const std::string& password = "");

void closeNodeFile() {
    if (g_imageFile.is_open()) g_imageFile.close();
    g_mounted = false; g_nodes.clear(); g_nodeBitmap.clear(); g_blockBitmap.clear();
    g_currentLinks.clear(); g_imagePath.clear(); g_encryptionKey.clear();
    while (!g_history.empty()) g_history.pop();
    g_currentPath = "/";
}

void navigateTo(uint32_t nodeId, const std::string& linkName = "") {
    if (!g_mounted) return;
    g_history.push(g_currentNode);
    g_currentNode = nodeId;
    g_currentLinks = readLinks(g_nodes[g_currentNode]);
    g_selectedIndex = -1; g_scrollY = 0;
    if (!linkName.empty()) g_currentPath += (g_currentPath == "/" ? "" : "/") + linkName;
}

void navigateBack() {
    if (!g_history.empty()) {
        g_currentNode = g_history.top();
        g_history.pop();
        g_currentLinks = readLinks(g_nodes[g_currentNode]);
        g_selectedIndex = -1; g_scrollY = 0;
        size_t pos = g_currentPath.rfind('/');
        if (pos != std::string::npos && pos > 0) g_currentPath = g_currentPath.substr(0, pos);
        else g_currentPath = "/";
    }
}

void drawFolderIcon(HDC hdc, int x, int y, int size, bool selected) {
    COLORREF color = selected ? RGB(255, 220, 100) : FOLDER_COLOR;
    HBRUSH hBrush = CreateSolidBrush(color);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 140, 40));
    SelectObject(hdc, hBrush); SelectObject(hdc, hPen);
    int tabW = size / 3, tabH = size / 6;
    POINT tab[] = {{x, y + tabH}, {x, y}, {x + tabW, y}, {x + tabW + tabH/2, y + tabH}};
    Polygon(hdc, tab, 4);
    RoundRect(hdc, x, y + tabH, x + size, y + size, 8, 8);
    DeleteObject(hBrush); DeleteObject(hPen);
}

void drawFileIcon(HDC hdc, int x, int y, int size, bool selected) {
    COLORREF color = selected ? RGB(140, 200, 255) : FILE_COLOR;
    HBRUSH hBrush = CreateSolidBrush(color);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(60, 120, 180));
    SelectObject(hdc, hBrush); SelectObject(hdc, hPen);
    int fold = size / 5;
    POINT pts[] = {{x, y}, {x + size - fold, y}, {x + size, y + fold}, {x + size, y + size}, {x, y + size}};
    Polygon(hdc, pts, 5);
    MoveToEx(hdc, x + size - fold, y, NULL);
    LineTo(hdc, x + size - fold, y + fold);
    LineTo(hdc, x + size, y + fold);
    DeleteObject(hBrush); DeleteObject(hPen);
}

int getItemAtPoint(int x, int y, RECT& clientRect) {
    if (y < TOOLBAR_HEIGHT) return -1;
    int contentWidth = clientRect.right;
    int cols = std::max(1, (contentWidth - ICON_SPACING) / (ITEM_WIDTH + ICON_SPACING));
    int ix = (x - ICON_SPACING) / (ITEM_WIDTH + ICON_SPACING);
    int iy = (y - TOOLBAR_HEIGHT + g_scrollY - ICON_SPACING) / (ITEM_HEIGHT + ICON_SPACING);
    if (ix < 0 || ix >= cols) return -1;
    int idx = iy * cols + ix;
    return (idx >= 0 && idx < (int)g_currentLinks.size()) ? idx : -1;
}

void PaintWindow(HWND hwnd, HDC hdc) {
    RECT rc; GetClientRect(hwnd, &rc);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    SelectObject(hdcMem, hbm);
    
    HBRUSH hBg = CreateSolidBrush(BG_COLOR);
    FillRect(hdcMem, &rc, hBg);
    DeleteObject(hBg);
    
    RECT rcToolbar = {0, 0, rc.right, TOOLBAR_HEIGHT};
    HBRUSH hToolbar = CreateSolidBrush(TOOLBAR_COLOR);
    FillRect(hdcMem, &rcToolbar, hToolbar);
    DeleteObject(hToolbar);
    
    SetBkMode(hdcMem, TRANSPARENT);
    SelectObject(hdcMem, g_hFont);
    
    if (!g_mounted) {
        SetTextColor(hdcMem, RGB(120, 120, 120));
        RECT rcText = rc;
        DrawTextA(hdcMem, "Drop a .node file here\nor press Ctrl+O to open", -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        SetTextColor(hdcMem, TEXT_COLOR);
        std::string pathText = " " + g_currentPath;
        TextOutA(hdcMem, 40, 10, pathText.c_str(), (int)pathText.length());
        
        if (!g_history.empty()) {
            HBRUSH hBack = CreateSolidBrush(RGB(60, 60, 60));
            RECT rcBack = {5, 8, 35, 32};
            FillRect(hdcMem, &rcBack, hBack);
            DeleteObject(hBack);
            SetTextColor(hdcMem, TEXT_COLOR);
            TextOutA(hdcMem, 12, 10, "<", 1);
        }
        
        int contentWidth = rc.right;
        int cols = std::max(1, (contentWidth - ICON_SPACING) / (ITEM_WIDTH + ICON_SPACING));
        int startX = ICON_SPACING;
        int startY = TOOLBAR_HEIGHT + ICON_SPACING - g_scrollY;
        
        SelectObject(hdcMem, g_hFontSmall);
        
        for (int i = 0; i < (int)g_currentLinks.size(); i++) {
            int col = i % cols;
            int row = i / cols;
            int x = startX + col * (ITEM_WIDTH + ICON_SPACING);
            int y = startY + row * (ITEM_HEIGHT + ICON_SPACING);
            
            if (y + ITEM_HEIGHT < TOOLBAR_HEIGHT || y > rc.bottom) continue;
            
            bool selected = (i == g_selectedIndex);
            bool hovered = (i == g_hoverIndex);
            
            if (selected || hovered) {
                HBRUSH hSel = CreateSolidBrush(selected ? SELECT_COLOR : HOVER_COLOR);
                RECT rcItem = {x - 5, y - 5, x + ITEM_WIDTH + 5, y + ITEM_HEIGHT + 5};
                FillRect(hdcMem, &rcItem, hSel);
                DeleteObject(hSel);
            }
            
            const LinkEntry& link = g_currentLinks[i];
            bool isFolder = (g_nodes[link.targetNodeId].edgeCount > 0);
            
            int iconX = x + (ITEM_WIDTH - ICON_SIZE) / 2;
            int iconY = y;
            
            if (isFolder) drawFolderIcon(hdcMem, iconX, iconY, ICON_SIZE, selected);
            else drawFileIcon(hdcMem, iconX, iconY, ICON_SIZE, selected);
            
            SetTextColor(hdcMem, TEXT_COLOR);
            RECT rcName = {x, y + ICON_SIZE + 4, x + ITEM_WIDTH, y + ITEM_HEIGHT};
            DrawTextA(hdcMem, link.name, -1, &rcName, DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
        
        if (g_currentLinks.empty()) {
            SetTextColor(hdcMem, RGB(100, 100, 100));
            SelectObject(hdcMem, g_hFont);
            RECT rcEmpty = {0, TOOLBAR_HEIGHT, rc.right, rc.bottom};
            DrawTextA(hdcMem, "This node is empty", -1, &rcEmpty, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
    DeleteDC(hdcMem);
    DeleteObject(hbm);
}

void openFileDialog(HWND hwnd) {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Node Files (*.node)\0*.node\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        closeNodeFile();
        if (openNodeFile(filename)) InvalidateRect(hwnd, NULL, TRUE);
    }
}

bool openNodeFile(const std::string& path, const std::string& password) {
    g_imageFile.open(path, std::ios::in | std::ios::binary);
    if (!g_imageFile) return false;
    g_imageFile.read(reinterpret_cast<char*>(&g_superblock), sizeof(Superblock));
    g_isEncrypted = false; g_encryptionKey.clear();
    
    bool hasSalt = false;
    for (size_t i = 0; i < SALT_SIZE; i++) if (g_superblock.salt[i] != 0) { hasSalt = true; break; }
    
    if (hasSalt) {
        g_isEncrypted = true;
        std::string pwd = password;
        if (pwd.empty()) {
            char inputBuf[256] = "";
            if (MessageBoxA(g_hwnd, "This file is encrypted.\nClick OK to enter password.", "Password Required", MB_OKCANCEL) == IDCANCEL) {
                g_imageFile.close(); return false;
            }
            AllocConsole();
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE), hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleMode(hIn, ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
            DWORD written, read;
            WriteConsoleA(hOut, "Enter password: ", 16, &written, NULL);
            ReadConsoleA(hIn, inputBuf, 255, &read, NULL);
            if (read > 0 && inputBuf[read-1] == '\n') inputBuf[read-1] = 0;
            if (read > 1 && inputBuf[read-2] == '\r') inputBuf[read-2] = 0;
            FreeConsole();
            pwd = inputBuf;
        }
        if (pwd.empty()) { MessageBoxA(NULL, "No password.", "Error", MB_ICONERROR); g_imageFile.close(); return false; }
        g_encryptionKey = deriveKey(pwd, g_superblock.salt);
        if (!verifyEncryptedMagic(g_superblock.encryptedMagic, g_encryptionKey)) {
            MessageBoxA(NULL, "Incorrect password.", "Error", MB_ICONERROR);
            g_imageFile.close(); g_encryptionKey.clear(); return false;
        }
        const size_t ENCRYPTED_START = 8 + SALT_SIZE + VERIFY_TAG_SIZE;
        xorData(reinterpret_cast<char*>(&g_superblock) + ENCRYPTED_START, sizeof(Superblock) - ENCRYPTED_START, ENCRYPTED_START, g_encryptionKey);
    } else {
        uint32_t magicData[2]; memcpy(magicData, g_superblock.encryptedMagic, 8);
        if (magicData[0] != NODE_MAGIC) { MessageBoxA(NULL, "Invalid file.", "Error", MB_ICONERROR); g_imageFile.close(); return false; }
    }
    
    g_nodeBitmap.resize((g_superblock.totalNodes + 7) / 8);
    g_imageFile.seekg(getNodeBitmapOffset());
    g_imageFile.read(reinterpret_cast<char*>(g_nodeBitmap.data()), g_nodeBitmap.size());
    if (g_isEncrypted) xorData(reinterpret_cast<char*>(g_nodeBitmap.data()), g_nodeBitmap.size(), getNodeBitmapOffset(), g_encryptionKey);
    
    g_blockBitmap.resize((g_superblock.totalBlocks + 7) / 8);
    g_imageFile.seekg(getBlockBitmapOffset());
    g_imageFile.read(reinterpret_cast<char*>(g_blockBitmap.data()), g_blockBitmap.size());
    if (g_isEncrypted) xorData(reinterpret_cast<char*>(g_blockBitmap.data()), g_blockBitmap.size(), getBlockBitmapOffset(), g_encryptionKey);
    
    g_nodes.resize(g_superblock.totalNodes);
    g_imageFile.seekg(getNodeTableOffset());
    g_imageFile.read(reinterpret_cast<char*>(g_nodes.data()), g_nodes.size() * sizeof(GraphNode));
    if (g_isEncrypted) xorData(reinterpret_cast<char*>(g_nodes.data()), g_nodes.size() * sizeof(GraphNode), getNodeTableOffset(), g_encryptionKey);
    
    g_mounted = true; g_imagePath = path; g_currentNode = 0;
    g_currentLinks = readLinks(g_nodes[0]); g_selectedIndex = -1; g_scrollY = 0;
    g_currentPath = "/";
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        g_hFontSmall = CreateFontA(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        char filename[MAX_PATH];
        DragQueryFileA(hDrop, 0, filename, MAX_PATH);
        DragFinish(hDrop);
        closeNodeFile();
        if (openNodeFile(filename)) InvalidateRect(hwnd, NULL, TRUE);
        } return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        PaintWindow(hwnd, hdc);
        EndPaint(hwnd, &ps);
        } return 0;
    case WM_MOUSEMOVE:
        if (g_mounted) {
            RECT rc; GetClientRect(hwnd, &rc);
            int idx = getItemAtPoint(LOWORD(lParam), HIWORD(lParam), rc);
            if (idx != g_hoverIndex) { g_hoverIndex = idx; InvalidateRect(hwnd, NULL, FALSE); }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (g_mounted) {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            if (y < TOOLBAR_HEIGHT && x < 40 && !g_history.empty()) {
                navigateBack();
                InvalidateRect(hwnd, NULL, TRUE);
            } else {
                RECT rc; GetClientRect(hwnd, &rc);
                int idx = getItemAtPoint(x, y, rc);
                if (idx >= 0) { g_selectedIndex = idx; InvalidateRect(hwnd, NULL, FALSE); }
            }
        }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (g_mounted && g_selectedIndex >= 0 && g_selectedIndex < (int)g_currentLinks.size()) {
            const LinkEntry& link = g_currentLinks[g_selectedIndex];
            navigateTo(link.targetNodeId, link.name);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (g_mounted) {
            g_scrollY -= GET_WHEEL_DELTA_WPARAM(wParam) / 2;
            if (g_scrollY < 0) g_scrollY = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wParam == 'O') { openFileDialog(hwnd); return 0; }
        }
        if (wParam == VK_BACK && g_mounted) { navigateBack(); InvalidateRect(hwnd, NULL, TRUE); }
        if (wParam == VK_RETURN && g_mounted && g_selectedIndex >= 0) {
            const LinkEntry& link = g_currentLinks[g_selectedIndex];
            navigateTo(link.targetNodeId, link.name);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_DESTROY:
        closeNodeFile();
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    WNDCLASSEXA wc = {sizeof(WNDCLASSEX), CS_DBLCLKS, WndProc, 0, 0, hInstance, NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, "NexploreClass", NULL};
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, "NexploreClass", "Nexplore - Node Explorer", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);
    BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    if (strlen(lpCmdLine) > 0) {
        std::string path = lpCmdLine;
        if (path.front() == '"') path = path.substr(1);
        if (path.back() == '"') path.pop_back();
        if (openNodeFile(path)) InvalidateRect(hwnd, NULL, TRUE);
    }
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}
