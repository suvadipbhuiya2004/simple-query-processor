#include "parser/lexer.h"

#include <cctype>
#include <stdexcept>
#include <string>

// Lexer implementation for a small SQL subset.

namespace {
bool IsAlpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool IsDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool IsAlphaNumeric(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool IsWhitespace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string ToUpper(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}
}  // namespace

Lexer::Lexer(const std::string& input) : input(input), pos(0) {}

char Lexer::peek() const {
    return pos < input.size() ? input[pos] : '\0';
}

char Lexer::advance() {
    return pos < input.size() ? input[pos++] : '\0';
}

void Lexer::skipWhitespace() {
    while (IsWhitespace(peek())) {
        advance();
    }
}

Token Lexer::identifier() {
    std::string value;
    while (IsAlphaNumeric(peek()) || peek() == '_') {
        value += advance();
    }

    const std::string upper = ToUpper(value);

    if (upper == "SELECT") return {TokenType::SELECT, upper};
    if (upper == "FROM") return {TokenType::FROM, upper};
    if (upper == "WHERE") return {TokenType::WHERE, upper};
    if (upper == "ORDER") return {TokenType::ORDER, upper};
    if (upper == "BY") return {TokenType::BY, upper};
    if (upper == "LIMIT") return {TokenType::LIMIT, upper};
    if (upper == "GROUP") return {TokenType::GROUP, upper};
    if (upper == "BY") return {TokenType::BY, upper};
    if (upper == "HAVING") return {TokenType::HAVING, upper};
    if (upper == "SUM") return Token{TokenType::SUM, upper};
    if (upper == "AVG") return Token{TokenType::AVG, upper};
    if (upper == "COUNT") return Token{TokenType::COUNT, upper};
    if (upper == "MIN") return Token{TokenType::MIN, upper};
    if (upper == "MAX") return Token{TokenType::MAX, upper};
    if (upper == "AND") return {TokenType::AND, upper};
    if (upper == "OR") return {TokenType::OR, upper};

    return {TokenType::IDENT, value};
}

Token Lexer::number() {
    std::string value;
    while (IsDigit(peek())) {
        value += advance();
    }
    return {TokenType::NUMBER, value};
}

Token Lexer::string() {
    // Consume opening quote.
    advance();

    std::string value;

    while (true) {
        const char c = peek();

        if (c == '\0') {
            throw std::runtime_error("Unterminated string literal at position " + std::to_string(pos));
        }

        if (c == '\'') {
            advance();

            // SQL escaping for a single quote inside string literal: ''
            if (peek() == '\'') {
                value += '\'';
                advance();
                continue;
            }

            break;
        }

        value += advance();
    }

    return {TokenType::STRING, value};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve((input.size() / 2U) + 1U);

    while (true) {
        skipWhitespace();

        const char c = peek();
        if (c == '\0') {
            break;
        }

        if (IsAlpha(c) || c == '_') {
            tokens.push_back(identifier());
        } else if (IsDigit(c)) {
            tokens.push_back(number());
        } else if (c == '\'') {
            tokens.push_back(string());
        } else if (c == ',') {
            advance();
            tokens.push_back({TokenType::COMMA, ","});
        } else if (c == '*') {
            advance();
            tokens.push_back({TokenType::STAR, "*"});
        } else if (c == '(') {
            advance();
            tokens.push_back({TokenType::LPAREN, "("});
        } else if (c == ')') {
            advance();
            tokens.push_back({TokenType::RPAREN, ")"});
        } else if (c == ';') {
            // Optional statement terminator.
            advance();
        } else if (c == '>' || c == '<' || c == '=' || c == '!') {
            // Support operators like >, <, =, >=, <=, != and <>.
            std::string op(1, advance());

            if (peek() == '=') {
                op += advance();
            } else if (op == "<" && peek() == '>') {
                op += advance();
            }

            if (op == "!") {
                throw std::runtime_error("Unexpected operator '!' at position " + std::to_string(pos - 1U));
            }

            tokens.push_back({TokenType::OP, op});
        } else {
            throw std::runtime_error(
                "Unexpected character '" + std::string(1, c) + "' at position " + std::to_string(pos));
        }
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}