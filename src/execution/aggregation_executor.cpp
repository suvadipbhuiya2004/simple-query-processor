#include "execution/aggregation_executor.hpp"
#include "execution/expression.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{

    enum class AggFn
    {
        COUNT,
        SUM,
        AVG,
        MIN,
        MAX,
        COUNT_DISTINCT
    };

    struct AggregateSpec
    {
        AggFn fn;
        std::string arg;
        std::vector<std::string> args;
        std::string outputName;
    };

    struct AggregateState
    {
        std::int64_t count{0};
        long double sum{0};
        bool hasValue{false};
        std::string minValue;
        std::string maxValue;
        std::unordered_set<std::string> distinct;
    };

    struct GroupState
    {
        Row exemplar;
        std::vector<AggregateState> states;
    };

    bool tryParseNumeric(const std::string &s, long double &out) noexcept
    {
        if (s.empty())
            return false;
        char *end = nullptr;
        const long double v = std::strtold(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0')
            return false;
        out = v;
        return true;
    }

    int compareValues(const std::string &a, const std::string &b)
    {
        long double an = 0;
        long double bn = 0;
        const bool aNum = tryParseNumeric(a, an);
        const bool bNum = tryParseNumeric(b, bn);
        if (aNum && bNum)
            return (an < bn) ? -1 : ((an > bn) ? 1 : 0);
        if (aNum != bNum)
            return aNum ? -1 : 1;
        const int c = a.compare(b);
        return (c < 0) ? -1 : ((c > 0) ? 1 : 0);
    }

    std::string toNumericString(long double v)
    {
        if (std::isfinite(v) && std::fabsl(v - std::llround(v)) < 1e-12L)
            return std::to_string(static_cast<long long>(std::llround(v)));

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << static_cast<double>(v);
        std::string out = oss.str();
        while (!out.empty() && out.back() == '0')
            out.pop_back();
        if (!out.empty() && out.back() == '.')
            out.pop_back();
        return out.empty() ? "0" : out;
    }

    std::vector<std::string> splitArguments(const std::string &argStr)
    {
        std::vector<std::string> result;
        std::string current;
        for (char c : argStr)
        {
            if (c == ',')
            {
                if (!current.empty())
                {
                    const auto start = current.find_first_not_of(" \t");
                    const auto end = current.find_last_not_of(" \t");
                    if (start != std::string::npos)
                        result.push_back(current.substr(start, end - start + 1));
                    current.clear();
                }
            }
            else
            {
                current += c;
            }
        }
        if (!current.empty())
        {
            const auto start = current.find_first_not_of(" \t");
            const auto end = current.find_last_not_of(" \t");
            if (start != std::string::npos)
                result.push_back(current.substr(start, end - start + 1));
        }
        return result;
    }

    bool parseAggregateRef(const std::string &name, AggregateSpec &spec)
    {
        const auto lp = name.find('(');
        const auto rp = name.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp + 1)
            return false;

        const std::string fn = name.substr(0, lp);
        const std::string arg = name.substr(lp + 1, rp - lp - 1);
        if (arg.empty())
            return false;

        if (fn == "COUNT")
            spec.fn = AggFn::COUNT;
        else if (fn == "SUM")
            spec.fn = AggFn::SUM;
        else if (fn == "AVG")
            spec.fn = AggFn::AVG;
        else if (fn == "MIN")
            spec.fn = AggFn::MIN;
        else if (fn == "MAX")
            spec.fn = AggFn::MAX;
        else if (fn == "COUNT_DISTINCT")
            spec.fn = AggFn::COUNT_DISTINCT;
        else
            return false;

        spec.arg = arg;
        spec.args = splitArguments(arg);
        spec.outputName = name;
        return true;
    }

    void collectAggregateRefs(const Expr *expr, std::vector<AggregateSpec> &specs, std::unordered_set<std::string> &seen)
    {
        if (!expr)
            return;
        if (const auto *col = dynamic_cast<const Column *>(expr))
        {
            AggregateSpec spec;
            if (parseAggregateRef(col->name, spec) && seen.insert(spec.outputName).second)
                specs.push_back(std::move(spec));
            return;
        }
        if (const auto *bin = dynamic_cast<const BinaryExpr *>(expr))
        {
            collectAggregateRefs(bin->left.get(), specs, seen);
            collectAggregateRefs(bin->right.get(), specs, seen);
        }
    }

    const std::string &resolveColumnRef(const Row &row, const std::string &name)
    {
        const auto it = row.find(name);
        if (it == row.end())
            throw std::runtime_error("AggregationExecutor: unknown column in aggregate: '" + name + "'");
        return it->second;
    }

    std::string finalizeAggregate(const AggregateSpec &spec, const AggregateState &state)
    {
        switch (spec.fn)
        {
        case AggFn::COUNT:
            return std::to_string(state.count);
        case AggFn::COUNT_DISTINCT:
            return std::to_string(state.distinct.size());
        case AggFn::SUM:
            return toNumericString(state.sum);
        case AggFn::AVG:
            return state.count == 0 ? "0" : toNumericString(state.sum / static_cast<long double>(state.count));
        case AggFn::MIN:
            return state.hasValue ? state.minValue : "";
        case AggFn::MAX:
            return state.hasValue ? state.maxValue : "";
        }
        throw std::runtime_error("AggregationExecutor: unsupported aggregate function");
    }

}

// AggregationExecutor
AggregationExecutor::AggregationExecutor(std::unique_ptr<Executor> child, const AggregationNode *node) : child_(std::move(child)), node_(node) {
    if (!child_) throw std::runtime_error("AggregationExecutor: null child executor");
    if (!node_)  throw std::runtime_error("AggregationExecutor: null plan node");
}

void AggregationExecutor::open() {
    child_->open();
    results_.clear();
    cursor_ = 0;

    std::vector<AggregateSpec> specs;
    std::unordered_set<std::string> seenSpec;
    specs.reserve(node_->selectExprs.size() + 2);
    for (const auto &expr : node_->selectExprs)
        collectAggregateRefs(expr.get(), specs, seenSpec);
    collectAggregateRefs(node_->havingExpr.get(), specs, seenSpec);
    if (!node_->orderByExpr.empty())
    {
        AggregateSpec orderSpec;
        if (parseAggregateRef(node_->orderByExpr, orderSpec) && seenSpec.insert(orderSpec.outputName).second)
            specs.push_back(std::move(orderSpec));
    }

    std::unordered_map<std::string, std::size_t> groupIndex;
    std::vector<GroupState> groups;
    groups.reserve(64);

    Row row;
    while (child_->next(row)) {
        const std::string key = node_->groupExprs.empty() ? std::string("__global_group__") : buildGroupKey(row);
        auto [it, inserted] = groupIndex.emplace(key, groups.size());
        if (inserted) {
            GroupState gs;
            gs.exemplar = row;
            gs.states.resize(specs.size());
            groups.push_back(std::move(gs));
        }

        GroupState &group = groups[it->second];
        for (std::size_t i = 0; i < specs.size(); ++i)
        {
            const auto &spec = specs[i];
            auto &state = group.states[i];

            if (spec.fn == AggFn::COUNT)
            {
                if (spec.arg == "*")
                {
                    ++state.count;
                }
                else
                {
                    (void)resolveColumnRef(row, spec.arg);
                    ++state.count;
                }
                continue;
            }

            if (spec.fn == AggFn::COUNT_DISTINCT)
            {
                if (spec.arg == "*")
                    throw std::runtime_error("COUNT_DISTINCT(*) is not supported");
                std::string key;
                if (spec.args.size() == 1)
                {
                    key = resolveColumnRef(row, spec.args[0]);
                }
                else
                {
                    for (std::size_t j = 0; j < spec.args.size(); ++j)
                    {
                        if (j > 0) key += "\x1f";
                        key += resolveColumnRef(row, spec.args[j]);
                    }
                }
                state.distinct.insert(key);
                continue;
            }

            const std::string &v = resolveColumnRef(row, spec.arg);

            if (spec.fn == AggFn::MIN)
            {
                // Skip empty strings (treat as NULL)
                if (!v.empty() && (!state.hasValue || compareValues(v, state.minValue) < 0))
                {
                    state.minValue = v;
                    state.hasValue = true;
                }
                continue;
            }

            if (spec.fn == AggFn::MAX)
            {
                // Skip empty strings (treat as NULL)
                if (!v.empty() && (!state.hasValue || compareValues(v, state.maxValue) > 0))
                {
                    state.maxValue = v;
                    state.hasValue = true;
                }
                continue;
            }

            // For SUM and AVG, skip empty strings (treat as NULL)
            if (v.empty())
                continue;

            long double num = 0;
            if (!tryParseNumeric(v, num))
                throw std::runtime_error("Non-numeric value for aggregate on column '" + spec.arg + "': '" + v + "'");
            state.sum += num;
            ++state.count;
            state.hasValue = true;
        }
    }

    if (groups.empty() && node_->groupExprs.empty() && !specs.empty())
    {
        GroupState gs;
        gs.states.resize(specs.size());
        groups.push_back(std::move(gs));
    }

    results_.reserve(groups.size());
    for (auto &group : groups)
    {
        Row out = std::move(group.exemplar);
        for (std::size_t i = 0; i < specs.size(); ++i)
            out[specs[i].outputName] = finalizeAggregate(specs[i], group.states[i]);
        results_.push_back(std::move(out));
    }

    //  apply HAVING filter
    if (node_->havingExpr) {
        auto it = results_.begin();
        while (it != results_.end()) {
            if (!ExpressionEvaluator::evalPredicate(node_->havingExpr.get(), *it)) {
                it = results_.erase(it);
            } 
            else {
                ++it;
            }
        }
    }
}

bool AggregationExecutor::next(Row& row) {
    if (cursor_ >= results_.size()) return false;
    row = std::move(results_[cursor_++]);
    return true;
}

void AggregationExecutor::close() {
    child_->close();
    results_.clear();
    cursor_ = 0;
}

std::string AggregationExecutor::buildGroupKey(const Row& row) const {
    std::string key;
    for (const auto& expr : node_->groupExprs) {
        key += ExpressionEvaluator::eval(expr.get(), row);
        key += '\x1f';
    }
    return key;
}