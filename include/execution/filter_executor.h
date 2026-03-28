#pragma once
#include "execution/executor.h"
#include "execution/expression.h"

// Passes through only rows where predicate is true.
class FilterExecutor : public Executor {
private:
    std::unique_ptr<Executor> child;
    std::unique_ptr<Expr> predicate;

public:
    FilterExecutor(std::unique_ptr<Executor> c, std::unique_ptr<Expr> p);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};