#pragma once
#include <memory>
#include <string>
#include <vector>

// Base type for expression nodes used in AST and plan predicates.
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

// Parsed SELECT statement container.
struct SelectStmt {
    std::vector<std::unique_ptr<Expr>> columns;
    std::string table;
    std::unique_ptr<Expr> where;
    std::string orderBy;
    std::vector<std::unique_ptr<Expr>> groupBy;
    std::unique_ptr<Expr> having;
    int limit = -1;
};

// Add this class definition in src/parser/ast.h
class AggregateExpr : public Expr {
public:
    std::string funcName;       // "SUM", "COUNT", "AVG", etc.
    std::unique_ptr<Expr> arg;  // The expression inside: e.g., 'salary' in SUM(salary)

    AggregateExpr(std::string func, std::unique_ptr<Expr> argument)
        : funcName(std::move(func)), arg(std::move(argument)) {}

    // Ensure you have a clone method if your engine uses it for planning
    std::unique_ptr<Expr> clone() const override {
        return std::make_unique<AggregateExpr>(
            funcName, 
            arg ? arg->clone() : nullptr
        );
    }
};