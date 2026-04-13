#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "app/query_engine.hpp"
#include "app/sql_script_loader.hpp"

namespace {

    constexpr const char *kAnsiReset = "\x1b[0m";
    constexpr const char *kAnsiRed = "\x1b[31m";
    constexpr const char *kAnsiYellow = "\x1b[33m";

    std::string colorize(const std::string &text, const char *colorCode) {
        return std::string(colorCode) + text + kAnsiReset;
    }

}

int main(int argc, char *argv[]){
    try{
        QueryEngineApp app(QueryEngineApp::resolveDataDirectory());
        app.initialize();

        const auto statements = SqlScriptLoader::loadStatements(argc, argv);
        app.executeScript(statements);

        return 0;
    }
    catch (const std::exception &ex) {
        const std::string message = ex.what();
        const bool isWarningSummary = message.rfind("Execution summary:", 0) == 0;
        std::cerr << colorize(message, isWarningSummary ? kAnsiYellow : kAnsiRed) << '\n';
        return 1;
    }
}
