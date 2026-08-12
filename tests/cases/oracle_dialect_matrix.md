# Oracle 方言用例矩阵

本文件记录 Oracle 方言转换层的回归用例。可执行夹具为 `tests/cases/oracle_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。

## 矩阵统计与 session 回归

夹具包含 251 条 `status = "final"` 用例和 852 个独立 patch。59 条用例包含 statement 级 `query_graph.session`，覆盖 `O043`、`O043Q`、`O044` 至 `O047`、`O082` 至 `O086`，以及 `ORA-*` session 用例；这 59 条用例均至少包含一个非空 session item。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session action、item scope、target kind、name 及 value 字段均属于比较范围。

## INSERT VALUES 回归：bind 与表达式混合

`ORA-BM001` 至 `ORA-BM010` 覆盖 Oracle 单行 `INSERT ... VALUES`。`:name`、`:1` 按 Oracle bind 处理；`?` 只按 JDBC prepared-statement 模板处理，Oracle 原生 bind 变量使用冒号前缀标记。

10 条用例均为单行 VALUES；`DEFAULT` 只作为独立 cell。每条用例逐 cell 断言 `row`、`column`、`kind` 和 `selector`，逐个直接 bind 断言 `bind_key`、`bind_kind`、`bind_sql`、全局 `bind_position` 和 `selector`；通过表达式后的直接 bind 位置验证嵌套 bind 已计入全局序号，并禁止 `SYSDATE`、`CURRENT_TIMESTAMP` 出现在 `query_graph.fields[].column` 中。

| ID | SQL 形态 | 关键边界 |
| --- | --- | --- |
| `ORA-BM001` | 三个独立 JDBC `?` + 尾部 `SYSDATE` | 逐字节保留带不规则空白和分号的原始输入 SQL；`?` 仅属 JDBC 模板 |
| `ORA-BM002` | 首位 `CURRENT_TIMESTAMP` + 三个独立 JDBC `?` | expression cell 在首列，bind 位置仍从 1 连续编号 |
| `ORA-BM003` | `:1`、`:2`、`:3` + 尾部 `SYSDATE` | 数字形式的 Oracle 冒号 bind 标记 |
| `ORA-BM004` | 首位 `SYSDATE` + 三个 named bind | 具名 Oracle 冒号 bind 标记 |
| `ORA-BM005` | named bind 与 `SYSDATE` / `CURRENT_TIMESTAMP` 交错 | expression cell 不得造成后续 bind 错位 |
| `ORA-BM006` | bind + `NULL` + `SYSDATE` + bind | `NULL` 为 literal，`SYSDATE` 为 expression |
| `ORA-BM007` | bind + 独立 `DEFAULT` + `CURRENT_TIMESTAMP` + bind | `DEFAULT` 不嵌入其他表达式 |
| `ORA-BM008` | 直接 bind + `COALESCE(:retry_count, 0)` + `SYSDATE` + 直接 bind | 尾部直接 bind 为 position 3，验证嵌套 bind 已计数 |
| `ORA-BM009` | 直接 bind + 包含 `:enabled` 的 `CASE` 表达式 + `CAST(:amount AS NUMBER)` + `SYSDATE` + 直接 bind | 尾部直接 bind 为 position 4，验证两个嵌套 bind 均已计数 |
| `ORA-BM010` | schema-qualified quoted identifiers + 三个冒号前缀 bind + `CURRENT_TIMESTAMP` | 混合大小写标识符、不规则空白和分号原样保留 |

## 支持用例

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| O001 | `SELECT` + `NVL` + 命名 bind | Oracle `:name` bind 转换与还原 |
| O002 | q-quoted 字符串 | `q'[...]'` 转换为安全字符串字面量 |
| O003 | national q-quoted 字符串 | `nq'[...]'` 转换后保留 national 字符串语义 |
| O003A | 重复 national q-quoted 字符串 | 普通字符串和 national 字符串内容相同时只恢复 national 项 |
| O003B | national 字符串字面量 | `N'...'` 输入保留 national 字符串语义 |
| O003C | 重复 national 字符串字面量 | 普通字符串和 `N'...'` 内容相同时只恢复 national 项 |
| O004 | `MINUS` | Oracle `MINUS` 与核心 `EXCEPT` 的双向转换 |
| O005 | `OFFSET ... FETCH` | Oracle 分页语法 |
| O006 | `ROWNUM` 条件 | 伪列作为条件表达式 |
| O007 | 多表 JOIN + bind | 表、选择列、连接列和条件列识别 |
| O008 | `INSERT ... VALUES` + bind | 插入列识别和 bind 还原 |
| O009 | 多行 `INSERT ... VALUES` | 多行值列表 |
| O010 | `INSERT ... SELECT` | 目标表、来源表和插入列识别 |
| O011 | `UPDATE` + 多赋值 + bind | 更新列、条件列和 bind 还原 |
| O012 | `DELETE` + 条件 | 条件删除 |
| O013 | 重复命名 bind | 同名 bind 使用同一个内部参数编号 |
| O014 | 位置 bind | `:1`、`:2` 转换与还原 |
| O015 | `DATE` literal | 日期字面量 |
| O016 | `CASE` 表达式 | 条件表达式 |
| O017 | `EXISTS` 子查询 | 子查询表和条件列识别 |
| O018 | `GROUP BY` + `HAVING` | 聚合查询 |
| O019 | `UNION ALL` | 集合查询 |
| O020 | `INTERSECT` | 集合查询 |
| O021 | `MERGE` | 合并语句基础结构 |
| O022 | `CREATE TABLE` | Oracle 常见类型名的建表语句 |
| O023 | `CREATE SEQUENCE` | 序列创建语句 |
| O024 | `CREATE OR REPLACE VIEW` | 视图创建语句 |
| O025 | `DROP TABLE` | 删除表 |
| O026 | `TRUNCATE TABLE` | 清空表 |
| O027 | 事务控制 | `SAVEPOINT`、`ROLLBACK TO SAVEPOINT`、`COMMIT` |
| O028 | 授权语句 | `GRANT`、`REVOKE` |
| O029 | 注释语句 | `COMMENT ON TABLE` |
| O030 | `FOR UPDATE NOWAIT` | 行锁查询 |
| O031 | `DECODE` + `SYSDATE` | Oracle 常见函数和伪列 |
| O032 | `ROW_NUMBER() OVER` | 分析函数 |
| O033 | `TIMESTAMP` literal | 时间戳字面量 |
| O034 | quoted identifiers | 大小写敏感对象名和列名 |
| O035 | `ALTER TABLE ... ADD` | 添加列 |
| O036 | `CREATE INDEX` | 创建索引 |
| O037 | `DROP INDEX` | 删除索引 |
| O038 | `IN` + 多 bind | 条件列表中的多个 bind |
| O039 | `DELETE` + `DATE` literal | 条件删除和日期字面量 |
| O040 | materialized view | 物化视图兼容语法 |
| O041 | unsupported 关键字字符串 | 字符串中的 `RETURNING`、`@`、`(+)` 不触发 unsupported |
| O042 | 层次查询关键字注释 | 注释中的 `CONNECT BY` 文本不参与 SQL 语法识别 |
| O042Q | unsupported 关键字受保护标识符 | 引号标识符中的 `RETURNING` 和 `@` 不触发 unsupported |
| O043 | `ALTER SESSION SET CURRENT_SCHEMA` | 当前 schema 会话上下文切换 |
| O043Q | `ALTER SESSION SET CURRENT_SCHEMA="..."` | 带引号 schema 标识符，公共 literal view 暴露 quoted identifier 语义 |
| O044 | `ALTER SESSION SET CONTAINER` | 当前 container 会话上下文切换 |
| O045 | `ALTER SESSION SET CONTAINER=CDB$ROOT` | 官方 root container 名称 |
| O046 | `ALTER SESSION SET CONTAINER ... SERVICE ...` | container 切换和 service 子句 |
| O047 | `SELECT ...; ALTER SESSION SET CURRENT_SCHEMA` | 多语句中的查询和 schema 切换保持独立输出 |
| O048 | `INSERT ... VALUES (?, ?, ?)` | JDBC 风格位置参数转换、插入列识别和公开形态还原 |
| O049 | `UPDATE ... SET ... WHERE ... = ?` | SET/WHERE 中的位置参数转换和公开形态还原 |
| O050 | `EXECUTE IMMEDIATE ... USING ...` | Oracle 动态 SQL 执行语句、SQL 文本和 bind 参数公开形态还原 |
| O051 | 多命名 bind 查询 | `SELECT` 条件中的多个 `:name` bind |
| O052 | `IN` + 多命名 bind | `IN (:a, :b, :c)` 条件中的 bind 还原 |
| O053 | `FETCH FIRST` + bind | 分页限制中的 bind 还原 |
| O054 | `INSERT ... VALUES` + 多命名 bind | 插入列和命名 bind 值列表 |
| O055 | `UPDATE` + 多命名 bind | 更新列、条件列和命名 bind |
| O056 | `DELETE` + 多命名 bind | 条件删除和命名 bind |
| O057 | 位置 bind 对 | `:1`、`:2` 条件参数 |
| O058 | `INSERT ... VALUES (?, ?, ?)` 扩展 | 插入列和 JDBC 风格位置参数 |
| O059 | `DELETE ... WHERE ... = ?` | 条件删除中的 JDBC 风格位置参数 |
| O060 | `EXECUTE IMMEDIATE` 更新语句 | 动态 UPDATE SQL 文本和多个 USING bind |
| O061 | ROWNUM 嵌套分页 + bind | 嵌套查询、`a.*`、伪列别名和命名 bind |
| O062 | `NVL` + `TO_CHAR` + `UPPER` | 函数 `target_path`、嵌套函数、参数序号和 WHERE bind |
| O063 | `CASE` 表达式输出 | `CASE WHEN` 中字段的 `target_path` 归属 |
| O064 | `GROUP BY` + `HAVING` + `ORDER BY` | 聚合输出和非输出子句字段归属 |
| O065 | `UPDATE` + 多命名 bind | update/where 子句、bind 字段和空 value |
| O066 | ROWNUM 分页字段归属 | 嵌套查询、`a.*`、ROWNUM 条件和外层条件归属 |
| O067 | `:1` 与 `?` 位置 bind 混用 | `bind_kind`、`bind_sql` 区分 Oracle 位置 bind 和 JDBC 位置参数 |
| O068 | `BETWEEN` + 多命名 bind | `BETWEEN` 条件中的多个命名 bind 和字段值关联 |
| O069 | `NOT IN` + 多命名 bind | 否定 `IN` 条件中的多个命名 bind 和字段值关联 |
| O070 | `NOT BETWEEN` + 多命名 bind | 否定 `BETWEEN` 条件中的多个命名 bind 和字段值关联 |
| O071 | `NOT LIKE` + 命名 bind | 否定 `LIKE` 条件中的命名 bind、字段级 operator 和关键字归属 |
| O072 | `DISTINCT` + `LIKE` bind | DISTINCT 投影、LIKE 命名 bind 和字段归属 |
| O073 | 嵌套函数投影 | `LOWER(UPPER(...))` 的有序 `target_path` |
| O074 | `DELETE ... IN` + 命名 bind | 条件删除、集合参数和字段 operator |
| O075 | `UPDATE ... EXISTS` | 子查询条件、相关字段和 SET bind |
| O076 | 无列名 `INSERT` | 无列名插入、行 cell、命名 bind 和空列名输出 |
| O077 | `CREATE OR REPLACE VIEW` + JOIN 聚合 | 视图创建、JOIN 条件和 GROUP BY 聚合 |
| O078 | ROWNUM 嵌套分页真实字段集 | 多字段投影、`a.*`、ROWNUM 条件和分页 bind |
| O079 | `LEFT JOIN` + `alias.*` | 限定星号、JOIN/ON 字段和 WHERE bind |
| O080 | `ORDER BY 1` | 数字排序项和投影顺序相关语法 |
| O081 | `SELECT :bind FROM dual` | DUAL 查询和 SELECT 列表中的命名 bind |
| O082 | `ALTER SESSION SET NLS_DATE_FORMAT` | 字符串型普通 session 参数 |
| O083 | `ALTER SESSION SET NLS_DATE_LANGUAGE` | 标识符型普通 session 参数 |
| O084 | `ALTER SESSION SET INSTANCE` | 数字型普通 session 参数 |
| O085 | `ALTER SESSION SET ERROR_ON_OVERLAP_TIME` | 布尔/枚举型普通 session 参数 |
| O086 | `ALTER SESSION SET NLS_NUMERIC_CHARACTERS` | 带标点字符串的普通 session 参数 |
| O087 | 多语句命名 bind | 多语句输入中命名 bind 的 `bind_position` 按整条 SQL 全局递增 |
| O088 | `oracle-select-derived-query-graph` | 派生表 + 输出别名 + 命名 bind | 派生表字段、输出别名和条件 bind 的 `query_graph` 表达 |
| O089 | `oracle-select-reference-024` | SELECT 参考用例 024 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O090 | `oracle-select-reference-026` | SELECT 参考用例 026 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O091 | `oracle-select-reference-028` | SELECT 参考用例 028 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O092 | `oracle-select-reference-033` | SELECT 参考用例 033 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O093 | `oracle-select-reference-044` | SELECT 参考用例 044 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O094 | `oracle-select-reference-045` | SELECT 参考用例 045 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O095 | `oracle-select-reference-048` | SELECT 参考用例 048 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析和 View JSON 结构 |
| O096 | `oracle-select-reference-049` | SELECT 参考用例 049 | Oracle/ROWNUM/复杂派生表 SELECT 示例解析、`d.* -> b.* -> o.*` 链路和 View JSON 结构 |
| O097 | `oracle-select-reference-046` | SELECT 参考用例 046 | Oracle 复杂派生表和多 JOIN 子查询解析和 View JSON 结构 |
| O098 | `oracle-select-reference-047` | SELECT 参考用例 047 | Oracle UNION + 复杂派生表子查询解析和 View JSON 结构 |
| O099 | `oracle-select-nested-star-query-graph` | 多层派生表 + ROWNUM + `SELECT *` | `query_graph` 表达派生表 `*` 链路和 UNION 分支 |
| O100 | `oracle-field-match-kind-direct-and-expression` | 直接字段条件 + 函数包裹字段条件 | `query_graph.values[].field_match_kind` 区分 `direct_field` 和 `expression_field` |
| O101 | `oracle-expression-field-case-expression-value` | CASE 返回字段再与 bind 比较 | CASE 表达式字段输出 `expression_field` value 关系 |
| O102 | `oracle-expression-field-multi-field-expression-value` | `NVL(SECRET, ID)`、`SECRET || ID` 与 bind 比较 | 表达式内字段分别保留 `expression_field` value 关系 |
| O103 | `oracle-expression-field-value-side-expression` | 字段与值侧函数、拼接、CAST 比较 | 值侧表达式输出 `kind=expression`，不暴露 direct bind |
| O104 | `oracle-expression-field-dml-expression-values` | INSERT/UPDATE 表达式赋值 | DML cell/assignment 输出 `kind=expression` |
| O105 | `oracle-update-positional-bind-rhs-crypto-source` | `UPDATE ... SET protected = :1` | UPDATE SET 右值为 Oracle 位置 bind 的保护字段来源表达 |
| O106 | `oracle-update-named-bind-rhs-crypto-source` | `UPDATE ... SET protected = :name` | UPDATE SET 右值为 Oracle 命名 bind 的保护字段来源表达 |
| O107 | `oracle-update-question-bind-rhs-crypto-source` | `UPDATE ... SET protected = ?` | UPDATE SET 右值为 JDBC 位置 bind 的保护字段来源表达 |
| O108 | `oracle-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = :1, protected2 = :2` | 多个保护字段的 SET bind、字段归属和全局 bind 序号 |
| O132 | `oracle-insert-all-bind-branches` | `INSERT ALL` 多分支 bind | 分支 cell 暴露 bind key、bind kind、bind SQL 和全局 bind 序号 |
| O133 | `oracle-insert-all-multi-target` | `INSERT ALL` 多目标表 | 每个 INTO 分支保留独立 target relation、target columns 和 rows |
| O134 | `oracle-insert-select-union-literals` | `INSERT ... SELECT ... UNION ALL` literal 来源 | source target 通过 value index 暴露 literal |
| O135 | `oracle-insert-select-union-positional-binds` | `INSERT ... SELECT ... UNION ALL` 位置 bind 来源 | source target 通过 value index 暴露位置 bind |
| O136 | `oracle-insert-select-union-named-binds` | `INSERT ... SELECT ... UNION ALL` 命名 bind 来源 | source target 通过 value index 暴露命名 bind |
| O137 | `INSERT ALL` | Oracle 多表插入 | `insert_mode=all`、分支 target relation、target columns、rows 和 deparse |
| O138 | `INSERT FIRST` | 条件多表插入 | `insert_mode=first` 和 branch condition selector |
| O139 | `oracle-insert-first-direct-source-fields` | `INSERT FIRST` branch cell 引用 source query 字段 | branch cell 输出 `kind=field` 并通过 `source_target` 指向 source query 输出项 |
| O141A | `oracle-insert-first-grouped-when-else-branches` | `INSERT FIRST` 单个 `WHEN/ELSE` 下多个 `INTO` | `branch_kind=when/else`，deparse 保留同组 `INTO`，避免 `INSERT FIRST` 语义被拆分 |
| O140 | `oracle-insert-all-conditional` | 条件 `INSERT ALL WHEN ... THEN` | `insert_mode=all`、branch condition selector、bind 序号和 source target 关联 |
| O141 | `oracle-insert-all-multiple-into-per-when` | 单个 WHEN 下多个 INTO 分支 | 同一 WHEN 下多个 branch 保留独立分支和 condition selector，ELSE 分支可解析 |
| O142 | `oracle-insert-select-source-fields` | `INSERT ... SELECT` 直接字段来源 | source query 输出字段保持 `kind=field` 和字段归属 |
| O143 | `oracle-insert-select-expression-targets` | `INSERT ... SELECT` 表达式来源 | source query 表达式 target 保持 `kind=expression`，字段路径可见 |
| O144 | `oracle-insert-all-source-field-and-expression-cells` | `INSERT ALL` branch cell 混合 source field 与 expression | direct field cell 使用 `source_target`，表达式 cell 不误标成 field/literal/bind |
| O145 | `oracle-insert-select-union-distinct-literals` | `INSERT ... SELECT ... UNION` literal 来源 | set kind、branch targets、literal values 和 target ordinal 保持稳定 |
| O146 | `oracle-insert-select-intersect-binds` | `INSERT ... SELECT ... INTERSECT` 位置 bind 来源 | set kind、branch targets、bind key/SQL/全局序号保持稳定 |
| O147 | `oracle-insert-select-minus-named-binds` | `INSERT ... SELECT ... MINUS` 命名 bind 来源 | Oracle `MINUS` 公共形态、branch targets、bind key/SQL/全局序号保持稳定 |
| O148 | `oracle-insert-all-schema-qualified-targets` | schema-qualified `INSERT ALL` 目标表 | 每个 branch target relation 保留 schema/table，bind key/SQL/全局序号保持稳定 |
| O149 | `oracle-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | Oracle 字面量 ESCAPE 输出到 `values[].like_escape` |
| O150 | `oracle-not-like-escape-named-bind` | `NOT LIKE :pattern ESCAPE :escape_char` | 命名 pattern bind 与命名 escape bind 保留公开 SQL 和全局序号 |
| O151 | `oracle-like-escape-question-bind` | `LIKE ? ESCAPE ?` | Oracle JDBC 风格位置参数的 ESCAPE 结构化输出 |
| O152 | `oracle-like-escape-expression` | `LIKE :pattern ESCAPE UPPER('!')` | ESCAPE 为表达式时输出 `like_escape.kind=expression` |
| O153 | `oracle-derived-like-escape-literal` | 派生表外层 `LIKE ... ESCAPE` | 派生表字段归属下的 LIKE ESCAPE 输出保持稳定 |
| O154 | `oracle-like-without-explicit-escape` | `LIKE :pattern` | 无显式 ESCAPE 时不输出 `like_escape` |
| O155 | `oracle-p3-update-alias-qualified-assignment` | `UPDATE ... x SET x.email = :1` | alias-qualified assignment target 暴露真实列 `email`，并保留 RHS bind selector |
| O156 | `oracle-p3-update-multiple-alias-qualified-assignments` | 多个 `x.column = :bind` 赋值项 | 多 assignment 均输出真实目标列，WHERE bind 正常归属 |
| O157 | `oracle-p3-update-from-source-field` | `UPDATE ... SET name = s.name FROM src s` | assignment RHS 暴露 `kind=field`、`source_field` 和 WHERE field-to-field 谓词 |
| O158 | `oracle-p3-update-schema-qualified-alias-target` | schema-qualified UPDATE target | schema/table/alias 保留，assignment 目标列不误报为 alias |
| O159 | `oracle-p3-update-scalar-subquery-predicate` | UPDATE + scalar subquery | 外层字段和内层 predicate field/value/operator 均进入 query_graph |
| O160 | `oracle-p3-delete-exists-correlated-predicate` | DELETE + correlated EXISTS | AND 谓词树、field-to-field correlation 和 literal selector 保持结构化 |
| O161 | `oracle-p3-select-or-predicate-and-order-by` | SELECT + OR + ORDER BY | OR 两侧谓词保留，ORDER BY 字段不污染输出 target lineage |
| O162 | `oracle-p3-insert-all-independent-branches` | `INSERT ALL` 多 branch | branch target relation、target columns、rows 和 bind cell selector 独立输出 |
| O163 | `oracle-p3-merge-update-source-target-lineage` | MERGE matched UPDATE | `s.email` assignment 关联 source field 和 source target |
| O164 | `oracle-p3-merge-insert-source-target-lineage` | MERGE not matched INSERT | INSERT cell `s.email` 关联 source field 和 source target |
| O165 | `oracle-p3-select-distinct-base-field-lineage` | SELECT DISTINCT direct field | DISTINCT 不改变 base field pass-through lineage |
| O166 | `oracle-p3-select-alias-order-by-lineage` | SELECT alias + ORDER BY | 输出 alias 保留，ORDER BY 字段独立归属 |
| O167 | `oracle-p3-select-star-rowid-lineage` | qualified star + ROWID | `x.*` 和 `x.ROWID` 分别结构化，ROWID 不污染 star lineage |
| O168 | `oracle-p3-update-full-alias-qualified-crypto-shape` | alias-qualified UPDATE + scalar subquery | 多 protected-column 赋值和子查询 predicate 的 P3 综合形态 |
| O169 | `oracle-regexp-like-function-predicate` | `REGEXP_LIKE(name, :pat)` | 函数谓词复用 `fields/values/predicates` 输出字段、bind 和 expression predicate |
| OU015 | `oracle-database-link` | `table@database_link` | 远程对象引用基础形态，View JSON 输出 `link` |
| O170 | `oracle-database-link-schema-alias-bind` | `schema.table@link alias` + bind | schema/table/alias/link 和 bind 归属 |
| O171 | `oracle-database-link-update-target` | `UPDATE table@link ...` | DML target 的 database link 保留 |
| O172 | `oracle-database-link-insert-target` | `INSERT INTO table@link ...` | INSERT target 的 database link 保留 |
| O173 | `oracle-database-link-delete-target` | `DELETE FROM table@link ...` | DELETE target 的 database link 保留 |
| O174 | `oracle-database-link-quoted-identifiers` | `"TABLE"@"LINK"` | quoted identifier 形态的 database link 保留 |
| OU014 | `oracle-create-synonym` | `CREATE SYNONYM u FOR users` | Oracle synonym 创建语句 |
| O175 | `oracle-create-public-synonym` | `CREATE OR REPLACE PUBLIC SYNONYM ...` | Oracle public synonym 创建语句 |
| O176 | `oracle-drop-synonym` | `DROP SYNONYM ... FORCE` | Oracle synonym 删除语句 |
| OU016 | `oracle-explain-plan` | `EXPLAIN PLAN FOR SELECT ...` | Oracle explain plan 语句保留 |
| O177 | `oracle-explain-plan-into` | `EXPLAIN PLAN SET STATEMENT_ID ... INTO ... FOR SELECT ...` | Oracle explain plan 带计划表形态 |
| O178 | `oracle-union-all-three-branch-scope` | 显式分组的三分支 `UNION ALL` | 两级集合拓扑、分支顺序和 literal selector 保持稳定 |
| O179 | `oracle-grouped-union-all-intersect` | 显式分组的 `UNION ALL` 与 `INTERSECT` | 分组边界、运算符类型和所选分支 patch 后反解析保持稳定 |
| O180 | `oracle-union-all-root-cte-scope` | 根级 CTE 跨 `UNION ALL` 分支可见 | 两个分支均解析到同一 CTE source block |
| O181 | `oracle-union-all-qualified-table-bypasses-cte` | 与 CTE 同名的 schema-qualified 及 database-link 基表 | `app.src` 与 `src@remote_db` 均保持 base relation，不误解析为 CTE |
| O182 | `oracle-correlated-union-all-subquery-scope` | 相关子查询内的 `UNION ALL` | 两个集合分支分别保留本地 relation，并解析到外层 `o` relation |
| O183 | `oracle-upper-reverse-bind-expression-predicate` | `:v = UPPER(SECRET)` | expression predicate 通过 `right_field` 引用 `SECRET`，仅输出比较左侧 bind value |
| O184 | `oracle-in-subquery-named-bind-membership` | `ID IN (SELECT USER_ID ... STATUS = :status)` | 外层 membership 字段和 `IN` predicate 与内层 block、target、过滤字段及命名 bind 分离归属；6 个 patch 覆盖两层字段、关系、target、插入 target 与 bind |
| O185 | `oracle-direct-bind-null-test` | `:STATUS IS NOT NULL AND DELETED_AT IS NULL` | 直接 bind NULL 测试输出 expression predicate 并仅引用真实命名 bind；字段 NULL 测试输出 comparison 且不生成 NULL value，AND 顺序及 4 个 patch 精确验证 |
| O186 | `oracle-nested-select-target-multi-replace-middle` | 派生表内层三输出项 SELECT 的中间项替换为三个双引号输出项 | replacement 仅在内层 target list 原位置展开，内外 block、relation 和 target 顺序保持正确；独立 insert patch 验证内层列表位置 |
| O187 | `oracle-merge-update-compound-rhs` | MERGE UPDATE 分支中的复合赋值右值 | 分支 assignment 通过 `rhs_fields` 和 `rhs_values` 归属 source field、位置参数与 literal；source alias 保持稳定，并精确验证 MERGE assignment 替换与插入 |
| O188 | `oracle-merge-insert-structured-pair-rewrite` | MERGE INSERT 目标列与 VALUES cell 结构化改写：三列来源关系、目标列与完整 cell 定位、列值对成对插入和删除，以及标识符形式与未修改 SQL 原文保留 |
| O189 | `oracle-insert-returning-rowid-into-bind` | `INSERT ... VALUES ... RETURNING ROWID INTO :NAV_ROWID` | 单个 `ROWID` pseudo result target 通过 `sink_value` 关联单个冒号宿主绑定变量；VALUES cell、返回 target 和输出绑定变量的 replace patch 均保持精确反解析 |
| O190 | `oracle-update-returning-rowid-into-bind` | `UPDATE ... RETURNING ROWID INTO :NAV_ROWID` | `target_after` 中的单个 `ROWID` pseudo target 关联单个冒号宿主绑定变量；assignment、返回 target 和输出绑定变量的 replace patch 均保持精确反解析 |
| O191 | `oracle-delete-returning-rowid-into-bind` | `DELETE ... RETURNING ROWID INTO :NAV_ROWID` | `target_before` 中的单个 `ROWID` pseudo target 关联单个冒号宿主绑定变量；条件值、返回 target 和输出绑定变量的 replace patch 均保持精确反解析 |
| O192 | `oracle-merge-update-where-delete-where-conditional-insert` | matched UPDATE 同时含 action `WHERE` 和附属 `DELETE WHERE`，后续为带条件的 INSERT | UPDATE 分支同时输出 `condition_selector` 与 `delete_condition_selector`，DELETE 条件仍归属同一 UPDATE 分支；5 个独立 patch 覆盖 assignment 替换/插入以及三类条件值替换 |
| O193 | `oracle-merge-delete-where-updated-target-value` | matched UPDATE 后附属 `DELETE WHERE t.STATUS = 'CLOSED'` | DELETE 条件按更新后的 target 值建模，不生成独立 DELETE action；3 个独立 patch 覆盖 assignment 替换/插入与 DELETE 条件值替换 |

## ROWNUM 谓词语义回归

以下 5 条最终用例验证 `ROWNUM` 仅作为伪列表达式参与谓词，不进入 `query_graph.fields`。谓词保留原始运算符并指向对侧 literal 或 bind value；组合条件同时精确保留布尔树、block 和 DML 归属。这些用例不产生 session 投影。

| ID | 用例 | SQL 形态 | 覆盖点 |
| --- | --- | --- | --- |
| `O-RN001` | `oracle-rownum-and-named-bind` | 普通字段条件与 `ROWNUM <= :limit` | `AND` 布尔树、无 field 的 ROWNUM expression predicate 和命名 bind 位置 |
| `O-RN002` | `oracle-rownum-derived-order-by-limit` | 派生表 `ORDER BY` + 外层 `ROWNUM < 11` | 内外 block、派生 relation、星号来源和 literal predicate 归属 |
| `O-RN003` | `oracle-rownum-right-operand-equality` | `1 = ROWNUM` | ROWNUM 位于比较右侧时保留原始运算符和左侧 literal selector |
| `O-RN004` | `oracle-rownum-greater-than-literal` | `ROWNUM > 1` | 大于运算符和对侧 literal 的 expression predicate |
| `O-RN005` | `oracle-delete-rownum-batch-limit` | `DELETE ... expired = 1 AND ROWNUM <= :batch_size` | DELETE 目标、`AND` 布尔树、普通字段谓词及 ROWNUM bind predicate 归属 |

## 层次查询回归

以下 4 条 final 用例定义 Oracle 层次查询边界。`START WITH` 必须位于 `CONNECT BY` 之前；字段、值和谓词进入既有 Query Graph 数组，层次伪列、`PRIOR`、`NOCYCLE` 与 `CONNECT_BY_ROOT` 使用公共 View 字段表达。每条用例包含 5 个独立 patch，合计 20 个，其中 16 个 `replace`、4 个 `insert_column`。

| ID | 用例 | SQL 形态 | 覆盖点 |
| --- | --- | --- | --- |
| O194 | `oracle-hierarchical-basic-level-bind` | `START WITH` 命名 bind + `CONNECT BY PRIOR` + `LEVEL` | `start_with` / `connect_by` clause、relationless `LEVEL` pseudo target、`PRIOR` 字段 occurrence 和 bind 归属 |
| O195 | `oracle-hierarchical-compound-connect-where-depth` | `WHERE` + `START WITH` + 复合 `CONNECT BY` | WHERE、START WITH、CONNECT BY 的语义遍历顺序，字段比较、`LEVEL` 深度条件和命名 bind |
| O196 | `oracle-hierarchical-connect-by-root` | `CONNECT_BY_ROOT employee_id` + `LEVEL` | expression target 保持不变，底层字段通过 `operator/CONNECT_BY_ROOT/arg_index=0` 的 `target_path` 归属 |
| O197 | `oracle-hierarchical-nocycle-pseudocolumns` | `CONNECT BY NOCYCLE` + `CONNECT_BY_ISLEAF` + `CONNECT_BY_ISCYCLE` | CONNECT BY 根 predicate 的 `nocycle` 标记、两个 relationless pseudo target 及 field 回指 |

## RETURNING INTO 多结果对回归

Oracle `INSERT`、`UPDATE`、`DELETE` 的 `RETURNING ... INTO` 支持 `N >= 1` 个返回 target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 一一配对。O189 至 O191 验证单对基线；以下 3 条 final 用例各验证 8 对结果及一次成对 `insert_column`：同一个 patch 在相同 ordinal 同时插入 target 和 receiver，使结果扩展为 9 对，不允许拆成单侧插入。View 使用一个 `kind = "sink"` 通道，每个 target 的 `sink_value` 指向对应 ordinal 的输出 bind。

| ID | 用例 | DML | 验证重点 |
| --- | --- | --- | --- |
| O198 | `oracle-insert-returning-eight-target-bind-pairs` | INSERT | 8 对结果；在 index 0 成对插入 `tenant_id` / `:out_tenant_id`，验证头部 9 对及 ordinal 对齐 |
| O199 | `oracle-update-returning-eight-target-bind-pairs` | UPDATE | 8 对结果；在 index 4 成对插入 `postal_code` / `:out_postal_code`，验证中部 9 对及 ordinal 对齐 |
| O200 | `oracle-delete-returning-eight-target-bind-pairs` | DELETE | 8 对结果；在 index 8 成对插入 `tenant_id` / `:out_tenant_id`，验证尾部 9 对及 ordinal 对齐 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。未纳入该可执行夹具的语法边界由 `doc/oracle_official_syntax_coverage.csv` 维护。

`RETURNING ... INTO` 不接受 `BULK COLLECT`、非冒号 bind receiver 或 target/receiver 数量不等的输入；成对 `insert_column` 必须在同一个 patch 中同时插入两侧。

## 维护要求

- 新增 Oracle 支持项必须同步更新 `tests/cases/oracle_dialect_input.json`、本矩阵和可执行回归测试。
- 未纳入可执行夹具的语法不得在本矩阵中登记为已验证用例。
