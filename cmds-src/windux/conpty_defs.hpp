// Definitions for Windows Pseudo Console (ConPTY)
// Necessary because MinGW might not have these in headers yet.

#ifndef CONPTY_DEFS_HPP
#define CONPTY_DEFS_HPP

#include <windows.h>

// Handle to a Pseudo Console
typedef void* HPCON;

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE \
    ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif

// Function Pointers
typedef HRESULT (WINAPI *PFN_CreatePseudoConsole)(COORD size, HANDLE hInput, HANDLE hOutput, DWORD dwFlags, HPCON* phPC);
typedef HRESULT (WINAPI *PFN_ResizePseudoConsole)(HPCON hPC, COORD size);
typedef void (WINAPI *PFN_ClosePseudoConsole)(HPCON hPC);

// Initialization Helper
struct ConPTYContext {
    HPCON hPC;
    HMODULE hKernel32;
    PFN_CreatePseudoConsole CreatePseudoConsole;
    PFN_ResizePseudoConsole ResizePseudoConsole;
    PFN_ClosePseudoConsole ClosePseudoConsole;

    bool Init() {
        hKernel32 = LoadLibraryA("kernel32.dll");
        if (!hKernel32) return false;

        CreatePseudoConsole = (PFN_CreatePseudoConsole)GetProcAddress(hKernel32, "CreatePseudoConsole");
        ResizePseudoConsole = (PFN_ResizePseudoConsole)GetProcAddress(hKernel32, "ResizePseudoConsole");
        ClosePseudoConsole = (PFN_ClosePseudoConsole)GetProcAddress(hKernel32, "ClosePseudoConsole");

        return (CreatePseudoConsole && ResizePseudoConsole && ClosePseudoConsole);
    }

    void Shutdown() {
        if (hPC && ClosePseudoConsole) ClosePseudoConsole(hPC);
        if (hKernel32) FreeLibrary(hKernel32);
    }
};

#endif // CONPTY_DEFS_HPP
