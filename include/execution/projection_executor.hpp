#pragma once
#include "execution/executor.hpp"
#include <string>
#include <vector>

// keeps only the selected columns from each child row.
class ProjectionExecutor final : public Executor {
private:
    std::unique_ptr<Executor> child_;
    std::vector<std::string> columns_;
    bool selectAll_;
    
public:
    ProjectionExecutor(std::unique_ptr<Executor> child, std::vector<std::string> columns, bool selectAll);

    void open() override;
    bool next(Row& row) override;
    void close() override;
};