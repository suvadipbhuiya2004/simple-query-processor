#pragma once
#include "execution/executor.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Removes duplicate rows while preserving first-seen order.
class DistinctExecutor final : public Executor {
private:
    std::unique_ptr<Executor> child_;
    std::vector<Row> outputRows_;
    std::size_t cursor_{0};

    static std::string rowSignature(const Row& row);
    
public:
    explicit DistinctExecutor(std::unique_ptr<Executor> child);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};
