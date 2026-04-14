#include "execution/filter_executor.hpp"
#include "planner/plan.hpp"
#include "execution/executor_builder.hpp"
#include <memory>
#include <unordered_set>
#include <stdexcept>
#include <utility>

namespace
{

    std::unique_ptr<SelectStmt> cloneSelectWithOuterRow(const SelectStmt &stmt, const Row &outerRow);
    using AliasSet = std::unordered_set<std::string>;

    AliasSet collectLocalAliases(const SelectStmt &stmt)
    {
        AliasSet aliases;
        aliases.insert(stmt.from.effectiveName());
        for (const auto &j : stmt.joins)
            aliases.insert(j.right.effectiveName());
        return aliases;
    }

    bool shouldBindFromOuterRow(const std::string &colName, const AliasSet &localAliases, const Row &outerRow)
    {
        const auto dot = colName.find('.');
        if (dot == std::string::npos)
            return false;

        const std::string qualifier = colName.substr(0, dot);
        if (localAliases.count(qualifier) > 0)
            return false;

        return outerRow.find(colName) != outerRow.end();
    }

    std::unique_ptr<Expr> cloneExprWithOuterRow(const Expr *expr, const Row &outerRow, const AliasSet &localAliases)
    {
        if (expr == nullptr)
            return nullptr;

        if (const auto *col = dynamic_cast<const Column *>(expr))
        {
            if (shouldBindFromOuterRow(col->name, localAliases, outerRow))
            {
                const auto it = outerRow.find(col->name);
                return std::make_unique<Literal>(it->second);
            }
            return std::make_unique<Column>(col->name);
        }

        if (const auto *lit = dynamic_cast<const Literal *>(expr))
            return std::make_unique<Literal>(lit->value);

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
            return std::make_unique<BinaryExpr>(
                cloneExprWithOuterRow(bin->left.get(), outerRow, localAliases), bin->op,
                cloneExprWithOuterRow(bin->right.get(), outerRow, localAliases));

        if (const auto *exists = dynamic_cast<const ExistsExpr *>(expr))
            return std::make_unique<ExistsExpr>(cloneSelectWithOuterRow(*exists->subquery, outerRow));

        if (const auto *inExpr = dynamic_cast<const InExpr *>(expr))
            return std::make_unique<InExpr>(
                cloneExprWithOuterRow(inExpr->value.get(), outerRow, localAliases),
                cloneSelectWithOuterRow(*inExpr->subquery, outerRow));

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            std::vector<std::unique_ptr<Expr>> list;
            list.reserve(inList->list.size());
            for (const auto &item : inList->list)
                list.push_back(cloneExprWithOuterRow(item.get(), outerRow, localAliases));
            return std::make_unique<InListExpr>(
                cloneExprWithOuterRow(inList->value.get(), outerRow, localAliases), std::move(list));
        }

        throw std::runtime_error("FilterExecutor: unsupported expression in correlated subquery");
    }

    std::unique_ptr<SelectStmt> cloneSelectWithOuterRow(const SelectStmt &stmt, const Row &outerRow)
    {
        auto clone = std::make_unique<SelectStmt>();
        const AliasSet localAliases = collectLocalAliases(stmt);
        clone->from = stmt.from;
        clone->joins.reserve(stmt.joins.size());
        clone->distinct = stmt.distinct;
        clone->table = stmt.table;
        clone->orderBy = stmt.orderBy;
        clone->orderByAscending = stmt.orderByAscending;
        clone->limit = stmt.limit;

        for (const auto &column : stmt.columns)
            clone->columns.push_back(cloneExprWithOuterRow(column.get(), outerRow, localAliases));

        for (const auto &join : stmt.joins)
        {
            JoinClause joinClone;
            joinClone.type = join.type;
            joinClone.right = join.right;
            joinClone.condition = cloneExprWithOuterRow(join.condition.get(), outerRow, localAliases);
            clone->joins.push_back(std::move(joinClone));
        }

        clone->where = cloneExprWithOuterRow(stmt.where.get(), outerRow, localAliases);
        clone->groupBy.reserve(stmt.groupBy.size());
        for (const auto &expr : stmt.groupBy)
            clone->groupBy.push_back(cloneExprWithOuterRow(expr.get(), outerRow, localAliases));
        clone->having = cloneExprWithOuterRow(stmt.having.get(), outerRow, localAliases);
        return clone;
    }

    std::string firstSelectedValue(const Row &row, const SelectStmt &subquery)
    {
        if (!subquery.columns.empty())
        {
            if (const auto *col = dynamic_cast<const Column *>(subquery.columns[0].get()))
            {
                const auto it = row.find(col->name);
                if (it != row.end())
                    return it->second;
            }
        }

        if (row.size() == 1)
            return row.begin()->second;

        if (row.empty())
            return {};

        throw std::runtime_error("FilterExecutor: subquery IN expects a single selected column");
    }

}

FilterExecutor::FilterExecutor(std::unique_ptr<Executor> child, std::unique_ptr<Expr> predicate, Database *db)
    : child_(std::move(child)), predicate_(std::move(predicate)), db_(db)
{
    if (!child_)
        throw std::runtime_error("FilterExecutor: null child executor");

    hasSubquery_ = exprContainsSubquery(predicate_.get());
    if (!hasSubquery_)
    {
        compiledPredicate_ = CompiledPredicate::compile(predicate_.get());
    }
}

void FilterExecutor::open()
{
    child_->open();
}
void FilterExecutor::close()
{
    child_->close();
}

bool FilterExecutor::evalPredicateWithSubqueries(const Expr *expr, const Row &row)
{
    if (expr == nullptr)
        return true;

    // Handle EXISTS
    if (const auto *exists = dynamic_cast<const ExistsExpr *>(expr))
    {
        if (!db_ || !exists->subquery)
            return false;
        Planner planner;
        auto correlatedSubquery = cloneSelectWithOuterRow(*exists->subquery, row);
        auto plan = planner.createPlan(*correlatedSubquery, db_);
        auto executor = ExecutorBuilder::build(plan.get(), *db_);
        executor->open();
        Row row_ignored;
        const bool hasRows = executor->next(row_ignored);
        executor->close();
        return hasRows;
    }

    // Handle IN with subquery
    if (const auto *inExpr = dynamic_cast<const InExpr *>(expr))
    {
        if (!db_ || !inExpr->subquery)
            return false;

        const std::string val = ExpressionEvaluator::eval(inExpr->value.get(), row);
        Planner planner;
        auto correlatedSubquery = cloneSelectWithOuterRow(*inExpr->subquery, row);
        auto plan = planner.createPlan(*correlatedSubquery, db_);
        auto executor = ExecutorBuilder::build(plan.get(), *db_);
        executor->open();
        Row subRow;
        while (executor->next(subRow))
        {
            if (val == firstSelectedValue(subRow, *correlatedSubquery))
            {
                executor->close();
                return true;
            }
        }
        executor->close();
        return false;
    }

    // Handle IN list
    if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
    {
        const std::string val = ExpressionEvaluator::eval(inList->value.get(), row);
        for (const auto &item : inList->list)
        {
            if (val == ExpressionEvaluator::eval(item.get(), row))
            {
                return true;
            }
        }
        return false;
    }

    // Handle NOT operator
    if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
    {
        if (bin->op == "NOT")
        {
            return !evalPredicateWithSubqueries(bin->left.get(), row);
        }
        if (bin->op == "AND")
        {
            return evalPredicateWithSubqueries(bin->left.get(), row) && evalPredicateWithSubqueries(bin->right.get(), row);
        }
        if (bin->op == "OR")
        {
            return evalPredicateWithSubqueries(bin->left.get(), row) || evalPredicateWithSubqueries(bin->right.get(), row);
        }
        // Comparison operator - use standard evaluator
        const std::string lhs = ExpressionEvaluator::eval(bin->left.get(), row);
        const std::string rhs = ExpressionEvaluator::eval(bin->right.get(), row);
        return ExpressionEvaluator::compareValues(lhs, bin->op, rhs);
    }

    // Fall back to standard expression evaluator
    return ExpressionEvaluator::evalPredicate(expr, row);
}

bool FilterExecutor::next(Row &row)
{
    while (child_->next(row))
    {
        const bool passed = hasSubquery_ ? evalPredicateWithSubqueries(predicate_.get(), row) : compiledPredicate_.evaluatePredicate(row);
        if (passed)
        {
            return true;
        }
    }
    return false;
}