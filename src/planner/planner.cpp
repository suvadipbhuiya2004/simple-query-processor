#include "planner/planner.h"

#include <stdexcept>

// Planner translates parsed AST into a runnable logical plan tree.

namespace {
std::vector<std::unique_ptr<Expr>> CloneExprList(const std::vector<std::unique_ptr<Expr>>& source) {
    std::vector<std::unique_ptr<Expr>> cloned;
    cloned.reserve(source.size());

    for (const auto& expr : source) {
        if (!expr) {
            throw std::runtime_error("Planner received a null expression in SELECT list");
        }
        cloned.push_back(expr->clone());
    }

    return cloned;
}
}  // namespace

std::unique_ptr<PlanNode> Planner::createPlan(const SelectStmt& stmt) {
    // Keep explicit checks here so unsupported clauses fail early.
    if (stmt.table.empty()) {
        throw std::runtime_error("FROM table name cannot be empty");
    }

    if (stmt.columns.empty()) {
        throw std::runtime_error("SELECT list cannot be empty");
    }

    if (!stmt.orderBy.empty()) {
        throw std::runtime_error("ORDER BY is parsed but not yet supported by planner/execution layer");
    }

    if (stmt.limit >= 0) {
        throw std::runtime_error("LIMIT is parsed but not yet supported by planner/execution layer");
    }

    // Step 1: Base scan node
    auto scan = std::make_unique<SeqScanNode>(stmt.table);

    std::unique_ptr<PlanNode> current = std::move(scan);

    // Step 2: WHERE -> Filter
    if (stmt.where) {
        auto filter = std::make_unique<FilterNode>(stmt.where->clone());
        filter->children.push_back(std::move(current));
        current = std::move(filter);
    }

    // GROUP
    if (!stmt.groupBy.empty()) {
        std::vector<std::unique_ptr<Expr>> groupExprs;
        for (const auto& expr : stmt.groupBy) {
            groupExprs.push_back(expr->clone());
        }
        
        std::unique_ptr<Expr> havingExpr = (stmt.having) ? stmt.having->clone() : nullptr;

        // Use 'AggregationNode' to match the expected symbol
        auto aggNode = std::make_unique<AggregationNode>(
            std::move(groupExprs), 
            std::move(havingExpr)
        );
        
        aggNode->children.push_back(std::move(current));
        current = std::move(aggNode);
    }

    // Step 3: SELECT -> Projection
    auto projection = std::make_unique<ProjectionNode>(CloneExprList(stmt.columns));
    projection->children.push_back(std::move(current));

    current = std::move(projection);

    return current;
}