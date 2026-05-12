# Dameng Dialect Case Matrix

This file records regression cases for the Dameng dialect conversion layer. The executable fixture is `tests/cases/dameng_dialect_input.json`; `tests/unit/test_dameng_dialect_case_matrix.c` verifies parsing, SQL View JSON, deparse output, and error codes.

## Supported Cases

| ID | Case | Coverage |
| --- | --- | --- |
| D001 | `SELECT` + `NVL` + named bind | Dameng-compatible `:name` bind conversion and restoration |
| D002 | `SET SCHEMA` | current-schema session context switching |
| D003 | `ALTER SESSION SET CURRENT_SCHEMA` | schema session switching |
| D004 | `MINUS` | bidirectional Dameng `MINUS` and core set-operator conversion |
| D005 | `LIMIT n OFFSET n` | basic Dameng pagination |
| D006 | `LIMIT offset,n` | comma pagination conversion to the core pagination structure |
| D007 | `SELECT TOP n` | basic `TOP` conversion |
| D008 | multi-table JOIN + bind | table, selected-column, join-column, and predicate-column extraction |
| D009 | `INSERT ... VALUES` + bind | inserted-column extraction and bind restoration |
| D010 | multi-row `INSERT ... VALUES` | multi-row value lists |
| D011 | `INSERT ... SELECT` | target table, source table, and inserted-column extraction |
| D012 | `UPDATE` + multiple assignments + bind | updated-column, predicate-column, and bind restoration |
| D013 | `DELETE` + predicate | conditional delete |
| D014 | `MERGE` | basic merge statement |
| D015 | `CREATE TABLE` | table creation |
| D016 | `CREATE OR REPLACE VIEW` | view creation |
| D017 | `CREATE SEQUENCE` | sequence creation |
| D018 | `ALTER TABLE ... ADD` | add column |
| D019 | `CREATE INDEX` | create index |
| D020 | `DROP TABLE` + `TRUNCATE TABLE` | table drop and truncation |
| D021 | transaction control | `BEGIN`, `COMMIT`, and `ROLLBACK` |
| D022 | privilege statements | `GRANT` and `REVOKE` |
| D023 | `ROWNUM` predicate | pseudo-column in a predicate expression |
| D024 | `FOR UPDATE NOWAIT` | row-locking query |
| D025 | q-quoted string | `q'[...]'` string compatibility handling |
| D026 | `SET SCHEMA; SELECT` | schema switching and query remain separate in multi-statement input |
| D027 | `DATE` + `TIMESTAMP` literal | date and timestamp literals |
| D028 | `GROUP BY` + `HAVING` + window function | aggregate query and analytic function |
| D029 | `SELECT TOP offset,count` | `TOP` offset/count conversion to the core pagination structure |
| D030 | `INSERT ... VALUES (?, ?, ?)` | JDBC-style positional parameter conversion, inserted-column extraction, and public-form restoration |
| D031 | `UPDATE ... SET ... WHERE ... = ?` | positional parameter conversion and public-form restoration in SET/WHERE clauses |
| D032 | `EXEC SQL PREPARE ... FROM ...` | Dameng embedded-SQL prepare statement with SQL text and `?` placeholders restored in public form |
| D033 | `EXEC SQL EXECUTE ... USING ...` | Dameng embedded-SQL execute statement and arguments restored in public form |
| D034 | `EXEC SQL DEALLOCATE PREPARE ...` | Dameng embedded-SQL prepared statement deallocation |
| D035 | query with multiple named binds | multiple `:name` binds in query predicates |
| D036 | `IN` + multiple named binds | multiple named binds in predicate lists |
| D037 | `INSERT ... VALUES` + multiple named binds | insert columns and named-bind value lists |
| D038 | multi-row `INSERT ... VALUES` + `?` | multi-row JDBC-style parameterized insert |
| D039 | `UPDATE ... SET ... WHERE ... = ?` | updated columns, predicate columns, and positional parameters |
| D040 | `DELETE ... WHERE ... = ?` | conditional delete and positional parameters |
| D041 | `EXEC SQL PREPARE` + INSERT | embedded-SQL prepared insert text |
| D042 | `EXEC SQL EXECUTE` + named binds | prepared statement execution with named bind arguments |
| D043 | `EXEC SQL EXECUTE` + `?` parameters | prepared statement execution with positional parameters |

## Explicitly Unsupported Cases

The following constructs have Dameng-specific or compatibility-mode semantics. The conversion layer returns `SQLPARSER_STATUS_UNSUPPORTED` or a parse error instead of producing SQL with unreliable semantics.

| ID | Case | Reason |
| --- | --- | --- |
| DU001 | `CONNECT BY` | hierarchical query semantics need a dedicated query model |
| DU002 | `PIVOT` | table transformation semantics need a dedicated query model |
| DU003 | `RETURNING ... INTO` | non-equivalent host-variable return target |
| DU004 | DMSQL block | outside SQL statement conversion scope |
| DU005 | `TOP ... PERCENT` | percentage row-count semantics cannot be downgraded to ordinary `LIMIT` |
| DU006 | `INSERT ALL` | non-equivalent multi-table insert semantics |
| DU007 | database link | remote object reference semantics |
| DU008 | national q-quoted string | national character-set semantics are not silently downgraded |
| DU009 | other `ALTER SESSION` parameters | session parameters other than current-schema switching |
| DU010 | `CREATE PROCEDURE` | DMSQL program unit |
| DU011 | `TOP ... WITH TIES` | ties semantics cannot be downgraded to ordinary `LIMIT` |
| DU012 | `ALTER SESSION SET CONTAINER` | Dameng does not support container session semantics |

## Maintenance

- New Dameng support must update `tests/cases/dameng_dialect_input.json`, this matrix, and executable regression tests.
- Dameng-only syntax that cannot be mapped with equivalent semantics must return `SQLPARSER_STATUS_UNSUPPORTED` or a parse error.
