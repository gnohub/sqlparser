# v2.17.1 Release Notes

Version 2.17.1 improves batch INSERT parsing, query graph construction and string rewriting, and fixes related literal-text lifetimes. Public function signatures, structure layouts, exports and the 2.17.0 calling rules remain unchanged.

## Improvements and Fixes

- Optimize preprocessing, parsing, query graph construction and string rewriting for eligible single-statement batch INSERTs, reducing repeated parsing and intermediate AST materialization.
- Optimize INSERT validation and batch string replacement in SQL Server and compatible dialects, reusing validation evidence and combining source edits for eligible statements while retaining final validation.
- Optimize string commits, SQL value function classification and identifier-origin handling for Oracle-family multi-table inserts.
- Reduce repeated traversal, allocation and copying in serialization, selector resolution and source edits. Ineligible inputs retain generic processing and validation.
- Fix the lifetime of some literal text in Oracle-family multi-table insert graphs.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for check commands and reproduction.
