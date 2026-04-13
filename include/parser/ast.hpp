#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Expression AST nodes
struct Expr {
    virtual ~Expr() = default;
    virtual std::unique_ptr<Expr> clone() const = 0;
};

struct Column : Expr {
    std::string name;

    explicit Column(std::string n);
    std::unique_ptr<Expr> clone() const override;
};

struct Literal : Expr {
    std::string value;

    explicit Literal(std::string v);
    std::unique_ptr<Expr> clone() const override;
};

struct BinaryExpr : Expr {
    std::unique_ptr<Expr> left;
    std::string op;
    std::unique_ptr<Expr> right;

    BinaryExpr(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r);
    std::unique_ptr<Expr> clone() const override;
};

enum class JoinType {
    INNER,
    LEFT,
    RIGHT,
    FULL,
    CROSS,
};

struct TableRef {
    std::string table;
    std::string alias;

    std::string effectiveName() const {
        return alias.empty() ? table : alias;
    }
};

struct JoinClause {
    JoinType type{JoinType::INNER};
    TableRef right;
    std::unique_ptr<Expr> condition;
};

// Statement AST nodes
struct SelectStmt {
    TableRef from;
    std::vector<JoinClause> joins;
    bool distinct{false};

    std::vector<std::unique_ptr<Expr>> columns;

    // Backward-compatibility convenience field for single-table paths.
    std::string table;

    std::unique_ptr<Expr> where;
    std::string orderBy;
    std::vector<std::unique_ptr<Expr>> groupBy;
    std::unique_ptr<Expr> having;
    int limit = -1;
};

struct ColumnDef {
    std::string name;
    std::string type;
    bool primaryKey = false;
    bool unique = false;
    bool notNull = false;
    std::optional<std::string> foreignKey;
};

struct CreateTableStmt {
    std::string table;
    std::vector<ColumnDef> columns;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> columns;
    std::vector<std::vector<std::unique_ptr<Expr>>> valueRows;
};

struct UpdateAssignment {
    std::string column;
    std::unique_ptr<Expr> value;
};

struct UpdateStmt {
    std::string table;
    std::vector<UpdateAssignment> assignments;
    std::unique_ptr<Expr> where;
};

struct DeleteStmt {
    std::string table;
    std::unique_ptr<Expr> where;
};

// Top-level statement container
enum class StatementType { 
    SELECT, 
    CREATE_TABLE, 
    INSERT, 
    UPDATE, 
    DELETE_ 
};

struct Statement {
    StatementType type{StatementType::SELECT};
    std::unique_ptr<SelectStmt> select;
    std::unique_ptr<CreateTableStmt> createTable;
    std::unique_ptr<InsertStmt> insert;
    std::unique_ptr<UpdateStmt> update;
    std::unique_ptr<DeleteStmt> deleteStmt;
};