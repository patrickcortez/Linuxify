/*
 * fs_context.hpp - Navigation Context and Path Utilities for LevelFS
 * 
 * Compile: Include in mount.cpp with #include "fs_context.hpp"
 */

#ifndef FS_CONTEXT_HPP
#define FS_CONTEXT_HPP

#include "fs_common.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <cstring>

using namespace std;

struct NavigationContext {
    uint64_t currentDirCluster;
    uint64_t currentContentCluster;
    uint64_t rootContentCluster;
    uint64_t currentLevelID;
    uint64_t rootLevelID;
    uint32_t currentFolderPerms;
    string currentPath;
    string currentVersion;
    
    NavigationContext() {
        currentDirCluster = 0;
        currentContentCluster = 0;
        rootContentCluster = 0;
        currentLevelID = 0;
        rootLevelID = 0;
        currentFolderPerms = PERM_ROOT_DEFAULT;
        currentPath = "/";
        currentVersion = "master";
    }
    
    void reset(uint64_t rootDir, uint64_t rootContent, uint64_t rootLevel) {
        currentDirCluster = rootDir;
        currentContentCluster = rootContent;
        rootContentCluster = rootContent;
        currentLevelID = rootLevel;
        rootLevelID = rootLevel;
        currentFolderPerms = PERM_ROOT_DEFAULT;
        currentPath = "/";
        currentVersion = "master";
    }
    
    bool isAtRoot() const {
        return currentPath == "/" || currentContentCluster == rootContentCluster;
    }
    
    void appendPath(const string& name) {
        if (currentPath.back() != '/') {
            currentPath += '/';
        }
        currentPath += name;
    }
    
    string getParentPath() const {
        if (currentPath == "/" || currentPath.empty()) {
            return "/";
        }
        
        size_t lastSlash = currentPath.rfind('/');
        if (lastSlash == 0) {
            return "/";
        }
        if (lastSlash == string::npos) {
            return "/";
        }
        
        return currentPath.substr(0, lastSlash);
    }
    
    string getCurrentFolderName() const {
        if (currentPath == "/" || currentPath.empty()) {
            return "/";
        }
        
        size_t lastSlash = currentPath.rfind('/');
        if (lastSlash == string::npos) {
            return currentPath;
        }
        
        return currentPath.substr(lastSlash + 1);
    }
    
    void serialize(char* buffer, size_t bufferSize) const {
        if (bufferSize < 256) return;
        
        memset(buffer, 0, bufferSize);
        
        memcpy(buffer, &currentDirCluster, 8);
        memcpy(buffer + 8, &currentContentCluster, 8);
        memcpy(buffer + 16, &rootContentCluster, 8);
        memcpy(buffer + 24, &currentLevelID, 8);
        memcpy(buffer + 32, &rootLevelID, 8);
        memcpy(buffer + 40, &currentFolderPerms, 4);
        
        size_t pathLen = min(currentPath.length(), (size_t)100);
        memcpy(buffer + 48, currentPath.c_str(), pathLen);
        
        size_t versionLen = min(currentVersion.length(), (size_t)32);
        memcpy(buffer + 148, currentVersion.c_str(), versionLen);
    }
    
    void deserialize(const char* buffer, size_t bufferSize) {
        if (bufferSize < 256) return;
        
        memcpy(&currentDirCluster, buffer, 8);
        memcpy(&currentContentCluster, buffer + 8, 8);
        memcpy(&rootContentCluster, buffer + 16, 8);
        memcpy(&currentLevelID, buffer + 24, 8);
        memcpy(&rootLevelID, buffer + 32, 8);
        memcpy(&currentFolderPerms, buffer + 40, 4);
        
        char pathBuf[101] = {0};
        memcpy(pathBuf, buffer + 48, 100);
        currentPath = string(pathBuf);
        
        char versionBuf[33] = {0};
        memcpy(versionBuf, buffer + 148, 32);
        currentVersion = string(versionBuf);
    }
};

class PathUtils {
public:
    static vector<string> splitPath(const string& path) {
        vector<string> parts;
        stringstream ss(path);
        string part;
        
        while (getline(ss, part, '/')) {
            if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
        }
        
        return parts;
    }
    
    static string joinPath(const vector<string>& parts) {
        if (parts.empty()) {
            return "/";
        }
        
        string result;
        for (const auto& part : parts) {
            result += "/" + part;
        }
        
        return result;
    }
    
    static string normalizePath(const string& path) {
        vector<string> parts = splitPath(path);
        vector<string> normalized;
        
        for (const auto& part : parts) {
            if (part == "..") {
                if (!normalized.empty()) {
                    normalized.pop_back();
                }
            } else if (part != ".") {
                normalized.push_back(part);
            }
        }
        
        return joinPath(normalized);
    }
    
    static bool isAbsolute(const string& path) {
        return !path.empty() && path[0] == '/';
    }
    
    static bool isRelative(const string& path) {
        return !isAbsolute(path);
    }
    
    static string getBasename(const string& path) {
        if (path.empty() || path == "/") {
            return "";
        }
        
        size_t lastSlash = path.rfind('/');
        if (lastSlash == string::npos) {
            return path;
        }
        
        return path.substr(lastSlash + 1);
    }
    
    static string getDirname(const string& path) {
        if (path.empty() || path == "/") {
            return "/";
        }
        
        size_t lastSlash = path.rfind('/');
        if (lastSlash == 0) {
            return "/";
        }
        if (lastSlash == string::npos) {
            return ".";
        }
        
        return path.substr(0, lastSlash);
    }
    
    static pair<string, string> splitNameExtension(const string& filename) {
        size_t dotPos = filename.rfind('.');
        
        if (dotPos == string::npos || dotPos == 0) {
            return make_pair(filename, string(""));
        }
        
        return make_pair(filename.substr(0, dotPos), filename.substr(dotPos + 1));
    }
    
    static string combinePath(const string& base, const string& name) {
        if (base.empty() || base == "/") {
            return "/" + name;
        }
        
        if (base.back() == '/') {
            return base + name;
        }
        
        return base + "/" + name;
    }
    
    static bool hasExtension(const string& filename) {
        size_t dotPos = filename.rfind('.');
        return dotPos != string::npos && dotPos != 0 && dotPos != filename.length() - 1;
    }
    
    static string getExtension(const string& filename) {
        size_t dotPos = filename.rfind('.');
        
        if (dotPos == string::npos || dotPos == 0 || dotPos == filename.length() - 1) {
            return "";
        }
        
        return filename.substr(dotPos + 1);
    }
    
    static bool pathContains(const string& parent, const string& child) {
        string normParent = normalizePath(parent);
        string normChild = normalizePath(child);
        
        if (normParent.length() >= normChild.length()) {
            return false;
        }
        
        return normChild.substr(0, normParent.length()) == normParent &&
               (normChild[normParent.length()] == '/' || normParent == "/");
    }
    
    static int getPathDepth(const string& path) {
        return splitPath(path).size();
    }
    
    static string getPathComponent(const string& path, int index) {
        vector<string> parts = splitPath(path);
        
        if (index < 0 || index >= (int)parts.size()) {
            return "";
        }
        
        return parts[index];
    }
    
    static string truncateName(const string& name, size_t maxLen) {
        if (name.length() <= maxLen) {
            return name;
        }
        
        return name.substr(0, maxLen);
    }
    
    static bool isValidName(const string& name) {
        if (name.empty() || name.length() > 23) {
            return false;
        }
        
        for (char c : name) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || 
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                return false;
            }
        }
        
        return true;
    }
    
    static bool isValidExtension(const string& ext) {
        if (ext.length() > 7) {
            return false;
        }
        
        for (char c : ext) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || 
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '.') {
                return false;
            }
        }
        
        return true;
    }
};

class ContextManager {
private:
    NavigationContext& context;
    DiskDevice& disk;

public:
    ContextManager(NavigationContext& ctx, DiskDevice& d) : context(ctx), disk(d) {}
    
    bool saveContext(uint64_t contextCluster) {
        if (!disk.isOpen()) return false;
        
        char buffer[SECTOR_SIZE];
        memset(buffer, 0, SECTOR_SIZE);
        context.serialize(buffer, SECTOR_SIZE);
        
        return disk.writeSector(contextCluster * SECTORS_PER_CLUSTER, buffer);
    }
    
    bool loadContext(uint64_t contextCluster) {
        if (!disk.isOpen()) return false;
        
        char buffer[SECTOR_SIZE];
        
        if (!disk.readSector(contextCluster * SECTORS_PER_CLUSTER, buffer)) {
            return false;
        }
        
        context.deserialize(buffer, SECTOR_SIZE);
        return true;
    }
    
    void enterFolder(const string& name, uint64_t newContentCluster, uint32_t folderPerms) {
        context.currentContentCluster = newContentCluster;
        context.currentFolderPerms = folderPerms;
        context.appendPath(name);
    }
    
    void goToRoot() {
        context.currentDirCluster = context.currentDirCluster;
        context.currentContentCluster = context.rootContentCluster;
        context.currentLevelID = context.rootLevelID;
        context.currentFolderPerms = PERM_ROOT_DEFAULT;
        context.currentPath = "/";
        context.currentVersion = "master";
    }
    
    void switchVersion(const string& version, uint64_t contentCluster, uint64_t levelID) {
        context.currentContentCluster = contentCluster;
        context.currentLevelID = levelID;
        context.currentVersion = version;
    }
    
    bool canRead() const {
        return (context.currentFolderPerms & PERM_READ) != 0;
    }
    
    bool canWrite() const {
        return (context.currentFolderPerms & PERM_WRITE) != 0;
    }
    
    bool canExecute() const {
        return (context.currentFolderPerms & PERM_EXEC) != 0;
    }
    
    string getContextInfo() const {
        stringstream ss;
        ss << "Path: " << context.currentPath << "\n";
        ss << "Version: " << context.currentVersion << "\n";
        ss << "Level ID: " << context.currentLevelID << "\n";
        ss << "Content Cluster: " << context.currentContentCluster << "\n";
        ss << "Permissions: ";
        ss << ((context.currentFolderPerms & PERM_READ) ? 'r' : '-');
        ss << ((context.currentFolderPerms & PERM_WRITE) ? 'w' : '-');
        ss << ((context.currentFolderPerms & PERM_EXEC) ? 'x' : '-');
        ss << "\n";
        return ss.str();
    }
};

#endif
