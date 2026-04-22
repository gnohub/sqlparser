# Examples Guide

The `examples/` directory contains small programs built on top of the public header `sqlparser/sqlparser.h`.

## Example List

- `examples/01_select_inspect.c`
  Demonstrates `SELECT` parsing, relation inspection, name traversal, and `WHERE` literal inspection.
- `examples/02_insert_values_replace_literal.c`
  Demonstrates `INSERT ... VALUES` column lookup, cell access, and literal replacement.
- `examples/03_insert_select_inspect.c`
  Demonstrates structural inspection for `INSERT ... SELECT`.
- `examples/04_update_replace_assignment.c`
  Demonstrates reading and rewriting `UPDATE SET` assignments and `WHERE` literals.
- `examples/05_delete_inspect.c`
  Demonstrates target-relation lookup and conditional rewrite for `DELETE`.
- `examples/06_ddl_inspect.c`
  Demonstrates DDL node inspection, name traversal, and object-name rewrite.
- `examples/07_multi_statement_walk.c`
  Demonstrates traversal of multi-statement input.
- `examples/08_model_roundtrip.c`
  Demonstrates model JSON export, patch application, and SQL regeneration.
- `examples/09_expression_rewrite.c`
  Demonstrates reading and rewriting arbitrary `UPDATE` assignment expressions and `INSERT` cell expressions, including `DEFAULT`.

## Build

```bash
make examples
```

## Run

Example:

```bash
./bin/examples/02_insert_values_replace_literal
```

## Notes

- The examples use only public APIs.
- The source files include inline comments that describe the key steps.
- For complete API details, see [API Reference](../doc/api_reference.en.md).
