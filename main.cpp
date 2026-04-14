#include <exception>
#include <iostream>
#include <string>
#include "app/cli/cli_options.hpp"
#include "app/cli/query_engine_cli.hpp"
#include "app/query_engine.hpp"
#include "common/ansi.hpp"

int main(int argc, char *argv[])
{
    std::cout << "\n";
    try
    {
        const CliOptions options = CliOptionsParser::parse(argc, argv);

        if (options.mode == CliMode::Help)
        {
            std::cout << CliOptionsParser::usage(options.programName) << "\n";
            std::cout << QueryEngineCli::metaCommandHelp() << "\n";
            return 0;
        }

        QueryEngineApp app(QueryEngineApp::resolveDataDirectory());
        app.initialize();
        QueryEngineCli cli(app);
        return cli.run(options);
    }
    catch (const std::exception &ex)
    {
        const std::string message = ex.what();
        const bool isWarningSummary = message.rfind("Execution summary:", 0) == 0;
        const bool isCliUsageError =
            message.rfind("Unknown argument:", 0) == 0 ||
            message.rfind("Missing value after", 0) == 0 ||
            message.rfind("Conflicting mode:", 0) == 0;

        std::string finalMessage = message;
        if (isCliUsageError)
        {
            std::string programName = (argc > 0 && argv[0]) ? argv[0] : "query_engine";
            const std::size_t slash = programName.find_last_of("/\\");
            if (slash != std::string::npos && slash + 1 < programName.size())
            {
                programName = programName.substr(slash + 1);
            }
            finalMessage += "\n\n" + CliOptionsParser::usage(programName) + "\n" + QueryEngineCli::metaCommandHelp();
        }

        std::cerr << "\n"
                  << ansi::colorize(finalMessage, isWarningSummary ? ansi::kYellow : ansi::kRed) << "\n\n";
        return isCliUsageError ? 2 : 1;
    }
}
