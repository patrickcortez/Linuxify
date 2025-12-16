// Compile: g++ -std=c++17 -static -o var.exe var.cpp
// Run: var <command> [args...]

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <windows.h>

namespace fs = std::filesystem;

std::string getVarFilePath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path().parent_path();
    fs::path varPath = exeDir / "linuxdb" / "var.lin";
    
    if (!fs::exists(varPath.parent_path())) {
        fs::create_directories(varPath.parent_path());
    }
    
    return varPath.string();
}

struct VarStore {
    std::map<std::string, std::string> scalars;
    std::map<std::string, std::vector<std::string>> arrays;
    
    void load(const std::string& path) {
        std::ifstream file(path);
        if (!file) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;
            
            std::string name = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            
            if (name.size() >= 2 && name.substr(name.size() - 2) == "[]") {
                std::string arrName = name.substr(0, name.size() - 2);
                std::vector<std::string> arr;
                
                if (value.size() >= 2 && value[0] == '{' && value[value.size() - 1] == '}') {
                    std::string inner = value.substr(1, value.size() - 2);
                    std::stringstream ss(inner);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item.erase(0, item.find_first_not_of(" \t"));
                        item.erase(item.find_last_not_of(" \t") + 1);
                        arr.push_back(item);
                    }
                }
                arrays[arrName] = arr;
            } else {
                scalars[name] = value;
            }
        }
    }
    
    void save(const std::string& path) {
        std::ofstream file(path);
        if (!file) {
            std::cerr << "var: error: cannot write to " << path << std::endl;
            return;
        }
        
        file << "# Linuxify Persistent Variables\n";
        file << "# Format: VAR=value or ARR[]={val1,val2,val3}\n\n";
        
        for (const auto& pair : scalars) {
            file << pair.first << "=" << pair.second << "\n";
        }
        
        for (const auto& pair : arrays) {
            file << pair.first << "[]={";
            for (size_t i = 0; i < pair.second.size(); ++i) {
                if (i > 0) file << ",";
                file << pair.second[i];
            }
            file << "}\n";
        }
    }
};

void printUsage() {
    std::cout << "Usage: var <command> [args...]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  var list                         List all variables\n";
    std::cout << "  var mod <name> <value>           Modify scalar variable\n";
    std::cout << "  var mod <name[N]> <value>        Modify array element at index N\n";
    std::cout << "  var insert <arrayname> <value>   Append value to array\n";
    std::cout << "  var purge <arrayname> <N>        Delete element at index N from array\n";
    std::cout << "  var del <name>                   Delete variable or entire array\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    
    std::string varPath = getVarFilePath();
    VarStore store;
    store.load(varPath);
    
    std::string cmd = argv[1];
    
    if (cmd == "list") {
        std::cout << "Scalar Variables:\n";
        if (store.scalars.empty()) {
            std::cout << "  (none)\n";
        } else {
            for (const auto& pair : store.scalars) {
                std::cout << "  " << pair.first << "=" << pair.second << "\n";
            }
        }
        
        std::cout << "\nArray Variables:\n";
        if (store.arrays.empty()) {
            std::cout << "  (none)\n";
        } else {
            for (const auto& pair : store.arrays) {
                std::cout << "  " << pair.first << "[]={";
                for (size_t i = 0; i < pair.second.size(); ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << pair.second[i];
                }
                std::cout << "} (" << pair.second.size() << " elements)\n";
            }
        }
        return 0;
        
    } else if (cmd == "mod") {
        if (argc < 4) {
            std::cerr << "var: mod: missing arguments\n";
            std::cerr << "Usage: var mod <name> <value> OR var mod <name[N]> <value>\n";
            return 1;
        }
        
        std::string target = argv[2];
        std::string newValue = argv[3];
        
        size_t bracket = target.find('[');
        if (bracket != std::string::npos && target.back() == ']') {
            std::string arrName = target.substr(0, bracket);
            std::string idxStr = target.substr(bracket + 1, target.size() - bracket - 2);
            
            auto it = store.arrays.find(arrName);
            if (it == store.arrays.end()) {
                std::cerr << "var: mod: array '" << arrName << "' does not exist\n";
                return 1;
            }
            
            try {
                size_t idx = std::stoul(idxStr);
                if (idx >= it->second.size()) {
                    std::cerr << "var: mod: index " << idx << " out of bounds (array has " << it->second.size() << " elements)\n";
                    return 1;
                }
                it->second[idx] = newValue;
                store.save(varPath);
                std::cout << "Modified: " << arrName << "[" << idx << "]=" << newValue << "\n";
            } catch (...) {
                std::cerr << "var: mod: invalid index '" << idxStr << "'\n";
                return 1;
            }
        } else {
            auto arrIt = store.arrays.find(target);
            if (arrIt != store.arrays.end()) {
                std::cerr << "var: mod: '" << target << "' is an array. Use var mod " << target << "[N] <value> to modify an element\n";
                return 1;
            }
            
            auto scalarIt = store.scalars.find(target);
            if (scalarIt == store.scalars.end()) {
                std::cerr << "var: mod: variable '" << target << "' does not exist\n";
                return 1;
            }
            
            store.scalars[target] = newValue;
            store.save(varPath);
            std::cout << "Modified: " << target << "=" << newValue << "\n";
        }
        return 0;
        
    } else if (cmd == "insert") {
        if (argc < 4) {
            std::cerr << "var: insert: missing arguments\n";
            std::cerr << "Usage: var insert <arrayname> <value>\n";
            return 1;
        }
        
        std::string arrName = argv[2];
        std::string value = argv[3];
        
        auto it = store.arrays.find(arrName);
        if (it == store.arrays.end()) {
            std::cerr << "var: insert: array '" << arrName << "' does not exist\n";
            std::cerr << "Hint: Create it first with: export -p -arr " << arrName << "={}\n";
            return 1;
        }
        
        it->second.push_back(value);
        store.save(varPath);
        std::cout << "Inserted: " << arrName << "[" << (it->second.size() - 1) << "]=" << value << "\n";
        return 0;
        
    } else if (cmd == "purge") {
        if (argc < 4) {
            std::cerr << "var: purge: missing arguments\n";
            std::cerr << "Usage: var purge <arrayname> <N>\n";
            return 1;
        }
        
        std::string arrName = argv[2];
        std::string idxStr = argv[3];
        
        auto it = store.arrays.find(arrName);
        if (it == store.arrays.end()) {
            std::cerr << "var: purge: array '" << arrName << "' does not exist\n";
            return 1;
        }
        
        try {
            size_t idx = std::stoul(idxStr);
            if (idx >= it->second.size()) {
                std::cerr << "var: purge: index " << idx << " out of bounds (array has " << it->second.size() << " elements)\n";
                return 1;
            }
            std::string removed = it->second[idx];
            it->second.erase(it->second.begin() + idx);
            store.save(varPath);
            std::cout << "Purged: " << arrName << "[" << idx << "] (was '" << removed << "')\n";
        } catch (...) {
            std::cerr << "var: purge: invalid index '" << idxStr << "'\n";
            return 1;
        }
        return 0;
        
    } else if (cmd == "del") {
        if (argc < 3) {
            std::cerr << "var: del: missing variable name\n";
            std::cerr << "Usage: var del <name>\n";
            return 1;
        }
        
        std::string name = argv[2];
        
        auto scalarIt = store.scalars.find(name);
        if (scalarIt != store.scalars.end()) {
            store.scalars.erase(scalarIt);
            store.save(varPath);
            std::cout << "Deleted variable: " << name << "\n";
            return 0;
        }
        
        auto arrIt = store.arrays.find(name);
        if (arrIt != store.arrays.end()) {
            store.arrays.erase(arrIt);
            store.save(varPath);
            std::cout << "Deleted array: " << name << "\n";
            return 0;
        }
        
        std::cerr << "var: del: '" << name << "' does not exist\n";
        return 1;
        
    } else if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
        
    } else {
        std::cerr << "var: unknown command '" << cmd << "'\n";
        printUsage();
        return 1;
    }
    
    return 0;
}
