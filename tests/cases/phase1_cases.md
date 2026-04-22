# Phase 1 SQL Cases

This file records the first regression matrix that the library must hold while
the extractor and rewrite layers evolve.

The executable source of truth is now `tests/cases/sql_batch_input.json`, where
each case contains:

- the SQL text
- the case name
- the expected regression assertions

Current phase 1 coverage:

| Case ID | Category | SQL | Current expectation |
| --- | --- | --- | --- |
| P001 | select-basic | `SELECT 1` | parse, parse-tree JSON, summary JSON, deparse |
| P002 | select-filter | `SELECT id, name FROM public.users WHERE id = 42` | selected columns, filter columns, table extraction |
| P003 | select-join | `SELECT u.id, u.name, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid'` | selected columns, join columns, where columns |
| P004 | select-cte | `WITH active_users AS (...) SELECT ... FROM active_users` | CTE name, outer selected columns, upstream filter columns |
| P005 | insert-single-row | `INSERT INTO public.users (id, name) VALUES (1, 'alice')` | insert columns, deparse |
| P006 | insert-multi-row | `INSERT INTO public.users (id, name) VALUES (1, 'alice'), (2, 'bob')` | multi-row insert parse and deparse |
| P007 | insert-from-select | `INSERT INTO public.user_archive (id, name) SELECT ... FROM public.users WHERE ...` | insert columns plus inner select/where extraction |
| P008 | update-basic | `UPDATE public.users SET name = 'alice', status = 'active' WHERE id = 1` | update columns, where columns |
| P009 | delete-conditional | `DELETE FROM public.users WHERE id = 1 AND status = 'active'` | multi-condition delete extraction |
| P010 | delete-in-list | `DELETE FROM public.users WHERE id IN (1, 2, 3) AND status = 'active'` | delete predicate extraction with `IN` |
| P011 | drop-table | `DROP TABLE public.users` | DDL classification, deparse |
| P012 | drop-view | `DROP VIEW public.v_users` | DDL classification for views |
| P013 | view-create | `CREATE VIEW public.v_users AS SELECT id, name FROM public.users` | view definition parse, inner select extraction |
| P014 | transaction-basic | `BEGIN; COMMIT;` | multi-statement transaction counting |
| P015 | transaction-rollback | `BEGIN; INSERT ...; ROLLBACK;` | transaction + DML combined parse |
| P016 | multi-statement | `SELECT 1; INSERT INTO public.audit_log ...` | mixed statement counting |
| P017 | quoted-identifiers | `SELECT "User"."Name" FROM "User"` | quoted identifier preservation |
| P018 | literal-semicolon | `SELECT ';' AS token` | literal parsing stability |
| P019 | parse-error | `SELECT FROM` | structured parse failure |

The current executable regression path includes:

- the API smoke test in `tests/unit/test_api_smoke.c`
- the file-driven API matrix test in `tests/unit/test_api_case_matrix.c`
- the batch CLI regression fixture in `tests/cases/sql_batch_input.json`

Additional cases should continue to be appended in the JSON fixture first so
the regression set remains executable instead of drifting into documentation
only.
