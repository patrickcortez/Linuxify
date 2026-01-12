// Linuxify AI Command - Native Gemini Client for Windows
// Compile: g++ -std=c++17 -static -o ai.exe ai.cpp -lwininet

#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <sstream>

// Link against the Windows Internet library
#pragma comment(lib, "wininet")

// 1. JSON String Escaper (Basic)
std::string escapeJson(const std::string& input) {
    std::string output;
    for (char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += c; break;
        }
    }
    return output;
}

// 2. HTTP POST Request using WinInet
std::string makeRequest(const std::string& apiKey, const std::string& prompt) {
    HINTERNET hInternet = InternetOpenA("LinuxifyAI/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return "Error: InternetOpen failed";

    HINTERNET hConnect = InternetConnectA(
        hInternet, 
        "generativelanguage.googleapis.com", 
        INTERNET_DEFAULT_HTTPS_PORT, 
        NULL, NULL, 
        INTERNET_SERVICE_HTTP, 
        0, 1
    );

    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return "Error: Could not connect to Google API";
    }

    HINTERNET hRequest = HttpOpenRequestA(
        hConnect, 
        "POST", 
        ("/v1beta/models/gemini-2.5-flash:generateContent?key=" + apiKey).c_str(),
        NULL, NULL, NULL, 
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 
        1
    );

    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return "Error: Could not create request";
    }

    std::string headers = "Content-Type: application/json\r\n";
    std::string body = "{\"contents\":[{\"parts\":[{\"text\":\"" + escapeJson(prompt) + "\"}]}]}";

    if (!HttpSendRequestA(hRequest, headers.c_str(), headers.length(), (LPVOID)body.c_str(), body.length())) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return "Error: Request failed";
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    return response;
}

// 3. Simple Regex JSON Parser to extract "text"
std::string parseResponse(const std::string& json) {
    // Look for "text": "..."
    std::regex textRegex("\"text\":\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch match;
    
    // We only care about the first match for simple prompts
    if (std::regex_search(json, match, textRegex)) {
        std::string text = match[1].str();
        
        // Unescape the JSON string for display
        std::string unescaped;
        for (size_t i = 0; i < text.length(); i++) {
            if (text[i] == '\\' && i + 1 < text.length()) {
                char next = text[i + 1];
                switch (next) {
                    case 'n': unescaped += '\n'; break;
                    case 't': unescaped += '\t'; break;
                    case '\"': unescaped += '\"'; break;
                    case '\\': unescaped += '\\'; break;
                    default: unescaped += next; break;
                }
                i++;
            } else {
                unescaped += text[i];
            }
        }
        return unescaped;
    }
    
    // Fallback: Check for error messages
    if (json.find("\"error\"") != std::string::npos) {
        return "API Error: " + json;
    }
    
    return "Error: Could not parse response.\nRaw: " + json;
}

int main(int argc, char* argv[]) {
    // 1. Get Environment Variable
    char* envKey = getenv("GEMINI_API_KEY");
    if (!envKey) {
        std::cerr << "Error: GEMINI_API_KEY environment variable is not set.\n"
                  << "Please run: export GEMINI_API_KEY=your_key_here\n";
        return 1;
    }
    std::string apiKey = envKey;

    // Sanitize API Key
    // 1. Trim whitespace
    apiKey.erase(0, apiKey.find_first_not_of(" \t\r\n"));
    apiKey.erase(apiKey.find_last_not_of(" \t\r\n") + 1);

    // 2. Fix Double Paste (Common User Error)
    // Google API usage keys start with "AIza" and are usually 39 chars long.
    // If we see "AIza" appearing twice and the length looks like double, cut it.
    if (apiKey.length() > 60 && apiKey.substr(0, 4) == "AIza") {
        size_t secondM = apiKey.find("AIza", 4);
        if (secondM != std::string::npos && secondM == apiKey.length() / 2) {
             std::cerr << "Warning: Detected duplicated API key. Auto-fixing..." << std::endl;
             apiKey = apiKey.substr(0, secondM);
        }
    }

    // 2. Process Arguments
    if (argc < 2) {
        std::cout << "Usage: ai \"your prompt here\"\n";
        return 0;
    }

    std::string prompt;
    for (int i = 1; i < argc; i++) {
        if (i > 1) prompt += " ";
        prompt += argv[i];
    }

    // 3. Execute
    // std::cout << "Thinking..." << std::endl;
    std::string jsonResponse = makeRequest(apiKey, prompt);
    std::string reply = parseResponse(jsonResponse);

    // 4. Output
    std::cout << reply << std::endl;

    return 0;
}