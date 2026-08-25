# SQL Server 官方语法覆盖统计

本文件记录 SQL Server 方言相对于 Microsoft 官方 Transact-SQL Reference 的覆盖统计。完整逐条清单见 [sqlserver_official_syntax_coverage.csv](sqlserver_official_syntax_coverage.csv)。

## 统计来源

- [Microsoft Learn: Transact-SQL Reference](https://learn.microsoft.com/en-us/sql/t-sql/language-reference)
- [MicrosoftDocs/sql-docs: `docs/t-sql`](https://github.com/MicrosoftDocs/sql-docs/tree/live/docs/t-sql)
统计范围固定为官方文档仓库中的以下目录：

| 目录 | 条目数 |
| --- | ---: |
| `docs/t-sql/statements` | 368 |
| `docs/t-sql/queries` | 41 |
| `docs/t-sql/language-elements` | 115 |
| `docs/t-sql/functions` | 361 |
| `docs/t-sql/data-types` | 44 |
| `docs/t-sql/system-stored-procedures` | 5 |
| 合计 | 934 |

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前 SQL Server 方言已经具备代表性覆盖，或可由现有核心 AST 直接承载。 |
| `HOOK_ONLY` | 尚未进入可执行回归，但可以通过方言 hook、预处理、后处理或类型/函数映射完成，不需要新增 SQL Server 专用 AST。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要 SQL Server 专用模型。 |
| `MODEL_REQUIRED` | 需要 SQL Server 专用 AST/模型，通常涉及批处理、变量、控制流、过程体、管理语句、安全语句、Service Broker、备份恢复、提示、专用表源或专有 DDL 语义。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 条目数 | 占全部 934 条 |
| --- | ---: | ---: |
| `CURRENT` | 442 | 47.32% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 117 | 12.53% |
| `MODEL_REQUIRED` | 336 | 35.97% |
| `REFERENCE_ONLY` | 39 | 4.18% |

剔除 `REFERENCE_ONLY` 后，官方可实现条目为 895 条。其中当前已覆盖 442 条，未覆盖 453 条。

| 未覆盖分类 | 条目数 | 占未覆盖 453 条 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 117 | 25.83% |
| `MODEL_REQUIRED` | 336 | 74.17% |

当前 `HOOK_ONLY` 条目已经全部进入可执行回归。剩余未覆盖条目均需要 SQL Server 专用模型或属于基础形态已覆盖、完整语法仍需扩展模型的混合项。

`MIXED_MODEL` 中已有 95 条基础 case 进入可执行回归，包括数据库、schema、role、application role、user、synonym、type、index、sequence、view、statistics、`SELECT INTO`、基础全文谓词、CTAS、别名、子查询、基础 `ALTER DATABASE`、基础 `ALTER TABLE`、`DROP TYPE`、`DROP USER` 公开形态恢复、`CREATE USER` 专属选项、`ALTER USER` 常见选项、`CREATE ROLE AUTHORIZATION`、`ALTER ROLE` 成员/重命名、`ALTER SCHEMA TRANSFER`、`ALTER AUTHORIZATION` 基础形态、`DROP SCHEMA IF EXISTS`、基础表提示和查询提示、基础 `SET` 会话/执行环境语句，以及 `IF...ELSE` 分支内的 `BEGIN...END`。完整官方语法仍按 `MIXED_MODEL` 统计。

`OUTPUT` 条目由 33 条成功路径和 10 条错误路径覆盖，包含 `INSERT`、`UPDATE`、`DELETE`、`MERGE`、sink/client 双通道和嵌套 DML。具有显式非空 sink column list 且改写前 target/column 数量相等时，paired `insert_column` 可在同一序号原子插入两侧；原本合法的不等长 `OUTPUT ... INTO` 仍可解析和反解析，但不支持该成对改写。sink relation 的 database/schema/object 方括号状态按段保留，sink column 使用独立 `quoted_identifier`；1 条新增用例和 7 个独立 patch 覆盖四类 DML sink 与 patch 后重算。

普通 relation 同样按 database、schema、object 名称段保留方括号状态，DML target column 独立保留定界状态；未定界或不存在的段不输出 true 标志。另一条新增用例和 7 个独立 patch 覆盖 SELECT、INSERT、UPDATE FROM、DELETE 与 MERGE。

`IF...ELSE` 条目由 36 条成功路径和 9 条错误路径覆盖，包含单语句分支、多语句块、`ELSE IF`、嵌套、条件查询、DML、DDL、事务和语法边界。

`MERGE` 条目包含独立的 `WHEN MATCHED ... THEN DELETE` action；`insert_column` 支持 column-only、value-only、paired 三态，省略目标列列表的 not-matched INSERT 仍输出目标列表 selector，并可分别物化列列表、在保持省略时追加 VALUES cell 或替换现有 cell。显式列表继续支持 paired 添加。2 条可执行用例和 6 个独立 patch 验证这些边界，其中省略列表用例的 3 个 patch 独立执行；最终列值等宽校验与失败整批回滚由核心 API 单元测试验证。

relation DDL 的当前基础合同使用 `kind = "ddl"` 根 block 和 `ddl_role = "target"|"reference"`，覆盖 CREATE/ALTER TABLE 的 FK、CREATE INDEX、TRUNCATE、多对象 DROP、CREATE VIEW 和正式 `SELECT INTO`。查询支撑型 target 通过 `source_block` 指向 SELECT block；DROP target 无 relation selector，同名 quoted/unquoted 分段按精确来源输出。10 条新增 final 用例和 12 个独立 patch 还验证单语句和普通多语句 batch 中的 relation patch 均不给 CREATE INDEX 增加 `USING btree`，并保留 `TRUNCATE TABLE` 公开 surface。该基础入口证据不自动代表兼容入口。

## 按目录统计

| 目录 | `CURRENT` | `HOOK_ONLY` | `MIXED_MODEL` | `MODEL_REQUIRED` | `REFERENCE_ONLY` | 合计 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `statements` | 17 | 0 | 102 | 248 | 1 | 368 |
| `queries` | 18 | 0 | 12 | 8 | 3 | 41 |
| `language-elements` | 64 | 0 | 3 | 45 | 3 | 115 |
| `functions` | 321 | 0 | 0 | 16 | 24 | 361 |
| `data-types` | 17 | 0 | 0 | 19 | 8 | 44 |
| `system-stored-procedures` | 5 | 0 | 0 | 0 | 0 | 5 |

## 结论

SQL Server 方言已经覆盖所有只依赖现有 AST 和方言 hook 即可承载的官方条目。剩余 453 条未覆盖项中，336 条需要 SQL Server 专用模型，117 条属于基础形态可覆盖但完整官方语法仍需扩展模型的混合项。
