// g++ -O2 -std=c++17 -mwindows -municode main.cpp -o fps-explorer.exe -lgdi32 -lshell32 -lole32 -lmsimg32
#include <windows.h>
#include <string>
#include <cmath>
#include <shlobj.h>
#include <shellapi.h>
#include "player.hpp"
#include "raycaster.hpp"
#include "renderer.hpp"
#include "map_gen.hpp"
#include "console.hpp"
#include "window_manager.hpp"

static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;
static const wchar_t* CLASS_NAME = L"FPSExplorerClass";

static Player gPlayer;
static Raycaster gRaycaster;
static Renderer gRenderer;
static RoomMap gRoom;
static bool gKeys[256] = {};
static bool gRunning = true;
static HBITMAP gBackBuffer = NULL;
static HDC gBackDC = NULL;
static HDC gSpriteDC = NULL;
static UINT32* gPixels = nullptr;
static int gCenterX, gCenterY;
static bool gMouseCaptured = false;
static Texture gWallTex;
static Texture gDoorTex;
static GameConsole gConsole;
static WindowManager gWinMgr;

static std::wstring gNearDoorLabel;
static std::wstring gNearDoorPath;
static bool gNearDoor = false;
static std::wstring gNearFileName;
static std::wstring gNearFilePath;
static bool gNearFile = false;
static std::wstring gLinoPath;
static std::wstring gLinuxifyPath;

static bool isTextFile(const std::wstring& name) {
    std::wstring ext;
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    ext = name.substr(dot);
    for (auto& c : ext) c = towlower(c);
    return ext == L".txt" || ext == L".md" || ext == L".log" || ext == L".cfg" ||
           ext == L".ini" || ext == L".conf" || ext == L".json" || ext == L".xml" ||
           ext == L".html" || ext == L".css" || ext == L".js" || ext == L".py" ||
           ext == L".cpp" || ext == L".c" || ext == L".h" || ext == L".hpp" ||
           ext == L".java" || ext == L".sh" || ext == L".bat" || ext == L".ps1" ||
           ext == L".yaml" || ext == L".yml" || ext == L".toml" || ext == L".csv" ||
           ext == L".rs" || ext == L".go" || ext == L".rb" || ext == L".php";
}

static void openFile(const std::wstring& path, const std::wstring& name) {
    if (isTextFile(name) && !gLinoPath.empty()) {
        gWinMgr.openWindow(gLinoPath, path, name);
    } else {
        gWinMgr.openWindow(path, L"", name);
    }
}

static int findNearestFile(const std::vector<FileEntity>& entities, double posX, double posY,
                           double dirX, double dirY) {
    double bestDist = 2.5;
    int bestIdx = -1;
    for (int i = 0; i < (int)entities.size(); i++) {
        if (entities[i].isDirectory) continue;
        double dx = entities[i].x - posX;
        double dy = entities[i].y - posY;
        double dist = sqrt(dx * dx + dy * dy);
        if (dist > 2.5) continue;
        double dot = dx * dirX + dy * dirY;
        if (dot < 0.3) continue;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void loadRoom(const std::wstring& path) {
    gRoom.cleanup();
    gRoom = generateMap(path);
    gPlayer.init(gRoom.spawnX, gRoom.spawnY);
}

void enterDoor(const std::wstring& path) {
    loadRoom(path);
}

#define IDM_NEW_FILE  1001
#define IDM_NEW_FOLDER 1002
#define IDC_NAME_EDIT 2001

static wchar_t gDialogResult[MAX_PATH] = {};

static INT_PTR CALLBACK NameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hDlg, IDC_NAME_EDIT));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextW(hDlg, IDC_NAME_EDIT, gDialogResult, MAX_PATH);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            gDialogResult[0] = L'\0';
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        gDialogResult[0] = L'\0';
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void dlgAlign(BYTE*& p) {
    ULONG_PTR addr = (ULONG_PTR)p;
    p = (BYTE*)((addr + 3) & ~3);
}

static void dlgWriteWord(BYTE*& p, WORD v) { *(WORD*)p = v; p += 2; }

static void dlgWriteStr(BYTE*& p, const wchar_t* s) {
    size_t len = wcslen(s) + 1;
    memcpy(p, s, len * 2);
    p += len * 2;
}

static bool showNameInputDialog(HWND parent, const wchar_t* title) {
    gDialogResult[0] = L'\0';
    BYTE buf[2048] = {};
    BYTE* p = buf;

    DLGTEMPLATE* dlg = (DLGTEMPLATE*)p;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dlg->cdit = 4;
    dlg->cx = 220;
    dlg->cy = 70;
    p += sizeof(DLGTEMPLATE);
    dlgWriteWord(p, 0);
    dlgWriteWord(p, 0);
    dlgWriteStr(p, title);

    // Label
    dlgAlign(p);
    DLGITEMTEMPLATE* item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    item->x = 10; item->y = 8; item->cx = 200; item->cy = 10;
    item->id = 0xFFFF;
    p += sizeof(DLGITEMTEMPLATE);
    dlgWriteWord(p, 0xFFFF); dlgWriteWord(p, 0x0082);
    dlgWriteStr(p, L"Enter name:");
    dlgWriteWord(p, 0);

    // Edit
    dlgAlign(p);
    item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    item->x = 10; item->y = 22; item->cx = 200; item->cy = 14;
    item->id = IDC_NAME_EDIT;
    p += sizeof(DLGITEMTEMPLATE);
    dlgWriteWord(p, 0xFFFF); dlgWriteWord(p, 0x0081);
    dlgWriteWord(p, 0);
    dlgWriteWord(p, 0);

    // OK button
    dlgAlign(p);
    item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP;
    item->x = 60; item->y = 46; item->cx = 45; item->cy = 14;
    item->id = IDOK;
    p += sizeof(DLGITEMTEMPLATE);
    dlgWriteWord(p, 0xFFFF); dlgWriteWord(p, 0x0080);
    dlgWriteStr(p, L"OK");
    dlgWriteWord(p, 0);

    // Cancel button
    dlgAlign(p);
    item = (DLGITEMTEMPLATE*)p;
    item->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
    item->x = 115; item->y = 46; item->cx = 45; item->cy = 14;
    item->id = IDCANCEL;
    p += sizeof(DLGITEMTEMPLATE);
    dlgWriteWord(p, 0xFFFF); dlgWriteWord(p, 0x0080);
    dlgWriteStr(p, L"Cancel");
    dlgWriteWord(p, 0);

    INT_PTR res = DialogBoxIndirectW(GetModuleHandleW(NULL), (DLGTEMPLATE*)buf, parent, NameDlgProc);
    return res == IDOK && gDialogResult[0] != L'\0';
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        gRunning = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam < 256) gKeys[wParam] = true;
        if (wParam == VK_OEM_3) {
            if (!gLinuxifyPath.empty()) {
                gConsole.toggle(gLinuxifyPath, gRoom.currentPath);
            }
            return 0;
        }
        if (wParam == VK_TAB) {
            if (gWinMgr.desktopMode) {
                gWinMgr.desktopMode = false;
                if (!gMouseCaptured) {
                    SetCapture(hwnd);
                    ShowCursor(FALSE);
                    POINT center = {SCREEN_W / 2, SCREEN_H / 2};
                    ClientToScreen(hwnd, &center);
                    SetCursorPos(center.x, center.y);
                    gMouseCaptured = true;
                }
            } else {
                bool anyOpen = false;
                for (auto* w : gWinMgr.windows) if (!w->closed) anyOpen = true;
                if (anyOpen) {
                    gWinMgr.desktopMode = true;
                    if (gMouseCaptured) {
                        ReleaseCapture();
                        ShowCursor(TRUE);
                        gMouseCaptured = false;
                    }
                }
            }
            return 0;
        }
        if (gConsole.isOpen) {
            if (wParam == VK_ESCAPE) {
                gConsole.isOpen = false;
            } else {
                gConsole.handleKey((int)wParam);
            }
            return 0;
        }
        if (gWinMgr.desktopMode) {
            gWinMgr.handleKey((int)wParam);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            gRunning = false;
            PostQuitMessage(0);
        }
        if (wParam == VK_BACK) {
            std::wstring parent = std::filesystem::path(gRoom.currentPath).parent_path().wstring();
            if (!parent.empty() && parent != gRoom.currentPath) {
                loadRoom(parent);
            }
        }
        if (wParam == 'E' && gNearDoor && !gNearDoorPath.empty()) {
            enterDoor(gNearDoorPath);
        }
        if (wParam == 'E' && !gNearDoor && gNearFile && !gNearFilePath.empty()) {
            openFile(gNearFilePath, gNearFileName);
        }
        return 0;
    case WM_CHAR:
        if (gConsole.isOpen) {
            gConsole.handleChar((wchar_t)wParam);
            return 0;
        }
        if (gWinMgr.desktopMode) {
            gWinMgr.handleChar((wchar_t)wParam);
            return 0;
        }
        return 0;
    case WM_KEYUP:
        if (wParam < 256) gKeys[wParam] = false;
        return 0;
    case WM_MOUSEMOVE:
        if (gWinMgr.desktopMode) {
            gWinMgr.handleMouseMove(LOWORD(lParam), HIWORD(lParam));
        } else if (gMouseCaptured) {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            int dx = mx - (SCREEN_W / 2);
            if (dx != 0) {
                gPlayer.rotate(dx * 0.003);
            }
            POINT center = {SCREEN_W / 2, SCREEN_H / 2};
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (gWinMgr.desktopMode) {
            gWinMgr.handleMouseDown(LOWORD(lParam), HIWORD(lParam));
        } else if (!gMouseCaptured) {
            SetCapture(hwnd);
            ShowCursor(FALSE);
            POINT center = {SCREEN_W / 2, SCREEN_H / 2};
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
            gMouseCaptured = true;
        }
        return 0;
    case WM_LBUTTONUP:
        if (gWinMgr.desktopMode) {
            gWinMgr.handleMouseUp();
        }
        return 0;
    case WM_RBUTTONDOWN: {
        if (gMouseCaptured) {
            ReleaseCapture();
            ShowCursor(TRUE);
            gMouseCaptured = false;
        }
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_NEW_FILE, L"New File");
        AppendMenuW(hMenu, MF_STRING, IDM_NEW_FOLDER, L"New Folder");
        POINT pt;
        GetCursorPos(&pt);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
        if (cmd == IDM_NEW_FILE || cmd == IDM_NEW_FOLDER) {
            const wchar_t* dlgTitle = (cmd == IDM_NEW_FILE) ? L"New File" : L"New Folder";
            if (showNameInputDialog(hwnd, dlgTitle)) {
                std::wstring newPath = gRoom.currentPath + L"\\" + gDialogResult;
                if (cmd == IDM_NEW_FOLDER) {
                    std::filesystem::create_directory(newPath);
                } else {
                    HANDLE hf = CreateFileW(newPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hf != INVALID_HANDLE_VALUE) CloseHandle(hf);
                }
                loadRoom(gRoom.currentPath);
            }
        }
        return 0;
    }
    case WM_KILLFOCUS:
        if (gMouseCaptured) {
            ReleaseCapture();
            ShowCursor(TRUE);
            gMouseCaptured = false;
        }
        return 0;
    case WM_SETCURSOR:
        if (gMouseCaptured) {
            SetCursor(NULL);
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    RECT wr = {0, 0, SCREEN_W, SCREEN_H};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"FPS Filesystem Explorer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    HDC screenDC = GetDC(hwnd);
    gBackDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SCREEN_W;
    bmi.bmiHeader.biHeight = -SCREEN_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    gBackBuffer = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, (void**)&gPixels, NULL, 0);
    SelectObject(gBackDC, gBackBuffer);
    gSpriteDC = CreateCompatibleDC(screenDC);
    ReleaseDC(hwnd, screenDC);

    gRaycaster.init(SCREEN_W, SCREEN_H);
    gRenderer.init(SCREEN_W, SCREEN_H);
    gConsole.init();
    gWinMgr.init();

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = std::filesystem::path(exePath).parent_path().wstring();

    std::wstring assetsDir = exeDir + L"\\assets";
    if (!std::filesystem::exists(assetsDir)) {
        assetsDir = std::filesystem::path(exeDir).parent_path().wstring() + L"\\assets";
    }
    if (!std::filesystem::exists(assetsDir)) {
        MessageBoxW(hwnd, L"Could not find assets folder.\nPlace the assets folder next to fps-explorer.exe or its parent directory.",
                    L"FPS Explorer - Missing Assets", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wstring wallPath = assetsDir + L"\\Wall.bmp";
    std::wstring doorPath = assetsDir + L"\\doors.bmp";
    if (!loadBmpTexture(wallPath, gWallTex)) {
        MessageBoxW(hwnd, (L"Failed to load Wall.bmp from:\n" + wallPath).c_str(),
                    L"FPS Explorer - Missing Asset", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!loadBmpTexture(doorPath, gDoorTex)) {
        MessageBoxW(hwnd, (L"Failed to load doors.bmp from:\n" + doorPath).c_str(),
                    L"FPS Explorer - Missing Asset", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wstring linoCandidate = std::filesystem::path(exeDir).parent_path().wstring() + L"\\lino.exe";
    if (std::filesystem::exists(linoCandidate)) {
        gLinoPath = linoCandidate;
    }
    std::wstring linuxifyCandidate = std::filesystem::path(exeDir).parent_path().wstring() + L"\\linuxify.exe";
    if (std::filesystem::exists(linuxifyCandidate)) {
        gLinuxifyPath = linuxifyCandidate;
    }

    wchar_t homePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, homePath) == S_OK) {
        loadRoom(std::wstring(homePath));
    } else {
        loadRoom(L"C:\\");
    }

    LARGE_INTEGER freq, lastTime, curTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    MSG msg;
    while (gRunning) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                gRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!gRunning) break;

        QueryPerformanceCounter(&curTime);
        float dt = (float)(curTime.QuadPart - lastTime.QuadPart) / (float)freq.QuadPart;
        lastTime = curTime;
        if (dt > 0.05f) dt = 0.05f;

        if (!gConsole.isOpen && !gWinMgr.desktopMode) {
            gPlayer.update(dt, gKeys, gRoom.grid);
        }

        gConsole.update();
        gWinMgr.update();

        gNearDoor = false;
        gNearDoorLabel.clear();
        gNearDoorPath.clear();
        int doorCheck = gPlayer.checkDoor(gRoom.grid);
        if (doorCheck >= 0) {
            int dx = doorCheck / 10000;
            int dy = doorCheck % 10000;
            for (auto& d : gRoom.doors) {
                if (d.gridX == dx && d.gridY == dy) {
                    gNearDoor = true;
                    gNearDoorPath = d.targetPath;
                    gNearDoorLabel = std::filesystem::path(d.targetPath).filename().wstring();
                    break;
                }
            }
        }

        gNearFile = false;
        gNearFileName.clear();
        gNearFilePath.clear();
        int nearIdx = findNearestFile(gRoom.entities, gPlayer.posX, gPlayer.posY,
                                      gPlayer.dirX, gPlayer.dirY);
        if (nearIdx >= 0) {
            gNearFile = true;
            gNearFileName = gRoom.entities[nearIdx].name;
            gNearFilePath = gRoom.entities[nearIdx].fullPath;
        }

        gRaycaster.render(gPixels, gPlayer.posX, gPlayer.posY,
                          gPlayer.dirX, gPlayer.dirY,
                          gPlayer.planeX, gPlayer.planeY, gRoom.grid,
                          &gWallTex, &gDoorTex);

        gRenderer.drawSprites(gBackDC, gSpriteDC, gRoom.entities,
                              gPlayer.posX, gPlayer.posY,
                              gPlayer.dirX, gPlayer.dirY,
                              gPlayer.planeX, gPlayer.planeY,
                              gRaycaster.zBuffer);

        gRenderer.drawHUD(gBackDC, gRoom.currentPath, gNearDoor, gNearDoorLabel,
                          gNearFile, gNearFileName);

        gConsole.render(gBackDC, SCREEN_W, SCREEN_H);
        gWinMgr.render(gBackDC, SCREEN_W, SCREEN_H);

        HDC hdc = GetDC(hwnd);
        BitBlt(hdc, 0, 0, SCREEN_W, SCREEN_H, gBackDC, 0, 0, SRCCOPY);
        ReleaseDC(hwnd, hdc);
    }

    gConsole.cleanup();
    gWinMgr.cleanup();
    gRoom.cleanup();
    gRenderer.cleanup();
    if (gBackBuffer) DeleteObject(gBackBuffer);
    if (gBackDC) DeleteDC(gBackDC);
    if (gSpriteDC) DeleteDC(gSpriteDC);

    return 0;
}
