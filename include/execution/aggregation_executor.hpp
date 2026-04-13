#pragma once
#include "execution/executor.hpp"
#include "planner/plan.hpp"
#include <cstddef>
#include <vector>

// implements GROUP BY with an optional HAVING filter.
//  Need to implement COUNT, SUM, AVG, MIN, MAX, etc.
class AggregationExecutor final : public Executor{
private:
    std::unique_ptr<Executor> child_;
    const AggregationNode *node_;

    std::vector<Row> results_;
    std::size_t cursor_{0};

    std::string buildGroupKey(const Row &row) const;

public:
    AggregationExecutor(std::unique_ptr<Executor> child, const AggregationNode *node);

    void open() override;
    bool next(Row &row) override;
    void close() override;
};