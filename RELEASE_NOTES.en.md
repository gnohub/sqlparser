# v2.11.0 Release Notes

`v2.11.0` adds source-spelling preservation for identifiers during deparse.

## Highlights

- Unchanged database, schema, table, column, alias, function, index,
  constraint, and CTE identifiers retain their source SQL spelling.
- Unquoted identifiers retain their original case instead of being uniformly
  normalized by the deparser.
- Text inside comments, strings, bind parameters, and quoted identifiers is
  not mistaken for a source identifier.
- Nodes created by a patch do not inherit spelling from unrelated source
  nodes with the same name.

## Compatibility

- The public API, public structures, View JSON, and Query Graph remain
  unchanged.
- The shared-library ABI major remains `libsqlparser.so.0`; the ABI export
  check covers 146 public symbols.

## Release Validation

- Strict GCC 8.3 build and full test suite
- ASan, UBSan, and Valgrind memory checks
- ABI export check
- Per-call performance and allocation measurements

Vendored `libpg_query` tag: `17-6.2.2`.
