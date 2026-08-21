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
| `MODEL_REQUIRED` | 需要 Oracle 专用模型，通常涉及 PL/SQL、表变换或闪回。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

## 统计结果

| 状态 | 语法组数 | 占全部 47 组 |
| --- | ---: | ---: |
| `CURRENT` | 38 | 80.85% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 4.26% |
| `MODEL_REQUIRED` | 7 | 14.89% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 47 组。其中 38 组归类为 `CURRENT`，另有 9 组未完整覆盖。

| 未完整覆盖分类 | 语法组数 | 占未完整覆盖 9 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 22.22% |
| `MODEL_REQUIRED` | 7 | 77.78% |

## 结论

Oracle `MERGE` 的 `CURRENT` 边界包含 matched UPDATE 的 action `WHERE`、同一 UPDATE 分支上的附属 `DELETE WHERE`，以及带条件的 not-matched INSERT。`insert_column` 支持 column-only、value-only、paired 三态；省略目标列列表的 INSERT 仍输出目标列表 selector，并可分别物化列列表、在保持省略时追加 VALUES cell 或替换现有 cell，显式列表继续支持 paired 添加。可执行矩阵使用 3 条用例和 11 个独立 patch 验证这些边界，其中省略列表用例的 3 个 patch 独立执行；最终列值等宽校验与失败整批回滚由核心 API 单元测试验证。

Oracle 层次查询的 `CURRENT` 边界包括 `START WITH`、`CONNECT BY`、一元 `PRIOR`、`LEVEL`、`CONNECT_BY_ROOT`、`CONNECT_BY_ISLEAF`、`CONNECT_BY_ISCYCLE`、`NOCYCLE` 和复合层次条件；源文本必须使用 `START WITH` 在 `CONNECT BY` 之前的顺序。可执行矩阵包含 4 条 `final` 用例和 20 个独立 patch；`CONNECT BY` 在 `START WITH` 之前的反向子句顺序不在当前范围。

`RETURNING ... INTO` 已覆盖 `INSERT`、`UPDATE`、`DELETE` 的 `N >= 1` 个返回 target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 配对；不接受 `BULK COLLECT`、非冒号 bind receiver 或数量不等的两侧列表。成对 `insert_column` 使用同一个 patch 同时插入 target 和 receiver，不拆分单侧操作。O198 至 O200 分别验证 8 对结果及头部、中部、尾部插入后的 9 对结果；整个 Oracle 可执行夹具包含 253 条 `final` 用例和 857 个独立 patch。Oracle 当前剩余未完整覆盖项主要属于无法安全映射到共享 AST 的专属语义。`SYNONYM` 和 `EXPLAIN PLAN FOR` 已覆盖基础语句解析、keyword 和反解析；完整对象属性或执行计划语义需要 Oracle 专用模型。
