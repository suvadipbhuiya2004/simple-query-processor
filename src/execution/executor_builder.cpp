#include "execution/executor_builder.hpp"
#include "execution/aggregation_executor.hpp"
#include "execution/distinct_executor.hpp"
#include "execution/filter_executor.hpp"
#include "execution/index_scan_executor.hpp"
#include "execution/join_executor.hpp"
#include "execution/limit_executor.hpp"
#include "execution/projection_executor.hpp"
#include "execution/seq_scan_executor.hpp"
#include "execution/sort_executor.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

    std::vector<std::string> getTableColumns(Database &db, const std::string &tableName)
    {
        if (db.hasSchema(tableName))
        {
            return db.getSchema(tableName);
        }

        std::vector<std::string> columns;
        const Table &table = db.getTable(tableName);
        if (!table.empty())
        {
            columns.reserve(table.front().size());
            for (const auto &[name, _] : table.front())
            {
                columns.push_back(name);
            }
            std::sort(columns.begin(), columns.end());
        }
        return columns;
    }

    std::vector<std::string> deriveQualifiedColumns(const PlanNode *plan, Database &db)
    {
        if (plan == nullptr)
        {
            throw std::runtime_error("deriveQualifiedColumns: null plan node");
        }

        switch (plan->type)
        {
        case PlanType::SEQ_SCAN:
        {
            const auto *node = static_cast<const SeqScanNode *>(plan);
            std::vector<std::string> qualified;
            const auto baseColumns = node->requiredColumns.empty() ? getTableColumns(db, node->table) : node->requiredColumns;
            qualified.reserve(baseColumns.size());
            for (const auto &col : baseColumns)
            {
                qualified.push_back(node->outputQualifier + "." + col);
            }
            return qualified;
        }
        case PlanType::INDEX_SCAN:
        {
            const auto *node = static_cast<const IndexScanNode *>(plan);
            std::vector<std::string> qualified;
            const auto baseColumns = node->requiredColumns.empty() ? getTableColumns(db, node->table) : node->requiredColumns;
            qualified.reserve(baseColumns.size());
            for (const auto &col : baseColumns)
            {
                qualified.push_back(node->outputQualifier + "." + col);
            }
            return qualified;
        }
        case PlanType::JOIN:
        {
            if (plan->children.size() != 2u)
            {
                throw std::runtime_error("JoinNode must have exactly two children");
            }
            auto left = deriveQualifiedColumns(plan->children[0].get(), db);
            auto right = deriveQualifiedColumns(plan->children[1].get(), db);
            left.insert(left.end(), right.begin(), right.end());
            return left;
        }
        case PlanType::FILTER:
        case PlanType::DISTINCT:
        case PlanType::SORT:
        case PlanType::LIMIT:
        case PlanType::AGGREGATION:
            if (plan->children.size() != 1u)
            {
                throw std::runtime_error("Unary plan node must have exactly one child");
            }
            return deriveQualifiedColumns(plan->children[0].get(), db);
        case PlanType::PROJECTION:
        {
            const auto *node = static_cast<const ProjectionNode *>(plan);
            std::vector<std::string> columns;
            columns.reserve(node->columns.size());
            for (const auto &expr : node->columns)
            {
                const auto *col = dynamic_cast<const Column *>(expr.get());
                if (col == nullptr || col->name == "*")
                {
                    throw std::runtime_error("deriveQualifiedColumns: projection requires explicit column names");
                }
                columns.push_back(col->name);
            }
            return columns;
        }
        default:
            throw std::runtime_error("deriveQualifiedColumns: unknown plan node type");
        }
    }

}

// ExecutorBuilder::build — recursive plan-tree → executor-tree conversion.
std::unique_ptr<Executor> ExecutorBuilder::build(const PlanNode *plan, Database &db)
{
    if (plan == nullptr)
    {
        throw std::runtime_error("ExecutorBuilder: null plan node");
    }

    switch (plan->type)
    {

    case PlanType::SEQ_SCAN:
    {
        const auto *node = static_cast<const SeqScanNode *>(plan);
        const std::vector<std::string> *schema =
            db.hasSchema(node->table) ? &db.getSchema(node->table) : nullptr;
        return std::make_unique<SeqScanExecutor>(
            &db.getTable(node->table), schema, node->outputQualifier, node->requiredColumns,
            node->pushedPredicate ? node->pushedPredicate->clone() : nullptr, node->alwaysEmpty);
    }

    case PlanType::INDEX_SCAN:
    {
        const auto *node = static_cast<const IndexScanNode *>(plan);
        return std::make_unique<IndexScanExecutor>(
            &db, node->table, node->outputQualifier, node->lookupColumn, node->lookupValue,
            node->requiredColumns, node->pushedPredicate ? node->pushedPredicate->clone() : nullptr,
            node->alwaysEmpty);
    }

    case PlanType::JOIN:
    {
        const auto *node = static_cast<const JoinNode *>(plan);
        if (plan->children.size() != 2u)
            throw std::runtime_error("JoinNode must have exactly two children");

        auto leftColumns = deriveQualifiedColumns(plan->children[0].get(), db);
        auto rightColumns = deriveQualifiedColumns(plan->children[1].get(), db);

        auto left = build(plan->children[0].get(), db);
        auto right = build(plan->children[1].get(), db);
        return std::make_unique<JoinExecutor>(std::move(left), std::move(right), node->joinType, node->algorithm, node->condition ? node->condition->clone() : nullptr, std::move(leftColumns), std::move(rightColumns));
    }

    case PlanType::FILTER:
    {
        const auto *node = static_cast<const FilterNode *>(plan);
        if (plan->children.size() != 1u)
            throw std::runtime_error("FilterNode must have exactly one child");
        if (!node->predicate)
            throw std::runtime_error("FilterNode has a null predicate");
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<FilterExecutor>(std::move(child), node->predicate->clone(), &db);
    }

    case PlanType::PROJECTION:
    {
        const auto *node = static_cast<const ProjectionNode *>(plan);
        if (plan->children.size() != 1u)
            throw std::runtime_error("ProjectionNode must have exactly one child");

        bool selectAll = false;
        std::vector<std::string> cols;
        cols.reserve(node->columns.size());

        for (const auto &expr : node->columns)
        {
            const auto *col = dynamic_cast<const Column *>(expr.get());
            if (col == nullptr)
            {
                throw std::runtime_error("Only column references are supported in SELECT list");
            }
            if (col->name == "*")
            {
                if (node->columns.size() != 1u)
                    throw std::runtime_error("SELECT * cannot be mixed with named columns");
                selectAll = true;
                cols.clear();
                break;
            }
            cols.push_back(col->name);
        }

        if (!selectAll && cols.empty())
            throw std::runtime_error("ProjectionNode has no columns");

        auto child = build(plan->children[0].get(), db);
        return std::make_unique<ProjectionExecutor>(std::move(child), std::move(cols), selectAll);
    }

    case PlanType::DISTINCT:
    {
        if (plan->children.size() != 1u)
            throw std::runtime_error("DistinctNode must have exactly one child");
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<DistinctExecutor>(std::move(child));
    }

    case PlanType::SORT:
    {
        const auto *node = static_cast<const SortNode *>(plan);
        if (plan->children.size() != 1u)
            throw std::runtime_error("SortNode must have exactly one child");
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<SortExecutor>(std::move(child), node->orderByColumn, node->ascending);
    }

    case PlanType::LIMIT:
    {
        const auto *node = static_cast<const LimitNode *>(plan);
        if (plan->children.size() != 1u)
            throw std::runtime_error("LimitNode must have exactly one child");
        if (node->limitCount < 0)
            throw std::runtime_error("LimitNode: negative LIMIT value");
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<LimitExecutor>(std::move(child), static_cast<std::size_t>(node->limitCount));
    }

    case PlanType::AGGREGATION:
    {
        const auto *node = static_cast<const AggregationNode *>(plan);
        if (plan->children.size() != 1u)
            throw std::runtime_error("AggregationNode must have exactly one child");
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<AggregationExecutor>(std::move(child), node);
    }

    default:
        throw std::runtime_error("ExecutorBuilder: unknown plan node type");
    }
}