# PostgreSQL 官方语法覆盖统计

本文件记录 PostgreSQL 默认方言相对于 PostgreSQL 官方 SQL Commands 文档的覆盖统计。完整逐条清单见 [postgresql_official_syntax_coverage.csv](postgresql_official_syntax_coverage.csv)。

## 统计来源

- [PostgreSQL 17: SQL Commands](https://www.postgresql.org/docs/17/sql-commands.html)
- [PostgreSQL 17: The SQL Language](https://www.postgresql.org/docs/17/sql.html)
- 统计日期：2026-05-08

统计范围固定为官方 SQL Commands 中与公共 API、SQL View JSON、deparse 和可执行回归测试直接相关的语法组。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前默认方言已有代表性可执行覆盖，或由固定 PostgreSQL 解析内核直接承载。 |
| `HOOK_ONLY` | 解析内核可承载，但当前公共 SQL View 或回归矩阵尚未提供专用覆盖。 |
| `MIXED_MODEL` | 基础语句可解析，完整对象归属、选项或结构化编辑需要扩展公共模型。 |
| `MODEL_REQUIRED` | 需要新增公共模型或专用结构后才能完整支持。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 语法组数 | 占全部 41 组 |
| --- | ---: | ---: |
| `CURRENT` | 36 | 87.80% |
| `HOOK_ONLY` | 4 | 9.76% |
| `MIXED_MODEL` | 1 | 2.44% |
| `MODEL_REQUIRED` | 0 | 0.00% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 41 组。其中当前已覆盖 36 组，未覆盖 5 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 5 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 4 | 80.00% |
| `MIXED_MODEL` | 1 | 20.00% |
| `MODEL_REQUIRED` | 0 | 0.00% |

## 结论

PostgreSQL 是默认解析内核方言，当前缺口主要不是语法解析能力，而是少量语句在公共 SQL View、selector 或回归矩阵中的专用覆盖。需要扩展公共模型的条目为 1 组。
