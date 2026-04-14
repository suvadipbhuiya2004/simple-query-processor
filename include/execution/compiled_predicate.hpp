#pragma once

#include "parser/ast.hpp"
#include "storage/table.hpp"

#include <cstdint>
#include <string>
#include <vector>

class CompiledPredicate {
  public:
    CompiledPredicate() = default;

    static CompiledPredicate compile(const Expr *expr);

    // Null expression means TRUE.
    bool evaluatePredicate(const Row &row) const;

    // Scalar evaluation is used by some operators (comparisons / IN lists).
    std::string evaluateScalar(const Row &row) const;

    bool empty() const noexcept {
        return root_ < 0;
    }

  private:
    enum class Tag : std::uint8_t {
        COLUMN,
        LITERAL,
        LITERAL_INT,
        BINARY,
        IN_LIST,
    };

    struct Node {
        Tag tag{Tag::LITERAL};
        std::string text;
        std::int64_t intValue{0};
        int left{-1};
        int right{-1};
        std::vector<int> list;
    };

    std::vector<Node> nodes_;
    int root_{-1};

    int compileNode(const Expr *expr);

    bool evalPredicateNode(int nodeIndex, const Row &row) const;
    std::string evalScalarNode(int nodeIndex, const Row &row) const;

    static bool compareValues(const std::string &left, const std::string &op,
                              const std::string &right);
    static bool parseInt64(const std::string &s, std::int64_t &out) noexcept;
    static bool parseNumber(const std::string &s, long double &out) noexcept;
    static bool truthy(const std::string &v);
};

bool exprContainsSubquery(const Expr *expr);
