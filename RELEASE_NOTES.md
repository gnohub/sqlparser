# v2.15.4 发布说明

`v2.15.4` 将既有公开改写接口统一到一个 patch 事务执行链，并在必须完整 AST 反解析时保留各方言的分页语法家族。

## 统一改写事务

- 20 个 statement/index 改写函数和 20 个 selector 改写函数继续保留，函数签名与调用方式不变。
- 所有便捷改写入口现在组装等价 patch，并通过 `sqlparser_apply_patch()` 的事务候选执行。patch 分派只调用内部 in-place primitive，不再回调公开改写函数，因此不会形成递归或第二套提交路径。
- 每次调用最多提交一次。失败和结果无实际变化的调用不修改原 handle、generation 或派生缓存；发生实际修改时 generation 递增一次。批量 patch 中任一项失败时整批回滚。
- 已无调用的直接结构化改写 helper 与 multi-table INSERT cell 二次 candidate clone 已删除。

## 分页语法家族

- 内置 parser 的私有 `SelectStmt` / protobuf 状态区分 `LIMIT`、`OFFSET ... ROWS`、`FETCH FIRST` 和 `FETCH NEXT`。该状态仅用于 AST 生命周期与反解析，不新增公共 View、Query Graph 或 C API 字段。
- 可执行局部源码 edit 时，未修改区域仍按输入逐字节保留。必须完整 AST 反解析时，不保证原始空白、大小写或 `ROW` / `ROWS` 拼写，但会保持所选方言有效的分页家族。
- 本项目九个方言入口的分页行为为：
  - PostgreSQL / Vastbase-PostgreSQL：保留输入的 `LIMIT` 或标准 `OFFSET ... FETCH` 家族。
  - MySQL / Vastbase-MySQL：保持 `LIMIT` 家族，包括逗号式 offset/count 的既有方言状态。
  - Oracle / Vastbase-Oracle：`OFFSET ... ROWS` 以及 `[OFFSET ... ROWS] FETCH FIRST|NEXT ... ROWS ONLY` 完整反解析后仍为 Oracle 分页，不再输出 `LIMIT`。
  - SQL Server / Vastbase-SQLServer：保持 `OFFSET ... ROWS` / `OFFSET ... FETCH`；`TOP` 继续由独立方言状态恢复。
  - Dameng：继续区分 `TOP`、`LIMIT` 与标准 `OFFSET ... FETCH`；完整 AST fallback 可以把 `LIMIT offset,count` 规范为语义等价的 `LIMIT count OFFSET offset`。
- 上述 Vastbase 条目描述的是本项目兼容入口的可执行回归契约，不推导 Vastbase 服务端的官方语法承诺。
- 本版本不增加 `FETCH ... PERCENT` 语义。既有 SQL Server 与 Dameng `TOP ... PERCENT [WITH TIES]` 能力不受影响。

## Oracle 投影改写场景

以下 Oracle 语句替换投影列后，分页尾部不会变为 `LIMIT 1000 OFFSET 0`：

```sql
SELECT "APP"."T".*,
       ROWID "NAVICAT_ROWID"
FROM "APP"."T"
OFFSET 0 ROWS FETCH NEXT 1000 ROWS ONLY
```

- 能可靠定位投影源码区间时，仅替换该区间，分页尾部逐字节保留。
- 即使后续改写使局部源码状态不再完整、必须从 AST 生成整句，输出仍使用 Oracle `OFFSET ... FETCH ... ONLY` 家族。

## API 与兼容性

- `sqlparser_apply_patch()` 是推荐的统一改写入口。既有 statement、selector 与结构化便捷改写函数继续可用并保留各自的公开参数校验；转换为 patch 后，共享原子失败回滚、generation 更新和派生缓存失效规则。
- 本版本不新增或删除公开函数、公开枚举、公开结构体字段或资源所有权规则；动态库 ABI 主版本保持 `libsqlparser.so.0`。

## 验证

- 九套 fixture 未新增 case，仍包含 2,781 条 final case 和 9,034 个 patch；全量 `make test` 通过。
- 定向完整 AST fallback 回归覆盖 PostgreSQL / Vastbase-PostgreSQL `LIMIT`、MySQL / Vastbase-MySQL 嵌套及逗号式 `LIMIT`、Oracle / Vastbase-Oracle `FETCH`、SQL Server / Vastbase-SQLServer `OFFSET ... FETCH`，以及 Dameng `TOP` / `LIMIT` 与混合 `FETCH` / `TOP` owner。
- 核心 API、identifier spelling、robustness 与分页方言状态的定向 Valgrind 检查均为 `0 bytes in 0 blocks`，错误数为 0。
- 本次 protobuf 生成验证使用 protoc 25.1 和 protoc-gen-c 1.5.1；生成脚本显式保留既有 `SelectStmt` 字段号，并输出分页字段、枚举与 `String.location`。

内置 `libpg_query` 标签：`17-6.2.2`。
内置 Jansson 版本：`2.15`。
