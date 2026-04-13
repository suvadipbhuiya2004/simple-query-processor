#pragma once
#include "parser/ast.hpp"
#include "parser/token.hpp"
#include <cstddef>
#include <vector>

//------------------------------------------------------------------------
// Parser: converts a token stream into a Statement AST.
// Supports SELECT / CREATE TABLE / INSERT / UPDATE / DELETE.
//------------------------------------------------------------------------
class Parser {
private:
    std::vector<Token> tokens_;
    std::size_t pos_{0};

    const Token& peek() const;
    const Token& consume(TokenType expected);
    bool check(TokenType type) const noexcept;
    bool match(TokenType type) noexcept;

    // Statement parsers
    SelectStmt parseSelect();
    CreateTableStmt parseCreateTable();
    InsertStmt parseInsert();
    UpdateStmt parseUpdate();
    DeleteStmt parseDelete();

    // CREATE TABLE helpers
    ColumnDef parseColumnDef();
    void parseOptionalTypeModifier(ColumnDef& col);
    bool parseOptionalColumnConstraint(ColumnDef& col);
    bool parseTableConstraint(std::vector<ColumnDef>& cols);
    std::string parseForeignKeyReference();
    std::vector<std::string> parseIdentifierList();

    // INSERT helpers
    std::vector<std::unique_ptr<Expr>> parseValueList();

    // UPDATE helpers
    std::vector<UpdateAssignment> parseAssignments();

    // SELECT helpers
    std::vector<std::unique_ptr<Expr>> parseSelectColumns();
    std::vector<std::unique_ptr<Expr>> parseGroupByColumns();
    TableRef parseTableRef();
    JoinClause parseJoinClause();
    std::string parseQualifiedIdentifier();

    // Expression parsing (Pratt / recursive descent)
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseOr();
    std::unique_ptr<Expr> parseAnd();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseTerm();

public:
    explicit Parser(std::vector<Token> tokens);

    // Parse a full SELECT statement
    SelectStmt parse();

    // Parse any supported statement type.
    Statement parseStatement();
};