#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/data_type.hpp"

TEST(DataTypeTest, ParsesSupportedTypes) {
    {
        const LogicalType t = parseLogicalType("INT");
        EXPECT_EQ(t.kind, LogicalTypeKind::INT);
        EXPECT_EQ(t.normalizedName, "INT");
    }
    {
        const LogicalType t = parseLogicalType("VARCHAR");
        EXPECT_EQ(t.kind, LogicalTypeKind::VARCHAR);
        EXPECT_EQ(t.width, 255u);
        EXPECT_EQ(t.normalizedName, "VARCHAR(255)");
    }
    {
        const LogicalType t = parseLogicalType("VARCHAR(64)");
        EXPECT_EQ(t.kind, LogicalTypeKind::VARCHAR);
        EXPECT_EQ(t.width, 64u);
        EXPECT_EQ(t.normalizedName, "VARCHAR(64)");
    }
    {
        const LogicalType t = parseLogicalType("TEXT");
        EXPECT_EQ(t.kind, LogicalTypeKind::TEXT);
        EXPECT_EQ(t.normalizedName, "TEXT");
    }
    {
        const LogicalType t = parseLogicalType("BOOLEAN");
        EXPECT_EQ(t.kind, LogicalTypeKind::BOOLEAN);
        EXPECT_EQ(t.normalizedName, "BOOLEAN");
    }
    {
        const LogicalType t = parseLogicalType("FLOAT");
        EXPECT_EQ(t.kind, LogicalTypeKind::FLOAT);
        EXPECT_EQ(t.normalizedName, "FLOAT");
    }
    {
        const LogicalType t = parseLogicalType("DOUBLE");
        EXPECT_EQ(t.kind, LogicalTypeKind::DOUBLE_);
        EXPECT_EQ(t.normalizedName, "DOUBLE");
    }
    {
        const LogicalType t = parseLogicalType("TIMESTAMP");
        EXPECT_EQ(t.kind, LogicalTypeKind::TIMESTAMP);
        EXPECT_EQ(t.normalizedName, "TIMESTAMP");
    }
}

TEST(DataTypeTest, ParsesEnumType) {
    const LogicalType t = parseLogicalType("ENUM('active', 'inactive', 'paused')");
    EXPECT_EQ(t.kind, LogicalTypeKind::ENUM);
    ASSERT_EQ(t.enumValues.size(), 3u);
    EXPECT_EQ(t.enumValues[0], "active");
    EXPECT_EQ(t.enumValues[1], "inactive");
    EXPECT_EQ(t.enumValues[2], "paused");
}

TEST(DataTypeTest, RejectsUnsupportedTypes) {
    EXPECT_THROW(parseLogicalType("BIT(8)"), std::runtime_error);
    EXPECT_THROW(parseLogicalType("DECIMAL(10,2)"), std::runtime_error);
    EXPECT_THROW(parseLogicalType("DATE"), std::runtime_error);
    EXPECT_THROW(parseLogicalType("TIME"), std::runtime_error);
    EXPECT_THROW(parseLogicalType("JSONB"), std::runtime_error);
    EXPECT_THROW(parseLogicalType("ARRAY(INT)"), std::runtime_error);
}

TEST(DataTypeTest, ValidatesAndNormalizesValues) {
    const LogicalType booleanType = parseLogicalType("BOOLEAN");
    EXPECT_EQ(normalizeTypedValue(booleanType, "1", "is_active"), "true");
    EXPECT_EQ(normalizeTypedValue(booleanType, "FALSE", "is_active"), "false");
    EXPECT_THROW(validateTypedValue(booleanType, "yes", "is_active"), std::runtime_error);

    const LogicalType timestampType = parseLogicalType("TIMESTAMP");
    EXPECT_NO_THROW(validateTypedValue(timestampType, "2024-10-01 14:30", "created_at"));
    EXPECT_NO_THROW(validateTypedValue(timestampType, "2024-10-01T14:30:25", "created_at"));
    EXPECT_THROW(validateTypedValue(timestampType, "2024-13-01 12:00", "created_at"),
                 std::runtime_error);

    const LogicalType enumType = parseLogicalType("ENUM('draft','published')");
    EXPECT_NO_THROW(validateTypedValue(enumType, "draft", "status"));
    EXPECT_THROW(validateTypedValue(enumType, "archived", "status"), std::runtime_error);
}
