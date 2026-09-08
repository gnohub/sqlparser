# KingbaseES MySQL Compatibility-Mode Case Matrix

The executable fixture is `tests/cases/kingbase_mysql_dialect_input.json`. This matrix uses KingbaseES `V008R006C009B0014` and MySQL-compatible `V009R003C011` as documentation baselines. It includes only SQL forms directly confirmed by official KingbaseES documentation and already present in the MySQL fixture. Every case is `final` and is verified by `tests/unit/test_kingbase_mysql_dialect_case_matrix.c`.

## Baseline and Entry Boundary

`kingbase-mysql` is one dialect entry that accepts the union of officially documented V8 and V9 MySQL syntax. It does not split by release and introduces no version field, detection, or branch. KingbaseES V8R6 initializes MySQL mode with `initdb -m/--dbmode=mysql` (or numeric value `2`), while V9R3 is distributed as a dedicated MySQL-compatible edition.

The V9 increment only broadens accepted syntax. An adjacent `@var` and `PREPARE ... FROM` have distinct grammatical contexts, so they do not reinterpret V8 relation/dblink syntax or `PREPARE ... AS`, and the first 36 V8 contracts remain unchanged.

- Initialization mode: [initdb](https://help.kingbase.com.cn/v8/admin/reference/ref-server/initdb.html)
- Documentation release: [V8 downloads](https://help.kingbase.com.cn/v8/download.html)
- Mode index: [MySQL mode index](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/MySQL_schema_index.html)
- Compatibility overview: [MySQL compatibility overview](https://help.kingbase.com.cn/v8/development/develop-transfer/kes-vs-mysql/overview.html)
- V9R3C11 product entry: [KingbaseES MySQL-compatible V9](https://help.kingbase.com.cn/v9.3.11/index.html)
- V9R3C11 SQL reference: [MySQL SQL reference](https://help.kingbase.com.cn/v9.3.11/development/application-develop-guide/reference/mysql/index.html)
- V9R3 feature baseline: [V009R003C010 release notes](https://help.kingbase.com.cn/v9.3.11/intro/releasenotes-external-v9/V9.3.10.html)

## Fixture Totals

The fixture contains 42 cases with `status = "final"` and 112 independent patches. SQL text, expected Views, patches, and bind-occurrence assertions are selected from the existing MySQL fixture according to the documented V8/V9 scope. Only case-name prefixes are changed.

## Official Sources and Coverage

| Category | Official source | Covered cases |
| --- | --- | --- |
| Comments and delimited identifiers | [SQL elements](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html), [V8R6C8 release notes](https://help.kingbase.com.cn/v8/intro/releasenotes-external/V8.6.8.14.html) | `KBM001`, `KBM034` |
| Client bind markers | [Go prepare and bind](https://help.kingbase.com.cn/v8/development/client-interfaces/go/go-3.html), [JDBC interface](https://help.kingbase.com.cn/v8/development/client-interfaces/jdbc/jdbc-2.html) | `KBM004`–`KBM007`, `KBM010`, `KBM013`, `KBM016`, `KBM017`, `KBM020`, `KBM024`, `KBM031`–`KBM033`, `KBM036` |
| SELECT, CTE, JOIN, LIMIT, and index hints | [Queries and subqueries](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html), [SELECT in MySQL mode](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | `KBM004`, `KBM005`, `KBM007`, `KBM021`, `KBM022`, `KBM028`, `KBM029`, `KBM034`, `KBM035` |
| INSERT and duplicate-key updates | [INSERT in MySQL mode](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html) | `KBM006`, `KBM009`–`KBM011`, `KBM020`, `KBM025`, `KBM026` |
| REPLACE | [REPLACE INTO](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | `KBM012`–`KBM015` |
| UPDATE | [UPDATE in MySQL mode](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | `KBM016`, `KBM027`, `KBM032`, `KBM033` |
| DELETE | [MULTIPLE TABLE DELETE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html), [compatibility overview](https://help.kingbase.com.cn/v8/development/develop-transfer/kes-vs-mysql/overview.html) | `KBM017`, `KBM030`, `KBM031` |
| DDL and data types | [CREATE TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html), [ALTER TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_3.html), [MySQL mode index](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/MySQL_schema_index.html) | `KBM002`, `KBM008`, `KBM018`, `KBM019`, `KBM023` |
| Functions and structured expressions | [Functions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Function.html), [Expressions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Expressions.html) | `KBM024`, `KBM025`, `KBM036` |
| Transaction control | [SQL compatibility table](https://help.kingbase.com.cn/v8/development/develop-transfer/kes-vs-mysql/kes-vs-mysql-3.html) | `KBM003` |
| V9 user variables | [V9 user variables](https://help.kingbase.com.cn/v9.3.11/development/application-develop-guide/reference/mysql/structure_language/user_defined_variables.html) | `KBM039`–`KBM042` |
| V9 prepared statements | [V9 SQL reference](https://help.kingbase.com.cn/v9.3.11/development/application-develop-guide/reference/mysql/index.html), [V9R3 release notes](https://help.kingbase.com.cn/v9.3.11/intro/releasenotes-external-v9/V9.3.10.html) | `KBM037`–`KBM040` |

## Case Inventory

| ID | Case | Independent patches | Validation focus |
| --- | --- | ---: | --- |
| `KBM001` | `kingbase-mysql-hash-comment` | 3 | MySQL-mode `#` line comments and byte-exact replay |
| `KBM002` | `kingbase-mysql-alter-table-add-column` | 0 | `ALTER TABLE ... ADD COLUMN` |
| `KBM003` | `kingbase-mysql-start-transaction` | 0 | `START TRANSACTION` and multi-statement counting |
| `KBM004` | `kingbase-mysql-select-question-params` | 6 | JDBC-style positional parameters in SELECT predicates |
| `KBM005` | `kingbase-mysql-select-limit-question-params` | 6 | documented `LIMIT count OFFSET start` form and binds |
| `KBM006` | `kingbase-mysql-insert-multi-row-question-params` | 3 | multi-row VALUES and positional parameters |
| `KBM007` | `kingbase-mysql-view-join-on` | 6 | JOIN/ON/WHERE field and bind ownership |
| `KBM008` | `kingbase-mysql-create-table-if-not-exists` | 0 | conditional table creation and backtick identifiers |
| `KBM009` | `kingbase-mysql-insert-ignore` | 2 | `INSERT IGNORE` |
| `KBM010` | `kingbase-mysql-insert-ignore-on-duplicate-key` | 3 | IGNORE, VALUES, and duplicate-key update composition |
| `KBM011` | `kingbase-mysql-on-duplicate-key-assignment-patch` | 3 | duplicate-key assignment insertion, replacement, and deletion |
| `KBM012` | `kingbase-mysql-replace-into` | 2 | REPLACE VALUES |
| `KBM013` | `kingbase-mysql-replace-set` | 2 | REPLACE SET and positional parameters |
| `KBM014` | `kingbase-mysql-replace-without-into` | 2 | REPLACE with omitted INTO |
| `KBM015` | `kingbase-mysql-replace-table-source` | 2 | REPLACE TABLE source |
| `KBM016` | `kingbase-mysql-update-ignore` | 5 | `UPDATE IGNORE` |
| `KBM017` | `kingbase-mysql-delete-join` | 4 | multiple-table DELETE JOIN |
| `KBM018` | `kingbase-mysql-auto-increment` | 0 | AUTO_INCREMENT column attribute |
| `KBM019` | `kingbase-mysql-unsigned` | 0 | UNSIGNED numeric attribute |
| `KBM020` | `kingbase-mysql-insert-select-source-block-graph` | 5 | INSERT SELECT source block |
| `KBM021` | `kingbase-mysql-with-cte-select` | 6 | CTE source relationships |
| `KBM022` | `kingbase-mysql-window-row-number` | 5 | window function partition and ordering fields |
| `KBM023` | `kingbase-mysql-common-data-types` | 0 | BIGINT, DECIMAL, DATETIME, and BOOLEAN |
| `KBM024` | `kingbase-mysql-json-contains-function-predicate` | 4 | predicate structure for a `mysql_json` plug-in function |
| `KBM025` | `kingbase-mysql-on-duplicate-values-function` | 3 | `VALUES(column)` in duplicate-key updates |
| `KBM026` | `kingbase-mysql-insert-set-paired-column-patch` | 2 | INSERT SET column/value structure and paired patches |
| `KBM027` | `kingbase-mysql-update-order-limit` | 6 | ORDER BY/LIMIT on single-table UPDATE |
| `KBM028` | `kingbase-mysql-select-use-index` | 5 | USE INDEX |
| `KBM029` | `kingbase-mysql-select-force-and-ignore-index` | 5 | FORCE INDEX, IGNORE KEY, and a set query |
| `KBM030` | `kingbase-mysql-delete-limit-only` | 3 | LIMIT on single-table DELETE |
| `KBM031` | `kingbase-mysql-delete-left-join-where-three-and` | 4 | LEFT JOIN multiple-table deletion and compound predicates |
| `KBM032` | `kingbase-mysql-update-multiple-target-three-table-bind-order` | 3 | three joined tables, multiple assignment targets, and 12 binds |
| `KBM033` | `kingbase-mysql-update-multiple-target-four-relation-comma-list` | 3 | four comma-separated relations and multiple update targets |
| `KBM034` | `kingbase-mysql-quoted-alias-output-flags` | 2 | backtick relation aliases and output-alias flags |
| `KBM035` | `kingbase-mysql-cte-explicit-columns-ordinal` | 2 | ordinal mapping for explicit CTE columns |
| `KBM036` | `kingbase-mysql-predicate-expression-function-argument-kinds` | 5 | variadic CONCAT, nested functions, and argument patches |
| `KBM037` | `kingbase-mysql-prepare-from-literal` | 0 | V9 MySQL-form `PREPARE ... FROM` with a string source |
| `KBM038` | `kingbase-mysql-deallocate-prepare` | 0 | V9 `DEALLOCATE PREPARE` |
| `KBM039` | `kingbase-mysql-prepare-from-user-variable` | 0 | V9 user variable as the PREPARE source |
| `KBM040` | `kingbase-mysql-execute-using-multiple-vars` | 0 | ordered V9 user variables in EXECUTE USING |
| `KBM041` | `kingbase-mysql-user-variable-basic` | 0 | V9 single-variable SET assignment |
| `KBM042` | `kingbase-mysql-user-variable-punctuation` | 0 | dots, underscores, and dollar signs in V9 user-variable names |

The function in `KBM024` belongs to the officially documented V8 `mysql_json` plug-in scope. The case defines SQL syntax and parsing behavior, not plug-in installation state.

## Excluded Boundaries

- `RLIKE`, `INSERT DELAYED`, `LOGFILE GROUP`, `LOCK/UNLOCK INSTANCE`, `SET PASSWORD FOR`, and `CHECK/CHECKSUM TABLE` are explicitly unsupported in the official compatibility table and are not positive cases.
- The complete V8/V9 syntax references do not confirm `LOW_PRIORITY`, `HIGH_PRIORITY`, `REPLACE DELAYED`, `ENGINE`, table-level `DEFAULT CHARSET`, `ZEROFILL`, `USE database`, or INSERT row aliases; these are not inferred.
- V9 documents `/*! specific code */` comments but does not define MySQL version-marker execution rules, so versioned executable comments are excluded.
- The exact verifiable V8/V9 SELECT grammar confirms only `LIMIT count OFFSET start`; this fixture does not infer the MySQL `LIMIT offset,count` form.
- V8 retains server-side `PREPARE name(types) AS statement` with `$N` parameters. The dedicated V9 MySQL SQL reference adds user variables and the MySQL prepared-statement group. This entry accepts both syntax families as a union without selecting by a version field. `DROP PREPARE` is not a positive V9R3C11 contract.
- The V9 migration guide retains an older statement that SQL user variables are unsupported, which conflicts with the dedicated V9R3C11 SQL reference and V9R3 release notes. This matrix treats the explicit syntax in the dedicated SQL reference as the V9 positive authority and includes only its directly documented single-variable assignment, variable-name, and prepared-statement forms.
- This fixture defines the reusable MySQL subset rather than every KingbaseES-specific extension. DML `RETURNING`, for example, requires dedicated KingbaseES cases and cannot be copied equivalently from the existing MySQL fixture.
