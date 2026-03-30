#pragma once
#include "execution/executor.h"

#include <string>
#include <vector>

// Projects selected columns from child rows.
class ProjectionExecutor : public Executor {
private:
    std::unique_ptr<Executor> child;
    std::vector<std::string> columns;
    bool selectAll;

public:
    ProjectionExecutor(std::unique_ptr<Executor> c, std::vector<std::string> cols, bool selectAll);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};