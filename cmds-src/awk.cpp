// Compile: g++ -std=c++17 -static -o ../cmds/awk.exe awk.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>

namespace fs = std::filesystem;

void printError(const std::string& msg) {
    std::cerr << msg << std::endl;
}

std::string resolvePath(const std::string& path) {
    if (path.empty()) {
        return fs::current_path().string();
    }
    fs::path p(path);
    if (p.is_absolute()) {
        try {
            return fs::canonical(p).string();
        } catch (...) {
            return p.string();
        }
    }
    fs::path fullPath = fs::current_path() / path;
    try {
        return fs::canonical(fullPath).string();
    } catch (...) {
        return fullPath.string();
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for(int i = 0; i < argc; ++i) args.push_back(argv[i]);

    if (args.size() < 2) {
        printError("awk: missing program");
        return 1;
    }
    
    std::string fieldSepStr = " ";
    std::string program;
    std::vector<std::string> files;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-F" && i + 1 < args.size()) {
            fieldSepStr = args[++i];
        } else if (args[i].substr(0, 2) == "-F") {
            fieldSepStr = args[i].substr(2);
        } else if (program.empty() && (args[i][0] == '{' || args[i][0] == '\'')) {
            program = args[i];
        } else if (program.empty() && args[i][0] != '-') {
            program = args[i];
        } else if (args[i][0] != '-') {
            files.push_back(args[i]);
        }
    }

    std::map<std::string, std::string> vars;
    vars["FS"] = fieldSepStr;
    vars["OFS"] = " ";
    vars["ORS"] = "\n";
    vars["NR"] = "0";
    vars["NF"] = "0";
    vars["FILENAME"] = "";

    auto splitFields = [&](const std::string& line) -> std::vector<std::string> {
        std::vector<std::string> fields;
        fields.push_back(line); // $0 is full line
        std::string fs = vars["FS"];
        
        if (fs == " ") {
            std::string token;
            bool inToken = false;
            for (char c : line) {
                if (std::isspace(c)) {
                    if (inToken) {
                        fields.push_back(token);
                        token.clear();
                        inToken = false;
                    }
                } else {
                    token += c;
                    inToken = true;
                }
            }
            if (inToken) fields.push_back(token);
        } else if (fs.length() == 1) {
            std::istringstream iss(line);
            std::string token;
            while (std::getline(iss, token, fs[0])) {
                fields.push_back(token);
            }
        } else {
            size_t pos = 0, found;
            while ((found = line.find(fs, pos)) != std::string::npos) {
                fields.push_back(line.substr(pos, found - pos));
                pos = found + fs.length();
            }
            fields.push_back(line.substr(pos));
        }
        
        vars["NF"] = std::to_string(fields.size() - 1);
        return fields;
    };

    auto evalExpr = [&](const std::string& expr, const std::vector<std::string>& fields) -> std::string {
        std::string result = expr;
        
        for (int i = 9; i >= 0; --i) {
            std::string placeholder = "$" + std::to_string(i);
            size_t pos;
            while ((pos = result.find(placeholder)) != std::string::npos) {
                std::string val = (i < (int)fields.size()) ? fields[i] : "";
                result.replace(pos, placeholder.length(), val);
            }
        }
        
        for (const auto& kv : vars) {
            size_t pos;
            while ((pos = result.find(kv.first)) != std::string::npos) {
                bool validBoundary = true;
                if (pos > 0 && (std::isalnum(result[pos - 1]) || result[pos - 1] == '_')) validBoundary = false;
                size_t endPos = pos + kv.first.length();
                if (endPos < result.length() && (std::isalnum(result[endPos]) || result[endPos] == '_')) validBoundary = false;
                if (validBoundary) {
                    result.replace(pos, kv.first.length(), kv.second);
                } else {
                    break;
                }
            }
        }
        
        size_t lenPos;
        while ((lenPos = result.find("length(")) != std::string::npos) {
            size_t endParen = result.find(")", lenPos);
            if (endParen != std::string::npos) {
                std::string arg = result.substr(lenPos + 7, endParen - lenPos - 7);
                result.replace(lenPos, endParen - lenPos + 1, std::to_string(arg.length()));
            } else break;
        }
        
        while ((lenPos = result.find("toupper(")) != std::string::npos) {
            size_t endParen = result.find(")", lenPos);
            if (endParen != std::string::npos) {
                std::string arg = result.substr(lenPos + 8, endParen - lenPos - 8);
                std::transform(arg.begin(), arg.end(), arg.begin(), ::toupper);
                result.replace(lenPos, endParen - lenPos + 1, arg);
            } else break;
        }
        
        while ((lenPos = result.find("tolower(")) != std::string::npos) {
            size_t endParen = result.find(")", lenPos);
            if (endParen != std::string::npos) {
                std::string arg = result.substr(lenPos + 8, endParen - lenPos - 8);
                std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);
                result.replace(lenPos, endParen - lenPos + 1, arg);
            } else break;
        }
        
        return result;
    };

    auto parseAction = [&](const std::string& action, const std::vector<std::string>& fields) {
        std::string act = action;
        while (!act.empty() && (act.front() == '{' || act.front() == ' ')) act.erase(0, 1);
        while (!act.empty() && (act.back() == '}' || act.back() == ' ')) act.pop_back();
        
        std::istringstream iss(act);
        std::string stmt;
        while (std::getline(iss, stmt, ';')) {
            while (!stmt.empty() && stmt.front() == ' ') stmt.erase(0, 1);
            while (!stmt.empty() && stmt.back() == ' ') stmt.pop_back();
            if (stmt.empty()) continue;
            
            if (stmt.substr(0, 5) == "print") {
                std::string printArgs = stmt.substr(5);
                while (!printArgs.empty() && printArgs.front() == ' ') printArgs.erase(0, 1);
                
                if (printArgs.empty()) {
                    std::cout << fields[0] << vars["ORS"];
                } else {
                    std::vector<std::string> parts;
                    std::string current;
                    bool inQuote = false;
                    for (size_t i = 0; i < printArgs.length(); ++i) {
                        char c = printArgs[i];
                        if (c == '"') {
                            inQuote = !inQuote;
                        } else if ((c == ',' || c == ' ') && !inQuote) {
                            if (!current.empty()) {
                                parts.push_back(current);
                                current.clear();
                            }
                        } else {
                            current += c;
                        }
                    }
                    if (!current.empty()) parts.push_back(current);
                    
                    std::string output;
                    for (size_t i = 0; i < parts.size(); ++i) {
                        if (i > 0) output += vars["OFS"];
                        output += evalExpr(parts[i], fields);
                    }
                    std::cout << output << vars["ORS"];
                }
            } else if (stmt.substr(0, 6) == "printf") {
                std::string printfArgs = stmt.substr(6);
                while (!printfArgs.empty() && printfArgs.front() == ' ') printfArgs.erase(0, 1);
                
                size_t firstQuote = printfArgs.find('"');
                size_t lastQuote = printfArgs.rfind('"');
                if (firstQuote != std::string::npos && lastQuote > firstQuote) {
                    std::string fmt = printfArgs.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                    std::string argsStr = printfArgs.substr(lastQuote + 1);
                    
                    std::vector<std::string> printfVals;
                    std::istringstream argStream(argsStr);
                    std::string arg;
                    while (std::getline(argStream, arg, ',')) {
                        while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
                        printfVals.push_back(evalExpr(arg, fields));
                    }
                    
                    std::string output;
                    size_t valIdx = 0;
                    for (size_t i = 0; i < fmt.length(); ++i) {
                        if (fmt[i] == '%' && i + 1 < fmt.length()) {
                            char spec = fmt[i + 1];
                            if (spec == 's' && valIdx < printfVals.size()) {
                                output += printfVals[valIdx++];
                                i++;
                            } else if (spec == 'd' && valIdx < printfVals.size()) {
                                try { output += std::to_string(std::stoi(printfVals[valIdx++])); } catch (...) { output += "0"; }
                                i++;
                            } else if (spec == '%') {
                                output += '%';
                                i++;
                            } else {
                                output += fmt[i];
                            }
                        } else if (fmt[i] == '\\' && i + 1 < fmt.length()) {
                            char esc = fmt[i + 1];
                            if (esc == 'n') output += '\n';
                            else if (esc == 't') output += '\t';
                            else output += esc;
                            i++;
                        } else {
                            output += fmt[i];
                        }
                    }
                    std::cout << output;
                }
            }
        }
    };

    std::string beginBlock, endBlock, mainBlock;
    size_t beginPos = program.find("BEGIN");
    size_t endPos = program.find("END");
    
    if (beginPos != std::string::npos) {
        size_t braceStart = program.find('{', beginPos);
        if (braceStart != std::string::npos) {
            int braceCount = 1;
            size_t braceEnd = braceStart + 1;
            while (braceEnd < program.length() && braceCount > 0) {
                if (program[braceEnd] == '{') braceCount++;
                else if (program[braceEnd] == '}') braceCount--;
                braceEnd++;
            }
            beginBlock = program.substr(braceStart, braceEnd - braceStart);
        }
    }
    
    if (endPos != std::string::npos) {
        size_t braceStart = program.find('{', endPos);
        if (braceStart != std::string::npos) {
            int braceCount = 1;
            size_t braceEnd = braceStart + 1;
            while (braceEnd < program.length() && braceCount > 0) {
                if (program[braceEnd] == '{') braceCount++;
                else if (program[braceEnd] == '}') braceCount--;
                braceEnd++;
            }
            endBlock = program.substr(braceStart, braceEnd - braceStart);
        }
    }
    
    size_t mainStart = program.find('{');
    if (mainStart != std::string::npos) {
        if (beginPos != std::string::npos && mainStart > beginPos && mainStart < beginPos + 10) {
            mainStart = program.find('}', mainStart);
            if (mainStart != std::string::npos) mainStart = program.find('{', mainStart);
        }
        if (mainStart != std::string::npos && endPos != std::string::npos && mainStart > endPos) {
            mainStart = std::string::npos;
        }
        if (mainStart != std::string::npos) {
            int braceCount = 1;
            size_t braceEnd = mainStart + 1;
            while (braceEnd < program.length() && braceCount > 0) {
                if (program[braceEnd] == '{') braceCount++;
                else if (program[braceEnd] == '}') braceCount--;
                braceEnd++;
            }
            mainBlock = program.substr(mainStart, braceEnd - mainStart);
        }
    }
    
    if (mainBlock.empty() && beginBlock.empty() && endBlock.empty()) {
        mainBlock = program;
    }

    if (!beginBlock.empty()) {
        std::vector<std::string> emptyFields = {""};
        parseAction(beginBlock, emptyFields);
    }

    auto processLine = [&](const std::string& line, const std::string& filename) {
        int nr = std::stoi(vars["NR"]) + 1;
        vars["NR"] = std::to_string(nr);
        vars["FILENAME"] = filename;
        
        std::vector<std::string> fields = splitFields(line);
        
        if (!mainBlock.empty()) {
            parseAction(mainBlock, fields);
        }
    };

    if (!files.empty()) {
        for (const auto& filePath : files) {
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("awk: cannot open '" + filePath + "'");
                continue;
            }
            std::string line;
            while (std::getline(file, line)) {
                processLine(line, filePath);
            }
        }
    } else {
        std::string line;
        while (std::getline(std::cin, line)) {
            processLine(line, "");
        }
    }

    if (!endBlock.empty()) {
        std::vector<std::string> emptyFields = {""};
        parseAction(endBlock, emptyFields);
    }
    return 0;
}
