# Oracle 官方语法覆盖统计

本文件记录 Oracle 方言相对于 Oracle Database SQL Language Reference 的覆盖统计。完整逐条清单见 [oracle_official_syntax_coverage.csv](oracle_official_syntax_coverage.csv)。

## 统计来源

- [Oracle Database 23ai SQL Language Reference: Types of SQL Statements](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/Types-of-SQL-Statements.html)
- [Oracle Database 23ai SQL Language Reference: SELECT](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/SELECT.html)
- [Oracle Database 23ai SQL Language Reference: ALTER SESSION](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/ALTER-SESSION.html)
统计范围固定为当前 Oracle 方言转换层涉及的官方语法组，包括查询、DML、常见 DDL、事务语句、表达式、权限语句和 Oracle 专属语义。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前 Oracle 方言已有代表性可执行覆盖，或可由当前 AST 安全表达。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要 Oracle 专用模型。 |
| `MODEL_REQUIRED` | 需要 Oracle 专用模型，通常涉及层级查询、PL/SQL、表变换或闪回。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 语法组数 | 占全部 47 组 |
| --- | ---: | ---: |
| `CURRENT` | 37 | 78.72% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 4.26% |
| `MODEL_REQUIRED` | 8 | 17.02% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 47 组。其中 37 组归类为 `CURRENT`，另有 10 组未完整覆盖。

| 未完整覆盖分类 | 语法组数 | 占未完整覆盖 10 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 20.00% |
| `MODEL_REQUIRED` | 8 | 80.00% |

## 结论

`RETURNING ... INTO` 已覆盖 `INSERT`、`UPDATE`、`DELETE` 的单个返回 target 与单个冒号宿主绑定变量；多 target、多宿主绑定变量和 `BULK COLLECT` 不在当前范围内。Oracle 当前剩余未完整覆盖项主要属于无法安全映射到共享 AST 的专属语义。`SYNONYM` 和 `EXPLAIN PLAN FOR` 已覆盖基础语句解析、keyword 和反解析；完整对象属性或执行计划语义需要 Oracle 专用模型。
