# v2.15.3 发布说明

`v2.15.3` 增加 Oracle 与 Dameng 层次查询的结构化解析、Query Graph 与 patch 能力，并支持 Vastbase-SQLServer 已验证的基础 `CONNECT BY` 形态。

## 支持范围

- Oracle 支持 `START WITH ... CONNECT BY [NOCYCLE] ...`，以及 `PRIOR`、`LEVEL`、`CONNECT_BY_ROOT`、`CONNECT_BY_ISLEAF` 和 `CONNECT_BY_ISCYCLE`。Oracle 模式不接受 `CONNECT BY ... START WITH ...` 的反向子句顺序。
- Dameng 支持 `START WITH`、`CONNECT BY [NOCYCLE]`、`PRIOR`、`LEVEL` 和 `CONNECT_BY_ROOT`，并保留 `START WITH ... CONNECT BY ...` 与 `CONNECT BY ... START WITH ...` 两种源文本顺序。
- Vastbase-SQLServer 仅支持基础 `CONNECT BY` 条件。`START WITH`、`PRIOR`、`NOCYCLE` 和 `CONNECT_BY_ROOT` 不在该模式的本次支持范围内。
- 层次上下文限定在当前 SELECT query block；嵌套 SELECT 不继承外层的伪列或 `PRIOR` 状态。带标识符定界符的 `"LEVEL"` 保持普通字段语义。

## Query Graph

- `sqlparser_clause_kind_t` 追加 `SQLPARSER_CLAUSE_KIND_START_WITH = 11` 和 `SQLPARSER_CLAUSE_KIND_CONNECT_BY = 12`。
- `START WITH` 与 `CONNECT BY` 不生成专用 hierarchy 对象；条件中的字段、值和谓词继续进入 `fields[]`、`values[]` 和 `predicates[]`，并使用 `start_with` 或 `connect_by` clause。
- 层次伪列在 `fields[]` 中使用 `pseudo: true`；`PRIOR` 操作数内的字段 occurrence 使用 `prior: true`；`CONNECT BY NOCYCLE` 仅在该子句的根 predicate 上使用 `nocycle: true`。
- `CONNECT_BY_ROOT` target 保持 expression 类型，并通过底层 field 的既有 `target_path` 记录 operator 路径。本版本不增加 target kind、selector 或专用 patch 类型。
- View 中条件相关 occurrence 按 `WHERE`、`START WITH`、`CONNECT BY`、`GROUP BY` / `HAVING`、窗口与 `ORDER BY` 的语义顺序构建。Selector 仍为通用 AST descriptor 定位结果，调用方应将其作为不透明路径使用。

## Patch 与反解析

- relation、field、value 和 SELECT target 改写复用现有 selector 与 patch 类型。
- 可准确定位源码区间的替换使用局部源码 edit，仅替换目标区间。未修改的层次子句顺序、换行、空白、关键字大小写和标识符定界符保持原文。
- Oracle、Dameng 与 Vastbase-SQLServer 的方言预处理保留内部层次关键字的源文本映射；含层次子句的 patch 重新构建 bind 状态时会遍历 `START WITH` 和 `CONNECT BY` 表达式。

## API 与兼容性

- `sqlparser_graph_field_t` 追加 `pseudo` 和 `prior`；`sqlparser_graph_predicate_t` 追加 `nocycle`。
- 本版本不增加公开函数或资源所有权规则。
- 公开枚举追加取值，公开结构体追加字段。C 调用方应使用 2.15.3 头文件重新编译。
- 动态库 ABI 主版本保持 `libsqlparser.so.0`。

## 用例与文档

- Oracle 新增 4 条 final case 和 20 个 patch，覆盖基础层次查询、复合条件、`CONNECT_BY_ROOT`、`NOCYCLE` 以及 `CONNECT_BY_ISLEAF` / `CONNECT_BY_ISCYCLE` 伪列。
- Dameng 新增 4 条 final case 和 20 个 patch，覆盖两种子句顺序、两种父子字段方向、`CONNECT_BY_ROOT` 与 `NOCYCLE`。
- Vastbase-SQLServer 新增 1 条 final case 和 5 个 patch，覆盖基础 `CONNECT BY` 以及普通 `WHERE` / `ORDER BY` 与层次条件的独立表达。
- 本版本共新增 9 条 final case 和 45 个独立 patch。当前九套 fixture 共包含 2,781 条 final case 和 9,034 个 patch。

## 验证

- Oracle 方言回归完成 248 条 case 和 849 个 patch，Dameng 完成 174 条 case 和 633 个 patch，Vastbase-SQLServer 完成 601 条 case 和 1,847 个 patch，失败数均为 0。
- 原始反解析、View JSON、patch 反解析和 runner 内部错误数均为 0。
- 核心 API 测试通过，覆盖层次上下文隔离、方言边界、原文反解析、patch 后重新解析和失败回滚。
- 相关方言目标的定向 Valgrind 检查退出时为 `0 bytes in 0 blocks`，错误数为 0。

内置 `libpg_query` 标签：`17-6.2.2`。
内置 Jansson 版本：`2.15`。
