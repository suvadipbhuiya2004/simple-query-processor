#pragma once

#include "planner/plan.hpp"
#include "planner/statistics.hpp"
#include <string>

class CostModel {
  public:
    static double estimateEqualityFilterSelectivity(const StatisticsCatalog &stats, const std::string &table, const std::string &column);
    static double estimateEqualityJoinSelectivity(const StatisticsCatalog &stats, const std::string &leftTable, const std::string &leftColumn, const std::string &rightTable, const std::string &rightColumn);
    static double estimateJoinOutputRows(double leftRows, double rightRows, double selectivity);
    static JoinAlgorithm chooseJoinAlgorithm(double leftRows, double rightRows, bool equiJoin, bool leftSorted, bool rightSorted, JoinType joinType);
};
