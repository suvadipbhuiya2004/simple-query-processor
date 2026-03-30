#pragma once
#include "parser/ast.h"
#include "planner/plan_node.h"

// Creates a logical plan tree from parsed AST.
class Planner {
public:
    std::unique_ptr<PlanNode> createPlan(const SelectStmt& stmt);
};