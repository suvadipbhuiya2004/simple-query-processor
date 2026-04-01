#pragma once
#include "execution/executor.h"
#include "planner/plan_node.h"
#include <unordered_map>
#include <vector>

class AggregationExecutor : public Executor {
public:
    AggregationExecutor(std::unique_ptr<Executor> child, const AggregationNode* node);

    void open() override;
    bool next(Row& row) override;
    void close() override;

private:
    std::unique_ptr<Executor> child_;
    const AggregationNode* node_;
    
    // Storage for grouped rows
    std::vector<Row> aggregated_results_;
    size_t cursor_{0};

    // Helper to create a unique key for the group based on GROUP BY columns
    std::string generateGroupKey(const Row& row);
};