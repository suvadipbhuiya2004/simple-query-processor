#include "execution/projection_executor.h"
#include "execution/expression.h"
#include <stdexcept>

ProjectionExecutor::ProjectionExecutor(std::unique_ptr<Executor> c, const ProjectionNode* node)
    : child(std::move(c)), node(node) {
    if (!child) {
        throw std::runtime_error("ProjectionExecutor received a null child executor");
    }
}

void ProjectionExecutor::open() {
    child->open();
}

bool ProjectionExecutor::next(Row& row) {
    Row input;
    if (!child->next(input)) {
        return false;
    }

    // Special Case: Handle SELECT *
    if (node->columns.size() == 1) {
        if (auto col = dynamic_cast<const Column*>(node->columns[0].get())) {
            if (col->name == "*") {
                row = std::move(input);
                return true;
            }
        }
    }

    Row output;
    for (const auto& expr : node->columns) {
        std::string value = ExpressionEvaluator::eval(expr.get(), input);
        
        std::string key;
        if (auto col = dynamic_cast<const Column*>(expr.get())) {
            key = col->name;
        } else if (auto agg = dynamic_cast<const AggregateExpr*>(expr.get())) {
            std::string colName = "";
            if (agg->arg) {
                if (auto argCol = dynamic_cast<const Column*>(agg->arg.get())) {
                    colName = argCol->name;
                }
            }
            key = agg->funcName + (colName.empty() ? "" : "_" + colName);
        }
        output[key] = value;
    }

    row = std::move(output);
    return true;
}

void ProjectionExecutor::close() {
    child->close();
}