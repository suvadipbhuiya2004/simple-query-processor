#pragma once
#include "execution/executor.hpp"
#include <cstddef>

// forwards at most `limitCount` rows from its child
class LimitExecutor final : public Executor {
private:
    std::unique_ptr<Executor> child_;
    std::size_t limitCount_;
    std::size_t emitted_{0};

public:
    LimitExecutor(std::unique_ptr<Executor> child, std::size_t limitCount);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};