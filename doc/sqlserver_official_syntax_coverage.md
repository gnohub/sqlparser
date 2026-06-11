# SQL Server 官方语法覆盖统计

本文件记录 SQL Server 方言相对于 Microsoft 官方 Transact-SQL Reference 的覆盖统计。完整逐条清单见 [sqlserver_official_syntax_coverage.csv](sqlserver_official_syntax_coverage.csv)。

## 统计来源

- [Microsoft Learn: Transact-SQL Reference](https://learn.microsoft.com/en-us/sql/t-sql/language-reference)
- [MicrosoftDocs/sql-docs: `docs/t-sql`](https://github.com/MicrosoftDocs/sql-docs/tree/live/docs/t-sql)
- 统计日期：2026-06-11

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
| `CURRENT` | 440 | 47.11% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 116 | 12.42% |
| `MODEL_REQUIRED` | 339 | 36.30% |
| `REFERENCE_ONLY` | 39 | 4.18% |

剔除 `REFERENCE_ONLY` 后，官方可实现条目为 895 条。其中当前已覆盖 440 条，未覆盖 455 条。

| 未覆盖分类 | 条目数 | 占未覆盖 455 条 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 116 | 25.49% |
| `MODEL_REQUIRED` | 339 | 74.51% |

当前 `HOOK_ONLY` 条目已经全部进入可执行回归。剩余未覆盖条目均需要 SQL Server 专用模型或属于基础形态已覆盖、完整语法仍需扩展模型的混合项。

`MIXED_MODEL` 中已有 94 条基础 case 进入可执行回归，包括数据库、schema、role、application role、user、synonym、type、index、sequence、view、statistics、`SELECT INTO`、基础全文谓词、CTAS、别名、子查询、基础 `ALTER DATABASE`、基础 `ALTER TABLE`、`DROP TYPE`、`DROP USER` 公开形态恢复、`CREATE USER` 专属选项、`ALTER USER` 常见选项、`CREATE ROLE AUTHORIZATION`、`ALTER ROLE` 成员/重命名、`ALTER SCHEMA TRANSFER`、`ALTER AUTHORIZATION` 基础形态、`DROP SCHEMA IF EXISTS`、基础表提示和查询提示，以及基础 `SET` 会话/执行环境语句。完整官方语法仍按 `MIXED_MODEL` 统计。

## 按目录统计

| 目录 | `CURRENT` | `HOOK_ONLY` | `MIXED_MODEL` | `MODEL_REQUIRED` | `REFERENCE_ONLY` | 合计 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `statements` | 17 | 0 | 102 | 248 | 1 | 368 |
| `queries` | 17 | 0 | 12 | 9 | 3 | 41 |
| `language-elements` | 63 | 0 | 2 | 47 | 3 | 115 |
| `functions` | 321 | 0 | 0 | 16 | 24 | 361 |
| `data-types` | 17 | 0 | 0 | 19 | 8 | 44 |
| `system-stored-procedures` | 5 | 0 | 0 | 0 | 0 | 5 |

## 结论

SQL Server 方言已经覆盖所有只依赖现有 AST 和方言 hook 即可承载的官方条目。剩余 455 条未覆盖项中，339 条需要 SQL Server 专用模型，116 条属于基础形态可覆盖但完整官方语法仍需专用模型的混合项。
