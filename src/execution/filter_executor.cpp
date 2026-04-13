#include "execution/filter_executor.hpp"
#include <stdexcept>
#include <utility>

FilterExecutor::FilterExecutor(std::unique_ptr<Executor> child, std::unique_ptr<Expr> predicate) : child_(std::move(child)), predicate_(std::move(predicate)) {
    if (!child_) throw std::runtime_error("FilterExecutor: null child executor");
}

void FilterExecutor::open() { child_->open(); }
void FilterExecutor::close() { child_->close(); }

bool FilterExecutor::next(Row& row) {
    while (child_->next(row)) {
        if (ExpressionEvaluator::evalPredicate(predicate_.get(), row)) {
            return true;
        }
    }
    return false;
}