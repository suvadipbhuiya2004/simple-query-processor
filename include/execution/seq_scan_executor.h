#pragma once
#include "execution/executor.h"

#include <cstddef>

// Scans all rows from a single table in stored order.
class SeqScanExecutor : public Executor {
private:
    Table* table;
    size_t index;

public:
    explicit SeqScanExecutor(Table* t);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};