// g++ -O3 -o ../cmds/mount.exe mount.cpp
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

int main(int argc, char* argv[]) {
    if (argc == 1) {
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (drives & (1 << i)) {
                char driveName[] = { (char)('A' + i), ':', '\\', '\0' };
                char volumeName[MAX_PATH + 1] = {0};
                char fileSystemName[MAX_PATH + 1] = {0};
                
                if (GetVolumeInformationA(driveName, volumeName, sizeof(volumeName), NULL, NULL, NULL, fileSystemName, sizeof(fileSystemName))) {
                    std::cout << driveName << " on /" << (char)('a' + i) << " type " << fileSystemName << " (rw,local)\n";
                }
            }
        }
        return 0;
    }

    if (argc >= 3) {
        std::string device = argv[argc - 2];
        std::string dir = argv[argc - 1];

        if (device.length() == 2 && device[1] == ':') {
            device += "\\";
        }

        char volumeName[MAX_PATH];
        if (!GetVolumeNameForVolumeMountPointA(device.c_str(), volumeName, MAX_PATH)) {
            if (device.find("\\\\?\\Volume") == 0) {
                strncpy_s(volumeName, device.c_str(), MAX_PATH);
                if (volumeName[strlen(volumeName) - 1] != '\\') {
                    strncat_s(volumeName, "\\", MAX_PATH - strlen(volumeName));
                }
            } else {
                 std::cerr << "Linuxify: mount: cannot resolve device volume GUID for " << device << "\n";
                 return 1;
            }
        }

        if (dir.back() != '\\' && dir.back() != '/') {
            dir += "\\";
        }

        if (SetVolumeMountPointA(dir.c_str(), volumeName)) {
            return 0;
        } else {
            DWORD err = GetLastError();
            std::cerr << "Linuxify: mount: failed to mount " << device << " to " << dir << " (Error " << err << ")\n";
            return 1;
        }
    }

    std::cerr << "Usage: mount [DEVICE] [DIR]\n";
    return 1;
}
