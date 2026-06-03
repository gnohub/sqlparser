# Vastbase PostgreSQL 兼容模式用例矩阵

可执行夹具：`tests/cases/vastbase_postgresql_dialect_input.json`。单元测试会逐条验证解析、View JSON、反解析输出和明确不支持语法返回码。

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
| `VPG137` | `vastbase-postgresql-parse-error` | SELECT FROM | 明确不支持 |
