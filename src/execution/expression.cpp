#include "execution/expression.hpp"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>

// ExpressionEvaluator
std::string ExpressionEvaluator::eval(const Expr* expr, const Row& row) {
    if (expr == nullptr) {
        throw std::runtime_error("Cannot evaluate a null expression");
    }
    return evalScalar(expr, row);
}

bool ExpressionEvaluator::evalPredicate(const Expr* expr, const Row& row) {
    if (expr == nullptr) return true;

    if (const auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        if (bin->op == "AND") {
            // Short-circuit evaluation
            return evalPredicate(bin->left.get(), row) && evalPredicate(bin->right.get(), row);
        }
        if (bin->op == "OR") {
            return evalPredicate(bin->left.get(), row) || evalPredicate(bin->right.get(), row);
        }
        // Comparison operator: evaluate both sides as scalars then compare.
        const std::string lhs = evalScalar(bin->left.get(), row);
        const std::string rhs = evalScalar(bin->right.get(), row);
        return compareValues(lhs, bin->op, rhs);
    }

    // Scalar truth: non-empty and not "0"
    const std::string v = evalScalar(expr, row);
    return !v.empty() && v != "0";
}


std::string ExpressionEvaluator::evalScalar(const Expr* expr, const Row& row) {
    if (const auto* col = dynamic_cast<const Column*>(expr)) {
        const auto it = row.find(col->name);
        if (it == row.end()) {
            throw std::runtime_error("Unknown column referenced: '" + col->name + "'");
        }
        return it->second;
    }
    if (const auto* lit = dynamic_cast<const Literal*>(expr)) {
        return lit->value;
    }
    if (const auto* bin = dynamic_cast<const BinaryExpr*>(expr)) {
        // A BinaryExpr used as a scalar returns "1" or "0".
        return evalPredicate(bin, row) ? "1" : "0";
    }
    throw std::runtime_error("Unsupported expression type in scalar evaluation");
}

bool ExpressionEvaluator::tryParseInt64(const std::string& s, std::int64_t &out) noexcept {
    if (s.empty()) return false;
    errno = 0;
    const char* begin = s.c_str();
    char *end = nullptr;
    const long long parsed = std::strtoll(begin, &end, 10);
    if (begin == end || *end != '\0' || errno == ERANGE) return false;
    if (parsed < std::numeric_limits<std::int64_t>::min() || parsed > std::numeric_limits<std::int64_t>::max()) return false;
    out = static_cast<std::int64_t>(parsed);
    return true;
}

bool ExpressionEvaluator::compareValues(const std::string& left, const std::string& op, const std::string& right) {
    // Try numeric comparison first; fall back to lexicographic.
    int cmp = 0;
    std::int64_t li = 0, ri = 0;
    if (tryParseInt64(left, li) && tryParseInt64(right, ri)) {
        cmp = (li < ri) ? -1 : (li > ri) ? 1 : 0;
    } 
    else {
        const int c = left.compare(right);
        cmp = (c < 0) ? -1 : (c > 0) ? 1 : 0;
    }

    if (op == "=" || op == "==") return cmp == 0;
    if (op == "!=" || op == "<>") return cmp != 0;
    if (op == ">") return cmp >  0;
    if (op == "<") return cmp <  0;
    if (op == ">=") return cmp >= 0;
    if (op == "<=") return cmp <= 0;

    throw std::runtime_error("Unsupported comparison operator: " + op);
}