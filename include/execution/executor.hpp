#pragma once
#include "storage/table.hpp"
#include <memory>

// volcano/iterator model base interface.
// Lifecycle: open() → next()* → close()
class Executor {
protected:
    Executor() = default;
    
public:
    virtual void open() = 0;
    virtual bool next(Row& row) = 0;
    virtual void close() = 0;
    virtual ~Executor() = default;

    // Non-copyable
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
};