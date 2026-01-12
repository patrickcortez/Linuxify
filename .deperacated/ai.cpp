// Linuxify AI Command - Native Gemini Client for Windows
// Compile: g++ -std=c++17 -static -o ai.exe ai.cpp -lwininet

#include <windows.h>
#include <wininet.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

// Link against the Windows Internet library
#pragma comment(lib, "wininet")

// Helper function to escape special characters for JSON request body
std::string escapeJson(const std::string& input) {
    std::string output;
    output.reserve(input.length() * 2); 
    for (char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", c);
                    output += buf;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

// Function to make the HTTP POST request
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

    if (!HttpSendRequestA(hRequest, headers.c_str(), static_cast<DWORD>(headers.length()), (LPVOID)body.c_str(), static_cast<DWORD>(body.length()))) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return "Error: Request failed";
    }

    // Check HTTP Status Code
    DWORD statusCode = 0;
    DWORD statusCodeLen = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeLen, NULL);

    std::string response;
    char buffer[4096];
    DWORD bytesRead;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    if (statusCode != 200) {
        // Return the error body directly if not 200, so we can parse the error message
        return "API Error (" + std::to_string(statusCode) + "): " + response;
    }

    return response;
}

// Helper to skip whitespace
size_t skipWhitespace(const std::string& json, size_t pos) {
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }
    return pos;
}

// Helper to find a specific key in current scope
// Returns position AFTER the key and colon, or std::string::npos
size_t findKey(const std::string& json, size_t start, const std::string& key) {
    size_t pos = start;
    while (pos < json.length()) {
        pos = json.find('"', pos);
        if (pos == std::string::npos) return std::string::npos;
        
        size_t keyStart = pos + 1;
        size_t keyEnd = json.find('"', keyStart);
        if (keyEnd == std::string::npos) return std::string::npos;
        
        std::string currentKey = json.substr(keyStart, keyEnd - keyStart);
        pos = keyEnd + 1;
        
        pos = skipWhitespace(json, pos);
        if (pos < json.length() && json[pos] == ':') {
            pos++; // Skip colon
            if (currentKey == key) {
                return skipWhitespace(json, pos);
            }
        }
        
        // If not the key we want, we need to skip the value. 
        // This is a naive skip, a full parser would be better but this is sufficient for known structure traversal
        // However, since we are looking for a specific path, we can just search forward if we are careful.
        // But for "candidates", "content", "parts", they are unique enough in the structure.
    }
    return std::string::npos;
}

// Extract string value starting at pos (must be at opening quote)
std::string extractString(const std::string& json, size_t& pos) {
    if (pos >= json.length() || json[pos] != '"') return "";
    
    pos++; // skip opening quote
    std::string result;
    bool escape = false;
    
    while (pos < json.length()) {
        char c = json[pos];
        if (escape) {
            switch (c) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (pos + 4 < json.length()) {
                        std::string hex = json.substr(pos + 1, 4);
                        try {
                            int code = std::stoi(hex, nullptr, 16);
                            if (code < 128) result += (char)code;
                            else result += "?"; // Simplified unicode handling
                        } catch (...) {}
                        pos += 4;
                    }
                    break;
                }
                default: result += c; break;
            }
            escape = false;
        } else {
            if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                pos++; // skip closing quote
                return result;
            } else {
                result += c;
            }
        }
        pos++;
    }
    return result;
}

std::string parseResponse(const std::string& json) {
    // We expect: { "candidates": [ { "content": { "parts": [ { "text": "VALUE" } ] } } ] }
    
    // 1. Find "candidates"
    size_t pos = json.find("\"candidates\"");
    if (pos == std::string::npos) {
        if (json.find("\"error\"") != std::string::npos) return json; // Return raw error
        return "Error: Invalid response format (no candidates)";
    }
    
    // 2. Find "content" after "candidates"
    pos = json.find("\"content\"", pos);
    if (pos == std::string::npos) return "Error: Invalid response format (no content)";
    
    // 3. Find "parts" after "content"
    pos = json.find("\"parts\"", pos);
    if (pos == std::string::npos) return "Error: Invalid response format (no parts)";
    
    // 4. Find "text" after "parts"
    pos = json.find("\"text\"", pos);
    if (pos == std::string::npos) return "Error: Invalid response format (no text)";
    
    // 5. Move to the value start
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "Error: Parse error after text key";
    pos = skipWhitespace(json, pos + 1);
    
    // 6. Extract the string
    return extractString(json, pos);
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
    size_t first = apiKey.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
         // All whitespace
         std::cerr << "Error: GEMINI_API_KEY is empty or whitespace only.\n";
         return 1;
    }
    apiKey.erase(0, first);
    size_t last = apiKey.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) {
        apiKey.erase(last + 1);
    }

    // 2. Fix Double Paste (Common User Error)
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
    std::string jsonResponse = makeRequest(apiKey, prompt);
    
    // Check for API errors early
    if (jsonResponse.find("API Error") == 0) {
        std::cerr << jsonResponse << std::endl;
        return 1;
    }

    std::string reply = parseResponse(jsonResponse);

    // 4. Output
    std::cout << reply << std::endl;

    return 0;
}