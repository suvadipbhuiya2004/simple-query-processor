#include "execution/aggregation_executor.h"
#include "execution/expression.h"  // Ensure this header is included
#include <unordered_map>
#include <stdexcept>

AggregationExecutor::AggregationExecutor(std::unique_ptr<Executor> child, const AggregationNode* node)
    : child_(std::move(child)), node_(node) {}

void AggregationExecutor::open() {
    child_->open();
    
    std::unordered_map<std::string, Row> groups;
    Row row;

    // 1. Grouping logic
    while (child_->next(row)) {
        std::string key = generateGroupKey(row);
        if (groups.find(key) == groups.end()) {
            groups[key] = row; 
        }
    }

    // 2. Apply HAVING filter using ExpressionEvaluator
    for (auto& [key, group_row] : groups) {
        if (node_->havingExpr) {
            // Change 'evaluate' to 'ExpressionEvaluator::evalPredicate'
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
        // Change 'evaluate' to 'ExpressionEvaluator::eval'
        key += ExpressionEvaluator::eval(expr.get(), row) + "|";
    }
    return key;
}