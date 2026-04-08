#pragma once
#include "execution/executor.h"

#include <cstddef>

// Emits at most N rows from child executor.
class LimitExecutor : public Executor {
private:
    std::unique_ptr<Executor> child;
    size_t limitCount;
    size_t emitted{0};

public:
    LimitExecutor(std::unique_ptr<Executor> c, size_t limit);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};
