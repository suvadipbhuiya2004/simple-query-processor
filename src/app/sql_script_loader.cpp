#include "app/sql_script_loader.hpp"
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    std::string trim(const std::string &s)
    {
        const std::string ws = " \t\n\r\f\v";
        const auto start = s.find_first_not_of(ws);
        if (start == std::string::npos)
            return {};
        const auto end = s.find_last_not_of(ws);
        return s.substr(start, end - start + 1);
    }

    std::string readFile(const std::string &path)
    {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("Cannot open SQL file: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // Split SQL text on ';' boundaries, honouring:
    //   • single-quoted string literals (with '' escape for embedded quotes)
    //   • -- line comments
    //   • /* block comments */
    std::vector<SqlStatement> splitStatements(const std::string &sql)
    {
        std::vector<SqlStatement> stmts;
        std::string current;
        bool inString = false;
        bool inLineComment = false;
        bool inBlockComment = false;

        std::size_t line = 1;
        std::size_t column = 1;
        std::size_t stmtStartLine = 1;
        std::size_t stmtStartColumn = 1;
        bool hasStmtStart = false;

        auto advancePos = [&](char ch)
        {
            if (ch == '\n')
            {
                ++line;
                column = 1;
            }
            else
            {
                ++column;
            }
        };

        for (std::size_t i = 0; i < sql.size(); ++i)
        {
            const char c = sql[i];
            const char next = (i + 1 < sql.size()) ? sql[i + 1] : '\0';

            //  Inside a line comment
            if (inLineComment)
            {
                if (c == '\n')
                {
                    inLineComment = false;
                    if (hasStmtStart)
                        current += c;
                }
                advancePos(c);
                continue;
            }

            //  Inside a block comment
            if (inBlockComment)
            {
                if (c == '*' && next == '/')
                {
                    inBlockComment = false;
                    advancePos(c);
                    advancePos(next);
                    ++i;
                    continue;
                }
                if (c == '\n' && hasStmtStart)
                    current += c;
                advancePos(c);
                continue;
            }

            //  Inside a string literal
            if (inString)
            {
                if (!hasStmtStart)
                {
                    hasStmtStart = true;
                    stmtStartLine = line;
                    stmtStartColumn = column;
                }
                current += c;
                if (c == '\'')
                {
                    advancePos(c);
                    if (next == '\'')
                    {
                        current += next;
                        advancePos(next);
                        ++i;
                    }
                    else
                    {
                        inString = false;
                    }
                    continue;
                }
                advancePos(c);
                continue;
            }

            //  Normal context
            if (c == '\'')
            {
                if (!hasStmtStart)
                {
                    hasStmtStart = true;
                    stmtStartLine = line;
                    stmtStartColumn = column;
                }
                inString = true;
                current += c;
                advancePos(c);
                continue;
            }
            if (c == '-' && next == '-')
            {
                inLineComment = true;
                advancePos(c);
                advancePos(next);
                ++i;
                continue;
            }
            if (c == '/' && next == '*')
            {
                inBlockComment = true;
                advancePos(c);
                advancePos(next);
                ++i;
                continue;
            }
            if (c == ';')
            {
                const std::string s = trim(current);
                if (!s.empty())
                {
                    stmts.push_back(SqlStatement{
                        s,
                        stmtStartLine,
                        stmtStartColumn,
                    });
                }
                current.clear();
                hasStmtStart = false;
                advancePos(c);
                continue;
            }

            if (!hasStmtStart && !std::isspace(static_cast<unsigned char>(c)))
            {
                hasStmtStart = true;
                stmtStartLine = line;
                stmtStartColumn = column;
            }

            current += c;
            advancePos(c);
        }

        if (inString)
        {
            throw std::runtime_error("Unterminated string literal in SQL file");
        }
        if (inBlockComment)
        {
            throw std::runtime_error("Unterminated block comment in SQL file");
        }
        const std::string trailing = trim(current);
        if (!trailing.empty())
        {
            stmts.push_back(SqlStatement{
                trailing,
                stmtStartLine,
                stmtStartColumn,
            });
        }
        return stmts;
    }

    // Locate queries.sql: explicit path from argv[1], or check well-known locations.
    std::string resolveSqlFile(int argc, char * /*argv*/[])
    {
        // argc == 1 means only the program name — no extra args expected.
        if (argc != 1)
        {
            throw std::runtime_error("Usage: run without arguments. Place SQL statements in queries.sql next to the executable.");
        }

        for (const char *p : {"queries.sql", "../queries.sql"})
        {
            std::ifstream f(p);
            if (f.is_open())
                return p;
        }
        throw std::runtime_error(
            "Cannot find queries.sql. Create it in the project root or next to the executable.");
    }

}

// SqlScriptLoader

std::vector<SqlStatement> SqlScriptLoader::loadStatements(int argc, char *argv[])
{
    const std::string path = resolveSqlFile(argc, argv);
    const std::string content = readFile(path);
    auto statements = splitStatements(content);
    if (statements.empty())
        throw std::runtime_error("No executable SQL statements found in: " + path);
    return statements;
}

std::vector<std::string> SqlScriptLoader::loadQueries(int argc, char *argv[])
{
    const auto statements = loadStatements(argc, argv);
    std::vector<std::string> queries;
    queries.reserve(statements.size());
    for (const auto &stmt : statements)
        queries.push_back(stmt.text);
    return queries;
}