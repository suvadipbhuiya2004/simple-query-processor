#include "execution/limit_executor.h"

#include <stdexcept>
#include <utility>

LimitExecutor::LimitExecutor(std::unique_ptr<Executor> c, size_t limit)
    : child(std::move(c)), limitCount(limit) {
    if (!child) {
        throw std::runtime_error("LimitExecutor received a null child executor");
    }
}

void LimitExecutor::open() {
    child->open();
    emitted = 0;
}

bool LimitExecutor::next(Row& row) {
    if (emitted >= limitCount) {
        return false;
    }

    if (!child->next(row)) {
        return false;
    }

    ++emitted;
    return true;
}

void LimitExecutor::close() {
    child->close();
    emitted = 0;
}
