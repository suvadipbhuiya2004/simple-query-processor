#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class LogicalTypeKind {
    INT,
    VARCHAR,
    TEXT,
    BOOLEAN,
    FLOAT,
    DOUBLE_,
    TIMESTAMP,
    ENUM,
    UNKNOWN,
};

struct LogicalType {
    LogicalTypeKind kind{LogicalTypeKind::UNKNOWN};
    std::string normalizedName;

    // VARCHAR(n)
    std::size_t width{0};

    // ENUM('A','B',...) list
    std::vector<std::string> enumValues;
};

// Parses and normalizes a SQL type name, throws on malformed/unsupported forms.
LogicalType parseLogicalType(const std::string &rawType);

// Returns true only if parseLogicalType succeeds.
bool isSupportedLogicalType(const std::string &rawType) noexcept;

// Validates a scalar string value against the parsed logical type.
// Empty values are accepted and treated as NULL-like; NOT NULL is enforced elsewhere.
void validateTypedValue(const LogicalType &type, const std::string &value, const std::string &columnName);

// Returns a canonical storage representation for a typed value.
// For most types this is the original value; BOOLEAN is normalized to true/false.
std::string normalizeTypedValue(const LogicalType &type, const std::string &value, const std::string &columnName);
