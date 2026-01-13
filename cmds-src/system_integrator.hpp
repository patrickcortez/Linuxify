#pragma once

#include <windows.h>
#include <string>
#include <iostream>
#include <filesystem>
#include "../registry.hpp" 

namespace fs = std::filesystem;

class SystemIntegrator {
public:
    static bool isElevated() {
        BOOL fRet = FALSE;
        HANDLE hToken = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            TOKEN_ELEVATION elevation;
            DWORD cbSize = sizeof(TOKEN_ELEVATION);
            if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
                fRet = elevation.TokenIsElevated;
            }
        }
        if (hToken) {
            CloseHandle(hToken);
        }
        return fRet;
    }

    static void enforceDeepIntegration() {
        if (!isElevated()) {
            std::cerr << "Error: Deep System Integration requires Administrator privileges." << std::endl;
            std::cerr << "Run linuxify as Administrator to nuke external shells." << std::endl;
            return;
        }

        std::string myPath = getCurrentExePath();
        
        // Strategy: "Delete" by redirecting execution (IFEO Injection)
        // This effectively kills the original binary's ability to run.
        if (redirectBinary("cmd.exe", myPath)) {
            std::cout << "[SUCCESS] cmd.exe has been neutralized." << std::endl;
        } else {
             std::cerr << "[FAILED] Could not neutralize cmd.exe." << std::endl;
        }

        if (redirectBinary("powershell.exe", myPath)) {
            std::cout << "[SUCCESS] powershell.exe has been neutralized." << std::endl;
        } else {
             std::cerr << "[FAILED] Could not neutralize powershell.exe." << std::endl;
        }
        
        if (redirectBinary("pwsh.exe", myPath)) {
             std::cout << "[SUCCESS] pwsh.exe has been neutralized." << std::endl;
        }
    }
    
    static void restoreSystemShells() {
         if (!isElevated()) {
            std::cerr << "Error: Administrator privileges required to restore shells." << std::endl;
            return;
        }
        
        removeRedirect("cmd.exe");
        removeRedirect("powershell.exe");
        removeRedirect("pwsh.exe");
        
        std::cout << "System shells restored." << std::endl;
    }

private:
    static std::string getCurrentExePath() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return std::string(buffer);
    }

    static bool redirectBinary(const std::string& targetExe, const std::string& debuggerExe) {
        // HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\target.exe
        // Value: Debbuger = "path\to\linuxify.exe"
        
        std::string keyPath = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + targetExe;
        HKEY hKey;
        
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, NULL, 
            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
            return false;
        }
        
        // The "Debugger" value causes Windows to launch our exe INSTEAD of the target
        // passing the target command line as arguments.
        if (RegSetValueExA(hKey, "Debugger", 0, REG_SZ, 
            (const BYTE*)debuggerExe.c_str(), (DWORD)(debuggerExe.size() + 1)) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return false;
        }
        
        RegCloseKey(hKey);
        return true;
    }
    
    static void removeRedirect(const std::string& targetExe) {
        std::string keyPath = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + targetExe;
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, keyPath.c_str());
    }
};
