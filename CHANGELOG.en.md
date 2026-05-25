# Changelog

## Unreleased

## 0.8.0

### Dialect Capabilities

- Added Oracle ordinary `ALTER SESSION SET <parameter> = <value>` session parameter assignments for string, identifier, numeric, and boolean/enumerated values
- Kept Oracle `ALTER SESSION` public output in the original parameter/value form without exposing internal conversion prefixes
- Fixed MySQL parameterized comma pagination for `LIMIT ?, ?` while preserving the public MySQL deparse form

### Tests and Coverage

- Expanded the existing PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case matrices for additional DDL, DML, JOIN, function, expression, bind, pagination, and context-switching scenarios
- Added Oracle regression coverage for `ALTER SESSION SET NLS_DATE_FORMAT`, `NLS_DATE_LANGUAGE`, `NLS_NUMERIC_CHARACTERS`, `INSTANCE`, and `ERROR_ON_OVERLAP_TIME`
- Updated the Chinese and English case matrices, dialect coverage summary, and Oracle official syntax coverage summary

## 0.7.0

### UPDATE SET Rewrite

- Added assignment-level patch support for `UPDATE SET`, allowing callers to append, delete, and replace full assignments through `stmt[n].assignment[i]` selectors
- Added `SQLPARSER_PATCH_INSERT_ASSIGNMENT`, `SQLPARSER_PATCH_DELETE_ASSIGNMENT`, and `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`
- Added `sqlparser_update_insert_assignment_sql()`, `sqlparser_update_delete_assignment()`, `sqlparser_update_set_assignment_full_sql()`, and the corresponding selector APIs
- Preserved the existing RHS-only assignment behavior of `SQLPARSER_PATCH_REPLACE`

### Tests and Documentation

- Added `examples/patch/17_update_set_patch.c` to demonstrate `UPDATE SET` assignment append, delete, and full replacement through `sqlparser_apply_patch()`
- Expanded core API and robustness tests for Oracle bind fragments, invalid selectors, out-of-range indexes, empty-`SET` protection, and handle usability after failures
- Updated the Chinese and English API reference, SQL View JSON guide, examples guide, and MSVC example build list

## 0.6.0

### SQL View Structures

- Made SQL View C structures the canonical structured-output source; `sqlparser_export_view_json()` now serializes JSON on demand from those structures
- Extended `sqlparser_column_view_t` and `sqlparser_cell_view_t` with bind name, bind kind, original bind SQL, bind selector, clause id, and SELECT target path fields
- Added the public `sqlparser_bind_kind_t`, `sqlparser_bind_kind_name()`, `sqlparser_statement_clause_at()`, and `sqlparser_clause_sql()` APIs
- Extended `sqlparser_clause_kind_t` with `on`, `group_by`, and `having` clause kinds
- Removed the old `target_kind`, `target_name`, and `target_arg_index` JSON fields in favor of ordered `target_path` entries for SELECT output hierarchy

### Semantics and Dialects

- Classify binds as positional or named while preserving `bind_sql` for original forms such as `?`, `:1`, `:name`, `$1`, and `@name`
- Stop exposing bind placeholders as ordinary `value` payloads, so callers do not confuse placeholders with literal values
- Stop exposing SELECT output expression operators and values as condition metadata; output shape is represented through `target_path`
- Preserve complete public SQL operator text for `NOT IN`, `NOT LIKE`, `NOT ILIKE`, and `NOT SIMILAR TO`

### Tests and Documentation

- Expanded PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case matrices for additional SELECT, INSERT, UPDATE, DELETE, JOIN, function, expression, and bind scenarios
- Added SQL View public C-structure semantic tests to verify consistency between the public structs and View JSON
- Added generic assertions for bind fields, cell binds, `clause_id`, and `target_path`
- Updated the Chinese and English API reference and SQL View JSON guide

## 0.5.0

### SQL View Semantics

- Added SELECT target semantic paths through `target_path` for functions, expressions, CASE, and nested output hierarchy
- Added clause attribution ids to SQL View JSON so field references can be associated with SELECT, WHERE, JOIN/ON, ORDER BY, and related locations
- Expanded dialect View JSON semantic coverage for function outputs, expression outputs, star outputs, nested SELECT, and bind predicates

## 0.4.0

### Dialect Capabilities

- Added the Dameng `SQLPARSER_DIALECT_DAMENG` conversion layer, covering
  `SET SCHEMA`, `MINUS`, `LIMIT`, `TOP`, binds, common DML/DDL, transactions,
  and privilege statements
- Added Dameng public-output rules so deparse and SQL View JSON do not expose
  internal parameter names or internal conversion SQL
- Added prepared / parameterized SQL coverage for PostgreSQL, MySQL, Oracle,
  SQL Server, and Dameng, including SQL Server `sp_executesql` and Dameng
  `EXEC SQL PREPARE`

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
- Added the Dameng dialect case matrix, official syntax coverage summary, CLI
  batch fixture coverage, and dialect example
- Updated prepared / bind case matrices, dialect coverage summaries, and
  official syntax coverage summaries

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
