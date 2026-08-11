# v2.15.4 Release Notes

`v2.15.4` routes the existing public mutators through one patch transaction
path and preserves each dialect's pagination family when a full-AST deparse is
required.

## Unified Mutation Transactions

- The 20 statement/index mutators and 20 selector mutators remain available
  with unchanged signatures and calling conventions.
- Every convenience mutator now builds an equivalent patch and executes it on
  the transaction candidate used by `sqlparser_apply_patch()`. Patch dispatch
  calls only internal in-place primitives, so it does not recurse through
  public mutators or maintain a second commit path.
- Each call commits at most once. A failure or effective no-op leaves the
  original handle, generation, and derived caches unchanged; an actual change
  increments the generation once. If any item in a patch list fails, the whole
  list rolls back.
- Unused direct structured-mutation helpers and the second candidate clone for
  multi-table INSERT cells were removed.

## Pagination Families

- Private `SelectStmt` / protobuf state in the vendored parser distinguishes
  `LIMIT`, `OFFSET ... ROWS`, `FETCH FIRST`, and `FETCH NEXT`. This state is
  used only for the AST lifecycle and deparse; it adds no public View, Query
  Graph, or C API field.
- When a local source edit is available, unchanged regions remain
  byte-preserved. A full-AST deparse does not guarantee original whitespace,
  case, or `ROW` / `ROWS` spelling, but it keeps a valid pagination family for
  the selected dialect.
- Pagination behavior for the project's nine dialect entry points is:
  - PostgreSQL / Vastbase-PostgreSQL retain the input `LIMIT` or standard
    `OFFSET ... FETCH` family.
  - MySQL / Vastbase-MySQL retain the `LIMIT` family, including existing
    dialect state for the comma offset/count form.
  - Oracle / Vastbase-Oracle keep `OFFSET ... ROWS` and
    `[OFFSET ... ROWS] FETCH FIRST|NEXT ... ROWS ONLY` after a full deparse and
    no longer emit `LIMIT`.
  - SQL Server / Vastbase-SQLServer keep `OFFSET ... ROWS` /
    `OFFSET ... FETCH`; `TOP` continues to use independent dialect state.
  - Dameng continues to distinguish `TOP`, `LIMIT`, and standard
    `OFFSET ... FETCH`. A full-AST fallback may canonicalize
    `LIMIT offset,count` to the semantically equivalent
    `LIMIT count OFFSET offset`.
- The Vastbase entries above describe the executable contract of this
  project's compatibility entry points; they do not infer an official
  Vastbase server grammar guarantee.
- This release does not add `FETCH ... PERCENT` semantics. Existing SQL Server
  and Dameng `TOP ... PERCENT [WITH TIES]` support is unchanged.

## Oracle Projection Rewrite Scenario

Replacing a projection in the following Oracle statement no longer allows the
pagination tail to become `LIMIT 1000 OFFSET 0`:

```sql
SELECT "APP"."T".*,
       ROWID "NAVICAT_ROWID"
FROM "APP"."T"
OFFSET 0 ROWS FETCH NEXT 1000 ROWS ONLY
```

- When the projection source interval is reliable, only that interval is
  replaced and the pagination tail remains byte-preserved.
- Even if a later mutation makes the local source state incomplete and forces
  whole-statement AST generation, the output remains in the Oracle
  `OFFSET ... FETCH ... ONLY` family.

## API and Compatibility

- `sqlparser_apply_patch()` is the recommended mutation gateway. Existing
  statement, selector, and structured convenience mutation functions remain
  available and retain their public argument validation. After conversion to a
  patch, they share atomic rollback, generation updates, and derived-cache
  invalidation rules.
- This release adds or removes no public functions, public enum values, public
  structure fields, or resource-ownership rules. The shared-library ABI major
  remains `libsqlparser.so.0`.

## Validation

- No fixture cases were added. The nine fixtures still contain 2,781 final
  cases and 9,034 patches, and the full `make test` suite passed.
- Targeted full-AST fallback regressions cover PostgreSQL /
  Vastbase-PostgreSQL `LIMIT`, nested and comma-form MySQL / Vastbase-MySQL
  `LIMIT`, Oracle / Vastbase-Oracle `FETCH`, SQL Server /
  Vastbase-SQLServer `OFFSET ... FETCH`, and Dameng `TOP` / `LIMIT` with mixed
  `FETCH` / `TOP` ownership.
- Targeted Valgrind checks for the core API, identifier spelling, robustness,
  and pagination dialect state each reported `0 bytes in 0 blocks` and zero
  errors.
- This protobuf generation verification used protoc 25.1 and protoc-gen-c
  1.5.1. The generator explicitly preserves existing `SelectStmt` field
  numbers and emits the pagination field, enum, and `String.location`.

Vendored `libpg_query` tag: `17-6.2.2`.
Vendored Jansson version: `2.15`.
