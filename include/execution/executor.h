#pragma once
#include "planner/plan_node.h"
#include "storage/table.h"

// Base interface implemented by all physical executors.
class Executor {
public:
    virtual void open() = 0;
    virtual bool next(Row& row) = 0;
    virtual void close() = 0;
    virtual ~Executor() = default;
};