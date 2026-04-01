#include "parser/parser.h"

#include <limits>
#include <stdexcept>
#include <string>

// Parser implementation for SELECT ... FROM ... WHERE ... shape.

namespace {
std::string TokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::SELECT:
            return "SELECT";
        case TokenType::FROM:
            return "FROM";
        case TokenType::WHERE:
            return "WHERE";
        case TokenType::ORDER:
            return "ORDER";
        case TokenType::BY:
            return "BY";
        case TokenType::LIMIT:
            return "LIMIT";
        case TokenType::IDENT:
            return "IDENT";
        case TokenType::NUMBER:
            return "NUMBER";
        case TokenType::STRING:
            return "STRING";
        case TokenType::OP:
            return "OP";
        case TokenType::COMMA:
            return "COMMA";
        case TokenType::STAR:
            return "STAR";
        case TokenType::END:
            return "END";
        case TokenType::GROUP:  
            return "GROUP";  
        case TokenType::HAVING: 
            return "HAVING";
    }

    return "UNKNOWN";
}
}  // namespace

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), pos(0) {}

const Token& Parser::peek() const {
    if (pos >= tokens.size()) {
        throw std::runtime_error("Unexpected end of input while parsing");
    }
    return tokens[pos];
}

const Token& Parser::consume(TokenType type) {
    const Token& current = peek();
    if (current.type != type) {
        throw std::runtime_error(
            "Unexpected token. Expected " + TokenTypeToString(type) + " but got " +
            TokenTypeToString(current.type) +
            (current.value.empty() ? std::string() : " ('" + current.value + "')"));
    }
    return tokens[pos++];
}

SelectStmt Parser::parse() {
    // Parse one full statement and ensure no extra trailing tokens.
    SelectStmt stmt = parseSelect();
    consume(TokenType::END);
    return stmt;
}

std::vector<std::unique_ptr<Expr>> Parser::parseGroupBy() {
    std::vector<std::unique_ptr<Expr>> cols;

    while (true) {
        cols.push_back(std::make_unique<Column>(consume(TokenType::IDENT).value));

        if (peek().type == TokenType::COMMA) {
            consume(TokenType::COMMA);
        } else break;
    }

    return cols;
}

SelectStmt Parser::parseSelect() {
    consume(TokenType::SELECT);

    auto columns = parseColumns();

    consume(TokenType::FROM);
    std::string table = consume(TokenType::IDENT).value;

    std::unique_ptr<Expr> where = nullptr;
    std::vector<std::unique_ptr<Expr>> groupBy;  
    std::unique_ptr<Expr> having = nullptr;
    std::string orderBy;
    int limit = -1;

    if (peek().type == TokenType::WHERE) {
        consume(TokenType::WHERE);
        where = parseExpression();
    }

    if (peek().type == TokenType::GROUP) {
        consume(TokenType::GROUP);             
        consume(TokenType::BY);                
        groupBy = parseGroupBy();                
    }

    if (peek().type == TokenType::HAVING) {
        consume(TokenType::HAVING);
        having = parseExpression();
    }

    if (peek().type == TokenType::ORDER) {
        consume(TokenType::ORDER);
        consume(TokenType::BY);
        orderBy = consume(TokenType::IDENT).value;
    }

    if (peek().type == TokenType::LIMIT) {
        consume(TokenType::LIMIT);

        long long parsedLimit = 0;
        const auto& limitToken = consume(TokenType::NUMBER);

        try {
            parsedLimit = std::stoll(limitToken.value);
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid LIMIT value: '" + limitToken.value + "'");
        }

        if (parsedLimit < 0 || parsedLimit > std::numeric_limits<int>::max()) {
            throw std::runtime_error("LIMIT value is out of range: '" + limitToken.value + "'");
        }

        limit = static_cast<int>(parsedLimit);
    }

    SelectStmt stmt;
    stmt.columns = std::move(columns);
    stmt.table = std::move(table);
    stmt.where = std::move(where);
    stmt.groupBy = std::move(groupBy);
    stmt.having = std::move(having);
    stmt.orderBy = std::move(orderBy);
    stmt.limit = limit;

    return stmt;
}

std::vector<std::unique_ptr<Expr>> Parser::parseColumns() {
    std::vector<std::unique_ptr<Expr>> cols;

    if (peek().type == TokenType::STAR) {
        consume(TokenType::STAR);
        cols.push_back(std::make_unique<Column>("*"));
        return cols;
    }

    while (true) {
        cols.push_back(std::make_unique<Column>(consume(TokenType::IDENT).value));

        if (peek().type == TokenType::COMMA) {
            consume(TokenType::COMMA);
        } else break;
    }

    return cols;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    // Current grammar supports chained binary comparisons only.
    auto left = parseTerm();

    while (peek().type == TokenType::OP) {
        std::string op = consume(TokenType::OP).value;
        auto right = parseTerm();
        left = std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expr> Parser::parseTerm() {
    const Token& t = peek();

    if (t.type == TokenType::IDENT) {
        return std::make_unique<Column>(consume(TokenType::IDENT).value);
    }
    else if (t.type == TokenType::NUMBER || t.type == TokenType::STRING) {
        return std::make_unique<Literal>(consume(t.type).value);
    }

    throw std::runtime_error("Invalid expression starting with token: " + TokenTypeToString(t.type));
}