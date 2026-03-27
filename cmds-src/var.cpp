// Compile: g++ -std=c++17 -static -o var.exe var.cpp
// Run: var <command> [args...]

#include "../shell_streams.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <iostream>
#include <windows.h>

namespace fs = std::filesystem;

std::string getVarFilePath() {
    const char* appdata = std::getenv("APPDATA");
    fs::path linuxdbDir;
    if (appdata) {
        linuxdbDir = fs::path(appdata) / "Linuxify" / "linuxdb";
    } else {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        linuxdbDir = fs::path(exePath).parent_path().parent_path() / "linuxdb";
    }
    
    if (!fs::exists(linuxdbDir)) {
        fs::create_directories(linuxdbDir);
    }
    
    return (linuxdbDir / "var.lin").string();
}

struct VarStore {
    std::map<std::string, std::string> scalars;
    std::map<std::string, std::vector<std::string>> arrays;
    
    std::map<std::string, std::map<std::string, std::string>> objects;
    std::map<std::string, std::map<std::string, std::vector<std::string>>> objArrays;
    
    void load(const std::string& path) {
        std::ifstream file(path);
        if (!file) return;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            
            if (line.substr(0, 4) == "OBJ:") {
                std::string rest = line.substr(4);
                size_t braceStart = rest.find('{');
                if (braceStart == std::string::npos) continue;
                std::string objName = rest.substr(0, braceStart);
                objName.erase(0, objName.find_first_not_of(" \t"));
                objName.erase(objName.find_last_not_of(" \t") + 1);
                if (objName.empty()) continue;
                std::string body = rest.substr(braceStart + 1);
                if (!body.empty() && body.back() == '}') body.pop_back();
                std::map<std::string, std::string> members;
                std::map<std::string, std::vector<std::string>> arrMembers;
                size_t p = 0;
                while (p < body.size()) {
                    size_t colonPos = body.find(':', p);
                    if (colonPos == std::string::npos) break;
                    std::string key = body.substr(p, colonPos - p);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    p = colonPos + 1;
                    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) p++;
                    if (p < body.size() && body[p] == '[') {
                        size_t closeBrk = body.find(']', p);
                        if (closeBrk == std::string::npos) break;
                        std::string arrInner = body.substr(p + 1, closeBrk - p - 1);
                        std::vector<std::string> arrVals;
                        std::stringstream ass(arrInner);
                        std::string aitem;
                        while (std::getline(ass, aitem, ',')) {
                            aitem.erase(0, aitem.find_first_not_of(" \t\""));
                            aitem.erase(aitem.find_last_not_of(" \t\"") + 1);
                            if (!aitem.empty()) arrVals.push_back(aitem);
                        }
                        arrMembers[key] = arrVals;
                        p = closeBrk + 1;
                        if (p < body.size() && body[p] == ',') p++;
                    } else {
                        bool inQuote = false;
                        char qChar = '\0';
                        size_t valStart = p;
                        while (p < body.size()) {
                            if (!inQuote && (body[p] == '"' || body[p] == '\'')) { inQuote = true; qChar = body[p]; p++; continue; }
                            if (inQuote && body[p] == qChar) { inQuote = false; p++; continue; }
                            if (!inQuote && body[p] == ',') break;
                            p++;
                        }
                        std::string val = body.substr(valStart, p - valStart);
                        val.erase(0, val.find_first_not_of(" \t\"\'")); 
                        val.erase(val.find_last_not_of(" \t\"\'" ) + 1);
                        members[key] = val;
                        if (p < body.size() && body[p] == ',') p++;
                    }
                }
                objects[objName] = members;
                if (!arrMembers.empty()) objArrays[objName] = arrMembers;
                continue;
            }
            
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
            ShellIO::serr << "var: error: cannot write to " << path << ShellIO::endl;
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
        
        for (const auto& pair : objects) {
            file << "OBJ:" << pair.first << "{";
            bool first = true;
            for (const auto& mem : pair.second) {
                if (!first) file << ",";
                first = false;
                file << mem.first << ":\"" << mem.second << "\"";
            }
            auto arrIt = objArrays.find(pair.first);
            if (arrIt != objArrays.end()) {
                for (const auto& arrMem : arrIt->second) {
                    if (!first) file << ",";
                    first = false;
                    file << arrMem.first << ":[";
                    for (size_t i = 0; i < arrMem.second.size(); ++i) {
                        if (i > 0) file << ",";
                        file << "\"" << arrMem.second[i] << "\"";
                    }
                    file << "]";
                }
            }
            file << "}\n";
        }
    }
};

void printUsage() {
    ShellIO::sout << "Usage: var <command> [args...]\n" << ShellIO::endl;
    ShellIO::sout << "Commands:\n" << ShellIO::endl;
    ShellIO::sout << "  var list                         List all variables\n" << ShellIO::endl;
    ShellIO::sout << "  var mod <name> <value>           Modify scalar variable\n" << ShellIO::endl;
    ShellIO::sout << "  var mod <name[N]> <value>        Modify array element at index N\n" << ShellIO::endl;
    ShellIO::sout << "  var insert <arrayname> <value>   Append value to array\n" << ShellIO::endl;
    ShellIO::sout << "  var purge <arrayname> <N>        Delete element at index N from array\n" << ShellIO::endl;
    ShellIO::sout << "  var del <name>                   Delete variable or entire array\n" << ShellIO::endl;
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
        ShellIO::sout << "Scalar Variables:\n" << ShellIO::endl;
        if (store.scalars.empty()) {
            ShellIO::sout << "  (none)\n" << ShellIO::endl;
        } else {
            for (const auto& pair : store.scalars) {
                ShellIO::sout << "  " << pair.first << "=" << pair.second << ShellIO::endl;
            }
        }
        
        ShellIO::sout << "\nArray Variables:\n" << ShellIO::endl;
        if (store.arrays.empty()) {
            ShellIO::sout << "  (none)\n" << ShellIO::endl;
        } else {
            for (const auto& pair : store.arrays) {
                ShellIO::sout << "  " << pair.first << "[]={";
                for (size_t i = 0; i < pair.second.size(); ++i) {
                    if (i > 0) ShellIO::sout << ",";
                    ShellIO::sout << pair.second[i];
                }
                ShellIO::sout << "} (" << pair.second.size() << " elements)\n" << ShellIO::endl;
            }
        }
        
        ShellIO::sout << "\nEnvironmental Objects:\n" << ShellIO::endl;
        if (store.objects.empty() && store.objArrays.empty()) {
            ShellIO::sout << "  (none)\n" << ShellIO::endl;
        } else {
            for (const auto& pair : store.objects) {
                ShellIO::sout << "  " << pair.first << "{";
                bool first = true;
                for (const auto& mem : pair.second) {
                    if (!first) ShellIO::sout << ",";
                    first = false;
                    ShellIO::sout << mem.first << ":" << mem.second;
                }
                auto arrIt = store.objArrays.find(pair.first);
                if (arrIt != store.objArrays.end()) {
                    for (const auto& amem : arrIt->second) {
                        if (!first) ShellIO::sout << ",";
                        first = false;
                        ShellIO::sout << amem.first << ":[";
                        for (size_t i = 0; i < amem.second.size(); ++i) {
                            if (i > 0) ShellIO::sout << ",";
                            ShellIO::sout << amem.second[i];
                        }
                        ShellIO::sout << "]";
                    }
                }
                ShellIO::sout << "}\n" << ShellIO::endl;
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
                ShellIO::serr << "var: mod: array '" << arrName << "' does not exist\n" << ShellIO::endl;
                return 1;
            }
            
            try {
                size_t idx = std::stoul(idxStr);
                if (idx >= it->second.size()) {
                    ShellIO::serr << "var: mod: index " << idx << " out of bounds (array has " << it->second.size() << " elements)\n" << ShellIO::endl;
                    return 1;
                }
                it->second[idx] = newValue;
                store.save(varPath);
                ShellIO::sout << "Modified: " << arrName << "[" << idx << "]=" << newValue << ShellIO::endl;
            } catch (...) {
                ShellIO::serr << "var: mod: invalid index '" << idxStr << "'\n" << ShellIO::endl;
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
                ShellIO::serr << "var: mod: variable '" << target << "' does not exist\n" << ShellIO::endl;
                return 1;
            }
            
            store.scalars[target] = newValue;
            store.save(varPath);
            ShellIO::sout << "Modified: " << target << "=" << newValue << ShellIO::endl;
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
            ShellIO::serr << "var: insert: array '" << arrName << "' does not exist\n" << ShellIO::endl;
            ShellIO::serr << "Hint: Create it first with: export -p -arr " << arrName << "={}\n" << ShellIO::endl;
            return 1;
        }
        
        it->second.push_back(value);
        store.save(varPath);
        ShellIO::sout << "Inserted: " << arrName << "[" << (it->second.size() - 1) << "]=" << value << ShellIO::endl;
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
            ShellIO::serr << "var: purge: array '" << arrName << "' does not exist\n" << ShellIO::endl;
            return 1;
        }
        
        try {
            size_t idx = std::stoul(idxStr);
            if (idx >= it->second.size()) {
                ShellIO::serr << "var: purge: index " << idx << " out of bounds (array has " << it->second.size() << " elements)\n" << ShellIO::endl;
                return 1;
            }
            std::string removed = it->second[idx];
            it->second.erase(it->second.begin() + idx);
            store.save(varPath);
            ShellIO::sout << "Purged: " << arrName << "[" << idx << "] (was '" << removed << "')\n" << ShellIO::endl;
        } catch (...) {
            ShellIO::serr << "var: purge: invalid index '" << idxStr << "'\n" << ShellIO::endl;
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
            ShellIO::sout << "Deleted variable: " << name << ShellIO::endl;
            return 0;
        }
        
        auto arrIt = store.arrays.find(name);
        if (arrIt != store.arrays.end()) {
            store.arrays.erase(arrIt);
            store.save(varPath);
            ShellIO::sout << "Deleted array: " << name << ShellIO::endl;
            return 0;
        }
        
        auto objIt = store.objects.find(name);
        if (objIt != store.objects.end()) {
            store.objects.erase(objIt);
            auto oaIt = store.objArrays.find(name);
            if (oaIt != store.objArrays.end()) store.objArrays.erase(oaIt);
            store.save(varPath);
            ShellIO::sout << "Deleted object: " << name << ShellIO::endl;
            return 0;
        }
        
        ShellIO::serr << "var: del: '" << name << "' does not exist\n" << ShellIO::endl;
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
