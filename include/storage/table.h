#pragma once
#include "common/types.h"
#include <string>
#include <unordered_map>
#include <vector>

// Row is a map from column name to value.
using Row = std::unordered_map<std::string, Value>;
// Table is a simple list of rows.
using Table = std::vector<Row>;

// In-memory table registry used by executors.
class Database {
public:
    std::unordered_map<std::string, Table> tables;
    std::unordered_map<std::string, std::vector<std::string>> schemas;

    bool hasTable(const std::string& name) const;
    Table& getTable(const std::string& name);
    const Table& getTable(const std::string& name) const;
    bool hasSchema(const std::string& name) const;
    const std::vector<std::string>& getSchema(const std::string& name) const;
};