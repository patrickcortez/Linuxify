#ifndef LINUXIFY_FUZZY_HPP
#define LINUXIFY_FUZZY_HPP

#include <string>
#include <vector>
#include <random>
#include <functional>
#include <iostream>
#include <algorithm>

namespace Linuxify {

    class ShellFuzzer {
    private:
        std::mt19937 rng;
        
        // "Safe" commands that perform read-only or harmless actions
        const std::vector<std::string> safeCmds = {
            "echo", "pwd", "cd .", "ls", "whoami", "date", "true", "false", 
            "help", "history", "version", "man"
        };

        // Garbage characters to test parser robustness
        const std::string garbageChars = "!@#$%^&*()_+{}|:<>?`~-=[];',./\"\\";

    public:
        ShellFuzzer() {
            std::random_device rd;
            rng.seed(rd());
        }

        std::string generateRandomString(size_t length) {
            std::string s;
            s.reserve(length);
            std::uniform_int_distribution<int> dist(0, 255); // Full ASCII range
            for (size_t i = 0; i < length; i++) {
                char c = (char)dist(rng);
                // Filter out nulls/newlines to simulate somewhat "typeable" garbage
                if (c == 0 || c == '\n' || c == '\r') c = ' '; 
                s += c;
            }
            return s;
        }

        std::string generateStructure() {
            // Generate valid-looking but garbage parsing structures
            std::uniform_int_distribution<int> dist(0, 5);
            int type = dist(rng);
            
            std::string cmd = safeCmds[rng() % safeCmds.size()];
            
            switch (type) {
                case 0: return cmd + " " + generateRandomString(10); // Cmd + Args
                case 1: return cmd + " \"unclosed quote " + generateRandomString(5); // Unclosed quote
                case 2: return cmd + " | " + safeCmds[rng() % safeCmds.size()]; // Pipe
                case 3: return cmd + " && " + generateRandomString(5); // Operator + Garbage
                case 4: return std::string(100, 'A'); // Buffer overflow attempt
                case 5: return garbageChars; // Pure garbage
                default: return "echo test";
            }
        }

        void run(int iterations, std::function<void(std::string)> executor) {
            std::cout << "\n[FUZZ] Starting Stress Test (" << iterations << " iterations)...\n";
            
            int survived = 0;
            for (int i = 0; i < iterations; i++) {
                std::string input = generateStructure();
                
                // std::cout << "\r[FUZZ] Testing: " << input.substr(0, 40) << "..."; 
                
                try {
                    executor(input);
                    survived++;
                } catch (const std::exception& e) {
                    std::cerr << "\n[FUZZ] CRASH CAUGHT: " << e.what() << "\nInput: " << input << "\n";
                } catch (...) {
                    std::cerr << "\n[FUZZ] UNKNOWN CRASH CAUGHT!\nInput: " << input << "\n";
                }
            }
            
            std::cout << "\n[FUZZ] Completed.\n";
            std::cout << "Survived: " << survived << "/" << iterations << "\n";
        }
    };

}
#endif
