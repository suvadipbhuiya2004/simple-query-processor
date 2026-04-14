# Features

This file keeps a simple snapshot of what the engine can do today.

## Implemented

### Query Modes
- SELECT (execute and return tuples)
- PATH SELECT (show execution path used by engine)

### Core SQL
- CREATE TABLE
- ALTER TABLE
- INSERT
- UPDATE
- DELETE

### SELECT Clauses
- WHERE
- DISTINCT
- JOIN (INNER, LEFT, RIGHT, FULL, CROSS)
- GROUP BY
- HAVING
- ORDER BY (ASC, DESC)
- LIMIT
- Subqueries in WHERE:
	- IN (SELECT ...)
	- NOT IN (SELECT ...)
	- EXISTS (SELECT ...)

### Aggregates
- COUNT(*)
- COUNT(a)
- COUNT_DISTINCT(a)
- COUNT_DISTINCT(a, b)
- SUM(a)
- AVG(a)
- MIN(a)
- MAX(a)

### ALTER TABLE
- Add column
- Drop column
- Rename column
- Change datatype
- Add constraints

### Constraints
- PRIMARY KEY
- UNIQUE
- NOT NULL
- FOREIGN KEY
- CHECK

### Types
- INT
- VARCHAR(n)
- TEXT
- BOOLEAN
- FLOAT
- DOUBLE
- TIMESTAMP
- ENUM('x','y',...) (internally TEXT + CHECK)

### PATH SELECT Output
- Numbered execution path steps
- Join type + selected join algorithm + ON condition
- Sort key + direction
- Final projected columns
- Subquery path(s) for IN / NOT IN / EXISTS

### Optimizer / Execution (implemented)
- Predicate pushdown
- Projection pruning
- Filter simplification and merge
- Adaptive join algorithm choice
- Index-aware equality access path (in-memory hash index cache)
- Cost-based inner join reordering (current scoped model)

## Not Implemented

### SQL / Language
- UNION / EXCEPT / INTERSECT
- WITH (CTE)
- Window functions
- CASE expression support
- Persistent CREATE VIEW objects

### Database Features
- Transactions (ACID semantics)
- Stored procedures / triggers
- Persistent disk index structures (B-tree/hash)
- Full concurrency and locking model

## Quick Notes
- `queries.sql` includes ALTER TABLE examples for all supported ALTER operations.
- `queries.sql` includes PATH SELECT examples across joins, aggregates, and subqueries.
- In PATH output, a filter may appear under `SEQ_SCAN` when predicate pushdown is applied.
- Test suite currently has 109 tests.
