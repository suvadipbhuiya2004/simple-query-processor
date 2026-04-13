# Simple Query Processor

Implements a small SQL engine with parser, planner, and executor layers:

```text
Parser -> AST -> Planner -> Plan Tree -> Execution
```

## Supported Statements

```sql
SELECT <column_list | *> FROM <table> [WHERE <expr>] [GROUP BY <columns>] [HAVING <expr>] [ORDER BY <column>] [LIMIT <n>]
SELECT ... FROM <table> [INNER|LEFT|RIGHT|FULL [OUTER]|CROSS] JOIN <table> [ON <expr>]
CREATE TABLE <table> (<column_defs>)
INSERT INTO <table> [(columns...)] VALUES (values...)
UPDATE <table> SET <column = value, ...> [WHERE <expr>]
DELETE FROM <table> [WHERE <expr>]
```

Examples:

```sql
CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT);
INSERT INTO students (id, name, age) VALUES (1, 'Alice', 20);
UPDATE students SET age = 21 WHERE id = 1;
DELETE FROM students WHERE id = 1;

SELECT * FROM users;
SELECT name FROM users WHERE age >= 35 ORDER BY age LIMIT 3;
```

Supported predicate operators: `=` `==` `!=` `<>` `>` `<` `>=` `<=`

Join algorithms available in the executor: `HASH` (default), `NESTED_LOOP`, and `MERGE`.
Default join strategy is configured in [include/planner/plan.hpp](include/planner/plan.hpp#L27).

## Metadata Catalog

Table definitions are stored in `data/metadata.json`.

Schema shape:

```json
{
	"tables": {
		"students": {
			"file": "students.csv",
			"columns": [
				{ "name": "id", "type": "INT", "primary_key": true },
				{ "name": "name", "type": "VARCHAR" },
				{ "name": "age", "type": "INT" }
			]
		}
	}
}
```

Notes:

- Table data files use `.csv` extension.
- `CREATE TABLE` updates `metadata.json` and creates a matching `.csv` file.
- `INSERT`, `UPDATE`, and `DELETE` persist row changes back to table files.
- Basic constraints are enforced: `PRIMARY KEY` uniqueness, `INT`/`VARCHAR` types, and `FOREIGN KEY` references.

## Build and Run

```bash
make run
```

The executable reads and executes all SQL statements from `queries.sql`.

## Tests

```bash
make test
make test-verbose
```

## Not Yet Supported

- Aggregation functions like `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`
- `ASC`, `DESC` using `ORDER BY`
- Query optimization
