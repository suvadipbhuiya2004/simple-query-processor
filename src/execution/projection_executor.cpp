#include "execution/projection_executor.hpp"
#include <stdexcept>
#include <utility>

ProjectionExecutor::ProjectionExecutor(std::unique_ptr<Executor> child, std::vector<std::string> columns, bool selectAll)
    : child_(std::move(child)), columns_(std::move(columns)), selectAll_(selectAll) {
    if (!child_)
        throw std::runtime_error("ProjectionExecutor: null child executor");
    if (!selectAll_ && columns_.empty()) {
        throw std::runtime_error("ProjectionExecutor: no columns specified");
    }
}

void ProjectionExecutor::open() {
    child_->open();
}
void ProjectionExecutor::close() {
    child_->close();
}

bool ProjectionExecutor::next(Row &row) {
    if (!child_->next(inputBuffer_))
        return false;

    if (selectAll_) {
        row = std::move(inputBuffer_);
        return true;
    }

    row.clear();
    row.reserve(columns_.size());
    for (const auto &col : columns_) {
        const auto it = inputBuffer_.find(col);
        if (it == inputBuffer_.end()) {
            throw std::runtime_error("Projected column not found in row: '" + col + "'");
        }
        row.emplace(col, std::move(it->second));
    }
    return true;
}