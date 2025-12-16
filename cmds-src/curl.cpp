// g++ -std=c++17 -static -o curl.exe curl.cpp -lwininet

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

#pragma comment(lib, "wininet.lib")

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
    
    std::vector<std::string> headers;
    std::vector<std::pair<std::string, std::string>> formData;
    
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
    
    int maxRedirects;
    int timeout;
    int connectTimeout;
    int retryCount;
    int retryDelay;
    long maxFileSize;
    long speedLimit;

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

        char buffer[65536];
        DWORD bytesRead;
        DWORD totalBytes = 0;
        clock_t startTime = clock();

        while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            out->write(buffer, bytesRead);
            totalBytes += bytesRead;
            
            if (showProgress && contentLength > 0) {
                printProgress(totalBytes, contentLength);
            }
            
            if (maxFileSize > 0 && totalBytes > (DWORD)maxFileSize) {
                if (!silent) std::cerr << "\ncurl: maximum file size exceeded\n";
                break;
            }
        }

        if (showProgress && contentLength > 0) {
            std::cerr << "\n";
        }

        if (!outputFile.empty() && !silent) {
            double elapsed = (double)(clock() - startTime) / CLOCKS_PER_SEC;
            double speed = elapsed > 0 ? totalBytes / elapsed : 0;
            
            std::cerr << "  Downloaded " << totalBytes << " bytes";
            if (speed >= 1024 * 1024) {
                std::cerr << " (" << std::fixed << std::setprecision(1) << (speed / (1024 * 1024)) << " MB/s)";
            } else if (speed >= 1024) {
                std::cerr << " (" << std::fixed << std::setprecision(1) << (speed / 1024) << " KB/s)";
            }
            std::cerr << "\n";
        }

        return 0;
    }

public:
    CurlClient() : hInternet(NULL), hConnect(NULL), hRequest(NULL),
                   userAgent("curl/8.0 (linuxify)"), verbose(false), silent(false),
                   showHeaders(false), includeHeaders(false), followRedirects(true),
                   insecure(false), showProgress(false), resumeDownload(false),
                   failOnError(false), compressedResponse(false), headOnly(false),
                   uploadMode(false), appendOutput(false), createDirs(false), remoteTime(false),
                   maxRedirects(50), timeout(0), connectTimeout(300), retryCount(0), 
                   retryDelay(1), maxFileSize(0), speedLimit(0), method("GET") {}

    ~CurlClient() { cleanup(); }

    void setVerbose(bool v) { verbose = v; }
    void setSilent(bool s) { silent = s; if (s) showProgress = false; }
    void setShowHeaders(bool h) { showHeaders = h; }
    void setIncludeHeaders(bool h) { includeHeaders = h; }
    void setFollowRedirects(bool f) { followRedirects = f; }
    void setMaxRedirects(int m) { maxRedirects = m; }
    void setInsecure(bool i) { insecure = i; }
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
            } else {
                result = executeHTTP(url, protocol, host, port, path, urlUser, urlPass);
            }
            
            attempts++;
        } while (result != 0 && attempts <= retryCount);

        return result;
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
    std::string url;
    std::string writeOut;
    bool showError = false;

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
        } else if ((arg == "-w" || arg == "--write-out") && i + 1 < argc) {
            writeOut = argv[++i];
        } else if (arg[0] != '-') {
            url = arg;
        }
    }

    if (url.empty()) {
        std::cerr << "curl: no URL specified\n";
        std::cerr << "curl: try 'curl --help' for more information\n";
        return 1;
    }

    int result = client.execute(url);
    
    if (!writeOut.empty()) {
        std::cout << writeOut;
    }

    return result;
}
