#pragma once

#include "execution/compiled_predicate.hpp"
#include "execution/executor.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// scans all rows from a Table in stored order.
class SeqScanExecutor final : public Executor {
  private:
    const Table *table_;
    std::vector<std::string> columns_;
    std::vector<std::string> qualifiedColumns_;
    std::string qualifier_;
    std::size_t index_{0};
    std::size_t outputRowReserve_{0};
    CompiledPredicate pushedPredicate_;
    bool hasPushedPredicate_{false};
    bool alwaysEmpty_{false};

  public:
    SeqScanExecutor(const Table *table, const std::vector<std::string> *schema, std::string qualifier, std::vector<std::string> requiredColumns, std::unique_ptr<Expr> pushedPredicate, bool alwaysEmpty);

    void open() override;
    bool next(Row &row) override;
    void close() override;
};