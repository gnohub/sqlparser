# SQL Server 官方语法覆盖统计

本文件记录 SQL Server 方言相对于 Microsoft 官方 Transact-SQL Reference 的覆盖统计。完整逐条清单见 [sqlserver_official_syntax_coverage.csv](sqlserver_official_syntax_coverage.csv)。

## 统计来源

- [Microsoft Learn: Transact-SQL Reference](https://learn.microsoft.com/en-us/sql/t-sql/language-reference)
- [MicrosoftDocs/sql-docs: `docs/t-sql`](https://github.com/MicrosoftDocs/sql-docs/tree/live/docs/t-sql)
- 统计日期：2026-05-08

统计范围固定为官方文档仓库中的以下目录：

| 目录 | 条目数 |
| --- | ---: |
| `docs/t-sql/statements` | 368 |
| `docs/t-sql/queries` | 41 |
| `docs/t-sql/language-elements` | 115 |
| `docs/t-sql/functions` | 361 |
| `docs/t-sql/data-types` | 44 |
| 合计 | 929 |

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前 SQL Server 方言已经具备代表性覆盖，或可由现有核心 AST 直接承载。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成，不需要新增 SQL Server 专用 AST。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要 SQL Server 专用模型。 |
| `MODEL_REQUIRED` | 需要 SQL Server 专用 AST/模型，通常涉及批处理、变量、控制流、过程体、管理语句、安全语句、Service Broker、备份恢复、提示、专用表源或专有 DDL 语义。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 条目数 | 占全部 934 条 |
| --- | ---: | ---: |
| `CURRENT` | 192 | 20.56% |
| `HOOK_ONLY` | 235 | 25.16% |
| `MIXED_MODEL` | 82 | 8.78% |
| `MODEL_REQUIRED` | 386 | 41.33% |
| `REFERENCE_ONLY` | 39 | 4.18% |

剔除 `REFERENCE_ONLY` 后，官方可实现条目为 895 条。其中当前已覆盖 192 条，未覆盖 703 条。

| 未覆盖分类 | 条目数 | 占未覆盖 703 条 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 235 | 33.43% |
| `MIXED_MODEL` | 82 | 11.66% |
| `MODEL_REQUIRED` | 386 | 54.91% |

如果按“完整官方语法是否需要扩展自有 SQL Server 模型”统计，`MIXED_MODEL + MODEL_REQUIRED` 为 468 条，占未覆盖条目的 66.57%。只依赖现有 AST 和 hook 即可补齐的为 235 条，占 33.43%。

## 按目录统计

| 目录 | `CURRENT` | `HOOK_ONLY` | `MIXED_MODEL` | `MODEL_REQUIRED` | `REFERENCE_ONLY` | 合计 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `statements` | 12 | 5 | 59 | 291 | 1 | 368 |
| `queries` | 15 | 0 | 10 | 13 | 3 | 41 |
| `language-elements` | 52 | 0 | 13 | 47 | 3 | 115 |
| `functions` | 94 | 227 | 0 | 16 | 24 | 361 |
| `data-types` | 14 | 3 | 0 | 19 | 8 | 44 |
| `system-stored-procedures` | 5 | 0 | 0 | 0 | 0 | 5 |

## 结论

SQL Server 剩余未覆盖条目中，严格不能只靠现有 AST 解决的为 386 条，占 54.91%。另有 82 条属于基础形态可 hook、完整官方语法需要专用模型的混合项。按完整官方语法覆盖口径计算，需要 SQL Server 专用模型的条目合计 468 条，占 66.57%。
