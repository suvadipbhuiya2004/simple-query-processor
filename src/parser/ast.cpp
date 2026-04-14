#include "parser/ast.hpp"
#include <utility>

namespace
{

    std::vector<std::unique_ptr<Expr>> cloneExprVec(const std::vector<std::unique_ptr<Expr>> &src)
    {
        std::vector<std::unique_ptr<Expr>> out;
        out.reserve(src.size());
        for (const auto &e : src)
            out.push_back(e ? e->clone() : nullptr);
        return out;
    }

    std::unique_ptr<SelectStmt> cloneSelectStmt(const SelectStmt &src)
    {
        auto out = std::make_unique<SelectStmt>();
        out->from = src.from;
        out->joins.reserve(src.joins.size());
        for (const auto &j : src.joins)
        {
            JoinClause jc;
            jc.type = j.type;
            jc.right = j.right;
            jc.condition = j.condition ? j.condition->clone() : nullptr;
            out->joins.push_back(std::move(jc));
        }
        out->distinct = src.distinct;
        out->columns = cloneExprVec(src.columns);
        out->table = src.table;
        out->where = src.where ? src.where->clone() : nullptr;
        out->orderBy = src.orderBy;
        out->orderByAscending = src.orderByAscending;
        out->groupBy = cloneExprVec(src.groupBy);
        out->having = src.having ? src.having->clone() : nullptr;
        out->limit = src.limit;
        return out;
    }

} // namespace

// Column
Column::Column(std::string n) : name(std::move(n)) {}

std::unique_ptr<Expr> Column::clone() const
{
    return std::make_unique<Column>(name);
}

// Literal
Literal::Literal(std::string v) : value(std::move(v)) {}

std::unique_ptr<Expr> Literal::clone() const
{
    return std::make_unique<Literal>(value);
}

// BinaryExpr
BinaryExpr::BinaryExpr(std::unique_ptr<Expr> l, std::string o, std::unique_ptr<Expr> r)
    : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}

std::unique_ptr<Expr> BinaryExpr::clone() const
{
    // Deep copy
    return std::make_unique<BinaryExpr>(left->clone(), op, right->clone());
}

// ExistsExpr
ExistsExpr::ExistsExpr(std::unique_ptr<SelectStmt> sq) : subquery(std::move(sq)) {}

std::unique_ptr<Expr> ExistsExpr::clone() const
{
    return std::make_unique<ExistsExpr>(subquery ? cloneSelectStmt(*subquery) : nullptr);
}

// InExpr
InExpr::InExpr(std::unique_ptr<Expr> v, std::unique_ptr<SelectStmt> sq)
    : value(std::move(v)), subquery(std::move(sq)) {}

std::unique_ptr<Expr> InExpr::clone() const
{
    return std::make_unique<InExpr>(value ? value->clone() : nullptr, subquery ? cloneSelectStmt(*subquery) : nullptr);
}

// InListExpr
InListExpr::InListExpr(std::unique_ptr<Expr> v, std::vector<std::unique_ptr<Expr>> l)
    : value(std::move(v)), list(std::move(l)) {}

std::unique_ptr<Expr> InListExpr::clone() const
{
    std::vector<std::unique_ptr<Expr>> clonedList;
    for (const auto &item : list)
    {
        clonedList.push_back(item->clone());
    }
    return std::make_unique<InListExpr>(value->clone(), std::move(clonedList));
}