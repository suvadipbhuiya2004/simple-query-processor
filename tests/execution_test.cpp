#include <gtest/gtest.h>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/planner.h"
#include "execution/executor_builder.h"
#include "storage/table.h"

#include <unordered_set>

class ExecutionTest : public ::testing::Test {
protected:
    Database db;

    void SetUp() override {
        Table users;
        users.push_back({{"id", "1"}, {"name", "Alice"}, {"age", "25"}, {"department", "Engineering"}});
        users.push_back({{"id", "2"}, {"name", "Bob"}, {"age", "30"}, {"department", "Sales"}});
        users.push_back({{"id", "3"}, {"name", "Charlie"}, {"age", "20"}, {"department", "Engineering"}});
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

TEST_F(ExecutionTest, OrderByAscending) {
    Lexer lexer("SELECT * FROM users ORDER BY age");
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

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].at("name"), "Charlie");
    EXPECT_EQ(results[1].at("name"), "Alice");
    EXPECT_EQ(results[2].at("name"), "Bob");
}

TEST_F(ExecutionTest, LimitRowCount) {
    Lexer lexer("SELECT * FROM users LIMIT 2");
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

    EXPECT_EQ(results.size(), 2);
}

TEST_F(ExecutionTest, OrderByWithLimit) {
    Lexer lexer("SELECT name FROM users ORDER BY age LIMIT 2");
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

    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(results[0].size(), 1);
    ASSERT_EQ(results[1].size(), 1);
    EXPECT_EQ(results[0].at("name"), "Charlie");
    EXPECT_EQ(results[1].at("name"), "Alice");
}

TEST_F(ExecutionTest, LimitZeroReturnsNoRows) {
    Lexer lexer("SELECT * FROM users LIMIT 0");
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

    EXPECT_TRUE(results.empty());
}

TEST_F(ExecutionTest, FilterNotEqualAliasOperator) {
    Lexer lexer("SELECT name FROM users WHERE age <> 30");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::unordered_set<std::string> names;
    while (executor->next(row)) {
        names.insert(row.at("name"));
    }
    executor->close();

    EXPECT_EQ(names.size(), 2);
    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Charlie"));
}

TEST_F(ExecutionTest, GroupBySingleColumn) {
    Lexer lexer("SELECT department FROM users GROUP BY department");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::unordered_set<std::string> departments;
    while (executor->next(row)) {
        departments.insert(row.at("department"));
    }
    executor->close();

    // Engineering and Sales should be the only two unique departments returned
    EXPECT_EQ(departments.size(), 2);
    EXPECT_TRUE(departments.count("Engineering"));
    EXPECT_TRUE(departments.count("Sales"));
}

TEST_F(ExecutionTest, GroupByWithHaving) {
    // Note: If your engine doesn't support strings with single quotes yet, 
    // change 'Engineering' to "Engineering" depending on your Lexer's string rules.
    Lexer lexer("SELECT department FROM users GROUP BY department HAVING department = 'Engineering'");
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

    // Only Engineering should pass the HAVING filter
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("department"), "Engineering");
}

TEST_F(ExecutionTest, ComplexFilterQuery) {
    // (Engineering AND age > 22) OR name = 'Bob'
    // Alice: Engineering, 25 -> Matches first part
    // Bob: Sales, 30 -> Matches second part
    // Charlie: Engineering, 20 -> Matches neither
    Lexer lexer("SELECT name FROM users WHERE (department = 'Engineering' AND age > 22) OR name = 'Bob'");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::unordered_set<std::string> names;
    while (executor->next(row)) {
        names.insert(row.at("name"));
    }
    executor->close();

    EXPECT_EQ(names.size(), 2);
    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Bob"));
    EXPECT_FALSE(names.count("Charlie"));
}
