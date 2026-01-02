// Linuxify Shell Client Demo - Using linuxify.hpp library
// Compile: g++ -std=c++17 -I.. -o shell_demo.exe shell_demo.cpp
// Run: Start linuxify.exe first, then run this program

#include <iostream>
#include "../linuxify.hpp"

int main() {
    std::cout << "=== Linuxify Shell Client Demo ===" << std::endl;
    std::cout << std::endl;
    
    if (!Linuxify::isRunning()) {
        std::cout << "Error: Linuxify shell is not running!" << std::endl;
        std::cout << "Please start linuxify.exe first." << std::endl;
        return 1;
    }
    
    std::cout << "[1] Using linuxify() function:" << std::endl;
    auto result = linuxify("pwd");
    std::cout << "   pwd: " << result.output << std::endl;
    
    std::cout << std::endl << "[2] Using Linuxify::exec():" << std::endl;
    std::cout << "   ls: " << Linuxify::exec("ls").output << std::endl;
    
    std::cout << std::endl << "[3] Using Shell class with operator():" << std::endl;
    Linuxify::Shell shell;
    std::cout << "   echo: " << shell("echo Hello World!") << std::endl;
    
    std::cout << std::endl << "[4] Using convenience functions:" << std::endl;
    std::cout << "   Linuxify::pwd(): " << Linuxify::pwd() << std::endl;
    
    std::cout << std::endl << "[5] Using string literal syntax:" << std::endl;
    using namespace Linuxify;
    std::cout << "   \"whoami\"_sh: " << "whoami"_sh << std::endl;
    
    std::cout << std::endl << "[6] Arithmetic through shell:" << std::endl;
    std::cout << "   2+2*10 = " << linuxify("2+2*10").output << std::endl;
    
    std::cout << std::endl << "[7] Checking result status:" << std::endl;
    auto res = linuxify("pwd");
    if (res.success()) {
        std::cout << "   Command succeeded with exit code: " << res.exitCode << std::endl;
    }
    
    std::cout << std::endl << "=== Demo Complete ===" << std::endl;
    return 0;
}
