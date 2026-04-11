#include "execution/executor_builder.h"

#include "execution/filter_executor.h"
#include "execution/projection_executor.h"
#include "execution/seq_scan_executor.h"
#include "execution/aggregation_executor.h"

#include <stdexcept>
#include <utility>
#include <vector>

// Converts logical plan nodes into concrete executor objects.

std::unique_ptr<Executor> ExecutorBuilder::build(const PlanNode* plan, Database& db) {
    if (plan == nullptr) {
        throw std::runtime_error("Cannot build executor from null plan node");
    }

    if (plan->type == PlanType::SEQ_SCAN) {
        const auto* node = dynamic_cast<const SeqScanNode*>(plan);
        if (node == nullptr) {
            throw std::runtime_error("Plan type mismatch: expected SeqScanNode");
        }
        return std::make_unique<SeqScanExecutor>(&db.getTable(node->table));
    }

    if (plan->type == PlanType::FILTER) {
        const auto* node = dynamic_cast<const FilterNode*>(plan);
        if (node == nullptr) {
            throw std::runtime_error("Plan type mismatch: expected FilterNode");
        }
        if (plan->children.size() != 1U) {
            throw std::runtime_error("Filter node must have exactly one child");
        }
        if (!node->predicate) {
            throw std::runtime_error("Filter node has null predicate");
        }

        auto child = build(plan->children[0].get(), db);
        return std::make_unique<FilterExecutor>(std::move(child), node->predicate->clone());
    }

    if (plan->type == PlanType::PROJECTION) {
        const auto* node = dynamic_cast<const ProjectionNode*>(plan);
        if (node == nullptr) {
            throw std::runtime_error("Plan type mismatch: expected ProjectionNode");
        }
        if (plan->children.size() != 1U) {
            throw std::runtime_error("Projection node must have exactly one child");
        }

        // --- FIXED BLOCK ---
        // Instead of extracting strings and throwing errors on AggregateExpr,
        // we now pass the node directly to the ProjectionExecutor.
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<ProjectionExecutor>(std::move(child), node);
        // -------------------
    }

    if (plan->type == PlanType::AGGREGATION) {
        const auto* node = dynamic_cast<const AggregationNode*>(plan);
        if (node == nullptr) {
            throw std::runtime_error("Plan type mismatch: expected AggregationNode");
        }
        
        if (plan->children.empty()) {
            throw std::runtime_error("Aggregation node must have at least one child");
        }

        auto child = build(plan->children[0].get(), db);
        return std::make_unique<AggregationExecutor>(std::move(child), node);
    }

    throw std::runtime_error("Unknown plan node");
}