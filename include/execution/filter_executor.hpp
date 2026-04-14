#pragma once

#include "execution/compiled_predicate.hpp"
#include "execution/executor.hpp"
#include "execution/expression.hpp"
#include "storage/table.hpp"
#include <memory>

// passes through only rows where predicate evaluates true.
class FilterExecutor final : public Executor {
  private:
    std::unique_ptr<Executor> child_;
    std::unique_ptr<Expr> predicate_;
    Database *db_; // For evaluating subqueries
    CompiledPredicate compiledPredicate_;
    bool hasSubquery_{false};

    // Evaluate with subquery support
    bool evalPredicateWithSubqueries(const Expr *expr, const Row &row);

  public:
    FilterExecutor(std::unique_ptr<Executor> child, std::unique_ptr<Expr> predicate, Database *db = nullptr);

    void open() override;
    bool next(Row &row) override;
    void close() override;
};