#include "execution/filter_executor.h"

#include <stdexcept>
#include <utility>

// Filter executor delegates row production to child and applies predicate.

FilterExecutor::FilterExecutor(std::unique_ptr<Executor> c, std::unique_ptr<Expr> p)
    : child(std::move(c)), predicate(std::move(p)) {
    if (!child) {
        throw std::runtime_error("FilterExecutor received a null child executor");
    }
}

void FilterExecutor::open() {
    child->open();
}

bool FilterExecutor::next(Row& row) {
    while (child->next(row)) {
        if (!predicate || ExpressionEvaluator::evalPredicate(predicate.get(), row)) {
            return true;
        }
    }
    return false;
}

void FilterExecutor::close() {
    child->close();
}
