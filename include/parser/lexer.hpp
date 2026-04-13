#pragma once
#include "parser/token.hpp"
#include <cstddef>
#include <string>
#include <vector>

// Converts a SQL string into a flat token stream.
class Lexer {
private:
    std::string input_;
    std::size_t pos_{0};
    std::size_t line_{1};
    std::size_t column_{1};

    char peek() const noexcept;
    char advance() noexcept;
    void skipWhitespace() noexcept;
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanString();
    Token scanOperator();
    
public:
    explicit Lexer(std::string input);
    std::vector<Token> tokenize();
};