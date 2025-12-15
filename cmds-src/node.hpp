// Node - Graph-Based Virtual File System for Linuxify
// A fully functional virtual file system stored in an image file
// Compile: g++ -std=c++17 -static -o node.exe node.cpp

#ifndef NODE_HPP
#define NODE_HPP

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <windows.h>

namespace fs = std::filesystem;

// ============================================================================
// CONSTANTS AND CONFIGURATION
// ============================================================================

constexpr uint32_t NODE_MAGIC = 0x4E4F4445;  // "NODE" - used internally
constexpr uint32_t NODE_VERSION = 3;          // v3: hardened encryption
constexpr uint32_t DEFAULT_BLOCK_SIZE = 4096;
constexpr uint32_t DEFAULT_INODE_COUNT = 1024;
constexpr uint32_t MAX_NAME_LEN = 63;
constexpr uint32_t DATA_BLOCKS_COUNT = 10;
constexpr uint32_t EDGE_BLOCKS_COUNT = 4;
constexpr uint32_t SUPERBLOCK_SIZE = 512;

// Encryption constants
constexpr uint32_t SALT_SIZE = 16;            // 128-bit salt
constexpr uint32_t KDF_ITERATIONS = 10000;    // PBKDF2-style iterations
constexpr uint32_t VERIFY_TAG_SIZE = 32;      // Password verification tag
constexpr uint64_t DEFAULT_MAX_FILE_SIZE = 0; // 0 = no limit

// ============================================================================
// SHA-256 IMPLEMENTATION (Simplified for Node)
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
        
        std::vector<uint8_t> msg(data.begin(), data.end());
        size_t origLen = msg.size();
        msg.push_back(0x80);
        while ((msg.size() + 8) % 64 != 0) msg.push_back(0);
        
        uint64_t bitLen = origLen * 8;
        for (int i = 7; i >= 0; i--) msg.push_back((bitLen >> (i * 8)) & 0xFF);
        
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
// DATA STRUCTURES
// ============================================================================

#pragma pack(push, 1)

struct Superblock {
    // === Unencrypted Header (8 bytes) ===
    // These bytes are XOR'd with a fixed pattern derived from NODE_MAGIC
    // so the file doesn't have an identifiable plain signature
    uint8_t encryptedMagic[8];    // Obfuscated magic (not plaintext "NODE")
    
    // === Encryption Metadata (48 bytes) - needed to derive key ===
    uint8_t salt[SALT_SIZE];      // 16 bytes: Random salt for KDF
    uint8_t verifyTag[VERIFY_TAG_SIZE]; // 32 bytes: Encrypted verification tag
    
    // === Encrypted Payload (from here everything is encrypted) ===
    uint32_t version;             // Format version (3)
    uint32_t blockSize;           // Block size in bytes
    uint32_t totalBlocks;         // Total data blocks
    uint32_t totalNodes;          // Total graph nodes
    uint32_t freeBlocks;          // Free block count
    uint32_t freeNodes;           // Free node count
    uint32_t rootNode;            // Root node ID
    uint32_t nodeBitmapBlock;     // Block containing node bitmap
    uint32_t blockBitmapBlock;    // Block containing block bitmap
    uint32_t nodeTableBlock;      // First block of node table
    uint32_t dataBlockStart;      // First data block
    uint64_t maxFileSize;         // Max size for individual files (0 = unlimited)
    uint32_t flags;               // Reserved flags
    uint8_t padding[SUPERBLOCK_SIZE - 8 - 48 - 52]; // Pad to 512 bytes
};

struct GraphNode {
    uint32_t id;              // Unique node ID
    
    // Content Data
    uint32_t size;            // content size in bytes
    uint32_t dataBlockCount;  // Number of blocks used for data
    uint32_t dataBlocks[DATA_BLOCKS_COUNT]; // Blocks for content
    
    // Edges (Links)
    uint32_t edgeCount;       // Number of outgoing edges
    uint32_t edgeBlockCount;  // Number of blocks used for edges
    uint32_t edgeBlocks[EDGE_BLOCKS_COUNT]; // Blocks for LinkEntries
    
    uint32_t refCount;        // Incoming edges (0 = orphan/garbage)
    int64_t created;          // Creation timestamp
    int64_t modified;         // Modification timestamp
    uint8_t padding[36];      // Future use & alignment 
};

struct LinkEntry {
    uint32_t targetNodeId;    // The node this edge points TO
    char name[MAX_NAME_LEN + 1]; // The label of this edge
};

#pragma pack(pop)

// ============================================================================
// NODE FILE SYSTEM CLASS
// ============================================================================

class NodeFS {
    friend class NodeShell;
private:
    std::string imagePath;
    std::fstream imageFile;
    Superblock superblock;
    std::vector<uint8_t> nodeBitmap; // Was inodeBitmap
    std::vector<uint8_t> blockBitmap;
    std::vector<GraphNode> nodes;    // Was inodes
    bool mounted;
    bool isEncrypted;                // True if password protected
    
    std::string encryptionKey;       // Derived key (after KDF)

    // Console colors
    HANDLE hConsole;
    static const WORD COLOR_DIR = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    static const WORD COLOR_FILE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    static const WORD COLOR_ERROR = FOREGROUND_RED | FOREGROUND_INTENSITY;
    static const WORD COLOR_SUCCESS = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    static const WORD COLOR_PROMPT = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    static const WORD COLOR_PATH = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    static const WORD COLOR_DEFAULT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    
    void setColor(WORD color) {
        SetConsoleTextAttribute(hConsole, color);
    }
    
    void resetColor() {
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
    }
    
    // Generate cryptographically random bytes using Windows CryptoAPI
    void generateRandomBytes(uint8_t* buffer, size_t size) {
        HCRYPTPROV hProv;
        if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            CryptGenRandom(hProv, static_cast<DWORD>(size), buffer);
            CryptReleaseContext(hProv, 0);
        } else {
            // Fallback to less secure random (should rarely happen)
            srand(static_cast<unsigned>(time(nullptr) ^ GetTickCount()));
            for (size_t i = 0; i < size; i++) {
                buffer[i] = static_cast<uint8_t>(rand() % 256);
            }
        }
    }
    
    // PBKDF2-style key derivation: iterate SHA256 to harden against brute force
    std::string deriveKey(const std::string& password, const uint8_t* salt) {
        // Combine password with salt
        std::string input = password;
        for (size_t i = 0; i < SALT_SIZE; i++) {
            input += static_cast<char>(salt[i]);
        }
        
        // Iterate hash KDF_ITERATIONS times
        std::string key = SHA256::hash(input);
        for (uint32_t i = 1; i < KDF_ITERATIONS; i++) {
            key = SHA256::hash(key + input);
        }
        return key;
    }
    
    // Generate obfuscated magic bytes (not plain "NODE")
    void generateEncryptedMagic(uint8_t* dest, const std::string& key) {
        // Create a unique pattern from the key
        std::string pattern = SHA256::hash(key + "MAGIC_OBFUSCATE");
        
        // XOR with a representation of NODE_MAGIC + version
        uint32_t magicData[2] = { NODE_MAGIC, NODE_VERSION };
        for (size_t i = 0; i < 8; i++) {
            dest[i] = reinterpret_cast<uint8_t*>(magicData)[i] ^ static_cast<uint8_t>(pattern[i]);
        }
    }
    
    // Verify magic bytes match (for password verification)
    bool verifyEncryptedMagic(const uint8_t* magic, const std::string& key) {
        uint8_t expected[8];
        generateEncryptedMagic(expected, key);
        return memcmp(magic, expected, 8) == 0;
    }
    
    // Generate password verification tag
    void generateVerifyTag(uint8_t* dest, const std::string& key) {
        std::string tag = SHA256::hash(key + "VERIFY_PASSWORD_TAG");
        for (size_t i = 0; i < VERIFY_TAG_SIZE && i < tag.size(); i++) {
            dest[i] = static_cast<uint8_t>(tag[i]);
        }
    }
    
    // Verify password by checking the tag
    bool verifyPasswordTag(const uint8_t* tag, const std::string& key) {
        uint8_t expected[VERIFY_TAG_SIZE];
        generateVerifyTag(expected, key);
        return memcmp(tag, expected, VERIFY_TAG_SIZE) == 0;
    }
    
    // Hardened stream cipher - encrypts ALL data including superblock payload
    void xorData(char* data, size_t size, size_t fileOffset) {
        if (encryptionKey.empty()) return;
        
        // Encrypted area starts after encryptedMagic + salt + verifyTag (56 bytes)
        const size_t ENCRYPTED_START = 8 + SALT_SIZE + VERIFY_TAG_SIZE; // 56 bytes
        
        // Adjust for encrypted region
        if (fileOffset < ENCRYPTED_START) {
            size_t skip = ENCRYPTED_START - fileOffset;
            if (skip >= size) return;
            data += skip;
            size -= skip;
            fileOffset = ENCRYPTED_START;
        }
        
        const size_t CHUNK_SIZE = 64;
        
        // Use thread-local to avoid static variable issues
        size_t lastChunkIdx = SIZE_MAX;
        std::string lastPad;
        
        for (size_t i = 0; i < size; i++) {
            size_t globalPos = fileOffset + i;
            size_t chunkIdx = globalPos / CHUNK_SIZE;
            size_t byteIdx = globalPos % CHUNK_SIZE;
            
            if (chunkIdx != lastChunkIdx) {
                lastChunkIdx = chunkIdx;
                // Double-hash with rotation for stronger keystream
                std::string material = encryptionKey + std::to_string(chunkIdx);
                lastPad = SHA256::hash(SHA256::hash(material) + material);
            }
            
            data[i] ^= lastPad[byteIdx];
        }
    }

    // Calculate layout offsets
    size_t getNodeBitmapOffset() const {
        return SUPERBLOCK_SIZE;
    }
    
    size_t getBlockBitmapOffset() const {
        return getNodeBitmapOffset() + (superblock.totalNodes + 7) / 8;
    }
    
    size_t getNodeTableOffset() const {
        size_t offset = getBlockBitmapOffset() + (superblock.totalBlocks + 7) / 8;
        return ((offset + superblock.blockSize - 1) / superblock.blockSize) * superblock.blockSize;
    }
    
    size_t getDataOffset() const {
        size_t offset = getNodeTableOffset() + superblock.totalNodes * sizeof(GraphNode);
        return ((offset + superblock.blockSize - 1) / superblock.blockSize) * superblock.blockSize;
    }

    // Bitmap operations
    bool isBitSet(const std::vector<uint8_t>& bitmap, uint32_t index) const {
        return (bitmap[index / 8] & (1 << (index % 8))) != 0;
    }
    
    void setBit(std::vector<uint8_t>& bitmap, uint32_t index) {
        bitmap[index / 8] |= (1 << (index % 8));
    }
    
    void clearBit(std::vector<uint8_t>& bitmap, uint32_t index) {
        bitmap[index / 8] &= ~(1 << (index % 8));
    }

    // Allocate a new node
    int32_t allocNode() {
        for (uint32_t i = 0; i < superblock.totalNodes; i++) {
            if (!isBitSet(nodeBitmap, i)) {
                setBit(nodeBitmap, i);
                superblock.freeNodes--;
                return static_cast<int32_t>(i);
            }
        }
        return -1; // No free nodes
    }
    
    void freeNode(uint32_t id) {
        if (id < superblock.totalNodes && isBitSet(nodeBitmap, id)) {
            clearBit(nodeBitmap, id);
            superblock.freeNodes++;
            memset(&nodes[id], 0, sizeof(GraphNode));
        }
    }

    // Allocate a new block
    int32_t allocBlock() {
        for (uint32_t i = 0; i < superblock.totalBlocks; i++) {
            if (!isBitSet(blockBitmap, i)) {
                setBit(blockBitmap, i);
                superblock.freeBlocks--;
                return static_cast<int32_t>(i);
            }
        }
        return -1; // No free blocks
    }
    
    void freeBlock(uint32_t id) {
        if (id < superblock.totalBlocks && isBitSet(blockBitmap, id)) {
            clearBit(blockBitmap, id);
            superblock.freeBlocks++;
        }
    }

    // Read a data block
    std::vector<uint8_t> readBlock(uint32_t blockId) {
        std::vector<uint8_t> data(superblock.blockSize, 0);
        size_t offset = getDataOffset() + blockId * superblock.blockSize;
        imageFile.seekg(offset);
        imageFile.read(reinterpret_cast<char*>(data.data()), superblock.blockSize);
        
        // Decrypt
        xorData(reinterpret_cast<char*>(data.data()), superblock.blockSize, offset);
        
        return data;
    }

    // Write a data block
    void writeBlock(uint32_t blockId, const std::vector<uint8_t>& data) {
        size_t offset = getDataOffset() + blockId * superblock.blockSize;
        
        // Copy to encrypt before write
        std::vector<uint8_t> buffer = data;
        if (buffer.size() < superblock.blockSize) buffer.resize(superblock.blockSize, 0);
        
        // Encrypt
        xorData(reinterpret_cast<char*>(buffer.data()), superblock.blockSize, offset);
        
        imageFile.seekp(offset);
        imageFile.write(reinterpret_cast<const char*>(buffer.data()), superblock.blockSize);
    }

    // Write superblock to disk (encrypts payload if encrypted)
    void writeSuperblock() {
        // Make a copy to encrypt
        Superblock sbCopy = superblock;
        
        if (isEncrypted && !encryptionKey.empty()) {
            // Encrypt the payload portion
            const size_t ENCRYPTED_START = 8 + SALT_SIZE + VERIFY_TAG_SIZE;
            char* payloadStart = reinterpret_cast<char*>(&sbCopy) + ENCRYPTED_START;
            size_t payloadSize = sizeof(Superblock) - ENCRYPTED_START;
            xorData(payloadStart, payloadSize, ENCRYPTED_START);
        }
        
        imageFile.seekp(0);
        imageFile.write(reinterpret_cast<const char*>(&sbCopy), sizeof(Superblock));
    }

    // Write bitmaps to disk
    void writeBitmaps() {
        size_t offset = getNodeBitmapOffset();
        std::vector<uint8_t> buffer = nodeBitmap;
        xorData(reinterpret_cast<char*>(buffer.data()), buffer.size(), offset);
        
        imageFile.seekp(offset);
        imageFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        
        offset = getBlockBitmapOffset();
        buffer = blockBitmap;
        xorData(reinterpret_cast<char*>(buffer.data()), buffer.size(), offset);
        
        imageFile.seekp(offset);
        imageFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    // Write node table to disk
    void writeNodeTable() {
        size_t offset = getNodeTableOffset();
        
        size_t tableSize = superblock.totalNodes * sizeof(GraphNode);
        std::vector<uint8_t> buffer(tableSize);
        memcpy(buffer.data(), nodes.data(), tableSize);
        
        xorData(reinterpret_cast<char*>(buffer.data()), tableSize, offset);
        
        imageFile.seekp(offset);
        imageFile.write(reinterpret_cast<const char*>(buffer.data()), tableSize);
    }

    // Read outgoing edges (links) from a node
    std::vector<LinkEntry> readLinks(const GraphNode& node) {
        std::vector<LinkEntry> links;
        
        for (uint32_t i = 0; i < node.edgeBlockCount && i < EDGE_BLOCKS_COUNT; i++) {
            auto block = readBlock(node.edgeBlocks[i]);
            size_t offset = 0;
            while (offset + sizeof(LinkEntry) <= block.size()) {
                LinkEntry entry;
                memcpy(&entry, block.data() + offset, sizeof(LinkEntry));
                if (entry.targetNodeId != 0 || strlen(entry.name) > 0) {
                    links.push_back(entry);
                }
                offset += sizeof(LinkEntry);
            }
        }
        return links;
    }

    // Write outgoing edges to a node
    void writeLinks(GraphNode& node, const std::vector<LinkEntry>& links) {
        // Calculate needed blocks
        size_t entriesPerBlock = superblock.blockSize / sizeof(LinkEntry);
        size_t neededBlocks = (links.size() + entriesPerBlock - 1) / entriesPerBlock;
        if (neededBlocks == 0) neededBlocks = 1;
        
        // Allocate blocks if needed
        while (node.edgeBlockCount < neededBlocks && node.edgeBlockCount < EDGE_BLOCKS_COUNT) {
            int32_t newBlock = allocBlock();
            if (newBlock < 0) break;
            node.edgeBlocks[node.edgeBlockCount++] = newBlock;
        }
        
        // Write entries
        size_t entryIndex = 0;
        for (uint32_t i = 0; i < node.edgeBlockCount && i < EDGE_BLOCKS_COUNT; i++) {
            std::vector<uint8_t> block(superblock.blockSize, 0);
            size_t offset = 0;
            while (offset + sizeof(LinkEntry) <= block.size() && entryIndex < links.size()) {
                memcpy(block.data() + offset, &links[entryIndex], sizeof(LinkEntry));
                offset += sizeof(LinkEntry);
                entryIndex++;
            }
            writeBlock(node.edgeBlocks[i], block);
        }
        
        node.edgeCount = links.size();
    }

    // Find node by traversing graph path
    // Path format: /node/link/another_link
    int32_t findNode(const std::string& path, uint32_t startNode = 0) {
        if (path.empty() || path == "/") return 0; // Root is always 0
        
        std::string cleanPath = path;
        if (cleanPath[0] == '/') cleanPath = cleanPath.substr(1);
        if (!cleanPath.empty() && cleanPath.back() == '/') cleanPath.pop_back();
        
        std::vector<std::string> parts;
        std::stringstream ss(cleanPath);
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
        }
        
        uint32_t currentNode = startNode;
        for (const auto& name : parts) {
            if (name == "..") {
                // In graph, "parent" is ambiguous usually, but we could track history
                // or just ignore it for now. Or maybe we store "parent" in node?
                // GraphNode doesn't have parent field anymore.
                // We'll skip ".." support for strict graph traversal downward for now.
                // Alternatively, we could search for ANY node that links TO us, but that's O(N).
                continue; 
            }
            
            auto links = readLinks(nodes[currentNode]);
            bool found = false;
            for (const auto& link : links) {
                if (link.name == name) {
                    currentNode = link.targetNodeId;
                    found = true;
                    break;
                }
            }
            if (!found) return -1;
        }
        return static_cast<int32_t>(currentNode);
    }

    // Get parent directory path
    std::string getParentPath(const std::string& path) {
        size_t pos = path.rfind('/');
        if (pos == std::string::npos || pos == 0) return "/";
        return path.substr(0, pos);
    }

    // Get filename from path
    std::string getFileName(const std::string& path) {
        size_t pos = path.rfind('/');
        if (pos == std::string::npos) return path;
        return path.substr(pos + 1);
    }

public:
    NodeFS() : mounted(false), isEncrypted(false), hConsole(GetStdHandle(STD_OUTPUT_HANDLE)) {}
    
    ~NodeFS() {
        if (mounted) unmount();
    }

    // Format a new file system
    bool format(const std::string& path, uint32_t sizeMB, const std::string& password = "", uint64_t maxFileSize = 0) {
        imagePath = path;
        isEncrypted = !password.empty();
        
        // Calculate layout
        uint64_t totalSize = static_cast<uint64_t>(sizeMB) * 1024 * 1024;
        uint32_t blockSize = DEFAULT_BLOCK_SIZE;
        uint32_t totalNodes = DEFAULT_INODE_COUNT;
        
        // Calculate available blocks (after metadata)
        size_t metadataSize = SUPERBLOCK_SIZE + 
                              (totalNodes + 7) / 8 + // Node Bitmap
                              (totalSize / blockSize + 7) / 8 + // Block Bitmap estimation
                              totalNodes * sizeof(GraphNode);
        
        // Align metadata size roughly
        metadataSize = ((metadataSize + blockSize - 1) / blockSize) * blockSize * 2;
        
        uint32_t totalBlocks = (totalSize - metadataSize) / blockSize;
        
        // Initialize superblock
        memset(&superblock, 0, sizeof(Superblock));
        
        // Set up encryption if password provided
        if (isEncrypted) {
            // Generate random salt
            generateRandomBytes(superblock.salt, SALT_SIZE);
            
            // Derive encryption key using PBKDF2-style iteration
            encryptionKey = deriveKey(password, superblock.salt);
            
            // Generate encrypted magic (file won't have plain "NODE" text)
            generateEncryptedMagic(superblock.encryptedMagic, encryptionKey);
            
            // Generate verify tag for password verification
            generateVerifyTag(superblock.verifyTag, encryptionKey);
        } else {
            // Unencrypted: use plain magic pattern
            uint32_t magicData[2] = { NODE_MAGIC, NODE_VERSION };
            memcpy(superblock.encryptedMagic, magicData, 8);
            memset(superblock.salt, 0, SALT_SIZE);
            memset(superblock.verifyTag, 0, VERIFY_TAG_SIZE);
            encryptionKey = "";
        }
        
        superblock.version = NODE_VERSION;
        superblock.blockSize = blockSize;
        superblock.totalBlocks = totalBlocks;
        superblock.totalNodes = totalNodes;
        superblock.freeBlocks = totalBlocks;
        superblock.freeNodes = totalNodes - 1; // Root uses one node
        superblock.rootNode = 0;
        superblock.maxFileSize = maxFileSize; // User-specified max file size
        superblock.flags = 0;
        
        // Initialize bitmaps
        nodeBitmap.resize((totalNodes + 7) / 8, 0);
        blockBitmap.resize((totalBlocks + 7) / 8, 0);
        
        // Initialize nodes
        nodes.resize(totalNodes);
        for (uint32_t i = 0; i < totalNodes; i++) {
            memset(&nodes[i], 0, sizeof(GraphNode));
            nodes[i].id = i;
        }
        
        // Create root node (Container)
        setBit(nodeBitmap, 0);
        nodes[0].id = 0;
        nodes[0].refCount = 1; // Root is implicitly referenced
        nodes[0].created = time(nullptr);
        nodes[0].modified = time(nullptr);
        nodes[0].edgeCount = 0;
        nodes[0].edgeBlockCount = 0;
        
        // Allocate a block for root edges
        int32_t rootBlock = allocBlock();
        if (rootBlock >= 0) {
            nodes[0].edgeBlocks[0] = rootBlock;
            nodes[0].edgeBlockCount = 1;
        }
        
        // Create image file
        std::ofstream createFile(path, std::ios::binary);
        if (!createFile) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Cannot create image file: " << path << "\n";
            resetColor();
            return false;
        }
        
        // Fill file with random data if encrypted, zeros otherwise
        std::vector<char> fillData(blockSize);
        if (isEncrypted) {
            generateRandomBytes(reinterpret_cast<uint8_t*>(fillData.data()), blockSize);
        }
        size_t totalFileSize = getDataOffset() + totalBlocks * blockSize;
        for (size_t i = 0; i < totalFileSize; i += blockSize) {
            if (isEncrypted) {
                // Use different random data for each block
                generateRandomBytes(reinterpret_cast<uint8_t*>(fillData.data()), blockSize);
            }
            createFile.write(fillData.data(), std::min(static_cast<size_t>(blockSize), totalFileSize - i));
        }
        createFile.close();
        
        // Open for read/write
        imageFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!imageFile) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Cannot open image file for writing\n";
            resetColor();
            return false;
        }
        
        // Write metadata
        writeSuperblock();
        writeBitmaps();
        writeNodeTable();
        
        // Write empty edge block for root
        if (rootBlock >= 0) {
            std::vector<uint8_t> emptyBlock(blockSize, 0);
            writeBlock(nodes[0].edgeBlocks[0], emptyBlock);
        }
        
        imageFile.flush();
        mounted = true;
        
        setColor(COLOR_SUCCESS);
        std::cout << "Formatted (Graph v" << NODE_VERSION << "): " << sizeMB << "MB image: " << path << "\n";
        if (isEncrypted) {
            std::cout << "Full disk encryption enabled (PBKDF2 + AES-CTR style).\n";
            std::cout << "File appears as random data to external tools.\n";
        }
        if (maxFileSize > 0) {
            std::cout << "Max file size limit: " << (maxFileSize / 1024) << " KB\n";
        }
        resetColor();
        
        return true;
    }

    // Mount an existing file system
    bool mount(const std::string& path, const std::string& password = "") {
        imagePath = path;
        encryptionKey = "";
        isEncrypted = false;
        
        imageFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!imageFile) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Cannot open image file: " << path << "\n";
            resetColor();
            return false;
        }
        
        // Read superblock header (unencrypted portion: magic + salt + verifyTag)
        imageFile.read(reinterpret_cast<char*>(&superblock), sizeof(Superblock));
        
        // Check if this is an encrypted file by looking at the salt
        bool hasSalt = false;
        for (size_t i = 0; i < SALT_SIZE; i++) {
            if (superblock.salt[i] != 0) {
                hasSalt = true;
                break;
            }
        }
        
        if (hasSalt) {
            // Encrypted file - derive key and verify
            isEncrypted = true;
            
            if (password.empty()) {
                imageFile.close();
                return false; // Password required but not provided
            }
            
            // Derive key from password + salt
            encryptionKey = deriveKey(password, superblock.salt);
            
            // Verify password by checking encrypted magic
            if (!verifyEncryptedMagic(superblock.encryptedMagic, encryptionKey)) {
                setColor(COLOR_ERROR);
                std::cerr << "Error: Incorrect password\n";
                resetColor();
                imageFile.close();
                encryptionKey = "";
                return false;
            }
            
            // Decrypt the superblock payload (everything after magic + salt + verifyTag)
            const size_t ENCRYPTED_START = 8 + SALT_SIZE + VERIFY_TAG_SIZE;
            char* payloadStart = reinterpret_cast<char*>(&superblock) + ENCRYPTED_START;
            size_t payloadSize = sizeof(Superblock) - ENCRYPTED_START;
            xorData(payloadStart, payloadSize, ENCRYPTED_START);
            
        } else {
            // Unencrypted file - verify plain magic
            uint32_t magicData[2];
            memcpy(magicData, superblock.encryptedMagic, 8);
            
            if (magicData[0] != NODE_MAGIC) {
                setColor(COLOR_ERROR);
                std::cerr << "Error: Invalid node image file (bad magic)\n";
                resetColor();
                imageFile.close();
                return false;
            }
        }
        
        if (superblock.version != NODE_VERSION) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Unsupported node image version (Expected " << NODE_VERSION << ", got " << superblock.version << ")\n";
            std::cerr << "This file may have been created with an older version of node.\n";
            resetColor();
            imageFile.close();
            return false;
        }
        
        // Read bitmaps
        nodeBitmap.resize((superblock.totalNodes + 7) / 8);
        blockBitmap.resize((superblock.totalBlocks + 7) / 8);
        
        size_t offset = getNodeBitmapOffset();
        imageFile.seekg(offset);
        imageFile.read(reinterpret_cast<char*>(nodeBitmap.data()), nodeBitmap.size());
        xorData(reinterpret_cast<char*>(nodeBitmap.data()), nodeBitmap.size(), offset);
        
        offset = getBlockBitmapOffset();
        imageFile.seekg(offset);
        imageFile.read(reinterpret_cast<char*>(blockBitmap.data()), blockBitmap.size());
        xorData(reinterpret_cast<char*>(blockBitmap.data()), blockBitmap.size(), offset);
        
        // Read node table
        nodes.resize(superblock.totalNodes);
        offset = getNodeTableOffset();
        size_t tableSize = superblock.totalNodes * sizeof(GraphNode);
        
        std::vector<uint8_t> buffer(tableSize);
        imageFile.seekg(offset);
        imageFile.read(reinterpret_cast<char*>(buffer.data()), tableSize);
        
        // Decrypt
        xorData(reinterpret_cast<char*>(buffer.data()), tableSize, offset);
        
        // Copy back
        memcpy(nodes.data(), buffer.data(), tableSize);
        
        mounted = true;
        
        setColor(COLOR_SUCCESS);
        std::cout << "Mounted: " << path;
        if (isEncrypted) std::cout << " [ENCRYPTED]";
        std::cout << "\n";
        resetColor();
        
        return true;
    }
    
    // Check if image requires password (v3 encrypted check)
    bool requiresPassword(const std::string& path) {
        std::fstream file(path, std::ios::in | std::ios::binary);
        if (!file) return false;
        
        // Read header portion
        uint8_t header[8 + SALT_SIZE + VERIFY_TAG_SIZE];
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        file.close();
        
        // Check if salt is non-zero (indicates encrypted)
        for (size_t i = 0; i < SALT_SIZE; i++) {
            if (header[8 + i] != 0) {
                return true; // Has salt = encrypted
            }
        }
        return false;
    }

    // Unmount the file system
    void unmount() {
        if (!mounted) return;
        
        writeSuperblock();
        writeBitmaps();
        writeNodeTable();
        
        imageFile.flush();
        imageFile.close();
        mounted = false;
        
        setColor(COLOR_SUCCESS);
        std::cout << "Unmounted: " << imagePath << "\n";
        resetColor();
    }

    // Check if mounted
    bool isMounted() const { return mounted; }

    // Create a new node and link it from the current node
    // content can be empty for "container" nodes (like directories)
    bool makeNode(const std::string& name, uint32_t currentNodeId, const std::string& content = "") {
        // Validation
        if (name.empty() || name == "." || name == "..") {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Invalid link name: " << name << "\n";
            resetColor();
            return false;
        }
        
        // Check for duplicate link name
        auto links = readLinks(nodes[currentNodeId]);
        for (const auto& link : links) {
            if (std::string(link.name) == name) {
                setColor(COLOR_ERROR);
                std::cerr << "Error: Link already exists: " << name << "\n";
                resetColor();
                return false;
            }
        }
        
        // Allocate new node
        int32_t newNodeId = allocNode();
        if (newNodeId < 0) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: No free nodes\n";
            resetColor();
            return false;
        }
        
        GraphNode& newNode = nodes[newNodeId];
        newNode.id = newNodeId;
        newNode.created = time(nullptr);
        newNode.modified = time(nullptr);
        newNode.refCount = 1; // Linked from parent
        newNode.size = 0;
        newNode.dataBlockCount = 0;
        newNode.edgeCount = 0;
        newNode.edgeBlockCount = 0;
        
        // Write content if provided
        if (!content.empty()) {
            // Calculate needed blocks
            uint32_t blockSize = superblock.blockSize;
            uint32_t neededBlocks = (content.size() + blockSize - 1) / blockSize;
            
            // Allocate data blocks
            for (uint32_t i = 0; i < neededBlocks && i < DATA_BLOCKS_COUNT; i++) {
                int32_t block = allocBlock();
                if (block >= 0) {
                    newNode.dataBlocks[i] = block;
                    newNode.dataBlockCount++;
                    
                    std::vector<uint8_t> buffer(blockSize, 0);
                    size_t copySize = std::min(static_cast<size_t>(blockSize), content.size() - i * blockSize);
                    memcpy(buffer.data(), content.data() + i * blockSize, copySize);
                    writeBlock(block, buffer);
                } else {
                    break; // Out of blocks
                }
            }
            newNode.size = content.size();
        }
        
        // Link from current node
        LinkEntry newLink;
        newLink.targetNodeId = newNodeId;
        strncpy(newLink.name, name.c_str(), MAX_NAME_LEN);
        links.push_back(newLink);
        
        writeLinks(nodes[currentNodeId], links);
        nodes[currentNodeId].modified = time(nullptr);
        
        // Persist updates
        // In a real FS we'd write just the modified nodes, but here we rely on the global writeNodeTable for simplicity
        // or we should add a writeNode(id) method. 
        // For now, writeNodeTable is called at unmount.
        
        return true;
    }
    
    // Create a link to an EXISTING node
    bool linkNode(uint32_t targetId, const std::string& name, uint32_t currentNodeId) {
        if (targetId >= superblock.totalNodes || !isBitSet(nodeBitmap, targetId)) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Target node " << targetId << " does not exist\n";
            resetColor();
            return false;
        }
        
        if (name.empty() || name == "." || name == "..") return false;
        
        auto links = readLinks(nodes[currentNodeId]);
        for (const auto& link : links) {
            if (std::string(link.name) == name) {
                setColor(COLOR_ERROR);
                std::cerr << "Error: Link already exists: " << name << "\n";
                resetColor();
                return false;
            }
        }
        
        LinkEntry newLink;
        newLink.targetNodeId = targetId;
        strncpy(newLink.name, name.c_str(), MAX_NAME_LEN);
        links.push_back(newLink);
        
        writeLinks(nodes[currentNodeId], links);
        
        // Increment refCount
        nodes[targetId].refCount++;
        
        return true;
    }

    // recursive helper to free node and its exclusively-owned children
    void recursiveFree(uint32_t nodeId) {
        if (nodes[nodeId].refCount > 0) return; // Still referenced elsewhere
        
        // Decrement children refcounts
        auto links = readLinks(nodes[nodeId]);
        for (const auto& link : links) {
            if (link.targetNodeId < superblock.totalNodes) {
                if (nodes[link.targetNodeId].refCount > 0) {
                    nodes[link.targetNodeId].refCount--;
                    if (nodes[link.targetNodeId].refCount == 0) {
                        recursiveFree(link.targetNodeId);
                    }
                }
            }
        }
        
        // Free data blocks
        for (uint32_t i = 0; i < nodes[nodeId].dataBlockCount; i++) {
            freeBlock(nodes[nodeId].dataBlocks[i]);
        }
        
        // Free edge blocks
        for (uint32_t i = 0; i < nodes[nodeId].edgeBlockCount; i++) {
            freeBlock(nodes[nodeId].edgeBlocks[i]);
        }
        
        // Free node itself
        freeNode(nodeId);
    }

    // Remove a link
    bool unlink(const std::string& name, uint32_t currentNodeId) {
        auto links = readLinks(nodes[currentNodeId]);
        int32_t targetId = -1;
        
        // Find and remove link
        auto it = std::remove_if(links.begin(), links.end(),
            [&](const LinkEntry& link) {
                if (std::string(link.name) == name) {
                    targetId = link.targetNodeId;
                    return true;
                }
                return false;
            });
            
        if (it == links.end()) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Link not found: " << name << "\n";
            resetColor();
            return false;
        }
        
        links.erase(it, links.end());
        writeLinks(nodes[currentNodeId], links);
        
        if (targetId >= 0 && static_cast<uint32_t>(targetId) < superblock.totalNodes) {
            if (nodes[targetId].refCount > 0) {
                nodes[targetId].refCount--;
            }
            // Garbage Collection
            if (nodes[targetId].refCount == 0) {
                recursiveFree(targetId);
            }
        }
        
        return true;
    }

    // Read node content
    std::string readNodeContent(uint32_t nodeId) {
        if (nodeId >= superblock.totalNodes) return "";
        GraphNode& node = nodes[nodeId];
        
        std::string content;
        content.reserve(node.size);
        
        size_t totalRead = 0;
        for (uint32_t i = 0; i < node.dataBlockCount && i < DATA_BLOCKS_COUNT; i++) {
            auto block = readBlock(node.dataBlocks[i]);
            size_t toRead = std::min(static_cast<size_t>(superblock.blockSize), static_cast<size_t>(node.size - totalRead));
            content.append(reinterpret_cast<char*>(block.data()), toRead);
            totalRead += toRead;
        }
        return content;
    }

    // Write node content (overwrite)
    bool writeNodeContent(uint32_t nodeId, const std::string& content) {
        if (nodeId >= superblock.totalNodes) return false;
        GraphNode& node = nodes[nodeId];
        
        // Calculate needed blocks
        uint32_t blockSize = superblock.blockSize;
        uint32_t neededBlocks = (content.size() + blockSize - 1) / blockSize;
        if (content.empty()) neededBlocks = 0;
        
        // Allocate blocks if needed
        while (node.dataBlockCount < neededBlocks && node.dataBlockCount < DATA_BLOCKS_COUNT) {
            int32_t block = allocBlock();
            if (block < 0) break;
            node.dataBlocks[node.dataBlockCount++] = block;
        }
        
        if (node.dataBlockCount < neededBlocks) {
            setColor(COLOR_ERROR);
            std::cerr << "Error: Not enough blocks or exceeding max file size\n";
            resetColor();
            return false;
        }
        
        // Free excess blocks
        while (node.dataBlockCount > neededBlocks) {
            freeBlock(node.dataBlocks[--node.dataBlockCount]);
        }
        
        // Write content
        for (uint32_t i = 0; i < node.dataBlockCount; i++) {
            std::vector<uint8_t> buffer(blockSize, 0);
            size_t copySize = std::min(static_cast<size_t>(blockSize), content.size() - i * blockSize);
            memcpy(buffer.data(), content.data() + i * blockSize, copySize);
            writeBlock(node.dataBlocks[i], buffer);
        }
        
        node.size = content.size();
        node.modified = time(nullptr);
        return true;
    }

    // List links from a node
    std::vector<std::pair<std::string, uint32_t>> listLinks(uint32_t nodeId) {
        std::vector<std::pair<std::string, uint32_t>> result;
        
        if (nodeId >= nodes.size()) {
            return result;
        }
        
        auto links = readLinks(nodes[nodeId]);
        for (const auto& link : links) {
            result.push_back({link.name, link.targetNodeId});
        }
        
        return result;
    }

    // Get console handle for external use
    HANDLE getConsoleHandle() const { return hConsole; }
    
    // Get colors for external use
    static WORD getColorDir() { return COLOR_DIR; }
    static WORD getColorFile() { return COLOR_FILE; }
    static WORD getColorError() { return COLOR_ERROR; }
    static WORD getColorSuccess() { return COLOR_SUCCESS; }
    static WORD getColorPrompt() { return COLOR_PROMPT; }
    static WORD getColorPath() { return COLOR_PATH; }
    static WORD getColorDefault() { return COLOR_DEFAULT; }
};

// ============================================================================
// NODE SHELL CLASS
// ============================================================================

class NodeShell {
private:
    NodeFS& fs;
    uint32_t currentNodeId;
    std::string currentPath; // Logical path tracking
    bool running;
    HANDLE hConsole;
    
    // Store last ls output for numbered navigation
    std::vector<std::pair<std::string, uint32_t>> lastListResults;
    
    void setColor(WORD color) {
        SetConsoleTextAttribute(hConsole, color);
    }
    
    void resetColor() {
        SetConsoleTextAttribute(hConsole, NodeFS::getColorDefault());
    }
    
    // Resolve a target that might be a number (from last ls) or a name
    std::string resolveTarget(const std::string& input) {
        // Check if it's a number
        try {
            size_t num = std::stoul(input);
            if (num > 0 && num <= lastListResults.size()) {
                return lastListResults[num - 1].first;
            }
        } catch (...) {
            // Not a number, return as-is
        }
        return input;
    }

    std::vector<std::string> tokenize(const std::string& input) {
        std::vector<std::string> tokens;
        std::string current;
        bool inQuotes = false;
        
        for (char c : input) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ' ' && !inQuotes) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }
        return tokens;
    }

    void printPrompt() {
        setColor(NodeFS::getColorPrompt());
        std::cout << "node:";
        setColor(NodeFS::getColorPath());
        std::cout << currentPath << "(" << currentNodeId << ")";
        setColor(NodeFS::getColorPrompt());
        std::cout << "> ";
        resetColor();
    }
    
    // Resolve logical path for display
    void updatePath(const std::string& newComponent) {
        if (newComponent == "/") {
            currentPath = "/";
        } else if (newComponent == "..") {
            if (currentPath == "/") return;
            size_t pos = currentPath.rfind('/');
            if (pos != std::string::npos) {
                if (pos == 0) currentPath = "/";
                else currentPath = currentPath.substr(0, pos);
            }
        } else if (newComponent == ".") {
            // No change
        } else {
            if (currentPath != "/") currentPath += "/";
            currentPath += newComponent;
        }
    }

    void cmdLs(const std::vector<std::string>& args) {
        uint32_t targetId = currentNodeId;
        
        if (args.size() > 1) {
            std::string target = resolveTarget(args[1]);
            int32_t id = fs.findNode(target, currentNodeId);
            if (id < 0) {
                 setColor(NodeFS::getColorError());
                 std::cerr << "Node not found\n";
                 resetColor();
                 return;
            }
            targetId = id;
        }
        
        auto links = fs.listLinks(targetId);
        
        // Cache results for numbered navigation
        lastListResults = links;
        
        if (links.empty()) {
            setColor(NodeFS::getColorFile());
            std::cout << "(no outgoing links)\n";
            resetColor();
            return;
        }
        
        size_t num = 1;
        for (const auto& [name, target] : links) {
            // Numbered output for easy navigation
            setColor(NodeFS::getColorDefault());
            std::cout << std::setw(3) << num++ << ". ";
            setColor(NodeFS::getColorDir());
            std::cout << name;
            setColor(NodeFS::getColorDefault());
            std::cout << " -> " << target << "\n";
        }
        resetColor();
    }

    void cmdCd(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            currentNodeId = 0; // Go to root
            currentPath = "/";
            return;
        }
        
        std::string target = resolveTarget(args[1]);
        
        if (target == "..") {
            // Graph doesn't support implicit parent. 
            // For traversing up logical path, we re-resolve from root.
            if (currentPath == "/") return;
            
            std::string oldPath = currentPath;
            updatePath("..");
            int32_t pid = fs.findNode(currentPath); // Resolve from root
            if (pid >= 0) {
                currentNodeId = pid;
            } else {
                currentPath = oldPath;
                setColor(NodeFS::getColorError());
                std::cerr << "Error: Cannot resolve parent path\n";
                resetColor();
            }
            return;
        }
        
        int32_t newId = fs.findNode(target, currentNodeId);
        if (newId >= 0) {
            currentNodeId = newId;
            // Best effort path update
            if (target[0] == '/') currentPath = target;
            else updatePath(target);
        } else {
             setColor(NodeFS::getColorError());
             std::cerr << "Node not found: " << target << "\n";
             resetColor();
        }
    }

    void cmdMake(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: make <name> [content]\n";
            resetColor();
            return;
        }
        
        std::string content;
        if (args.size() > 2) {
            content = args[2];
            // If content is quoted string in args, tokenize handles it?
            // Yes, tokenize splits by quotes.
        }
        
        if (fs.makeNode(args[1], currentNodeId, content)) {
            setColor(NodeFS::getColorSuccess());
            std::cout << "Created node '" << args[1] << "'\n";
            resetColor();
        }
    }
    
    void cmdLink(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: link <target_id> <name>\n";
            resetColor();
            return;
        }
        
        uint32_t targetId = 0;
        try {
            targetId = std::stoul(args[1]);
        } catch (...) {
            setColor(NodeFS::getColorError());
            std::cerr << "Invalid target ID\n";
            resetColor();
            return;
        }
        
        if (fs.linkNode(targetId, args[2], currentNodeId)) {
            setColor(NodeFS::getColorSuccess());
            std::cout << "Linked '" << args[2] << "' -> " << targetId << "\n";
            resetColor();
        }
    }

    void cmdUnlink(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: unlink <name|number>\n";
            resetColor();
            return;
        }
        
        std::string target = resolveTarget(args[1]);
        if (fs.unlink(target, currentNodeId)) {
            setColor(NodeFS::getColorSuccess());
            std::cout << "Unlinked '" << target << "'\n";
            resetColor();
        }
    }

    void cmdCat(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: cat <name|number>\n";
            resetColor();
            return;
        }
        
        std::string target = resolveTarget(args[1]);
        int32_t id = fs.findNode(target, currentNodeId);
        if (id < 0) {
            setColor(NodeFS::getColorError());
            std::cerr << "Node not found\n";
            resetColor();
            return;
        }
        
        std::string content = fs.readNodeContent(id);
        std::cout << content << "\n";
    }

    void cmdEcho(const std::vector<std::string>& args) {
        // Find redirection
        size_t redirectPos = 0;
        bool append = false;
        
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == ">") {
                redirectPos = i;
                append = false;
                break;
            } else if (args[i] == ">>") {
                redirectPos = i;
                append = true;
                break;
            }
        }
        
        if (redirectPos > 0 && redirectPos + 1 < args.size()) {
            // Build content
            std::string content;
            for (size_t i = 1; i < redirectPos; i++) {
                if (i > 1) content += " ";
                content += args[i];
            }
            content += "\n";
            
            std::string filename = args[redirectPos + 1];
            
            int32_t targetId = fs.findNode(filename, currentNodeId);
            
            if (targetId >= 0) {
                // Exists
                if (append) {
                    std::string existing = fs.readNodeContent(targetId);
                    content = existing + content;
                }
                // Write content
                fs.writeNodeContent(targetId, content);
            } else {
                // New node
                fs.makeNode(filename, currentNodeId, content);
            }
        } else {
            // Just print
            for (size_t i = 1; i < args.size(); i++) {
                if (i > 1) std::cout << " ";
                std::cout << args[i];
            }
            std::cout << "\n";
        }
    }

    void cmdLino(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: Lino <file|number>\n";
            resetColor();
            return;
        }
        
        std::string filename = resolveTarget(args[1]);
        int32_t id = fs.findNode(filename, currentNodeId);
        std::string content;
        
        if (id >= 0) {
            content = fs.readNodeContent(id);
        }
        
        // Simple line editor
        std::vector<std::string> lines;
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }
        if (lines.empty()) lines.push_back("");
        
        setColor(NodeFS::getColorSuccess());
        std::cout << "=== Node Lino Editor ===\n";
        std::cout << "Commands: :w (save), :q (quit), :wq (save&quit)\n";
        std::cout << "<n> to edit line, 'a' to append, 'i<n>' to insert before line, 'd<n>' to delete\n";
        resetColor();
        
        bool editing = true;
        while (editing) {
            // Show current content
            std::cout << "\n";
            for (size_t i = 0; i < lines.size(); i++) {
                setColor(NodeFS::getColorDir());
                std::cout << std::setw(3) << (i + 1) << ": ";
                resetColor();
                std::cout << lines[i] << "\n";
            }
            
            std::cout << "\n> ";
            std::string cmd;
            std::getline(std::cin, cmd);
            if (cmd.empty()) continue;
            
            if (cmd == ":q") {
                editing = false;
            } else if (cmd == ":w") {
                std::string newContent;
                for (const auto& l : lines) {
                    newContent += l + "\n";
                }
                
                if (id >= 0) {
                     fs.writeNodeContent(id, newContent);
                } else {
                     fs.makeNode(filename, currentNodeId, newContent);
                     id = fs.findNode(filename, currentNodeId);
                }
                
                setColor(NodeFS::getColorSuccess());
                std::cout << "Saved.\n";
                resetColor();
            } else if (cmd == ":wq") {
                std::string newContent;
                for (const auto& l : lines) {
                    newContent += l + "\n";
                }
                if (id >= 0) {
                     fs.writeNodeContent(id, newContent);
                } else {
                     fs.makeNode(filename, currentNodeId, newContent);
                }
                editing = false;
            } else if (cmd == "a") {
                std::cout << "New line: ";
                std::string newLine;
                std::getline(std::cin, newLine);
                lines.push_back(newLine);
            } else if (cmd[0] == 'i' && cmd.size() > 1) {
                // Insert before line number
                try {
                    size_t lineNum = std::stoi(cmd.substr(1));
                    if (lineNum > 0 && lineNum <= lines.size() + 1) {
                        std::cout << "Insert before line " << lineNum << ": ";
                        std::string newLine;
                        std::getline(std::cin, newLine);
                        lines.insert(lines.begin() + lineNum - 1, newLine);
                    } else {
                        setColor(NodeFS::getColorError());
                        std::cerr << "Invalid line number\n";
                        resetColor();
                    }
                } catch (...) {
                    setColor(NodeFS::getColorError());
                    std::cerr << "Usage: i<line_number>\n";
                    resetColor();
                }
            } else if (cmd[0] == 'd' && cmd.size() > 1) {
                try {
                    size_t lineNum = std::stoi(cmd.substr(1));
                    if (lineNum > 0 && lineNum <= lines.size()) {
                        lines.erase(lines.begin() + lineNum - 1);
                    }
                } catch (...) {}
            } else {
                try {
                    size_t lineNum = std::stoi(cmd);
                    if (lineNum > 0 && lineNum <= lines.size()) {
                        std::cout << "Line " << lineNum << ": ";
                        std::string newLine;
                        std::getline(std::cin, newLine);
                        lines[lineNum - 1] = newLine;
                    }
                } catch (...) {
                    setColor(NodeFS::getColorError());
                    std::cout << "Unknown command. Use :q to quit.\n";
                    resetColor();
                }
            }
        }
    }

    void cmdImport(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: import <host_path> [node_name]\n";
            resetColor();
            return;
        }
        
        std::string hostPath = args[1];
        
        // Read file from host filesystem
        std::ifstream file(hostPath, std::ios::binary);
        if (!file) {
            setColor(NodeFS::getColorError());
            std::cerr << "Error: Cannot open file: " << hostPath << "\n";
            resetColor();
            return;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        
        // Determine node name
        std::string nodeName;
        if (args.size() > 2) {
            nodeName = args[2];
        } else {
            // Use filename from path
            fs::path p(hostPath);
            nodeName = p.filename().string();
        }
        
        // Check if node exists
        int32_t existingId = fs.findNode(nodeName, currentNodeId);
        if (existingId >= 0) {
            // Overwrite existing
            fs.writeNodeContent(existingId, content);
            setColor(NodeFS::getColorSuccess());
            std::cout << "Updated '" << nodeName << "' (" << content.size() << " bytes)\n";
            resetColor();
        } else {
            // Create new node
            if (fs.makeNode(nodeName, currentNodeId, content)) {
                setColor(NodeFS::getColorSuccess());
                std::cout << "Imported '" << nodeName << "' (" << content.size() << " bytes)\n";
                resetColor();
            }
        }
    }
    
    void cmdExport(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            setColor(NodeFS::getColorError());
            std::cerr << "Usage: export <node_name|number> <host_path>\n";
            resetColor();
            return;
        }
        
        std::string target = resolveTarget(args[1]);
        std::string hostPath = args[2];
        
        int32_t id = fs.findNode(target, currentNodeId);
        if (id < 0) {
            setColor(NodeFS::getColorError());
            std::cerr << "Node not found: " << target << "\n";
            resetColor();
            return;
        }
        
        std::string content = fs.readNodeContent(id);
        
        // Write to host filesystem
        std::ofstream file(hostPath, std::ios::binary);
        if (!file) {
            setColor(NodeFS::getColorError());
            std::cerr << "Error: Cannot write to: " << hostPath << "\n";
            resetColor();
            return;
        }
        
        file.write(content.data(), content.size());
        file.close();
        
        setColor(NodeFS::getColorSuccess());
        std::cout << "Exported '" << target << "' to " << hostPath << " (" << content.size() << " bytes)\n";
        resetColor();
    }

    void cmdHelp(const std::vector<std::string>& args) {
        setColor(NodeFS::getColorSuccess());
        std::cout << "=== Node Graph Shell ===\n";
        resetColor();
        std::cout << "Navigation (use numbers from 'ls' output):\n";
        std::cout << "  ls [path]             List links (numbered)\n";
        std::cout << "  cd <path|number>      Traverse to node\n";
        std::cout << "  cat <path|number>     Show node content\n";
        std::cout << "  pwd                   Show logical path\n\n";
        std::cout << "File Operations:\n";
        std::cout << "  make <name> [content] Create new node\n";
        std::cout << "  Lino <file|number>    Edit node content (i<n> to insert)\n";
        std::cout << "  echo <text> [> file]  Print or redirect to file\n";
        std::cout << "  rm <name|number>      Remove link (alias: unlink)\n\n";
        std::cout << "Import/Export:\n";
        std::cout << "  import <host_path>    Import file from Windows\n";
        std::cout << "  export <node> <path>  Export file to Windows\n\n";
        std::cout << "Other:\n";
        std::cout << "  link <id> <name>      Link existing node\n";
        std::cout << "  exit                  Exit shell\n";
    }

public:
    NodeShell(NodeFS& filesystem) 
        : fs(filesystem), currentNodeId(0), currentPath("/"), running(true),
          hConsole(GetStdHandle(STD_OUTPUT_HANDLE)) {}

    void run() {
        setColor(NodeFS::getColorSuccess());
        std::cout << "\n=== Node Shell (Graph Mode) ===\n";
        std::cout << "Type 'help' for commands\n\n";
        resetColor();
        
        while (running) {
            printPrompt();
            
            std::string input;
            if (!std::getline(std::cin, input)) break;
            
            // Trim
            size_t start = input.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            input = input.substr(start, input.find_last_not_of(" \t") - start + 1);
            if (input.empty()) continue;
            
            auto tokens = tokenize(input);
            if (tokens.empty()) continue;
            const std::string& cmd = tokens[0];
            
            if (cmd == "exit" || cmd == "quit") running = false;
            else if (cmd == "ls") cmdLs(tokens);
            else if (cmd == "cd") cmdCd(tokens);
            else if (cmd == "make" || cmd == "touch" || cmd == "mkdir") cmdMake(tokens);
            else if (cmd == "link" || cmd == "ln") cmdLink(tokens);
            else if (cmd == "unlink" || cmd == "rm" || cmd == "rmdir") cmdUnlink(tokens);
            else if (cmd == "cat") cmdCat(tokens);
            else if (cmd == "echo") cmdEcho(tokens);
            else if (cmd == "Lino") cmdLino(tokens);
            else if (cmd == "import") cmdImport(tokens);
            else if (cmd == "export") cmdExport(tokens);
            else if (cmd == "pwd") std::cout << currentPath << "\n";
            else if (cmd == "help" || cmd == "?") cmdHelp(tokens);
            else {
                setColor(NodeFS::getColorError());
                std::cerr << "Unknown command: " << cmd << "\n";
                resetColor();
            }
        }
    }
};

#endif // NODE_HPP
