# Features

This file keeps a simple snapshot of what the engine can do today.

## Implemented

### Core SQL
- SELECT
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

### Database Features
- Transactions (ACID semantics)
- Views
- Stored procedures / triggers
- Persistent disk index structures (B-tree/hash)
- Full concurrency and locking model

## Quick Notes
- `queries.sql` includes ALTER TABLE examples for all supported ALTER operations.
- `queries.sql` also includes a larger bulk insert workload for runtime testing.
- Test suite currently has 107 tests.
