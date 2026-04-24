# SQL 用例矩阵

本文件记录 `tests/cases/sql_batch_input.json` 覆盖的回归用例。可执行数据以 JSON 夹具为准，本文档用于说明用例分类和验证重点。

## 覆盖范围

| 用例 ID | 分类 | SQL | 验证重点 |
| --- | --- | --- | --- |
| P001 | select-basic | `SELECT 1` | 解析、解析树 JSON、摘要 JSON、反解析 |
| P002 | select-filter | `SELECT id, name FROM public.users WHERE id = 42` | 查询列、过滤列、表名提取 |
| P003 | select-join | `SELECT u.id, u.name, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid'` | 查询列、关联列、条件列 |
| P004 | select-cte | `WITH active_users AS (...) SELECT ... FROM active_users` | CTE 名称、外层查询列、上游过滤列 |
| P005 | insert-single-row | `INSERT INTO public.users (id, name) VALUES (1, 'alice')` | 插入列、反解析 |
| P006 | insert-multi-row | `INSERT INTO public.users (id, name) VALUES (1, 'alice'), (2, 'bob')` | 多行 INSERT 解析与反解析 |
| P007 | insert-from-select | `INSERT INTO public.user_archive (id, name) SELECT ... FROM public.users WHERE ...` | 插入列与内层 SELECT / WHERE 提取 |
| P008 | update-basic | `UPDATE public.users SET name = 'alice', status = 'active' WHERE id = 1` | 更新列、条件列 |
| P009 | delete-conditional | `DELETE FROM public.users WHERE id = 1 AND status = 'active'` | 多条件 delete 提取 |
| P010 | delete-in-list | `DELETE FROM public.users WHERE id IN (1, 2, 3) AND status = 'active'` | `IN` 条件下的 delete 谓词提取 |
| P011 | drop-table | `DROP TABLE public.users` | DDL 分类、deparse |
| P012 | drop-view | `DROP VIEW public.v_users` | view DDL 分类 |
| P013 | view-create | `CREATE VIEW public.v_users AS SELECT id, name FROM public.users` | view 定义 parse 与内层 select 提取 |
| P014 | transaction-basic | `BEGIN; COMMIT;` | 多语句事务计数 |
| P015 | transaction-rollback | `BEGIN; INSERT ...; ROLLBACK;` | 事务与 DML 组合解析 |
| P016 | multi-statement | `SELECT 1; INSERT INTO public.audit_log ...` | 混合语句计数 |
| P017 | quoted-identifiers | `SELECT "User"."Name" FROM "User"` | 引号标识符保留 |
| P018 | literal-semicolon | `SELECT ';' AS token` | 字符串字面量中的分号处理 |
| P019 | parse-error | `SELECT FROM` | 结构化解析错误 |

## 可执行入口

- API 烟测：`tests/unit/test_api_smoke.c`
- API 矩阵测试：`tests/unit/test_api_case_matrix.c`
- CLI 批量夹具：`tests/cases/sql_batch_input.json`

新增回归用例应写入 `tests/cases/sql_batch_input.json`，保证文档说明与可执行测试保持一致。
