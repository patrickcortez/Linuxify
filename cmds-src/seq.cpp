// g++ -O3 -o ../cmds/seq.exe seq.cpp
#include <iostream>
#include <string>

void printError(const std::string& msg) {
    std::cerr << "Linuxify: " << msg << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printError("seq: missing operand");
        return 1;
    }
    
    if (std::string(argv[1]) == "--help") {
         std::cout << "Usage: seq [FIRST [INCREMENT]] LAST\n";
         return 0;
    }

    double first = 1.0;
    double inc = 1.0;
    double last = 1.0;

    try {
        if (argc == 2) {
            last = std::stod(argv[1]);
        } else if (argc == 3) {
            first = std::stod(argv[1]);
            last = std::stod(argv[2]);
        } else if (argc >= 4) {
            first = std::stod(argv[1]);
            inc = std::stod(argv[2]);
            last = std::stod(argv[3]);
        }
    } catch (...) {
        printError("seq: invalid floating point argument");
        return 1;
    }

    if (inc == 0.0) {
        printError("seq: zero increment");
        return 1;
    }

    if (inc > 0) {
        for (double i = first; i <= last; i += inc) {
            std::cout << i << "\n";
        }
    } else {
         for (double i = first; i >= last; i += inc) {
            std::cout << i << "\n";
        }
    }

    return 0;
}
