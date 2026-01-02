/*
 * Compile: c:\Users\patri\OneDrive\Documents\Projects\Linuxify\toolchain\compiler\mingw64\bin\g++.exe test/comment_test.cpp -o test/comment_test.exe
 * Run: ./test/comment_test.exe
 */

#include <iostream>

// This function prints a greeting
void greet() {
    std::cout << "Hello, World!" << std::endl; // Output message
}

int main() {
    /* 
     * Main entry point of the program.
     * Calling the greet function.
     */
    greet();
    return 0;
}
