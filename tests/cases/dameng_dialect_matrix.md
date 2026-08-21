# 达梦方言用例矩阵

本文件记录达梦方言转换层的回归用例。可执行夹具为 `tests/cases/dameng_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。用例提供 `bind_occurrences` 时，runner 还会对原 SQL 及每个 patch 后 SQL 的 `position`、`kind`、`key` 和原始 `sql` 逐项精确校验，重复 key 不合并，`position` 按整段 SQL 连续编号。

## 矩阵统计与 session 回归

夹具包含 185 条 `status = "final"` 用例和 658 个独立 patch；其中 8 条用例及其 23 个 patch 含完整 bind occurrence 断言。34 条用例包含 statement 级 `query_graph.session`，覆盖 `D002`、`D003`、`D003Q`、`D026`、`D089` 至 `D095` 和 `DM-*` session 用例；这 34 条用例均至少包含一个非空 session item。

用例提供 `query_graph.session` 时，矩阵测试会随完整 View JSON 精确校验 session action、item scope、target kind、name 及 value 字段。每条用例还会反解析未修改的 handle，并将结果与输入 SQL 逐字节比较。

## 支持用例

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| D001 | `SELECT` + `NVL` + 命名 bind | 达梦兼容 `:name` bind 转换与还原 |
| D002 | `SET SCHEMA` | 当前 schema 会话上下文切换 |
| D003 | `ALTER SESSION SET CURRENT_SCHEMA` | schema 会话切换语句 |
| D003Q | `ALTER SESSION SET CURRENT_SCHEMA="..."` | 带引号 schema 标识符，公共 literal view 暴露 quoted identifier 语义 |
| D089 | `ALTER SESSION SET NLS_DATE_FORMAT` | 当前会话 DATE 日期串格式 |
| D090 | `ALTER SESSION SET NLS_TIMESTAMP_FORMAT` | 当前会话 TIMESTAMP 日期串格式 |
| D091 | `ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT` | 当前会话 TIMESTAMP_TZ 日期串格式 |
| D092 | `ALTER SESSION SET NLS_TIME_FORMAT` | 当前会话 TIME 日期串格式 |
| D093 | `ALTER SESSION SET NLS_TIME_TZ_FORMAT` | 当前会话 TIME_TZ 日期串格式 |
| D094 | `ALTER SESSION SET NLS_SORT` | 当前会话自然语言排序方式 |
| D095 | `ALTER SESSION SET CASE_SENSITIVE` | 当前会话大小写敏感属性 |
| D004 | `MINUS` | 达梦 `MINUS` 与核心集合运算双向转换 |
| D005 | `LIMIT n OFFSET n` | 达梦分页基础形态 |
| D006 | `LIMIT offset,n` | 逗号分页转换为核心分页结构 |
| D007 | `SELECT TOP n` | `TOP` 基础形态转换并在 deparse 中保留公开形态 |
| D008 | 多表 JOIN + bind | 表、选择列、连接列和条件列识别 |
| D009 | `INSERT ... VALUES` + bind | 插入列识别和 bind 还原 |
| D010 | 多行 `INSERT ... VALUES` | 多行值列表 |
| D011 | `INSERT ... SELECT` | 目标表、来源表和插入列识别 |
| D012 | `UPDATE` + 多赋值 + bind | 更新列、条件列和 bind 还原 |
| D013 | `DELETE` + 条件 | 条件删除 |
| D014 | `MERGE` | 合并语句基础结构 |
| D015 | `CREATE TABLE` | 建表语句 |
| D016 | `CREATE OR REPLACE VIEW` | 视图创建语句 |
| D017 | `CREATE SEQUENCE` | 序列创建语句 |
| D018 | `ALTER TABLE ... ADD` | 添加列 |
| D019 | `CREATE INDEX` | 创建索引 |
| D020 | `DROP TABLE` + `TRUNCATE TABLE` | 删除和清空表 |
| D021 | 事务控制 | `BEGIN`、`COMMIT`、`ROLLBACK` |
| D022 | 授权语句 | `GRANT`、`REVOKE` |
| D023 | `ROWNUM` 条件 | 伪列作为条件表达式 |
| D024 | `FOR UPDATE NOWAIT` | 行锁查询 |
| D025 | q-quoted 字符串 | `q'[...]'` 字符串兼容处理 |
| D026 | `SET SCHEMA; SELECT` | 多语句中的 schema 切换和查询保持独立输出 |
| D027 | `DATE` + `TIMESTAMP` literal | 日期和时间戳字面量 |
| D028 | `GROUP BY` + `HAVING` + 窗口函数 | 聚合查询和分析函数 |
| D029 | `SELECT TOP offset,count` | `TOP` offset/count 形态转换并在 deparse 中保留公开形态 |
| D030 | `INSERT ... VALUES (?, ?, ?)` | JDBC 风格位置参数转换、插入列识别和公开形态还原 |
| D031 | `UPDATE ... SET ... WHERE ... = ?` | SET/WHERE 中的位置参数转换和公开形态还原 |
| D032 | `EXEC SQL PREPARE ... FROM ...` | 达梦嵌入式 SQL prepare 语句、SQL 文本和 `?` 占位符公开形态还原 |
| D033 | `EXEC SQL EXECUTE ... USING ...` | 达梦嵌入式 SQL execute 语句和参数公开形态还原 |
| D034 | `EXEC SQL DEALLOCATE PREPARE ...` | 达梦嵌入式 SQL prepared statement 释放语句 |
| D035 | 多命名 bind 查询 | 查询条件中的多个 `:name` bind |
| D036 | `IN` + 多命名 bind | 条件列表中的多个命名 bind |
| D037 | `INSERT ... VALUES` + 多命名 bind | 插入列和命名 bind 值列表 |
| D038 | 多行 `INSERT ... VALUES` + `?` | 多行 JDBC 风格参数化插入 |
| D039 | `UPDATE ... SET ... WHERE ... = ?` | 更新列、条件列和位置参数 |
| D040 | `DELETE ... WHERE ... = ?` | 条件删除和位置参数 |
| D041 | `EXEC SQL PREPARE` + INSERT | 嵌入式 SQL prepared insert 文本 |
| D042 | `EXEC SQL EXECUTE` + 命名 bind | prepared statement 执行和命名 bind 参数 |
| D043 | `EXEC SQL EXECUTE` + `?` 参数 | prepared statement 执行和位置参数 |
| D044 | `TOP` + 直接字段 + 命名 bind | TOP 查询、直接输出字段、WHERE bind 和 ORDER BY 归属 |
| D045 | `CASE` 表达式输出 | `CASE WHEN` 中字段的 `target_path` 归属 |
| D046 | `GROUP BY` + `HAVING` + `ORDER BY` | 聚合输出和非输出子句字段归属 |
| D047 | `UPDATE` + 多命名 bind | update/where 子句、bind 字段和空 value |
| D048 | `JOIN ... ON` + bind | JOIN/ON 字段、WHERE bind 和表字段归属 |
| D049 | `NVL` 函数输出 | 函数 `target_path`、参数序号和 WHERE bind |
| D050 | `BETWEEN` + 多命名 bind | `BETWEEN` 条件中的多个命名 bind 和字段值关联 |
| D051 | `NOT IN` + 多命名 bind | 否定 `IN` 条件中的多个命名 bind 和字段值关联 |
| D052 | `NOT BETWEEN` + 多命名 bind | 否定 `BETWEEN` 条件中的多个命名 bind 和字段值关联 |
| D053 | `NOT LIKE` + 命名 bind | 否定 `LIKE` 条件中的命名 bind、字段级 operator 和关键字归属 |
| D054 | `DISTINCT` + `LIKE` bind | DISTINCT 投影、LIKE 命名 bind 和字段归属 |
| D055 | 嵌套函数投影 | `LOWER(UPPER(...))` 的有序 `target_path` |
| D056 | `DELETE ... IN` + 命名 bind | 条件删除、集合参数和字段 operator |
| D057 | `UPDATE ... EXISTS` | 子查询条件、相关字段和 SET bind |
| D058 | 无列名 `INSERT` | 无列名插入、行 cell、命名 bind 和空列名输出 |
| D059 | `CREATE OR REPLACE VIEW` + JOIN 聚合 | 视图创建、JOIN 条件和 GROUP BY 聚合 |
| D060 | ROWNUM 嵌套分页真实字段集 | 多字段投影、`a.*`、ROWNUM 条件和分页 bind |
| D061 | `LEFT JOIN` + `alias.*` | 限定星号、JOIN/ON 字段和 WHERE bind |
| D062 | `LIMIT/OFFSET` + `?` 参数 | 分页子句中的位置参数 |
| D063 | `SELECT :bind FROM dual` | DUAL 查询和 SELECT 列表中的命名 bind |
| D064 | 多 `UPDATE` + `MERGE` 中的匿名/命名 bind | 按全文顺序保留重复 `:merge_value`，排除注释中的 `:comment`/`?`；复杂 patch 覆盖子查询、CAST、CASE、`LIMIT/OFFSET`、命名/数字/匿名 bind 和保护区，并精确校验删除/插入后重编号 |
| D065 | `dameng-select-derived-query-graph` | 派生表 + 输出别名 + 命名 bind | 派生表字段向内层真实表字段的 `query_graph` 来源链路 映射和 `output_name` |
| D066 | `dameng-select-reference-024` | SELECT 参考用例 024 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D067 | `dameng-select-reference-026` | SELECT 参考用例 026 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D068 | `dameng-select-reference-028` | SELECT 参考用例 028 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D069 | `dameng-select-reference-033` | SELECT 参考用例 033 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D070 | `dameng-select-reference-044` | SELECT 参考用例 044 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D071 | `dameng-select-reference-045` | SELECT 参考用例 045 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D072 | `dameng-select-reference-048` | SELECT 参考用例 048 | 达梦/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| D073 | `dameng-select-reference-049` | SELECT 参考用例 049 | 达梦/ROWNUM/复杂派生表 `*` 链路和 UNION 分支的 `query_graph` 表达 |
| D074 | `dameng-select-reference-046` | SELECT 参考用例 046 | 达梦复杂派生表和多 JOIN 子查询解析和 View JSON 结构 |
| D075 | `dameng-select-reference-047` | SELECT 参考用例 047 | 达梦 UNION + 复杂派生表子查询解析和 View JSON 结构 |
| D076 | `dameng-field-match-kind-direct-and-expression` | 直接字段条件 + 函数包裹字段条件 | `query_graph.values[].field_match_kind` 区分 `direct_field` 和 `expression_field` |
| D077 | `dameng-expression-field-case-expression-value` | CASE 返回字段再与 bind 比较 | CASE 表达式字段输出 `expression_field` value 关系 |
| D078 | `dameng-expression-field-multi-field-expression-value` | `NVL(secret, id)`、`secret || id` 与 bind 比较 | 表达式内字段分别保留 `expression_field` value 关系 |
| D079 | `dameng-expression-field-value-side-expression` | 字段与值侧函数、拼接、CAST 比较 | 值侧表达式输出 `kind=expression`，不暴露 direct bind |
| D080 | `dameng-expression-field-dml-expression-values` | INSERT/UPDATE 表达式赋值 | DML cell/assignment 输出 `kind=expression` |
| D081 | `dameng-update-positional-bind-rhs-crypto-source` | `UPDATE ... SET protected = :1` | UPDATE SET 右值为位置 bind 的保护字段来源表达 |
| D082 | `dameng-update-named-bind-rhs-crypto-source` | `UPDATE ... SET protected = :name` | UPDATE SET 右值为命名 bind 的保护字段来源表达 |
| D083 | `dameng-update-question-bind-rhs-crypto-source` | `UPDATE ... SET protected = ?` | UPDATE SET 右值为 JDBC 位置 bind 的保护字段来源表达 |
| D084 | `dameng-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = :1, protected2 = :2` | 多个保护字段的 SET bind、字段归属和全局 bind 序号 |
| D085 | `dameng-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | 达梦字面量 ESCAPE 输出到 `values[].like_escape` |
| D086 | `dameng-not-like-escape-named-bind` | `NOT LIKE :pattern ESCAPE :escape_char` | 命名 bind pattern 与 escape 分别保留公开 SQL 和全局序号 |
| D087 | `dameng-like-escape-question-bind` | `LIKE ? ESCAPE ?` | JDBC 风格位置参数 pattern 和 escape 的结构化输出 |
| D088 | `dameng-like-without-explicit-escape` | `LIKE :pattern` | 无显式 ESCAPE 时不输出 `like_escape` |
| D134 | `dameng-top-percent` | `SELECT TOP 10 PERCENT ...` | TOP 百分比形态解析并在 deparse 中保留 `PERCENT` |
| D135 | `dameng-top-with-ties` | `SELECT TOP 2 WITH TIES ...` | TOP 同分保留形态解析并在 deparse 中保留 `WITH TIES` |
| D136 | `dameng-top-percent-with-ties` | `SELECT TOP 70 PERCENT WITH TIES ...` | 达梦官方组合 TOP 形态解析并保留公开输出 |
| D137 | `dameng-limit-before-top-restoration` | `LIMIT` 语句后接 `TOP` 语句 | TOP 回写按生成的 LIMIT 序号匹配，不会覆盖前置普通 LIMIT |

## 多表插入

达梦官方语法支持 `<multi_insert_stmt>`，包括 `INSERT ALL`、`INSERT FIRST`、`WHEN ... THEN`、`ELSE`、多个 `INTO` 分支以及后续查询表达式。当前实现使用通用 DML graph 结构表达该类语句，不新增方言专用 JSON 字段。

| ID | 用例 | SQL 形态 | 覆盖内容 |
| --- | --- | --- | --- |
| DU006 | `dameng-insert-all` | `INSERT ALL INTO ... VALUES ... SELECT ...` | `insert_mode=all`、多 branch、目标表和 deparse |
| D120 | `dameng-insert-all-bind-branches` | 多 branch bind cell | branch cell 的 `bind_key`、`bind_sql` 和全局 `bind_position` |
| D121 | `dameng-insert-first-direct-source-fields` | `INSERT FIRST WHEN ... ELSE ... SELECT ...` | `insert_mode=first`、condition selector、source target/field 关联 |
| D122 | `dameng-insert-all-conditional` | 多个 `WHEN ... THEN` | 多 condition selector、bind 序号和 source target 关联 |
| D123 | `dameng-insert-all-multiple-into-per-when` | 单个 `WHEN` 下多个 `INTO` | 多 branch 与 bind 顺序稳定 |
| D124 | `dameng-insert-all-source-field-and-expression-cells` | source field 与表达式 cell 混合 | direct field 使用 `source_target`，表达式 cell 不误标 |
| D125 | `dameng-insert-all-schema-qualified-targets` | schema-qualified 多目标表 | branch target relation 保留 schema/table，bind 信息稳定 |
| D126 | `dameng-insert-first-grouped-when-else-branches` | `INSERT FIRST` 单个 `WHEN/ELSE` 下多个 `INTO` | `branch_kind=when/else`，deparse 保留同组 `INTO`，避免 `INSERT FIRST` 语义被拆分 |
| D127 | `dameng-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | INSERT SELECT 的目标列、来源块、来源字段和 bind 归属 |
| D128 | `dameng-merge-update-source-target-lineage` | MERGE matched UPDATE | `s.email` assignment 关联 source field 和 source target |
| D129 | `dameng-merge-insert-source-target-lineage` | MERGE not matched INSERT | INSERT cell `s.email` 关联 source field 和 source target |
| D130 | `dameng-regexp-like-function-predicate` | `REGEXP_LIKE(name, :pat)` | 函数谓词复用 `fields/values/predicates` 输出字段、bind 和 expression predicate |
| D131 | `dameng-select-alias-order-by-lineage` | `SELECT u.email AS e ... ORDER BY u.email` | SELECT 输出 alias 保留 base field lineage，ORDER BY 字段独立归属 |
| D132 | `dameng-select-or-predicate-order-by-lineage` | `WHERE field = :bind OR field = :bind ORDER BY ...` | OR 谓词树保留两个比较子节点、bind 和独立 ORDER BY 字段归属 |
| D133 | `dameng-unsupported-keywords-in-quoted-identifiers` | `SELECT "RETURNING", "CONNECT" ...` | unsupported 预筛选不会误伤受保护标识符 |
| DU007 | `dameng-database-link` | `table@database_link` | 远程对象引用基础形态，View JSON 输出 `link` |
| D138 | `dameng-database-link-schema-alias-bind` | `schema.table@link alias` + bind | schema/table/alias/link 和 bind 归属 |
| D139 | `dameng-database-link-update-target` | `UPDATE table@link ...` | DML target 的 database link 保留 |
| D140 | `dameng-database-link-insert-target` | `INSERT INTO table@link ...` | INSERT target 的 database link 保留 |
| D141 | `dameng-database-link-delete-target` | `DELETE FROM table@link ...` | DELETE target 的 database link 保留 |
| D142 | `dameng-database-link-quoted-identifiers` | `"TABLE"@"LINK"` | quoted identifier 形态的 database link 保留 |
| DU008 | national q-quoted 字符串 | `nq'[...]'` 转换后保留 national 字符串语义 |
| DU008A | 重复 national q-quoted 字符串 | 普通字符串和 national 字符串内容相同时只恢复 national 项 |
| DU008B | national 字符串字面量 | `N'...'` 输入保留 national 字符串语义 |
| DU008C | 重复 national 字符串字面量 | 普通字符串和 `N'...'` 内容相同时只恢复 national 项 |

## INSERT VALUES 回归：bind 与表达式混合

`DM-BM001` 至 `DM-BM010` 覆盖达梦 INSERT VALUES 中 JDBC 参数标记、时间函数、复合表达式、literal、`DEFAULT` 和多行值的组合。

本组中的 `?` 仅作为 JDBC 预编译语句参数标记；执行时必须经过对应的 prepare/bind 流程。用例验证解析、View 单元映射、bind 全局序号和逐字节反解析，不连接数据库。`DEFAULT` 只作为独立值单元使用。`DM-BM009` 验证多行 `VALUES` 的解析、View 映射和反解析，不验证列存储表上的执行语义。

| ID | SQL 组合 | 回归目标 |
| --- | --- | --- |
| `DM-BM001` | 三个 `?` + 尾部 `SYSDATE()` | 尾部时间表达式不得复制前一个 bind，也不得出现在 `query_graph.fields[].column` 中 |
| `DM-BM002` | 头部 `SYSDATE()` + 三个 `?` | 表达式在 bind 之前时保持单元位置和 bind 顺序 |
| `DM-BM003` | `?` 与两种当前时间函数交错 | 多个表达式与 bind 交错时保持列序 |
| `DM-BM004` | `?`、`NULL`、`SYSDATE()` 混合 | bind、`NULL` 字面量和表达式的种类互不污染 |
| `DM-BM005` | `?`、`DEFAULT`、`CURRENT_TIMESTAMP()` 混合 | `DEFAULT` 独立单元和时间表达式分类 |
| `DM-BM006` | `?`、字符串字面量、`SYSDATE()` 混合 | 普通字面量不会改变相邻 bind 和表达式位置 |
| `DM-BM007` | 直接 `?`、`COALESCE(?, 'fallback')`、`SYSDATE()` 和后续直接 `?` | 表达式内参数计入全局序号，后续直接 bind 位置正确 |
| `DM-BM008` | 直接 `?`、包含 `?` 的 `CASE` 表达式、`CAST(? AS INTEGER)`、`SYSDATE()` 和后续直接 `?` | 多个含参表达式之后的直接 bind 保持全局位置 |
| `DM-BM009` | 三行 `VALUES` 中 bind 与时间函数换位 | 多行单元坐标和跨行 bind 顺序稳定 |
| `DM-BM010` | schema-qualified 引号标识符、不规则空白、`?` 和 `SYSDATE` | 保持标识符、空白、单元映射并逐字节还原输入 SQL |

## ROWNUM 谓词语义回归

本组覆盖 ROWNUM 与普通条件组成布尔树、排序后 Top-N、反向等值条件、大于边界条件以及 DELETE 批处理条件。ROWNUM 条件在 View 中表达为不关联 `fields[]` 的 expression predicate，并指向对侧 literal 或 bind value。

| ID | 用例 | SQL 形态 | 覆盖内容 |
| --- | --- | --- | --- |
| D-RN001 | `dameng-rownum-and-named-bind` | 普通比较 `AND ROWNUM <= :limit` | AND 根节点同时保留普通 comparison 和无字段 ROWNUM expression；命名 bind 序号为 1 |
| D-RN002 | `dameng-rownum-ordered-top-n` | 内层 `ORDER BY`、外层 `ROWNUM < 11` | 排序字段属于内层 block，ROWNUM predicate 和 literal 属于外层 block |
| D-RN003 | `dameng-rownum-reversed-equality` | `1 = ROWNUM` | 反向操作数保持原始顺序，literal 由无字段 equality expression 引用 |
| D-RN004 | `dameng-rownum-greater-than-boundary` | `ROWNUM > 1` | 大于边界条件在 View 中完整保留 predicate 和 literal，不进行语义折叠 |
| D-RN005 | `dameng-delete-rownum-batch-limit` | DELETE 普通比较 `AND ROWNUM <= :batch_size` | DELETE DML 目标、AND 布尔树和 ROWNUM bind 的 block、位置及归属 |

## RETURN/RETURNING INTO 回归

当前覆盖 `INSERT`、`DELETE` 的 `RETURNING <target, ...> INTO <:bind, ...>`，以及 `UPDATE` 的 `RETURN <target, ...> INTO <:bind, ...>`。每个列表均须包含 `N >= 1` 项，两个列表严格等长并按序号一一配对；`INTO` 项必须是冒号宿主 bind。View 通过 `dml.result_channels` 的 sink channel 表达返回通道，每个返回 target 的 `sink_value` 指向 `query_graph.values[]` 中同序号的宿主 bind。该边界不包含 `BULK COLLECT`、非冒号 bind 接收项或不等长列表。

| ID | 用例 | SQL 形态 | 覆盖内容 |
| --- | --- | --- | --- |
| D143 | `dameng-insert-values-returning-rowid-into-bind` | `INSERT ... VALUES ... RETURNING ROWID INTO :NAV_ROWID` | INSERT `target_after` 引用、ROWID pseudo target、sink bind、replace patch 和逐字节 deparse |
| D144 | `dameng-update-return-rowid-into-bind` | `UPDATE ... RETURN ROWID INTO :NAV_ROWID` | UPDATE `target_after` 引用、达梦 `RETURN` 关键字、sink bind、replace patch 和逐字节 deparse |
| D145 | `dameng-delete-returning-rowid-into-bind` | `DELETE ... RETURNING ROWID INTO :NAV_ROWID` | DELETE `target_before` 引用、ROWID pseudo target、sink bind、replace patch 和逐字节 deparse |
| D146 | `dameng-merge-update-delete-where` | matched UPDATE 同时含 action `WHERE` 和附属 `DELETE WHERE` | UPDATE 分支同时输出 `condition_selector` 与 `delete_condition_selector`，DELETE 条件不生成独立 action；3 个独立 patch 覆盖 assignment、action 条件值和 DELETE 条件值替换 |
| D151 | `dameng-insert-returning-eight-target-bind-pairs` | INSERT `RETURNING` 8 target ↔ 8 bind | 严格等长按序配对；头部原子插入 1 组 target/bind 后为 9↔9 |
| D152 | `dameng-update-return-eight-target-bind-pairs` | UPDATE `RETURN` 8 target ↔ 8 bind | 完整顺序枚举 SET、WHERE 及 8 个 `INTO` bind，根 SQL 共 11 个 occurrence；中部成对插入 `last_login_at` / `:out_last_login_at` 后为 12 个，并保持 9↔9 顺序对齐 |
| D153 | `dameng-delete-returning-eight-target-bind-pairs` | DELETE `RETURNING` 8 target ↔ 8 bind | 严格等长按序配对；尾部原子插入 1 组 target/bind 后为 9↔9 |

## 层次查询回归

以下 4 条 final 用例定义达梦层次查询边界，包括 `START WITH ... CONNECT BY` 与 `CONNECT BY ... START WITH` 两种源文本顺序、`PRIOR` 的两种父子字段方向、`LEVEL`、`NOCYCLE` 和 `CONNECT_BY_ROOT`。每条用例包含 5 个独立 patch，合计 20 个，其中 16 个 `replace`、4 个 `insert_column`。

| ID | 用例 | SQL 形态 | 覆盖内容 |
| --- | --- | --- | --- |
| D147 | `dameng-hierarchical-start-connect-prior-level` | `START WITH` 后接 `CONNECT BY PRIOR` + `LEVEL` | `start_with` / `connect_by` clause、relationless `LEVEL` pseudo target 和左侧 `PRIOR` 字段 occurrence |
| D148 | `dameng-hierarchical-connect-start-source-order` | `CONNECT BY` 后接 `START WITH` | 反向源文本子句顺序逐字保留，View 仍按 START WITH、CONNECT BY 语义顺序构建 |
| D149 | `dameng-hierarchical-prior-reverse-direction` | `PRIOR manager_id = employee_id` | `PRIOR` 位于比较左侧但作用字段方向与基础用例相反，field-to-field 归属保持明确 |
| D150 | `dameng-hierarchical-connect-by-root-nocycle` | `CONNECT_BY_ROOT` + `LEVEL` + `CONNECT BY NOCYCLE` | expression target 的 operator `target_path`、relationless pseudo target 和 CONNECT BY 根 predicate 的 `nocycle` 标记 |

## 多表单目标 UPDATE 回归

达梦多表 `UPDATE` 支持 JOIN 链、逗号 relation 列表及两者混合，但全部 SET assignment 必须解析到同一个 table object；该唯一对象作为 statement 和 DML target relation。

| ID | 用例 | SQL 形态 | 覆盖内容 |
| --- | --- | --- | --- |
| D154 | `dameng-multitable-update-two-table-join-first-target-contract` | 两表 JOIN，首 relation 为目标 | 唯一 target、ON/WHERE 归属及 assignment、relation patch |
| D155 | `dameng-multitable-update-three-table-middle-target-contract` | 三 relation 逗号列表，中间 relation 为目标 | 非首目标解析、bind 顺序及 assignment patch |
| D156 | `dameng-multitable-update-four-table-last-target-contract` | 四 relation 逗号列表，末 relation 为目标 | quoted object、末 relation target 及 patch |
| D157 | `dameng-multitable-update-same-table-distinct-alias-contract` | 同一表对象使用不同 alias | 按 alias 区分唯一写入对象及复合右值 patch |
| D158 | `dameng-multitable-update-four-table-join-chain-contract` | LEFT/INNER/RIGHT JOIN 链 | JOIN condition、唯一中间 target 和 assignment patch |
| D159 | `dameng-multitable-update-join-comma-mixed-contract` | JOIN 与逗号 relation 混合 | 混合 relation list、唯一 target 和反解析 |

## Query Graph 引号别名合同

`relations[].alias_quoted_identifier` 仅在 relation alias 使用双引号定界时为 `true`。`targets[].output_quoted_identifier` 在 output name 来自双引号显式别名，或无显式别名时继承双引号字段名时为 `true`；View JSON 不输出值为 `false` 的键。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| D160 | `dameng-quoted-relation-alias-and-target-output-contract` | 双引号 relation/派生表 alias 与 output name | 两个引号标志、字段名继承及 2 个 output alias patch |

## MERGE INSERT 独立列值改写

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| D161 | `dameng-merge-omitted-insert-column-value-independent` | 省略目标列列表的 `WHEN NOT MATCHED THEN INSERT VALUES (...)` | 省略状态仍输出 `target_list_selector`；3 个独立 patch 分别验证 column-only 物化列列表、value-only 追加 cell 和现有 `merge_insert_cell` 替换；与既有 paired 模式共同覆盖 `insert_column` 三态合同 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。未纳入该可执行夹具的语法边界由 `doc/dameng_official_syntax_coverage.csv` 维护。

## 维护要求

- 新增达梦支持项必须同步更新 `tests/cases/dameng_dialect_input.json`、本矩阵和可执行回归测试。
- 未纳入可执行夹具的语法不得在本矩阵中登记为已验证用例。
