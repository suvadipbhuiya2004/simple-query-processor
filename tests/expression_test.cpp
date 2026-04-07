#include <gtest/gtest.h>
#include "execution/expression.h"

TEST(ExpressionTest, ScalarLiteral) {
    Literal lit("42");
    Row row;
    EXPECT_EQ(ExpressionEvaluator::eval(&lit, row), "42");
}

TEST(ExpressionTest, ScalarColumn) {
    Column col("name");
    Row row = {{"name", "Alice"}, {"age", "25"}};
    EXPECT_EQ(ExpressionEvaluator::eval(&col, row), "Alice");
}

TEST(ExpressionTest, NumericComparison) {
    auto left = std::make_unique<Literal>("10");
    auto right = std::make_unique<Literal>("5");
    BinaryExpr gt(std::move(left), ">", std::move(right));
    
    Row row;
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&gt, row));
    
    auto left2 = std::make_unique<Literal>("10");
    auto right2 = std::make_unique<Literal>("20");
    BinaryExpr lt(std::move(left2), "<", std::move(right2));
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&lt, row));
}

TEST(ExpressionTest, StringComparison) {
    auto left = std::make_unique<Literal>("apple");
    auto right = std::make_unique<Literal>("banana");
    BinaryExpr lt(std::move(left), "<", std::move(right));
    
    Row row;
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&lt, row));
}

TEST(ExpressionTest, Equality) {
    auto left = std::make_unique<Literal>("test");
    auto right = std::make_unique<Literal>("test");
    BinaryExpr eq(std::move(left), "=", std::move(right));
    
    Row row;
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&eq, row));
}

TEST(ExpressionTest, NotEqual) {
    auto left = std::make_unique<Literal>("abc");
    auto right = std::make_unique<Literal>("def");
    BinaryExpr ne(std::move(left), "!=", std::move(right));
    
    Row row;
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&ne, row));
}

TEST(ExpressionTest, UnknownColumn) {
    Column col("missing");
    Row row = {{"name", "Alice"}};
    EXPECT_THROW(ExpressionEvaluator::eval(&col, row), std::runtime_error);
}

TEST(ExpressionTest, GreaterEqualAndLessEqual) {
    auto geLeft = std::make_unique<Literal>("10");
    auto geRight = std::make_unique<Literal>("10");
    BinaryExpr ge(std::move(geLeft), ">=", std::move(geRight));

    auto leLeft = std::make_unique<Literal>("3");
    auto leRight = std::make_unique<Literal>("5");
    BinaryExpr le(std::move(leLeft), "<=", std::move(leRight));

    Row row;
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&ge, row));
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&le, row));
}

TEST(ExpressionTest, AngleBracketNotEqualAlias) {
    auto left = std::make_unique<Literal>("alice");
    auto right = std::make_unique<Literal>("bob");
    BinaryExpr ne(std::move(left), "<>", std::move(right));

    Row row;
    EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&ne, row));
}

TEST(ExpressionTest, BooleanLogic) {
    Row row = {{"a", "1"}, {"b", "0"}};
    
    // (a = 1) AND (b = 0) -> TRUE
    {
        auto left = std::make_unique<BinaryExpr>(std::make_unique<Column>("a"), "=", std::make_unique<Literal>("1"));
        auto right = std::make_unique<BinaryExpr>(std::make_unique<Column>("b"), "=", std::make_unique<Literal>("0"));
        BinaryExpr andExpr(std::move(left), "AND", std::move(right));
        EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&andExpr, row));
    }
    
    // (a = 0) AND (b = 0) -> FALSE
    {
        auto left = std::make_unique<BinaryExpr>(std::make_unique<Column>("a"), "=", std::make_unique<Literal>("0"));
        auto right = std::make_unique<BinaryExpr>(std::make_unique<Column>("b"), "=", std::make_unique<Literal>("0"));
        BinaryExpr andExpr(std::move(left), "AND", std::move(right));
        EXPECT_FALSE(ExpressionEvaluator::evalPredicate(&andExpr, row));
    }
    
    // (a = 0) OR (b = 0) -> TRUE
    {
        auto left = std::make_unique<BinaryExpr>(std::make_unique<Column>("a"), "=", std::make_unique<Literal>("0"));
        auto right = std::make_unique<BinaryExpr>(std::make_unique<Column>("b"), "=", std::make_unique<Literal>("0"));
        BinaryExpr orExpr(std::move(left), "OR", std::move(right));
        EXPECT_TRUE(ExpressionEvaluator::evalPredicate(&orExpr, row));
    }
}
