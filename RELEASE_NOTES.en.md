# v2.16.12 Release Notes

Query Graph exposes function or opaque RHS expressions for `WHERE`, `ON`, and `HAVING` predicates. Functions expose a normalized name, ordered literal/bind/field/expression arguments, and selectors; opaque expressions support whole-expression replacement.

`expression` and `expression_arg` selectors support replacement, while `expression_args` supports function-argument insertion and deletion. Functions are treated as variadic; only selector/index validity and result-SQL parseability are checked, not function signatures, arity, or argument types.

This release adds read-only expression/argument APIs, three selector kinds, and two patch operations. Existing public structure layouts and ownership rules remain unchanged, with 162 public symbols.

Fifty-four final cases and 196 patches bring the nine fixtures to 3,062 final cases and 9,621 patches. The full `make test` suite, all nine dialect matrices, CLI checks, and the ABI export check passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
