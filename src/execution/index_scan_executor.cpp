#include "execution/index_scan_executor.hpp"
#include "storage/table.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

IndexScanExecutor::IndexScanExecutor(const Database *db, std::string table, std::string qualifier, std::string lookupColumn, std::string lookupValue, std::vector<std::string> requiredColumns, std::unique_ptr<Expr> pushedPredicate, bool alwaysEmpty)
    : db_(db), table_(std::move(table)), qualifier_(std::move(qualifier)),
      lookupColumn_(std::move(lookupColumn)), lookupValue_(std::move(lookupValue)),
      alwaysEmpty_(alwaysEmpty) {
    if (db_ == nullptr) {
        throw std::runtime_error("IndexScanExecutor: null database pointer");
    }
    if (!db_->hasTable(table_)) {
        throw std::runtime_error("IndexScanExecutor: unknown table: " + table_);
    }

    const std::vector<std::string> *schema =
        db_->hasSchema(table_) ? &db_->getSchema(table_) : nullptr;
    if (schema != nullptr) {
        columns_ = *schema;
    } else {
        const Table &data = db_->getTable(table_);
        if (!data.empty()) {
            columns_.reserve(data.front().size());
            for (const auto &[k, _] : data.front()) {
                columns_.push_back(k);
            }
            std::sort(columns_.begin(), columns_.end());
        }
    }

    if (!requiredColumns.empty()) {
        std::unordered_set<std::string> keep(requiredColumns.begin(), requiredColumns.end());
        std::vector<std::string> pruned;
        pruned.reserve(columns_.size());
        for (const auto &c : columns_) {
            if (keep.count(c) != 0) {
                pruned.push_back(c);
            }
        }
        if (!pruned.empty()) {
            columns_ = std::move(pruned);
        }
    }

    qualifiedColumns_.reserve(columns_.size());
    for (const auto &c : columns_) {
        qualifiedColumns_.push_back(qualifier_ + "." + c);
    }
    outputRowReserve_ = columns_.size() * 2U + 4U;

    if (pushedPredicate) {
        pushedPredicate_ = CompiledPredicate::compile(pushedPredicate.get());
        hasPushedPredicate_ = true;
    }
}

void IndexScanExecutor::open() {
    cursor_ = 0;
    rowIds_.clear();

    if (alwaysEmpty_) {
        return;
    }

    const auto &index = db_->getOrBuildHashIndex(table_, lookupColumn_);
    const auto it = index.find(lookupValue_);
    if (it == index.end()) {
        return;
    }
    rowIds_ = it->second;
}

bool IndexScanExecutor::next(Row &row) {
    if (alwaysEmpty_) {
        return false;
    }

    const Table &sourceTable = db_->getTable(table_);
    while (cursor_ < rowIds_.size()) {
        const std::size_t rowId = rowIds_[cursor_++];
        if (rowId >= sourceTable.size()) {
            continue;
        }

        const Row &source = sourceTable[rowId];
        if (hasPushedPredicate_ && !pushedPredicate_.evaluatePredicate(source)) {
            continue;
        }

        row.clear();
        row.reserve(outputRowReserve_);
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            const auto &col = columns_[i];
            const auto it = source.find(col);
            const std::string value = (it == source.end()) ? std::string() : it->second;
            row[col] = value;
            row[qualifiedColumns_[i]] = value;
        }
        return true;
    }

    return false;
}

void IndexScanExecutor::close() {
    rowIds_.clear();
    cursor_ = 0;
}
