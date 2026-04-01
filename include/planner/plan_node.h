#pragma once
#include <memory>
#include <string>
#include <vector>
#include "parser/ast.h"

// Logical plan node types supported by this engine.
enum class PlanType {
    SEQ_SCAN,
    FILTER,
    PROJECTION,
    AGGREGATION
};

class PlanNode {
public:
    PlanType type;
    std::vector<std::unique_ptr<PlanNode>> children;

    explicit PlanNode(PlanType t);
    virtual ~PlanNode() = default;
};


class SeqScanNode : public PlanNode {
public:
    // Table name to scan.
    std::string table;

    explicit SeqScanNode(std::string t);
};


class FilterNode : public PlanNode {
public:
    // Predicate expression applied to each child row.
    std::unique_ptr<Expr> predicate;

    explicit FilterNode(std::unique_ptr<Expr> pred);
};


class ProjectionNode : public PlanNode {
public:
    // Expressions to keep in final output rows.
    std::vector<std::unique_ptr<Expr>> columns;

    explicit ProjectionNode(std::vector<std::unique_ptr<Expr>> cols);
};

class AggregationNode : public PlanNode {
public:
    std::vector<std::unique_ptr<Expr>> groupExprs;
    std::unique_ptr<Expr> havingExpr;

    AggregationNode(std::vector<std::unique_ptr<Expr>> group_exprs,
                    std::unique_ptr<Expr> having_expr)
        : PlanNode(PlanType::AGGREGATION), 
          groupExprs(std::move(group_exprs)),
          havingExpr(std::move(having_expr)) {}
};