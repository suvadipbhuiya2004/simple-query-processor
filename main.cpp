#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "execution/executor_builder.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/planner.h"
#include "storage/csv_loader.h"
#include "storage/table.h"

namespace {
std::string Trim(std::string text) {
    const std::string whitespace = " \t\n\r\f\v";
    const size_t start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }

    const size_t end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open SQL file: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<std::string> SplitSqlStatements(const std::string& sqlContent) {
    std::vector<std::string> statements;
    std::string current;
    bool inStringLiteral = false;
    bool inLineComment = false;
    bool inBlockComment = false;

    for (size_t i = 0; i < sqlContent.size(); ++i) {
        const char c = sqlContent[i];
        const char next = (i + 1 < sqlContent.size()) ? sqlContent[i + 1] : '\0';

        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
                current += c;
            }
            continue;
        }

        if (inBlockComment) {
            if (c == '*' && next == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (c == '\'') {
            current += c;

            if (inStringLiteral && next == '\'') {
                current += next;
                ++i;
            } else {
                inStringLiteral = !inStringLiteral;
            }

            continue;
        }

        if (!inStringLiteral && c == '-' && next == '-') {
            inLineComment = true;
            ++i;
            continue;
        }

        if (!inStringLiteral && c == '/' && next == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }

        if (c == ';' && !inStringLiteral) {
            const std::string statement = Trim(current);
            if (!statement.empty()) {
                statements.push_back(statement);
            }
            current.clear();
            continue;
        }

        current += c;
    }

    if (inStringLiteral) {
        throw std::runtime_error("Unterminated string literal in SQL file");
    }

    if (inBlockComment) {
        throw std::runtime_error("Unterminated block comment in SQL file");
    }

    const std::string trailingStatement = Trim(current);
    if (!trailingStatement.empty()) {
        statements.push_back(trailingStatement);
    }

    return statements;
}

std::string ResolveSqlFilePath(int argc, char* argv[]) {
    (void)argv;

    if (argc != 1) {
        throw std::runtime_error("Custom query input is disabled. Put one or more statements in queries.sql and run without arguments.");
    }

    const std::vector<std::string> candidatePaths = {
        "queries.sql",
        "../queries.sql"
    };

    for (const auto& path : candidatePaths) {
        std::ifstream file(path);
        if (file.is_open()) {
            return path;
        }
    }

    throw std::runtime_error("Could not find queries.sql. Create it in project root or run from build directory.");
}

std::vector<std::string> LoadQueries(int argc, char* argv[]) {
    const std::string sqlFilePath = ResolveSqlFilePath(argc, argv);
    const std::string sqlContent = ReadTextFile(sqlFilePath);
    std::vector<std::string> queries = SplitSqlStatements(sqlContent);

    if (queries.empty()) {
        throw std::runtime_error("No executable SQL statements found in file: " + sqlFilePath);
    }

    return queries;
}

bool IsSelectStar(const SelectStmt& stmt) {
    if (stmt.columns.size() != 1) {
        return false;
    }

    const auto* column = dynamic_cast<const Column*>(stmt.columns[0].get());
    return column != nullptr && column->name == "*";
}

std::vector<std::string> OrderedColumnsFromStatement(const SelectStmt& stmt, const Database& db) {
    if (IsSelectStar(stmt)) {
        if (db.hasSchema(stmt.table)) {
            return db.getSchema(stmt.table);
        }
        throw std::runtime_error("Could not determine column order for SELECT * on table: " + stmt.table);
    }

    std::vector<std::string> columns;
    columns.reserve(stmt.columns.size());

    for (const auto& expr : stmt.columns) {
        const auto* column = dynamic_cast<const Column*>(expr.get());
        if (column == nullptr) {
            throw std::runtime_error("Only column projection is supported in SELECT list");
        }
        columns.push_back(column->name);
    }

    return columns;
}

bool IsNumericValue(const std::string& value) {
    static const std::regex kNumericPattern(R"(^[+-]?(\d+(\.\d+)?|\.\d+)$)");
    return std::regex_match(value, kNumericPattern);
}

std::vector<bool> InferNumericColumns(const std::vector<std::vector<std::string>>& rows,
                                      const std::vector<std::string>& columns) {
    std::vector<bool> numeric(columns.size(), true);

    for (size_t columnIndex = 0; columnIndex < columns.size(); ++columnIndex) {
        for (const auto& row : rows) {
            if (columnIndex >= row.size()) {
                numeric[columnIndex] = false;
                break;
            }
            if (!row[columnIndex].empty() && !IsNumericValue(row[columnIndex])) {
                numeric[columnIndex] = false;
                break;
            }
        }
    }

    return numeric;
}

void PrintTable(const std::vector<std::string>& columns, const std::vector<std::vector<std::string>>& rows) {
    if (columns.empty()) {
        std::cout << "(0 tuples)\n";
        return;
    }

    std::vector<size_t> widths(columns.size(), 0);
    for (size_t i = 0; i < columns.size(); ++i) {
        widths[i] = columns[i].size();
    }

    for (const auto& row : rows) {
        for (size_t i = 0; i < columns.size() && i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    const std::vector<bool> numericColumns = InferNumericColumns(rows, columns);

    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) {
            std::cout << " | ";
        }
        std::cout << std::left << std::setw(static_cast<int>(widths[i])) << columns[i];
    }
    std::cout << '\n';

    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) {
            std::cout << "-+-";
        }
        std::cout << std::string(widths[i], '-');
    }
    std::cout << '\n';

    for (const auto& row : rows) {
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) {
                std::cout << " | ";
            }

            const std::string value = i < row.size() ? row[i] : "";
            if (numericColumns[i]) {
                std::cout << std::right << std::setw(static_cast<int>(widths[i])) << value;
            } else {
                std::cout << std::left << std::setw(static_cast<int>(widths[i])) << value;
            }
        }
        std::cout << '\n';
    }

    const size_t tupleCount = rows.size();
    std::cout << '\n'
              << "(" << tupleCount << ' ' << (tupleCount == 1 ? "tuple" : "tuples") << ")\n";
}

// Try common relative paths so run works from root and build directory.
void LoadUsersTable(Database& db) {
    const std::vector<std::string> candidatePaths = {
        "data/users.db",
        "../data/users.db"
    };

    std::string attemptedPaths;
    attemptedPaths.reserve(128);

    for (const auto& path : candidatePaths) {
        try {
            CsvLoader::loadIntoDatabase(db, "users", path);
            return;
        } catch (const std::exception&) {
            if (!attemptedPaths.empty()) {
                attemptedPaths += ", ";
            }
            attemptedPaths += path;
        }
    }

    throw std::runtime_error("Could not load users table file. Tried paths: " + attemptedPaths);
}

void ExecuteQuery(const std::string& query, Database& db) {
    Lexer lexer(query);
    const auto tokens = lexer.tokenize();

    Parser parser(tokens);
    SelectStmt ast = parser.parse();
    const std::vector<std::string> orderedColumns = OrderedColumnsFromStatement(ast, db);

    Planner planner;
    auto plan = planner.createPlan(ast);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<std::vector<std::string>> rows;
    while (executor->next(row)) {
        std::vector<std::string> printableRow;
        printableRow.reserve(orderedColumns.size());
        for (const auto& column : orderedColumns) {
            const auto it = row.find(column);
            if (it == row.end()) {
                throw std::runtime_error("Column not found in result row: " + column);
            }
            printableRow.push_back(it->second);
        }

        rows.push_back(std::move(printableRow));
    }

    executor->close();

    PrintTable(orderedColumns, rows);
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        // 1) Load sample data.
        Database db;
        LoadUsersTable(db);

        // 2) Load one or more statements from a .sql file.
        const std::vector<std::string> queries = LoadQueries(argc, argv);

        // 3) Execute each statement in order.
        for (size_t i = 0; i < queries.size(); ++i) {
            ExecuteQuery(queries[i], db);

            if (i + 1 < queries.size()) {
                std::cout << '\n';
            }
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Query execution error: " << ex.what() << '\n';
        return 1;
    }
}