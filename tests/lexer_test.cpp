#include <gtest/gtest.h>
#include "parser/lexer.h"
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

TEST(LexerTest, AggregateFunctions) {
    Lexer lexer("SELECT SUM(age), COUNT(*) FROM users");
    auto tokens = lexer.tokenize();

    // Expected: SELECT, SUM, LPAREN, IDENT(age), RPAREN, COMMA, COUNT, LPAREN, STAR, RPAREN, FROM, IDENT(users), END
    ASSERT_EQ(tokens.size(), 13);
    EXPECT_EQ(tokens[1].type, TokenType::SUM);
    EXPECT_EQ(tokens[2].type, TokenType::LPAREN);
    EXPECT_EQ(tokens[6].type, TokenType::COUNT);
    EXPECT_EQ(tokens[8].type, TokenType::STAR);
    EXPECT_EQ(tokens[9].type, TokenType::RPAREN);
}
