#include "execution/projection_executor.h"

#include <stdexcept>
#include <utility>

// Projection executor keeps selected columns from each child row.

ProjectionExecutor::ProjectionExecutor(std::unique_ptr<Executor> c, std::vector<std::string> cols, bool selectAll)
    : child(std::move(c)), columns(std::move(cols)), selectAll(selectAll) {
    if (!child) {
        throw std::runtime_error("ProjectionExecutor received a null child executor");
    }
    if (!this->selectAll && columns.empty()) {
        throw std::runtime_error("ProjectionExecutor requires at least one projected column");
    }
}

void ProjectionExecutor::open() {
    child->open();
}

bool ProjectionExecutor::next(Row& row) {
    Row input;
    if (!child->next(input)) {
        return false;
    }

    if (selectAll) {
        row = std::move(input);
        return true;
    }

    Row output;
    output.reserve(columns.size());

    for (const auto& columnName : columns) {
        const auto it = input.find(columnName);
        if (it == input.end()) {
            throw std::runtime_error("Column not found in row: " + columnName);
        }
        output[columnName] = it->second;
    }

    row = std::move(output);
    return true;
}

void ProjectionExecutor::close() {
    child->close();
}
