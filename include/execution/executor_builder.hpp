#pragma once
#include "execution/executor.hpp"
#include "planner/plan.hpp"
#include "storage/table.hpp"
#include <memory>

// converts a logical plan tree into a physical executor tree.
class ExecutorBuilder {
public:
    static std::unique_ptr<Executor> build(const PlanNode* plan, Database& db);
};