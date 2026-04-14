#include <gtest/gtest.h>
#include "parser/lexer.hpp"
#include "parser/parser.hpp"

TEST(ParserTest, SimpleSelect) {
    Lexer lexer("SELECT * FROM users");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    EXPECT_EQ(stmt.columns.size(), 1);
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[0].get())->name, "*");
    EXPECT_EQ(stmt.where, nullptr);
}

TEST(ParserTest, SpecificColumns) {
    Lexer lexer("SELECT name, age FROM users");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    ASSERT_EQ(stmt.columns.size(), 2);
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[0].get())->name, "name");
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[1].get())->name, "age");
}

TEST(ParserTest, WhereClause) {
    Lexer lexer("SELECT * FROM users WHERE age = 25");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_NE(stmt.where, nullptr);
    auto *binaryExpr = dynamic_cast<BinaryExpr *>(stmt.where.get());
    ASSERT_NE(binaryExpr, nullptr);
    EXPECT_EQ(binaryExpr->op, "=");

    auto *left = dynamic_cast<Column *>(binaryExpr->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->name, "age");

    auto *right = dynamic_cast<Literal *>(binaryExpr->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->value, "25");
}

TEST(ParserTest, OrderByLimit) {
    Lexer lexer("SELECT * FROM users ORDER BY age LIMIT 10");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.orderBy, "age");
    EXPECT_TRUE(stmt.orderByAscending);
    EXPECT_EQ(stmt.limit, 10);
}

TEST(ParserTest, OrderByDescDirection) {
    Lexer lexer("SELECT * FROM users ORDER BY age DESC");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.orderBy, "age");
    EXPECT_FALSE(stmt.orderByAscending);
}

TEST(ParserTest, AggregateFunctionsInSelectAndHaving) {
    Lexer lexer("SELECT department, COUNT(*), AVG(age) FROM users GROUP BY department HAVING COUNT(*) > 1");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_EQ(stmt.columns.size(), 3);
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[0].get())->name, "department");
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[1].get())->name, "COUNT(*)");
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[2].get())->name, "AVG(age)");
    ASSERT_NE(stmt.having, nullptr);
}

TEST(ParserTest, InvalidSyntax) {
    Lexer lexer("SELECT * FROM");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    EXPECT_THROW(parser.parse(), std::runtime_error);
}

TEST(ParserTest, SemicolonTerminator) {
    Lexer lexer("SELECT id FROM users;");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    ASSERT_EQ(stmt.columns.size(), 1);
    EXPECT_EQ(dynamic_cast<Column *>(stmt.columns[0].get())->name, "id");
}

TEST(ParserTest, AngleBracketNotEqualOperator) {
    Lexer lexer("SELECT * FROM users WHERE age <> 30");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_NE(stmt.where, nullptr);
    auto *binaryExpr = dynamic_cast<BinaryExpr *>(stmt.where.get());
    ASSERT_NE(binaryExpr, nullptr);
    EXPECT_EQ(binaryExpr->op, "<>");
}

TEST(ParserTest, LimitOutOfRange) {
    Lexer lexer("SELECT * FROM users LIMIT 999999999999999999999999");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    EXPECT_THROW(parser.parse(), std::runtime_error);
}

TEST(ParserTest, PrecedenceAndParentheses) {
    // AND has higher precedence than OR
    {
        Lexer lexer("SELECT * FROM users WHERE a = 1 OR b = 2 AND c = 3");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();

        // Should be (a=1) OR ((b=2) AND (c=3))
        auto *orExpr = dynamic_cast<BinaryExpr *>(stmt.where.get());
        ASSERT_NE(orExpr, nullptr);
        EXPECT_EQ(orExpr->op, "OR");

        auto *andExpr = dynamic_cast<BinaryExpr *>(orExpr->right.get());
        ASSERT_NE(andExpr, nullptr);
        EXPECT_EQ(andExpr->op, "AND");
    }

    // Parentheses override precedence
    {
        Lexer lexer("SELECT * FROM users WHERE (a = 1 OR b = 2) AND c = 3");
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();

        // Should be ((a=1) OR (b=2)) AND (c=3)
        auto *andExpr = dynamic_cast<BinaryExpr *>(stmt.where.get());
        ASSERT_NE(andExpr, nullptr);
        EXPECT_EQ(andExpr->op, "AND");

        auto *orExpr = dynamic_cast<BinaryExpr *>(andExpr->left.get());
        ASSERT_NE(orExpr, nullptr);
        EXPECT_EQ(orExpr->op, "OR");
    }
}

TEST(ParserTest, CreateTableStatement) {
    Lexer lexer("CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT)");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    EXPECT_EQ(stmt.type, StatementType::CREATE_TABLE);
    ASSERT_NE(stmt.createTable, nullptr);
    EXPECT_EQ(stmt.createTable->table, "students");
    ASSERT_EQ(stmt.createTable->columns.size(), 3);
    EXPECT_EQ(stmt.createTable->columns[0].name, "id");
    EXPECT_EQ(stmt.createTable->columns[0].type, "INT");
    EXPECT_TRUE(stmt.createTable->columns[0].primaryKey);
}

TEST(ParserTest, InsertStatement) {
    Lexer lexer("INSERT INTO students (id, name, age) VALUES (1, 'Alice', 20)");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    EXPECT_EQ(stmt.type, StatementType::INSERT);
    ASSERT_NE(stmt.insert, nullptr);
    EXPECT_EQ(stmt.insert->table, "students");
    ASSERT_EQ(stmt.insert->columns.size(), 3);
    ASSERT_EQ(stmt.insert->valueRows.size(), 1);
    ASSERT_EQ(stmt.insert->valueRows[0].size(), 3);

    auto *value0 = dynamic_cast<Literal *>(stmt.insert->valueRows[0][0].get());
    ASSERT_NE(value0, nullptr);
    EXPECT_EQ(value0->value, "1");
}

TEST(ParserTest, InsertMultipleRows) {
    Lexer lexer("INSERT INTO students VALUES (1, 'Alice', 20), (2, 'Bob', 22)");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    EXPECT_EQ(stmt.type, StatementType::INSERT);
    ASSERT_NE(stmt.insert, nullptr);
    EXPECT_EQ(stmt.insert->table, "students");
    ASSERT_EQ(stmt.insert->valueRows.size(), 2);
    ASSERT_EQ(stmt.insert->valueRows[0].size(), 3);
    ASSERT_EQ(stmt.insert->valueRows[1].size(), 3);
}

TEST(ParserTest, CreateTableWithTypeModifierAndConstraints) {
    Lexer lexer("CREATE TABLE partnerUniversity (name VARCHAR(100) UNIQUE NOT NULL)");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    ASSERT_NE(stmt.createTable, nullptr);
    ASSERT_EQ(stmt.createTable->columns.size(), 1);
    EXPECT_EQ(stmt.createTable->columns[0].name, "name");
    EXPECT_EQ(stmt.createTable->columns[0].type, "VARCHAR(100)");
    EXPECT_TRUE(stmt.createTable->columns[0].unique);
    EXPECT_TRUE(stmt.createTable->columns[0].notNull);
}

TEST(ParserTest, CreateTableWithSupportedDataTypes) {
    Lexer lexer("CREATE TABLE metrics ("
                "is_active BOOLEAN, "
                "title VARCHAR(128), "
                "price FLOAT, "
                "ratio DOUBLE, "
                "created_at TIMESTAMP, "
                "notes TEXT, "
                "status ENUM('active','inactive','paused')"
                ")");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    ASSERT_NE(stmt.createTable, nullptr);
    ASSERT_EQ(stmt.createTable->columns.size(), 7);

    EXPECT_EQ(stmt.createTable->columns[0].type, "BOOLEAN");
    EXPECT_EQ(stmt.createTable->columns[1].type, "VARCHAR(128)");
    EXPECT_EQ(stmt.createTable->columns[2].type, "FLOAT");
    EXPECT_EQ(stmt.createTable->columns[3].type, "DOUBLE");
    EXPECT_EQ(stmt.createTable->columns[4].type, "TIMESTAMP");
    EXPECT_EQ(stmt.createTable->columns[5].type, "TEXT");
    EXPECT_EQ(stmt.createTable->columns[6].type, "ENUM('active','inactive','paused')");
}

TEST(ParserTest, CreateTableIfNotExistsAndCheck) {
    Lexer lexer("CREATE TABLE IF NOT EXISTS grades (score INT CHECK (score >= 0 AND score <= 100), "
                "status VARCHAR(20) CHECK (status IN ('PASS', 'FAIL')))");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    ASSERT_NE(stmt.createTable, nullptr);
    EXPECT_TRUE(stmt.createTable->ifNotExists);
    ASSERT_EQ(stmt.createTable->columns.size(), 2);
    ASSERT_TRUE(stmt.createTable->columns[0].checkExpr.has_value());
    ASSERT_TRUE(stmt.createTable->columns[1].checkExpr.has_value());
}

TEST(ParserTest, CreateTableWithTableLevelForeignKeyConstraint) {
    Lexer lexer("CREATE TABLE classes (id INT, student_id INT, FOREIGN KEY (student_id) REFERENCES "
                "students(id))");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    ASSERT_NE(stmt.createTable, nullptr);
    ASSERT_EQ(stmt.createTable->columns.size(), 2);
    EXPECT_EQ(stmt.createTable->columns[1].name, "student_id");
    ASSERT_TRUE(stmt.createTable->columns[1].foreignKey.has_value());
    EXPECT_EQ(*stmt.createTable->columns[1].foreignKey, "students.id");
}

TEST(ParserTest, CreateTableWithCompositePrimaryKeyConstraint) {
    Lexer lexer("CREATE TABLE enrollment (student_id INT, course_id INT, PRIMARY KEY (student_id, "
                "course_id))");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    ASSERT_NE(stmt.createTable, nullptr);
    ASSERT_EQ(stmt.createTable->columns.size(), 2);
    EXPECT_EQ(stmt.createTable->columns[0].name, "student_id");
    EXPECT_EQ(stmt.createTable->columns[1].name, "course_id");
    EXPECT_TRUE(stmt.createTable->columns[0].primaryKey);
    EXPECT_TRUE(stmt.createTable->columns[1].primaryKey);
}

TEST(ParserTest, UpdateStatement) {
    Lexer lexer("UPDATE students SET age = 21, name = 'Alicia' WHERE id = 1");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    EXPECT_EQ(stmt.type, StatementType::UPDATE);
    ASSERT_NE(stmt.update, nullptr);
    EXPECT_EQ(stmt.update->table, "students");
    ASSERT_EQ(stmt.update->assignments.size(), 2);
    EXPECT_EQ(stmt.update->assignments[0].column, "age");
    ASSERT_NE(stmt.update->where, nullptr);
}

TEST(ParserTest, DeleteStatement) {
    Lexer lexer("DELETE FROM students WHERE id = 10");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    EXPECT_EQ(stmt.type, StatementType::DELETE_);
    ASSERT_NE(stmt.deleteStmt, nullptr);
    EXPECT_EQ(stmt.deleteStmt->table, "students");
    ASSERT_NE(stmt.deleteStmt->where, nullptr);
}

TEST(ParserTest, AlterTableAddColumn) {
    Lexer lexer("ALTER TABLE users ADD COLUMN age INT");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);

    Statement stmt = parser.parseStatement();
    EXPECT_EQ(stmt.type, StatementType::ALTER_TABLE);
    ASSERT_NE(stmt.alterTable, nullptr);
    EXPECT_EQ(stmt.alterTable->table, "users");
    EXPECT_EQ(stmt.alterTable->action, AlterActionKind::ADD_COLUMN);
    EXPECT_EQ(stmt.alterTable->columnDef.name, "age");
    EXPECT_EQ(stmt.alterTable->columnDef.type, "INT");
}

TEST(ParserTest, AlterTableDropRenameAndType) {
    {
        Lexer lexer("ALTER TABLE users DROP COLUMN age");
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parseStatement();
        ASSERT_NE(stmt.alterTable, nullptr);
        EXPECT_EQ(stmt.alterTable->action, AlterActionKind::DROP_COLUMN);
        EXPECT_EQ(stmt.alterTable->columnName, "age");
    }

    {
        Lexer lexer("ALTER TABLE users RENAME COLUMN fullname TO display_name");
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parseStatement();
        ASSERT_NE(stmt.alterTable, nullptr);
        EXPECT_EQ(stmt.alterTable->action, AlterActionKind::RENAME_COLUMN);
        EXPECT_EQ(stmt.alterTable->columnName, "fullname");
        EXPECT_EQ(stmt.alterTable->newColumnName, "display_name");
    }

    {
        Lexer lexer("ALTER TABLE users ALTER COLUMN score TYPE DOUBLE");
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parseStatement();
        ASSERT_NE(stmt.alterTable, nullptr);
        EXPECT_EQ(stmt.alterTable->action, AlterActionKind::ALTER_COLUMN_TYPE);
        EXPECT_EQ(stmt.alterTable->columnName, "score");
        EXPECT_EQ(stmt.alterTable->newType, "DOUBLE");
    }
}

TEST(ParserTest, AlterTableAddConstraintForms) {
    {
        Lexer lexer("ALTER TABLE users ADD CONSTRAINT users_email_uniq UNIQUE (email)");
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parseStatement();
        ASSERT_NE(stmt.alterTable, nullptr);
        EXPECT_EQ(stmt.alterTable->action, AlterActionKind::ADD_CONSTRAINT);
        EXPECT_EQ(stmt.alterTable->constraintKind, AlterConstraintKind::UNIQUE);
        ASSERT_EQ(stmt.alterTable->constraintColumns.size(), 1u);
        EXPECT_EQ(stmt.alterTable->constraintColumns[0], "email");
    }

    {
        Lexer lexer("ALTER TABLE users ADD CONSTRAINT CHECK (age >= 18)");
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parseStatement();
        ASSERT_NE(stmt.alterTable, nullptr);
        EXPECT_EQ(stmt.alterTable->constraintKind, AlterConstraintKind::CHECK);
        EXPECT_EQ(stmt.alterTable->checkExpr, "age >= 18");
    }

    {
        Lexer lexer("ALTER TABLE enrollment ADD CONSTRAINT FOREIGN KEY (course_id) REFERENCES course(id)");
        Parser parser(lexer.tokenize());
        Statement stmt = parser.parseStatement();
        ASSERT_NE(stmt.alterTable, nullptr);
        EXPECT_EQ(stmt.alterTable->constraintKind, AlterConstraintKind::FOREIGN_KEY);
        ASSERT_EQ(stmt.alterTable->constraintColumns.size(), 1u);
        EXPECT_EQ(stmt.alterTable->constraintColumns[0], "course_id");
        EXPECT_EQ(stmt.alterTable->referencedTable, "course");
        EXPECT_EQ(stmt.alterTable->referencedColumn, "id");
    }
}

TEST(ParserTest, ConstructFromTemporaryTokenVector) {
    Parser parser(Lexer("SELECT * FROM users").tokenize());
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.table, "users");
    ASSERT_EQ(stmt.columns.size(), 1);
    auto *col = dynamic_cast<Column *>(stmt.columns[0].get());
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->name, "*");
}

TEST(ParserTest, ParseInnerJoinWithAliases) {
    Lexer lexer("SELECT u.name, d.dept_name FROM users AS u INNER JOIN departments d ON u.dept_id = d.dept_id");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_EQ(stmt.from.table, "users");
    EXPECT_EQ(stmt.from.alias, "u");
    ASSERT_EQ(stmt.joins.size(), 1);
    EXPECT_EQ(stmt.joins[0].type, JoinType::INNER);
    EXPECT_EQ(stmt.joins[0].right.table, "departments");
    EXPECT_EQ(stmt.joins[0].right.alias, "d");
    ASSERT_NE(stmt.joins[0].condition, nullptr);

    ASSERT_EQ(stmt.columns.size(), 2);
    auto *c0 = dynamic_cast<Column *>(stmt.columns[0].get());
    auto *c1 = dynamic_cast<Column *>(stmt.columns[1].get());
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c0->name, "u.name");
    EXPECT_EQ(c1->name, "d.dept_name");
}

TEST(ParserTest, ParseFullOuterJoin) {
    Lexer lexer("SELECT users.id, departments.dept_id "
                "FROM users FULL OUTER JOIN departments "
                "ON users.dept_id = departments.dept_id");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_EQ(stmt.joins.size(), 1);
    EXPECT_EQ(stmt.joins[0].type, JoinType::FULL);
    ASSERT_NE(stmt.joins[0].condition, nullptr);
}

TEST(ParserTest, ParseCrossJoinWithoutOnClause) {
    Lexer lexer("SELECT users.id FROM users CROSS JOIN departments");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_EQ(stmt.joins.size(), 1);
    EXPECT_EQ(stmt.joins[0].type, JoinType::CROSS);
    EXPECT_EQ(stmt.joins[0].condition, nullptr);
}

TEST(ParserTest, ParseDistinctQualifiedColumn) {
    Lexer lexer("SELECT DISTINCT s.name " "FROM Student s " "JOIN Enrollment e ON s.student_id = e.student_id");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();

    EXPECT_TRUE(stmt.distinct);
    ASSERT_EQ(stmt.columns.size(), 1);
    auto *col = dynamic_cast<Column *>(stmt.columns[0].get());
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->name, "s.name");
}
