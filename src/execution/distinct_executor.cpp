#include "execution/distinct_executor.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

DistinctExecutor::DistinctExecutor(std::unique_ptr<Executor> child) : child_(std::move(child)) {
    if (!child_) {
        throw std::runtime_error("DistinctExecutor: null child executor");
    }
}

std::string DistinctExecutor::rowSignature(const Row& row) {
    std::vector<const std::pair<const std::string, std::string>*> fields;
    fields.reserve(row.size());
    for (const auto& kv : row) {
        fields.push_back(&kv);
    }

    std::sort(fields.begin(), fields.end(), [](const auto* a, const auto* b) {
        return a->first < b->first;
    });

    std::string signature;
    for (const auto* entry : fields) {
        signature += std::to_string(entry->first.size());
        signature += '#';
        signature += entry->first;
        signature += '=';
        signature += std::to_string(entry->second.size());
        signature += '#';
        signature += entry->second;
        signature += '|';
    }
    return signature;
}

void DistinctExecutor::open() {
    outputRows_.clear();
    cursor_ = 0;

    std::unordered_set<std::string> seen;
    Row row;

    child_->open();
    while (child_->next(row)) {
        const std::string sig = rowSignature(row);
        if (seen.insert(sig).second) {
            outputRows_.push_back(row);
        }
    }
    child_->close();
}

bool DistinctExecutor::next(Row& row) {
    if (cursor_ >= outputRows_.size()) return false;
    row = std::move(outputRows_[cursor_++]);
    return true;
}

void DistinctExecutor::close() {
    outputRows_.clear();
    cursor_ = 0;
}
