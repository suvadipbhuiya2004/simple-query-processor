#pragma once

#include "execution/executor.hpp"
#include <cstddef>
#include <string>
#include <vector>

// Buffers child rows, caches ORDER BY keys once, then emits in sorted order.
class SortExecutor final : public Executor {
private:
    struct SortItem {
        Row row;
        std::string key;
    };

    std::unique_ptr<Executor> child_;
    std::string orderByColumn_;
    bool ascending_{true};
    std::vector<SortItem> buffer_;
    std::size_t cursor_{0};

public:
    SortExecutor(std::unique_ptr<Executor> child, std::string orderByColumn, bool ascending);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};