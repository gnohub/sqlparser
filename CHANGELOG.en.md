# Changelog

## 0.1.0-dev

### Core Capabilities

- Stable public C API for `sql -> handle -> rewrite -> deparse`
- Parsing and structural inspection for `SELECT`, `INSERT`, `UPDATE`,
  `DELETE`, `MERGE`, transaction control, and common DDL
- Precise rewrites for relation names, name atoms, literals, `WHERE` literals,
  `UPDATE` assignments, and `INSERT` cells
- Expression-level rewrite support for `DEFAULT` and arbitrary SQL expressions
- Export and import support for parse-tree JSON, summary JSON, and stable model
  JSON

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

### Tests and Performance

- Expanded SQL fixture coverage for subqueries, `CASE`, window functions,
  `ON CONFLICT`, `RETURNING`, `UPDATE ... FROM`, `DELETE ... USING`, `MERGE`,
  transaction control, common DDL, `GRANT/REVOKE`, and maintenance statements
- Added installed-library API smoke coverage, `valgrind` leak checks, and
  expression-rewrite regression
- Extended benchmarks for read paths, rewrite paths, and `rewrite + deparse`
  single-call measurements

### Documentation

- Chinese and English quick-start guides, API reference, model JSON guide, CLI
  guide, and architecture guide
- Added compatibility policy and public changelog
