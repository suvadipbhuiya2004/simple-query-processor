#include "app/cli/cli_options.hpp"

#include <stdexcept>
#include <string>

namespace
{

    std::string executableName(const char *argv0)
    {
        if (argv0 == nullptr)
        {
            return "query_engine";
        }

        std::string full(argv0);
        const std::size_t slash = full.find_last_of("/\\");
        if (slash == std::string::npos)
        {
            return full.empty() ? "query_engine" : full;
        }
        return (slash + 1 < full.size()) ? full.substr(slash + 1) : "query_engine";
    }

    void ensureModeUnset(const CliOptions &options, const std::string &flag)
    {
        if (options.mode == CliMode::DefaultScript)
        {
            return;
        }
        throw std::runtime_error("Conflicting mode: '" + flag + "' cannot be combined with another execution mode.");
    }

    const char *requireNextArg(int argc, char *argv[], int &i, const std::string &flag)
    {
        if (i + 1 >= argc)
        {
            throw std::runtime_error("Missing value after " + flag + ".");
        }
        ++i;
        return argv[i];
    }

}

CliOptions CliOptionsParser::parse(int argc, char *argv[])
{
    CliOptions options;
    options.programName = executableName((argc > 0) ? argv[0] : nullptr);

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            options.mode = CliMode::Help;
            return options;
        }

        if (arg == "-r" || arg == "--repl")
        {
            ensureModeUnset(options, arg);
            options.mode = CliMode::Repl;
            continue;
        }

        if (arg == "-q" || arg == "--query")
        {
            ensureModeUnset(options, arg);
            options.mode = CliMode::SingleQuery;
            options.queryText = requireNextArg(argc, argv, i, arg);
            continue;
        }

        if (arg.rfind("--query=", 0) == 0)
        {
            ensureModeUnset(options, "--query");
            options.mode = CliMode::SingleQuery;
            options.queryText = arg.substr(std::string("--query=").size());
            continue;
        }

        if (arg == "-f" || arg == "--file")
        {
            ensureModeUnset(options, arg);
            options.mode = CliMode::ScriptFile;
            options.sqlFilePath = requireNextArg(argc, argv, i, arg);
            continue;
        }

        if (arg.rfind("--file=", 0) == 0)
        {
            ensureModeUnset(options, "--file");
            options.mode = CliMode::ScriptFile;
            options.sqlFilePath = arg.substr(std::string("--file=").size());
            continue;
        }

        throw std::runtime_error("Unknown argument: " + arg);
    }

    return options;
}

std::string CliOptionsParser::usage(const std::string &programName)
{
    return "Usage:\n"
           "  " +
           programName + "                    Run queries.sql in batch mode\n"
                         "  " +
           programName + " --file <path>      Run SQL script file\n"
                         "  " +
           programName + " --query \"<sql>\"   Run a single SQL string\n"
                         "  " +
           programName + " --repl             Start interactive SQL terminal\n"
                         "  " +
           programName + " --help             Show this help\n";
}
