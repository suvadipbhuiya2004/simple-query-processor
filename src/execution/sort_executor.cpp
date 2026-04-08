#include "execution/sort_executor.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
bool TryParseInt64(const std::string& value, int64_t& out) {
    if (value.empty()) {
        return false;
    }

    errno = 0;
    const char* begin = value.c_str();
    char* end = nullptr;
    const long long parsed = std::strtoll(begin, &end, 10);

    if (begin == end || *end != '\0' || errno == ERANGE) {
        return false;
    }

    if (parsed < std::numeric_limits<int64_t>::min() ||
        parsed > std::numeric_limits<int64_t>::max()) {
        return false;
    }

    out = static_cast<int64_t>(parsed);
    return true;
}

int CompareValues(const std::string& left, const std::string& right) {
    int64_t leftInt = 0;
    int64_t rightInt = 0;
    const bool leftIsNumber = TryParseInt64(left, leftInt);
    const bool rightIsNumber = TryParseInt64(right, rightInt);

    if (leftIsNumber && rightIsNumber) {
        if (leftInt < rightInt) {
            return -1;
        }
        if (leftInt > rightInt) {
            return 1;
        }
        return 0;
    }

    // Keep a deterministic total order for mixed types: numeric values first.
    if (leftIsNumber != rightIsNumber) {
        return leftIsNumber ? -1 : 1;
    }

    const int cmp = left.compare(right);
    if (cmp < 0) {
        return -1;
    }
    if (cmp > 0) {
        return 1;
    }
    return 0;
}
}  // namespace

SortExecutor::SortExecutor(std::unique_ptr<Executor> c, std::string column)
    : child(std::move(c)), orderByColumn(std::move(column)) {
    if (!child) {
        throw std::runtime_error("SortExecutor received a null child executor");
    }
    if (orderByColumn.empty()) {
        throw std::runtime_error("SortExecutor requires a non-empty ORDER BY column");
    }
}

void SortExecutor::open() {
    child->open();
    rows.clear();
    cursor = 0;

    Row row;
    while (child->next(row)) {
        rows.push_back(row);
    }

    std::stable_sort(rows.begin(), rows.end(), [this](const Row& a, const Row& b) {
        const auto aIt = a.find(orderByColumn);
        if (aIt == a.end()) {
            throw std::runtime_error("ORDER BY column not found in row: " + orderByColumn);
        }

        const auto bIt = b.find(orderByColumn);
        if (bIt == b.end()) {
            throw std::runtime_error("ORDER BY column not found in row: " + orderByColumn);
        }

        return CompareValues(aIt->second, bIt->second) < 0;
    });
}

bool SortExecutor::next(Row& row) {
    if (cursor >= rows.size()) {
        return false;
    }

    row = std::move(rows[cursor++]);
    return true;
}

void SortExecutor::close() {
    child->close();
    rows.clear();
    cursor = 0;
}
