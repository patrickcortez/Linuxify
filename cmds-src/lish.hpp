// Linuxify Bash Interpreter - Lexer, Parser, Executor
// A modular shell script interpreter
// Compile: g++ -std=c++17 -static -o bash.exe bash.cpp

#pragma once

#include "../shell_streams.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <functional>
#include <regex>
#include <windows.h>

namespace Bash {

// ============================================================================
// TOKEN TYPES
// ============================================================================
enum class TokenType {
    // Literals
    WORD,           // command, argument, identifier
    STRING,         // "quoted string" or 'single quoted'
    NUMBER,         // 123
    
    // Variables
    VARIABLE,       // $VAR or ${VAR}
    ASSIGNMENT,     // VAR=value
    
    // Operators
    PIPE,           // |
    REDIRECT_OUT,   // >
    REDIRECT_APPEND,// >>
    REDIRECT_IN,    // <
    AND,            // &&
    OR,             // ||
    SEMICOLON,      // ;
    AMPERSAND,      // &
    
    // Grouping
    LPAREN,         // (
    RPAREN,         // )
    LBRACE,         // {
    RBRACE,         // }
    LBRACKET,       // [
    RBRACKET,       // ]
    
    // Keywords (prefixed to avoid Windows macro conflicts)
    KW_IF,          // if
    KW_THEN,        // then
    KW_ELSE,        // else
    KW_ELIF,        // elif
    KW_FI,          // fi
    KW_FOR,         // for
    KW_IN,          // in
    KW_DO,          // do
    KW_DONE,        // done
    KW_WHILE,       // while
    KW_UNTIL,       // until
    KW_CASE,        // case
    KW_ESAC,        // esac
    KW_FUNCTION,    // function
    
    // Special
    NEWLINE,        // \n
    COMMENT,        // # comment
    END_OF_FILE,    // EOF
};

// Token structure
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
    
    Token(TokenType t, const std::string& v, int l = 0, int c = 0)
        : type(t), value(v), line(l), column(c) {}
    
    std::string typeName() const {
        switch (type) {
            case TokenType::WORD: return "WORD";
            case TokenType::STRING: return "STRING";
            case TokenType::NUMBER: return "NUMBER";
            case TokenType::VARIABLE: return "VARIABLE";
            case TokenType::ASSIGNMENT: return "ASSIGNMENT";
            case TokenType::PIPE: return "PIPE";
            case TokenType::REDIRECT_OUT: return "REDIRECT_OUT";
            case TokenType::REDIRECT_APPEND: return "REDIRECT_APPEND";
            case TokenType::REDIRECT_IN: return "REDIRECT_IN";
            case TokenType::AND: return "AND";
            case TokenType::OR: return "OR";
            case TokenType::SEMICOLON: return "SEMICOLON";
            case TokenType::AMPERSAND: return "AMPERSAND";
            case TokenType::KW_IF: return "IF";
            case TokenType::KW_THEN: return "THEN";
            case TokenType::KW_ELSE: return "ELSE";
            case TokenType::KW_FI: return "FI";
            case TokenType::KW_FOR: return "FOR";
            case TokenType::KW_IN: return "IN";
            case TokenType::KW_DO: return "DO";
            case TokenType::KW_DONE: return "DONE";
            case TokenType::KW_WHILE: return "WHILE";
            case TokenType::KW_FUNCTION: return "FUNCTION";
            case TokenType::NEWLINE: return "NEWLINE";
            case TokenType::COMMENT: return "COMMENT";
            case TokenType::END_OF_FILE: return "EOF";
            default: return "UNKNOWN";
        }
    }
};

// ============================================================================
// LEXER - Tokenizes shell script input
// ============================================================================
class Lexer {
private:
    std::string source;
    size_t pos = 0;
    int line = 1;
    int column = 1;
    
    // Keyword map
    std::map<std::string, TokenType> keywords = {
        {"if", TokenType::KW_IF},
        {"then", TokenType::KW_THEN},
        {"else", TokenType::KW_ELSE},
        {"elif", TokenType::KW_ELIF},
        {"fi", TokenType::KW_FI},
        {"for", TokenType::KW_FOR},
        {"in", TokenType::KW_IN},
        {"do", TokenType::KW_DO},
        {"done", TokenType::KW_DONE},
        {"while", TokenType::KW_WHILE},
        {"until", TokenType::KW_UNTIL},
        {"case", TokenType::KW_CASE},
        {"esac", TokenType::KW_ESAC},
        {"function", TokenType::KW_FUNCTION},
    };
    
    char current() const {
        return pos < source.length() ? source[pos] : '\0';
    }
    
    char peek(int offset = 1) const {
        size_t p = pos + offset;
        return p < source.length() ? source[p] : '\0';
    }
    
    void advance() {
        if (current() == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        pos++;
    }
    
    void skipWhitespace() {
        while (current() == ' ' || current() == '\t' || current() == '\r') {
            advance();
        }
    }
    
    Token readString(char quote) {
        int startLine = line, startCol = column;
        advance(); // skip opening quote
        std::string value;
        
        while (current() != '\0' && current() != quote) {
            if (current() == '\\' && peek() != '\0') {
                advance();
                switch (current()) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    default: value += current(); break;
                }
            } else {
                value += current();
            }
            advance();
        }
        advance(); // skip closing quote
        return Token(TokenType::STRING, value, startLine, startCol);
    }
    
    Token readVariable() {
        int startLine = line, startCol = column;
        advance(); // skip $
        std::string name;
        
        if (current() == '{') {
            advance(); // skip {
            while (current() != '\0' && current() != '}') {
                name += current();
                advance();
            }
            advance(); // skip }
            return Token(TokenType::VARIABLE, "${" + name + "}", startLine, startCol);
        } else if (current() == '?') {
            name = "?";
            advance();
            return Token(TokenType::VARIABLE, "$?", startLine, startCol);
        } else if (current() == '$') {
            name = "$";
            advance();
            return Token(TokenType::VARIABLE, "$$", startLine, startCol);
        } else if (current() == '(') {
            // Command substitution $(...)
            advance();
            int depth = 1;
            while (current() != '\0' && depth > 0) {
                if (current() == '(') depth++;
                else if (current() == ')') depth--;
                if (depth > 0) name += current();
                advance();
            }
            return Token(TokenType::VARIABLE, "$(" + name + ")", startLine, startCol);
        } else {
            while (isalnum(current()) || current() == '_') {
                name += current();
                advance();
            }
        }
        
        // Return with $ prefix so expandVariables can recognize it
        return Token(TokenType::VARIABLE, "$" + name, startLine, startCol);
    }
    
    Token readWord() {
        int startLine = line, startCol = column;
        std::string value;
        
        while (current() != '\0' && !isspace(current()) &&
               current() != ';' && current() != '|' && current() != '&' &&
               current() != '>' && current() != '<' && current() != '(' &&
               current() != ')' && current() != '{' && current() != '}' &&
               current() != '"' && current() != '\'' && current() != '#' &&
               current() != '$') {
            
            if (current() == '\\' && peek() != '\0') {
                advance();
                value += current();
            } else {
                value += current();
            }
            advance();
        }
        
        // Check for assignment (VAR=value)
        size_t eqPos = value.find('=');
        if (eqPos != std::string::npos && eqPos > 0) {
            // It's an assignment
            return Token(TokenType::ASSIGNMENT, value, startLine, startCol);
        }
        
        // Check for keyword
        auto it = keywords.find(value);
        if (it != keywords.end()) {
            return Token(it->second, value, startLine, startCol);
        }
        
        // Check if it's a number
        bool isNum = !value.empty();
        for (char c : value) {
            if (!isdigit(c)) { isNum = false; break; }
        }
        
        return Token(isNum ? TokenType::NUMBER : TokenType::WORD, value, startLine, startCol);
    }

public:
    explicit Lexer(const std::string& src) : source(src) {}
    
    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        
        while (pos < source.length()) {
            skipWhitespace();
            
            if (current() == '\0') break;
            
            int startLine = line, startCol = column;
            
            // Newline
            if (current() == '\n') {
                tokens.push_back(Token(TokenType::NEWLINE, "\\n", startLine, startCol));
                advance();
                continue;
            }
            
            // Comment
            if (current() == '#') {
                std::string comment;
                while (current() != '\0' && current() != '\n') {
                    comment += current();
                    advance();
                }
                tokens.push_back(Token(TokenType::COMMENT, comment, startLine, startCol));
                continue;
            }
            
            // String
            if (current() == '"' || current() == '\'') {
                tokens.push_back(readString(current()));
                continue;
            }
            
            // Variable
            if (current() == '$') {
                tokens.push_back(readVariable());
                continue;
            }
            
            // Operators
            if (current() == '|') {
                advance();
                if (current() == '|') {
                    advance();
                    tokens.push_back(Token(TokenType::OR, "||", startLine, startCol));
                } else {
                    tokens.push_back(Token(TokenType::PIPE, "|", startLine, startCol));
                }
                continue;
            }
            
            if (current() == '&') {
                advance();
                if (current() == '&') {
                    advance();
                    tokens.push_back(Token(TokenType::AND, "&&", startLine, startCol));
                } else {
                    tokens.push_back(Token(TokenType::AMPERSAND, "&", startLine, startCol));
                }
                continue;
            }
            
            if (current() == '>') {
                advance();
                if (current() == '>') {
                    advance();
                    tokens.push_back(Token(TokenType::REDIRECT_APPEND, ">>", startLine, startCol));
                } else {
                    tokens.push_back(Token(TokenType::REDIRECT_OUT, ">", startLine, startCol));
                }
                continue;
            }
            
            if (current() == '<') {
                advance();
                tokens.push_back(Token(TokenType::REDIRECT_IN, "<", startLine, startCol));
                continue;
            }
            
            if (current() == ';') {
                advance();
                tokens.push_back(Token(TokenType::SEMICOLON, ";", startLine, startCol));
                continue;
            }
            
            if (current() == '(') {
                advance();
                tokens.push_back(Token(TokenType::LPAREN, "(", startLine, startCol));
                continue;
            }
            
            if (current() == ')') {
                advance();
                tokens.push_back(Token(TokenType::RPAREN, ")", startLine, startCol));
                continue;
            }
            
            if (current() == '{') {
                advance();
                tokens.push_back(Token(TokenType::LBRACE, "{", startLine, startCol));
                continue;
            }
            
            if (current() == '}') {
                advance();
                tokens.push_back(Token(TokenType::RBRACE, "}", startLine, startCol));
                continue;
            }
            
            if (current() == '[') {
                advance();
                tokens.push_back(Token(TokenType::LBRACKET, "[", startLine, startCol));
                continue;
            }
            
            if (current() == ']') {
                advance();
                tokens.push_back(Token(TokenType::RBRACKET, "]", startLine, startCol));
                continue;
            }
            
            // Word (command, argument, etc.)
            tokens.push_back(readWord());
        }
        
        tokens.push_back(Token(TokenType::END_OF_FILE, "", line, column));
        return tokens;
    }
};

// ============================================================================
// AST NODES - Abstract Syntax Tree
// ============================================================================
struct ASTNode {
    virtual ~ASTNode() = default;
    virtual std::string type() const = 0;
};

// Simple command: ls -la
struct CommandNode : ASTNode {
    std::vector<std::string> args;  // First element is the command name
    std::vector<std::pair<std::string, std::string>> redirects;  // type, target
    bool background = false;
    
    std::string type() const override { return "Command"; }
};

// Pipeline: cmd1 | cmd2 | cmd3
struct PipelineNode : ASTNode {
    std::vector<std::shared_ptr<CommandNode>> commands;
    
    std::string type() const override { return "Pipeline"; }
};

// Compound: cmd1 && cmd2 || cmd3
struct CompoundNode : ASTNode {
    std::vector<std::shared_ptr<ASTNode>> nodes;
    std::vector<TokenType> operators;  // AND, OR, SEMICOLON
    
    std::string type() const override { return "Compound"; }
};

// Assignment: VAR=value
struct AssignmentNode : ASTNode {
    std::string name;
    std::string value;
    
    std::string type() const override { return "Assignment"; }
};

// If statement
struct IfNode : ASTNode {
    std::shared_ptr<ASTNode> condition;
    std::vector<std::shared_ptr<ASTNode>> thenBody;
    std::vector<std::shared_ptr<ASTNode>> elseBody;
    
    std::string type() const override { return "If"; }
};

// For loop
struct ForNode : ASTNode {
    std::string variable;
    std::vector<std::string> values;
    std::vector<std::shared_ptr<ASTNode>> body;
    
    std::string type() const override { return "For"; }
};

// While loop
struct WhileNode : ASTNode {
    std::shared_ptr<ASTNode> condition;
    std::vector<std::shared_ptr<ASTNode>> body;
    
    std::string type() const override { return "While"; }
};

// Function definition
struct FunctionNode : ASTNode {
    std::string name;
    std::vector<std::shared_ptr<ASTNode>> body;
    
    std::string type() const override { return "Function"; }
};

// ============================================================================
// PARSER - Builds AST from tokens
// ============================================================================
class Parser {
private:
    std::vector<Token> tokens;
    size_t pos = 0;
    
    Token current() const {
        return pos < tokens.size() ? tokens[pos] : Token(TokenType::END_OF_FILE, "");
    }
    
    Token peek(int offset = 1) const {
        size_t p = pos + offset;
        return p < tokens.size() ? tokens[p] : Token(TokenType::END_OF_FILE, "");
    }
    
    void advance() { pos++; }
    
    bool check(TokenType type) const {
        return current().type == type;
    }
    
    bool match(TokenType type) {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }
    
    void skipNewlines() {
        while (check(TokenType::NEWLINE) || check(TokenType::COMMENT)) {
            advance();
        }
    }
    
    std::shared_ptr<CommandNode> parseCommand() {
        auto cmd = std::make_shared<CommandNode>();
        
        while (!check(TokenType::END_OF_FILE) && !check(TokenType::NEWLINE) &&
               !check(TokenType::PIPE) && !check(TokenType::AND) && !check(TokenType::OR) &&
               !check(TokenType::SEMICOLON) && !check(TokenType::KW_THEN) && !check(TokenType::KW_DO) &&
               !check(TokenType::KW_DONE) && !check(TokenType::KW_FI) && !check(TokenType::KW_ELSE) &&
               !check(TokenType::KW_ELIF) && !check(TokenType::KW_FOR) && !check(TokenType::KW_WHILE) &&
               !check(TokenType::KW_IF)) {
            
            if (check(TokenType::REDIRECT_OUT) || check(TokenType::REDIRECT_APPEND)) {
                std::string redirType = current().value;
                advance();
                if (check(TokenType::WORD) || check(TokenType::STRING)) {
                    cmd->redirects.push_back({redirType, current().value});
                    advance();
                }
            } else if (check(TokenType::AMPERSAND)) {
                cmd->background = true;
                advance();
            } else if (check(TokenType::WORD) || check(TokenType::STRING) || 
                       check(TokenType::NUMBER) || check(TokenType::VARIABLE)) {
                cmd->args.push_back(current().value);
                advance();
            } else if (check(TokenType::LBRACKET)) {
                // [ condition ] - test command
                cmd->args.push_back("[");
                advance();
                while (!check(TokenType::RBRACKET) && !check(TokenType::END_OF_FILE)) {
                    if (check(TokenType::WORD) || check(TokenType::STRING) || 
                        check(TokenType::NUMBER) || check(TokenType::VARIABLE)) {
                        cmd->args.push_back(current().value);
                    }
                    advance();
                }
                if (check(TokenType::RBRACKET)) {
                    cmd->args.push_back("]");
                    advance();
                }
            } else {
                advance();
            }
        }
        
        return cmd;
    }
    
    std::shared_ptr<PipelineNode> parsePipeline() {
        auto pipeline = std::make_shared<PipelineNode>();
        
        auto first = parseCommand();
        if (!first->args.empty()) {
            pipeline->commands.push_back(first);
        }
        
        while (check(TokenType::PIPE)) {
            advance();
            auto next = parseCommand();
            if (!next->args.empty()) {
                pipeline->commands.push_back(next);
            }
        }
        
        return pipeline;
    }
    
    std::shared_ptr<IfNode> parseIf() {
        auto node = std::make_shared<IfNode>();
        
        advance(); // skip 'if'
        skipNewlines();
        
        // Parse condition (usually [ ... ] or command)
        node->condition = parsePipeline();
        
        // Skip semicolon if present (e.g., if [ cond ]; then)
        match(TokenType::SEMICOLON);
        skipNewlines();
        
        // Must have 'then'
        if (!match(TokenType::KW_THEN)) {
            return node;  // Malformed if
        }
        skipNewlines();
        
        // Parse then body
        while (!check(TokenType::KW_FI) && !check(TokenType::KW_ELSE) && 
               !check(TokenType::KW_ELIF) && !check(TokenType::END_OF_FILE)) {
            // Skip semicolons between statements
            while (check(TokenType::SEMICOLON)) {
                advance();
            }
            skipNewlines();
            
            if (check(TokenType::KW_FI) || check(TokenType::KW_ELSE) || 
                check(TokenType::KW_ELIF) || check(TokenType::END_OF_FILE)) break;
            
            auto stmt = parseStatement();
            if (stmt) node->thenBody.push_back(stmt);
            
            // Skip semicolons and newlines after statement
            while (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE) || check(TokenType::COMMENT)) {
                advance();
            }
        }
        
        // Parse else body
        if (check(TokenType::KW_ELSE)) {
            advance();
            skipNewlines();
            while (!check(TokenType::KW_FI) && !check(TokenType::END_OF_FILE)) {
                // Skip semicolons between statements
                while (check(TokenType::SEMICOLON)) {
                    advance();
                }
                skipNewlines();
                
                if (check(TokenType::KW_FI) || check(TokenType::END_OF_FILE)) break;
                
                auto stmt = parseStatement();
                if (stmt) node->elseBody.push_back(stmt);
                
                // Skip semicolons and newlines after statement
                while (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE) || check(TokenType::COMMENT)) {
                    advance();
                }
            }
        }
        
        match(TokenType::KW_FI);
        return node;
    }
    
    std::shared_ptr<ForNode> parseFor() {
        auto node = std::make_shared<ForNode>();
        
        advance(); // skip 'for'
        skipNewlines();
        
        // Variable name
        if (check(TokenType::WORD)) {
            node->variable = current().value;
            advance();
        }
        
        skipNewlines();
        match(TokenType::KW_IN);
        
        // Values - stop at do, semicolon, newline, or EOF
        while (!check(TokenType::KW_DO) && !check(TokenType::SEMICOLON) && 
               !check(TokenType::NEWLINE) && !check(TokenType::END_OF_FILE)) {
            if (check(TokenType::WORD) || check(TokenType::STRING) || check(TokenType::VARIABLE) || check(TokenType::NUMBER)) {
                node->values.push_back(current().value);
            }
            advance();
        }
        
        // Skip semicolon if present
        match(TokenType::SEMICOLON);
        skipNewlines();
        
        // Must have 'do'
        if (!match(TokenType::KW_DO)) {
            // If no 'do', something is wrong - return what we have
            return node;
        }
        skipNewlines();
        
        // Body - parse until 'done'
        while (!check(TokenType::KW_DONE) && !check(TokenType::END_OF_FILE)) {
            // Skip semicolons between statements
            while (check(TokenType::SEMICOLON)) {
                advance();
            }
            skipNewlines();
            
            if (check(TokenType::KW_DONE) || check(TokenType::END_OF_FILE)) break;
            
            auto stmt = parseStatement();
            if (stmt) node->body.push_back(stmt);
            
            // Skip semicolons and newlines after statement
            while (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE) || check(TokenType::COMMENT)) {
                advance();
            }
        }
        
        match(TokenType::KW_DONE);
        return node;
    }
    
    std::shared_ptr<WhileNode> parseWhile() {
        auto node = std::make_shared<WhileNode>();
        
        advance(); // skip 'while'
        skipNewlines();
        
        node->condition = parsePipeline();
        
        skipNewlines();
        match(TokenType::KW_DO);
        skipNewlines();
        
        while (!check(TokenType::KW_DONE) && !check(TokenType::END_OF_FILE)) {
            auto stmt = parseStatement();
            if (stmt) node->body.push_back(stmt);
            skipNewlines();
        }
        
        match(TokenType::KW_DONE);
        return node;
    }
    
    std::shared_ptr<ASTNode> parseStatement() {
        skipNewlines();
        
        if (check(TokenType::KW_IF)) {
            return parseIf();
        }
        
        if (check(TokenType::KW_FOR)) {
            return parseFor();
        }
        
        if (check(TokenType::KW_WHILE)) {
            return parseWhile();
        }
        
        if (check(TokenType::ASSIGNMENT)) {
            auto node = std::make_shared<AssignmentNode>();
            std::string val = current().value;
            size_t eq = val.find('=');
            node->name = val.substr(0, eq);
            node->value = val.substr(eq + 1);
            advance();
            
            // If value is empty and next token is STRING, use that as value
            // This handles: NAME="value" where NAME= is one token and "value" is next
            if (node->value.empty() && check(TokenType::STRING)) {
                node->value = current().value;
                advance();
            }
            // Also handle unquoted value after =
            else if (node->value.empty() && (check(TokenType::WORD) || check(TokenType::NUMBER))) {
                node->value = current().value;
                advance();
            }
            
            return node;
        }
        
        if (check(TokenType::COMMENT)) {
            advance();
            return nullptr;
        }
        
        auto pipeline = parsePipeline();
        if (!pipeline->commands.empty()) {
            return pipeline;
        }
        
        return nullptr;
    }

public:
    explicit Parser(const std::vector<Token>& toks) : tokens(toks) {}
    
    std::vector<std::shared_ptr<ASTNode>> parse() {
        std::vector<std::shared_ptr<ASTNode>> program;
        
        while (!check(TokenType::END_OF_FILE)) {
            auto stmt = parseStatement();
            if (stmt) {
                program.push_back(stmt);
            }
            
            // Skip statement separators
            while (check(TokenType::NEWLINE) || check(TokenType::SEMICOLON) || check(TokenType::COMMENT)) {
                advance();
            }
        }
        
        return program;
    }
};

// ============================================================================
// EXECUTOR - Executes AST
// ============================================================================
class Executor {
private:
    std::map<std::string, std::string> variables;
    std::map<std::string, std::vector<std::shared_ptr<ASTNode>>> functions;
    int lastExitCode = 0;
    std::string currentDir;
    bool debugMode = false;
    
    // Built-in commands
    std::map<std::string, std::function<int(const std::vector<std::string>&)>> builtins;
    
    void setupBuiltins() {
        builtins["echo"] = [this](const std::vector<std::string>& args) {
            for (size_t i = 1; i < args.size(); i++) {
                if (i > 1) ShellIO::sout << " ";
                ShellIO::sout << expandVariables(args[i]);
            }
            ShellIO::sout << ShellIO::endl;
            return 0;
        };
        
        builtins["export"] = [this](const std::vector<std::string>& args) {
            for (size_t i = 1; i < args.size(); i++) {
                size_t eq = args[i].find('=');
                if (eq != std::string::npos) {
                    std::string name = args[i].substr(0, eq);
                    std::string value = args[i].substr(eq + 1);
                    variables[name] = value;
                    SetEnvironmentVariableA(name.c_str(), value.c_str());
                }
            }
            return 0;
        };
        
        builtins["cd"] = [this](const std::vector<std::string>& args) {
            std::string path = args.size() > 1 ? args[1] : std::string(getenv("USERPROFILE") ? getenv("USERPROFILE") : ".");
            if (SetCurrentDirectoryA(path.c_str())) {
                char buf[MAX_PATH];
                GetCurrentDirectoryA(MAX_PATH, buf);
                currentDir = buf;
                return 0;
            }
            ShellIO::serr << "cd: " << path << ": No such directory" << ShellIO::endl;
            return 1;
        };
        
        builtins["exit"] = [](const std::vector<std::string>& args) {
            int code = args.size() > 1 ? std::stoi(args[1]) : 0;
            exit(code);
            return code;
        };
        
        builtins["set"] = [this](const std::vector<std::string>& args) {
            for (const auto& [name, value] : variables) {
                ShellIO::sout << name << "=" << value << ShellIO::endl;
            }
            return 0;
        };
        
        builtins["true"] = [](const std::vector<std::string>&) { return 0; };
        builtins["false"] = [](const std::vector<std::string>&) { return 1; };
        
        builtins["["] = [this](const std::vector<std::string>& args) {
            return executeTest(args);
        };
        
        builtins["test"] = [this](const std::vector<std::string>& args) {
            return executeTest(args);
        };
        
        builtins["pwd"] = [this](const std::vector<std::string>&) {
            ShellIO::sout << currentDir << ShellIO::endl;
            return 0;
        };
        
        builtins["help"] = [](const std::vector<std::string>&) {
            ShellIO::sout << ShellIO::Color::LightGreen << "Linuxify Shell (lish) Built-in Commands:\n\n" << ShellIO::Color::Reset;
            
            ShellIO::sout << "  echo <args>       Print arguments to stdout\n";
            ShellIO::sout << "  cd <dir>          Change current directory\n";
            ShellIO::sout << "  pwd               Print working directory\n";
            ShellIO::sout << "  export VAR=val    Set environment variable\n";
            ShellIO::sout << "  set               Display all variables\n";
            ShellIO::sout << "  exit [code]       Exit the shell\n";
            ShellIO::sout << "  test / [ ... ]    Evaluate conditional expressions\n";
            ShellIO::sout << "  true              Return exit code 0\n";
            ShellIO::sout << "  false             Return exit code 1\n";
            ShellIO::sout << "\n";
            
            ShellIO::sout << ShellIO::Color::LightGreen << "Script Features:\n" << ShellIO::Color::Reset;
            
            ShellIO::sout << "  Variables         NAME=\"value\", $NAME, ${NAME}\n";
            ShellIO::sout << "  If/Else           if [ cond ]; then ... fi\n";
            ShellIO::sout << "  For Loop          for i in 1 2 3; do ... done\n";
            ShellIO::sout << "  While Loop        while [ cond ]; do ... done\n";
            ShellIO::sout << "  Pipes             cmd1 | cmd2\n";
            ShellIO::sout << "  Comments          # comment\n";
            ShellIO::sout << "\n";
            
            ShellIO::sout << ShellIO::Color::LightGreen << "Test Operators:\n" << ShellIO::Color::Reset;
            
            ShellIO::sout << "  -f FILE           File exists\n";
            ShellIO::sout << "  -d FILE           Directory exists\n";
            ShellIO::sout << "  -e FILE           Path exists\n";
            ShellIO::sout << "  -z STRING         String is empty\n";
            ShellIO::sout << "  -n STRING         String is not empty\n";
            ShellIO::sout << "  a = b             Strings equal\n";
            ShellIO::sout << "  a -eq b           Numbers equal\n";
            ShellIO::sout << "  a -lt/-gt b       Less/Greater than\n";
            
            return 0;
        };
    }
    
    // Expand variables in a string
    std::string expandVariables(const std::string& input) {
        std::string result = input;
        
        // Handle $VAR and ${VAR}
        std::regex varRegex("\\$\\{?([a-zA-Z_][a-zA-Z0-9_]*)\\}?");
        std::smatch match;
        std::string temp = result;
        
        while (std::regex_search(temp, match, varRegex)) {
            std::string varName = match[1];
            std::string varValue;
            
            if (variables.count(varName)) {
                varValue = variables[varName];
            } else {
                char* envVal = getenv(varName.c_str());
                if (envVal) varValue = envVal;
            }
            
            size_t pos = result.find(match[0]);
            if (pos != std::string::npos) {
                result.replace(pos, match[0].length(), varValue);
            }
            temp = match.suffix();
        }
        
        // Handle $?
        size_t pos = result.find("$?");
        if (pos != std::string::npos) {
            result.replace(pos, 2, std::to_string(lastExitCode));
        }
        
        return result;
    }
    
    // Execute test command  ([ ... ])
    int executeTest(const std::vector<std::string>& args) {
        if (args.size() < 2) return 1;
        
        size_t i = 1;
        if (args[0] == "[" && !args.empty() && args.back() == "]") {
            // Skip closing bracket
        }
        
        // Simple tests
        if (args.size() >= 3) {
            std::string op = args.size() > 2 ? args[2] : "";
            std::string left = expandVariables(args[1]);
            std::string right = args.size() > 3 ? expandVariables(args[3]) : "";
            
            // String comparisons
            if (op == "=" || op == "==") return left == right ? 0 : 1;
            if (op == "!=") return left != right ? 0 : 1;
            
            // Numeric comparisons - wrap in try-catch for safety
            try {
                if (op == "-eq") return std::stoi(left) == std::stoi(right) ? 0 : 1;
                if (op == "-ne") return std::stoi(left) != std::stoi(right) ? 0 : 1;
                if (op == "-lt") return std::stoi(left) < std::stoi(right) ? 0 : 1;
                if (op == "-le") return std::stoi(left) <= std::stoi(right) ? 0 : 1;
                if (op == "-gt") return std::stoi(left) > std::stoi(right) ? 0 : 1;
                if (op == "-ge") return std::stoi(left) >= std::stoi(right) ? 0 : 1;
            } catch (const std::exception&) {
                ShellIO::serr << "bash: test: integer expression expected" << ShellIO::endl;
                return 2;
            }
        }
        
        if (args.size() >= 2) {
            std::string op = args[1];
            std::string operand = args.size() > 2 ? expandVariables(args[2]) : "";
            
            // File tests
            if (op == "-f") return GetFileAttributesA(operand.c_str()) != INVALID_FILE_ATTRIBUTES ? 0 : 1;
            if (op == "-d") {
                DWORD attr = GetFileAttributesA(operand.c_str());
                return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 0 : 1;
            }
            if (op == "-e") return GetFileAttributesA(operand.c_str()) != INVALID_FILE_ATTRIBUTES ? 0 : 1;
            if (op == "-z") return operand.empty() ? 0 : 1;
            if (op == "-n") return !operand.empty() ? 0 : 1;
        }
        
        return 1;
    }
    
    // Execute external command using CreateProcessA
    int executeExternal(const std::vector<std::string>& args) {
        if (args.empty()) return 0;
        
        std::string cmdLine;
        for (const auto& arg : args) {
            if (!cmdLine.empty()) cmdLine += " ";
            std::string expanded = expandVariables(arg);
            if (expanded.find(' ') != std::string::npos) {
                cmdLine += "\"" + expanded + "\"";
            } else {
                cmdLine += expanded;
            }
        }
        
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        ZeroMemory(&pi, sizeof(pi));
        
        char cmdBuffer[8192];
        strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);
        
        if (!CreateProcessA(
            NULL,
            cmdBuffer,
            NULL,
            NULL,
            TRUE,   // Inherit handles
            0,
            NULL,
            currentDir.c_str(),
            &si,
            &pi
        )) {
            ShellIO::serr << "lish: command not found: " << args[0] << ShellIO::endl;
            return 127;
        }
        
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        return (int)exitCode;
    }

public:
    Executor() {
        char buf[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, buf);
        currentDir = buf;
        setupBuiltins();
    }
    
    void setDebug(bool debug) { debugMode = debug; }
    
    void setVariable(const std::string& name, const std::string& value) {
        variables[name] = value;
    }
    
    std::string getVariable(const std::string& name) {
        return variables.count(name) ? variables[name] : "";
    }
    
    int execute(std::shared_ptr<ASTNode> node) {
        if (!node) return 0;
        
        if (debugMode) {
            ShellIO::sout << "[DEBUG] Executing: " << node->type() << ShellIO::endl;
        }
        
        // Assignment
        if (auto assign = std::dynamic_pointer_cast<AssignmentNode>(node)) {
            variables[assign->name] = expandVariables(assign->value);
            return 0;
        }
        
        // Command
        if (auto cmd = std::dynamic_pointer_cast<CommandNode>(node)) {
            if (cmd->args.empty()) return 0;
            
            // Expand variables in all arguments
            std::vector<std::string> expandedArgs;
            for (const auto& arg : cmd->args) {
                expandedArgs.push_back(expandVariables(arg));
            }
            
            std::string cmdName = expandedArgs[0];
            
            // Check built-ins
            if (builtins.count(cmdName)) {
                lastExitCode = builtins[cmdName](expandedArgs);
                return lastExitCode;
            }
            
            // Check functions
            if (functions.count(cmdName)) {
                for (auto& stmt : functions[cmdName]) {
                    lastExitCode = execute(stmt);
                }
                return lastExitCode;
            }
            
            // External command
            lastExitCode = executeExternal(expandedArgs);
            return lastExitCode;
        }
        
        // Pipeline
        if (auto pipeline = std::dynamic_pointer_cast<PipelineNode>(node)) {
            if (pipeline->commands.size() == 1) {
                return execute(pipeline->commands[0]);
            }
            
            // For multi-command pipeline, build a command line with pipes
            std::string fullCmd;
            for (size_t i = 0; i < pipeline->commands.size(); i++) {
                if (i > 0) fullCmd += " | ";
                for (const auto& arg : pipeline->commands[i]->args) {
                    fullCmd += expandVariables(arg) + " ";
                }
            }
            lastExitCode = system(fullCmd.c_str());
            return lastExitCode;
        }
        
        // If statement
        if (auto ifNode = std::dynamic_pointer_cast<IfNode>(node)) {
            int condResult = execute(ifNode->condition);
            
            if (condResult == 0) {
                for (auto& stmt : ifNode->thenBody) {
                    lastExitCode = execute(stmt);
                }
            } else {
                for (auto& stmt : ifNode->elseBody) {
                    lastExitCode = execute(stmt);
                }
            }
            return lastExitCode;
        }
        
        // For loop
        if (auto forNode = std::dynamic_pointer_cast<ForNode>(node)) {
            for (const auto& value : forNode->values) {
                variables[forNode->variable] = expandVariables(value);
                for (auto& stmt : forNode->body) {
                    lastExitCode = execute(stmt);
                }
            }
            return lastExitCode;
        }
        
        // While loop
        if (auto whileNode = std::dynamic_pointer_cast<WhileNode>(node)) {
            while (execute(whileNode->condition) == 0) {
                for (auto& stmt : whileNode->body) {
                    lastExitCode = execute(stmt);
                }
            }
            return lastExitCode;
        }
        
        // Function definition
        if (auto funcNode = std::dynamic_pointer_cast<FunctionNode>(node)) {
            functions[funcNode->name] = funcNode->body;
            return 0;
        }
        
        return 0;
    }
    
    int run(const std::vector<std::shared_ptr<ASTNode>>& program) {
        for (auto& node : program) {
            execute(node);
        }
        return lastExitCode;
    }
};

// ============================================================================
// INTERPRETER - Main entry point
// ============================================================================
class Interpreter {
private:
    Executor executor;
    bool debugMode = false;
    
public:
    void setDebug(bool debug) { 
        debugMode = debug; 
        executor.setDebug(debug);
    }
    
    int runScript(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) {
            std::cerr << "lish: " << filename << ": No such file\n";
            return 1;
        }
        
        // Read first line to check shebang
        std::string firstLine;
        std::getline(file, firstLine);
        
        // Check for shebang
        if (firstLine.size() > 2 && firstLine[0] == '#' && firstLine[1] == '!') {
            std::string shebang = firstLine.substr(2);
            // Trim whitespace
            shebang.erase(0, shebang.find_first_not_of(" \t\r\n"));
            shebang.erase(shebang.find_last_not_of(" \t\r\n") + 1);
            
            // Get interpreter name
            size_t spacePos = shebang.find(' ');
            std::string interpreterSpec = (spacePos != std::string::npos) ? shebang.substr(0, spacePos) : shebang;
            
            // Get just the filename for comparison
            std::string interpreterName = interpreterSpec;
            size_t lastSlash = interpreterSpec.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                interpreterName = interpreterSpec.substr(lastSlash + 1);
            }
            std::transform(interpreterName.begin(), interpreterName.end(), interpreterName.begin(), ::tolower);
            
            // Remove .exe if present
            if (interpreterName.size() > 4 && interpreterName.substr(interpreterName.size() - 4) == ".exe") {
                interpreterName = interpreterName.substr(0, interpreterName.size() - 4);
            }
            
            // If it's not lish/bash/sh, hand off to the other interpreter
            if (interpreterName != "lish" && interpreterName != "bash" && interpreterName != "sh") {
                // Find the interpreter
                std::string interpreterPath;
                
                // Check if absolute path exists
                if (GetFileAttributesA(interpreterSpec.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    interpreterPath = interpreterSpec;
                } else {
                    // Try to find in PATH
                    char pathBuf[MAX_PATH];
                    DWORD result = SearchPathA(NULL, interpreterName.c_str(), ".exe", MAX_PATH, pathBuf, NULL);
                    if (result > 0) {
                        interpreterPath = pathBuf;
                    }
                }
                
                if (!interpreterPath.empty()) {
                    // Execute with the other interpreter
                    std::string cmdLine = "\"" + interpreterPath + "\" \"" + filename + "\"";
                    
                    STARTUPINFOA si;
                    PROCESS_INFORMATION pi;
                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESTDHANDLES;
                    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                    ZeroMemory(&pi, sizeof(pi));
                    
                    char cmdBuffer[4096];
                    strncpy_s(cmdBuffer, cmdLine.c_str(), sizeof(cmdBuffer) - 1);
                    
                    if (CreateProcessA(NULL, cmdBuffer, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                        WaitForSingleObject(pi.hProcess, INFINITE);
                        DWORD exitCode;
                        GetExitCodeProcess(pi.hProcess, &exitCode);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        return (int)exitCode;
                    } else {
                        ShellIO::serr << "lish: cannot execute interpreter: " << interpreterSpec << ShellIO::endl;
                        return 127;
                    }
                } else {
                    ShellIO::serr << "lish: interpreter not found: " << interpreterSpec << ShellIO::endl;
                    return 127;
                }
            }
        }
        
        // Reset file and read all content
        file.clear();
        file.seekg(0);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return runCode(buffer.str());
    }
    
    int runCode(const std::string& code) {
        try {
            Lexer lexer(code);
            auto tokens = lexer.tokenize();
            
            if (debugMode) {
                std::cout << "[DEBUG] Tokens:\n";
                for (const auto& t : tokens) {
                    std::cout << "  " << t.typeName() << ": " << t.value << "\n";
                }
            }
            
            Parser parser(tokens);
            auto program = parser.parse();
            
            if (debugMode) {
                std::cout << "[DEBUG] AST Nodes: " << program.size() << "\n";
            }
            
            return executor.run(program);
        } catch (const std::exception& e) {
            ShellIO::serr << "lish: error: " << e.what() << ShellIO::endl;
            return 1;
        }
    }
    
    void interactive() {
        // Print header
        ShellIO::sout << ShellIO::Color::LightGreen << "Linuxify Shell (lish) - Interactive Mode\n" << ShellIO::Color::Reset;
        ShellIO::sout << "Type 'exit' to quit, 'help' for commands.\n\n";
        
        std::string line;
        
        while (true) {
            // Get current directory
            char cwd[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, cwd);
            
            // Print colored prompt
            ShellIO::sout << ShellIO::Color::LightGreen << "lish" << ShellIO::Color::Reset << ":";
            ShellIO::sout << ShellIO::Color::LightBlue << cwd << ShellIO::Color::Reset << "$ ";
            
            if (!ShellIO::sin.getline(line)) break;
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            if (line.find_last_not_of(" \t\r\n") != std::string::npos) {
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
            }
            
            if (line.empty()) continue;
            if (line == "exit") break;
            
            runCode(line);
            ShellIO::sout << ShellIO::endl;
        }
    }
};

} // namespace Bash
