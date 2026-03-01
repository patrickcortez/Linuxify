// Compile: g++ -std=c++17 -static -o ../cmds/sed.exe sed.cpp
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <regex>
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
        printError("sed: missing script");
        return 1;
    }
    
    std::vector<std::string> scripts;
    std::vector<std::string> files;
    bool inPlace = false;
    std::string inPlaceSuffix;
    bool quietMode = false;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-n" || args[i] == "--quiet" || args[i] == "--silent") {
            quietMode = true;
        } else if (args[i] == "-e" && i + 1 < args.size()) {
            scripts.push_back(args[++i]);
        } else if (args[i] == "-i" || args[i].substr(0, 2) == "-i") {
            inPlace = true;
            if (args[i].length() > 2) inPlaceSuffix = args[i].substr(2);
        } else if (args[i][0] == '-') {
        } else if (scripts.empty()) {
            scripts.push_back(args[i]);
        } else {
            files.push_back(args[i]);
        }
    }
    
    if (scripts.empty()) {
        printError("sed: missing script");
        return 1;
    }

    struct SedCommand {
        std::string addr1;
        std::string addr2;
        char cmd;
        std::string arg1;
        std::string arg2;
        bool globalFlag;
        bool printFlag;
    };

    auto parseScript = [&](const std::string& script) -> std::vector<SedCommand> {
        std::vector<SedCommand> commands;
        size_t pos = 0;
        
        while (pos < script.length()) {
            while (pos < script.length() && (script[pos] == ' ' || script[pos] == '\t' || script[pos] == ';')) pos++;
            if (pos >= script.length()) break;
            
            SedCommand cmd = {"", "", '\0', "", "", false, false};
            
            if (std::isdigit(script[pos]) || script[pos] == '$') {
                while (pos < script.length() && (std::isdigit(script[pos]) || script[pos] == '$')) {
                    cmd.addr1 += script[pos++];
                }
                if (pos < script.length() && script[pos] == ',') {
                    pos++;
                    while (pos < script.length() && (std::isdigit(script[pos]) || script[pos] == '$')) {
                        cmd.addr2 += script[pos++];
                    }
                }
            } else if (script[pos] == '/') {
                pos++;
                while (pos < script.length() && script[pos] != '/') {
                    if (script[pos] == '\\' && pos + 1 < script.length()) {
                        cmd.addr1 += script[pos++];
                    }
                    cmd.addr1 += script[pos++];
                }
                if (pos < script.length()) pos++;
            }
            
            while (pos < script.length() && (script[pos] == ' ' || script[pos] == '\t')) pos++;
            if (pos >= script.length()) break;
            
            cmd.cmd = script[pos++];
            
            if (cmd.cmd == 's' && pos < script.length()) {
                char delim = script[pos++];
                bool escaped = false;
                while (pos < script.length()) {
                    if (escaped) {
                        cmd.arg1 += script[pos++];
                        escaped = false;
                    } else if (script[pos] == '\\') {
                        escaped = true;
                        cmd.arg1 += script[pos++];
                    } else if (script[pos] == delim) {
                        pos++;
                        break;
                    } else {
                        cmd.arg1 += script[pos++];
                    }
                }
                escaped = false;
                while (pos < script.length()) {
                    if (escaped) {
                        cmd.arg2 += script[pos++];
                        escaped = false;
                    } else if (script[pos] == '\\') {
                        escaped = true;
                        cmd.arg2 += script[pos++];
                    } else if (script[pos] == delim) {
                        pos++;
                        break;
                    } else {
                        cmd.arg2 += script[pos++];
                    }
                }
                while (pos < script.length() && script[pos] != ';' && script[pos] != '\n') {
                    if (script[pos] == 'g') cmd.globalFlag = true;
                    else if (script[pos] == 'p') cmd.printFlag = true;
                    pos++;
                }
            } else if (cmd.cmd == 'y' && pos < script.length()) {
                char delim = script[pos++];
                while (pos < script.length() && script[pos] != delim) cmd.arg1 += script[pos++];
                if (pos < script.length()) pos++;
                while (pos < script.length() && script[pos] != delim) cmd.arg2 += script[pos++];
                if (pos < script.length()) pos++;
            }
            
            commands.push_back(cmd);
        }
        return commands;
    };

    std::vector<SedCommand> allCommands;
    for (const auto& script : scripts) {
        auto cmds = parseScript(script);
        allCommands.insert(allCommands.end(), cmds.begin(), cmds.end());
    }

    auto matchAddress = [](const std::string& addr, int lineNum, int lastLine, const std::string& line) -> bool {
        if (addr.empty()) return true;
        if (addr == "$") return lineNum == lastLine;
        if (std::isdigit(addr[0])) return lineNum == std::stoi(addr);
        try {
            std::regex re(addr);
            return std::regex_search(line, re);
        } catch (...) {
            return line.find(addr) != std::string::npos;
        }
    };

    auto processLines = [&](std::vector<std::string>& lines) -> std::string {
        std::ostringstream output;
        int lastLine = (int)lines.size();
        std::map<int, bool> inRange;
        
        for (int lineNum = 1; lineNum <= (int)lines.size(); ++lineNum) {
            std::string line = lines[lineNum - 1];
            bool deleted = false;
            bool printed = false;
            
            for (size_t ci = 0; ci < allCommands.size(); ++ci) {
                const auto& cmd = allCommands[ci];
                
                bool inAddr = false;
                if (cmd.addr1.empty() && cmd.addr2.empty()) {
                    inAddr = true;
                } else if (cmd.addr2.empty()) {
                    inAddr = matchAddress(cmd.addr1, lineNum, lastLine, line);
                } else {
                    if (!inRange[ci] && matchAddress(cmd.addr1, lineNum, lastLine, line)) {
                        inRange[ci] = true;
                    }
                    if (inRange[ci]) {
                        inAddr = true;
                        if (matchAddress(cmd.addr2, lineNum, lastLine, line)) {
                            inRange[ci] = false;
                        }
                    }
                }
                
                if (!inAddr) continue;
                
                switch (cmd.cmd) {
                    case 'd':
                        deleted = true;
                        break;
                    case 'p':
                        output << line << "\n";
                        printed = true;
                        break;
                    case 'q':
                        if (!quietMode && !deleted) output << line << "\n";
                        return output.str();
                    case 's': {
                        try {
                            std::regex re(cmd.arg1);
                            std::string repl = cmd.arg2;
                            if (cmd.globalFlag) {
                                line = std::regex_replace(line, re, repl);
                            } else {
                                line = std::regex_replace(line, re, repl, std::regex_constants::format_first_only);
                            }
                            if (cmd.printFlag) {
                                output << line << "\n";
                                printed = true;
                            }
                        } catch (...) {
                            if (cmd.globalFlag) {
                                size_t pos = 0;
                                while ((pos = line.find(cmd.arg1, pos)) != std::string::npos) {
                                    line.replace(pos, cmd.arg1.length(), cmd.arg2);
                                    pos += cmd.arg2.length();
                                }
                            } else {
                                size_t pos = line.find(cmd.arg1);
                                if (pos != std::string::npos) {
                                    line.replace(pos, cmd.arg1.length(), cmd.arg2);
                                }
                            }
                        }
                        break;
                    }
                    case 'y': {
                        for (char& c : line) {
                            size_t idx = cmd.arg1.find(c);
                            if (idx != std::string::npos && idx < cmd.arg2.length()) {
                                c = cmd.arg2[idx];
                            }
                        }
                        break;
                    }
                }
                
                if (deleted) break;
            }
            
            if (!deleted && !quietMode) {
                output << line << "\n";
            }
        }
        return output.str();
    };

    if (!files.empty()) {
        for (const auto& filePath : files) {
            std::vector<std::string> lines;
            std::ifstream file(resolvePath(filePath));
            if (!file) {
                printError("sed: cannot open '" + filePath + "'");
                continue;
            }
            std::string line;
            while (std::getline(file, line)) lines.push_back(line);
            file.close();
            
            std::string result = processLines(lines);
            
            if (inPlace) {
                if (!inPlaceSuffix.empty()) {
                    fs::copy_file(resolvePath(filePath), resolvePath(filePath) + inPlaceSuffix, fs::copy_options::overwrite_existing);
                }
                std::ofstream out(resolvePath(filePath));
                out << result;
            } else {
                std::cout << result;
            }
        }
    } else {
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(std::cin, line)) lines.push_back(line);
        std::cout << processLines(lines);
    }
    return 0;
}
