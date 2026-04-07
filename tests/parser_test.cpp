#include <gtest/gtest.h>
#include "parser/lexer.h"
#include "parser/parser.h"

TEST(ParserTest, SimpleSelect) {
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    EXPECT_EQ(stmt.columns.size(), 1);
    EXPECT_EQ(dynamic_cast<Column*>(stmt.columns[0].get())->name, "*");
    EXPECT_EQ(stmt.where, nullptr);
}

TEST(ParserTest, SpecificColumns) {
    Lexer lexer("SELECT name, age FROM users");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    ASSERT_EQ(stmt.columns.size(), 2);
    EXPECT_EQ(dynamic_cast<Column*>(stmt.columns[0].get())->name, "name");
    EXPECT_EQ(dynamic_cast<Column*>(stmt.columns[1].get())->name, "age");
}

TEST(ParserTest, WhereClause) {
    Lexer lexer("SELECT * FROM users WHERE age = 25");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_NE(stmt.where, nullptr);
    auto* binaryExpr = dynamic_cast<BinaryExpr*>(stmt.where.get());
    ASSERT_NE(binaryExpr, nullptr);
    EXPECT_EQ(binaryExpr->op, "=");

    auto* left = dynamic_cast<Column*>(binaryExpr->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->name, "age");

    auto* right = dynamic_cast<Literal*>(binaryExpr->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->value, "25");
}

TEST(ParserTest, OrderByLimit) {
    Lexer lexer("SELECT * FROM users ORDER BY age LIMIT 10");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.orderBy, "age");
    EXPECT_EQ(stmt.limit, 10);
}

TEST(ParserTest, InvalidSyntax) {
    Lexer lexer("SELECT * FROM");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    EXPECT_THROW(parser.parse(), std::runtime_error);
}

TEST(ParserTest, SemicolonTerminator) {
    Lexer lexer("SELECT id FROM users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    ASSERT_EQ(stmt.columns.size(), 1);
    EXPECT_EQ(dynamic_cast<Column*>(stmt.columns[0].get())->name, "id");
}

TEST(ParserTest, AngleBracketNotEqualOperator) {
    Lexer lexer("SELECT * FROM users WHERE age <> 30");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_NE(stmt.where, nullptr);
    auto* binaryExpr = dynamic_cast<BinaryExpr*>(stmt.where.get());
    ASSERT_NE(binaryExpr, nullptr);
    EXPECT_EQ(binaryExpr->op, "<>");
}

TEST(ParserTest, LimitOutOfRange) {
    Lexer lexer("SELECT * FROM users LIMIT 999999999999999999999999");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    EXPECT_THROW(parser.parse(), std::runtime_error);
}

TEST(ParserTest, PrecedenceAndParentheses) {
    // AND has higher precedence than OR
    {
        Lexer lexer("SELECT * FROM users WHERE a = 1 OR b = 2 AND c = 3");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();
        
        // Should be (a=1) OR ((b=2) AND (c=3))
        auto* orExpr = dynamic_cast<BinaryExpr*>(stmt.where.get());
        ASSERT_NE(orExpr, nullptr);
        EXPECT_EQ(orExpr->op, "OR");
        
        auto* andExpr = dynamic_cast<BinaryExpr*>(orExpr->right.get());
        ASSERT_NE(andExpr, nullptr);
        EXPECT_EQ(andExpr->op, "AND");
    }

    // Parentheses override precedence
    {
        Lexer lexer("SELECT * FROM users WHERE (a = 1 OR b = 2) AND c = 3");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();
        
        // Should be ((a=1) OR (b=2)) AND (c=3)
        auto* andExpr = dynamic_cast<BinaryExpr*>(stmt.where.get());
        ASSERT_NE(andExpr, nullptr);
        EXPECT_EQ(andExpr->op, "AND");
        
        auto* orExpr = dynamic_cast<BinaryExpr*>(andExpr->left.get());
        ASSERT_NE(orExpr, nullptr);
        EXPECT_EQ(orExpr->op, "OR");
    }
}
