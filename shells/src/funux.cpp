// Compile: g++ -std=c++17 -static -o Funux.exe funux.cpp -lshlwapi
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <conio.h>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <random>
#include <map>
#include <unordered_map>
#include <functional>
#include <sys/utime.h>
#include <thread>
#include <chrono>
#include <deque>
#include <mutex>

#include "process.hpp"
#include "scheduler.hpp"

namespace fs = std::filesystem;

namespace ANSI {
    inline std::string moveTo(int row, int col) {
        return "\033[" + std::to_string(row) + ";" + std::to_string(col) + "H";
    }
    inline std::string fg256(int code) {
        return "\033[38;5;" + std::to_string(code) + "m";
    }
    inline std::string bg256(int code) {
        return "\033[48;5;" + std::to_string(code) + "m";
    }
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* CLEAR_SCREEN = "\033[2J";
    const char* CURSOR_HOME = "\033[H";
    const char* CURSOR_HIDE = "\033[?25l";
    const char* CURSOR_SHOW = "\033[?25h";
    const char* ALT_BUFFER_ON = "\033[?1049h";
    const char* ALT_BUFFER_OFF = "\033[?1049l";
}

const std::vector<std::string> ASCII_GALLERY = {
R"(    (o_o)   "Why do today what you can put off until tomorrow?")",
R"(   (^_^)   "Success! The dice rolled in your favor!")",
R"(   (T_T)   "Another command lost to the void...")",
R"(   (>_<)   "Error 418: I'm a teapot, not a shell!")",
R"(   (-_-)   "Contemplating the meaning of /dev/null...")",
R"(   (\/)><  "Funux has booted! Let the gambling begin!")",
R"(   [o_O]   "Reality.exe has stopped responding...")",
R"(   {^o^}   "Welcome, brave user! May the odds be ever in your favor!")",
R"(   |>_>|   "Looking for motivation... still looking...")",
R"(   <@_@>   "Hypnotized by the blinking cursor...")"
};

const std::map<std::string, std::string> MAN_PAGES = {
    {"ls", "ls [opts] [path] - List directory contents. -a show hidden."},
    {"cd", "cd [dir] - Change directory."},
    {"nano", "nano [file] - Edit file with lazy-loading editor."},
    {"lun", R"(lun - Lundb custom command manager
  lun add <cmd> <path-to-exe> [-d <desc>] - Add custom command
  lun del <cmd> - Remove custom command
  lun status - List all custom commands)"},
    {"help", "help - Display available commands."}
};

struct LundbEntry {
    std::string command;
    std::string exeName;
    std::string description;
};

class Lundb {
private:
    std::string lundbPath;
    std::string manifestPath;
    std::vector<LundbEntry> entries;
    
    void loadManifest() {
        entries.clear();
        std::ifstream f(manifestPath);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            LundbEntry entry;
            size_t p1 = line.find('|');
            size_t p2 = line.find('|', p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            entry.command = line.substr(0, p1);
            entry.exeName = line.substr(p1 + 1, p2 - p1 - 1);
            entry.description = line.substr(p2 + 1);
            entries.push_back(entry);
        }
    }
    
    void saveManifest() {
        std::ofstream f(manifestPath, std::ios::trunc);
        for (const auto& e : entries) {
            f << e.command << "|" << e.exeName << "|" << e.description << "\n";
        }
    }
    
public:
    void init(const std::string& exeDir) {
        lundbPath = exeDir + "\\Lundb";
        manifestPath = lundbPath + "\\manifest.ldb";
        if (!fs::exists(lundbPath)) {
            fs::create_directory(lundbPath);
        }
        loadManifest();
    }
    
    bool addCommand(const std::string& cmd, const std::string& exePath, const std::string& desc, std::string& error) {
        for (const auto& e : entries) {
            if (e.command == cmd) {
                error = "Command '" + cmd + "' already exists.";
                return false;
            }
        }
        if (!fs::exists(exePath)) {
            error = "File not found: " + exePath;
            return false;
        }
        std::string exeName = fs::path(exePath).filename().string();
        std::string destPath = lundbPath + "\\" + exeName;
        try {
            fs::copy_file(exePath, destPath, fs::copy_options::overwrite_existing);
        } catch (const std::exception& ex) {
            error = std::string("Copy failed: ") + ex.what();
            return false;
        }
        LundbEntry entry;
        entry.command = cmd;
        entry.exeName = exeName;
        entry.description = desc.empty() ? "No description" : desc;
        entries.push_back(entry);
        saveManifest();
        return true;
    }
    
    bool delCommand(const std::string& cmd, std::string& error) {
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->command == cmd) {
                std::string exeFile = lundbPath + "\\" + it->exeName;
                try { if (fs::exists(exeFile)) fs::remove(exeFile); } catch (...) {}
                entries.erase(it);
                saveManifest();
                return true;
            }
        }
        error = "Command '" + cmd + "' not found.";
        return false;
    }
    
    std::vector<LundbEntry> getStatus() const { return entries; }
    
    bool findCommand(const std::string& cmd, std::string& exePath) const {
        for (const auto& e : entries) {
            if (e.command == cmd) {
                exePath = lundbPath + "\\" + e.exeName;
                return true;
            }
        }
        return false;
    }
};

class TrashSystem {
private:
    std::string trashPath;
    std::string manifestPath;
    struct TrashEntry {
        std::string originalPath;
        std::string trashName;
        std::string deleteTime;
        uint64_t fileSize;
    };
    std::vector<TrashEntry> entries;
    
    void loadManifest() {
        entries.clear();
        std::ifstream f(manifestPath);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            TrashEntry e;
            size_t p1 = line.find('|'), p2 = line.find('|', p1+1), p3 = line.find('|', p2+1);
            if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) continue;
            e.originalPath = line.substr(0, p1);
            e.trashName = line.substr(p1+1, p2-p1-1);
            e.deleteTime = line.substr(p2+1, p3-p2-1);
            e.fileSize = std::stoull(line.substr(p3+1));
            entries.push_back(e);
        }
    }
    
    void saveManifest() {
        std::ofstream f(manifestPath, std::ios::trunc);
        for (const auto& e : entries) {
            f << e.originalPath << "|" << e.trashName << "|" << e.deleteTime << "|" << e.fileSize << "\n";
        }
    }
    
    std::string generateTrashName(const std::string& origName) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(ms) + "_" + origName;
    }
    
    std::string getCurrentTime() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

public:
    void init(const std::string& exeDir) {
        trashPath = exeDir + "\\Trash";
        manifestPath = trashPath + "\\trash.manifest";
        if (!fs::exists(trashPath)) fs::create_directory(trashPath);
        loadManifest();
    }
    
    bool moveToTrash(const std::string& filePath, std::string& error) {
        if (!fs::exists(filePath)) { error = "File not found: " + filePath; return false; }
        try {
            std::string fname = fs::path(filePath).filename().string();
            std::string tname = generateTrashName(fname);
            std::string dest = trashPath + "\\" + tname;
            uint64_t fsize = fs::is_directory(filePath) ? 0 : fs::file_size(filePath);
            fs::rename(filePath, dest);
            TrashEntry e;
            e.originalPath = filePath;
            e.trashName = tname;
            e.deleteTime = getCurrentTime();
            e.fileSize = fsize;
            entries.push_back(e);
            saveManifest();
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }
    
    bool restore(int index, std::string& error) {
        if (index < 0 || index >= (int)entries.size()) { error = "Invalid index"; return false; }
        try {
            std::string src = trashPath + "\\" + entries[index].trashName;
            std::string dest = entries[index].originalPath;
            if (fs::exists(dest)) { error = "Destination exists"; return false; }
            fs::rename(src, dest);
            entries.erase(entries.begin() + index);
            saveManifest();
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }
    
    bool permanentDelete(int index, std::string& error) {
        if (index < 0 || index >= (int)entries.size()) { error = "Invalid index"; return false; }
        try {
            std::string path = trashPath + "\\" + entries[index].trashName;
            if (fs::is_directory(path)) fs::remove_all(path);
            else fs::remove(path);
            entries.erase(entries.begin() + index);
            saveManifest();
            return true;
        } catch (const std::exception& ex) {
            error = ex.what();
            return false;
        }
    }
    
    void emptyTrash() {
        for (const auto& e : entries) {
            std::string path = trashPath + "\\" + e.trashName;
            try { if (fs::is_directory(path)) fs::remove_all(path); else fs::remove(path); } catch (...) {}
        }
        entries.clear();
        saveManifest();
    }
    
    std::vector<TrashEntry> list() const { return entries; }
    
    uint64_t getTotalSize() const {
        uint64_t total = 0;
        for (const auto& e : entries) total += e.fileSize;
        return total;
    }
    
    int getCount() const { return (int)entries.size(); }
};

class FileHasher {
public:
    static uint32_t crc32(const std::string& data) {
        static uint32_t table[256];
        static bool initialized = false;
        if (!initialized) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
                table[i] = c;
            }
            initialized = true;
        }
        uint32_t crc = 0xFFFFFFFF;
        for (unsigned char c : data) crc = table[(crc ^ c) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }
    
    static std::string crc32File(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return "ERROR";
        std::ostringstream oss;
        oss << f.rdbuf();
        uint32_t crc = crc32(oss.str());
        std::ostringstream hex;
        hex << std::hex << std::setfill('0') << std::setw(8) << crc;
        return hex.str();
    }
    
    static uint64_t fnv1a(const std::string& data) {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : data) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }
    
    static std::string md5Simple(const std::string& data) {
        uint64_t h1 = fnv1a(data);
        uint64_t h2 = fnv1a(data + "salt");
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << h1 << std::setw(16) << h2;
        return oss.str();
    }
};

class SystemInfo {
public:
    static std::string getCPUInfo() {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        std::ostringstream oss;
        oss << "Processors: " << si.dwNumberOfProcessors;
        switch (si.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: oss << " (x64)"; break;
            case PROCESSOR_ARCHITECTURE_INTEL: oss << " (x86)"; break;
            case PROCESSOR_ARCHITECTURE_ARM: oss << " (ARM)"; break;
            case PROCESSOR_ARCHITECTURE_ARM64: oss << " (ARM64)"; break;
            default: oss << " (Unknown)"; break;
        }
        return oss.str();
    }
    
    static std::string getMemoryInfo() {
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        std::ostringstream oss;
        oss << "Total: " << (mem.ullTotalPhys / 1024 / 1024) << " MB, ";
        oss << "Available: " << (mem.ullAvailPhys / 1024 / 1024) << " MB, ";
        oss << "Used: " << mem.dwMemoryLoad << "%";
        return oss.str();
    }
    
    static std::string getDiskInfo(const std::string& drive) {
        ULARGE_INTEGER free, total, totalFree;
        if (GetDiskFreeSpaceExA(drive.c_str(), &free, &total, &totalFree)) {
            std::ostringstream oss;
            oss << drive << " Total: " << (total.QuadPart / 1024 / 1024 / 1024) << " GB, ";
            oss << "Free: " << (totalFree.QuadPart / 1024 / 1024 / 1024) << " GB";
            return oss.str();
        }
        return "Unable to get disk info";
    }
    
    static std::string getUptime() {
        DWORD ms = GetTickCount();
        DWORD sec = ms / 1000;
        DWORD min = sec / 60;
        DWORD hr = min / 60;
        DWORD days = hr / 24;
        std::ostringstream oss;
        oss << days << "d " << (hr % 24) << "h " << (min % 60) << "m " << (sec % 60) << "s";
        return oss.str();
    }
    
    static std::vector<std::string> getEnvVars() {
        std::vector<std::string> vars;
        char* env = GetEnvironmentStringsA();
        if (env) {
            for (char* p = env; *p; p += strlen(p) + 1) {
                vars.push_back(p);
            }
            FreeEnvironmentStringsA(env);
        }
        return vars;
    }
    
    static std::string getOSVersion() {
        std::ostringstream oss;
        oss << "Windows Build ";
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buffer[256];
            DWORD size = sizeof(buffer);
            if (RegQueryValueExA(hKey, "CurrentBuild", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
                oss << buffer;
            }
            RegCloseKey(hKey);
        }
        return oss.str();
    }
};

class CompressionUtil {
public:
    static std::string rleEncode(const std::string& data) {
        if (data.empty()) return "";
        std::ostringstream oss;
        char current = data[0];
        int count = 1;
        for (size_t i = 1; i < data.size(); i++) {
            if (data[i] == current && count < 255) {
                count++;
            } else {
                oss << (char)count << current;
                current = data[i];
                count = 1;
            }
        }
        oss << (char)count << current;
        return oss.str();
    }
    
    static std::string rleDecode(const std::string& data) {
        std::ostringstream oss;
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
            int count = (unsigned char)data[i];
            char ch = data[i + 1];
            for (int j = 0; j < count; j++) oss << ch;
        }
        return oss.str();
    }
    
    static bool compressFile(const std::string& src, const std::string& dest) {
        std::ifstream in(src, std::ios::binary);
        if (!in) return false;
        std::ostringstream oss;
        oss << in.rdbuf();
        std::string compressed = rleEncode(oss.str());
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        out << "RLE1" << compressed;
        return true;
    }
    
    static bool decompressFile(const std::string& src, const std::string& dest) {
        std::ifstream in(src, std::ios::binary);
        if (!in) return false;
        char header[4];
        in.read(header, 4);
        if (std::string(header, 4) != "RLE1") return false;
        std::ostringstream oss;
        oss << in.rdbuf();
        std::string decompressed = rleDecode(oss.str());
        std::ofstream out(dest, std::ios::binary);
        if (!out) return false;
        out << decompressed;
        return true;
    }
};

class EnvironmentManager {
private:
    std::map<std::string, std::string> localEnv;
public:
    void set(const std::string& name, const std::string& value) { localEnv[name] = value; }
    std::string get(const std::string& name) const {
        auto it = localEnv.find(name);
        if (it != localEnv.end()) return it->second;
        char* val = getenv(name.c_str());
        return val ? val : "";
    }
    void unset(const std::string& name) { localEnv.erase(name); }
    std::map<std::string, std::string> getAll() const { return localEnv; }
    std::string expand(const std::string& str) const {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find('$', pos)) != std::string::npos) {
            size_t end = pos + 1;
            while (end < result.size() && (isalnum(result[end]) || result[end] == '_')) end++;
            std::string varName = result.substr(pos + 1, end - pos - 1);
            std::string value = get(varName);
            result = result.substr(0, pos) + value + result.substr(end);
            pos += value.size();
        }
        return result;
    }
};

class FileWatcher {
private:
    std::map<std::string, std::filesystem::file_time_type> watchedFiles;
public:
    void watch(const std::string& path) {
        if (fs::exists(path)) {
            watchedFiles[path] = fs::last_write_time(path);
        }
    }
    void unwatch(const std::string& path) { watchedFiles.erase(path); }
    std::vector<std::string> checkChanges() {
        std::vector<std::string> changed;
        for (auto& kv : watchedFiles) {
            if (fs::exists(kv.first)) {
                auto newTime = fs::last_write_time(kv.first);
                if (newTime != kv.second) {
                    changed.push_back(kv.first);
                    kv.second = newTime;
                }
            }
        }
        return changed;
    }
    int getWatchCount() const { return (int)watchedFiles.size(); }
};

class BatchProcessor {
public:
    struct BatchResult {
        int total;
        int success;
        int failed;
        std::vector<std::string> errors;
    };
    
    static BatchResult copyBatch(const std::vector<std::string>& sources, const std::string& destDir) {
        BatchResult result = {0, 0, 0, {}};
        for (const auto& src : sources) {
            result.total++;
            try {
                std::string dest = destDir + "\\" + fs::path(src).filename().string();
                fs::copy(src, dest, fs::copy_options::overwrite_existing | fs::copy_options::recursive);
                result.success++;
            } catch (const std::exception& e) {
                result.failed++;
                result.errors.push_back(src + ": " + e.what());
            }
        }
        return result;
    }
    
    static BatchResult renameBatch(const std::vector<std::pair<std::string, std::string>>& pairs) {
        BatchResult result = {0, 0, 0, {}};
        for (const auto& p : pairs) {
            result.total++;
            try {
                fs::rename(p.first, p.second);
                result.success++;
            } catch (const std::exception& e) {
                result.failed++;
                result.errors.push_back(p.first + ": " + e.what());
            }
        }
        return result;
    }
    
    static BatchResult deleteBatch(const std::vector<std::string>& paths) {
        BatchResult result = {0, 0, 0, {}};
        for (const auto& p : paths) {
            result.total++;
            try {
                if (fs::is_directory(p)) fs::remove_all(p);
                else fs::remove(p);
                result.success++;
            } catch (const std::exception& e) {
                result.failed++;
                result.errors.push_back(p + ": " + e.what());
            }
        }
        return result;
    }
};

class TextProcessor {
public:
    static std::string toUpper(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::toupper);
        return r;
    }
    
    static std::string toLower(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return r;
    }
    
    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    }
    
    static std::string replace(const std::string& s, const std::string& from, const std::string& to) {
        std::string result = s;
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
        return result;
    }
    
    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> tokens;
        std::stringstream ss(s);
        std::string token;
        while (std::getline(ss, token, delim)) tokens.push_back(token);
        return tokens;
    }
    
    static std::string join(const std::vector<std::string>& v, const std::string& delim) {
        std::string result;
        for (size_t i = 0; i < v.size(); i++) {
            if (i > 0) result += delim;
            result += v[i];
        }
        return result;
    }
    
    static int countLines(const std::string& s) {
        return (int)std::count(s.begin(), s.end(), '\n') + 1;
    }
    
    static int countWords(const std::string& s) {
        std::istringstream iss(s);
        int count = 0;
        std::string word;
        while (iss >> word) count++;
        return count;
    }
    
    static std::string reverse(const std::string& s) {
        return std::string(s.rbegin(), s.rend());
    }
};

class DiskAnalyzer {
public:
    struct DirStats {
        uint64_t totalSize;
        int fileCount;
        int dirCount;
        std::string largestFile;
        uint64_t largestFileSize;
    };
    
    static DirStats analyze(const std::string& path) {
        DirStats stats = {0, 0, 0, "", 0};
        try {
            for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
                if (fs::is_directory(entry)) {
                    stats.dirCount++;
                } else if (fs::is_regular_file(entry)) {
                    stats.fileCount++;
                    uint64_t size = fs::file_size(entry);
                    stats.totalSize += size;
                    if (size > stats.largestFileSize) {
                        stats.largestFileSize = size;
                        stats.largestFile = entry.path().string();
                    }
                }
            }
        } catch (...) {}
        return stats;
    }
    
    static std::vector<std::pair<std::string, uint64_t>> findLargeFiles(const std::string& path, uint64_t minSize) {
        std::vector<std::pair<std::string, uint64_t>> results;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
                if (fs::is_regular_file(entry)) {
                    uint64_t size = fs::file_size(entry);
                    if (size >= minSize) {
                        results.push_back({entry.path().string(), size});
                    }
                }
            }
        } catch (...) {}
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return results;
    }
    
    static std::vector<std::string> findDuplicates(const std::string& path) {
        std::map<uint64_t, std::vector<std::string>> sizeMap;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
                if (fs::is_regular_file(entry)) {
                    uint64_t size = fs::file_size(entry);
                    sizeMap[size].push_back(entry.path().string());
                }
            }
        } catch (...) {}
        std::vector<std::string> duplicates;
        for (const auto& kv : sizeMap) {
            if (kv.second.size() > 1) {
                std::map<std::string, std::vector<std::string>> hashMap;
                for (const auto& f : kv.second) {
                    std::string hash = FileHasher::crc32File(f);
                    hashMap[hash].push_back(f);
                }
                for (const auto& hkv : hashMap) {
                    if (hkv.second.size() > 1) {
                        for (const auto& f : hkv.second) duplicates.push_back(f);
                    }
                }
            }
        }
        return duplicates;
    }
};

class Clipboard {
public:
    static bool setText(const std::string& text) {
        if (!OpenClipboard(NULL)) return false;
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (!hMem) { CloseClipboard(); return false; }
        memcpy(GlobalLock(hMem), text.c_str(), text.size() + 1);
        GlobalUnlock(hMem);
        SetClipboardData(CF_TEXT, hMem);
        CloseClipboard();
        return true;
    }
    
    static std::string getText() {
        if (!OpenClipboard(NULL)) return "";
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (!hData) { CloseClipboard(); return ""; }
        char* pszText = static_cast<char*>(GlobalLock(hData));
        std::string text = pszText ? pszText : "";
        GlobalUnlock(hData);
        CloseClipboard();
        return text;
    }
};

class ProcessManager {
public:
    struct ProcessInfo {
        DWORD pid;
        std::string name;
        uint64_t memoryUsage;
        DWORD parentPid;
    };
    
    static std::vector<ProcessInfo> getProcessList() {
        std::vector<ProcessInfo> procs;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return procs;
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                ProcessInfo pi;
                pi.pid = pe.th32ProcessID;
                pi.name = pe.szExeFile;
                pi.parentPid = pe.th32ParentProcessID;
                pi.memoryUsage = 0;
                HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                        pi.memoryUsage = pmc.WorkingSetSize;
                    }
                    CloseHandle(hProc);
                }
                procs.push_back(pi);
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
        return procs;
    }
    
    static bool killProcess(DWORD pid) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!hProc) return false;
        bool result = TerminateProcess(hProc, 1);
        CloseHandle(hProc);
        return result;
    }
    
    static ProcessInfo findByName(const std::string& name) {
        auto procs = getProcessList();
        for (const auto& p : procs) {
            if (p.name == name) return p;
        }
        return {0, "", 0, 0};
    }
};

class HistoryManager {
private:
    std::deque<std::string> history;
    int maxSize;
    std::string historyFile;
public:
    HistoryManager(int max = 1000) : maxSize(max) {}
    
    void init(const std::string& exeDir) {
        historyFile = exeDir + "\\history.txt";
        std::ifstream f(historyFile);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) history.push_back(line);
        }
    }
    
    void add(const std::string& cmd) {
        if (!cmd.empty() && (history.empty() || history.back() != cmd)) {
            history.push_back(cmd);
            if ((int)history.size() > maxSize) history.pop_front();
        }
    }
    
    void save() {
        std::ofstream f(historyFile, std::ios::trunc);
        for (const auto& h : history) f << h << "\n";
    }
    
    std::string get(int index) const {
        if (index < 0 || index >= (int)history.size()) return "";
        return history[history.size() - 1 - index];
    }
    
    std::vector<std::string> search(const std::string& pattern) const {
        std::vector<std::string> results;
        for (const auto& h : history) {
            if (h.find(pattern) != std::string::npos) results.push_back(h);
        }
        return results;
    }
    
    int size() const { return (int)history.size(); }
    void clear() { history.clear(); }
};

class AliasManager {
private:
    std::map<std::string, std::string> aliases;
    std::string aliasFile;
public:
    void init(const std::string& exeDir) {
        aliasFile = exeDir + "\\aliases.txt";
        std::ifstream f(aliasFile);
        std::string line;
        while (std::getline(f, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                aliases[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }
    
    void save() {
        std::ofstream f(aliasFile, std::ios::trunc);
        for (const auto& kv : aliases) f << kv.first << "=" << kv.second << "\n";
    }
    
    void set(const std::string& name, const std::string& cmd) { aliases[name] = cmd; save(); }
    void remove(const std::string& name) { aliases.erase(name); save(); }
    std::string resolve(const std::string& name) const {
        auto it = aliases.find(name);
        return it != aliases.end() ? it->second : "";
    }
    std::map<std::string, std::string> getAll() const { return aliases; }
};

class BookmarkManager {
private:
    std::map<std::string, std::string> bookmarks;
    std::string bookmarkFile;
public:
    void init(const std::string& exeDir) {
        bookmarkFile = exeDir + "\\bookmarks.txt";
        std::ifstream f(bookmarkFile);
        std::string line;
        while (std::getline(f, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                bookmarks[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }
    
    void save() {
        std::ofstream f(bookmarkFile, std::ios::trunc);
        for (const auto& kv : bookmarks) f << kv.first << "=" << kv.second << "\n";
    }
    
    void add(const std::string& name, const std::string& path) { bookmarks[name] = path; save(); }
    void remove(const std::string& name) { bookmarks.erase(name); save(); }
    std::string get(const std::string& name) const {
        auto it = bookmarks.find(name);
        return it != bookmarks.end() ? it->second : "";
    }
    std::map<std::string, std::string> getAll() const { return bookmarks; }
};

class Base64 {
private:
    static const std::string chars;
public:
    static std::string encode(const std::string& data) {
        std::string result;
        int val = 0, valb = -6;
        for (unsigned char c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                result.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (result.size() % 4) result.push_back('=');
        return result;
    }
    
    static std::string decode(const std::string& data) {
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[chars[i]] = i;
        std::string result;
        int val = 0, valb = -8;
        for (unsigned char c : data) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                result.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }
};
const std::string Base64::chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

template<typename T>
class ObjectPool {
private:
    std::vector<T*> pool;
    std::vector<T*> inUse;
    size_t blockSize;
    std::mutex poolMutex;
public:
    ObjectPool(size_t initialSize = 16, size_t block = 16) : blockSize(block) {
        for (size_t i = 0; i < initialSize; i++) {
            pool.push_back(new T());
        }
    }
    
    ~ObjectPool() {
        for (auto* p : pool) delete p;
        for (auto* p : inUse) delete p;
    }
    
    T* acquire() {
        std::lock_guard<std::mutex> lock(poolMutex);
        if (pool.empty()) {
            for (size_t i = 0; i < blockSize; i++) {
                pool.push_back(new T());
            }
        }
        T* obj = pool.back();
        pool.pop_back();
        inUse.push_back(obj);
        return obj;
    }
    
    void release(T* obj) {
        std::lock_guard<std::mutex> lock(poolMutex);
        auto it = std::find(inUse.begin(), inUse.end(), obj);
        if (it != inUse.end()) {
            inUse.erase(it);
            pool.push_back(obj);
        }
    }
    
    size_t available() const { return pool.size(); }
    size_t active() const { return inUse.size(); }
};

class MemoryPool {
private:
    std::vector<void*> allocations;
    size_t totalAllocated = 0;
    size_t peakUsage = 0;
    std::mutex memMutex;
public:
    void* alloc(size_t size) {
        std::lock_guard<std::mutex> lock(memMutex);
        void* ptr = malloc(size);
        if (ptr) {
            allocations.push_back(ptr);
            totalAllocated += size;
            if (totalAllocated > peakUsage) peakUsage = totalAllocated;
        }
        return ptr;
    }
    
    void free(void* ptr) {
        std::lock_guard<std::mutex> lock(memMutex);
        auto it = std::find(allocations.begin(), allocations.end(), ptr);
        if (it != allocations.end()) {
            allocations.erase(it);
            ::free(ptr);
        }
    }
    
    void freeAll() {
        std::lock_guard<std::mutex> lock(memMutex);
        for (void* p : allocations) ::free(p);
        allocations.clear();
        totalAllocated = 0;
    }
    
    size_t getActiveCount() const { return allocations.size(); }
    size_t getTotalAllocated() const { return totalAllocated; }
    size_t getPeakUsage() const { return peakUsage; }
};

class GarbageCollector {
private:
    std::vector<std::string> tempFiles;
    std::vector<std::string> tempDirs;
    std::deque<std::pair<std::string, std::chrono::steady_clock::time_point>> scheduledDeletes;
    size_t collectedBytes = 0;
    int collectionCount = 0;
    std::mutex gcMutex;
public:
    void trackTempFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(gcMutex);
        tempFiles.push_back(path);
    }
    
    void trackTempDir(const std::string& path) {
        std::lock_guard<std::mutex> lock(gcMutex);
        tempDirs.push_back(path);
    }
    
    void scheduleDelete(const std::string& path, int delayMs) {
        std::lock_guard<std::mutex> lock(gcMutex);
        auto deleteTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
        scheduledDeletes.push_back({path, deleteTime});
    }
    
    int collect() {
        std::lock_guard<std::mutex> lock(gcMutex);
        int collected = 0;
        
        for (const auto& f : tempFiles) {
            try {
                if (fs::exists(f)) {
                    collectedBytes += fs::file_size(f);
                    fs::remove(f);
                    collected++;
                }
            } catch (...) {}
        }
        tempFiles.clear();
        
        for (const auto& d : tempDirs) {
            try {
                if (fs::exists(d)) {
                    for (const auto& e : fs::recursive_directory_iterator(d, fs::directory_options::skip_permission_denied)) {
                        if (fs::is_regular_file(e)) collectedBytes += fs::file_size(e);
                    }
                    fs::remove_all(d);
                    collected++;
                }
            } catch (...) {}
        }
        tempDirs.clear();
        
        auto now = std::chrono::steady_clock::now();
        while (!scheduledDeletes.empty() && scheduledDeletes.front().second <= now) {
            try {
                std::string path = scheduledDeletes.front().first;
                if (fs::exists(path)) {
                    if (fs::is_directory(path)) fs::remove_all(path);
                    else fs::remove(path);
                    collected++;
                }
            } catch (...) {}
            scheduledDeletes.pop_front();
        }
        
        collectionCount++;
        return collected;
    }
    
    size_t getCollectedBytes() const { return collectedBytes; }
    int getCollectionCount() const { return collectionCount; }
    int getPendingDeletes() const { return (int)scheduledDeletes.size(); }
};

class VariableTable {
private:
    std::map<std::string, std::string> variables;
    std::map<std::string, std::function<std::string()>> dynamicVars;
    std::vector<std::string> positionalArgs;
    int lastExitCode = 0;
    int lastPid = 0;
    int shellPid = 0;
    std::string shellName;
    std::string currentDir;
    std::string homeDir;
public:
    void init() {
        shellPid = GetCurrentProcessId();
        shellName = "funux";
        char* home = getenv("USERPROFILE");
        homeDir = home ? home : "";
        char buf[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buf);
        currentDir = buf;
        
        dynamicVars["?"] = [this]() { return std::to_string(lastExitCode); };
        dynamicVars["!"] = [this]() { return std::to_string(lastPid); };
        dynamicVars["$"] = [this]() { return std::to_string(shellPid); };
        dynamicVars["0"] = [this]() { return shellName; };
        dynamicVars["#"] = [this]() { return std::to_string(positionalArgs.size()); };
        dynamicVars["*"] = [this]() { 
            std::string result;
            for (size_t i = 0; i < positionalArgs.size(); i++) {
                if (i > 0) result += " ";
                result += positionalArgs[i];
            }
            return result;
        };
        dynamicVars["@"] = dynamicVars["*"];
        dynamicVars["PWD"] = [this]() { return currentDir; };
        dynamicVars["OLDPWD"] = [this]() { return variables.count("OLDPWD") ? variables["OLDPWD"] : ""; };
        dynamicVars["HOME"] = [this]() { return homeDir; };
        dynamicVars["USER"] = []() { char* u = getenv("USERNAME"); return u ? u : ""; };
        dynamicVars["HOSTNAME"] = []() { char n[256]; DWORD s = sizeof(n); GetComputerNameA(n, &s); return std::string(n); };
        dynamicVars["RANDOM"] = []() { return std::to_string(rand() % 32768); };
        dynamicVars["SECONDS"] = []() { 
            static auto start = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
        };
        dynamicVars["LINENO"] = []() { return "1"; };
        dynamicVars["SHLVL"] = []() { return "1"; };
        dynamicVars["SHELL"] = [this]() { return shellName; };
        dynamicVars["EPOCH"] = []() { return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); };
    }
    
    void set(const std::string& name, const std::string& value) { variables[name] = value; }
    
    std::string get(const std::string& name) const {
        if (name.size() == 1 && isdigit(name[0])) {
            int idx = name[0] - '0';
            if (idx == 0) return shellName;
            if (idx <= (int)positionalArgs.size()) return positionalArgs[idx - 1];
            return "";
        }
        
        auto dit = dynamicVars.find(name);
        if (dit != dynamicVars.end()) return dit->second();
        
        auto it = variables.find(name);
        if (it != variables.end()) return it->second;
        
        char* env = getenv(name.c_str());
        return env ? env : "";
    }
    
    void unset(const std::string& name) { variables.erase(name); }
    
    void setExitCode(int code) { lastExitCode = code; }
    void setLastPid(int pid) { lastPid = pid; }
    void setCurrentDir(const std::string& dir) { currentDir = dir; }
    void setPositionalArgs(const std::vector<std::string>& args) { positionalArgs = args; }
    
    std::string expand(const std::string& input) const {
        std::string result = input;
        size_t pos = 0;
        
        while ((pos = result.find('$', pos)) != std::string::npos) {
            if (pos + 1 >= result.size()) break;
            
            if (result[pos + 1] == '{') {
                size_t end = result.find('}', pos + 2);
                if (end != std::string::npos) {
                    std::string varExpr = result.substr(pos + 2, end - pos - 2);
                    std::string defaultVal;
                    size_t colonPos = varExpr.find(":-");
                    std::string varName = varExpr;
                    if (colonPos != std::string::npos) {
                        varName = varExpr.substr(0, colonPos);
                        defaultVal = varExpr.substr(colonPos + 2);
                    }
                    std::string value = get(varName);
                    if (value.empty() && !defaultVal.empty()) value = defaultVal;
                    result = result.substr(0, pos) + value + result.substr(end + 1);
                    pos += value.size();
                    continue;
                }
            }
            
            if (result[pos + 1] == '(' && pos + 2 < result.size() && result[pos + 2] == '(') {
                size_t end = result.find("))", pos + 3);
                if (end != std::string::npos) {
                    pos = end + 2;
                    continue;
                }
            }
            
            size_t end = pos + 1;
            if (strchr("?!$#*@0123456789", result[end])) {
                std::string varName(1, result[end]);
                std::string value = get(varName);
                result = result.substr(0, pos) + value + result.substr(end + 1);
                pos += value.size();
                continue;
            }
            
            while (end < result.size() && (isalnum(result[end]) || result[end] == '_')) end++;
            if (end > pos + 1) {
                std::string varName = result.substr(pos + 1, end - pos - 1);
                std::string value = get(varName);
                result = result.substr(0, pos) + value + result.substr(end);
                pos += value.size();
            } else {
                pos++;
            }
        }
        
        return result;
    }
    
    std::map<std::string, std::string> getAll() const { return variables; }
};

class JobControl {
public:
    struct Job {
        int id;
        DWORD pid;
        std::string command;
        bool running;
        bool background;
        std::chrono::steady_clock::time_point startTime;
    };
private:
    std::vector<Job> jobs;
    int nextJobId = 1;
    std::mutex jobMutex;
public:
    int addJob(DWORD pid, const std::string& cmd, bool background) {
        std::lock_guard<std::mutex> lock(jobMutex);
        Job j;
        j.id = nextJobId++;
        j.pid = pid;
        j.command = cmd;
        j.running = true;
        j.background = background;
        j.startTime = std::chrono::steady_clock::now();
        jobs.push_back(j);
        return j.id;
    }
    
    void updateJobStatus() {
        std::lock_guard<std::mutex> lock(jobMutex);
        for (auto& j : jobs) {
            if (j.running) {
                HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, j.pid);
                if (hProc) {
                    DWORD result = WaitForSingleObject(hProc, 0);
                    if (result == WAIT_OBJECT_0) j.running = false;
                    CloseHandle(hProc);
                } else {
                    j.running = false;
                }
            }
        }
    }
    
    void removeFinished() {
        std::lock_guard<std::mutex> lock(jobMutex);
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const Job& j) { return !j.running; }), jobs.end());
    }
    
    bool bringToForeground(int jobId) {
        std::lock_guard<std::mutex> lock(jobMutex);
        for (auto& j : jobs) {
            if (j.id == jobId && j.running) {
                HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, j.pid);
                if (hProc) {
                    WaitForSingleObject(hProc, INFINITE);
                    CloseHandle(hProc);
                    j.running = false;
                    j.background = false;
                    return true;
                }
            }
        }
        return false;
    }
    
    std::vector<Job> getJobs() const { return jobs; }
    int getActiveJobCount() const { return (int)std::count_if(jobs.begin(), jobs.end(), [](const Job& j) { return j.running; }); }
};

class StringBuffer {
private:
    std::vector<char> buffer;
    size_t capacity;
    size_t writePos = 0;
    std::mutex bufMutex;
public:
    StringBuffer(size_t cap = 65536) : capacity(cap) { buffer.resize(capacity); }
    
    void write(const std::string& data) {
        std::lock_guard<std::mutex> lock(bufMutex);
        size_t toWrite = std::min(data.size(), capacity - writePos);
        std::copy(data.begin(), data.begin() + toWrite, buffer.begin() + writePos);
        writePos += toWrite;
        if (writePos >= capacity) writePos = 0;
    }
    
    std::string read(size_t count) {
        std::lock_guard<std::mutex> lock(bufMutex);
        size_t toRead = std::min(count, writePos);
        std::string result(buffer.begin(), buffer.begin() + toRead);
        return result;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(bufMutex);
        writePos = 0;
    }
    
    size_t size() const { return writePos; }
    size_t getCapacity() const { return capacity; }
};

class CommandSubstitution {
public:
    static std::string substitute(const std::string& input, std::function<std::string(const std::string&)> executor) {
        std::string result = input;
        
        size_t pos = 0;
        while ((pos = result.find("$(", pos)) != std::string::npos) {
            int depth = 1;
            size_t end = pos + 2;
            while (end < result.size() && depth > 0) {
                if (result[end] == '(') depth++;
                else if (result[end] == ')') depth--;
                end++;
            }
            if (depth == 0) {
                std::string cmd = result.substr(pos + 2, end - pos - 3);
                std::string output = executor(cmd);
                while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
                    output.pop_back();
                }
                result = result.substr(0, pos) + output + result.substr(end);
                pos += output.size();
            } else {
                pos++;
            }
        }
        
        pos = 0;
        while ((pos = result.find('`', pos)) != std::string::npos) {
            size_t end = result.find('`', pos + 1);
            if (end != std::string::npos) {
                std::string cmd = result.substr(pos + 1, end - pos - 1);
                std::string output = executor(cmd);
                while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
                    output.pop_back();
                }
                result = result.substr(0, pos) + output + result.substr(end + 1);
                pos += output.size();
            } else {
                pos++;
            }
        }
        
        return result;
    }
};

class ArithmeticExpansion {
public:
    static std::string evaluate(const std::string& expr) {
        std::string clean;
        for (char c : expr) if (!isspace(c)) clean += c;
        
        try {
            std::vector<double> nums;
            std::vector<char> ops;
            size_t i = 0;
            
            while (i < clean.size()) {
                if (isdigit(clean[i]) || (clean[i] == '-' && nums.empty())) {
                    size_t start = i;
                    if (clean[i] == '-') i++;
                    while (i < clean.size() && (isdigit(clean[i]) || clean[i] == '.')) i++;
                    nums.push_back(std::stod(clean.substr(start, i - start)));
                } else if (clean[i] == '+' || clean[i] == '-' || clean[i] == '*' || clean[i] == '/' || clean[i] == '%') {
                    ops.push_back(clean[i++]);
                } else {
                    i++;
                }
            }
            
            while (!ops.empty() && nums.size() > 1) {
                double b = nums.back(); nums.pop_back();
                double a = nums.back(); nums.pop_back();
                char op = ops.back(); ops.pop_back();
                double result = 0;
                switch (op) {
                    case '+': result = a + b; break;
                    case '-': result = a - b; break;
                    case '*': result = a * b; break;
                    case '/': result = b != 0 ? a / b : 0; break;
                    case '%': result = (int)a % (int)b; break;
                }
                nums.push_back(result);
            }
            
            if (!nums.empty()) {
                double val = nums[0];
                if (val == (int)val) return std::to_string((int)val);
                return std::to_string(val);
            }
        } catch (...) {}
        return "0";
    }
    
    static std::string expand(const std::string& input) {
        std::string result = input;
        size_t pos = 0;
        
        while ((pos = result.find("$((", pos)) != std::string::npos) {
            size_t end = result.find("))", pos + 3);
            if (end != std::string::npos) {
                std::string expr = result.substr(pos + 3, end - pos - 3);
                std::string value = evaluate(expr);
                result = result.substr(0, pos) + value + result.substr(end + 2);
                pos += value.size();
            } else {
                pos++;
            }
        }
        return result;
    }
};

class LazyNano {
private:
    std::string filename;
    std::fstream file;
    std::map<int, std::string> lineCache;
    std::vector<std::streampos> lineOffsets;
    int totalLines = 0;
    int viewportStart = 0;
    int viewportHeight = 20;
    int cursorX = 0, cursorY = 0;
    bool modified = false;
    bool isNewFile = false;
    std::vector<std::string> newFileBuffer;
    static constexpr int CACHE_SIZE = 50;
    
    void indexFile() {
        lineOffsets.clear();
        file.clear();
        file.seekg(0);
        std::streampos pos = 0;
        std::string line;
        while (std::getline(file, line)) {
            lineOffsets.push_back(pos);
            pos = file.tellg();
        }
        totalLines = (int)lineOffsets.size();
        if (totalLines == 0) totalLines = 1;
        file.clear();
    }
    
    std::string getLine(int lineNum) {
        if (isNewFile) {
            if (lineNum < (int)newFileBuffer.size()) return newFileBuffer[lineNum];
            return "";
        }
        if (lineNum < 0 || lineNum >= (int)lineOffsets.size()) return "";
        auto it = lineCache.find(lineNum);
        if (it != lineCache.end()) return it->second;
        file.clear();
        file.seekg(lineOffsets[lineNum]);
        std::string line;
        std::getline(file, line);
        if ((int)lineCache.size() >= CACHE_SIZE) {
            int toRemove = -1;
            for (auto& kv : lineCache) {
                if (kv.first < viewportStart || kv.first > viewportStart + viewportHeight + 10) {
                    toRemove = kv.first;
                    break;
                }
            }
            if (toRemove != -1) lineCache.erase(toRemove);
        }
        lineCache[lineNum] = line;
        return line;
    }
    
    void setLine(int lineNum, const std::string& content) {
        if (isNewFile) {
            while ((int)newFileBuffer.size() <= lineNum) newFileBuffer.push_back("");
            newFileBuffer[lineNum] = content;
        } else {
            lineCache[lineNum] = content;
        }
        modified = true;
    }
    
    void render(int termWidth, int termHeight) {
        viewportHeight = termHeight - 6;
        std::cout << ANSI::CURSOR_HOME << ANSI::bg256(232);
        
        std::cout << ANSI::fg256(34) << "\xE2\x95\x94";
        for (int i = 0; i < termWidth - 2; i++) std::cout << "\xE2\x95\x90";
        std::cout << "\xE2\x95\x97\n";
        
        std::string title = " FUNUX NANO - " + filename + (modified ? " [*]" : "") + " ";
        int pad = (termWidth - 2 - (int)title.size()) / 2;
        std::cout << "\xE2\x95\x91" << ANSI::fg256(46);
        for (int i = 0; i < pad; i++) std::cout << " ";
        std::cout << title;
        for (int i = 0; i < termWidth - 2 - pad - (int)title.size(); i++) std::cout << " ";
        std::cout << ANSI::fg256(34) << "\xE2\x95\x91\n";
        
        std::cout << "\xE2\x95\xA0";
        for (int i = 0; i < termWidth - 2; i++) std::cout << "\xE2\x95\x90";
        std::cout << "\xE2\x95\xA3\n";
        
        int maxLines = isNewFile ? (int)newFileBuffer.size() : totalLines;
        for (int i = 0; i < viewportHeight; i++) {
            int lineNum = viewportStart + i;
            std::cout << "\xE2\x95\x91" << ANSI::fg256(240);
            std::string lineNumStr = (lineNum < maxLines) ? std::to_string(lineNum + 1) : "~";
            while (lineNumStr.size() < 4) lineNumStr = " " + lineNumStr;
            std::cout << lineNumStr << " " << ANSI::fg256(34) << "\xE2\x94\x82" << ANSI::fg256(46);
            
            std::string content = (lineNum < maxLines) ? getLine(lineNum) : "";
            int maxContent = termWidth - 10;
            if ((int)content.size() > maxContent) content = content.substr(0, maxContent);
            
            if (lineNum == cursorY) {
                for (int c = 0; c < (int)content.size(); c++) {
                    if (c == cursorX) std::cout << ANSI::bg256(22) << content[c] << ANSI::bg256(232);
                    else std::cout << content[c];
                }
                if (cursorX >= (int)content.size()) std::cout << ANSI::bg256(22) << " " << ANSI::bg256(232);
                for (int c = (int)content.size() + (cursorX >= (int)content.size() ? 1 : 0); c < maxContent; c++) std::cout << " ";
            } else {
                std::cout << content;
                for (int c = (int)content.size(); c < maxContent; c++) std::cout << " ";
            }
            std::cout << ANSI::fg256(34) << "\xE2\x95\x91\n";
        }
        
        std::cout << "\xE2\x95\xA0";
        for (int i = 0; i < termWidth - 2; i++) std::cout << "\xE2\x95\x90";
        std::cout << "\xE2\x95\xA3\n";
        
        std::cout << "\xE2\x95\x91" << ANSI::fg256(46);
        std::string status = " ^S Save  ^X Exit  Line " + std::to_string(cursorY + 1) + "/" + std::to_string(maxLines) + "  Col " + std::to_string(cursorX + 1) + " ";
        std::cout << status;
        for (int i = (int)status.size(); i < termWidth - 2; i++) std::cout << " ";
        std::cout << ANSI::fg256(34) << "\xE2\x95\x91\n";
        
        std::cout << "\xE2\x95\x9A";
        for (int i = 0; i < termWidth - 2; i++) std::cout << "\xE2\x95\x90";
        std::cout << "\xE2\x95\x9D" << ANSI::RESET;
        std::cout.flush();
    }
    
    void saveFile() {
        std::ofstream out(filename, std::ios::trunc);
        if (!out) return;
        if (isNewFile) {
            for (const auto& line : newFileBuffer) out << line << "\n";
        } else {
            for (int i = 0; i < totalLines; i++) out << getLine(i) << "\n";
        }
        out.close();
        modified = false;
    }
    
public:
    void run(const std::string& path) {
        filename = path;
        if (fs::exists(filename)) {
            file.open(filename, std::ios::in | std::ios::out);
            if (file.is_open()) { indexFile(); isNewFile = false; }
        } else {
            isNewFile = true;
            newFileBuffer.push_back("");
            totalLines = 1;
        }
        
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD oldMode;
        GetConsoleMode(hIn, &oldMode);
        SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
        
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int termWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int termHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        
        std::cout << ANSI::ALT_BUFFER_ON << ANSI::CURSOR_HIDE << ANSI::bg256(232) << ANSI::CLEAR_SCREEN;
        
        bool editing = true;
        while (editing) {
            render(termWidth, termHeight);
            int ch = _getch();
            if (ch == 0 || ch == 224) {
                int ext = _getch();
                switch (ext) {
                    case 72: if (cursorY > 0) { cursorY--; if (cursorY < viewportStart) viewportStart = cursorY; std::string l = getLine(cursorY); if (cursorX > (int)l.size()) cursorX = (int)l.size(); } break;
                    case 80: { int mx = isNewFile ? (int)newFileBuffer.size() : totalLines; if (cursorY < mx - 1) { cursorY++; if (cursorY >= viewportStart + viewportHeight) viewportStart++; std::string l = getLine(cursorY); if (cursorX > (int)l.size()) cursorX = (int)l.size(); } } break;
                    case 75: if (cursorX > 0) cursorX--; break;
                    case 77: { std::string l = getLine(cursorY); if (cursorX < (int)l.size()) cursorX++; } break;
                    case 73: viewportStart -= viewportHeight; if (viewportStart < 0) viewportStart = 0; cursorY = viewportStart; break;
                    case 81: { int mx = isNewFile ? (int)newFileBuffer.size() : totalLines; viewportStart += viewportHeight; if (viewportStart > mx - viewportHeight) viewportStart = std::max(0, mx - viewportHeight); cursorY = viewportStart; } break;
                }
            } else if (ch == 19) { saveFile(); }
            else if (ch == 24) { editing = false; }
            else if (ch == 13) {
                std::string cur = getLine(cursorY);
                if (cursorX > (int)cur.size()) cursorX = (int)cur.size();
                std::string before = cur.substr(0, cursorX);
                std::string after = (cursorX < (int)cur.size()) ? cur.substr(cursorX) : "";
                setLine(cursorY, before);
                if (isNewFile) { newFileBuffer.insert(newFileBuffer.begin() + cursorY + 1, after); totalLines = (int)newFileBuffer.size(); }
                else { for (int i = totalLines; i > cursorY + 1; i--) lineCache[i] = getLine(i - 1); lineCache[cursorY + 1] = after; totalLines++; }
                cursorY++; cursorX = 0;
                if (cursorY >= viewportStart + viewportHeight) viewportStart++;
            } else if (ch == 8) {
                std::string l = getLine(cursorY);
                if (cursorX > 0) { l.erase(cursorX - 1, 1); setLine(cursorY, l); cursorX--; }
                else if (cursorY > 0) {
                    std::string prev = getLine(cursorY - 1);
                    cursorX = (int)prev.size();
                    setLine(cursorY - 1, prev + l);
                    if (isNewFile) { newFileBuffer.erase(newFileBuffer.begin() + cursorY); totalLines = (int)newFileBuffer.size(); }
                    else { for (int i = cursorY; i < totalLines - 1; i++) lineCache[i] = getLine(i + 1); lineCache.erase(totalLines - 1); totalLines--; }
                    cursorY--;
                    if (cursorY < viewportStart) viewportStart = cursorY;
                }
            } else if (ch >= 32 && ch < 127) {
                std::string l = getLine(cursorY);
                if (cursorX > (int)l.size()) cursorX = (int)l.size();
                l.insert(cursorX, 1, (char)ch);
                setLine(cursorY, l);
                cursorX++;
            }
        }
        std::cout << ANSI::ALT_BUFFER_OFF << ANSI::CURSOR_SHOW << ANSI::RESET;
        SetConsoleMode(hIn, oldMode);
        if (file.is_open()) file.close();
    }
};

struct DesktopIcon {
    std::string name;
    std::string path;
    bool isFolder;
    bool isShortcut;
    bool isExecutable;
    int gridX;
    int gridY;
};

struct FileEntry {
    std::string name;
    std::string path;
    bool isFolder;
    uint64_t size;
    std::string modTime;
};

class FileExplorer {
private:
    std::string currentPath;
    std::vector<FileEntry> entries;
    int selectedIndex = 0;
    int scrollOffset = 0;
    int visibleRows = 20;
    int termWidth = 80, termHeight = 24;
    bool shouldExit = false;
    std::string fileToEdit;
    bool needsFullRedraw = true;
    
    void scanDirectory() {
        entries.clear();
        
        if (currentPath.size() > 3) {
            FileEntry parent;
            parent.name = "..";
            parent.path = fs::path(currentPath).parent_path().string();
            parent.isFolder = true;
            parent.size = 0;
            parent.modTime = "";
            entries.push_back(parent);
        }
        
        try {
            std::vector<FileEntry> folders, files;
            for (const auto& entry : fs::directory_iterator(currentPath, fs::directory_options::skip_permission_denied)) {
                FileEntry fe;
                fe.path = entry.path().string();
                fe.name = entry.path().filename().string();
                fe.isFolder = fs::is_directory(entry);
                
                try {
                    if (!fe.isFolder) fe.size = fs::file_size(entry);
                    else fe.size = 0;
                    
                    auto ftime = fs::last_write_time(entry);
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
                    std::ostringstream oss;
                    oss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M");
                    fe.modTime = oss.str();
                } catch (...) {
                    fe.size = 0;
                    fe.modTime = "";
                }
                
                if (fe.isFolder) folders.push_back(fe);
                else files.push_back(fe);
            }
            
            std::sort(folders.begin(), folders.end(), [](const FileEntry& a, const FileEntry& b) {
                return a.name < b.name;
            });
            std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
                return a.name < b.name;
            });
            
            for (const auto& f : folders) entries.push_back(f);
            for (const auto& f : files) entries.push_back(f);
        } catch (...) {}
        
        selectedIndex = 0;
        scrollOffset = 0;
        needsFullRedraw = true;
    }
    
    std::string formatSize(uint64_t size) {
        if (size < 1024) return std::to_string(size) + " B";
        if (size < 1024 * 1024) return std::to_string(size / 1024) + " KB";
        if (size < 1024 * 1024 * 1024) return std::to_string(size / 1024 / 1024) + " MB";
        return std::to_string(size / 1024 / 1024 / 1024) + " GB";
    }
    
    void draw() {
        if (needsFullRedraw) {
            std::cout << ANSI::bg256(17) << ANSI::CLEAR_SCREEN << ANSI::CURSOR_HOME;
            
            std::cout << ANSI::bg256(18) << ANSI::fg256(255);
            for (int i = 0; i < termWidth; i++) std::cout << " ";
            std::cout << ANSI::moveTo(1, 2) << ANSI::fg256(220) << "File Explorer" << ANSI::fg256(240) << " - ";
            
            std::string displayPath = currentPath;
            if ((int)displayPath.size() > termWidth - 30) {
                displayPath = "..." + displayPath.substr(displayPath.size() - (termWidth - 33));
            }
            std::cout << ANSI::fg256(250) << displayPath;
            
            std::cout << ANSI::moveTo(2, 1) << ANSI::bg256(236) << ANSI::fg256(250);
            std::cout << " Name";
            for (int i = 5; i < termWidth - 30; i++) std::cout << " ";
            std::cout << "Size        Modified        ";
            
            needsFullRedraw = false;
        }
        
        visibleRows = termHeight - 5;
        
        for (int i = 0; i < visibleRows; i++) {
            int idx = scrollOffset + i;
            std::cout << ANSI::moveTo(3 + i, 1);
            
            if (idx < (int)entries.size()) {
                const FileEntry& fe = entries[idx];
                bool selected = (idx == selectedIndex);
                
                std::cout << (selected ? ANSI::bg256(24) : ANSI::bg256(17));
                std::cout << (selected ? ANSI::fg256(255) : ANSI::fg256(250));
                
                std::string icon = fe.isFolder ? " 📁 " : " 📄 ";
                if (fe.name == "..") icon = " ⬆️ ";
                std::cout << icon;
                
                std::string name = fe.name;
                int maxNameLen = termWidth - 35;
                if ((int)name.size() > maxNameLen) name = name.substr(0, maxNameLen - 3) + "...";
                std::cout << name;
                for (int j = (int)name.size() + 4; j < termWidth - 30; j++) std::cout << " ";
                
                if (fe.isFolder) {
                    std::cout << ANSI::fg256(240) << "<DIR>       ";
                } else {
                    std::string sizeStr = formatSize(fe.size);
                    std::cout << ANSI::fg256(45) << sizeStr;
                    for (int j = (int)sizeStr.size(); j < 12; j++) std::cout << " ";
                }
                
                std::cout << ANSI::fg256(240) << fe.modTime;
                for (int j = (int)fe.modTime.size(); j < 16; j++) std::cout << " ";
            } else {
                std::cout << ANSI::bg256(17);
                for (int j = 0; j < termWidth; j++) std::cout << " ";
            }
        }
        
        std::cout << ANSI::moveTo(termHeight, 1) << ANSI::bg256(235) << ANSI::fg256(240);
        for (int i = 0; i < termWidth; i++) std::cout << " ";
        std::cout << ANSI::moveTo(termHeight, 2);
        std::cout << ANSI::fg256(250) << "↑↓: Select | Enter: Open | Ctrl+N: New | Backspace: Up | Esc: Back to Desktop";
        
        std::cout << ANSI::moveTo(termHeight, termWidth - 15);
        std::cout << ANSI::fg256(46) << entries.size() << " items";
        
        std::cout.flush();
    }
    
    void createNewItem() {
        std::cout << ANSI::moveTo(termHeight - 1, 1) << ANSI::bg256(236) << ANSI::fg256(255);
        for (int i = 0; i < termWidth; i++) std::cout << " ";
        std::cout << ANSI::moveTo(termHeight - 1, 2) << "Create (F)ile or (D)irectory? ";
        std::cout.flush();
        
        int choice = _getch();
        if (choice != 'f' && choice != 'F' && choice != 'd' && choice != 'D') {
            needsFullRedraw = true;
            return;
        }
        bool isDir = (choice == 'd' || choice == 'D');
        
        std::cout << ANSI::moveTo(termHeight - 1, 1);
        for (int i = 0; i < termWidth; i++) std::cout << " ";
        std::cout << ANSI::moveTo(termHeight - 1, 2) << "Name: ";
        std::cout << ANSI::CURSOR_SHOW;
        std::cout.flush();
        
        std::string name;
        while (true) {
            int ch = _getch();
            if (ch == 27) { needsFullRedraw = true; std::cout << ANSI::CURSOR_HIDE; return; }
            if (ch == 13) break;
            if (ch == 8 && !name.empty()) { name.pop_back(); std::cout << "\b \b"; }
            else if (ch >= 32 && ch < 127 && name.size() < 50) {
                name += (char)ch;
                std::cout << (char)ch;
            }
            std::cout.flush();
        }
        std::cout << ANSI::CURSOR_HIDE;
        
        if (!name.empty()) {
            std::string fullPath = currentPath + "\\" + name;
            try {
                if (isDir) {
                    fs::create_directory(fullPath);
                } else {
                    std::ofstream f(fullPath);
                    f.close();
                }
                scanDirectory();
            } catch (...) {}
        }
        needsFullRedraw = true;
    }
    
    void updateTermSize() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int newWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int newHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (newWidth != termWidth || newHeight != termHeight) {
            termWidth = newWidth;
            termHeight = newHeight;
            needsFullRedraw = true;
        }
    }
    
public:
    bool hasFileToEdit() const { return !fileToEdit.empty(); }
    std::string getFileToEdit() { std::string f = fileToEdit; fileToEdit.clear(); return f; }
    bool shouldExitExplorer() const { return shouldExit; }
    
    void run(const std::string& startPath = "") {
        if (startPath.empty()) {
            char* userProfile = getenv("USERPROFILE");
            currentPath = userProfile ? userProfile : "C:\\";
        } else {
            currentPath = startPath;
        }
        
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD oldMode;
        GetConsoleMode(hIn, &oldMode);
        SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
        
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode;
        GetConsoleMode(hOut, &outMode);
        SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        
        std::cout << ANSI::CURSOR_HIDE;
        
        updateTermSize();
        scanDirectory();
        shouldExit = false;
        fileToEdit.clear();
        
        while (!shouldExit && fileToEdit.empty()) {
            updateTermSize();
            draw();
            
            if (_kbhit()) {
                int ch = _getch();
                
                if (ch == 27) {
                    shouldExit = true;
                } else if (ch == 14) {
                    createNewItem();
                } else if (ch == 8) {
                    if (currentPath.size() > 3) {
                        currentPath = fs::path(currentPath).parent_path().string();
                        scanDirectory();
                    }
                } else if (ch == 13) {
                    if (selectedIndex >= 0 && selectedIndex < (int)entries.size()) {
                        const FileEntry& fe = entries[selectedIndex];
                        if (fe.isFolder) {
                            currentPath = fe.path;
                            scanDirectory();
                        } else {
                            std::string ext = fs::path(fe.path).extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if (ext == ".txt" || ext == ".md" || ext == ".log" || ext == ".ini" || 
                                ext == ".cfg" || ext == ".json" || ext == ".xml" || ext == ".html" ||
                                ext == ".css" || ext == ".js" || ext == ".cpp" || ext == ".c" ||
                                ext == ".h" || ext == ".hpp" || ext == ".py" || ext == ".sh") {
                                fileToEdit = fe.path;
                            } else {
                                ShellExecuteA(NULL, "open", fe.path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                            }
                        }
                    }
                } else if (ch == 0 || ch == 224) {
                    int ext = _getch();
                    switch (ext) {
                        case 72:
                            if (selectedIndex > 0) {
                                selectedIndex--;
                                if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
                            }
                            break;
                        case 80:
                            if (selectedIndex < (int)entries.size() - 1) {
                                selectedIndex++;
                                if (selectedIndex >= scrollOffset + visibleRows) scrollOffset = selectedIndex - visibleRows + 1;
                            }
                            break;
                        case 73:
                            selectedIndex = std::max(0, selectedIndex - visibleRows);
                            scrollOffset = std::max(0, scrollOffset - visibleRows);
                            break;
                        case 81:
                            selectedIndex = std::min((int)entries.size() - 1, selectedIndex + visibleRows);
                            if (selectedIndex >= scrollOffset + visibleRows) scrollOffset = selectedIndex - visibleRows + 1;
                            break;
                        case 71:
                            selectedIndex = 0;
                            scrollOffset = 0;
                            break;
                        case 79:
                            selectedIndex = (int)entries.size() - 1;
                            if (selectedIndex >= visibleRows) scrollOffset = selectedIndex - visibleRows + 1;
                            break;
                    }
                }
            }
            Sleep(30);
        }
        
        SetConsoleMode(hIn, oldMode);
    }
};

class DesktopEnvironment {
private:
    int termWidth = 80, termHeight = 24;
    std::vector<DesktopIcon> icons;
    int selectedIndex = 0;
    int gridCols = 6;
    int gridRows = 4;
    int iconWidth = 12;
    int iconHeight = 5;
    bool inTerminal = false;
    bool inExplorer = false;
    bool inNano = false;
    std::string fileToEdit;
    std::string nanoFilePath;
    std::string externalAppPath;
    std::mt19937 rng;
    std::string desktopPath;
    std::string wallpaperColor;
    int clockTick = 0;
    bool needsFullRedraw = true;
    int lastSelectedIndex = -1;
    int lastTermWidth = 0;
    int lastTermHeight = 0;
    bool hasBooted = false;
    
    std::string getDesktopPath() {
        char buf[MAX_PATH];
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        std::string exePath = fs::path(buf).parent_path().string();
        std::string funuxDesktop = exePath + "\\Desktop";
        
        if (!fs::exists(funuxDesktop)) {
            fs::create_directory(funuxDesktop);
        }
        return funuxDesktop;
    }
    
    void scanDesktop() {
        icons.clear();
        
        DesktopIcon terminalIcon;
        terminalIcon.name = "Terminal";
        terminalIcon.path = "__TERMINAL__";
        terminalIcon.isFolder = false;
        terminalIcon.isShortcut = false;
        terminalIcon.isExecutable = true;
        terminalIcon.gridX = 0;
        terminalIcon.gridY = 0;
        icons.push_back(terminalIcon);
        
        DesktopIcon filesIcon;
        filesIcon.name = "Files";
        filesIcon.path = "__EXPLORER__";
        filesIcon.isFolder = true;
        filesIcon.isShortcut = false;
        filesIcon.isExecutable = false;
        filesIcon.gridX = 1;
        filesIcon.gridY = 0;
        icons.push_back(filesIcon);
        
        DesktopIcon nanoIcon;
        nanoIcon.name = "Nano";
        nanoIcon.path = "__NANO__";
        nanoIcon.isFolder = false;
        nanoIcon.isShortcut = false;
        nanoIcon.isExecutable = true;
        nanoIcon.gridX = 2;
        nanoIcon.gridY = 0;
        icons.push_back(nanoIcon);
        
        try {
            int idx = 3;
            for (const auto& entry : fs::directory_iterator(desktopPath, fs::directory_options::skip_permission_denied)) {
                DesktopIcon icon;
                icon.path = entry.path().string();
                icon.name = entry.path().filename().string();
                
                if (icon.name.size() > 10) icon.name = icon.name.substr(0, 9) + "~";
                
                icon.isFolder = fs::is_directory(entry);
                icon.isShortcut = entry.path().extension() == ".lnk" || entry.path().extension() == ".url";
                icon.isExecutable = entry.path().extension() == ".exe" || entry.path().extension() == ".bat" || entry.path().extension() == ".cmd";
                
                icon.gridX = idx % gridCols;
                icon.gridY = idx / gridCols;
                
                icons.push_back(icon);
                idx++;
                
                if (idx >= gridCols * gridRows) break;
            }
        } catch (...) {}
        
        for (size_t i = 0; i < icons.size(); i++) {
            icons[i].gridX = i % gridCols;
            icons[i].gridY = i / gridCols;
        }
    }
    
    void drawLoadingScreen() {
        std::cout << ANSI::ALT_BUFFER_ON << ANSI::CURSOR_HIDE;
        std::cout << ANSI::bg256(17) << ANSI::CLEAR_SCREEN;
        
        std::vector<std::string> logo = {
            R"(  ██╗     ██╗   ██╗███╗   ██╗██╗   ██╗██╗  ██╗)",
            R"(  ██║     ██║   ██║████╗  ██║██║   ██║╚██╗██╔╝)",
            R"(  ██║     ██║   ██║██╔██╗ ██║██║   ██║ ╚███╔╝ )",
            R"(  ██║     ██║   ██║██║╚██╗██║██║   ██║ ██╔██╗ )",
            R"(  ███████╗╚██████╔╝██║ ╚████║╚██████╔╝██╔╝ ██╗)",
            R"(  ╚══════╝ ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═╝)"
        };
        
        int logoStartY = termHeight / 2 - 5;
        int logoStartX = (termWidth - 48) / 2;
        
        for (size_t i = 0; i < logo.size(); i++) {
            std::cout << ANSI::moveTo(logoStartY + i, logoStartX) << ANSI::fg256(39) << logo[i];
        }
        
        std::cout << ANSI::moveTo(logoStartY + 7, (termWidth - 20) / 2) << ANSI::fg256(45) << "Desktop Environment";
        std::cout << ANSI::moveTo(logoStartY + 8, (termWidth - 24) / 2) << ANSI::fg256(240) << "By Patrick Andrew Cortez";
        
        int barY = logoStartY + 11;
        int barWidth = 40;
        int barX = (termWidth - barWidth - 2) / 2;
        
        std::vector<std::string> loadingSteps = {
            "Initializing kernel...",
            "Loading system services...",
            "Mounting filesystems...",
            "Starting desktop manager...",
            "Scanning desktop items...",
            "Loading user preferences...",
            "Preparing workspace...",
            "Almost ready..."
        };
        
        for (int step = 0; step < 8; step++) {
            std::cout << ANSI::moveTo(barY, barX) << ANSI::fg256(240) << "[";
            
            int filled = (step + 1) * barWidth / 8;
            for (int j = 0; j < barWidth; j++) {
                if (j < filled) {
                    int color = 39 + (j * 6 / barWidth);
                    std::cout << ANSI::fg256(color) << "█";
                } else {
                    std::cout << ANSI::fg256(236) << "░";
                }
            }
            std::cout << ANSI::fg256(240) << "]";
            
            std::cout << ANSI::moveTo(barY + 2, (termWidth - (int)loadingSteps[step].size()) / 2);
            std::cout << ANSI::fg256(250) << "                                        ";
            std::cout << ANSI::moveTo(barY + 2, (termWidth - (int)loadingSteps[step].size()) / 2);
            std::cout << ANSI::fg256(250) << loadingSteps[step];
            
            int pct = (step + 1) * 100 / 8;
            std::cout << ANSI::moveTo(barY + 1, (termWidth - 4) / 2) << ANSI::fg256(46) << pct << "%";
            
            std::cout.flush();
            Sleep(300 + rand() % 200);
        }
        
        std::cout << ANSI::moveTo(barY + 4, (termWidth - 16) / 2) << ANSI::fg256(46) << "Welcome to Lunux!";
        std::cout.flush();
        Sleep(500);
    }
    
    void drawDesktop() {
        if (needsFullRedraw || termWidth != lastTermWidth || termHeight != lastTermHeight) {
            std::cout << ANSI::bg256(17) << ANSI::CLEAR_SCREEN << ANSI::CURSOR_HOME;
            
            std::cout << ANSI::bg256(18) << ANSI::fg256(255);
            for (int i = 0; i < termWidth; i++) std::cout << " ";
            
            std::cout << ANSI::moveTo(1, 2) << ANSI::fg256(46) << "Lunux" << ANSI::fg256(240) << " Desktop";
            
            std::cout << ANSI::moveTo(termHeight, 1) << ANSI::bg256(235) << ANSI::fg256(240);
            for (int i = 0; i < termWidth; i++) std::cout << " ";
            std::cout << ANSI::moveTo(termHeight, 2);
            std::cout << ANSI::fg256(250) << "Arrow Keys: Navigate | Enter: Open | T: Terminal | R: Refresh | Esc: Exit";
            
            for (size_t i = 0; i < icons.size(); i++) {
                drawIcon(icons[i], i == (size_t)selectedIndex);
            }
            
            needsFullRedraw = false;
            lastTermWidth = termWidth;
            lastTermHeight = termHeight;
            lastSelectedIndex = selectedIndex;
        } else if (selectedIndex != lastSelectedIndex) {
            if (lastSelectedIndex >= 0 && lastSelectedIndex < (int)icons.size()) {
                drawIcon(icons[lastSelectedIndex], false);
            }
            if (selectedIndex >= 0 && selectedIndex < (int)icons.size()) {
                drawIcon(icons[selectedIndex], true);
            }
            lastSelectedIndex = selectedIndex;
        }
        
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream timeStr;
        timeStr << std::put_time(std::localtime(&t), "%H:%M:%S");
        std::cout << ANSI::moveTo(1, termWidth - 10) << ANSI::bg256(18) << ANSI::fg256(255) << timeStr.str();
        
        if (selectedIndex >= 0 && selectedIndex < (int)icons.size()) {
            std::cout << ANSI::moveTo(termHeight, termWidth - 25) << ANSI::bg256(235);
            std::cout << ANSI::fg256(46) << "[" << icons[selectedIndex].name << "]     ";
        }
        
        std::cout.flush();
    }
    
    void drawIcon(const DesktopIcon& icon, bool selected) {
        int startX = 3 + icon.gridX * iconWidth;
        int startY = 3 + icon.gridY * iconHeight;
        
        std::string borderColor = selected ? ANSI::fg256(46) : ANSI::fg256(240);
        std::string bgColor = selected ? ANSI::bg256(24) : ANSI::bg256(17);
        std::string fgColor = ANSI::fg256(255);
        
        std::string iconChar;
        int iconFg;
        int iconPad = 3;
        if (icon.path == "__TERMINAL__") {
            iconChar = ">_";
            iconFg = 46;
            iconPad = 3;
        } else if (icon.path == "__EXPLORER__") {
            iconChar = "[]";
            iconFg = 220;
            iconPad = 3;
        } else if (icon.path == "__NANO__") {
            iconChar = "ED";
            iconFg = 45;
            iconPad = 3;
        } else if (icon.isFolder) {
            iconChar = "[=]";
            iconFg = 220;
            iconPad = 2;
        } else if (icon.isShortcut) {
            iconChar = "->";
            iconFg = 39;
            iconPad = 3;
        } else if (icon.isExecutable) {
            iconChar = "<>";
            iconFg = 196;
            iconPad = 3;
        } else {
            iconChar = "..";
            iconFg = 250;
            iconPad = 3;
        }
        
        std::cout << ANSI::moveTo(startY, startX) << bgColor << borderColor;
        std::cout << "+--------+";
        std::cout << ANSI::moveTo(startY + 1, startX) << "|" << ANSI::fg256(iconFg);
        for (int i = 0; i < iconPad; i++) std::cout << " ";
        std::cout << iconChar;
        for (int i = 0; i < 8 - iconPad - (int)iconChar.size(); i++) std::cout << " ";
        std::cout << borderColor << "|";
        std::cout << ANSI::moveTo(startY + 2, startX) << "|        |";
        std::cout << ANSI::moveTo(startY + 3, startX) << "+--------+";
        
        std::string displayName = icon.name;
        if (displayName.size() > 10) displayName = displayName.substr(0, 9) + "~";
        int nameX = startX + (10 - (int)displayName.size()) / 2;
        std::cout << ANSI::moveTo(startY + 4, nameX) << ANSI::bg256(17) << ANSI::fg256(255) << displayName;
    }
    
    void openItem(const DesktopIcon& icon) {
        if (icon.path == "__TERMINAL__") {
            inTerminal = true;
            return;
        }
        
        if (icon.path == "__EXPLORER__") {
            inExplorer = true;
            return;
        }
        
        if (icon.path == "__NANO__") {
            inNano = true;
            nanoFilePath = "";
            return;
        }
        
        if (icon.isFolder) {
            inExplorer = true;
            fileToEdit = icon.path;
            return;
        }
        
        std::string ext = fs::path(icon.path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".txt" || ext == ".md" || ext == ".log" || ext == ".ini" || 
            ext == ".cfg" || ext == ".json" || ext == ".xml" || ext == ".html" ||
            ext == ".css" || ext == ".js" || ext == ".cpp" || ext == ".c" ||
            ext == ".h" || ext == ".hpp" || ext == ".py" || ext == ".sh") {
            fileToEdit = icon.path;
            return;
        }
        
        if (ext == ".exe" || ext == ".bat" || ext == ".cmd") {
            externalAppPath = icon.path;
            return;
        }
        
        ShellExecuteA(NULL, "open", icon.path.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    
    void updateTermSize() {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int newWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        int newHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        
        int newGridCols = (newWidth - 4) / iconWidth;
        int newGridRows = (newHeight - 6) / iconHeight;
        if (newGridCols < 2) newGridCols = 2;
        if (newGridRows < 2) newGridRows = 2;
        
        if (newWidth != termWidth || newHeight != termHeight || newGridCols != gridCols || newGridRows != gridRows) {
            termWidth = newWidth;
            termHeight = newHeight;
            gridCols = newGridCols;
            gridRows = newGridRows;
            
            for (size_t i = 0; i < icons.size(); i++) {
                icons[i].gridX = i % gridCols;
                icons[i].gridY = i / gridCols;
            }
            
            needsFullRedraw = true;
        }
    }

public:
    bool shouldLaunchTerminal() const { return inTerminal; }
    bool shouldLaunchExplorer() const { return inExplorer; }
    bool shouldLaunchNano() const { return inNano; }
    bool hasFileToEdit() const { return !fileToEdit.empty() && !inExplorer && !inNano; }
    bool hasExternalApp() const { return !externalAppPath.empty(); }
    std::string getFileToEdit() const { return fileToEdit; }
    std::string getNanoFilePath() const { return nanoFilePath; }
    std::string getExternalAppPath() const { return externalAppPath; }
    std::string getExplorerStartPath() const { return inExplorer && !fileToEdit.empty() ? fileToEdit : ""; }
    
    void reset() {
        inTerminal = false;
        inExplorer = false;
        inNano = false;
        fileToEdit.clear();
        nanoFilePath.clear();
        externalAppPath.clear();
        needsFullRedraw = true;
        lastSelectedIndex = -1;
    }
    
    void run() {
        std::random_device rd;
        rng.seed(rd());
        
        desktopPath = getDesktopPath();
        
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD oldMode;
        GetConsoleMode(hIn, &oldMode);
        SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
        
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode;
        GetConsoleMode(hOut, &outMode);
        SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        
        updateTermSize();
        if (!hasBooted) {
            drawLoadingScreen();
            hasBooted = true;
        }
        scanDesktop();
        
        bool running = true;
        while (running && !inTerminal && !inExplorer && !inNano && fileToEdit.empty() && externalAppPath.empty()) {
            updateTermSize();
            drawDesktop();
            
            if (_kbhit()) {
                int ch = _getch();
                
                if (ch == 27) {
                    running = false;
                } else if (ch == 't' || ch == 'T') {
                    inTerminal = true;
                } else if (ch == 'f' || ch == 'F') {
                    inExplorer = true;
                } else if (ch == 'r' || ch == 'R') {
                    scanDesktop();
                    needsFullRedraw = true;
                } else if (ch == 13) {
                    if (selectedIndex >= 0 && selectedIndex < (int)icons.size()) {
                        openItem(icons[selectedIndex]);
                    }
                } else if (ch == 0 || ch == 224) {
                    int ext = _getch();
                    int curX = selectedIndex % gridCols;
                    int curY = selectedIndex / gridCols;
                    
                    switch (ext) {
                        case 72: if (curY > 0) selectedIndex -= gridCols; break;
                        case 80: if (selectedIndex + gridCols < (int)icons.size()) selectedIndex += gridCols; break;
                        case 75: if (selectedIndex > 0) selectedIndex--; break;
                        case 77: if (selectedIndex < (int)icons.size() - 1) selectedIndex++; break;
                    }
                    
                    if (selectedIndex < 0) selectedIndex = 0;
                    if (selectedIndex >= (int)icons.size()) selectedIndex = (int)icons.size() - 1;
                }
            }
            FunuxSys::Scheduler::get().tick();
            Sleep(50);
        }
        
        SetConsoleMode(hIn, oldMode);
    }
};

std::string getLinuxifyPath() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    fs::path exePath = fs::path(buf);
    fs::path linuxifyPath = exePath.parent_path().parent_path().parent_path() / "linuxify.exe";
    if (fs::exists(linuxifyPath)) {
        return linuxifyPath.string();
    }
    fs::path altPath = exePath.parent_path().parent_path() / "linuxify.exe";
    if (fs::exists(altPath)) {
        return altPath.string();
    }
    fs::path sameDirPath = exePath.parent_path() / "linuxify.exe";
    if (fs::exists(sameDirPath)) {
        return sameDirPath.string();
    }
    return "";
}

int main() {
    // Force US Standard Layout to fix "Dead Keys" (double press for quotes)
    HKL hUsLayout = LoadKeyboardLayoutA("00000409", KLF_ACTIVATE | KLF_SUBSTITUTE_OK);
    if (hUsLayout) {
        ActivateKeyboardLayout(hUsLayout, KLF_SETFORPROCESS);
    }

    SetConsoleOutputCP(CP_UTF8);
    
    DesktopEnvironment desktop;
    FileExplorer explorer;
    
    bool firstRun = true;
    bool running = true;
    
    while (running) {
        if (!firstRun) {
            desktop.reset();
        }
        firstRun = false;
        
        desktop.run();
        
        if (desktop.shouldLaunchTerminal()) {
            std::string linuxifyPath = getLinuxifyPath();
            if (!linuxifyPath.empty()) {
                std::cout << ANSI::ALT_BUFFER_OFF << ANSI::CURSOR_SHOW << ANSI::RESET;
                std::cout << ANSI::CLEAR_SCREEN << ANSI::CURSOR_HOME;
                
                HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
                DWORD oldMode;
                GetConsoleMode(hIn, &oldMode);
                SetConsoleMode(hIn, oldMode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
                
                FunuxSys::ProcessManager::get().spawnAndWait(linuxifyPath, "", "");
                
                SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
                std::cout << ANSI::CURSOR_HIDE;
            } else {
                std::cout << ANSI::CLEAR_SCREEN << ANSI::CURSOR_HOME;
                std::cout << ANSI::fg256(196) << "Error: linuxify.exe not found!\n";
                std::cout << ANSI::fg256(250) << "Please ensure linuxify.exe is in the parent directory.\n";
                std::cout << ANSI::fg256(240) << "Press any key to return to desktop...\n";
                _getch();
            }
        } else if (desktop.shouldLaunchExplorer()) {
            std::string startPath = desktop.getExplorerStartPath();
            explorer.run(startPath);
            
            while (explorer.hasFileToEdit()) {
                std::string file = explorer.getFileToEdit();
                LazyNano nano;
                nano.run(file);
                explorer.run(fs::path(file).parent_path().string());
            }
        } else if (desktop.shouldLaunchNano()) {
            std::cout << ANSI::CLEAR_SCREEN << ANSI::CURSOR_HOME;
            std::cout << ANSI::fg256(46) << "Enter file path to edit (or press Enter for new file): " << ANSI::RESET;
            std::cout << ANSI::CURSOR_SHOW;
            
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD oldMode;
            GetConsoleMode(hIn, &oldMode);
            SetConsoleMode(hIn, oldMode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            
            std::string filePath;
            std::getline(std::cin, filePath);
            
            SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
            std::cout << ANSI::CURSOR_HIDE;
            
            if (filePath.empty()) {
                filePath = "untitled.txt";
            }
            
            LazyNano nano;
            nano.run(filePath);
        } else if (desktop.hasExternalApp()) {
            std::string appPath = desktop.getExternalAppPath();
            std::string workDir = fs::path(appPath).parent_path().string();
            
            std::cout << ANSI::CLEAR_SCREEN << ANSI::CURSOR_HOME << ANSI::CURSOR_SHOW;
            
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD oldMode;
            GetConsoleMode(hIn, &oldMode);
            SetConsoleMode(hIn, oldMode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            
            FunuxSys::ProcessManager::get().spawnAndWait(appPath, "", workDir);
            
            SetConsoleMode(hIn, oldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
            std::cout << ANSI::CURSOR_HIDE;
        } else if (desktop.hasFileToEdit()) {
            std::string file = desktop.getFileToEdit();
            LazyNano nano;
            nano.run(file);
        } else {
            running = false;
        }
    }
    
    std::cout << ANSI::ALT_BUFFER_OFF << ANSI::CURSOR_SHOW << ANSI::RESET;
    return 0;
}
