#pragma once
#include "parser/token.h"
#include "parser/ast.h"
#include <cstddef>
#include <vector>

// Parses token stream into SelectStmt AST.
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    SelectStmt parse();

private:
    const std::vector<Token>& tokens;
    size_t pos{0};

    bool match(TokenType type);
    const Token& previous() const;
    bool isAtEnd() const;

    const Token& peek() const;
    const Token& consume(TokenType type);

    SelectStmt parseSelect();
    std::vector<std::unique_ptr<Expr>> parseColumns();
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseTerm();
    std::vector<std::unique_ptr<Expr>> parseGroupBy();

    std::unique_ptr<Expr> parsePrimary();
};