#pragma once

#include "app/cli/cli_options.hpp"
#include "app/query_engine.hpp"

#include <string>

class QueryEngineCli
{
public:
    explicit QueryEngineCli(QueryEngineApp &app);

    int run(const CliOptions &options);
    static std::string metaCommandHelp();

private:
    QueryEngineApp &app_;

    int runDefaultScript() const;
    int runScriptFile(const std::string &path) const;
    int runSingleQuery(const std::string &queryText) const;
    int runRepl();

    void printWelcome() const;
    void printTables() const;
    void printSchema(const std::string &tableName) const;
    bool handleMetaCommand(const std::string &line, bool &shouldExit);
};
