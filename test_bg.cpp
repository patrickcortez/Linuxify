// Simple test program for background process testing
// Counts forever until killed

#include <iostream>
#include <windows.h>

int main() {
    std::cout << "Background test started! (Ctrl+C or kill to stop)" << std::endl;
    
    int i = 1;
    while (true) {
        std::cout << "Count: " << i++ << std::endl;
        Sleep(1000);  // Sleep 1 second
    }
    
    return 0;
}
