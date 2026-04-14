#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>

#include "app/query_engine.hpp"
#include "storage/csv_loader.hpp"
#include "storage/metadata_catalog.hpp"

namespace {

    class AlterTableAppTest : public ::testing::Test {
    protected:
        std::filesystem::path dataDir;

        void SetUp() override {
            const auto nonce = static_cast<unsigned long long>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            dataDir = std::filesystem::temp_directory_path() /
                      ("sqp_alter_table_test_" + std::to_string(nonce));
            std::filesystem::create_directories(dataDir);
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(dataDir, ec);
        }
    };

    TEST_F(AlterTableAppTest, SupportsCoreAlterOperations) {
        QueryEngineApp app(dataDir.string());
        app.initialize();

        app.executeStatement("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(20))");
        app.executeStatement("INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob')");

        app.executeStatement("ALTER TABLE users ADD COLUMN age INT");
        app.executeStatement("UPDATE users SET age = 30 WHERE id = 1");
        app.executeStatement("ALTER TABLE users ALTER COLUMN age TYPE DOUBLE");
        app.executeStatement("ALTER TABLE users RENAME COLUMN name TO full_name");
        app.executeStatement("ALTER TABLE users ADD CONSTRAINT UNIQUE (full_name)");
        app.executeStatement("ALTER TABLE users ADD CONSTRAINT CHECK (id > 0)");
        app.executeStatement("ALTER TABLE users DROP COLUMN age");

        MetadataCatalog cat(dataDir.string());
        cat.load();
        const TableMetadata &tm = cat.getTable("users");

        ASSERT_EQ(tm.columns.size(), 2u);
        EXPECT_EQ(tm.columns[0].name, "id");
        EXPECT_EQ(tm.columns[1].name, "full_name");
        EXPECT_TRUE(tm.columns[1].unique);
        ASSERT_EQ(tm.tableChecks.size(), 1u);
        EXPECT_EQ(tm.tableChecks[0], "id > 0");

        const Table persisted = CsvLoader::loadTable(cat.tableFilePath("users"));
        ASSERT_EQ(persisted.size(), 2u);
        for (const auto &row : persisted) {
            EXPECT_TRUE(row.count("id"));
            EXPECT_TRUE(row.count("full_name"));
            EXPECT_FALSE(row.count("age"));
        }
    }

    TEST_F(AlterTableAppTest, RejectsNotNullAddColumnOnNonEmptyTable) {
        QueryEngineApp app(dataDir.string());
        app.initialize();

        app.executeStatement("CREATE TABLE users (id INT PRIMARY KEY)");
        app.executeStatement("INSERT INTO users VALUES (1)");

        EXPECT_THROW(app.executeStatement("ALTER TABLE users ADD COLUMN age INT NOT NULL"), std::runtime_error);

        MetadataCatalog cat(dataDir.string());
        cat.load();
        const TableMetadata &tm = cat.getTable("users");
        ASSERT_EQ(tm.columns.size(), 1u);
        EXPECT_EQ(tm.columns[0].name, "id");
    }

    TEST_F(AlterTableAppTest, RenameColumnUpdatesReferencingForeignKeys) {
        QueryEngineApp app(dataDir.string());
        app.initialize();

        app.executeStatement("CREATE TABLE parents (id INT PRIMARY KEY)");
        app.executeStatement("CREATE TABLE children (id INT PRIMARY KEY, parent_id INT, FOREIGN KEY (parent_id) REFERENCES parents(id))");
        app.executeStatement("INSERT INTO parents VALUES (1)");
        app.executeStatement("INSERT INTO children VALUES (10, 1)");

        app.executeStatement("ALTER TABLE parents RENAME COLUMN id TO parent_key");

        MetadataCatalog cat(dataDir.string());
        cat.load();

        const TableMetadata &parentMeta = cat.getTable("parents");
        ASSERT_EQ(parentMeta.columns.size(), 1u);
        EXPECT_EQ(parentMeta.columns[0].name, "parent_key");

        const TableMetadata &childMeta = cat.getTable("children");
        ASSERT_EQ(childMeta.columns.size(), 2u);
        ASSERT_TRUE(childMeta.columns[1].foreignKey.has_value());
        EXPECT_EQ(*childMeta.columns[1].foreignKey, "parents.parent_key");

        EXPECT_NO_THROW(app.executeStatement("INSERT INTO children VALUES (11, 1)"));
        EXPECT_THROW(app.executeStatement("INSERT INTO children VALUES (12, 999)"), std::runtime_error);
    }

    TEST_F(AlterTableAppTest, AddForeignKeyConstraintAfterTableCreation) {
        QueryEngineApp app(dataDir.string());
        app.initialize();

        app.executeStatement("CREATE TABLE departments (id INT PRIMARY KEY, name VARCHAR(30))");
        app.executeStatement("CREATE TABLE users (id INT PRIMARY KEY, dept_id INT)");
        app.executeStatement("INSERT INTO departments VALUES (1, 'Engineering')");
        app.executeStatement("INSERT INTO users VALUES (1, 1)");

        app.executeStatement("ALTER TABLE users ADD CONSTRAINT FOREIGN KEY (dept_id) REFERENCES departments(id)");

        EXPECT_NO_THROW(app.executeStatement("INSERT INTO users VALUES (2, 1)"));
        EXPECT_THROW(app.executeStatement("INSERT INTO users VALUES (3, 999)"), std::runtime_error);
    }

}
