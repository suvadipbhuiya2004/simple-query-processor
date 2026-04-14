#include "app/query_engine.hpp"
#include "common/ansi.hpp"
#include "common/data_type.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "execution/executor_builder.hpp"
#include "execution/expression.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "planner/plan.hpp"
#include "storage/csv_loader.hpp"

namespace fs = std::filesystem;

namespace
{

    constexpr const char *kDataDir = "data";

    // ANSI colour aliases
    constexpr const char *kRed = ansi::kRed;
    constexpr const char *kGreen = ansi::kGreen;
    constexpr const char *kYellow = ansi::kYellow;
    constexpr const char *kBlue = ansi::kBlue;
    constexpr const char *kGray = ansi::kGray;
    constexpr const char *kOrange = ansi::kOrange;

    std::string colorize(std::string text, const char *code)
    {
        return ansi::colorize(std::move(text), code);
    }

    std::string toUpper(std::string s)
    {
        for (char &c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    std::string toLower(std::string s)
    {
        for (char &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string trim(std::string s)
    {
        const auto isWs = [](unsigned char c)
        { return std::isspace(c) != 0; };
        const auto b = std::find_if_not(s.begin(), s.end(), isWs);
        const auto e = std::find_if_not(s.rbegin(), s.rend(), isWs).base();
        if (b >= e)
            return {};
        return std::string(b, e);
    }

    // Returns the part after the last '.', or the full string if no '.' found.
    std::string stripQualifier(const std::string &col)
    {
        const auto pos = col.rfind('.');
        return (pos == std::string::npos) ? col : col.substr(pos + 1);
    }

    // Type-system helpers

    std::string sqlQuoteLiteral(const std::string &v)
    {
        std::string out;
        out.reserve(v.size() + 2);
        out.push_back('\'');
        for (const char c : v)
        {
            if (c == '\'')
                out += "''";
            else
                out.push_back(c);
        }
        out.push_back('\'');
        return out;
    }

    std::string buildEnumCompactList(const std::vector<std::string> &enumValues)
    {
        std::string out;
        for (std::size_t i = 0; i < enumValues.size(); ++i)
        {
            if (i)
                out += ", ";
            out += sqlQuoteLiteral(enumValues[i]);
        }
        return out;
    }

    std::string buildEnumPredicate(const std::string &columnName, const std::vector<std::string> &enumValues)
    {
        return columnName + " IN (" + buildEnumCompactList(enumValues) + ")";
    }

    bool containsCaseInsensitive(const std::string &haystack, const std::string &needle)
    {
        return toUpper(haystack).find(toUpper(needle)) != std::string::npos;
    }

    // Value predicates  (regex compiled once at program start)

    bool isIntegerValue(const std::string &v)
    {
        static const std::regex kInt{R"(^[+-]?\d+$)"};
        return std::regex_match(v, kInt);
    }
    bool isNumericValue(const std::string &v)
    {
        static const std::regex kNum{R"(^[+-]?(\d+(\.\d+)?|\.\d+)$)"};
        return std::regex_match(v, kNum);
    }

    // Integer-aware scalar comparison: returns -1 / 0 / 1.
    int compareScalar(const std::string &l, const std::string &r)
    {
        if (isNumericValue(l) && isNumericValue(r))
        {
            const long double lv = std::strtold(l.c_str(), nullptr);
            const long double rv = std::strtold(r.c_str(), nullptr);
            return (lv < rv) ? -1 : (lv > rv) ? 1
                                              : 0;
        }
        const int c = l.compare(r);
        return (c < 0) ? -1 : (c > 0) ? 1
                                      : 0;
    }

    // CHECK expression evaluator

    bool tokenIsWord(const Token &tok, const char *word)
    {
        const std::string w = toUpper(word);
        if (tok.type == TokenType::AND)
            return w == "AND";
        if (tok.type == TokenType::OR)
            return w == "OR";
        if (tok.type == TokenType::NOT)
            return w == "NOT";
        if (tok.type == TokenType::IN)
            return w == "IN";
        if (tok.type == TokenType::EXISTS)
            return w == "EXISTS";
        if (tok.type == TokenType::IDENT)
            return toUpper(tok.value) == w;
        return false;
    }

    class CheckExprParser
    {
    public:
        CheckExprParser(std::vector<Token> tokens, const Row &row)
            : toks_(std::move(tokens)), row_(row) {}

        bool parse()
        {
            const bool v = parseOr();
            if (peek().type != TokenType::END)
                throw std::runtime_error("Invalid CHECK near: '" + peek().value + "'");
            return v;
        }

    private:
        std::vector<Token> toks_;
        std::size_t pos_{0};
        const Row &row_;

        const Token &peek() const noexcept
        {
            return toks_[pos_];
        }
        const Token &eat()
        {
            return toks_[pos_++];
        }

        bool matchWord(const char *word)
        {
            if (tokenIsWord(peek(), word))
            {
                ++pos_;
                return true;
            }
            return false;
        }
        void expectWord(const char *word)
        {
            if (!matchWord(word))
                throw std::runtime_error(std::string("Expected '") + word + "' in CHECK");
        }

        std::string parseValue()
        {
            const Token &t = peek();
            if (t.type == TokenType::NUMBER || t.type == TokenType::STRING)
            {
                ++pos_;
                return t.value;
            }
            if (t.type == TokenType::IDENT)
            {
                ++pos_;
                const auto it = row_.find(t.value);
                return (it != row_.end()) ? it->second : t.value;
            }
            throw std::runtime_error("Invalid CHECK value: '" + t.value + "'");
        }

        bool parsePrimary()
        {
            if (peek().type == TokenType::LPAREN)
            {
                ++pos_;
                const bool v = parseOr();
                if (peek().type != TokenType::RPAREN)
                    throw std::runtime_error("Missing ')' in CHECK");
                ++pos_;
                return v;
            }
            if (peek().type != TokenType::IDENT)
                throw std::runtime_error("CHECK must start with a column name");

            const std::string colName = eat().value;
            const auto colIt = row_.find(colName);
            if (colIt == row_.end())
                throw std::runtime_error("CHECK references unknown column: " + colName);
            const std::string &lhs = colIt->second;

            if (matchWord("BETWEEN"))
            {
                const std::string lo = parseValue();
                expectWord("AND");
                const std::string hi = parseValue();
                return compareScalar(lhs, lo) >= 0 && compareScalar(lhs, hi) <= 0;
            }

            const bool negated = matchWord("NOT");
            if (matchWord("IN"))
            {
                if (peek().type != TokenType::LPAREN)
                    throw std::runtime_error("IN must be followed by '('");
                ++pos_;
                bool found = false;
                while (true)
                {
                    if (compareScalar(lhs, parseValue()) == 0)
                        found = true;
                    if (peek().type != TokenType::COMMA)
                        break;
                    ++pos_;
                }
                if (peek().type != TokenType::RPAREN)
                    throw std::runtime_error("Missing ')' after IN list");
                ++pos_;
                return negated ? !found : found;
            }
            if (negated)
                throw std::runtime_error("Expected IN after NOT in CHECK");

            if (peek().type != TokenType::OP)
                throw std::runtime_error("Expected comparison operator in CHECK");
            const std::string op = eat().value;
            const std::string rhs = parseValue();
            const int cmp = compareScalar(lhs, rhs);
            if (op == "=" || op == "==")
                return cmp == 0;
            if (op == "!=" || op == "<>")
                return cmp != 0;
            if (op == ">")
                return cmp > 0;
            if (op == "<")
                return cmp < 0;
            if (op == ">=")
                return cmp >= 0;
            if (op == "<=")
                return cmp <= 0;
            throw std::runtime_error("Unsupported operator in CHECK: " + op);
        }

        bool parseAnd()
        {
            bool v = parsePrimary();
            while (matchWord("AND"))
                v = v && parsePrimary();
            return v;
        }
        bool parseOr()
        {
            bool v = parseAnd();
            while (matchWord("OR"))
                v = v || parseAnd();
            return v;
        }
    };

    bool evalCheckExpr(const std::string &expr, const Row &row)
    {
        Lexer lexer(expr);
        auto toks = lexer.tokenize();
        return CheckExprParser(std::move(toks), row).parse();
    }

    std::string normalizeColumnCheckExpr(const std::string &expr, const std::string &columnName)
    {
        Lexer lexer(expr);
        auto toks = lexer.tokenize();
        if (toks.size() < 2)
            return expr;

        // Support compact metadata form: "'A', 'B'" -> "col IN ('A', 'B')".
        const TokenType t0 = toks[0].type;
        const TokenType t1 = toks[1].type;
        const bool looksLikeList =
            t0 == TokenType::STRING || t0 == TokenType::NUMBER || t0 == TokenType::LPAREN ||
            (t0 == TokenType::IDENT && (t1 == TokenType::COMMA || t1 == TokenType::END));

        if (!looksLikeList)
            return expr;
        return columnName + " IN (" + expr + ")";
    }

    std::string regexEscape(std::string s)
    {
        static const std::regex kMeta{R"([.^$|()\[\]{}*+?\\])"};
        return std::regex_replace(s, kMeta, R"(\\$&)");
    }

    std::string normalizeStoredColumnCheckExpr(const std::string &expr, const std::string &columnName)
    {
        const std::string trimmedExpr = trim(expr);
        const std::regex kInPattern("^\\s*" + regexEscape(columnName) + "\\s+IN\\s*\\((.*)\\)\\s*$", std::regex::icase);
        std::smatch m;
        if (std::regex_match(trimmedExpr, m, kInPattern) && m.size() >= 2)
            return trim(m[1].str());
        return trimmedExpr;
    }

    std::vector<std::string> dedupeChecksPreserveOrder(const std::vector<std::string> &checks)
    {
        std::vector<std::string> deduped;
        deduped.reserve(checks.size());
        std::unordered_set<std::string> seen;
        seen.reserve(checks.size());

        for (const auto &raw : checks)
        {
            const std::string normalized = trim(raw);
            if (normalized.empty())
            {
                continue;
            }
            if (seen.insert(normalized).second)
            {
                deduped.push_back(normalized);
            }
        }
        return deduped;
    }

    bool hasTableCheck(const std::vector<std::string> &checks, const std::string &expr)
    {
        const std::string normalized = trim(expr);
        for (const auto &existing : checks)
        {
            if (trim(existing) == normalized)
            {
                return true;
            }
        }
        return false;
    }

    // Column / catalog helpers

    const ColumnMetadata *findColumn(const TableMetadata &tm, const std::string &name)
    {
        for (const auto &c : tm.columns)
            if (c.name == name)
                return &c;
        return nullptr;
    }

    ColumnMetadata *findMutableColumn(TableMetadata &tm, const std::string &name)
    {
        for (auto &c : tm.columns)
            if (c.name == name)
                return &c;
        return nullptr;
    }

    bool hasColumnName(const TableMetadata &tm, const std::string &name)
    {
        return findColumn(tm, name) != nullptr;
    }

    std::string rewriteIdentifierInExpression(const std::string &expr, const std::string &from,
                                              const std::string &to)
    {
        Lexer lexer(expr);
        const auto toks = lexer.tokenize();

        std::string out;
        bool first = true;
        for (const auto &tok : toks)
        {
            if (tok.type == TokenType::END)
            {
                break;
            }

            std::string piece;
            if (tok.type == TokenType::STRING)
            {
                piece = "'" + tok.value + "'";
            }
            else
            {
                piece = tok.value;
            }

            if (tok.type == TokenType::IDENT && tok.value == from)
            {
                piece = to;
            }

            if (!first)
            {
                out.push_back(' ');
            }
            out += piece;
            first = false;
        }
        return out;
    }

    bool expressionReferencesIdentifier(const std::string &expr, const std::string &ident)
    {
        Lexer lexer(expr);
        const auto toks = lexer.tokenize();
        for (const auto &tok : toks)
        {
            if (tok.type == TokenType::IDENT && tok.value == ident)
            {
                return true;
            }
        }
        return false;
    }

    std::pair<std::string, std::string> parseFkRef(const std::string &ref)
    {
        const auto dot = ref.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= ref.size())
            throw std::runtime_error("Invalid FK format (expected table.column): " + ref);
        return {ref.substr(0, dot), ref.substr(dot + 1)};
    }

    std::optional<std::string> findTableCI(const MetadataCatalog &cat, const std::string &name)
    {
        if (cat.hasTable(name))
            return name;
        const std::string lower = toLower(name);
        std::optional<std::string> match;
        for (const auto &[k, _] : cat.allTables())
        {
            if (toLower(k) == lower)
            {
                if (match && *match != k)
                    throw std::runtime_error("Ambiguous case-insensitive table match: " + name);
                match = k;
            }
        }
        return match;
    }

    // Output formatting

    std::vector<bool> inferNumericCols(const std::vector<std::string> &cols, const std::vector<std::vector<std::string>> &rows)
    {
        std::vector<bool> numeric(cols.size(), true);
        for (std::size_t ci = 0; ci < cols.size(); ++ci)
        {
            for (const auto &row : rows)
            {
                const std::string &v = ci < row.size() ? row[ci] : "";
                if (!v.empty() && !isNumericValue(v))
                {
                    numeric[ci] = false;
                    break;
                }
            }
        }
        return numeric;
    }

    void printTable(const std::vector<std::string> &cols, const std::vector<std::vector<std::string>> &rows)
    {
        if (cols.empty())
        {
            std::cout << colorize("(0 tuples)\n", kGray);
            return;
        }

        // Strip qualifiers once and cache — avoids calling twice per header.
        std::vector<std::string> headers;
        headers.reserve(cols.size());
        for (const auto &c : cols)
            headers.push_back(stripQualifier(c));

        std::vector<std::size_t> widths(cols.size());
        for (std::size_t i = 0; i < cols.size(); ++i)
            widths[i] = headers[i].size();
        for (const auto &row : rows)
            for (std::size_t i = 0; i < cols.size() && i < row.size(); ++i)
                widths[i] = std::max(widths[i], row[i].size());

        const auto numeric = inferNumericCols(cols, rows);

        // Header — left-aligned, padded with spaces before applying colour so
        // ANSI codes don't confuse setw.
        for (std::size_t i = 0; i < cols.size(); ++i)
        {
            if (i)
                std::cout << " | ";
            std::string padded = headers[i];
            padded += std::string(widths[i] - headers[i].size(), ' ');
            std::cout << colorize(std::move(padded), kBlue);
        }
        std::cout << '\n';

        // Separator
        for (std::size_t i = 0; i < cols.size(); ++i)
        {
            if (i)
                std::cout << "-+-";
            std::cout << std::string(widths[i], '-');
        }
        std::cout << '\n';

        // Data rows
        for (const auto &row : rows)
        {
            for (std::size_t i = 0; i < cols.size(); ++i)
            {
                if (i)
                    std::cout << " | ";
                const std::string &v = i < row.size() ? row[i] : "";
                if (numeric[i])
                    std::cout << std::right << std::setw(static_cast<int>(widths[i])) << v;
                else
                    std::cout << std::left << std::setw(static_cast<int>(widths[i])) << v;
            }
            std::cout << '\n';
        }

        const std::size_t n = rows.size();
        std::cout << colorize('(' + std::to_string(n) + ' ' + (n == 1 ? "tuple" : "tuples") + ")\n", kGray);
    }

    std::string joinAlgorithmLabel(JoinAlgorithm algo)
    {
        switch (algo)
        {
        case JoinAlgorithm::HASH:
            return "hash";
        case JoinAlgorithm::NESTED_LOOP:
            return "nested_loop";
        case JoinAlgorithm::MERGE:
            return "merge";
        }
        return "unknown";
    }

    std::string joinTypeLabel(JoinType type)
    {
        switch (type)
        {
        case JoinType::INNER:
            return "inner";
        case JoinType::LEFT:
            return "left";
        case JoinType::RIGHT:
            return "right";
        case JoinType::FULL:
            return "full";
        case JoinType::CROSS:
            return "cross";
        }
        return "unknown";
    }

    std::string joinStrings(const std::vector<std::string> &items, const char *sep = ", ")
    {
        if (items.empty())
        {
            return {};
        }
        std::string out = items.front();
        for (std::size_t i = 1; i < items.size(); ++i)
        {
            out += sep;
            out += items[i];
        }
        return out;
    }

    std::string exprToString(const Expr *expr)
    {
        if (expr == nullptr)
        {
            return "<null>";
        }

        if (const auto *col = dynamic_cast<const Column *>(expr))
        {
            return col->name;
        }
        if (const auto *lit = dynamic_cast<const Literal *>(expr))
        {
            if (lit->value.empty())
            {
                return "''";
            }
            return isNumericValue(lit->value) ? lit->value : sqlQuoteLiteral(lit->value);
        }
        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            if (bin->op == "NOT")
            {
                return "NOT (" + exprToString(bin->left.get()) + ")";
            }
            return "(" + exprToString(bin->left.get()) + " " + bin->op + " " +
                   exprToString(bin->right.get()) + ")";
        }
        if (const auto *exists = dynamic_cast<const ExistsExpr *>(expr))
        {
            (void)exists;
            return "EXISTS(subquery)";
        }
        if (const auto *inExpr = dynamic_cast<const InExpr *>(expr))
        {
            return exprToString(inExpr->value.get()) + " IN (subquery)";
        }
        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            std::vector<std::string> values;
            values.reserve(inList->list.size());
            for (const auto &item : inList->list)
            {
                values.push_back(exprToString(item.get()));
            }
            return exprToString(inList->value.get()) + " IN (" + joinStrings(values) + ")";
        }
        return "<expr>";
    }

    std::string formatExprList(const std::vector<std::unique_ptr<Expr>> &exprs)
    {
        std::vector<std::string> out;
        out.reserve(exprs.size());
        for (const auto &expr : exprs)
        {
            out.push_back(exprToString(expr.get()));
        }
        return joinStrings(out);
    }

    std::string indentLines(const std::string &text, const std::string &prefix)
    {
        if (text.empty())
        {
            return text;
        }

        std::string out;
        out.reserve(text.size() + prefix.size() * 4);
        bool lineStart = true;
        for (char c : text)
        {
            if (lineStart)
            {
                out += prefix;
                lineStart = false;
            }
            out.push_back(c);
            if (c == '\n')
            {
                lineStart = true;
            }
        }
        return out;
    }

    struct SubqueryInfo
    {
        std::string context;
        const SelectStmt *stmt{nullptr};
    };

    void collectSubqueriesFromExpr(const Expr *expr, const std::string &context,
                                   std::vector<SubqueryInfo> &out);

    void collectSubqueriesFromSelect(const SelectStmt &stmt, const std::string &context,
                                     std::vector<SubqueryInfo> &out)
    {
        for (const auto &join : stmt.joins)
        {
            if (join.condition)
            {
                collectSubqueriesFromExpr(join.condition.get(), context + " JOIN", out);
            }
        }

        if (stmt.where)
        {
            collectSubqueriesFromExpr(stmt.where.get(), context + " WHERE", out);
        }

        if (stmt.having)
        {
            collectSubqueriesFromExpr(stmt.having.get(), context + " HAVING", out);
        }
    }

    void collectSubqueriesFromExpr(const Expr *expr, const std::string &context, std::vector<SubqueryInfo> &out)
    {
        if (expr == nullptr)
        {
            return;
        }

        if (const auto *exists = dynamic_cast<const ExistsExpr *>(expr))
        {
            if (exists->subquery)
            {
                out.push_back({context + " EXISTS", exists->subquery.get()});
                collectSubqueriesFromSelect(*exists->subquery, context + " EXISTS SUBQUERY", out);
            }
            return;
        }

        if (const auto *inExpr = dynamic_cast<const InExpr *>(expr))
        {
            collectSubqueriesFromExpr(inExpr->value.get(), context, out);
            if (inExpr->subquery)
            {
                out.push_back({context + " IN", inExpr->subquery.get()});
                collectSubqueriesFromSelect(*inExpr->subquery, context + " IN SUBQUERY", out);
            }
            return;
        }

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            collectSubqueriesFromExpr(bin->left.get(), context, out);
            collectSubqueriesFromExpr(bin->right.get(), context, out);
            return;
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            collectSubqueriesFromExpr(inList->value.get(), context, out);
            for (const auto &item : inList->list)
            {
                collectSubqueriesFromExpr(item.get(), context, out);
            }
        }
    }

    std::string describePlanNode(const PlanNode &node)
    {
        switch (node.type)
        {
        case PlanType::SEQ_SCAN:
        {
            const auto &scan = static_cast<const SeqScanNode &>(node);
            std::string out = "SEQ_SCAN(table=" + scan.table;
            if (scan.outputQualifier != scan.table)
            {
                out += ", alias=" + scan.outputQualifier;
            }
            if (scan.pushedPredicate)
            {
                out += ", filter=" + exprToString(scan.pushedPredicate.get());
            }
            if (scan.alwaysEmpty)
            {
                out += ", always_empty=true";
            }
            out += ")";
            return out;
        }
        case PlanType::INDEX_SCAN:
        {
            const auto &scan = static_cast<const IndexScanNode &>(node);
            std::string out = "INDEX_SCAN(table=" + scan.table;
            if (scan.outputQualifier != scan.table)
            {
                out += ", alias=" + scan.outputQualifier;
            }
            out += ", lookup=" + scan.outputQualifier + "." + scan.lookupColumn +
                   " = " + sqlQuoteLiteral(scan.lookupValue);
            if (scan.pushedPredicate)
            {
                out += ", filter=" + exprToString(scan.pushedPredicate.get());
            }
            if (scan.alwaysEmpty)
            {
                out += ", always_empty=true";
            }
            out += ")";
            return out;
        }
        case PlanType::JOIN:
        {
            const auto &join = static_cast<const JoinNode &>(node);
            std::string out = "JOIN(type=" + joinTypeLabel(join.joinType) +
                              ", algo=" + joinAlgorithmLabel(join.algorithm);
            if (join.condition)
            {
                out += ", on=" + exprToString(join.condition.get());
            }
            out += ")";
            return out;
        }
        case PlanType::FILTER:
        {
            const auto &filter = static_cast<const FilterNode &>(node);
            return "FILTER(" + exprToString(filter.predicate.get()) + ")";
        }
        case PlanType::PROJECTION:
        {
            const auto &projection = static_cast<const ProjectionNode &>(node);
            return "SELECT(cols=[" + formatExprList(projection.columns) + "])";
        }
        case PlanType::DISTINCT:
            return "DISTINCT";
        case PlanType::AGGREGATION:
        {
            const auto &agg = static_cast<const AggregationNode &>(node);
            std::string out = "AGGREGATE(groups=[" + formatExprList(agg.groupExprs) + "]";
            if (agg.havingExpr)
            {
                out += ", having=" + exprToString(agg.havingExpr.get());
            }
            out += ")";
            return out;
        }
        case PlanType::SORT:
        {
            const auto &sort = static_cast<const SortNode &>(node);
            return "SORT(by=" + sort.orderByColumn + ", dir=" + std::string(sort.ascending ? "ASC" : "DESC") + ")";
        }
        case PlanType::LIMIT:
        {
            const auto &lim = static_cast<const LimitNode &>(node);
            return "LIMIT(" + std::to_string(lim.limitCount) + ")";
        }
        }
        return "UNKNOWN";
    }

    void collectPlanExecutionPath(const PlanNode *node, std::vector<std::string> &out)
    {
        if (node == nullptr)
        {
            return;
        }

        for (const auto &child : node->children)
        {
            collectPlanExecutionPath(child.get(), out);
        }
        out.push_back(describePlanNode(*node));
    }

    std::string renderPlanExecutionPath(const PlanNode *root)
    {
        std::vector<std::string> nodes;
        collectPlanExecutionPath(root, nodes);
        if (nodes.empty())
        {
            return "(empty plan)";
        }

        std::string out;
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            if (!out.empty())
            {
                out += '\n';
            }
            out += std::to_string(i + 1);
            out += ". ";
            out += nodes[i];
        }
        return out;
    }

    // Legacy CSV inference helpers

    std::string inferColType(const std::string &colName, const Table &rows)
    {
        for (const auto &row : rows)
        {
            const auto it = row.find(colName);
            if (it == row.end() || it->second.empty())
                continue;
            if (!isIntegerValue(it->second))
                return "VARCHAR(255)";
        }
        return "INT";
    }

    bool isUniqueNonEmpty(const std::string &colName, const Table &rows)
    {
        if (rows.empty())
            return false;
        std::unordered_set<std::string> seen;
        for (const auto &row : rows)
        {
            const auto it = row.find(colName);
            if (it == row.end() || it->second.empty())
                return false;
            if (!seen.insert(it->second).second)
                return false;
        }
        return true;
    }

    // Constraint validation
    // Build FK lookup sets once before the row loop.
    // Maps "refTable.refCol" → set of valid values.
    // Turns per-row FK scan from O(refTableSize) → O(1).
    using FkLookup = std::unordered_map<std::string, std::unordered_set<std::string>>;

    FkLookup buildFkLookup(const TableMetadata &tm, const std::string &tableName, const Database &db, const Table &candidateRows)
    {
        FkLookup lookup;
        for (const auto &c : tm.columns)
        {
            if (!c.foreignKey)
                continue;
            const auto [refTable, refCol] = parseFkRef(*c.foreignKey);
            const std::string key = refTable + "." + refCol;
            if (lookup.count(key))
                continue;

            const Table &src = (refTable == tableName) ? candidateRows : db.getTable(refTable);
            auto &s = lookup[key];
            s.reserve(src.size());
            for (const auto &rr : src)
            {
                const auto it = rr.find(refCol);
                if (it != rr.end())
                    s.insert(it->second);
            }
        }
        return lookup;
    }

    // Validates the complete post-mutation table state against a supplied table metadata snapshot.
    void validateRowsAgainstMetadata(const std::string &tableName, const TableMetadata &tm, const Table &rows, const Database &db)
    {

        std::vector<const ColumnMetadata *> pkCols;
        for (const auto &c : tm.columns)
            if (c.primaryKey)
                pkCols.push_back(&c);
        const bool compositePK = pkCols.size() > 1;

        // Pre-build FK lookup — O(sum of referenced table sizes), done once.
        const FkLookup fkLookup = buildFkLookup(tm, tableName, db, rows);

        std::unordered_set<std::string> pkSeen;
        pkSeen.reserve(rows.size());
        std::unordered_map<std::string, std::unordered_set<std::string>> uniqueSeen;
        for (const auto &c : tm.columns)
            if (c.unique)
                uniqueSeen[c.name];

        std::vector<LogicalType> logicalTypes;
        logicalTypes.reserve(tm.columns.size());
        for (const auto &c : tm.columns)
            logicalTypes.push_back(parseLogicalType(c.type));

        for (const auto &row : rows)
        {
            for (std::size_t colIdx = 0; colIdx < tm.columns.size(); ++colIdx)
            {
                const auto &c = tm.columns[colIdx];
                const auto vIt = row.find(c.name);
                if (vIt == row.end())
                    throw std::runtime_error("Row missing column '" + c.name + "' in: " + tableName);
                const std::string &val = vIt->second;

                if (c.notNull && val.empty())
                    throw std::runtime_error("Column '" + c.name + "' cannot be NULL");

                validateTypedValue(logicalTypes[colIdx], val, c.name);

                if (c.checkExpr && !evalCheckExpr(normalizeColumnCheckExpr(*c.checkExpr, c.name), row))
                    throw std::runtime_error("CHECK failed for '" + c.name + "': " + *c.checkExpr);

                const bool enforceUnique = c.unique && !(compositePK && c.primaryKey);
                if (enforceUnique && !val.empty())
                    if (!uniqueSeen[c.name].insert(val).second)
                        throw std::runtime_error("Duplicate value '" + val + "' for UNIQUE '" + c.name + "'");
            }

            // PK uniqueness
            if (!pkCols.empty())
            {
                std::string key, keyDisp;
                key.reserve(pkCols.size() * 8);
                for (std::size_t i = 0; i < pkCols.size(); ++i)
                {
                    const auto vIt = row.find(pkCols[i]->name);
                    if (vIt == row.end() || vIt->second.empty())
                        throw std::runtime_error("PRIMARY KEY '" + pkCols[i]->name + "' cannot be empty");
                    if (i)
                    {
                        key += '\x1f';
                        keyDisp += ", ";
                    }
                    key += vIt->second;
                    keyDisp += pkCols[i]->name + "=" + vIt->second;
                }
                if (!pkSeen.insert(key).second)
                    throw std::runtime_error("Duplicate PK (" + keyDisp + ") in: " + tableName);
            }

            // Table-level CHECK constraints
            for (const auto &expr : tm.tableChecks)
                if (!evalCheckExpr(expr, row))
                    throw std::runtime_error("CHECK failed: " + expr);

            // FK constraints — O(1) via pre-built sets
            for (const auto &c : tm.columns)
            {
                if (!c.foreignKey)
                    continue;
                const auto vIt = row.find(c.name);
                if (vIt == row.end() || vIt->second.empty())
                    continue;

                const auto [refTable, refCol] = parseFkRef(*c.foreignKey);
                const std::string fkKey = refTable + "." + refCol;
                const auto setIt = fkLookup.find(fkKey);
                if (setIt == fkLookup.end() || !setIt->second.count(vIt->second))
                    throw std::runtime_error("FK failed on '" + c.name + "': value '" + vIt->second + "' not in " + refTable + "." + refCol);
            }
        }
    }

    // Validates the complete post-mutation table state.
    // Caller must pass ALL rows that will exist after the operation.
    void validateTableState(const std::string &tableName, const Table &rows, const MetadataCatalog &cat, const Database &db)
    {
        validateRowsAgainstMetadata(tableName, cat.getTable(tableName), rows, db);
    }

    void enforceDeleteConstraints(const std::string &tableName, const Table &deletedRows, const MetadataCatalog &cat, const Database &db)
    {
        if (deletedRows.empty())
            return;

        const auto &tm = cat.getTable(tableName);
        std::unordered_map<std::string, std::unordered_set<std::string>> deleted;
        deleted.reserve(tm.columns.size());
        for (const auto &c : tm.columns)
        {
            auto &s = deleted[c.name];
            s.reserve(deletedRows.size());
            for (const auto &row : deletedRows)
            {
                const auto it = row.find(c.name);
                if (it != row.end())
                    s.insert(it->second);
            }
        }

        for (const auto &[rTbl, rMeta] : cat.allTables())
        {
            if (rTbl == tableName || !db.hasTable(rTbl))
                continue;
            const Table &rRows = db.getTable(rTbl);
            for (const auto &col : rMeta.columns)
            {
                if (!col.foreignKey)
                    continue;
                const auto [refTable, refCol] = parseFkRef(*col.foreignKey);
                if (refTable != tableName)
                    continue;
                const auto delIt = deleted.find(refCol);
                if (delIt == deleted.end())
                    continue;
                for (const auto &row : rRows)
                {
                    const auto vIt = row.find(col.name);
                    if (vIt == row.end() || vIt->second.empty())
                        continue;
                    if (delIt->second.count(vIt->second))
                        throw std::runtime_error("DELETE violates FK: " + rTbl + "." + col.name + " references '" + vIt->second + "'");
                }
            }
        }
    }

    // Persistence

    void persistTable(const std::string &tableName, const MetadataCatalog &cat, const Database &db)
    {
        const auto &meta = cat.getTable(tableName);
        std::vector<std::string> headers;
        headers.reserve(meta.columns.size());
        for (const auto &c : meta.columns)
            headers.push_back(c.name);
        CsvLoader::saveTable(cat.tableFilePath(tableName), headers, db.getTable(tableName));
    }

    // Bootstrap

    void loadTablesFromMetadata(Database &db, const MetadataCatalog &cat)
    {
        for (const auto &[tname, tmeta] : cat.allTables())
        {
            std::vector<std::string> headers;
            headers.reserve(tmeta.columns.size());
            for (const auto &c : tmeta.columns)
                headers.push_back(c.name);

            const std::string path = cat.dataDirectory() + "/" + tmeta.file;
            std::error_code ec;
            if (!fs::exists(path, ec))
                CsvLoader::saveTable(path, headers, Table{});

            CsvLoader::loadIntoDatabase(db, tname, path);

            if (db.getSchema(tname) != headers)
                throw std::runtime_error("Schema mismatch between metadata and CSV: " + tname);
        }
    }

    bool registerLegacyTables(Database &db, MetadataCatalog &cat)
    {
        bool changed = false;
        for (const auto &entry : fs::directory_iterator(cat.dataDirectory()))
        {
            if (!entry.is_regular_file())
                continue;
            const fs::path &src = entry.path();
            if (toLower(src.extension().string()) != ".csv")
                continue;
            const std::string tname = src.stem().string();
            if (cat.hasTable(tname))
                continue;

            CsvLoader::loadIntoDatabase(db, tname, src.string());
            TableMetadata meta;
            meta.file = src.filename().string();
            const Table &rows = db.getTable(tname);
            for (const auto &col : db.getSchema(tname))
            {
                ColumnMetadata cm;
                cm.name = col;
                cm.type = inferColType(col, rows);
                if (col == "id" && isUniqueNonEmpty(col, rows))
                    cm.primaryKey = cm.notNull = true;
                meta.columns.push_back(std::move(cm));
            }
            cat.upsertTable(tname, std::move(meta));
            changed = true;
        }
        return changed;
    }

    bool normalizeCatalogTypes(MetadataCatalog &cat)
    {
        bool changed = false;
        std::vector<std::pair<std::string, TableMetadata>> updates;
        for (const auto &[tname, tm] : cat.allTables())
        {
            TableMetadata updated = tm;
            bool dirty = false;
            std::size_t pkCount = 0;
            for (const auto &c : updated.columns)
                if (c.primaryKey)
                    ++pkCount;
            const bool composite = pkCount > 1;
            for (auto &c : updated.columns)
            {
                const LogicalType parsed = parseLogicalType(c.type);
                if (parsed.kind == LogicalTypeKind::ENUM)
                {
                    if (c.type != "TEXT")
                    {
                        c.type = "TEXT";
                        dirty = true;
                    }

                    const std::string enumCompact = buildEnumCompactList(parsed.enumValues);
                    const std::string enumPredicate = buildEnumPredicate(c.name, parsed.enumValues);
                    if (!c.checkExpr || trim(*c.checkExpr).empty())
                    {
                        c.checkExpr = enumCompact;
                        dirty = true;
                    }
                    else
                    {
                        const std::string normalizedExisting =
                            normalizeColumnCheckExpr(*c.checkExpr, c.name);
                        if (!containsCaseInsensitive(normalizedExisting, enumPredicate))
                        {
                            c.checkExpr = "(" + *c.checkExpr + ") AND (" + enumPredicate + ")";
                            dirty = true;
                        }
                    }
                }
                else
                {
                    const std::string norm = parsed.normalizedName;
                    if (norm != c.type)
                    {
                        c.type = norm;
                        dirty = true;
                    }
                }

                if (c.primaryKey && !c.notNull)
                {
                    c.notNull = true;
                    dirty = true;
                }
                if (composite && c.primaryKey && c.unique)
                {
                    c.unique = false;
                    dirty = true;
                }
                if (c.checkExpr)
                {
                    const std::string compact = normalizeStoredColumnCheckExpr(*c.checkExpr, c.name);
                    if (compact != *c.checkExpr)
                    {
                        c.checkExpr = compact;
                        dirty = true;
                    }
                }
            }

            const std::vector<std::string> dedupedChecks = dedupeChecksPreserveOrder(updated.tableChecks);
            if (dedupedChecks != updated.tableChecks)
            {
                updated.tableChecks = dedupedChecks;
                dirty = true;
            }

            if (dirty)
            {
                updates.emplace_back(tname, std::move(updated));
                changed = true;
            }
        }
        for (auto &[n, m] : updates)
            cat.upsertTable(n, std::move(m));
        return changed;
    }

    void bootstrapDatabase(Database &db, MetadataCatalog &cat)
    {
        const bool existed = cat.metadataFileExists();
        cat.load();
        bool legacy = false;
        if (!existed)
            legacy = registerLegacyTables(db, cat);
        const bool normalized = normalizeCatalogTypes(cat);
        loadTablesFromMetadata(db, cat);
        (void)legacy;
        (void)normalized;
        cat.save();
    }

    // CREATE TABLE metadata builder

    TableMetadata buildTableMetadata(const CreateTableStmt &stmt, const MetadataCatalog &cat)
    {
        if (stmt.table.empty())
            throw std::runtime_error("CREATE TABLE: table name is empty");
        if (stmt.columns.empty())
            throw std::runtime_error("CREATE TABLE: at least one column required");

        std::unordered_set<std::string> seen;
        TableMetadata meta;
        meta.file = stmt.table + ".csv";
        meta.tableChecks = dedupeChecksPreserveOrder(stmt.tableChecks);

        for (const auto &def : stmt.columns)
        {
            if (def.name.empty())
                throw std::runtime_error("CREATE TABLE: column name is empty");
            if (!seen.insert(def.name).second)
                throw std::runtime_error("Duplicate column: " + def.name);

            ColumnMetadata c;
            c.name = def.name;
            const LogicalType parsedType = parseLogicalType(def.type);
            if (parsedType.kind == LogicalTypeKind::ENUM)
            {
                c.type = "TEXT";
                const std::string enumPredicate = buildEnumPredicate(c.name, parsedType.enumValues);
                if (def.checkExpr && !trim(*def.checkExpr).empty())
                    c.checkExpr = "(" + *def.checkExpr + ") AND (" + enumPredicate + ")";
                else
                    c.checkExpr = buildEnumCompactList(parsedType.enumValues);
            }
            else
            {
                c.type = parsedType.normalizedName;
                c.checkExpr = def.checkExpr;
            }
            c.primaryKey = def.primaryKey;
            c.unique = def.unique;
            c.notNull = def.notNull || def.primaryKey;
            c.foreignKey = def.foreignKey;
            if (c.checkExpr)
                c.checkExpr = normalizeStoredColumnCheckExpr(*c.checkExpr, c.name);

            if (c.foreignKey)
            {
                const auto parsedRef = parseFkRef(*c.foreignKey);
                const std::string &refTable = parsedRef.first;
                const std::string &refCol = parsedRef.second;
                if (refTable == stmt.table)
                {
                    const bool exists =
                        std::any_of(stmt.columns.begin(), stmt.columns.end(),
                                    [&](const ColumnDef &d)
                                    { return d.name == refCol; });
                    if (!exists)
                        throw std::runtime_error("FK references unknown local column: " +
                                                 *c.foreignKey);
                }
                else
                {
                    const auto canonical = findTableCI(cat, refTable);
                    if (!canonical)
                        throw std::runtime_error("FK references unknown table: " + refTable);
                    if (!findColumn(cat.getTable(*canonical), refCol))
                        throw std::runtime_error("FK references unknown column: " + *c.foreignKey);
                    c.foreignKey = *canonical + "." + refCol;
                }
            }
            meta.columns.push_back(std::move(c));
        }
        return meta;
    }

    // Statement executors

    void executeSelect(const SelectStmt &stmt, Database &db)
    {
        std::vector<std::string> orderedCols;
        const bool isStar = stmt.columns.size() == 1 &&
                            dynamic_cast<const Column *>(stmt.columns[0].get()) != nullptr &&
                            static_cast<const Column *>(stmt.columns[0].get())->name == "*";

        if (isStar)
        {
            if (stmt.joins.empty())
            {
                if (!db.hasSchema(stmt.table))
                    throw std::runtime_error("Unknown table for SELECT *: " + stmt.table);
                orderedCols = db.getSchema(stmt.table);
            }
            else
            {
                auto appendQ = [&](const TableRef &ref)
                {
                    if (!db.hasSchema(ref.table))
                        throw std::runtime_error("Unknown table in SELECT * join: " + ref.table);
                    const std::string q = ref.effectiveName();
                    for (const auto &col : db.getSchema(ref.table))
                        orderedCols.push_back(q + "." + col);
                };
                appendQ(stmt.from);
                for (const auto &j : stmt.joins)
                    appendQ(j.right);
            }
        }
        else
        {
            orderedCols.reserve(stmt.columns.size());
            for (const auto &e : stmt.columns)
            {
                const auto *col = dynamic_cast<const Column *>(e.get());
                if (!col)
                    throw std::runtime_error("Only column references supported in SELECT list");
                orderedCols.push_back(col->name);
            }
        }

        Planner planner;
        auto plan = planner.createPlan(stmt, &db);
        auto exec = ExecutorBuilder::build(plan.get(), db);
        exec->open();

        std::vector<std::vector<std::string>> rows;
        Row row;
        while (exec->next(row))
        {
            std::vector<std::string> pr;
            pr.reserve(orderedCols.size());
            for (const auto &col : orderedCols)
            {
                const auto it = row.find(col);
                if (it == row.end())
                    throw std::runtime_error("Column not in result: " + col);
                pr.push_back(it->second);
            }
            rows.push_back(std::move(pr));
        }
        exec->close();
        printTable(orderedCols, rows);
    }

    void executePathSelect(const SelectStmt &stmt, Database &db)
    {
        Planner planner;
        auto plan = planner.createPlan(stmt, &db);

        std::cout << colorize("Execution path:", kBlue) << '\n';
        std::cout << renderPlanExecutionPath(plan.get()) << '\n';

        std::vector<SubqueryInfo> subqueries;
        collectSubqueriesFromSelect(stmt, "MAIN", subqueries);
        if (!subqueries.empty())
        {
            std::cout << colorize("Subquery path(s):", kOrange) << '\n';
            for (std::size_t i = 0; i < subqueries.size(); ++i)
            {
                if (subqueries[i].stmt == nullptr)
                {
                    continue;
                }
                auto subPlan = planner.createPlan(*subqueries[i].stmt, &db);
                std::cout << std::to_string(i + 1) << ". " << subqueries[i].context << '\n';
                std::cout << indentLines(renderPlanExecutionPath(subPlan.get()), "   ") << '\n';
            }
        }
    }

    // executeCreateTable
    // INVARIANT: metadata for a table is written EXACTLY ONCE — at creation.
    // IF NOT EXISTS on an existing table is a SILENT SKIP.
    // No metadata patching, no CHECK constraint merging — those semantics were
    // a design mistake and are removed here.
    void executeCreateTable(const CreateTableStmt &stmt, Database &db, MetadataCatalog &cat)
    {
        const auto existing = findTableCI(cat, stmt.table);
        if (existing.has_value() || db.hasTable(stmt.table))
        {
            if (stmt.ifNotExists)
            {
                std::cout << colorize("Table '" + stmt.table + "' already exists; skipping (IF NOT EXISTS).", kYellow) << '\n';
                return;
            }
            throw std::runtime_error("CREATE TABLE failed: table already exists: " + (existing.has_value() ? *existing : stmt.table));
        }

        // Metadata is built and written once here — never again for this table.
        TableMetadata meta = buildTableMetadata(stmt, cat);
        std::vector<std::string> headers;
        headers.reserve(meta.columns.size());
        for (const auto &c : meta.columns)
            headers.push_back(c.name);

        cat.upsertTable(stmt.table, meta);
        db.tables[stmt.table] = Table{};
        db.schemas[stmt.table] = headers;
        db.markTableMutated(stmt.table);

        persistTable(stmt.table, cat, db);
        cat.save();

        std::cout << colorize("Table '" + stmt.table + "' created.", kGreen) << '\n';
    }

    void executeAlterTable(const AlterTableStmt &stmt, Database &db, MetadataCatalog &cat)
    {
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
        {
            throw std::runtime_error("Unknown table in ALTER TABLE: " + stmt.table);
        }

        const TableMetadata &currentMeta = cat.getTable(stmt.table);
        TableMetadata nextMeta = currentMeta;
        Table nextRows = db.getTable(stmt.table);
        std::vector<std::pair<std::string, TableMetadata>> sideMetadataUpdates;

        auto columnExists = [&](const std::string &name)
        { return hasColumnName(nextMeta, name); };

        auto normalizeFkRef = [&](std::optional<std::string> &foreignKey)
        {
            if (!foreignKey.has_value())
            {
                return;
            }
            const auto parsedRef = parseFkRef(*foreignKey);
            const std::string &refTable = parsedRef.first;
            const std::string &refCol = parsedRef.second;

            if (refTable == stmt.table)
            {
                if (!columnExists(refCol))
                {
                    throw std::runtime_error("FK references unknown local column: " + *foreignKey);
                }
                foreignKey = refTable + "." + refCol;
                return;
            }

            const auto canonical = findTableCI(cat, refTable);
            if (!canonical)
            {
                throw std::runtime_error("FK references unknown table: " + refTable);
            }
            if (!findColumn(cat.getTable(*canonical), refCol))
            {
                throw std::runtime_error("FK references unknown column: " + *foreignKey);
            }
            foreignKey = *canonical + "." + refCol;
        };

        switch (stmt.action)
        {
        case AlterActionKind::ADD_COLUMN:
        {
            const ColumnDef &def = stmt.columnDef;
            if (def.name.empty())
            {
                throw std::runtime_error("ALTER TABLE ADD COLUMN requires a column name");
            }
            if (columnExists(def.name))
            {
                throw std::runtime_error("Column already exists: " + def.name);
            }

            ColumnMetadata col;
            col.name = def.name;

            const LogicalType parsedType = parseLogicalType(def.type);
            if (parsedType.kind == LogicalTypeKind::ENUM)
            {
                col.type = "TEXT";
                const std::string enumPredicate = buildEnumPredicate(col.name, parsedType.enumValues);
                if (def.checkExpr && !trim(*def.checkExpr).empty())
                {
                    col.checkExpr = "(" + *def.checkExpr + ") AND (" + enumPredicate + ")";
                }
                else
                {
                    col.checkExpr = buildEnumCompactList(parsedType.enumValues);
                }
            }
            else
            {
                col.type = parsedType.normalizedName;
                col.checkExpr = def.checkExpr;
            }

            col.primaryKey = def.primaryKey;
            col.unique = def.unique;
            col.notNull = def.notNull || def.primaryKey;
            col.foreignKey = def.foreignKey;

            if (col.checkExpr)
            {
                col.checkExpr = normalizeStoredColumnCheckExpr(*col.checkExpr, col.name);
            }
            normalizeFkRef(col.foreignKey);

            if (col.notNull && !nextRows.empty())
            {
                throw std::runtime_error("Cannot add NOT NULL column '" + col.name + "' to non-empty table without defaults");
            }
            if (col.primaryKey && !nextRows.empty())
            {
                throw std::runtime_error("Cannot add PRIMARY KEY column '" + col.name + "' to non-empty table without defaults");
            }

            nextMeta.columns.push_back(col);
            for (auto &row : nextRows)
            {
                row[col.name] = "";
            }
            break;
        }

        case AlterActionKind::DROP_COLUMN:
        {
            if (stmt.columnName.empty())
            {
                throw std::runtime_error("ALTER TABLE DROP COLUMN requires a column name");
            }
            if (!columnExists(stmt.columnName))
            {
                throw std::runtime_error("Unknown column in DROP COLUMN: " + stmt.columnName);
            }
            if (nextMeta.columns.size() <= 1)
            {
                throw std::runtime_error("Cannot drop the last column of a table");
            }

            const ColumnMetadata *target = findColumn(nextMeta, stmt.columnName);
            if (target != nullptr && target->primaryKey)
            {
                throw std::runtime_error("Cannot drop PRIMARY KEY column: " + stmt.columnName);
            }

            for (const auto &[tname, tmeta] : cat.allTables())
            {
                for (const auto &c : tmeta.columns)
                {
                    if (!c.foreignKey)
                    {
                        continue;
                    }
                    const auto [refTable, refCol] = parseFkRef(*c.foreignKey);
                    if (refTable == stmt.table && refCol == stmt.columnName)
                    {
                        throw std::runtime_error("Cannot drop column '" + stmt.columnName + "' referenced by FK " + tname + "." + c.name);
                    }
                }
            }

            for (const auto &check : nextMeta.tableChecks)
            {
                if (expressionReferencesIdentifier(check, stmt.columnName))
                {
                    throw std::runtime_error("Cannot drop column '" + stmt.columnName + "' referenced in table CHECK: " + check);
                }
            }
            for (const auto &c : nextMeta.columns)
            {
                if (c.name == stmt.columnName || !c.checkExpr)
                {
                    continue;
                }
                if (expressionReferencesIdentifier(*c.checkExpr, stmt.columnName))
                {
                    throw std::runtime_error("Cannot drop column '" + stmt.columnName + "' referenced by CHECK on column '" + c.name + "'");
                }
            }

            nextMeta.columns.erase(
                std::remove_if(nextMeta.columns.begin(), nextMeta.columns.end(), [&](const ColumnMetadata &c)
                               { return c.name == stmt.columnName; }),
                nextMeta.columns.end());
            for (auto &row : nextRows)
            {
                row.erase(stmt.columnName);
            }
            break;
        }

        case AlterActionKind::RENAME_COLUMN:
        {
            if (stmt.columnName.empty() || stmt.newColumnName.empty())
            {
                throw std::runtime_error("ALTER TABLE RENAME COLUMN requires source and target names");
            }
            if (!columnExists(stmt.columnName))
            {
                throw std::runtime_error("Unknown column in RENAME COLUMN: " + stmt.columnName);
            }
            if (columnExists(stmt.newColumnName))
            {
                throw std::runtime_error("Target column already exists: " + stmt.newColumnName);
            }

            for (auto &c : nextMeta.columns)
            {
                if (c.name == stmt.columnName)
                {
                    c.name = stmt.newColumnName;
                }
                if (c.checkExpr)
                {
                    c.checkExpr = rewriteIdentifierInExpression(*c.checkExpr, stmt.columnName, stmt.newColumnName);
                }
                if (c.foreignKey)
                {
                    const auto [refTable, refCol] = parseFkRef(*c.foreignKey);
                    if (refTable == stmt.table && refCol == stmt.columnName)
                    {
                        c.foreignKey = refTable + "." + stmt.newColumnName;
                    }
                }
            }

            for (auto &expr : nextMeta.tableChecks)
            {
                expr = rewriteIdentifierInExpression(expr, stmt.columnName, stmt.newColumnName);
            }

            for (auto &row : nextRows)
            {
                auto it = row.find(stmt.columnName);
                if (it == row.end())
                {
                    row[stmt.newColumnName] = "";
                    continue;
                }
                const std::string val = it->second;
                row.erase(it);
                row[stmt.newColumnName] = val;
            }

            for (const auto &[tname, tmeta] : cat.allTables())
            {
                if (tname == stmt.table)
                {
                    continue;
                }
                TableMetadata updated = tmeta;
                bool dirty = false;
                for (auto &c : updated.columns)
                {
                    if (!c.foreignKey)
                    {
                        continue;
                    }
                    const auto [refTable, refCol] = parseFkRef(*c.foreignKey);
                    if (refTable == stmt.table && refCol == stmt.columnName)
                    {
                        c.foreignKey = refTable + "." + stmt.newColumnName;
                        dirty = true;
                    }
                }
                if (dirty)
                {
                    sideMetadataUpdates.emplace_back(tname, std::move(updated));
                }
            }
            break;
        }

        case AlterActionKind::ALTER_COLUMN_TYPE:
        {
            if (stmt.columnName.empty())
            {
                throw std::runtime_error("ALTER TABLE ALTER COLUMN requires a column name");
            }
            ColumnMetadata *col = findMutableColumn(nextMeta, stmt.columnName);
            if (col == nullptr)
            {
                throw std::runtime_error("Unknown column in ALTER COLUMN: " + stmt.columnName);
            }

            const LogicalType parsedType = parseLogicalType(stmt.newType);

            if (parsedType.kind == LogicalTypeKind::ENUM)
            {
                col->type = "TEXT";
                const std::string enumPredicate =
                    buildEnumPredicate(stmt.columnName, parsedType.enumValues);
                if (col->checkExpr && !trim(*col->checkExpr).empty())
                {
                    const std::string existing = normalizeColumnCheckExpr(*col->checkExpr, col->name);
                    if (!containsCaseInsensitive(existing, enumPredicate))
                    {
                        col->checkExpr = "(" + *col->checkExpr + ") AND (" + enumPredicate + ")";
                    }
                }
                else
                {
                    col->checkExpr = buildEnumCompactList(parsedType.enumValues);
                }
                if (col->checkExpr)
                {
                    col->checkExpr = normalizeStoredColumnCheckExpr(*col->checkExpr, col->name);
                }
            }
            else
            {
                col->type = parsedType.normalizedName;
            }

            for (auto &row : nextRows)
            {
                const auto it = row.find(stmt.columnName);
                if (it == row.end())
                {
                    continue;
                }
                it->second = normalizeTypedValue(parsedType, it->second, stmt.columnName);
            }
            break;
        }

        case AlterActionKind::ADD_CONSTRAINT:
        {
            switch (stmt.constraintKind)
            {
            case AlterConstraintKind::PRIMARY_KEY:
            {
                if (stmt.constraintColumns.empty())
                {
                    throw std::runtime_error("PRIMARY KEY constraint requires column list");
                }
                const bool composite = stmt.constraintColumns.size() > 1;
                for (const auto &name : stmt.constraintColumns)
                {
                    ColumnMetadata *col = findMutableColumn(nextMeta, name);
                    if (col == nullptr)
                    {
                        throw std::runtime_error("PRIMARY KEY references unknown column: " + name);
                    }
                    col->primaryKey = true;
                    col->notNull = true;
                    if (composite)
                    {
                        col->unique = false;
                    }
                }
                break;
            }
            case AlterConstraintKind::UNIQUE:
            {
                if (stmt.constraintColumns.empty())
                {
                    throw std::runtime_error("UNIQUE constraint requires column list");
                }
                for (const auto &name : stmt.constraintColumns)
                {
                    ColumnMetadata *col = findMutableColumn(nextMeta, name);
                    if (col == nullptr)
                    {
                        throw std::runtime_error("UNIQUE references unknown column: " + name);
                    }
                    col->unique = true;
                }
                break;
            }
            case AlterConstraintKind::FOREIGN_KEY:
            {
                if (stmt.constraintColumns.size() != 1)
                {
                    throw std::runtime_error("FOREIGN KEY constraint currently supports one local column");
                }
                ColumnMetadata *col = findMutableColumn(nextMeta, stmt.constraintColumns.front());
                if (col == nullptr)
                {
                    throw std::runtime_error("FOREIGN KEY references unknown local column: " + stmt.constraintColumns.front());
                }
                const auto canonical = findTableCI(cat, stmt.referencedTable);
                if (!canonical)
                {
                    throw std::runtime_error("FOREIGN KEY references unknown table: " + stmt.referencedTable);
                }
                if (!findColumn(cat.getTable(*canonical), stmt.referencedColumn))
                {
                    throw std::runtime_error("FOREIGN KEY references unknown column: " + stmt.referencedTable + "." + stmt.referencedColumn);
                }
                col->foreignKey = *canonical + "." + stmt.referencedColumn;
                break;
            }
            case AlterConstraintKind::CHECK:
            {
                if (trim(stmt.checkExpr).empty())
                {
                    throw std::runtime_error("CHECK constraint expression cannot be empty");
                }
                if (!hasTableCheck(nextMeta.tableChecks, stmt.checkExpr))
                {
                    nextMeta.tableChecks.push_back(trim(stmt.checkExpr));
                }
                break;
            }
            }
            break;
        }
        }

        validateRowsAgainstMetadata(stmt.table, nextMeta, nextRows, db);

        for (auto &[tname, tmeta] : sideMetadataUpdates)
        {
            cat.upsertTable(tname, std::move(tmeta));
        }
        cat.upsertTable(stmt.table, nextMeta);
        db.getTable(stmt.table) = std::move(nextRows);

        std::vector<std::string> headers;
        headers.reserve(nextMeta.columns.size());
        for (const auto &c : nextMeta.columns)
        {
            headers.push_back(c.name);
        }
        db.schemas[stmt.table] = std::move(headers);

        db.markTableMutated(stmt.table);
        persistTable(stmt.table, cat, db);
        cat.save();

        std::cout << colorize("Table '" + stmt.table + "' altered.", kGreen) << '\n';
    }

    // executeInsert
    // Validates only the NEW rows against existing data — no full-table copy.
    // Full candidate = existing + new is constructed only for validateTableState.
    std::size_t executeInsert(const InsertStmt &stmt, Database &db, const MetadataCatalog &cat)
    {
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
            throw std::runtime_error("Unknown table in INSERT: " + stmt.table);
        if (stmt.valueRows.empty())
            throw std::runtime_error("INSERT requires at least one VALUES row");

        const auto &tm = cat.getTable(stmt.table);
        std::vector<std::string> targetCols;
        if (stmt.columns.empty())
        {
            targetCols.reserve(tm.columns.size());
            for (const auto &c : tm.columns)
                targetCols.push_back(c.name);
        }
        else
        {
            targetCols = stmt.columns;
        }

        {
            std::unordered_set<std::string> seenCols;
            for (const auto &cn : targetCols)
            {
                if (!seenCols.insert(cn).second)
                    throw std::runtime_error("Duplicate column in INSERT: " + cn);
                if (!findColumn(tm, cn))
                    throw std::runtime_error("Unknown column in INSERT: " + cn);
            }
        }

        std::unordered_map<std::string, LogicalType> logicalTypes;
        logicalTypes.reserve(tm.columns.size());
        for (const auto &c : tm.columns)
            logicalTypes.emplace(c.name, parseLogicalType(c.type));

        Table newRows;
        newRows.reserve(stmt.valueRows.size());
        for (const auto &valRow : stmt.valueRows)
        {
            if (targetCols.size() != valRow.size())
                throw std::runtime_error("INSERT column/value mismatch: " + std::to_string(targetCols.size()) + " cols, " + std::to_string(valRow.size()) + " vals");
            Row r;
            r.reserve(tm.columns.size());
            for (const auto &c : tm.columns)
                r[c.name] = "";
            const Row emptyCtx;
            for (std::size_t i = 0; i < targetCols.size(); ++i)
            {
                const std::string raw = ExpressionEvaluator::eval(valRow[i].get(), emptyCtx);
                r[targetCols[i]] =
                    normalizeTypedValue(logicalTypes.at(targetCols[i]), raw, targetCols[i]);
            }
            newRows.push_back(std::move(r));
        }

        // Build full candidate for constraint validation.
        Table &existing = db.getTable(stmt.table);
        Table candidate;
        candidate.reserve(existing.size() + newRows.size());
        candidate = existing;
        for (auto &r : newRows)
            candidate.push_back(r);

        validateTableState(stmt.table, candidate, cat, db);
        existing = std::move(candidate);
        db.markTableMutated(stmt.table);
        return newRows.size();
    }

    std::size_t executeUpdate(const UpdateStmt &stmt, Database &db, const MetadataCatalog &cat)
    {
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
            throw std::runtime_error("Unknown table in UPDATE: " + stmt.table);
        if (stmt.assignments.empty())
            throw std::runtime_error("UPDATE requires at least one SET assignment");

        const auto &tm = cat.getTable(stmt.table);
        for (const auto &a : stmt.assignments)
            if (!findColumn(tm, a.column))
                throw std::runtime_error("Unknown column in UPDATE SET: " + a.column);

        std::unordered_map<std::string, LogicalType> assignmentTypes;
        assignmentTypes.reserve(stmt.assignments.size());
        for (const auto &a : stmt.assignments)
        {
            const auto *meta = findColumn(tm, a.column);
            assignmentTypes.emplace(a.column, parseLogicalType(meta->type));
        }

        const Table &current = db.getTable(stmt.table);
        Table candidate;
        candidate.reserve(current.size());
        std::size_t updated = 0;

        for (const auto &row : current)
        {
            if (stmt.where && !ExpressionEvaluator::evalPredicate(stmt.where.get(), row))
            {
                candidate.push_back(row); // unmatched — unchanged
            }
            else
            {
                Row r = row;
                for (const auto &a : stmt.assignments)
                {
                    const std::string raw = ExpressionEvaluator::eval(a.value.get(), row);
                    r[a.column] = normalizeTypedValue(assignmentTypes.at(a.column), raw, a.column);
                }
                candidate.push_back(std::move(r));
                ++updated;
            }
        }

        if (updated == 0)
            return 0;
        validateTableState(stmt.table, candidate, cat, db);
        db.getTable(stmt.table) = std::move(candidate);
        db.markTableMutated(stmt.table);
        return updated;
    }

    std::size_t executeDelete(const DeleteStmt &stmt, Database &db, const MetadataCatalog &cat)
    {
        if (!cat.hasTable(stmt.table) || !db.hasTable(stmt.table))
            throw std::runtime_error("Unknown table in DELETE: " + stmt.table);

        const Table &current = db.getTable(stmt.table);
        Table kept, deleted;
        kept.reserve(current.size());

        for (const auto &row : current)
        {
            if (!stmt.where || ExpressionEvaluator::evalPredicate(stmt.where.get(), row))
                deleted.push_back(row);
            else
                kept.push_back(row);
        }

        if (deleted.empty())
            return 0;
        enforceDeleteConstraints(stmt.table, deleted, cat, db);
        db.getTable(stmt.table) = std::move(kept);
        db.markTableMutated(stmt.table);
        return deleted.size();
    }

    // Parse / resolve / dispatch

    Statement parseOne(const std::string &query)
    {
        Lexer lexer(query);
        Parser parser(lexer.tokenize());
        return parser.parseStatement();
    }

    // Resolves table names case-insensitively. Called after parsing, before dispatch.
    // CREATE TABLE is intentionally excluded — the table doesn't exist yet.
    void resolveTableNames(Statement &stmt, const MetadataCatalog &cat)
    {
        auto canon = [&](const std::string &name) -> std::string
        {
            const auto c = findTableCI(cat, name);
            if (!c)
                throw std::runtime_error("Unknown table: " + name);
            return *c;
        };
        switch (stmt.type)
        {
        case StatementType::SELECT:
        case StatementType::PATH_SELECT:
            stmt.select->from.table = canon(stmt.select->from.table);
            stmt.select->table = stmt.select->from.table;
            for (auto &j : stmt.select->joins)
                j.right.table = canon(j.right.table);
            break;
        case StatementType::INSERT:
            stmt.insert->table = canon(stmt.insert->table);
            break;
        case StatementType::UPDATE:
            stmt.update->table = canon(stmt.update->table);
            break;
        case StatementType::DELETE_:
            stmt.deleteStmt->table = canon(stmt.deleteStmt->table);
            break;
        case StatementType::ALTER_TABLE:
            stmt.alterTable->table = canon(stmt.alterTable->table);
            break;
        case StatementType::CREATE_TABLE:
            break; // no-op
        default:
            throw std::runtime_error("Unsupported statement type");
        }
    }

    void executeDispatch(Statement &stmt, Database &db, MetadataCatalog &cat)
    {
        switch (stmt.type)
        {
        case StatementType::SELECT:
            executeSelect(*stmt.select, db);
            break;
        case StatementType::PATH_SELECT:
            executePathSelect(*stmt.select, db);
            break;
        case StatementType::CREATE_TABLE:
            executeCreateTable(*stmt.createTable, db, cat);
            break;
        case StatementType::INSERT:
        {
            const auto n = executeInsert(*stmt.insert, db, cat);
            persistTable(stmt.insert->table, cat, db);
            std::cout << colorize('(' + std::to_string(n) + ' ' + (n == 1 ? "row" : "rows") + " inserted)", n == 0 ? kYellow : kGreen) << '\n';
            break;
        }
        case StatementType::UPDATE:
        {
            const auto n = executeUpdate(*stmt.update, db, cat);
            persistTable(stmt.update->table, cat, db);
            std::cout << colorize('(' + std::to_string(n) + ' ' + (n == 1 ? "row" : "rows") + " updated)", n == 0 ? kYellow : kGreen) << '\n';
            break;
        }
        case StatementType::DELETE_:
        {
            const auto n = executeDelete(*stmt.deleteStmt, db, cat);
            persistTable(stmt.deleteStmt->table, cat, db);
            std::cout << colorize('(' + std::to_string(n) + ' ' + (n == 1 ? "row" : "rows") + " deleted)", n == 0 ? kYellow : kGreen) << '\n';
            break;
        }
        case StatementType::ALTER_TABLE:
            executeAlterTable(*stmt.alterTable, db, cat);
            break;
        default:
            throw std::runtime_error("Unsupported statement type");
        }
    }

    // Script error helpers

    struct ErrorLocation
    {
        bool has{false};
        std::size_t line{1}, col{1};
    };

    ErrorLocation extractLocalLoc(const std::string &msg)
    {
        static const std::regex kLoc(R"(line\s+([0-9]+)\s*,\s*column\s+([0-9]+))", std::regex::icase);
        std::smatch m;
        if (!std::regex_search(msg, m, kLoc) || m.size() != 3)
            return {};
        return {true, static_cast<std::size_t>(std::stoull(m[1].str())),
                static_cast<std::size_t>(std::stoull(m[2].str()))};
    }

    std::string formatScriptError(const SqlStatement &stmt, const std::string &msg)
    {
        std::size_t line = stmt.startLine, col = stmt.startColumn;
        const auto loc = extractLocalLoc(msg);
        if (loc.has)
        {
            line = stmt.startLine + (loc.line - 1);
            col = (loc.line == 1) ? stmt.startColumn + (loc.col - 1) : loc.col;
        }
        static const std::regex kSuffix(R"(\s*at\s+line\s+[0-9]+\s*,\s*column\s+[0-9]+)", std::regex::icase);
        return "Error at Ln " + std::to_string(line) + ", Col " + std::to_string(col) + ": " + std::regex_replace(msg, kSuffix, "");
    }

    // Data directory resolution

    std::string resolveDataDirImpl()
    {
        for (const std::string &p : {std::string(kDataDir), "../" + std::string(kDataDir)})
        {
            std::error_code ec;
            if (fs::exists(p, ec) && fs::is_directory(p, ec))
                return p;
        }
        const fs::path fallback = kDataDir;
        std::error_code ec;
        fs::create_directories(fallback, ec);
        if (ec)
            throw std::runtime_error("Cannot create data directory: " + ec.message());
        return fallback.string();
    }

}

// QueryEngineApp — public API

QueryEngineApp::QueryEngineApp(std::string dataDirectory) : catalog_(std::move(dataDirectory)) {}

std::string QueryEngineApp::resolveDataDirectory()
{
    return resolveDataDirImpl();
}

void QueryEngineApp::initialize()
{
    bootstrapDatabase(db_, catalog_);
}

void QueryEngineApp::executeStatement(const std::string &query)
{
    Statement stmt = parseOne(query);
    if (stmt.type != StatementType::CREATE_TABLE)
        resolveTableNames(stmt, catalog_);
    executeDispatch(stmt, db_, catalog_);
}

void QueryEngineApp::executeScript(const std::vector<SqlStatement> &statements)
{
    if (statements.empty())
        return;

    // parse all
    std::vector<Statement> parsed;
    parsed.reserve(statements.size());
    for (std::size_t i = 0; i < statements.size(); ++i)
    {
        try
        {
            parsed.push_back(parseOne(statements[i].text));
        }
        catch (const std::exception &ex)
        {
            throw std::runtime_error(formatScriptError(statements[i], ex.what()));
        }
    }

    // resolve + execute; continue on execution errors.
    std::size_t ok = 0, fail = 0;
    for (std::size_t i = 0; i < parsed.size(); ++i)
    {
        try
        {
            if (parsed[i].type != StatementType::CREATE_TABLE)
                resolveTableNames(parsed[i], catalog_);
            executeDispatch(parsed[i], db_, catalog_);
            ++ok;
        }
        catch (const std::exception &ex)
        {
            ++fail;
            std::cerr << colorize("Execution failed (Ln " + std::to_string(statements[i].startLine) + ", Col " + std::to_string(statements[i].startColumn) + "): " + ex.what(), kRed) << '\n';
        }
        if (i + 1 < parsed.size())
            std::cout << '\n';
    }

    if (fail > 0)
        throw std::runtime_error("Execution summary: " + std::to_string(ok) + " succeeded, " + std::to_string(fail) + " failed.");

    std::cout << colorize("\nExecution summary: all " + std::to_string(ok) + " statement(s) succeeded.", kGreen) << "\n\n";
}

std::vector<QueryEngineApp::TableSummary> QueryEngineApp::getTableSummaries() const
{
    std::vector<TableSummary> summaries;
    summaries.reserve(catalog_.allTables().size());

    for (const auto &[tableName, tableMeta] : catalog_.allTables())
    {
        std::size_t rowCount = 0;
        if (db_.hasTable(tableName))
        {
            rowCount = db_.getTable(tableName).size();
        }
        summaries.push_back(TableSummary{tableName, tableMeta.columns.size(), rowCount});
    }

    std::sort(summaries.begin(), summaries.end(), [](const TableSummary &lhs, const TableSummary &rhs)
              { return lhs.name < rhs.name; });
    return summaries;
}

TableMetadata QueryEngineApp::getTableMetadata(const std::string &tableName) const
{
    return catalog_.getTable(tableName);
}
