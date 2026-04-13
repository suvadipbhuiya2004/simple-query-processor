#include "execution/sort_executor.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

namespace{

    bool tryParseInt64(const std::string &s, std::int64_t &out) noexcept{
        if (s.empty()) return false;
        errno = 0;
        const char *begin = s.c_str();
        char *end = nullptr;
        const long long n = std::strtoll(begin, &end, 10);
        if (begin == end || *end != '\0' || errno == ERANGE)
            return false;
        out = static_cast<std::int64_t>(n);
        return true;
    }

    // Returns -1 / 0 / 1 with mixed-type ordering:
    int compareForSort(const std::string &a, const std::string &b) {
        std::int64_t ai = 0, bi = 0;
        const bool an = tryParseInt64(a, ai);
        const bool bn = tryParseInt64(b, bi);

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
SortExecutor::SortExecutor(std::unique_ptr<Executor> child, std::string col) : child_(std::move(child)), orderByColumn_(std::move(col)){
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
        buffer_.push_back(std::move(row));
    }

    // stable_sort preserves original insertion order for equal keys,
    std::stable_sort(buffer_.begin(), buffer_.end(), [this](const Row &a, const Row &b){
        const auto aIt = a.find(orderByColumn_);
        const auto bIt = b.find(orderByColumn_);
        if (aIt == a.end())
        throw std::runtime_error("ORDER BY column missing in row: " + orderByColumn_);
        if (bIt == b.end())
        throw std::runtime_error("ORDER BY column missing in row: " + orderByColumn_);
        return compareForSort(aIt->second, bIt->second) < 0;
    });
}

bool SortExecutor::next(Row &row) {
    if (cursor_ >= buffer_.size())
        return false;
    row = std::move(buffer_[cursor_++]);
    return true;
}

void SortExecutor::close() {
    child_->close();
    buffer_.clear();
    cursor_ = 0;
}