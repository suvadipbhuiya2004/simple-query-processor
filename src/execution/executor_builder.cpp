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

        bool selectAll = false;
        std::vector<std::string> projectedColumns;
        projectedColumns.reserve(node->columns.size());

        for (const auto& expr : node->columns) {
            const auto* column = dynamic_cast<const Column*>(expr.get());
            if (column == nullptr) {
                throw std::runtime_error("Only column references are supported in SELECT list");
            }

            // SELECT * is represented as a single column named "*".
            if (column->name == "*") {
                if (node->columns.size() != 1U) {
                    throw std::runtime_error("SELECT * cannot be mixed with explicit columns");
                }
                selectAll = true;
                projectedColumns.clear();
                break;
            }

            projectedColumns.push_back(column->name);
        }

        if (!selectAll && projectedColumns.empty()) {
            throw std::runtime_error("Projection node has no columns");
        }

        auto child = build(plan->children[0].get(), db);
        return std::make_unique<ProjectionExecutor>(std::move(child), std::move(projectedColumns), selectAll);
    }

    // Add this before the final 'Unknown plan node' error
    if (plan->type == PlanType::AGGREGATION) {
        const auto* node = dynamic_cast<const AggregationNode*>(plan);
        if (node == nullptr) {
            throw std::runtime_error("Plan type mismatch: expected AggregationNode");
        }
        
        auto child = build(plan->children[0].get(), db);
        return std::make_unique<AggregationExecutor>(std::move(child), node);
    }

    throw std::runtime_error("Unknown plan node");
}
