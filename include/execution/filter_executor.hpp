#pragma once
#include "execution/executor.hpp"
#include "execution/expression.hpp"
#include <memory>

// passes through only rows where predicate evaluates true.
class FilterExecutor final : public Executor {
private:
    std::unique_ptr<Executor> child_;
    std::unique_ptr<Expr> predicate_;
    
public:
    FilterExecutor(std::unique_ptr<Executor> child, std::unique_ptr<Expr> predicate);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};