#include "execution/compiled_predicate.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>

namespace
{

    bool isFalseToken(const std::string &raw)
    {
        std::string v;
        v.reserve(raw.size());
        for (char c : raw)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
            {
                v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        return v.empty() || v == "0" || v == "false" || v == "f";
    }

}

CompiledPredicate CompiledPredicate::compile(const Expr *expr)
{
    CompiledPredicate out;
    if (expr != nullptr)
    {
        out.root_ = out.compileNode(expr);
    }
    return out;
}

int CompiledPredicate::compileNode(const Expr *expr)
{
    if (expr == nullptr)
    {
        throw std::runtime_error("CompiledPredicate: cannot compile null expression node");
    }

    if (const auto *col = dynamic_cast<const Column *>(expr))
    {
        Node n;
        n.tag = Tag::COLUMN;
        n.text = col->name;
        nodes_.push_back(std::move(n));
        return static_cast<int>(nodes_.size() - 1);
    }

    if (const auto *lit = dynamic_cast<const Literal *>(expr))
    {
        Node n;
        std::int64_t parsed = 0;
        if (parseInt64(lit->value, parsed))
        {
            n.tag = Tag::LITERAL_INT;
            n.intValue = parsed;
        }
        else
        {
            n.tag = Tag::LITERAL;
            n.text = lit->value;
        }
        nodes_.push_back(std::move(n));
        return static_cast<int>(nodes_.size() - 1);
    }

    if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
    {
        Node n;
        n.tag = Tag::BINARY;
        n.text = bin->op;
        n.left = compileNode(bin->left.get());
        n.right = compileNode(bin->right.get());
        nodes_.push_back(std::move(n));
        return static_cast<int>(nodes_.size() - 1);
    }

    if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
    {
        Node n;
        n.tag = Tag::IN_LIST;
        n.left = compileNode(inList->value.get());
        n.list.reserve(inList->list.size());
        for (const auto &item : inList->list)
        {
            n.list.push_back(compileNode(item.get()));
        }
        nodes_.push_back(std::move(n));
        return static_cast<int>(nodes_.size() - 1);
    }

    throw std::runtime_error("CompiledPredicate: unsupported expression type for compilation");
}

bool CompiledPredicate::evaluatePredicate(const Row &row) const
{
    if (root_ < 0)
    {
        return true;
    }
    return evalPredicateNode(root_, row);
}

std::string CompiledPredicate::evaluateScalar(const Row &row) const
{
    if (root_ < 0)
    {
        throw std::runtime_error("CompiledPredicate: cannot scalar-evaluate empty expression");
    }
    return evalScalarNode(root_, row);
}

bool CompiledPredicate::evalPredicateNode(int nodeIndex, const Row &row) const
{
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodes_.size())
    {
        throw std::runtime_error("CompiledPredicate: invalid node index");
    }

    const Node &node = nodes_[static_cast<std::size_t>(nodeIndex)];

    if (node.tag == Tag::IN_LIST)
    {
        const std::string lhs = evalScalarNode(node.left, row);
        for (int itemIndex : node.list)
        {
            if (lhs == evalScalarNode(itemIndex, row))
            {
                return true;
            }
        }
        return false;
    }

    if (node.tag == Tag::BINARY)
    {
        if (node.text == "NOT")
        {
            return !evalPredicateNode(node.left, row);
        }
        if (node.text == "AND")
        {
            return evalPredicateNode(node.left, row) && evalPredicateNode(node.right, row);
        }
        if (node.text == "OR")
        {
            return evalPredicateNode(node.left, row) || evalPredicateNode(node.right, row);
        }

        const std::string lhs = evalScalarNode(node.left, row);
        const std::string rhs = evalScalarNode(node.right, row);
        return compareValues(lhs, node.text, rhs);
    }

    return truthy(evalScalarNode(nodeIndex, row));
}

std::string CompiledPredicate::evalScalarNode(int nodeIndex, const Row &row) const
{
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= nodes_.size())
    {
        throw std::runtime_error("CompiledPredicate: invalid node index");
    }

    const Node &node = nodes_[static_cast<std::size_t>(nodeIndex)];
    switch (node.tag)
    {
    case Tag::COLUMN:
    {
        const auto it = row.find(node.text);
        if (it != row.end())
        {
            return it->second;
        }

        const auto dot = node.text.rfind('.');
        if (dot != std::string::npos && dot + 1 < node.text.size())
        {
            const std::string bare = node.text.substr(dot + 1);
            const auto bareIt = row.find(bare);
            if (bareIt != row.end())
            {
                return bareIt->second;
            }
        }

        throw std::runtime_error("Unknown column referenced: '" + node.text + "'");
    }
    case Tag::LITERAL:
        return node.text;
    case Tag::LITERAL_INT:
        return std::to_string(node.intValue);
    case Tag::BINARY:
    case Tag::IN_LIST:
        return evalPredicateNode(nodeIndex, row) ? "1" : "0";
    }

    throw std::runtime_error("CompiledPredicate: invalid node tag");
}

bool CompiledPredicate::compareValues(const std::string &left, const std::string &op, const std::string &right)
{
    int cmp = 0;

    std::int64_t li = 0;
    std::int64_t ri = 0;
    if (parseInt64(left, li) && parseInt64(right, ri))
    {
        cmp = (li < ri) ? -1 : (li > ri) ? 1 : 0;
    }
    else
    {
        long double ln = 0;
        long double rn = 0;
        if (parseNumber(left, ln) && parseNumber(right, rn))
        {
            cmp = (ln < rn) ? -1 : (ln > rn) ? 1 : 0;
        }
        else
        {
            const int c = left.compare(right);
            cmp = (c < 0) ? -1 : (c > 0) ? 1 : 0;
        }
    }

    if (op == "=" || op == "==")
    {
        return cmp == 0;
    }
    if (op == "!=" || op == "<>")
    {
        return cmp != 0;
    }
    if (op == ">")
    {
        return cmp > 0;
    }
    if (op == "<")
    {
        return cmp < 0;
    }
    if (op == ">=")
    {
        return cmp >= 0;
    }
    if (op == "<=")
    {
        return cmp <= 0;
    }

    throw std::runtime_error("CompiledPredicate: unsupported comparison operator: " + op);
}

bool CompiledPredicate::parseInt64(const std::string &s, std::int64_t &out) noexcept
{
    if (s.empty())
    {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const long long n = std::strtoll(s.c_str(), &end, 10);
    if (s.c_str() == end || *end != '\0' || errno == ERANGE)
    {
        return false;
    }
    out = static_cast<std::int64_t>(n);
    return true;
}

bool CompiledPredicate::parseNumber(const std::string &s, long double &out) noexcept
{
    if (s.empty())
    {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const long double n = std::strtold(s.c_str(), &end);
    if (s.c_str() == end || *end != '\0' || errno == ERANGE)
    {
        return false;
    }
    out = n;
    return true;
}

bool CompiledPredicate::truthy(const std::string &v)
{
    return !isFalseToken(v);
}

bool exprContainsSubquery(const Expr *expr)
{
    if (expr == nullptr)
    {
        return false;
    }

    if (dynamic_cast<const ExistsExpr *>(expr) != nullptr ||
        dynamic_cast<const InExpr *>(expr) != nullptr)
    {
        return true;
    }

    if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
    {
        return exprContainsSubquery(bin->left.get()) || exprContainsSubquery(bin->right.get());
    }

    if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
    {
        if (exprContainsSubquery(inList->value.get()))
        {
            return true;
        }
        for (const auto &item : inList->list)
        {
            if (exprContainsSubquery(item.get()))
            {
                return true;
            }
        }
    }

    return false;
}
