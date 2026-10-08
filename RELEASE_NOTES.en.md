# v2.17.2 Release Notes

Version 2.17.2 improves batch INSERT parsing, query graph construction and string rewriting. Public APIs and the 2.17.0 calling rules remain unchanged.

## Improvements

- Optimize native construction of MySQL scalar INSERTs by reusing completed source validation, reducing repeated scanning and validation.
- Optimize scalar VALUES encoding and temporary write plans, reducing repeated decoding, size calculation and copying.
- Optimize Oracle-family multi-table inserts by reusing source spans and identifier-origin information, and improving branch storage, query graph construction and string rewriting.
- Optimize preprocessing, query graphs and string rewriting for single- and multi-statement SQL Server-family INSERTs and Dameng scalar INSERTs.
- Retain generic processing and validation for ineligible inputs.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for check commands and reproduction.
