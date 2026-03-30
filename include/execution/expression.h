#pragma once
#include "parser/ast.h"
#include "storage/table.h"

#include <cstdint>
#include <string>

// Evaluates scalar expressions and WHERE predicates against a row.
class ExpressionEvaluator {
public:
    static std::string eval(const Expr* expr, const Row& row);
    static bool evalPredicate(const Expr* expr, const Row& row);

private:
    static std::string EvalScalar(const Expr* expr, const Row& row);
    static bool TryParseInt64(const std::string& value, int64_t& out);
    static bool CompareValues(const std::string& left, const std::string& op, const std::string& right);
};