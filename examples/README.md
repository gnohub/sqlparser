# Examples Guide

The `examples/` directory contains small programs built on top of the public
header `sqlparser/sqlparser.h`. The examples cover structural inspection,
unified patch-based rewrite, fine-grained convenience APIs, and dialect usage.

## Layout

- `examples/patch/`
  Recommended integration style. Rewrites go through `sqlparser_apply_patch()`.
- `examples/convenience/`
  Fine-grained convenience APIs for callers that already hold exact indexes or
  structured selectors.
- `examples/inspect/`
  Read-only structural inspection and traversal examples.
- `examples/dialect/`
  Dialect parsing, SQL View JSON, patch-based rewrite, and deparse examples.

## Recommended Rewrite Examples

- `examples/patch/08_view_patch.c`
  Demonstrates SQL View JSON export, structured patch application, and SQL regeneration.
- `examples/patch/13_select_target_patch.c`
  Demonstrates expanding `SELECT *`, inserting an output target, and deleting an output target through `sqlparser_apply_patch()`.
- `examples/patch/14_where_patch.c`
  Demonstrates adding a missing `WHERE` clause and appending conditions with `AND` or `OR` through `sqlparser_apply_patch()`.
- `examples/patch/15_insert_columns_patch.c`
  Demonstrates adding and deleting `INSERT ... VALUES` columns through `sqlparser_apply_patch()`.
- `examples/patch/16_clause_patch.c`
  Demonstrates SELECT output-list, WHERE condition, and ORDER BY rewrites through generic `clause` patches.

## Convenience API Examples

- `examples/convenience/02_insert_values_replace_literal.c`
  Demonstrates `INSERT ... VALUES` column lookup, cell access, and literal replacement.
- `examples/convenience/04_update_replace_assignment.c`
  Demonstrates reading and rewriting `UPDATE SET` assignments and `WHERE` literals.
- `examples/convenience/05_delete_inspect.c`
  Demonstrates target-relation lookup and conditional rewrite for `DELETE`.
- `examples/convenience/06_ddl_inspect.c`
  Demonstrates DDL node inspection, name traversal, and object-name rewrite.
- `examples/convenience/09_expression_rewrite.c`
  Demonstrates arbitrary `UPDATE` assignment and `INSERT` cell expression rewrites, including `DEFAULT`.
- `examples/convenience/13_select_target_rewrite.c`
  Demonstrates fine-grained SELECT output-list rewrite APIs.
- `examples/convenience/14_where_convenience.c`
  Demonstrates fine-grained WHERE condition rewrite APIs.

## Inspection and Dialect Examples

- `examples/inspect/01_select_inspect.c`
  Demonstrates `SELECT` parsing, relation inspection, name traversal, and `WHERE` literal inspection.
- `examples/inspect/03_insert_select_inspect.c`
  Demonstrates structural inspection for `INSERT ... SELECT`.
- `examples/inspect/07_multi_statement_walk.c`
  Demonstrates traversal of multi-statement input.
- `examples/dialect/10_mysql_dialect.c`
  Demonstrates parsing MySQL SQL with explicit dialect options and rewriting an `INSERT ... VALUES` cell.
- `examples/dialect/11_oracle_dialect.c`
  Demonstrates parsing Oracle SQL with explicit dialect options, exporting SQL View JSON, and restoring Oracle bind placeholders during deparse.
- `examples/dialect/12_sqlserver_dialect.c`
  Demonstrates parsing SQL Server SQL with explicit dialect options, exporting SQL View JSON, and restoring SQL Server parameters and `TOP` during deparse.
- `examples/dialect/17_dameng_dialect.c`
  Demonstrates parsing Dameng SQL with explicit dialect options, exporting SQL View JSON, and restoring binds, `SET SCHEMA`, and `TOP` during deparse.

## Build

```bash
make examples
```

## Run

Example:

```bash
./bin/examples/patch/16_clause_patch
```

## Notes

- The examples use only public APIs.
- Integration code should usually start with `examples/patch/`.
- The source files include inline comments that describe the key steps.
- For complete API details, see [API Reference](../doc/api_reference.en.md).
