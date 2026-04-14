#include "app/cli/query_engine_cli.hpp"

#include "app/cli/line_editor.hpp"
#include "app/sql_script_loader.hpp"
#include "common/ansi.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    std::string trim(const std::string &value)
    {
        const auto isWs = [](unsigned char ch)
        { return std::isspace(ch) != 0; };
        const auto begin = std::find_if_not(value.begin(), value.end(), isWs);
        const auto end = std::find_if_not(value.rbegin(), value.rend(), isWs).base();
        if (begin >= end)
        {
            return {};
        }
        return std::string(begin, end);
    }

    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string stripWrappingQuotes(std::string value)
    {
        if (value.size() >= 2)
        {
            const char first = value.front();
            const char last = value.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
            {
                return value.substr(1, value.size() - 2);
            }
        }
        return value;
    }

    void eraseAllSubstrings(std::string &text, const std::string &needle)
    {
        if (needle.empty())
        {
            return;
        }

        std::size_t pos = text.find(needle);
        while (pos != std::string::npos)
        {
            text.erase(pos, needle.size());
            pos = text.find(needle, pos);
        }
    }

    std::string normalizeReplLine(std::string line)
    {
        // Terminals with bracketed paste enabled wrap pasted content in ESC[200~ ... ESC[201~.
        eraseAllSubstrings(line, "\x1b[200~");
        eraseAllSubstrings(line, "\x1b[201~");

        // Windows-style CRLF pastes can inject '\r' before '\n'; strip it for parser stability.
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

        // Some terminals can collapse pasted line breaks and glue clause keywords to identifiers
        // (for example: "usernameFROM users"). Repair only safe keyword boundaries.
        static const std::vector<std::string> kClauseKeywords = {
            "FROM", "WHERE", "GROUP", "ORDER", "HAVING", "JOIN", "LEFT", "RIGHT",
            "FULL", "INNER", "CROSS", "LIMIT", "UNION", "EXISTS", "SELECT", "PATH",
            "ON", "BY", "AND", "OR"};

        auto isWordChar = [](char ch)
        {
            const unsigned char u = static_cast<unsigned char>(ch);
            return std::isalnum(u) != 0 || ch == '_';
        };
        auto startsWithKeywordCI = [](const std::string &src, std::size_t pos, const std::string &keyword)
        {
            if (pos + keyword.size() > src.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < keyword.size(); ++i)
            {
                const unsigned char a = static_cast<unsigned char>(src[pos + i]);
                const unsigned char b = static_cast<unsigned char>(keyword[i]);
                if (std::toupper(a) != std::toupper(b))
                {
                    return false;
                }
            }
            return true;
        };

        std::string repaired;
        repaired.reserve(line.size() + 8);

        bool inSingleQuote = false;
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char ch = line[i];

            if (ch == '\'')
            {
                repaired.push_back(ch);
                if (inSingleQuote)
                {
                    if (i + 1 < line.size() && line[i + 1] == '\'')
                    {
                        repaired.push_back(line[i + 1]);
                        ++i;
                    }
                    else
                    {
                        inSingleQuote = false;
                    }
                }
                else
                {
                    inSingleQuote = true;
                }
                continue;
            }

            if (!inSingleQuote && !repaired.empty())
            {
                const char prevOut = repaired.back();
                if (isWordChar(prevOut) || prevOut == ')')
                {
                    for (const auto &keyword : kClauseKeywords)
                    {
                        if (!startsWithKeywordCI(line, i, keyword))
                        {
                            continue;
                        }
                        const std::size_t end = i + keyword.size();
                        if (end < line.size() && isWordChar(line[end]))
                        {
                            continue;
                        }
                        repaired.push_back(' ');
                        break;
                    }
                }
            }

            repaired.push_back(ch);
        }

        line = std::move(repaired);
        return line;
    }

    std::string normalizeHistoryEntry(std::string sql)
    {
        for (char &ch : sql)
        {
            if (ch == '\n' || ch == '\r' || ch == '\t')
            {
                ch = ' ';
            }
        }

        std::string compact;
        compact.reserve(sql.size());
        bool prevSpace = false;
        for (const char ch : sql)
        {
            const bool isSpace = std::isspace(static_cast<unsigned char>(ch)) != 0;
            if (isSpace)
            {
                if (!prevSpace)
                {
                    compact.push_back(' ');
                }
                prevSpace = true;
            }
            else
            {
                compact.push_back(ch);
                prevSpace = false;
            }
        }

        compact = trim(compact);
        if (!compact.empty() && compact.back() != ';')
        {
            compact.push_back(';');
        }
        return compact;
    }

    bool containsStatementTerminator(const std::string &sqlText)
    {
        bool inString = false;
        bool inLineComment = false;
        bool inBlockComment = false;

        for (std::size_t i = 0; i < sqlText.size(); ++i)
        {
            const char c = sqlText[i];
            const char next = (i + 1 < sqlText.size()) ? sqlText[i + 1] : '\0';

            if (inLineComment)
            {
                if (c == '\n')
                {
                    inLineComment = false;
                }
                continue;
            }

            if (inBlockComment)
            {
                if (c == '*' && next == '/')
                {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }

            if (inString)
            {
                if (c == '\'' && next == '\'')
                {
                    ++i;
                    continue;
                }
                if (c == '\'')
                {
                    inString = false;
                }
                continue;
            }

            if (c == '\'')
            {
                inString = true;
                continue;
            }
            if (c == '-' && next == '-')
            {
                inLineComment = true;
                ++i;
                continue;
            }
            if (c == '/' && next == '*')
            {
                inBlockComment = true;
                ++i;
                continue;
            }
            if (c == ';')
            {
                return true;
            }
        }

        return false;
    }

    std::string colorize(const std::string &text, const char *color)
    {
        return ansi::colorize(text, color);
    }

}

QueryEngineCli::QueryEngineCli(QueryEngineApp &app) : app_(app) {}

int QueryEngineCli::run(const CliOptions &options)
{
    switch (options.mode)
    {
    case CliMode::DefaultScript:
        return runDefaultScript();
    case CliMode::ScriptFile:
        return runScriptFile(options.sqlFilePath);
    case CliMode::SingleQuery:
        return runSingleQuery(options.queryText);
    case CliMode::Repl:
        return runRepl();
    case CliMode::Help:
        return 0;
    }

    throw std::runtime_error("Unsupported CLI mode.");
}

std::string QueryEngineCli::metaCommandHelp()
{
    return "REPL commands:\n"
           "  .help             Show REPL help\n"
           "  .tables           List loaded tables\n"
           "  .schema <table>   Show table schema\n"
           "  .run <file.sql>   Execute statements from file\n"
           "  .clear            Clear terminal screen\n"
           "  .quit | .exit     Exit terminal\n";
}

int QueryEngineCli::runDefaultScript() const
{
    const auto statements = SqlScriptLoader::loadDefaultStatements();
    app_.executeScript(statements);
    return 0;
}

int QueryEngineCli::runScriptFile(const std::string &path) const
{
    const auto statements = SqlScriptLoader::loadStatementsFromFile(path);
    app_.executeScript(statements);
    return 0;
}

int QueryEngineCli::runSingleQuery(const std::string &queryText) const
{
    const auto statements = SqlScriptLoader::splitStatements(queryText);
    if (statements.empty())
    {
        const std::string singleStatement = trim(queryText);
        if (singleStatement.empty())
        {
            throw std::runtime_error("No executable SQL statement found in --query input.");
        }
        app_.executeStatement(singleStatement);
        return 0;
    }

    for (std::size_t i = 0; i < statements.size(); ++i)
    {
        app_.executeStatement(statements[i].text);
        if (i + 1 < statements.size())
        {
            std::cout << '\n';
        }
    }
    return 0;
}

int QueryEngineCli::runRepl()
{
    printWelcome();

    ReplLineEditor lineEditor;
    std::string pendingSql;
    while (true)
    {
        const bool hasPendingSql = !trim(pendingSql).empty();
        const std::string prompt = colorize(hasPendingSql ? "...> " : "BongoDB> ", ansi::kBlue);

        std::string line;
        if (!lineEditor.readLine(prompt, line))
        {
            std::cout << '\n';
            break;
        }
        line = normalizeReplLine(std::move(line));

        const std::string trimmedLine = trim(line);
        if (!hasPendingSql && trimmedLine.empty())
        {
            continue;
        }

        if (!hasPendingSql && !trimmedLine.empty() && trimmedLine.front() == '.')
        {
            bool shouldExit = false;
            const bool handled = handleMetaCommand(trimmedLine, shouldExit);
            if (!handled)
            {
                std::cerr << colorize("Unknown command: " + trimmedLine + " (try .help)", ansi::kYellow) << '\n';
            }
            if (shouldExit)
            {
                break;
            }
            continue;
        }

        pendingSql += line;
        pendingSql.push_back('\n');

        if (!containsStatementTerminator(pendingSql))
        {
            continue;
        }

        try
        {
            const auto statements = SqlScriptLoader::splitStatements(pendingSql);
            if (statements.empty())
            {
                std::cout << colorize("No executable SQL statement found.", ansi::kYellow) << '\n';
            }
            for (std::size_t i = 0; i < statements.size(); ++i)
            {
                app_.executeStatement(statements[i].text);
                lineEditor.addHistoryEntry(normalizeHistoryEntry(statements[i].text));
                if (i + 1 < statements.size())
                {
                    std::cout << '\n';
                }
            }
        }
        catch (const std::exception &ex)
        {
            std::cerr << colorize(ex.what(), ansi::kRed) << '\n';
        }

        pendingSql.clear();
    }

    std::cout << colorize("Session closed.", ansi::kGray) << '\n';
    return 0;
}

void QueryEngineCli::printWelcome() const
{
    std::cout << "\n-----------------------------------------------------------------------------------------------\n\n";
    std::cout << colorize("Simple Query Processor Terminal", ansi::kBrinjal) << '\n';
    std::cout << colorize("Type SQL ending with ';' to execute. Use .help for commands.", ansi::kGray) << '\n';
    std::cout << "\n-----------------------------------------------------------------------------------------------\n";
}

void QueryEngineCli::printTables() const
{
    const auto summaries = app_.getTableSummaries();
    if (summaries.empty())
    {
        std::cout << colorize("No tables loaded.", ansi::kYellow) << '\n';
        return;
    }

    std::cout << colorize("Loaded tables:", ansi::kGreen) << '\n';
    for (const auto &table : summaries)
    {
        std::cout << "  " << colorize(table.name, ansi::kBlue) << "  (columns: " << table.columnCount << ", rows: " << table.rowCount << ")\n";
    }
}

void QueryEngineCli::printSchema(const std::string &tableName) const
{
    if (tableName.empty())
    {
        std::cerr << colorize("Usage: .schema <table>", ansi::kYellow) << '\n';
        return;
    }

    try
    {
        const TableMetadata metadata = app_.getTableMetadata(tableName);
        std::cout << colorize("Schema: " + tableName, ansi::kGreen) << '\n';

        for (const auto &column : metadata.columns)
        {
            std::vector<std::string> qualifiers;
            if (column.primaryKey)
            {
                qualifiers.push_back("PK");
            }
            if (column.unique)
            {
                qualifiers.push_back("UNIQUE");
            }
            if (column.notNull)
            {
                qualifiers.push_back("NOT NULL");
            }
            if (column.foreignKey.has_value())
            {
                qualifiers.push_back("FK->" + *column.foreignKey);
            }
            if (column.checkExpr.has_value())
            {
                qualifiers.push_back("CHECK(" + *column.checkExpr + ")");
            }

            std::ostringstream line;
            line << "  - " << column.name << " " << column.type;
            if (!qualifiers.empty())
            {
                line << " [";
                for (std::size_t i = 0; i < qualifiers.size(); ++i)
                {
                    if (i > 0)
                    {
                        line << ", ";
                    }
                    line << qualifiers[i];
                }
                line << "]";
            }
            std::cout << line.str() << '\n';
        }

        if (!metadata.tableChecks.empty())
        {
            std::cout << "  table checks:\n";
            for (const auto &check : metadata.tableChecks)
            {
                std::cout << "    * " << check << '\n';
            }
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << colorize(ex.what(), ansi::kRed) << '\n';
    }
}

bool QueryEngineCli::handleMetaCommand(const std::string &line, bool &shouldExit)
{
    std::istringstream in(line);
    std::string command;
    in >> command;
    command = toLower(command);

    if (command == ".help")
    {
        std::cout << metaCommandHelp();
        return true;
    }
    if (command == ".quit" || command == ".exit")
    {
        shouldExit = true;
        return true;
    }
    if (command == ".clear")
    {
        std::cout << "\x1b[2J\x1b[H";
        return true;
    }
    if (command == ".tables")
    {
        printTables();
        return true;
    }
    if (command == ".schema")
    {
        std::string tableName;
        std::getline(in, tableName);
        tableName = stripWrappingQuotes(trim(tableName));
        printSchema(tableName);
        return true;
    }
    if (command == ".run")
    {
        std::string path;
        std::getline(in, path);
        path = stripWrappingQuotes(trim(path));
        if (path.empty())
        {
            std::cerr << colorize("Usage: .run <file.sql>", ansi::kYellow) << '\n';
            return true;
        }

        try
        {
            runScriptFile(path);
        }
        catch (const std::exception &ex)
        {
            std::cerr << colorize(ex.what(), ansi::kRed) << '\n';
        }
        return true;
    }

    return false;
}
