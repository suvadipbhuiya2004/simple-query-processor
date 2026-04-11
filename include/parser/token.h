#pragma once
#include <string>

// Token kinds produced by the lexer.
enum class TokenType {
    SELECT, 
    FROM, 
    WHERE, 
    ORDER, 
    GROUP,
    HAVING,
    BY, 
    LIMIT,
    NUMBER, 
    STRING,
    OP, 
    COMMA, 
    STAR,
    IDENT, 
    END,
    SUM,
    AVG,
    COUNT,
    MIN,
    MAX,
    LPAREN,  
    RPAREN
};

struct Token {
    TokenType type;
    std::string value;
};