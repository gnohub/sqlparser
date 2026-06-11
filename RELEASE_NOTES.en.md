# v2.8.0 Release Notes

`v2.8.0` expands Query Graph, DML structured output, and multi-dialect
coverage. The release focuses on Oracle P3 structured output, MySQL / SQL
Server / Dameng / Vastbase compatibility coverage, and public preservation of
national / Unicode string forms.

## Highlights

- Updated the public version to `2.8.0`.
- Added Query Graph predicates for comparisons, boolean combinations, `EXISTS`,
  and expression predicates.
- Added source field / source target links to DML assignments, cells, and
  branches for field movement, `MERGE` source lineage, and multi-branch insert
  sources.
- Expanded Oracle coverage for alias-qualified `UPDATE`, `INSERT ALL` /
  `INSERT FIRST`, `MERGE` source lineage, `DISTINCT`, `ORDER BY`, and qualified
  star plus `ROWID`.
- Expanded MySQL, SQL Server, Dameng, and Vastbase compatibility coverage for
  syntax that can be safely represented by the current structures.
- MySQL, Oracle, PostgreSQL, Dameng, and matching Vastbase compatibility modes
  preserve public `N'...'` / `n'...'` national string forms.
- Oracle, Dameng, and Vastbase-Oracle preserve `nq'...'` national q-quoted
  string semantics.
- The public header layout changed. Rebuild callers with this version's header
  before linking against this version of the library.

## Release Validation

This release validation includes:

- `git diff --check`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Current ABI exported symbols: `135`
- Vendored `libpg_query` tag: `17-6.2.2`
