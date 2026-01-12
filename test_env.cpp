#include <iostream>
#include <cstdlib>
#include <string>

int main() {
    const char* key = "GEMINI_API_KEY";
    char* val = getenv(key);
    std::cout << "Checking env var '" << key << "'..." << std::endl;
    if (val) {
        std::cout << "FOUND: " << val << std::endl;
    } else {
        std::cout << "NOT FOUND" << std::endl;
    }
    return 0;
}
