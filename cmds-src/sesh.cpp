// sesh.cpp - Session Management Implementation
#include "sesh.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <shlobj.h>
#include "../shell_streams.hpp"

namespace fs = std::filesystem;

namespace Sesh {

    const char* MAGIC_HEADER = "LNXSES01";

    // Helper to get session directory
    fs::path getSessionDir() {
        char* homeDir = getenv("USERPROFILE");
        if (!homeDir) return "";
        return fs::path(homeDir) / ".linuxify" / "sessions";
    }

    struct SessionHeader {
        char magic[8]; // "LNXSES01"
        uint64_t timestamp;
        uint32_t envCount;
        uint32_t historyCount;
        uint32_t cwdLen;
        // Output Buffer Info
        int16_t bufferRows;
        int16_t bufferCols;
        int16_t windowRows;
        int16_t windowCols;
    };

    void init() {
         fs::path sessDir = getSessionDir();
         if (!sessDir.empty() && !fs::exists(sessDir)) {
             try {
                 fs::create_directories(sessDir);
             } catch (...) {
                 // Silent fail on init, will report on save
             }
         }
    }

    void saveSession(ShellContext& ctx, const std::string& sessionName) {
        fs::path sessDir = getSessionDir();
        if (sessDir.empty()) {
            ShellIO::serr << "Error: Could not determine home directory." << ShellIO::endl;
            return;
        }

        if (!fs::exists(sessDir)) {
             try { fs::create_directories(sessDir); } 
             catch (...) {
                 ShellIO::serr << "Error: Could not create session directory." << ShellIO::endl;
                 return;
             }
        }

        // Fix extension logic
        std::string filename = sessionName;
        if (filename.length() < 5 || filename.substr(filename.length() - 5) != ".sesh") {
            filename += ".sesh";
        }
        
        fs::path file = sessDir / filename;
        
        // 1. Capture Screen Buffer
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);
        
        int width = csbi.dwSize.X;
        int height = csbi.dwSize.Y;
        
        std::vector<CHAR_INFO> buffer(width * height);
        COORD bufferSize = { (SHORT)width, (SHORT)height };
        COORD bufferCoord = { 0, 0 };
        SMALL_RECT readRegion = { 0, 0, (SHORT)(width - 1), (SHORT)(height - 1) };
        
        if (!ReadConsoleOutputA(hConsole, buffer.data(), bufferSize, bufferCoord, &readRegion)) {
            ShellIO::serr << "Warning: Failed to capture screen buffer. Saving context only." << ShellIO::endl;
            // Continue anyway, just save empty buffer
            width = 0; height = 0;
        }

        std::ofstream out(file, std::ios::binary);
        if (!out) {
            ShellIO::serr << "Error: Could not write to file " << file.string() << ShellIO::endl;
            return;
        }

        // 2. Prepare Header
        SessionHeader header;
        memcpy(header.magic, MAGIC_HEADER, 8);
        header.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        header.envCount = (uint32_t)ctx.sessionEnv.size();
        header.historyCount = (uint32_t)ctx.commandHistory.size();
        header.cwdLen = (uint32_t)ctx.currentDir.length();
        header.bufferRows = (int16_t)height;
        header.bufferCols = (int16_t)width;
        header.windowRows = (int16_t)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
        header.windowCols = (int16_t)(csbi.srWindow.Right - csbi.srWindow.Left + 1);

        out.write((char*)&header, sizeof(header));
        
        // 3. Write CWD
        out.write(ctx.currentDir.c_str(), header.cwdLen);
        
        // 4. Write Env Vars
        for (const auto& [key, val] : ctx.sessionEnv) {
            uint32_t kLen = (uint32_t)key.length();
            uint32_t vLen = (uint32_t)val.length();
            out.write((char*)&kLen, sizeof(kLen));
            out.write(key.c_str(), kLen);
            out.write((char*)&vLen, sizeof(vLen));
            out.write(val.c_str(), vLen);
        }
        
        // 5. Write History
        for (const auto& cmd : ctx.commandHistory) {
            uint32_t len = (uint32_t)cmd.length();
            out.write((char*)&len, sizeof(len));
            out.write(cmd.c_str(), len);
        }
        
        // 6. Write Screen Buffer
        if (width > 0 && height > 0) {
            out.write((char*)buffer.data(), buffer.size() * sizeof(CHAR_INFO));
        }

        out.close();
        ShellIO::sout << "Session '" << sessionName << "' saved." << ShellIO::endl;
    }

    void loadSession(ShellContext& ctx, const std::string& sessionName) {
        fs::path sessDir = getSessionDir();
        
        // Fix extension logic
        std::string filename = sessionName;
        if (filename.length() < 5 || filename.substr(filename.length() - 5) != ".sesh") {
            filename += ".sesh";
        }
        
        fs::path file = sessDir / filename;

        if (!fs::exists(file)) {
            ShellIO::serr << "Error: Session '" << sessionName << "' not found." << ShellIO::endl;
            return;
        }

        std::ifstream in(file, std::ios::binary);
        if (!in) {
            ShellIO::serr << "Error: Could not read session file." << ShellIO::endl;
            return;
        }

        SessionHeader header;
        in.read((char*)&header, sizeof(header));
        
        if (strncmp(header.magic, MAGIC_HEADER, 8) != 0) {
            ShellIO::serr << "Error: Invalid session file format." << ShellIO::endl;
            return;
        }

        // Restore CWD
        std::vector<char> cwdBuf(header.cwdLen + 1);
        in.read(cwdBuf.data(), header.cwdLen);
        cwdBuf[header.cwdLen] = '\0';
        std::string newDir = cwdBuf.data();
        
        // Restore Env
        ctx.sessionEnv.clear();
        for (uint32_t i = 0; i < header.envCount; i++) {
            uint32_t kLen, vLen;
            in.read((char*)&kLen, sizeof(kLen));
            std::vector<char> k(kLen + 1);
            in.read(k.data(), kLen); 
            k[kLen] = '\0';
            
            in.read((char*)&vLen, sizeof(vLen));
            std::vector<char> v(vLen + 1);
            in.read(v.data(), vLen);
            v[vLen] = '\0';
            
            ctx.sessionEnv[k.data()] = v.data();
        }
        ctx.interpreter.bindVariables(ctx.sessionEnv);
        
        // Restore History
        ctx.commandHistory.clear();
        for (uint32_t i = 0; i < header.historyCount; i++) {
            uint32_t len;
            in.read((char*)&len, sizeof(len));
            std::vector<char> cmd(len + 1);
            in.read(cmd.data(), len);
            cmd[len] = '\0';
            ctx.commandHistory.push_back(cmd.data());
        }
        
        // Restore Screen
        if (header.bufferRows > 0 && header.bufferCols > 0) {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            
            // Resize buffer first
            COORD newSize = { (SHORT)header.bufferCols, (SHORT)header.bufferRows };
            SetConsoleScreenBufferSize(hConsole, newSize);
            
            // Read buffer data
            std::vector<CHAR_INFO> buffer(header.bufferCols * header.bufferRows);
            in.read((char*)buffer.data(), buffer.size() * sizeof(CHAR_INFO));
            
            // Write to console
            COORD bufferCoord = { 0, 0 };
            SMALL_RECT writeRegion = { 0, 0, (SHORT)(header.bufferCols - 1), (SHORT)(header.bufferRows - 1) };
            WriteConsoleOutputA(hConsole, buffer.data(), newSize, bufferCoord, &writeRegion);
            
            // Restore window size if possible (best effort)
             SMALL_RECT window = { 0, 0, (SHORT)(header.windowCols - 1), (SHORT)(header.windowRows - 1) };
             // Ensure window is within buffer info
             if (window.Right >= newSize.X) window.Right = newSize.X - 1;
             if (window.Bottom >= newSize.Y) window.Bottom = newSize.Y - 1;
             SetConsoleWindowInfo(hConsole, TRUE, &window);
             
             // Move cursor to bottom
             SetConsoleCursorPosition(hConsole, {0, (SHORT)(header.bufferRows - 1)});
        }
        
        // Apply Directory Change
        if (fs::exists(newDir)) {
            ctx.currentDir = newDir;
            SetCurrentDirectoryA(newDir.c_str());
        }

        ShellIO::sout << "Session '" << sessionName << "' loaded." << ShellIO::endl;
    }

    void listSessions() {
        fs::path sessDir = getSessionDir();
        if (!fs::exists(sessDir)) {
            ShellIO::serr << "No saved sessions found." << ShellIO::endl;
            return;
        }

        ShellIO::sout << "Saved Sessions:" << ShellIO::endl;
        for (const auto& entry : fs::directory_iterator(sessDir)) {
            if (entry.path().extension() == ".sesh") {
                std::string name = entry.path().stem().string();
                
                // Read timestamp
                std::ifstream in(entry.path(), std::ios::binary);
                SessionHeader header;
                if (in.read((char*)&header, sizeof(header))) {
                    time_t t = (time_t)header.timestamp;
                    char timeBuf[64];
                    ctime_s(timeBuf, sizeof(timeBuf), &t);
                    // remove newline
                    std::string timeStr = timeBuf;
                    if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();
                    
                    ShellIO::sout << "  " << name << "\t(" << timeStr << ")" << ShellIO::endl;
                } else {
                     ShellIO::sout << "  " << name << "\t(Corrupt)" << ShellIO::endl;
                }
            }
        }
    }

} // namespace Sesh
