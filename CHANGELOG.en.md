# Changelog

## Unreleased

### SQL View and Rewrite

- Added generic `SELECT` output-list read, replace, insert, and delete APIs
- Added generic `WHERE` condition read, set, and `AND` / `OR` append APIs
- Added generic statement-level `clause` selectors for rewriting `select_list`,
  `where`, and `order_by` through `stmt[n].clause[m]`
- Added `statements[].clauses[]` to SQL View JSON for writable
  statement-level clause slots

### Tests and Documentation

- Added examples for `SELECT` output-list and `WHERE` condition rewrites
- Added a generic `clause` patch example covering SELECT output lists, WHERE
  conditions, and ORDER BY insertion
- Grouped examples into `patch`, `convenience`, `inspect`, and `dialect`;
  integration code should start with the `patch` examples
- Added WHERE rewrite regression coverage for PostgreSQL, MySQL, Oracle, and
  SQL Server, covering every PostgreSQL AST type that exposes `where_clause`

## 0.3.0

### Dialect Capabilities

- Added PostgreSQL session-schema context output for `SET search_path`,
  `SET LOCAL search_path`, and `SET SCHEMA`
- Added MySQL `USE db_name` default-database switching
- Added SQL Server `USE database_name` database-context switching
- Added Oracle `ALTER SESSION SET CURRENT_SCHEMA`, `ALTER SESSION SET
  CONTAINER`, and `ALTER SESSION SET CONTAINER ... SERVICE ...`
- Fixed context-switch handling in multi-statement input so parse, SQL View
  JSON, and deparse stay in the public dialect form

### SQL View and Rewrite

- Session-context statements reuse the existing
  `statements[].objects[].columns[].value` structure; no separate JSON format is
  introduced
- `stmt[n].value[m]` selectors can rewrite context-switch targets and deparse
  back to the corresponding dialect SQL
- Fixed edge cases where SQL Server, MySQL, and Oracle deparse could expose
  internal `sqlparser_current_*` sentinel names

### Tests and Documentation

- Added multi-statement context-switch regression cases for MySQL, Oracle, and
  SQL Server
- Updated dialect support docs, official syntax coverage checklists, and
  executable coverage summaries

## 0.2.0

### Core Capabilities

- Stable public C API for `sql -> handle -> rewrite -> deparse`
- Parsing and structural inspection for `SELECT`, `INSERT`, `UPDATE`,
  `DELETE`, `MERGE`, transaction control, and common DDL
- Precise rewrites for relation names, name atoms, literals, `WHERE` literals,
  `UPDATE` assignments, and `INSERT` cells
- Expression-level rewrite support for `DEFAULT` and arbitrary SQL expressions
- SQL View JSON export, SQL View C structured traversal, and structured patch
  write-back
- Diagnostic SQL View JSON export on demand
- Configurable resource limits for SQL input, expression SQL fragments,
  generated output, and statement count
- Dialect framework with PostgreSQL as the default and MySQL / Oracle /
  SQL Server dialect conversion layers
- Reduced the default generated-output limit to 4 MB and removed avoidable
  resident AST and string copies from parse/deparse paths

### Packaging and Build

- Pinned vendored `libpg_query` version stored in the repository
- Public release surface for the header, static library, shared library, and
  `pkg-config` metadata
- Strict-build, install-smoke, `valgrind` leak-check, loop-regression,
  benchmark-smoke, and one-shot `verify` entry points
- Build invalidation based on compiler-option signatures for project objects and
  vendored parser objects
- Added `make abi-check` to verify shared-library exports against the public
  header
- Added Linux/GCC GitHub Actions CI gates
- Extended CI with JSON fixture validation and source-package smoke
- Added `make dist` for source release packages
- Added a Windows/MSVC NMake build entry point for the static library, CLI, unit
  tests, and examples
- The Windows/MSVC build uses the vendored Jansson source and does not require an
  external package manager

### Tests and Performance

- Expanded SQL fixture coverage for subqueries, `CASE`, window functions,
  `ON CONFLICT`, `RETURNING`, `UPDATE ... FROM`, `DELETE ... USING`, `MERGE`,
  transaction control, common DDL, `GRANT/REVOKE`, and maintenance statements
- Added a MySQL dialect case matrix covering supported statement shapes and
  explicitly unsupported syntax
- Added an Oracle dialect case matrix covering supported statement shapes,
  public output rules, and explicitly unsupported syntax
- Added a SQL Server dialect case matrix covering supported T-SQL statement
  shapes, public output rules, and explicitly unsupported syntax
- Added installed-library API smoke coverage, `valgrind` leak checks, and
  expression-rewrite regression
- Added stability regression for malformed SQL, argument validation, resource
  limits, and failed-rewrite rollback
- Extended benchmarks for read paths, rewrite paths, and `rewrite + deparse`
  single-call measurements
- Added capability-grouped test entry points for parse, inspect, rewrite,
  deparse, SQL View JSON, CLI, install smoke, and ABI

### Documentation

- Chinese and English quick-start guides, API reference, SQL View JSON guide, CLI
  guide, and architecture guide
- Added Oracle and SQL Server dialect support notes and `v0.2.0` release notes
- Added public changelog
