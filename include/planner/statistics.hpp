#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Database;

struct NumericHistogram {
    long double minValue{0.0L};
    long double maxValue{0.0L};
    std::vector<std::size_t> bins;
};

struct ColumnStatistics {
    std::size_t distinctCount{0};
    bool isNumeric{false};
    std::optional<NumericHistogram> histogram;
};

struct TableStatistics {
    std::size_t rowCount{0};
    std::unordered_map<std::string, ColumnStatistics> columns;
};

class StatisticsCatalog {
  public:
    explicit StatisticsCatalog(const Database &db);

    const TableStatistics *getTable(const std::string &table) const;
    std::size_t rowCountOrDefault(const std::string &table, std::size_t dflt = 1000) const;
    std::optional<std::size_t> distinctCount(const std::string &table, const std::string &column) const;
    std::optional<NumericHistogram> numericHistogram(const std::string &table, const std::string &column) const;

  private:
    std::unordered_map<std::string, TableStatistics> tables_;
};
