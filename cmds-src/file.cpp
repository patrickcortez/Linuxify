// Compile: g++ -std=c++17 -static -o ../cmds/file.exe file.cpp
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

std::string resolvePath(const std::string& path) {
    if (path.empty()) {
        return fs::current_path().string();
    }
    fs::path p(path);
    if (p.is_absolute()) {
        try {
            return fs::canonical(p).string();
        } catch (...) {
            return p.string();
        }
    }
    fs::path fullPath = fs::current_path() / path;
    try {
        return fs::canonical(fullPath).string();
    } catch (...) {
        return fullPath.string();
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    if (args.size() < 2) {
        printError("file: missing file operand");
        return 1;
    }
    
    bool brief = false;
    bool mimeType = false;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-b" || args[i] == "--brief") brief = true;
        else if (args[i] == "-i" || args[i] == "--mime-type") mimeType = true;
        else if (args[i][0] != '-') files.push_back(args[i]);
    }
    
    int exitCode = 0;
    for (const auto& arg : files) {
        std::string filePath = resolvePath(arg);
        
        if (!fs::exists(filePath)) {
            if (!brief) std::cout << arg << ": ";
            std::cout << "cannot open (No such file or directory)\n";
            exitCode = 1;
            continue;
        }
        
        if (!brief) std::cout << arg << ": ";
        
        if (fs::is_directory(filePath)) {
            std::cout << (mimeType ? "inode/directory" : "directory") << "\n";
            continue;
        }
        
        if (fs::is_symlink(filePath)) {
            std::cout << (mimeType ? "inode/symlink" : "symbolic link") << "\n";
            continue;
        }
        
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            std::cout << "cannot open\n";
            exitCode = 1;
            continue;
        }
        
        unsigned char magic[32] = {0};
        file.read(reinterpret_cast<char*>(magic), 32);
        size_t bytesRead = file.gcount();
        
        if (bytesRead == 0) {
            std::cout << (mimeType ? "inode/x-empty" : "empty") << "\n";
            continue;
        }
        
        if (magic[0] == 0x4D && magic[1] == 0x5A) {
            std::cout << (mimeType ? "application/x-dosexec" : "PE32 executable (Windows)") << "\n";
        } else if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
            std::cout << (mimeType ? "application/x-executable" : "ELF executable") << "\n";
        } else if (magic[0] == 0xCA && magic[1] == 0xFE && magic[2] == 0xBA && magic[3] == 0xBE) {
            std::cout << (mimeType ? "application/x-mach-binary" : "Mach-O universal binary") << "\n";
        } else if (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') {
            std::cout << (mimeType ? "image/png" : "PNG image data") << "\n";
        } else if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) {
            std::cout << (mimeType ? "image/jpeg" : "JPEG image data") << "\n";
        } else if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == '8') {
            std::cout << (mimeType ? "image/gif" : "GIF image data") << "\n";
        } else if (magic[0] == 'B' && magic[1] == 'M') {
            std::cout << (mimeType ? "image/bmp" : "BMP image data") << "\n";
        } else if (magic[0] == 0x00 && magic[1] == 0x00 && magic[2] == 0x01 && magic[3] == 0x00) {
            std::cout << (mimeType ? "image/x-icon" : "ICO image data") << "\n";
        } else if (magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F') {
            if (magic[8] == 'W' && magic[9] == 'A' && magic[10] == 'V' && magic[11] == 'E') {
                std::cout << (mimeType ? "audio/wav" : "WAV audio") << "\n";
            } else if (magic[8] == 'A' && magic[9] == 'V' && magic[10] == 'I') {
                std::cout << (mimeType ? "video/avi" : "AVI video") << "\n";
            } else if (magic[8] == 'W' && magic[9] == 'E' && magic[10] == 'B' && magic[11] == 'P') {
                std::cout << (mimeType ? "image/webp" : "WebP image") << "\n";
            } else {
                std::cout << (mimeType ? "application/octet-stream" : "RIFF data") << "\n";
            }
        } else if (magic[0] == 'O' && magic[1] == 'g' && magic[2] == 'g' && magic[3] == 'S') {
            std::cout << (mimeType ? "application/ogg" : "Ogg data") << "\n";
        } else if (magic[0] == 'f' && magic[1] == 'L' && magic[2] == 'a' && magic[3] == 'C') {
            std::cout << (mimeType ? "audio/flac" : "FLAC audio") << "\n";
        } else if (magic[0] == 0xFF && (magic[1] & 0xE0) == 0xE0) {
            std::cout << (mimeType ? "audio/mpeg" : "MP3 audio") << "\n";
        } else if (magic[0] == 'I' && magic[1] == 'D' && magic[2] == '3') {
            std::cout << (mimeType ? "audio/mpeg" : "MP3 audio (ID3 tag)") << "\n";
        } else if (magic[4] == 'f' && magic[5] == 't' && magic[6] == 'y' && magic[7] == 'p') {
            std::cout << (mimeType ? "video/mp4" : "MP4/M4A media") << "\n";
        } else if (magic[0] == 0x1A && magic[1] == 0x45 && magic[2] == 0xDF && magic[3] == 0xA3) {
            std::cout << (mimeType ? "video/webm" : "WebM/MKV video") << "\n";
        } else if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
            file.seekg(30);
            char nameTest[8] = {0};
            file.read(nameTest, 8);
            if (strncmp(nameTest, "word/", 5) == 0) {
                std::cout << (mimeType ? "application/vnd.openxmlformats-officedocument.wordprocessingml.document" : "Microsoft Word 2007+ document") << "\n";
            } else if (strncmp(nameTest, "xl/", 3) == 0) {
                std::cout << (mimeType ? "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" : "Microsoft Excel 2007+ spreadsheet") << "\n";
            } else if (strncmp(nameTest, "ppt/", 4) == 0) {
                std::cout << (mimeType ? "application/vnd.openxmlformats-officedocument.presentationml.presentation" : "Microsoft PowerPoint 2007+ presentation") << "\n";
            } else {
                std::cout << (mimeType ? "application/zip" : "Zip archive data") << "\n";
            }
        } else if (magic[0] == 0x1F && magic[1] == 0x8B) {
            std::cout << (mimeType ? "application/gzip" : "gzip compressed data") << "\n";
        } else if (magic[0] == 0x42 && magic[1] == 0x5A && magic[2] == 0x68) {
            std::cout << (mimeType ? "application/x-bzip2" : "bzip2 compressed data") << "\n";
        } else if (magic[0] == 0xFD && magic[1] == 0x37 && magic[2] == 0x7A && magic[3] == 0x58 && magic[4] == 0x5A) {
            std::cout << (mimeType ? "application/x-xz" : "XZ compressed data") << "\n";
        } else if (magic[0] == 0x37 && magic[1] == 0x7A && magic[2] == 0xBC && magic[3] == 0xAF) {
            std::cout << (mimeType ? "application/x-7z-compressed" : "7-zip archive data") << "\n";
        } else if (magic[0] == 0x52 && magic[1] == 0x61 && magic[2] == 0x72 && magic[3] == 0x21) {
            std::cout << (mimeType ? "application/x-rar" : "RAR archive data") << "\n";
        } else if (magic[0] == '%' && magic[1] == 'P' && magic[2] == 'D' && magic[3] == 'F') {
            std::cout << (mimeType ? "application/pdf" : "PDF document") << "\n";
        } else if (magic[0] == 0xD0 && magic[1] == 0xCF && magic[2] == 0x11 && magic[3] == 0xE0) {
            std::cout << (mimeType ? "application/msword" : "Microsoft Office document (OLE)") << "\n";
        } else if (magic[0] == 0x25 && magic[1] == 0x21 && magic[2] == 0x50 && magic[3] == 0x53) {
            std::cout << (mimeType ? "application/postscript" : "PostScript document") << "\n";
        } else if (magic[0] == 0xEF && magic[1] == 0xBB && magic[2] == 0xBF) {
            std::cout << (mimeType ? "text/plain; charset=utf-8" : "UTF-8 Unicode text (with BOM)") << "\n";
        } else if (magic[0] == 0xFE && magic[1] == 0xFF) {
            std::cout << (mimeType ? "text/plain; charset=utf-16be" : "UTF-16 BE Unicode text") << "\n";
        } else if (magic[0] == 0xFF && magic[1] == 0xFE) {
            std::cout << (mimeType ? "text/plain; charset=utf-16le" : "UTF-16 LE Unicode text") << "\n";
        } else if (magic[0] == '<' && magic[1] == '?') {
            if (magic[2] == 'x' && magic[3] == 'm' && magic[4] == 'l') {
                std::cout << (mimeType ? "application/xml" : "XML document") << "\n";
            } else {
                std::cout << (mimeType ? "text/x-php" : "PHP script") << "\n";
            }
        } else if (magic[0] == '<' && magic[1] == '!' && magic[2] == 'D') {
            std::cout << (mimeType ? "text/html" : "HTML document") << "\n";
        } else if (magic[0] == '<' && (magic[1] == 'h' || magic[1] == 'H')) {
            std::cout << (mimeType ? "text/html" : "HTML document") << "\n";
        } else if (magic[0] == '{' || magic[0] == '[') {
            std::cout << (mimeType ? "application/json" : "JSON data") << "\n";
        } else if (magic[0] == '#' && magic[1] == '!') {
            std::cout << (mimeType ? "text/x-shellscript" : "script, shebang executable") << "\n";
        } else if (magic[0] == 0x00 && magic[1] == 0x00 && magic[2] == 0x00) {
            std::cout << (mimeType ? "application/octet-stream" : "binary data") << "\n";
        } else {
            bool isText = true;
            for (size_t j = 0; j < bytesRead; ++j) {
                if (magic[j] < 0x09 || (magic[j] > 0x0D && magic[j] < 0x20 && magic[j] != 0x1B)) {
                    if (magic[j] != 0) isText = false;
                }
            }
            if (isText) {
                std::cout << (mimeType ? "text/plain" : "ASCII text") << "\n";
            } else {
                std::cout << (mimeType ? "application/octet-stream" : "data") << "\n";
            }
        }
    }
    return exitCode;
}
