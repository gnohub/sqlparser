# Oracle Dialect Case Matrix

This file records regression cases for the Oracle dialect conversion layer. The executable fixture is `tests/cases/oracle_dialect_input.json`; `tests/unit/test_oracle_dialect_case_matrix.c` verifies parsing, SQL View JSON, deparse output, and error codes.

## Supported Cases

| ID | Case | Coverage |
| --- | --- | --- |
| O001 | `SELECT` + `NVL` + named bind | Oracle `:name` bind conversion and restoration |
| O002 | q-quoted string | `q'[...]'` conversion to a safe string literal |
| O004 | `MINUS` | bidirectional Oracle `MINUS` and core `EXCEPT` conversion |
| O005 | `OFFSET ... FETCH` | Oracle pagination syntax |
| O006 | `ROWNUM` predicate | pseudo-column in a predicate expression |
| O007 | multi-table JOIN + bind | table, selected-column, join-column, and predicate-column extraction |
| O008 | `INSERT ... VALUES` + bind | inserted-column extraction and bind restoration |
| O009 | multi-row `INSERT ... VALUES` | multi-row value lists |
| O010 | `INSERT ... SELECT` | target table, source table, and inserted-column extraction |
| O011 | `UPDATE` + multiple assignments + bind | updated-column, predicate-column, and bind restoration |
| O012 | `DELETE` + predicate | conditional delete |
| O013 | repeated named bind | one internal parameter number for the same bind name |
| O014 | positional bind | `:1` and `:2` conversion and restoration |
| O015 | `DATE` literal | date literal |
| O016 | `CASE` expression | conditional expression |
| O017 | `EXISTS` subquery | subquery table and predicate-column extraction |
| O018 | `GROUP BY` + `HAVING` | aggregate query |
| O019 | `UNION ALL` | set query |
| O020 | `INTERSECT` | set query |
| O021 | `MERGE` | basic merge statement |
| O022 | `CREATE TABLE` | table creation with common Oracle type names |
| O023 | `CREATE SEQUENCE` | sequence creation |
| O024 | `CREATE OR REPLACE VIEW` | view creation |
| O025 | `DROP TABLE` | table drop |
| O026 | `TRUNCATE TABLE` | table truncation |
| O027 | transaction control | `SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and `COMMIT` |
| O028 | privilege statements | `GRANT` and `REVOKE` |
| O029 | comment statement | `COMMENT ON TABLE` |
| O030 | `FOR UPDATE NOWAIT` | row-locking query |
| O031 | `DECODE` + `SYSDATE` | common Oracle function and pseudo-column |
| O032 | `ROW_NUMBER() OVER` | analytic function |
| O033 | `TIMESTAMP` literal | timestamp literal |
| O034 | quoted identifiers | case-sensitive object and column names |
| O035 | `ALTER TABLE ... ADD` | add column |
| O036 | `CREATE INDEX` | create index |
| O037 | `DROP INDEX` | drop index |
| O038 | `IN` + multiple binds | multiple binds in a predicate list |
| O039 | `DELETE` + `DATE` literal | conditional delete and date literal |
| O040 | materialized view | compatible materialized-view syntax |
| O041 | unsupported keywords in string | `RETURNING`, `@`, and `(+)` inside strings do not trigger unsupported |
| O042 | unsupported keywords in comment | `CONNECT BY` inside comments does not trigger unsupported |
| O043 | `ALTER SESSION SET CURRENT_SCHEMA` | current-schema session context switching |
| O044 | `ALTER SESSION SET CONTAINER` | current-container session context switching |
| O045 | `ALTER SESSION SET CONTAINER=CDB$ROOT` | official root container name |
| O046 | `ALTER SESSION SET CONTAINER ... SERVICE ...` | container switching with the `SERVICE` clause |
| O047 | `SELECT ...; ALTER SESSION SET CURRENT_SCHEMA` | query and schema switching remain separate in multi-statement input |
| O048 | `INSERT ... VALUES (?, ?, ?)` | JDBC-style positional parameter conversion, inserted-column extraction, and public-form restoration |
| O049 | `UPDATE ... SET ... WHERE ... = ?` | positional parameter conversion and public-form restoration in SET/WHERE clauses |
| O050 | `EXECUTE IMMEDIATE ... USING ...` | Oracle dynamic SQL execution with SQL text and bind arguments restored in public form |
| O051 | multiple named-bind query | multiple `:name` binds in `SELECT` predicates |
| O052 | `IN` + multiple named binds | bind restoration in `IN (:a, :b, :c)` predicates |
| O053 | `FETCH FIRST` + bind | bind restoration in pagination limits |
| O054 | `INSERT ... VALUES` + multiple named binds | insert columns and named-bind value lists |
| O055 | `UPDATE` + multiple named binds | updated columns, predicate columns, and named binds |
| O056 | `DELETE` + multiple named binds | conditional delete and named binds |
| O057 | positional bind pair | `:1` and `:2` predicate parameters |
| O058 | expanded `INSERT ... VALUES (?, ?, ?)` | insert columns and JDBC-style positional parameters |
| O059 | `DELETE ... WHERE ... = ?` | JDBC-style positional parameters in conditional delete |
| O060 | `EXECUTE IMMEDIATE` update statement | dynamic UPDATE SQL text and multiple USING binds |
| O061 | nested ROWNUM pagination with bind | nested query, `a.*`, pseudo-column alias, and named binds |
| O062 | `NVL` + `TO_CHAR` + `UPPER` | function `target_path`, nested function, argument index, and WHERE bind |
| O063 | `CASE` expression output | `target_path` attribution for fields inside `CASE WHEN` |
| O064 | `GROUP BY` + `HAVING` + `ORDER BY` | aggregate output and non-output clause attribution |
| O065 | `UPDATE` + multiple named binds | update/where clauses, bind fields, and null values |
| O066 | ROWNUM pagination attribution | nested query, `a.*`, ROWNUM predicate, and outer predicate attribution |
| O067 | mixed `:1` and `?` positional binds | `bind_kind` and `bind_sql` distinguish Oracle positional binds from JDBC positional markers |
| O068 | `BETWEEN` + multiple named binds | multiple named binds and field-value attribution in `BETWEEN` predicates |
| O069 | `NOT IN` + multiple named binds | multiple named binds and field-value attribution in negated `IN` predicates |
| O070 | `NOT BETWEEN` + multiple named binds | multiple named binds and field-value attribution in negated `BETWEEN` predicates |
| O071 | `NOT LIKE` + named bind | named bind, field-level operator, and keyword attribution in negated `LIKE` predicates |
| O072 | `DISTINCT` + `LIKE` bind | DISTINCT projection, LIKE named bind, and field attribution |
| O073 | nested function projection | ordered `target_path` for `LOWER(UPPER(...))` |
| O074 | `DELETE ... IN` + named bind | conditional delete, collection parameters, and field operator |
| O075 | `UPDATE ... EXISTS` | subquery predicate, correlated fields, and SET bind |
| O076 | columnless `INSERT` | columnless insert, row cells, named binds, and null column names |
| O077 | `CREATE OR REPLACE VIEW` + aggregate JOIN | view creation, JOIN predicates, and GROUP BY aggregation |
| O078 | realistic nested ROWNUM pagination field set | multi-field projection, `a.*`, ROWNUM predicates, and pagination binds |
| O079 | `LEFT JOIN` + `alias.*` | qualified star, JOIN/ON fields, and WHERE bind |
| O080 | `ORDER BY 1` | ordinal sort item and projection-order related syntax |
| O081 | `SELECT :bind FROM dual` | DUAL query and named bind in the SELECT list |
| O082 | `ALTER SESSION SET NLS_DATE_FORMAT` | string-valued ordinary session parameter |
| O083 | `ALTER SESSION SET NLS_DATE_LANGUAGE` | identifier-valued ordinary session parameter |
| O084 | `ALTER SESSION SET INSTANCE` | numeric ordinary session parameter |
| O085 | `ALTER SESSION SET ERROR_ON_OVERLAP_TIME` | boolean/enumerated ordinary session parameter |
| O086 | `ALTER SESSION SET NLS_NUMERIC_CHARACTERS` | punctuation-bearing string session parameter |
| O087 | multi-statement named binds | named-bind `bind_position` increases globally across the full input SQL |

## Explicitly Unsupported Cases

The following constructs have Oracle-specific semantics. The conversion layer returns `SQLPARSER_STATUS_UNSUPPORTED` instead of producing SQL with unreliable semantics.

| ID | Case | Reason |
| --- | --- | --- |
| O003 | national q-quoted string | national character-set semantics are not silently downgraded |
| OU001 | `CONNECT BY` | non-equivalent hierarchical query semantics |
| OU002 | `(+)` | non-equivalent legacy outer join semantics |
| OU003 | `INSERT ALL` | non-equivalent multi-table insert semantics |
| OU004 | `RETURNING ... INTO` | non-equivalent host-variable return target |
| OU005 | PL/SQL block | outside SQL statement conversion scope |
| OU006 | `CREATE PROCEDURE` | PL/SQL unit |
| OU007 | `CREATE PACKAGE` | PL/SQL unit |
| OU008 | `PIVOT` | Oracle table transformation |
| OU009 | `UNPIVOT` | Oracle table transformation |
| OU010 | `MODEL` | Oracle model clause |
| OU011 | flashback query | Oracle flashback query semantics |
| OU012 | `MATCH_RECOGNIZE` | row pattern recognition semantics |
| OU014 | `CREATE SYNONYM` | Oracle synonym object |
| OU015 | database link | remote object reference semantics |
| OU016 | `EXPLAIN PLAN FOR` | Oracle explain plan output semantics |
| OU017 | `CONNECT_BY_ROOT` | hierarchical-query expression |
| OU018 | `INSERT FIRST` | conditional multi-table insert semantics |

## Maintenance

- New Oracle support must update `tests/cases/oracle_dialect_input.json`, this matrix, and executable regression tests.
- Oracle-only syntax that cannot be mapped with equivalent semantics must return `SQLPARSER_STATUS_UNSUPPORTED`.
