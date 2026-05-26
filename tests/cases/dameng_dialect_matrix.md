# 达梦方言用例矩阵

本文件记录达梦方言转换层的回归用例。可执行夹具为 `tests/cases/dameng_dialect_input.json`，单元测试 `tests/unit/test_dameng_dialect_case_matrix.c` 会逐条验证解析结果、SQL View JSON、反解析输出和错误码。

## 支持用例

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| D001 | `SELECT` + `NVL` + 命名 bind | 达梦兼容 `:name` bind 转换与还原 |
| D002 | `SET SCHEMA` | 当前 schema 会话上下文切换 |
| D003 | `ALTER SESSION SET CURRENT_SCHEMA` | schema 会话切换语句 |
| D004 | `MINUS` | 达梦 `MINUS` 与核心集合运算双向转换 |
| D005 | `LIMIT n OFFSET n` | 达梦分页基础形态 |
| D006 | `LIMIT offset,n` | 逗号分页转换为核心分页结构 |
| D007 | `SELECT TOP n` | `TOP` 基础形态转换 |
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
| D029 | `SELECT TOP offset,count` | `TOP` offset/count 形态转换为核心分页结构 |
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

## 明确不支持用例

以下语法具有达梦或兼容模式下的专属语义，当前不会尝试映射为 PostgreSQL AST。转换层返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误，避免生成语义不可靠的 SQL。

| ID | 用例 | 原因 |
| --- | --- | --- |
| DU001 | `CONNECT BY` | 层级查询语义需要专用查询模型 |
| DU002 | `PIVOT` | 表变换语义需要专用查询模型 |
| DU003 | `RETURNING ... INTO` | 返回目标和宿主变量语义不等价 |
| DU004 | DMSQL block | 超出 SQL 语句转换范围 |
| DU005 | `TOP ... PERCENT` | 百分比行数语义不能降级为普通 `LIMIT` |
| DU006 | `INSERT ALL` | 多表插入语义不等价 |
| DU007 | database link | 远程对象引用语义 |
| DU008 | national q-quoted 字符串 | national 字符集语义当前不做静默降级 |
| DU009 | 其他 `ALTER SESSION` 参数 | 除当前 schema 切换外的会话参数 |
| DU010 | `CREATE PROCEDURE` | DMSQL 程序单元 |
| DU011 | `TOP ... WITH TIES` | ties 语义不能降级为普通 `LIMIT` |
| DU012 | `ALTER SESSION SET CONTAINER` | 达梦当前不支持 container 会话语义 |

## 维护要求

- 新增达梦支持项必须同步更新 `tests/cases/dameng_dialect_input.json`、本矩阵和可执行回归测试。
- 无法保证语义等价的达梦专有语法必须返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误。
