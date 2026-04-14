#pragma once

#include <string>

enum class CliMode
{
    Help,
    DefaultScript,
    ScriptFile,
    SingleQuery,
    Repl,
};

struct CliOptions
{
    CliMode mode{CliMode::DefaultScript};
    std::string programName;
    std::string sqlFilePath;
    std::string queryText;
};

class CliOptionsParser
{
public:
    static CliOptions parse(int argc, char *argv[]);
    static std::string usage(const std::string &programName);
};
