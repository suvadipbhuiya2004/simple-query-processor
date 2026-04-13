#include "execution/seq_scan_executor.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

SeqScanExecutor::SeqScanExecutor(const Table* table, const std::vector<std::string>* schema, std::string qualifier) : table_(table), qualifier_(std::move(qualifier)) {
    if (table_ == nullptr) {
        throw std::runtime_error("SeqScanExecutor: null table pointer");
    }

    if (qualifier_.empty()) {
        throw std::runtime_error("SeqScanExecutor: empty table qualifier");
    }

    if (schema != nullptr && !schema->empty()) {
        columns_ = *schema;
    } 
    else if (!table_->empty()) {
        columns_.reserve(table_->front().size());
        for (const auto& [name, _] : table_->front()) {
            columns_.push_back(name);
        }
        std::sort(columns_.begin(), columns_.end());
    }
}

void SeqScanExecutor::open() {
    index_ = 0;
}

bool SeqScanExecutor::next(Row& row) {
    if (index_ >= table_->size()) return false;

    row.clear();
    const Row& source = (*table_)[index_++];
    row.reserve(columns_.size() * 2U + 4U);

    for (const auto& column : columns_) {
        const auto it = source.find(column);
        const std::string value = (it == source.end()) ? std::string() : it->second;
        row[column] = value;
        row[qualifier_ + "." + column] = value;
    }

    return true;
}

void SeqScanExecutor::close() {
    // No resources to release; reset for potential re-use.
    index_ = 0;
}