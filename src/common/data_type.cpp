#include "common/data_type.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>

namespace
{

    std::string trim(const std::string &s)
    {
        const auto isWs = [](unsigned char c)
        { return std::isspace(c) != 0; };
        const auto b = std::find_if_not(s.begin(), s.end(), isWs);
        const auto e = std::find_if_not(s.rbegin(), s.rend(), isWs).base();
        if (b >= e)
        {
            return {};
        }
        return std::string(b, e);
    }

    std::string toUpper(std::string s)
    {
        for (char &c : s)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return s;
    }

    bool startsWith(const std::string &s, const std::string &prefix)
    {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    std::size_t parsePositiveInt(const std::string &raw, const std::string &context)
    {
        const std::string t = trim(raw);
        if (t.empty())
        {
            throw std::runtime_error("Expected positive integer in " + context);
        }
        for (char c : t)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
            {
                throw std::runtime_error("Expected positive integer in " + context + ": '" + raw + "'");
            }
        }

        const unsigned long long n = std::stoull(t);
        if (n == 0 || n > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        {
            throw std::runtime_error("Out-of-range integer in " + context + ": '" + raw + "'");
        }
        return static_cast<std::size_t>(n);
    }

    bool parseSignedInteger(const std::string &raw, long long &out) noexcept
    {
        const std::string t = trim(raw);
        if (t.empty())
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        const long long n = std::strtoll(t.c_str(), &end, 10);
        if (t.c_str() == end || *end != '\0' || errno == ERANGE)
        {
            return false;
        }
        out = n;
        return true;
    }

    bool parseFloatingNumber(const std::string &raw, long double &out) noexcept
    {
        const std::string t = trim(raw);
        if (t.empty())
        {
            return false;
        }
        errno = 0;
        char *end = nullptr;
        const long double n = std::strtold(t.c_str(), &end);
        if (t.c_str() == end || *end != '\0' || errno == ERANGE)
        {
            return false;
        }
        out = n;
        return true;
    }

    bool isLeapYear(int year)
    {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    bool isValidDate(const std::string &raw)
    {
        const std::string s = trim(raw);
        if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        {
            return false;
        }

        auto parse2 = [&](std::size_t offset) -> int
        {
            if (!std::isdigit(static_cast<unsigned char>(s[offset])) ||
                !std::isdigit(static_cast<unsigned char>(s[offset + 1])))
            {
                return -1;
            }
            return (s[offset] - '0') * 10 + (s[offset + 1] - '0');
        };

        int year = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(s[static_cast<std::size_t>(i)])))
            {
                return false;
            }
            year = year * 10 + (s[static_cast<std::size_t>(i)] - '0');
        }

        const int month = parse2(5);
        const int day = parse2(8);
        if (month < 1 || month > 12 || day < 1)
        {
            return false;
        }

        static const int daysInMonth[] = {
            31,
            28,
            31,
            30,
            31,
            30,
            31,
            31,
            30,
            31,
            30,
            31,
        };
        int maxDay = daysInMonth[month - 1];
        if (month == 2 && isLeapYear(year))
        {
            maxDay = 29;
        }

        return day <= maxDay;
    }

    bool isValidTime(const std::string &raw)
    {
        const std::string s = trim(raw);

        // Accept HH:MM or HH:MM:SS(.fraction)
        auto read2 = [&](std::size_t off) -> int
        {
            if (off + 1 >= s.size())
            {
                return -1;
            }
            if (!std::isdigit(static_cast<unsigned char>(s[off])) ||
                !std::isdigit(static_cast<unsigned char>(s[off + 1])))
            {
                return -1;
            }
            return (s[off] - '0') * 10 + (s[off + 1] - '0');
        };

        if (s.size() < 5 || s[2] != ':')
        {
            return false;
        }

        const int hh = read2(0);
        const int mm = read2(3);
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59)
        {
            return false;
        }

        if (s.size() == 5)
        {
            return true;
        }

        if (s.size() < 8 || s[5] != ':')
        {
            return false;
        }

        const int ss = read2(6);
        if (ss < 0 || ss > 59)
        {
            return false;
        }

        if (s.size() == 8)
        {
            return true;
        }

        if (s[8] != '.')
        {
            return false;
        }

        for (std::size_t i = 9; i < s.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(s[i])))
            {
                return false;
            }
        }

        return s.size() > 9;
    }

    bool isValidTimestamp(const std::string &raw)
    {
        const std::string s = trim(raw);
        if (s.size() < 16)
        {
            return false;
        }

        const auto sepPos = s.find_first_of(" T");
        if (sepPos == std::string::npos)
        {
            return false;
        }

        const std::string datePart = s.substr(0, sepPos);
        const std::string timePart = trim(s.substr(sepPos + 1));

        return isValidDate(datePart) && isValidTime(timePart);
    }

    std::optional<bool> parseBooleanValue(const std::string &raw)
    {
        const std::string u = toUpper(trim(raw));
        if (u == "TRUE" || u == "T" || u == "1")
        {
            return true;
        }
        if (u == "FALSE" || u == "F" || u == "0")
        {
            return false;
        }
        return std::nullopt;
    }

    std::optional<std::string> unquoteSqlToken(const std::string &raw)
    {
        const std::string t = trim(raw);
        if (t.size() < 2 || t.front() != '\'' || t.back() != '\'')
        {
            return std::nullopt;
        }

        std::string out;
        out.reserve(t.size() - 2);
        for (std::size_t i = 1; i + 1 < t.size(); ++i)
        {
            if (t[i] == '\'' && i + 1 < t.size() - 1 && t[i + 1] == '\'')
            {
                out.push_back('\'');
                ++i;
            }
            else
            {
                out.push_back(t[i]);
            }
        }
        return out;
    }

    std::vector<std::string> splitCommaTopLevel(const std::string &s)
    {
        std::vector<std::string> out;
        std::string cur;
        bool inSingle = false;

        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];

            if (inSingle)
            {
                cur.push_back(c);
                if (c == '\'' && i + 1 < s.size() && s[i + 1] == '\'')
                {
                    cur.push_back('\'');
                    ++i;
                    continue;
                }
                if (c == '\'')
                {
                    inSingle = false;
                }
                continue;
            }

            if (c == '\'')
            {
                inSingle = true;
                cur.push_back(c);
                continue;
            }

            if (c == ',')
            {
                out.push_back(trim(cur));
                cur.clear();
                continue;
            }

            cur.push_back(c);
        }

        if (inSingle)
        {
            throw std::runtime_error("Malformed ENUM list: " + s);
        }

        out.push_back(trim(cur));
        return out;
    }

    std::vector<std::string> parseEnumValues(const std::string &rawInside)
    {
        const auto toks = splitCommaTopLevel(rawInside);
        if (toks.empty())
        {
            throw std::runtime_error("ENUM must define at least one value");
        }

        std::vector<std::string> values;
        values.reserve(toks.size());
        for (const auto &tok : toks)
        {
            const auto u = unquoteSqlToken(tok);
            if (!u.has_value())
            {
                throw std::runtime_error("ENUM values must be SQL strings: " + tok);
            }
            values.push_back(*u);
        }
        return values;
    }

} // namespace

LogicalType parseLogicalType(const std::string &rawType)
{
    const std::string trimmed = trim(rawType);
    if (trimmed.empty())
    {
        throw std::runtime_error("Column type is empty");
    }

    std::string compact;
    compact.reserve(trimmed.size());
    bool inSingle = false;
    for (std::size_t i = 0; i < trimmed.size(); ++i)
    {
        const char c = trimmed[i];
        if (c == '\'')
        {
            compact.push_back(c);
            if (inSingle && i + 1 < trimmed.size() && trimmed[i + 1] == '\'')
            {
                compact.push_back('\'');
                ++i;
                continue;
            }
            inSingle = !inSingle;
            continue;
        }
        if (!inSingle && std::isspace(static_cast<unsigned char>(c)))
        {
            continue;
        }
        compact.push_back(c);
    }

    const std::string upper = toUpper(compact);
    LogicalType out;

    if (upper == "INT" || upper == "INTEGER")
    {
        out.kind = LogicalTypeKind::INT;
        out.normalizedName = "INT";
        return out;
    }

    if (upper == "TEXT")
    {
        out.kind = LogicalTypeKind::TEXT;
        out.normalizedName = "TEXT";
        return out;
    }

    if (upper == "BOOLEAN" || upper == "BOOL")
    {
        out.kind = LogicalTypeKind::BOOLEAN;
        out.normalizedName = "BOOLEAN";
        return out;
    }

    if (upper == "VARCHAR")
    {
        out.kind = LogicalTypeKind::VARCHAR;
        out.width = 255;
        out.normalizedName = "VARCHAR(255)";
        return out;
    }

    if (startsWith(upper, "VARCHAR(") && upper.back() == ')')
    {
        const std::string inside = compact.substr(8, compact.size() - 9);
        out.kind = LogicalTypeKind::VARCHAR;
        out.width = parsePositiveInt(inside, "VARCHAR(n)");
        out.normalizedName = "VARCHAR(" + std::to_string(out.width) + ")";
        return out;
    }

    if (upper == "FLOAT" || upper == "REAL")
    {
        out.kind = LogicalTypeKind::FLOAT;
        out.normalizedName = "FLOAT";
        return out;
    }

    if (upper == "DOUBLE" || upper == "DOUBLEPRECISION")
    {
        out.kind = LogicalTypeKind::DOUBLE_;
        out.normalizedName = "DOUBLE";
        return out;
    }

    if (upper == "TIMESTAMP")
    {
        out.kind = LogicalTypeKind::TIMESTAMP;
        out.normalizedName = "TIMESTAMP";
        return out;
    }

    if (startsWith(upper, "ENUM(") && upper.back() == ')')
    {
        const std::string inside = compact.substr(5, compact.size() - 6);
        out.kind = LogicalTypeKind::ENUM;
        out.enumValues = parseEnumValues(inside);

        out.normalizedName = "ENUM(";
        for (std::size_t i = 0; i < out.enumValues.size(); ++i)
        {
            if (i > 0)
            {
                out.normalizedName += ",";
            }
            out.normalizedName += "'" + out.enumValues[i] + "'";
        }
        out.normalizedName += ")";
        return out;
    }

    throw std::runtime_error("Unsupported type '" + rawType + "'. Supported: INT, VARCHAR, TEXT, BOOLEAN, FLOAT, DOUBLE, TIMESTAMP, ENUM");
}

bool isSupportedLogicalType(const std::string &rawType) noexcept
{
    try
    {
        (void)parseLogicalType(rawType);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void validateTypedValue(const LogicalType &type, const std::string &value, const std::string &columnName)
{
    if (value.empty())
    {
        return;
    }

    switch (type.kind)
    {
    case LogicalTypeKind::INT:
    {
        long long n = 0;
        if (!parseSignedInteger(value, n))
        {
            throw std::runtime_error("Column '" + columnName + "' expects INT, got: '" + value + "'");
        }
        (void)n;
        return;
    }
    case LogicalTypeKind::VARCHAR:
        if (value.size() > type.width)
        {
            throw std::runtime_error("Column '" + columnName + "' exceeds VARCHAR(" + std::to_string(type.width) + ") limit");
        }
        return;
    case LogicalTypeKind::TEXT:
        return;
    case LogicalTypeKind::BOOLEAN:
        if (!parseBooleanValue(value).has_value())
        {
            throw std::runtime_error("Column '" + columnName + "' expects BOOLEAN, got: '" + value + "'");
        }
        return;
    case LogicalTypeKind::FLOAT:
    case LogicalTypeKind::DOUBLE_:
    {
        long double n = 0;
        if (!parseFloatingNumber(value, n))
        {
            throw std::runtime_error("Column '" + columnName + "' expects numeric value, got: '" + value + "'");
        }
        (void)n;
        return;
    }
    case LogicalTypeKind::TIMESTAMP:
        if (!isValidTimestamp(value))
        {
            throw std::runtime_error("Column '" + columnName + "' expects TIMESTAMP (YYYY-MM-DD HH:MM[:SS]), got: '" + value + "'");
        }
        return;
    case LogicalTypeKind::ENUM:
        if (std::find(type.enumValues.begin(), type.enumValues.end(), value) ==
            type.enumValues.end())
        {
            throw std::runtime_error("Column '" + columnName + "' expects ENUM value, got: '" + value + "'");
        }
        return;
    case LogicalTypeKind::UNKNOWN:
        break;
    }

    throw std::runtime_error("Unsupported type for column '" + columnName + "'");
}

std::string normalizeTypedValue(const LogicalType &type, const std::string &value, const std::string &columnName)
{
    validateTypedValue(type, value, columnName);

    if (value.empty())
    {
        return value;
    }

    if (type.kind == LogicalTypeKind::BOOLEAN)
    {
        const auto b = parseBooleanValue(value);
        return (b.has_value() && *b) ? "true" : "false";
    }

    return value;
}
