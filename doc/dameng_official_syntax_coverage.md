# 达梦官方语法覆盖统计

本文件记录达梦方言相对于达梦官方 DM_SQL 文档的覆盖统计。完整逐条清单见 [dameng_official_syntax_coverage.csv](dameng_official_syntax_coverage.csv)。

## 统计来源

- [达梦 DM_SQL 数据查询语句](https://eco.dameng.com/document/dm/zh-cn/pm/check-phrases.html)
- [达梦 DM_SQL 数据的插入、删除和修改](https://eco.dameng.com/document/dm/zh-cn/pm/insertion-deletion-modification)
- [达梦 DM_SQL 数据定义语句](https://eco.dameng.com/document/dm/zh-cn/pm/definition-statement.html)
- [达梦 DMSQL 程序中的 SQL 语句](https://eco.dameng.com/document/dm/zh-cn/pm/dm8_sql-sql-statement)
统计范围固定为当前达梦方言转换层涉及的官方语法组，包括查询、DML、常见 DDL、事务语句、表达式、权限语句和达梦专属语义。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前达梦方言已有代表性可执行覆盖，或可由当前 AST 安全表达。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要达梦专用模型。 |
| `MODEL_REQUIRED` | 需要达梦专用模型，通常涉及层级查询、表变换、DMSQL 程序单元或闪回。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

`RETURNING_INTO` 的 `CURRENT` 边界为 `INSERT`、`DELETE` 的 `RETURNING <单个表达式> INTO <单个冒号宿主绑定变量>`，以及 `UPDATE` 的 `RETURN <单个表达式> INTO <单个冒号宿主绑定变量>`。该边界不包含多个返回 target、多个 `INTO` 宿主绑定变量或 `BULK COLLECT`。

## 统计结果

| 状态 | 语法组数 | 占全部 38 组 |
| --- | ---: | ---: |
| `CURRENT` | 32 | 84.21% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 6 | 15.79% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 38 组。其中当前已覆盖 32 组，未覆盖 6 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 6 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 6 | 100.00% |

## 结论

达梦 `MERGE` 的 `CURRENT` 边界包含 matched UPDATE 的 action `WHERE` 以及归属同一 UPDATE 分支的附属 `DELETE WHERE`。可执行矩阵使用 1 条用例和 3 个独立 patch 验证该边界。

达梦当前已覆盖常用查询、DML、DDL、事务、权限、`SET SCHEMA`、代表性会话参数设置语句、远程对象引用基础形态，以及上述单表达式、单宿主绑定变量的 `RETURN`/`RETURNING ... INTO` 形态。其余 6 个语法组依赖达梦专属查询模型或程序单元语义，当前不纳入 PostgreSQL 兼容转换。
