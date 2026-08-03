# v2.14.1 发布说明

`v2.14.1` 是 `v2.14.0` 的补丁版本，完善 MERGE INSERT 的结构化定位与改写能力，并修正 SQL Server 兼容模式下局部 patch 触发未修改 SQL 表面规范化的问题。

## MERGE INSERT 结构化改写

- MERGE INSERT 的每个显式目标列提供 `merge_insert_column` selector，每个完整 VALUES cell 提供 `merge_insert_cell` selector，显式目标列列表提供 `insert_branch_columns` selector。
- 根 MERGE 通过 WHEN 分支序号和列序号定位；同一 statement 内的嵌套 MERGE 额外包含 DML 索引，避免多个 MERGE 之间的 selector 冲突。
- 单列与完整 cell 可独立替换。目标列列表支持按同一索引原子插入或删除目标列和值，避免两侧数量或顺序失配。
- 新 cell 可来自 SQL、source selector、literal 或 bind；field、bind、expression 以及 `source_field`、`source_target` 来源关系继续保留。

## Patch 与反解析表面保留

- SQL Server 与 Vastbase-SQLServer 控制流中的 SELECT、INSERT、UPDATE、DELETE 和 MERGE 支持局部源码改写，未修改分支继续保留原始换行、空白、括号、标识符定界符和大小写。
- CTE DML、`UNION ALL`、表提示和多行 `DROP ... IF EXISTS` 等边界在 patch 后不再被折叠或重排。
- ODBC `{fn ...}` scalar wrapper 的完整 SELECT target 替换会覆盖 wrapper，不会残留 `{fn ` 前缀。
- UPDATE assignment 通过真实 OUTPUT target 校验 `OUTPUT` 边界；UPDATE OUTPUT target 列表只在可验证的 `FROM` 或 `WHERE` 边界执行局部改写，无法证明边界时继续安全回退。

## 兼容性

- `sqlparser_selector_kind_t` 仅在末尾追加 `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN` 和 `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL`。
- 既有枚举值、公开函数签名和公开结构体布局保持不变；动态库 ABI 主版本仍为 `libsqlparser.so.0`。
- MERGE INSERT View 新增目标列 selector、cell selector 和可用时的 `target_list_selector`。消费方可按需读取这些新增字段，既有字段含义不变。

## 发布验证

- 九套可执行方言夹具包含 2,755 条 `status = "final"` 用例和 8,918 个独立 patch。
- 每条用例校验原始 SQL 的逐字节反解析和期望 View JSON；每个 patch 校验期望 SQL、重新解析后的二次反解析以及 patch/fresh View 一致性。
- 远端完整 `make test` 退出码为 0；九套夹具的 case、patch、原始反解析、View、patch 反解析和 runner error 均为 0 失败。

内置 `libpg_query` 标签：`17-6.2.2`。
