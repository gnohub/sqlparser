# Vastbase Dialect Support

`sqlparser` provides four explicit Vastbase compatibility modes:

| CLI name | C enum | Compatibility entry |
| --- | --- | --- |
| `vastbase-oracle` | `SQLPARSER_DIALECT_VASTBASE_ORACLE` | Oracle compatibility mode |
| `vastbase-mysql` | `SQLPARSER_DIALECT_VASTBASE_MYSQL` | MySQL compatibility mode |
| `vastbase-postgresql` | `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` | PostgreSQL compatibility mode |
| `vastbase-sqlserver` | `SQLPARSER_DIALECT_VASTBASE_SQLSERVER` | SQL Server compatibility mode |

In the CLI, `vastbase` is a deterministic alias for `vastbase-oracle`. The
library does not infer a compatibility mode from SQL text.

## Usage

```c
sqlparser_parse_options_t options;

sqlparser_parse_options_default(&options);
options.dialect = SQLPARSER_DIALECT_VASTBASE_ORACLE;
```

```bash
./bin/sqlparser_cli --dialect vastbase-oracle --mode view \
  "ALTER SESSION SET CURRENT_SCHEMA=APP"
```

```bash
./bin/sqlparser_cli --dialect vastbase-mysql --mode view \
  "SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?"
```

## Coverage

The four Vastbase modes are verified by executable regression matrices:

| Mode | Fixture | Unit Test | Successful Cases | Expected-Failure Cases | Total Cases |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | `tests/cases/vastbase_oracle_dialect_input.json` | `tests/unit/test_vastbase_oracle_dialect_case_matrix.c` | 254 | 0 | 254 |
| `vastbase-mysql` | `tests/cases/vastbase_mysql_dialect_input.json` | `tests/unit/test_vastbase_mysql_dialect_case_matrix.c` | 271 | 0 | 271 |
| `vastbase-postgresql` | `tests/cases/vastbase_postgresql_dialect_input.json` | `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c` | 213 | 0 | 213 |
| `vastbase-sqlserver` | `tests/cases/vastbase_sqlserver_dialect_input.json` | `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c` | 625 | 0 | 625 |

All four fixtures contain only `final` cases. Their independent patch counts are 832 for `vastbase-oracle`, 858 for `vastbase-mysql`, 689 for `vastbase-postgresql`, and 1892 for `vastbase-sqlserver`. Each entry verifies the exact source state of Query Graph delimited identifiers: relation `database_quoted_identifier`, `schema_quoted_identifier`, object `quoted_identifier`, and `alias_quoted_identifier`, plus field `quoted_identifier`, target `output_quoted_identifier`, and DML-column `quoted_identifier`. The `vastbase-oracle` entry also verifies database-link `link_quoted_identifier` and `INSERT ALL/FIRST` branch target columns; `vastbase-sqlserver` also verifies `OUTPUT ... INTO` sink relations and sink columns. Every field applies only to its own source token. An undelimited source leaves the C flag at `0` and omits the corresponding View JSON key. Each entry recognizes only its existing delimiter: double quotes for Oracle/PostgreSQL, backticks for MySQL, and brackets for SQL Server. Relation or DML-column patches recompute flags from the new source, and patched and reparsed handles must have identical Views. `U&"..."` is outside this contract. This is a project compatibility-entry contract and executable evidence, not a claim that the Vastbase server documentation defines the same syntax scope.

Twenty-eight final cases and 50 independent patches across the four project compatibility entries verify relation-bearing DDL projection. Query Graph represents DDL with a `kind = "ddl"` root block and distinguishes operated and referenced objects through relation `ddl_role = "target"` / `"reference"`; delimiter flags remain independent for each database/schema/object source segment. CREATE VIEW, CTAS, CREATE MATERIALIZED VIEW, and `SELECT INTO` at the entries where they are verified connect the DDL target to a separate SELECT block through `source_block`. Foreign-key references and verified PostgreSQL LIKE/INHERITS/partition references are projected as `reference` according to their actual syntax. The PostgreSQL-compatible entry also verifies the foreign-table target lifecycle; the MySQL/PostgreSQL/SQL Server-compatible entries verify exact source state for identically spelled quoted/unquoted DROP segments, and the SQL Server-compatible entry additionally verifies public-surface retention after DDL relation patches in an ordinary multi-statement batch. Only dialect forms that parse successfully and have reconciled View/patch expectations in the fixtures belong to this contract. This is project compatibility-entry evidence, not an official Vastbase server syntax claim.

All four project compatibility entries verify ordinal overlay of explicit CTE column names onto directly enumerable source-block targets only for forms valid in their own fixtures, while preserving delimiter state from each column-name token. Vastbase PostgreSQL additionally verifies that a shorter list overrides only the target prefix. Explicit names participate in DML `source_target` lineage, and repeated CTE references share the same overlaid source block. The applicable entries verify SET-result, recursive-SET, or star boundaries according to their own fixtures, without fabricating result targets, crossing branches, or expanding stars. This is a project fixture contract, not a claim about official Vastbase server syntax.

The four Vastbase project compatibility entries reuse `insert_column` for
`MERGE ... WHEN NOT MATCHED THEN INSERT ... VALUES` rewrites: `name` alone
inserts only a target column, one value source alone inserts only a VALUES
cell, and both together retain paired insertion. An omitted target list with
VALUES still exposes `target_list_selector`, so it can be materialized or a
cell can be inserted while the list remains omitted; existing target columns
and cells remain independently replaceable. The two sides may be temporarily
unequal within one patch batch. If an explicit target list exists after the
batch, its width must match the VALUES width or the entire batch rolls back
atomically. Paired deletion is unchanged, and `MERGE INSERT DEFAULT VALUES` is
outside this mutation boundary. This contract and its executable evidence do
not claim the same official Vastbase server syntax support.

The `vastbase-sqlserver` mode includes SQL Server DML `OUTPUT` result channels
and `IF...ELSE` control flow.

The `vastbase-mysql` project compatibility-entry contract supports MySQL multi-target
multi-table `UPDATE` across JOIN chains and comma-separated relation lists,
with each assignment target field identifying its write relation. `ORDER BY`
and `LIMIT` are rejected for this multi-table form. This contract and its
executable evidence do not claim the same official Vastbase server syntax
support.

The `vastbase-sqlserver` mode supports a basic `CONNECT BY` condition.
`START WITH`, `PRIOR`, `NOCYCLE`, and `CONNECT_BY_ROOT` are outside this
compatibility entry's support boundary.
Within a basic `CONNECT BY` query block, `CONNECT_BY_ROOT expr` without an
explicit `AS` is rejected as an out-of-bound hierarchy operator. An ordinary
field with the same name remains available through an explicit `AS` alias or a
delimited identifier.

As a `vastbase-sqlserver` project compatibility-entry contract, paired
`insert_column` applies only to a sink `OUTPUT ... INTO` channel with an
explicit non-empty sink-column list when the OUTPUT-target and sink-column
counts are strictly equal before the rewrite; it atomically inserts both sides
at the same ordinal. Legally unequal OUTPUT lists still parse and deparse, but
do not support paired insertion. Client `OUTPUT` and `OUTPUT ... INTO` without
an explicit sink-column list are also outside this mutation boundary. Three
cases cover 8↔8 pairs for INSERT, UPDATE, and DELETE, plus atomic head, middle,
and tail insertions that produce 9↔9 pairs. This contract and its executable
evidence do not claim the same official Vastbase server syntax support.

As a `vastbase-oracle` project compatibility-entry contract, `RETURNING ... INTO` on
`INSERT ... VALUES`, `UPDATE`, and `DELETE` supports `N >= 1` result targets
with exactly N colon-prefixed host binds, paired by ordinal. It rejects
`BULK COLLECT`, receivers other than colon-prefixed binds, and unequal list
lengths. The same `insert_column` patch inserts both the target and receiver;
one-sided insertion is not exposed. This contract and its executable evidence
do not claim the same official Vastbase server syntax support.

## Official References

- [Vastbase G100 positioning](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/aabafe2193fb4584b290dc9cdcc5c035)
- [Vastbase G100 V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d)
- [PostgreSQL compatibility](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281)
- [MySQL compatibility: backticks as identifiers](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html)
