# 达梦方言用例矩阵

本文件记录达梦方言转换层的回归用例。可执行夹具为 `tests/cases/dameng_dialect_input.json`，单元测试 `tests/unit/test_dameng_dialect_case_matrix.c` 会逐条验证解析结果、View JSON、反解析输出和错误码。

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
| D064 | 多语句 `?` 参数 | 多语句输入中位置参数 `bind_position` 按整条 SQL 全局递增 |
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

## 明确不支持用例

以下语法具有达梦或兼容模式下的专属语义，当前不会尝试映射为 PostgreSQL AST。转换层返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误，避免生成语义不可靠的 SQL。

| ID | 用例 | 原因 |
| --- | --- | --- |
| DU001 | `CONNECT BY` | 层级查询语义需要专用查询模型 |
| DU002 | `PIVOT` | 表变换语义需要专用查询模型 |
| DU003 | `RETURNING ... INTO` | 返回目标和宿主变量语义不等价 |
| DU004 | DMSQL block | 超出 SQL 语句转换范围 |
| DU010 | `CREATE PROCEDURE` | DMSQL 程序单元 |
| DU012 | `ALTER SESSION SET CONTAINER` | 达梦当前不支持 container 会话语义 |

## 维护要求

- 新增达梦支持项必须同步更新 `tests/cases/dameng_dialect_input.json`、本矩阵和可执行回归测试。
- 无法保证语义等价的达梦专有语法必须返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误。
