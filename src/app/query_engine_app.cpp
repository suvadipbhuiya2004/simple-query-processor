#include "app/query_engine.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "execution/executor_builder.hpp"
#include "execution/expression.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "planner/plan.hpp"
#include "storage/csv_loader.hpp"

#define DATADIR "data"

namespace fs = std::filesystem;

namespace{

    // ANSI color codes
    constexpr const char *kAnsiReset = "\x1b[0m";
    constexpr const char *kAnsiRed = "\x1b[31m";
    constexpr const char *kAnsiGreen = "\x1b[32m";
    constexpr const char *kAnsiYellow = "\x1b[33m";


    std::string colorize(const std::string &text, const char *colorCode){
        return std::string(colorCode) + text + kAnsiReset;
    }

    std::string toUpper(std::string s){
        for (char &c : s){
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return s;
    }
    std::string toLower(std::string s){
        for (char &c : s){
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }


    // Remove all whitespace and uppercase.
    std::string normalizeType(std::string t){
        t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return std::isspace(c) != 0; }), t.end());
        return toUpper(std::move(t));
    }

    bool isVarcharType(const std::string &norm){
        return norm == "VARCHAR" || norm.rfind("VARCHAR(", 0) == 0;
    }
    bool isTextType(const std::string &norm) { return norm == "TEXT"; }
    bool isIntType(const std::string &norm) { return norm == "INT"; }

    // Returns the max length from VARCHAR(n), or nullopt for bare VARCHAR.
    std::optional<std::size_t> varcharLimit(const std::string &norm)
    {
        if (norm == "VARCHAR"){
            return std::nullopt;
        }
        const std::string prefix = "VARCHAR(";
        if (norm.rfind(prefix, 0) != 0 || norm.back() != ')'){
            return std::nullopt;
        }
        const std::string digits = norm.substr(prefix.size(), norm.size() - prefix.size() - 1);
        if (digits.empty()){
            return std::nullopt;
        }
        for (char c : digits){
            if (!std::isdigit(static_cast<unsigned char>(c))){
                return std::nullopt;
            }
        }
        const unsigned long long n = std::stoull(digits);
        if (n == 0 || n > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())){
            throw std::runtime_error("Invalid VARCHAR length: " + digits);
        }
        return static_cast<std::size_t>(n);
    }

    std::string normalizeColumnType(const std::string &raw){
        std::string n = normalizeType(raw);
        if (n == "VARCHAR"){
            return "VARCHAR(255)";
        }
        return n;
    }


    bool isIntegerValue(const std::string &v){
        static const std::regex kInt{R"(^[+-]?\d+$)"};
        return std::regex_match(v, kInt);
    }
    bool isNumericValue(const std::string &v){
        static const std::regex kNum{R"(^[+-]?(\d+(\.\d+)?|\.\d+)$)"};
        return std::regex_match(v, kNum);
    }

    void validateValueType(const ColumnMetadata &col, const std::string &value){
        const std::string norm = normalizeColumnType(col.type);
        if (isIntType(norm)){
            if (!value.empty() && !isIntegerValue(value)){
                throw std::runtime_error("Column '" + col.name + "' expects INT, got: '" + value + "'");
            }
            return;
        }
        if (isVarcharType(norm)){
            const auto lim = varcharLimit(norm);
            const std::size_t maxLen = lim.value_or(255u);
            if (value.size() > maxLen){
                throw std::runtime_error("Column '" + col.name + "' exceeds VARCHAR(" + std::to_string(maxLen) + ") limit");
            }
            return;
        }
        if (isTextType(norm)){
            return;
        }
        throw std::runtime_error("Unsupported column type '" + col.type + "' for column '" + col.name + "'");
    }


    const ColumnMetadata *findColumn(const TableMetadata &tm, const std::string &name){
        for (const auto &c : tm.columns){
            if (c.name == name){
                return &c;
            }
        }
        return nullptr;
    }

    std::pair<std::string, std::string> parseFkRef(const std::string &ref){
        const auto dot = ref.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= ref.size()){
            throw std::runtime_error("Invalid FK format, expected table.column: " + ref);
        }
        return {ref.substr(0, dot), ref.substr(dot + 1)};
    }

    std::optional<std::string> findTableCI(const MetadataCatalog &cat, const std::string &name){
        if (cat.hasTable(name)){
            return name;
        }
        const std::string lower = toLower(name);
        std::optional<std::string> match;
        for (const auto &[k, _] : cat.allTables()){
            if (toLower(k) == lower){
                if (match && *match != k){
                    throw std::runtime_error("Ambiguous case-insensitive table match: " + name);
                }
                match = k;
            }
        }
        return match;
    }

    //  Output formatting 
    std::vector<bool> inferNumericCols(const std::vector<std::string> &cols, const std::vector<std::vector<std::string>> &rows){
        std::vector<bool> numeric(cols.size(), true);
        for (std::size_t ci = 0; ci < cols.size(); ++ci){
            for (const auto &row : rows){
                const std::string &v = ci < row.size() ? row[ci] : "";
                if (!v.empty() && !isNumericValue(v)){
                    numeric[ci] = false;
                    break;
                }
            }
        }
        return numeric;
    }

    void printTable(const std::vector<std::string> &cols, const std::vector<std::vector<std::string>> &rows){
        if (cols.empty()){
            std::cout << "(0 tuples)\n";
            return;
        }

        std::vector<std::size_t> widths(cols.size());
        for (std::size_t i = 0; i < cols.size(); ++i){
            widths[i] = cols[i].size();
        }
        for (const auto &row : rows){
            for (std::size_t i = 0; i < cols.size() && i < row.size(); ++i){
                widths[i] = std::max(widths[i], row[i].size());
            }
        }

        const auto numeric = inferNumericCols(cols, rows);

        // Header
        for (std::size_t i = 0; i < cols.size(); ++i){
            if (i){
                std::cout << " | ";
            }
            std::cout << std::left << std::setw(static_cast<int>(widths[i])) << cols[i];
        }
        std::cout << '\n';

        // Separator
        for (std::size_t i = 0; i < cols.size(); ++i){
            if (i){
                std::cout << "-+-";
            }
            std::cout << std::string(widths[i], '-');
        }
        std::cout << '\n';

        // Data rows
        for (const auto &row : rows){
            for (std::size_t i = 0; i < cols.size(); ++i){
                if (i){
                    std::cout << " | ";
                }
                const std::string &v = i < row.size() ? row[i] : "";
                if (numeric[i]){
                    std::cout << std::right << std::setw(static_cast<int>(widths[i])) << v;
                }
                else{
                    std::cout << std::left << std::setw(static_cast<int>(widths[i])) << v;
                }
            }
            std::cout << '\n';
        }

        const std::size_t n = rows.size();
        std::cout << '(' << n << ' ' << (n == 1 ? "tuple" : "tuples") << ")\n";
    }

    // Data inference for legacy CSV files
    std::string inferColType(const std::string &colName, const Table &rows){
        for (const auto &row : rows){
            const auto it = row.find(colName);
            if (it == row.end() || it->second.empty()){
                continue;
            }
            if (!isIntegerValue(it->second)){
                return "VARCHAR(255)";
            }
        }
        return "INT";
    }

    bool isUniqueNonEmpty(const std::string &colName, const Table &rows){
        if (rows.empty()){
            return false;
        }
        std::unordered_set<std::string> seen;
        for (const auto &row : rows){
            const auto it = row.find(colName);
            if (it == row.end() || it->second.empty()){
                return false;
            }
            if (!seen.insert(it->second).second){
                return false;
            }
        }
        return true;
    }

    //  Constraint validation 
    void validateTableState(const std::string &tableName, const Table &rows, const MetadataCatalog &cat, const Database &db){
        const auto &tm = cat.getTable(tableName);

        // Collect primary key columns.
        std::vector<const ColumnMetadata *> pkCols;
        for (const auto &c : tm.columns){
            if (c.primaryKey){
                pkCols.push_back(&c);
            }
        }
        const bool compositePK = pkCols.size() > 1;

        // Sets for uniqueness checks.
        std::unordered_set<std::string> pkSeen;
        std::unordered_map<std::string, std::unordered_set<std::string>> uniqueSeen;
        for (const auto &c : tm.columns){
            if (c.unique){
                uniqueSeen[c.name];
            }
        }

        for (const auto &row : rows){
            // Per-column checks.
            for (const auto &c : tm.columns){
                const auto vIt = row.find(c.name);
                if (vIt == row.end()){
                    throw std::runtime_error("Row missing column '" + c.name + "' in table: " + tableName);
                }
                if (c.notNull && vIt->second.empty()){
                    throw std::runtime_error("Column '" + c.name + "' cannot be NULL");
                }

                validateValueType(c, vIt->second);

                // UNIQUE: skip if this col is part of a composite PK
                const bool enforceUnique = c.unique && !(compositePK && c.primaryKey);
                if (enforceUnique && !vIt->second.empty()){
                    if (!uniqueSeen[c.name].insert(vIt->second).second){
                        throw std::runtime_error("Duplicate value '" + vIt->second + "' for UNIQUE column '" + c.name + "'");
                    }
                }
            }

            // Composite / single PK tuple uniqueness.
            if (!pkCols.empty()){
                std::string key;
                std::string keyDisplay;
                key.reserve(pkCols.size() * 8);
                keyDisplay.reserve(pkCols.size() * 16);
                for (std::size_t i = 0; i < pkCols.size(); ++i){
                    const auto vIt = row.find(pkCols[i]->name);
                    if (vIt == row.end() || vIt->second.empty())
                        throw std::runtime_error("PRIMARY KEY column '" + pkCols[i]->name + "' cannot be empty");
                    if (i){
                        key += '\x1f';
                        keyDisplay += ", ";
                    }
                    key += vIt->second;
                    keyDisplay += pkCols[i]->name + "=" + vIt->second;
                }
                if (!pkSeen.insert(key).second)
                    throw std::runtime_error("Duplicate PRIMARY KEY (" + keyDisplay + ") in table: " + tableName);
            }

            // Foreign key checks.
            for (const auto &c : tm.columns){
                if (!c.foreignKey)
                    continue;
                const auto vIt = row.find(c.name);
                if (vIt == row.end() || vIt->second.empty())
                    continue;

                const auto fkParts = parseFkRef(*c.foreignKey);
                const std::string &refTable = fkParts.first;
                const std::string &refCol = fkParts.second;
                if (!cat.hasTable(refTable))
                    throw std::runtime_error("FK references unknown table: " + refTable);
                if (!findColumn(cat.getTable(refTable), refCol))
                    throw std::runtime_error("FK references unknown column: " + *c.foreignKey);

                const Table &refRows = (refTable == tableName) ? rows : db.getTable(refTable);
                bool found = false;
                for (const auto &rr : refRows){
                    const auto rit = rr.find(refCol);
                    if (rit != rr.end() && rit->second == vIt->second){
                        found = true;
                        break;
                    }
                }
                if (!found){
                    throw std::runtime_error("FK constraint failed on '" + c.name + "': value '" + vIt->second + "' not found in " + refTable + "." + refCol); 
                }
            }
        }
    }

    void enforceDeleteConstraints(const std::string &tableName, const Table &deletedRows, const MetadataCatalog &cat, const Database &db){
        if (deletedRows.empty()) return;

        // Build deleted-values index per column.
        const auto &tm = cat.getTable(tableName);
        std::unordered_map<std::string, std::unordered_set<std::string>> deleted;
        for (const auto &c : tm.columns){
            auto &s = deleted[c.name];
            for (const auto &row : deletedRows){
                const auto it = row.find(c.name);
                if (it != row.end()) s.insert(it->second);
            }
        }

        for (const auto &[rTableName, rMeta] : cat.allTables()){
            if (rTableName == tableName || !db.hasTable(rTableName)) continue;
            const Table &rRows = db.getTable(rTableName);

            for (const auto &col : rMeta.columns){
                if (!col.foreignKey) continue;
                const auto fkParts = parseFkRef(*col.foreignKey);
                const std::string &refTable = fkParts.first;
                const std::string &refCol = fkParts.second;
                if (refTable != tableName) continue;

                const auto delIt = deleted.find(refCol);
                if (delIt == deleted.end()) continue;

                for (const auto &row : rRows) {
                    const auto vIt = row.find(col.name);
                    if (vIt == row.end() || vIt->second.empty()) continue;
                    if (delIt->second.count(vIt->second))
                        throw std::runtime_error("DELETE violates FK: " + rTableName + "." + col.name + " references '" + vIt->second + "'");
                }
            }
        }
    }

    // Persistence 
    void persistTable(const std::string &tableName, const MetadataCatalog &cat, const Database &db){
        const auto &meta = cat.getTable(tableName);
        std::vector<std::string> headers;
        headers.reserve(meta.columns.size());
        for (const auto &c : meta.columns)
            headers.push_back(c.name);
        CsvLoader::saveTable(cat.tableFilePath(tableName), headers, db.getTable(tableName));
    }

    // Bootstrap helpers
    void loadTablesFromMetadata(Database &db, const MetadataCatalog &cat){
        for (const auto &[tname, tmeta] : cat.allTables()){
            std::vector<std::string> headers;
            headers.reserve(tmeta.columns.size());
            for (const auto &c : tmeta.columns)
                headers.push_back(c.name);

            const std::string path = cat.dataDirectory() + "/" + tmeta.file;
            std::error_code ec;
            if (!fs::exists(path, ec)){
                // Create an empty CSV so the file always exists on disk.
                CsvLoader::saveTable(path, headers, Table{});
            }

            CsvLoader::loadIntoDatabase(db, tname, path);

            // Guard against metadata/CSV schema drift.
            if (db.getSchema(tname) != headers){
                throw std::runtime_error("Schema mismatch between metadata and CSV for table: " + tname);
            }
        }
    }

    bool registerLegacyTables(Database &db, MetadataCatalog &cat){
        bool changed = false;

        for (const auto &entry : fs::directory_iterator(cat.dataDirectory())){
            if (!entry.is_regular_file()) continue;

            const fs::path &src = entry.path();
            const std::string ext = toLower(src.extension().string());
            if (ext != ".csv") continue;

            fs::path csvPath = src;
            
            const std::string tname = csvPath.stem().string();
            if (cat.hasTable(tname)) continue;

            CsvLoader::loadIntoDatabase(db, tname, csvPath.string());

            TableMetadata meta;
            meta.file = csvPath.filename().string();
            const Table &rows = db.getTable(tname);
            for (const auto &col : db.getSchema(tname)){
                ColumnMetadata cm;
                cm.name = col;
                cm.type = inferColType(col, rows);
                if (col == "id" && isUniqueNonEmpty(col, rows)){
                    cm.primaryKey = cm.notNull = true;
                }
                meta.columns.push_back(std::move(cm));
            }
            cat.upsertTable(tname, std::move(meta));
            changed = true;
        }
        return changed;
    }

    bool normalizeCatalogTypes(MetadataCatalog &cat){
        bool changed = false;
        std::vector<std::pair<std::string, TableMetadata>> updates;

        for (const auto &[tname, tm] : cat.allTables()){
            TableMetadata updated = tm;
            bool dirty = false;

            std::size_t pkCount = 0;
            for (const auto &c : updated.columns){
                if (c.primaryKey) ++pkCount;
            }
            const bool composite = pkCount > 1;

            for (auto &c : updated.columns){
                const std::string norm = normalizeColumnType(c.type);
                if (norm != c.type){
                    c.type = norm;
                    dirty = true;
                }
                if (c.primaryKey && !c.notNull){
                    c.notNull = true;
                    dirty = true;
                }
                if (composite && c.primaryKey && c.unique){
                    c.unique = false;
                    dirty = true;
                }
            }
            if (dirty){
                updates.emplace_back(tname, std::move(updated));
                changed = true;
            }
        }
        for (auto &[n, m] : updates)
            cat.upsertTable(n, std::move(m));
        return changed;
    }

    void bootstrapDatabase(Database &db, MetadataCatalog &cat){
        const bool existed = cat.metadataFileExists();
        cat.load();

        bool legacyDiscovered = false;
        if (!existed){
            legacyDiscovered = registerLegacyTables(db, cat);
        }

        const bool normalized = normalizeCatalogTypes(cat);
        loadTablesFromMetadata(db, cat);

        if (!existed || legacyDiscovered || normalized) cat.save();
    }

    //  Metadata build for CREATE TABLE

    TableMetadata buildTableMetadata(const CreateTableStmt &stmt, const MetadataCatalog &cat){
        if (stmt.table.empty())
            throw std::runtime_error("CREATE TABLE: table name is empty");
        if (stmt.columns.empty())
            throw std::runtime_error("CREATE TABLE: at least one column required");

        std::unordered_set<std::string> seen;
        TableMetadata meta;
        meta.file = stmt.table + ".csv";

        for (const auto &def : stmt.columns){
            if (def.name.empty())
                throw std::runtime_error("CREATE TABLE: column name is empty");
            if (!seen.insert(def.name).second)
                throw std::runtime_error("Duplicate column in CREATE TABLE: " + def.name);

            ColumnMetadata c;
            c.name = def.name;
            c.type = normalizeColumnType(def.type);
            c.primaryKey = def.primaryKey;
            c.unique = def.unique;
            c.notNull = def.notNull || def.primaryKey;
            c.foreignKey = def.foreignKey;

            if (!isIntType(c.type) && !isVarcharType(c.type) && !isTextType(c.type))
                throw std::runtime_error("Unsupported column type '" + c.type + "'. Supported: INT, VARCHAR, TEXT");

            if (isVarcharType(c.type) && !varcharLimit(c.type).has_value() && c.type != "VARCHAR(255)")
                throw std::runtime_error("Invalid VARCHAR definition for column '" + c.name + "': " + c.type);

            if (c.foreignKey){
                const auto fkParts = parseFkRef(*c.foreignKey);
                const std::string refTable = fkParts.first;
                const std::string refCol = fkParts.second;
                if (refTable == stmt.table){
                    // Self-reference: check that the referenced column appears in the list.
                    bool exists = std::any_of(stmt.columns.begin(), stmt.columns.end(), [&refCol](const ColumnDef &d) { return d.name == refCol; });
                    if (!exists)
                        throw std::runtime_error("FK references unknown local column: " + *c.foreignKey);
                }
                else{
                    const auto canonical = findTableCI(cat, refTable);
                    if (!canonical)
                        throw std::runtime_error("FK references unknown table: " + refTable);
                    if (!findColumn(cat.getTable(*canonical), refCol))
                        throw std::runtime_error("FK references unknown column: " + *c.foreignKey);
                    c.foreignKey = *canonical + "." + refCol;
                }
            }
            meta.columns.push_back(std::move(c));
        }
        return meta;
    }

    // Statement executors
    void executeSelect(const SelectStmt &stmt, Database &db){
        // Determine output column order before running the plan.
        std::vector<std::string> orderedCols;
        const bool isStar = (stmt.columns.size() == 1 && dynamic_cast<const Column *>(stmt.columns[0].get()) != nullptr && static_cast<const Column *>(stmt.columns[0].get())->name == "*");

        if (isStar){
            if (stmt.joins.empty()){
                if (!db.hasSchema(stmt.table))
                    throw std::runtime_error("Unknown table for SELECT *: " + stmt.table);
                orderedCols = db.getSchema(stmt.table);
            }
            else{
                auto appendQualifiedColumns = [&](const TableRef &ref){
                    if (!db.hasSchema(ref.table)){
                        throw std::runtime_error("Unknown table in SELECT * join expansion: " + ref.table);
                    }
                    const std::string qualifier = ref.effectiveName();
                    const auto &schema = db.getSchema(ref.table);
                    orderedCols.reserve(orderedCols.size() + schema.size());
                    for (const auto &col : schema){
                        orderedCols.push_back(qualifier + "." + col);
                    }
                };

                appendQualifiedColumns(stmt.from);
                for (const auto &join : stmt.joins){
                    appendQualifiedColumns(join.right);
                }
            }
        }
        else{
            orderedCols.reserve(stmt.columns.size());
            for (const auto &e : stmt.columns){
                const auto *col = dynamic_cast<const Column *>(e.get());
                if (!col){
                    throw std::runtime_error("Only column references are supported in SELECT list");
                }
                orderedCols.push_back(col->name);
            }
        }

        Planner planner;
        auto plan = planner.createPlan(stmt);
        auto exec = ExecutorBuilder::build(plan.get(), db);
        exec->open();

        std::vector<std::vector<std::string>> rows;
        Row row;
        while (exec->next(row)){
            std::vector<std::string> printRow;
            printRow.reserve(orderedCols.size());
            for (const auto &col : orderedCols){
                const auto it = row.find(col);
                if (it == row.end())
                    throw std::runtime_error("Column not in result row: " + col);
                printRow.push_back(it->second);
            }
            rows.push_back(std::move(printRow));
        }
        exec->close();
        printTable(orderedCols, rows);
    }

    void executeCreateTable(const CreateTableStmt &stmt, Database &db, MetadataCatalog &cat){
        const auto existing = findTableCI(cat, stmt.table);
        if (existing.has_value() || db.hasTable(stmt.table)){
            const std::string existingName = existing.has_value() ? *existing : stmt.table;
            throw std::runtime_error("CREATE TABLE failed: table already exists: " + existingName);
        }

        TableMetadata meta = buildTableMetadata(stmt, cat);
        std::vector<std::string> headers;
        headers.reserve(meta.columns.size());
        for (const auto &c : meta.columns)
            headers.push_back(c.name);

        cat.upsertTable(stmt.table, meta);
        db.tables[stmt.table] = Table{};
        db.schemas[stmt.table] = headers;

        persistTable(stmt.table, cat, db);
        cat.save();
        std::cout << colorize("Table '" + stmt.table + "' created.", kAnsiGreen) << '\n';
    }

    std::size_t executeInsert(const InsertStmt &stmt, Database &db, const MetadataCatalog &cat){
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
            throw std::runtime_error("Unknown table in INSERT: " + stmt.table);
        if (stmt.valueRows.empty())
            throw std::runtime_error("INSERT requires at least one VALUES row");

        const auto &tm = cat.getTable(stmt.table);

        // Determine target column list.
        std::vector<std::string> targetCols;
        if (stmt.columns.empty()){
            targetCols.reserve(tm.columns.size());
            for (const auto &c : tm.columns)
                targetCols.push_back(c.name);
        }
        else{
            targetCols = stmt.columns;
        }

        // Validate column names (and duplicates) up front.
        std::unordered_set<std::string> seenCols;
        for (const auto &cn : targetCols){
            if (!seenCols.insert(cn).second)
                throw std::runtime_error("Duplicate column in INSERT list: " + cn);
            if (!findColumn(tm, cn))
                throw std::runtime_error("Unknown column in INSERT list: " + cn);
        }

        // Build candidate rows (copy existing, append new).
        Table candidateRows = db.getTable(stmt.table);
        std::size_t inserted = 0;

        for (const auto &valRow : stmt.valueRows){
            if (targetCols.size() != valRow.size())
                throw std::runtime_error("INSERT column/value count mismatch: " + std::to_string(targetCols.size()) + " columns, " + std::to_string(valRow.size()) + " values");

            Row newRow;
            newRow.reserve(tm.columns.size());
            for (const auto &c : tm.columns)
                newRow[c.name] = "";

            const Row emptyCtx;
            for (std::size_t i = 0; i < targetCols.size(); ++i){
                newRow[targetCols[i]] = ExpressionEvaluator::eval(valRow[i].get(), emptyCtx);
            }
            candidateRows.push_back(std::move(newRow));
            ++inserted;
        }

        validateTableState(stmt.table, candidateRows, cat, db);
        db.getTable(stmt.table) = std::move(candidateRows);
        return inserted;
    }

    std::size_t executeUpdate(const UpdateStmt &stmt, Database &db, const MetadataCatalog &cat){
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
            throw std::runtime_error("Unknown table in UPDATE: " + stmt.table);
        if (stmt.assignments.empty())
            throw std::runtime_error("UPDATE requires at least one SET assignment");

        const auto &tm = cat.getTable(stmt.table);
        for (const auto &a : stmt.assignments){
            if (!findColumn(tm, a.column))
                throw std::runtime_error("Unknown column in UPDATE SET: " + a.column);
        }

        const Table &current = db.getTable(stmt.table);
        Table candidate = current;
        std::size_t updated = 0;

        for (std::size_t i = 0; i < candidate.size(); ++i){
            if (stmt.where && !ExpressionEvaluator::evalPredicate(stmt.where.get(), current[i]))
                continue;
            for (const auto &a : stmt.assignments){
                candidate[i][a.column] = ExpressionEvaluator::eval(a.value.get(), current[i]);
            }
            ++updated;
        }

        if (updated == 0)
            return 0;
        validateTableState(stmt.table, candidate, cat, db);
        db.getTable(stmt.table) = std::move(candidate);
        return updated;
    }

    std::size_t executeDelete(const DeleteStmt &stmt, Database &db, const MetadataCatalog &cat){
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
            throw std::runtime_error("Unknown table in DELETE: " + stmt.table);

        const Table &current = db.getTable(stmt.table);
        Table kept, deleted;

        for (const auto &row : current){
            const bool matches = !stmt.where || ExpressionEvaluator::evalPredicate(stmt.where.get(), row);
            if (matches)
                deleted.push_back(row);
            else
                kept.push_back(row);
        }

        if (deleted.empty())
            return 0;
        enforceDeleteConstraints(stmt.table, deleted, cat, db);
        db.getTable(stmt.table) = std::move(kept);
        return deleted.size();
    }

    // Central dispatch
    struct LocalErrorLocation{
        bool has{false};
        std::size_t line{1};
        std::size_t column{1};
    };

    LocalErrorLocation extractLocalErrorLocation(const std::string &message){
        static const std::regex kLoc(R"(line\s+([0-9]+)\s*,\s*column\s+([0-9]+))", std::regex::icase);
        std::smatch match;
        if (!std::regex_search(message, match, kLoc) || match.size() != 3){
            return {};
        }

        LocalErrorLocation loc;
        loc.has = true;
        loc.line = static_cast<std::size_t>(std::stoull(match[1].str()));
        loc.column = static_cast<std::size_t>(std::stoull(match[2].str()));
        return loc;
    }

    std::string formatScriptParseError(const SqlStatement &stmt, const std::string &message){
        std::size_t line = stmt.startLine;
        std::size_t column = stmt.startColumn;

        const LocalErrorLocation local = extractLocalErrorLocation(message);
        if (local.has){
            line = stmt.startLine + (local.line - 1);
            column = (local.line == 1) ? stmt.startColumn + (local.column - 1) : local.column;
        }

        static const std::regex kLocSuffix(R"(\s*at\s+line\s+[0-9]+\s*,\s*column\s+[0-9]+)", std::regex::icase);
        const std::string cleanedMessage = std::regex_replace(message, kLocSuffix, "");

        return "Parse error at Ln " + std::to_string(line) + ", Col " + std::to_string(column) + ": " + cleanedMessage;
    }

    Statement parseStatementInternal(const std::string &query){
        try{
            Lexer lexer(query);
            Parser parser(lexer.tokenize());
            return parser.parseStatement();
        }
        catch (const std::exception &ex){
            throw std::runtime_error(std::string(ex.what()));
        }
    }

    void executeParsedStatementInternal(Statement &stmt, Database &db, MetadataCatalog &cat){
        switch (stmt.type){

        case StatementType::SELECT:{
            if (!stmt.select)
                throw std::runtime_error("Internal: missing SELECT payload");

            const auto canonFrom = findTableCI(cat, stmt.select->from.table);
            if (!canonFrom){
                throw std::runtime_error("Unknown table in SELECT: " + stmt.select->from.table);
            }
            stmt.select->from.table = *canonFrom;
            stmt.select->table = *canonFrom;

            for (auto &join : stmt.select->joins){
                const auto canonJoin = findTableCI(cat, join.right.table);
                if (!canonJoin){
                    throw std::runtime_error("Unknown table in JOIN: " + join.right.table);
                }
                join.right.table = *canonJoin;
            }

            executeSelect(*stmt.select, db);
            break;
        }

        case StatementType::CREATE_TABLE:
            if (!stmt.createTable)
                throw std::runtime_error("Internal: missing CREATE TABLE payload");
            executeCreateTable(*stmt.createTable, db, cat);
            break;

        case StatementType::INSERT:{
            if (!stmt.insert)
                throw std::runtime_error("Internal: missing INSERT payload");
            const auto canon = findTableCI(cat, stmt.insert->table);
            if (!canon)
                throw std::runtime_error("Unknown table in INSERT: " + stmt.insert->table);
            stmt.insert->table = *canon;
            const auto n = executeInsert(*stmt.insert, db, cat);
            persistTable(*canon, cat, db);
            const std::string message = "(" + std::to_string(n) + ' ' + (n == 1 ? "row" : "rows") + " inserted)";
            std::cout << colorize(message, n == 0 ? kAnsiYellow : kAnsiGreen) << '\n';
            break;
        }

        case StatementType::UPDATE:{
            if (!stmt.update)
                throw std::runtime_error("Internal: missing UPDATE payload");
            const auto canon = findTableCI(cat, stmt.update->table);
            if (!canon)
                throw std::runtime_error("Unknown table in UPDATE: " + stmt.update->table);
            stmt.update->table = *canon;
            const auto n = executeUpdate(*stmt.update, db, cat);
            persistTable(*canon, cat, db);
            const std::string message = "(" + std::to_string(n) + ' ' + (n == 1 ? "row" : "rows") + " updated)";
            std::cout << colorize(message, n == 0 ? kAnsiYellow : kAnsiGreen) << '\n';
            break;
        }

        case StatementType::DELETE_:{
            if (!stmt.deleteStmt)
                throw std::runtime_error("Internal: missing DELETE payload");
            const auto canon = findTableCI(cat, stmt.deleteStmt->table);
            if (!canon)
                throw std::runtime_error("Unknown table in DELETE: " + stmt.deleteStmt->table);
            stmt.deleteStmt->table = *canon;
            const auto n = executeDelete(*stmt.deleteStmt, db, cat);
            persistTable(*canon, cat, db);
            const std::string message = "(" + std::to_string(n) + ' ' + (n == 1 ? "row" : "rows") + " deleted)";
            std::cout << colorize(message, n == 0 ? kAnsiYellow : kAnsiGreen) << '\n';
            break;
        }

        default:
            throw std::runtime_error("Unsupported statement type");
        }
    }

    void executeStatementInternal(const std::string &query, Database &db, MetadataCatalog &cat){
        Statement stmt = parseStatementInternal(query);
        executeParsedStatementInternal(stmt, db, cat);
    }

    std::string resolveDataDirImpl(){
        for (const std::string& p : {std::string(DATADIR), std::string("../") + DATADIR}){
            std::error_code ec;
            if (fs::exists(p, ec) && fs::is_directory(p, ec))
                return p;
        }
        // Create "data/" in the working directory as a fallback.
        const fs::path fallback = DATADIR;
        std::error_code ec;
        fs::create_directories(fallback, ec);
        if (ec)
            throw std::runtime_error("Cannot create data directory: " + ec.message());
        return fallback.string();
    }

}

// QueryEngineApp
QueryEngineApp::QueryEngineApp(std::string dataDirectory) : catalog_(std::move(dataDirectory)) {}

std::string QueryEngineApp::resolveDataDirectory(){
    return resolveDataDirImpl();
}

void QueryEngineApp::initialize(){
    bootstrapDatabase(db_, catalog_);
}

void QueryEngineApp::executeStatement(const std::string &query){
    executeStatementInternal(query, db_, catalog_);
}

void QueryEngineApp::executeScript(const std::vector<SqlStatement> &statements){
    if (statements.empty())
        return;

    std::vector<Statement> parsedStatements;
    parsedStatements.reserve(statements.size());

    // Parse the complete script. Abort on the first parse error.
    for (std::size_t i = 0; i < statements.size(); ++i){
        try{
            parsedStatements.push_back(parseStatementInternal(statements[i].text));
        }
        catch (const std::exception &ex){
            throw std::runtime_error(formatScriptParseError(statements[i], ex.what()));
        }
    }

    // Execute statements independently after full parse success.
    std::size_t successCount = 0;
    std::size_t failureCount = 0;

    for (std::size_t i = 0; i < parsedStatements.size(); ++i){
        try{
            executeParsedStatementInternal(parsedStatements[i], db_, catalog_);
            ++successCount;
        }
        catch (const std::exception &ex){
            ++failureCount;
            const std::string errorMessage = "Execution failed (Ln " + std::to_string(statements[i].startLine) + ", Col " + std::to_string(statements[i].startColumn) + "): " + ex.what();
            std::cerr << colorize(errorMessage, kAnsiRed) << '\n';
        }

        if (i + 1 < parsedStatements.size()){
            std::cout << '\n';
        }
    }

    if (failureCount > 0){
        throw std::runtime_error("Execution summary: " + std::to_string(successCount) + " succeeded, " + std::to_string(failureCount) + " failed.");
    }

    std::cout << colorize("Execution summary: all " + std::to_string(successCount) + " statement(s) succeeded.", kAnsiGreen) << '\n';
}