#include <gtest/gtest.h>
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "planner/plan.hpp"
#include "execution/executor_builder.hpp"
#include "storage/table.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace
{

    JoinNode *findJoinNode(PlanNode *node)
    {
        if (node == nullptr)
            return nullptr;
        if (node->type == PlanType::JOIN)
        {
            return static_cast<JoinNode *>(node);
        }
        for (auto &child : node->children)
        {
            if (auto *found = findJoinNode(child.get()))
            {
                return found;
            }
        }
        return nullptr;
    }

    SeqScanNode *findSeqScanByQualifier(PlanNode *node, const std::string &qualifier)
    {
        if (node == nullptr)
            return nullptr;
        if (node->type == PlanType::SEQ_SCAN)
        {
            auto *scan = static_cast<SeqScanNode *>(node);
            if (scan->outputQualifier == qualifier)
            {
                return scan;
            }
        }
        for (auto &child : node->children)
        {
            if (auto *found = findSeqScanByQualifier(child.get(), qualifier))
            {
                return found;
            }
        }
        return nullptr;
    }

    IndexScanNode *findIndexScanByQualifier(PlanNode *node, const std::string &qualifier)
    {
        if (node == nullptr)
            return nullptr;
        if (node->type == PlanType::INDEX_SCAN)
        {
            auto *scan = static_cast<IndexScanNode *>(node);
            if (scan->outputQualifier == qualifier)
            {
                return scan;
            }
        }
        for (auto &child : node->children)
        {
            if (auto *found = findIndexScanByQualifier(child.get(), qualifier))
            {
                return found;
            }
        }
        return nullptr;
    }

    void collectLeafQualifiers(const PlanNode *node, std::vector<std::string> &out)
    {
        if (node == nullptr)
            return;
        if (node->type == PlanType::SEQ_SCAN)
        {
            const auto *scan = static_cast<const SeqScanNode *>(node);
            out.push_back(scan->outputQualifier);
            return;
        }
        if (node->type == PlanType::INDEX_SCAN)
        {
            const auto *scan = static_cast<const IndexScanNode *>(node);
            out.push_back(scan->outputQualifier);
            return;
        }
        for (const auto &child : node->children)
        {
            collectLeafQualifiers(child.get(), out);
        }
    }

    std::vector<Row> runSelect(Database &db, const std::string &sql,
                               JoinAlgorithm *algoOverride = nullptr)
    {
        Lexer lexer(sql);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();

        Planner planner;
        auto plan = planner.createPlan(stmt, &db);

        if (algoOverride != nullptr)
        {
            auto *join = findJoinNode(plan.get());
            if (join == nullptr)
            {
                throw std::runtime_error("Expected JOIN node but none was found in plan");
            }
            join->algorithm = *algoOverride;
        }

        auto executor = ExecutorBuilder::build(plan.get(), db);
        executor->open();

        Row row;
        std::vector<Row> results;
        while (executor->next(row))
        {
            results.push_back(row);
        }
        executor->close();
        return results;
    }

} 

class ExecutionTest : public ::testing::Test
{
protected:
    Database db;

    void SetUp() override
    {
        Table users;
        users.push_back({{"id", "1"},
                         {"name", "Alice"},
                         {"age", "25"},
                         {"department", "Engineering"},
                         {"dept_id", "10"}});
        users.push_back({{"id", "2"},
                         {"name", "Bob"},
                         {"age", "30"},
                         {"department", "Sales"},
                         {"dept_id", "20"}});
        users.push_back({{"id", "3"},
                         {"name", "Charlie"},
                         {"age", "20"},
                         {"department", "Engineering"},
                         {"dept_id", "30"}});
        db.tables["users"] = std::move(users);

        Table departments;
        departments.push_back({{"dept_id", "10"}, {"dept_name", "Engineering"}});
        departments.push_back({{"dept_id", "20"}, {"dept_name", "Sales"}});
        departments.push_back({{"dept_id", "40"}, {"dept_name", "HR"}});
        db.tables["departments"] = std::move(departments);

        db.schemas["users"] = {"id", "name", "age", "department", "dept_id"};
        db.schemas["departments"] = {"dept_id", "dept_name"};
    }
};

TEST_F(ExecutionTest, SelectAll)
{
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    EXPECT_EQ(results.size(), 3);
}

TEST_F(ExecutionTest, FilterQuery)
{
    Lexer lexer("SELECT name FROM users WHERE age > 21");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    // Alice (25) and Bob (30) match. Charlie (20) does not.
    EXPECT_EQ(results.size(), 2);

    for (const auto &r : results)
    {
        EXPECT_EQ(r.size(), 1);
        EXPECT_TRUE(r.count("name"));
    }
}

TEST_F(ExecutionTest, OptimizerPushesPredicateIntoSeqScan)
{
    Lexer lexer("SELECT name FROM users WHERE age > 21");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    ASSERT_EQ(plan->type, PlanType::PROJECTION);
    ASSERT_EQ(plan->children.size(), 1u);
    auto *scan = dynamic_cast<SeqScanNode *>(plan->children[0].get());
    ASSERT_NE(scan, nullptr);
    ASSERT_NE(scan->pushedPredicate, nullptr);

    const bool hasName = std::find(scan->requiredColumns.begin(), scan->requiredColumns.end(),
                                   "name") != scan->requiredColumns.end();
    const bool hasAge = std::find(scan->requiredColumns.begin(), scan->requiredColumns.end(),
                                  "age") != scan->requiredColumns.end();
    EXPECT_TRUE(hasName);
    EXPECT_TRUE(hasAge);
}

TEST_F(ExecutionTest, OptimizerConstantFalseMarksScanEmpty)
{
    Lexer lexer("SELECT name FROM users WHERE 1 = 0");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    ASSERT_EQ(plan->type, PlanType::PROJECTION);
    ASSERT_EQ(plan->children.size(), 1u);
    auto *scan = dynamic_cast<SeqScanNode *>(plan->children[0].get());
    ASSERT_NE(scan, nullptr);
    EXPECT_TRUE(scan->alwaysEmpty);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();
    Row row;
    EXPECT_FALSE(executor->next(row));
    executor->close();
}

TEST_F(ExecutionTest, OptimizerPushesJoinSideFilterIntoLeftScan)
{
    Lexer lexer("SELECT users.name FROM users INNER JOIN departments "
                "ON users.dept_id = departments.dept_id "
                "WHERE users.age > 21");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    ASSERT_EQ(plan->type, PlanType::PROJECTION);
    auto *join = findJoinNode(plan.get());
    ASSERT_NE(join, nullptr);

    auto *leftScan = findSeqScanByQualifier(plan.get(), "users");
    auto *rightScan = findSeqScanByQualifier(plan.get(), "departments");
    ASSERT_NE(leftScan, nullptr);
    ASSERT_NE(rightScan, nullptr);

    EXPECT_NE(leftScan->pushedPredicate, nullptr);
    EXPECT_EQ(rightScan->pushedPredicate, nullptr);
}

TEST_F(ExecutionTest, OptimizerDoesNotPushRightOnlyFilterThroughLeftJoin)
{
    Lexer lexer("SELECT users.id FROM users LEFT JOIN departments "
                "ON users.dept_id = departments.dept_id "
                "WHERE departments.dept_name = 'Engineering'");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    ASSERT_EQ(plan->type, PlanType::PROJECTION);
    ASSERT_EQ(plan->children.size(), 1u);
    EXPECT_EQ(plan->children[0]->type, PlanType::FILTER);
    ASSERT_EQ(plan->children[0]->children.size(), 1u);
    EXPECT_EQ(plan->children[0]->children[0]->type, PlanType::JOIN);

    auto *rightScan = findSeqScanByQualifier(plan.get(), "departments");
    ASSERT_NE(rightScan, nullptr);
    EXPECT_EQ(rightScan->pushedPredicate, nullptr);
}

TEST_F(ExecutionTest, OptimizerChoosesIndexScanForLargeEqualityFilter)
{
    Table big;
    big.reserve(600);
    for (int i = 0; i < 600; ++i)
    {
        big.push_back({{"id", std::to_string(i)}, {"bucket", std::to_string(i % 10)}});
    }
    db.tables["big"] = std::move(big);
    db.schemas["big"] = {"id", "bucket"};
    db.markTableMutated("big");

    Lexer lexer("SELECT id FROM big WHERE bucket = 3");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto *idx = findIndexScanByQualifier(plan.get(), "big");
    ASSERT_NE(idx, nullptr);
    EXPECT_EQ(idx->lookupColumn, "bucket");
    EXPECT_EQ(idx->lookupValue, "3");
}

TEST_F(ExecutionTest, CostBasedPlannerReordersInnerJoinLeaves)
{
    Table ta;
    Table tb;
    Table tc;
    ta.reserve(1000);
    tb.reserve(10);
    tc.reserve(100);

    for (int i = 0; i < 1000; ++i)
    {
        ta.push_back({{"id", std::to_string(i)}, {"k", std::to_string(i % 10)}});
    }
    for (int i = 0; i < 10; ++i)
    {
        tb.push_back({{"id", std::to_string(i)}, {"k", std::to_string(i)}});
    }
    for (int i = 0; i < 100; ++i)
    {
        tc.push_back({{"id", std::to_string(i)}, {"k", std::to_string(i % 10)}});
    }

    db.tables["ta"] = std::move(ta);
    db.tables["tb"] = std::move(tb);
    db.tables["tc"] = std::move(tc);
    db.schemas["ta"] = {"id", "k"};
    db.schemas["tb"] = {"id", "k"};
    db.schemas["tc"] = {"id", "k"};
    db.markTableMutated("ta");
    db.markTableMutated("tb");
    db.markTableMutated("tc");

    Lexer lexer("SELECT ta.id FROM ta "
                "INNER JOIN tb ON ta.k = tb.k "
                "INNER JOIN tc ON tb.k = tc.k");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    std::vector<std::string> leaves;
    collectLeafQualifiers(plan.get(), leaves);
    ASSERT_EQ(leaves.size(), 3u);

    // The smallest table should be joined first by the DP reorderer.
    EXPECT_EQ(leaves[0], "tb");
}

TEST_F(ExecutionTest, OrderByAscending)
{
    Lexer lexer("SELECT * FROM users ORDER BY age");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].at("name"), "Charlie");
    EXPECT_EQ(results[1].at("name"), "Alice");
    EXPECT_EQ(results[2].at("name"), "Bob");
}

TEST_F(ExecutionTest, OrderByDescending)
{
    Lexer lexer("SELECT * FROM users ORDER BY age DESC");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].at("name"), "Bob");
    EXPECT_EQ(results[1].at("name"), "Alice");
    EXPECT_EQ(results[2].at("name"), "Charlie");
}

TEST_F(ExecutionTest, LimitRowCount)
{
    Lexer lexer("SELECT * FROM users LIMIT 2");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    EXPECT_EQ(results.size(), 2);
}

TEST_F(ExecutionTest, OrderByWithLimit)
{
    Lexer lexer("SELECT name FROM users ORDER BY age LIMIT 2");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(results[0].size(), 1);
    ASSERT_EQ(results[1].size(), 1);
    EXPECT_EQ(results[0].at("name"), "Charlie");
    EXPECT_EQ(results[1].at("name"), "Alice");
}

TEST_F(ExecutionTest, LimitZeroReturnsNoRows)
{
    Lexer lexer("SELECT * FROM users LIMIT 0");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    EXPECT_TRUE(results.empty());
}

TEST_F(ExecutionTest, FilterNotEqualAliasOperator)
{
    Lexer lexer("SELECT name FROM users WHERE age <> 30");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::unordered_set<std::string> names;
    while (executor->next(row))
    {
        names.insert(row.at("name"));
    }
    executor->close();

    EXPECT_EQ(names.size(), 2);
    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Charlie"));
}

TEST_F(ExecutionTest, GroupBySingleColumn)
{
    Lexer lexer("SELECT department FROM users GROUP BY department");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::unordered_set<std::string> departments;
    while (executor->next(row))
    {
        departments.insert(row.at("department"));
    }
    executor->close();

    // Engineering and Sales should be the only two unique departments returned
    EXPECT_EQ(departments.size(), 2);
    EXPECT_TRUE(departments.count("Engineering"));
    EXPECT_TRUE(departments.count("Sales"));
}

TEST_F(ExecutionTest, GroupByWithHaving)
{
    // Note: If your engine doesn't support strings with single quotes yet,
    // change 'Engineering' to "Engineering" depending on your Lexer's string rules.
    Lexer lexer(
        "SELECT department FROM users GROUP BY department HAVING department = 'Engineering'");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::vector<Row> results;
    while (executor->next(row))
    {
        results.push_back(row);
    }
    executor->close();

    // Only Engineering should pass the HAVING filter
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("department"), "Engineering");
}

TEST_F(ExecutionTest, AggregateGlobalCountSumAvgMinMax)
{
    auto results =
        runSelect(db, "SELECT COUNT(*), SUM(age), AVG(age), MIN(age), MAX(age) FROM users");

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT(*)"), "3");
    EXPECT_EQ(results[0].at("SUM(age)"), "75");
    EXPECT_EQ(results[0].at("AVG(age)"), "25");
    EXPECT_EQ(results[0].at("MIN(age)"), "20");
    EXPECT_EQ(results[0].at("MAX(age)"), "30");
}

TEST_F(ExecutionTest, AggregateGroupByWithHavingAndOrderBy)
{
    auto results = runSelect(db, "SELECT department, COUNT(*), AVG(age) FROM users GROUP BY "
                                 "department HAVING COUNT(*) >= 1 ORDER BY AVG(age) DESC");

    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].at("department"), "Sales");
    EXPECT_EQ(results[0].at("COUNT(*)"), "1");
    EXPECT_EQ(results[0].at("AVG(age)"), "30");

    EXPECT_EQ(results[1].at("department"), "Engineering");
    EXPECT_EQ(results[1].at("COUNT(*)"), "2");
    EXPECT_EQ(results[1].at("AVG(age)"), "22.5");
}

TEST_F(ExecutionTest, ComplexFilterQuery)
{
    // (Engineering AND age > 22) OR name = 'Bob'
    // Alice: Engineering, 25 -> Matches first part
    // Bob: Sales, 30 -> Matches second part
    // Charlie: Engineering, 20 -> Matches neither
    Lexer lexer(
        "SELECT name FROM users WHERE (department = 'Engineering' AND age > 22) OR name = 'Bob'");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    Planner planner;
    auto plan = planner.createPlan(stmt, &db);

    auto executor = ExecutorBuilder::build(plan.get(), db);
    executor->open();

    Row row;
    std::unordered_set<std::string> names;
    while (executor->next(row))
    {
        names.insert(row.at("name"));
    }
    executor->close();

    EXPECT_EQ(names.size(), 2);
    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Bob"));
    EXPECT_FALSE(names.count("Charlie"));
}

TEST_F(ExecutionTest, InnerJoinDefaultHashAlgorithm)
{
    const auto results = runSelect(db, "SELECT users.name, departments.dept_name "
                                       "FROM users INNER JOIN departments "
                                       "ON users.dept_id = departments.dept_id");

    ASSERT_EQ(results.size(), 2);
    std::unordered_set<std::string> names;
    std::unordered_set<std::string> deptNames;
    for (const auto &row : results)
    {
        names.insert(row.at("users.name"));
        deptNames.insert(row.at("departments.dept_name"));
    }

    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Bob"));
    EXPECT_TRUE(deptNames.count("Engineering"));
    EXPECT_TRUE(deptNames.count("Sales"));
}

TEST_F(ExecutionTest, LeftJoinIncludesUnmatchedLeftRows)
{
    const auto results = runSelect(db, "SELECT users.id, departments.dept_name "
                                       "FROM users LEFT JOIN departments "
                                       "ON users.dept_id = departments.dept_id");

    ASSERT_EQ(results.size(), 3);
    bool sawUnmatchedLeft = false;
    for (const auto &row : results)
    {
        if (row.at("users.id") == "3")
        {
            sawUnmatchedLeft = true;
            EXPECT_EQ(row.at("departments.dept_name"), "");
        }
    }
    EXPECT_TRUE(sawUnmatchedLeft);
}

TEST_F(ExecutionTest, RightJoinIncludesUnmatchedRightRows)
{
    const auto results = runSelect(db, "SELECT users.id, departments.dept_name "
                                       "FROM users RIGHT JOIN departments "
                                       "ON users.dept_id = departments.dept_id");

    // users: id 1 (dept_id 10), 2 (dept_id 20), 3 (dept_id 30)
    // departments: dept_id 10, 20, 40
    // Matches: (1,10), (2,20); Unmatched right: (40,HR)
    // Also: user 3 has dept_id 30 which doesn't match any department
    ASSERT_EQ(results.size(), 4);
    bool sawUnmatchedRight = false;
    for (const auto &row : results)
    {
        if (row.at("departments.dept_name") == "HR")
        {
            sawUnmatchedRight = true;
            EXPECT_EQ(row.at("users.id"), "");
        }
    }
    EXPECT_TRUE(sawUnmatchedRight);
}

TEST_F(ExecutionTest, FullJoinIncludesUnmatchedBothSides)
{
    const auto results = runSelect(db, "SELECT users.id, departments.dept_id "
                                       "FROM users FULL OUTER JOIN departments "
                                       "ON users.dept_id = departments.dept_id");

    ASSERT_EQ(results.size(), 4);

    bool sawLeftOnly = false;
    bool sawRightOnly = false;
    for (const auto &row : results)
    {
        if (row.at("users.id") == "3" && row.at("departments.dept_id").empty())
        {
            sawLeftOnly = true;
        }
        if (row.at("users.id").empty() && row.at("departments.dept_id") == "40")
        {
            sawRightOnly = true;
        }
    }

    EXPECT_TRUE(sawLeftOnly);
    EXPECT_TRUE(sawRightOnly);
}

TEST_F(ExecutionTest, CrossJoinReturnsCartesianProduct)
{
    const auto results = runSelect(db, "SELECT users.id, departments.dept_id "
                                       "FROM users CROSS JOIN departments");

    EXPECT_EQ(results.size(), 9);
}

TEST_F(ExecutionTest, NestedLoopJoinAlgorithmWorksWhenSelected)
{
    JoinAlgorithm algo = JoinAlgorithm::NESTED_LOOP;
    const auto results = runSelect(db,
                                   "SELECT users.name, departments.dept_name "
                                   "FROM users INNER JOIN departments "
                                   "ON users.dept_id = departments.dept_id",
                                   &algo);

    ASSERT_EQ(results.size(), 2);
}

TEST_F(ExecutionTest, MergeJoinAlgorithmWorksWhenSelected)
{
    JoinAlgorithm algo = JoinAlgorithm::MERGE;
    const auto results = runSelect(db,
                                   "SELECT users.name, departments.dept_name "
                                   "FROM users INNER JOIN departments "
                                   "ON users.dept_id = departments.dept_id",
                                   &algo);

    ASSERT_EQ(results.size(), 2);
}

TEST_F(ExecutionTest, DistinctRemovesDuplicateProjectedRows)
{
    const auto results = runSelect(db, "SELECT DISTINCT users.department FROM users");

    ASSERT_EQ(results.size(), 2);
    std::unordered_set<std::string> departments;
    for (const auto &row : results)
    {
        departments.insert(row.at("users.department"));
    }

    EXPECT_TRUE(departments.count("Engineering"));
    EXPECT_TRUE(departments.count("Sales"));
}

TEST_F(ExecutionTest, CorrelatedExistsSubqueryFiltersRows)
{
    const auto results = runSelect(db, "SELECT users.name FROM users "
                                       "WHERE EXISTS ("
                                       "SELECT departments.dept_id FROM departments "
                                       "WHERE departments.dept_id = users.dept_id)");

    ASSERT_EQ(results.size(), 2);
    std::unordered_set<std::string> names;
    for (const auto &row : results)
    {
        names.insert(row.at("users.name"));
    }

    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Bob"));
    EXPECT_FALSE(names.count("Charlie"));
}

TEST_F(ExecutionTest, CorrelatedNotExistsSubqueryFiltersRows)
{
    const auto results = runSelect(db, "SELECT users.name FROM users "
                                       "WHERE NOT EXISTS ("
                                       "SELECT departments.dept_id FROM departments "
                                       "WHERE departments.dept_id = users.dept_id)");

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("users.name"), "Charlie");
}

TEST_F(ExecutionTest, InSubqueryMatchesProjectedColumnDeterministically)
{
    const auto results =
        runSelect(db, "SELECT users.name FROM users "
                      "WHERE users.dept_id IN (SELECT departments.dept_id FROM departments)");

    ASSERT_EQ(results.size(), 2);
    std::unordered_set<std::string> names;
    for (const auto &row : results)
    {
        names.insert(row.at("users.name"));
    }

    EXPECT_TRUE(names.count("Alice"));
    EXPECT_TRUE(names.count("Bob"));
    EXPECT_FALSE(names.count("Charlie"));
}

TEST_F(ExecutionTest, NotInSubqueryMatchesProjectedColumnDeterministically)
{
    const auto results =
        runSelect(db, "SELECT users.name FROM users "
                      "WHERE users.dept_id NOT IN (SELECT departments.dept_id FROM departments)");

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("users.name"), "Charlie");
}
