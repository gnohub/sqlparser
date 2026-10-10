# v2.17.3 Release Notes

Version 2.17.3 improves batch INSERT preprocessing, query graph construction and string rewriting, reducing repeated scans, copying and allocations. Public APIs, public structure layouts and the 2.17.0 calling rules remain unchanged.

## Improvements

- Optimize value storage and query graph construction for Oracle-family multi-table inserts by reusing source spans and identifier information. Reduce input copying and intermediate processing in batched string replacement while retaining commit validation.
- Optimize ordinary INSERT preprocessing in PostgreSQL and compatible dialects, reducing token-by-token processing when no dialect conversion is needed.
- Reuse parsing and validation information for SQL Server-family batch INSERTs and Dameng scalar INSERTs, reducing repeated scans during query graph construction.
- Optimize MySQL string replacement lookup, short literal storage and scalar VALUES location decoding, reducing repeated reads and allocations.
- Retain generic processing and validation for ineligible inputs.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for check commands and reproduction.
