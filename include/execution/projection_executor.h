#pragma once
#include "execution/executor.h"
#include "planner/plan_node.h"

class ProjectionExecutor : public Executor {
public:
    // Updated constructor signature
    ProjectionExecutor(std::unique_ptr<Executor> child, const ProjectionNode* node);

    void open() override;
    bool next(Row& row) override;
    void close() override;

private:
    std::unique_ptr<Executor> child;
    const ProjectionNode* node; // Store the node to access Expr objects
};