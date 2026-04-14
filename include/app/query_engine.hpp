#pragma once
#include "app/sql_script_loader.hpp"
#include "storage/metadata_catalog.hpp"
#include "storage/table.hpp"
#include <cstddef>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Owns the in-memory Database and MetadataCatalog.
// Responsibilities:
//   • Bootstrap: discover / load CSV files and metadata on startup.
//   • Execute: parse → plan → execute any SQL statement string.
//   • Persist: flush mutated tables back to disk after DML statements.
// ---------------------------------------------------------------------------
class QueryEngineApp
{
private:
    Database db_;
    MetadataCatalog catalog_;

public:
    struct TableSummary
    {
        std::string name;
        std::size_t columnCount{0};
        std::size_t rowCount{0};
    };

    explicit QueryEngineApp(std::string dataDirectory);

    // Resolves the data directory path
    static std::string resolveDataDirectory();

    // Load metadata + CSV files into memory
    void initialize();

    // Parse and execute a single SQL statement string
    void executeStatement(const std::string &query);

    // Parse all statements first, then execute only if parsing succeeds for all.
    void executeScript(const std::vector<SqlStatement> &statements);

    // Introspection helpers for terminal UI and admin tooling.
    [[nodiscard]] std::vector<TableSummary> getTableSummaries() const;
    [[nodiscard]] TableMetadata getTableMetadata(const std::string &tableName) const;
};
