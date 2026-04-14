# Simple Query Processor

A lightweight SQL engine in C++ with parser, planner, optimizer, and executor layers.

Architecture flow:

```text
SQL -> Lexer + Parser -> AST -> Planner -> Optimizer -> Executors
```

## Build and Run

```bash
make build        # Compile
make run          # Run queries.sql
make test         # Run unit tests
make test-verbose # Verbose unit tests
make rebuild      # Clean and rebuild
make clean        # Remove build artifacts
```

## Project Data Files

- Metadata catalog: data/metadata.json
- Table data: data/*.csv
- SQL workload: queries.sql

## metadata.json Structure

Top-level shape:

```json
{
	"tables": {
		"<table_name>": {
			"file": "<table_file>.csv",
			"columns": [
				{
					"name": "<column_name>",
					"type": "<type>",
					"primary_key": true,
					"unique": true,
					"not_null": true,
					"foreign_key": "<ref_table>.<ref_column>",
					"check": { "type": "enum", "values": ["A", "B"] }
				}
			],
			"table_checks": [
				"<sql_boolean_expression>"
			]
		}
	}
}
```

Column field meanings:
- name: column name
- type: normalized SQL type string (for example INT, VARCHAR(50), TIMESTAMP)
- primary_key: marks column as part of primary key
- unique: unique constraint
- not_null: disallow empty value
- foreign_key: reference in table.column format
- check: column-level validation rule

Supported check object formats:

```json
{ "type": "enum", "values": ["ok", "warn", "fail"] }
```

```json
{ "type": "range", "min": 0, "max": 100 }
```

```json
{ "type": "comparison", "operator": ">", "value": 0 }
```

```json
{ "type": "expression", "sql": "score >= 0 AND score <= 100" }
```

Notes:
- table_checks is optional.
- Legacy string check values are still readable, but object format is preferred.
- CREATE TABLE and ALTER TABLE keep metadata.json in sync with table schema.

## More Details

See [features.md](docs/features.md) for what more features support
