#include "execution/expression.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>

// Evaluates expressions used in filter predicates and projections.

std::string ExpressionEvaluator::eval(const Expr* expr, const Row& row) {
    return EvalScalar(expr, row);
}

bool ExpressionEvaluator::evalPredicate(const Expr* expr, const Row& row) {
    if (expr == nullptr) {
        return true;
    }

    if (auto bin = dynamic_cast<const BinaryExpr*>(expr)) {
        const auto left = EvalScalar(bin->left.get(), row);
        const auto right = EvalScalar(bin->right.get(), row);
        return CompareValues(left, bin->op, right);
    }

    const std::string value = EvalScalar(expr, row);
    return !value.empty() && value != "0";
}

std::string ExpressionEvaluator::EvalScalar(const Expr* expr, const Row& row) {
    if (expr == nullptr) {
        throw std::runtime_error("Cannot evaluate a null expression");
    }

    if (auto col = dynamic_cast<const Column*>(expr)) {
        const auto it = row.find(col->name);
        if (it == row.end()) {
            throw std::runtime_error("Unknown column in row: " + col->name);
        }
        return it->second;
    }

    if (auto lit = dynamic_cast<const Literal*>(expr)) {
        return lit->value;
    }

    if (auto bin = dynamic_cast<const BinaryExpr*>(expr)) {
        return evalPredicate(bin, row) ? "1" : "0";
    }

    throw std::runtime_error("Unsupported expression type");
}

bool ExpressionEvaluator::TryParseInt64(const std::string& value, int64_t& out) {
    if (value.empty()) {
        return false;
    }

    errno = 0;
    const char* begin = value.c_str();
    char* end = nullptr;
    const long long parsed = std::strtoll(begin, &end, 10);

    if (begin == end || *end != '\0' || errno == ERANGE) {
        return false;
    }

    if (parsed < std::numeric_limits<int64_t>::min() ||
        parsed > std::numeric_limits<int64_t>::max()) {
        return false;
    }

    out = static_cast<int64_t>(parsed);

    return true;
}

bool ExpressionEvaluator::CompareValues(const std::string& left, const std::string& op, const std::string& right) {
    int comparison = 0;
    int64_t leftInt = 0;
    int64_t rightInt = 0;

    // Try numeric compare first; fall back to lexical compare for non-numeric values.
    if (TryParseInt64(left, leftInt) && TryParseInt64(right, rightInt)) {
        if (leftInt < rightInt) {
            comparison = -1;
        } else if (leftInt > rightInt) {
            comparison = 1;
        }
    } else {
        const int cmp = left.compare(right);
        if (cmp < 0) {
            comparison = -1;
        } else if (cmp > 0) {
            comparison = 1;
        }
    }

    if (op == "=" || op == "==") {
        return comparison == 0;
    }
    if (op == "!=" || op == "<>") {
        return comparison != 0;
    }
    if (op == ">") {
        return comparison > 0;
    }
    if (op == "<") {
        return comparison < 0;
    }
    if (op == ">=") {
        return comparison >= 0;
    }
    if (op == "<=") {
        return comparison <= 0;
    }

    throw std::runtime_error("Unsupported operator in predicate: " + op);
}
