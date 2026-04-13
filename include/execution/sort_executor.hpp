#pragma once
#include "execution/executor.hpp"
#include <cstddef>
#include <string>
#include <vector>

// buffers all child rows then emits them in ascending order
class SortExecutor final : public Executor {
private:
    std::unique_ptr<Executor> child_;
    std::string orderByColumn_;
    std::vector<Row> buffer_;
    std::size_t cursor_{0};

public:
    SortExecutor(std::unique_ptr<Executor> child, std::string orderByColumn);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};