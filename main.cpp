#include <exception>
#include <iostream>
#include <string>
#include <vector>
#include "app/query_engine.hpp"
#include "app/sql_script_loader.hpp"
#include "common/ansi.hpp"

int main(int argc, char *argv[]){
    std::cout << "\n";
    try {
        QueryEngineApp app(QueryEngineApp::resolveDataDirectory());
        app.initialize();

        const auto statements = SqlScriptLoader::loadStatements(argc, argv);
        app.executeScript(statements);

        return 0;
    }
    catch (const std::exception &ex) {
        const std::string message = ex.what();
        const bool isWarningSummary = message.rfind("Execution summary:", 0) == 0;
        std::cerr << "\n" << ansi::colorize(message, isWarningSummary ? ansi::kYellow : ansi::kRed) << "\n\n";
        return 1;
    }
}
