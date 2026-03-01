// g++ -O3 -o ../cmds/umount.exe umount.cpp
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: umount [DIR]\n";
        return 1;
    }

    std::string dir = argv[1];
    if (dir.back() != '\\' && dir.back() != '/') {
        dir += "\\";
    }

    if (DeleteVolumeMountPointA(dir.c_str())) {
        return 0;
    } else {
        DWORD err = GetLastError();
        std::cerr << "Linuxify: umount: failed to unmount " << dir << " (Error " << err << ")\n";
        return 1;
    }
}
