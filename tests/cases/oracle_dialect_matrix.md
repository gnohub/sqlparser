# Oracle 方言用例矩阵

本文件记录 Oracle 方言转换层的回归用例。可执行夹具为 `tests/cases/oracle_dialect_input.json`，单元测试 `tests/unit/test_oracle_dialect_case_matrix.c` 会逐条验证解析结果、SQL View JSON、反解析输出和错误码。

## 支持用例

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| O001 | `SELECT` + `NVL` + 命名 bind | Oracle `:name` bind 转换与还原 |
| O002 | q-quoted 字符串 | `q'[...]'` 转换为安全字符串字面量 |
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
| O042 | unsupported 关键字注释 | 注释中的 `CONNECT BY` 不触发 unsupported |
| O043 | `ALTER SESSION SET CURRENT_SCHEMA` | 当前 schema 会话上下文切换 |
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

## 明确不支持用例

以下语法具有 Oracle 专有语义，当前不会尝试映射为 PostgreSQL AST。转换层返回 `SQLPARSER_STATUS_UNSUPPORTED`，避免生成语义不可靠的 SQL。

| ID | 用例 | 原因 |
| --- | --- | --- |
| O003 | national q-quoted 字符串 | national 字符集语义当前不做静默降级 |
| OU001 | `CONNECT BY` | 层级查询语义不等价 |
| OU002 | `(+)` | 旧式外连接语义不等价 |
| OU003 | `INSERT ALL` | 多表插入语义不等价 |
| OU004 | `RETURNING ... INTO` | 返回目标和宿主变量语义不等价 |
| OU005 | PL/SQL block | 超出 SQL 语句转换范围 |
| OU006 | `CREATE PROCEDURE` | PL/SQL 单元 |
| OU007 | `CREATE PACKAGE` | PL/SQL 单元 |
| OU008 | `PIVOT` | Oracle 表变换语义 |
| OU009 | `UNPIVOT` | Oracle 表变换语义 |
| OU010 | `MODEL` | Oracle model clause |
| OU011 | flashback query | Oracle 闪回查询语义 |
| OU012 | `MATCH_RECOGNIZE` | 行模式识别语义 |
| OU013 | 其他 `ALTER SESSION` 参数 | 除 `CURRENT_SCHEMA` 和 `CONTAINER/SERVICE` 外的会话参数 |
| OU014 | `CREATE SYNONYM` | Oracle 同义词对象 |
| OU015 | database link | 远程对象引用语义 |
| OU016 | `EXPLAIN PLAN FOR` | Oracle explain plan 输出语义 |
| OU017 | `CONNECT_BY_ROOT` | 层级查询相关表达式 |
| OU018 | `INSERT FIRST` | 条件多表插入语义 |

## 维护要求

- 新增 Oracle 支持项必须同步更新 `tests/cases/oracle_dialect_input.json`、本矩阵和可执行回归测试。
- 无法保证语义等价的 Oracle 专有语法必须返回 `SQLPARSER_STATUS_UNSUPPORTED`。
