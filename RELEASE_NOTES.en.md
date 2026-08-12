# v2.16.2 Release Notes

`v2.16.2` extends DML result-receiver lists in Oracle, the Vastbase-Oracle compatibility entry, and Dameng from one pair to `N >= 1` targets paired ordinally with exactly N colon-prefixed host binds. The Oracle family uses `RETURNING ... INTO`; Dameng uses `RETURNING` for `INSERT` and `DELETE` and `RETURN` for `UPDATE`. Query Graph continues to use the existing sink result channel, with each target's `sink_value` pointing to the output bind at the same ordinal; no public View field is added.

Existing `SQLPARSER_PATCH_INSERT_COLUMN` can now atomically insert a pair into `dml_result_targets`: `index` selects the common position, `default_sql` supplies the target SQL, and `name` supplies the receiver. Oracle, Dameng, and Vastbase-Oracle use a colon-bind receiver. SQL Server and Vastbase-SQLServer use a sink-column receiver for `OUTPUT ... INTO` with an explicit nonempty sink column list whose count equals the OUTPUT target count before mutation. Every failure rolls back with the transaction candidate, leaving the original handle and generation unchanged.

The release boundary is:

- Oracle and Vastbase-Oracle `INSERT`, `UPDATE`, and `DELETE` support equal-length `RETURNING ... INTO`. Dameng `INSERT` and `DELETE` support equal-length `RETURNING ... INTO`, while `UPDATE` supports equal-length `RETURN ... INTO`.
- `BULK COLLECT`, non-colon-bind receivers, empty lists, and unequal target/bind lists remain unsupported.
- Otherwise-valid unequal SQL Server-family `OUTPUT ... INTO` lists remain parseable and deparseable, but the paired patch requires an explicit nonempty sink column list with equal counts before mutation. Client `OUTPUT` and channels without an explicit column list do not qualify.
- PostgreSQL-family client `RETURNING` has no matching SQL receiver list and retains target-only mutation. MySQL-family gains no DML `RETURNING INTO` syntax.
- Vastbase-Oracle and Vastbase-SQLServer behavior is a project compatibility-entry contract and does not claim the same official Vastbase server syntax scope.

Fifteen final cases and 15 paired patches were added across the five applicable entries. Each entry has `INSERT`, `UPDATE`, and `DELETE` cases with eight original pairs and a head, middle, or tail insertion producing exactly nine pairs. The nine fixtures now contain 2,796 final cases and 9,049 patches. The remote strict core API test and all five affected dialect matrices passed; the five matrices covered 1,876 cases and 5,996 patches. Six targeted Valgrind checks each reported `0 bytes in 0 blocks` and zero errors.

This release adds no public functions, public enum values, public structure fields, View JSON fields, or resource-ownership rules.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
