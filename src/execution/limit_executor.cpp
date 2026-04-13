#include "execution/limit_executor.hpp"
#include <stdexcept>
#include <utility>

LimitExecutor::LimitExecutor(std::unique_ptr<Executor> child, std::size_t limitCount) : child_(std::move(child)), limitCount_(limitCount) {
    if (!child_) throw std::runtime_error("LimitExecutor: null child executor");
}

void LimitExecutor::open() {
    child_->open();
    emitted_ = 0;
}

bool LimitExecutor::next(Row& row) {
    if (emitted_ >= limitCount_) return false;
    if (!child_->next(row)) return false;
    ++emitted_;
    return true;
}

void LimitExecutor::close() {
    child_->close();
    emitted_ = 0;
}