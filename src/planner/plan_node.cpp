#include "planner/plan_node.h"

#include <utility>

// Lightweight constructors for plan node types.

PlanNode::PlanNode(PlanType t) : type(t) {}

SeqScanNode::SeqScanNode(std::string t) : PlanNode(PlanType::SEQ_SCAN), table(std::move(t)) {}

FilterNode::FilterNode(std::unique_ptr<Expr> pred)
    : PlanNode(PlanType::FILTER), predicate(std::move(pred)) {}

ProjectionNode::ProjectionNode(std::vector<std::unique_ptr<Expr>> cols)
    : PlanNode(PlanType::PROJECTION), columns(std::move(cols)) {}
