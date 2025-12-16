// g++ -std=c++17 -static -o linuxify main.cpp registry.cpp -lpsapi -lws2_32 -liphlpapi

#ifndef LINUXIFY_NETWORKING_HPP
#define LINUXIFY_NETWORKING_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "wlanapi.lib")

#include <wlanapi.h>

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

    static void printHeader(const std::string& title) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        std::cout << "=== " << title << " ===" << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    static bool initWinsock() {
        static bool initialized = false;
        if (!initialized) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
                initialized = true;
            }
        }
        return initialized;
    }

public:
    static void showIP(const std::vector<std::string>& args) {
        if (args.size() > 1) {
            if (args[1] == "addr" || args[1] == "a") {
                ifconfig(args);
            } else if (args[1] == "route" || args[1] == "r") {
                showRoutes();
            } else if (args[1] == "link" || args[1] == "l") {
                showInterfaces();
            } else if (args[1] == "neigh" || args[1] == "n") {
                std::vector<std::string> arpArgs = {"arp", "-a"};
                arp(arpArgs);
            } else {
                std::cout << "Usage: ip [addr|route|link|neigh]" << std::endl;
            }
        } else {
            ifconfig(args);
        }
    }

    static void ifconfig(const std::vector<std::string>& args) {
        printHeader("Network Interfaces");
        std::cout << std::endl;

        ULONG bufLen = 15000;
        PIP_ADAPTER_ADDRESSES pAddresses = NULL;
        ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS;

        pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
        if (pAddresses == NULL) {
            printError("Memory allocation failed");
            return;
        }

        DWORD ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            free(pAddresses);
            pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
            if (pAddresses == NULL) {
                printError("Memory allocation failed");
                return;
            }
            ret = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &bufLen);
        }

        if (ret != NO_ERROR) {
            printError("GetAdaptersAddresses failed");
            free(pAddresses);
            return;
        }

        bool showAll = (args.size() > 1 && args[1] == "-a");

        for (PIP_ADAPTER_ADDRESSES pCur = pAddresses; pCur != NULL; pCur = pCur->Next) {
            if (!showAll && pCur->OperStatus != IfOperStatusUp) continue;

            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::wcout << pCur->FriendlyName;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            
            std::cout << ": ";
            if (pCur->OperStatus == IfOperStatusUp) std::cout << "<UP>";
            else std::cout << "<DOWN>";
            
            if (pCur->IfType == IF_TYPE_ETHERNET_CSMACD) std::cout << " type ethernet";
            else if (pCur->IfType == IF_TYPE_IEEE80211) std::cout << " type wifi";
            else if (pCur->IfType == IF_TYPE_SOFTWARE_LOOPBACK) std::cout << " type loopback";
            
            std::cout << std::endl;

            if (pCur->PhysicalAddressLength > 0) {
                std::cout << "    link/ether ";
                for (ULONG i = 0; i < pCur->PhysicalAddressLength; i++) {
                    if (i > 0) std::cout << ":";
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)pCur->PhysicalAddress[i];
                }
                std::cout << std::dec << std::endl;
            }

            for (PIP_ADAPTER_UNICAST_ADDRESS pAddr = pCur->FirstUnicastAddress; pAddr != NULL; pAddr = pAddr->Next) {
                char addrStr[INET6_ADDRSTRLEN];
                DWORD addrLen = sizeof(addrStr);
                
                if (pAddr->Address.lpSockaddr->sa_family == AF_INET) {
                    struct sockaddr_in* sa = (struct sockaddr_in*)pAddr->Address.lpSockaddr;
                    inet_ntop(AF_INET, &sa->sin_addr, addrStr, addrLen);
                    std::cout << "    inet " << addrStr << "/" << (int)pAddr->OnLinkPrefixLength << std::endl;
                } else if (pAddr->Address.lpSockaddr->sa_family == AF_INET6) {
                    struct sockaddr_in6* sa6 = (struct sockaddr_in6*)pAddr->Address.lpSockaddr;
                    inet_ntop(AF_INET6, &sa6->sin6_addr, addrStr, addrLen);
                    std::cout << "    inet6 " << addrStr << "/" << (int)pAddr->OnLinkPrefixLength << std::endl;
                }
            }

            for (PIP_ADAPTER_GATEWAY_ADDRESS_LH pGw = pCur->FirstGatewayAddress; pGw != NULL; pGw = pGw->Next) {
                char gwStr[INET6_ADDRSTRLEN];
                if (pGw->Address.lpSockaddr->sa_family == AF_INET) {
                    struct sockaddr_in* sa = (struct sockaddr_in*)pGw->Address.lpSockaddr;
                    inet_ntop(AF_INET, &sa->sin_addr, gwStr, sizeof(gwStr));
                    std::cout << "    gateway " << gwStr << std::endl;
                }
            }

            std::cout << std::endl;
        }

        free(pAddresses);
    }

    static void showInterfaces() {
        printHeader("Network Interfaces");
        std::cout << std::endl;

        MIB_IF_TABLE2* pIfTable = NULL;
        if (GetIfTable2(&pIfTable) != NO_ERROR) {
            printError("Failed to get interface table");
            return;
        }

        std::cout << std::setw(40) << std::left << "Name" 
                  << std::setw(10) << "Status"
                  << std::setw(15) << "Speed"
                  << "Type" << std::endl;
        std::cout << std::string(75, '-') << std::endl;

        for (ULONG i = 0; i < pIfTable->NumEntries; i++) {
            MIB_IF_ROW2* row = &pIfTable->Table[i];
            
            std::wstring name(row->Description);
            std::string nameStr(name.begin(), name.end());
            if (nameStr.length() > 38) nameStr = nameStr.substr(0, 38) + "..";
            
            std::cout << std::setw(40) << std::left << nameStr;
            std::cout << std::setw(10) << (row->OperStatus == IfOperStatusUp ? "up" : "down");
            
            if (row->TransmitLinkSpeed > 0) {
                if (row->TransmitLinkSpeed >= 1000000000) {
                    std::cout << std::setw(15) << std::to_string(row->TransmitLinkSpeed / 1000000000) + " Gbps";
                } else if (row->TransmitLinkSpeed >= 1000000) {
                    std::cout << std::setw(15) << std::to_string(row->TransmitLinkSpeed / 1000000) + " Mbps";
                } else {
                    std::cout << std::setw(15) << std::to_string(row->TransmitLinkSpeed / 1000) + " Kbps";
                }
            } else {
                std::cout << std::setw(15) << "-";
            }
            
            switch (row->Type) {
                case IF_TYPE_ETHERNET_CSMACD: std::cout << "Ethernet"; break;
                case IF_TYPE_IEEE80211: std::cout << "WiFi"; break;
                case IF_TYPE_SOFTWARE_LOOPBACK: std::cout << "Loopback"; break;
                case IF_TYPE_TUNNEL: std::cout << "Tunnel"; break;
                default: std::cout << "Other"; break;
            }
            std::cout << std::endl;
        }

        FreeMibTable(pIfTable);
    }

    static void showRoutes() {
        printHeader("Routing Table");
        std::cout << std::endl;

        MIB_IPFORWARDTABLE* pTable = NULL;
        ULONG size = 0;
        
        GetIpForwardTable(NULL, &size, FALSE);
        pTable = (MIB_IPFORWARDTABLE*)malloc(size);
        if (pTable == NULL) {
            printError("Memory allocation failed");
            return;
        }

        if (GetIpForwardTable(pTable, &size, TRUE) != NO_ERROR) {
            printError("Failed to get routing table");
            free(pTable);
            return;
        }

        std::cout << std::setw(18) << std::left << "Destination"
                  << std::setw(18) << "Gateway"
                  << std::setw(18) << "Netmask"
                  << std::setw(8) << "Metric"
                  << "Interface" << std::endl;
        std::cout << std::string(75, '-') << std::endl;

        for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
            MIB_IPFORWARDROW* row = &pTable->table[i];
            
            struct in_addr dest, gw, mask;
            dest.S_un.S_addr = row->dwForwardDest;
            gw.S_un.S_addr = row->dwForwardNextHop;
            mask.S_un.S_addr = row->dwForwardMask;

            char destStr[16], gwStr[16], maskStr[16];
            inet_ntop(AF_INET, &dest, destStr, sizeof(destStr));
            inet_ntop(AF_INET, &gw, gwStr, sizeof(gwStr));
            inet_ntop(AF_INET, &mask, maskStr, sizeof(maskStr));

            std::cout << std::setw(18) << std::left << destStr
                      << std::setw(18) << gwStr
                      << std::setw(18) << maskStr
                      << std::setw(8) << row->dwForwardMetric1
                      << row->dwForwardIfIndex << std::endl;
        }

        free(pTable);
    }

    static void ping(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("ping: missing host");
            std::cout << "Usage: ping <host> [-c count] [-w timeout]" << std::endl;
            return;
        }

        std::string host = args[1];
        int count = 4;
        int timeout = 4000;

        for (size_t i = 2; i < args.size(); i++) {
            if ((args[i] == "-c" || args[i] == "-n") && i + 1 < args.size()) {
                try { count = std::stoi(args[++i]); } catch (...) {}
            } else if ((args[i] == "-w" || args[i] == "-W") && i + 1 < args.size()) {
                try { timeout = std::stoi(args[++i]) * 1000; } catch (...) {}
            }
        }

        if (!initWinsock()) {
            printError("Failed to initialize Winsock");
            return;
        }

        struct addrinfo hints = {0}, *result = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_RAW;

        if (getaddrinfo(host.c_str(), NULL, &hints, &result) != 0) {
            printError("Could not resolve hostname: " + host);
            return;
        }

        struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));

        std::cout << "PING " << host << " (" << ipStr << ") 32 bytes of data." << std::endl;

        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE) {
            printError("Failed to create ICMP handle");
            freeaddrinfo(result);
            return;
        }

        char sendData[32] = "LinuxifyPing";
        DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
        LPVOID replyBuffer = malloc(replySize);

        int received = 0;
        double minTime = 9999, maxTime = 0, totalTime = 0;

        for (int i = 0; i < count; i++) {
            DWORD ret = IcmpSendEcho(hIcmp, addr->sin_addr.S_un.S_addr, 
                                     sendData, sizeof(sendData), NULL,
                                     replyBuffer, replySize, timeout);

            if (ret > 0) {
                PICMP_ECHO_REPLY pReply = (PICMP_ECHO_REPLY)replyBuffer;
                double rtt = (double)pReply->RoundTripTime;
                
                std::cout << "32 bytes from " << ipStr << ": icmp_seq=" << (i + 1)
                          << " ttl=" << (int)pReply->Options.Ttl
                          << " time=" << std::fixed << std::setprecision(1) << rtt << " ms" << std::endl;
                
                received++;
                totalTime += rtt;
                if (rtt < minTime) minTime = rtt;
                if (rtt > maxTime) maxTime = rtt;
            } else {
                std::cout << "Request timeout for icmp_seq " << (i + 1) << std::endl;
            }

            if (i < count - 1) Sleep(1000);
        }

        std::cout << std::endl << "--- " << host << " ping statistics ---" << std::endl;
        std::cout << count << " packets transmitted, " << received << " received, "
                  << ((count - received) * 100 / count) << "% packet loss" << std::endl;
        
        if (received > 0) {
            std::cout << "rtt min/avg/max = " << std::fixed << std::setprecision(1)
                      << minTime << "/" << (totalTime / received) << "/" << maxTime << " ms" << std::endl;
        }

        free(replyBuffer);
        IcmpCloseHandle(hIcmp);
        freeaddrinfo(result);
    }

    static void traceroute(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("traceroute: missing host");
            std::cout << "Usage: traceroute <host> [-m max_hops]" << std::endl;
            return;
        }

        std::string host = args[1];
        int maxHops = 30;
        int timeout = 3000;

        for (size_t i = 2; i < args.size(); i++) {
            if ((args[i] == "-m" || args[i] == "-h") && i + 1 < args.size()) {
                try { maxHops = std::stoi(args[++i]); } catch (...) {}
            }
        }

        if (!initWinsock()) {
            printError("Failed to initialize Winsock");
            return;
        }

        struct addrinfo hints = {0}, *result = NULL;
        hints.ai_family = AF_INET;
        if (getaddrinfo(host.c_str(), NULL, &hints, &result) != 0) {
            printError("Could not resolve hostname: " + host);
            return;
        }

        struct sockaddr_in* destAddr = (struct sockaddr_in*)result->ai_addr;
        char destIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &destAddr->sin_addr, destIp, sizeof(destIp));

        std::cout << "traceroute to " << host << " (" << destIp << "), " << maxHops << " hops max" << std::endl;

        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp == INVALID_HANDLE_VALUE) {
            printError("Failed to create ICMP handle");
            freeaddrinfo(result);
            return;
        }

        char sendData[32] = "TracerouteData";
        DWORD replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 8;
        LPVOID replyBuffer = malloc(replySize);

        for (int ttl = 1; ttl <= maxHops; ttl++) {
            IP_OPTION_INFORMATION ipOpts = {0};
            ipOpts.Ttl = (UCHAR)ttl;

            std::cout << std::setw(2) << ttl << "  ";

            DWORD ret = IcmpSendEcho(hIcmp, destAddr->sin_addr.S_un.S_addr,
                                     sendData, sizeof(sendData), &ipOpts,
                                     replyBuffer, replySize, timeout);

            if (ret > 0) {
                PICMP_ECHO_REPLY pReply = (PICMP_ECHO_REPLY)replyBuffer;
                struct in_addr replyAddr;
                replyAddr.S_un.S_addr = pReply->Address;
                char replyIp[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &replyAddr, replyIp, sizeof(replyIp));

                std::cout << replyIp << "  " << pReply->RoundTripTime << " ms" << std::endl;

                if (pReply->Address == destAddr->sin_addr.S_un.S_addr) {
                    break;
                }
            } else {
                std::cout << "* * *" << std::endl;
            }
        }

        free(replyBuffer);
        IcmpCloseHandle(hIcmp);
        freeaddrinfo(result);
    }

    static void nslookup(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printError("nslookup: missing host");
            std::cout << "Usage: nslookup <host>" << std::endl;
            return;
        }

        if (!initWinsock()) {
            printError("Failed to initialize Winsock");
            return;
        }

        std::string host = args[1];

        std::string dnsServerIp;
        std::string dnsServerName = "Unknown";
        
        ULONG bufLen = 15000;
        PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
        if (pAddresses) {
            ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
            DWORD ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
            if (ret == ERROR_BUFFER_OVERFLOW) {
                free(pAddresses);
                pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufLen);
                if (pAddresses) {
                    ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
                }
            }
            
            if (ret == NO_ERROR && pAddresses) {
                for (PIP_ADAPTER_ADDRESSES pCur = pAddresses; pCur != NULL; pCur = pCur->Next) {
                    if (pCur->OperStatus != IfOperStatusUp) continue;
                    if (pCur->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                    
                    PIP_ADAPTER_DNS_SERVER_ADDRESS pDns = pCur->FirstDnsServerAddress;
                    if (pDns != NULL) {
                        if (pDns->Address.lpSockaddr->sa_family == AF_INET) {
                            struct sockaddr_in* sa = (struct sockaddr_in*)pDns->Address.lpSockaddr;
                            char ipStr[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &sa->sin_addr, ipStr, sizeof(ipStr));
                            dnsServerIp = ipStr;
                            break;
                        }
                    }
                }
            }
            free(pAddresses);
        }

        if (!dnsServerIp.empty()) {
            struct addrinfo hints = {0}, *result = NULL;
            hints.ai_family = AF_INET;
            hints.ai_flags = AI_NUMERICHOST;
            
            if (getaddrinfo(dnsServerIp.c_str(), NULL, &hints, &result) == 0) {
                char hostBuf[NI_MAXHOST];
                if (getnameinfo(result->ai_addr, (socklen_t)result->ai_addrlen, 
                               hostBuf, sizeof(hostBuf), NULL, 0, NI_NAMEREQD) == 0) {
                    dnsServerName = hostBuf;
                } else {
                    dnsServerName = dnsServerIp;
                }
                freeaddrinfo(result);
            }
        }

        std::cout << "Server:  " << dnsServerName << std::endl;
        if (!dnsServerIp.empty()) {
            std::cout << "Address:  " << dnsServerIp << std::endl;
        }
        std::cout << std::endl;

        struct addrinfo hints = {0}, *result = NULL;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        int ret = getaddrinfo(host.c_str(), NULL, &hints, &result);
        if (ret != 0) {
            printError("** server can't find " + host + ": NXDOMAIN");
            return;
        }

        std::cout << "Non-authoritative answer:" << std::endl;
        std::cout << "Name:    " << host << std::endl;

        std::vector<std::string> ipv4Addrs;
        std::vector<std::string> ipv6Addrs;

        for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
            char addrStr[INET6_ADDRSTRLEN];
            
            if (ptr->ai_family == AF_INET) {
                struct sockaddr_in* sa = (struct sockaddr_in*)ptr->ai_addr;
                inet_ntop(AF_INET, &sa->sin_addr, addrStr, sizeof(addrStr));
                std::string addr = addrStr;
                if (std::find(ipv4Addrs.begin(), ipv4Addrs.end(), addr) == ipv4Addrs.end()) {
                    ipv4Addrs.push_back(addr);
                }
            } else if (ptr->ai_family == AF_INET6) {
                struct sockaddr_in6* sa6 = (struct sockaddr_in6*)ptr->ai_addr;
                inet_ntop(AF_INET6, &sa6->sin6_addr, addrStr, sizeof(addrStr));
                std::string addr = addrStr;
                if (std::find(ipv6Addrs.begin(), ipv6Addrs.end(), addr) == ipv6Addrs.end()) {
                    ipv6Addrs.push_back(addr);
                }
            }
        }

        if (ipv4Addrs.size() + ipv6Addrs.size() == 1) {
            std::cout << "Address:  ";
            if (!ipv6Addrs.empty()) {
                std::cout << ipv6Addrs[0];
            } else {
                std::cout << ipv4Addrs[0];
            }
            std::cout << std::endl;
        } else {
            std::cout << "Addresses:  ";
            bool first = true;
            for (const auto& addr : ipv6Addrs) {
                if (!first) std::cout << "          ";
                std::cout << addr << std::endl;
                first = false;
            }
            for (size_t i = 0; i < ipv4Addrs.size(); i++) {
                if (!first) std::cout << "          ";
                std::cout << ipv4Addrs[i];
                if (i < ipv4Addrs.size() - 1) std::cout << ",";
                std::cout << std::endl;
                first = false;
            }
        }

        freeaddrinfo(result);
    }

    static void dig(const std::vector<std::string>& args) {
        nslookup(args);
    }

    static void hostname(const std::vector<std::string>& args) {
        char computerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = sizeof(computerName);
        
        if (GetComputerNameA(computerName, &size)) {
            bool showIp = false;
            for (size_t i = 1; i < args.size(); i++) {
                if (args[i] == "-i" || args[i] == "-I") showIp = true;
            }

            std::cout << computerName << std::endl;

            if (showIp) {
                if (initWinsock()) {
                    char hostName[256];
                    if (gethostname(hostName, sizeof(hostName)) == 0) {
                        struct addrinfo hints = {0}, *result = NULL;
                        hints.ai_family = AF_INET;
                        if (getaddrinfo(hostName, NULL, &hints, &result) == 0) {
                            for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
                                if (ptr->ai_family == AF_INET) {
                                    struct sockaddr_in* sa = (struct sockaddr_in*)ptr->ai_addr;
                                    char addrStr[INET_ADDRSTRLEN];
                                    inet_ntop(AF_INET, &sa->sin_addr, addrStr, sizeof(addrStr));
                                    std::cout << addrStr << std::endl;
                                }
                            }
                            freeaddrinfo(result);
                        }
                    }
                }
            }
        }
    }

    static void arp(const std::vector<std::string>& args) {
        printHeader("ARP Table");
        std::cout << std::endl;

        MIB_IPNETTABLE* pTable = NULL;
        ULONG size = 0;

        GetIpNetTable(NULL, &size, FALSE);
        pTable = (MIB_IPNETTABLE*)malloc(size);
        if (pTable == NULL) {
            printError("Memory allocation failed");
            return;
        }

        if (GetIpNetTable(pTable, &size, TRUE) != NO_ERROR) {
            printError("Failed to get ARP table");
            free(pTable);
            return;
        }

        std::cout << std::setw(18) << std::left << "IP Address"
                  << std::setw(20) << "MAC Address"
                  << std::setw(12) << "Type"
                  << "Interface" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
            MIB_IPNETROW* row = &pTable->table[i];
            
            struct in_addr addr;
            addr.S_un.S_addr = row->dwAddr;
            char ipStr[16];
            inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));

            std::cout << std::setw(18) << std::left << ipStr;

            if (row->dwPhysAddrLen > 0) {
                std::ostringstream mac;
                for (DWORD j = 0; j < row->dwPhysAddrLen; j++) {
                    if (j > 0) mac << "-";
                    mac << std::hex << std::setw(2) << std::setfill('0') << (int)row->bPhysAddr[j];
                }
                std::cout << std::setw(20) << mac.str();
            } else {
                std::cout << std::setw(20) << "(incomplete)";
            }

            std::string type;
            switch (row->dwType) {
                case MIB_IPNET_TYPE_STATIC: type = "static"; break;
                case MIB_IPNET_TYPE_DYNAMIC: type = "dynamic"; break;
                case MIB_IPNET_TYPE_INVALID: type = "invalid"; break;
                default: type = "other"; break;
            }
            std::cout << std::setw(12) << type << std::dec << row->dwIndex << std::endl;
        }

        free(pTable);
    }

    static void ss(const std::vector<std::string>& args) {
        printHeader("Socket Statistics");
        std::cout << std::endl;

        bool showTcp = true, showUdp = false, showListening = false;
        
        for (size_t i = 1; i < args.size(); i++) {
            for (char c : args[i]) {
                if (c == 't') { showTcp = true; showUdp = false; }
                else if (c == 'u') { showUdp = true; showTcp = false; }
                else if (c == 'a') { showTcp = true; showUdp = true; }
                else if (c == 'l') showListening = true;
            }
        }

        if (showTcp) {
            MIB_TCPTABLE* pTable = NULL;
            ULONG size = 0;

            GetTcpTable(NULL, &size, TRUE);
            pTable = (MIB_TCPTABLE*)malloc(size);
            if (pTable && GetTcpTable(pTable, &size, TRUE) == NO_ERROR) {
                std::cout << "TCP Connections:" << std::endl;
                std::cout << std::setw(6) << "State" << "  "
                          << std::setw(22) << "Local Address"
                          << std::setw(22) << "Remote Address" << std::endl;
                std::cout << std::string(55, '-') << std::endl;

                for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
                    MIB_TCPROW* row = &pTable->table[i];
                    
                    if (showListening && row->dwState != MIB_TCP_STATE_LISTEN) continue;

                    std::string state;
                    switch (row->dwState) {
                        case MIB_TCP_STATE_LISTEN: state = "LISTEN"; break;
                        case MIB_TCP_STATE_ESTAB: state = "ESTAB"; break;
                        case MIB_TCP_STATE_TIME_WAIT: state = "TIME_W"; break;
                        case MIB_TCP_STATE_CLOSE_WAIT: state = "CLOSE_W"; break;
                        case MIB_TCP_STATE_CLOSED: state = "CLOSED"; break;
                        default: state = "OTHER"; break;
                    }

                    struct in_addr localAddr, remoteAddr;
                    localAddr.S_un.S_addr = row->dwLocalAddr;
                    remoteAddr.S_un.S_addr = row->dwRemoteAddr;

                    char localIp[16], remoteIp[16];
                    inet_ntop(AF_INET, &localAddr, localIp, sizeof(localIp));
                    inet_ntop(AF_INET, &remoteAddr, remoteIp, sizeof(remoteIp));

                    std::ostringstream local, remote;
                    local << localIp << ":" << ntohs((u_short)row->dwLocalPort);
                    remote << remoteIp << ":" << ntohs((u_short)row->dwRemotePort);

                    std::cout << std::setw(6) << state << "  "
                              << std::setw(22) << local.str()
                              << std::setw(22) << remote.str() << std::endl;
                }
                free(pTable);
            }
        }

        if (showUdp) {
            MIB_UDPTABLE* pTable = NULL;
            ULONG size = 0;

            GetUdpTable(NULL, &size, TRUE);
            pTable = (MIB_UDPTABLE*)malloc(size);
            if (pTable && GetUdpTable(pTable, &size, TRUE) == NO_ERROR) {
                std::cout << std::endl << "UDP Sockets:" << std::endl;
                std::cout << std::setw(22) << "Local Address" << std::endl;
                std::cout << std::string(25, '-') << std::endl;

                for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
                    MIB_UDPROW* row = &pTable->table[i];

                    struct in_addr addr;
                    addr.S_un.S_addr = row->dwLocalAddr;
                    char ipStr[16];
                    inet_ntop(AF_INET, &addr, ipStr, sizeof(ipStr));

                    std::cout << ipStr << ":" << ntohs((u_short)row->dwLocalPort) << std::endl;
                }
                free(pTable);
            }
        }
    }

    static void netstat(const std::vector<std::string>& args) {
        ss(args);
    }

    static void curl(const std::vector<std::string>& args) {
        std::cout << "Use standalone curl.exe for HTTP requests." << std::endl;
        std::cout << "Run: curl --help for usage information." << std::endl;
    }

    static void wget(const std::vector<std::string>& args, const std::string& currentDir = "") {
        if (args.size() < 2) {
            printError("wget: missing URL");
            std::cout << "Usage: wget <url> [-O file]" << std::endl;
            return;
        }

        std::string url = args[1];
        std::string outputFile;
        bool quiet = false;

        for (size_t i = 2; i < args.size(); i++) {
            if ((args[i] == "-O" || args[i] == "-o") && i + 1 < args.size()) {
                outputFile = args[++i];
            } else if (args[i] == "-q" || args[i] == "--quiet") {
                quiet = true;
            }
        }

        if (outputFile.empty()) {
            size_t pos = url.find_last_of('/');
            if (pos != std::string::npos) {
                outputFile = url.substr(pos + 1);
                size_t qpos = outputFile.find('?');
                if (qpos != std::string::npos) outputFile = outputFile.substr(0, qpos);
            }
            if (outputFile.empty()) outputFile = "downloaded_file";
        }

        std::string fullPath = outputFile;
        if (!currentDir.empty() && outputFile.find(':') == std::string::npos && 
            outputFile[0] != '/' && outputFile[0] != '\\') {
            fullPath = currentDir + "\\" + outputFile;
        }

        if (!quiet) {
            std::cout << "Downloading: " << url << std::endl;
            std::cout << "Saving to: " << fullPath << std::endl;
        }

        HINTERNET hInternet = InternetOpenA("wget/linuxify", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hInternet) {
            printError("Failed to initialize internet connection");
            return;
        }

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(), NULL, 0,
                                          INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!hUrl) {
            printError("Failed to open URL");
            InternetCloseHandle(hInternet);
            return;
        }

        std::ofstream outFile(fullPath, std::ios::binary);
        if (!outFile) {
            printError("Cannot create output file: " + fullPath);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            return;
        }

        char buffer[8192];
        DWORD bytesRead;
        DWORD totalBytes = 0;

        while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            outFile.write(buffer, bytesRead);
            totalBytes += bytesRead;
        }

        outFile.close();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        if (!quiet) {
            printSuccess("Download complete: " + outputFile + " (" + std::to_string(totalBytes) + " bytes)");
        }
    }

    static void netShow() {
        printHeader("WiFi Networks");
        std::cout << std::endl;

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
            std::cout << std::setw(35) << std::left << "SSID"
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
                if (signalQuality > 80) signalBar = "████";
                else if (signalQuality > 60) signalBar = "███░";
                else if (signalQuality > 40) signalBar = "██░░";
                else if (signalQuality > 20) signalBar = "█░░░";
                else signalBar = "░░░░";

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

                if (!status.empty()) {
                    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                }

                std::cout << std::setw(35) << std::left << ssid;
                
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

    static void netConnect(const std::vector<std::string>& args) {
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

    static void netDisconnect(const std::vector<std::string>& args) {
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

    static void netStatus() {
        showInterfaces();
    }

    static void netCommand(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cout << "Usage: net <command>\n";
            std::cout << "Commands:\n";
            std::cout << "  net show     Show network interfaces\n";
            std::cout << "  net status   Show interface status\n";
            return;
        }
        
        if (args[1] == "show" || args[1] == "scan") netShow();
        else if (args[1] == "connect" || args[1] == "c") netConnect(args);
        else if (args[1] == "disconnect" || args[1] == "dc") netDisconnect(args);
        else if (args[1] == "status" || args[1] == "s") netStatus();
        else printError("Unknown net command: " + args[1]);
    }

    static void nc(const std::vector<std::string>& args) {
        if (args.size() < 3) {
            printError("nc: missing host and port");
            std::cout << "Usage: nc [-l] <host> <port>" << std::endl;
            return;
        }

        if (!initWinsock()) {
            printError("Failed to initialize Winsock");
            return;
        }

        bool listen = false;
        std::string host;
        int port = 0;

        for (size_t i = 1; i < args.size(); i++) {
            if (args[i] == "-l") listen = true;
            else if (host.empty() && !listen) host = args[i];
            else if (port == 0) {
                try { port = std::stoi(args[i]); } catch (...) { host = args[i]; }
            }
        }

        if (listen) {
            port = std::stoi(host);
            SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listenSock == INVALID_SOCKET) {
                printError("Failed to create socket");
                return;
            }

            struct sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);

            if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                printError("Bind failed");
                closesocket(listenSock);
                return;
            }

            if (::listen(listenSock, 1) == SOCKET_ERROR) {
                printError("Listen failed");
                closesocket(listenSock);
                return;
            }

            std::cout << "Listening on port " << port << "..." << std::endl;

            SOCKET clientSock = accept(listenSock, NULL, NULL);
            if (clientSock == INVALID_SOCKET) {
                closesocket(listenSock);
                return;
            }

            std::cout << "Connection accepted!" << std::endl;

            char buffer[1024];
            int bytesRecv;
            while ((bytesRecv = recv(clientSock, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytesRecv] = '\0';
                std::cout << buffer;
            }

            closesocket(clientSock);
            closesocket(listenSock);
        } else {
            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET) {
                printError("Failed to create socket");
                return;
            }

            struct addrinfo hints = {0}, *result = NULL;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
                printError("Could not resolve host");
                closesocket(sock);
                return;
            }

            std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;

            if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                printError("Connection failed");
                freeaddrinfo(result);
                closesocket(sock);
                return;
            }

            std::cout << "Connected!" << std::endl;
            freeaddrinfo(result);

            char buffer[1024];
            int bytesRecv;
            while ((bytesRecv = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytesRecv] = '\0';
                std::cout << buffer;
            }

            closesocket(sock);
        }
    }
};

#endif
