// main.cpp
// g++ -static main.cpp -o lgs.exe

#include <iostream>
#include <string>
#include "grid.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: grid <command> [args...]\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "create") {
        if (argc < 3) {
            std::cerr << "Usage: grid create <grid-name>\n";
            return 1;
        }
        createGrid(argv[2]);
    } else if (command == "use") {
        if (argc < 3) {
            std::cerr << "Usage: grid use <grid-name>\n";
            return 1;
        }
        useGrid(argv[2]);
    } else if (command == "add") {
        if (argc < 3) {
            std::cerr << "Usage: grid add <column-name>:<data-type>\n";
            return 1;
        }
        addColumn(argv[2]);
    } else if (command == "insert") {
        insertRow(argc, argv, 2);
    } else if (command == "select") {
        std::string condition = "";
        if (argc > 2) {
            condition = argv[2];
        }
        selectRows(condition);
    } else if (command == "edit") {
        if (argc < 5) {
            std::cerr << "Usage: grid edit <row-index> <column-index> <data>\n";
            return 1;
        }
        editData(std::stoi(argv[2]), std::stoi(argv[3]), argv[4]);
    } else if (command == "delete-row") {
        if (argc < 3) {
            std::cerr << "Usage: grid delete-row <row-index>\n";
            return 1;
        }
        deleteRow(std::stoi(argv[2]));
    } else if (command == "delete-column") {
        if (argc < 3) {
            std::cerr << "Usage: grid delete-column <column-index>\n";
            return 1;
        }
        deleteColumn(std::stoi(argv[2]));
    } else if (command == "delete") {
        if (argc < 4) {
            std::cerr << "Usage: grid delete <row-index> <column-index>\n";
            return 1;
        }
        deleteData(std::stoi(argv[2]), std::stoi(argv[3]));
    } else if (command == "list") {
        std::string flag = "";
        if (argc > 2) {
            flag = argv[2];
        }
        listGrid(flag);
    } else if (command == "import") {
        importGrid();
    } else if (command == "help"){
        printHelp();
        return 1;
    } else if (command == "which"){
        whichGrid();
        return 1;
    } else {
        std::cerr << "LGS: Command not found!\n";
        return 1;
    }

    return 0;
}