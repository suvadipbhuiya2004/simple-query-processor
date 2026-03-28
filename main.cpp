#include <algorithm>
#include <iostream>
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
// Build a query string from command-line args, or use default demo query.
std::string BuildQueryFromArgs(int argc, char* argv[]) {
    if (argc <= 1) {
        return "SELECT name FROM users WHERE age > 30";
    }

    std::ostringstream queryBuilder;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) {
            queryBuilder << ' ';
        }
        queryBuilder << argv[i];
    }

    return queryBuilder.str();
}

// Print row in deterministic key order for stable output.
void PrintRow(const Row& row) {
    std::vector<std::string> keys;
    keys.reserve(row.size());

    for (const auto& [key, _] : row) {
        keys.push_back(key);
    }

    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        std::cout << key << ": " << row.at(key) << ' ';
    }

    std::cout << '\n';
}

// Try common relative paths so run works from root and build directory.
void LoadUsersTable(Database& db) {
    const std::vector<std::string> candidatePaths = {
        "data/users.csv",
        "../data/users.csv"
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

    throw std::runtime_error("Could not load users CSV. Tried paths: " + attemptedPaths);
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        // 1) Load sample data.
        Database db;
        LoadUsersTable(db);

        // 2) Parse input query.
        const std::string query = BuildQueryFromArgs(argc, argv);

        Lexer lexer(query);
        const auto tokens = lexer.tokenize();

        Parser parser(tokens);
        SelectStmt ast = parser.parse();

        // 3) Plan + build execution pipeline.
        Planner planner;
        auto plan = planner.createPlan(ast);

        auto executor = ExecutorBuilder::build(plan.get(), db);
        executor->open();

        // 4) Execute and print rows.
        Row row;
        bool hasRows = false;
        while (executor->next(row)) {
            hasRows = true;
            PrintRow(row);
        }

        executor->close();

        if (!hasRows) {
            std::cout << "No rows matched.\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Query execution error: " << ex.what() << '\n';
        return 1;
    }
}