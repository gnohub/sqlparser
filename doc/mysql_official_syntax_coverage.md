# MySQL 官方语法覆盖统计

本文件记录 MySQL 方言相对于 MySQL 8.4 Reference Manual 的覆盖统计。完整逐条清单见 [mysql_official_syntax_coverage.csv](mysql_official_syntax_coverage.csv)。

## 统计来源

- [MySQL 8.4 Reference Manual: SQL Statements](https://dev.mysql.com/doc/refman/8.4/en/sql-statements.html)
- [MySQL 8.4 Reference Manual: Language Structure](https://dev.mysql.com/doc/refman/8.4/en/language-structure.html)
统计范围固定为当前 MySQL 方言转换层涉及的官方语法组，包括查询、DML、常见 DDL、事务语句、表达式、类型属性和 MySQL 专属语义。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前 MySQL 方言已有代表性可执行覆盖，或可由当前 AST 安全表达。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要专用模型。 |
| `MODEL_REQUIRED` | 需要 MySQL 专用模型，通常涉及 MySQL 专属 DML 语义、DDL 选项、类型属性或程序单元。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

`UPDATE_JOIN` 的 `CURRENT` 边界包括 JOIN 链和逗号 relation 列表中的多目标 assignment；每个 assignment 目标字段分别关联其写入 relation。多表形态不接受 `ORDER BY` 或 `LIMIT`。

## 统计结果

| 状态 | 语法组数 | 占全部 48 组 |
| --- | ---: | ---: |
| `CURRENT` | 41 | 85.42% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 4.17% |
| `MODEL_REQUIRED` | 5 | 10.42% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 48 组。其中当前已覆盖 41 组，未覆盖 7 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 7 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 28.57% |
| `MODEL_REQUIRED` | 5 | 71.43% |

## 结论

`WITH` CTE 的合法显式列名按 ordinal 投影到 `source_block` 中直接可枚举、连续且不含 `*`/`alias.*` 的 targets，并同步反引号定界状态。SET/递归 branch 保留自身输出，星号不展开，无法建立完整直接 target 对应关系时不推测映射。基础可执行夹具现包含 270 条 `final` 用例和 898 个独立 patch。

MySQL 剩余未覆盖项主要集中在多表 DELETE 完整语义、REPLACE 分区变体、程序对象和管理类语句。已闭环多目标多表 UPDATE、INSERT/UPDATE/DELETE 修饰符、REPLACE 基础公开形态，以及 `CREATE TABLE` 列属性、表选项和无查询表达式的分区尾部；部分已覆盖但完整官方语义需要专用模型的为 2 组，占 28.57%；需要 MySQL 专用模型的为 5 组，占 71.43%。

反引号标识符的可执行合同按 relation 的 database、schema、object 名称段及 DML target column 独立保留状态，并覆盖 `INSERT ... SET`、`REPLACE ... SET`；未定界或不存在的段不输出 true 标志。1 条用例和 5 个独立 patch 验证 quoted/unquoted 同名对照及 patch 后重算。夹具中的 MERGE 仅属于项目兼容入口，不作为 MySQL 官方语法覆盖声明。

MySQL 官方 relation DDL 的 `CURRENT` 合同使用 `kind = "ddl"` 根 block 和 `ddl_role = "target"|"reference"`，覆盖 CREATE/ALTER TABLE 的 FK、CREATE INDEX、TRUNCATE、RENAME、多对象 DROP，以及查询支撑型 VIEW/CTAS。查询支撑型 target 通过 `source_block` 指向 SELECT block；DROP target 无 relation selector，同名 quoted/unquoted 分段按精确来源输出。3 条新增 final 用例和 8 个独立 patch 验证这些边界。该证据不自动外推到项目兼容入口。
