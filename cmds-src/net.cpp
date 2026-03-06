#define _WIN32_WINNT 0x0601
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include <wlanapi.h>
#include <iomanip>
#include <algorithm>
#include <map>
#include "../shell_streams.hpp"

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "advapi32.lib")

// Print colorful error and exit
void printError(const std::string& message) {
    ShellIO::serr << ShellIO::Color::LightRed << "[ERROR] " << ShellIO::Color::Reset 
                  << message << ShellIO::endl;
}

void printSuccess(const std::string& message) {
    ShellIO::sout << ShellIO::Color::LightGreen << message << ShellIO::Color::Reset << ShellIO::endl;
}

void printHeader(const std::string& header) {
    ShellIO::sout << ShellIO::Color::Cyan << "=== " << header << " ===" << ShellIO::Color::Reset << ShellIO::endl;
}

// ---------------------------------------------------------
// SERVICE MANAGEMENT
// ---------------------------------------------------------

void listServices() {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM) {
        printError("Failed to open Service Control Manager. Try running as Administrator.");
        return;
    }

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;

    EnumServicesStatusEx(
        hSCM,
        SC_ENUM_PROCESS_INFO,
        SERVICE_WIN32,
        SERVICE_STATE_ALL,
        NULL,
        0,
        &bytesNeeded,
        &servicesReturned,
        &resumeHandle,
        NULL
    );

    std::vector<BYTE> buffer(bytesNeeded);
    ENUM_SERVICE_STATUS_PROCESS* pServices = (ENUM_SERVICE_STATUS_PROCESS*)buffer.data();

    if (!EnumServicesStatusEx(
            hSCM,
            SC_ENUM_PROCESS_INFO,
            SERVICE_WIN32,
            SERVICE_STATE_ALL,
            buffer.data(),
            bytesNeeded,
            &bytesNeeded,
            &servicesReturned,
            &resumeHandle,
            NULL)) {
        printError("Failed to enumerate services.");
        CloseServiceHandle(hSCM);
        return;
    }

    printHeader("Windows Services");
    
    // Header
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << std::left 
              << std::setw(35) << "Service Display Name" 
              << std::setw(30) << "Service Name" 
              << std::setw(15) << "Status" 
              << "PID" << std::endl;
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    std::cout << std::string(85, '-') << std::endl;

    for (DWORD i = 0; i < servicesReturned; i++) {
        std::string displayName = pServices[i].lpDisplayName;
        if (displayName.length() > 32) displayName = displayName.substr(0, 29) + "...";
        
        std::string serviceName = pServices[i].lpServiceName;
        if (serviceName.length() > 27) serviceName = serviceName.substr(0, 24) + "...";

        std::string statusStr;
        WORD colorAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

        switch (pServices[i].ServiceStatusProcess.dwCurrentState) {
            case SERVICE_STOPPED: 
                statusStr = "Stopped"; 
                colorAttr = FOREGROUND_RED | FOREGROUND_INTENSITY;
                break;
            case SERVICE_START_PENDING: statusStr = "Starting..."; break;
            case SERVICE_STOP_PENDING: statusStr = "Stopping..."; break;
            case SERVICE_RUNNING: 
                statusStr = "Running"; 
                colorAttr = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                break;
            case SERVICE_CONTINUE_PENDING: statusStr = "Continuing..."; break;
            case SERVICE_PAUSE_PENDING: statusStr = "Pausing..."; break;
            case SERVICE_PAUSED: 
                statusStr = "Paused"; 
                colorAttr = FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY; // Yellow
                break;
            default: statusStr = "Unknown"; break;
        }

        std::cout << std::left << std::setw(35) << displayName << std::setw(30) << serviceName;
        
        SetConsoleTextAttribute(hConsole, colorAttr);
        std::cout << std::setw(15) << statusStr;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        
        std::cout << pServices[i].ServiceStatusProcess.dwProcessId << std::endl;
    }

    CloseServiceHandle(hSCM);
}

void startService(const std::string& serviceName) {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        printError("Failed to open Service Control Manager. Try running as Administrator.");
        return;
    }

    SC_HANDLE hService = OpenServiceA(hSCM, serviceName.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hService) {
        printError("Failed to open service '" + serviceName + "'. Does it exist?");
        CloseServiceHandle(hSCM);
        return;
    }

    if (StartService(hService, 0, NULL)) {
        printSuccess("Requested start of service: " + serviceName);
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            printError("Service is already running.");
        } else {
            printError("Failed to start service. Error code: " + std::to_string(err));
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
}

void stopService(const std::string& serviceName) {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        printError("Failed to open Service Control Manager. Try running as Administrator.");
        return;
    }

    SC_HANDLE hService = OpenServiceA(hSCM, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hService) {
        printError("Failed to open service '" + serviceName + "'. Does it exist?");
        CloseServiceHandle(hSCM);
        return;
    }

    SERVICE_STATUS status;
    if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
        printSuccess("Requested stop of service: " + serviceName);
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            printError("Service is not running.");
        } else {
            printError("Failed to stop service. Error code: " + std::to_string(err));
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);
}

// ---------------------------------------------------------
// WIFI MANAGEMENT
// ---------------------------------------------------------

void netShow() {
    printHeader("WiFi Networks");

    HANDLE hClient = NULL;
    DWORD negotiatedVersion = 0;
    DWORD result = WlanOpenHandle(2, NULL, &negotiatedVersion, &hClient);
    if (result != ERROR_SUCCESS) {
        printError("Failed to open WLAN handle. WiFi may not be available.");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    result = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (result != ERROR_SUCCESS || pIfList == NULL || pIfList->dwNumberOfItems == 0) {
        printError("No WiFi interfaces found.");
        if (pIfList) WlanFreeMemory(pIfList);
        WlanCloseHandle(hClient, NULL);
        return;
    }

    for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
        PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[i];
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::wcout << L"Interface: " << pIfInfo->strInterfaceDescription << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

        WlanScan(hClient, &pIfInfo->InterfaceGuid, NULL, NULL, NULL);
        Sleep(2000);

        PWLAN_AVAILABLE_NETWORK_LIST pNetworkList = NULL;
        result = WlanGetAvailableNetworkList(hClient, &pIfInfo->InterfaceGuid, 0, NULL, &pNetworkList);
        if (result != ERROR_SUCCESS || pNetworkList == NULL) {
            printError("Failed to get network list.");
            continue;
        }

        std::cout << std::endl;
        std::cout << std::left 
                  << std::setw(35) << "SSID"
                  << std::setw(10) << "Signal"
                  << std::setw(15) << "Security"
                  << "Status" << std::endl;
        std::cout << std::string(70, '-') << std::endl;

        for (DWORD j = 0; j < pNetworkList->dwNumberOfItems; j++) {
            PWLAN_AVAILABLE_NETWORK pNetwork = &pNetworkList->Network[j];

            std::string ssid;
            if (pNetwork->dot11Ssid.uSSIDLength > 0) {
                ssid = std::string((char*)pNetwork->dot11Ssid.ucSSID, pNetwork->dot11Ssid.uSSIDLength);
            } else {
                ssid = "(Hidden Network)";
            }

            if (ssid.length() > 33) ssid = ssid.substr(0, 33) + "..";

            std::string signalBar;
            int signalQuality = pNetwork->wlanSignalQuality;
            if (signalQuality > 80) signalBar = "####";
            else if (signalQuality > 60) signalBar = "###-";
            else if (signalQuality > 40) signalBar = "##--";
            else if (signalQuality > 20) signalBar = "#---";
            else signalBar = "----";

            std::string security;
            switch (pNetwork->dot11DefaultAuthAlgorithm) {
                case DOT11_AUTH_ALGO_80211_OPEN: security = "Open"; break;
                case DOT11_AUTH_ALGO_80211_SHARED_KEY: security = "WEP"; break;
                case DOT11_AUTH_ALGO_WPA: security = "WPA"; break;
                case DOT11_AUTH_ALGO_WPA_PSK: security = "WPA-PSK"; break;
                case DOT11_AUTH_ALGO_RSNA: security = "WPA2"; break;
                case DOT11_AUTH_ALGO_RSNA_PSK: security = "WPA2-PSK"; break;
                case DOT11_AUTH_ALGO_WPA3: security = "WPA3"; break;
                case DOT11_AUTH_ALGO_WPA3_SAE: security = "WPA3-SAE"; break;
                default: security = "Unknown"; break;
            }

            std::string status;
            if (pNetwork->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) {
                status = "Connected";
            } else if (pNetwork->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) {
                status = "Saved";
            } else {
                status = "";
            }

            std::cout << std::left << std::setw(35) << ssid;
            
            if (signalQuality > 60) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            } else if (signalQuality > 30) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            } else {
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
            }
            std::cout << std::setw(10) << (signalBar + " " + std::to_string(signalQuality) + "%");
            
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout << std::setw(15) << security;

            if (!status.empty()) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                std::cout << status;
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            }
            std::cout << std::endl;
        }

        WlanFreeMemory(pNetworkList);
        std::cout << std::endl;
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
}

void netConnect(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        printError("net connect: missing SSID");
        std::cout << "Usage: net connect <SSID>" << std::endl;
        return;
    }

    std::string ssid = args[2];
    for (size_t i = 3; i < args.size(); i++) {
        ssid += " " + args[i];
    }

    HANDLE hClient = NULL;
    DWORD negotiatedVersion = 0;
    DWORD result = WlanOpenHandle(2, NULL, &negotiatedVersion, &hClient);
    if (result != ERROR_SUCCESS) {
        printError("Failed to open WLAN handle. WiFi may not be available.");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    result = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (result != ERROR_SUCCESS || pIfList == NULL || pIfList->dwNumberOfItems == 0) {
        printError("No WiFi interfaces found.");
        if (pIfList) WlanFreeMemory(pIfList);
        WlanCloseHandle(hClient, NULL);
        return;
    }

    PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[0];
    PWLAN_AVAILABLE_NETWORK_LIST pNetworkList = NULL;
    result = WlanGetAvailableNetworkList(hClient, &pIfInfo->InterfaceGuid, 0, NULL, &pNetworkList);
    
    bool networkFound = false;
    bool hasProfile = false;
    bool isSecured = false;
    DOT11_AUTH_ALGORITHM authAlgo = DOT11_AUTH_ALGO_80211_OPEN;
    DOT11_CIPHER_ALGORITHM cipherAlgo = DOT11_CIPHER_ALGO_NONE;
    
    if (result == ERROR_SUCCESS && pNetworkList != NULL) {
        for (DWORD j = 0; j < pNetworkList->dwNumberOfItems; j++) {
            PWLAN_AVAILABLE_NETWORK pNetwork = &pNetworkList->Network[j];
            std::string foundSsid;
            if (pNetwork->dot11Ssid.uSSIDLength > 0) {
                foundSsid = std::string((char*)pNetwork->dot11Ssid.ucSSID, pNetwork->dot11Ssid.uSSIDLength);
            }
            
            std::string ssidLower = ssid;
            std::string foundLower = foundSsid;
            std::transform(ssidLower.begin(), ssidLower.end(), ssidLower.begin(), ::tolower);
            std::transform(foundLower.begin(), foundLower.end(), foundLower.begin(), ::tolower);
            
            if (foundLower == ssidLower) {
                networkFound = true;
                ssid = foundSsid;
                if (pNetwork->dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) {
                    hasProfile = true;
                }
                authAlgo = pNetwork->dot11DefaultAuthAlgorithm;
                cipherAlgo = pNetwork->dot11DefaultCipherAlgorithm;
                if (authAlgo != DOT11_AUTH_ALGO_80211_OPEN) {
                    isSecured = true;
                }
                break;
            }
        }
        WlanFreeMemory(pNetworkList);
    }

    if (!networkFound) {
        printError("Network '" + ssid + "' not found. Make sure you're in range.");
        WlanFreeMemory(pIfList);
        WlanCloseHandle(hClient, NULL);
        return;
    }

    std::wstring wssid(ssid.begin(), ssid.end());

    if (!hasProfile) {
        std::wstring profileXml;
        
        if (!isSecured) {
            profileXml = L"<?xml version=\"1.0\"?>"
                L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                L"<name>" + wssid + L"</name>"
                L"<SSIDConfig><SSID><name>" + wssid + L"</name></SSID></SSIDConfig>"
                L"<connectionType>ESS</connectionType>"
                L"<connectionMode>manual</connectionMode>"
                L"<MSM><security>"
                L"<authEncryption><authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX></authEncryption>"
                L"</security></MSM>"
                L"</WLANProfile>";
                
            std::cout << "Connecting to open network '" << ssid << "'..." << std::endl;
        } else {
            std::cout << "Password for '" << ssid << "': ";
            std::string password;
            
            HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
            DWORD mode;
            GetConsoleMode(hStdin, &mode);
            SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));
            
            std::getline(std::cin, password);
            
            SetConsoleMode(hStdin, mode);
            std::cout << std::endl;
            
            if (password.empty()) {
                printError("Password cannot be empty.");
                WlanFreeMemory(pIfList);
                WlanCloseHandle(hClient, NULL);
                return;
            }
            
            if (password.length() < 8) {
                printError("Password must be at least 8 characters for WPA/WPA2.");
                WlanFreeMemory(pIfList);
                WlanCloseHandle(hClient, NULL);
                return;
            }
            
            std::wstring wpassword(password.begin(), password.end());
            
            std::wstring authType, encType;
            switch (authAlgo) {
                case DOT11_AUTH_ALGO_WPA_PSK:
                    authType = L"WPAPSK";
                    encType = L"TKIP";
                    break;
                case DOT11_AUTH_ALGO_RSNA_PSK:
                default:
                    authType = L"WPA2PSK";
                    encType = L"AES";
                    break;
            }
            
            if (cipherAlgo == DOT11_CIPHER_ALGO_CCMP) {
                encType = L"AES";
            } else if (cipherAlgo == DOT11_CIPHER_ALGO_TKIP) {
                encType = L"TKIP";
            }
            
            profileXml = L"<?xml version=\"1.0\"?>"
                L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                L"<name>" + wssid + L"</name>"
                L"<SSIDConfig><SSID><name>" + wssid + L"</name></SSID></SSIDConfig>"
                L"<connectionType>ESS</connectionType>"
                L"<connectionMode>auto</connectionMode>"
                L"<MSM><security>"
                L"<authEncryption>"
                L"<authentication>" + authType + L"</authentication>"
                L"<encryption>" + encType + L"</encryption>"
                L"<useOneX>false</useOneX>"
                L"</authEncryption>"
                L"<sharedKey>"
                L"<keyType>passPhrase</keyType>"
                L"<protected>false</protected>"
                L"<keyMaterial>" + wpassword + L"</keyMaterial>"
                L"</sharedKey>"
                L"</security></MSM>"
                L"</WLANProfile>";
                
            std::cout << "Connecting to '" << ssid << "'..." << std::endl;
        }
        
        DWORD reasonCode = 0;
        result = WlanSetProfile(hClient, &pIfInfo->InterfaceGuid, 0, profileXml.c_str(), NULL, TRUE, NULL, &reasonCode);
        
        if (result != ERROR_SUCCESS) {
            printError("Failed to create network profile. Error: " + std::to_string(result) + ", Reason: " + std::to_string(reasonCode));
            WlanFreeMemory(pIfList);
            WlanCloseHandle(hClient, NULL);
            return;
        }
    } else {
        std::cout << "Connecting to '" << ssid << "'..." << std::endl;
    }

    WLAN_CONNECTION_PARAMETERS connParams;
    ZeroMemory(&connParams, sizeof(connParams));
    connParams.wlanConnectionMode = wlan_connection_mode_profile;
    connParams.strProfile = wssid.c_str();
    connParams.pDot11Ssid = NULL;
    connParams.pDesiredBssidList = NULL;
    connParams.dot11BssType = dot11_BSS_type_infrastructure;
    connParams.dwFlags = 0;

    result = WlanConnect(hClient, &pIfInfo->InterfaceGuid, &connParams, NULL);
    
    if (result == ERROR_SUCCESS) {
        for (int attempt = 0; attempt < 10; attempt++) {
            Sleep(500);
            
            PWLAN_CONNECTION_ATTRIBUTES pConnAttr = NULL;
            DWORD dataSize = 0;
            WLAN_OPCODE_VALUE_TYPE opCode;
            result = WlanQueryInterface(hClient, &pIfInfo->InterfaceGuid, 
                                        wlan_intf_opcode_current_connection, NULL, 
                                        &dataSize, (PVOID*)&pConnAttr, &opCode);
            
            if (result == ERROR_SUCCESS && pConnAttr != NULL) {
                std::string connectedSsid;
                if (pConnAttr->wlanAssociationAttributes.dot11Ssid.uSSIDLength > 0) {
                    connectedSsid = std::string(
                        (char*)pConnAttr->wlanAssociationAttributes.dot11Ssid.ucSSID,
                        pConnAttr->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
                }
                
                if (pConnAttr->isState == wlan_interface_state_connected && connectedSsid == ssid) {
                    printSuccess("Successfully connected to '" + ssid + "'");
                    WlanFreeMemory(pConnAttr);
                    WlanFreeMemory(pIfList);
                    WlanCloseHandle(hClient, NULL);
                    return;
                }
                WlanFreeMemory(pConnAttr);
            }
        }
        
        printSuccess("Connection request sent. Verifying...");
        Sleep(2000);
        
        PWLAN_CONNECTION_ATTRIBUTES pConnAttr = NULL;
        DWORD dataSize = 0;
        WLAN_OPCODE_VALUE_TYPE opCode;
        result = WlanQueryInterface(hClient, &pIfInfo->InterfaceGuid, 
                                    wlan_intf_opcode_current_connection, NULL, 
                                    &dataSize, (PVOID*)&pConnAttr, &opCode);
        
        if (result == ERROR_SUCCESS && pConnAttr != NULL && pConnAttr->isState == wlan_interface_state_connected) {
            printSuccess("Connected!");
            WlanFreeMemory(pConnAttr);
        } else {
            printError("Connection may have failed. Check password and try again.");
            if (pConnAttr) WlanFreeMemory(pConnAttr);
        }
    } else {
        printError("Failed to initiate connection. Error code: " + std::to_string(result));
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
}

void netDisconnect() {
    HANDLE hClient = NULL;
    DWORD negotiatedVersion = 0;
    DWORD result = WlanOpenHandle(2, NULL, &negotiatedVersion, &hClient);
    if (result != ERROR_SUCCESS) {
        printError("Failed to open WLAN handle.");
        return;
    }

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    result = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (result != ERROR_SUCCESS || pIfList == NULL || pIfList->dwNumberOfItems == 0) {
        printError("No WiFi interfaces found.");
        if (pIfList) WlanFreeMemory(pIfList);
        WlanCloseHandle(hClient, NULL);
        return;
    }

    PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[0];

    result = WlanDisconnect(hClient, &pIfInfo->InterfaceGuid, NULL);
    if (result == ERROR_SUCCESS) {
        printSuccess("Disconnected from WiFi network.");
    } else {
        printError("Failed to disconnect. Error code: " + std::to_string(result));
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
}

// ---------------------------------------------------------
// ENTRY POINT
// ---------------------------------------------------------

int main(int argc, char* argv[]) {
    // Enable ANSI escape sequences
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }

    if (args.size() < 2) {
        std::cout << "Usage: net <command>\n";
        std::cout << "Commands:\n";
        std::cout << "  list                List Windows Services\n";
        std::cout << "  start <service>     Start a Windows Service\n";
        std::cout << "  stop <service>      Stop a Windows Service\n";
        std::cout << "  show                Show WiFi Networks\n";
        std::cout << "  connect <ssid>      Connect to WiFi\n";
        std::cout << "  disconnect          Disconnect from WiFi\n";
        return 0;
    }

    std::string cmd = args[1];

    if (cmd == "list") {
        listServices();
    } else if (cmd == "start") {
        if (args.size() < 3) {
            printError("Usage: net start <service_name>");
            return 1;
        }
        std::string serviceName = args[2];
        for (size_t i = 3; i < args.size(); i++) serviceName += " " + args[i];
        startService(serviceName);
    } else if (cmd == "stop") {
        if (args.size() < 3) {
            printError("Usage: net stop <service_name>");
            return 1;
        }
        std::string serviceName = args[2];
        for (size_t i = 3; i < args.size(); i++) serviceName += " " + args[i];
        stopService(serviceName);
    } else if (cmd == "show" || cmd == "scan") {
        netShow();
    } else if (cmd == "connect" || cmd == "c") {
        netConnect(args);
    } else if (cmd == "disconnect" || cmd == "dc") {
        netDisconnect();
    } else {
        printError("Unknown net command: " + cmd);
        return 1;
    }

    return 0;
}
