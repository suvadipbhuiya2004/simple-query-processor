#pragma once
#include "execution/executor.h"

#include <cstddef>
#include <string>
#include <vector>

// Buffers all child rows and emits them sorted by one column.
class SortExecutor : public Executor {
private:
    std::unique_ptr<Executor> child;
    std::string orderByColumn;
    std::vector<Row> rows;
    size_t cursor{0};

public:
    SortExecutor(std::unique_ptr<Executor> c, std::string column);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};
