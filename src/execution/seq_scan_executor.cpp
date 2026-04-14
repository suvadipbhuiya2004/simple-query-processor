#include "execution/seq_scan_executor.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

SeqScanExecutor::SeqScanExecutor(const Table *table, const std::vector<std::string> *schema, std::string qualifier, std::vector<std::string> requiredColumns, std::unique_ptr<Expr> pushedPredicate, bool alwaysEmpty)
    : table_(table), qualifier_(std::move(qualifier)), alwaysEmpty_(alwaysEmpty) {
    if (table_ == nullptr) {
        throw std::runtime_error("SeqScanExecutor: null table pointer");
    }

    if (qualifier_.empty()) {
        throw std::runtime_error("SeqScanExecutor: empty table qualifier");
    }

    if (schema != nullptr && !schema->empty()) {
        columns_ = *schema;
    } else if (!table_->empty()) {
        columns_.reserve(table_->front().size());
        for (const auto &[name, _] : table_->front()) {
            columns_.push_back(name);
        }
        std::sort(columns_.begin(), columns_.end());
    }

    if (!requiredColumns.empty()) {
        std::unordered_set<std::string> keep(requiredColumns.begin(), requiredColumns.end());
        std::vector<std::string> pruned;
        pruned.reserve(columns_.size());
        for (const auto &col : columns_) {
            if (keep.count(col) != 0) {
                pruned.push_back(col);
            }
        }
        if (!pruned.empty()) {
            columns_ = std::move(pruned);
        }
    }

    qualifiedColumns_.reserve(columns_.size());
    for (const auto &col : columns_) {
        qualifiedColumns_.push_back(qualifier_ + "." + col);
    }
    outputRowReserve_ = columns_.size() * 2U + 4U;

    if (pushedPredicate) {
        pushedPredicate_ = CompiledPredicate::compile(pushedPredicate.get());
        hasPushedPredicate_ = true;
    }
}

void SeqScanExecutor::open() {
    index_ = 0;
}

bool SeqScanExecutor::next(Row &row) {
    if (alwaysEmpty_) {
        return false;
    }

    while (index_ < table_->size()) {
        const Row &source = (*table_)[index_++];
        if (hasPushedPredicate_ && !pushedPredicate_.evaluatePredicate(source)) {
            continue;
        }

        row.clear();
        row.reserve(outputRowReserve_);
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            const auto &column = columns_[i];
            const auto it = source.find(column);
            const std::string value = (it == source.end()) ? std::string() : it->second;
            row[column] = value;
            row[qualifiedColumns_[i]] = value;
        }
        return true;
    }

    return false;
}

void SeqScanExecutor::close() {
    // No resources to release; reset for potential re-use.
    index_ = 0;
}