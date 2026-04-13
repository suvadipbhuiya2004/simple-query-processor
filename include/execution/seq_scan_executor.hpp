#pragma once
#include "execution/executor.hpp"
#include <cstddef>
#include <string>
#include <vector>

// scans all rows from a Table in stored order.
class SeqScanExecutor final : public Executor {
private:
    const Table* table_;
    std::vector<std::string> columns_;
    std::string qualifier_;
    std::size_t index_{0};
    
public:
    SeqScanExecutor(const Table* table, const std::vector<std::string>* schema, std::string qualifier);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};