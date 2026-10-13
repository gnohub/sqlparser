# v2.17.4 Release Notes

Version 2.17.4 improves batch INSERT processing, expression-fragment rewriting and name enumeration, reducing repeated parsing, serialization and allocations. An optional bulk name reader is added; existing function signatures, public structure layouts and calling rules remain unchanged.

## Improvements

- Optimize eligible MySQL batch INSERT storage and string replacement, reducing intermediate representations and copying.
- Optimize AST construction, fragment parsing and deparsing by reusing validated structures. Reuse the AST and update source locations for eligible PostgreSQL literal replacements.
- Reduce inactive-field scans in name, WHERE and identifier traversal. Add `sqlparser_statement_names()` to enumerate a statement's name atoms in one call.
- Fix rewriting fidelity for certain delimited identifiers, generated aliases and national character literals.
- Preserve existing fast-parser admission; retain generic processing and validation for ineligible inputs.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for check commands and reproduction.
