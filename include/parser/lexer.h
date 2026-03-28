#pragma once
#include "parser/token.h"
#include <cstddef>
#include <string>
#include <vector>

// Converts a SQL string into a flat token stream.
class Lexer {
public:
    explicit Lexer(const std::string& input);
    std::vector<Token> tokenize();

private:
    std::string input;
    size_t pos{0};

    char peek() const;
    char advance();
    void skipWhitespace();

    Token identifier();
    Token number();
    Token string();
};