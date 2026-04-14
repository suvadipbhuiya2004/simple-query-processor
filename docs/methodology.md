# Project Methodology

This document explains, in simple terms, how this project works and how queries are handled.

## 1. Start With Data and Metadata

- Table rows are stored in CSV files inside the data folder.
- Table schema and constraints are stored in metadata.json.
- On startup, metadata and CSV files are loaded into memory.

## 2. Read SQL Input

- SQL statements are read from queries.sql (or passed directly).
- The script is split into individual statements.
- Statement line and column positions are tracked for better error messages.

## 3. Convert SQL Text to Structure

- Lexer: converts raw SQL text into tokens.
- Parser: converts tokens into AST objects (structured statement trees).

## 4. Choose the Execution Path

- If statement is SELECT:
  - Build a logical plan.
  - Run optimizer rules (pushdown, simplification, pruning, join choices).
  - Build executors and run them to produce result rows.
- If statement is CREATE, ALTER, INSERT, UPDATE, DELETE:
  - Validate types and constraints.
  - Apply schema/data changes in memory.
  - Persist updates to CSV and metadata.json.

## 5. Enforce Correctness Rules

- Type checks: INT, VARCHAR, TEXT, BOOLEAN, FLOAT, DOUBLE, TIMESTAMP, ENUM.
- Constraint checks: PRIMARY KEY, UNIQUE, NOT NULL, FOREIGN KEY, CHECK.
- Validation is done before final persistence.

## 6. Return Output

- SELECT results are formatted and printed as tuples.
- Mutation queries print rows affected or success messages.
- Script mode prints a final execution summary.

## 7. Keep It Reliable

- Tests cover lexer, parser, execution, aggregation, and ALTER behavior.
- Metadata normalization keeps schema representation consistent.
- SQL demo scripts are written to be rerunnable where possible.

## 8. Implementation Style

- Correctness first, then optimization.
- Rule-based optimization with cost-based join decisions.
- Clear layer separation: parser, planner, optimizer, execution, storage.
