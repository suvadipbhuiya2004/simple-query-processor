# Simple Query Processor

Implements a minimal SQL pipeline in three layers:
```
Parser → AST → Planner → Plan Tree → Execution
```

## Supported Syntax
```sql
SELECT <column_list | *> FROM <table> [WHERE <expr>] [GROUP BY <columns>] [HAVING <expr>] [ORDER BY <column>] [LIMIT <n>]
```
```sql
SELECT * FROM users;
SELECT name FROM users WHERE age > 30;
SELECT name, age FROM users WHERE age >= 35;
SELECT name, city, department FROM users WHERE salary > 90000;
SELECT name FROM users ORDER BY age LIMIT 2;
```

Supported WHERE operators: `=` `==` `!=` `<>` `>` `<` `>=` `<=`

## Data Files

- Table files use a `.db` extension (example: `data/users.db`).
- File content is still CSV-formatted internally.

## Build & Run
```bash
make run                                # build and run using queries.sql
```

The executable reads one or more SQL statements from `queries.sql` and executes them in order.

Output is printed in relation-style table form with a tuple count, for example:

```text
name  | age
------+----
John  |  65
Anita |  28

(2 tuples)
```

Example `queries.sql`:

```sql
SELECT name FROM users WHERE age >= 35;
SELECT name, city FROM users WHERE salary > 100000;
```

## Tests
```bash
make test          # brief mode — failures + summary
make test-verbose  # full per-test output
```

## Not Yet Supported

- `JOIN` across multiple tables
- `GROUP BY` / aggregation functions
- Optimization
- And more if time permits
