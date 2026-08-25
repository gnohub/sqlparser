# Vastbase PostgreSQL 兼容模式用例矩阵

可执行夹具为 `tests/cases/vastbase_postgresql_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。

## 事务特征规范语义值回归

以下 4 条 final 用例覆盖 Vastbase PostgreSQL 兼容模式的常用事务隔离级别和访问模式。原始 SQL 反解析必须逐字节保持；View 必须按输入顺序输出去除 trivia 后的规范关键字值。session 语义值不提供 selector，因此这些用例不设置 patch。

| ID | 用例 | SQL | 验证重点 |
| --- | --- | --- | --- |
| `VPG-TX001` | `vastbase-postgresql-session-transaction-commented-read-uncommitted` | ALTER/*command*/SESSION SET TRANSACTION ISOLATION/*name*/LEVEL READ/*value*/UNCOMMITTED; | `READ UNCOMMITTED` 规范值及单事务特征 |
| `VPG-TX002` | `vastbase-postgresql-session-characteristics-commented-repeatable-read-write` | ALTER SESSION SET SESSION/*scope*/CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE/*value*/READ, READ/*mode*/WRITE; | `REPEATABLE READ`、`READ WRITE` 及 session characteristics 入口 |
| `VPG-TX003` | `vastbase-postgresql-session-transaction-commented-serializable-read-only` | ALTER SESSION SET TRANSACTION ISOLATION LEVEL SERIALIZABLE/*tail*/, READ/*mode*/ONLY; | `SERIALIZABLE`、`READ ONLY` 及逗号前注释 |
| `VPG-TX004` | `vastbase-postgresql-session-transaction-commented-option-order` | ALTER SESSION SET TRANSACTION read/*mode*/write, ISOLATION/*name*/LEVEL read/*value*/committed; | 输入选项顺序、小写原文保留及 `READ COMMITTED` 规范值 |

## 矩阵统计与 session 回归

夹具包含 209 条 `status = "final"` 用例和 684 个独立 patch，其中 35 条用例的期望 View 包含非空 session 投影。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session action、item scope、target kind、name、value 类型、规范文本及顺序均属于比较范围。

## MERGE INSERT 独立列值改写回归

以下 final 用例定义项目 `vastbase-postgresql` 兼容入口合同，不声称 Vastbase 服务端官网定义了相同语法范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VPG203` | `vastbase-postgresql-merge-omitted-insert-column-value-independent` | final | 3 | 省略目标列清单时输出 `target_list_selector`；分别验证仅增加目标列并物化清单、仅增加 VALUES cell 且保持清单省略，以及替换已有 cell |

## 定界别名状态回归

`vastbase-postgresql-quoted-relation-alias-and-target-output-contract` 及其 2 个 output alias patch 验证 Query Graph 字段合同：relation alias 的精确来源 token 使用双引号时输出 `alias_quoted_identifier: true`；target 的 `output_name` 来源于带双引号的显式 alias，或无显式 alias 时来源于带双引号的直接字段 token，则输出 `output_quoted_identifier: true`。未定界来源不输出对应字段。该合同属于项目兼容入口，不代表 Vastbase 服务端官方语法范围。

## 定界关系分段与 DML 列状态回归

以下 final 用例以同名定界/未定界标识符对照验证 relation 的 database、schema、object 分段状态和 DML 目标列状态；4 个独立 patch 覆盖 relation 整体替换、MERGE INSERT 目标列替换与定界目标列插入，并要求 patch handle 与重新解析 handle 的标志一致。该用例定义项目 `vastbase-postgresql` 兼容入口合同，不声称 Vastbase 服务端官网定义了相同语法范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VPG204` | `vastbase-postgresql-quoted-identifier-segment-and-dml-column-inventory` | final | 4 | 多语句 SELECT、INSERT、UPDATE、DELETE、MERGE 与 `DEFAULT VALUES` 覆盖 `database_quoted_identifier`、`schema_quoted_identifier`、relation `quoted_identifier` 及 DML column `quoted_identifier`；未定界同名分段不输出对应 View 字段 |

## DDL relation 投影回归

以下 5 条 final 用例定义项目 `vastbase-postgresql` 兼容入口的 DDL Query Graph 合同：DDL 根块使用 `kind = "ddl"`，relation 以 `ddl_role = "target"` 或 `"reference"` 区分操作对象与引用对象，并保留每个 database/schema/object 来源分段的定界状态。查询驱动的 CREATE 对象和 `SELECT INTO` 将 DDL target 的 `source_block` 指向独立 SELECT 块。语法形态以夹具中已验证的 PostgreSQL 兼容语法为边界；该合同属于项目兼容入口，不声称 Vastbase 服务端官网定义了相同范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VPG205` | `vastbase-postgresql-ddl-relation-direct-inventory` | final | 8 | CREATE TABLE 的 FK、LIKE、INHERITS reference，ALTER TABLE FK，CREATE INDEX，多对象 DROP/TRUNCATE，RENAME 旧对象与 DROP VIEW/MATERIALIZED VIEW target；relation patch 重算定界状态 |
| `VPG206` | `vastbase-postgresql-ddl-relation-partition-operations` | final | 3 | ATTACH/DETACH PARTITION 中被操作表为 target、分区表为 reference，两者可独立改写 |
| `VPG207` | `vastbase-postgresql-ddl-relation-query-backed-inventory` | final | 3 | CREATE VIEW、CTAS 与 CREATE MATERIALIZED VIEW 的 DDL target、SELECT 来源及 `source_block` 关联 |
| `VPG208` | `vastbase-postgresql-ddl-relation-select-into` | final | 2 | `SELECT INTO` 目标作为 DDL target，FROM relation 保留在独立 SELECT 块，两侧 selector 均可改写 |
| `VPG209` | `vastbase-postgresql-ddl-relation-foreign-table-and-exact-drop-spelling` | final | 1 | CREATE/ALTER/RENAME/DROP FOREIGN TABLE target 生命周期与 patch；同名 DROP、`if` 标识符、U& identifier/`UESCAPE` 后续目标的精确分段状态 |

## 完整绑定占位符 occurrence 回归

以下 2 条 final 用例定义项目 `vastbase-postgresql` 兼容入口的 handle 级 occurrence 合同，不作为 Vastbase 服务端官方能力声明。runner 对输入及每个 patch 后的公开 SQL 逐项断言 `position`、`kind`、`key` 和 `sql`；重复 `$n` 不合并，多语句编号不重置，字符串、注释、定界标识符和 JSONB `?` / `?|` / `?&` 操作符不计入。

| 用例 | 根 occurrence | Patch | 基础入口关系 | 验证重点 |
| --- | ---: | ---: | --- | --- |
| `vastbase-postgresql-insert-returning` | 0 | 5 | SQL、patch 和 occurrence 断言镜像基础 `insert-returning`；仅用例名和方言入口选择不同 | 无 bind 的原 SQL 返回空列表；RETURNING target 改写为 `$1 AS echoed` 后返回单个 occurrence |
| `vastbase-postgresql-postgresql-multi-statement-global-bind-position` | 8 | 5 | SQL、patch 和 occurrence 断言镜像基础 `postgresql-multi-statement-global-bind-position`；仅用例名和方言入口选择不同 | UPDATE、MERGE、带参数 CALL、重复 key、注释保护；复杂改写覆盖子查询、LIMIT/OFFSET、CASE、cast 和 JSONB 问号操作符 |

| ID | 用例 | SQL | 状态 |
| --- | --- | --- | --- |
| `VPG001` | `vastbase-postgresql-select-basic` | SELECT 1 | 已覆盖 |
| `VPG002` | `vastbase-postgresql-select-filter` | SELECT id, name FROM public.users WHERE id = 42 | 已覆盖 |
| `VPG003` | `vastbase-postgresql-select-join` | SELECT u.id, u.name, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid' | 已覆盖 |
| `VPG004` | `vastbase-postgresql-select-cte` | WITH active_users AS (SELECT id, name FROM public.users WHERE status = 'active') SELECT au.id, au.name FROM active_users au | 已覆盖 |
| `VPG005` | `vastbase-postgresql-insert-single-row` | INSERT INTO public.users (id, name) VALUES (1, 'alice') | 已覆盖 |
| `VPG006` | `vastbase-postgresql-insert-multi-row` | INSERT INTO public.users (id, name) VALUES (1, 'alice'), (2, 'bob') | 已覆盖 |
| `VPG007` | `vastbase-postgresql-insert-from-select` | INSERT INTO public.user_archive (id, name) SELECT id, name FROM public.users WHERE status = 'inactive' | 已覆盖 |
| `VPG008` | `vastbase-postgresql-update-basic` | UPDATE public.users SET name = 'alice', status = 'active' WHERE id = 1 | 已覆盖 |
| `VPG009` | `vastbase-postgresql-delete-conditional` | DELETE FROM public.users WHERE id = 1 AND status = 'active' | 已覆盖 |
| `VPG010` | `vastbase-postgresql-delete-in-list` | DELETE FROM public.users WHERE id IN (1, 2, 3) AND status = 'active' | 已覆盖 |
| `VPG011` | `vastbase-postgresql-drop-table` | DROP TABLE public.users | 已覆盖 |
| `VPG012` | `vastbase-postgresql-drop-view` | DROP VIEW public.v_users | 已覆盖 |
| `VPG013` | `vastbase-postgresql-create-view` | CREATE VIEW public.v_users AS SELECT id, name FROM public.users | 已覆盖 |
| `VPG014` | `vastbase-postgresql-truncate-table` | TRUNCATE TABLE public.users | 已覆盖 |
| `VPG015` | `vastbase-postgresql-comment-table` | COMMENT ON TABLE public.users IS 'hot' | 已覆盖 |
| `VPG016` | `vastbase-postgresql-rename-table` | ALTER TABLE public.users RENAME TO users_archive | 已覆盖 |
| `VPG017` | `vastbase-postgresql-alter-table-add-column` | ALTER TABLE public.users ADD COLUMN age integer | 已覆盖 |
| `VPG018` | `vastbase-postgresql-create-index` | CREATE INDEX idx_users_name ON public.users (name) | 已覆盖 |
| `VPG019` | `vastbase-postgresql-drop-index` | DROP INDEX public.idx_users_name | 已覆盖 |
| `VPG020` | `vastbase-postgresql-explain-select` | EXPLAIN SELECT id FROM public.users | 已覆盖 |
| `VPG021` | `vastbase-postgresql-copy-table` | COPY public.users (id, name) FROM STDIN | 已覆盖 |
| `VPG022` | `vastbase-postgresql-lock-table` | LOCK TABLE public.users IN ACCESS EXCLUSIVE MODE | 已覆盖 |
| `VPG023` | `vastbase-postgresql-call-procedure` | CALL public.refresh_users() | 已覆盖 |
| `VPG024` | `vastbase-postgresql-do-block` | DO $$ BEGIN NULL; END $$ | 已覆盖 |
| `VPG025` | `vastbase-postgresql-create-table-as` | CREATE TABLE public.active_users AS SELECT id FROM public.users WHERE status = 'active' | 已覆盖 |
| `VPG026` | `vastbase-postgresql-transaction-begin-commit` | BEGIN; COMMIT; | 已覆盖 |
| `VPG027` | `vastbase-postgresql-transaction-begin-insert-rollback` | BEGIN; INSERT INTO public.users (id, name) VALUES (1, 'alice'); ROLLBACK; | 已覆盖 |
| `VPG028` | `vastbase-postgresql-multi-statement-mixed` | SELECT 1; INSERT INTO public.audit_log (id, action) VALUES (1, 'login') | 已覆盖 |
| `VPG029` | `vastbase-postgresql-quoted-identifiers` | SELECT "User"."Name" FROM "User" | 已覆盖 |
| `VPG030` | `vastbase-postgresql-literal-semicolon` | SELECT ';' AS token | 已覆盖 |
| `VPG031` | `vastbase-postgresql-select-subquery-exists` | SELECT u.id, EXISTS (SELECT 1 FROM public.orders o WHERE o.user_id = u.id) AS has_orders FROM public.users u | 已覆盖 |
| `VPG032` | `vastbase-postgresql-select-case-window` | SELECT p.id, CASE WHEN p.status = 'paid' THEN p.order_no ELSE 'pending' END AS label, sum(p.amount) OVER (PARTITION BY p.user_id ORDER BY p.created_at) AS running_amount FROM public.payments p | 已覆盖 |
| `VPG033` | `vastbase-postgresql-select-union-order-limit` | SELECT id FROM public.users UNION ALL SELECT id FROM public.user_archive ORDER BY id LIMIT 10 | 已覆盖 |
| `VPG034` | `vastbase-postgresql-insert-on-conflict-update` | INSERT INTO public.users (id, name) VALUES (1, 'alice') ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name RETURNING id | 已覆盖 |
| `VPG035` | `vastbase-postgresql-insert-returning` | INSERT INTO public.audit_log (id, action) VALUES (1, 'login') RETURNING id, action | 已覆盖 |
| `VPG036` | `vastbase-postgresql-update-from-returning` | UPDATE public.users AS u SET status = src.status FROM public.user_stage AS src WHERE u.id = src.id RETURNING u.id | 已覆盖 |
| `VPG037` | `vastbase-postgresql-delete-using-returning` | DELETE FROM public.users USING public.user_archive AS ua WHERE public.users.id = ua.id RETURNING public.users.id | 已覆盖 |
| `VPG038` | `vastbase-postgresql-merge-basic` | MERGE INTO public.target_table AS t USING public.source_table AS s ON t.id = s.id WHEN MATCHED THEN UPDATE SET name = s.name WHEN NOT MATCHED THEN INSERT (id, name) VALUES (s.id, s.name) | 已覆盖 |
| `VPG039` | `vastbase-postgresql-savepoint-release` | BEGIN; SAVEPOINT sp1; RELEASE SAVEPOINT sp1; COMMIT; | 已覆盖 |
| `VPG040` | `vastbase-postgresql-rollback-to-savepoint` | BEGIN; SAVEPOINT sp1; INSERT INTO public.audit_log (id, action) VALUES (1, 'login'); ROLLBACK TO SAVEPOINT sp1; COMMIT; | 已覆盖 |
| `VPG041` | `vastbase-postgresql-create-materialized-view` | CREATE MATERIALIZED VIEW public.mv_users AS SELECT id, name FROM public.users | 已覆盖 |
| `VPG042` | `vastbase-postgresql-alter-table-drop-column` | ALTER TABLE public.users DROP COLUMN age | 已覆盖 |
| `VPG043` | `vastbase-postgresql-create-schema` | CREATE SCHEMA analytics | 已覆盖 |
| `VPG044` | `vastbase-postgresql-drop-schema` | DROP SCHEMA analytics | 已覆盖 |
| `VPG045` | `vastbase-postgresql-grant-select` | GRANT SELECT ON TABLE public.users TO analyst | 已覆盖 |
| `VPG046` | `vastbase-postgresql-revoke-select` | REVOKE SELECT ON TABLE public.users FROM analyst | 已覆盖 |
| `VPG047` | `vastbase-postgresql-analyze-table` | ANALYZE public.users | 已覆盖 |
| `VPG048` | `vastbase-postgresql-vacuum-analyze-table` | VACUUM ANALYZE public.users | 已覆盖 |
| `VPG049` | `vastbase-postgresql-postgresql-set-search-path` | SET search_path TO app_schema, public | 已覆盖 |
| `VPG050` | `vastbase-postgresql-postgresql-set-schema` | SET SCHEMA 'app_schema' | 已覆盖 |
| `VPG051` | `vastbase-postgresql-postgresql-set-local-search-path` | SET LOCAL search_path = app_schema | 已覆盖 |
| `VPG052` | `vastbase-postgresql-postgresql-prepare-select` | PREPARE user_by_id(int) AS SELECT * FROM users WHERE id = $1 | 已覆盖 |
| `VPG053` | `vastbase-postgresql-postgresql-execute-prepared` | EXECUTE user_by_id(42) | 已覆盖 |
| `VPG054` | `vastbase-postgresql-postgresql-deallocate-prepare` | DEALLOCATE PREPARE user_by_id | 已覆盖 |
| `VPG055` | `vastbase-postgresql-postgresql-select-dollar-params` | SELECT id, name FROM users WHERE id = $1 AND status = $2 | 已覆盖 |
| `VPG056` | `vastbase-postgresql-postgresql-select-in-dollar-params` | SELECT id FROM users WHERE status IN ($1, $2, $3) | 已覆盖 |
| `VPG057` | `vastbase-postgresql-postgresql-select-between-dollar-params` | SELECT id FROM users WHERE age BETWEEN $1 AND $2 | 已覆盖 |
| `VPG058` | `vastbase-postgresql-postgresql-select-not-in-dollar-params` | SELECT id FROM users WHERE status NOT IN ($1, $2) | 已覆盖 |
| `VPG059` | `vastbase-postgresql-postgresql-select-not-between-dollar-params` | SELECT id FROM users WHERE age NOT BETWEEN $1 AND $2 | 已覆盖 |
| `VPG060` | `vastbase-postgresql-postgresql-select-limit-dollar-params` | SELECT id FROM users WHERE name LIKE $1 ORDER BY id LIMIT $2 OFFSET $3 | 已覆盖 |
| `VPG061` | `vastbase-postgresql-postgresql-insert-dollar-params` | INSERT INTO users (id, name, status) VALUES ($1, $2, $3) | 已覆盖 |
| `VPG062` | `vastbase-postgresql-postgresql-insert-multi-row-dollar-params` | INSERT INTO users (id, name) VALUES ($1, $2), ($3, $4) | 已覆盖 |
| `VPG063` | `vastbase-postgresql-postgresql-update-dollar-params` | UPDATE users SET name = $1, status = $2 WHERE id = $3 | 已覆盖 |
| `VPG064` | `vastbase-postgresql-postgresql-delete-dollar-params` | DELETE FROM users WHERE id = $1 AND status = $2 | 已覆盖 |
| `VPG065` | `vastbase-postgresql-postgresql-prepare-insert` | PREPARE insert_user(int, text) AS INSERT INTO users (id, name) VALUES ($1, $2) | 已覆盖 |
| `VPG066` | `vastbase-postgresql-postgresql-prepare-update` | PREPARE update_user(text, int) AS UPDATE users SET name = $1 WHERE id = $2 | 已覆盖 |
| `VPG067` | `vastbase-postgresql-postgresql-prepare-delete` | PREPARE delete_user(int) AS DELETE FROM users WHERE id = $1 | 已覆盖 |
| `VPG068` | `vastbase-postgresql-postgresql-execute-prepared-with-args` | EXECUTE insert_user(1, 'bob') | 已覆盖 |
| `VPG069` | `vastbase-postgresql-postgresql-deallocate-all` | DEALLOCATE ALL | 已覆盖 |
| `VPG070` | `vastbase-postgresql-postgresql-view-direct-column` | SELECT name FROM users | 已覆盖 |
| `VPG071` | `vastbase-postgresql-postgresql-view-star-qualified-star` | SELECT u.*, u.id FROM public.users u | 已覆盖 |
| `VPG072` | `vastbase-postgresql-postgresql-view-functions-and-args` | SELECT CONCAT(UPPER(first_name), last_name), COALESCE(nickname, name), CAST(age AS text) FROM users | 已覆盖 |
| `VPG073` | `vastbase-postgresql-postgresql-view-distinct-nested-functions` | SELECT DISTINCT LOW(UPPER(name)) FROM table1 | 已覆盖 |
| `VPG074` | `vastbase-postgresql-postgresql-view-expressions-and-case` | SELECT first_name \|\| last_name, UPPER(name) \|\| '_x', CASE WHEN state = 1 THEN name ELSE fallback_name END FROM users | 已覆盖 |
| `VPG075` | `vastbase-postgresql-postgresql-view-group-having-order` | SELECT dept, COUNT(id) FROM users GROUP BY dept HAVING COUNT(id) > 1 ORDER BY dept | 已覆盖 |
| `VPG076` | `vastbase-postgresql-postgresql-view-join-on` | SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = 'paid' | 已覆盖 |
| `VPG077` | `vastbase-postgresql-postgresql-view-window-array-row-tests` | SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at), ARRAY[id, age], ROW(id, age), name IS NULL, active IS TRUE FROM users | 已覆盖 |
| `VPG078` | `vastbase-postgresql-postgresql-view-bind-values` | UPDATE servers SET ip = $1 WHERE id = $2 | 已覆盖 |
| `VPG079` | `vastbase-postgresql-postgresql-view-not-like-bind` | SELECT id FROM users WHERE name NOT LIKE $1 | 已覆盖 |
| `VPG080` | `vastbase-postgresql-postgresql-view-not-ilike-bind` | SELECT id FROM users WHERE name NOT ILIKE $1 | 已覆盖 |
| `VPG081` | `vastbase-postgresql-postgresql-view-not-similar-bind` | SELECT id FROM users WHERE name NOT SIMILAR TO $1 | 已覆盖 |
| `VPG082` | `vastbase-postgresql-postgresql-create-table-if-not-exists-types` | CREATE TABLE IF NOT EXISTS public.secure_users (id BIGINT, name VARCHAR(64), phone TEXT, payload JSONB, created_at TIMESTAMP) | 已覆盖 |
| `VPG083` | `vastbase-postgresql-postgresql-insert-without-column-list` | INSERT INTO public.users VALUES ($1, $2, $3) | 已覆盖 |
| `VPG084` | `vastbase-postgresql-postgresql-update-in-not-in-conditions` | UPDATE public.users SET status = $1 WHERE phone IN ($2, $3) AND email NOT IN ($4, $5) | 已覆盖 |
| `VPG085` | `vastbase-postgresql-postgresql-select-rich-where` | SELECT id, phone FROM public.users WHERE phone IS NOT NULL AND amount BETWEEN $1 AND $2 AND name LIKE $3 | 已覆盖 |
| `VPG086` | `vastbase-postgresql-postgresql-select-derived-table-filter` | SELECT t.id, t.phone FROM (SELECT id, phone FROM public.users WHERE status = $1) t WHERE t.phone = $2 | 已覆盖 |
| `VPG087` | `vastbase-postgresql-postgresql-select-nested-derived-query-graph` | SELECT s.name AS outer_name FROM (SELECT x.name FROM (SELECT name FROM public.aaa WHERE age <= 18) x WHERE x.name IS NOT NULL) s | 已覆盖 |
| `VPG088` | `vastbase-postgresql-postgresql-select-scalar-subquery` | SELECT u.id, (SELECT COUNT(*) FROM public.orders o WHERE o.user_id = u.id) AS order_count FROM public.users u WHERE u.phone = $1 | 已覆盖 |
| `VPG089` | `vastbase-postgresql-postgresql-select-intersect` | SELECT phone FROM public.users INTERSECT SELECT phone FROM public.contacts | 已覆盖 |
| `VPG090` | `vastbase-postgresql-postgresql-create-view-join-aggregate` | CREATE VIEW public.v_user_order_count AS SELECT u.id, COUNT(o.id) AS order_count FROM public.users u JOIN public.orders o ON u.id = o.user_id GROUP BY u.id | 已覆盖 |
| `VPG091` | `vastbase-postgresql-postgresql-select-order-by-ordinal` | SELECT id, phone FROM public.users ORDER BY 1 | 已覆盖 |
| `VPG092` | `vastbase-postgresql-postgresql-select-quoted-mixed-identifiers` | SELECT "IdCard", "Phone Number" FROM "User Info" WHERE "Phone Number" = $1 | 已覆盖 |
| `VPG093` | `vastbase-postgresql-postgresql-dollar-quoted-string-global-bind-position` | SELECT $tag$fake ? marker$tag$ AS note FROM public.users WHERE id = $1 AND name = $2 | 已覆盖 |
| `VPG094` | `vastbase-postgresql-postgresql-multi-statement-global-bind-position` | UPDATE users SET a = $1 WHERE b = $2; UPDATE users SET c = $3 WHERE d = $4 | 已覆盖 |
| `VPG095` | `vastbase-postgresql-postgresql-select-reference-001` | SELECT id, name, age FROM users; | 已覆盖 |
| `VPG096` | `vastbase-postgresql-postgresql-select-reference-004` | SELECT * FROM employees; | 已覆盖 |
| `VPG097` | `vastbase-postgresql-postgresql-select-reference-005` | SELECT u.id AS user_id, u.name AS user_name FROM users u; | 已覆盖 |
| `VPG098` | `vastbase-postgresql-postgresql-select-reference-007` | SELECT * FROM products WHERE price > 100 AND category = 'Electronics'; | 已覆盖 |
| `VPG099` | `vastbase-postgresql-postgresql-select-reference-009` | SELECT name, price FROM products ORDER BY price DESC, name ASC; | 已覆盖 |
| `VPG100` | `vastbase-postgresql-postgresql-select-reference-011` | SELECT DISTINCT department FROM employees; | 已覆盖 |
| `VPG101` | `vastbase-postgresql-postgresql-select-reference-013` | SELECT a.id, a.name FROM (SELECT id, name FROM users WHERE active = 1) a; | 已覆盖 |
| `VPG102` | `vastbase-postgresql-postgresql-select-reference-015` | SELECT id, name FROM (SELECT name, id, age FROM users); | 已覆盖 |
| `VPG103` | `vastbase-postgresql-postgresql-select-reference-017` | SELECT * FROM orders o WHERE EXISTS (SELECT 1 FROM order_items oi WHERE oi.order_id = o.id); | 已覆盖 |
| `VPG104` | `vastbase-postgresql-postgresql-select-reference-018` | SELECT * FROM products WHERE category_id IN (SELECT id FROM categories WHERE active = 1); | 已覆盖 |
| `VPG105` | `vastbase-postgresql-postgresql-select-reference-019` | SELECT id, name, (SELECT COUNT(*) FROM orders WHERE user_id = u.id) AS order_count FROM users u; | 已覆盖 |
| `VPG106` | `vastbase-postgresql-postgresql-select-reference-020` | SELECT id, name, (SELECT product_id FROM orders WHERE user_id = u.id) AS product_id FROM users u; | 已覆盖 |
| `VPG107` | `vastbase-postgresql-postgresql-select-reference-021` | SELECT b.* FROM (SELECT a.* FROM (SELECT id, name FROM users) a WHERE id > 10) b WHERE name LIKE 'A%'; | 已覆盖 |
| `VPG108` | `vastbase-postgresql-postgresql-select-reference-030` | SELECT * FROM ( SELECT t1.*, (SELECT COUNT(*) FROM table2 t2 WHERE t2.id = t1.id AND t2.col = (SELECT MAX(col) FROM table3 WHERE table3.ref = t1.ref) ) as cnt FROM table1 t1 ) sub; | 已覆盖 |
| `VPG109` | `vastbase-postgresql-postgresql-select-reference-031` | SELECT u.name, o.order_date FROM users u INNER JOIN orders o ON u.id = o.user_id; | 已覆盖 |
| `VPG110` | `vastbase-postgresql-postgresql-select-reference-032` | SELECT d.name, COUNT(e.id) FROM departments d LEFT JOIN employees e ON d.id = e.dept_id GROUP BY d.name; | 已覆盖 |
| `VPG111` | `vastbase-postgresql-postgresql-select-reference-034` | SELECT c.name, o.amount FROM customers c RIGHT JOIN orders o ON c.id = o.customer_id; | 已覆盖 |
| `VPG112` | `vastbase-postgresql-postgresql-select-reference-035` | SELECT a.id, b.value FROM table_a a LEFT JOIN table_b b ON a.key = b.key; | 已覆盖 |
| `VPG113` | `vastbase-postgresql-postgresql-select-reference-036` | SELECT u.name, p.product_name, o.quantity FROM users u JOIN orders o ON u.id = o.user_id JOIN products p ON o.product_id = p.id; | 已覆盖 |
| `VPG114` | `vastbase-postgresql-postgresql-select-reference-037` | SELECT * FROM table1 t1 JOIN table2 t2 ON t1.id = t2.ref_id AND t1.status = 'active'; | 已覆盖 |
| `VPG115` | `vastbase-postgresql-postgresql-select-reference-038` | SELECT u.name, sq.user_id, sq.total FROM users u JOIN (SELECT user_id, SUM(amount) as total FROM orders GROUP BY user_id) sq ON u.id = sq.user_id; | 已覆盖 |
| `VPG116` | `vastbase-postgresql-postgresql-select-reference-039` | SELECT id, name FROM active_users UNION ALL SELECT NULL as id, name FROM inactive_users; | 已覆盖 |
| `VPG117` | `vastbase-postgresql-postgresql-select-reference-040` | SELECT u1.id, u1.name FROM active_users u1 UNION ALL SELECT NULL as id, name FROM inactive_users; | 已覆盖 |
| `VPG118` | `vastbase-postgresql-postgresql-select-reference-041` | SELECT product_id FROM orders_2023 UNION SELECT product_id FROM orders_2024; | 已覆盖 |
| `VPG119` | `vastbase-postgresql-postgresql-select-reference-042` | SELECT id, name, status FROM users WHERE active = 1 UNION SELECT id, name, 'inactive' as status FROM user WHERE active = 0 ORDER BY name; | 已覆盖 |
| `VPG120` | `vastbase-postgresql-postgresql-select-reference-043` | SELECT * FROM table_a UNION ALL SELECT * FROM table_b UNION ALL SELECT * FROM table_c; | 已覆盖 |
| `VPG121` | `vastbase-postgresql-postgresql-select-reference-046` | SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zhengmingwjid AS zmwj_key, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm; | 已覆盖 |
| `VPG122` | `vastbase-postgresql-postgresql-select-reference-047` | SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zxsq_zmwj_t_rid AS zmwj_key, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm UNION SELECT NULL AS zxsq_wj_xxgx_t_rid, z.zxsq_zmwj_t_rid AS zmwj_key, z.zhengmingwjdm, z.wenjiansxbm, z.wenjianysmc, z.wenjiansm, z.wenjianscfs AS wenjianlybj, NULL AS chuangjiansj, z.wenjianfwqlj, z.futubj, NULL AS yewulxbm, NULL AS wenjianywbm, NULL AS wenjiansxmc FROM zxsq_zmwj_t z WHERE z.del_flag = '0' AND z.zxsq_zmwj_t_rid = ''; | 已覆盖 |
| `VPG123` | `vastbase-postgresql-postgresql-select-nested-join-derived-query-graph` | SELECT s.name AS outer_name FROM public.left_t l JOIN (public.mid_t m JOIN (SELECT id, name FROM public.aaa WHERE age <= 18) s ON s.id = m.id) ON m.id = l.id WHERE s.name IS NOT NULL | 已覆盖 |
| `VPG124` | `vastbase-postgresql-postgresql-select-union-derived-scope` | SELECT a FROM (SELECT * FROM abc) UNION ALL SELECT b FROM (SELECT * FROM ttt) | 已覆盖 |
| `VPG125` | `vastbase-postgresql-postgresql-select-unqualified-multi-table-scope` | SELECT id FROM users u JOIN orders o ON u.id = o.user_id WHERE id = 1 | 已覆盖 |
| `VPG126` | `vastbase-postgresql-postgresql-field-match-kind-direct-and-expression` | SELECT id FROM public.users WHERE secret = $1 AND UPPER(secret) = $2 | 已覆盖 |
| `VPG127` | `vastbase-postgresql-postgresql-expression-field-case-expression-value` | SELECT id FROM public.users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = $1 | 已覆盖 |
| `VPG128` | `vastbase-postgresql-postgresql-expression-field-multi-field-expression-value` | SELECT id FROM public.users WHERE COALESCE(secret, id) = $1 AND secret \|\| id = $2 | 已覆盖 |
| `VPG129` | `vastbase-postgresql-postgresql-expression-field-value-side-expression` | SELECT id FROM public.users WHERE secret = UPPER($1) AND secret = $2 \|\| 'x' AND secret = CAST($3 AS text) | 已覆盖 |
| `VPG130` | `vastbase-postgresql-postgresql-expression-field-dml-expression-values` | INSERT INTO public.users (id, secret) VALUES (1, UPPER($1)); UPDATE public.users SET secret = $2 \|\| 'x' WHERE id = 1 | 已覆盖 |
| `VPG131` | `vastbase-postgresql-postgresql-update-bind-rhs-crypto-source` | UPDATE public.dbp_crypto_test SET secret = $1 WHERE id = $2 | 已覆盖 |
| `VPG132` | `vastbase-postgresql-postgresql-update-multiple-bind-rhs-crypto-source` | UPDATE public.dbp_crypto_test SET phone = $1, secret = $2 WHERE id = $3 | 已覆盖 |
| `VPG133` | `vastbase-postgresql-postgresql-like-escape-literal` | SELECT id FROM public.users WHERE name LIKE 'A!_%' ESCAPE '!' | 已覆盖 |
| `VPG134` | `vastbase-postgresql-postgresql-not-like-escape-bind` | SELECT id FROM public.users WHERE name NOT LIKE $1 ESCAPE $2 | 已覆盖 |
| `VPG135` | `vastbase-postgresql-postgresql-ilike-escape-bind` | SELECT id FROM public.users WHERE name ILIKE $1 ESCAPE $2 | 已覆盖 |
| `VPG136` | `vastbase-postgresql-postgresql-like-without-explicit-escape` | SELECT id FROM public.users WHERE name LIKE $1 | 已覆盖 |
| `VPG138` | `vastbase-postgresql-update-from-source-field-graph` | UPDATE public.t AS t SET name = s.name FROM public.src AS s WHERE t.id = s.id AND s.active = $1 | 已覆盖 |
| `VPG139` | `vastbase-postgresql-insert-select-source-block-graph` | INSERT INTO public.t (id, email) SELECT s.id, s.email FROM public.src s WHERE s.active = $1 | 已覆盖 |
| `VPG140` | `vastbase-postgresql-merge-source-target-graph` | MERGE INTO public.t USING (SELECT $1 AS id, $2 AS email) s ON t.id=s.id WHEN MATCHED THEN UPDATE SET email=s.email WHEN NOT MATCHED THEN INSERT (id,email) VALUES(s.id,s.email) | 已覆盖 |
| `VPG141` | `vastbase-postgresql-regexp-like-function-predicate` | SELECT * FROM public.users WHERE regexp_like(name, $1) | 已覆盖 |
| `VPG142` | `vastbase-postgresql-select-alias-order-by-lineage` | SELECT u.email AS e FROM public.users u ORDER BY u.email | 已覆盖 |
| `VPG143` | `vastbase-postgresql-select-or-predicate-order-by-lineage` | SELECT u.id, u.email, u.bank_card FROM public.users u WHERE u.email = $1 OR u.bank_card = $2 ORDER BY u.id | 已覆盖 |
| `VPG144` | `vastbase-postgresql-national-string-literal` | SELECT 'prefix' AS prefix_value, N'Alice''s order' AS label FROM users WHERE name = n'Bob' | 已覆盖 |
| `VPG145` | `vastbase-postgresql-national-string-duplicate-literal` | SELECT 'same' AS plain_value, N'same' AS national_value FROM users | 已覆盖 |
| `VPG146` | `vastbase-postgresql-data-modifying-cte-delete-multi-reference` | DELETE CTE 的 `RETURNING` 结果被外层 JOIN 重复引用 | 单个 DELETE DML 根、唯一结果块及两处 CTE relation 共享 `source_block`；2 个独立 patch 覆盖结果项替换与插入 |
| `VPG147` | `vastbase-postgresql-data-modifying-cte-update-delete-root` | UPDATE CTE + 顶层 DELETE | 顶层 DELETE 为 D0、UPDATE CTE 为 D1 子节点；UPDATE 结果块供 DELETE `USING` relation 引用，2 个独立 patch 精确验证 D1 结果列表 |
| `VPG148` | `vastbase-postgresql-data-modifying-cte-two-deletes-side-effect` | 无 `RETURNING` 的 DELETE CTE + 有 `RETURNING` 的同级 DELETE CTE | 两个 SELECT-root DML 按声明顺序保持独立根；D0 无结果但保留副作用语义，D1 单独提供结果块；2 个独立 patch 验证 D1 结果列表 |
| `VPG149` | `vastbase-postgresql-data-modifying-cte-sibling-lineage` | INSERT CTE 的 `RETURNING` 驱动同级 UPDATE CTE | D0 INSERT 与 D1 UPDATE 保持独立根和独立结果块，UPDATE 赋值通过 `source_field`/`source_target` 指向 INSERT 的 `payload`；3 个独立 patch 覆盖两个 DML ordinal 及结果项插入 |
| `VPG150` | `vastbase-postgresql-data-modifying-cte-merge-returning` | 带 UPDATE、INSERT 分支及 `RETURNING` 的 MERGE CTE | MERGE D0 的 target/source relation、ON 谓词、分支赋值与 INSERT 行、结果块、`RETURNING t.*` 的 `target_after` 来源及外层 CTE `source_block`；2 个独立 patch 覆盖结果项替换与插入 |
| `VPG151` | `vastbase-postgresql-on-conflict-assignment-list-contract` | 根 `INSERT ... ON CONFLICT DO UPDATE SET` 双赋值 | `assignment[A]` 按序定位冲突更新项；插入、整项替换和删除 3 个 patch 精确反解析 |
| `VPG152` | `vastbase-postgresql-data-modifying-cte-update-assignment-list-contract` | data-modifying CTE 中的嵌套 `UPDATE` 双赋值 | `assignment[D][A]` 以 0 基 DML 序号定位嵌套赋值；3 个 assignment patch 保持 `RETURNING` 和外层查询 |

## INSERT VALUES 回归：bind 与表达式混合

`VPG-BM001` 至 `VPG-BM010` 是 Vastbase Parse/Bind/Execute（PBE）、PREPARE 或驱动模板语境下的 INSERT 输入。这里的 `$n` 表示在 Bind 阶段提供的位置参数；这些位置参数必须通过 PBE、PREPARE 或对应的驱动 bind 流程提供。

每条用例逐个校验 VALUES 单元格的 `row`、`column`、`kind` 和 `selector`。直接 bind 单元格还校验 `bind_key`、`bind_kind`、`bind_sql`、`bind_position`；表达式内部的 bind 按当前公开契约不直接挂到单元格上，而由其后的直接 bind 全局 `bind_position` 验证已计入扫描顺序。时间函数名不得出现在 `query_graph.fields[].column` 中。

| 用例 ID | VALUES 形态 | 验证重点 |
| --- | --- | --- |
| `VPG-BM001` | 三个直接 bind + 末尾 `CURRENT_TIMESTAMP` | 连续直接 bind 与尾部时间表达式 |
| `VPG-BM002` | `now()` 在首位 + 三个直接 bind | 表达式位于首列 |
| `VPG-BM003` | `$1`、`CAST($2 AS text)`、`$3`、`CURRENT_TIMESTAMP` | bind 与表达式交错，嵌套 bind 参与全局计数 |
| `VPG-BM004` | 直接 bind、`NULL`、`now()`、直接 bind | literal 与时间表达式混合 |
| `VPG-BM005` | 直接 bind、独立 `DEFAULT`、`CURRENT_TIMESTAMP`、直接 bind | `DEFAULT` 仅作为独立 VALUES 单元格 |
| `VPG-BM006` | 直接 bind、字符串 literal、`now()`、直接 bind | literal、表达式与 bind 混合 |
| `VPG-BM007` | `$1`、`COALESCE($2, 'fallback')`、`CURRENT_TIMESTAMP`、`$3` | COALESCE 内嵌 bind 与后续全局位置 |
| `VPG-BM008` | `$1`、包含 `$2` 的 `CASE` 表达式、`now()`、`$3` | CASE 表达式内 bind 与后续全局位置 |
| `VPG-BM009` | 三行 VALUES，bind 与表达式位置逐行变化 | 跨行 cell 坐标及连续全局 bind 位置 |
| `VPG-BM010` | schema-qualified quoted identifiers、非常规空白、三个直接 bind + 时间表达式 | 引号标识符、原始空白及末尾表达式 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。解析失败路径由独立单元测试维护，不在该 fixture 中登记。
