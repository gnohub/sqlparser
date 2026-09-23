# v2.16.17 Release Notes

Corrected string literal output for SQL Server, Oracle, Dameng, and their corresponding compatibility entries, preserving backslashes and national-string `N` prefixes in complete SQL, fragment reads, and source copies.

Fixed backslash escaping in string literal rewrites for MySQL and its compatibility entries, preventing changes to the supplied string value or invalid SQL output.

Public APIs, public structure layouts, View JSON fields, and ownership rules remain unchanged.

New string regressions cover all 13 dialect entries, including statement and fragment output, string values, source copies, batch ordering, and rollback. The full `make test` suite passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
