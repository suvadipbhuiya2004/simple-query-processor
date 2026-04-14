#pragma once

#include "execution/compiled_predicate.hpp"
#include "execution/executor.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Database;

// Equality lookup scan backed by database hash-index cache.
class IndexScanExecutor final : public Executor {
  private:
    const Database *db_;
    std::string table_;
    std::string qualifier_;
    std::string lookupColumn_;
    std::string lookupValue_;
    std::vector<std::string> columns_;
    std::vector<std::string> qualifiedColumns_;
    std::vector<std::size_t> rowIds_;
    std::size_t cursor_{0};
    std::size_t outputRowReserve_{0};

    CompiledPredicate pushedPredicate_;
    bool hasPushedPredicate_{false};
    bool alwaysEmpty_{false};

  public:
    IndexScanExecutor(const Database *db, std::string table, std::string qualifier,
                      std::string lookupColumn, std::string lookupValue,
                      std::vector<std::string> requiredColumns,
                      std::unique_ptr<Expr> pushedPredicate, bool alwaysEmpty);

    void open() override;
    bool next(Row &row) override;
    void close() override;
};
