#pragma once
#include "common/types.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Row: maps column name → string value
using Row = std::unordered_map<std::string, Value>;

// Table: ordered list of rows
using Table = std::vector<Row>;

class Database {
  public:
    struct HashIndexEntry {
        std::size_t version{0};
        std::unordered_map<std::string, std::vector<std::size_t>> buckets;
    };

    // all read/write accesses within executors go through the typed API below.
    std::unordered_map<std::string, Table> tables;
    std::unordered_map<std::string, std::vector<std::string>> schemas;

    // Table API
    [[nodiscard]] bool hasTable(const std::string &name) const noexcept;
    Table &getTable(const std::string &name);
    const Table &getTable(const std::string &name) const;

    // Schema API
    [[nodiscard]] bool hasSchema(const std::string &name) const noexcept;
    const std::vector<std::string> &getSchema(const std::string &name) const;

    // Mutation / index cache API
    void markTableMutated(const std::string &name);
    [[nodiscard]] std::size_t tableVersion(const std::string &name) const noexcept;
    const std::unordered_map<std::string, std::vector<std::size_t>> &
    getOrBuildHashIndex(const std::string &tableName, const std::string &columnName) const;

  private:
    std::unordered_map<std::string, std::size_t> tableVersions_;
    mutable std::unordered_map<std::string, std::unordered_map<std::string, HashIndexEntry>> hashIndexCache_;
};