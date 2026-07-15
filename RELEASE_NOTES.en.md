# v2.10.0 Release Notes

`v2.10.0` adds parsing, structured traversal, and rewrite support for SQL
Server DML `OUTPUT`, nested DML, and `IF...ELSE` control flow. The same
capabilities are available in Vastbase-SQLServer compatibility mode.

## Highlights

- Supports SQL Server `INSERT` with explicit or omitted `INTO`.
- Supports `OUTPUT` and `OUTPUT ... INTO` on `INSERT`, `UPDATE`, `DELETE`, and
  `MERGE`.
- Query Graph represents client/sink result channels, `INSERTED` / `DELETED` /
  source-field origins, and nested-DML parentage.
- Adds selectors for DML result targets, sink relations, and sink columns;
  these selectors are writable through the unified patch API.
- Supports single-statement, multi-statement, and nested `IF...ELSE` forms.
  Control conditions and branch SQL are traversable through public C structs.
- The SQL Server and Vastbase-SQLServer dialect matrices each contain 546
  cases: 517 supported paths and 29 error or explicitly unsupported paths.

## Compatibility

- New APIs and enum values are append-only additions.
- Existing public function signatures and public structure layouts remain
  unchanged.
- View JSON adds optional `control_flow`, DML `result_channels`, and `children`
  members only for matching statements.
- The shared-library ABI major remains `libsqlparser.so.0`; the ABI export
  check covers 146 public symbols.

## Release Validation

- Strict GCC 8.3 build and full test suite
- ASan, UBSan, and Valgrind memory checks
- ABI export check
- Full Windows VS 2022 x64/MSVC 19.39 test suite

Vendored `libpg_query` tag: `17-6.2.2`.
