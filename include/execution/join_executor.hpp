#pragma once
#include "execution/executor.hpp"
#include "execution/expression.hpp"
#include "planner/plan.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// =============================================================================
// Supports INNER / LEFT / RIGHT / FULL / CROSS join semantics.
//
// Three physical algorithms:
//   HASH        – O(n+m) average. Best for large equi-joins. DEFAULT.
//   NESTED_LOOP – O(n*m).         Correct for any condition.
//   MERGE       – O(n log n).     Best for pre-sorted or equi-join inputs.
// =============================================================================
class JoinExecutor final : public Executor{
private:
    std::unique_ptr<Executor> left_;
    std::unique_ptr<Executor> right_;

    JoinType joinType_;
    JoinAlgorithm algorithm_;
    std::unique_ptr<Expr> condition_;

    // Schema 
    std::vector<std::string> leftColumns_;
    std::vector<std::string> rightColumns_;
    std::unordered_set<std::string> leftColumnSet_;
    std::unordered_set<std::string> rightColumnSet_;

    std::unordered_map<std::string, std::string> uniqueBareToQualified_;

    std::vector<Row> outputRows_;
    std::size_t cursor_{0};


    void buildUniqueBareNameMap();

    Row makeNullSide(const std::vector<std::string> &columns) const;
    Row mergeRows(const Row &leftRow, const Row &rightRow) const;
    bool matchesCondition(const Row &merged) const;
    const std::string &getColumnValue(const Row &row, const std::string &column) const;

    // Try to extract left/right key column names from a simple equi-condition.
    bool extractEquiJoinKeys(std::string &leftKey, std::string &rightKey) const;

    // Algorithm implementations
    void runNestedLoopJoin(const std::vector<Row> &leftRows,const std::vector<Row> &rightRows);
    void runHashJoin(const std::vector<Row> &leftRows, const std::vector<Row> &rightRows);
    void runMergeJoin(const std::vector<Row> &leftRows, const std::vector<Row> &rightRows);

public:
    JoinExecutor(std::unique_ptr<Executor> left,std::unique_ptr<Executor> right, JoinType joinType, JoinAlgorithm algorithm, std::unique_ptr<Expr> condition, std::vector<std::string> leftQualifiedColumns, std::vector<std::string> rightQualifiedColumns);

    void open() override;
    bool next(Row &row) override;
    void close() override;
};