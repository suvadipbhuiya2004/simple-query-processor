#include "parser/parser.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

    std::string tokenTypeName(TokenType t)
    {
        switch (t)
        {
        case TokenType::CREATE:
            return "CREATE";
        case TokenType::TABLE:
            return "TABLE";
        case TokenType::INSERT:
            return "INSERT";
        case TokenType::INTO:
            return "INTO";
        case TokenType::VALUES:
            return "VALUES";
        case TokenType::UPDATE:
            return "UPDATE";
        case TokenType::SET:
            return "SET";
        case TokenType::DELETE_:
            return "DELETE";
        case TokenType::DROP:
            return "DROP";
        case TokenType::PRIMARY:
            return "PRIMARY";
        case TokenType::KEY:
            return "KEY";
        case TokenType::FOREIGN:
            return "FOREIGN";
        case TokenType::REFERENCES:
            return "REFERENCES";
        case TokenType::DOT:
            return "DOT";
        case TokenType::SELECT:
            return "SELECT";
        case TokenType::FROM:
            return "FROM";
        case TokenType::WHERE:
            return "WHERE";
        case TokenType::DISTINCT:
            return "DISTINCT";
        case TokenType::JOIN:
            return "JOIN";
        case TokenType::INNER:
            return "INNER";
        case TokenType::LEFT:
            return "LEFT";
        case TokenType::RIGHT:
            return "RIGHT";
        case TokenType::FULL:
            return "FULL";
        case TokenType::OUTER:
            return "OUTER";
        case TokenType::CROSS:
            return "CROSS";
        case TokenType::ON:
            return "ON";
        case TokenType::AS:
            return "AS";
        case TokenType::ORDER:
            return "ORDER";
        case TokenType::GROUP:
            return "GROUP";
        case TokenType::HAVING:
            return "HAVING";
        case TokenType::BY:
            return "BY";
        case TokenType::LIMIT:
            return "LIMIT";
        case TokenType::AND:
            return "AND";
        case TokenType::OR:
            return "OR";
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
        case TokenType::LPAREN:
            return "LPAREN";
        case TokenType::RPAREN:
            return "RPAREN";
        case TokenType::IDENT:
            return "IDENT";
        case TokenType::END:
            return "END";
        }
        return "UNKNOWN";
    }

    std::string tokenLocation(const Token &t)
    {
        return " at line " + std::to_string(t.line) +
               ", column " + std::to_string(t.column);
    }

    std::string toUpper(std::string s)
    {
        for (char &c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token &Parser::peek() const
{
    if (pos_ >= tokens_.size())
        throw std::runtime_error("Unexpected end of input while parsing");
    return tokens_[pos_];
}

bool Parser::check(TokenType type) const noexcept
{
    return pos_ < tokens_.size() && tokens_[pos_].type == type;
}

bool Parser::match(TokenType type) noexcept
{
    if (check(type))
    {
        ++pos_;
        return true;
    }
    return false;
}

const Token &Parser::consume(TokenType expected)
{
    const Token &cur = peek();
    if (cur.type != expected)
    {
        std::string msg = "Expected " + tokenTypeName(expected) +
                          " but got " + tokenTypeName(cur.type) +
                          tokenLocation(cur);
        if (!cur.value.empty())
            msg += " ('" + cur.value + "')";
        throw std::runtime_error(std::move(msg));
    }
    return tokens_[pos_++];
}

SelectStmt Parser::parse()
{
    if (!check(TokenType::SELECT))
        throw std::runtime_error("parse() only accepts SELECT" + tokenLocation(peek()));
    SelectStmt stmt = parseSelect();
    consume(TokenType::END);
    return stmt;
}

Statement Parser::parseStatement()
{
    Statement stmt;
    switch (peek().type)
    {
    case TokenType::SELECT:
        stmt.type = StatementType::SELECT;
        stmt.select = std::make_unique<SelectStmt>(parseSelect());
        break;
    case TokenType::CREATE:
        stmt.type = StatementType::CREATE_TABLE;
        stmt.createTable = std::make_unique<CreateTableStmt>(parseCreateTable());
        break;
    case TokenType::INSERT:
        stmt.type = StatementType::INSERT;
        stmt.insert = std::make_unique<InsertStmt>(parseInsert());
        break;
    case TokenType::UPDATE:
        stmt.type = StatementType::UPDATE;
        stmt.update = std::make_unique<UpdateStmt>(parseUpdate());
        break;
    case TokenType::DELETE_:
        stmt.type = StatementType::DELETE_;
        stmt.deleteStmt = std::make_unique<DeleteStmt>(parseDelete());
        break;
    default:
        throw std::runtime_error(
            "Unsupported statement: " + tokenTypeName(peek().type) + tokenLocation(peek()));
    }
    consume(TokenType::END);
    return stmt;
}

//  SELECT 

SelectStmt Parser::parseSelect()
{
    consume(TokenType::SELECT);
    const bool distinct = match(TokenType::DISTINCT);
    auto cols = parseSelectColumns();
    consume(TokenType::FROM);
    TableRef from = parseTableRef();

    std::vector<JoinClause> joins;
    while (check(TokenType::JOIN) || check(TokenType::INNER) ||
           check(TokenType::LEFT) || check(TokenType::RIGHT) ||
           check(TokenType::FULL) || check(TokenType::CROSS))
    {
        joins.push_back(parseJoinClause());
    }

    SelectStmt stmt;
    stmt.columns = std::move(cols);
    stmt.from = std::move(from);
    stmt.joins = std::move(joins);
    stmt.distinct = distinct;
    stmt.table = stmt.from.table;

    if (match(TokenType::WHERE))
        stmt.where = parseExpression();
    if (check(TokenType::GROUP))
    {
        consume(TokenType::GROUP);
        consume(TokenType::BY);
        stmt.groupBy = parseGroupByColumns();
    }
    if (match(TokenType::HAVING))
        stmt.having = parseExpression();
    if (check(TokenType::ORDER))
    {
        consume(TokenType::ORDER);
        consume(TokenType::BY);
        stmt.orderBy = parseQualifiedIdentifier();
    }
    if (match(TokenType::LIMIT))
    {
        const Token &limitTok = consume(TokenType::NUMBER);
        const std::string &raw = limitTok.value;
        long long n = 0;
        try
        {
            n = std::stoll(raw);
        }
        catch (...)
        {
            throw std::runtime_error(
                "Invalid LIMIT value: '" + raw + "'" + tokenLocation(limitTok));
        }
        if (n < 0 || n > std::numeric_limits<int>::max())
            throw std::runtime_error(
                "LIMIT out of range: '" + raw + "'" + tokenLocation(limitTok));
        stmt.limit = static_cast<int>(n);
    }
    return stmt;
}

std::vector<std::unique_ptr<Expr>> Parser::parseSelectColumns()
{
    std::vector<std::unique_ptr<Expr>> cols;
    if (check(TokenType::STAR))
    {
        consume(TokenType::STAR);
        cols.push_back(std::make_unique<Column>("*"));
        return cols;
    }
    while (true)
    {
        cols.push_back(std::make_unique<Column>(parseQualifiedIdentifier()));
        if (!match(TokenType::COMMA))
            break;
    }
    return cols;
}

std::vector<std::unique_ptr<Expr>> Parser::parseGroupByColumns()
{
    std::vector<std::unique_ptr<Expr>> cols;
    while (true)
    {
        cols.push_back(std::make_unique<Column>(parseQualifiedIdentifier()));
        if (!match(TokenType::COMMA))
            break;
    }
    return cols;
}

std::string Parser::parseQualifiedIdentifier()
{
    std::string name = consume(TokenType::IDENT).value;
    while (match(TokenType::DOT))
    {
        name += "." + consume(TokenType::IDENT).value;
    }
    return name;
}

TableRef Parser::parseTableRef()
{
    TableRef ref;
    ref.table = consume(TokenType::IDENT).value;

    if (match(TokenType::AS))
    {
        ref.alias = consume(TokenType::IDENT).value;
    }
    else if (check(TokenType::IDENT))
    {
        ref.alias = consume(TokenType::IDENT).value;
    }
    return ref;
}

JoinClause Parser::parseJoinClause()
{
    JoinClause join;

    if (match(TokenType::INNER))
    {
        consume(TokenType::JOIN);
        join.type = JoinType::INNER;
    }
    else if (match(TokenType::LEFT))
    {
        match(TokenType::OUTER);
        consume(TokenType::JOIN);
        join.type = JoinType::LEFT;
    }
    else if (match(TokenType::RIGHT))
    {
        match(TokenType::OUTER);
        consume(TokenType::JOIN);
        join.type = JoinType::RIGHT;
    }
    else if (match(TokenType::FULL))
    {
        match(TokenType::OUTER);
        consume(TokenType::JOIN);
        join.type = JoinType::FULL;
    }
    else if (match(TokenType::CROSS))
    {
        consume(TokenType::JOIN);
        join.type = JoinType::CROSS;
    }
    else
    {
        consume(TokenType::JOIN);
        join.type = JoinType::INNER;
    }

    join.right = parseTableRef();

    if (join.type != JoinType::CROSS)
    {
        consume(TokenType::ON);
        join.condition = parseExpression();
    }

    return join;
}

//  CREATE TABLE --

CreateTableStmt Parser::parseCreateTable()
{
    consume(TokenType::CREATE);
    consume(TokenType::TABLE);

    // Optional: IF NOT EXISTS
    if (check(TokenType::IDENT) && toUpper(peek().value) == "IF")
    {
        consume(TokenType::IDENT);
        if (check(TokenType::IDENT) && toUpper(peek().value) == "NOT")
        {
            consume(TokenType::IDENT);
            if (check(TokenType::IDENT) && toUpper(peek().value) == "EXISTS")
                consume(TokenType::IDENT);
        }
    }

    const std::string table = consume(TokenType::IDENT).value;
    consume(TokenType::LPAREN);

    std::vector<ColumnDef> columns;
    while (!check(TokenType::RPAREN) && !check(TokenType::END))
    {
        if (!parseTableConstraint(columns))
            columns.push_back(parseColumnDef());
        if (!match(TokenType::COMMA))
            break;
    }
    consume(TokenType::RPAREN);

    CreateTableStmt stmt;
    stmt.table = table;
    stmt.columns = std::move(columns);
    return stmt;
}

ColumnDef Parser::parseColumnDef()
{
    ColumnDef col;
    col.name = consume(TokenType::IDENT).value;
    col.type = consume(TokenType::IDENT).value;
    parseOptionalTypeModifier(col);
    while (parseOptionalColumnConstraint(col))
    {
    }
    return col;
}

void Parser::parseOptionalTypeModifier(ColumnDef &col)
{
    if (!check(TokenType::LPAREN))
        return;
    consume(TokenType::LPAREN);
    col.type += '(';
    bool first = true;
    while (!check(TokenType::RPAREN) && !check(TokenType::END))
    {
        if (!first)
        {
            consume(TokenType::COMMA);
            col.type += ',';
        }
        const TokenType tt = peek().type;
        if (tt != TokenType::NUMBER && tt != TokenType::IDENT)
            throw std::runtime_error(
                "Expected NUMBER or IDENT in type modifier, got: " +
                tokenTypeName(tt) + tokenLocation(peek()));
        col.type += consume(tt).value;
        first = false;
    }
    consume(TokenType::RPAREN);
    col.type += ')';
}

bool Parser::parseOptionalColumnConstraint(ColumnDef &col)
{
    if (check(TokenType::PRIMARY))
    {
        consume(TokenType::PRIMARY);
        consume(TokenType::KEY);
        col.primaryKey = col.notNull = true;
        return true;
    }
    if (check(TokenType::REFERENCES))
    {
        consume(TokenType::REFERENCES);
        col.foreignKey = parseForeignKeyReference();
        return true;
    }
    if (check(TokenType::FOREIGN))
    {
        consume(TokenType::FOREIGN);
        match(TokenType::KEY);
        match(TokenType::REFERENCES);
        col.foreignKey = parseForeignKeyReference();
        return true;
    }
    if (check(TokenType::IDENT))
    {
        const std::string up = toUpper(peek().value);
        if (up == "UNIQUE")
        {
            consume(TokenType::IDENT);
            col.unique = true;
            return true;
        }
        if (up == "NOT")
        {
            consume(TokenType::IDENT);
            if (check(TokenType::IDENT) && toUpper(peek().value) == "NULL")
            {
                consume(TokenType::IDENT);
                col.notNull = true;
            }
            return true;
        }
        if (up == "NULL")
        {
            consume(TokenType::IDENT);
            col.notNull = false;
            return true;
        }
        if (up == "DEFAULT")
        {
            consume(TokenType::IDENT);
            const TokenType tt = peek().type;
            if (tt == TokenType::NUMBER || tt == TokenType::STRING || tt == TokenType::IDENT)
                consume(tt);
            return true;
        }
    }
    return false;
}

bool Parser::parseTableConstraint(std::vector<ColumnDef> &cols)
{
    if (check(TokenType::IDENT) && toUpper(peek().value) == "CONSTRAINT")
    {
        consume(TokenType::IDENT);
        consume(TokenType::IDENT);
    }
    if (check(TokenType::PRIMARY))
    {
        consume(TokenType::PRIMARY);
        consume(TokenType::KEY);
        for (const auto &pkName : parseIdentifierList())
        {
            const auto it = std::find_if(cols.begin(), cols.end(),
                                         [&](const ColumnDef &c)
                                         { return c.name == pkName; });
            if (it == cols.end())
                throw std::runtime_error(
                    "PRIMARY KEY references unknown column: " + pkName);
            it->primaryKey = it->notNull = true;
        }
        return true;
    }
    if (check(TokenType::FOREIGN))
    {
        consume(TokenType::FOREIGN);
        match(TokenType::KEY);
        auto localCols = parseIdentifierList();
        consume(TokenType::REFERENCES);
        const std::string refTable = consume(TokenType::IDENT).value;
        std::vector<std::string> refCols;
        if (check(TokenType::DOT))
        {
            consume(TokenType::DOT);
            refCols.push_back(consume(TokenType::IDENT).value);
        }
        else
        {
            refCols = parseIdentifierList();
        }
        if (localCols.size() == 1 && refCols.size() == 1)
        {
            const auto it = std::find_if(cols.begin(), cols.end(),
                                         [&](const ColumnDef &c)
                                         { return c.name == localCols.front(); });
            if (it != cols.end())
                it->foreignKey = refTable + "." + refCols.front();
        }
        return true;
    }
    return false;
}

std::vector<std::string> Parser::parseIdentifierList()
{
    consume(TokenType::LPAREN);
    std::vector<std::string> ids;
    while (true)
    {
        ids.push_back(consume(TokenType::IDENT).value);
        if (!match(TokenType::COMMA))
            break;
    }
    consume(TokenType::RPAREN);
    return ids;
}

std::string Parser::parseForeignKeyReference()
{
    const std::string refTable = consume(TokenType::IDENT).value;
    if (check(TokenType::DOT))
    {
        consume(TokenType::DOT);
        return refTable + "." + consume(TokenType::IDENT).value;
    }
    consume(TokenType::LPAREN);
    std::string refCol = consume(TokenType::IDENT).value;
    consume(TokenType::RPAREN);
    return refTable + "." + refCol;
}

//  INSERT 

InsertStmt Parser::parseInsert()
{
    consume(TokenType::INSERT);
    consume(TokenType::INTO);
    const std::string table = consume(TokenType::IDENT).value;

    std::vector<std::string> columns;
    if (check(TokenType::LPAREN))
    {
        consume(TokenType::LPAREN);
        while (true)
        {
            columns.push_back(consume(TokenType::IDENT).value);
            if (!match(TokenType::COMMA))
                break;
        }
        consume(TokenType::RPAREN);
    }

    consume(TokenType::VALUES);
    std::vector<std::vector<std::unique_ptr<Expr>>> rows;
    rows.push_back(parseValueList());
    while (match(TokenType::COMMA))
        rows.push_back(parseValueList());

    InsertStmt stmt;
    stmt.table = table;
    stmt.columns = std::move(columns);
    stmt.valueRows = std::move(rows);
    return stmt;
}

std::vector<std::unique_ptr<Expr>> Parser::parseValueList()
{
    consume(TokenType::LPAREN);
    std::vector<std::unique_ptr<Expr>> vals;
    while (true)
    {
        const TokenType tt = peek().type;
        if (tt == TokenType::NUMBER || tt == TokenType::STRING || tt == TokenType::IDENT)
            vals.push_back(std::make_unique<Literal>(consume(tt).value));
        else
            throw std::runtime_error(
                "Expected literal value in VALUES, got: " + tokenTypeName(tt) +
                tokenLocation(peek()));
        if (!match(TokenType::COMMA))
            break;
    }
    consume(TokenType::RPAREN);
    return vals;
}

//  UPDATE 

UpdateStmt Parser::parseUpdate()
{
    consume(TokenType::UPDATE);
    const std::string table = consume(TokenType::IDENT).value;
    consume(TokenType::SET);
    UpdateStmt stmt;
    stmt.table = table;
    stmt.assignments = parseAssignments();
    if (match(TokenType::WHERE))
        stmt.where = parseExpression();
    return stmt;
}

std::vector<UpdateAssignment> Parser::parseAssignments()
{
    std::vector<UpdateAssignment> asgns;
    while (true)
    {
        UpdateAssignment a;
        a.column = consume(TokenType::IDENT).value;
        const auto &op = consume(TokenType::OP);
        if (op.value != "=" && op.value != "==")
            throw std::runtime_error(
                "SET only supports '=', got: " + op.value + tokenLocation(op));
        a.value = parseExpression();
        asgns.push_back(std::move(a));
        if (!check(TokenType::COMMA))
            break;
        // Don't consume the comma if the next real token is WHERE/END
        if (pos_ + 1 < tokens_.size())
        {
            const TokenType lookahead = tokens_[pos_ + 1].type;
            if (lookahead == TokenType::WHERE || lookahead == TokenType::END)
                break;
        }
        match(TokenType::COMMA);
    }
    return asgns;
}

//  DELETE 

DeleteStmt Parser::parseDelete()
{
    consume(TokenType::DELETE_);
    consume(TokenType::FROM);
    DeleteStmt stmt;
    stmt.table = consume(TokenType::IDENT).value;
    if (match(TokenType::WHERE))
        stmt.where = parseExpression();
    return stmt;
}

//  Expressions ---

std::unique_ptr<Expr> Parser::parseExpression() { return parseOr(); }

std::unique_ptr<Expr> Parser::parseOr()
{
    auto left = parseAnd();
    while (check(TokenType::OR))
    {
        consume(TokenType::OR);
        auto right = parseAnd();
        left = std::make_unique<BinaryExpr>(std::move(left), "OR", std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAnd()
{
    auto left = parseComparison();
    while (check(TokenType::AND))
    {
        consume(TokenType::AND);
        auto right = parseComparison();
        left = std::make_unique<BinaryExpr>(std::move(left), "AND", std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseComparison()
{
    auto left = parseTerm();
    if (check(TokenType::OP))
    {
        std::string op = consume(TokenType::OP).value;
        auto right = parseTerm();
        return std::make_unique<BinaryExpr>(std::move(left), std::move(op),
                                            std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseTerm()
{
    const Token &t = peek();
    if (t.type == TokenType::LPAREN)
    {
        consume(TokenType::LPAREN);
        auto e = parseExpression();
        consume(TokenType::RPAREN);
        return e;
    }
    if (t.type == TokenType::IDENT)
        return std::make_unique<Column>(parseQualifiedIdentifier());
    if (t.type == TokenType::NUMBER)
        return std::make_unique<Literal>(consume(TokenType::NUMBER).value);
    if (t.type == TokenType::STRING)
        return std::make_unique<Literal>(consume(TokenType::STRING).value);

    throw std::runtime_error(
        "Unexpected token in expression: " + tokenTypeName(t.type) +
        (t.value.empty() ? "" : " ('" + t.value + "')") + tokenLocation(t));
}