#pragma once
#include "app/sql_script_loader.hpp"
#include "storage/metadata_catalog.hpp"
#include "storage/table.hpp"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Owns the in-memory Database and MetadataCatalog.
// Responsibilities:
//   • Bootstrap: discover / load CSV files and metadata on startup.
//   • Execute: parse → plan → execute any SQL statement string.
//   • Persist: flush mutated tables back to disk after DML statements.
// ---------------------------------------------------------------------------
class QueryEngineApp {
private:
    Database db_;
    MetadataCatalog catalog_;

public:
    explicit QueryEngineApp(std::string dataDirectory);

    // Resolves the data directory path
    static std::string resolveDataDirectory();

    // Load metadata + CSV files into memory
    void initialize();

    // Parse and execute a single SQL statement string
    void executeStatement(const std::string& query);

    // Parse all statements first, then execute only if parsing succeeds for all.
    void executeScript(const std::vector<SqlStatement>& statements);
};