// Linuxify Networking Module
// Provides networking commands: ping, traceroute, nslookup, curl, wget
// Custom commands: net show, net connect, net disconnect

#ifndef LINUXIFY_NETWORKING_HPP
#define LINUXIFY_NETWORKING_HPP

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

class Networking {
private:
    static void printError(const std::string& msg) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cerr << "Error: " << msg << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    
    static void printSuccess(const std::string& msg) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << msg << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

public:
    // ip - Show IP configuration (like Linux ip command)
    static void showIP(const std::vector<std::string>& args) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        if (args.size() > 1) {
            if (args[1] == "addr" || args[1] == "a") {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "=== IP Addresses ===" << std::endl;
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                system("ipconfig | findstr /C:\"IPv4\" /C:\"IPv6\" /C:\"adapter\"");
            } else if (args[1] == "route" || args[1] == "r") {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "=== Routing Table ===" << std::endl;
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                system("route print");
            } else if (args[1] == "link" || args[1] == "l") {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << "=== Network Interfaces ===" << std::endl;
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                system("netsh interface show interface");
            } else {
                std::cout << "Usage: ip [addr|route|link]" << std::endl;
            }
        } else {
            // Default: show all
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "=== Network Configuration ===" << std::endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            system("ipconfig");
        }
    }

    // ping - Ping a host
    static void ping(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("ping: missing host");
            std::cout << "Usage: ping <host> [-c count]" << std::endl;
            return;
        }
        
        std::string host = args[1];
        int count = 4;  // Default
        
        for (size_t i = 2; i < args.size(); i++) {
            if ((args[i] == "-c" || args[i] == "-n") && i + 1 < args.size()) {
                count = std::stoi(args[i + 1]);
                i++;
            }
        }
        
        std::string cmd = "ping -n " + std::to_string(count) + " " + host;
        system(cmd.c_str());
    }

    // traceroute - Trace route to host
    static void traceroute(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("traceroute: missing host");
            std::cout << "Usage: traceroute <host>" << std::endl;
            return;
        }
        
        std::string cmd = "tracert " + args[1];
        system(cmd.c_str());
    }

    // nslookup - DNS lookup
    static void nslookup(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("nslookup: missing host");
            std::cout << "Usage: nslookup <host>" << std::endl;
            return;
        }
        
        std::string cmd = "nslookup " + args[1];
        if (args.size() > 2) {
            cmd += " " + args[2];  // Optional DNS server
        }
        system(cmd.c_str());
    }

    // dig - DNS lookup (alias for nslookup with cleaner output)
    static void dig(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("dig: missing host");
            std::cout << "Usage: dig <host>" << std::endl;
            return;
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== DNS Lookup: " << args[1] << " ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        std::string cmd = "nslookup " + args[1] + " 2>nul | findstr /C:\"Name\" /C:\"Address\"";
        system(cmd.c_str());
    }

    // curl - HTTP request (simplified)
    static void curl(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("curl: missing URL");
            std::cout << "Usage: curl <url> [-o output_file]" << std::endl;
            return;
        }
        
        std::string url = args[1];
        std::string outputFile;
        bool showHeaders = false;
        bool silent = false;
        
        for (size_t i = 2; i < args.size(); i++) {
            if ((args[i] == "-o" || args[i] == "-O") && i + 1 < args.size()) {
                outputFile = args[i + 1];
                i++;
            } else if (args[i] == "-I" || args[i] == "--head") {
                showHeaders = true;
            } else if (args[i] == "-s" || args[i] == "--silent") {
                silent = true;
            }
        }
        
        // Use Windows curl if available, otherwise use PowerShell
        std::string cmd;
        if (!outputFile.empty()) {
            cmd = "curl -L -o \"" + outputFile + "\" \"" + url + "\" 2>nul || powershell -Command \"Invoke-WebRequest -Uri '" + url + "' -OutFile '" + outputFile + "'\"";
        } else if (showHeaders) {
            cmd = "curl -I \"" + url + "\" 2>nul || powershell -Command \"(Invoke-WebRequest -Uri '" + url + "' -Method Head).Headers | Format-Table -AutoSize\"";
        } else {
            cmd = "curl -L \"" + url + "\" 2>nul || powershell -Command \"(Invoke-WebRequest -Uri '" + url + "').Content\"";
        }
        
        system(cmd.c_str());
    }

    // wget - Download file
    static void wget(const std::vector<std::string>& args, const std::string& currentDir = "") {
        if (args.size() < 2) {
            printError("wget: missing URL");
            std::cout << "Usage: wget <url> [-O output_file]" << std::endl;
            return;
        }
        
        std::string url = args[1];
        std::string outputFile;
        
        for (size_t i = 2; i < args.size(); i++) {
            if ((args[i] == "-O" || args[i] == "-o") && i + 1 < args.size()) {
                outputFile = args[i + 1];
                i++;
            }
        }
        
        if (outputFile.empty()) {
            // Extract filename from URL
            size_t pos = url.find_last_of("/\\");
            if (pos != std::string::npos) {
                outputFile = url.substr(pos + 1);
                // Remove query string
                size_t qpos = outputFile.find("?");
                if (qpos != std::string::npos) {
                    outputFile = outputFile.substr(0, qpos);
                }
            }
            if (outputFile.empty()) {
                outputFile = "downloaded_file";
            }
        }
        
        // Make path absolute if currentDir is provided
        std::string fullPath = outputFile;
        if (!currentDir.empty() && outputFile.find(':') == std::string::npos && outputFile[0] != '/' && outputFile[0] != '\\') {
            fullPath = currentDir + "\\" + outputFile;
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Downloading: " << url << std::endl;
        std::cout << "Saving to: " << fullPath << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        // Use PowerShell with absolute path
        std::string cmd = "powershell -NoProfile -Command \"$ProgressPreference = 'Continue'; [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri '" + url + "' -OutFile '" + fullPath + "' -UseBasicParsing; Write-Host 'OK' } catch { Write-Host $_.Exception.Message; exit 1 }\"";
        int result = system(cmd.c_str());
        
        // Check if file was created and has content
        std::ifstream checkFile(fullPath, std::ios::ate);
        if (checkFile.good() && checkFile.tellg() > 0) {
            printSuccess("Download complete: " + outputFile + " (" + std::to_string(checkFile.tellg()) + " bytes)");
        } else {
            printError("Download failed - file not created or empty");
        }
    }

    // net show - Show available WiFi networks
    static void netShow() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Available WiFi Networks ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        // Use netsh to list available networks
        system("netsh wlan show networks mode=bssid");
    }

    // net connect - Connect to a WiFi network
    static void netConnect(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("net connect: missing SSID");
            std::cout << "Usage: net connect <ssid> [-p <password>]" << std::endl;
            return;
        }
        
        std::string ssid = args[2];
        std::string password;
        std::string iface = "Wi-Fi";  // Default interface
        
        for (size_t i = 3; i < args.size(); i++) {
            if ((args[i] == "-p" || args[i] == "--password") && i + 1 < args.size()) {
                password = args[i + 1];
                i++;
            } else if ((args[i] == "-i" || args[i] == "--interface") && i + 1 < args.size()) {
                iface = args[i + 1];
                i++;
            }
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        if (password.empty()) {
            // Try to connect to a known network
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Connecting to: " << ssid << std::endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            std::string cmd = "netsh wlan connect name=\"" + ssid + "\" interface=\"" + iface + "\"";
            int result = system(cmd.c_str());
            
            if (result != 0) {
                printError("Failed to connect. Network may require a password or profile doesn't exist.");
                std::cout << "Try: net connect " << ssid << " -p <password>" << std::endl;
            }
        } else {
            // Create a temporary profile and connect
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "Creating profile for: " << ssid << std::endl;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            // Create XML profile
            std::string profileXml = R"(<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
    <name>)" + ssid + R"(</name>
    <SSIDConfig>
        <SSID>
            <name>)" + ssid + R"(</name>
        </SSID>
    </SSIDConfig>
    <connectionType>ESS</connectionType>
    <connectionMode>auto</connectionMode>
    <MSM>
        <security>
            <authEncryption>
                <authentication>WPA2PSK</authentication>
                <encryption>AES</encryption>
                <useOneX>false</useOneX>
            </authEncryption>
            <sharedKey>
                <keyType>passPhrase</keyType>
                <protected>false</protected>
                <keyMaterial>)" + password + R"(</keyMaterial>
            </sharedKey>
        </security>
    </MSM>
</WLANProfile>)";
            
            // Write profile to temp file
            std::string tempProfile = std::getenv("TEMP") ? std::string(std::getenv("TEMP")) + "\\linuxify_wifi.xml" : "linuxify_wifi.xml";
            std::ofstream profileFile(tempProfile);
            profileFile << profileXml;
            profileFile.close();
            
            // Add profile
            std::string addCmd = "netsh wlan add profile filename=\"" + tempProfile + "\" 2>nul";
            system(addCmd.c_str());
            
            // Connect
            std::string connectCmd = "netsh wlan connect name=\"" + ssid + "\" interface=\"" + iface + "\"";
            int result = system(connectCmd.c_str());
            
            // Clean up temp file
            remove(tempProfile.c_str());
            
            if (result == 0) {
                printSuccess("Connected to " + ssid);
            } else {
                printError("Failed to connect to " + ssid);
            }
        }
    }

    // net disconnect - Disconnect from current network
    static void netDisconnect(const std::vector<std::string>& args) {
        std::string iface = "Wi-Fi";
        
        if (args.size() >= 3) {
            iface = args[2];
        }
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "Disconnecting from WiFi..." << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        std::string cmd = "netsh wlan disconnect interface=\"" + iface + "\"";
        int result = system(cmd.c_str());
        
        if (result == 0) {
            printSuccess("Disconnected");
        }
    }

    // net status - Show current connection status
    static void netStatus() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== Current Connection ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        std::cout << std::endl;
        
        system("netsh wlan show interfaces");
    }

    // Main net command handler
    static void netCommand(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cout << "Usage: net <command>" << std::endl;
            std::cout << "Commands:" << std::endl;
            std::cout << "  net show            Show available WiFi networks" << std::endl;
            std::cout << "  net connect <ssid>  Connect to a network" << std::endl;
            std::cout << "  net disconnect      Disconnect from WiFi" << std::endl;
            std::cout << "  net status          Show connection status" << std::endl;
            return;
        }
        
        std::string subCmd = args[1];
        
        if (subCmd == "show" || subCmd == "scan") {
            netShow();
        } else if (subCmd == "connect" || subCmd == "c") {
            netConnect(args);
        } else if (subCmd == "disconnect" || subCmd == "dc") {
            netDisconnect(args);
        } else if (subCmd == "status" || subCmd == "s") {
            netStatus();
        } else {
            printError("Unknown net command: " + subCmd);
            std::cout << "Use 'net' for help" << std::endl;
        }
    }

    // netstat - Show network statistics
    static void netstat(const std::vector<std::string>& args) {
        std::string flags = "";
        
        for (size_t i = 1; i < args.size(); i++) {
            if (args[i][0] == '-') {
                flags += " " + args[i];
            }
        }
        
        if (flags.empty()) {
            flags = " -an";  // Default: all connections, numeric
        }
        
        std::string cmd = "netstat" + flags;
        system(cmd.c_str());
    }
};

#endif // LINUXIFY_NETWORKING_HPP
