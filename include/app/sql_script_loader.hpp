#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct SqlStatement {
    std::string text;
    std::size_t startLine{1};
    std::size_t startColumn{1};
};

// ---------------------------------------------------------------------------
// locates and splits a SQL script file into individual statements ready for execution
//
// Statement splitting rules:
//   • ';' is the statement terminator.
//   • Single-quoted string literals are handled ('' escape inside strings).
//   • -- line comments and /* block comments */ are stripped.
//   • Blank / whitespace-only statements are silently skipped.
// ---------------------------------------------------------------------------
class SqlScriptLoader {
public:
    static std::vector<SqlStatement> loadStatements(int argc, char* argv[]);
    static std::vector<std::string> loadQueries(int argc, char* argv[]);
};