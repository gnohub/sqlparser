# v2.17.7 Release Notes

Version 2.17.7 fixes identifier delimiter metadata loss during assignment rewriting.

## Changes

- Preserve target identifier delimiters and `quoted_identifier` when adding assignments through `name + source_selector`, including subsequent single and batch edits.
- Retain identifier origin mappings during Dameng SQL-fragment preprocessing and add related JSON regression coverage across all 13 dialect entries.

## API Compatibility

- Public function signatures, structure layouts, selectors, and calling rules remain unchanged from 2.17.6.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for check commands and reproduction.
