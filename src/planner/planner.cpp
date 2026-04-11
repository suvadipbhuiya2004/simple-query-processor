#include "planner/planner.h"
#include <stdexcept>

namespace
{
    std::vector<std::unique_ptr<Expr>> CloneExprList(const std::vector<std::unique_ptr<Expr>> &source)
    {
        std::vector<std::unique_ptr<Expr>> cloned;
        cloned.reserve(source.size());
        for (const auto &expr : source)
        {
            if (!expr) throw std::runtime_error("Planner received a null expression");
            cloned.push_back(expr->clone());
        }
        return cloned;
    }
} 

std::unique_ptr<PlanNode> Planner::createPlan(const SelectStmt &stmt)
{
    if (stmt.table.empty()) throw std::runtime_error("FROM table name cannot be empty");
    if (stmt.columns.empty()) throw std::runtime_error("SELECT list cannot be empty");

    // Unsupported features
    if (!stmt.orderBy.empty()) throw std::runtime_error("ORDER BY not supported");
    if (stmt.limit >= 0) throw std::runtime_error("LIMIT not supported");

    auto scan = std::make_unique<SeqScanNode>(stmt.table);
    std::unique_ptr<PlanNode> current = std::move(scan);

    if (stmt.where)
    {
        auto filter = std::make_unique<FilterNode>(stmt.where->clone());
        filter->children.push_back(std::move(current));
        current = std::move(filter);
    }

    // Aggregation Detection
    bool hasAggregates = false;
    std::vector<std::unique_ptr<Expr>> aggExprs;

    for (const auto &col : stmt.columns)
    {
        if (col && dynamic_cast<AggregateExpr*>(col.get())) // Fixed warning: removed 'auto* agg ='
        {
            hasAggregates = true;
            aggExprs.push_back(col->clone());
        }
    }

    if (!stmt.groupBy.empty() || hasAggregates)
    {
        std::vector<std::unique_ptr<Expr>> groupExprs;
        for (const auto &expr : stmt.groupBy) groupExprs.push_back(expr->clone());

        std::unique_ptr<Expr> havingExpr = (stmt.having) ? stmt.having->clone() : nullptr;

        // Matches the AggregationNode constructor in plan_node.h
        auto aggNode = std::make_unique<AggregationNode>(
            std::move(groupExprs),
            std::move(aggExprs),
            std::move(havingExpr));

        aggNode->children.push_back(std::move(current));
        current = std::move(aggNode);
    }

    auto projection = std::make_unique<ProjectionNode>(CloneExprList(stmt.columns));
    projection->children.push_back(std::move(current));
    return std::move(projection);
}