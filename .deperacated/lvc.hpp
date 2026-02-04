// Linuxify Version Control (LVC) - Production Grade
// A sophisticated git-like version control system
// Features: Myers diff, rolling hash delta, SHA-256, branches, merge

#ifndef LVC_HPP
#define LVC_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <functional>
#include <algorithm>
#include <numeric>
#include <queue>
#include <stack>
#include <memory>
#include <regex>
#include <windows.h>

namespace fs = std::filesystem;

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

namespace LVCConfig {
    constexpr size_t CHUNK_SIZE = 64;           // Rolling hash window
    constexpr size_t MIN_MATCH = 4;             // Minimum match for delta
    constexpr size_t MAX_DELTA_CHAIN = 50;      // Max delta chain depth
    constexpr size_t HASH_PRIME = 31;           // Rolling hash prime
    constexpr size_t HASH_MOD = 1000000007;     // Rolling hash modulo
}

// ============================================================================
// SHA-256 IMPLEMENTATION (Simplified but functional)
// ============================================================================

class SHA256 {
private:
    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
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
        uint32_t h[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        
        // Padding
        std::vector<uint8_t> msg(data.begin(), data.end());
        size_t origLen = msg.size();
        msg.push_back(0x80);
        while ((msg.size() + 8) % 64 != 0) msg.push_back(0);
        
        uint64_t bitLen = origLen * 8;
        for (int i = 7; i >= 0; i--) msg.push_back((bitLen >> (i * 8)) & 0xFF);
        
        // Process blocks
        for (size_t i = 0; i < msg.size(); i += 64) {
            uint32_t w[64];
            for (int j = 0; j < 16; j++) {
                w[j] = (msg[i + j*4] << 24) | (msg[i + j*4 + 1] << 16) |
                       (msg[i + j*4 + 2] << 8) | msg[i + j*4 + 3];
            }
            for (int j = 16; j < 64; j++) {
                w[j] = ep1(w[j-2]) + w[j-7] + ep0(w[j-15]) + w[j-16];
            }
            
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
            uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
            
            for (int j = 0; j < 64; j++) {
                uint32_t t1 = hh + sig1(e) + ch(e, f, g) + K[j] + w[j];
                uint32_t t2 = sig0(a) + maj(a, b, c);
                hh = g; g = f; f = e; e = d + t1;
                d = c; c = b; b = a; a = t1 + t2;
            }
            
            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
        
        std::ostringstream oss;
        for (int i = 0; i < 8; i++) {
            oss << std::hex << std::setfill('0') << std::setw(8) << h[i];
        }
        return oss.str();
    }
};

constexpr uint32_t SHA256::K[64];

// ============================================================================
// MYERS DIFF ALGORITHM - O((N+M)D) optimal diff
// ============================================================================

class MyersDiff {
public:
    enum EditType { EQUAL, INSERT, DEL };
    
    struct Edit {
        EditType type;
        int oldStart, oldEnd;
        int newStart, newEnd;
        std::vector<std::string> lines;
    };
    
    struct DiffHunk {
        int oldStart, oldCount;
        int newStart, newCount;
        std::vector<std::pair<char, std::string>> lines; // +/-/space, content
    };

private:
    static std::vector<std::string> splitLines(const std::string& s) {
        std::vector<std::string> lines;
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) {
            lines.push_back(line);
        }
        return lines;
    }
    
    // Core Myers algorithm - find shortest edit script
    static std::vector<std::pair<int,int>> shortestEdit(
        const std::vector<std::string>& a, 
        const std::vector<std::string>& b) 
    {
        int n = (int)a.size(), m = (int)b.size();
        int max = n + m;
        
        std::vector<int> v(2 * max + 1, 0);
        std::vector<std::vector<int>> trace;
        
        for (int d = 0; d <= max; d++) {
            trace.push_back(v);
            
            for (int k = -d; k <= d; k += 2) {
                int x;
                if (k == -d || (k != d && v[k - 1 + max] < v[k + 1 + max])) {
                    x = v[k + 1 + max];
                } else {
                    x = v[k - 1 + max] + 1;
                }
                
                int y = x - k;
                
                while (x < n && y < m && a[x] == b[y]) {
                    x++; y++;
                }
                
                v[k + max] = x;
                
                if (x >= n && y >= m) {
                    // Backtrack
                    std::vector<std::pair<int,int>> path;
                    for (int dd = d; dd >= 0; dd--) {
                        int kk = x - y;
                        int prevK;
                        
                        if (kk == -dd || (kk != dd && trace[dd][kk - 1 + max] < trace[dd][kk + 1 + max])) {
                            prevK = kk + 1;
                        } else {
                            prevK = kk - 1;
                        }
                        
                        int prevX = trace[dd][prevK + max];
                        int prevY = prevX - prevK;
                        
                        while (x > prevX && y > prevY) {
                            path.push_back({x - 1, y - 1});
                            x--; y--;
                        }
                        
                        if (dd > 0) {
                            path.push_back({prevX, prevY});
                        }
                        
                        x = prevX;
                        y = prevY;
                    }
                    
                    std::reverse(path.begin(), path.end());
                    return path;
                }
            }
        }
        
        return {};
    }

public:
    // Compute unified diff hunks
    static std::vector<DiffHunk> diff(const std::string& oldText, const std::string& newText, int contextLines = 3) {
        auto oldLines = splitLines(oldText);
        auto newLines = splitLines(newText);
        
        int n = (int)oldLines.size(), m = (int)newLines.size();
        
        // Build edit operations
        std::vector<Edit> edits;
        auto path = shortestEdit(oldLines, newLines);
        
        int oldIdx = 0, newIdx = 0;
        
        for (const auto& [px, py] : path) {
            // Matches before this point
            while (oldIdx < px && newIdx < py) {
                Edit e;
                e.type = EQUAL;
                e.oldStart = oldIdx;
                e.oldEnd = oldIdx + 1;
                e.newStart = newIdx;
                e.newEnd = newIdx + 1;
                e.lines.push_back(oldLines[oldIdx]);
                edits.push_back(e);
                oldIdx++; newIdx++;
            }
            
            // Deletions
            while (oldIdx < px) {
                Edit e;
                e.type = DEL;
                e.oldStart = oldIdx;
                e.oldEnd = oldIdx + 1;
                e.newStart = newIdx;
                e.newEnd = newIdx;
                e.lines.push_back(oldLines[oldIdx]);
                edits.push_back(e);
                oldIdx++;
            }
            
            // Insertions
            while (newIdx < py) {
                Edit e;
                e.type = INSERT;
                e.oldStart = oldIdx;
                e.oldEnd = oldIdx;
                e.newStart = newIdx;
                e.newEnd = newIdx + 1;
                e.lines.push_back(newLines[newIdx]);
                edits.push_back(e);
                newIdx++;
            }
        }
        
        // Remaining equals
        while (oldIdx < n && newIdx < m) {
            Edit e;
            e.type = EQUAL;
            e.oldStart = oldIdx;
            e.oldEnd = oldIdx + 1;
            e.newStart = newIdx;
            e.newEnd = newIdx + 1;
            e.lines.push_back(oldLines[oldIdx]);
            edits.push_back(e);
            oldIdx++; newIdx++;
        }
        
        // Remaining in old (deletions)
        while (oldIdx < n) {
            Edit e;
            e.type = DEL;
            e.oldStart = oldIdx;
            e.oldEnd = oldIdx + 1;
            e.lines.push_back(oldLines[oldIdx]);
            edits.push_back(e);
            oldIdx++;
        }
        
        // Remaining in new (insertions)
        while (newIdx < m) {
            Edit e;
            e.type = INSERT;
            e.newStart = newIdx;
            e.newEnd = newIdx + 1;
            e.lines.push_back(newLines[newIdx]);
            edits.push_back(e);
            newIdx++;
        }
        
        // Group into hunks with context
        std::vector<DiffHunk> hunks;
        if (edits.empty()) return hunks;
        
        // Find change regions
        std::vector<std::pair<int, int>> changeRegions;
        int start = -1;
        for (int i = 0; i < (int)edits.size(); i++) {
            if (edits[i].type != EQUAL) {
                if (start == -1) start = i;
            } else if (start != -1) {
                changeRegions.push_back({start, i});
                start = -1;
            }
        }
        if (start != -1) changeRegions.push_back({start, (int)edits.size()});
        
        // Merge nearby regions and add context
        for (const auto& [s, e] : changeRegions) {
            DiffHunk hunk;
            int ctxStart = std::max(0, s - contextLines);
            int ctxEnd = std::min((int)edits.size(), e + contextLines);
            
            // Calculate line numbers
            hunk.oldStart = edits[ctxStart].oldStart + 1;
            hunk.newStart = edits[ctxStart].newStart + 1;
            hunk.oldCount = 0;
            hunk.newCount = 0;
            
            for (int i = ctxStart; i < ctxEnd; i++) {
                const auto& ed = edits[i];
                for (const auto& line : ed.lines) {
                    if (ed.type == EQUAL) {
                        hunk.lines.push_back({' ', line});
                        hunk.oldCount++;
                        hunk.newCount++;
                    } else if (ed.type == DEL) {
                        hunk.lines.push_back({'-', line});
                        hunk.oldCount++;
                    } else {
                        hunk.lines.push_back({'+', line});
                        hunk.newCount++;
                    }
                }
            }
            
            if (!hunk.lines.empty()) hunks.push_back(hunk);
        }
        
        return hunks;
    }
    
    // Generate unified diff string
    static std::string unifiedDiff(const std::string& oldPath, const std::string& newPath,
                                   const std::string& oldText, const std::string& newText) {
        auto hunks = diff(oldText, newText);
        if (hunks.empty()) return "";
        
        std::ostringstream oss;
        oss << "--- " << oldPath << "\n";
        oss << "+++ " << newPath << "\n";
        
        for (const auto& hunk : hunks) {
            oss << "@@ -" << hunk.oldStart << "," << hunk.oldCount
                << " +" << hunk.newStart << "," << hunk.newCount << " @@\n";
            
            for (const auto& [prefix, line] : hunk.lines) {
                oss << prefix << line << "\n";
            }
        }
        
        return oss.str();
    }
    
    // Print colorized diff
    static void printColorDiff(const std::string& oldPath, const std::string& newPath,
                               const std::string& oldText, const std::string& newText) {
        auto hunks = diff(oldText, newText);
        if (hunks.empty()) return;
        
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "--- " << oldPath << "\n";
        std::cout << "+++ " << newPath << "\n";
        
        for (const auto& hunk : hunks) {
            SetConsoleTextAttribute(h, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "@@ -" << hunk.oldStart << "," << hunk.oldCount
                      << " +" << hunk.newStart << "," << hunk.newCount << " @@\n";
            
            for (const auto& [prefix, line] : hunk.lines) {
                if (prefix == '+') {
                    SetConsoleTextAttribute(h, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                } else if (prefix == '-') {
                    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_INTENSITY);
                } else {
                    SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
                std::cout << prefix << line << "\n";
            }
        }
        
        SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    
    // Get diff statistics
    static std::pair<int, int> stats(const std::string& oldText, const std::string& newText) {
        auto hunks = diff(oldText, newText);
        int added = 0, deleted = 0;
        for (const auto& h : hunks) {
            for (const auto& [p, l] : h.lines) {
                if (p == '+') added++;
                else if (p == '-') deleted++;
            }
        }
        return {added, deleted};
    }
};

// ============================================================================
// ROLLING HASH DELTA COMPRESSION (rsync-style)
// ============================================================================

class DeltaCompression {
public:
    struct DeltaOp {
        enum Type { COPY, INSERT } type;
        size_t srcOffset;
        size_t length;
        std::string data;
    };

private:
    // Rolling hash using Rabin fingerprint style
    struct RollingHash {
        size_t hash = 0;
        size_t power = 1;
        std::deque<uint8_t> window;
        size_t windowSize;
        
        RollingHash(size_t size = LVCConfig::CHUNK_SIZE) : windowSize(size) {
            for (size_t i = 0; i < windowSize - 1; i++) {
                power = (power * LVCConfig::HASH_PRIME) % LVCConfig::HASH_MOD;
            }
        }
        
        void add(uint8_t byte) {
            if (window.size() >= windowSize) {
                uint8_t old = window.front();
                window.pop_front();
                hash = (hash + LVCConfig::HASH_MOD - (old * power) % LVCConfig::HASH_MOD) % LVCConfig::HASH_MOD;
            }
            window.push_back(byte);
            hash = (hash * LVCConfig::HASH_PRIME + byte) % LVCConfig::HASH_MOD;
        }
        
        size_t getHash() const { return hash; }
        bool full() const { return window.size() >= windowSize; }
    };

public:
    // Build signature of source for matching
    static std::unordered_map<size_t, std::vector<size_t>> buildSignature(const std::string& src) {
        std::unordered_map<size_t, std::vector<size_t>> sig;
        
        if (src.size() < LVCConfig::CHUNK_SIZE) return sig;
        
        RollingHash rh;
        for (size_t i = 0; i < src.size(); i++) {
            rh.add((uint8_t)src[i]);
            if (rh.full() && i >= LVCConfig::CHUNK_SIZE - 1) {
                sig[rh.getHash()].push_back(i - LVCConfig::CHUNK_SIZE + 1);
            }
        }
        return sig;
    }
    
    // Create delta between source and target
    static std::vector<DeltaOp> createDelta(const std::string& src, const std::string& tgt) {
        std::vector<DeltaOp> ops;
        
        if (src.empty()) {
            if (!tgt.empty()) {
                DeltaOp op;
                op.type = DeltaOp::INSERT;
                op.data = tgt;
                op.length = tgt.size();
                ops.push_back(op);
            }
            return ops;
        }
        
        auto sig = buildSignature(src);
        RollingHash rh;
        std::string pending;
        size_t pos = 0;
        
        while (pos < tgt.size()) {
            rh.add((uint8_t)tgt[pos]);
            
            if (rh.full()) {
                auto it = sig.find(rh.getHash());
                if (it != sig.end()) {
                    // Verify match and extend
                    size_t srcPos = it->second[0];
                    size_t matchStart = pos - LVCConfig::CHUNK_SIZE + 1;
                    
                    // Verify exact match
                    bool match = true;
                    for (size_t i = 0; i < LVCConfig::CHUNK_SIZE && match; i++) {
                        if (src[srcPos + i] != tgt[matchStart + i]) match = false;
                    }
                    
                    if (match) {
                        // Flush pending inserts
                        if (!pending.empty()) {
                            // Remove the chunk we're about to copy
                            if (pending.size() >= LVCConfig::CHUNK_SIZE) {
                                pending = pending.substr(0, pending.size() - LVCConfig::CHUNK_SIZE + 1);
                            } else {
                                pending.clear();
                            }
                            
                            if (!pending.empty()) {
                                DeltaOp iop;
                                iop.type = DeltaOp::INSERT;
                                iop.data = pending;
                                iop.length = pending.size();
                                ops.push_back(iop);
                            }
                            pending.clear();
                        }
                        
                        // Extend match forward
                        size_t len = LVCConfig::CHUNK_SIZE;
                        while (srcPos + len < src.size() && matchStart + len < tgt.size() &&
                               src[srcPos + len] == tgt[matchStart + len]) {
                            len++;
                        }
                        
                        DeltaOp cop;
                        cop.type = DeltaOp::COPY;
                        cop.srcOffset = srcPos;
                        cop.length = len;
                        ops.push_back(cop);
                        
                        pos = matchStart + len;
                        rh = RollingHash();
                        continue;
                    }
                }
            }
            
            pending += tgt[pos];
            pos++;
        }
        
        // Flush remaining
        if (!pending.empty()) {
            DeltaOp iop;
            iop.type = DeltaOp::INSERT;
            iop.data = pending;
            iop.length = pending.size();
            ops.push_back(iop);
        }
        
        return ops;
    }
    
    // Apply delta to reconstruct target
    static std::string applyDelta(const std::string& src, const std::vector<DeltaOp>& ops) {
        std::string result;
        for (const auto& op : ops) {
            if (op.type == DeltaOp::COPY) {
                if (op.srcOffset + op.length <= src.size()) {
                    result += src.substr(op.srcOffset, op.length);
                }
            } else {
                result += op.data;
            }
        }
        return result;
    }
    
    // Serialize delta for storage
    static std::string serialize(const std::vector<DeltaOp>& ops) {
        std::ostringstream oss;
        oss << "DELTA\n" << ops.size() << "\n";
        for (const auto& op : ops) {
            if (op.type == DeltaOp::COPY) {
                oss << "C " << op.srcOffset << " " << op.length << "\n";
            } else {
                oss << "I " << op.data.size() << "\n";
                oss.write(op.data.data(), op.data.size());
            }
        }
        return oss.str();
    }
    
    // Deserialize delta
    static std::vector<DeltaOp> deserialize(const std::string& data) {
        std::vector<DeltaOp> ops;
        std::istringstream iss(data);
        
        std::string header;
        std::getline(iss, header);
        if (header != "DELTA") return ops;
        
        size_t count;
        iss >> count;
        iss.ignore();
        
        for (size_t i = 0; i < count; i++) {
            char type;
            iss >> type;
            
            DeltaOp op;
            if (type == 'C') {
                op.type = DeltaOp::COPY;
                iss >> op.srcOffset >> op.length;
                iss.ignore();
            } else {
                op.type = DeltaOp::INSERT;
                size_t len;
                iss >> len;
                iss.ignore();
                op.data.resize(len);
                iss.read(&op.data[0], len);
                op.length = len;
            }
            ops.push_back(op);
        }
        return ops;
    }
    
    // Calculate compression ratio
    static double compressionRatio(const std::string& original, const std::vector<DeltaOp>& delta) {
        std::string serialized = serialize(delta);
        if (original.empty()) return 1.0;
        return (double)serialized.size() / original.size();
    }
};

// ============================================================================
// LVC OBJECT DATABASE - Git-like content-addressable storage
// ============================================================================

class LVCObjectDB {
public:
    enum ObjectType { BLOB, TREE, COMMIT, DELTA, TAG };
    
    struct TreeEntry {
        std::string mode;
        std::string type;
        std::string hash;
        std::string name;
    };
    
    struct CommitData {
        std::string tree;
        std::string parent;
        std::string author;
        std::string committer;
        std::string timestamp;
        std::string version;
        std::string message;
    };

private:
    std::string objectsDir;
    std::map<std::string, std::string> cache;
    
    std::string getObjectPath(const std::string& hash) {
        return objectsDir + "/" + hash.substr(0, 2) + "/" + hash.substr(2);
    }

public:
    LVCObjectDB(const std::string& dir) : objectsDir(dir) {}
    
    // Store object and return hash
    std::string store(const std::string& content, ObjectType type) {
        std::string typeStr;
        switch (type) {
            case BLOB: typeStr = "blob"; break;
            case TREE: typeStr = "tree"; break;
            case COMMIT: typeStr = "commit"; break;
            case DELTA: typeStr = "delta"; break;
            case TAG: typeStr = "tag"; break;
        }
        
        std::string header = typeStr + " " + std::to_string(content.size()) + '\0';
        std::string full = header + content;
        std::string hash = SHA256::hash(full);
        
        std::string path = getObjectPath(hash);
        if (!fs::exists(path)) {
            fs::create_directories(fs::path(path).parent_path());
            std::ofstream f(path, std::ios::binary);
            f << full;
        }
        
        return hash;
    }
    
    // Retrieve object content
    std::string get(const std::string& hash) {
        if (hash.size() < 3) return "";
        
        // Check cache
        auto it = cache.find(hash);
        if (it != cache.end()) return it->second;
        
        std::string path = getObjectPath(hash);
        std::ifstream f(path, std::ios::binary);
        if (!f) return "";
        
        std::ostringstream oss;
        oss << f.rdbuf();
        std::string full = oss.str();
        
        size_t nullPos = full.find('\0');
        std::string content = (nullPos != std::string::npos) ? full.substr(nullPos + 1) : full;
        
        cache[hash] = content;
        return content;
    }
    
    // Get object type
    ObjectType getType(const std::string& hash) {
        std::string path = getObjectPath(hash);
        std::ifstream f(path, std::ios::binary);
        if (!f) return BLOB;
        
        char buf[10];
        f.read(buf, 10);
        std::string header(buf, f.gcount());
        
        if (header.find("tree") == 0) return TREE;
        if (header.find("commit") == 0) return COMMIT;
        if (header.find("delta") == 0) return DELTA;
        if (header.find("tag") == 0) return TAG;
        return BLOB;
    }
    
    // Store blob with delta compression
    std::string storeBlob(const std::string& content, const std::string& baseHash = "") {
        if (baseHash.empty()) {
            return store(content, BLOB);
        }
        
        std::string base = getBlob(baseHash);
        auto delta = DeltaCompression::createDelta(base, content);
        double ratio = DeltaCompression::compressionRatio(content, delta);
        
        if (ratio < 0.8) {
            std::string deltaData = "base:" + baseHash + "\n" + DeltaCompression::serialize(delta);
            return store(deltaData, DELTA);
        }
        
        return store(content, BLOB);
    }
    
    // Get blob, reconstructing from deltas if needed
    std::string getBlob(const std::string& hash, int depth = 0) {
        if (depth > (int)LVCConfig::MAX_DELTA_CHAIN) {
            return "";  // Prevent infinite loops
        }
        
        std::string content = get(hash);
        
        if (content.find("base:") == 0) {
            size_t nl = content.find('\n');
            std::string baseHash = content.substr(5, nl - 5);
            std::string deltaData = content.substr(nl + 1);
            
            std::string base = getBlob(baseHash, depth + 1);
            auto delta = DeltaCompression::deserialize(deltaData);
            return DeltaCompression::applyDelta(base, delta);
        }
        
        return content;
    }
    
    // Create tree object
    std::string createTree(const std::vector<TreeEntry>& entries) {
        std::ostringstream oss;
        for (const auto& e : entries) {
            oss << e.mode << " " << e.type << " " << e.hash << "\t" << e.name << "\n";
        }
        return store(oss.str(), TREE);
    }
    
    // Parse tree object
    std::vector<TreeEntry> parseTree(const std::string& hash) {
        std::vector<TreeEntry> entries;
        std::string content = get(hash);
        
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            
            size_t sp1 = line.find(' ');
            size_t sp2 = line.find(' ', sp1 + 1);
            size_t tab = line.find('\t');
            
            if (sp1 != std::string::npos && sp2 != std::string::npos && tab != std::string::npos) {
                TreeEntry e;
                e.mode = line.substr(0, sp1);
                e.type = line.substr(sp1 + 1, sp2 - sp1 - 1);
                e.hash = line.substr(sp2 + 1, tab - sp2 - 1);
                e.name = line.substr(tab + 1);
                entries.push_back(e);
            }
        }
        return entries;
    }
    
    // Create commit object
    std::string createCommit(const CommitData& data) {
        std::ostringstream oss;
        oss << "tree " << data.tree << "\n";
        if (!data.parent.empty()) oss << "parent " << data.parent << "\n";
        oss << "author " << data.author << " " << data.timestamp << "\n";
        oss << "committer " << data.committer << " " << data.timestamp << "\n";
        oss << "version " << data.version << "\n";
        oss << "\n" << data.message;
        return store(oss.str(), COMMIT);
    }
    
    // Parse commit object
    CommitData parseCommit(const std::string& hash) {
        CommitData data;
        std::string content = get(hash);
        
        std::istringstream iss(content);
        std::string line;
        bool inMessage = false;
        
        while (std::getline(iss, line)) {
            if (inMessage) {
                data.message += line + "\n";
            } else if (line.empty()) {
                inMessage = true;
            } else if (line.find("tree ") == 0) {
                data.tree = line.substr(5);
            } else if (line.find("parent ") == 0) {
                data.parent = line.substr(7);
            } else if (line.find("author ") == 0) {
                size_t lastSpace = line.rfind(' ');
                data.author = line.substr(7, lastSpace - 7);
                data.timestamp = line.substr(lastSpace + 1);
            } else if (line.find("version ") == 0) {
                data.version = line.substr(8);
            }
        }
        
        return data;
    }
    
    bool exists(const std::string& hash) {
        return fs::exists(getObjectPath(hash));
    }
};

// ============================================================================
// LVC - Main Version Control Class
// ============================================================================

class LVC {
private:
    std::string repoPath;
    std::string lvcDir;
    std::unique_ptr<LVCObjectDB> db;
    
    // Paths
    std::string indexFile;
    std::string headFile;
    std::string configFile;
    std::string refsDir;
    std::string branchesDir;
    std::string tagsDir;
    std::string stashDir;
    std::string logFile;
    
    // State
    std::map<std::string, std::string> stagedFiles;  // path -> hash
    std::string currentBranch;
    std::string headCommit;
    
    // Console output
    HANDLE hConsole;
    
    void setColor(WORD color) {
        SetConsoleTextAttribute(hConsole, color);
    }
    
    void resetColor() {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    
    void printSuccess(const std::string& msg) {
        setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << msg << std::endl;
        resetColor();
    }
    
    void printError(const std::string& msg) {
        setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cerr << "error: " << msg << std::endl;
        resetColor();
    }
    
    void printWarning(const std::string& msg) {
        setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cerr << "warning: " << msg << std::endl;
        resetColor();
    }
    
    void printInfo(const std::string& msg) {
        setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << msg << std::endl;
        resetColor();
    }
    
    // File utilities
    std::string readFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return "";
        std::ostringstream oss;
        oss << f.rdbuf();
        return oss.str();
    }
    
    void writeFile(const std::string& path, const std::string& content) {
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream f(path, std::ios::binary);
        f << content;
    }
    
    std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S %z");
        return oss.str();
    }
    
    std::string getUser() {
        char buf[256];
        DWORD size = sizeof(buf);
        GetUserNameA(buf, &size);
        return std::string(buf);
    }
    
    // Index management
    void loadIndex() {
        stagedFiles.clear();
        std::ifstream f(indexFile);
        std::string line;
        while (std::getline(f, line)) {
            size_t sp = line.find(' ');
            if (sp != std::string::npos) {
                stagedFiles[line.substr(sp + 1)] = line.substr(0, sp);
            }
        }
    }
    
    void saveIndex() {
        std::ofstream f(indexFile);
        for (const auto& [path, hash] : stagedFiles) {
            f << hash << " " << path << "\n";
        }
    }
    
    // Reference management
    void loadHead() {
        std::string content = readFile(headFile);
        if (content.find("ref: ") == 0) {
            size_t nl = content.find('\n');
            std::string ref = content.substr(5, nl - 5);
            currentBranch = fs::path(ref).filename().string();
            headCommit = readFile(lvcDir + "/" + ref);
        } else {
            currentBranch = "";
            headCommit = content;
        }
        // Trim
        while (!headCommit.empty() && (headCommit.back() == '\n' || headCommit.back() == '\r')) {
            headCommit.pop_back();
        }
    }
    
    void saveHead(const std::string& commit, const std::string& branch = "") {
        if (!branch.empty()) {
            writeFile(headFile, "ref: refs/heads/" + branch + "\n");
            writeFile(branchesDir + "/" + branch, commit + "\n");
        } else {
            writeFile(headFile, commit + "\n");
        }
        headCommit = commit;
        currentBranch = branch;
    }
    
    std::string getRef(const std::string& name) {
        // Check branches
        std::string content = readFile(branchesDir + "/" + name);
        if (!content.empty()) {
            while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
                content.pop_back();
            }
            return content;
        }
        // Check tags
        content = readFile(tagsDir + "/" + name);
        if (!content.empty()) {
            while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
                content.pop_back();
            }
            return content;
        }
        return "";
    }
    
    // Get tree from commit
    std::map<std::string, std::string> getTreeFiles(const std::string& treeHash) {
        std::map<std::string, std::string> files;
        auto entries = db->parseTree(treeHash);
        for (const auto& e : entries) {
            files[e.name] = e.hash;
        }
        return files;
    }

public:
    LVC(const std::string& path = ".") {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        // Use current_path() for "." to avoid Windows path resolution issues
        if (path == ".") {
            repoPath = fs::current_path().string();
        } else {
            repoPath = fs::absolute(path).lexically_normal().string();
        }
        // Remove trailing separator if present
        while (!repoPath.empty() && (repoPath.back() == '\\' || repoPath.back() == '/')) {
            repoPath.pop_back();
        }
        lvcDir = (fs::path(repoPath) / ".lvc").string();
        indexFile = lvcDir + "/index";
        headFile = lvcDir + "/HEAD";
        configFile = lvcDir + "/config";
        refsDir = lvcDir + "/refs";
        branchesDir = refsDir + "/heads";
        tagsDir = refsDir + "/tags";
        stashDir = lvcDir + "/stash";
        logFile = lvcDir + "/logs/HEAD";
        
        if (isInitialized()) {
            db = std::make_unique<LVCObjectDB>(lvcDir + "/objects");
        }
    }
    
    bool isInitialized() { return fs::exists(lvcDir); }
    
    // ========== COMMANDS ==========
    
    void init() {
        if (isInitialized()) {
            printError("repository already initialized in " + repoPath);
            return;
        }
        
        fs::create_directories(lvcDir + "/objects");
        fs::create_directories(branchesDir);
        fs::create_directories(tagsDir);
        fs::create_directories(stashDir);
        fs::create_directories(lvcDir + "/logs");
        
        writeFile(headFile, "ref: refs/heads/main\n");
        writeFile(indexFile, "");
        writeFile(configFile, "[core]\n\trepositoryformatversion = 0\n");
        writeFile(logFile, "");
        
        db = std::make_unique<LVCObjectDB>(lvcDir + "/objects");
        
        printSuccess("Initialized LVC repository in " + repoPath);
        std::cout << "\n  .lvc/\n";
        std::cout << "  ├── objects/   (content-addressable storage)\n";
        std::cout << "  ├── refs/      (branches + tags)\n";
        std::cout << "  ├── logs/      (reflog)\n";
        std::cout << "  ├── index      (staging area)\n";
        std::cout << "  ├── HEAD       (current branch)\n";
        std::cout << "  └── config     (repository config)\n";
    }
    
    void add(const std::vector<std::string>& paths) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadIndex();
        loadHead();
        
        std::map<std::string, std::string> prevTree;
        if (!headCommit.empty()) {
            auto c = db->parseCommit(headCommit);
            prevTree = getTreeFiles(c.tree);
        }
        
        bool addAll = std::find(paths.begin(), paths.end(), ".") != paths.end() ||
                      std::find(paths.begin(), paths.end(), "-A") != paths.end();
        
        int added = 0;
        
        auto addFile = [&](const fs::path& filePath) {
            if (!fs::is_regular_file(filePath)) return;
            
            std::string rel = fs::relative(filePath, repoPath).string();
            std::replace(rel.begin(), rel.end(), '\\', '/');
            
            if (rel.find(".lvc") == 0) return;
            
            std::string content = readFile(filePath.string());
            std::string prevHash = prevTree.count(rel) ? prevTree[rel] : "";
            std::string hash = db->storeBlob(content, prevHash);
            
            stagedFiles[rel] = hash;
            added++;
        };
        
        if (addAll) {
            for (const auto& e : fs::recursive_directory_iterator(repoPath)) {
                addFile(e.path());
            }
        } else {
            for (const auto& p : paths) {
                fs::path fp = fs::path(repoPath) / p;
                if (!fs::exists(fp)) {
                    printError("pathspec '" + p + "' did not match any files");
                    continue;
                }
                if (fs::is_directory(fp)) {
                    for (const auto& e : fs::recursive_directory_iterator(fp)) {
                        addFile(e.path());
                    }
                } else {
                    addFile(fp);
                }
            }
        }
        
        saveIndex();
        std::cout << "Staged " << added << " files\n";
    }
    
    void commit(const std::string& version, const std::string& message) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadIndex();
        loadHead();
        
        if (stagedFiles.empty()) {
            printError("nothing to commit, working tree clean");
            return;
        }
        
        // Check version exists
        if (!getRef(version).empty() || !readFile(refsDir + "/versions/" + version).empty()) {
            printError("version '" + version + "' already exists");
            return;
        }
        
        // Create tree
        std::vector<LVCObjectDB::TreeEntry> entries;
        for (const auto& [path, hash] : stagedFiles) {
            LVCObjectDB::TreeEntry e;
            e.mode = "100644";
            e.type = "blob";
            e.hash = hash;
            e.name = path;
            entries.push_back(e);
        }
        std::string treeHash = db->createTree(entries);
        
        // Create commit
        LVCObjectDB::CommitData data;
        data.tree = treeHash;
        data.parent = headCommit;
        data.author = getUser();
        data.committer = getUser();
        data.timestamp = getTimestamp();
        data.version = version;
        data.message = message;
        
        std::string commitHash = db->createCommit(data);
        
        // Update refs
        fs::create_directories(refsDir + "/versions");
        writeFile(refsDir + "/versions/" + version, commitHash + "\n");
        
        if (currentBranch.empty()) currentBranch = "main";
        saveHead(commitHash, currentBranch);
        
        // Log
        std::ofstream log(logFile, std::ios::app);
        log << commitHash << " " << version << " " << getTimestamp() << " " << message << "\n";
        
        // Clear index
        stagedFiles.clear();
        saveIndex();
        
        setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "[" << currentBranch << " " << commitHash.substr(0, 8) << "] ";
        resetColor();
        std::cout << version << ": " << message << "\n";
        std::cout << " " << entries.size() << " files committed\n";
    }
    
    void diff(const std::string& ref1 = "", const std::string& ref2 = "") {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadHead();
        
        if (ref1.empty()) {
            // Diff working tree vs HEAD
            if (headCommit.empty()) {
                printError("no commits yet");
                return;
            }
            
            auto c = db->parseCommit(headCommit);
            auto tree = getTreeFiles(c.tree);
            
            int modified = 0, added = 0, deleted = 0;
            
            for (const auto& e : fs::recursive_directory_iterator(repoPath)) {
                if (!fs::is_regular_file(e.path())) continue;
                
                std::string rel = fs::relative(e.path(), repoPath).string();
                std::replace(rel.begin(), rel.end(), '\\', '/');
                if (rel.find(".lvc") == 0) continue;
                
                auto it = tree.find(rel);
                std::string newContent = readFile(e.path().string());
                
                if (it == tree.end()) {
                    setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    std::cout << "A  " << rel << "\n";
                    resetColor();
                    added++;
                } else {
                    std::string oldContent = db->getBlob(it->second);
                    if (oldContent != newContent) {
                        setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                        std::cout << "M  " << rel << "\n";
                        resetColor();
                        MyersDiff::printColorDiff(rel, rel, oldContent, newContent);
                        std::cout << "\n";
                        modified++;
                    }
                    tree.erase(it);
                }
            }
            
            for (const auto& [path, hash] : tree) {
                setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::cout << "D  " << path << "\n";
                resetColor();
                deleted++;
            }
            
            if (modified == 0 && added == 0 && deleted == 0) {
                std::cout << "No changes\n";
            } else {
                std::cout << "\n" << modified << " modified, " << added << " added, " << deleted << " deleted\n";
            }
        } else {
            // Diff between two refs
            std::string hash1 = getRef(ref1);
            if (hash1.empty()) hash1 = readFile(refsDir + "/versions/" + ref1);
            if (hash1.empty()) { printError("unknown ref: " + ref1); return; }
            while (!hash1.empty() && hash1.back() == '\n') hash1.pop_back();
            
            std::string hash2 = ref2.empty() ? headCommit : getRef(ref2);
            if (hash2.empty() && !ref2.empty()) hash2 = readFile(refsDir + "/versions/" + ref2);
            if (hash2.empty()) { printError("unknown ref: " + ref2); return; }
            while (!hash2.empty() && hash2.back() == '\n') hash2.pop_back();
            
            auto t1 = getTreeFiles(db->parseCommit(hash1).tree);
            auto t2 = getTreeFiles(db->parseCommit(hash2).tree);
            
            setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "diff " << ref1 << ".." << (ref2.empty() ? "HEAD" : ref2) << "\n\n";
            resetColor();
            
            std::set<std::string> allPaths;
            for (const auto& [p, h] : t1) allPaths.insert(p);
            for (const auto& [p, h] : t2) allPaths.insert(p);
            
            for (const auto& path : allPaths) {
                auto it1 = t1.find(path), it2 = t2.find(path);
                
                if (it1 == t1.end()) {
                    setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    std::cout << "A  " << path << "\n";
                } else if (it2 == t2.end()) {
                    setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
                    std::cout << "D  " << path << "\n";
                } else if (it1->second != it2->second) {
                    setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    std::cout << "M  " << path << "\n";
                    resetColor();
                    MyersDiff::printColorDiff(path, path, 
                        db->getBlob(it1->second), db->getBlob(it2->second));
                    std::cout << "\n";
                }
            }
            resetColor();
        }
    }
    
    void log(int count = 10) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadHead();
        
        std::string cur = headCommit;
        int shown = 0;
        
        while (!cur.empty() && shown < count) {
            auto c = db->parseCommit(cur);
            
            setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "commit " << cur << "\n";
            resetColor();
            
            if (!currentBranch.empty() && shown == 0) {
                setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "  (HEAD -> " << currentBranch << ")\n";
                resetColor();
            }
            
            setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << "Version: " << c.version << "\n";
            resetColor();
            std::cout << "Author:  " << c.author << "\n";
            std::cout << "Date:    " << c.timestamp << "\n";
            if (!c.message.empty()) {
                std::cout << "\n    " << c.message << "\n";
            }
            std::cout << "\n";
            
            cur = c.parent;
            shown++;
        }
        
        if (cur.empty() && shown == 0) {
            std::cout << "No commits yet\n";
        }
    }
    
    void status() {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadIndex();
        loadHead();
        
        std::cout << "On branch " << (currentBranch.empty() ? "(detached)" : currentBranch) << "\n";
        
        if (!headCommit.empty()) {
            auto c = db->parseCommit(headCommit);
            std::cout << "Current version: " << c.version << "\n";
        } else {
            std::cout << "\nNo commits yet\n";
        }
        
        // Get files from last commit (if any)
        std::map<std::string, std::string> committedFiles;
        if (!headCommit.empty()) {
            auto c = db->parseCommit(headCommit);
            committedFiles = getTreeFiles(c.tree);
        }
        
        // Scan working directory for changes
        std::vector<std::string> untracked;
        std::vector<std::string> modified;
        std::vector<std::string> deleted;
        std::set<std::string> seen;
        
        for (const auto& e : fs::recursive_directory_iterator(repoPath)) {
            if (!fs::is_regular_file(e.path())) continue;
            
            std::string rel = fs::relative(e.path(), repoPath).string();
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (rel.find(".lvc") == 0) continue;
            
            seen.insert(rel);
            
            auto committedIt = committedFiles.find(rel);
            auto stagedIt = stagedFiles.find(rel);
            
            if (committedIt == committedFiles.end() && stagedIt == stagedFiles.end()) {
                // Not in commit and not staged = untracked
                untracked.push_back(rel);
            } else if (committedIt != committedFiles.end()) {
                // Check if modified since last commit
                std::string currentContent = readFile(e.path().string());
                std::string committedContent = db->getBlob(committedIt->second);
                if (currentContent != committedContent && stagedIt == stagedFiles.end()) {
                    modified.push_back(rel);
                }
            }
        }
        
        // Check for deleted files
        for (const auto& [path, hash] : committedFiles) {
            if (seen.find(path) == seen.end()) {
                deleted.push_back(path);
            }
        }
        
        // Show staged files
        if (!stagedFiles.empty()) {
            std::cout << "\nChanges to be committed:\n";
            setColor(FOREGROUND_GREEN);
            for (const auto& [path, hash] : stagedFiles) {
                std::cout << "  new file:   " << path << "\n";
            }
            resetColor();
        }
        
        // Show modified files (not staged)
        if (!modified.empty()) {
            std::cout << "\nChanges not staged for commit:\n";
            setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
            for (const auto& path : modified) {
                std::cout << "  modified:   " << path << "\n";
            }
            resetColor();
            std::cout << "  (use \"lvc add <file>...\" to stage)\n";
        }
        
        // Show deleted files
        if (!deleted.empty()) {
            std::cout << "\nDeleted files:\n";
            setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
            for (const auto& path : deleted) {
                std::cout << "  deleted:    " << path << "\n";
            }
            resetColor();
        }
        
        // Show untracked files
        if (!untracked.empty()) {
            std::cout << "\nUntracked files:\n";
            setColor(FOREGROUND_RED);
            for (const auto& path : untracked) {
                std::cout << "  " << path << "\n";
            }
            resetColor();
            std::cout << "  (use \"lvc add <file>...\" to include in commit)\n";
        }
        
        if (stagedFiles.empty() && modified.empty() && deleted.empty() && untracked.empty()) {
            std::cout << "\nNothing to commit, working tree clean\n";
        }
    }
    
    void rebuild(const std::string& version) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        std::string hash = readFile(refsDir + "/versions/" + version);
        if (hash.empty()) hash = getRef(version);
        if (hash.empty()) { printError("version not found: " + version); return; }
        while (!hash.empty() && hash.back() == '\n') hash.pop_back();
        
        std::cout << "Restore working directory to version " << version << "?\n";
        std::cout << "This will overwrite local changes. Continue? (yes/no): ";
        
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "yes" && resp != "y") {
            std::cout << "Cancelled\n";
            return;
        }
        
        auto c = db->parseCommit(hash);
        auto tree = getTreeFiles(c.tree);
        
        int restored = 0;
        for (const auto& [path, blobHash] : tree) {
            std::string content = db->getBlob(blobHash);
            writeFile((fs::path(repoPath) / path).string(), content);
            restored++;
        }
        
        saveHead(hash, currentBranch);
        stagedFiles.clear();
        saveIndex();
        
        printSuccess("Restored " + std::to_string(restored) + " files to version " + version);
    }
    
    void versions() {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadHead();
        std::string curVer;
        if (!headCommit.empty()) {
            curVer = db->parseCommit(headCommit).version;
        }
        
        std::string verDir = refsDir + "/versions";
        if (!fs::exists(verDir)) {
            std::cout << "No versions yet\n";
            std::cout << "  (use \"lvc add .\" then \"lvc commit -v <version> -m <message>\" to create first version)\n";
            return;
        }
        
        // Collect versions with timestamps for sorting
        std::vector<std::tuple<std::string, std::string, std::string, std::string>> versionList; // version, date, author, message
        
        for (const auto& e : fs::directory_iterator(verDir)) {
            std::string v = e.path().filename().string();
            std::string hash = readFile(e.path().string());
            while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r')) hash.pop_back();
            
            if (!hash.empty() && db->exists(hash)) {
                auto c = db->parseCommit(hash);
                versionList.push_back({v, c.timestamp, c.author, c.message});
            } else {
                versionList.push_back({v, "", "", ""});
            }
        }
        
        // Sort by timestamp descending (newest first)
        std::sort(versionList.begin(), versionList.end(), 
            [](const auto& a, const auto& b) { return std::get<1>(a) > std::get<1>(b); });
        
        setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Versions (" << versionList.size() << " total):\n\n";
        resetColor();
        
        for (const auto& [v, date, author, msg] : versionList) {
            if (v == curVer) {
                setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "* ";
            } else {
                std::cout << "  ";
            }
            
            setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << std::setw(12) << std::left << v;
            resetColor();
            
            if (!date.empty()) {
                std::cout << "  " << date;
            }
            if (!msg.empty()) {
                std::cout << "  " << msg;
            }
            
            if (v == curVer) {
                setColor(FOREGROUND_GREEN);
                std::cout << " (current)";
            }
            resetColor();
            std::cout << "\n";
        }
    }
    
    void show(const std::string& version) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        std::string hash = readFile(refsDir + "/versions/" + version);
        if (hash.empty()) hash = getRef(version);
        if (hash.empty()) { printError("unknown version: " + version); return; }
        while (!hash.empty() && hash.back() == '\n') hash.pop_back();
        
        auto c = db->parseCommit(hash);
        auto tree = getTreeFiles(c.tree);
        
        setColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "commit " << hash << "\n";
        resetColor();
        setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        std::cout << "Version: " << c.version << "\n";
        resetColor();
        std::cout << "Author:  " << c.author << "\n";
        std::cout << "Date:    " << c.timestamp << "\n";
        if (!c.message.empty()) {
            std::cout << "\n    " << c.message << "\n";
        }
        std::cout << "\nFiles: " << tree.size() << "\n";
        for (const auto& [path, h] : tree) {
            std::cout << "  " << path << "\n";
        }
    }
    
    void branch(const std::string& name = "", bool del = false) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadHead();
        
        if (name.empty()) {
            // List branches
            if (!fs::exists(branchesDir)) {
                std::cout << "No branches\n";
                return;
            }
            for (const auto& e : fs::directory_iterator(branchesDir)) {
                std::string b = e.path().filename().string();
                if (b == currentBranch) {
                    setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                    std::cout << "* " << b << "\n";
                    resetColor();
                } else {
                    std::cout << "  " << b << "\n";
                }
            }
        } else if (del) {
            // Delete branch
            if (name == currentBranch) {
                printError("cannot delete current branch");
                return;
            }
            fs::remove(branchesDir + "/" + name);
            std::cout << "Deleted branch " << name << "\n";
        } else {
            // Create branch
            if (headCommit.empty()) {
                printError("cannot create branch without commits");
                return;
            }
            writeFile(branchesDir + "/" + name, headCommit + "\n");
            std::cout << "Created branch " << name << " at " << headCommit.substr(0, 8) << "\n";
        }
    }
    
    void checkout(const std::string& target) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        // Check if branch
        std::string hash = readFile(branchesDir + "/" + target);
        bool isBranch = !hash.empty();
        
        if (hash.empty()) {
            hash = readFile(refsDir + "/versions/" + target);
        }
        if (hash.empty()) {
            printError("unknown branch or version: " + target);
            return;
        }
        
        while (!hash.empty() && hash.back() == '\n') hash.pop_back();
        
        auto c = db->parseCommit(hash);
        auto tree = getTreeFiles(c.tree);
        
        for (const auto& [path, blobHash] : tree) {
            std::string content = db->getBlob(blobHash);
            writeFile((fs::path(repoPath) / path).string(), content);
        }
        
        if (isBranch) {
            saveHead(hash, target);
            std::cout << "Switched to branch '" << target << "'\n";
        } else {
            writeFile(headFile, hash + "\n");
            headCommit = hash;
            currentBranch = "";
            std::cout << "HEAD is now at " << hash.substr(0, 8) << " " << c.version << "\n";
        }
    }
    
    void blame(const std::string& filePath) {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadHead();
        if (headCommit.empty()) { printError("no commits"); return; }
        
        // Get file content
        std::string fullPath = (fs::path(repoPath) / filePath).string();
        std::string content = readFile(fullPath);
        if (content.empty()) { printError("file not found: " + filePath); return; }
        
        auto lines = [](const std::string& s) {
            std::vector<std::string> v;
            std::istringstream iss(s);
            std::string line;
            while (std::getline(iss, line)) v.push_back(line);
            return v;
        };
        
        auto currentLines = lines(content);
        std::vector<std::string> blame(currentLines.size(), "");
        
        // Walk commits backwards
        std::string cur = headCommit;
        std::string prevContent;
        
        while (!cur.empty()) {
            auto c = db->parseCommit(cur);
            auto tree = getTreeFiles(c.tree);
            
            std::string rel = filePath;
            std::replace(rel.begin(), rel.end(), '\\', '/');
            
            if (tree.count(rel)) {
                std::string thisContent = db->getBlob(tree[rel]);
                auto thisLines = lines(thisContent);
                
                // Match lines
                for (size_t i = 0; i < currentLines.size(); i++) {
                    if (blame[i].empty()) {
                        for (size_t j = 0; j < thisLines.size(); j++) {
                            if (currentLines[i] == thisLines[j]) {
                                blame[i] = cur.substr(0, 8) + " " + c.version;
                                break;
                            }
                        }
                    }
                }
            }
            
            cur = c.parent;
        }
        
        // Print
        for (size_t i = 0; i < currentLines.size(); i++) {
            setColor(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::cout << std::setw(40) << std::left << (blame[i].empty() ? "????????" : blame[i]);
            resetColor();
            std::cout << " " << (i + 1) << ") " << currentLines[i] << "\n";
        }
    }
    
    void stash(const std::string& action = "push") {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadIndex();
        
        if (action == "push" || action == "save") {
            if (stagedFiles.empty()) {
                printError("no changes to stash");
                return;
            }
            
            // Save staged files
            std::string stashId = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            std::ostringstream oss;
            for (const auto& [path, hash] : stagedFiles) {
                oss << hash << " " << path << "\n";
            }
            writeFile(stashDir + "/" + stashId, oss.str());
            
            stagedFiles.clear();
            saveIndex();
            
            printSuccess("Saved working directory");
        } else if (action == "pop" || action == "apply") {
            // Get latest stash
            std::string latest;
            for (const auto& e : fs::directory_iterator(stashDir)) {
                std::string n = e.path().filename().string();
                if (latest.empty() || n > latest) latest = n;
            }
            
            if (latest.empty()) {
                printError("no stash entries");
                return;
            }
            
            std::string content = readFile(stashDir + "/" + latest);
            std::istringstream iss(content);
            std::string line;
            while (std::getline(iss, line)) {
                size_t sp = line.find(' ');
                if (sp != std::string::npos) {
                    stagedFiles[line.substr(sp + 1)] = line.substr(0, sp);
                }
            }
            saveIndex();
            
            if (action == "pop") {
                fs::remove(stashDir + "/" + latest);
            }
            
            printSuccess("Applied stash");
        } else if (action == "list") {
            int count = 0;
            for (const auto& e : fs::directory_iterator(stashDir)) {
                std::cout << "stash@{" << count++ << "}\n";
            }
            if (count == 0) std::cout << "No stash entries\n";
        } else if (action == "clear") {
            for (const auto& e : fs::directory_iterator(stashDir)) {
                fs::remove(e.path());
            }
            printSuccess("Cleared all stash entries");
        }
    }
    
    void reset(const std::string& mode = "--soft") {
        if (!isInitialized()) { printError("not an lvc repository"); return; }
        
        loadIndex();
        
        if (mode == "--soft") {
            // Keep staged files
            std::cout << "Index preserved\n";
        } else if (mode == "--hard") {
            stagedFiles.clear();
            saveIndex();
            printSuccess("Index cleared");
        }
    }
};

#endif // LVC_HPP
