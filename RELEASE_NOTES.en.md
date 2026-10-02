# v2.17.0 Release Notes

Version 2.17.0 improves parsing and rewriting performance and memory use. Public function signatures, structure layouts and exports are unchanged, but failure handling and borrowed-view lifetimes are incompatible with 2.16.x.

## Memory and Implementation

- Encode protobuf directly from the parser's raw tree to reduce temporary intermediate objects. Unsupported structures retain the original conversion and validation path.
- Reduce copies of dialect state, structural rows and internal SQL, and shorten the lifetime of old ASTs, query graphs and serialized buffers.
- Remove whole-handle rollback copies. Required syntax, shape and resource-limit checks remain; memory and timing improvements depend on the workload and environment.

## Calling Rules

- Reuse a successfully parsed handle across apply/deparse rounds. Successful deparse does not destroy the handle; each returned SQL string is independently allocated and freed with `sqlparser_string_free()`.
- After apply or deparse failure, internal state is released. Destroy the handle exactly once; do not inspect, retry or mutate it. To recover, parse a new handle from caller-saved SQL.
- Every nonempty apply invalidates old borrowed views, including identical-value and modify-then-undo batches. Reacquire views after success. Only a successful empty patch list preserves them.
- Caller SQL remains unchanged; source selectors read the current state in execution order. Previously returned independent SQL/JSON strings remain valid across later handle edits or destruction.
- Convenience mutation APIs follow the same failure-handling requirement. Concurrent mutation of one handle is unsupported.

## Validation

- Linux AArch64 build, full `make test` and ABI checks passed, retaining 162 public exports.
- Fourteen related unit programs and both 5,000-row full pipelines passed Valgrind: zero errors and zero bytes in zero blocks at exit, with no suppressions.

See the [Test guide](tests/README.en.md) and [Benchmark guide](bench/README.en.md) for the check list and reproduction.
