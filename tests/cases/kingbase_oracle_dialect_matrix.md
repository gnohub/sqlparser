# KingbaseES Oracle 兼容模式统一入口用例矩阵

## 基线与范围

- 产品基线：KingbaseES `V008R006C009B0014` 与 V9R2C12 Oracle 兼容版的语法并集。
- 对外入口统一为 `kingbase-oracle`，不输出版本字段，也不按版本分派。
- 夹具：`tests/cases/kingbase_oracle_dialect_input.json`。
- Runner：`tests/unit/test_kingbase_oracle_dialect_case_matrix.c`。
- 当前包含 43 条 `status = "final"` 用例和 157 个独立 patch。
- 用例仅复用既有 Oracle/Base 夹具中已有的 View、selector、patch 和反解析期望；未纳入缺少 V8/V9 官方依据或无法精确复用既有期望的语法。

官方基线：

- [V8R6C9B14 手册下载](https://help.kingbase.com.cn/v8/download.html)
- [V8 Oracle 兼容性说明](https://help.kingbase.com.cn/v8/PDF/KingbaseES%E4%B8%8EOracle%E7%9A%84%E5%85%BC%E5%AE%B9%E6%80%A7%E8%AF%B4%E6%98%8E.pdf)
- [V9 Oracle SQL 兼容性说明](https://help.kingbase.com.cn/v9/development/develop-transfer/kes-vs-oracle/kes-vs-oracle-3.html)
- [V9R2 Oracle 兼容版 INSERT](https://help.kingbase.com.cn/v9.2.10/development/application-develop-guide/reference/oracle/sql/statement/insert.html)
- [V9 Oracle 至 KingbaseES 迁移最佳实践](https://help.kingbase.com.cn/v9/PDF/Oracle%E8%87%B3KingbaseES%E8%BF%81%E7%A7%BB%E6%9C%80%E4%BD%B3%E5%AE%9E%E8%B7%B5.pdf)

## 词法、标识符与绑定变量

官方依据：

- [SQL 基本元素](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html)
- [表达式与占位符表达式](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Expressions.html)
- [Go 接口的 `$N`、`?`、`:NAME` 占位符](https://help.kingbase.com.cn/v8/development/client-interfaces/go/go-3.html)

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| KO002 | `kingbase-oracle-select-bind-nvl` | Oracle 命名 bind、函数参数、literal 与字段归属 |
| KO003 | `kingbase-oracle-q-quoted-string` | `q'[...]'` 界定字符串及原文恢复 |
| KO004 | `kingbase-oracle-national-q-quoted-string` | `nq'{...}'` national 界定字符串 |
| KO020 | `kingbase-oracle-quoted-identifiers` | 双引号表名、列名及 quoted flags |
| KO024 | `kingbase-oracle-insert-question-params` | JDBC `?` 位置占位符 |
| KO025 | `kingbase-oracle-select-positional-bind-pair` | Oracle `:1`、`:2` 位置 bind |
| KO038 | `kingbase-oracle-dollar-quoted-string-global-bind-position` | 美元引用字符串中的伪占位符保护及 `$N` 全局编号 |

## SELECT、CTE、JOIN 与分页

官方依据：

- [查询、子查询、层次查询、DUAL 与 PIVOT/UNPIVOT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html)
- [SELECT、LIMIT/OFFSET/FETCH 与 Oracle 模式 SELECT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)
- [操作符与集合操作](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Operators.html)
- [V8R6 SQL 能力范围](https://help.kingbase.com.cn/v8/development/develop-transfer/transplant-r3/transplant-r3-2.html)

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| KO005 | `kingbase-oracle-minus-set-operator` | Oracle `MINUS` 集合运算 |
| KO006 | `kingbase-oracle-offset-fetch` | `OFFSET ... FETCH NEXT` 分页 |
| KO007 | `kingbase-oracle-rownum-filter` | `ROWNUM` 伪列谓词 |
| KO008 | `kingbase-oracle-join-bind` | JOIN、ON、WHERE 与命名 bind |
| KO017 | `kingbase-oracle-for-update-nowait` | `FOR UPDATE NOWAIT` |
| KO019 | `kingbase-oracle-analytic-row-number` | `ROW_NUMBER() OVER` 分析函数 |
| KO029 | `kingbase-oracle-database-link-schema-alias-bind` | `schema.table@dblink`、别名与 bind |
| KO030 | `kingbase-oracle-union-all-root-cte-scope` | 根级 CTE 跨 `UNION ALL` 分支作用域 |
| KO034 | `kingbase-oracle-cte-explicit-column-set-boundary` | CTE 显式列名按序映射及集合边界 |
| KO037 | `kingbase-oracle-select-limit-dollar-params` | KingbaseES `LIMIT/OFFSET` 与 `$N` bind |
| KO040 | `kingbase-oracle-cte-explicit-column-set-recursive-boundary` | 普通 CTE、`WITH RECURSIVE` 与显式列名 |

## INSERT、UPSERT、MERGE、UPDATE 与 DELETE

官方依据：

- [Oracle 模式 INSERT、INSERT ALL/FIRST、ON CONFLICT 与 RETURNING](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html)
- [MERGE 与 UPDATE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)
- [Oracle SQL/PLSQL 兼容范围](https://help.kingbase.com.cn/v8/PDF/KingbaseES%E4%B8%8EOracle%E7%9A%84%E5%85%BC%E5%AE%B9%E6%80%A7%E8%AF%B4%E6%98%8E.pdf)

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| KO001 | `kingbase-oracle-merge-insert-structured-pair-rewrite` | MERGE `WHEN NOT MATCHED` 目标列和值的结构化 patch |
| KO009 | `kingbase-oracle-insert-values-bind` | 单行 INSERT、目标列、命名 bind 与 literal |
| KO010 | `kingbase-oracle-insert-returning-rowid-into-bind` | `RETURNING ROWID INTO :bind` |
| KO011 | `kingbase-oracle-insert-values-multi-row` | 多行 `VALUES` |
| KO012 | `kingbase-oracle-update-bind` | 多 assignment、WHERE 与命名 bind |
| KO013 | `kingbase-oracle-delete-conditional` | DELETE 目标和复合条件 |
| KO026 | `kingbase-oracle-insert-all` | 无条件 `INSERT ALL` 多目标分支 |
| KO028 | `kingbase-oracle-insert-first` | 条件 `INSERT FIRST` 分支 |
| KO036 | `kingbase-oracle-insert-on-conflict-update` | `ON CONFLICT DO UPDATE`、`EXCLUDED` 与 `RETURNING` |
| KO039 | `kingbase-oracle-on-conflict-assignment-list-contract` | UPSERT assignment 列表插入、替换与删除 |

## DDL 与对象引用

官方依据：

- [CREATE SEQUENCE、SYNONYM、TABLE、VIEW 与 DELETE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html)
- [ALTER TABLE 与 COMMENT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_3.html)
- [CREATE DATABASE LINK、DIRECTORY 与 INDEX](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_4.html)
- [CREATE MATERIALIZED VIEW](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_5.html)

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| KO014 | `kingbase-oracle-create-table` | `NUMBER`、`VARCHAR2`、`DATE` 类型 |
| KO015 | `kingbase-oracle-create-sequence` | Oracle 风格序列选项 |
| KO016 | `kingbase-oracle-create-view` | `CREATE OR REPLACE VIEW` 目标与来源关系 |
| KO021 | `kingbase-oracle-alter-table-add-column` | `ALTER TABLE ... ADD` |
| KO022 | `kingbase-oracle-create-index` | 索引目标表和索引列 |
| KO023 | `kingbase-oracle-create-materialized-view-compatible-form` | 物化视图目标和来源查询 |
| KO027 | `kingbase-oracle-create-synonym` | 私有同义词 |
| KO033 | `kingbase-oracle-ddl-relation-create-table-foreign-key` | quoted schema/table、外键来源 relation 与 DDL 图 |

## 函数、表达式与层次查询

官方依据：

- [KingbaseES SQL 函数](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Function.html)
- [层次查询](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html)
- [层次查询操作符](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Operators.html)

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| KO018 | `kingbase-oracle-decode-sysdate` | `DECODE`、`SYSDATE` 与 literal 参数 |
| KO031 | `kingbase-oracle-hierarchical-basic-level-bind` | `START WITH`、`CONNECT BY PRIOR`、`LEVEL` 与 bind |
| KO032 | `kingbase-oracle-hierarchical-nocycle-pseudocolumns` | `NOCYCLE`、`CONNECT_BY_ISLEAF`、`CONNECT_BY_ISCYCLE` |
| KO035 | `kingbase-oracle-predicate-expression-like-concat-mixed-args` | KingbaseES 多参数 `CONCAT`、嵌套函数和 RHS 参数 patch |

## V9R2 增量覆盖

官方依据：

- [V9 Oracle SQL 兼容性说明](https://help.kingbase.com.cn/v9/development/develop-transfer/kes-vs-oracle/kes-vs-oracle-3.html)
- [V9 Oracle 至 KingbaseES 迁移最佳实践](https://help.kingbase.com.cn/v9/PDF/Oracle%E8%87%B3KingbaseES%E8%BF%81%E7%A7%BB%E6%9C%80%E4%BD%B3%E5%AE%9E%E8%B7%B5.pdf)

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| KO041 | `kingbase-oracle-merge-update-where-delete-where-conditional-insert` | MERGE matched UPDATE、action WHERE、附属 DELETE WHERE 与条件 INSERT |
| KO042 | `kingbase-oracle-p3-update-multiple-alias-qualified-assignments` | 表别名限定的多列 UPDATE assignment |
| KO043 | `kingbase-oracle-database-link-update-target` | DBLink 远程 UPDATE 目标、bind 与 patch |

## 已知边界

- `kingbase-oracle` 接受已纳入的 V8/V9 语法并集；case、View 和 selector 中不携带版本信息。
- `INSERT ALL/FIRST` 仅覆盖官方声明的基础能力；不包含通过 `PARTITION`、`SUBPARTITION`、物化视图或 `@dblink` 作为目标的形态。
- `REPLACE INTO`、多表 DELETE/UPDATE 和 `#` 注释属于 KingbaseES MySQL 兼容模式，不纳入本矩阵。
- V9 官方兼容性表将 DELETE hint 标为不支持，而迁移资料存在不同表述；本批不据此增加 hint case。
- FORCE VIEW、SAMPLE、FLASHBACK、DML 分区和 JSON 扩展虽有官方资料，但现有 Oracle/Base fixture 没有可原样复用的精确期望，本批不新增推测性 case。
- PL/SQL 匿名块、过程、函数、包和触发器属于独立的过程语言边界，不由本批 SQL 结构化用例承诺。
- ksql 的 `\set SQLTERM` 和 `/` 是客户端脚本控制，不作为服务端 SQL 输入。
