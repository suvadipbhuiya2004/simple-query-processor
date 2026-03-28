#pragma once
#include <string>

// Token kinds produced by the lexer.
enum class TokenType {
    SELECT, 
    FROM, 
    WHERE, 
    ORDER, 
    BY, 
    LIMIT,
    NUMBER, 
    STRING,
    OP, 
    COMMA, 
    STAR,
    IDENT, 
    END
};

struct Token {
    TokenType type;
    std::string value;
};