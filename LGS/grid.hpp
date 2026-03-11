// grid.hpp
#pragma once
#include <string>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
#include <sstream>

namespace fs = std::filesystem;

inline std::string getGridsDirectory() {
    return "grids";
}

inline void ensureGridsDirectory() {
    if (!fs::exists(getGridsDirectory())) {
        fs::create_directories(getGridsDirectory());
    }
}

inline void createGrid(const std::string& name) {
    ensureGridsDirectory();
    std::string schemaPath = getGridsDirectory() + "/" + name + ".schema";
    std::string dataPath = getGridsDirectory() + "/" + name + ".data";
    
    if (fs::exists(schemaPath) || fs::exists(dataPath)) {
        std::cerr << "Grid " << name << " already exists\n";
        return;
    }
    
    std::ofstream schemaFile(schemaPath);
    if (!schemaFile) {
        std::cerr << "Failed to create " << schemaPath << "\n";
        return;
    }
    schemaFile.close();
    
    std::ofstream dataFile(dataPath, std::ios::binary);
    if (!dataFile) {
        std::cerr << "Failed to create " << dataPath << "\n";
        return;
    }
    dataFile.close();
}

inline void useGrid(const std::string& name) {
    ensureGridsDirectory();
    std::string currentFile = getGridsDirectory() + "/.current";
    std::string schemaPath = getGridsDirectory() + "/" + name + ".schema";
    
    if (!fs::exists(schemaPath)) {
        std::cerr << "Grid " << name << " does not exist\n";
        return;
    }
    
    std::ofstream out(currentFile);
    if (!out) {
        std::cerr << "Failed to set current grid\n";
        return;
    }
    out << name;
    out.close();
}

inline std::string getCurrentGrid() {
    std::string currentFile = getGridsDirectory() + "/.current";
    if (!fs::exists(currentFile)) {
        return "";
    }
    
    std::ifstream in(currentFile);
    std::string name;
    if (in) {
        in >> name;
    }
    return name;
}

inline std::vector<std::pair<std::string, std::string>> getSchema(const std::string& name) {
    std::vector<std::pair<std::string, std::string>> schema;
    std::ifstream schemaFile(getGridsDirectory() + "/" + name + ".schema");
    std::string line;
    while (std::getline(schemaFile, line)) {
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            schema.push_back({line.substr(0, colon), line.substr(colon + 1)});
        }
    }
    return schema;
}

inline void addColumn(const std::string& columnDef) {
    std::string name = getCurrentGrid();
    if (name.empty()) {
        std::cerr << "No grid currently selected\n";
        return;
    }
    
    std::string schemaPath = getGridsDirectory() + "/" + name + ".schema";
    std::ofstream out(schemaPath, std::ios::app);
    if (!out) {
        std::cerr << "Failed to open schema file\n";
        return;
    }
    out << columnDef << "\n";
    out.close();
}

inline void writeRow(std::ostream& dataFile, const std::vector<std::pair<std::string, std::string>>& schema, const std::vector<std::string>& rowData) {
    for (size_t c = 0; c < schema.size(); ++c) {
        std::string valStr = (c < rowData.size()) ? rowData[c] : "";
        std::string type = schema[c].second;

        if (type == "String") {
            uint32_t len = valStr.length();
            dataFile.write(reinterpret_cast<const char*>(&len), sizeof(len));
            if (len > 0) dataFile.write(valStr.c_str(), len);
        } else if (type == "Integer") {
            int32_t val = 0;
            try { val = std::stoi(valStr); } catch (...) {}
            dataFile.write(reinterpret_cast<const char*>(&val), sizeof(val));
        } else if (type == "Float") {
            float val = 0.0f;
            try { val = std::stof(valStr); } catch (...) {}
            dataFile.write(reinterpret_cast<const char*>(&val), sizeof(val));
        } else if (type == "Boolean") {
            uint8_t val = (valStr == "true" || valStr == "1" || valStr == "True") ? 1 : 0;
            dataFile.write(reinterpret_cast<const char*>(&val), sizeof(val));
        }
    }
}

inline void readData(const std::string& name, const std::vector<std::pair<std::string, std::string>>& schema, std::vector<std::vector<std::string>>& rows, std::vector<uint8_t>& actives) {
    std::ifstream dataFile(getGridsDirectory() + "/" + name + ".data", std::ios::binary);
    if (!dataFile) return;

    while (dataFile.peek() != EOF) {
        uint8_t active;
        if (!dataFile.read(reinterpret_cast<char*>(&active), sizeof(active))) break;

        std::vector<std::string> rowStrs;
        for (const auto& col : schema) {
            std::string type = col.second;
            std::string strVal;

            if (type == "String") {
                uint32_t len = 0;
                if (!dataFile.read(reinterpret_cast<char*>(&len), sizeof(len))) break;
                if (len > 0 && len <= 1000000) {
                    std::string s(len, '\0');
                    dataFile.read(&s[0], len);
                    strVal = s;
                }
            } else if (type == "Integer") {
                int32_t val = 0;
                if (!dataFile.read(reinterpret_cast<char*>(&val), sizeof(val))) break;
                strVal = std::to_string(val);
            } else if (type == "Float") {
                float val = 0.0f;
                if (!dataFile.read(reinterpret_cast<char*>(&val), sizeof(val))) break;
                strVal = std::to_string(val);
            } else if (type == "Boolean") {
                uint8_t val = 0;
                if (!dataFile.read(reinterpret_cast<char*>(&val), sizeof(val))) break;
                strVal = val ? "true" : "false";
            }
            rowStrs.push_back(strVal);
        }
        
        actives.push_back(active);
        rows.push_back(rowStrs);
    }
}

inline void writeData(const std::string& name, const std::vector<std::pair<std::string, std::string>>& schema, const std::vector<std::vector<std::string>>& rows, const std::vector<uint8_t>& actives) {
    std::ofstream dataFile(getGridsDirectory() + "/" + name + ".data", std::ios::binary | std::ios::trunc);
    if (!dataFile) return;

    for (size_t r = 0; r < rows.size(); ++r) {
        uint8_t active = actives[r];
        dataFile.write(reinterpret_cast<const char*>(&active), sizeof(active));
        writeRow(dataFile, schema, rows[r]);
    }
}

inline void insertRow(int argc, char** argv, int startIdx) {
    std::string name = getCurrentGrid();
    if (name.empty()) {
        std::cerr << "No grid currently selected\n";
        return;
    }
    
    auto schema = getSchema(name);
    if (argc - startIdx != (int)schema.size()) {
        std::cerr << "Argument count mismatch with schema\n";
        return;
    }
    
    std::vector<std::string> rowData;
    for (size_t i = 0; i < schema.size(); ++i) {
        rowData.push_back(argv[startIdx + i]);
    }
    
    std::ofstream dataFile(getGridsDirectory() + "/" + name + ".data", std::ios::app | std::ios::binary);
    if (!dataFile) {
        std::cerr << "Failed to open data file\n";
        return;
    }
    
    uint8_t active = 1;
    dataFile.write(reinterpret_cast<const char*>(&active), sizeof(active));
    writeRow(dataFile, schema, rowData);
    dataFile.close();
}

inline void selectRows(const std::string& condition = "") {
    std::string name = getCurrentGrid();
    if (name.empty()) {
        std::cerr << "No grid currently selected\n";
        return;
    }
    
    auto schema = getSchema(name);
    if (schema.empty()) return;
    
    std::string condCol, condVal;
    if (!condition.empty()) {
        size_t eq = condition.find('=');
        if (eq != std::string::npos) {
            condCol = condition.substr(0, eq);
            condVal = condition.substr(eq + 1);
        }
    }
    
    std::vector<std::vector<std::string>> rows;
    std::vector<uint8_t> actives;
    readData(name, schema, rows, actives);
    
    std::cout << "INDEX\t";
    for (const auto& col : schema) {
        std::cout << col.first << "\t";
    }
    std::cout << "\n";
    
    for (size_t r = 0; r < rows.size(); ++r) {
        if (!actives[r]) continue;
        
        bool match = condition.empty();
        if (!condition.empty()) {
            for (size_t c = 0; c < schema.size(); ++c) {
                if (schema[c].first == condCol && rows[r][c] == condVal) {
                    match = true;
                    break;
                }
            }
        }
        
        if (match) {
            std::cout << "[" << r << "]\t";
            for (const auto& s : rows[r]) {
                std::cout << s << "\t";
            }
            std::cout << "\n";
        }
    }
}

inline void editData(int rowIdx, int colIdx, const std::string& data) {
    std::string name = getCurrentGrid();
    if (name.empty()) return;
    auto schema = getSchema(name);
    std::vector<std::vector<std::string>> rows;
    std::vector<uint8_t> actives;
    readData(name, schema, rows, actives);

    if (rowIdx >= 0 && rowIdx < (int)rows.size() && colIdx >= 0 && colIdx < (int)schema.size()) {
        rows[rowIdx][colIdx] = data;
        writeData(name, schema, rows, actives);
    } else {
        std::cerr << "Invalid row or column index\n";
    }
}

inline void deleteRow(int rowIdx) {
    std::string name = getCurrentGrid();
    if (name.empty()) return;
    auto schema = getSchema(name);
    std::vector<std::vector<std::string>> rows;
    std::vector<uint8_t> actives;
    readData(name, schema, rows, actives);

    if (rowIdx >= 0 && rowIdx < (int)actives.size()) {
        actives[rowIdx] = 0; 
        writeData(name, schema, rows, actives);
    } else {
        std::cerr << "Invalid row index\n";
    }
}

inline void deleteColumn(int colIdx) {
    std::string name = getCurrentGrid();
    if (name.empty()) return;
    auto schema = getSchema(name);
    std::vector<std::vector<std::string>> rows;
    std::vector<uint8_t> actives;
    readData(name, schema, rows, actives);

    if (colIdx >= 0 && colIdx < (int)schema.size()) {
        schema.erase(schema.begin() + colIdx);
        for (auto& row : rows) {
            if (colIdx < (int)row.size()) {
                row.erase(row.begin() + colIdx);
            }
        }
        
        std::ofstream schemaFile(getGridsDirectory() + "/" + name + ".schema", std::ios::trunc);
        for (const auto& col : schema) {
            schemaFile << col.first << ":" << col.second << "\n";
        }
        schemaFile.close();
        
        writeData(name, schema, rows, actives);
    } else {
        std::cerr << "Invalid column index\n";
    }
}

inline void deleteData(int rowIdx, int colIdx) {
    std::string name = getCurrentGrid();
    if (name.empty()) return;
    auto schema = getSchema(name);
    std::vector<std::vector<std::string>> rows;
    std::vector<uint8_t> actives;
    readData(name, schema, rows, actives);

    if (rowIdx >= 0 && rowIdx < (int)rows.size() && colIdx >= 0 && colIdx < (int)schema.size()) {
        std::string type = schema[colIdx].second;
        if (type == "String") rows[rowIdx][colIdx] = "";
        else if (type == "Integer" || type == "Float") rows[rowIdx][colIdx] = "0";
        else if (type == "Boolean") rows[rowIdx][colIdx] = "false";
        
        writeData(name, schema, rows, actives);
    } else {
        std::cerr << "Invalid row or column index\n";
    }
}

inline void listGrid(const std::string& flag) {
    if (flag.empty()) {
        ensureGridsDirectory();
        for (const auto& entry : fs::directory_iterator(getGridsDirectory())) {
            if (entry.path().extension() == ".schema") {
                std::cout << entry.path().stem().string() << "\n";
            }
        }
    } else {
        std::string name = getCurrentGrid();
        if (name.empty()) {
            std::cerr << "No grid currently selected\n";
            return;
        }
        auto schema = getSchema(name);

        if (flag == "-c") {
            for (size_t i = 0; i < schema.size(); ++i) {
                std::cout << "[" << i << "] " << schema[i].first << " (" << schema[i].second << ")\n";
            }
        } else if (flag == "-r") {
            selectRows("");
        } else if (flag == "-a") {
            std::vector<std::vector<std::string>> rows;
            std::vector<uint8_t> actives;
            readData(name, schema, rows, actives);
            for (size_t r = 0; r < rows.size(); ++r) {
                if (!actives[r]) continue;
                for (size_t c = 0; c < schema.size(); ++c) {
                    std::cout << "Row " << r << ", Col " << c << " (" << schema[c].first << "): " << rows[r][c] << "\n";
                }
            }
        } else {
            std::cerr << "Unknown list flag\n";
        }
    }
}

inline void importGrid() {
    std::string name = getCurrentGrid();
    if (name.empty()) {
        std::cerr << "No grid currently selected\n";
        return;
    }
    auto schema = getSchema(name);
    if (schema.empty()) return;

    std::ofstream dataFile(getGridsDirectory() + "/" + name + ".data", std::ios::app | std::ios::binary);
    if (!dataFile) return;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> rowData;
        while (ss >> token) {
            rowData.push_back(token);
        }
        
        if (rowData.size() == schema.size()) {
            uint8_t active = 1;
            dataFile.write(reinterpret_cast<const char*>(&active), sizeof(active));
            writeRow(dataFile, schema, rowData);
        } else {
            std::cerr << "Warning: skipping line due to column mismatch\n";
        }
    }
}

inline void printHelp() {
    std::cout << "Linuxify Grid System (LGS) - Command Reference\n";
    std::cout << "------------------------------------------------\n";
    std::cout << "grid create <grid-name>                     : create a new grid(binary file)\n";
    std::cout << "grid use <grid-name>                        : use a grid\n";
    std::cout << "grid add <column-name>:<data-type>          : add a column to the grid\n";
    std::cout << "grid insert <row-data>                      : insert data to the grid\n";
    std::cout << "grid select <condition>                     : select data from the grid\n";
    std::cout << "grid edit <row-index> <column-index> <data> : edit data in the grid\n";
    std::cout << "grid delete-row <row-index>                 : delete a row from the grid\n";
    std::cout << "grid delete-column <column-index>            : delete a column from the grid\n";
    std::cout << "grid delete <row-index> <column-index>      : delete a singular data from the grid\n";
    std::cout << "grid list                                   : list all grids\n";
    std::cout << "grid list -c                                : list all columns in the grid\n";
    std::cout << "grid list -r                                : list all rows in the grid\n";
    std::cout << "grid list -a                                : list all datas in grid row+column\n";
    std::cout << "grid import                                 : for piping other command output to grid\n";
    std::cout << "grid help                                   : show this help\n";
    std::cout << "grid which                                  : show the current grid\n";
}

inline void whichGrid() {
    std::string name = getCurrentGrid();
    if (name.empty()) {
        std::cerr << "No grid currently selected\n";
    } else {
        std::cout << name << "\n";
    }
}
