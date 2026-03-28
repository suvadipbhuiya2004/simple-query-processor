#include "parser/ast.h"

#include <utility>

// Small constructors and clone helpers for AST nodes.

Column::Column(std::string n) : name(std::move(n)) {}

std::unique_ptr<Expr> Column::clone() const {
    return std::make_unique<Column>(name);
}

Literal::Literal(std::string v) : value(std::move(v)) {}

std::unique_ptr<Expr> Literal::clone() const {
    return std::make_unique<Literal>(value);
}

BinaryExpr::BinaryExpr(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
    : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}

std::unique_ptr<Expr> BinaryExpr::clone() const {
    // Deep copy so planner/execution can safely own independent trees.
    return std::make_unique<BinaryExpr>(left->clone(), op, right->clone());
}
