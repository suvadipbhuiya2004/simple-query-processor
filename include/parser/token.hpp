#pragma once
#include <cstddef>
#include <string>


enum class TokenType {
    // DDL / DML keywords
    CREATE, TABLE,
    INSERT, INTO, VALUES,
    UPDATE, SET,
    DELETE_, DROP,
    PRIMARY, KEY, FOREIGN, REFERENCES,

    // Query keywords
    SELECT, FROM, WHERE,
    DISTINCT,
    JOIN, INNER, LEFT, RIGHT, FULL, OUTER, CROSS, ON, AS,
    ORDER, GROUP, HAVING, BY, LIMIT,

    // Logical operators
    AND, OR,

    // Literals and identifiers
    NUMBER, STRING, IDENT,

    // Punctuation / operators
    OP,      // =, !=, <>, <, >, <=, >=
    COMMA,
    STAR,
    LPAREN,  // (
    RPAREN,  // )
    DOT,

    END
};

struct Token {
    TokenType type;
    std::string value;
    std::size_t line{1};
    std::size_t column{1};
};