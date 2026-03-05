#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>

struct FileEntity {
    double x, y;
    std::wstring name;
    std::wstring fullPath;
    bool isDirectory;
    HBITMAP iconBitmap;
    int iconW, iconH;

    FileEntity() : x(0), y(0), isDirectory(false), iconBitmap(NULL), iconW(32), iconH(32) {}

    void extractIcon(const std::wstring& fullPath) {
        SHFILEINFOW sfi = {};
        DWORD_PTR hr = SHGetFileInfoW(fullPath.c_str(),
            isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
            &sfi, sizeof(sfi),
            SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);

        if (hr && sfi.hIcon) {
            ICONINFO ii;
            if (GetIconInfo(sfi.hIcon, &ii)) {
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
                if (ii.hbmColor) {
                    BITMAP bm;
                    GetObject(ii.hbmColor, sizeof(BITMAP), &bm);
                    iconW = bm.bmWidth;
                    iconH = bm.bmHeight;
                    iconBitmap = ii.hbmColor;
                } else {
                    iconBitmap = NULL;
                }
            }
            DestroyIcon(sfi.hIcon);
        }
    }

    void cleanup() {
        if (iconBitmap) {
            DeleteObject(iconBitmap);
            iconBitmap = NULL;
        }
    }
};
