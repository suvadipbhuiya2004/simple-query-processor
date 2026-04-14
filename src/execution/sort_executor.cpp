#include "execution/sort_executor.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace{

    bool tryParseNumber(const std::string &s, long double &out) noexcept{
        if (s.empty()) return false;
        const char *begin = s.c_str();
        char *end = nullptr;
        const long double n = std::strtold(begin, &end);
        if (begin == end || *end != '\0' || errno == ERANGE)
            return false;
        out = n;
        return true;
    }

    // Returns -1 / 0 / 1 with mixed-type ordering:
    int compareForSort(const std::string &a, const std::string &b) {
        long double ai = 0, bi = 0;
        const bool an = tryParseNumber(a, ai);
        const bool bn = tryParseNumber(b, bi);

        if (an && bn) {
            return (ai < bi) ? -1 : (ai > bi) ? 1 : 0;
        }
        if (an != bn) {
            // Numeric before non-numeric
            return an ? -1 : 1;
        }
        const int cmp = a.compare(b);
        return (cmp < 0) ? -1 : (cmp > 0) ? 1 : 0;
    }

}

// SortExecutor
SortExecutor::SortExecutor(std::unique_ptr<Executor> child, std::string col, bool ascending)
    : child_(std::move(child)), orderByColumn_(std::move(col)), ascending_(ascending) {
    if (!child_)
        throw std::runtime_error("SortExecutor: null child executor");
    if (orderByColumn_.empty())
        throw std::runtime_error("SortExecutor: ORDER BY column must not be empty");
}

void SortExecutor::open(){
    child_->open();
    buffer_.clear();
    cursor_ = 0;

    Row row;
    while (child_->next(row)){
        const auto it = row.find(orderByColumn_);
        if (it == row.end())
            throw std::runtime_error("ORDER BY column missing in row: " + orderByColumn_);

        SortItem item;
        item.key = it->second;
        item.row = std::move(row);
        buffer_.push_back(std::move(item));
    }

    // stable_sort preserves original insertion order for equal keys,
    std::stable_sort(buffer_.begin(), buffer_.end(), [this](const SortItem &a, const SortItem &b){
        const int cmp = compareForSort(a.key, b.key);
        return ascending_ ? (cmp < 0) : (cmp > 0);
    });
}

bool SortExecutor::next(Row &row) {
    if (cursor_ >= buffer_.size())
        return false;
    row = std::move(buffer_[cursor_++].row);
    return true;
}

void SortExecutor::close() {
    child_->close();
    buffer_.clear();
    cursor_ = 0;
}