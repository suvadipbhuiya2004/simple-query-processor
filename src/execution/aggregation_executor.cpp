#include "execution/aggregation_executor.hpp"
#include "execution/expression.hpp"

#include <stdexcept>
#include <unordered_map>
#include <utility>

// AggregationExecutor
AggregationExecutor::AggregationExecutor(std::unique_ptr<Executor> child, const AggregationNode *node) : child_(std::move(child)), node_(node) {
    if (!child_) throw std::runtime_error("AggregationExecutor: null child executor");
    if (!node_)  throw std::runtime_error("AggregationExecutor: null plan node");
}

void AggregationExecutor::open() {
    child_->open();
    results_.clear();
    cursor_ = 0;

    // group rows — first-row-wins per group key.
    std::unordered_map<std::string, std::size_t> groupIndex;
    Row row;
    while (child_->next(row)) {
        const std::string key = buildGroupKey(row);
        if (groupIndex.find(key) == groupIndex.end()) {
            groupIndex[key] = results_.size();
            results_.push_back(row);
        }
        // To Do: accumulate aggregate functions here (SUM, COUNT, …)
    }

    //  apply HAVING filter
    if (node_->havingExpr) {
        auto it = results_.begin();
        while (it != results_.end()) {
            if (!ExpressionEvaluator::evalPredicate(node_->havingExpr.get(), *it)) {
                it = results_.erase(it);
            } 
            else {
                ++it;
            }
        }
    }
}

bool AggregationExecutor::next(Row& row) {
    if (cursor_ >= results_.size()) return false;
    row = std::move(results_[cursor_++]);
    return true;
}

void AggregationExecutor::close() {
    child_->close();
    results_.clear();
    cursor_ = 0;
}

std::string AggregationExecutor::buildGroupKey(const Row& row) const {
    std::string key;
    for (const auto& expr : node_->groupExprs) {
        key += ExpressionEvaluator::eval(expr.get(), row);
        key += '\x1f';
    }
    return key;
}