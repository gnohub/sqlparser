# Vastbase Oracle Compatibility Case Matrix

The executable fixture is `tests/cases/vastbase_oracle_dialect_input.json`. For every final case, the runner requires unchanged SQL to deparse byte for byte, compares the actual View with the expected JSON structure, and executes each patch independently. Patched SQL must match `patch.deparse` byte for byte, remain identical after a fresh parse and second deparse, and produce the same View from the patched and freshly parsed handles.

## Canonical Transaction Characteristic Values

These four final cases cover common transaction isolation levels and access modes in Vastbase Oracle compatibility mode. Generation-0 deparse must preserve every input byte, while View must emit trivia-free canonical keyword values in input order. Session semantic values expose no selector, so these cases intentionally have no patch entries.

| ID | Case | SQL | Verification focus |
| --- | --- | --- | --- |
| `VO-TX001` | `vastbase-oracle-session-transaction-commented-read-uncommitted` | ALTER/*command*/SESSION SET TRANSACTION ISOLATION/*name*/LEVEL READ/*value*/UNCOMMITTED; | canonical `READ UNCOMMITTED` and a single characteristic |
| `VO-TX002` | `vastbase-oracle-session-characteristics-commented-repeatable-read-write` | ALTER SESSION SET SESSION/*scope*/CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE/*value*/READ, READ/*mode*/WRITE; | `REPEATABLE READ`, `READ WRITE`, and the session-characteristics entry |
| `VO-TX003` | `vastbase-oracle-session-transaction-commented-serializable-read-only` | ALTER SESSION SET TRANSACTION ISOLATION LEVEL SERIALIZABLE/*tail*/, READ/*mode*/ONLY; | `SERIALIZABLE`, `READ ONLY`, and trivia before the comma |
| `VO-TX004` | `vastbase-oracle-session-transaction-commented-option-order` | ALTER SESSION SET TRANSACTION read/*mode*/write, ISOLATION/*name*/LEVEL read/*value*/committed; | input option order, lowercase source preservation, and canonical `READ COMMITTED` |

## Matrix Counts and Session Regression

The fixture contains 213 cases with `status = "final"`. The expected View contains a non-empty session projection in 41 cases.

View validation compares JSON structures; object-key order and formatting whitespace do not participate. Session action, item scope, target kind, name, value kind, canonical text, and value order are all part of that comparison.

## ROWNUM Predicate Semantics Regression

These five final cases verify Query Graph semantics for `ROWNUM` comparisons in Vastbase Oracle compatibility mode. As a pseudo expression, `ROWNUM` does not enter `fields` or relation lineage. A literal or bind on the other side enters `values` and is referenced by an expression predicate without a field. Boolean composition, derived-query scope, operand direction, and DELETE DML ownership follow the source SQL exactly.

| ID | Case | SQL | Verification focus |
| --- | --- | --- | --- |
| `VO-RN001` | `vastbase-oracle-rownum-conjunction-named-bind` | SELECT id FROM users WHERE active = 1 AND ROWNUM <= :limit | the AND root references the ordinary field comparison and the `ROWNUM` expression predicate in source order; `:limit` is position 1 |
| `VO-RN002` | `vastbase-oracle-rownum-derived-order-by-filter` | SELECT * FROM (SELECT id, created_at FROM orders ORDER BY created_at DESC) WHERE ROWNUM < 11 | the `ROWNUM` predicate belongs to the outer block while derived-source and inner ORDER BY field ownership remain separate |
| `VO-RN003` | `vastbase-oracle-rownum-reversed-literal-comparison` | SELECT id FROM users WHERE 1 = ROWNUM | reversed operands still expose the literal selector and an expression predicate without a field |
| `VO-RN004` | `vastbase-oracle-rownum-greater-than-literal` | SELECT id FROM users WHERE ROWNUM > 1 | the `>` operator and literal value are preserved exactly, without a `ROWNUM` field |
| `VO-RN005` | `vastbase-oracle-delete-rownum-conjunction-named-bind` | DELETE FROM audit_log WHERE expired = 1 AND ROWNUM <= :batch_size | the DML object references the DELETE target relation; the AND predicate tree and `:batch_size` at position 1 belong to the root block |

## INSERT VALUES Regression: Mixed Binds and Expressions

`VO-BM001` through `VO-BM010` cover single-row `INSERT ... VALUES` statements
using Vastbase PBE `$n` positional parameters in Oracle compatibility mode.
These SQL strings are statement bodies for the prepare/bind flow, not direct
execution statements with unresolved parameters.

All ten cases use single-row VALUES statements, and `DEFAULT` is always
standalone. Every case asserts each cell's `row`, `column`, `kind`, and
`selector`, plus every direct
bind's `bind_key`, `bind_kind`, `bind_sql`, global `bind_position`, and
`selector`. A later direct-bind position verifies that nested binds were
counted in the global sequence. `SYSDATE` and `CURRENT_TIMESTAMP` must be expressions and must not
appear in `query_graph.fields[].column`.

| ID | SQL shape | Boundary |
| --- | --- | --- |
| `VO-BM001` | three standalone `$n` binds + trailing `SYSDATE` | preserves the original input SQL byte for byte, including irregular spacing and semicolon |
| `VO-BM002` | leading `CURRENT_TIMESTAMP` + three standalone `$n` binds | a leading expression must not shift bind positions |
| `VO-BM003` | `$1`, `$2`, `$3` + trailing `SYSDATE` | consecutive positional parameters and a trailing time expression |
| `VO-BM004` | leading `SYSDATE` + three `$n` binds | preserves bind order when an expression occupies the first cell |
| `VO-BM005` | `$n` binds interleaved with `SYSDATE` / `CURRENT_TIMESTAMP` | expression cells must not shift later binds |
| `VO-BM006` | bind + `NULL` + `SYSDATE` + bind | `NULL` is literal and `SYSDATE` is expression |
| `VO-BM007` | bind + standalone `DEFAULT` + `CURRENT_TIMESTAMP` + bind | `DEFAULT` is not nested in another expression |
| `VO-BM008` | direct bind + `COALESCE($2, 0)` + `SYSDATE` + direct bind | trailing direct bind at position 3 verifies that the nested bind was counted |
| `VO-BM009` | direct bind + a `CASE` expression containing `$2` + `CAST($3 AS NUMBER)` + `SYSDATE` + direct bind | trailing direct bind at position 4 verifies that both nested binds were counted |
| `VO-BM010` | schema-qualified quoted identifiers + three `$n` binds + `CURRENT_TIMESTAMP` | preserves mixed-case identifiers, irregular whitespace, and semicolon |

## Supported Cases

| ID | Case | SQL | Status |
| --- | --- | --- | --- |
| `VO001` | `vastbase-oracle-select-bind-nvl` | SELECT NVL(u.name, 'N/A') AS label FROM users u WHERE u.id = :id | covered |
| `VO002` | `vastbase-oracle-q-quoted-string` | SELECT q'[Bob's order]' AS label FROM dual | covered |
| `VO003` | `vastbase-oracle-national-q-quoted-string` | SELECT nq'{Alice's order}' AS label FROM dual | covered |
| `VO003A` | `vastbase-oracle-national-q-quoted-duplicate-literal` | SELECT 'same' AS ascii_value, nq'{same}' AS national_value FROM dual | covered |
| `VO003B` | `vastbase-oracle-national-string-literal` | SELECT N'Alice''s order' AS label FROM dual | covered |
| `VO003C` | `vastbase-oracle-national-string-duplicate-literal` | SELECT 'same' AS ascii_value, N'same' AS national_value FROM dual | covered |
| `VO004` | `vastbase-oracle-minus-set-operator` | SELECT id FROM active_users MINUS SELECT id FROM archived_users | covered |
| `VO005` | `vastbase-oracle-offset-fetch` | SELECT id FROM users ORDER BY id OFFSET 5 ROWS FETCH NEXT 10 ROWS ONLY | covered |
| `VO006` | `vastbase-oracle-rownum-filter` | SELECT id FROM users WHERE ROWNUM <= 10 | covered |
| `VO007` | `vastbase-oracle-join-bind` | SELECT u.id, u.name, o.order_no FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = :status | covered |
| `VO008` | `vastbase-oracle-insert-values-bind` | INSERT INTO users (id, name) VALUES (:id, 'bob') | covered |
| `VO009` | `vastbase-oracle-insert-values-multi-row` | INSERT INTO users (id, name) VALUES (1, 'bob'), (2, 'alice') | covered |
| `VO010` | `vastbase-oracle-insert-select` | INSERT INTO archive_users (id, name) SELECT id, name FROM users WHERE status = :status | covered |
| `VO134` | `vastbase-oracle-insert-select-union-literals` | INSERT INTO users (id, name) SELECT 1, 'a' FROM dual UNION ALL SELECT 2, 'b' FROM dual | covered |
| `VO135` | `vastbase-oracle-insert-select-union-positional-binds` | INSERT INTO users (id, name) SELECT :1, :2 FROM dual UNION ALL SELECT :3, :4 FROM dual | covered |
| `VO136` | `vastbase-oracle-insert-select-union-named-binds` | INSERT INTO users (id, name) SELECT :id1, :name1 FROM dual UNION ALL SELECT :id2, :name2 FROM dual | covered |
| `VO011` | `vastbase-oracle-update-bind` | UPDATE users SET name = :name, status = 'active' WHERE id = :id | covered |
| `VO012` | `vastbase-oracle-delete-conditional` | DELETE FROM users WHERE id = :id AND status = 'inactive' | covered |
| `VO013` | `vastbase-oracle-repeated-bind` | SELECT id FROM users WHERE id = :id OR manager_id = :id | covered |
| `VO015` | `vastbase-oracle-date-literal` | SELECT DATE '2024-01-01' AS created_on FROM dual | covered |
| `VO016` | `vastbase-oracle-case-expression` | SELECT CASE WHEN status = 'A' THEN 'active' ELSE 'inactive' END AS status_name FROM users | covered |
| `VO017` | `vastbase-oracle-exists-subquery` | SELECT id FROM users u WHERE EXISTS (SELECT 1 FROM orders o WHERE o.user_id = u.id) | covered |
| `VO018` | `vastbase-oracle-group-having` | SELECT status, COUNT(*) AS cnt FROM users GROUP BY status HAVING COUNT(*) > 1 | covered |
| `VO019` | `vastbase-oracle-union-all` | SELECT id FROM active_users UNION ALL SELECT id FROM archived_users | covered |
| `VO020` | `vastbase-oracle-intersect` | SELECT id FROM active_users INTERSECT SELECT id FROM archived_users | covered |
| `VO021` | `vastbase-oracle-merge-basic` | MERGE INTO users u USING staging_users s ON (u.id = s.id) WHEN MATCHED THEN UPDATE SET name = s.name WHEN NOT MATCHED THEN INSERT (id, name) VALUES (s.id, s.name) | covered |
| `VO022` | `vastbase-oracle-create-table` | CREATE TABLE users (id NUMBER(10), name VARCHAR2(64), created_at DATE) | covered |
| `VO023` | `vastbase-oracle-create-sequence` | CREATE SEQUENCE user_seq START WITH 1 INCREMENT BY 1 | covered |
| `VO024` | `vastbase-oracle-create-view` | CREATE OR REPLACE VIEW v_users AS SELECT id, name FROM users | covered |
| `VO025` | `vastbase-oracle-drop-table` | DROP TABLE users | covered |
| `VO026` | `vastbase-oracle-truncate-table` | TRUNCATE TABLE users | covered |
| `VO027` | `vastbase-oracle-transaction-control` | SAVEPOINT s1; ROLLBACK TO SAVEPOINT s1; COMMIT | covered |
| `VO028` | `vastbase-oracle-grant-revoke` | GRANT SELECT ON users TO app_user; REVOKE SELECT ON users FROM app_user | covered |
| `VO029` | `vastbase-oracle-comment-on-table` | COMMENT ON TABLE users IS 'user table' | covered |
| `VO030` | `vastbase-oracle-for-update-nowait` | SELECT id FROM users WHERE id = :id FOR UPDATE NOWAIT | covered |
| `VO031` | `vastbase-oracle-decode-sysdate` | SELECT DECODE(status, 'A', 'active', 'inactive') AS status_name, SYSDATE AS now_value FROM users | covered |
| `VO032` | `vastbase-oracle-analytic-row-number` | SELECT id, ROW_NUMBER() OVER (PARTITION BY status ORDER BY created_at) AS rn FROM users | covered |
| `VO033` | `vastbase-oracle-timestamp-literal` | SELECT TIMESTAMP '2024-01-01 12:30:00' AS ts FROM dual | covered |
| `VO034` | `vastbase-oracle-quoted-identifiers` | SELECT "Name" FROM "Users" WHERE "Id" = :id | covered |
| `VO035` | `vastbase-oracle-alter-table-add-column` | ALTER TABLE users ADD age NUMBER(3) | covered |
| `VO036` | `vastbase-oracle-create-index` | CREATE INDEX idx_users_name ON users (name) | covered |
| `VO037` | `vastbase-oracle-drop-index` | DROP INDEX idx_users_name | covered |
| `VO038` | `vastbase-oracle-in-list-binds` | SELECT id FROM users WHERE status IN (:status1, :status2) | covered |
| `VO039` | `vastbase-oracle-delete-date-literal` | DELETE FROM users WHERE created_at < DATE '2020-01-01' | covered |
| `VO040` | `vastbase-oracle-create-materialized-view-compatible-form` | CREATE MATERIALIZED VIEW mv_users AS SELECT id, name FROM users | covered |
| `VO041` | `vastbase-oracle-unsupported-keywords-in-string` | SELECT 'RETURNING @ (+)' AS label FROM dual | covered |
| `VO042` | `vastbase-oracle-unsupported-keywords-in-comment` | SELECT id FROM users /* CONNECT BY PRIOR id = manager_id */ WHERE id = :id | covered |
| `VO042Q` | `vastbase-oracle-unsupported-keywords-in-quoted-identifiers` | SELECT "RETURNING", "email@domain" FROM users | covered |
| `VO043` | `vastbase-oracle-alter-session-current-schema` | ALTER SESSION SET CURRENT_SCHEMA=KDES | covered |
| `VO043Q` | `vastbase-oracle-alter-session-current-schema-quoted-identifier` | ALTER SESSION SET CURRENT_SCHEMA="KdesMixed" | covered |
| `VO044` | `vastbase-oracle-alter-session-container` | ALTER SESSION SET CONTAINER=PDB1 | covered |
| `VO045` | `vastbase-oracle-alter-session-container-root` | ALTER SESSION SET CONTAINER=CDB$ROOT | covered |
| `VO046` | `vastbase-oracle-alter-session-container-service` | ALTER SESSION SET CONTAINER=PDB1 SERVICE=APP_SVC | covered |
| `VO047` | `vastbase-oracle-alter-session-current-schema-in-multi-statement` | SELECT * FROM users; ALTER SESSION SET CURRENT_SCHEMA=KDES | covered |
| `VO048` | `vastbase-oracle-insert-question-params` | INSERT INTO users (username, email, age) VALUES (?, ?, ?) | covered |
| `VO049` | `vastbase-oracle-update-question-params` | UPDATE users SET email = ? WHERE username = ? | covered |
| `VO050` | `vastbase-oracle-execute-immediate` | EXECUTE IMMEDIATE 'SELECT * FROM users WHERE id = :id' USING :id | covered |
| `VO051` | `vastbase-oracle-select-multiple-named-binds` | SELECT id, name FROM users WHERE id = :id AND status = :status | covered |
| `VO052` | `vastbase-oracle-select-in-named-binds` | SELECT id FROM users WHERE status IN (:status1, :status2, :status3) | covered |
| `VO053` | `vastbase-oracle-select-fetch-bind` | SELECT id FROM users WHERE name LIKE :pattern ORDER BY id FETCH FIRST :limit ROWS ONLY | covered |
| `VO054` | `vastbase-oracle-insert-multiple-named-binds` | INSERT INTO users (id, name, status) VALUES (:id, :name, :status) | covered |
| `VO055` | `vastbase-oracle-update-multiple-named-binds` | UPDATE users SET name = :name, status = :status WHERE id = :id | covered |
| `VO056` | `vastbase-oracle-delete-multiple-named-binds` | DELETE FROM users WHERE id = :id AND status = :status | covered |
| `VO057` | `vastbase-oracle-select-positional-bind-pair` | SELECT id FROM users WHERE id = :1 AND status = :2 | covered |
| `VO058` | `vastbase-oracle-insert-question-params-expanded` | INSERT INTO users (id, name, status) VALUES (?, ?, ?) | covered |
| `VO059` | `vastbase-oracle-delete-question-params` | DELETE FROM users WHERE id = ? AND status = ? | covered |
| `VO060` | `vastbase-oracle-execute-immediate-update-using` | EXECUTE IMMEDIATE 'UPDATE users SET name = :name WHERE id = :id' USING :name, :id | covered |
| `VO061` | `vastbase-oracle-rownum-pagination-nested-bind` | SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD,WORKER, WEBSITE,MSDEPLOYPORT, "UID", PWD, KEY_ENCRYPTION, MODIFYTIME FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) WHERE RN > :startRow | covered |
| `VO062` | `vastbase-oracle-view-nvl-upper-functions` | SELECT NVL(TO_CHAR(commission_pct), 'Not Applicable') commission, UPPER(last_name) FROM employees WHERE employee_id = :employee_id | covered |
| `VO063` | `vastbase-oracle-view-case-expression` | SELECT CASE WHEN state = 1 THEN name ELSE fallback_name END FROM users | covered |
| `VO064` | `vastbase-oracle-view-group-having-order` | SELECT department_id, COUNT(employee_id) FROM employees GROUP BY department_id HAVING COUNT(employee_id) > 1 ORDER BY department_id | covered |
| `VO065` | `vastbase-oracle-view-update-named-binds` | UPDATE SERVERS SET IP = :aaa WHERE ID = :id | covered |
| `VO066` | `vastbase-oracle-view-rownum-pagination-attribution` | SELECT IP, AREACODE, "UID" FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) WHERE RN > :startRow | covered |
| `VO067` | `vastbase-oracle-view-mixed-positional-binds` | SELECT abc FROM table1 WHERE abc LIKE :1 AND def LIKE ? | covered |
| `VO068` | `vastbase-oracle-select-between-named-binds` | SELECT id FROM users WHERE age BETWEEN :min_age AND :max_age | covered |
| `VO069` | `vastbase-oracle-select-not-in-named-binds` | SELECT id FROM users WHERE status NOT IN (:status1, :status2) | covered |
| `VO070` | `vastbase-oracle-select-not-between-named-binds` | SELECT id FROM users WHERE age NOT BETWEEN :min_age AND :max_age | covered |
| `VO071` | `vastbase-oracle-select-not-like-named-bind` | SELECT id FROM users WHERE name NOT LIKE :name_pattern | covered |
| `VO072` | `vastbase-oracle-select-distinct-like-bind` | SELECT DISTINCT name FROM table1 WHERE name LIKE :name | covered |
| `VO073` | `vastbase-oracle-select-distinct-nested-functions` | SELECT DISTINCT LOWER(UPPER(name)) FROM table1 | covered |
| `VO074` | `vastbase-oracle-delete-in-named-binds` | DELETE FROM users WHERE email IN (:email1, :email2) | covered |
| `VO075` | `vastbase-oracle-update-exists-subquery` | UPDATE users u SET status = :status WHERE EXISTS (SELECT 1 FROM orders o WHERE o.user_id = u.id AND o.phone = :phone) | covered |
| `VO076` | `vastbase-oracle-insert-without-column-list` | INSERT INTO users VALUES (:id, :name, :age) | covered |
| `VO077` | `vastbase-oracle-create-or-replace-view` | CREATE OR REPLACE VIEW v_user_orders AS SELECT u.id, COUNT(o.id) AS order_count FROM users u JOIN orders o ON u.id = o.user_id GROUP BY u.id | covered |
| `VO078` | `vastbase-oracle-rownum-pagination-realistic` | SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD, WORKER, WEBSITE, MSDEPLOYPORT, "UID", PWD, KEY_ENCRYPTION, MODIFYTIME FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) WHERE RN > :startRow | covered |
| `VO079` | `vastbase-oracle-select-alias-star` | SELECT u.*, o.order_no FROM users u LEFT JOIN orders o ON u.id = o.user_id WHERE o.status = :status | covered |
| `VO080` | `vastbase-oracle-select-order-by-ordinal` | SELECT id, phone FROM users ORDER BY 1 | covered |
| `VO081` | `vastbase-oracle-select-from-dual-bind` | SELECT :value FROM dual | covered |
| `VO089` | `vastbase-oracle-select-reference-024` | select * from (select id as aa, name as bb, department as cc FROM (select e.*,rownum as row_num from employees_uuid e where rownum <= 100) where row_num >= 1 and (id = 1 or id=2)) where aa = 1; | covered |
| `VO090` | `vastbase-oracle-select-reference-026` | select b.* from (select id as aa, department as cc, name as bb FROM (select e.*,rownum as row_num from employees_uuid e where rownum <= 100) a where row_num >= 1 and (id = 1 or id=2)) b where aa = 1; | covered |
| `VO091` | `vastbase-oracle-select-reference-028` | SELECT * FROM (SELECT e.*, ROWNUM rn FROM employees e WHERE ROWNUM <= 100) WHERE rn > 50; | covered |
| `VO092` | `vastbase-oracle-select-reference-033` | select * from (select rownum as num, * from (select * from ( select b.*, rownum autorowno, c.* from PERSON b left join XQGL_XQBC c on b.ID = c.xqgl_id ) a) ) d; | covered |
| `VO093` | `vastbase-oracle-select-reference-044` | select * from ( select /*+first_rows*/ z_results.*,rownum autorowno from ( select t.XQGL_ID xqdh, t.XQGL_BT xqbt, t.xqgl_lx xqlx, (select dict_name from xggl_dict c where trim(t.XQGL_LX) = c.dict_id and c.dict_type='4') xqlxmc, t.XQGL_CJRDM xqcjrdm, t.XQGL_CJRME xqcjr, t.XQGL_LXDH lxdh, t.XQGL_SWJGDM swjgdm, t.XQGL_SWJGMC swjgmc, to_char(t.XQGL_XQCJRQ,'yyyy-MM-dd') xqcjrq, to_char(t.XQGL_XQWCRQ,'yyyy-MM-dd') xqwcrq, t.XQGL_GJGQ gjgq, t.XQGL_CONTENT content, t.XQGL_FLAG flag, t.XQGL_XQQR xqqr, t.XQGL_XQFK xqfk, t.XQGL_REMARK mark, t.XQGL_REMARK1 mgzd, case when t.XQGL_REMARK1 = '1' then '是' else '否' end as mgzdmc, t.XQGL_REMARK2 mark2, t.XQGL_REMARK3 mark3, t.xqgl_zt xqzt, t.XQGL_ZTMC xqztmc, t.xqgl_ywlx ywlx, t.XQGL_YWLXMC ywlxmc, t.xqgl_fzlx fzlx, t.XQGL_FZLXMC fzlxmc, t.XQGL_QRRQ qrrq, t.XQGL_FKRQ fkrq, t.xqgl_fk_xs fkxs, case when t.XQGL_FK_XS ='1' then '书面' when t.XQGL_FK_XS ='2' then '线上' end as fkxsmc, t.XQGL_XQFK_CONTENT fkcontent, t.xqgl_xqff_zygs_id zygs, t.xqgl_xqff_xzgs_id xzgs, t.XQGL_XQFF_ZYGS zygsmc, t.XQGL_XQFF_XZGS xzgsmc, t.xqgl_xbrq xbrq, b.xqgl_bccontent bccontent, b.xqgl_bcfkcontent bcfkcontent from xqgl t left join (select * from xqgl_xqbc where xqgl_type = 'bc') b on t.xqgl_id = b.xqgl_id where t.XQGL_DELETE !='Y' and t.XQGL_CJRDM = '110' and t.XQGL_SWJGDM = '110' order by t.XQGL_XQCJRQ desc ) z_results where rownum<=20 ) where autorowno>= 1; | covered |
| `VO094` | `vastbase-oracle-select-reference-045` | select * from (select /*+first_rows*/ z_results.*,rownum autorowno from ( select t.fxpcbh "XXPC_BH", decode(fa.fa_mc,'','','[' \|\| fa.fa_mc \|\|'] ')\|\|t.fxpcmc "XXPC_MC", t.fxpczt "XXPCZT_DM", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXSCFS' and dm.dm = t.fxscfs) "FXSCFS", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXPCZT' and dm.dm = t.fxpczt) "XXPCZT_MC", to_char(t.cjsj,'yyy-mm-dd') "LRSJ", (select c.swjgmc from wd_swjg c where c.swjg_dm = t.cjsscsdm) "LYCS", (select c.swjgmc from wd_swig c where c.swjg_dm= t.cjswjgdm) "SWJG", nv1(t.fxxxsl,0) "FXXXS", nv1(t.shtgsl,0) "FXTGSL", nv1(t.shbtgsl,0) "FXBTGSL", (select c.czry_mc from dm_czry c where c.swry_dm = t.sprdm) spr, to_char(nv1(t.cjsj,t.tssj),'yyyy-m-dd') tssj, to_char(t.spsj,'yyyy-mm-dd') spsj, t.spsm, decode(t.spyj,'Y','通过','不通过') spyj, n.file_name fileName, n.file_path filePath, n.complete_date completeDate, n.remark from rwtc_fxpc t left join rwtc_fxpc_ydzn n on t.fxpcbh = n.fxpcbh left join fxfx_sm_slb sl on sl.smsl_bh=t.fxpcbh left join fxfx_fxfa fa on sl.fa_bh=fa.fa_bh where 1=1 and t.cjswjgdm = '110' and (t.fxpczt in ('02') or t.fxxxsl = (t.shtgsl+ t.shbtgsl)) order by fxpczt asc, t.spsj desc ) z_results where rownum<=20 ) aaa where autorowno>= 1; | covered |
| `VO095` | `vastbase-oracle-select-reference-048` | select * from (select * from (select CONCAT(filePath, fxpczt) as path, b.* from (select * from (select /*+first_rows*/ z_results.*, rownum autorowno from ( select t.fxpcbh "XXPC_BH", decode(fa.fa_mc,'','','[' \|\| fa.fa_mc \|\|'] ')\|\|t.fxpcmc "XXPC_MC", t.fxpczt "XXPCZT_DM", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXSCFS' and dm.dm = t.fxscfs) "FXSCFS", (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXPCZT' and dm.dm = t.fxpczt) "XXPCZT_MC", to_char(t.cjsj,'yyy-mm-dd') "LRSJ", (select c.swjgmc from wd_swjg c where c.swjg_dm = t.cjsscsdm) "LYCS", (select c.swjgmc from wd_swig c where c.swjg_dm= t.cjswjgdm) "SWJG", nv1(t.fxxxsl,0) "FXXXS", nv1(t.shtgsl,0) "FXTGSL", nv1(t.shbtgsl,0) "FXBTGSL", (select c.czry_mc from dm_czry c where c.swry_dm = t.sprdm) spr, to_char(nv1(t.cjsj,t.tssj),'yyyy-m-dd') tssj, to_char(t.spsj,'yyyy-mm-dd') spsj, t.spsm, decode(t.spyj,'Y','通过','不通过') spyj, n.file_name fileName, n.file_path filePath, n.complete_date completeDate, n.remark from rwtc_fxpc t left join rwtc_fxpc_ydzn n on t.fxpcbh = n.fxpcbh left join fxfx_sm_slb sl on sl.smsl_bh=t.fxpcbh left join fxfx_fxfa fa on sl.fa_bh=fa.fa_bh where 1=1 and t.cjswjgdm = '110' and (t.fxpczt in ('02') or t.fxxxsl = (t.shtgsl+ t.shbtgsl)) order by fxpczt asc, t.spsj desc ) z_results where rownum<=20 ) a1 where autorowno>= 1) b)); | covered |
| `VO096` | `vastbase-oracle-select-reference-049` | select * from (select rownum,* from (select * from (select o.*, rownum as rnum from ( SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zxsq_zmwj_t_rid AS zmwj_key, (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXSCFS' and dm.dm = x.fxscfs) fxscfs, (select dm.mc from rwtc_dm dm where dm.lx_dm = 'RWTCFXPCZT' and dm.dm = x.fxpczt) xxpczt, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm UNION SELECT NULL AS zxsq_wj_xxgx_t_rid, z.zxsq_zmwj_t_rid AS zmwj_key, NULL AS fxscfs, NULL AS xxpczt, z.zhengmingwjdm, z.wenjiansxbm, z.wenjianysmc, z.wenjiansm, z.wenjianscfs AS wenjianlybj, NULL AS chuangjiansj, z.wenjianfwqlj, z.futubj, NULL AS yewulxbm, NULL AS wenjianywbm, NULL AS wenjiansxmc FROM zxsq_zmwj_t z WHERE z.del_flag = '0' and z.test_column = '1' AND z.zxsq_zmwj_t_rid = '1' ) o )) b) d; | covered |
| `VO097` | `vastbase-oracle-select-reference-046` | SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zhengmingwjid AS zmwj_key, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm; | covered |
| `VO098` | `vastbase-oracle-select-reference-047` | SELECT a.*, b.wenjiansxmc FROM ( SELECT x.zxsq_wj_xxgx_t_rid, x.zxsq_zmwj_t_rid AS zmwj_key, x.zhengmingwjdm, x.wenjiansxbm, x.fujiawjmc AS wenjianysmc, x.fujianwjsm AS wenjiansm, x.wenjianlybj, x.create_time AS chuangjiansj, z.wenjianfwqlj, z.futubj, x.yewulxbm, x.wenjianywbm FROM zxsq_wj_xxgx_t x LEFT JOIN zxsq_zmwj_t z ON x.zhengmingwjid = z.zxsq_zmwj_t_rid LEFT JOIN zxsq_dzsqqqjl_cg_t c ON x.dianzisqajbh = c.dianzisqajbh WHERE x.del_flag = '0' AND (z.del_flag = '0' OR z.del_flag IS NULL) AND x.zhengmingwjbm != '123456' AND x.zhubiaom = '789' AND x.yewulxbm = '1011' AND c.create_user_jgdm = '1213' AND x.wenjianywbm = '11' ) a LEFT JOIN zxsq_fjwjywdz_t b ON a.wenjiansxbm = b.wenjiansxbm AND a.yewulxbm = b.yewulxbm UNION SELECT NULL AS zxsq_wj_xxgx_t_rid, z.zxsq_zmwj_t_rid AS zmwj_key, z.zhengmingwjdm, z.wenjiansxbm, z.wenjianysmc, z.wenjiansm, z.wenjianscfs AS wenjianlybj, NULL AS chuangjiansj, z.wenjianfwqlj, z.futubj, NULL AS yewulxbm, NULL AS wenjianywbm, NULL AS wenjiansxmc FROM zxsq_zmwj_t z WHERE z.del_flag = '0' AND z.zxsq_zmwj_t_rid = ''; | covered |
| `VO137` | `vastbase-oracle-insert-all` | INSERT ALL INTO users (id) VALUES (1) INTO users (id) VALUES (2) SELECT 1 FROM dual | covered |
| `VO132` | `vastbase-oracle-insert-all-bind-branches` | INSERT ALL INTO users (id, name) VALUES (:1, :2) INTO users (id, name) VALUES (:3, :name4) SELECT 1 FROM dual | covered |
| `VO133` | `vastbase-oracle-insert-all-multi-target` | INSERT ALL INTO users (id, name) VALUES (1, 'a') INTO phones (id, phone) VALUES (2, '13800138000') SELECT 1 FROM dual | covered |
| `VO082` | `vastbase-oracle-alter-session-nls-date-format` | ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD' | covered |
| `VO083` | `vastbase-oracle-alter-session-nls-language` | ALTER SESSION SET NLS_DATE_LANGUAGE = French | covered |
| `VO084` | `vastbase-oracle-alter-session-numeric-parameter` | ALTER SESSION SET INSTANCE = 2 | covered |
| `VO085` | `vastbase-oracle-alter-session-boolean-parameter` | ALTER SESSION SET ERROR_ON_OVERLAP_TIME = TRUE | covered |
| `VO086` | `vastbase-oracle-alter-session-nls-numeric-characters` | ALTER SESSION SET NLS_NUMERIC_CHARACTERS = '.,' | covered |
| `VO087` | `vastbase-oracle-multi-statement-global-bind-position` | UPDATE users SET a = :same WHERE b = :b; UPDATE users SET c = :same WHERE d = :d | covered |
| `VO088` | `vastbase-oracle-select-derived-query-graph` | SELECT s.name AS outer_name FROM (SELECT id, name FROM KDES.USERS WHERE age <= :age) s WHERE s.name LIKE :name | covered |
| `VO099` | `vastbase-oracle-select-nested-star-query-graph` | SELECT * FROM (SELECT ROWNUM, * FROM (SELECT * FROM (SELECT o.*, ROWNUM AS rnum FROM (SELECT x.id FROM users x UNION SELECT y.id FROM archived_users y) o)) b) d | covered |
| `VO100` | `vastbase-oracle-field-match-kind-direct-and-expression` | SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :plain_secret AND UPPER(SECRET) = :upper_secret | covered |
| `VO101` | `vastbase-oracle-expression-field-case-expression-value` | SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN ID = 1 THEN SECRET ELSE BACKUP_SECRET END = :v | covered |
| `VO102` | `vastbase-oracle-expression-field-multi-field-expression-value` | SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE NVL(SECRET, ID) = :v1 AND SECRET \|\| ID = :v2 | covered |
| `VO103` | `vastbase-oracle-expression-field-value-side-expression` | SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = UPPER(:v1) AND SECRET = :v2 \|\| 'x' AND SECRET = CAST(:v3 AS VARCHAR(32)) | covered |
| `VO104` | `vastbase-oracle-expression-field-dml-expression-values` | INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (1, UPPER(:v1)); UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = :v2 \|\| 'x' WHERE ID = 1 | covered |
| `VO105` | `vastbase-oracle-update-positional-bind-rhs-crypto-source` | UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = :1 WHERE ID = :2 | covered |
| `VO106` | `vastbase-oracle-update-named-bind-rhs-crypto-source` | UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = :secret_value WHERE ID = :id | covered |
| `VO107` | `vastbase-oracle-update-question-bind-rhs-crypto-source` | UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = ? WHERE ID = ? | covered |
| `VO108` | `vastbase-oracle-update-multiple-bind-rhs-crypto-source` | UPDATE KDES.DBP_CRYPTO_TEST SET PHONE = :1, SECRET = :2 WHERE ID = :3 | covered |
| `VOU014` | `vastbase-oracle-create-synonym` | CREATE SYNONYM u FOR users | covered |
| `VO175` | `vastbase-oracle-create-public-synonym` | CREATE OR REPLACE PUBLIC SYNONYM app_users FOR kdes.users | covered |
| `VO176` | `vastbase-oracle-drop-synonym` | DROP SYNONYM app_users FORCE | covered |
| `VOU015` | `vastbase-oracle-database-link` | SELECT * FROM users@remote_db | covered |
| `VOU016` | `vastbase-oracle-explain-plan` | EXPLAIN PLAN FOR SELECT * FROM users | covered |
| `VO177` | `vastbase-oracle-explain-plan-into` | EXPLAIN PLAN SET STATEMENT_ID = 'q1' INTO plan_table FOR SELECT id FROM users WHERE id = :id | covered |
| `VO138` | `vastbase-oracle-insert-first` | INSERT FIRST WHEN 1 = 1 THEN INTO users (id) VALUES (1) SELECT 1 FROM dual | covered |
| `VO139` | `vastbase-oracle-insert-first-direct-source-fields` | INSERT FIRST WHEN amount > 100 THEN INTO big_orders (id, amount) VALUES (order_id, amount) ELSE INTO small_orders (id, amount) VALUES (order_id, amount) SELECT id AS order_id, amount FROM orders | covered |
| `VO140` | `vastbase-oracle-insert-all-conditional` | INSERT ALL WHEN flag = 1 THEN INTO users (id, flag_copy) VALUES (:1, flag) WHEN flag = 2 THEN INTO audit_users (id, flag_copy) VALUES (:2, flag) SELECT flag FROM source_table | covered |
| `VO141` | `vastbase-oracle-insert-all-multiple-into-per-when` | INSERT ALL WHEN flag = 1 THEN INTO users (id) VALUES (:1) INTO audit_users (id) VALUES (:2) ELSE INTO rejected_users (id) VALUES (:3) SELECT flag FROM source_table | covered |
| `VO142` | `vastbase-oracle-insert-select-source-fields` | INSERT INTO users (id, name) SELECT src_id, src_name FROM source_users | covered |
| `VO143` | `vastbase-oracle-insert-select-expression-targets` | INSERT INTO users (id, name) SELECT src_id + 1, UPPER(src_name) FROM source_users | covered |
| `VO144` | `vastbase-oracle-insert-all-source-field-and-expression-cells` | INSERT ALL INTO users (id, name, name_upper) VALUES (src_id, src_name, UPPER(src_name)) SELECT src_id, src_name FROM source_users | covered |
| `VO145` | `vastbase-oracle-insert-select-union-distinct-literals` | INSERT INTO users (id, name) SELECT 1, 'a' FROM dual UNION SELECT 2, 'b' FROM dual | covered |
| `VO146` | `vastbase-oracle-insert-select-intersect-binds` | INSERT INTO users (id, name) SELECT :1, :2 FROM dual INTERSECT SELECT :3, :4 FROM dual | covered |
| `VO147` | `vastbase-oracle-insert-select-minus-named-binds` | INSERT INTO users (id, name) SELECT :id1, :name1 FROM dual MINUS SELECT :id2, :name2 FROM dual | covered |
| `VO148` | `vastbase-oracle-insert-all-schema-qualified-targets` | INSERT ALL INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (950001, 'a') INTO KDES.DBP_PHONE_TEST (ID, PHONE) VALUES (:2, :phone) SELECT 1 FROM DUAL | covered |
| `VO149` | `vastbase-oracle-like-escape-literal` | SELECT ID FROM KDES.USERS WHERE NAME LIKE 'A!_%' ESCAPE '!' | covered |
| `VO150` | `vastbase-oracle-not-like-escape-named-bind` | SELECT ID FROM KDES.USERS WHERE NAME NOT LIKE :pattern ESCAPE :escape_char | covered |
| `VO151` | `vastbase-oracle-like-escape-question-bind` | SELECT ID FROM KDES.USERS WHERE NAME LIKE ? ESCAPE ? | covered |
| `VO152` | `vastbase-oracle-like-escape-expression` | SELECT ID FROM KDES.USERS WHERE NAME LIKE :pattern ESCAPE UPPER('!') | covered |
| `VO153` | `vastbase-oracle-derived-like-escape-literal` | SELECT D.ID FROM (SELECT ID, NAME FROM KDES.USERS) D WHERE D.NAME LIKE :pattern ESCAPE '!' | covered |
| `VO154` | `vastbase-oracle-like-without-explicit-escape` | SELECT ID FROM KDES.USERS WHERE NAME LIKE :pattern | covered |
| `VO155` | `vastbase-oracle-p3-update-alias-qualified-assignment` | UPDATE encrypt_test_data x SET x.email = :1 WHERE x.id = :2 | covered |
| `VO156` | `vastbase-oracle-p3-update-multiple-alias-qualified-assignments` | UPDATE encrypt_test_data x SET x.email = :1, x.secret_sn = :2 WHERE x.phone = :3 | covered |
| `VO157` | `vastbase-oracle-p3-update-from-source-field` | UPDATE t SET name = s.name FROM src s WHERE t.id = s.id | covered |
| `VO158` | `vastbase-oracle-p3-update-schema-qualified-alias-target` | UPDATE KDES.ENCRYPT_TEST_DATA x SET x.email = :1 WHERE x.id = :2 | covered |
| `VO159` | `vastbase-oracle-p3-update-scalar-subquery-predicate` | UPDATE encrypt_test_data x SET x.email = :1 WHERE x.id = (SELECT y.id FROM encrypt_test_data y WHERE y.phone = :2) | covered |
| `VO160` | `vastbase-oracle-p3-delete-exists-correlated-predicate` | DELETE FROM encrypt_test_data x WHERE EXISTS (...) | covered |
| `VO161` | `vastbase-oracle-p3-select-or-predicate-and-order-by` | SELECT x.id,x.email,x.bank_card FROM encrypt_test_data x WHERE ... OR ... ORDER BY x.id | covered |
| `VO162` | `vastbase-oracle-p3-insert-all-independent-branches` | INSERT ALL INTO encrypt_test_data(...) VALUES(...) INTO encrypt_test_data(...) VALUES(...) SELECT 1 FROM dual | covered |
| `VO163` | `vastbase-oracle-p3-merge-update-source-target-lineage` | MERGE INTO t USING (SELECT :1 id, :2 email FROM dual) s ... UPDATE SET t.email=s.email | covered |
| `VO164` | `vastbase-oracle-p3-merge-insert-source-target-lineage` | MERGE INTO t USING (SELECT :1 id, :2 email FROM dual) s ... INSERT(id,email) VALUES(s.id,s.email) | covered |
| `VO165` | `vastbase-oracle-p3-select-distinct-base-field-lineage` | SELECT DISTINCT x.email FROM encrypt_test_data x | covered |
| `VO166` | `vastbase-oracle-p3-select-alias-order-by-lineage` | SELECT x.email AS e FROM encrypt_test_data x ORDER BY x.email | covered |
| `VO167` | `vastbase-oracle-p3-select-star-rowid-lineage` | SELECT x.*, x.ROWID FROM encrypt_test_data x ORDER BY x.id | covered |
| `VO168` | `vastbase-oracle-p3-update-full-alias-qualified-crypto-shape` | UPDATE encrypt_test_data x SET x.email=:1, x.secret_sn=:2, x.special_str=:3, x.remark=:4 WHERE ... | covered |
| `VO169` | `vastbase-oracle-regexp-like-function-predicate` | SELECT * FROM users WHERE REGEXP_LIKE(name, :pat) | covered |
| `VO170` | `vastbase-oracle-database-link-schema-alias-bind` | SELECT u.id FROM kdes.users@remote_db u WHERE u.id = :id | covered |
| `VO171` | `vastbase-oracle-database-link-update-target` | UPDATE users@remote_db SET name = :name WHERE id = :id | covered |
| `VO172` | `vastbase-oracle-database-link-insert-target` | INSERT INTO users@remote_db (id, name) VALUES (:id, :name) | covered |
| `VO173` | `vastbase-oracle-database-link-delete-target` | DELETE FROM users@remote_db WHERE id = :id | covered |
| `VO174` | `vastbase-oracle-database-link-quoted-identifiers` | SELECT * FROM "USERS"@"REMOTE_DB" | covered |
| `VO178` | `vastbase-oracle-union-all-three-branch-scope` | (SELECT 1 AS C FROM DUAL UNION ALL SELECT 2 AS C FROM DUAL) UNION ALL SELECT 3 AS C FROM DUAL | covered |
| `VO179` | `vastbase-oracle-grouped-union-all-intersect` | (SELECT 1 AS C FROM DUAL UNION ALL SELECT 2 AS C FROM DUAL) INTERSECT (SELECT 3 AS C FROM DUAL UNION ALL SELECT 4 AS C FROM DUAL) | covered |
| `VO180` | `vastbase-oracle-union-all-root-cte-scope` | WITH src AS (SELECT 1 AS C FROM DUAL) SELECT C FROM src UNION ALL SELECT C FROM src | covered |
| `VO181` | `vastbase-oracle-union-all-qualified-table-bypasses-cte` | WITH src AS (SELECT 1 AS id FROM DUAL) (SELECT src.id FROM src UNION ALL SELECT s.id FROM app.src s) UNION ALL SELECT r.id FROM src@remote_db r | covered |
| `VO182` | `vastbase-oracle-correlated-union-all-subquery-scope` | SELECT o.id FROM orders o WHERE EXISTS (SELECT 1 FROM order_items i WHERE i.order_id = o.id UNION ALL SELECT 1 FROM archived_order_items a WHERE a.order_id = o.id) | covered |

## Coverage Boundary

This matrix lists only cases that parse successfully and have final View and
patch expectations. Syntax outside the executable fixture must not be listed
here as a validated case.
