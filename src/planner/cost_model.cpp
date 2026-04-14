#include "planner/cost_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

    double clampSelectivity(double s)
    {
        if (!std::isfinite(s))
        {
            return 0.1;
        }
        if (s < 1e-6)
        {
            return 1e-6;
        }
        if (s > 1.0)
        {
            return 1.0;
        }
        return s;
    }

}

double CostModel::estimateEqualityFilterSelectivity(const StatisticsCatalog &stats, const std::string &table, const std::string &column)
{
    const auto ndv = stats.distinctCount(table, column);
    if (!ndv.has_value() || *ndv == 0)
    {
        return 0.1;
    }
    return clampSelectivity(1.0 / static_cast<double>(*ndv));
}

double CostModel::estimateEqualityJoinSelectivity(const StatisticsCatalog &stats, const std::string &leftTable, const std::string &leftColumn, const std::string &rightTable, const std::string &rightColumn)
{
    const auto lNdv = stats.distinctCount(leftTable, leftColumn);
    const auto rNdv = stats.distinctCount(rightTable, rightColumn);
    if (!lNdv.has_value() || !rNdv.has_value() || *lNdv == 0 || *rNdv == 0)
    {
        return 0.1;
    }
    const std::size_t denom = std::max(*lNdv, *rNdv);
    if (denom == 0)
    {
        return 0.1;
    }
    return clampSelectivity(1.0 / static_cast<double>(denom));
}

double CostModel::estimateJoinOutputRows(double leftRows, double rightRows, double selectivity)
{
    if (!std::isfinite(leftRows) || leftRows < 0.0)
    {
        leftRows = 0.0;
    }
    if (!std::isfinite(rightRows) || rightRows < 0.0)
    {
        rightRows = 0.0;
    }
    return std::max(1.0, leftRows * rightRows * clampSelectivity(selectivity));
}

JoinAlgorithm CostModel::chooseJoinAlgorithm(double leftRows, double rightRows, bool equiJoin, bool leftSorted, bool rightSorted, JoinType joinType)
{
    if (joinType == JoinType::CROSS || !equiJoin)
    {
        return JoinAlgorithm::NESTED_LOOP;
    }

    if (joinType != JoinType::INNER)
    {
        return JoinAlgorithm::HASH;
    }

    const double minSide = std::min(leftRows, rightRows);
    const double product = leftRows * rightRows;
    if (minSide <= 32.0 || product <= 4096.0)
    {
        return JoinAlgorithm::NESTED_LOOP;
    }

    if (leftSorted && rightSorted && minSide >= 256.0)
    {
        return JoinAlgorithm::MERGE;
    }

    return JoinAlgorithm::HASH;
}
