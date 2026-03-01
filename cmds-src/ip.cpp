// g++ -O3 -o ../cmds/ip.exe ip.cpp -liphlpapi -lws2_32
#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <iomanip>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

void printError(const std::string& msg) {
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cerr << "Linuxify: " << msg << "\n";
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void showIP() {
    ULONG outBufLen = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
    
    DWORD dwRetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen);
    
    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
        dwRetVal = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen);
    }
    
    if (dwRetVal == NO_ERROR) {
        PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
        int idx = 1;
        while (pCurrAddresses) {
            std::cout << idx++ << ": " << pCurrAddresses->AdapterName << ": ";
            
            // Simplified status
            if (pCurrAddresses->OperStatus == IfOperStatusUp) {
                std::cout << "<UP,LOWER_UP> ";
            } else {
                std::cout << "<DOWN> ";
            }
            
            wprintf(L"mtu %d qdisc %s state %s group default qlen 1000\n",
                    pCurrAddresses->Mtu, 
                    pCurrAddresses->OperStatus == IfOperStatusUp ? L"noqueue" : L"noop",
                    pCurrAddresses->OperStatus == IfOperStatusUp ? L"UP" : L"DOWN");
            
            std::cout << "    link/ether ";
            for (DWORD i = 0; i < pCurrAddresses->PhysicalAddressLength; i++) {
                if (i == (pCurrAddresses->PhysicalAddressLength - 1))
                    printf("%.2X\n", (int)pCurrAddresses->PhysicalAddress[i]);
                else
                    printf("%.2X:", (int)pCurrAddresses->PhysicalAddress[i]);
            }
            
            PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
            while (pUnicast != NULL) {
                char ipString[INET6_ADDRSTRLEN];
                DWORD ipStringLen = INET6_ADDRSTRLEN;
                
                sockaddr* addr = pUnicast->Address.lpSockaddr;
                
                if (addr->sa_family == AF_INET) {
                    sockaddr_in* ipv4 = (sockaddr_in*)addr;
                    inet_ntop(AF_INET, &(ipv4->sin_addr), ipString, ipStringLen);
                    std::cout << "    inet " << ipString << "/" << (int)pUnicast->OnLinkPrefixLength << " scope global " << pCurrAddresses->AdapterName << "\n";
                } else if (addr->sa_family == AF_INET6) {
                    sockaddr_in6* ipv6 = (sockaddr_in6*)addr;
                    inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipString, ipStringLen);
                    std::cout << "    inet6 " << ipString << "/" << (int)pUnicast->OnLinkPrefixLength << " scope link \n";
                }
                
                pUnicast = pUnicast->Next;
            }
            std::cout << "\n";
            pCurrAddresses = pCurrAddresses->Next;
        }
    } else {
        printError("Call to GetAdaptersAddresses failed with error: " + std::to_string(dwRetVal));
    }
    
    if (pAddresses) {
        free(pAddresses);
    }
}

int main(int argc, char* argv[]) {
    // Only handling `ip a` or `ip addr` right now.
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "a" || arg == "addr" || arg == "address") {
            showIP();
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ip [ OPTIONS ] OBJECT { COMMAND | help }\n";
            std::cout << "where  OBJECT := { link | address | addrlabel | route | rule | neigh | ntable |\n";
            std::cout << "                   tunnel | tuntap | maddress | mroute | mrule | monitor | xfrm |\n";
            std::cout << "                   netns | l2tp | fou | macsec | tcp_metrics | token | netconf | vrf | \n";
            std::cout << "                   sr | nexthop | mptcp }\n";
            std::cout << "       OPTIONS := { -V[ersion] | -s[tatistics] | -d[etails] | -r[esolve] |\n";
            std::cout << "                    -h[uman-readable] | -iec | -j[son] | -p[retty] |\n";
            std::cout << "                    -f[amily] { inet | inet6 | ipx | dnet | mpls | bridge | link } |\n";
            std::cout << "                    -4 | -6 | -I | -D | -M | -B | -0 |\n";
            std::cout << "                    -l[oops] { maximum-addr-flush-attempts } | -br[ief] |\n";
            std::cout << "                    -o[neline] | -t[imestamp] | -ts[hort] | -b[atch] [filename] |\n";
            std::cout << "                    -rc[vbuf] [size] | -n[etns] name | -N[umeric] | -a[ll] |\n";
            std::cout << "                    -c[olor]}\n";
            return 0;
        } else {
            // Unimplemented IP args
            printError("ip: object '" + arg + "' is unknown, try 'ip help'.");
            return 1;
        }
    }
    
    // Default action without args
    std::cout << "Usage: ip [ OPTIONS ] OBJECT { COMMAND | help }\n";
    std::cout << "where  OBJECT := { link | address | route | ... }\n";
    return 1;
}
