# v2.10.1 Release Notes

`v2.10.1` fixes deparse ordering for MySQL and Vastbase-MySQL index hints.

## Highlights

- `USE INDEX`, `IGNORE INDEX`, `FORCE INDEX`, and their `KEY` forms remain
  after the table name or alias and before subsequent query clauses.
- Fixed index-hint placement with grouping, window, set-operation, locking,
  and JOIN clauses.
- Fixed index-hint restoration for the right relation of `STRAIGHT_JOIN`.
- The MySQL and Vastbase-MySQL dialect matrices each contain 173 supported
  cases.

## Compatibility

- The public API, public structures, and View JSON remain unchanged.
- The shared-library ABI major remains `libsqlparser.so.0`; the ABI export
  check covers 146 public symbols.

## Release Validation

- Strict GCC 8.3 build and full test suite
- ASan, UBSan, and Valgrind memory checks
- ABI export check
- Full Windows VS 2022 x64/MSVC 19.39 test suite

Vendored `libpg_query` tag: `17-6.2.2`.
