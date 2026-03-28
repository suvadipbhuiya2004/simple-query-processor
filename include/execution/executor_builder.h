#pragma once
#include "execution/executor.h"
#include "planner/plan_node.h"
#include "storage/table.h"

// Builds an executor tree from a validated plan tree.
class ExecutorBuilder {
public:
    static std::unique_ptr<Executor> build(const PlanNode* plan, Database& db);
};