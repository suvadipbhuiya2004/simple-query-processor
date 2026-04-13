#include "execution/projection_executor.hpp"
#include <stdexcept>
#include <utility>

ProjectionExecutor::ProjectionExecutor(std::unique_ptr<Executor> child, std::vector<std::string> columns, bool selectAll) : child_(std::move(child)), columns_(std::move(columns)), selectAll_(selectAll) {
    if (!child_) throw std::runtime_error("ProjectionExecutor: null child executor");
    if (!selectAll_ && columns_.empty()) {
        throw std::runtime_error("ProjectionExecutor: no columns specified");
    }
}

void ProjectionExecutor::open() { child_->open(); }
void ProjectionExecutor::close() { child_->close(); }

bool ProjectionExecutor::next(Row& row) {
    Row input;
    if (!child_->next(input)) return false;

    if (selectAll_) {
        row = std::move(input);
        return true;
    }

    Row output;
    output.reserve(columns_.size());
    for (const auto& col : columns_) {
        const auto it = input.find(col);
        if (it == input.end()) {
            throw std::runtime_error("Projected column not found in row: '" + col + "'");
        }
        output.emplace(col, std::move(it->second));
    }
    row = std::move(output);
    return true;
}