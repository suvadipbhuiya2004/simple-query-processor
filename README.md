# Simple Query Processor

Implements a minimal SQL pipeline in three layers:
```
Parser → AST → Planner → Plan Tree → Execution
```

## Supported Syntax
```sql
SELECT <column_list | *> FROM <table> [WHERE <expr>]
```
```sql
SELECT * FROM users;
SELECT name FROM users WHERE age > 30;
SELECT name, age FROM users WHERE age >= 35;
SELECT name, city, department FROM users WHERE salary > 90000;
```

Supported WHERE operators: `=` `==` `!=` `<>` `>` `<` `>=` `<=`

## Build & Run
```bash
make run                                          # build and run
make run RUN_ARGS='"SELECT name FROM users"'      # with a custom query
```

Or manually:
```bash
cmake -S . -B build && cmake --build build
./build/query_engine "SELECT name FROM users WHERE age >= 35"
```

## Tests
```bash
make test          # brief mode — failures + summary
make test-verbose  # full per-test output
```

## Not Yet Supported

- `JOIN` across multiple tables
- `GROUP BY` / aggregation functions
- `ORDER BY` / `LIMIT`
- `AND` / `OR` with parenthesised expressions
- Optimization
- And more if time permits
