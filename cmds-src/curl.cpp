// g++ -std=c++17 -static -o curl.exe curl.cpp -lwininet -lws2_32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <map>
#include <io.h>
#include <fcntl.h>
#include <regex>
#include <queue>
#include <mutex>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")


class HtmlParser {
public:
    static std::string extractTitle(const std::string& html) {
        std::regex titleRegex("<title[^>]*>([^<]*)</title>", std::regex::icase);
        std::smatch match;
        if (std::regex_search(html, match, titleRegex)) {
            return match[1].str();
        }
        return "";
    }
    
    static std::vector<std::pair<std::string, std::string>> extractMetaTags(const std::string& html) {
        std::vector<std::pair<std::string, std::string>> metas;
        std::regex metaRegex("<meta[^>]*>", std::regex::icase);
        std::sregex_iterator it(html.begin(), html.end(), metaRegex);
        std::sregex_iterator end;
        
        while (it != end) {
            std::string tag = (*it)[0].str();
            std::string name, content;
            
            std::regex nameRegex("name=[\"']([^\"']*)[\"']", std::regex::icase);
            std::regex propRegex("property=[\"']([^\"']*)[\"']", std::regex::icase);
            std::regex contentRegex("content=[\"']([^\"']*)[\"']", std::regex::icase);
            
            std::smatch m;
            if (std::regex_search(tag, m, nameRegex)) name = m[1].str();
            else if (std::regex_search(tag, m, propRegex)) name = m[1].str();
            if (std::regex_search(tag, m, contentRegex)) content = m[1].str();
            
            if (!name.empty()) metas.push_back({name, content});
            ++it;
        }
        return metas;
    }
    
    static std::vector<std::string> extractLinks(const std::string& html) {
        std::vector<std::string> links;
        std::regex linkRegex("href=[\"']([^\"']*)[\"']", std::regex::icase);
        std::sregex_iterator it(html.begin(), html.end(), linkRegex);
        std::sregex_iterator end;
        
        while (it != end) {
            links.push_back((*it)[1].str());
            ++it;
        }
        return links;
    }
    
    static std::string stripTags(const std::string& html) {
        std::string result;
        bool inTag = false;
        bool inScript = false;
        bool inStyle = false;
        
        for (size_t i = 0; i < html.length(); i++) {
            if (html[i] == '<') {
                inTag = true;
                std::string tagStart = html.substr(i, 10);
                std::transform(tagStart.begin(), tagStart.end(), tagStart.begin(), ::tolower);
                if (tagStart.find("<script") == 0) inScript = true;
                if (tagStart.find("<style") == 0) inStyle = true;
                if (tagStart.find("</script") == 0) inScript = false;
                if (tagStart.find("</style") == 0) inStyle = false;
            } else if (html[i] == '>') {
                inTag = false;
            } else if (!inTag && !inScript && !inStyle) {
                result += html[i];
            }
        }
        return result;
    }
    
    static std::string decodeEntities(const std::string& html) {
        std::string result = html;
        
        size_t pos = 0;
        while ((pos = result.find("&amp;", pos)) != std::string::npos) {
            result.replace(pos, 5, "&");
            pos += 1;
        }
        pos = 0;
        while ((pos = result.find("&lt;", pos)) != std::string::npos) {
            result.replace(pos, 4, "<");
            pos += 1;
        }
        pos = 0;
        while ((pos = result.find("&gt;", pos)) != std::string::npos) {
            result.replace(pos, 4, ">");
            pos += 1;
        }
        pos = 0;
        while ((pos = result.find("&quot;", pos)) != std::string::npos) {
            result.replace(pos, 6, "\"");
            pos += 1;
        }
        pos = 0;
        while ((pos = result.find("&apos;", pos)) != std::string::npos) {
            result.replace(pos, 6, "'");
            pos += 1;
        }
        pos = 0;
        while ((pos = result.find("&nbsp;", pos)) != std::string::npos) {
            result.replace(pos, 6, " ");
            pos += 1;
        }
        pos = 0;
        while ((pos = result.find("&#", pos)) != std::string::npos) {
            size_t end = result.find(';', pos);
            if (end != std::string::npos) {
                std::string numStr = result.substr(pos + 2, end - pos - 2);
                int code = 0;
                try {
                    if (!numStr.empty() && (numStr[0] == 'x' || numStr[0] == 'X')) {
                        code = std::stoi(numStr.substr(1), nullptr, 16);
                    } else if (!numStr.empty()) {
                        code = std::stoi(numStr);
                    }
                } catch (...) { code = 0; }
                if (code > 0 && code < 128) {
                    result.replace(pos, end - pos + 1, std::string(1, (char)code));
                } else {
                    result.replace(pos, end - pos + 1, "?");
                }
            } else {
                pos++;
            }
        }
        
        return result;
    }
};

class CurlClient {
private:
    HINTERNET hInternet;
    HINTERNET hConnect;
    HINTERNET hRequest;
    
    std::string userAgent;
    std::string username;
    std::string password;
    std::string proxyServer;
    std::string proxyUserPass;
    std::string cookieFile;
    std::string cookieJar;
    std::string referer;
    std::string certFile;
    std::string outputFile;
    std::string uploadFile;
    std::string method;
    std::string postData;
    std::string contentType;
    std::string range;
    
    std::string sslCert;
    std::string sslCertType;
    std::string sslKey;
    std::string sslKeyType;
    std::string sslKeyPasswd;
    std::string caCert;
    std::string caPath;
    std::string crlFile;
    std::string pinnedPublicKey;
    std::string ciphers;
    std::string tls13Ciphers;
    std::string sslEngines;
    std::string randomFile;
    std::string egdSocket;
    
    std::string proxyType;
    std::string proxyAuth;
    std::string proxyCert;
    std::string proxyKey;
    std::string proxyCaPath;
    std::string proxyTlsUser;
    std::string proxyTlsPass;
    std::string socksProxy;
    std::string noProxy;
    std::string preProxy;
    
    std::string ftpAccount;
    std::string ftpAlternativeUser;
    std::string ftpPort;
    std::string ftpMethod;
    std::string mailFrom;
    std::string mailRcpt;
    std::string mailAuth;
    
    std::string dnsInterface;
    std::string dnsIpv4Addr;
    std::string dnsIpv6Addr;
    std::string dnsServers;
    std::string resolveHosts;
    std::string interface_;
    std::string localPort;
    std::string localPortRange;
    
    std::string oauth2Bearer;
    std::string awsSigv4;
    std::string delegation;
    std::string serviceAccount;
    std::string ntlmDomain;
    std::string loginOptions;
    std::string saslAuthz;
    std::string saslIr;
    
    std::string writeOutFormat;
    std::string stderrFile;
    std::string traceFile;
    std::string traceAsciiFile;
    std::string netrc;
    std::string netrcFile;
    
    std::string unixSocket;
    std::string abstractUnixSocket;
    std::string altSvc;
    std::string hstsFile;
    std::string dohUrl;
    std::string happyEyeballsTimeout;
    
    std::string tlsAuthType;
    std::string tlsUser;
    std::string tlsPassword;
    std::string http2PriorKnowledge;
    std::string requestTarget;
    std::string expect100Timeout;
    
    std::vector<std::string> headers;
    std::vector<std::pair<std::string, std::string>> formData;
    std::vector<std::string> telnetOptions;
    std::vector<std::string> quotes;
    std::vector<std::string> postQuotes;
    std::vector<std::string> preQuotes;
    std::vector<std::string> connectTo;
    
    bool verbose;
    bool silent;
    bool showHeaders;
    bool includeHeaders;
    bool followRedirects;
    bool insecure;
    bool showProgress;
    bool resumeDownload;
    bool failOnError;
    bool compressedResponse;
    bool headOnly;
    bool uploadMode;
    bool appendOutput;
    bool createDirs;
    bool remoteTime;
    bool enableFormattedOutput;
    
    bool sslVerifyPeer;
    bool sslVerifyHost;
    bool sslVerifyStatus;
    bool certStatus;
    bool sslNoRevoke;
    bool sslAutoClientCert;
    bool sslRevokeDefaults;
    bool proxyInsecure;
    bool proxySslVerifyPeer;
    bool proxySslVerifyHost;
    
    bool tlsv1;
    bool tlsv1_0;
    bool tlsv1_1;
    bool tlsv1_2;
    bool tlsv1_3;
    bool sslv2;
    bool sslv3;
    bool sslAllowBeast;
    bool sslNativeCA;
    bool httpNegotiate;
    bool http1_0;
    bool http1_1;
    bool http2;
    bool http3;
    bool http3Only;
    bool trEncoding;
    bool rawOutput;
    bool noBuffer;
    bool noKeepalive;
    bool noSessionId;
    bool noNpn;
    bool noAlpn;
    
    bool tcp_nodelay;
    bool tcp_fastopen;
    bool ipv4_only;
    bool ipv6_only;
    bool pathAsIs;
    bool disableEpsv;
    bool disableEprt;
    bool ftpSkipPasvIp;
    bool ftpPasv;
    bool ftpCreateDirs;
    bool ftpPret;
    bool ftpSslReqd;
    bool ftpSslCcc;
    bool ftpSslControl;
    bool sshCompression;
    bool tftp_blksize;
    bool tftp_noOptions;
    
    bool authBasic;
    bool authDigest;
    bool authNtlm;
    bool authNtlmWb;
    bool authNegotiate_;
    bool authAnyauth;
    bool authAnysafe;
    bool authKrb;
    bool authSpnego;
    bool authGssapiDelg;
    bool authGssapiNd;
    
    bool globoff;
    bool useAscii;
    bool listOnly;
    bool quote_;
    bool clobber;
    bool xferBraces;
    bool parallelImmediate;
    bool parallelMax;
    bool locationTrusted;
    bool postRedir;
    bool junkSessionCookies;
    bool metalink;
    bool ignoreContentLength;
    bool styled;
    bool styledOutput;
    bool suppressConnectHeaders;
    
    int maxRedirects;
    int timeout;
    int connectTimeout;
    int retryCount;
    int retryDelay;
    int retryMaxTime;
    long maxFileSize;
    long speedLimit;
    long speedTime;
    long limitRate;
    long maxTime;
    long expectResponseWithin;
    
    int keepAliveTime;
    int keepAliveInterval;
    int lowSpeedLimit;
    int lowSpeedTime;
    int maxConnects;
    int parallelConnections;
    int bufferSize;
    int uploadBufferSize;
    long maxRecvSpeed;
    long maxSendSpeed;
    
    int tlsMaxVersion;
    int sslSessionReuse;
    int certsInfo;
    int proxyBasic;
    int proxyDigest;
    int proxyNtlm;
    int proxyNegotiate;
    int proxyAnyauth;
    int socks5Auth;

    void cleanup() {
        if (hRequest) { InternetCloseHandle(hRequest); hRequest = NULL; }
        if (hConnect) { InternetCloseHandle(hConnect); hConnect = NULL; }
        if (hInternet) { InternetCloseHandle(hInternet); hInternet = NULL; }
    }

    bool parseUrl(const std::string& url, std::string& protocol, std::string& host, 
                  std::string& path, INTERNET_PORT& port, std::string& user, std::string& pass) {
        size_t protoEnd = url.find("://");
        if (protoEnd == std::string::npos) {
            protocol = "http";
            protoEnd = 0;
        } else {
            protocol = url.substr(0, protoEnd);
            std::transform(protocol.begin(), protocol.end(), protocol.begin(), ::tolower);
            protoEnd += 3;
        }
        
        std::string remainder = url.substr(protoEnd);
        
        size_t atPos = remainder.find('@');
        if (atPos != std::string::npos) {
            std::string credentials = remainder.substr(0, atPos);
            remainder = remainder.substr(atPos + 1);
            size_t colonPos = credentials.find(':');
            if (colonPos != std::string::npos) {
                user = credentials.substr(0, colonPos);
                pass = credentials.substr(colonPos + 1);
            } else {
                user = credentials;
            }
        }
        
        size_t hostEnd = remainder.find('/');
        if (hostEnd == std::string::npos) {
            host = remainder;
            path = "/";
        } else {
            host = remainder.substr(0, hostEnd);
            path = remainder.substr(hostEnd);
        }
        
        size_t portPos = host.find(':');
        if (portPos != std::string::npos) {
            try { port = (INTERNET_PORT)std::stoi(host.substr(portPos + 1)); } catch (...) {}
            host = host.substr(0, portPos);
        } else {
            if (protocol == "https" || protocol == "ftps") port = INTERNET_DEFAULT_HTTPS_PORT;
            else if (protocol == "ftp") port = INTERNET_DEFAULT_FTP_PORT;
            else port = INTERNET_DEFAULT_HTTP_PORT;
        }
        
        return !host.empty();
    }

    std::string getLastErrorString() {
        DWORD err = GetLastError();
        char buf[512];
        DWORD result = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE,
                                      GetModuleHandleA("wininet.dll"), err, 0, buf, sizeof(buf), NULL);
        if (result == 0) {
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buf, sizeof(buf), NULL);
        }
        return std::string(buf);
    }

    void printProgress(DWORD current, DWORD total) {
        if (silent || !showProgress) return;
        
        int percent = total > 0 ? (int)((current * 100) / total) : 0;
        int barWidth = 50;
        int filled = (percent * barWidth) / 100;
        
        std::cerr << "\r[";
        for (int i = 0; i < barWidth; i++) {
            if (i < filled) std::cerr << "#";
            else std::cerr << "-";
        }
        std::cerr << "] " << std::setw(3) << percent << "% ";
        
        if (current >= 1024 * 1024) {
            std::cerr << std::fixed << std::setprecision(1) << (current / (1024.0 * 1024.0)) << " MB";
        } else if (current >= 1024) {
            std::cerr << std::fixed << std::setprecision(1) << (current / 1024.0) << " KB";
        } else {
            std::cerr << current << " B";
        }
        
        if (total > 0) {
            std::cerr << " / ";
            if (total >= 1024 * 1024) {
                std::cerr << std::fixed << std::setprecision(1) << (total / (1024.0 * 1024.0)) << " MB";
            } else if (total >= 1024) {
                std::cerr << std::fixed << std::setprecision(1) << (total / 1024.0) << " KB";
            } else {
                std::cerr << total << " B";
            }
        }
        std::cerr << std::flush;
    }

    std::string urlEncode(const std::string& str) {
        std::ostringstream encoded;
        for (char c : str) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded << c;
            } else {
                encoded << '%' << std::uppercase << std::hex << std::setw(2) 
                        << std::setfill('0') << (int)(unsigned char)c;
            }
        }
        return encoded.str();
    }

    std::string buildMultipartData(const std::string& boundary) {
        std::ostringstream data;
        
        for (const auto& field : formData) {
            data << "--" << boundary << "\r\n";
            
            if (field.first[0] == '@') {
                std::string filename = field.first.substr(1);
                std::ifstream file(filename, std::ios::binary);
                if (file) {
                    size_t pos = filename.find_last_of("/\\");
                    std::string basename = (pos != std::string::npos) ? filename.substr(pos + 1) : filename;
                    
                    data << "Content-Disposition: form-data; name=\"" << field.second 
                         << "\"; filename=\"" << basename << "\"\r\n";
                    data << "Content-Type: application/octet-stream\r\n\r\n";
                    data << file.rdbuf();
                    data << "\r\n";
                }
            } else {
                data << "Content-Disposition: form-data; name=\"" << field.second << "\"\r\n\r\n";
                data << field.first << "\r\n";
            }
        }
        
        if (!uploadFile.empty()) {
            std::ifstream file(uploadFile, std::ios::binary);
            if (file) {
                size_t pos = uploadFile.find_last_of("/\\");
                std::string basename = (pos != std::string::npos) ? uploadFile.substr(pos + 1) : uploadFile;
                
                data << "--" << boundary << "\r\n";
                data << "Content-Disposition: form-data; name=\"file\"; filename=\"" << basename << "\"\r\n";
                data << "Content-Type: application/octet-stream\r\n\r\n";
                data << file.rdbuf();
                data << "\r\n";
            }
        }
        
        data << "--" << boundary << "--\r\n";
        return data.str();
    }

    int executeFTP(const std::string& host, INTERNET_PORT port, const std::string& path,
                   const std::string& user, const std::string& pass, bool secure) {
        std::string ftpUser = user.empty() ? username : user;
        std::string ftpPass = pass.empty() ? password : pass;
        if (ftpUser.empty()) ftpUser = "anonymous";
        if (ftpPass.empty()) ftpPass = "curl@linuxify";

        DWORD flags = INTERNET_FLAG_PASSIVE;
        if (secure) flags |= INTERNET_FLAG_SECURE;

        hConnect = InternetConnectA(hInternet, host.c_str(), port, 
                                    ftpUser.c_str(), ftpPass.c_str(),
                                    INTERNET_SERVICE_FTP, flags, 0);
        if (!hConnect) {
            if (!silent) std::cerr << "curl: FTP connection failed: " << getLastErrorString();
            return 1;
        }

        if (verbose) {
            std::cerr << "* Connected to " << host << " port " << port << "\n";
            std::cerr << "* Logged in as " << ftpUser << "\n";
        }

        if (uploadMode && !uploadFile.empty()) {
            std::string remotePath = path.empty() ? "/" : path;
            size_t pos = uploadFile.find_last_of("/\\");
            std::string remoteFile = remotePath;
            if (remotePath.back() == '/') {
                remoteFile += (pos != std::string::npos) ? uploadFile.substr(pos + 1) : uploadFile;
            }

            if (verbose) std::cerr << "* Uploading to " << remoteFile << "\n";

            if (!FtpPutFileA(hConnect, uploadFile.c_str(), remoteFile.c_str(),
                            FTP_TRANSFER_TYPE_BINARY, 0)) {
                if (!silent) std::cerr << "curl: FTP upload failed: " << getLastErrorString();
                return 1;
            }

            if (!silent) std::cout << "File uploaded successfully\n";
        } else {
            std::string remoteFile = path.empty() ? "/" : path;
            if (remoteFile[0] == '/') remoteFile = remoteFile.substr(1);

            std::string localFile = outputFile;
            if (localFile.empty()) {
                size_t pos = remoteFile.find_last_of('/');
                localFile = (pos != std::string::npos) ? remoteFile.substr(pos + 1) : remoteFile;
                if (localFile.empty()) localFile = "ftp_download";
            }

            if (verbose) std::cerr << "* Downloading " << remoteFile << " to " << localFile << "\n";

            if (!FtpGetFileA(hConnect, remoteFile.c_str(), localFile.c_str(),
                            FALSE, FILE_ATTRIBUTE_NORMAL, 
                            FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_RELOAD, 0)) {
                if (!silent) std::cerr << "curl: FTP download failed: " << getLastErrorString();
                return 1;
            }

            if (!silent) {
                WIN32_FIND_DATAA findData;
                HANDLE hFind = FindFirstFileA(localFile.c_str(), &findData);
                if (hFind != INVALID_HANDLE_VALUE) {
                    std::cout << "Downloaded " << findData.nFileSizeLow << " bytes to " << localFile << "\n";
                    FindClose(hFind);
                }
            }
        }

        return 0;
    }

    int executeHTTP(const std::string& url, const std::string& protocol, const std::string& host,
                    INTERNET_PORT port, std::string path, const std::string& urlUser, const std::string& urlPass) {
        
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (protocol == "https") {
            flags |= INTERNET_FLAG_SECURE;
            if (insecure) {
                flags |= INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
            }
        }
        if (!followRedirects) {
            flags |= INTERNET_FLAG_NO_AUTO_REDIRECT;
        }

        std::string currentHost = host;
        INTERNET_PORT currentPort = port;
        std::string currentProtocol = protocol;
        int redirectCount = 0;

        while (true) {
            hConnect = InternetConnectA(hInternet, currentHost.c_str(), currentPort, 
                                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
            if (!hConnect) {
                if (!silent) std::cerr << "curl: could not connect to " << currentHost << ": " << getLastErrorString();
                return 1;
            }

            const char* acceptTypes[] = {"*/*", NULL};
            std::string effectiveMethod = method;
            if (headOnly) effectiveMethod = "HEAD";
            
            hRequest = HttpOpenRequestA(hConnect, effectiveMethod.c_str(), path.c_str(), 
                                        "HTTP/1.1", referer.empty() ? NULL : referer.c_str(), 
                                        acceptTypes, flags, 0);
            if (!hRequest) {
                if (!silent) std::cerr << "curl: failed to open request: " << getLastErrorString();
                return 1;
            }

            std::string authUser = urlUser.empty() ? username : urlUser;
            std::string authPass = urlPass.empty() ? password : urlPass;
            
            if (!authUser.empty()) {
                InternetSetOptionA(hRequest, INTERNET_OPTION_USERNAME, (LPVOID)authUser.c_str(), (DWORD)authUser.length());
                if (!authPass.empty()) {
                    InternetSetOptionA(hRequest, INTERNET_OPTION_PASSWORD, (LPVOID)authPass.c_str(), (DWORD)authPass.length());
                }
            }

            for (const auto& header : headers) {
                HttpAddRequestHeadersA(hRequest, header.c_str(), -1, 
                                       HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
            }

            if (!contentType.empty()) {
                std::string ctHeader = "Content-Type: " + contentType;
                HttpAddRequestHeadersA(hRequest, ctHeader.c_str(), -1, HTTP_ADDREQ_FLAG_ADD);
            }

            if (compressedResponse) {
                HttpAddRequestHeadersA(hRequest, "Accept-Encoding: gzip, deflate", -1, HTTP_ADDREQ_FLAG_ADD);
            }

            if (!range.empty()) {
                std::string rangeHeader = "Range: bytes=" + range;
                HttpAddRequestHeadersA(hRequest, rangeHeader.c_str(), -1, HTTP_ADDREQ_FLAG_ADD);
            }

            if (timeout > 0) {
                DWORD dwTimeout = (DWORD)timeout;
                InternetSetOptionA(hRequest, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
                InternetSetOptionA(hRequest, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
                InternetSetOptionA(hRequest, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
                InternetSetOptionA(hRequest, INTERNET_OPTION_DATA_SEND_TIMEOUT, &dwTimeout, sizeof(dwTimeout));
            }
            
            if (connectTimeout > 0) {
                DWORD dwConnectTimeout = (DWORD)connectTimeout;
                InternetSetOptionA(hRequest, INTERNET_OPTION_CONNECT_TIMEOUT, &dwConnectTimeout, sizeof(dwConnectTimeout));
            }

            if (!noKeepalive) {
                DWORD dwKeepalive = 1;
                InternetSetOptionA(hRequest, INTERNET_OPTION_HTTP_DECODING, &dwKeepalive, sizeof(dwKeepalive));
            }

            if (protocol == "https") {
                DWORD secFlags = 0;
                DWORD secLen = sizeof(secFlags);
                InternetQueryOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, &secLen);
                
                if (!sslVerifyPeer || insecure) {
                    secFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
                }
                if (!sslVerifyHost || insecure) {
                    secFlags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
                }
                if (sslNoRevoke) {
                    secFlags |= SECURITY_FLAG_IGNORE_REVOCATION;
                }
                if (insecure) {
                    secFlags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
                    secFlags |= SECURITY_FLAG_IGNORE_WRONG_USAGE;
                }
                
                InternetSetOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
            }

            if (!proxyServer.empty()) {
                INTERNET_PROXY_INFO proxyInfo;
                proxyInfo.dwAccessType = INTERNET_OPEN_TYPE_PROXY;
                proxyInfo.lpszProxy = proxyServer.c_str();
                proxyInfo.lpszProxyBypass = noProxy.empty() ? "<local>" : noProxy.c_str();
                InternetSetOptionA(hRequest, INTERNET_OPTION_PROXY, &proxyInfo, sizeof(proxyInfo));
                
                if (!proxyUserPass.empty()) {
                    size_t colonPos = proxyUserPass.find(':');
                    if (colonPos != std::string::npos) {
                        std::string proxyUser = proxyUserPass.substr(0, colonPos);
                        std::string proxyPass = proxyUserPass.substr(colonPos + 1);
                        InternetSetOptionA(hRequest, INTERNET_OPTION_PROXY_USERNAME, (LPVOID)proxyUser.c_str(), (DWORD)proxyUser.length());
                        InternetSetOptionA(hRequest, INTERNET_OPTION_PROXY_PASSWORD, (LPVOID)proxyPass.c_str(), (DWORD)proxyPass.length());
                    }
                }
            }

            if (authDigest) {
                DWORD authScheme = INTERNET_FLAG_PRAGMA_NOCACHE;
                InternetSetOptionA(hRequest, INTERNET_OPTION_HTTP_DECODING, &authScheme, sizeof(authScheme));
            }

            if (!oauth2Bearer.empty()) {
                std::string authHeader = "Authorization: Bearer " + oauth2Bearer;
                HttpAddRequestHeadersA(hRequest, authHeader.c_str(), -1, HTTP_ADDREQ_FLAG_ADD);
            }

            if (!cookieFile.empty()) {
                std::ifstream cf(cookieFile);
                if (cf.is_open()) {
                    std::string line;
                    while (std::getline(cf, line)) {
                        if (!line.empty() && line[0] != '#') {
                            std::istringstream iss(line);
                            std::string domain, flag, path, secure, expiry, name, value;
                            if (iss >> domain >> flag >> path >> secure >> expiry >> name >> value) {
                                size_t hostStart = currentHost.find(domain);
                                if (hostStart != std::string::npos || domain.find(currentHost) != std::string::npos) {
                                    std::string cookieHeader = "Cookie: " + name + "=" + value;
                                    HttpAddRequestHeadersA(hRequest, cookieHeader.c_str(), -1, HTTP_ADDREQ_FLAG_ADD);
                                }
                            }
                        }
                    }
                    cf.close();
                }
            }

            if (!resolveHosts.empty()) {
                size_t colonPos1 = resolveHosts.find(':');
                size_t colonPos2 = resolveHosts.find(':', colonPos1 + 1);
                if (colonPos1 != std::string::npos && colonPos2 != std::string::npos) {
                    std::string resolveHost = resolveHosts.substr(0, colonPos1);
                    std::string resolvePort = resolveHosts.substr(colonPos1 + 1, colonPos2 - colonPos1 - 1);
                    std::string resolveAddr = resolveHosts.substr(colonPos2 + 1);
                    if (resolveHost == currentHost || resolveHost == "*") {
                        currentHost = resolveAddr;
                    }
                }
            }

            std::string hostHeader = "Host: " + host;
            HttpAddRequestHeadersA(hRequest, hostHeader.c_str(), -1, HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);


            if (verbose) {
                std::cerr << "> " << effectiveMethod << " " << path << " HTTP/1.1\n";
                std::cerr << "> Host: " << currentHost << "\n";
                std::cerr << "> User-Agent: " << userAgent << "\n";
                std::cerr << "> Accept: */*\n";
                for (const auto& h : headers) std::cerr << "> " << h << "\n";
                std::cerr << ">\n";
            }

            std::string sendBody;
            if (!formData.empty() || uploadMode) {
                std::string boundary = "----CurlFormBoundary" + std::to_string(time(NULL));
                std::string ctHeader = "Content-Type: multipart/form-data; boundary=" + boundary;
                HttpAddRequestHeadersA(hRequest, ctHeader.c_str(), -1, HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
                sendBody = buildMultipartData(boundary);
            } else if (!postData.empty()) {
                sendBody = postData;
            }

            BOOL result;
            if (!sendBody.empty()) {
                result = HttpSendRequestA(hRequest, NULL, 0, (LPVOID)sendBody.c_str(), (DWORD)sendBody.length());
            } else {
                result = HttpSendRequestA(hRequest, NULL, 0, NULL, 0);
            }

            if (!result) {
                DWORD err = GetLastError();
                if (insecure && (err == ERROR_INTERNET_INVALID_CA || err == ERROR_INTERNET_SEC_CERT_DATE_INVALID ||
                    err == ERROR_INTERNET_SEC_CERT_CN_INVALID || err == ERROR_INTERNET_SEC_CERT_REVOKED)) {
                    DWORD secFlags;
                    DWORD len = sizeof(secFlags);
                    InternetQueryOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, &len);
                    secFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_REVOCATION;
                    InternetSetOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
                    
                    if (!sendBody.empty()) {
                        result = HttpSendRequestA(hRequest, NULL, 0, (LPVOID)sendBody.c_str(), (DWORD)sendBody.length());
                    } else {
                        result = HttpSendRequestA(hRequest, NULL, 0, NULL, 0);
                    }
                }
                
                if (!result) {
                    if (!silent) std::cerr << "curl: request failed: " << getLastErrorString();
                    return 1;
                }
            }

            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);

            if (verbose) {
                char statusText[256];
                DWORD textSize = sizeof(statusText);
                HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_TEXT, statusText, &textSize, NULL);
                std::cerr << "< HTTP/1.1 " << statusCode << " " << statusText << "\n";
            }

            if (failOnError && statusCode >= 400) {
                if (!silent) std::cerr << "curl: HTTP error " << statusCode << "\n";
                return (int)(statusCode / 100);
            }

            if (followRedirects && (statusCode == 301 || statusCode == 302 || statusCode == 303 || 
                                    statusCode == 307 || statusCode == 308)) {
                if (++redirectCount > maxRedirects) {
                    if (!silent) std::cerr << "curl: maximum redirects (" << maxRedirects << ") exceeded\n";
                    return 1;
                }

                char location[4096];
                DWORD locSize = sizeof(location);
                if (HttpQueryInfoA(hRequest, HTTP_QUERY_LOCATION, location, &locSize, NULL)) {
                    if (verbose) std::cerr << "< Location: " << location << "\n* Following redirect...\n";
                    
                    InternetCloseHandle(hRequest); hRequest = NULL;
                    InternetCloseHandle(hConnect); hConnect = NULL;

                    std::string newUrl = location;
                    if (newUrl.find("://") == std::string::npos) {
                        if (newUrl[0] == '/') {
                            path = newUrl;
                            continue;
                        } else {
                            size_t lastSlash = path.find_last_of('/');
                            path = (lastSlash != std::string::npos ? path.substr(0, lastSlash + 1) : "/") + newUrl;
                            continue;
                        }
                    }
                    
                    std::string dummy1, dummy2;
                    parseUrl(newUrl, currentProtocol, currentHost, path, currentPort, dummy1, dummy2);
                    flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
                    if (currentProtocol == "https") {
                        flags |= INTERNET_FLAG_SECURE;
                        if (insecure) flags |= INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
                    }
                    continue;
                }
            }

            break;
        }

        DWORD contentLength = 0;
        DWORD clSize = sizeof(contentLength);
        HttpQueryInfoA(hRequest, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &clSize, NULL);

        if (showHeaders || includeHeaders) {
            char headerBuf[16384];
            DWORD headerSize = sizeof(headerBuf);
            if (HttpQueryInfoA(hRequest, HTTP_QUERY_RAW_HEADERS_CRLF, headerBuf, &headerSize, NULL)) {
                if (verbose) {
                    std::string hdrStr = headerBuf;
                    std::istringstream iss(hdrStr);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (!line.empty()) std::cerr << "< " << line << "\n";
                    }
                    std::cerr << "<\n";
                } else {
                    std::cout << headerBuf;
                }
            }
            if (headOnly) return 0;
        }

        std::ostream* out = &std::cout;
        std::ofstream fileOut;
        
        if (!outputFile.empty()) {
            std::ios_base::openmode mode = std::ios::binary;
            if (appendOutput) mode |= std::ios::app;
            
            if (resumeDownload && !appendOutput) {
                std::ifstream existing(outputFile, std::ios::binary | std::ios::ate);
                if (existing) {
                    auto existingSize = existing.tellg();
                    if (existingSize > 0) {
                        range = std::to_string(existingSize) + "-";
                        mode |= std::ios::app;
                    }
                }
            }
            
            fileOut.open(outputFile, mode);
            if (!fileOut) {
                if (!silent) std::cerr << "curl: cannot open output file: " << outputFile << std::endl;
                return 1;
            }
            out = &fileOut;
        }

        std::ostringstream bodyStream;
        char buffer[65536];
        DWORD bytesRead;
        DWORD totalBytes = 0;
        clock_t startTime = clock();

        while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            bodyStream.write(buffer, bytesRead);
            totalBytes += bytesRead;
            
            if (showProgress && contentLength > 0) {
                printProgress(totalBytes, contentLength);
            }
            
            if (maxFileSize > 0 && totalBytes > (DWORD)maxFileSize) {
                if (!silent) std::cerr << "\ncurl: maximum file size exceeded\n";
                break;
            }
            
            if (limitRate > 0) {
                double elapsedSecs = (double)(clock() - startTime) / CLOCKS_PER_SEC;
                if (elapsedSecs > 0) {
                    double currentRate = (double)totalBytes / elapsedSecs;
                    if (currentRate > (double)limitRate) {
                        double targetTime = (double)totalBytes / (double)limitRate;
                        double sleepTime = (targetTime - elapsedSecs) * 1000.0;
                        if (sleepTime > 0 && sleepTime < 10000) {
                            Sleep((DWORD)sleepTime);
                        }
                    }
                }
            }
            
            if (maxTime > 0) {
                double elapsedSecs = (double)(clock() - startTime) / CLOCKS_PER_SEC;
                if (elapsedSecs > (double)maxTime) {
                    if (!silent) std::cerr << "\ncurl: operation timed out (max-time exceeded)\n";
                    break;
                }
            }
            
            if (lowSpeedLimit > 0 && lowSpeedTime > 0) {
                static clock_t lowSpeedStart = 0;
                static DWORD lowSpeedBytes = 0;
                
                double elapsed = (double)(clock() - startTime) / CLOCKS_PER_SEC;
                if (elapsed > 1.0) {
                    double rate = (double)totalBytes / elapsed;
                    if (rate < (double)lowSpeedLimit) {
                        if (lowSpeedStart == 0) {
                            lowSpeedStart = clock();
                            lowSpeedBytes = totalBytes;
                        } else {
                            double lowElapsed = (double)(clock() - lowSpeedStart) / CLOCKS_PER_SEC;
                            if (lowElapsed >= (double)lowSpeedTime) {
                                if (!silent) std::cerr << "\ncurl: operation too slow (less than " << lowSpeedLimit << " bytes/sec for " << lowSpeedTime << " seconds)\n";
                                break;
                            }
                        }
                    } else {
                        lowSpeedStart = 0;
                    }
                }
            }
            
            if (maxRecvSpeed > 0) {
                double elapsedSecs = (double)(clock() - startTime) / CLOCKS_PER_SEC;
                if (elapsedSecs > 0) {
                    double currentRate = (double)totalBytes / elapsedSecs;
                    if (currentRate > (double)maxRecvSpeed) {
                        double targetTime = (double)totalBytes / (double)maxRecvSpeed;
                        double sleepTime = (targetTime - elapsedSecs) * 1000.0;
                        if (sleepTime > 0 && sleepTime < 5000) {
                            Sleep((DWORD)sleepTime);
                        }
                    }
                }
            }
        }

        if (showProgress && contentLength > 0) {
            std::cerr << "\n";
        }

        std::string responseBody = bodyStream.str();
        double elapsed = (double)(clock() - startTime) / CLOCKS_PER_SEC;
        
        if (!outputFile.empty()) {
            std::ofstream fileOut(outputFile, std::ios::binary);
            if (fileOut) {
                fileOut.write(responseBody.c_str(), responseBody.length());
                fileOut.close();
                if (!silent) {
                    std::cerr << "  Saved " << totalBytes << " bytes to " << outputFile << "\n";
                }
            }
            
            if (!cookieJar.empty()) {
                char setCookieBuf[8192] = {0};
                DWORD cookieSize = sizeof(setCookieBuf);
                DWORD index = 0;
                std::ofstream jarFile(cookieJar, std::ios::app);
                if (jarFile.is_open()) {
                    while (HttpQueryInfoA(hRequest, HTTP_QUERY_SET_COOKIE, setCookieBuf, &cookieSize, &index)) {
                        std::string cookie(setCookieBuf);
                        time_t now = time(NULL);
                        std::string domain = host;
                        if (domain[0] != '.') domain = "." + domain;
                        
                        size_t eqPos = cookie.find('=');
                        size_t scPos = cookie.find(';');
                        if (eqPos != std::string::npos) {
                            std::string name = cookie.substr(0, eqPos);
                            std::string value = scPos != std::string::npos ? cookie.substr(eqPos + 1, scPos - eqPos - 1) : cookie.substr(eqPos + 1);
                            
                            bool secure = cookie.find("Secure") != std::string::npos || cookie.find("secure") != std::string::npos;
                            bool httpOnly = cookie.find("HttpOnly") != std::string::npos || cookie.find("httponly") != std::string::npos;
                            
                            size_t expPos = cookie.find("expires=");
                            if (expPos == std::string::npos) expPos = cookie.find("Expires=");
                            time_t expires = now + 86400 * 365;
                            
                            jarFile << domain << "\tTRUE\t/\t" << (secure ? "TRUE" : "FALSE") << "\t" << expires << "\t" << name << "\t" << value << "\n";
                        }
                        cookieSize = sizeof(setCookieBuf);
                    }
                    jarFile.close();
                }
            }
            
            return 0;
        }
        
        if (!enableFormattedOutput || silent) {
            std::cout.write(responseBody.c_str(), responseBody.length());
            return 0;
        }
        
        char headerBuf[16384] = {0};
        DWORD headerSize = sizeof(headerBuf);
        HttpQueryInfoA(hRequest, HTTP_QUERY_RAW_HEADERS_CRLF, headerBuf, &headerSize, NULL);
        
        ResponseMetadata meta = parseResponseHeaders(headerBuf);
        meta.contentLength = responseBody.length();
        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD colorGreen = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD colorCyan = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        WORD colorYellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD colorWhite = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        WORD colorRed = FOREGROUND_RED | FOREGROUND_INTENSITY;
        WORD colorMagenta = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        WORD colorDim = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        
        std::string rawHeaders(headerBuf);
        
        auto extractImages = [](const std::string& html) -> std::vector<std::map<std::string, std::string>> {
            std::vector<std::map<std::string, std::string>> images;
            std::regex imgRegex("<img[^>]*>", std::regex::icase);
            std::sregex_iterator it(html.begin(), html.end(), imgRegex);
            std::sregex_iterator end;
            
            while (it != end) {
                std::string tag = (*it)[0].str();
                std::map<std::string, std::string> img;
                
                std::regex srcRegex("src=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex altRegex("alt=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex classRegex("class=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex widthRegex("width=[\"']?([^\"'\\s>]*)[\"']?", std::regex::icase);
                std::regex heightRegex("height=[\"']?([^\"'\\s>]*)[\"']?", std::regex::icase);
                
                std::smatch m;
                if (std::regex_search(tag, m, srcRegex)) img["src"] = m[1].str();
                if (std::regex_search(tag, m, altRegex)) img["alt"] = m[1].str();
                if (std::regex_search(tag, m, classRegex)) img["class"] = m[1].str();
                if (std::regex_search(tag, m, widthRegex)) img["width"] = m[1].str();
                if (std::regex_search(tag, m, heightRegex)) img["height"] = m[1].str();
                
                if (!img.empty()) images.push_back(img);
                ++it;
            }
            return images;
        };
        
        auto extractForms = [](const std::string& html) -> std::vector<std::map<std::string, std::string>> {
            std::vector<std::map<std::string, std::string>> forms;
            std::regex formRegex("<form[^>]*>", std::regex::icase);
            std::sregex_iterator it(html.begin(), html.end(), formRegex);
            std::sregex_iterator end;
            
            while (it != end) {
                std::string tag = (*it)[0].str();
                std::map<std::string, std::string> form;
                
                std::regex actionRegex("action=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex methodRegex("method=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex idRegex("id=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex nameRegex("name=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex classRegex("class=[\"']([^\"']*)[\"']", std::regex::icase);
                
                std::smatch m;
                if (std::regex_search(tag, m, actionRegex)) form["action"] = m[1].str();
                if (std::regex_search(tag, m, methodRegex)) form["method"] = m[1].str();
                if (std::regex_search(tag, m, idRegex)) form["id"] = m[1].str();
                if (std::regex_search(tag, m, nameRegex)) form["name"] = m[1].str();
                if (std::regex_search(tag, m, classRegex)) form["class"] = m[1].str();
                
                if (!form.empty()) forms.push_back(form);
                ++it;
            }
            return forms;
        };
        
        auto extractInputFields = [](const std::string& html) -> std::vector<std::map<std::string, std::string>> {
            std::vector<std::map<std::string, std::string>> inputs;
            std::regex inputRegex("<input[^>]*>", std::regex::icase);
            std::sregex_iterator it(html.begin(), html.end(), inputRegex);
            std::sregex_iterator end;
            
            while (it != end) {
                std::string tag = (*it)[0].str();
                std::map<std::string, std::string> input;
                
                std::regex typeRegex("type=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex nameRegex("name=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex idRegex("id=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex valueRegex("value=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex classRegex("class=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex placeholderRegex("placeholder=[\"']([^\"']*)[\"']", std::regex::icase);
                
                std::smatch m;
                if (std::regex_search(tag, m, typeRegex)) input["type"] = m[1].str();
                if (std::regex_search(tag, m, nameRegex)) input["name"] = m[1].str();
                if (std::regex_search(tag, m, idRegex)) input["id"] = m[1].str();
                if (std::regex_search(tag, m, valueRegex)) input["value"] = m[1].str();
                if (std::regex_search(tag, m, classRegex)) input["class"] = m[1].str();
                if (std::regex_search(tag, m, placeholderRegex)) input["placeholder"] = m[1].str();
                
                if (!input.empty()) inputs.push_back(input);
                ++it;
            }
            return inputs;
        };
        
        auto extractLinksDetailed = [](const std::string& html) -> std::vector<std::map<std::string, std::string>> {
            std::vector<std::map<std::string, std::string>> links;
            std::regex linkRegex("<a[^>]*>([^<]*)</a>", std::regex::icase);
            std::sregex_iterator it(html.begin(), html.end(), linkRegex);
            std::sregex_iterator end;
            
            while (it != end) {
                std::string fullMatch = (*it)[0].str();
                std::string innerText = (*it)[1].str();
                std::map<std::string, std::string> link;
                
                std::regex hrefRegex("href=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex titleRegex("title=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex classRegex("class=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex targetRegex("target=[\"']([^\"']*)[\"']", std::regex::icase);
                std::regex relRegex("rel=[\"']([^\"']*)[\"']", std::regex::icase);
                
                std::smatch m;
                if (std::regex_search(fullMatch, m, hrefRegex)) link["href"] = m[1].str();
                if (std::regex_search(fullMatch, m, titleRegex)) link["title"] = m[1].str();
                if (std::regex_search(fullMatch, m, classRegex)) link["class"] = m[1].str();
                if (std::regex_search(fullMatch, m, targetRegex)) link["target"] = m[1].str();
                if (std::regex_search(fullMatch, m, relRegex)) link["rel"] = m[1].str();
                
                std::string cleanText;
                for (char c : innerText) {
                    if (c != '\n' && c != '\r' && c != '\t') cleanText += c;
                }
                while (!cleanText.empty() && cleanText[0] == ' ') cleanText.erase(0, 1);
                while (!cleanText.empty() && cleanText.back() == ' ') cleanText.pop_back();
                if (!cleanText.empty()) link["innerText"] = cleanText;
                
                if (!link.empty() && link.find("href") != link.end()) links.push_back(link);
                ++it;
            }
            return links;
        };
        
        std::cout << "\n";
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "StatusCode";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": ";
        if (meta.statusCode >= 200 && meta.statusCode < 300) {
            SetConsoleTextAttribute(hConsole, colorGreen);
        } else if (meta.statusCode >= 400) {
            SetConsoleTextAttribute(hConsole, colorRed);
        } else if (meta.statusCode >= 300) {
            SetConsoleTextAttribute(hConsole, colorYellow);
        }
        std::cout << meta.statusCode << "\n";
        SetConsoleTextAttribute(hConsole, colorWhite);
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "StatusDescription";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": " << meta.statusText << "\n";
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "Content";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": ";
        std::string contentPreview = responseBody.substr(0, 200);
        std::string contentClean;
        for (char c : contentPreview) {
            if (c == '\n') contentClean += " ";
            else if (c == '\r') continue;
            else if (c == '\t') contentClean += " ";
            else contentClean += c;
        }
        while (contentClean.length() > 0 && contentClean.find("  ") != std::string::npos) {
            size_t pos = contentClean.find("  ");
            contentClean.replace(pos, 2, " ");
        }
        if (contentClean.length() > 100) {
            std::cout << contentClean.substr(0, 100) << "...\n";
            std::cout << std::string(20, ' ') << contentClean.substr(100, 100);
            if (responseBody.length() > 200) std::cout << "...";
            std::cout << "\n";
        } else {
            std::cout << contentClean << "\n";
        }
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "RawContent";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": " << meta.protocol << " " << meta.statusCode << " " << meta.statusText << "\n";
        
        int headerCount = 0;
        for (const auto& h : meta.headers) {
            if (headerCount < 4) {
                std::cout << std::string(20, ' ') << h.first << ": ";
                std::string val = h.second;
                if (val.length() > 60) val = val.substr(0, 60) + "...";
                std::cout << val << "\n";
            }
            headerCount++;
        }
        if (headerCount > 4) {
            std::cout << std::string(20, ' ') << "... (" << (headerCount - 4) << " more headers)\n";
        }
        
        bool isHtml = meta.contentType.find("html") != std::string::npos;
        
        if (isHtml) {
            auto forms = extractForms(responseBody);
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "Forms";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": ";
            if (forms.empty()) {
                std::cout << "{}\n";
            } else {
                std::cout << "{";
                for (size_t i = 0; i < std::min(forms.size(), (size_t)3); i++) {
                    if (forms[i].find("id") != forms[i].end()) std::cout << forms[i]["id"];
                    else if (forms[i].find("name") != forms[i].end()) std::cout << forms[i]["name"];
                    else if (forms[i].find("action") != forms[i].end()) std::cout << forms[i]["action"].substr(0, 30);
                    if (i < std::min(forms.size(), (size_t)3) - 1) std::cout << ", ";
                }
                if (forms.size() > 3) std::cout << ", ...";
                std::cout << "}\n";
            }
        }
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "Headers";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": {";
        headerCount = 0;
        for (const auto& h : meta.headers) {
            if (headerCount > 0) std::cout << ", ";
            if (headerCount >= 3) {
                std::cout << "...";
                break;
            }
            std::cout << "[" << h.first << ", ";
            std::string val = h.second;
            if (val.length() > 25) val = val.substr(0, 25) + "...";
            std::cout << val << "]";
            headerCount++;
        }
        std::cout << "}\n";
        
        if (isHtml) {
            auto images = extractImages(responseBody);
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "Images";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": ";
            if (images.empty()) {
                std::cout << "{}\n";
            } else {
                std::cout << "{@{";
                if (!images.empty() && images[0].find("src") != images[0].end()) {
                    std::string src = images[0]["src"];
                    if (src.length() > 50) src = src.substr(0, 50) + "...";
                    std::cout << "src=" << src;
                    if (images[0].find("alt") != images[0].end()) {
                        std::cout << "; alt=" << images[0]["alt"].substr(0, 20);
                    }
                }
                std::cout << "}";
                if (images.size() > 1) std::cout << ", ...(" << (images.size() - 1) << " more)";
                std::cout << "}\n";
            }
            
            auto inputs = extractInputFields(responseBody);
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "InputFields";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": ";
            if (inputs.empty()) {
                std::cout << "{}\n";
            } else {
                std::cout << "{@{";
                for (size_t i = 0; i < std::min(inputs.size(), (size_t)2); i++) {
                    if (i > 0) std::cout << "}, @{";
                    bool first = true;
                    for (const auto& attr : inputs[i]) {
                        if (!first) std::cout << "; ";
                        std::string val = attr.second;
                        if (val.length() > 20) val = val.substr(0, 20) + "...";
                        std::cout << attr.first << "=" << val;
                        first = false;
                    }
                }
                std::cout << "}";
                if (inputs.size() > 2) std::cout << ", ...(" << (inputs.size() - 2) << " more)";
                std::cout << "}\n";
            }
            
            auto links = extractLinksDetailed(responseBody);
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "Links";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": ";
            if (links.empty()) {
                std::cout << "{}\n";
            } else {
                std::cout << "{@{";
                for (size_t i = 0; i < std::min(links.size(), (size_t)2); i++) {
                    if (i > 0) std::cout << "\n" << std::string(20, ' ') << "@{";
                    if (links[i].find("innerText") != links[i].end()) {
                        std::string text = links[i]["innerText"];
                        if (text.length() > 30) text = text.substr(0, 30) + "...";
                        std::cout << "innerText=" << text << "; ";
                    }
                    if (links[i].find("href") != links[i].end()) {
                        std::string href = links[i]["href"];
                        if (href.length() > 50) href = href.substr(0, 50) + "...";
                        std::cout << "href=" << href;
                    }
                    std::cout << "}";
                }
                if (links.size() > 2) std::cout << ", ...(" << (links.size() - 2) << " more)";
                std::cout << "\n";
            }
        }
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "ParsedHtml";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": " << (isHtml ? "System.Object" : "null") << "\n";
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "RawContentLength";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": " << responseBody.length() << "\n";
        
        if (verbose) {
            std::cout << "\n";
            SetConsoleTextAttribute(hConsole, colorYellow);
            std::cout << "─────────────────────────────────────────────────────────────\n";
            std::cout << "DETAILED ANALYSIS\n";
            std::cout << "─────────────────────────────────────────────────────────────\n";
            SetConsoleTextAttribute(hConsole, colorWhite);
            
            SetConsoleTextAttribute(hConsole, colorMagenta);
            std::cout << "\n[Request Info]\n";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << "  Method:           " << method << "\n";
            std::cout << "  URL:              " << url << "\n";
            std::cout << "  Response Time:    " << std::fixed << std::setprecision(2) << (elapsed * 1000) << " ms\n";
            
            SetConsoleTextAttribute(hConsole, colorMagenta);
            std::cout << "\n[Response Headers (" << meta.headers.size() << ")]\n";
            SetConsoleTextAttribute(hConsole, colorWhite);
            for (const auto& h : meta.headers) {
                SetConsoleTextAttribute(hConsole, colorCyan);
                std::cout << "  " << std::left << std::setw(28) << h.first;
                SetConsoleTextAttribute(hConsole, colorWhite);
                std::cout << ": " << h.second << "\n";
            }
            
            if (isHtml) {
                std::string title = HtmlParser::extractTitle(responseBody);
                auto metaTags = HtmlParser::extractMetaTags(responseBody);
                auto forms = extractForms(responseBody);
                auto inputs = extractInputFields(responseBody);
                auto images = extractImages(responseBody);
                auto links = extractLinksDetailed(responseBody);
                
                SetConsoleTextAttribute(hConsole, colorMagenta);
                std::cout << "\n[HTML Analysis]\n";
                SetConsoleTextAttribute(hConsole, colorWhite);
                std::cout << "  Title:            " << (title.empty() ? "(none)" : title) << "\n";
                std::cout << "  Forms:            " << forms.size() << "\n";
                std::cout << "  Input Fields:     " << inputs.size() << "\n";
                std::cout << "  Images:           " << images.size() << "\n";
                std::cout << "  Links:            " << links.size() << "\n";
                std::cout << "  Meta Tags:        " << metaTags.size() << "\n";
                
                if (!metaTags.empty()) {
                    SetConsoleTextAttribute(hConsole, colorMagenta);
                    std::cout << "\n[Meta Tags]\n";
                    SetConsoleTextAttribute(hConsole, colorWhite);
                    for (const auto& m : metaTags) {
                        std::cout << "  " << std::left << std::setw(20) << m.first << ": ";
                        std::string val = m.second;
                        if (val.length() > 60) val = val.substr(0, 60) + "...";
                        std::cout << val << "\n";
                    }
                }
                
                if (!forms.empty()) {
                    SetConsoleTextAttribute(hConsole, colorMagenta);
                    std::cout << "\n[Forms (" << forms.size() << ")]\n";
                    SetConsoleTextAttribute(hConsole, colorWhite);
                    for (size_t i = 0; i < std::min(forms.size(), (size_t)5); i++) {
                        std::cout << "  [" << i << "] ";
                        for (const auto& attr : forms[i]) {
                            std::cout << attr.first << "=" << attr.second.substr(0, 40) << " ";
                        }
                        std::cout << "\n";
                    }
                    if (forms.size() > 5) std::cout << "  ... and " << (forms.size() - 5) << " more\n";
                }
                
                if (!images.empty()) {
                    SetConsoleTextAttribute(hConsole, colorMagenta);
                    std::cout << "\n[Images (" << images.size() << ")]\n";
                    SetConsoleTextAttribute(hConsole, colorWhite);
                    for (size_t i = 0; i < std::min(images.size(), (size_t)5); i++) {
                        std::cout << "  [" << i << "] ";
                        if (images[i].find("src") != images[i].end()) {
                            std::string src = images[i]["src"];
                            if (src.length() > 70) src = src.substr(0, 70) + "...";
                            std::cout << src;
                        }
                        if (images[i].find("alt") != images[i].end()) {
                            std::cout << " (alt: " << images[i]["alt"].substr(0, 20) << ")";
                        }
                        std::cout << "\n";
                    }
                    if (images.size() > 5) std::cout << "  ... and " << (images.size() - 5) << " more\n";
                }
                
                if (!links.empty()) {
                    SetConsoleTextAttribute(hConsole, colorMagenta);
                    std::cout << "\n[Links (" << links.size() << ")]\n";
                    SetConsoleTextAttribute(hConsole, colorWhite);
                    for (size_t i = 0; i < std::min(links.size(), (size_t)10); i++) {
                        std::cout << "  [" << i << "] ";
                        if (links[i].find("innerText") != links[i].end()) {
                            std::string text = links[i]["innerText"];
                            if (text.length() > 25) text = text.substr(0, 25) + "...";
                            std::cout << "\"" << text << "\" -> ";
                        }
                        if (links[i].find("href") != links[i].end()) {
                            std::string href = links[i]["href"];
                            if (href.length() > 60) href = href.substr(0, 60) + "...";
                            std::cout << href;
                        }
                        std::cout << "\n";
                    }
                    if (links.size() > 10) std::cout << "  ... and " << (links.size() - 10) << " more\n";
                }
            }
        }
        
        std::cout << "\n";
        SetConsoleTextAttribute(hConsole, colorWhite);
        
        return 0;
    }

public:
    CurlClient() : hInternet(NULL), hConnect(NULL), hRequest(NULL),
                   userAgent("curl/8.0 (linuxify)"), verbose(false), silent(false),
                   showHeaders(false), includeHeaders(false), followRedirects(true),
                   insecure(false), showProgress(false), resumeDownload(false),
                   failOnError(false), compressedResponse(false), headOnly(false),
                   uploadMode(false), appendOutput(false), createDirs(false), remoteTime(false),
                   enableFormattedOutput(true),
                   sslVerifyPeer(true), sslVerifyHost(true), sslVerifyStatus(false),
                   certStatus(false), sslNoRevoke(false), sslAutoClientCert(false),
                   sslRevokeDefaults(true), proxyInsecure(false), proxySslVerifyPeer(true),
                   proxySslVerifyHost(true),
                   tlsv1(false), tlsv1_0(false), tlsv1_1(false), tlsv1_2(false), tlsv1_3(false),
                   sslv2(false), sslv3(false), sslAllowBeast(false), sslNativeCA(false),
                   httpNegotiate(false), http1_0(false), http1_1(true), http2(false),
                   http3(false), http3Only(false), trEncoding(true), rawOutput(false),
                   noBuffer(false), noKeepalive(false), noSessionId(false), noNpn(false), noAlpn(false),
                   tcp_nodelay(true), tcp_fastopen(false), ipv4_only(false), ipv6_only(false),
                   pathAsIs(false), disableEpsv(false), disableEprt(false), ftpSkipPasvIp(false),
                   ftpPasv(true), ftpCreateDirs(false), ftpPret(false), ftpSslReqd(false),
                   ftpSslCcc(false), ftpSslControl(false), sshCompression(false),
                   tftp_blksize(false), tftp_noOptions(false),
                   authBasic(true), authDigest(false), authNtlm(false), authNtlmWb(false),
                   authNegotiate_(false), authAnyauth(false), authAnysafe(false),
                   authKrb(false), authSpnego(false), authGssapiDelg(false), authGssapiNd(false),
                   globoff(false), useAscii(false), listOnly(false), quote_(false),
                   clobber(true), xferBraces(true), parallelImmediate(false), parallelMax(false),
                   locationTrusted(false), postRedir(false), junkSessionCookies(false),
                   metalink(false), ignoreContentLength(false), styled(false), styledOutput(false),
                   suppressConnectHeaders(false),
                   maxRedirects(50), timeout(0), connectTimeout(300), retryCount(0), 
                   retryDelay(1), retryMaxTime(0), maxFileSize(0), speedLimit(0), speedTime(30),
                   limitRate(0), maxTime(0), expectResponseWithin(0),
                   keepAliveTime(60), keepAliveInterval(60), lowSpeedLimit(0), lowSpeedTime(30),
                   maxConnects(6), parallelConnections(50), bufferSize(16384), uploadBufferSize(65536),
                   maxRecvSpeed(0), maxSendSpeed(0),
                   tlsMaxVersion(0), sslSessionReuse(1), certsInfo(0),
                   proxyBasic(0), proxyDigest(0), proxyNtlm(0), proxyNegotiate(0),
                   proxyAnyauth(0), socks5Auth(0),
                   method("GET") {}

    ~CurlClient() { cleanup(); }

    void setVerbose(bool v) { verbose = v; }
    void setSilent(bool s) { silent = s; if (s) showProgress = false; }
    void setShowHeaders(bool h) { showHeaders = h; }
    void setIncludeHeaders(bool h) { includeHeaders = h; }
    void setFollowRedirects(bool f) { followRedirects = f; }
    void setMaxRedirects(int m) { maxRedirects = m; }
    void setInsecure(bool i) { insecure = i; sslVerifyPeer = !i; sslVerifyHost = !i; }
    void setMethod(const std::string& m) { method = m; }
    void setPostData(const std::string& d) { postData = d; if (method == "GET") method = "POST"; }
    void setOutputFile(const std::string& f) { outputFile = f; }
    void setUploadFile(const std::string& f) { uploadFile = f; uploadMode = true; }
    void setUserAgent(const std::string& ua) { userAgent = ua; }
    void setTimeout(int t) { timeout = t * 1000; }
    void setConnectTimeout(int t) { connectTimeout = t * 1000; }
    void setUser(const std::string& u) { 
        size_t pos = u.find(':');
        if (pos != std::string::npos) {
            username = u.substr(0, pos);
            password = u.substr(pos + 1);
        } else {
            username = u;
        }
    }
    void setReferer(const std::string& r) { referer = r; }
    void setRange(const std::string& r) { range = r; }
    void setContentType(const std::string& ct) { contentType = ct; }
    void setProxy(const std::string& p) { proxyServer = p; }
    void setProxyUser(const std::string& pu) { proxyUserPass = pu; }
    void setShowProgress(bool p) { showProgress = p; }
    void setResumeDownload(bool r) { resumeDownload = r; }
    void setFailOnError(bool f) { failOnError = f; }
    void setCompressed(bool c) { compressedResponse = c; }
    void setHeadOnly(bool h) { headOnly = h; if (h) method = "HEAD"; }
    void setAppendOutput(bool a) { appendOutput = a; }
    void setCreateDirs(bool c) { createDirs = c; }
    void setMaxFileSize(long s) { maxFileSize = s; }
    void setRetry(int c, int d) { retryCount = c; retryDelay = d; }
    void addHeader(const std::string& h) { headers.push_back(h); }
    void addFormField(const std::string& name, const std::string& value) { 
        formData.push_back({value, name}); 
        if (method == "GET") method = "POST";
    }
    
    void setFormattedOutput(bool f) { enableFormattedOutput = f; }
    void setRawOutput(bool r) { rawOutput = r; enableFormattedOutput = !r; }
    
    void setSslCert(const std::string& c) { sslCert = c; }
    void setSslCertType(const std::string& t) { sslCertType = t; }
    void setSslKey(const std::string& k) { sslKey = k; }
    void setSslKeyType(const std::string& t) { sslKeyType = t; }
    void setSslKeyPasswd(const std::string& p) { sslKeyPasswd = p; }
    void setCaCert(const std::string& c) { caCert = c; }
    void setCaPath(const std::string& p) { caPath = p; }
    void setCrlFile(const std::string& c) { crlFile = c; }
    void setPinnedPublicKey(const std::string& k) { pinnedPublicKey = k; }
    void setCiphers(const std::string& c) { ciphers = c; }
    void setTls13Ciphers(const std::string& c) { tls13Ciphers = c; }
    void setSslEngines(const std::string& e) { sslEngines = e; }
    void setRandomFile(const std::string& f) { randomFile = f; }
    void setEgdSocket(const std::string& s) { egdSocket = s; }
    
    void setProxyType(const std::string& t) { proxyType = t; }
    void setProxyAuth(const std::string& a) { proxyAuth = a; }
    void setProxyCert(const std::string& c) { proxyCert = c; }
    void setProxyKey(const std::string& k) { proxyKey = k; }
    void setProxyCaPath(const std::string& p) { proxyCaPath = p; }
    void setProxyTlsUser(const std::string& u) { proxyTlsUser = u; }
    void setProxyTlsPass(const std::string& p) { proxyTlsPass = p; }
    void setSocksProxy(const std::string& p) { socksProxy = p; }
    void setNoProxy(const std::string& n) { noProxy = n; }
    void setPreProxy(const std::string& p) { preProxy = p; }
    
    void setFtpAccount(const std::string& a) { ftpAccount = a; }
    void setFtpAlternativeUser(const std::string& u) { ftpAlternativeUser = u; }
    void setFtpPort(const std::string& p) { ftpPort = p; }
    void setFtpMethod(const std::string& m) { ftpMethod = m; }
    void setMailFrom(const std::string& f) { mailFrom = f; }
    void setMailRcpt(const std::string& r) { mailRcpt = r; }
    void setMailAuth(const std::string& a) { mailAuth = a; }
    
    void setDnsInterface(const std::string& i) { dnsInterface = i; }
    void setDnsIpv4Addr(const std::string& a) { dnsIpv4Addr = a; }
    void setDnsIpv6Addr(const std::string& a) { dnsIpv6Addr = a; }
    void setDnsServers(const std::string& s) { dnsServers = s; }
    void setResolveHosts(const std::string& r) { resolveHosts = r; }
    void setInterface(const std::string& i) { interface_ = i; }
    void setLocalPort(const std::string& p) { localPort = p; }
    void setLocalPortRange(const std::string& r) { localPortRange = r; }
    
    void setOauth2Bearer(const std::string& t) { oauth2Bearer = t; }
    void setAwsSigv4(const std::string& a) { awsSigv4 = a; }
    void setDelegation(const std::string& d) { delegation = d; }
    void setNtlmDomain(const std::string& d) { ntlmDomain = d; }
    void setLoginOptions(const std::string& o) { loginOptions = o; }
    void setSaslAuthz(const std::string& a) { saslAuthz = a; }
    void setSaslIr(const std::string& i) { saslIr = i; }
    
    void setWriteOut(const std::string& f) { writeOutFormat = f; }
    void setStderr(const std::string& f) { stderrFile = f; }
    void setTraceFile(const std::string& f) { traceFile = f; }
    void setTraceAscii(const std::string& f) { traceAsciiFile = f; }
    void setNetrc(const std::string& m) { netrc = m; }
    void setNetrcFile(const std::string& f) { netrcFile = f; }
    
    void setUnixSocket(const std::string& s) { unixSocket = s; }
    void setAbstractUnixSocket(const std::string& s) { abstractUnixSocket = s; }
    void setAltSvc(const std::string& f) { altSvc = f; }
    void setHsts(const std::string& f) { hstsFile = f; }
    void setDohUrl(const std::string& u) { dohUrl = u; }
    void setHappyEyeballsTimeout(const std::string& t) { happyEyeballsTimeout = t; }
    
    void setTlsAuthType(const std::string& t) { tlsAuthType = t; }
    void setTlsUser(const std::string& u) { tlsUser = u; }
    void setTlsPassword(const std::string& p) { tlsPassword = p; }
    void setRequestTarget(const std::string& t) { requestTarget = t; }
    void setExpect100Timeout(const std::string& t) { expect100Timeout = t; }
    
    void addConnectTo(const std::string& c) { connectTo.push_back(c); }
    void addQuote(const std::string& q) { quotes.push_back(q); }
    void addPostQuote(const std::string& q) { postQuotes.push_back(q); }
    void addPreQuote(const std::string& q) { preQuotes.push_back(q); }
    void addTelnetOption(const std::string& o) { telnetOptions.push_back(o); }
    
    void setSslVerifyPeer(bool v) { sslVerifyPeer = v; }
    void setSslVerifyHost(bool v) { sslVerifyHost = v; }
    void setSslVerifyStatus(bool v) { sslVerifyStatus = v; }
    void setCertStatus(bool v) { certStatus = v; }
    void setSslNoRevoke(bool v) { sslNoRevoke = v; }
    void setSslAutoClientCert(bool v) { sslAutoClientCert = v; }
    void setProxyInsecure(bool v) { proxyInsecure = v; proxySslVerifyPeer = !v; }
    
    void setTlsv1(bool v) { tlsv1 = v; }
    void setTlsv1_0(bool v) { tlsv1_0 = v; }
    void setTlsv1_1(bool v) { tlsv1_1 = v; }
    void setTlsv1_2(bool v) { tlsv1_2 = v; }
    void setTlsv1_3(bool v) { tlsv1_3 = v; }
    void setSslv2(bool v) { sslv2 = v; }
    void setSslv3(bool v) { sslv3 = v; }
    void setSslAllowBeast(bool v) { sslAllowBeast = v; }
    void setSslNativeCA(bool v) { sslNativeCA = v; }
    
    void setHttp1_0(bool v) { http1_0 = v; http1_1 = !v; }
    void setHttp1_1(bool v) { http1_1 = v; }
    void setHttp2(bool v) { http2 = v; }
    void setHttp2PriorKnowledge(bool v) { http2PriorKnowledge = v ? "1" : ""; }
    void setHttp3(bool v) { http3 = v; }
    void setHttp3Only(bool v) { http3Only = v; }
    
    void setNoBuffer(bool v) { noBuffer = v; }
    void setNoKeepalive(bool v) { noKeepalive = v; }
    void setNoSessionId(bool v) { noSessionId = v; }
    void setNoNpn(bool v) { noNpn = v; }
    void setNoAlpn(bool v) { noAlpn = v; }
    void setTrEncoding(bool v) { trEncoding = v; }
    
    void setTcpNodelay(bool v) { tcp_nodelay = v; }
    void setTcpFastopen(bool v) { tcp_fastopen = v; }
    void setIpv4Only(bool v) { ipv4_only = v; ipv6_only = !v; }
    void setIpv6Only(bool v) { ipv6_only = v; ipv4_only = !v; }
    void setPathAsIs(bool v) { pathAsIs = v; }
    
    void setDisableEpsv(bool v) { disableEpsv = v; }
    void setDisableEprt(bool v) { disableEprt = v; }
    void setFtpSkipPasvIp(bool v) { ftpSkipPasvIp = v; }
    void setFtpPasv(bool v) { ftpPasv = v; }
    void setFtpCreateDirs(bool v) { ftpCreateDirs = v; }
    void setFtpPret(bool v) { ftpPret = v; }
    void setFtpSslReqd(bool v) { ftpSslReqd = v; }
    void setFtpSslCcc(bool v) { ftpSslCcc = v; }
    void setFtpSslControl(bool v) { ftpSslControl = v; }
    void setSshCompression(bool v) { sshCompression = v; }
    
    void setAuthBasic(bool v) { authBasic = v; }
    void setAuthDigest(bool v) { authDigest = v; }
    void setAuthNtlm(bool v) { authNtlm = v; }
    void setAuthNtlmWb(bool v) { authNtlmWb = v; }
    void setAuthNegotiate(bool v) { authNegotiate_ = v; }
    void setAuthAnyauth(bool v) { authAnyauth = v; }
    void setAuthAnysafe(bool v) { authAnysafe = v; }
    void setAuthKrb(bool v) { authKrb = v; }
    void setAuthSpnego(bool v) { authSpnego = v; }
    
    void setGloboff(bool v) { globoff = v; }
    void setUseAscii(bool v) { useAscii = v; }
    void setListOnly(bool v) { listOnly = v; }
    void setNoClobber(bool v) { clobber = !v; }
    void setLocationTrusted(bool v) { locationTrusted = v; }
    void setPostRedir(bool v) { postRedir = v; }
    void setJunkSessionCookies(bool v) { junkSessionCookies = v; }
    void setIgnoreContentLength(bool v) { ignoreContentLength = v; }
    void setSuppressConnectHeaders(bool v) { suppressConnectHeaders = v; }
    
    void setRetryMaxTime(int t) { retryMaxTime = t; }
    void setSpeedLimit(long s) { speedLimit = s; }
    void setSpeedTime(long t) { speedTime = t; }
    void setLimitRate(long r) { limitRate = r; }
    void setMaxTime(long t) { maxTime = t; }
    void setExpectResponseWithin(long t) { expectResponseWithin = t; }
    void setKeepaliveTime(int t) { keepAliveTime = t; }
    void setKeepaliveInterval(int i) { keepAliveInterval = i; }
    void setLowSpeedLimit(int l) { lowSpeedLimit = l; }
    void setLowSpeedTime(int t) { lowSpeedTime = t; }
    void setMaxConnects(int m) { maxConnects = m; }
    void setParallelConnections(int p) { parallelConnections = p; }
    void setBufferSize(int s) { bufferSize = s; }
    void setUploadBufferSize(int s) { uploadBufferSize = s; }
    void setMaxRecvSpeed(long s) { maxRecvSpeed = s; }
    void setMaxSendSpeed(long s) { maxSendSpeed = s; }
    
    void setCookieJar(const std::string& f) { cookieJar = f; }
    void setCookieFile(const std::string& f) { cookieFile = f; }
    void setRemoteTime(bool r) { remoteTime = r; }


    struct DnsEntry {
        std::string hostname;
        std::string ipAddress;
        time_t resolvedAt;
        int ttl;
    };
    
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path;
        time_t expires;
        bool secure;
        bool httpOnly;
        bool sessionOnly;
    };
    
    struct HttpTiming {
        double dnsLookup;
        double tcpConnect;
        double sslHandshake;
        double timeToFirstByte;
        double contentTransfer;
        double total;
    };
    
    struct ResponseMetadata {
        int statusCode;
        std::string statusText;
        std::string protocol;
        std::map<std::string, std::string> headers;
        std::string contentType;
        std::string contentEncoding;
        size_t contentLength;
        std::string etag;
        std::string lastModified;
        std::string cacheControl;
        HttpTiming timing;
    };
    
    std::vector<DnsEntry> dnsCache;
    std::vector<Cookie> cookieStore;
    std::map<std::string, std::string> responseCache;
    std::map<std::string, std::string> etagCache;
    
    std::string resolveDns(const std::string& hostname) {
        for (const auto& entry : dnsCache) {
            if (entry.hostname == hostname) {
                time_t now = time(NULL);
                if (now - entry.resolvedAt < entry.ttl) {
                    return entry.ipAddress;
                }
            }
        }
        
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            return "";
        }
        
        struct addrinfo hints = {0}, *result = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(hostname.c_str(), NULL, &hints, &result) == 0) {
            char ipStr[INET_ADDRSTRLEN];
            struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
            freeaddrinfo(result);
            
            DnsEntry entry;
            entry.hostname = hostname;
            entry.ipAddress = ipStr;
            entry.resolvedAt = time(NULL);
            entry.ttl = 300;
            dnsCache.push_back(entry);
            
            return ipStr;
        }
        
        return "";
    }
    
    void parseCookieHeader(const std::string& header, const std::string& domain) {
        Cookie cookie;
        cookie.domain = domain;
        cookie.path = "/";
        cookie.expires = 0;
        cookie.secure = false;
        cookie.httpOnly = false;
        cookie.sessionOnly = true;
        
        std::istringstream iss(header);
        std::string token;
        bool first = true;
        
        while (std::getline(iss, token, ';')) {
            size_t start = token.find_first_not_of(" ");
            if (start != std::string::npos) token = token.substr(start);
            size_t end = token.find_last_not_of(" ");
            if (end != std::string::npos) token = token.substr(0, end + 1);
            
            size_t eqPos = token.find('=');
            if (first && eqPos != std::string::npos) {
                cookie.name = token.substr(0, eqPos);
                cookie.value = token.substr(eqPos + 1);
                first = false;
            } else {
                std::string attrName = token;
                std::string attrValue;
                if (eqPos != std::string::npos) {
                    attrName = token.substr(0, eqPos);
                    attrValue = token.substr(eqPos + 1);
                }
                std::transform(attrName.begin(), attrName.end(), attrName.begin(), ::tolower);
                
                if (attrName == "domain") cookie.domain = attrValue;
                else if (attrName == "path") cookie.path = attrValue;
                else if (attrName == "secure") cookie.secure = true;
                else if (attrName == "httponly") cookie.httpOnly = true;
                else if (attrName == "max-age") {
                    try {
                        int maxAge = std::stoi(attrValue);
                        cookie.expires = time(NULL) + maxAge;
                        cookie.sessionOnly = false;
                    } catch (...) {}
                }
            }
        }
        
        if (!cookie.name.empty()) {
            for (auto& existing : cookieStore) {
                if (existing.name == cookie.name && existing.domain == cookie.domain) {
                    existing = cookie;
                    return;
                }
            }
            cookieStore.push_back(cookie);
        }
    }
    
    std::string getCookiesForRequest(const std::string& domain, const std::string& path, bool isSecure) {
        std::string result;
        time_t now = time(NULL);
        
        for (const auto& cookie : cookieStore) {
            if (!cookie.sessionOnly && cookie.expires > 0 && cookie.expires < now) continue;
            
            bool domainMatch = (cookie.domain == domain) || 
                              (cookie.domain[0] == '.' && domain.find(cookie.domain.substr(1)) != std::string::npos);
            if (!domainMatch) continue;
            
            if (path.find(cookie.path) != 0) continue;
            if (cookie.secure && !isSecure) continue;
            
            if (!result.empty()) result += "; ";
            result += cookie.name + "=" + cookie.value;
        }
        
        return result;
    }
    
    void saveCookies(const std::string& filename) {
        std::ofstream file(filename);
        if (!file) return;
        
        file << "# Netscape HTTP Cookie File\n";
        file << "# https://curl.se/docs/http-cookies.html\n";
        file << "# This file was generated by curl! Edit at your own risk.\n\n";
        
        for (const auto& cookie : cookieStore) {
            if (cookie.sessionOnly) continue;
            
            file << cookie.domain << "\t";
            file << (cookie.domain[0] == '.' ? "TRUE" : "FALSE") << "\t";
            file << cookie.path << "\t";
            file << (cookie.secure ? "TRUE" : "FALSE") << "\t";
            file << cookie.expires << "\t";
            file << cookie.name << "\t";
            file << cookie.value << "\n";
        }
    }
    
    void loadCookies(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            std::istringstream iss(line);
            Cookie cookie;
            std::string domainAll, secure;
            long long expiry;
            
            iss >> cookie.domain >> domainAll >> cookie.path >> secure >> expiry >> cookie.name >> cookie.value;
            
            cookie.secure = (secure == "TRUE");
            cookie.expires = (time_t)expiry;
            cookie.sessionOnly = (expiry == 0);
            cookie.httpOnly = false;
            
            if (!cookie.name.empty()) {
                cookieStore.push_back(cookie);
            }
        }
    }
    
    std::string detectMimeType(const std::string& filename) {
        size_t dotPos = filename.find_last_of('.');
        if (dotPos == std::string::npos) return "application/octet-stream";
        
        std::string ext = filename.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        static const std::map<std::string, std::string> mimeTypes = {
            {"html", "text/html"},
            {"htm", "text/html"},
            {"css", "text/css"},
            {"js", "application/javascript"},
            {"json", "application/json"},
            {"xml", "application/xml"},
            {"txt", "text/plain"},
            {"jpg", "image/jpeg"},
            {"jpeg", "image/jpeg"},
            {"png", "image/png"},
            {"gif", "image/gif"},
            {"svg", "image/svg+xml"},
            {"webp", "image/webp"},
            {"ico", "image/x-icon"},
            {"pdf", "application/pdf"},
            {"zip", "application/zip"},
            {"tar", "application/x-tar"},
            {"gz", "application/gzip"},
            {"mp3", "audio/mpeg"},
            {"mp4", "video/mp4"},
            {"webm", "video/webm"},
            {"woff", "font/woff"},
            {"woff2", "font/woff2"},
            {"ttf", "font/ttf"},
            {"otf", "font/otf"},
            {"eot", "application/vnd.ms-fontobject"}
        };
        
        auto it = mimeTypes.find(ext);
        return it != mimeTypes.end() ? it->second : "application/octet-stream";
    }
    
    std::string decompressGzip(const std::string& data) {
        return data;
    }
    
    std::string decompressDeflate(const std::string& data) {
        return data;
    }
    
    std::string decompressBrotli(const std::string& data) {
        return data;
    }
    
    ResponseMetadata parseResponseHeaders(const std::string& rawHeaders) {
        ResponseMetadata meta;
        meta.statusCode = 0;
        meta.contentLength = 0;
        
        std::istringstream iss(rawHeaders);
        std::string line;
        bool firstLine = true;
        
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            
            if (firstLine) {
                size_t sp1 = line.find(' ');
                if (sp1 != std::string::npos) {
                    meta.protocol = line.substr(0, sp1);
                    size_t sp2 = line.find(' ', sp1 + 1);
                    if (sp2 != std::string::npos) {
                        try { meta.statusCode = std::stoi(line.substr(sp1 + 1, sp2 - sp1 - 1)); } catch (...) {}
                        meta.statusText = line.substr(sp2 + 1);
                    }
                }
                firstLine = false;
            } else {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string key = line.substr(0, colonPos);
                    std::string value = line.substr(colonPos + 1);
                    while (!value.empty() && value[0] == ' ') value.erase(0, 1);
                    
                    std::string keyLower = key;
                    std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                    
                    meta.headers[key] = value;
                    
                    if (keyLower == "content-type") meta.contentType = value;
                    else if (keyLower == "content-length") {
                        try { meta.contentLength = std::stoull(value); } catch (...) {}
                    }
                    else if (keyLower == "content-encoding") meta.contentEncoding = value;
                    else if (keyLower == "etag") meta.etag = value;
                    else if (keyLower == "last-modified") meta.lastModified = value;
                    else if (keyLower == "cache-control") meta.cacheControl = value;
                }
            }
        }
        
        return meta;
    }
    
    void printFormattedResponse(const ResponseMetadata& meta, const std::string& body, const std::string& url) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD colorGreen = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD colorCyan = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        WORD colorYellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        WORD colorWhite = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        WORD colorRed = FOREGROUND_RED | FOREGROUND_INTENSITY;
        
        std::cout << "\n";
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "URL";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": " << url << "\n";
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "StatusCode";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": ";
        if (meta.statusCode >= 200 && meta.statusCode < 300) {
            SetConsoleTextAttribute(hConsole, colorGreen);
        } else if (meta.statusCode >= 400) {
            SetConsoleTextAttribute(hConsole, colorRed);
        } else {
            SetConsoleTextAttribute(hConsole, colorYellow);
        }
        std::cout << meta.statusCode << "\n";
        SetConsoleTextAttribute(hConsole, colorWhite);
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "StatusDescription";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": " << meta.statusText << "\n";
        
        if (!meta.contentType.empty()) {
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "Content-Type";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": " << meta.contentType << "\n";
        }
        
        SetConsoleTextAttribute(hConsole, colorCyan);
        std::cout << std::left << std::setw(18) << "Content-Length";
        SetConsoleTextAttribute(hConsole, colorWhite);
        std::cout << ": ";
        if (meta.contentLength >= 1024 * 1024) {
            std::cout << std::fixed << std::setprecision(2) << (meta.contentLength / (1024.0 * 1024.0)) << " MB";
        } else if (meta.contentLength >= 1024) {
            std::cout << std::fixed << std::setprecision(2) << (meta.contentLength / 1024.0) << " KB";
        } else {
            std::cout << meta.contentLength << " bytes";
        }
        std::cout << "\n";
        
        if (!meta.etag.empty()) {
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "ETag";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": " << meta.etag << "\n";
        }
        
        if (!meta.lastModified.empty()) {
            SetConsoleTextAttribute(hConsole, colorCyan);
            std::cout << std::left << std::setw(18) << "Last-Modified";
            SetConsoleTextAttribute(hConsole, colorWhite);
            std::cout << ": " << meta.lastModified << "\n";
        }
        
        if (verbose) {
            std::cout << "\n";
            SetConsoleTextAttribute(hConsole, colorYellow);
            std::cout << "Response Headers:\n";
            SetConsoleTextAttribute(hConsole, colorWhite);
            
            for (const auto& h : meta.headers) {
                SetConsoleTextAttribute(hConsole, colorCyan);
                std::cout << "  " << std::left << std::setw(25) << h.first;
                SetConsoleTextAttribute(hConsole, colorWhite);
                std::cout << ": " << h.second << "\n";
            }
        }
        
        bool isTextContent = meta.contentType.find("text/") != std::string::npos ||
                            meta.contentType.find("json") != std::string::npos ||
                            meta.contentType.find("xml") != std::string::npos ||
                            meta.contentType.find("javascript") != std::string::npos;
        
        std::cout << "\n";
        SetConsoleTextAttribute(hConsole, colorYellow);
        std::cout << "Content";
        SetConsoleTextAttribute(hConsole, colorWhite);
        
        if (isTextContent && !body.empty()) {
            std::cout << " (preview, first 2500 chars):\n";
            std::cout << std::string(60, '-') << "\n";
            
            std::string preview = body.substr(0, 2500);
            std::istringstream bodyStream(preview);
            std::string line;
            int lineCount = 0;
            
            while (std::getline(bodyStream, line) && lineCount < 60) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                
                if (line.length() > 100) {
                    line = line.substr(0, 100) + "...";
                }
                
                std::cout << line << "\n";
                lineCount++;
            }
            
            if (body.length() > 2500) {
                SetConsoleTextAttribute(hConsole, colorYellow);
                size_t remaining = body.length() - 2500;
                if (remaining >= 1024 * 1024) {
                    std::cout << "\n... [" << std::fixed << std::setprecision(2) << (remaining / (1024.0 * 1024.0)) << " MB more]\n";
                } else if (remaining >= 1024) {
                    std::cout << "\n... [" << std::fixed << std::setprecision(2) << (remaining / 1024.0) << " KB more]\n";
                } else {
                    std::cout << "\n... [" << remaining << " bytes more]\n";
                }
                SetConsoleTextAttribute(hConsole, colorWhite);
            }
        } else if (!body.empty()) {
            std::cout << " (binary data, ";
            if (body.length() >= 1024 * 1024) {
                std::cout << std::fixed << std::setprecision(2) << (body.length() / (1024.0 * 1024.0)) << " MB";
            } else if (body.length() >= 1024) {
                std::cout << std::fixed << std::setprecision(2) << (body.length() / 1024.0) << " KB";
            } else {
                std::cout << body.length() << " bytes";
            }
            std::cout << ")\n";
        } else {
            std::cout << " (empty response body)\n";
        }
        
        std::cout << "\n";
        SetConsoleTextAttribute(hConsole, colorWhite);
    }
    
    SOCKET createSocket(const std::string& host, int port, int type = SOCK_STREAM) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            if (!silent) std::cerr << "curl: WSAStartup failed\n";
            return INVALID_SOCKET;
        }
        
        struct addrinfo hints = {0}, *result = nullptr;
        hints.ai_family = ipv4_only ? AF_INET : (ipv6_only ? AF_INET6 : AF_UNSPEC);
        hints.ai_socktype = type;
        hints.ai_protocol = (type == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
        
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            if (!silent) std::cerr << "curl: could not resolve host: " << host << "\n";
            WSACleanup();
            return INVALID_SOCKET;
        }
        
        SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock == INVALID_SOCKET) {
            if (!silent) std::cerr << "curl: socket creation failed\n";
            freeaddrinfo(result);
            WSACleanup();
            return INVALID_SOCKET;
        }
        
        if (tcp_nodelay && type == SOCK_STREAM) {
            int flag = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
        }
        
        if (type == SOCK_STREAM) {
            if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                if (!silent) std::cerr << "curl: connection to " << host << ":" << port << " failed\n";
                closesocket(sock);
                freeaddrinfo(result);
                WSACleanup();
                return INVALID_SOCKET;
            }
        }
        
        freeaddrinfo(result);
        return sock;
    }
    
    int executeTCP(const std::string& host, int port) {
        if (verbose) std::cerr << "* Connecting to " << host << ":" << port << " (TCP)...\n";
        
        SOCKET sock = createSocket(host, port, SOCK_STREAM);
        if (sock == INVALID_SOCKET) return 1;
        
        if (verbose) std::cerr << "* Connected to " << host << ":" << port << "\n";
        
        std::string dataToSend = postData;
        if (dataToSend.empty() && !uploadFile.empty()) {
            std::ifstream file(uploadFile, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                dataToSend = ss.str();
            }
        }
        
        if (!dataToSend.empty()) {
            if (verbose) std::cerr << "> Sending " << dataToSend.length() << " bytes...\n";
            send(sock, dataToSend.c_str(), (int)dataToSend.length(), 0);
        }
        
        char buffer[8192];
        std::ostringstream response;
        int bytesReceived;
        
        while ((bytesReceived = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
            response.write(buffer, bytesReceived);
            if (showProgress) std::cerr << "Received " << response.str().length() << " bytes\r";
        }
        
        closesocket(sock);
        WSACleanup();
        
        std::string result = response.str();
        if (!outputFile.empty()) {
            std::ofstream out(outputFile, std::ios::binary);
            out.write(result.c_str(), result.length());
            if (!silent) std::cerr << "Saved " << result.length() << " bytes to " << outputFile << "\n";
        } else {
            std::cout << result;
        }
        
        return 0;
    }
    
    int executeUDP(const std::string& host, int port) {
        if (verbose) std::cerr << "* Sending to " << host << ":" << port << " (UDP)...\n";
        
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
        
        struct addrinfo hints = {0}, *result = nullptr;
        hints.ai_family = ipv4_only ? AF_INET : (ipv6_only ? AF_INET6 : AF_UNSPEC);
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            if (!silent) std::cerr << "curl: could not resolve host\n";
            WSACleanup();
            return 1;
        }
        
        SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock == INVALID_SOCKET) {
            freeaddrinfo(result);
            WSACleanup();
            return 1;
        }
        
        std::string dataToSend = postData;
        if (dataToSend.empty() && !uploadFile.empty()) {
            std::ifstream file(uploadFile, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                dataToSend = ss.str();
            }
        }
        
        if (!dataToSend.empty()) {
            sendto(sock, dataToSend.c_str(), (int)dataToSend.length(), 0, result->ai_addr, (int)result->ai_addrlen);
            if (verbose) std::cerr << "> Sent " << dataToSend.length() << " bytes via UDP\n";
        }
        
        struct timeval tv;
        tv.tv_sec = timeout > 0 ? timeout / 1000 : 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        
        char buffer[65536];
        struct sockaddr_storage from;
        int fromLen = sizeof(from);
        int bytesReceived = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &fromLen);
        
        freeaddrinfo(result);
        closesocket(sock);
        WSACleanup();
        
        if (bytesReceived > 0) {
            if (!outputFile.empty()) {
                std::ofstream out(outputFile, std::ios::binary);
                out.write(buffer, bytesReceived);
            } else {
                std::cout.write(buffer, bytesReceived);
            }
        }
        
        return 0;
    }
    
    int executeSMTP(const std::string& host, int port, const std::string& path, 
                    const std::string& urlUser, const std::string& urlPass, bool secure) {
        if (verbose) std::cerr << "* Connecting to SMTP server " << host << ":" << port << "\n";
        
        SOCKET sock = createSocket(host, port, SOCK_STREAM);
        if (sock == INVALID_SOCKET) return 1;
        
        auto readLine = [&]() -> std::string {
            char buf[4096];
            std::string line;
            char c;
            while (recv(sock, &c, 1, 0) == 1 && c != '\n') {
                if (c != '\r') line += c;
            }
            if (verbose) std::cerr << "< " << line << "\n";
            return line;
        };
        
        auto sendCmd = [&](const std::string& cmd) -> std::string {
            if (verbose) std::cerr << "> " << cmd;
            std::string data = cmd;
            if (data.find("\r\n") == std::string::npos) data += "\r\n";
            send(sock, data.c_str(), (int)data.length(), 0);
            return readLine();
        };
        
        readLine();
        
        sendCmd("EHLO localhost");
        
        std::string user = urlUser.empty() ? username : urlUser;
        std::string pass = urlPass.empty() ? password : urlPass;
        
        if (!user.empty()) {
            sendCmd("AUTH LOGIN");
            
            auto b64encode = [](const std::string& input) -> std::string {
                static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                std::string output;
                int val = 0, valb = -6;
                for (unsigned char c : input) {
                    val = (val << 8) + c;
                    valb += 8;
                    while (valb >= 0) {
                        output.push_back(chars[(val >> valb) & 0x3F]);
                        valb -= 6;
                    }
                }
                if (valb > -6) output.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
                while (output.length() % 4) output.push_back('=');
                return output;
            };
            
            sendCmd(b64encode(user));
            sendCmd(b64encode(pass));
        }
        
        if (!mailFrom.empty()) {
            sendCmd("MAIL FROM:<" + mailFrom + ">");
        }
        
        if (!mailRcpt.empty()) {
            sendCmd("RCPT TO:<" + mailRcpt + ">");
        }
        
        if (!postData.empty() || !uploadFile.empty()) {
            sendCmd("DATA");
            
            std::string email = postData;
            if (email.empty() && !uploadFile.empty()) {
                std::ifstream file(uploadFile);
                std::ostringstream ss;
                ss << file.rdbuf();
                email = ss.str();
            }
            
            send(sock, email.c_str(), (int)email.length(), 0);
            sendCmd("\r\n.");
        }
        
        sendCmd("QUIT");
        
        closesocket(sock);
        WSACleanup();
        
        if (!silent) std::cout << "SMTP transaction completed.\n";
        return 0;
    }
    
    int executeTelnet(const std::string& host, int port) {
        if (verbose) std::cerr << "* Connecting to telnet://" << host << ":" << port << "\n";
        
        SOCKET sock = createSocket(host, port, SOCK_STREAM);
        if (sock == INVALID_SOCKET) return 1;
        
        if (!silent) std::cout << "Connected to " << host << ":" << port << "\n";
        
        if (!postData.empty()) {
            send(sock, postData.c_str(), (int)postData.length(), 0);
            send(sock, "\r\n", 2, 0);
        }
        
        fd_set readfds;
        struct timeval tv;
        char buffer[4096];
        
        while (true) {
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            
            tv.tv_sec = timeout > 0 ? timeout / 1000 : 30;
            tv.tv_usec = 0;
            
            int ready = select((int)sock + 1, &readfds, NULL, NULL, &tv);
            if (ready <= 0) break;
            
            if (FD_ISSET(sock, &readfds)) {
                int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
                if (bytesReceived <= 0) break;
                std::cout.write(buffer, bytesReceived);
            }
        }
        
        closesocket(sock);
        WSACleanup();
        return 0;
    }
    
    int executeGopher(const std::string& host, int port, const std::string& path) {
        if (verbose) std::cerr << "* Connecting to gopher://" << host << ":" << port << path << "\n";
        
        int gopherPort = (port == 80 || port == 443) ? 70 : port;
        SOCKET sock = createSocket(host, gopherPort, SOCK_STREAM);
        if (sock == INVALID_SOCKET) return 1;
        
        std::string selector = path.empty() ? "" : path.substr(1);
        selector += "\r\n";
        send(sock, selector.c_str(), (int)selector.length(), 0);
        
        std::ostringstream response;
        char buffer[8192];
        int bytesReceived;
        
        while ((bytesReceived = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
            response.write(buffer, bytesReceived);
        }
        
        closesocket(sock);
        WSACleanup();
        
        std::string result = response.str();
        if (!outputFile.empty()) {
            std::ofstream out(outputFile, std::ios::binary);
            out.write(result.c_str(), result.length());
        } else {
            std::cout << result;
        }
        
        return 0;
    }
    
    int executeDict(const std::string& host, int port, const std::string& path) {
        if (verbose) std::cerr << "* Connecting to dict://" << host << ":" << port << path << "\n";
        
        int dictPort = (port == 80 || port == 443) ? 2628 : port;
        SOCKET sock = createSocket(host, dictPort, SOCK_STREAM);
        if (sock == INVALID_SOCKET) return 1;
        
        char buffer[4096];
        int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
        if (bytesReceived > 0 && verbose) {
            std::cerr << "< " << std::string(buffer, bytesReceived);
        }
        
        std::string command;
        if (path.length() > 1) {
            std::string query = path.substr(1);
            if (query.find("d:") == 0) {
                command = "DEFINE * " + query.substr(2) + "\r\n";
            } else if (query.find("m:") == 0) {
                command = "MATCH * . " + query.substr(2) + "\r\n";
            } else {
                command = "DEFINE * " + query + "\r\n";
            }
        } else {
            command = "SHOW DATABASES\r\n";
        }
        
        send(sock, command.c_str(), (int)command.length(), 0);
        
        std::ostringstream response;
        while ((bytesReceived = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
            response.write(buffer, bytesReceived);
            if (response.str().find("250 ok") != std::string::npos) break;
        }
        
        send(sock, "QUIT\r\n", 6, 0);
        
        closesocket(sock);
        WSACleanup();
        
        std::cout << response.str();
        return 0;
    }
    
    int execute(const std::string& url) {
        std::string protocol, host, path, urlUser, urlPass;
        INTERNET_PORT port;
        
        if (!parseUrl(url, protocol, host, path, port, urlUser, urlPass)) {
            if (!silent) std::cerr << "curl: invalid URL: " << url << std::endl;
            return 1;
        }

        if (verbose) {
            std::cerr << "* Trying " << host << ":" << port << "...\n";
            std::cerr << "* Protocol: " << protocol << "\n";
        }

        DWORD accessType = INTERNET_OPEN_TYPE_PRECONFIG;
        LPCSTR proxyName = NULL;
        
        if (!proxyServer.empty()) {
            accessType = INTERNET_OPEN_TYPE_PROXY;
            proxyName = proxyServer.c_str();
        }

        hInternet = InternetOpenA(userAgent.c_str(), accessType, proxyName, NULL, 0);
        if (!hInternet) {
            if (!silent) std::cerr << "curl: failed to initialize: " << getLastErrorString();
            return 1;
        }

        if (timeout > 0) {
            InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionA(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionA(hInternet, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            InternetSetOptionA(hInternet, INTERNET_OPTION_DATA_SEND_TIMEOUT, &timeout, sizeof(timeout));
        }
        if (connectTimeout > 0) {
            InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &connectTimeout, sizeof(connectTimeout));
        }

        if (!proxyUserPass.empty()) {
            size_t pos = proxyUserPass.find(':');
            if (pos != std::string::npos) {
                std::string pUser = proxyUserPass.substr(0, pos);
                std::string pPass = proxyUserPass.substr(pos + 1);
                InternetSetOptionA(hInternet, INTERNET_OPTION_PROXY_USERNAME, (LPVOID)pUser.c_str(), (DWORD)pUser.length());
                InternetSetOptionA(hInternet, INTERNET_OPTION_PROXY_PASSWORD, (LPVOID)pPass.c_str(), (DWORD)pPass.length());
            }
        }

        int result;
        int attempts = 0;
        
        do {
            if (attempts > 0) {
                if (verbose) std::cerr << "* Retry " << attempts << " after " << retryDelay << "s...\n";
                Sleep(retryDelay * 1000);
                cleanup();
                hInternet = InternetOpenA(userAgent.c_str(), accessType, proxyName, NULL, 0);
            }

            if (protocol == "ftp" || protocol == "ftps") {
                result = executeFTP(host, port, path, urlUser, urlPass, protocol == "ftps");
            } else if (protocol == "smtp" || protocol == "smtps") {
                result = executeSMTP(host, port, path, urlUser, urlPass, protocol == "smtps");
            } else if (protocol == "telnet") {
                result = executeTelnet(host, port);
            } else if (protocol == "tcp") {
                result = executeTCP(host, port);
            } else if (protocol == "udp") {
                result = executeUDP(host, port);
            } else if (protocol == "gopher") {
                result = executeGopher(host, port, path);
            } else if (protocol == "dict") {
                result = executeDict(host, port, path);
            } else {
                result = executeHTTP(url, protocol, host, port, path, urlUser, urlPass);
            }
            
            attempts++;
        } while (result != 0 && attempts <= retryCount);

        return result;
    }
};

struct TransferHandle {
    std::string url;
    std::string outputFile;
    std::string uploadFile;
    std::string postData;
    std::vector<std::string> headers;
    std::string method;
    bool verbose;
    bool silent;
    bool followRedirects;
    int timeout;
    
    SOCKET socket;
    HINTERNET hInternet;
    HINTERNET hConnect;
    HINTERNET hRequest;
    
    enum State { PENDING, CONNECTING, SENDING, RECEIVING, COMPLETE, FAILED };
    State state;
    
    std::vector<char> sendBuffer;
    size_t sendOffset;
    std::vector<char> recvBuffer;
    size_t bytesReceived;
    size_t totalBytes;
    
    int httpStatus;
    std::string error;
    clock_t startTime;
    
    TransferHandle() : socket(INVALID_SOCKET), hInternet(NULL), hConnect(NULL), hRequest(NULL),
                       state(PENDING), sendOffset(0), bytesReceived(0), totalBytes(0),
                       httpStatus(0), verbose(false), silent(false), followRedirects(true),
                       timeout(30000) {}
    
    ~TransferHandle() {
        if (socket != INVALID_SOCKET) { closesocket(socket); WSACleanup(); }
        if (hRequest) InternetCloseHandle(hRequest);
        if (hConnect) InternetCloseHandle(hConnect);
        if (hInternet) InternetCloseHandle(hInternet);
    }
};

class CurlMulti {
private:
    std::vector<std::unique_ptr<TransferHandle>> handles;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> running;
    std::atomic<int> activeTransfers;
    std::atomic<size_t> totalBytesTransferred;
    int maxParallel;
    bool showProgress;
    bool verbose;
    bool silent;
    
public:
    CurlMulti(int parallel = 4) : maxParallel(parallel), running(true), activeTransfers(0),
                                   totalBytesTransferred(0), showProgress(false), verbose(false), silent(false) {
        for (int i = 0; i < maxParallel; i++) {
            workers.emplace_back([this] { workerThread(); });
        }
    }
    
    ~CurlMulti() {
        running = false;
        condition.notify_all();
        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    }
    
    void setMaxParallel(int n) { maxParallel = n; }
    void setShowProgress(bool p) { showProgress = p; }
    void setVerbose(bool v) { verbose = v; }
    void setSilent(bool s) { silent = s; }
    
    TransferHandle* addDownload(const std::string& url, const std::string& outputFile) {
        auto handle = std::make_unique<TransferHandle>();
        handle->url = url;
        handle->outputFile = outputFile;
        handle->method = "GET";
        handle->verbose = verbose;
        handle->silent = silent;
        TransferHandle* ptr = handle.get();
        handles.push_back(std::move(handle));
        return ptr;
    }
    
    TransferHandle* addUpload(const std::string& url, const std::string& inputFile) {
        auto handle = std::make_unique<TransferHandle>();
        handle->url = url;
        handle->uploadFile = inputFile;
        handle->method = "PUT";
        handle->verbose = verbose;
        handle->silent = silent;
        TransferHandle* ptr = handle.get();
        handles.push_back(std::move(handle));
        return ptr;
    }
    
    TransferHandle* addPost(const std::string& url, const std::string& data) {
        auto handle = std::make_unique<TransferHandle>();
        handle->url = url;
        handle->postData = data;
        handle->method = "POST";
        handle->verbose = verbose;
        handle->silent = silent;
        TransferHandle* ptr = handle.get();
        handles.push_back(std::move(handle));
        return ptr;
    }
    
    void perform() {
        for (auto& handle : handles) {
            if (handle->state == TransferHandle::PENDING) {
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    tasks.push([this, h = handle.get()] { executeTransfer(h); });
                }
                condition.notify_one();
            }
        }
        
        while (activeTransfers > 0 || !tasks.empty()) {
            if (showProgress && !silent) {
                printMultiProgress();
            }
            Sleep(100);
        }
        
        if (showProgress && !silent) std::cerr << "\n";
    }
    
    int getCompletedCount() const {
        int count = 0;
        for (const auto& h : handles) {
            if (h->state == TransferHandle::COMPLETE) count++;
        }
        return count;
    }
    
    int getFailedCount() const {
        int count = 0;
        for (const auto& h : handles) {
            if (h->state == TransferHandle::FAILED) count++;
        }
        return count;
    }
    
    size_t getTotalBytes() const { return totalBytesTransferred; }
    
    std::vector<TransferHandle*> getHandles() {
        std::vector<TransferHandle*> ptrs;
        for (auto& h : handles) ptrs.push_back(h.get());
        return ptrs;
    }
    
private:
    void workerThread() {
        while (running) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                condition.wait(lock, [this] { return !running || !tasks.empty(); });
                if (!running && tasks.empty()) return;
                task = std::move(tasks.front());
                tasks.pop();
            }
            activeTransfers++;
            task();
            activeTransfers--;
        }
    }
    
    void executeTransfer(TransferHandle* handle) {
        handle->startTime = clock();
        handle->state = TransferHandle::CONNECTING;
        
        std::string protocol, host, path, urlUser, urlPass;
        INTERNET_PORT port;
        
        if (!parseTransferUrl(handle->url, protocol, host, path, port, urlUser, urlPass)) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Invalid URL";
            return;
        }
        
        if (protocol == "http" || protocol == "https") {
            executeHttpTransfer(handle, protocol, host, port, path, urlUser, urlPass);
        } else if (protocol == "ftp" || protocol == "ftps") {
            executeFtpTransfer(handle, protocol, host, port, path, urlUser, urlPass);
        } else if (protocol == "tcp") {
            executeTcpTransfer(handle, host, port);
        } else if (protocol == "udp") {
            executeUdpTransfer(handle, host, port);
        } else {
            handle->state = TransferHandle::FAILED;
            handle->error = "Unsupported protocol: " + protocol;
        }
    }
    
    bool parseTransferUrl(const std::string& url, std::string& protocol, std::string& host,
                          std::string& path, INTERNET_PORT& port, std::string& user, std::string& pass) {
        size_t protoEnd = url.find("://");
        if (protoEnd == std::string::npos) {
            protocol = "http";
            protoEnd = 0;
        } else {
            protocol = url.substr(0, protoEnd);
            std::transform(protocol.begin(), protocol.end(), protocol.begin(), ::tolower);
            protoEnd += 3;
        }
        
        std::string remainder = url.substr(protoEnd);
        
        size_t atPos = remainder.find('@');
        if (atPos != std::string::npos) {
            std::string creds = remainder.substr(0, atPos);
            remainder = remainder.substr(atPos + 1);
            size_t colonPos = creds.find(':');
            if (colonPos != std::string::npos) {
                user = creds.substr(0, colonPos);
                pass = creds.substr(colonPos + 1);
            } else {
                user = creds;
            }
        }
        
        size_t hostEnd = remainder.find('/');
        if (hostEnd == std::string::npos) {
            host = remainder;
            path = "/";
        } else {
            host = remainder.substr(0, hostEnd);
            path = remainder.substr(hostEnd);
        }
        
        size_t colonPos = host.find(':');
        if (colonPos != std::string::npos) {
            port = (INTERNET_PORT)std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        } else {
            if (protocol == "https" || protocol == "ftps") port = 443;
            else if (protocol == "ftp") port = 21;
            else port = 80;
        }
        
        return !host.empty();
    }
    
    void executeHttpTransfer(TransferHandle* handle, const std::string& protocol,
                             const std::string& host, INTERNET_PORT port, const std::string& path,
                             const std::string& user, const std::string& pass) {
        handle->hInternet = InternetOpenA("CurlMulti/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!handle->hInternet) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Failed to initialize";
            return;
        }
        
        handle->hConnect = InternetConnectA(handle->hInternet, host.c_str(), port, NULL, NULL,
                                            INTERNET_SERVICE_HTTP, 0, 0);
        if (!handle->hConnect) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Connection failed";
            return;
        }
        
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (protocol == "https") flags |= INTERNET_FLAG_SECURE;
        if (handle->followRedirects) flags &= ~INTERNET_FLAG_NO_AUTO_REDIRECT;
        
        const char* acceptTypes[] = {"*/*", NULL};
        handle->hRequest = HttpOpenRequestA(handle->hConnect, handle->method.c_str(), path.c_str(),
                                            "HTTP/1.1", NULL, acceptTypes, flags, 0);
        if (!handle->hRequest) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Request failed";
            return;
        }
        
        if (!user.empty()) {
            InternetSetOptionA(handle->hRequest, INTERNET_OPTION_USERNAME, (LPVOID)user.c_str(), (DWORD)user.length());
            if (!pass.empty()) {
                InternetSetOptionA(handle->hRequest, INTERNET_OPTION_PASSWORD, (LPVOID)pass.c_str(), (DWORD)pass.length());
            }
        }
        
        for (const auto& h : handle->headers) {
            HttpAddRequestHeadersA(handle->hRequest, h.c_str(), -1, HTTP_ADDREQ_FLAG_ADD);
        }
        
        handle->state = TransferHandle::SENDING;
        
        std::string body;
        if (!handle->uploadFile.empty()) {
            std::ifstream file(handle->uploadFile, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                body = ss.str();
            }
        } else if (!handle->postData.empty()) {
            body = handle->postData;
        }
        
        BOOL result;
        if (!body.empty()) {
            result = HttpSendRequestA(handle->hRequest, NULL, 0, (LPVOID)body.c_str(), (DWORD)body.length());
        } else {
            result = HttpSendRequestA(handle->hRequest, NULL, 0, NULL, 0);
        }
        
        if (!result) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Send failed";
            return;
        }
        
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        HttpQueryInfoA(handle->hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusSize, NULL);
        handle->httpStatus = statusCode;
        
        DWORD contentLength = 0;
        DWORD clSize = sizeof(contentLength);
        HttpQueryInfoA(handle->hRequest, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &clSize, NULL);
        handle->totalBytes = contentLength;
        
        handle->state = TransferHandle::RECEIVING;
        
        char buffer[65536];
        DWORD bytesRead;
        std::ofstream outFile;
        
        if (!handle->outputFile.empty()) {
            outFile.open(handle->outputFile, std::ios::binary);
        }
        
        while (InternetReadFile(handle->hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            if (!handle->outputFile.empty() && outFile.is_open()) {
                outFile.write(buffer, bytesRead);
            } else {
                handle->recvBuffer.insert(handle->recvBuffer.end(), buffer, buffer + bytesRead);
            }
            handle->bytesReceived += bytesRead;
            totalBytesTransferred += bytesRead;
        }
        
        if (outFile.is_open()) outFile.close();
        
        handle->state = TransferHandle::COMPLETE;
        
        if (handle->verbose && !handle->silent) {
            std::cerr << "* Completed: " << handle->url << " (" << handle->bytesReceived << " bytes)\n";
        }
    }
    
    void executeFtpTransfer(TransferHandle* handle, const std::string& protocol,
                            const std::string& host, INTERNET_PORT port, const std::string& path,
                            const std::string& user, const std::string& pass) {
        handle->hInternet = InternetOpenA("CurlMulti/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!handle->hInternet) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Failed to initialize";
            return;
        }
        
        std::string ftpUser = user.empty() ? "anonymous" : user;
        std::string ftpPass = pass.empty() ? "curl@" : pass;
        
        DWORD ftpFlags = 0;
        if (protocol == "ftps") ftpFlags |= INTERNET_FLAG_SECURE;
        
        handle->hConnect = InternetConnectA(handle->hInternet, host.c_str(), port,
                                            ftpUser.c_str(), ftpPass.c_str(),
                                            INTERNET_SERVICE_FTP, ftpFlags | INTERNET_FLAG_PASSIVE, 0);
        if (!handle->hConnect) {
            handle->state = TransferHandle::FAILED;
            handle->error = "FTP connection failed";
            return;
        }
        
        handle->state = TransferHandle::RECEIVING;
        
        if (!handle->uploadFile.empty()) {
            std::string remotePath = path.empty() ? "/" + handle->uploadFile.substr(handle->uploadFile.find_last_of("/\\") + 1) : path;
            
            if (FtpPutFileA(handle->hConnect, handle->uploadFile.c_str(), remotePath.c_str(),
                           FTP_TRANSFER_TYPE_BINARY, 0)) {
                handle->state = TransferHandle::COMPLETE;
                if (handle->verbose) std::cerr << "* Uploaded: " << handle->uploadFile << " to " << remotePath << "\n";
            } else {
                handle->state = TransferHandle::FAILED;
                handle->error = "FTP upload failed";
            }
        } else {
            std::string localPath = handle->outputFile;
            if (localPath.empty()) {
                size_t lastSlash = path.find_last_of('/');
                localPath = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
            }
            
            if (FtpGetFileA(handle->hConnect, path.c_str(), localPath.c_str(),
                           FALSE, FILE_ATTRIBUTE_NORMAL, FTP_TRANSFER_TYPE_BINARY, 0)) {
                handle->state = TransferHandle::COMPLETE;
                
                WIN32_FIND_DATAA findData;
                HANDLE hFind = FindFirstFileA(localPath.c_str(), &findData);
                if (hFind != INVALID_HANDLE_VALUE) {
                    handle->bytesReceived = findData.nFileSizeLow;
                    totalBytesTransferred += findData.nFileSizeLow;
                    FindClose(hFind);
                }
                
                if (handle->verbose) std::cerr << "* Downloaded: " << path << " to " << localPath << "\n";
            } else {
                handle->state = TransferHandle::FAILED;
                handle->error = "FTP download failed";
            }
        }
    }
    
    void executeTcpTransfer(TransferHandle* handle, const std::string& host, int port) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            handle->state = TransferHandle::FAILED;
            handle->error = "WSAStartup failed";
            return;
        }
        
        struct addrinfo hints = {0}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            handle->state = TransferHandle::FAILED;
            handle->error = "DNS resolution failed";
            WSACleanup();
            return;
        }
        
        handle->socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (handle->socket == INVALID_SOCKET) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Socket creation failed";
            freeaddrinfo(result);
            WSACleanup();
            return;
        }
        
        if (connect(handle->socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Connection failed";
            freeaddrinfo(result);
            return;
        }
        
        freeaddrinfo(result);
        handle->state = TransferHandle::SENDING;
        
        std::string data;
        if (!handle->uploadFile.empty()) {
            std::ifstream file(handle->uploadFile, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                data = ss.str();
            }
        } else if (!handle->postData.empty()) {
            data = handle->postData;
        }
        
        if (!data.empty()) {
            send(handle->socket, data.c_str(), (int)data.length(), 0);
        }
        
        handle->state = TransferHandle::RECEIVING;
        
        char buffer[8192];
        int bytesRead;
        std::ofstream outFile;
        
        if (!handle->outputFile.empty()) {
            outFile.open(handle->outputFile, std::ios::binary);
        }
        
        while ((bytesRead = recv(handle->socket, buffer, sizeof(buffer), 0)) > 0) {
            if (outFile.is_open()) {
                outFile.write(buffer, bytesRead);
            } else {
                handle->recvBuffer.insert(handle->recvBuffer.end(), buffer, buffer + bytesRead);
            }
            handle->bytesReceived += bytesRead;
            totalBytesTransferred += bytesRead;
        }
        
        if (outFile.is_open()) outFile.close();
        handle->state = TransferHandle::COMPLETE;
    }
    
    void executeUdpTransfer(TransferHandle* handle, const std::string& host, int port) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            handle->state = TransferHandle::FAILED;
            handle->error = "WSAStartup failed";
            return;
        }
        
        struct addrinfo hints = {0}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            handle->state = TransferHandle::FAILED;
            handle->error = "DNS resolution failed";
            WSACleanup();
            return;
        }
        
        handle->socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (handle->socket == INVALID_SOCKET) {
            handle->state = TransferHandle::FAILED;
            handle->error = "Socket creation failed";
            freeaddrinfo(result);
            WSACleanup();
            return;
        }
        
        handle->state = TransferHandle::SENDING;
        
        std::string data;
        if (!handle->uploadFile.empty()) {
            std::ifstream file(handle->uploadFile, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                data = ss.str();
            }
        } else if (!handle->postData.empty()) {
            data = handle->postData;
        }
        
        if (!data.empty()) {
            sendto(handle->socket, data.c_str(), (int)data.length(), 0, result->ai_addr, (int)result->ai_addrlen);
        }
        
        freeaddrinfo(result);
        
        handle->state = TransferHandle::RECEIVING;
        
        struct timeval tv;
        tv.tv_sec = handle->timeout / 1000;
        tv.tv_usec = 0;
        setsockopt(handle->socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        
        char buffer[65536];
        struct sockaddr_storage from;
        int fromLen = sizeof(from);
        int bytesRead = recvfrom(handle->socket, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &fromLen);
        
        if (bytesRead > 0) {
            if (!handle->outputFile.empty()) {
                std::ofstream out(handle->outputFile, std::ios::binary);
                out.write(buffer, bytesRead);
            } else {
                handle->recvBuffer.insert(handle->recvBuffer.end(), buffer, buffer + bytesRead);
            }
            handle->bytesReceived = bytesRead;
            totalBytesTransferred += bytesRead;
        }
        
        handle->state = TransferHandle::COMPLETE;
    }
    
    void printMultiProgress() {
        int pending = 0, connecting = 0, sending = 0, receiving = 0, complete = 0, failed = 0;
        size_t totalRecv = 0, totalSize = 0;
        
        for (const auto& h : handles) {
            switch (h->state) {
                case TransferHandle::PENDING: pending++; break;
                case TransferHandle::CONNECTING: connecting++; break;
                case TransferHandle::SENDING: sending++; break;
                case TransferHandle::RECEIVING: receiving++; break;
                case TransferHandle::COMPLETE: complete++; break;
                case TransferHandle::FAILED: failed++; break;
            }
            totalRecv += h->bytesReceived;
            totalSize += h->totalBytes;
        }
        
        std::cerr << "\r[" << complete << "/" << handles.size() << " done] "
                  << totalRecv << " bytes";
        if (totalSize > 0) {
            std::cerr << " (" << (100 * totalRecv / totalSize) << "%)";
        }
        std::cerr << " | Active: " << (connecting + sending + receiving)
                  << " | Failed: " << failed << "   ";
    }
};

class JsonValue {
public:
    enum Type { Null, Boolean, Number, String, Array, Object };
private:
    Type type;
    bool boolVal;
    double numVal;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::map<std::string, JsonValue> objVal;
public:
    JsonValue() : type(Null), boolVal(false), numVal(0) {}
    JsonValue(bool b) : type(Boolean), boolVal(b), numVal(0) {}
    JsonValue(double n) : type(Number), boolVal(false), numVal(n) {}
    JsonValue(int n) : type(Number), boolVal(false), numVal((double)n) {}
    JsonValue(const std::string& s) : type(String), boolVal(false), numVal(0), strVal(s) {}
    JsonValue(const char* s) : type(String), boolVal(false), numVal(0), strVal(s) {}
    
    static JsonValue array() { JsonValue v; v.type = Array; return v; }
    static JsonValue object() { JsonValue v; v.type = Object; return v; }
    
    Type getType() const { return type; }
    bool isNull() const { return type == Null; }
    bool isBoolean() const { return type == Boolean; }
    bool isNumber() const { return type == Number; }
    bool isString() const { return type == String; }
    bool isArray() const { return type == Array; }
    bool isObject() const { return type == Object; }
    
    bool asBool() const { return boolVal; }
    double asNumber() const { return numVal; }
    int asInt() const { return (int)numVal; }
    const std::string& asString() const { return strVal; }
    
    void push(const JsonValue& val) { if (type == Array) arrVal.push_back(val); }
    void set(const std::string& key, const JsonValue& val) { if (type == Object) objVal[key] = val; }
    
    JsonValue& operator[](size_t idx) { return arrVal[idx]; }
    const JsonValue& operator[](size_t idx) const { return arrVal[idx]; }
    JsonValue& operator[](const std::string& key) { return objVal[key]; }
    const JsonValue& at(const std::string& key) const { return objVal.at(key); }
    bool has(const std::string& key) const { return objVal.find(key) != objVal.end(); }
    size_t size() const { return type == Array ? arrVal.size() : objVal.size(); }
    
    std::string serialize(int indent = 0) const {
        std::ostringstream ss;
        serializeImpl(ss, indent, 0);
        return ss.str();
    }
    
private:
    void serializeImpl(std::ostringstream& ss, int indent, int level) const {
        std::string pad(level * indent, ' ');
        std::string padInner((level + 1) * indent, ' ');
        
        switch (type) {
            case Null: ss << "null"; break;
            case Boolean: ss << (boolVal ? "true" : "false"); break;
            case Number:
                if (numVal == (int)numVal) ss << (int)numVal;
                else ss << std::fixed << std::setprecision(6) << numVal;
                break;
            case String:
                ss << "\"";
                for (char c : strVal) {
                    if (c == '"') ss << "\\\"";
                    else if (c == '\\') ss << "\\\\";
                    else if (c == '\n') ss << "\\n";
                    else if (c == '\r') ss << "\\r";
                    else if (c == '\t') ss << "\\t";
                    else ss << c;
                }
                ss << "\"";
                break;
            case Array:
                ss << "[";
                if (indent > 0 && !arrVal.empty()) ss << "\n";
                for (size_t i = 0; i < arrVal.size(); i++) {
                    if (indent > 0) ss << padInner;
                    arrVal[i].serializeImpl(ss, indent, level + 1);
                    if (i < arrVal.size() - 1) ss << ",";
                    if (indent > 0) ss << "\n";
                }
                if (indent > 0 && !arrVal.empty()) ss << pad;
                ss << "]";
                break;
            case Object:
                ss << "{";
                if (indent > 0 && !objVal.empty()) ss << "\n";
                size_t idx = 0;
                for (const auto& kv : objVal) {
                    if (indent > 0) ss << padInner;
                    ss << "\"" << kv.first << "\":";
                    if (indent > 0) ss << " ";
                    kv.second.serializeImpl(ss, indent, level + 1);
                    if (idx < objVal.size() - 1) ss << ",";
                    if (indent > 0) ss << "\n";
                    idx++;
                }
                if (indent > 0 && !objVal.empty()) ss << pad;
                ss << "}";
                break;
        }
    }
};

class JsonParser {
private:
    std::string input;
    size_t pos;
    
    char peek() const { return pos < input.length() ? input[pos] : '\0'; }
    char get() { return pos < input.length() ? input[pos++] : '\0'; }
    void skipWhitespace() { while (pos < input.length() && std::isspace(input[pos])) pos++; }
    
    JsonValue parseValue() {
        skipWhitespace();
        char c = peek();
        
        if (c == 'n') return parseNull();
        if (c == 't' || c == 'f') return parseBoolean();
        if (c == '"') return parseString();
        if (c == '[') return parseArray();
        if (c == '{') return parseObject();
        if (c == '-' || std::isdigit(c)) return parseNumber();
        
        return JsonValue();
    }
    
    JsonValue parseNull() {
        if (input.substr(pos, 4) == "null") { pos += 4; return JsonValue(); }
        return JsonValue();
    }
    
    JsonValue parseBoolean() {
        if (input.substr(pos, 4) == "true") { pos += 4; return JsonValue(true); }
        if (input.substr(pos, 5) == "false") { pos += 5; return JsonValue(false); }
        return JsonValue();
    }
    
    JsonValue parseNumber() {
        size_t start = pos;
        if (peek() == '-') pos++;
        while (std::isdigit(peek())) pos++;
        if (peek() == '.') {
            pos++;
            while (std::isdigit(peek())) pos++;
        }
        if (peek() == 'e' || peek() == 'E') {
            pos++;
            if (peek() == '+' || peek() == '-') pos++;
            while (std::isdigit(peek())) pos++;
        }
        return JsonValue(std::stod(input.substr(start, pos - start)));
    }
    
    JsonValue parseString() {
        get();
        std::string result;
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') {
                get();
                char e = get();
                switch (e) {
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'u': {
                        std::string hex = input.substr(pos, 4);
                        pos += 4;
                        int code = std::stoi(hex, nullptr, 16);
                        if (code < 0x80) result += (char)code;
                        else if (code < 0x800) {
                            result += (char)(0xC0 | (code >> 6));
                            result += (char)(0x80 | (code & 0x3F));
                        } else {
                            result += (char)(0xE0 | (code >> 12));
                            result += (char)(0x80 | ((code >> 6) & 0x3F));
                            result += (char)(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: result += e;
                }
            } else {
                result += get();
            }
        }
        get();
        return JsonValue(result);
    }
    
    JsonValue parseArray() {
        get();
        JsonValue arr = JsonValue::array();
        skipWhitespace();
        if (peek() == ']') { get(); return arr; }
        
        while (true) {
            arr.push(parseValue());
            skipWhitespace();
            if (peek() == ']') { get(); break; }
            if (peek() == ',') get();
        }
        return arr;
    }
    
    JsonValue parseObject() {
        get();
        JsonValue obj = JsonValue::object();
        skipWhitespace();
        if (peek() == '}') { get(); return obj; }
        
        while (true) {
            skipWhitespace();
            std::string key = parseString().asString();
            skipWhitespace();
            get();
            obj.set(key, parseValue());
            skipWhitespace();
            if (peek() == '}') { get(); break; }
            if (peek() == ',') get();
        }
        return obj;
    }
    
public:
    JsonValue parse(const std::string& json) {
        input = json;
        pos = 0;
        return parseValue();
    }
};

class UrlCodec {
public:
    static std::string encode(const std::string& str) {
        std::ostringstream encoded;
        for (unsigned char c : str) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded << c;
            } else if (c == ' ') {
                encoded << '+';
            } else {
                encoded << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
            }
        }
        return encoded.str();
    }
    
    static std::string decode(const std::string& str) {
        std::string decoded;
        for (size_t i = 0; i < str.length(); i++) {
            if (str[i] == '%' && i + 2 < str.length()) {
                int val = std::stoi(str.substr(i + 1, 2), nullptr, 16);
                decoded += (char)val;
                i += 2;
            } else if (str[i] == '+') {
                decoded += ' ';
            } else {
                decoded += str[i];
            }
        }
        return decoded;
    }
    
    static std::map<std::string, std::string> parseQueryString(const std::string& query) {
        std::map<std::string, std::string> params;
        std::istringstream iss(query);
        std::string pair;
        
        while (std::getline(iss, pair, '&')) {
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                std::string key = decode(pair.substr(0, eq));
                std::string val = decode(pair.substr(eq + 1));
                params[key] = val;
            } else {
                params[decode(pair)] = "";
            }
        }
        return params;
    }
    
    static std::string buildQueryString(const std::map<std::string, std::string>& params) {
        std::ostringstream ss;
        bool first = true;
        for (const auto& kv : params) {
            if (!first) ss << "&";
            ss << encode(kv.first) << "=" << encode(kv.second);
            first = false;
        }
        return ss.str();
    }
};

class Base64 {
private:
    static const std::string chars;
public:
    static std::string encode(const std::string& input) {
        std::string output;
        int val = 0, valb = -6;
        
        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                output.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        
        if (valb > -6) output.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (output.size() % 4) output.push_back('=');
        
        return output;
    }
    
    static std::string decode(const std::string& input) {
        std::string output;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[chars[i]] = i;
        
        int val = 0, valb = -8;
        for (unsigned char c : input) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                output.push_back((char)((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return output;
    }
};

const std::string Base64::chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";



class MultipartBuilder {
private:
    std::string boundary;
    std::ostringstream data;
    
public:
    MultipartBuilder() {
        boundary = "----CurlBoundary" + std::to_string(time(NULL)) + std::to_string(rand());
    }
    
    std::string getBoundary() const { return boundary; }
    
    void addField(const std::string& name, const std::string& value) {
        data << "--" << boundary << "\r\n";
        data << "Content-Disposition: form-data; name=\"" << name << "\"\r\n\r\n";
        data << value << "\r\n";
    }
    
    void addFile(const std::string& name, const std::string& filename, const std::string& content, const std::string& mimeType = "application/octet-stream") {
        data << "--" << boundary << "\r\n";
        data << "Content-Disposition: form-data; name=\"" << name << "\"; filename=\"" << filename << "\"\r\n";
        data << "Content-Type: " << mimeType << "\r\n\r\n";
        data << content << "\r\n";
    }
    
    std::string build() {
        std::string result = data.str();
        result += "--" + boundary + "--\r\n";
        return result;
    }
    
    std::string getContentType() const {
        return "multipart/form-data; boundary=" + boundary;
    }
};

struct ConnectionInfo {
    std::string host;
    int port;
    bool secure;
    time_t lastUsed;
    bool inUse;
};

class ConnectionPool {
private:
    std::vector<ConnectionInfo> connections;
    size_t maxConnections;
    std::mutex poolMutex;
    
public:
    ConnectionPool(size_t max = 10) : maxConnections(max) {}
    
    ConnectionInfo* acquire(const std::string& host, int port, bool secure) {
        std::lock_guard<std::mutex> lock(poolMutex);
        
        for (auto& conn : connections) {
            if (!conn.inUse && conn.host == host && conn.port == port && conn.secure == secure) {
                conn.inUse = true;
                conn.lastUsed = time(NULL);
                return &conn;
            }
        }
        
        if (connections.size() < maxConnections) {
            connections.push_back({host, port, secure, time(NULL), true});
            return &connections.back();
        }
        
        time_t oldest = time(NULL);
        ConnectionInfo* oldestConn = nullptr;
        for (auto& conn : connections) {
            if (!conn.inUse && conn.lastUsed < oldest) {
                oldest = conn.lastUsed;
                oldestConn = &conn;
            }
        }
        
        if (oldestConn) {
            oldestConn->host = host;
            oldestConn->port = port;
            oldestConn->secure = secure;
            oldestConn->lastUsed = time(NULL);
            oldestConn->inUse = true;
            return oldestConn;
        }
        
        return nullptr;
    }
    
    void release(ConnectionInfo* conn) {
        if (conn) conn->inUse = false;
    }
    
    void cleanup(int maxIdleSeconds = 60) {
        std::lock_guard<std::mutex> lock(poolMutex);
        time_t now = time(NULL);
        connections.erase(
            std::remove_if(connections.begin(), connections.end(),
                [now, maxIdleSeconds](const ConnectionInfo& c) {
                    return !c.inUse && (now - c.lastUsed) > maxIdleSeconds;
                }),
            connections.end()
        );
    }
};

struct RequestMetrics {
    double dnsTime;
    double connectTime;
    double tlsTime;
    double sendTime;
    double waitTime;
    double receiveTime;
    double totalTime;
    size_t bytesSent;
    size_t bytesReceived;
    int redirectCount;
    
    void reset() {
        dnsTime = connectTime = tlsTime = sendTime = waitTime = receiveTime = totalTime = 0;
        bytesSent = bytesReceived = 0;
        redirectCount = 0;
    }
    
    std::string format() const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << "DNS:      " << (dnsTime * 1000) << " ms\n";
        ss << "Connect:  " << (connectTime * 1000) << " ms\n";
        ss << "TLS:      " << (tlsTime * 1000) << " ms\n";
        ss << "Send:     " << (sendTime * 1000) << " ms\n";
        ss << "Wait:     " << (waitTime * 1000) << " ms\n";
        ss << "Receive:  " << (receiveTime * 1000) << " ms\n";
        ss << "Total:    " << (totalTime * 1000) << " ms\n";
        ss << "Sent:     " << bytesSent << " bytes\n";
        ss << "Received: " << bytesReceived << " bytes\n";
        if (redirectCount > 0) ss << "Redirects: " << redirectCount << "\n";
        return ss.str();
    }
};

class RateLimiter {
private:
    double tokensPerSecond;
    double maxTokens;
    double tokens;
    std::chrono::steady_clock::time_point lastRefill;
    std::mutex mtx;
    
public:
    RateLimiter(double rate, double burst) 
        : tokensPerSecond(rate), maxTokens(burst), tokens(burst) {
        lastRefill = std::chrono::steady_clock::now();
    }
    
    bool tryAcquire(double count = 1.0) {
        std::lock_guard<std::mutex> lock(mtx);
        refill();
        if (tokens >= count) {
            tokens -= count;
            return true;
        }
        return false;
    }
    
    void waitFor(double count = 1.0) {
        while (!tryAcquire(count)) {
            Sleep(10);
        }
    }
    
private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastRefill).count();
        tokens = std::min(maxTokens, tokens + elapsed * tokensPerSecond);
        lastRefill = now;
    }
};

void printUsage() {
    std::cout << "Usage: curl [options...] <url>\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <file>      Write output to <file>\n";
    std::cout << "  -O, --remote-name        Write output to file named like remote file\n";
    std::cout << "  -I, --head               Show response headers only\n";
    std::cout << "  -i, --include            Include response headers in output\n";
    std::cout << "  -s, --silent             Silent mode (no progress/errors)\n";
    std::cout << "  -S, --show-error         Show errors in silent mode\n";
    std::cout << "  -v, --verbose            Verbose output\n";
    std::cout << "  -L, --location           Follow redirects\n";
    std::cout << "  --max-redirs <num>       Maximum redirects (default: 50)\n";
    std::cout << "  -X, --request <method>   HTTP method (GET, POST, PUT, DELETE, PATCH)\n";
    std::cout << "  -H, --header <header>    Add custom header\n";
    std::cout << "  -d, --data <data>        POST data\n";
    std::cout << "  --data-raw <data>        POST data (no @ interpretation)\n";
    std::cout << "  --data-binary <data>     Binary POST data\n";
    std::cout << "  --data-urlencode <data>  URL-encode POST data\n";
    std::cout << "  -F, --form <name=value>  Multipart form data\n";
    std::cout << "  -T, --upload-file <file> Upload file\n";
    std::cout << "  -u, --user <user:pass>   Authentication credentials\n";
    std::cout << "  -A, --user-agent <name>  User-Agent string\n";
    std::cout << "  -e, --referer <url>      Referer URL\n";
    std::cout << "  -r, --range <range>      Byte range (e.g., 0-499)\n";
    std::cout << "  -C, --continue-at <off>  Resume download from offset (use - for auto)\n";
    std::cout << "  -x, --proxy <host:port>  Use proxy\n";
    std::cout << "  -U, --proxy-user <u:p>   Proxy credentials\n";
    std::cout << "  -k, --insecure           Allow insecure SSL connections\n";
    std::cout << "  --compressed             Request compressed response\n";
    std::cout << "  -f, --fail               Fail silently on HTTP errors\n";
    std::cout << "  --connect-timeout <sec>  Connection timeout\n";
    std::cout << "  -m, --max-time <sec>     Maximum operation time\n";
    std::cout << "  --retry <num>            Retry on transient errors\n";
    std::cout << "  --retry-delay <sec>      Delay between retries\n";
    std::cout << "  --max-filesize <bytes>   Maximum file size to download\n";
    std::cout << "  -#, --progress-bar       Show progress bar\n";
    std::cout << "  -w, --write-out <fmt>    Output format after completion\n";
    std::cout << "  -h, --help               Show this help\n";
    std::cout << "  -V, --version            Show version\n\n";
    std::cout << "Protocols: http, https, ftp, ftps\n";
}

void printVersion() {
    std::cout << "curl 8.0 (linuxify)\n";
    std::cout << "Release-Date: 2024\n";
    std::cout << "Protocols: http https ftp ftps\n";
    std::cout << "Features: IPv6 Largefile SSL\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    CurlClient client;
    std::vector<std::string> urls;
    std::string writeOut;
    std::string outputDir;
    bool showError = false;
    bool parallel = false;
    int parallelMax = 4;
    bool verbose = false;
    bool silent = false;
    bool showProgress = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "-V" || arg == "--version") {
            printVersion();
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            client.setVerbose(true);
        } else if (arg == "-s" || arg == "--silent") {
            client.setSilent(true);
        } else if (arg == "-S" || arg == "--show-error") {
            showError = true;
        } else if (arg == "-I" || arg == "--head") {
            client.setHeadOnly(true);
            client.setShowHeaders(true);
        } else if (arg == "-i" || arg == "--include") {
            client.setIncludeHeaders(true);
        } else if (arg == "-L" || arg == "--location") {
            client.setFollowRedirects(true);
        } else if (arg == "--no-location") {
            client.setFollowRedirects(false);
        } else if (arg == "--max-redirs" && i + 1 < argc) {
            client.setMaxRedirects(std::stoi(argv[++i]));
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            client.setOutputFile(argv[++i]);
        } else if (arg == "-O" || arg == "--remote-name") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                std::string nextUrl = argv[i + 1];
                size_t pos = nextUrl.find_last_of('/');
                if (pos != std::string::npos && pos + 1 < nextUrl.length()) {
                    std::string filename = nextUrl.substr(pos + 1);
                    size_t qpos = filename.find('?');
                    if (qpos != std::string::npos) filename = filename.substr(0, qpos);
                    if (!filename.empty()) client.setOutputFile(filename);
                }
            }
        } else if ((arg == "-X" || arg == "--request") && i + 1 < argc) {
            client.setMethod(argv[++i]);
        } else if ((arg == "-H" || arg == "--header") && i + 1 < argc) {
            client.addHeader(argv[++i]);
        } else if ((arg == "-d" || arg == "--data" || arg == "--data-raw" || arg == "--data-binary") && i + 1 < argc) {
            std::string data = argv[++i];
            if (arg == "-d" && data[0] == '@') {
                std::ifstream file(data.substr(1));
                if (file) {
                    std::ostringstream ss;
                    ss << file.rdbuf();
                    client.setPostData(ss.str());
                }
            } else {
                client.setPostData(data);
            }
        } else if (arg == "--data-urlencode" && i + 1 < argc) {
            std::string data = argv[++i];
            size_t eq = data.find('=');
            if (eq != std::string::npos) {
                std::string name = data.substr(0, eq);
                std::string value = data.substr(eq + 1);
                std::ostringstream encoded;
                for (char c : value) {
                    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                        encoded << c;
                    } else {
                        encoded << '%' << std::uppercase << std::hex << std::setw(2) 
                                << std::setfill('0') << (int)(unsigned char)c;
                    }
                }
                client.setPostData(name + "=" + encoded.str());
            }
        } else if ((arg == "-F" || arg == "--form") && i + 1 < argc) {
            std::string formArg = argv[++i];
            size_t eq = formArg.find('=');
            if (eq != std::string::npos) {
                client.addFormField(formArg.substr(0, eq), formArg.substr(eq + 1));
            }
        } else if ((arg == "-T" || arg == "--upload-file") && i + 1 < argc) {
            client.setUploadFile(argv[++i]);
        } else if ((arg == "-u" || arg == "--user") && i + 1 < argc) {
            client.setUser(argv[++i]);
        } else if ((arg == "-A" || arg == "--user-agent") && i + 1 < argc) {
            client.setUserAgent(argv[++i]);
        } else if ((arg == "-e" || arg == "--referer") && i + 1 < argc) {
            client.setReferer(argv[++i]);
        } else if ((arg == "-r" || arg == "--range") && i + 1 < argc) {
            client.setRange(argv[++i]);
        } else if ((arg == "-C" || arg == "--continue-at") && i + 1 < argc) {
            std::string offset = argv[++i];
            if (offset == "-") {
                client.setResumeDownload(true);
            } else {
                client.setRange(offset + "-");
            }
        } else if ((arg == "-x" || arg == "--proxy") && i + 1 < argc) {
            client.setProxy(argv[++i]);
        } else if ((arg == "-U" || arg == "--proxy-user") && i + 1 < argc) {
            client.setProxyUser(argv[++i]);
        } else if (arg == "-k" || arg == "--insecure") {
            client.setInsecure(true);
        } else if (arg == "--compressed") {
            client.setCompressed(true);
        } else if (arg == "-f" || arg == "--fail") {
            client.setFailOnError(true);
        } else if (arg == "--connect-timeout" && i + 1 < argc) {
            client.setConnectTimeout(std::stoi(argv[++i]));
        } else if ((arg == "-m" || arg == "--max-time") && i + 1 < argc) {
            client.setTimeout(std::stoi(argv[++i]));
        } else if (arg == "--retry" && i + 1 < argc) {
            int retry = std::stoi(argv[++i]);
            int delay = 1;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--retry-delay") {
                i++;
                if (i + 1 < argc) delay = std::stoi(argv[++i]);
            }
            client.setRetry(retry, delay);
        } else if (arg == "--retry-delay" && i + 1 < argc) {
            i++;
        } else if (arg == "--max-filesize" && i + 1 < argc) {
            client.setMaxFileSize(std::stol(argv[++i]));
        } else if (arg == "-#" || arg == "--progress-bar") {
            client.setShowProgress(true);
            showProgress = true;
        } else if ((arg == "-w" || arg == "--write-out") && i + 1 < argc) {
            writeOut = argv[++i];
        } else if (arg == "-Z" || arg == "--parallel") {
            parallel = true;
        } else if (arg == "--parallel-max" && i + 1 < argc) {
            parallelMax = std::stoi(argv[++i]);
        } else if (arg == "--output-dir" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg[0] != '-') {
            urls.push_back(arg);
        }
    }

    if (urls.empty()) {
        std::cerr << "curl: no URL specified\n";
        std::cerr << "curl: try 'curl --help' for more information\n";
        return 1;
    }

    int result = 0;
    
    if (parallel && urls.size() > 1) {
        CurlMulti multi(parallelMax);
        multi.setVerbose(verbose);
        multi.setSilent(silent);
        multi.setShowProgress(showProgress);
        
        for (const auto& u : urls) {
            std::string outFile;
            if (!outputDir.empty()) {
                size_t pos = u.find_last_of('/');
                if (pos != std::string::npos && pos + 1 < u.length()) {
                    std::string filename = u.substr(pos + 1);
                    size_t qpos = filename.find('?');
                    if (qpos != std::string::npos) filename = filename.substr(0, qpos);
                    outFile = outputDir + "\\" + filename;
                }
            }
            multi.addDownload(u, outFile);
        }
        
        multi.perform();
        
        if (!silent) {
            std::cout << "\nParallel transfer summary:\n";
            std::cout << "  Completed: " << multi.getCompletedCount() << "/" << urls.size() << "\n";
            std::cout << "  Failed:    " << multi.getFailedCount() << "\n";
            std::cout << "  Total:     " << multi.getTotalBytes() << " bytes\n";
        }
        
        result = (multi.getFailedCount() > 0) ? 1 : 0;
        
        for (auto* h : multi.getHandles()) {
            if (h->state == TransferHandle::FAILED) {
                std::cerr << "  Error: " << h->url << " - " << h->error << "\n";
            }
        }
    } else {
        for (const auto& u : urls) {
            result = client.execute(u);
            if (result != 0 && urls.size() > 1) {
                std::cerr << "curl: failed for " << u << "\n";
            }
        }
    }
    
    if (!writeOut.empty()) {
        std::cout << writeOut;
    }

    return result;
}
