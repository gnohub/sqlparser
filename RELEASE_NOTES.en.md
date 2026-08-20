# v2.16.6 Release Notes

Query Graph adds relation `alias_quoted_identifier` and target `output_quoted_identifier` to report whether an alias or output name uses an identifier delimiter; View JSON emits each field only when its value is `true`. An explicit output alias takes precedence. Without one, an output name taken directly from a field inherits that field's delimiter state. Double quotes, backticks, and brackets are recognized; a `U&` prefix is not counted separately.

This release adds no public export symbols, dynamic allocations, or resource-ownership rules. On the supported x86_64 and AArch64 layouts, existing member offsets and `sizeof` remain unchanged for the affected structures. With nine final cases and 18 patches added, the nine fixtures now contain 2,831 final cases and 9,136 patches. The full remote `make test` suite passed; the ABI/export check remains at 154 public symbols, and the targeted identifier Valgrind check reported `0 bytes in 0 blocks` and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
