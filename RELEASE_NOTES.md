# v2.15.1 发布说明

`v2.15.1` 为 Oracle、Dameng 和 Vastbase-Oracle 增加单返回 target、单宿主绑定变量的 `RETURN`/`RETURNING ... INTO` 结构化解析、View 与 patch 能力，并保持未修改 SQL 文本的原始形式。

## 支持范围

- Oracle 与 Vastbase-Oracle 支持 `INSERT`、`UPDATE`、`DELETE` 的单个 `RETURNING` 表达式与单个 `INTO` 冒号宿主绑定变量。
- Dameng 支持 `INSERT`、`DELETE` 的 `RETURNING ... INTO :bind`，以及 `UPDATE` 的 `RETURN ... INTO :bind`。
- 冒号与绑定变量名构成连续 token，例如 `:NAV_ROWID`。带空格的 `: NAV_ROWID` 不属于该语法。
- 多个返回 target、多个输出绑定变量和 `BULK COLLECT` 不在当前支持范围内。

## Query Graph 与 View

- DML 返回宿主绑定变量使用 `kind = "sink"` 的 result channel，不生成 `sink_relation`。
- 返回 target 的 `sink_value` 指向 `query_graph.values[]` 中的输出绑定变量；该 value 保留绑定变量的 key、kind、SQL、全局位置和既有 value selector。
- `ROWID` 作为 pseudo target 输出，不生成 field。`INSERT`、`UPDATE` 的返回引用使用 `target_after`，`DELETE` 使用 `target_before`。
- relation-backed sink 继续使用 `sink_relation` 和可选 `sink_columns`，与 host-bind sink 的表示互不混用。

## Patch 与反解析

- DML 输入值、返回 target 和输出绑定变量均复用既有 selector 与 `SQLPARSER_PATCH_REPLACE`，不增加专用 selector 或 patch 类型。
- 返回 target 和输出绑定变量可独立替换；重新生成 View 后，`sink_value` 关联、绑定变量序号和来源关系保持一致。
- patch 仅修改目标源码区间，未修改源码区间保持原文。
- 多语句中的空语句与纯注释片段不占用 statement 索引；Dameng `RETURN` 的原始字母大小写在反解析时恢复。

## API 与兼容性

- `sqlparser_graph_target_t` 追加 `sink_value_index` 和 `has_sink_value`，用于读取可选输出绑定变量关联。
- 本版本不新增公开函数、枚举、selector 类型或资源所有权规则。
- 公开结构体布局发生追加式变化。C 调用方应使用 2.15.1 头文件重新编译。
- 动态库 ABI 主版本保持 `libsqlparser.so.0`。

## 用例与文档

- Oracle、Dameng 和 Vastbase-Oracle 共新增 9 条 final case 和 27 个独立 patch，覆盖 `INSERT`、`UPDATE`、`DELETE` 的 DML 输入值、返回 target 与输出绑定变量改写。
- 当前九套 fixture 共包含 2,767 条 final case 和 8,972 个 patch。
- 仓库示例、文档和测试数据统一使用中性 schema 名 `APP`；该命名不限制调用方 SQL 中的 schema 名称。
- View JSON、API、方言支持范围、官方语法覆盖和 case matrix 的中英文文档已同步。

## 验证

- Oracle、Dameng 和 Vastbase-Oracle 三套相关矩阵完成 628 条 case 和 2,219 个 patch，失败数为 0。
- SQL batch 矩阵的 213 条 case 和 720 个 patch 同步通过；本次定向严格回归合计覆盖 841 条 case 和 2,939 个 patch。
- 受影响的核心 API、三个示例和 CLI 参数顺序检查通过。
- 三个相关方言完成定点 Valgrind 检查，退出时无残留内存，错误数为 0。

内置 `libpg_query` 标签：`17-6.2.2`。
内置 Jansson 版本：`2.15`。
