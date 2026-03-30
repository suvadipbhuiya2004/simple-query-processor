# Simple Query Processor

implemented these layers

- Parser -> AST
- Planner -> Plan Tree
- Execution Layer


## Supported Query Shape

Current syntax:

```sql
SELECT <column_list | *> FROM <table> [WHERE <expr>]
```

Examples:

```sql
SELECT name FROM users WHERE age > 30;
SELECT name, age FROM users WHERE age >= 35;
SELECT name, city, department FROM users WHERE salary > 90000;
SELECT * FROM users;
```

Supported WHERE comparison operators:

- `=` / `==`
- `!=` / `<>`
- `>`
- `<`
- `>=`
- `<=`


## Build And Run

From project root:

```bash
cmake -S . -B build
cmake --build build
./build/query_engine
```

Run with a custom query:

```bash
./build/query_engine "SELECT name, age FROM users WHERE age >= 35"
```



## Not Added Yet

These are left for future extension:

- Joins
- Grouping/Aggregation
- Multi-table planning
- Advanced expression grammar (AND/OR precedence, parentheses)
- ORDER BY/LIMIT physical operators