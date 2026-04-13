#include "planner/plan.hpp"
#include <stdexcept>
#include <utility>

namespace
{

    std::vector<std::unique_ptr<Expr>> cloneExprList(
        const std::vector<std::unique_ptr<Expr>> &src)
    {
        std::vector<std::unique_ptr<Expr>> out;
        out.reserve(src.size());
        for (const auto &e : src)
        {
            if (!e)
                throw std::runtime_error("Planner: null expression in column list");
            out.push_back(e->clone());
        }
        return out;
    }

}

// -----------------------------------------------------------------------------
// Planner::createPlan
//
// Builds the following pipeline (bottom → top):
//   SeqScan → [Filter] → [Aggregation] → [Sort] → [Limit] → Projection
// -----------------------------------------------------------------------------
std::unique_ptr<PlanNode> Planner::createPlan(const SelectStmt &stmt)
{
    if (stmt.from.table.empty())
        throw std::runtime_error("Planner: FROM table name is empty");
    if (stmt.columns.empty())
        throw std::runtime_error("Planner: SELECT column list is empty");

    // Sequential scan
    std::unique_ptr<PlanNode> root = std::make_unique<SeqScanNode>(
        stmt.from.table,
        stmt.from.effectiveName());

    // JOIN chain (left-deep)
    for (const auto &join : stmt.joins)
    {
        auto right = std::make_unique<SeqScanNode>(
            join.right.table,
            join.right.effectiveName());

        auto joinNode = std::make_unique<JoinNode>(
            join.type,
            kDefaultJoinAlgorithm,
            join.condition ? join.condition->clone() : nullptr);
        joinNode->children.push_back(std::move(root));
        joinNode->children.push_back(std::move(right));
        root = std::move(joinNode);
    }

    // WHERE → Filter
    if (stmt.where)
    {
        auto filter = std::make_unique<FilterNode>(stmt.where->clone());
        filter->children.push_back(std::move(root));
        root = std::move(filter);
    }

    // GROUP BY (+ optional HAVING) → Aggregation
    if (!stmt.groupBy.empty())
    {
        std::vector<std::unique_ptr<Expr>> groupExprs;
        groupExprs.reserve(stmt.groupBy.size());
        for (const auto &e : stmt.groupBy)
            groupExprs.push_back(e->clone());

        auto agg = std::make_unique<AggregationNode>(
            std::move(groupExprs),
            stmt.having ? stmt.having->clone() : nullptr);
        agg->children.push_back(std::move(root));
        root = std::move(agg);
    }

    // ORDER BY → Sort
    if (!stmt.orderBy.empty())
    {
        auto sort = std::make_unique<SortNode>(stmt.orderBy);
        sort->children.push_back(std::move(root));
        root = std::move(sort);
    }

    // LIMIT → Limit
    if (stmt.limit >= 0)
    {
        auto lim = std::make_unique<LimitNode>(stmt.limit);
        lim->children.push_back(std::move(root));
        root = std::move(lim);
    }

    // SELECT list → Projection  (always the root of the tree)
    auto proj = std::make_unique<ProjectionNode>(cloneExprList(stmt.columns));
    proj->children.push_back(std::move(root));
    root = std::move(proj);

    // DISTINCT → row-level dedup over projected output
    if (stmt.distinct)
    {
        auto distinct = std::make_unique<DistinctNode>();
        distinct->children.push_back(std::move(root));
        root = std::move(distinct);
    }

    return root;
}