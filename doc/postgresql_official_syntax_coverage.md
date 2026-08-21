# PostgreSQL 官方语法覆盖统计

本文件记录 PostgreSQL 默认方言相对于 PostgreSQL 官方 SQL Commands 文档的覆盖统计。完整逐条清单见 [postgresql_official_syntax_coverage.csv](postgresql_official_syntax_coverage.csv)。

## 统计来源

- [PostgreSQL 17: SQL Commands](https://www.postgresql.org/docs/17/sql-commands.html)
- [PostgreSQL 17: The SQL Language](https://www.postgresql.org/docs/17/sql.html)
- [PostgreSQL: Supported Features](https://www.postgresql.org/docs/current/features-sql-standard.html)
- [PostgreSQL pgsql-docs: document `N'...'` national character string literal syntax](https://www.postgresql.org/message-id/om3g7p7u3ztlrdp4tfswgulavljgn2fe6u2agk34mrr65dffuu%40cpzlzuv6flko)
统计范围固定为官方 SQL Commands 中与公共 API、View JSON、deparse 和可执行回归测试直接相关的语法组。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前默认方言已有代表性可执行覆盖，或由固定 PostgreSQL 解析内核直接承载。 |
| `HOOK_ONLY` | 解析内核可承载，但当前公共 query_graph 或回归矩阵尚未提供专用覆盖。 |
| `MIXED_MODEL` | 基础语句可解析，完整对象归属、选项或结构化编辑需要扩展公共模型。 |
| `MODEL_REQUIRED` | 需要新增公共模型或专用结构后才能完整支持。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 语法组数 | 占全部 42 组 |
| --- | ---: | ---: |
| `CURRENT` | 41 | 97.62% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 1 | 2.38% |
| `MODEL_REQUIRED` | 0 | 0.00% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 42 组。其中当前已覆盖 41 组，未覆盖 1 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 1 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 1 | 100.00% |
| `MODEL_REQUIRED` | 0 | 0.00% |

## 结论

PostgreSQL `MERGE` 支持独立的 `WHEN MATCHED ... THEN DELETE` action；`insert_column` 支持 column-only、value-only、paired 三态，省略目标列列表的 not-matched INSERT 仍输出目标列表 selector，并可分别物化列列表、在保持省略时追加 VALUES cell 或替换现有 cell。显式列表继续支持 paired 添加。可执行矩阵使用 2 条用例和 6 个独立 patch 验证这些边界，其中省略列表用例的 3 个 patch 独立执行；最终列值等宽校验与失败整批回滚由核心 API 单元测试验证。

PostgreSQL 是默认解析内核方言，当前没有只缺少 hook 或回归覆盖的语法组。剩余缺口为角色、用户、数据库对象管理类语句的完整对象归属和选项模型，需要扩展公共模型。
