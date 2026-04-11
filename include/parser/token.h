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
    AND,
    OR,
    NUMBER, 
    STRING,
    OP, 
    COMMA, 
    STAR,
    LPAREN,
    RPAREN,
    IDENT, 
    END,
    SUM,
    AVG,
    COUNT,
    MIN,
    MAX
};

struct Token {
    TokenType type;
    std::string value;
};