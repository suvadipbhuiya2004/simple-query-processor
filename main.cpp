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
void ExecuteQuery(const std::string& query, Database& db);

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

struct CliOptions {
    bool showHelp{false};
    bool replMode{false};
    std::string queryText;
    std::string filePath;
};

void PrintUsage(const char* programName) {
    std::cout << "Usage:\n"
              << "  " << programName << " [--file <path>]\n"
              << "  " << programName << " [--query <sql>]\n"
              << "  " << programName << " [--repl]\n"
              << "  " << programName << " [<sql-file>]\n\n"
              << "Options:\n"
              << "  -f, --file <path>   Run statements from a SQL file\n"
              << "  -q, --query <sql>   Run a single SQL command or a semicolon-separated set\n"
              << "  -i, --repl          Start an interactive SQL prompt\n"
              << "  -h, --help          Show this help message\n\n"
              << "Examples:\n"
              << "  " << programName << " --query \"SELECT * FROM users;\"\n"
              << "  " << programName << " --file queries.sql\n"
              << "  " << programName << " queries.sql\n";
}

CliOptions ParseCommandLine(int argc, char* argv[]) {
    CliOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "-h" || argument == "--help") {
            options.showHelp = true;
            continue;
        }

        if (argument == "-i" || argument == "--repl") {
            if (!options.queryText.empty() || !options.filePath.empty() || options.replMode) {
                throw std::runtime_error("Use only one input mode: --query, --file, --repl, or a positional file path");
            }
            options.replMode = true;
            continue;
        }

        if (argument == "-q" || argument == "--query") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing SQL text after --query");
            }
            if (!options.queryText.empty() || !options.filePath.empty() || options.replMode) {
                throw std::runtime_error("Use only one input mode: --query, --file, --repl, or a positional file path");
            }
            options.queryText = argv[++i];
            continue;
        }

        if (argument == "-f" || argument == "--file") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing file path after --file");
            }
            if (!options.queryText.empty() || !options.filePath.empty() || options.replMode) {
                throw std::runtime_error("Use only one input mode: --query, --file, --repl, or a positional file path");
            }
            options.filePath = argv[++i];
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            throw std::runtime_error("Unknown command line option: " + argument);
        }

        if (!options.queryText.empty() || !options.filePath.empty() || options.replMode) {
            throw std::runtime_error("Use only one input mode: --query, --file, --repl, or a positional file path");
        }

        options.filePath = argument;
    }

    return options;
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

std::vector<std::string> LoadQueriesFromFile(const std::string& sqlFilePath) {
    const std::string sqlContent = ReadTextFile(sqlFilePath);
    std::vector<std::string> queries = SplitSqlStatements(sqlContent);

    if (queries.empty()) {
        throw std::runtime_error("No executable SQL statements found in file: " + sqlFilePath);
    }

    return queries;
}

std::vector<std::string> LoadQueriesFromCli(const CliOptions& options) {
    if (!options.queryText.empty()) {
        std::vector<std::string> queries = SplitSqlStatements(options.queryText);
        if (queries.empty()) {
            throw std::runtime_error("No executable SQL statements found in --query input");
        }
        return queries;
    }

    if (!options.filePath.empty()) {
        return LoadQueriesFromFile(options.filePath);
    }

    if (options.replMode) {
        return {};
    }

    const std::vector<std::string> candidatePaths = {
        "queries.sql",
        "../queries.sql"
    };

    for (const auto& path : candidatePaths) {
        std::ifstream file(path);
        if (file.is_open()) {
            return LoadQueriesFromFile(path);
        }
    }

    throw std::runtime_error("Could not find queries.sql. Create it in project root or run from build directory.");
}

void ExecuteSqlStatements(const std::vector<std::string>& queries, Database& db) {
    for (size_t i = 0; i < queries.size(); ++i) {
        ExecuteQuery(queries[i], db);

        if (i + 1 < queries.size()) {
            std::cout << '\n';
        }
    }
}

void RunRepl(Database& db) {
    std::cout << "Simple Query Processor REPL\n"
              << "Type SQL and end each statement with ';'. Type 'quit' or 'exit' to leave.\n";

    std::string buffer;
    std::string line;

    while (true) {
        std::cout << (buffer.empty() ? "sql> " : "...> ") << std::flush;

        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        const std::string trimmedLine = Trim(line);
        if (buffer.empty() && (trimmedLine == "quit" || trimmedLine == "exit")) {
            break;
        }

        if (!buffer.empty()) {
            buffer += '\n';
        }
        buffer += line;

        if (buffer.find(';') == std::string::npos) {
            continue;
        }

        const std::vector<std::string> queries = SplitSqlStatements(buffer);
        buffer.clear();

        for (size_t i = 0; i < queries.size(); ++i) {
            try {
                ExecuteQuery(queries[i], db);
            } catch (const std::exception& ex) {
                std::cerr << "Query execution error: " << ex.what() << '\n';
            }

            if (i + 1 < queries.size()) {
                std::cout << '\n';
            }
        }
    }
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
        const CliOptions options = ParseCommandLine(argc, argv);

        if (options.showHelp) {
            PrintUsage(argv[0]);
            return 0;
        }

        // 1) Load sample data.
        Database db;
        LoadUsersTable(db);

        if (options.replMode) {
            RunRepl(db);
            return 0;
        }

        // 2) Load one or more statements from the selected input source.
        const std::vector<std::string> queries = LoadQueriesFromCli(options);

        // 3) Execute each statement in order.
        ExecuteSqlStatements(queries, db);

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Query execution error: " << ex.what() << '\n';
        return 1;
    }
}