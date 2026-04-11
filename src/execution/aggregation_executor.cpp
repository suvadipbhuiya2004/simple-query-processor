#include "execution/aggregation_executor.h"
#include "execution/expression.h"
#include <unordered_map>
#include <stdexcept>

AggregationExecutor::AggregationExecutor(std::unique_ptr<Executor> child, const AggregationNode* node)
    : child_(std::move(child)), node_(node) {}

void AggregationExecutor::open() {
    child_->open();
    
    // Key: Grouping string (e.g., "Engineering|")
    // Value: Pair containing the base Row and a map of aggregate running totals
    std::unordered_map<std::string, std::pair<Row, std::unordered_map<std::string, double>>> groups;
    Row row;

    while (child_->next(row)) {
        std::string key = generateGroupKey(row);
        
        if (groups.find(key) == groups.end()) {
            groups[key].first = row;
        }

        // Accumulate math for each Aggregate Expression found by the Planner
        for (const auto& aggExpr : node_->aggregateExprs) {
            auto* agg = dynamic_cast<AggregateExpr*>(aggExpr.get());
            if (!agg) continue;

            std::string colName = "";
            double val = 0;

            if (agg->arg) {
                // Evaluate the argument (e.g., 'age' in SUM(age))
                std::string evalResult = ExpressionEvaluator::eval(agg->arg.get(), row);
                try {
                    val = std::stod(evalResult);
                } catch (...) { val = 0; }

                if (auto* col = dynamic_cast<const Column*>(agg->arg.get())) {
                    colName = col->name;
                }
            }

            // Generate the result key used by the tests (e.g., "SUM_age")
            std::string resultKey = agg->funcName + (colName.empty() ? "" : "_" + colName);
            
            if (agg->funcName == "SUM") {
                groups[key].second[resultKey] += val;
            } else if (agg->funcName == "COUNT") {
                groups[key].second[resultKey] += 1;
            }
        }
    }

    // Finalize results and apply HAVING filter
    for (auto& [key, data] : groups) {
        Row& group_row = data.first;
        auto& agg_results = data.second;
        
        for (auto const& [name, val] : agg_results) {
            // Use static_cast to prevent scientific notation in result strings
            if (name.find("COUNT") != std::string::npos) {
                group_row[name] = std::to_string(static_cast<long long>(val));
            } else {
                // For SUM, to_string(double) gives 6 decimals (e.g., "45.000000")
                group_row[name] = std::to_string(val);
            }
        }

        if (node_->havingExpr) {
            if (!ExpressionEvaluator::evalPredicate(node_->havingExpr.get(), group_row)) {
                continue; 
            }
        }
        aggregated_results_.push_back(std::move(group_row));
    }

    cursor_ = 0;
}

bool AggregationExecutor::next(Row& row) {
    if (cursor_ < aggregated_results_.size()) {
        row = std::move(aggregated_results_[cursor_++]);
        return true;
    }
    return false;
}

void AggregationExecutor::close() {
    child_->close();
    aggregated_results_.clear();
}

std::string AggregationExecutor::generateGroupKey(const Row& row) {
    std::string key;
    for (const auto& expr : node_->groupExprs) {
        key += ExpressionEvaluator::eval(expr.get(), row) + "|";
    }
    return key;
}