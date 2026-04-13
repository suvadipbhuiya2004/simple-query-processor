#pragma once
#include "parser/ast.hpp"
#include "storage/table.hpp"
#include <cstdint>
#include <string>

// Evaluates scalar expressions and boolean predicates against a single Row
class ExpressionEvaluator {
private:
    static std::string evalScalar(const Expr* expr, const Row& row);
    static bool compareValues(const std::string& left, const std::string& op, const std::string& right);
    static bool tryParseInt64(const std::string& s, std::int64_t& out) noexcept;
    
public:
    // Evaluate expr as a scalar string value
    static std::string eval(const Expr* expr, const Row& row);

    // Evaluate expr as a boolean predicate.
    static bool evalPredicate(const Expr* expr, const Row& row);
};