// Linuxify Arithmetic Expression Evaluator
// Supports: +, -, *, /, ^, () with proper operator precedence
// Compile: g++ -std=c++17 -static -o linuxify.exe main.cpp registry.cpp -lpsapi -lws2_32 -liphlpapi -lwininet -lwlanapi 2>&1

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace Arith {

enum class TokenType {
    NUMBER,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    POWER,
    LPAREN,
    RPAREN,
    END
};

struct Token {
    TokenType type;
    double value;
    
    Token(TokenType t, double v = 0) : type(t), value(v) {}
};

class Tokenizer {
private:
    std::string input;
    size_t pos;
    
    char current() {
        if (pos >= input.length()) return '\0';
        return input[pos];
    }
    
    void advance() {
        if (pos < input.length()) pos++;
    }
    
    void skipWhitespace() {
        while (pos < input.length() && std::isspace(current())) {
            advance();
        }
    }
    
    double readNumber() {
        size_t start = pos;
        bool hasDecimal = false;
        
        if (current() == '-' || current() == '+') {
            advance();
        }
        
        while (std::isdigit(current()) || current() == '.') {
            if (current() == '.') {
                if (hasDecimal) break;
                hasDecimal = true;
            }
            advance();
        }
        
        std::string numStr = input.substr(start, pos - start);
        return std::stod(numStr);
    }
    
public:
    Tokenizer(const std::string& expr) : input(expr), pos(0) {}
    
    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        
        while (pos < input.length()) {
            skipWhitespace();
            
            if (pos >= input.length()) break;
            
            char c = current();
            
            if (std::isdigit(c) || c == '.') {
                tokens.push_back(Token(TokenType::NUMBER, readNumber()));
            }
            else if (c == '+') {
                tokens.push_back(Token(TokenType::PLUS));
                advance();
            }
            else if (c == '-') {
                if (tokens.empty() || 
                    tokens.back().type == TokenType::LPAREN ||
                    tokens.back().type == TokenType::PLUS ||
                    tokens.back().type == TokenType::MINUS ||
                    tokens.back().type == TokenType::MULTIPLY ||
                    tokens.back().type == TokenType::DIVIDE ||
                    tokens.back().type == TokenType::POWER) {
                    tokens.push_back(Token(TokenType::NUMBER, readNumber()));
                } else {
                    tokens.push_back(Token(TokenType::MINUS));
                    advance();
                }
            }
            else if (c == '*') {
                tokens.push_back(Token(TokenType::MULTIPLY));
                advance();
            }
            else if (c == '/') {
                tokens.push_back(Token(TokenType::DIVIDE));
                advance();
            }
            else if (c == '(') {
                tokens.push_back(Token(TokenType::LPAREN));
                advance();
            }
            else if (c == ')') {
                tokens.push_back(Token(TokenType::RPAREN));
                advance();
            }
            else if (c == '^') {
                tokens.push_back(Token(TokenType::POWER));
                advance();
            }
            else {
                throw std::runtime_error("Invalid character in expression: " + std::string(1, c));
            }
        }
        
        tokens.push_back(Token(TokenType::END));
        return tokens;
    }
};

class Parser {
private:
    std::vector<Token> tokens;
    size_t pos;
    
    Token& current() {
        return tokens[pos];
    }
    
    void advance() {
        if (pos < tokens.size() - 1) pos++;
    }
    
    bool match(TokenType type) {
        if (current().type == type) {
            advance();
            return true;
        }
        return false;
    }
    
    double parseExpression() {
        return parseAddSub();
    }
    
    double parseAddSub() {
        double left = parseMulDiv();
        
        while (current().type == TokenType::PLUS || current().type == TokenType::MINUS) {
            TokenType op = current().type;
            advance();
            double right = parseMulDiv();
            
            if (op == TokenType::PLUS) {
                left = left + right;
            } else {
                left = left - right;
            }
        }
        
        return left;
    }
    
    double parseMulDiv() {
        double left = parsePower();
        
        while (current().type == TokenType::MULTIPLY || current().type == TokenType::DIVIDE) {
            TokenType op = current().type;
            advance();
            double right = parsePower();
            
            if (op == TokenType::MULTIPLY) {
                left = left * right;
            } else {
                if (right == 0) {
                    throw std::runtime_error("Division by zero");
                }
                left = left / right;
            }
        }
        
        return left;
    }
    
    double parsePower() {
        double base = parsePrimary();
        
        if (current().type == TokenType::POWER) {
            advance();
            double exponent = parsePower();
            return std::pow(base, exponent);
        }
        
        return base;
    }
    
    double parsePrimary() {
        if (current().type == TokenType::NUMBER) {
            double value = current().value;
            advance();
            return value;
        }
        
        if (current().type == TokenType::LPAREN) {
            advance();
            double value = parseExpression();
            
            if (current().type != TokenType::RPAREN) {
                throw std::runtime_error("Missing closing parenthesis");
            }
            advance();
            return value;
        }
        
        if (current().type == TokenType::MINUS) {
            advance();
            return -parsePrimary();
        }
        
        if (current().type == TokenType::PLUS) {
            advance();
            return parsePrimary();
        }
        
        throw std::runtime_error("Unexpected token in expression");
    }
    
public:
    Parser(const std::vector<Token>& toks) : tokens(toks), pos(0) {}
    
    double parse() {
        double result = parseExpression();
        
        if (current().type != TokenType::END) {
            throw std::runtime_error("Unexpected token after expression");
        }
        
        return result;
    }
};

inline bool isArithmeticExpression(const std::string& input) {
    if (input.empty()) return false;
    
    bool hasOperator = false;
    bool hasDigit = false;
    int parenDepth = 0;
    
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        
        if (std::isdigit(c) || c == '.') {
            hasDigit = true;
        }
        else if (c == '+' || c == '*' || c == '/' || c == '^') {
            hasOperator = true;
        }
        else if (c == '-') {
            if (i > 0 && (std::isdigit(input[i-1]) || input[i-1] == ')')) {
                hasOperator = true;
            }
        }
        else if (c == '(') {
            parenDepth++;
        }
        else if (c == ')') {
            parenDepth--;
            if (parenDepth < 0) return false;
        }
        else if (std::isspace(c)) {
            continue;
        }
        else {
            return false;
        }
    }
    
    return hasDigit && (hasOperator || parenDepth == 0) && parenDepth == 0;
}

inline std::string evaluate(const std::string& expression) {
    if (expression.empty()) {
        throw std::runtime_error("Empty expression");
    }
    
    Tokenizer tokenizer(expression);
    std::vector<Token> tokens = tokenizer.tokenize();
    
    Parser parser(tokens);
    double result = parser.parse();
    
    if (result == std::floor(result) && std::abs(result) < 1e15) {
        return std::to_string(static_cast<long long>(result));
    }
    
    std::ostringstream oss;
    oss << std::setprecision(10) << result;
    std::string str = oss.str();
    
    size_t dotPos = str.find('.');
    if (dotPos != std::string::npos) {
        size_t lastNonZero = str.find_last_not_of('0');
        if (lastNonZero != std::string::npos && lastNonZero > dotPos) {
            str = str.substr(0, lastNonZero + 1);
        } else if (lastNonZero == dotPos) {
            str = str.substr(0, dotPos);
        }
    }
    
    return str;
}

inline double evaluateToDouble(const std::string& expression) {
    Tokenizer tokenizer(expression);
    std::vector<Token> tokens = tokenizer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

}
