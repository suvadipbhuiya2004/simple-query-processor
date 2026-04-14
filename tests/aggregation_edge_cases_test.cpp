#include <gtest/gtest.h>
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "planner/plan.hpp"
#include "execution/executor_builder.hpp"
#include "storage/table.hpp"
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

    std::vector<Row> runSelect(Database &db, const std::string &sql) {
        Lexer lexer(sql);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();

        Planner planner;
        auto plan = planner.createPlan(stmt, &db);

        auto executor = ExecutorBuilder::build(plan.get(), db);
        executor->open();

        Row row;
        std::vector<Row> results;
        while (executor->next(row)) {
            results.push_back(row);
        }
        executor->close();
        return results;
    }

}

class AggregationEdgeCasesTest : public ::testing::Test {
protected:
    Database db;

    void SetUp() override {
        Table sales;
        sales.push_back({{"customer", "Alice"}, {"product", "A"}, {"amount", "100"}});
        sales.push_back({{"customer", "Alice"}, {"product", "A"}, {"amount", "150"}});
        sales.push_back({{"customer", "Alice"}, {"product", "B"}, {"amount", "200"}});
        sales.push_back({{"customer", "Bob"}, {"product", "A"}, {"amount", "300"}});
        sales.push_back({{"customer", "Bob"}, {"product", "B"}, {"amount", "250"}});
        sales.push_back({{"customer", "Charlie"}, {"product", "A"}, {"amount", "400"}});
        sales.push_back({{"customer", "Charlie"}, {"product", "A"}, {"amount", "350"}});

        db.tables["sales"] = std::move(sales);
        db.schemas["sales"] = {"customer", "product", "amount"};

        Table scores;
        scores.push_back({{"name", "Alice"}, {"score", "95"}});
        scores.push_back({{"name", "Alice"}, {"score", "95"}});
        scores.push_back({{"name", "Bob"}, {"score", "87"}});
        scores.push_back({{"name", "Bob"}, {"score", "92"}});
        scores.push_back({{"name", "Charlie"}, {"score", ""}});
        scores.push_back({{"name", "Charlie"}, {"score", "88"}});
        scores.push_back({{"name", "David"}, {"score", "0"}});
        scores.push_back({{"name", "David"}, {"score", "-5"}});
        db.tables["scores"] = std::move(scores);
        db.schemas["scores"] = {"name", "score"};
    }
};

// Test 1: COUNT(*) basic
TEST_F(AggregationEdgeCasesTest, CountAllRows) {
    auto results = runSelect(db, "SELECT COUNT(*) FROM sales");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT(*)"), "7");
}

// Test 2: COUNT(column) with duplicates
TEST_F(AggregationEdgeCasesTest, CountColumn) {
    auto results = runSelect(db, "SELECT COUNT(customer) FROM sales");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT(customer)"), "7");
}

// Test 3: COUNT(DISTINCT single column)
TEST_F(AggregationEdgeCasesTest, CountDistinctSingleColumn) {
    auto results = runSelect(db, "SELECT COUNT_DISTINCT(customer) FROM sales");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT_DISTINCT(customer)"), "3");
}

// Test 4: COUNT(DISTINCT multiple columns)
TEST_F(AggregationEdgeCasesTest, CountDistinctMultipleColumns) {
    auto results = runSelect(db, "SELECT COUNT_DISTINCT(customer, product) FROM sales");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT_DISTINCT(customer, product)"), "5");
}

// Test 5: COUNT(DISTINCT) per group
TEST_F(AggregationEdgeCasesTest, CountDistinctPerGroup) {
    auto results = runSelect(db, "SELECT customer, COUNT_DISTINCT(product) FROM sales GROUP BY customer");
    ASSERT_EQ(results.size(), 3);

    std::unordered_map<std::string, std::string> resultMap;
    for (const auto &row : results) {
        resultMap[row.at("customer")] = row.at("COUNT_DISTINCT(product)");
    }

    EXPECT_EQ(resultMap["Alice"], "2");
    EXPECT_EQ(resultMap["Bob"], "2");
    EXPECT_EQ(resultMap["Charlie"], "1");
}

// Test 6: COUNT(DISTINCT multiple columns) per group
TEST_F(AggregationEdgeCasesTest, CountDistinctMultipleColumnsPerGroup) {
    auto results = runSelect(db, "SELECT customer, COUNT_DISTINCT(product, amount) FROM sales GROUP BY customer");
    ASSERT_EQ(results.size(), 3);

    std::unordered_map<std::string, std::string> resultMap;
    for (const auto &row : results) {
        resultMap[row.at("customer")] = row.at("COUNT_DISTINCT(product, amount)");
    }

    EXPECT_EQ(resultMap["Alice"], "3");
    EXPECT_EQ(resultMap["Bob"], "2");
    EXPECT_EQ(resultMap["Charlie"], "2");
}

// Test 7: SUM with GROUP BY
TEST_F(AggregationEdgeCasesTest, SumPerGroup) {
    auto results = runSelect(db, "SELECT customer, SUM(amount) FROM sales GROUP BY customer");
    ASSERT_EQ(results.size(), 3);

    std::unordered_map<std::string, std::string> resultMap;
    for (const auto &row : results) {
        resultMap[row.at("customer")] = row.at("SUM(amount)");
    }

    EXPECT_EQ(resultMap["Alice"], "450");
    EXPECT_EQ(resultMap["Bob"], "550");
    EXPECT_EQ(resultMap["Charlie"], "750");
}

// Test 8: AVG, MIN, MAX with GROUP BY
TEST_F(AggregationEdgeCasesTest, AvgMinMaxPerGroup) {
    auto results = runSelect(db, "SELECT customer, AVG(amount), MIN(amount), MAX(amount) FROM sales GROUP BY customer");
    ASSERT_EQ(results.size(), 3);

    std::unordered_map<std::string, Row> resultMap;
    for (const auto &row : results) {
        resultMap[row.at("customer")] = row;
    }

    EXPECT_EQ(resultMap["Alice"].at("AVG(amount)"), "150");
    EXPECT_EQ(resultMap["Alice"].at("MIN(amount)"), "100");
    EXPECT_EQ(resultMap["Alice"].at("MAX(amount)"), "200");

    EXPECT_EQ(resultMap["Bob"].at("AVG(amount)"), "275");
    EXPECT_EQ(resultMap["Bob"].at("MIN(amount)"), "250");
    EXPECT_EQ(resultMap["Bob"].at("MAX(amount)"), "300");
}

// Test 9: COUNT with empty string values (edge case)
TEST_F(AggregationEdgeCasesTest, CountWithEmptyStrings) {
    auto results = runSelect(db, "SELECT COUNT(score) FROM scores");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT(score)"), "8");
}

// Test 10: Negative numbers in aggregates
TEST_F(AggregationEdgeCasesTest, AggregatesWithNegativeNumbers) {
    auto results = runSelect(db, "SELECT SUM(score), AVG(score), MIN(score), MAX(score) FROM scores");
    ASSERT_EQ(results.size(), 1);

    EXPECT_EQ(results[0].at("MIN(score)"), "-5");
    EXPECT_EQ(results[0].at("MAX(score)"), "95");
}

// Test 11: COUNT(DISTINCT) with empty string
TEST_F(AggregationEdgeCasesTest, CountDistinctWithEmptyString) {
    auto results = runSelect(db, "SELECT COUNT_DISTINCT(score) FROM scores");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT_DISTINCT(score)"), "7");
}

// Test 12: All aggregate functions combined
TEST_F(AggregationEdgeCasesTest, AllAggregatesFunctionsCombined) {
    auto results = runSelect(db, "SELECT COUNT(*), COUNT_DISTINCT(customer), SUM(amount), AVG(amount), MIN(amount), MAX(amount) FROM sales");

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT(*)"), "7");
    EXPECT_EQ(results[0].at("COUNT_DISTINCT(customer)"), "3");
    EXPECT_EQ(results[0].at("SUM(amount)"), "1750");
    EXPECT_EQ(results[0].at("AVG(amount)"), "250");
    EXPECT_EQ(results[0].at("MIN(amount)"), "100");
    EXPECT_EQ(results[0].at("MAX(amount)"), "400");
}

// Test 13: GROUP BY with HAVING and COUNT(DISTINCT)
TEST_F(AggregationEdgeCasesTest, GroupByHavingWithCountDistinct) {
    auto results = runSelect(db, "SELECT customer, COUNT_DISTINCT(product) FROM sales GROUP BY customer HAVING COUNT_DISTINCT(product) >= 2");

    ASSERT_EQ(results.size(), 2);
    std::unordered_set<std::string> customers;
    for (const auto &row : results) {
        customers.insert(row.at("customer"));
    }
    EXPECT_TRUE(customers.count("Alice"));
    EXPECT_TRUE(customers.count("Bob"));
    EXPECT_FALSE(customers.count("Charlie"));
}

// Test 14: ORDER BY with aggregate
TEST_F(AggregationEdgeCasesTest, OrderByAggregate) {
    auto results = runSelect(db, "SELECT customer, COUNT_DISTINCT(product) FROM sales GROUP BY customer ORDER BY COUNT_DISTINCT(product) DESC");

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].at("COUNT_DISTINCT(product)"), "2");
    EXPECT_EQ(results[1].at("COUNT_DISTINCT(product)"), "2");
    EXPECT_EQ(results[2].at("COUNT_DISTINCT(product)"), "1");
}

// Test 15: Single row aggregate
TEST_F(AggregationEdgeCasesTest, SingleRowAggregate) {
    Table single;
    single.push_back({{"x", "42"}});
    db.tables["single"] = std::move(single);
    db.schemas["single"] = {"x"};

    auto results = runSelect(db, "SELECT COUNT(*), SUM(x), AVG(x) FROM single");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].at("COUNT(*)"), "1");
    EXPECT_EQ(results[0].at("SUM(x)"), "42");
    EXPECT_EQ(results[0].at("AVG(x)"), "42");
}
