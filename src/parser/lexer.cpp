#include "parser/lexer.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

// namespace
namespace
{

    struct KwEntry
    {
        const char *word;
        TokenType type;
    };

    constexpr KwEntry kKeywords[] = {
        {"ADD", TokenType::ADD},
        {"ALTER", TokenType::ALTER},
        {"ASC", TokenType::ASC},
        {"AS", TokenType::AS},
        {"AND", TokenType::AND},
        {"BY", TokenType::BY},
        {"CHECK", TokenType::CHECK},
        {"COLUMN", TokenType::COLUMN},
        {"CONSTRAINT", TokenType::CONSTRAINT},
        {"CROSS", TokenType::CROSS},
        {"CREATE", TokenType::CREATE},
        {"DELETE", TokenType::DELETE_},
        {"DESC", TokenType::DESC},
        {"DISTINCT", TokenType::DISTINCT},
        {"DROP", TokenType::DROP},
        {"FOREIGN", TokenType::FOREIGN},
        {"EXISTS", TokenType::EXISTS},
        {"FROM", TokenType::FROM},
        {"FULL", TokenType::FULL},
        {"GROUP", TokenType::GROUP},
        {"HAVING", TokenType::HAVING},
        {"INNER", TokenType::INNER},
        {"IN", TokenType::IN},
        {"INSERT", TokenType::INSERT},
        {"INTO", TokenType::INTO},
        {"JOIN", TokenType::JOIN},
        {"KEY", TokenType::KEY},
        {"LEFT", TokenType::LEFT},
        {"LIMIT", TokenType::LIMIT},
        {"NOT", TokenType::NOT},
        {"ON", TokenType::ON},
        {"OR", TokenType::OR},
        {"ORDER", TokenType::ORDER},
        {"OUTER", TokenType::OUTER},
        {"PATH", TokenType::PATH},
        {"PRIMARY", TokenType::PRIMARY},
        {"RENAME", TokenType::RENAME},
        {"REFERENCES", TokenType::REFERENCES},
        {"RIGHT", TokenType::RIGHT},
        {"SELECT", TokenType::SELECT},
        {"SET", TokenType::SET},
        {"TABLE", TokenType::TABLE},
        {"TO", TokenType::TO},
        {"TYPE", TokenType::TYPE},
        {"UPDATE", TokenType::UPDATE},
        {"VALUES", TokenType::VALUES},
        {"WHERE", TokenType::WHERE},
    };

    // Case-insensitive uppercase conversion
    inline char toUpper(char c) noexcept
    {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    std::string toUpperStr(std::string_view sv)
    {
        std::string out;
        out.reserve(sv.size());
        for (char c : sv)
        {
            out += toUpper(c);
        }
        return out;
    }

    TokenType lookupKeyword(const std::string &upper) noexcept
    {
        for (const auto &entry : kKeywords)
        {
            if (upper == entry.word)
            {
                return entry.type;
            }
        }
        return TokenType::IDENT;
    }

} // namespace

// Lexer
Lexer::Lexer(std::string input) : input_(std::move(input)) {}

char Lexer::peek() const noexcept
{
    return (pos_ < input_.size()) ? input_[pos_] : '\0';
}

char Lexer::advance() noexcept
{
    if (pos_ >= input_.size())
        return '\0';
    const char c = input_[pos_++];
    if (c == '\n')
    {
        ++line_;
        column_ = 1;
    }
    else
    {
        ++column_;
    }
    return c;
}

void Lexer::skipWhitespace() noexcept
{
    while (std::isspace(static_cast<unsigned char>(peek())))
    {
        advance();
    }
}

Token Lexer::scanIdentifierOrKeyword()
{
    const std::size_t startLine = line_;
    const std::size_t startCol = column_;
    const std::size_t start = pos_;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
    {
        advance();
    }
    std::string raw = input_.substr(start, pos_ - start);
    std::string upper = toUpperStr(raw);
    const TokenType kw = lookupKeyword(upper);
    if (kw != TokenType::IDENT)
    {
        return {kw, std::move(upper), startLine, startCol};
    }
    return {TokenType::IDENT, std::move(raw), startLine, startCol};
}

Token Lexer::scanNumber()
{
    const std::size_t startLine = line_;
    const std::size_t startCol = column_;
    const std::size_t start = pos_;

    // Optional sign for numeric literals: -1, +2, -3.14
    if (peek() == '+' || peek() == '-')
    {
        advance();
    }

    while (std::isdigit(static_cast<unsigned char>(peek())))
    {
        advance();
    }
    // Basic decimal support: consume one '.' followed by digits
    if (peek() == '.' && pos_ + 1 < input_.size() &&
        std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])))
    {
        advance(); // consume '.'
        while (std::isdigit(static_cast<unsigned char>(peek())))
        {
            advance();
        }
    }
    return {TokenType::NUMBER, input_.substr(start, pos_ - start), startLine, startCol};
}

Token Lexer::scanString()
{
    const std::size_t startLine = line_;
    const std::size_t startCol = column_;
    advance(); // consume opening '
    std::string value;
    value.reserve(32);
    while (true)
    {
        const char c = peek();
        if (c == '\0')
        {
            throw std::runtime_error("Unterminated string literal at line " + std::to_string(startLine) + ", column " + std::to_string(startCol));
        }
        if (c == '\'')
        {
            advance();
            if (peek() == '\'')
            {
                // SQL-style escaped single quote: ''
                value += '\'';
                advance();
                continue;
            }
            break;
        }
        value += advance();
    }
    return {TokenType::STRING, std::move(value), startLine, startCol};
}

Token Lexer::scanOperator()
{
    const std::size_t startLine = line_;
    const std::size_t startCol = column_;
    // Handles: =, !=, <>, <, >, <=, >=
    std::string op(1, advance());
    const char nx = peek();

    if (nx == '=')
    {
        op += advance(); // >=  <=  !=  ==
    }
    else if (op == "<" && nx == '>')
    {
        op += advance(); // <>
    }

    if (op == "!")
    {
        throw std::runtime_error("Unexpected '!' at line " + std::to_string(startLine) + ", column " + std::to_string(startCol) + ". Did you mean '!='?");
    }
    return {TokenType::OP, std::move(op), startLine, startCol};
}

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;
    tokens.reserve(input_.size() / 4 + 8);

    while (true)
    {
        skipWhitespace();
        const char c = peek();
        if (c == '\0')
            break;

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        {
            tokens.push_back(scanIdentifierOrKeyword());
        }
        else if (std::isdigit(static_cast<unsigned char>(c)) || ((c == '+' || c == '-') && pos_ + 1 < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_ + 1]))))
        {
            tokens.push_back(scanNumber());
        }
        else if (c == '\'')
        {
            tokens.push_back(scanString());
        }
        else
        {
            switch (c)
            {
            case ',':
            {
                const std::size_t tokLine = line_;
                const std::size_t tokCol = column_;
                advance();
                tokens.push_back({TokenType::COMMA, ",", tokLine, tokCol});
                break;
            }
            case '*':
            {
                const std::size_t tokLine = line_;
                const std::size_t tokCol = column_;
                advance();
                tokens.push_back({TokenType::STAR, "*", tokLine, tokCol});
                break;
            }
            case '(':
            {
                const std::size_t tokLine = line_;
                const std::size_t tokCol = column_;
                advance();
                tokens.push_back({TokenType::LPAREN, "(", tokLine, tokCol});
                break;
            }
            case ')':
            {
                const std::size_t tokLine = line_;
                const std::size_t tokCol = column_;
                advance();
                tokens.push_back({TokenType::RPAREN, ")", tokLine, tokCol});
                break;
            }
            case '.':
            {
                const std::size_t tokLine = line_;
                const std::size_t tokCol = column_;
                advance();
                tokens.push_back({TokenType::DOT, ".", tokLine, tokCol});
                break;
            }
            case ';':
                advance(); // statement terminator — skip
                break;
            case '=':
            case '!':
            case '<':
            case '>':
                tokens.push_back(scanOperator());
                break;
            default:
                throw std::runtime_error(std::string("Unexpected character '") + c + "' at line " + std::to_string(line_) + ", column " + std::to_string(column_));
            }
        }
    }

    tokens.push_back({TokenType::END, {}, line_, column_});
    return tokens;
}