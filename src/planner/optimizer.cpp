#include "planner/optimizer.hpp"

#include "execution/expression.hpp"
#include "storage/table.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{

    bool isAggregateReference(const std::string &name)
    {
        const auto l = name.find('(');
        const auto r = name.rfind(')');
        if (l == std::string::npos || r == std::string::npos || r <= l + 1)
        {
            return false;
        }
        const std::string fn = name.substr(0, l);
        return fn == "COUNT" || fn == "SUM" || fn == "AVG" || fn == "MIN" || fn == "MAX" || fn == "COUNT_DISTINCT";
    }

    std::vector<std::string> splitCommaTopLevel(const std::string &s)
    {
        std::vector<std::string> out;
        std::string cur;
        int depth = 0;
        bool inQuote = false;

        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];

            if (c == '\'')
            {
                cur.push_back(c);
                if (inQuote && i + 1 < s.size() && s[i + 1] == '\'')
                {
                    cur.push_back('\'');
                    ++i;
                    continue;
                }
                inQuote = !inQuote;
                continue;
            }

            if (!inQuote)
            {
                if (c == '(')
                {
                    ++depth;
                    cur.push_back(c);
                    continue;
                }
                if (c == ')')
                {
                    --depth;
                    cur.push_back(c);
                    continue;
                }
                if (c == ',' && depth == 0)
                {
                    auto trim = [](std::string x)
                    {
                        const auto isWs = [](unsigned char ch)
                        { return std::isspace(ch) != 0; };
                        const auto b = std::find_if_not(x.begin(), x.end(), isWs);
                        const auto e = std::find_if_not(x.rbegin(), x.rend(), isWs).base();
                        return (b >= e) ? std::string{} : std::string(b, e);
                    };
                    out.push_back(trim(cur));
                    cur.clear();
                    continue;
                }
            }

            cur.push_back(c);
        }

        auto trim = [](std::string x)
        {
            const auto isWs = [](unsigned char ch)
            { return std::isspace(ch) != 0; };
            const auto b = std::find_if_not(x.begin(), x.end(), isWs);
            const auto e = std::find_if_not(x.rbegin(), x.rend(), isWs).base();
            return (b >= e) ? std::string{} : std::string(b, e);
        };
        out.push_back(trim(cur));
        return out;
    }

    bool containsSubquery(const Expr *expr)
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
            return containsSubquery(bin->left.get()) || containsSubquery(bin->right.get());
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            if (containsSubquery(inList->value.get()))
            {
                return true;
            }
            for (const auto &item : inList->list)
            {
                if (containsSubquery(item.get()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool truthy(const std::string &raw)
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
        return !v.empty() && v != "0" && v != "false" && v != "f";
    }

    std::optional<std::string> tryEvalConstScalar(const Expr *expr);
    std::optional<bool> tryEvalConstPredicate(const Expr *expr);

    std::optional<std::string> tryEvalConstScalar(const Expr *expr)
    {
        if (expr == nullptr)
        {
            return std::nullopt;
        }

        if (const auto *lit = dynamic_cast<const Literal *>(expr))
        {
            return lit->value;
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            const auto pred = tryEvalConstPredicate(inList);
            if (!pred.has_value())
            {
                return std::nullopt;
            }
            return *pred ? std::string("1") : std::string("0");
        }

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            if (bin->op == "NOT" || bin->op == "AND" || bin->op == "OR")
            {
                const auto pred = tryEvalConstPredicate(bin);
                if (!pred.has_value())
                {
                    return std::nullopt;
                }
                return *pred ? std::string("1") : std::string("0");
            }

            const auto l = tryEvalConstScalar(bin->left.get());
            const auto r = tryEvalConstScalar(bin->right.get());
            if (!l.has_value() || !r.has_value())
            {
                return std::nullopt;
            }
            const bool result = ExpressionEvaluator::compareValues(*l, bin->op, *r);
            return result ? std::string("1") : std::string("0");
        }

        return std::nullopt;
    }

    std::optional<bool> tryEvalConstPredicate(const Expr *expr)
    {
        if (expr == nullptr)
        {
            return true;
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            const auto lhs = tryEvalConstScalar(inList->value.get());
            if (!lhs.has_value())
            {
                return std::nullopt;
            }
            for (const auto &item : inList->list)
            {
                const auto rhs = tryEvalConstScalar(item.get());
                if (!rhs.has_value())
                {
                    return std::nullopt;
                }
                if (*lhs == *rhs)
                {
                    return true;
                }
            }
            return false;
        }

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            if (bin->op == "NOT")
            {
                const auto left = tryEvalConstPredicate(bin->left.get());
                if (!left.has_value())
                {
                    return std::nullopt;
                }
                return !*left;
            }
            if (bin->op == "AND")
            {
                const auto left = tryEvalConstPredicate(bin->left.get());
                const auto right = tryEvalConstPredicate(bin->right.get());
                if (left.has_value() && !*left)
                {
                    return false;
                }
                if (right.has_value() && !*right)
                {
                    return false;
                }
                if (left.has_value() && right.has_value())
                {
                    return *left && *right;
                }
                return std::nullopt;
            }
            if (bin->op == "OR")
            {
                const auto left = tryEvalConstPredicate(bin->left.get());
                const auto right = tryEvalConstPredicate(bin->right.get());
                if (left.has_value() && *left)
                {
                    return true;
                }
                if (right.has_value() && *right)
                {
                    return true;
                }
                if (left.has_value() && right.has_value())
                {
                    return *left || *right;
                }
                return std::nullopt;
            }

            const auto l = tryEvalConstScalar(bin->left.get());
            const auto r = tryEvalConstScalar(bin->right.get());
            if (!l.has_value() || !r.has_value())
            {
                return std::nullopt;
            }
            return ExpressionEvaluator::compareValues(*l, bin->op, *r);
        }

        const auto scalar = tryEvalConstScalar(expr);
        if (!scalar.has_value())
        {
            return std::nullopt;
        }
        return truthy(*scalar);
    }

    std::unique_ptr<Expr> makeBoolLiteral(bool v)
    {
        return std::make_unique<Literal>(v ? "1" : "0");
    }

    std::unique_ptr<Expr> simplifyExpr(std::unique_ptr<Expr> expr)
    {
        if (!expr)
        {
            return expr;
        }

        if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get()))
        {
            bin->left = simplifyExpr(std::move(bin->left));
            bin->right = simplifyExpr(std::move(bin->right));

            const auto leftConst = tryEvalConstPredicate(bin->left.get());
            const auto rightConst = tryEvalConstPredicate(bin->right.get());

            if (bin->op == "NOT")
            {
                if (leftConst.has_value())
                {
                    return makeBoolLiteral(!*leftConst);
                }
                return expr;
            }

            if (bin->op == "AND")
            {
                if (leftConst.has_value() && !*leftConst)
                {
                    return makeBoolLiteral(false);
                }
                if (rightConst.has_value() && !*rightConst)
                {
                    return makeBoolLiteral(false);
                }
                if (leftConst.has_value() && *leftConst)
                {
                    return std::move(bin->right);
                }
                if (rightConst.has_value() && *rightConst)
                {
                    return std::move(bin->left);
                }
                if (leftConst.has_value() && rightConst.has_value())
                {
                    return makeBoolLiteral(*leftConst && *rightConst);
                }
                return expr;
            }

            if (bin->op == "OR")
            {
                if (leftConst.has_value() && *leftConst)
                {
                    return makeBoolLiteral(true);
                }
                if (rightConst.has_value() && *rightConst)
                {
                    return makeBoolLiteral(true);
                }
                if (leftConst.has_value() && !*leftConst)
                {
                    return std::move(bin->right);
                }
                if (rightConst.has_value() && !*rightConst)
                {
                    return std::move(bin->left);
                }
                if (leftConst.has_value() && rightConst.has_value())
                {
                    return makeBoolLiteral(*leftConst || *rightConst);
                }
                return expr;
            }

            const auto leftScalar = tryEvalConstScalar(bin->left.get());
            const auto rightScalar = tryEvalConstScalar(bin->right.get());
            if (leftScalar.has_value() && rightScalar.has_value())
            {
                const bool pred =
                    ExpressionEvaluator::compareValues(*leftScalar, bin->op, *rightScalar);
                return makeBoolLiteral(pred);
            }
            return expr;
        }

        if (auto *inList = dynamic_cast<InListExpr *>(expr.get()))
        {
            inList->value = simplifyExpr(std::move(inList->value));
            for (auto &item : inList->list)
            {
                item = simplifyExpr(std::move(item));
            }

            const auto pred = tryEvalConstPredicate(inList);
            if (pred.has_value())
            {
                return makeBoolLiteral(*pred);
            }
            return expr;
        }

        return expr;
    }

    void addColumnName(const std::string &name, std::unordered_map<std::string, std::unordered_set<std::string>> &qualified, std::unordered_set<std::string> &bare, bool &hasStar);

    enum class PredicateSide
    {
        NONE,
        LEFT,
        RIGHT,
        BOTH,
        UNKNOWN,
    };

    void collectOutputQualifiers(const PlanNode *node, std::unordered_set<std::string> &out)
    {
        if (node == nullptr)
        {
            return;
        }

        if (node->type == PlanType::SEQ_SCAN)
        {
            const auto *scan = static_cast<const SeqScanNode *>(node);
            out.insert(scan->outputQualifier);
            return;
        }

        for (const auto &child : node->children)
        {
            collectOutputQualifiers(child.get(), out);
        }
    }

    void flattenAndConjuncts(std::unique_ptr<Expr> expr, std::vector<std::unique_ptr<Expr>> &out)
    {
        if (!expr)
        {
            return;
        }

        auto *bin = dynamic_cast<BinaryExpr *>(expr.get());
        if (bin != nullptr && bin->op == "AND")
        {
            std::unique_ptr<Expr> left = std::move(bin->left);
            std::unique_ptr<Expr> right = std::move(bin->right);
            flattenAndConjuncts(std::move(left), out);
            flattenAndConjuncts(std::move(right), out);
            return;
        }

        out.push_back(std::move(expr));
    }

    std::unique_ptr<Expr> makeAndChain(std::vector<std::unique_ptr<Expr>> exprs)
    {
        if (exprs.empty())
        {
            return nullptr;
        }

        std::unique_ptr<Expr> root = std::move(exprs[0]);
        for (std::size_t i = 1; i < exprs.size(); ++i)
        {
            root = std::make_unique<BinaryExpr>(std::move(root), "AND", std::move(exprs[i]));
        }
        return root;
    }

    void collectPredicateSideRefs(const Expr *expr, const std::unordered_set<std::string> &leftQualifiers, const std::unordered_set<std::string> &rightQualifiers, bool &sawLeft, bool &sawRight, bool &sawUnknown, bool &sawSubquery)
    {
        if (expr == nullptr || sawSubquery)
        {
            return;
        }

        if (dynamic_cast<const ExistsExpr *>(expr) != nullptr ||
            dynamic_cast<const InExpr *>(expr) != nullptr)
        {
            sawSubquery = true;
            return;
        }

        if (const auto *col = dynamic_cast<const Column *>(expr))
        {
            const auto dot = col->name.find('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= col->name.size())
            {
                sawUnknown = true;
                return;
            }

            const std::string qualifier = col->name.substr(0, dot);
            const bool inLeft = leftQualifiers.count(qualifier) != 0;
            const bool inRight = rightQualifiers.count(qualifier) != 0;
            if (inLeft && !inRight)
            {
                sawLeft = true;
                return;
            }
            if (inRight && !inLeft)
            {
                sawRight = true;
                return;
            }
            sawUnknown = true;
            return;
        }

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            collectPredicateSideRefs(bin->left.get(), leftQualifiers, rightQualifiers, sawLeft, sawRight, sawUnknown, sawSubquery);
            collectPredicateSideRefs(bin->right.get(), leftQualifiers, rightQualifiers, sawLeft, sawRight, sawUnknown, sawSubquery);
            return;
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            collectPredicateSideRefs(inList->value.get(), leftQualifiers, rightQualifiers, sawLeft, sawRight, sawUnknown, sawSubquery);
            for (const auto &item : inList->list)
            {
                collectPredicateSideRefs(item.get(), leftQualifiers, rightQualifiers, sawLeft, sawRight, sawUnknown, sawSubquery);
            }
        }
    }

    PredicateSide classifyPredicateSide(const Expr *expr, const std::unordered_set<std::string> &leftQualifiers, const std::unordered_set<std::string> &rightQualifiers)
    {
        bool sawLeft = false;
        bool sawRight = false;
        bool sawUnknown = false;
        bool sawSubquery = false;

        collectPredicateSideRefs(expr, leftQualifiers, rightQualifiers, sawLeft, sawRight, sawUnknown, sawSubquery);

        if (sawSubquery || sawUnknown)
        {
            return PredicateSide::UNKNOWN;
        }
        if (sawLeft && sawRight)
        {
            return PredicateSide::BOTH;
        }
        if (sawLeft)
        {
            return PredicateSide::LEFT;
        }
        if (sawRight)
        {
            return PredicateSide::RIGHT;
        }
        return PredicateSide::NONE;
    }

    void attachFilterToChild(std::unique_ptr<PlanNode> &child, std::unique_ptr<Expr> predicate)
    {
        if (!predicate)
        {
            return;
        }

        if (child && child->type == PlanType::FILTER)
        {
            auto *existing = static_cast<FilterNode *>(child.get());
            existing->predicate = std::make_unique<BinaryExpr>(std::move(existing->predicate), "AND", std::move(predicate));
            return;
        }

        auto f = std::make_unique<FilterNode>(std::move(predicate));
        f->children.push_back(std::move(child));
        child = std::move(f);
    }

    void collectExprColumns(const Expr *expr, std::unordered_map<std::string, std::unordered_set<std::string>> &qualified, std::unordered_set<std::string> &bare, bool &hasStar, bool &hasSubquery)
    {
        if (expr == nullptr)
        {
            return;
        }

        if (dynamic_cast<const ExistsExpr *>(expr) != nullptr ||
            dynamic_cast<const InExpr *>(expr) != nullptr)
        {
            hasSubquery = true;
            return;
        }

        if (const auto *col = dynamic_cast<const Column *>(expr))
        {
            addColumnName(col->name, qualified, bare, hasStar);
            return;
        }

        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            collectExprColumns(bin->left.get(), qualified, bare, hasStar, hasSubquery);
            collectExprColumns(bin->right.get(), qualified, bare, hasStar, hasSubquery);
            return;
        }

        if (const auto *inList = dynamic_cast<const InListExpr *>(expr))
        {
            collectExprColumns(inList->value.get(), qualified, bare, hasStar, hasSubquery);
            for (const auto &item : inList->list)
            {
                collectExprColumns(item.get(), qualified, bare, hasStar, hasSubquery);
            }
        }
    }

    void addColumnName(const std::string &name, std::unordered_map<std::string, std::unordered_set<std::string>> &qualified, std::unordered_set<std::string> &bare, bool &hasStar)
    {
        if (name.empty())
        {
            return;
        }

        if (name == "*")
        {
            hasStar = true;
            return;
        }

        if (isAggregateReference(name))
        {
            const auto l = name.find('(');
            const auto r = name.rfind(')');
            const std::string args = name.substr(l + 1, r - l - 1);
            for (const auto &arg : splitCommaTopLevel(args))
            {
                if (!arg.empty() && arg != "*")
                {
                    addColumnName(arg, qualified, bare, hasStar);
                }
            }
            return;
        }

        const auto dot = name.find('.');
        if (dot != std::string::npos && dot > 0 && dot + 1 < name.size())
        {
            qualified[name.substr(0, dot)].insert(name.substr(dot + 1));
            return;
        }

        bare.insert(name);
    }

    void collectPlanColumns(const PlanNode *node, std::unordered_map<std::string, std::unordered_set<std::string>> &qualified, std::unordered_set<std::string> &bare, bool &hasStar, bool &hasSubquery)
    {
        if (node == nullptr)
        {
            return;
        }

        switch (node->type)
        {
        case PlanType::SEQ_SCAN:
        {
            const auto *scan = static_cast<const SeqScanNode *>(node);
            if (scan->pushedPredicate)
            {
                collectExprColumns(scan->pushedPredicate.get(), qualified, bare, hasStar, hasSubquery);
            }
            break;
        }
        case PlanType::INDEX_SCAN:
        {
            const auto *scan = static_cast<const IndexScanNode *>(node);
            addColumnName(scan->lookupColumn, qualified, bare, hasStar);
            if (scan->pushedPredicate)
            {
                collectExprColumns(scan->pushedPredicate.get(), qualified, bare, hasStar, hasSubquery);
            }
            break;
        }
        case PlanType::FILTER:
        {
            const auto *filter = static_cast<const FilterNode *>(node);
            collectExprColumns(filter->predicate.get(), qualified, bare, hasStar, hasSubquery);
            break;
        }
        case PlanType::PROJECTION:
        {
            const auto *proj = static_cast<const ProjectionNode *>(node);
            for (const auto &expr : proj->columns)
            {
                collectExprColumns(expr.get(), qualified, bare, hasStar, hasSubquery);
            }
            break;
        }
        case PlanType::AGGREGATION:
        {
            const auto *agg = static_cast<const AggregationNode *>(node);
            for (const auto &expr : agg->groupExprs)
            {
                collectExprColumns(expr.get(), qualified, bare, hasStar, hasSubquery);
            }
            for (const auto &expr : agg->selectExprs)
            {
                collectExprColumns(expr.get(), qualified, bare, hasStar, hasSubquery);
            }
            collectExprColumns(agg->havingExpr.get(), qualified, bare, hasStar, hasSubquery);
            if (!agg->orderByExpr.empty())
            {
                addColumnName(agg->orderByExpr, qualified, bare, hasStar);
            }
            break;
        }
        case PlanType::SORT:
        {
            const auto *sort = static_cast<const SortNode *>(node);
            addColumnName(sort->orderByColumn, qualified, bare, hasStar);
            break;
        }
        case PlanType::JOIN:
        {
            const auto *join = static_cast<const JoinNode *>(node);
            collectExprColumns(join->condition.get(), qualified, bare, hasStar, hasSubquery);
            break;
        }
        case PlanType::DISTINCT:
        case PlanType::LIMIT:
            break;
        }

        for (const auto &child : node->children)
        {
            collectPlanColumns(child.get(), qualified, bare, hasStar, hasSubquery);
        }
    }

    void applyPruningToSeqScans(
        PlanNode *node,
        const std::unordered_map<std::string, std::unordered_set<std::string>> &qualified,
        const std::unordered_set<std::string> &bare)
    {
        if (node == nullptr)
        {
            return;
        }

        if (node->type == PlanType::SEQ_SCAN)
        {
            auto *scan = static_cast<SeqScanNode *>(node);
            std::unordered_set<std::string> cols;

            const auto qIt = qualified.find(scan->outputQualifier);
            if (qIt != qualified.end())
            {
                cols.insert(qIt->second.begin(), qIt->second.end());
            }

            cols.insert(bare.begin(), bare.end());

            if (!cols.empty())
            {
                scan->requiredColumns.assign(cols.begin(), cols.end());
                std::sort(scan->requiredColumns.begin(), scan->requiredColumns.end());
            }
        }
        else if (node->type == PlanType::INDEX_SCAN)
        {
            auto *scan = static_cast<IndexScanNode *>(node);
            std::unordered_set<std::string> cols;

            const auto qIt = qualified.find(scan->outputQualifier);
            if (qIt != qualified.end())
            {
                cols.insert(qIt->second.begin(), qIt->second.end());
            }

            cols.insert(bare.begin(), bare.end());

            if (!cols.empty())
            {
                scan->requiredColumns.assign(cols.begin(), cols.end());
                std::sort(scan->requiredColumns.begin(), scan->requiredColumns.end());
            }
        }

        for (auto &child : node->children)
        {
            applyPruningToSeqScans(child.get(), qualified, bare);
        }
    }

    bool extractLookupEquality(const Expr *expr, const SeqScanNode *scan, std::string &outColumn,
                               std::string &outValue)
    {
        if (expr == nullptr || scan == nullptr)
        {
            return false;
        }

        const auto *bin = dynamic_cast<const BinaryExpr *>(expr);
        if (bin == nullptr || (bin->op != "=" && bin->op != "=="))
        {
            return false;
        }

        const auto *leftCol = dynamic_cast<const Column *>(bin->left.get());
        const auto *rightCol = dynamic_cast<const Column *>(bin->right.get());
        const auto *leftLit = dynamic_cast<const Literal *>(bin->left.get());
        const auto *rightLit = dynamic_cast<const Literal *>(bin->right.get());

        auto normalizeCol = [&](const std::string &name, std::string &normalized) -> bool
        {
            const auto dot = name.find('.');
            if (dot == std::string::npos)
            {
                normalized = name;
                return true;
            }
            if (dot == 0 || dot + 1 >= name.size())
            {
                return false;
            }
            const std::string qualifier = name.substr(0, dot);
            if (qualifier != scan->outputQualifier)
            {
                return false;
            }
            normalized = name.substr(dot + 1);
            return true;
        };

        if (leftCol != nullptr && rightLit != nullptr && rightCol == nullptr)
        {
            if (!normalizeCol(leftCol->name, outColumn))
            {
                return false;
            }
            outValue = rightLit->value;
            return true;
        }

        if (rightCol != nullptr && leftLit != nullptr && leftCol == nullptr)
        {
            if (!normalizeCol(rightCol->name, outColumn))
            {
                return false;
            }
            outValue = leftLit->value;
            return true;
        }

        return false;
    }

}

std::unique_ptr<PlanNode> PlanOptimizer::optimize(std::unique_ptr<PlanNode> root, const Database *db) const
{
    if (!root)
    {
        return root;
    }

    root = foldAndEliminate(std::move(root));
    root = combineFilters(std::move(root));
    root = pushdownPredicates(std::move(root));
    applyColumnPruning(root.get());
    root = chooseAccessPaths(std::move(root), db);
    return root;
}

std::unique_ptr<PlanNode> PlanOptimizer::chooseAccessPaths(std::unique_ptr<PlanNode> node, const Database *db) const
{
    if (!node)
    {
        return node;
    }

    for (auto &child : node->children)
    {
        child = chooseAccessPaths(std::move(child), db);
    }

    if (db == nullptr || node->type != PlanType::SEQ_SCAN)
    {
        return node;
    }

    auto *scan = static_cast<SeqScanNode *>(node.get());
    if (scan->alwaysEmpty)
    {
        return node;
    }
    if (!scan->pushedPredicate || containsSubquery(scan->pushedPredicate.get()))
    {
        return node;
    }
    if (!db->hasTable(scan->table))
    {
        return node;
    }

    const std::size_t rowCount = db->getTable(scan->table).size();
    if (rowCount < 256U)
    {
        return node;
    }

    std::string lookupColumn;
    std::string lookupValue;
    if (!extractLookupEquality(scan->pushedPredicate.get(), scan, lookupColumn, lookupValue))
    {
        return node;
    }

    auto indexNode = std::make_unique<IndexScanNode>(scan->table, scan->outputQualifier, lookupColumn, lookupValue);
    indexNode->requiredColumns = scan->requiredColumns;
    indexNode->alwaysEmpty = scan->alwaysEmpty;
    return indexNode;
}

std::unique_ptr<PlanNode> PlanOptimizer::foldAndEliminate(std::unique_ptr<PlanNode> node) const
{
    if (!node)
    {
        return node;
    }

    for (auto &child : node->children)
    {
        child = foldAndEliminate(std::move(child));
    }

    if (node->type != PlanType::FILTER)
    {
        return node;
    }

    auto *filter = static_cast<FilterNode *>(node.get());
    filter->predicate = simplifyExpr(std::move(filter->predicate));

    const auto pred = tryEvalConstPredicate(filter->predicate.get());
    if (!pred.has_value())
    {
        return node;
    }

    if (filter->children.empty())
    {
        return node;
    }

    std::unique_ptr<PlanNode> child = std::move(filter->children[0]);

    if (*pred)
    {
        return child;
    }

    if (child && child->type == PlanType::SEQ_SCAN)
    {
        auto *scan = static_cast<SeqScanNode *>(child.get());
        scan->alwaysEmpty = true;
        return child;
    }

    // Keep an always-false filter for non-scan child shapes.
    filter->children.clear();
    filter->children.push_back(std::move(child));
    filter->predicate = makeBoolLiteral(false);
    return node;
}

std::unique_ptr<PlanNode> PlanOptimizer::combineFilters(std::unique_ptr<PlanNode> node) const
{
    if (!node)
    {
        return node;
    }

    for (auto &child : node->children)
    {
        child = combineFilters(std::move(child));
    }

    if (node->type != PlanType::FILTER)
    {
        return node;
    }

    auto *upper = static_cast<FilterNode *>(node.get());
    if (upper->children.size() != 1 || !upper->children[0] ||
        upper->children[0]->type != PlanType::FILTER)
    {
        return node;
    }

    std::unique_ptr<PlanNode> lowerNode = std::move(upper->children[0]);
    auto *lower = static_cast<FilterNode *>(lowerNode.get());

    if (lower->children.size() != 1 || !lower->children[0])
    {
        return node;
    }

    auto combined = std::make_unique<BinaryExpr>(std::move(lower->predicate), "AND", std::move(upper->predicate));

    auto merged = std::make_unique<FilterNode>(std::move(combined));
    merged->children.push_back(std::move(lower->children[0]));
    return merged;
}

std::unique_ptr<PlanNode> PlanOptimizer::pushdownPredicates(std::unique_ptr<PlanNode> node) const
{
    if (!node)
    {
        return node;
    }

    for (auto &child : node->children)
    {
        child = pushdownPredicates(std::move(child));
    }

    if (node->type != PlanType::FILTER)
    {
        return node;
    }

    auto *filter = static_cast<FilterNode *>(node.get());

    // Try to push side-specific conjuncts through a JOIN child.
    if (filter->children.size() == 1 && filter->children[0] &&
        filter->children[0]->type == PlanType::JOIN && filter->predicate)
    {
        auto joinNode = std::move(filter->children[0]);
        auto *join = static_cast<JoinNode *>(joinNode.get());

        std::unordered_set<std::string> leftQualifiers;
        std::unordered_set<std::string> rightQualifiers;
        collectOutputQualifiers(join->children[0].get(), leftQualifiers);
        collectOutputQualifiers(join->children[1].get(), rightQualifiers);

        std::vector<std::unique_ptr<Expr>> conjuncts;
        flattenAndConjuncts(std::move(filter->predicate), conjuncts);

        std::vector<std::unique_ptr<Expr>> pushLeft;
        std::vector<std::unique_ptr<Expr>> pushRight;
        std::vector<std::unique_ptr<Expr>> keepAbove;
        pushLeft.reserve(conjuncts.size());
        pushRight.reserve(conjuncts.size());
        keepAbove.reserve(conjuncts.size());

        for (auto &c : conjuncts)
        {
            const PredicateSide side =
                classifyPredicateSide(c.get(), leftQualifiers, rightQualifiers);

            if (side == PredicateSide::LEFT &&
                (join->joinType == JoinType::INNER || join->joinType == JoinType::LEFT ||
                 join->joinType == JoinType::CROSS))
            {
                pushLeft.push_back(std::move(c));
                continue;
            }

            if (side == PredicateSide::RIGHT &&
                (join->joinType == JoinType::INNER || join->joinType == JoinType::RIGHT ||
                 join->joinType == JoinType::CROSS))
            {
                pushRight.push_back(std::move(c));
                continue;
            }

            keepAbove.push_back(std::move(c));
        }

        if (!pushLeft.empty())
        {
            attachFilterToChild(join->children[0], makeAndChain(std::move(pushLeft)));
        }
        if (!pushRight.empty())
        {
            attachFilterToChild(join->children[1], makeAndChain(std::move(pushRight)));
        }

        // Newly attached child filters should continue through the standard
        // pushdown path (e.g. Filter -> SeqScan predicate injection).
        join->children[0] = pushdownPredicates(std::move(join->children[0]));
        join->children[1] = pushdownPredicates(std::move(join->children[1]));

        if (keepAbove.empty())
        {
            return joinNode;
        }

        filter->children.clear();
        filter->children.push_back(std::move(joinNode));
        filter->predicate = makeAndChain(std::move(keepAbove));
    }

    if (filter->children.size() != 1 || !filter->children[0] ||
        filter->children[0]->type != PlanType::SEQ_SCAN)
    {
        return node;
    }

    if (containsSubquery(filter->predicate.get()))
    {
        return node;
    }

    std::unique_ptr<PlanNode> child = std::move(filter->children[0]);
    auto *scan = static_cast<SeqScanNode *>(child.get());

    if (scan->pushedPredicate)
    {
        scan->pushedPredicate = std::make_unique<BinaryExpr>(std::move(scan->pushedPredicate), "AND", std::move(filter->predicate));
    }
    else
    {
        scan->pushedPredicate = std::move(filter->predicate);
    }

    return child;
}

void PlanOptimizer::applyColumnPruning(PlanNode *root) const
{
    if (root == nullptr)
    {
        return;
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> qualified;
    std::unordered_set<std::string> bare;
    bool hasStar = false;
    bool hasSubquery = false;

    collectPlanColumns(root, qualified, bare, hasStar, hasSubquery);
    if (hasStar || hasSubquery)
    {
        return;
    }

    applyPruningToSeqScans(root, qualified, bare);
}
