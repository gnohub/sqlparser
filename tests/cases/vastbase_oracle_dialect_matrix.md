# Vastbase Oracle 兼容模式用例矩阵

可执行夹具为 `tests/cases/vastbase_oracle_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。

## 事务特征规范语义值回归

以下 4 条 final 用例覆盖 Vastbase Oracle 兼容模式的常用事务隔离级别和访问模式。原始 SQL 反解析必须逐字节保持；View 必须按输入顺序输出去除 trivia 后的规范关键字值。session 语义值不提供 selector，因此这些用例不设置 patch。

| ID | 用例 | SQL | 验证重点 |
| --- | --- | --- | --- |
| `VO-TX001` | `vastbase-oracle-session-transaction-commented-read-uncommitted` | ALTER/*command*/SESSION SET TRANSACTION ISOLATION/*name*/LEVEL READ/*value*/UNCOMMITTED; | `READ UNCOMMITTED` 规范值及单事务特征 |
| `VO-TX002` | `vastbase-oracle-session-characteristics-commented-repeatable-read-write` | ALTER SESSION SET SESSION/*scope*/CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE/*value*/READ, READ/*mode*/WRITE; | `REPEATABLE READ`、`READ WRITE` 及 session characteristics 入口 |
| `VO-TX003` | `vastbase-oracle-session-transaction-commented-serializable-read-only` | ALTER SESSION SET TRANSACTION ISOLATION LEVEL SERIALIZABLE/*tail*/, READ/*mode*/ONLY; | `SERIALIZABLE`、`READ ONLY` 及逗号前注释 |
| `VO-TX004` | `vastbase-oracle-session-transaction-commented-option-order` | ALTER SESSION SET TRANSACTION read/*mode*/write, ISOLATION/*name*/LEVEL read/*value*/committed; | 输入选项顺序、小写原文保留及 `READ COMMITTED` 规范值 |

## 矩阵统计与 session 回归

夹具包含 221 条用例和 793 个独立 patch，全部为 `status = "final"`；其中 41 条用例的期望 View 包含非空 session 投影。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session action、item scope、target kind、name、value 类型、规范文本及顺序均属于比较范围。

## 定界别名状态回归

`vastbase-oracle-quoted-relation-alias-and-target-output-contract` 及其 2 个 output alias patch 验证 Query Graph 字段合同：relation alias 的精确来源 token 使用双引号时输出 `alias_quoted_identifier: true`；target 的 `output_name` 来源于带双引号的显式 alias，或无显式 alias 时来源于带双引号的直接字段 token，则输出 `output_quoted_identifier: true`。未定界来源不输出对应字段。该合同属于项目兼容入口，不代表 Vastbase 服务端官方语法范围。

## 完整绑定占位符 occurrence 回归

以下 2 条 final 用例定义项目 `vastbase-oracle` 兼容入口的 handle 级 occurrence 合同，不作为 Vastbase 服务端官方能力声明。runner 对输入及每个 patch 后的公开 SQL 逐项断言 `position`、`kind`、`key` 和 `sql`；重复项不合并，多语句编号不重置，字符串、注释和定界标识符中的伪占位符不计入。

| 用例 | 根 occurrence | Patch | 基础入口关系 | 验证重点 |
| --- | ---: | ---: | --- | --- |
| `vastbase-oracle-multi-statement-global-bind-position` | 7 | 5 | 除用例名外，逐字段镜像 `oracle-multi-statement-global-bind-position` | UPDATE、MERGE、重复命名 bind；改写后覆盖函数、CAST、CASE、子查询、FETCH、数字位置 `:1`、匿名 `?`、点号 key 和保护区排除 |
| `vastbase-oracle-update-returning-eight-target-bind-pairs` | 11 | 1 | 除用例名外，逐字段镜像 `oracle-update-returning-eight-target-bind-pairs` | UPDATE 与 `RETURNING ... INTO` 的源码顺序；成对插入后 12 个 occurrence 连续重编号 |

## RETURNING INTO 宿主绑定变量结果通道回归

以下 6 条 final 用例定义项目 `vastbase-oracle` 兼容入口的 `RETURNING ... INTO` 合同，不作为 Vastbase 服务端官方语法声明。`INSERT ... VALUES`、`UPDATE` 和 `DELETE` 支持 `N >= 1` 个返回 target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 一一配对。View 将结果表达为不含 `sink_relation` 的单个 `kind = "sink"` 通道，每个 target 的 `sink_value` 指向 `query_graph.values[]` 中对应 ordinal 的输出 bind。VO186 至 VO188 各验证 8 对结果及一次成对 `insert_column`；同一个 patch 在相同 ordinal 同时插入 target 和 receiver，使结果扩展为 9 对，不允许拆成单侧插入。

| ID | 用例 | DML | 验证重点 |
| --- | --- | --- | --- |
| `VO183` | `vastbase-oracle-insert-returning-rowid-into-bind` | INSERT | `ROWID` 为 `pseudo` target，来源为 `target_after`；分别替换 VALUES cell、结果 target 和输出 bind 后精确反解析 |
| `VO184` | `vastbase-oracle-update-returning-rowid-into-bind` | UPDATE | `ROWID` 为 `pseudo` target，来源为 `target_after`；分别替换 assignment、结果 target 和输出 bind 后精确反解析 |
| `VO185` | `vastbase-oracle-delete-returning-rowid-into-bind` | DELETE | `ROWID` 为 `pseudo` target，来源为 `target_before`；分别替换 WHERE bind、结果 target 和输出 bind 后精确反解析 |
| `VO186` | `vastbase-oracle-insert-returning-eight-target-bind-pairs` | INSERT | 8 对结果；在 index 0 成对插入 `tenant_id` / `:out_tenant_id`，验证头部 9 对及 ordinal 对齐 |
| `VO187` | `vastbase-oracle-update-returning-eight-target-bind-pairs` | UPDATE | 8 对结果；在 index 4 成对插入 `postal_code` / `:out_postal_code`，验证中部 9 对及 ordinal 对齐 |
| `VO188` | `vastbase-oracle-delete-returning-eight-target-bind-pairs` | DELETE | 8 对结果；在 index 8 成对插入 `tenant_id` / `:out_tenant_id`，验证尾部 9 对及 ordinal 对齐 |

## ROWNUM 谓词语义回归

以下 5 条 final 用例验证 Vastbase Oracle 兼容模式下 `ROWNUM` 比较的 Query Graph 语义。`ROWNUM` 作为 pseudo 表达式不进入 `fields` 或 relation lineage；比较另一侧的 literal 或 bind 进入 `values`，并由无 field 的 expression predicate 引用。布尔组合、派生查询层级、操作数方向和 DELETE DML 归属均按原始 SQL 投影。

| ID | 用例 | SQL | 验证重点 |
| --- | --- | --- | --- |
| `VO-RN001` | `vastbase-oracle-rownum-conjunction-named-bind` | SELECT id FROM users WHERE active = 1 AND ROWNUM <= :limit | AND 根节点按源码顺序引用普通字段比较与 `ROWNUM` expression predicate；`:limit` 为 position 1 |
| `VO-RN002` | `vastbase-oracle-rownum-derived-order-by-filter` | SELECT * FROM (SELECT id, created_at FROM orders ORDER BY created_at DESC) WHERE ROWNUM < 11 | `ROWNUM` predicate 属于外层 block，派生表来源和内层 ORDER BY 字段归属保持独立 |
| `VO-RN003` | `vastbase-oracle-rownum-reversed-literal-comparison` | SELECT id FROM users WHERE 1 = ROWNUM | 反向操作数仍记录 literal selector，并建立无 field 的 expression predicate |
| `VO-RN004` | `vastbase-oracle-rownum-greater-than-literal` | SELECT id FROM users WHERE ROWNUM > 1 | `>` 操作符及 literal value 精确保留，`ROWNUM` 不产生 field |
| `VO-RN005` | `vastbase-oracle-delete-rownum-conjunction-named-bind` | DELETE FROM audit_log WHERE expired = 1 AND ROWNUM <= :batch_size | DML 对象引用 DELETE target relation；AND predicate tree 及 `:batch_size` position 1 归属于根 block |

## INSERT VALUES 回归：bind 与表达式混合

`VO-BM001` 至 `VO-BM010` 覆盖 Vastbase Oracle 兼容模式下使用 PBE `$n` 位置参数的单行 `INSERT ... VALUES`。这些 SQL 是 prepare/bind 流程中的 statement body，不作为带未绑定参数的直接执行语句。

10 条用例均为单行 VALUES，`DEFAULT` 只作为独立 cell。每条用例逐 cell 断言 `row`、`column`、`kind` 和 `selector`；逐个直接 bind 断言 `bind_key`、`bind_kind`、`bind_sql`、全局 `bind_position` 和 `selector`；通过表达式后的直接 bind 位置验证嵌套 bind 已计入全局序号。`SYSDATE`、`CURRENT_TIMESTAMP` 必须是 expression，且不得出现在 `query_graph.fields[].column` 中。

| ID | SQL 形态 | 边界 |
| --- | --- | --- |
| `VO-BM001` | 三个独立 `$n` bind + 尾部 `SYSDATE` | 逐字节保留带不规则空白和分号的原始输入 SQL |
| `VO-BM002` | 首位 `CURRENT_TIMESTAMP` + 三个独立 `$n` bind | 首列 expression 不得造成 bind 错位 |
| `VO-BM003` | `$1`、`$2`、`$3` + 尾部 `SYSDATE` | 连续位置参数及尾部时间表达式 |
| `VO-BM004` | 首位 `SYSDATE` + 三个 `$n` bind | 表达式位于首列时保持 bind 顺序 |
| `VO-BM005` | `$n` bind 与 `SYSDATE` / `CURRENT_TIMESTAMP` 交错 | expression cell 不得造成后续 bind 错位 |
| `VO-BM006` | bind + `NULL` + `SYSDATE` + bind | `NULL` 为 literal，`SYSDATE` 为 expression |
| `VO-BM007` | bind + 独立 `DEFAULT` + `CURRENT_TIMESTAMP` + bind | `DEFAULT` 不嵌入其他表达式 |
| `VO-BM008` | 直接 bind + `COALESCE($2, 0)` + `SYSDATE` + 直接 bind | 尾部直接 bind 为 position 3，验证嵌套 bind 已计数 |
| `VO-BM009` | 直接 bind + 包含 `$2` 的 `CASE` 表达式 + `CAST($3 AS NUMBER)` + `SYSDATE` + 直接 bind | 尾部直接 bind 为 position 4，验证两个嵌套 bind 均已计数 |
| `VO-BM010` | schema-qualified quoted identifiers + 三个 `$n` bind + `CURRENT_TIMESTAMP` | 保留大小写标识符、不规则空白和分号 |

## 支持用例

| ID | 用例 | SQL | 状态 |
| --- | --- | --- | --- |
| `VO001` | `vastbase-oracle-select-bind-nvl` | SELECT NVL(u.name, 'N/A') AS label FROM users u WHERE u.id = :id | 已覆盖 |
| `VO002` | `vastbase-oracle-q-quoted-string` | SELECT q'[Bob's order]' AS label FROM dual | 已覆盖 |
| `VO003` | `vastbase-oracle-national-q-quoted-string` | SELECT nq'{Alice's order}' AS label FROM dual | 已覆盖 |
| `VO003A` | `vastbase-oracle-national-q-quoted-duplicate-literal` | SELECT 'same' AS ascii_value, nq'{same}' AS national_value FROM dual | 已覆盖 |
| `VO003B` | `vastbase-oracle-national-string-literal` | SELECT N'Alice''s order' AS label FROM dual | 已覆盖 |
| `VO003C` | `vastbase-oracle-national-string-duplicate-literal` | SELECT 'same' AS ascii_value, N'same' AS national_value FROM dual | 已覆盖 |
| `VO004` | `vastbase-oracle-minus-set-operator` | SELECT id FROM active_users MINUS SELECT id FROM archived_users | 已覆盖 |
| `VO005` | `vastbase-oracle-offset-fetch` | SELECT id FROM users ORDER BY id OFFSET 5 ROWS FETCH NEXT 10 ROWS ONLY | 已覆盖 |
| `VO006` | `vastbase-oracle-rownum-filter` | SELECT id FROM users WHERE ROWNUM <= 10 | 已覆盖 |
| `VO007` | `vastbase-oracle-join-bind` | SELECT u.id, u.name, o.order_no FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = :status | 已覆盖 |
| `VO008` | `vastbase-oracle-insert-values-bind` | INSERT INTO users (id, name) VALUES (:id, 'bob') | 已覆盖 |
| `VO009` | `vastbase-oracle-insert-values-multi-row` | INSERT INTO users (id, name) VALUES (1, 'bob'), (2, 'alice') | 已覆盖 |
| `VO010` | `vastbase-oracle-insert-select` | INSERT INTO archive_users (id, name) SELECT id, name FROM users WHERE status = :status | 已覆盖 |
| `VO134` | `vastbase-oracle-insert-select-union-literals` | INSERT INTO users (id, name) SELECT 1, 'a' FROM dual UNION ALL SELECT 2, 'b' FROM dual | 已覆盖 |
| `VO135` | `vastbase-oracle-insert-select-union-positional-binds` | INSERT INTO users (id, name) SELECT :1, :2 FROM dual UNION ALL SELECT :3, :4 FROM dual | 已覆盖 |
| `VO136` | `vastbase-oracle-insert-select-union-named-binds` | INSERT INTO users (id, name) SELECT :id1, :name1 FROM dual UNION ALL SELECT :id2, :name2 FROM dual | 已覆盖 |
| `VO011` | `vastbase-oracle-update-bind` | UPDATE users SET name = :name, status = 'active' WHERE id = :id | 已覆盖 |
| `VO012` | `vastbase-oracle-delete-conditional` | DELETE FROM users WHERE id = :id AND status = 'inactive' | 已覆盖 |
| `VO013` | `vastbase-oracle-repeated-bind` | SELECT id FROM users WHERE id = :id OR manager_id = :id | 已覆盖 |
| `VO015` | `vastbase-oracle-date-literal` | SELECT DATE '2024-01-01' AS created_on FROM dual | 已覆盖 |
| `VO016` | `vastbase-oracle-case-expression` | SELECT CASE WHEN status = 'A' THEN 'active' ELSE 'inactive' END AS status_name FROM users | 已覆盖 |
| `VO017` | `vastbase-oracle-exists-subquery` | SELECT id FROM users u WHERE EXISTS (SELECT 1 FROM orders o WHERE o.user_id = u.id) | 已覆盖 |
| `VO018` | `vastbase-oracle-group-having` | SELECT status, COUNT(*) AS cnt FROM users GROUP BY status HAVING COUNT(*) > 1 | 已覆盖 |
| `VO019` | `vastbase-oracle-union-all` | SELECT id FROM active_users UNION ALL SELECT id FROM archived_users | 已覆盖 |
| `VO020` | `vastbase-oracle-intersect` | SELECT id FROM active_users INTERSECT SELECT id FROM archived_users | 已覆盖 |
| `VO021` | `vastbase-oracle-merge-basic` | MERGE INTO users u USING staging_users s ON (u.id = s.id) WHEN MATCHED THEN UPDATE SET name = s.name WHEN NOT MATCHED THEN INSERT (id, name) VALUES (s.id, s.name) | 已覆盖 |
| `VO022` | `vastbase-oracle-create-table` | CREATE TABLE users (id NUMBER(10), name VARCHAR2(64), created_at DATE) | 已覆盖 |
| `VO023` | `vastbase-oracle-create-sequence` | CREATE SEQUENCE user_seq START WITH 1 INCREMENT BY 1 | 已覆盖 |
| `VO024` | `vastbase-oracle-create-view` | CREATE OR REPLACE VIEW v_users AS SELECT id, name FROM users | 已覆盖 |
| `VO025` | `vastbase-oracle-drop-table` | DROP TABLE users | 已覆盖 |
| `VO026` | `vastbase-oracle-truncate-table` | TRUNCATE TABLE users | 已覆盖 |
| `VO027` | `vastbase-oracle-transaction-control` | SAVEPOINT s1; ROLLBACK TO SAVEPOINT s1; COMMIT | 已覆盖 |
| `VO028` | `vastbase-oracle-grant-revoke` | GRANT SELECT ON users TO app_user; REVOKE SELECT ON users FROM app_user | 已覆盖 |
| `VO029` | `vastbase-oracle-comment-on-table` | COMMENT ON TABLE users IS 'user table' | 已覆盖 |
| `VO030` | `vastbase-oracle-for-update-nowait` | SELECT id FROM users WHERE id = :id FOR UPDATE NOWAIT | 已覆盖 |
| `VO031` | `vastbase-oracle-decode-sysdate` | SELECT DECODE(status, 'A', 'active', 'inactive') AS status_name, SYSDATE AS now_value FROM users | 已覆盖 |
| `VO032` | `vastbase-oracle-analytic-row-number` | SELECT id, ROW_NUMBER() OVER (PARTITION BY status ORDER BY created_at) AS rn FROM users | 已覆盖 |
| `VO033` | `vastbase-oracle-timestamp-literal` | SELECT TIMESTAMP '2024-01-01 12:30:00' AS ts FROM dual | 已覆盖 |
| `VO034` | `vastbase-oracle-quoted-identifiers` | SELECT "Name" FROM "Users" WHERE "Id" = :id | 已覆盖 |
| `VO035` | `vastbase-oracle-alter-table-add-column` | ALTER TABLE users ADD age NUMBER(3) | 已覆盖 |
| `VO036` | `vastbase-oracle-create-index` | CREATE INDEX idx_users_name ON users (name) | 已覆盖 |
| `VO037` | `vastbase-oracle-drop-index` | DROP INDEX idx_users_name | 已覆盖 |
| `VO038` | `vastbase-oracle-in-list-binds` | SELECT id FROM users WHERE status IN (:status1, :status2) | 已覆盖 |
| `VO039` | `vastbase-oracle-delete-date-literal` | DELETE FROM users WHERE created_at < DATE '2020-01-01' | 已覆盖 |
| `VO040` | `vastbase-oracle-create-materialized-view-compatible-form` | CREATE MATERIALIZED VIEW mv_users AS SELECT id, name FROM users | 已覆盖 |
| `VO041` | `vastbase-oracle-unsupported-keywords-in-string` | SELECT 'RETURNING @ (+)' AS label FROM dual | 已覆盖 |
| `VO042` | `vastbase-oracle-unsupported-keywords-in-comment` | SELECT id FROM users /* CONNECT BY PRIOR id = manager_id */ WHERE id = :id | 已覆盖 |
| `VO042Q` | `vastbase-oracle-unsupported-keywords-in-quoted-identifiers` | SELECT "RETURNING", "email@domain" FROM users | 已覆盖 |
| `VO043` | `vastbase-oracle-alter-session-current-schema` | ALTER SESSION SET CURRENT_SCHEMA=APP | 已覆盖 |
| `VO043Q` | `vastbase-oracle-alter-session-current-schema-quoted-identifier` | ALTER SESSION SET CURRENT_SCHEMA="AppMixed" | 已覆盖 |
| `VO044` | `vastbase-oracle-alter-session-container` | ALTER SESSION SET CONTAINER=PDB1 | 已覆盖 |
| `VO045` | `vastbase-oracle-alter-session-container-root` | ALTER SESSION SET CONTAINER=CDB$ROOT | 已覆盖 |
| `VO046` | `vastbase-oracle-alter-session-container-service` | ALTER SESSION SET CONTAINER=PDB1 SERVICE=APP_SVC | 已覆盖 |
| `VO047` | `vastbase-oracle-alter-session-current-schema-in-multi-statement` | SELECT * FROM users; ALTER SESSION SET CURRENT_SCHEMA=APP | 已覆盖 |
| `VO048` | `vastbase-oracle-insert-question-params` | INSERT INTO users (username, email, age) VALUES (?, ?, ?) | 已覆盖 |
| `VO049` | `vastbase-oracle-update-question-params` | UPDATE users SET email = ? WHERE username = ? | 已覆盖 |
| `VO050` | `vastbase-oracle-execute-immediate` | EXECUTE IMMEDIATE 'SELECT * FROM users WHERE id = :id' USING :id | 已覆盖 |
| `VO051` | `vastbase-oracle-select-multiple-named-binds` | SELECT id, name FROM users WHERE id = :id AND status = :status | 已覆盖 |
| `VO052` | `vastbase-oracle-select-in-named-binds` | SELECT id FROM users WHERE status IN (:status1, :status2, :status3) | 已覆盖 |
| `VO053` | `vastbase-oracle-select-fetch-bind` | SELECT id FROM users WHERE name LIKE :pattern ORDER BY id FETCH FIRST :limit ROWS ONLY | 已覆盖 |
| `VO054` | `vastbase-oracle-insert-multiple-named-binds` | INSERT INTO users (id, name, status) VALUES (:id, :name, :status) | 已覆盖 |
| `VO055` | `vastbase-oracle-update-multiple-named-binds` | UPDATE users SET name = :name, status = :status WHERE id = :id | 已覆盖 |
| `VO056` | `vastbase-oracle-delete-multiple-named-binds` | DELETE FROM users WHERE id = :id AND status = :status | 已覆盖 |
| `VO057` | `vastbase-oracle-select-positional-bind-pair` | SELECT id FROM users WHERE id = :1 AND status = :2 | 已覆盖 |
| `VO058` | `vastbase-oracle-insert-question-params-expanded` | INSERT INTO users (id, name, status) VALUES (?, ?, ?) | 已覆盖 |
| `VO059` | `vastbase-oracle-delete-question-params` | DELETE FROM users WHERE id = ? AND status = ? | 已覆盖 |
| `VO060` | `vastbase-oracle-execute-immediate-update-using` | EXECUTE IMMEDIATE 'UPDATE users SET name = :name WHERE id = :id' USING :name, :id | 已覆盖 |
| `VO061` | `vastbase-oracle-rownum-pagination-nested-bind` | SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD,WORKER, WEBSITE,MSDEPLOYPORT, "UID", PWD, KEY_ENCRYPTION, MODIFYTIME FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) WHERE RN > :startRow | 已覆盖 |
| `VO062` | `vastbase-oracle-view-nvl-upper-functions` | SELECT NVL(TO_CHAR(commission_pct), 'Not Applicable') commission, UPPER(last_name) FROM employees WHERE employee_id = :employee_id | 已覆盖 |
| `VO063` | `vastbase-oracle-view-case-expression` | SELECT CASE WHEN state = 1 THEN name ELSE fallback_name END FROM users | 已覆盖 |
| `VO064` | `vastbase-oracle-view-group-having-order` | SELECT department_id, COUNT(employee_id) FROM employees GROUP BY department_id HAVING COUNT(employee_id) > 1 ORDER BY department_id | 已覆盖 |
| `VO065` | `vastbase-oracle-view-update-named-binds` | UPDATE SERVERS SET IP = :aaa WHERE ID = :id | 已覆盖 |
| `VO066` | `vastbase-oracle-view-rownum-pagination-attribution` | SELECT IP, AREACODE, "UID" FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) WHERE RN > :startRow | 已覆盖 |
| `VO067` | `vastbase-oracle-view-mixed-positional-binds` | SELECT abc FROM table1 WHERE abc LIKE :1 AND def LIKE ? | 已覆盖 |
| `VO068` | `vastbase-oracle-select-between-named-binds` | SELECT id FROM users WHERE age BETWEEN :min_age AND :max_age | 已覆盖 |
| `VO069` | `vastbase-oracle-select-not-in-named-binds` | SELECT id FROM users WHERE status NOT IN (:status1, :status2) | 已覆盖 |
| `VO070` | `vastbase-oracle-select-not-between-named-binds` | SELECT id FROM users WHERE age NOT BETWEEN :min_age AND :max_age | 已覆盖 |
| `VO071` | `vastbase-oracle-select-not-like-named-bind` | SELECT id FROM users WHERE name NOT LIKE :name_pattern | 已覆盖 |
| `VO072` | `vastbase-oracle-select-distinct-like-bind` | SELECT DISTINCT name FROM table1 WHERE name LIKE :name | 已覆盖 |
| `VO073` | `vastbase-oracle-select-distinct-nested-functions` | SELECT DISTINCT LOWER(UPPER(name)) FROM table1 | 已覆盖 |
| `VO074` | `vastbase-oracle-delete-in-named-binds` | DELETE FROM users WHERE email IN (:email1, :email2) | 已覆盖 |
| `VO075` | `vastbase-oracle-update-exists-subquery` | UPDATE users u SET status = :status WHERE EXISTS (SELECT 1 FROM orders o WHERE o.user_id = u.id AND o.phone = :phone) | 已覆盖 |
| `VO076` | `vastbase-oracle-insert-without-column-list` | INSERT INTO users VALUES (:id, :name, :age) | 已覆盖 |
| `VO077` | `vastbase-oracle-create-or-replace-view` | CREATE OR REPLACE VIEW v_user_orders AS SELECT u.id, COUNT(o.id) AS order_count FROM users u JOIN orders o ON u.id = o.user_id GROUP BY u.id | 已覆盖 |
| `VO078` | `vastbase-oracle-rownum-pagination-realistic` | SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD, WORKER, WEBSITE, MSDEPLOYPORT, "UID", PWD, KEY_ENCRYPTION, MODIFYTIME FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) WHERE RN > :startRow | 已覆盖 |
| `VO079` | `vastbase-oracle-select-alias-star` | SELECT u.*, o.order_no FROM users u LEFT JOIN orders o ON u.id = o.user_id WHERE o.status = :status | 已覆盖 |
| `VO080` | `vastbase-oracle-select-order-by-ordinal` | SELECT id, phone FROM users ORDER BY 1 | 已覆盖 |
| `VO081` | `vastbase-oracle-select-from-dual-bind` | SELECT :value FROM dual | 已覆盖 |
| `VO089` | `vastbase-oracle-select-reference-024` | select * from (select id as aa, name as bb, department as cc FROM (select e.*,rownum as row_num from employees_uuid e where rownum <= 100) where row_num >= 1 and (id = 1 or id=2)) where aa = 1; | 已覆盖 |
| `VO090` | `vastbase-oracle-select-reference-026` | select b.* from (select id as aa, department as cc, name as bb FROM (select e.*,rownum as row_num from employees_uuid e where rownum <= 100) a where row_num >= 1 and (id = 1 or id=2)) b where aa = 1; | 已覆盖 |
| `VO091` | `vastbase-oracle-select-reference-028` | SELECT * FROM (SELECT e.*, ROWNUM rn FROM employees e WHERE ROWNUM <= 100) WHERE rn > 50; | 已覆盖 |
| `VO092` | `vastbase-oracle-select-reference-033` | select * from (select rownum as num, * from (select * from ( select b.*, rownum autorowno, c.* from PERSON b left join XQGL_XQBC c on b.ID = c.xqgl_id ) a) ) d; | 已覆盖 |
| `VO093` | `vastbase-oracle-select-reference-044` | select * from ( select /*+first_rows*/ z_results.*,rownum autorowno from ( select t.XQGL_ID xqdh, t.XQGL_BT xqbt, t.xqgl_lx xqlx, (select dict_name from xggl_dict c where trim(t.XQGL_LX) = c.dict_id and c.dict_type='4') xqlxmc, t.XQGL_CJRDM xqcjrdm, t.XQGL_CJRME xqcjr, t.XQGL_LXDH lxdh, t.XQGL_SWJGDM swjgdm, t.XQGL_SWJGMC swjgmc, to_char(t.XQGL_XQCJRQ,'yyyy-MM-dd') xqcjrq, to_char(t.XQGL_XQWCRQ,'yyyy-MM-dd') xqwcrq, t.XQGL_GJGQ gjgq, t.XQGL_CONTENT content, t.XQGL_FLAG flag, t.XQGL_XQQR xqqr, t.XQGL_XQFK xqfk, t.XQGL_REMARK mark, t.XQGL_REMARK1 mgzd, case when t.XQGL_REMARK1 = '1' then '是' else '否' end as mgzdmc, t.XQGL_REMARK2 mark2, t.XQGL_REMARK3 mark3, t.xqgl_zt xqzt, t.XQGL_ZTMC xqztmc, t.xqgl_ywlx ywlx, t.XQGL_YWLXMC ywlxmc, t.xqgl_fzlx fzlx, t.XQGL_FZLXMC fzlxmc, t.XQGL_QRRQ qrrq, t.XQGL_FKRQ fkrq, t.xqgl_fk_xs fkxs, case when t.XQGL_FK_XS ='1' then '书面' when t.XQGL_FK_XS ='2' then '线上' end as fkxsmc, t.XQGL_XQFK_CONTENT fkcontent, t.xqgl_xqff_zygs_id zygs, t.xqgl_xqff_xzgs_id xzgs, t.XQGL_XQFF_ZYGS zygsmc, t.XQGL_XQFF_XZGS xzgsmc, t.xqgl_xbrq xbrq, b.xqgl_bccontent bccontent, b.xqgl_bcfkcontent bcfkcontent from xqgl t left join (select * from xqgl_xqbc where xqgl_type = 'bc') b on t.xqgl_id = b.xqgl_id where t.XQGL_DELETE !='Y' and t.XQGL_CJRDM = '110' and t.XQGL_SWJGDM = '110' order by t.XQGL_XQCJRQ desc ) z_results where rownum<=20 ) where autorowno>= 1; | 已覆盖 |
| `VO094` | `vastbase-oracle-select-reference-045` | select * from (select /*+first_rows*/ z_results.*,rownum autorowno from ( select t.fxpcbh "XXPC_BH", decode(fa.fa_mc,'','','[' \|\| fa.fa_mc \|\|'] ')\|\|t.fxpcmc "XXPC_MC", t.fxpczt "XXPCZT_DM", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXSCFS' and dm.dm = t.fxscfs) "FXSCFS", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXPCZT' and dm.dm = t.fxpczt) "XXPCZT_MC", to_char(t.cjsj,'yyy-mm-dd') "LRSJ", (select c.swjgmc from wd_swjg c where c.swjg_dm = t.cjsscsdm) "LYCS", (select c.swjgmc from wd_swig c where c.swjg_dm= t.cjswjgdm) "SWJG", nv1(t.fxxxsl,0) "FXXXS", nv1(t.shtgsl,0) "FXTGSL", nv1(t.shbtgsl,0) "FXBTGSL", (select c.czry_mc from dm_czry c where c.swry_dm = t.sprdm) spr, to_char(nv1(t.cjsj,t.tssj),'yyyy-m-dd') tssj, to_char(t.spsj,'yyyy-mm-dd') spsj, t.spsm, decode(t.spyj,'Y','通过','不通过') spyj, n.file_name fileName, n.file_path filePath, n.complete_date completeDate, n.remark from rwtc_fxpc t left join rwtc_fxpc_ydzn n on t.fxpcbh = n.fxpcbh left join fxfx_sm_slb sl on sl.smsl_bh=t.fxpcbh left join fxfx_fxfa fa on sl.fa_bh=fa.fa_bh where 1=1 and t.cjswjgdm = '110' and (t.fxpczt in ('02') or t.fxxxsl = (t.shtgsl+ t.shbtgsl)) order by fxpczt asc, t.spsj desc ) z_results where rownum<=20 ) aaa where autorowno>= 1; | 已覆盖 |
| `VO095` | `vastbase-oracle-select-reference-048` | select * from (select * from (select CONCAT(filePath, fxpczt) as path, b.* from (select * from (select /*+first_rows*/ z_results.*, rownum autorowno from ( select t.fxpcbh "XXPC_BH", decode(fa.fa_mc,'','','[' \|\| fa.fa_mc \|\|'] ')\|\|t.fxpcmc "XXPC_MC", t.fxpczt "XXPCZT_DM", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXSCFS' and dm.dm = t.fxscfs) "FXSCFS", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXPCZT' and dm.dm = t.fxpczt) "XXPCZT_MC", to_char(t.cjsj,'yyy-mm-dd') "LRSJ", (select c.swjgmc from wd_swjg c where c.swjg_dm = t.cjsscsdm) "LYCS", (select c.swjgmc from wd_swig c where c.swjg_dm= t.cjswjgdm) "SWJG", nv1(t.fxxxsl,0) "FXXXS", nv1(t.shtgsl,0) "FXTGSL", nv1(t.shbtgsl,0) "FXBTGSL", (select c.czry_mc from dm_czry c where c.swry_dm = t.sprdm) spr, to_char(nv1(t.cjsj,t.tssj),'yyyy-m-dd') tssj, to_char(t.spsj,'yyyy-mm-dd') spsj, t.spsm, decode(t.spyj,'Y','通过','不通过') spyj, n.file_name fileName, n.file_path filePath, n.complete_date completeDate, n.remark from rwtc_fxpc t left join rwtc_fxpc_ydzn n on t.fxpcbh = n.fxpcbh left join fxfx_sm_slb sl on sl.smsl_bh=t.fxpcbh left join fxfx_fxfa fa on sl.fa_bh=fa.fa_bh where 1=1 and t.cjswjgdm = '110' and (t.fxpczt in ('02') or t.fxxxsl = (t.shtgsl+ t.shbtgsl)) order by fxpczt asc, t.spsj desc ) z_results where rownum<=20 ) a1 where autorowno>= 1) b)); | 已覆盖 |
| `VO096` | `vastbase-oracle-select-reference-049` | select * from (select rownum,* from (select * from (select o.*, rownum as rnum from ( SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zxsq_zmwj_t_rid AS zmwj_key, (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXSCFS' and dm.dm = x.fxscfs) fxscfs, (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXPCZT' and dm.dm = x.fxpczt) xxpczt, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm UNION SELECT NULL AS zxsq_wj_xxgx_t_rid, z.zxsq_zmwj_t_rid AS zmwj_key, NULL AS fxscfs, NULL AS xxpczt, z.zhengmingwjdm, z.wenjiansxbm, z.wenjianysmc, z.wenjiansm, z.wenjianscfs AS wenjianlybj, NULL AS chuangjiansj, z.wenjianfwqlj, z.futubj, NULL AS yewulxbm, NULL AS wenjianywbm, NULL AS wenjiansxmc FROM zxsq_zmwj_t z WHERE z.del_flag = '0' and z.test_column = '1' AND z.zxsq_zmwj_t_rid = '1' ) o )) b) d; | 已覆盖 |
| `VO097` | `vastbase-oracle-select-reference-046` | SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zhengmingwjid AS zmwj_key, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm; | 已覆盖 |
| `VO098` | `vastbase-oracle-select-reference-047` | SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zxsq_zmwj_t_rid AS zmwj_key, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm UNION SELECT NULL AS zxsq_wj_xxgx_t_rid, z.zxsq_zmwj_t_rid AS zmwj_key, z.zhengmingwjdm, z.wenjiansxbm, z.wenjianysmc, z.wenjiansm, z.wenjianscfs AS wenjianlybj, NULL AS chuangjiansj, z.wenjianfwqlj, z.futubj, NULL AS yewulxbm, NULL AS wenjianywbm, NULL AS wenjiansxmc FROM zxsq_zmwj_t z WHERE z.del_flag = '0' AND z.zxsq_zmwj_t_rid = ''; | 已覆盖 |
| `VO137` | `vastbase-oracle-insert-all` | INSERT ALL INTO users (id) VALUES (1) INTO users (id) VALUES (2) SELECT 1 FROM dual | 已覆盖 |
| `VO132` | `vastbase-oracle-insert-all-bind-branches` | INSERT ALL INTO users (id, name) VALUES (:1, :2) INTO users (id, name) VALUES (:3, :name4) SELECT 1 FROM dual | 已覆盖 |
| `VO133` | `vastbase-oracle-insert-all-multi-target` | INSERT ALL INTO users (id, name) VALUES (1, 'a') INTO phones (id, phone) VALUES (2, '13800138000') SELECT 1 FROM dual | 已覆盖 |
| `VO082` | `vastbase-oracle-alter-session-nls-date-format` | ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD' | 已覆盖 |
| `VO083` | `vastbase-oracle-alter-session-nls-language` | ALTER SESSION SET NLS_DATE_LANGUAGE = French | 已覆盖 |
| `VO084` | `vastbase-oracle-alter-session-numeric-parameter` | ALTER SESSION SET INSTANCE = 2 | 已覆盖 |
| `VO085` | `vastbase-oracle-alter-session-boolean-parameter` | ALTER SESSION SET ERROR_ON_OVERLAP_TIME = TRUE | 已覆盖 |
| `VO086` | `vastbase-oracle-alter-session-nls-numeric-characters` | ALTER SESSION SET NLS_NUMERIC_CHARACTERS = '.,' | 已覆盖 |
| `VO087` | `vastbase-oracle-multi-statement-global-bind-position` | UPDATE users SET a = :same WHERE b = :b; UPDATE users SET c = :same WHERE d = :d | 已覆盖 |
| `VO088` | `vastbase-oracle-select-derived-query-graph` | SELECT s.name AS outer_name FROM (SELECT id, name FROM APP.USERS WHERE age <= :age) s WHERE s.name LIKE :name | 已覆盖 |
| `VO099` | `vastbase-oracle-select-nested-star-query-graph` | SELECT * FROM (SELECT ROWNUM, * FROM (SELECT * FROM (SELECT o.*, ROWNUM AS rnum FROM (SELECT x.id FROM users x UNION SELECT y.id FROM archived_users y) o)) b) d | 已覆盖 |
| `VO100` | `vastbase-oracle-field-match-kind-direct-and-expression` | SELECT ID FROM APP.DBP_CRYPTO_TEST WHERE SECRET = :plain_secret AND UPPER(SECRET) = :upper_secret | 已覆盖 |
| `VO101` | `vastbase-oracle-expression-field-case-expression-value` | SELECT ID FROM APP.DBP_CRYPTO_TEST WHERE CASE WHEN ID = 1 THEN SECRET ELSE BACKUP_SECRET END = :v | 已覆盖 |
| `VO102` | `vastbase-oracle-expression-field-multi-field-expression-value` | SELECT ID FROM APP.DBP_CRYPTO_TEST WHERE NVL(SECRET, ID) = :v1 AND SECRET \|\| ID = :v2 | 已覆盖 |
| `VO103` | `vastbase-oracle-expression-field-value-side-expression` | SELECT ID FROM APP.DBP_CRYPTO_TEST WHERE SECRET = UPPER(:v1) AND SECRET = :v2 \|\| 'x' AND SECRET = CAST(:v3 AS VARCHAR(32)) | 已覆盖 |
| `VO104` | `vastbase-oracle-expression-field-dml-expression-values` | INSERT INTO APP.DBP_CRYPTO_TEST (ID, SECRET) VALUES (1, UPPER(:v1)); UPDATE APP.DBP_CRYPTO_TEST SET SECRET = :v2 \|\| 'x' WHERE ID = 1 | 已覆盖 |
| `VO105` | `vastbase-oracle-update-positional-bind-rhs-crypto-source` | UPDATE APP.DBP_CRYPTO_TEST SET SECRET = :1 WHERE ID = :2 | 已覆盖 |
| `VO106` | `vastbase-oracle-update-named-bind-rhs-crypto-source` | UPDATE APP.DBP_CRYPTO_TEST SET SECRET = :secret_value WHERE ID = :id | 已覆盖 |
| `VO107` | `vastbase-oracle-update-question-bind-rhs-crypto-source` | UPDATE APP.DBP_CRYPTO_TEST SET SECRET = ? WHERE ID = ? | 已覆盖 |
| `VO108` | `vastbase-oracle-update-multiple-bind-rhs-crypto-source` | UPDATE APP.DBP_CRYPTO_TEST SET PHONE = :1, SECRET = :2 WHERE ID = :3 | 已覆盖 |
| `VOU014` | `vastbase-oracle-create-synonym` | CREATE SYNONYM u FOR users | 已覆盖 |
| `VO175` | `vastbase-oracle-create-public-synonym` | CREATE OR REPLACE PUBLIC SYNONYM app_users FOR app.users | 已覆盖 |
| `VO176` | `vastbase-oracle-drop-synonym` | DROP SYNONYM app_users FORCE | 已覆盖 |
| `VOU015` | `vastbase-oracle-database-link` | SELECT * FROM users@remote_db | 已覆盖 |
| `VOU016` | `vastbase-oracle-explain-plan` | EXPLAIN PLAN FOR SELECT * FROM users | 已覆盖 |
| `VO177` | `vastbase-oracle-explain-plan-into` | EXPLAIN PLAN SET STATEMENT_ID = 'q1' INTO plan_table FOR SELECT id FROM users WHERE id = :id | 已覆盖 |
| `VO138` | `vastbase-oracle-insert-first` | INSERT FIRST WHEN 1 = 1 THEN INTO users (id) VALUES (1) SELECT 1 FROM dual | 已覆盖 |
| `VO139` | `vastbase-oracle-insert-first-direct-source-fields` | INSERT FIRST WHEN amount > 100 THEN INTO big_orders (id, amount) VALUES (order_id, amount) ELSE INTO small_orders (id, amount) VALUES (order_id, amount) SELECT id AS order_id, amount FROM orders | 已覆盖 |
| `VO140` | `vastbase-oracle-insert-all-conditional` | INSERT ALL WHEN flag = 1 THEN INTO users (id, flag_copy) VALUES (:1, flag) WHEN flag = 2 THEN INTO audit_users (id, flag_copy) VALUES (:2, flag) SELECT flag FROM source_table | 已覆盖 |
| `VO141` | `vastbase-oracle-insert-all-multiple-into-per-when` | INSERT ALL WHEN flag = 1 THEN INTO users (id) VALUES (:1) INTO audit_users (id) VALUES (:2) ELSE INTO rejected_users (id) VALUES (:3) SELECT flag FROM source_table | 已覆盖 |
| `VO142` | `vastbase-oracle-insert-select-source-fields` | INSERT INTO users (id, name) SELECT src_id, src_name FROM source_users | 已覆盖 |
| `VO143` | `vastbase-oracle-insert-select-expression-targets` | INSERT INTO users (id, name) SELECT src_id + 1, UPPER(src_name) FROM source_users | 已覆盖 |
| `VO144` | `vastbase-oracle-insert-all-source-field-and-expression-cells` | INSERT ALL INTO users (id, name, name_upper) VALUES (src_id, src_name, UPPER(src_name)) SELECT src_id, src_name FROM source_users | 已覆盖 |
| `VO145` | `vastbase-oracle-insert-select-union-distinct-literals` | INSERT INTO users (id, name) SELECT 1, 'a' FROM dual UNION SELECT 2, 'b' FROM dual | 已覆盖 |
| `VO146` | `vastbase-oracle-insert-select-intersect-binds` | INSERT INTO users (id, name) SELECT :1, :2 FROM dual INTERSECT SELECT :3, :4 FROM dual | 已覆盖 |
| `VO147` | `vastbase-oracle-insert-select-minus-named-binds` | INSERT INTO users (id, name) SELECT :id1, :name1 FROM dual MINUS SELECT :id2, :name2 FROM dual | 已覆盖 |
| `VO148` | `vastbase-oracle-insert-all-schema-qualified-targets` | INSERT ALL INTO APP.DBP_CRYPTO_TEST (ID, SECRET) VALUES (950001, 'a') INTO APP.DBP_PHONE_TEST (ID, PHONE) VALUES (:2, :phone) SELECT 1 FROM DUAL | 已覆盖 |
| `VO149` | `vastbase-oracle-like-escape-literal` | SELECT ID FROM APP.USERS WHERE NAME LIKE 'A!_%' ESCAPE '!' | 已覆盖 |
| `VO150` | `vastbase-oracle-not-like-escape-named-bind` | SELECT ID FROM APP.USERS WHERE NAME NOT LIKE :pattern ESCAPE :escape_char | 已覆盖 |
| `VO151` | `vastbase-oracle-like-escape-question-bind` | SELECT ID FROM APP.USERS WHERE NAME LIKE ? ESCAPE ? | 已覆盖 |
| `VO152` | `vastbase-oracle-like-escape-expression` | SELECT ID FROM APP.USERS WHERE NAME LIKE :pattern ESCAPE UPPER('!') | 已覆盖 |
| `VO153` | `vastbase-oracle-derived-like-escape-literal` | SELECT D.ID FROM (SELECT ID, NAME FROM APP.USERS) D WHERE D.NAME LIKE :pattern ESCAPE '!' | 已覆盖 |
| `VO154` | `vastbase-oracle-like-without-explicit-escape` | SELECT ID FROM APP.USERS WHERE NAME LIKE :pattern | 已覆盖 |
| `VO155` | `vastbase-oracle-p3-update-alias-qualified-assignment` | UPDATE encrypt_test_data x SET x.email = :1 WHERE x.id = :2 | 已覆盖 |
| `VO156` | `vastbase-oracle-p3-update-multiple-alias-qualified-assignments` | UPDATE encrypt_test_data x SET x.email = :1, x.secret_sn = :2 WHERE x.phone = :3 | 已覆盖 |
| `VO157` | `vastbase-oracle-p3-update-from-source-field` | UPDATE t SET name = s.name FROM src s WHERE t.id = s.id | 已覆盖 |
| `VO158` | `vastbase-oracle-p3-update-schema-qualified-alias-target` | UPDATE APP.ENCRYPT_TEST_DATA x SET x.email = :1 WHERE x.id = :2 | 已覆盖 |
| `VO159` | `vastbase-oracle-p3-update-scalar-subquery-predicate` | UPDATE encrypt_test_data x SET x.email = :1 WHERE x.id = (SELECT y.id FROM encrypt_test_data y WHERE y.phone = :2) | 已覆盖 |
| `VO160` | `vastbase-oracle-p3-delete-exists-correlated-predicate` | DELETE FROM encrypt_test_data x WHERE EXISTS (...) | 已覆盖 |
| `VO161` | `vastbase-oracle-p3-select-or-predicate-and-order-by` | SELECT x.id,x.email,x.bank_card FROM encrypt_test_data x WHERE ... OR ... ORDER BY x.id | 已覆盖 |
| `VO162` | `vastbase-oracle-p3-insert-all-independent-branches` | INSERT ALL INTO encrypt_test_data(...) VALUES(...) INTO encrypt_test_data(...) VALUES(...) SELECT 1 FROM dual | 已覆盖 |
| `VO163` | `vastbase-oracle-p3-merge-update-source-target-lineage` | MERGE INTO t USING (SELECT :1 id, :2 email FROM dual) s ... UPDATE SET t.email=s.email | 已覆盖 |
| `VO164` | `vastbase-oracle-p3-merge-insert-source-target-lineage` | MERGE INTO t USING (SELECT :1 id, :2 email FROM dual) s ... INSERT(id,email) VALUES(s.id,s.email) | 已覆盖 |
| `VO165` | `vastbase-oracle-p3-select-distinct-base-field-lineage` | SELECT DISTINCT x.email FROM encrypt_test_data x | 已覆盖 |
| `VO166` | `vastbase-oracle-p3-select-alias-order-by-lineage` | SELECT x.email AS e FROM encrypt_test_data x ORDER BY x.email | 已覆盖 |
| `VO167` | `vastbase-oracle-p3-select-star-rowid-lineage` | SELECT x.*, x.ROWID FROM encrypt_test_data x ORDER BY x.id | 已覆盖 |
| `VO168` | `vastbase-oracle-p3-update-full-alias-qualified-crypto-shape` | UPDATE encrypt_test_data x SET x.email=:1, x.secret_sn=:2, x.special_str=:3, x.remark=:4 WHERE ... | 已覆盖 |
| `VO169` | `vastbase-oracle-regexp-like-function-predicate` | SELECT * FROM users WHERE REGEXP_LIKE(name, :pat) | 已覆盖 |
| `VO170` | `vastbase-oracle-database-link-schema-alias-bind` | SELECT u.id FROM app.users@remote_db u WHERE u.id = :id | 已覆盖 |
| `VO171` | `vastbase-oracle-database-link-update-target` | UPDATE users@remote_db SET name = :name WHERE id = :id | 已覆盖 |
| `VO172` | `vastbase-oracle-database-link-insert-target` | INSERT INTO users@remote_db (id, name) VALUES (:id, :name) | 已覆盖 |
| `VO173` | `vastbase-oracle-database-link-delete-target` | DELETE FROM users@remote_db WHERE id = :id | 已覆盖 |
| `VO174` | `vastbase-oracle-database-link-quoted-identifiers` | SELECT * FROM "USERS"@"REMOTE_DB" | 已覆盖 |
| `VO178` | `vastbase-oracle-union-all-three-branch-scope` | (SELECT 1 AS C FROM DUAL UNION ALL SELECT 2 AS C FROM DUAL) UNION ALL SELECT 3 AS C FROM DUAL | 已覆盖 |
| `VO179` | `vastbase-oracle-grouped-union-all-intersect` | (SELECT 1 AS C FROM DUAL UNION ALL SELECT 2 AS C FROM DUAL) INTERSECT (SELECT 3 AS C FROM DUAL UNION ALL SELECT 4 AS C FROM DUAL) | 已覆盖 |
| `VO180` | `vastbase-oracle-union-all-root-cte-scope` | WITH src AS (SELECT 1 AS C FROM DUAL) SELECT C FROM src UNION ALL SELECT C FROM src | 已覆盖 |
| `VO181` | `vastbase-oracle-union-all-qualified-table-bypasses-cte` | WITH src AS (SELECT 1 AS id FROM DUAL) (SELECT src.id FROM src UNION ALL SELECT s.id FROM app.src s) UNION ALL SELECT r.id FROM src@remote_db r | 已覆盖 |
| `VO182` | `vastbase-oracle-correlated-union-all-subquery-scope` | SELECT o.id FROM orders o WHERE EXISTS (SELECT 1 FROM order_items i WHERE i.order_id = o.id UNION ALL SELECT 1 FROM archived_order_items a WHERE a.order_id = o.id) | 已覆盖 |
| `VO183` | `vastbase-oracle-insert-returning-rowid-into-bind` | INSERT INTO "APP"."DBP_MANUAL_CLOB_FPE_STRESS" ("ID", "PROTECTED_CLOB_3") VALUES (:1, :2) RETURNING ROWID INTO :NAV_ROWID | 已覆盖 |
| `VO184` | `vastbase-oracle-update-returning-rowid-into-bind` | UPDATE "APP"."DBP_MANUAL_CLOB_FPE_STRESS" SET "PROTECTED_CLOB_3" = :1 WHERE "ID" = :2 RETURNING ROWID INTO :NAV_ROWID | 已覆盖 |
| `VO185` | `vastbase-oracle-delete-returning-rowid-into-bind` | DELETE FROM "APP"."DBP_MANUAL_CLOB_FPE_STRESS" WHERE "ID" = :1 RETURNING ROWID INTO :NAV_ROWID | 已覆盖 |
| `VO186` | `vastbase-oracle-insert-returning-eight-target-bind-pairs` | INSERT ... RETURNING 8 targets INTO 8 colon binds | 已覆盖 |
| `VO187` | `vastbase-oracle-update-returning-eight-target-bind-pairs` | UPDATE ... RETURNING 8 targets INTO 8 colon binds | 已覆盖 |
| `VO188` | `vastbase-oracle-delete-returning-eight-target-bind-pairs` | DELETE ... RETURNING 8 targets INTO 8 colon binds | 已覆盖 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。未纳入该可执行夹具的语法不得在本矩阵中登记为已验证用例。

`RETURNING ... INTO` 不接受 `BULK COLLECT`、非冒号 bind receiver 或 target/receiver 数量不等的输入。该边界仅是项目兼容入口合同及可执行证据，不声称 Vastbase 服务端官方支持同一语法范围。
