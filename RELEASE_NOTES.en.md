# v2.17.5 Release Notes

Version 2.17.5 adds predicate-to-subquery links, extends DELETE alias handling, and fixes SQL Server transaction output after rewriting.

## Changes

- Link direct-field `IN/NOT IN` predicates to their RHS query blocks across all 13 dialect entries, reusing existing ordered output targets, fields, values, and selectors. `NOT IN` retains its existing `NOT` parent structure.
- Extend single-target DELETE alias handling in MySQL, SQL Server, Dameng, and related compatibility entries, including target resolution and rewriting within multi-table sources.
- Preserve the SQL Server `TRANSACTION` keyword after rewriting mixed transaction batches, including related compatibility entries.
- Add related JSON regression cases, consolidate Oracle tests into the shared test layout, and merge duplicate reference implementations.

## API Compatibility

- Add `has_source_block` and `source_block_index` to `sqlparser_graph_predicate_t`, returned by the existing `sqlparser_query_graph_predicate_at()` function. View JSON exposes the link as `source_block`.
- No public functions are added; existing function signatures and calling rules remain unchanged. The public structure layout is extended, so callers must recompile against the updated header.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for check commands and reproduction.
