#include <gtest/gtest.h>
#include "parser/lexer.hpp"
#include <vector>

TEST(LexerTest, SimpleSelect) {
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::STAR);
    EXPECT_EQ(tokens[2].type, TokenType::FROM);
    EXPECT_EQ(tokens[3].type, TokenType::IDENT);
    EXPECT_EQ(tokens[3].value, "users");
    EXPECT_EQ(tokens[4].type, TokenType::END);
}

TEST(LexerTest, WhereClause) {
    Lexer lexer("SELECT name FROM users WHERE age > 21");
    auto tokens = lexer.tokenize();

    // SELECT, name, FROM, users, WHERE, age, >, 21, END
    ASSERT_EQ(tokens.size(), 9);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].value, "name");
    EXPECT_EQ(tokens[4].type, TokenType::WHERE);
    EXPECT_EQ(tokens[6].type, TokenType::OP);
    EXPECT_EQ(tokens[6].value, ">");
    EXPECT_EQ(tokens[7].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[7].value, "21");
}

TEST(LexerTest, StringLiteral) {
    Lexer lexer("SELECT * FROM users WHERE city = 'New York'");
    auto tokens = lexer.tokenize();

    // SELECT, *, FROM, users, WHERE, city, =, 'New York', END
    ASSERT_EQ(tokens.size(), 9);
    EXPECT_EQ(tokens[7].type, TokenType::STRING);
    EXPECT_EQ(tokens[7].value, "New York");
}

TEST(LexerTest, CaseInsensitivity) {
    Lexer lexer("select NAME from USERS");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].value, "NAME");
    EXPECT_EQ(tokens[2].type, TokenType::FROM);
    EXPECT_EQ(tokens[3].value, "USERS");
}

TEST(LexerTest, InvalidCharacter) {
    Lexer lexer("SELECT # FROM users");
    EXPECT_THROW(lexer.tokenize(), std::runtime_error);
}

TEST(LexerTest, UnterminatedString) {
    Lexer lexer("SELECT * FROM users WHERE city = 'London");
    EXPECT_THROW(lexer.tokenize(), std::runtime_error);
}

TEST(LexerTest, SemicolonTerminator) {
    Lexer lexer("SELECT name FROM users;");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].value, "name");
    EXPECT_EQ(tokens[2].type, TokenType::FROM);
    EXPECT_EQ(tokens[3].value, "users");
    EXPECT_EQ(tokens[4].type, TokenType::END);
}

TEST(LexerTest, EscapedSingleQuote) {
    Lexer lexer("SELECT * FROM users WHERE city = 'O''Reilly'");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 9);
    EXPECT_EQ(tokens[7].type, TokenType::STRING);
    EXPECT_EQ(tokens[7].value, "O'Reilly");
}

TEST(LexerTest, AngleBracketNotEqual) {
    Lexer lexer("SELECT * FROM users WHERE age <> 30");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 9);
    EXPECT_EQ(tokens[6].type, TokenType::OP);
    EXPECT_EQ(tokens[6].value, "<>");
}

TEST(LexerTest, AndOrParentheses) {
    Lexer lexer("WHERE (age > 20 AND city = 'Delhi') OR status = 'Active'");
    auto tokens = lexer.tokenize();

    // WHERE, (, age, >, 20, AND, city, =, 'Delhi', ), OR, status, =, 'Active', END
    ASSERT_EQ(tokens.size(), 15);
    EXPECT_EQ(tokens[0].type, TokenType::WHERE);
    EXPECT_EQ(tokens[1].type, TokenType::LPAREN);
    EXPECT_EQ(tokens[5].type, TokenType::AND);
    EXPECT_EQ(tokens[9].type, TokenType::RPAREN);
    EXPECT_EQ(tokens[10].type, TokenType::OR);
}

TEST(LexerTest, CreateTableStatement) {
    Lexer lexer("CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT)");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens[0].type, TokenType::CREATE);
    ASSERT_EQ(tokens[1].type, TokenType::TABLE);
    ASSERT_EQ(tokens[2].type, TokenType::IDENT);
    EXPECT_EQ(tokens[2].value, "students");
    EXPECT_EQ(tokens[6].type, TokenType::PRIMARY);
    EXPECT_EQ(tokens[7].type, TokenType::KEY);
    EXPECT_EQ(tokens.back().type, TokenType::END);
}

TEST(LexerTest, InsertUpdateDeleteKeywords) {
    {
        Lexer lexer("INSERT INTO students (id, name) VALUES (1, 'Alice')");
        auto tokens = lexer.tokenize();
        ASSERT_EQ(tokens[0].type, TokenType::INSERT);
        ASSERT_EQ(tokens[1].type, TokenType::INTO);
        ASSERT_EQ(tokens[8].type, TokenType::VALUES);
        EXPECT_EQ(tokens.back().type, TokenType::END);
    }

    {
        Lexer lexer("UPDATE students SET age = 21 WHERE id = 1");
        auto tokens = lexer.tokenize();
        ASSERT_EQ(tokens[0].type, TokenType::UPDATE);
        ASSERT_EQ(tokens[2].type, TokenType::SET);
        ASSERT_EQ(tokens[6].type, TokenType::WHERE);
        EXPECT_EQ(tokens.back().type, TokenType::END);
    }

    {
        Lexer lexer("DELETE FROM students WHERE id = 1");
        auto tokens = lexer.tokenize();
        ASSERT_EQ(tokens[0].type, TokenType::DELETE_);
        ASSERT_EQ(tokens[1].type, TokenType::FROM);
        ASSERT_EQ(tokens[3].type, TokenType::WHERE);
        EXPECT_EQ(tokens.back().type, TokenType::END);
    }
}

TEST(LexerTest, DistinctKeyword) {
    Lexer lexer("SELECT DISTINCT s.name FROM Student s");
    auto tokens = lexer.tokenize();

    ASSERT_GE(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::DISTINCT);
    EXPECT_EQ(tokens[2].type, TokenType::IDENT);
    EXPECT_EQ(tokens[2].value, "s");
    EXPECT_EQ(tokens[3].type, TokenType::DOT);
    EXPECT_EQ(tokens[4].type, TokenType::IDENT);
    EXPECT_EQ(tokens[4].value, "name");
}

TEST(LexerTest, OrderByDirectionKeywords) {
    Lexer lexer("SELECT * FROM users ORDER BY age DESC");
    auto tokens = lexer.tokenize();

    ASSERT_GE(tokens.size(), 9u);
    EXPECT_EQ(tokens[4].type, TokenType::ORDER);
    EXPECT_EQ(tokens[5].type, TokenType::BY);
    EXPECT_EQ(tokens[6].type, TokenType::IDENT);
    EXPECT_EQ(tokens[7].type, TokenType::DESC);
}

TEST(LexerTest, PathSelectKeywords) {
    Lexer lexer("PATH SELECT * FROM users");
    auto tokens = lexer.tokenize();

    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::PATH);
    EXPECT_EQ(tokens[1].type, TokenType::SELECT);
    EXPECT_EQ(tokens[2].type, TokenType::STAR);
    EXPECT_EQ(tokens[3].type, TokenType::FROM);
    EXPECT_EQ(tokens[4].type, TokenType::IDENT);
    EXPECT_EQ(tokens[4].value, "users");
    EXPECT_EQ(tokens[5].type, TokenType::END);
}
