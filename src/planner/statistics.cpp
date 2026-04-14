#include "planner/statistics.hpp"

#include "storage/table.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_set>

namespace
{

    bool tryParseNumber(const std::string &s, long double &out) noexcept
    {
        if (s.empty())
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        const long double v = std::strtold(s.c_str(), &end);
        if (s.c_str() == end || *end != '\0' || errno == ERANGE)
        {
            return false;
        }
        out = v;
        return true;
    }

    std::vector<std::size_t> buildHistogram(const std::vector<long double> &values, long double minV, long double maxV, std::size_t binCount)
    {
        std::vector<std::size_t> bins(binCount, 0);
        if (values.empty() || binCount == 0 || minV >= maxV)
        {
            return bins;
        }

        const long double span = maxV - minV;
        for (const long double v : values)
        {
            long double ratio = (v - minV) / span;
            if (ratio < 0.0L)
            {
                ratio = 0.0L;
            }
            if (ratio > 1.0L)
            {
                ratio = 1.0L;
            }

            std::size_t idx = static_cast<std::size_t>(ratio * static_cast<long double>(binCount));
            if (idx >= binCount)
            {
                idx = binCount - 1;
            }
            bins[idx] += 1;
        }
        return bins;
    }

} // namespace

StatisticsCatalog::StatisticsCatalog(const Database &db)
{
    tables_.reserve(db.tables.size());

    for (const auto &[tableName, rows] : db.tables)
    {
        TableStatistics ts;
        ts.rowCount = rows.size();

        std::vector<std::string> columns;
        if (db.hasSchema(tableName))
        {
            columns = db.getSchema(tableName);
        }
        else if (!rows.empty())
        {
            columns.reserve(rows.front().size());
            for (const auto &[name, _] : rows.front())
            {
                columns.push_back(name);
            }
            std::sort(columns.begin(), columns.end());
        }

        ts.columns.reserve(columns.size());

        for (const auto &col : columns)
        {
            ColumnStatistics cs;
            std::unordered_set<std::string> distinct;
            distinct.reserve(std::max<std::size_t>(rows.size(), 1U));

            bool numeric = true;
            long double minV = std::numeric_limits<long double>::max();
            long double maxV = std::numeric_limits<long double>::lowest();
            std::vector<long double> numericValues;
            numericValues.reserve(rows.size());

            for (const auto &row : rows)
            {
                const auto it = row.find(col);
                if (it == row.end())
                {
                    continue;
                }
                const std::string &v = it->second;
                if (v.empty())
                {
                    continue;
                }

                distinct.insert(v);

                long double n = 0.0L;
                if (!tryParseNumber(v, n))
                {
                    numeric = false;
                }
                else
                {
                    numericValues.push_back(n);
                    minV = std::min(minV, n);
                    maxV = std::max(maxV, n);
                }
            }

            cs.distinctCount = distinct.size();
            cs.isNumeric = numeric && !numericValues.empty();

            if (cs.isNumeric)
            {
                NumericHistogram h;
                h.minValue = minV;
                h.maxValue = maxV;
                h.bins = buildHistogram(numericValues, minV, maxV, 16U);
                cs.histogram = std::move(h);
            }

            ts.columns.emplace(col, std::move(cs));
        }

        tables_.emplace(tableName, std::move(ts));
    }
}

const TableStatistics *StatisticsCatalog::getTable(const std::string &table) const
{
    const auto it = tables_.find(table);
    return (it == tables_.end()) ? nullptr : &it->second;
}

std::size_t StatisticsCatalog::rowCountOrDefault(const std::string &table, std::size_t dflt) const
{
    const auto *t = getTable(table);
    return t ? t->rowCount : dflt;
}

std::optional<std::size_t> StatisticsCatalog::distinctCount(const std::string &table, const std::string &column) const
{
    const auto *t = getTable(table);
    if (!t)
    {
        return std::nullopt;
    }
    const auto it = t->columns.find(column);
    if (it == t->columns.end())
    {
        return std::nullopt;
    }
    return it->second.distinctCount;
}

std::optional<NumericHistogram>
StatisticsCatalog::numericHistogram(const std::string &table, const std::string &column) const
{
    const auto *t = getTable(table);
    if (!t)
    {
        return std::nullopt;
    }
    const auto it = t->columns.find(column);
    if (it == t->columns.end())
    {
        return std::nullopt;
    }
    if (!it->second.histogram.has_value())
    {
        return std::nullopt;
    }
    return it->second.histogram;
}
