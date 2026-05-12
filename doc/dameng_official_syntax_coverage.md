# 达梦官方语法覆盖统计

本文件记录达梦方言相对于达梦官方 DM_SQL 文档的覆盖统计。完整逐条清单见 [dameng_official_syntax_coverage.csv](dameng_official_syntax_coverage.csv)。

## 统计来源

- [达梦 DM_SQL 数据查询语句](https://eco.dameng.com/document/dm/zh-cn/pm/check-phrases.html)
- [达梦 DM_SQL 数据的插入、删除和修改](https://eco.dameng.com/document/dm/zh-cn/pm/insertion-deletion-modification)
- [达梦 DM_SQL 数据定义语句](https://eco.dameng.com/document/dm/zh-cn/pm/definition-statement.html)
- [达梦 DMSQL 程序中的 SQL 语句](https://eco.dameng.com/document/dm/zh-cn/pm/dm8_sql-sql-statement)
- 统计日期：2026-05-11

统计范围固定为当前达梦方言转换层涉及的官方语法组，包括查询、DML、常见 DDL、事务语句、表达式、权限语句和达梦专属语义。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前达梦方言已有代表性可执行覆盖，或可由当前 AST 安全表达。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要达梦专用模型。 |
| `MODEL_REQUIRED` | 需要达梦专用模型，通常涉及层级查询、表变换、DMSQL 程序单元、返回宿主变量、闪回或远程对象引用。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 语法组数 | 占全部 35 组 |
| --- | ---: | ---: |
| `CURRENT` | 26 | 74.29% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 1 | 2.86% |
| `MODEL_REQUIRED` | 8 | 22.86% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 35 组。其中当前已覆盖 26 组，未覆盖 9 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 9 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 1 | 11.11% |
| `MODEL_REQUIRED` | 8 | 88.89% |

## 结论

达梦当前已覆盖常用查询、DML、DDL、事务、权限和当前模式切换语句。剩余缺口主要是需要保留达梦专属语义的查询模型、程序单元和远程对象引用，不应降级为 PostgreSQL 兼容形态。
