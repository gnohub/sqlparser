# MySQL 官方语法覆盖统计

本文件记录 MySQL 方言相对于 MySQL 8.4 Reference Manual 的覆盖统计。完整逐条清单见 [mysql_official_syntax_coverage.csv](mysql_official_syntax_coverage.csv)。

## 统计来源

- [MySQL 8.4 Reference Manual: SQL Statements](https://dev.mysql.com/doc/refman/8.4/en/sql-statements.html)
- [MySQL 8.4 Reference Manual: Language Structure](https://dev.mysql.com/doc/refman/8.4/en/language-structure.html)
- 统计日期：2026-05-08

统计范围固定为当前 MySQL 方言转换层涉及的官方语法组，包括查询、DML、常见 DDL、事务语句、表达式、类型属性和 MySQL 专属语义。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前 MySQL 方言已有代表性可执行覆盖，或可由当前 AST 安全表达。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要专用模型。 |
| `MODEL_REQUIRED` | 需要 MySQL 专用模型，通常涉及 MySQL 专属 DML 语义、DDL 选项、类型属性或程序单元。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 语法组数 | 占全部 44 组 |
| --- | ---: | ---: |
| `CURRENT` | 21 | 47.73% |
| `HOOK_ONLY` | 5 | 11.36% |
| `MIXED_MODEL` | 2 | 4.55% |
| `MODEL_REQUIRED` | 16 | 36.36% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 44 组。其中当前已覆盖 21 组，未覆盖 23 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 23 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 5 | 21.74% |
| `MIXED_MODEL` | 2 | 8.70% |
| `MODEL_REQUIRED` | 16 | 69.57% |

## 结论

MySQL 剩余未覆盖项主要集中在外连接 DML、表选项、类型属性和程序对象。只依赖现有 AST 与 hook 可补齐的为 5 组，占未覆盖项 21.74%；部分已覆盖但完整官方语义需要专用模型的为 2 组，占 8.70%；需要 MySQL 专用模型的为 16 组，占 69.57%。
