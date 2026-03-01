// g++ -O3 -o ../cmds/uname.exe uname.cpp
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>

int main(int argc, char* argv[]) {
    bool showAll = false;
    bool showKernelName = true;       // -s
    bool showNodeName = false;        // -n
    bool showKernelRelease = false;   // -r
    bool showKernelVersion = false;   // -v
    bool showMachine = false;         // -m
    bool showProcessor = false;       // -p
    bool showHardwarePlatform = false;// -i
    bool showOS = false;              // -o

    if (argc > 1) {
        showKernelName = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-a" || arg == "--all") showAll = true;
            else if (arg == "-s" || arg == "--kernel-name") showKernelName = true;
            else if (arg == "-n" || arg == "--nodename") showNodeName = true;
            else if (arg == "-r" || arg == "--kernel-release") showKernelRelease = true;
            else if (arg == "-v" || arg == "--kernel-version") showKernelVersion = true;
            else if (arg == "-m" || arg == "--machine") showMachine = true;
            else if (arg == "-p" || arg == "--processor") showProcessor = true;
            else if (arg == "-i" || arg == "--hardware-platform") showHardwarePlatform = true;
            else if (arg == "-o" || arg == "--operating-system") showOS = true;
            else if (arg == "--help") {
                std::cout << "Usage: uname [OPTION]...\n";
                std::cout << "Print certain system information.  With no OPTION, same as -s.\n";
                return 0;
            }
        }
    }

    if (showAll) {
        showKernelName = showNodeName = showKernelRelease = showKernelVersion = showMachine = showOS = true;
        showProcessor = showHardwarePlatform = false; // Usually excluded in basic Windows/Linuxify ports or 'unknown'
    }

    std::vector<std::string> output;

    if (showKernelName) {
        output.push_back("Windows_NT"); // As per original main.cpp behavior
    }
    
    if (showNodeName) {
        char computerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = sizeof(computerName) / sizeof(computerName[0]);
        if (GetComputerNameA(computerName, &size)) {
            output.push_back(computerName);
        } else {
            output.push_back("unknown");
        }
    }
    
    if (showKernelRelease) {
        output.push_back("Linuxify-Shell"); // As per original main.cpp behavior
    }
    
    if (showKernelVersion) {
        output.push_back("1.0"); // As per original main.cpp behavior
    }
    
    if (showMachine) {
        SYSTEM_INFO sysInfo;
        GetNativeSystemInfo(&sysInfo);
        if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
            output.push_back("x86_64");
        } else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) {
            output.push_back("arm");
        } else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
            output.push_back("aarch64");
        } else {
            output.push_back("i386");
        }
    }
    
    if (showProcessor) {
        output.push_back("unknown");
    }
    
    if (showHardwarePlatform) {
         output.push_back("unknown");
    }
    
    if (showOS) {
        output.push_back("MS/Windows");
    }

    for (size_t i = 0; i < output.size(); ++i) {
        std::cout << output[i];
        if (i < output.size() - 1) std::cout << " ";
    }
    std::cout << "\n";

    return 0;
}
