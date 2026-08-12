# v2.16.1 Release Notes

`v2.16.1` reduces each internal Query Graph DML-cell record from 456 B to 80 B. Retained memory for 20,000 literal cells fell from 14.659 MiB to 1.685 MiB, an 88.504% reduction. The public API, View JSON, and DML semantics are unchanged.

Relevant regressions, five targeted Valgrind Memcheck runs, and the ABI check with 152 public symbols passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
