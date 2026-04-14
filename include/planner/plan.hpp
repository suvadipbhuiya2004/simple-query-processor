#pragma once
#include "parser/ast.hpp"
#include <memory>
#include <string>
#include <vector>

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    JOIN,
    FILTER,
    PROJECTION,
    DISTINCT,
    AGGREGATION,
    SORT,
    LIMIT
};

enum class JoinAlgorithm {
    HASH,
    NESTED_LOOP,
    MERGE,
};

// global join algo
inline constexpr JoinAlgorithm kDefaultJoinAlgorithm = JoinAlgorithm::HASH;

// Base plan node  (volcano / iterator model)
class PlanNode {
  public:
    PlanType type;
    std::vector<std::unique_ptr<PlanNode>> children;

    explicit PlanNode(PlanType t) noexcept : type(t) {}
    virtual ~PlanNode() = default;

    // Non-copyable.
    PlanNode(const PlanNode &) = delete;
    PlanNode &operator=(const PlanNode &) = delete;
    PlanNode(PlanNode &&) = default;
    PlanNode &operator=(PlanNode &&) = delete;
};

// Sequential full-table scan
class SeqScanNode : public PlanNode {
  public:
    std::string table;
    std::string outputQualifier;
    std::vector<std::string> requiredColumns;
    std::unique_ptr<Expr> pushedPredicate;
    bool alwaysEmpty{false};

    SeqScanNode(std::string t, std::string qualifier)
        : PlanNode(PlanType::SEQ_SCAN), table(std::move(t)), outputQualifier(std::move(qualifier)) {
    }
};

// Equality lookup scan using an index-aware access path.
class IndexScanNode : public PlanNode {
  public:
    std::string table;
    std::string outputQualifier;
    std::string lookupColumn;
    std::string lookupValue;
    std::vector<std::string> requiredColumns;
    std::unique_ptr<Expr> pushedPredicate;
    bool alwaysEmpty{false};

    IndexScanNode(std::string t, std::string qualifier, std::string column, std::string value)
        : PlanNode(PlanType::INDEX_SCAN), table(std::move(t)),
          outputQualifier(std::move(qualifier)), lookupColumn(std::move(column)),
          lookupValue(std::move(value)) {}
};

// Joins two child relations using ON predicate semantics.
class JoinNode : public PlanNode {
  public:
    JoinType joinType;
    JoinAlgorithm algorithm;
    std::unique_ptr<Expr> condition;

    JoinNode(JoinType t, JoinAlgorithm algo, std::unique_ptr<Expr> cond)
        : PlanNode(PlanType::JOIN), joinType(t), algorithm(algo), condition(std::move(cond)) {}
};

// Filters rows using a predicate expression.
class FilterNode : public PlanNode {
  public:
    std::unique_ptr<Expr> predicate;
    explicit FilterNode(std::unique_ptr<Expr> pred)
        : PlanNode(PlanType::FILTER), predicate(std::move(pred)) {}
};

// Projects a subset of columns (or SELECT *).
class ProjectionNode : public PlanNode {
  public:
    std::vector<std::unique_ptr<Expr>> columns;
    explicit ProjectionNode(std::vector<std::unique_ptr<Expr>> cols)
        : PlanNode(PlanType::PROJECTION), columns(std::move(cols)) {}
};

// Removes duplicate rows from child output.
class DistinctNode : public PlanNode {
  public:
    DistinctNode() : PlanNode(PlanType::DISTINCT) {}
};

// Groups rows by expressions and optionally filters groups with HAVING.
class AggregationNode : public PlanNode {
  public:
    std::vector<std::unique_ptr<Expr>> groupExprs;
    std::unique_ptr<Expr> havingExpr;
    std::vector<std::unique_ptr<Expr>> selectExprs;
    std::string orderByExpr;

    AggregationNode(std::vector<std::unique_ptr<Expr>> groupExprs_,
                    std::unique_ptr<Expr> havingExpr_,
                    std::vector<std::unique_ptr<Expr>> selectExprs_, std::string orderByExpr_)
        : PlanNode(PlanType::AGGREGATION), groupExprs(std::move(groupExprs_)),
          havingExpr(std::move(havingExpr_)), selectExprs(std::move(selectExprs_)),
          orderByExpr(std::move(orderByExpr_)) {}
};

// Sorts buffered rows by a single column.
class SortNode : public PlanNode {
  public:
    std::string orderByColumn;
    bool ascending;

    SortNode(std::string col, bool asc)
        : PlanNode(PlanType::SORT), orderByColumn(std::move(col)), ascending(asc) {}
};

// Emits at most N rows.
class LimitNode : public PlanNode {
  public:
    int limitCount;
    explicit LimitNode(int n) : PlanNode(PlanType::LIMIT), limitCount(n) {}
};

// =============================================================================
// Planner  — translates a parsed SelectStmt into a logical plan tree.
//
// Plan shape (bottom → top):
//   SeqScan → [Filter] → [Aggregation] → [Sort] → [Limit] → Projection
// =============================================================================
class Planner {
  public:
    std::unique_ptr<PlanNode> createPlan(const SelectStmt &stmt, const class Database *dbStats = nullptr);
};