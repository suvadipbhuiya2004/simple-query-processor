#include <gtest/gtest.h>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/planner.h"
#include "execution/executor_builder.h"
#include "storage/table.h"

class ExecutionTest : public ::testing::Test {
protected:
    Database db;

    void SetUp() override {
        Table users;
        users.push_back({{"id", "1"}, {"name", "Alice"}, {"age", "25"}});
        users.push_back({{"id", "2"}, {"name", "Bob"}, {"age", "30"}});
        users.push_back({{"id", "3"}, {"name", "Charlie"}, {"age", "20"}});
        db.tables["users"] = std::move(users);
    }
};

TEST_F(ExecutionTest, SelectAll) {
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row)) {
        results.push_back(row);
    }
    executor->close();

    EXPECT_EQ(results.size(), 3);
}

TEST_F(ExecutionTest, FilterQuery) {
    Lexer lexer("SELECT name FROM users WHERE age > 21");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row)) {
        results.push_back(row);
    }
    executor->close();

    // Alice (25) and Bob (30) match. Charlie (20) does not.
    EXPECT_EQ(results.size(), 2);
    
    for (const auto& r : results) {
        EXPECT_EQ(r.size(), 1);
        EXPECT_TRUE(r.count("name"));
    }
}

TEST_F(ExecutionTest, UnsupportedOrderBy) {
    Lexer lexer("SELECT * FROM users ORDER BY age");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    EXPECT_THROW(planner.createPlan(stmt), std::runtime_error);
}

TEST_F(ExecutionTest, UnsupportedLimit) {
    Lexer lexer("SELECT * FROM users LIMIT 10");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    EXPECT_THROW(planner.createPlan(stmt), std::runtime_error);
}
