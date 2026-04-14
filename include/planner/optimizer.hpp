#pragma once

#include "planner/plan.hpp"
#include <memory>

// Rule-based optimizer for logical plans.
// Current rules:
// 1) Constant folding and dead filter elimination
// 2) Filter combination (Filter(Filter(x)) -> Filter(and))
// 3) Predicate pushdown into SeqScan
// 4) Projection/column pruning at SeqScan
class PlanOptimizer {
  public:
    std::unique_ptr<PlanNode> optimize(std::unique_ptr<PlanNode> root, const class Database *db = nullptr) const;

  private:
    std::unique_ptr<PlanNode> foldAndEliminate(std::unique_ptr<PlanNode> node) const;
    std::unique_ptr<PlanNode> combineFilters(std::unique_ptr<PlanNode> node) const;
    std::unique_ptr<PlanNode> pushdownPredicates(std::unique_ptr<PlanNode> node) const;
    std::unique_ptr<PlanNode> chooseAccessPaths(std::unique_ptr<PlanNode> node, const class Database *db) const;

    void applyColumnPruning(PlanNode *root) const;
};
