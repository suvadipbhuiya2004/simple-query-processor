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
make run ARGS='--query "SELECT * FROM users;"'
make run ARGS='--repl'
```

The executable reads one or more SQL statements from `queries.sql` and executes them in order by default.

You can also run it directly with CLI options:

```bash
./build/query_engine --file queries.sql
./build/query_engine --query "SELECT name FROM users WHERE age > 30;"
./build/query_engine --repl
./build/query_engine queries.sql
./build/query_engine --help
```

CLI options:

- `--file <path>`: run statements from a SQL file.
- `--query <sql>`: run one SQL command or a semicolon-separated list.
- `--repl`: start an interactive SQL prompt.
- Positional file path: treated the same as `--file`.
- `--help`: show available options.

Web architecture notes are documented in [docs/web-architecture.md](docs/web-architecture.md).

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

## Web Terminal

You can run the SQL processor in a browser SQL workspace UI (Programiz-style layout).

```bash
make web-install
make web-run
```

`make web-install` creates a local virtual environment in `.venv/` and installs web dependencies there.

Then open:

```text
http://127.0.0.1:5000
```

The page has three panes:

- Left: schema explorer for main and sandbox tables.
- Center: SQL editor + output table.
- Right: table preview cards with sample rows.

All pane content is scrollable so large schemas and result sets fit cleanly.

### Main vs Sandbox

- `Main` target: read-only web access to your original project data (SELECT only).
- `Sandbox` target: temporary per-user SQLite database where users can run `CREATE`, `INSERT`, `UPDATE`, `DELETE`, and `SELECT` without affecting your real data files.
- `Reset Sandbox` recreates a fresh temporary workspace.

Supported shell-like commands from the old web terminal API are still available on `/api/execute`, but the default UI is now SQL-workspace focused.

Example sandbox workflow:

```sql
CREATE TABLE demo(id INTEGER, name TEXT);
INSERT INTO demo VALUES (1, 'alpha'), (2, 'beta');
SELECT * FROM demo;
```

Example main query:

```sql
SELECT name, age FROM users WHERE age > 40;
```

The preview panel shows a few rows from each table so users can understand table contents quickly.

## Not Yet Supported

- `JOIN` across multiple tables
- `GROUP BY` / aggregation functions
- Optimization
- And more if time permits
