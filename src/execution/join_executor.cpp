#include "execution/join_executor.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace
{

    // Returns true if `name` contains a '.', meaning it is already qualified
    bool isQualified(const std::string &name) noexcept
    {
        return name.find('.') != std::string::npos;
    }

    // Numeric values sort before non-numeric ones for mixed-type stability.
    bool tryParseInt64(const std::string &s, std::int64_t &out) noexcept
    {
        if (s.empty())
            return false;
        char *end = nullptr;
        errno = 0;
        const long long n = std::strtoll(s.c_str(), &end, 10);
        if (s.c_str() == end || *end != '\0' || errno == ERANGE)
            return false;
        out = static_cast<std::int64_t>(n);
        return true;
    }

    int cmpValues(const std::string &l, const std::string &r) noexcept
    {
        std::int64_t li = 0, ri = 0;
        const bool ln = tryParseInt64(l, li);
        const bool rn = tryParseInt64(r, ri);
        if (ln && rn)
            return (li < ri) ? -1 : (li > ri) ? 1 : 0;
        if (ln != rn)
            return ln ? -1 : 1; // numeric < non-numeric
        const int c = l.compare(r);
        return (c < 0) ? -1 : (c > 0) ? 1 : 0;
    }

    // Resolve a column name from a row:
    //   1. Exact match in the row map.
    //   2. Bare-name → qualified lookup via `bareMap` (unambiguous only).
    const std::string &resolveColumn(const Row &row, const std::string &col, const std::unordered_map<std::string, std::string> &bareMap)
    {
        const auto it = row.find(col);
        if (it != row.end())
            return it->second;
        if (!isQualified(col))
        {
            const auto mapIt = bareMap.find(col);
            if (mapIt != bareMap.end())
            {
                const auto rowIt = row.find(mapIt->second);
                if (rowIt != row.end())
                    return rowIt->second;
            }
        }
        throw std::runtime_error("JoinExecutor: unknown column: " + col);
    }

    // Sentinel empty string returned for null-side columns.
    static const std::string kEmpty;

    bool isMonotonicNonDecreasing(const std::vector<Row> &rows, const std::string &key, const std::unordered_map<std::string, std::string> &bareMap)
    {
        if (rows.size() < 2)
            return true;

        const std::string *prev = &resolveColumn(rows[0], key, bareMap);
        for (std::size_t i = 1; i < rows.size(); ++i)
        {
            const std::string &cur = resolveColumn(rows[i], key, bareMap);
            if (cmpValues(*prev, cur) > 0)
                return false;
            prev = &cur;
        }
        return true;
    }

}

// Constructor
JoinExecutor::JoinExecutor(std::unique_ptr<Executor> left, std::unique_ptr<Executor> right, JoinType joinType, JoinAlgorithm algorithm, std::unique_ptr<Expr> condition, std::vector<std::string> leftQualifiedColumns, std::vector<std::string> rightQualifiedColumns)
    : left_(std::move(left)), right_(std::move(right)), joinType_(joinType), algorithm_(algorithm),
      condition_(std::move(condition)), leftColumns_(std::move(leftQualifiedColumns)),
      rightColumns_(std::move(rightQualifiedColumns))
{
    if (!left_)
        throw std::runtime_error("JoinExecutor: null left executor");
    if (!right_)
        throw std::runtime_error("JoinExecutor: null right executor");
    if (leftColumns_.empty())
        throw std::runtime_error("JoinExecutor: empty left schema");
    if (rightColumns_.empty())
        throw std::runtime_error("JoinExecutor: empty right schema");

    leftColumnSet_.reserve(leftColumns_.size() * 2U + 1U);
    rightColumnSet_.reserve(rightColumns_.size() * 2U + 1U);
    uniqueBareToQualified_.reserve(leftColumns_.size() + rightColumns_.size());
    qualifiedToBare_.reserve(leftColumns_.size() + rightColumns_.size());

    leftColumnSet_.insert(leftColumns_.begin(), leftColumns_.end());
    rightColumnSet_.insert(rightColumns_.begin(), rightColumns_.end());
    buildUniqueBareNameMap();

    for (const auto &[bare, qualified] : uniqueBareToQualified_)
        qualifiedToBare_.emplace(qualified, bare);

    mergedReserve_ = leftColumns_.size() + rightColumns_.size() + qualifiedToBare_.size();

    if (condition_ && !exprContainsSubquery(condition_.get()))
    {
        compiledCondition_ = CompiledPredicate::compile(condition_.get());
        hasCompiledCondition_ = true;
    }
}

// buildUniqueBareNameMap — single pass, no temporary maps
void JoinExecutor::buildUniqueBareNameMap()
{
    uniqueBareToQualified_.clear();
    std::string scratch;

    auto process = [&](const std::vector<std::string> &cols)
    {
        for (const auto &q : cols)
        {
            const std::size_t dot = q.rfind('.');
            if (dot == std::string::npos)
                continue; // already bare
            const std::string bare = q.substr(dot + 1);
            const auto [it, inserted] = uniqueBareToQualified_.emplace(bare, q);
            if (!inserted)
            {
                // Ambiguous — mark with empty string so we know to skip it.
                it->second.clear();
            }
        }
    };

    process(leftColumns_);
    process(rightColumns_);

    // Remove ambiguous entries (value == "").
    for (auto it = uniqueBareToQualified_.begin(); it != uniqueBareToQualified_.end();)
    {
        if (it->second.empty())
            it = uniqueBareToQualified_.erase(it);
        else
            ++it;
    }
}

Row JoinExecutor::makeNullSide(const std::vector<std::string> &cols) const
{
    Row r;
    r.reserve(cols.size());
    for (const auto &c : cols)
        r.emplace(c, std::string{});
    return r;
}

// mergeRows: copies left and right values into a single output row.
// Also inserts unambiguous bare-name aliases so WHERE/SELECT expressions
// that use bare names work on the merged row.
Row JoinExecutor::mergeRows(const Row &leftRow, const Row &rightRow) const
{
    Row merged;
    merged.reserve(mergedReserve_);

    for (const auto &col : leftColumns_)
    {
        const auto it = leftRow.find(col);
        const std::string &value = (it != leftRow.end()) ? it->second : kEmpty;
        merged.emplace(col, value);

        const auto alias = qualifiedToBare_.find(col);
        if (alias != qualifiedToBare_.end())
            merged.emplace(alias->second, value);
    }
    for (const auto &col : rightColumns_)
    {
        const auto it = rightRow.find(col);
        const std::string &value = (it != rightRow.end()) ? it->second : kEmpty;
        merged.emplace(col, value);

        const auto alias = qualifiedToBare_.find(col);
        if (alias != qualifiedToBare_.end())
            merged.emplace(alias->second, value);
    }
    return merged;
}

bool JoinExecutor::matchesCondition(const Row &merged) const
{
    if (hasCompiledCondition_)
        return compiledCondition_.evaluatePredicate(merged);
    return ExpressionEvaluator::evalPredicate(condition_.get(), merged);
}

const std::string &JoinExecutor::getColumnValue(const Row &row, const std::string &col) const
{
    return resolveColumn(row, col, uniqueBareToQualified_);
}

// extractEquiJoinKeys
bool JoinExecutor::extractEquiJoinKeys(std::string &leftKey, std::string &rightKey) const
{
    if (!condition_)
        return false;
    const auto *expr = dynamic_cast<const BinaryExpr *>(condition_.get());
    if (!expr || (expr->op != "=" && expr->op != "=="))
        return false;

    const auto *lc = dynamic_cast<const Column *>(expr->left.get());
    const auto *rc = dynamic_cast<const Column *>(expr->right.get());
    if (!lc || !rc)
        return false;

    // Resolve each column name to LEFT or RIGHT side.
    auto side = [&](const std::string &col) -> int
    {
        if (leftColumnSet_.count(col))
            return 0; // LEFT
        if (rightColumnSet_.count(col))
            return 1; // RIGHT
        // Try bare-name resolution.
        const auto it = uniqueBareToQualified_.find(col);
        if (it != uniqueBareToQualified_.end())
        {
            if (leftColumnSet_.count(it->second))
                return 0;
            if (rightColumnSet_.count(it->second))
                return 1;
        }
        return -1; // unknown
    };

    const int ls = side(lc->name);
    const int rs = side(rc->name);

    if (ls == 0 && rs == 1)
    {
        leftKey = lc->name;
        rightKey = rc->name;
        return true;
    }
    if (ls == 1 && rs == 0)
    {
        leftKey = rc->name;
        rightKey = lc->name;
        return true;
    }
    return false;
}

// NESTED LOOP JOIN — O(n*m), supports any join condition
void JoinExecutor::runNestedLoopJoin(const std::vector<Row> &leftRows, const std::vector<Row> &rightRows)
{
    const Row nullLeft = makeNullSide(leftColumns_);
    const Row nullRight = makeNullSide(rightColumns_);

    // For RIGHT/FULL we need to track which right rows were ever matched.
    const bool needRightTracking = (joinType_ == JoinType::RIGHT || joinType_ == JoinType::FULL);
    std::vector<bool> rightMatched(needRightTracking ? rightRows.size() : 0, false);

    for (const auto &lRow : leftRows)
    {
        bool lMatched = false;

        for (std::size_t ri = 0; ri < rightRows.size(); ++ri)
        {
            Row merged = mergeRows(lRow, rightRows[ri]);
            if (matchesCondition(merged))
            {
                lMatched = true;
                if (needRightTracking)
                    rightMatched[ri] = true;
                outputRows_.push_back(std::move(merged));
            }
        }

        if (!lMatched && (joinType_ == JoinType::LEFT || joinType_ == JoinType::FULL))
        {
            outputRows_.push_back(mergeRows(lRow, nullRight));
        }
    }

    if (needRightTracking)
    {
        for (std::size_t ri = 0; ri < rightRows.size(); ++ri)
        {
            if (!rightMatched[ri])
            {
                outputRows_.push_back(mergeRows(nullLeft, rightRows[ri]));
            }
        }
    }
}

// HASH JOIN — O(n+m) average for equi-joins
//
// Optimisations vs original:
//   1. Build hash table on the SMALLER side to minimise memory.
//   2. For pure equi-joins, skip redundant matchesCondition() after bucket hit.
//   3. Falls back to nested-loop for CROSS or non-equi conditions.
void JoinExecutor::runHashJoin(const std::vector<Row> &leftRows, const std::vector<Row> &rightRows)
{
    std::string leftKey, rightKey;
    if (joinType_ == JoinType::CROSS || !extractEquiJoinKeys(leftKey, rightKey))
    {
        runNestedLoopJoin(leftRows, rightRows);
        return;
    }

    // Choose build side: outer-join semantics constrain which side is "probe"
    // (the preserved side). For INNER, pick the smaller side as build.
    const bool buildOnRight = (joinType_ == JoinType::LEFT || joinType_ == JoinType::FULL) || (joinType_ == JoinType::INNER && rightRows.size() <= leftRows.size());

    const std::vector<Row> &buildSide = buildOnRight ? rightRows : leftRows;
    const std::vector<Row> &probeSide = buildOnRight ? leftRows : rightRows;
    const std::string &buildKey = buildOnRight ? rightKey : leftKey;
    const std::string &probeKey = buildOnRight ? leftKey : rightKey;

    // Detect pure equi condition → skip redundant evalPredicate.
    const bool isPureEqui = [&]() -> bool
    {
        if (!condition_)
            return true;
        const auto *b = dynamic_cast<const BinaryExpr *>(condition_.get());
        if (!b || (b->op != "=" && b->op != "=="))
            return false;
        return dynamic_cast<const Column *>(b->left.get()) != nullptr &&
               dynamic_cast<const Column *>(b->right.get()) != nullptr;
    }();

    // Build phase
    std::unordered_map<std::string, std::vector<std::size_t>> buckets;
    buckets.reserve(buildSide.size() * 2u + 1u);
    for (std::size_t i = 0; i < buildSide.size(); ++i)
        buckets[getColumnValue(buildSide[i], buildKey)].push_back(i);

    const bool needBuildTracking = (joinType_ == JoinType::RIGHT || joinType_ == JoinType::FULL);
    std::vector<bool> buildMatched(needBuildTracking ? buildSide.size() : 0, false);

    const Row nullBuild = makeNullSide(buildOnRight ? rightColumns_ : leftColumns_);
    const Row nullProbe = makeNullSide(buildOnRight ? leftColumns_ : rightColumns_);

    // Probe phase
    for (const auto &pRow : probeSide)
    {
        bool pMatched = false;
        const auto bucketIt = buckets.find(getColumnValue(pRow, probeKey));

        if (bucketIt != buckets.end())
        {
            for (const std::size_t bi : bucketIt->second)
            {
                Row merged =
                    buildOnRight ? mergeRows(pRow, buildSide[bi]) : mergeRows(buildSide[bi], pRow);

                if (!isPureEqui && !matchesCondition(merged))
                    continue;

                pMatched = true;
                if (needBuildTracking)
                    buildMatched[bi] = true;
                outputRows_.push_back(std::move(merged));
            }
        }

        if (!pMatched)
        {
            const bool emitOuter =
                (buildOnRight && (joinType_ == JoinType::LEFT || joinType_ == JoinType::FULL)) ||
                (!buildOnRight && (joinType_ == JoinType::RIGHT || joinType_ == JoinType::FULL));
            if (emitOuter)
            {
                outputRows_.push_back(buildOnRight ? mergeRows(pRow, nullBuild)
                                                   : mergeRows(nullBuild, pRow));
            }
        }
    }

    // Emit unmatched build-side rows for RIGHT/FULL
    if (needBuildTracking)
    {
        for (std::size_t bi = 0; bi < buildSide.size(); ++bi)
        {
            if (!buildMatched[bi])
            {
                outputRows_.push_back(buildOnRight ? mergeRows(nullProbe, buildSide[bi])
                                                   : mergeRows(buildSide[bi], nullProbe));
            }
        }
    }
}

// --------------------------------------------------------------------------
// MERGE JOIN — O((n+m) log(n+m))
//
// Optimisations vs original:
//   1. Key values extracted into flat arrays before sorting — eliminates
//      O(n log n) unordered_map::find calls inside the comparator lambdas.
//   2. Unmatched right rows in the cmp>0 branch are emitted immediately
//      (marked rightMatched=true), saving a full O(m) pass at the end.
//   3. Falls back to nested-loop for CROSS or non-equi conditions.
// --------------------------------------------------------------------------
void JoinExecutor::runMergeJoin(const std::vector<Row> &leftRows,
                                const std::vector<Row> &rightRows)
{
    std::string leftKey, rightKey;
    if (joinType_ == JoinType::CROSS || !extractEquiJoinKeys(leftKey, rightKey))
    {
        runNestedLoopJoin(leftRows, rightRows);
        return;
    }

    const Row nullLeft = makeNullSide(leftColumns_);
    const Row nullRight = makeNullSide(rightColumns_);

    // Extract key values once — avoids repeated map lookups in comparators.
    std::vector<std::string> lKeys(leftRows.size());
    std::vector<std::string> rKeys(rightRows.size());
    for (std::size_t i = 0; i < leftRows.size(); ++i)
        lKeys[i] = getColumnValue(leftRows[i], leftKey);
    for (std::size_t i = 0; i < rightRows.size(); ++i)
        rKeys[i] = getColumnValue(rightRows[i], rightKey);

    // Sort-order index arrays
    std::vector<std::size_t> li(leftRows.size()), ri(rightRows.size());
    std::iota(li.begin(), li.end(), 0u);
    std::iota(ri.begin(), ri.end(), 0u);
    std::stable_sort(li.begin(), li.end(), [&](std::size_t a, std::size_t b)
                     { return cmpValues(lKeys[a], lKeys[b]) < 0; });
    std::stable_sort(ri.begin(), ri.end(), [&](std::size_t a, std::size_t b)
                     { return cmpValues(rKeys[a], rKeys[b]) < 0; });

    const bool needRightTracking = (joinType_ == JoinType::RIGHT || joinType_ == JoinType::FULL);
    std::vector<bool> rightMatched(needRightTracking ? rightRows.size() : 0, false);

    std::size_t i = 0, j = 0;
    while (i < li.size() && j < ri.size())
    {
        const std::string &lv = lKeys[li[i]];
        const std::string &rv = rKeys[ri[j]];
        const int cmp = cmpValues(lv, rv);

        if (cmp == 0)
        {
            // Find equal-key group extents
            std::size_t i2 = i;
            while (i2 < li.size() && cmpValues(lKeys[li[i2]], lv) == 0)
                ++i2;
            std::size_t j2 = j;
            while (j2 < ri.size() && cmpValues(rKeys[ri[j2]], rv) == 0)
                ++j2;

            for (std::size_t ii = i; ii < i2; ++ii)
            {
                bool lMatched = false;
                for (std::size_t jj = j; jj < j2; ++jj)
                {
                    const std::size_t rIdx = ri[jj];
                    Row merged = mergeRows(leftRows[li[ii]], rightRows[rIdx]);
                    if (matchesCondition(merged))
                    {
                        lMatched = true;
                        if (needRightTracking)
                            rightMatched[rIdx] = true;
                        outputRows_.push_back(std::move(merged));
                    }
                }
                if (!lMatched && (joinType_ == JoinType::LEFT || joinType_ == JoinType::FULL))
                    outputRows_.push_back(mergeRows(leftRows[li[ii]], nullRight));
            }
            i = i2;
            j = j2;
        }
        else if (cmp < 0)
        {
            // Left row(s) with no matching right key
            if (joinType_ == JoinType::LEFT || joinType_ == JoinType::FULL)
            {
                std::size_t i2 = i;
                while (i2 < li.size() && cmpValues(lKeys[li[i2]], lv) == 0)
                    ++i2;
                for (std::size_t ii = i; ii < i2; ++ii)
                    outputRows_.push_back(mergeRows(leftRows[li[ii]], nullRight));
                i = i2;
            }
            else
            {
                ++i;
            }
        }
        else
        {
            // Right row(s) with no matching left key — emit immediately for
            // RIGHT/FULL and mark as handled to avoid double-emit at the end.
            std::size_t j2 = j;
            while (j2 < ri.size() && cmpValues(rKeys[ri[j2]], rv) == 0)
                ++j2;
            if (joinType_ == JoinType::RIGHT || joinType_ == JoinType::FULL)
            {
                for (std::size_t jj = j; jj < j2; ++jj)
                {
                    if (needRightTracking)
                        rightMatched[ri[jj]] = true;
                    outputRows_.push_back(mergeRows(nullLeft, rightRows[ri[jj]]));
                }
            }
            j = j2;
        }
    }

    // Residual left rows
    if (joinType_ == JoinType::LEFT || joinType_ == JoinType::FULL)
    {
        while (i < li.size())
            outputRows_.push_back(mergeRows(leftRows[li[i++]], nullRight));
    }

    // Residual right rows (never visited in the main loop)
    if (joinType_ == JoinType::RIGHT || joinType_ == JoinType::FULL)
    {
        while (j < ri.size())
            outputRows_.push_back(mergeRows(nullLeft, rightRows[ri[j++]]));
    }

    // RIGHT/FULL: rows that were visited but whose every match failed condition
    if (needRightTracking)
    {
        for (std::size_t rIdx = 0; rIdx < rightRows.size(); ++rIdx)
            if (!rightMatched[rIdx])
                outputRows_.push_back(mergeRows(nullLeft, rightRows[rIdx]));
    }
}

JoinAlgorithm JoinExecutor::chooseAlgorithm(const std::vector<Row> &leftRows,
                                            const std::vector<Row> &rightRows) const
{
    // Respect explicit non-default overrides from planner/tests.
    if (algorithm_ != JoinAlgorithm::HASH)
        return algorithm_;

    // CROSS joins and non-equi joins are best handled with nested-loop here.
    std::string leftKey;
    std::string rightKey;
    const bool equiJoin = extractEquiJoinKeys(leftKey, rightKey);
    if (joinType_ == JoinType::CROSS || !equiJoin)
        return JoinAlgorithm::NESTED_LOOP;

    // Prefer hash for outer equi-joins; it has stable semantics here and
    // avoids small-input nested-loop edge behavior for RIGHT/FULL joins.
    if (joinType_ != JoinType::INNER)
        return JoinAlgorithm::HASH;

    const std::size_t leftN = leftRows.size();
    const std::size_t rightN = rightRows.size();
    if (leftN == 0 || rightN == 0)
        return JoinAlgorithm::NESTED_LOOP;

    // For tiny relations, nested loop often wins due lower setup overhead.
    const std::size_t minSide = std::min(leftN, rightN);
    const std::size_t maxSafe =
        (rightN == 0) ? 0 : (std::numeric_limits<std::size_t>::max() / rightN);
    const std::size_t product =
        (leftN > maxSafe) ? std::numeric_limits<std::size_t>::max() : leftN * rightN;
    if (minSide <= 32 || product <= 4096)
        return JoinAlgorithm::NESTED_LOOP;

    // If both inputs are already ordered by join keys and reasonably large,
    // merge join avoids hash build/probe overhead.
    const bool leftSorted = isMonotonicNonDecreasing(leftRows, leftKey, uniqueBareToQualified_);
    const bool rightSorted = isMonotonicNonDecreasing(rightRows, rightKey, uniqueBareToQualified_);
    if (leftSorted && rightSorted && minSide >= 256)
        return JoinAlgorithm::MERGE;

    // Default for larger equi-joins.
    return JoinAlgorithm::HASH;
}

// Executor lifecycle
void JoinExecutor::open()
{
    outputRows_.clear();
    cursor_ = 0;

    std::vector<Row> leftRows, rightRows;
    left_->open();
    {
        Row r;
        while (left_->next(r))
            leftRows.push_back(std::move(r));
    }
    left_->close();

    right_->open();
    {
        Row r;
        while (right_->next(r))
            rightRows.push_back(std::move(r));
    }
    right_->close();

    // Reserve output buffer with a reasonable heuristic.
    outputRows_.reserve(joinType_ == JoinType::INNER ? std::min(leftRows.size(), rightRows.size()) : std::max(leftRows.size(), rightRows.size()));

    const JoinAlgorithm selected = chooseAlgorithm(leftRows, rightRows);
    switch (selected)
    {
    case JoinAlgorithm::HASH:
        runHashJoin(leftRows, rightRows);
        break;
    case JoinAlgorithm::NESTED_LOOP:
        runNestedLoopJoin(leftRows, rightRows);
        break;
    case JoinAlgorithm::MERGE:
        runMergeJoin(leftRows, rightRows);
        break;
    default:
        throw std::runtime_error("JoinExecutor: unsupported algorithm");
    }
}

bool JoinExecutor::next(Row &row)
{
    if (cursor_ >= outputRows_.size())
        return false;
    row = std::move(outputRows_[cursor_++]);
    return true;
}

void JoinExecutor::close()
{
    outputRows_.clear();
    cursor_ = 0;
}