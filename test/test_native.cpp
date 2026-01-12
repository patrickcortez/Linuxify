#include <iostream>
#include <string>
#include <windows.h>

int main() {
    std::cout << "[Native] Checking Console Mode..." << std::endl;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(hIn, &mode)) {
        std::cout << "[Native] Stdin Mode: " << std::hex << mode << std::dec << std::endl;
    } else {
        std::cout << "[Native] Failed to get Stdin Mode: " << GetLastError() << std::endl;
    }

    std::cout << "[Native] Type something: ";
    std::string line;
    if (std::getline(std::cin, line)) {
        std::cout << "[Native] You typed: " << line << std::endl;
    } else {
        std::cout << "[Native] Failed to read line (EOF or Error)" << std::endl;
    }
    return 0;
}
