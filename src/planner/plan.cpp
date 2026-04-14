#include "planner/plan.hpp"

#include "planner/cost_model.hpp"
#include "planner/optimizer.hpp"
#include "planner/statistics.hpp"

#include "storage/table.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{

    std::vector<std::unique_ptr<Expr>> cloneExprList(const std::vector<std::unique_ptr<Expr>> &src)
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

    bool isAggregateReference(const std::string &name)
    {
        const auto l = name.find('(');
        const auto r = name.rfind(')');
        if (l == std::string::npos || r == std::string::npos || r <= l + 1)
            return false;
        const std::string fn = name.substr(0, l);
        return fn == "COUNT" || fn == "SUM" || fn == "AVG" || fn == "MIN" || fn == "MAX" ||
               fn == "COUNT_DISTINCT";
    }

    bool hasAggregateExpr(const Expr *expr)
    {
        if (!expr)
            return false;
        if (const auto *col = dynamic_cast<const Column *>(expr))
            return isAggregateReference(col->name);
        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
            return hasAggregateExpr(bin->left.get()) || hasAggregateExpr(bin->right.get());
        return false;
    }

    bool hasAggregateInSelect(const std::vector<std::unique_ptr<Expr>> &cols)
    {
        return std::any_of(cols.begin(), cols.end(), [](const std::unique_ptr<Expr> &e) { return hasAggregateExpr(e.get()); });
    }

    bool parseQualifiedColumn(const std::string &name, std::string &qualifier, std::string &column)
    {
        const auto dot = name.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
            return false;
        qualifier = name.substr(0, dot);
        column = name.substr(dot + 1);
        return true;
    }

    bool collectQualifierRefs(const Expr *expr, std::unordered_set<std::string> &out, bool &hasUnknown, bool &hasSubquery)
    {
        if (expr == nullptr || hasUnknown || hasSubquery)
            return !hasUnknown && !hasSubquery;

        if (dynamic_cast<const ExistsExpr *>(expr) != nullptr ||
            dynamic_cast<const InExpr *>(expr) != nullptr)
        {
            hasSubquery = true;
            return false;
        }

        if (const auto *col = dynamic_cast<const Column *>(expr))
        {
            std::string q;
            std::string c;
            if (!parseQualifiedColumn(col->name, q, c))
            {
                hasUnknown = true;
                return false;
            }
            out.insert(q);
            return true;
        }

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            collectQualifierRefs(bin->left.get(), out, hasUnknown, hasSubquery);
            collectQualifierRefs(bin->right.get(), out, hasUnknown, hasSubquery);
            return !hasUnknown && !hasSubquery;
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            collectQualifierRefs(inList->value.get(), out, hasUnknown, hasSubquery);
            for (const auto &item : inList->list)
            {
                collectQualifierRefs(item.get(), out, hasUnknown, hasSubquery);
            }
            return !hasUnknown && !hasSubquery;
        }

        return true;
    }

    struct EquiJoinCols
    {
        std::string leftQualifier;
        std::string leftColumn;
        std::string rightQualifier;
        std::string rightColumn;
    };

    std::optional<EquiJoinCols> extractEquiJoinCols(const Expr *expr)
    {
        if (expr == nullptr)
            return std::nullopt;
        const auto *bin = dynamic_cast<const BinaryExpr *>(expr);
        if (bin == nullptr || (bin->op != "=" && bin->op != "=="))
            return std::nullopt;

        const auto *left = dynamic_cast<const Column *>(bin->left.get());
        const auto *right = dynamic_cast<const Column *>(bin->right.get());
        if (left == nullptr || right == nullptr)
            return std::nullopt;

        EquiJoinCols out;
        if (!parseQualifiedColumn(left->name, out.leftQualifier, out.leftColumn))
            return std::nullopt;
        if (!parseQualifiedColumn(right->name, out.rightQualifier, out.rightColumn))
            return std::nullopt;
        return out;
    }

    bool containsEquiJoinExpr(const Expr *expr)
    {
        if (expr == nullptr)
            return false;
        if (extractEquiJoinCols(expr).has_value())
            return true;
        const auto *bin = dynamic_cast<const BinaryExpr *>(expr);
        if (bin == nullptr)
            return false;
        return containsEquiJoinExpr(bin->left.get()) || containsEquiJoinExpr(bin->right.get());
    }

    struct JoinPredicate
    {
        std::unique_ptr<Expr> expr;
        std::uint64_t mask{0};
        std::optional<EquiJoinCols> equi;
    };

    std::unique_ptr<Expr> makeAndExpr(std::vector<std::unique_ptr<Expr>> parts)
    {
        if (parts.empty())
            return nullptr;
        std::unique_ptr<Expr> out = std::move(parts[0]);
        for (std::size_t i = 1; i < parts.size(); ++i)
        {
            out = std::make_unique<BinaryExpr>(std::move(out), "AND", std::move(parts[i]));
        }
        return out;
    }

    double estimatePredicateSelectivity(const JoinPredicate &p, const std::unordered_map<std::string, std::string> &qualToTable, const StatisticsCatalog *stats)
    {
        if (stats == nullptr)
            return 0.1;

        if (!p.equi.has_value())
            return 0.1;

        const auto &eq = *p.equi;
        const auto lIt = qualToTable.find(eq.leftQualifier);
        const auto rIt = qualToTable.find(eq.rightQualifier);
        if (lIt == qualToTable.end() || rIt == qualToTable.end())
            return 0.1;

        return CostModel::estimateEqualityJoinSelectivity(*stats, lIt->second, eq.leftColumn, rIt->second, eq.rightColumn);
    }

    bool buildJoinOrderDp(const std::vector<double> &baseRows, const std::vector<JoinPredicate> &predicates, const std::unordered_map<std::string, std::string> &qualToTable, const StatisticsCatalog *stats, std::vector<std::size_t> &outOrder)
    {
        const std::size_t n = baseRows.size();
        if (n == 0 || n > 16)
            return false;

        const std::uint64_t fullMask = (n == 64) ? ~0ULL : ((1ULL << n) - 1ULL);

        struct DpState
        {
            bool valid{false};
            double cost{0.0};
            double rows{0.0};
            std::uint64_t prevMask{0};
            int added{-1};
        };

        std::vector<DpState> dp(static_cast<std::size_t>(1ULL << n));
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::uint64_t mask = (1ULL << i);
            dp[mask].valid = true;
            dp[mask].rows = std::max(1.0, baseRows[i]);
            dp[mask].cost = dp[mask].rows;
            dp[mask].prevMask = 0;
            dp[mask].added = static_cast<int>(i);
        }

        for (std::uint64_t mask = 1; mask <= fullMask; ++mask)
        {
            if (!dp[mask].valid)
                continue;

            for (std::size_t t = 0; t < n; ++t)
            {
                const std::uint64_t bit = (1ULL << t);
                if ((mask & bit) != 0)
                    continue;

                const std::uint64_t nextMask = mask | bit;
                bool hasJoinPredicate = false;
                double sel = 1.0;

                for (const auto &p : predicates)
                {
                    if ((p.mask & bit) == 0)
                        continue;
                    if ((p.mask & ~nextMask) != 0)
                        continue;
                    if ((p.mask & ~mask) == 0)
                        continue;

                    hasJoinPredicate = true;
                    sel *= estimatePredicateSelectivity(p, qualToTable, stats);
                }

                const double rowsOut =
                    CostModel::estimateJoinOutputRows(dp[mask].rows, std::max(1.0, baseRows[t]), sel);
                const double crossPenalty =
                    hasJoinPredicate ? 0.0 : (dp[mask].rows * std::max(1.0, baseRows[t]));
                const double cost = dp[mask].cost + rowsOut + crossPenalty;

                DpState &slot = dp[nextMask];
                if (!slot.valid || cost < slot.cost)
                {
                    slot.valid = true;
                    slot.cost = cost;
                    slot.rows = rowsOut;
                    slot.prevMask = mask;
                    slot.added = static_cast<int>(t);
                }
            }
        }

        if (!dp[fullMask].valid)
            return false;

        outOrder.clear();
        std::uint64_t cur = fullMask;
        while (cur != 0)
        {
            const DpState &s = dp[cur];
            if (s.added < 0)
                return false;
            outOrder.push_back(static_cast<std::size_t>(s.added));
            cur = s.prevMask;
        }
        std::reverse(outOrder.begin(), outOrder.end());
        return outOrder.size() == n;
    }

    std::optional<double> tableRowsFromStats(const TableRef &ref, const StatisticsCatalog *stats, const Database *db)
    {
        if (stats != nullptr)
            return static_cast<double>(stats->rowCountOrDefault(ref.table, 1000));
        if (db != nullptr && db->hasTable(ref.table))
            return static_cast<double>(db->getTable(ref.table).size());
        return std::nullopt;
    }

}

// createPlan
// Builds the following pipeline (bottom → top):
//   SeqScan → [Filter] → [Aggregation] → [Sort] → [Limit] → Projection
std::unique_ptr<PlanNode> Planner::createPlan(const SelectStmt &stmt, const Database *dbStats)
{
    if (stmt.from.table.empty())
        throw std::runtime_error("Planner: FROM table name is empty");
    if (stmt.columns.empty())
        throw std::runtime_error("Planner: SELECT column list is empty");

    std::optional<StatisticsCatalog> statistics;
    if (dbStats != nullptr)
        statistics.emplace(*dbStats);
    const StatisticsCatalog *statsPtr = statistics.has_value() ? &(*statistics) : nullptr;

    std::vector<TableRef> tableRefs;
    tableRefs.reserve(stmt.joins.size() + 1U);
    tableRefs.push_back(stmt.from);
    for (const auto &j : stmt.joins)
        tableRefs.push_back(j.right);

    std::unordered_map<std::string, std::size_t> qualToIndex;
    std::unordered_map<std::string, std::string> qualToTable;
    qualToIndex.reserve(tableRefs.size());
    qualToTable.reserve(tableRefs.size());
    for (std::size_t i = 0; i < tableRefs.size(); ++i)
    {
        const std::string q = tableRefs[i].effectiveName();
        qualToIndex.emplace(q, i);
        qualToTable.emplace(q, tableRefs[i].table);
    }

    std::vector<double> baseRows(tableRefs.size(), 1000.0);
    for (std::size_t i = 0; i < tableRefs.size(); ++i)
    {
        const auto est = tableRowsFromStats(tableRefs[i], statsPtr, dbStats);
        baseRows[i] = std::max(1.0, est.value_or(1000.0));
    }

    std::vector<JoinPredicate> joinPredicates;
    joinPredicates.reserve(stmt.joins.size());
    bool reorderableInnerJoinGraph = !stmt.joins.empty();

    for (const auto &j : stmt.joins)
    {
        if (j.type != JoinType::INNER)
            reorderableInnerJoinGraph = false;

        if (!j.condition)
            continue;

        std::unordered_set<std::string> quals;
        bool hasUnknown = false;
        bool hasSubquery = false;
        collectQualifierRefs(j.condition.get(), quals, hasUnknown, hasSubquery);
        if (hasUnknown || hasSubquery || quals.empty())
        {
            reorderableInnerJoinGraph = false;
            continue;
        }

        std::uint64_t mask = 0;
        for (const auto &q : quals)
        {
            const auto it = qualToIndex.find(q);
            if (it == qualToIndex.end())
            {
                reorderableInnerJoinGraph = false;
                break;
            }
            mask |= (1ULL << it->second);
        }
        if (mask == 0)
            continue;

        JoinPredicate p;
        p.expr = j.condition->clone();
        p.mask = mask;
        p.equi = extractEquiJoinCols(j.condition.get());
        joinPredicates.push_back(std::move(p));
    }

    std::vector<std::size_t> order;
    order.reserve(tableRefs.size());
    if (reorderableInnerJoinGraph)
    {
        if (!buildJoinOrderDp(baseRows, joinPredicates, qualToTable, statsPtr, order))
        {
            order.clear();
        }
    }
    if (order.empty())
    {
        order.resize(tableRefs.size());
        for (std::size_t i = 0; i < tableRefs.size(); ++i)
            order[i] = i;
    }

    auto makeScan = [&](std::size_t idx)
    {
        return std::make_unique<SeqScanNode>(tableRefs[idx].table, tableRefs[idx].effectiveName());
    };

    std::uint64_t currentMask = (1ULL << order[0]);
    double currentRows = baseRows[order[0]];
    std::unique_ptr<PlanNode> root = makeScan(order[0]);

    auto buildJoinPredicateForStep = [&](std::uint64_t oldMask, std::uint64_t newMask, std::size_t addedTable, bool &hasEqui, double &selectivity)
    {
        std::vector<std::unique_ptr<Expr>> active;
        hasEqui = false;
        selectivity = 1.0;

        for (const auto &p : joinPredicates)
        {
            const std::uint64_t bit = (1ULL << addedTable);
            if ((p.mask & bit) == 0)
                continue;
            if ((p.mask & ~newMask) != 0)
                continue;
            if ((p.mask & ~oldMask) == 0)
                continue;

            active.push_back(p.expr->clone());
            hasEqui = hasEqui || p.equi.has_value();
            selectivity *= estimatePredicateSelectivity(p, qualToTable, statsPtr);
        }

        return makeAndExpr(std::move(active));
    };

    for (std::size_t pos = 1; pos < order.size(); ++pos)
    {
        const std::size_t tIdx = order[pos];
        std::uint64_t nextMask = currentMask | (1ULL << tIdx);

        bool stepHasEqui = false;
        double stepSel = 1.0;
        std::unique_ptr<Expr> joinCond;

        if (reorderableInnerJoinGraph)
        {
            joinCond = buildJoinPredicateForStep(currentMask, nextMask, tIdx, stepHasEqui, stepSel);
        }
        else
        {
            const auto &originalJoin = stmt.joins[pos - 1];
            joinCond = originalJoin.condition ? originalJoin.condition->clone() : nullptr;
            stepHasEqui = containsEquiJoinExpr(joinCond.get());
            stepSel = stepHasEqui ? 0.1 : 1.0;
        }

        const JoinType joinType =
            reorderableInnerJoinGraph ? JoinType::INNER : stmt.joins[pos - 1].type;
        const double rightRows = baseRows[tIdx];
        const JoinAlgorithm algo = CostModel::chooseJoinAlgorithm(
            currentRows, rightRows, stepHasEqui, false, false, joinType);

        auto joinNode = std::make_unique<JoinNode>(joinType, algo, std::move(joinCond));
        joinNode->children.push_back(std::move(root));
        joinNode->children.push_back(makeScan(tIdx));
        root = std::move(joinNode);

        currentRows = CostModel::estimateJoinOutputRows(currentRows, rightRows, stepSel);
        currentMask = nextMask;
    }

    // WHERE → Filter
    if (stmt.where)
    {
        auto filter = std::make_unique<FilterNode>(stmt.where->clone());
        filter->children.push_back(std::move(root));
        root = std::move(filter);
    }

    const bool needsAggregation = !stmt.groupBy.empty() || hasAggregateInSelect(stmt.columns) || hasAggregateExpr(stmt.having.get()) || isAggregateReference(stmt.orderBy);

    // GROUP BY / aggregate expressions (+ optional HAVING) → Aggregation
    if (needsAggregation)
    {
        std::vector<std::unique_ptr<Expr>> groupExprs;
        groupExprs.reserve(stmt.groupBy.size());
        for (const auto &e : stmt.groupBy)
            groupExprs.push_back(e->clone());

        auto agg = std::make_unique<AggregationNode>(std::move(groupExprs), stmt.having ? stmt.having->clone() : nullptr, cloneExprList(stmt.columns), stmt.orderBy);
        agg->children.push_back(std::move(root));
        root = std::move(agg);
    }

    // ORDER BY → Sort
    if (!stmt.orderBy.empty())
    {
        auto sort = std::make_unique<SortNode>(stmt.orderBy, stmt.orderByAscending);
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

    PlanOptimizer optimizer;
    return optimizer.optimize(std::move(root), dbStats);
}