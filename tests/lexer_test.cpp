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
