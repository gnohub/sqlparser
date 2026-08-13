# v2.16.3 Release Notes

`v2.16.3` adds handle-level access to the complete bind occurrence list, returning every real placeholder in actual order across the current SQL while preserving repeated occurrences. Each item exposes its `position`, `kind`, `key`, and complete `sql`.

Existing Query Graph bind fields continue to represent semantic associations. Complete enumeration is exposed independently through the new APIs, with no View JSON schema change. The public API change is additive and does not alter existing call paths.

The strict incremental build, two targeted tests, and all nine dialect matrices passed, covering 2,796 final cases and 9,049 patches. The ABI/export check passed with 154 public symbols, and both targeted Valgrind checks reported `0 bytes in 0 blocks` and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
