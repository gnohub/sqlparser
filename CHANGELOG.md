# 变更记录

## 2.16.5

### 多表 UPDATE

- MySQL 与 Vastbase-MySQL 兼容入口支持 JOIN 链和逗号表列表中的多个写入目标；每个 assignment 按其限定符关联到对应 relation，混合目标不输出单一 `dml.target_relation`。
- Dameng 支持 JOIN 和逗号表列表形式的多表 UPDATE，并严格要求全部 assignment 指向同一个 table object；跨目标、未知或歧义限定符返回 `SQLPARSER_STATUS_UNSUPPORTED`。
- assignment 与 relation 改写继续使用既有 selector 和事务候选；失败保持原 handle、SQL、View 与 generation 不变。
- 本版本未新增公开 C 声明、View JSON 字段或资源所有权规则；Query Graph 继续通过既有 assignment `target_field` 与 field `relation` 表达写入归属。

### 用例与验证

- 新增 16 条 final case 和 41 个 patch；九套 fixture 当前合计 2,822 条 final case 和 9,118 个 patch。
- 远端完整 `make test` 通过。核心 API、MySQL、Vastbase-MySQL 和 Dameng 四项定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。

## 2.16.4

### Assignment 列表定位与改写

- 根 `INSERT` 冲突更新列表使用 `stmt[S].assignment[A]`，嵌套 `UPDATE` 赋值列表使用 `stmt[S].assignment[D][A]`；assignment 插入、整项替换和删除均支持这两类目标。
- MySQL 与 Vastbase-MySQL 覆盖 `ON DUPLICATE KEY UPDATE`，PostgreSQL 与 Vastbase-PostgreSQL 覆盖 `ON CONFLICT DO UPDATE` 和 data-modifying CTE 中的嵌套 `UPDATE`，SQL Server 与 Vastbase-SQLServer 覆盖带 `OUTPUT` 的嵌套 `UPDATE`。
- View JSON schema、公开 C 声明和资源所有权规则未变；assignment selector 输出范围扩展，部分 MySQL 冲突更新项由旧 value selector 改为 assignment selector。`INSERT ... SET` 的字段/值成对改写属于既有能力，本版本仅补充回归覆盖。

### 用例与验证

- 远端完整 `make test` 通过；其中六套受影响方言矩阵共 2,158 条 final case、6,798 个 patch。PostgreSQL、MySQL、SQL Server 三套基础方言矩阵的定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。
- 新增 10 条 final case 和 28 个 patch；九套 fixture 当前合计 2,806 条 final case 和 9,077 个 patch。

## 2.16.3

### 完整 bind occurrence 读取

- 新增 handle 级 `sqlparser_handle_bind_occurrences()` 与 `sqlparser_bind_occurrence_at()`，按当前整段 SQL 的实际顺序返回全部真实占位符；重复项不合并，多语句 position 连续编号。
- occurrence 提供 `position`、`kind`、`key` 和完整 `sql`，并遵循九个方言入口各自的占位符边界。实际改写后旧 view 失效，失败或无实际变化的改写保持原 view 有效。
- 既有 Query Graph bind 字段与调用方式保持不变，继续表达语义关联；完整列表独立提供，不写入 View JSON。

### 验证

- 严格增量构建、两个定向测试和九套方言矩阵通过，共 2,796 条 final case、9,049 个 patch。ABI/export 验证通过，共 154 个公开符号；两个定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。

## 2.16.2

### 多项 DML 结果接收端

- Oracle 与 Vastbase-Oracle 兼容入口的 `INSERT`、`UPDATE`、`DELETE` 支持 `N >= 1` 个 `RETURNING` target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 一一对应。Dameng 的 `INSERT`、`DELETE` 使用 `RETURNING`，`UPDATE` 使用 `RETURN`，并提供相同的等长配对能力。
- Query Graph 继续使用既有 sink result channel；每个结果 target 的 `sink_value` 分别指向同序号输出 bind。AST 内部只保存一份配对数量，不重复维护两套恒等计数。
- `BULK COLLECT`、非冒号 bind receiver、空列表或 target/receiver 数量不等的 Oracle-family/Dameng 输入明确返回 `SQLPARSER_STATUS_UNSUPPORTED`。

### 原子成对 Patch

- 既有 `SQLPARSER_PATCH_INSERT_COLUMN` 支持以 `dml_result_targets` 列表 selector 定位成对 DML 结果列表：`index` 是两侧共同插入位置，`default_sql` 提供 target SQL，`name` 提供 receiver。target 与 receiver 在同一事务候选中原子插入，任何解析、索引、receiver 或提交失败都不修改原 handle。
- SQL Server 与 Vastbase-SQLServer 兼容入口支持对显式非空、且改写前 OUTPUT target 与 sink column 数量相等的 `OUTPUT ... INTO sink(columns...)` 执行同序号成对插入。原本合法的不等长 `OUTPUT` 仍可解析和反解析，但不支持该成对 patch；client `OUTPUT` 和无显式 sink column list 的通道也保持原边界。
- PostgreSQL 与 Vastbase-PostgreSQL 的 `RETURNING` 是 client result list，没有对应的 SQL receiver list，继续使用既有 target-only 改写；MySQL 与 Vastbase-MySQL 不增加 DML `RETURNING INTO` 语法。

### API、用例与验证

- 本版本没有新增公开函数、公开枚举、公开结构体字段、View JSON 字段或资源所有权规则；既有 selector、patch operation 与 result-channel 表达保持不变。
- Oracle、Dameng、SQL Server、Vastbase-Oracle 和 Vastbase-SQLServer 共新增 15 条 final case 和 15 个独立 paired patch。每个适用入口均以 `INSERT`、`UPDATE`、`DELETE` 三条 8 对用例覆盖头、中、尾插入，patch 后严格变为 9 对。九套 fixture 当前合计 2,796 条 final case 和 9,049 个 patch。
- 远端严格核心 API 测试和五套受影响方言矩阵通过；五套矩阵合计 1,876/1,876 条 case、5,996/5,996 个 patch，原始反解析、View JSON 和 patch 反解析失败数均为 0。核心 API 与五套矩阵的六项定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。

## 2.16.1

### Query Graph DML cell 紧凑缓存

- DML cell 内部记录由 456 B 降至 80 B，公开 API 和 View JSON 不变。
- 20,000 个 literal cell 的 Query Graph 保留量由 14.659 MiB 降至 1.685 MiB，下降 88.504%。
- 相关回归、五项定向 Valgrind Memcheck 和 ABI 检查通过。

## 2.16.0

### Deparser 内存

- 在未启用 pretty-print 和 commas-start-of-line 的 non-pretty fallback 反解析路径中，不再为逗号创建 `DeparseStatePart`，而是直接写入与原合并结果相同的 `", "`；两条格式化分支保持不变。
- 16 个投影的 fallback deparse 累计请求量从 30,970 B 降至 6,394 B，峰值从 24,640 B 降至 5,310 B；256 个投影分别从 610,810 B 降至 152,058 B、516,160 B 降至 117,208 B。两组输出均与修改前逐字节一致。

### Patch AST 生命周期

- 成功且非 no-op 的 patch 在事务候选 handle 移交前释放已解包 AST；packed tree、surface SQL、generation、持久化 identifier/dialect 语义和派生缓存重绑定规则保持不变。失败及语义 no-op 继续保持原 handle 不变。
- control condition 渲染在读取 AST 前先获取当前 statement node；fallback deparse 需要方言后处理且当前无 AST 时，仅为该次操作重建并绑定 AST，返回前释放。
- 单项 replace 返回时保留量从 1,700 B 降至 364 B；同一 handle 连续四次 apply 的最终保留量从 1,505 B 降至 169 B，且没有逐次增长。

### Query Graph 紧凑缓存

- Query Graph 在缓存内使用私有紧凑 target/value 记录，公共 accessor 按需还原完整结构。Linux LP64 下 target 记录从 224 B 降至 104 B，value 记录从公共布局的 800 B 降至 88 B。
- 删除 target/value 缓存无调用方的空记录构造路径；意外的空 target/value source 或空 target identifier 现在明确返回 `SQLPARSER_STATUS_INTERNAL_ERROR`。这些检查仅处理内部一致性错误，公开 API 布局和成功路径行为不变。
- bind 文本由缓存内的连续文本池统一持有；`LIKE ... ESCAPE` 只为实际存在 escape 的 value 保留 56 B 稀疏记录。target、value、block 和普通索引池从 4 个元素起步；Graph 构建完成后，target、value、value text、LIKE ESCAPE 和 index pool 各最多执行一次按实际使用量收缩的 best-effort 尝试。
- 完整 Query Graph cache 的 allocator payload 实测为：100 个投影 20.66 KiB，129 个 26.30 KiB，200 个 40.19 KiB，256 个 51.12 KiB。

### API、兼容性与验证

- 公开 C API、公开枚举、公开结构体布局、View JSON schema 和资源所有权规则均不变。动态库仍导出 152 个公共符号，SONAME 保持 `libsqlparser.so.0`。
- 严格构建和全量 `make test` 通过；九组 case matrix 共 2,781/2,781 条 case、9,034/9,034 个 patch，unit、example 和 CLI 全部通过。24 条 AST/Graph→Patch→Graph 重建→fallback deparse 联合链路在配置的超时时间内完成；Memcheck 为 23,129 次分配/23,129 次释放、`0 bytes in 0 blocks`、0 errors，Helgrind 为 0 errors。
- 完整 `make verify-valgrind` 通过，unit、方言矩阵、example、CLI batch 和 install smoke 均为 `0 bytes in 0 blocks`、0 errors。ABI 保持 152 个公共符号，install smoke 确认版本文本与 API 均为 `2.16.0`。

## 2.15.4

### 统一改写事务

- 既有 40 个 statement/index 与 selector 便捷改写函数全部保留，并统一通过 `sqlparser_apply_patch()` 的事务候选执行；patch 分派直接调用内部 in-place primitive，不再回调公开改写函数。
- 每次便捷改写最多提交一次。失败和结果无实际变化的调用不修改原 handle 或 generation；发生实际修改时 generation 仅递增一次，派生缓存按同一规则失效。
- 删除已无调用的直接结构化改写与 multi-table INSERT cell 二次 clone 路径；改写失败继续由候选 handle 统一回滚。

### 分页语法家族

- 内部 `SelectStmt` 与 protobuf 追加私有分页样式，区分 `LIMIT`、`OFFSET ... ROWS`、`FETCH FIRST` 和 `FETCH NEXT`。完整 AST 反解析不再把 Oracle / Vastbase-Oracle 的 `OFFSET ... ROWS` 或 `[OFFSET ... ROWS] FETCH FIRST|NEXT ... ROWS ONLY` 降级为 `LIMIT`。
- PostgreSQL / Vastbase-PostgreSQL 保持 `LIMIT` 或标准 `OFFSET ... FETCH` 家族；MySQL / Vastbase-MySQL 保持 `LIMIT` 家族；SQL Server / Vastbase-SQLServer 保持 `OFFSET ... FETCH`，并继续由独立状态恢复 `TOP`；Dameng 继续区分 `TOP`、`LIMIT` 与标准 `OFFSET ... FETCH`。
- 局部源码 edit 可用时，未修改区域继续逐字节保留。完整 AST fallback 可以把 `ROW` 规范为 `ROWS`，并可以把 Dameng `LIMIT offset,count` 规范为语义等价的 `LIMIT count OFFSET offset`。本版本不新增 `FETCH ... PERCENT` 语义；既有 SQL Server 与 Dameng `TOP ... PERCENT [WITH TIES]` 不受影响。

### API、兼容性与验证

- 本版本不新增或删除公开函数、公开枚举、公开结构体字段或资源所有权规则；`LimitClauseStyle` 仅属于内置 parser AST/protobuf，不进入公共 View 或 Query Graph。
- 九套 fixture 未新增 case，仍包含 2,781 条 final case 和 9,034 个 patch。全量 `make test` 通过；核心 API、identifier spelling、robustness 与分页方言状态的定向 Valgrind 检查退出时均为 `0 bytes in 0 blocks`，错误数为 0。
- 内置 `libpg_query` 标签保持 `17-6.2.2`；protobuf 生成脚本显式保留既有字段号，并输出分页字段、枚举与既有 `String.location`。

## 2.15.3

### Oracle 与 Dameng 层次查询

- Oracle 支持 `START WITH ... CONNECT BY [NOCYCLE] ...`，并在层次查询中识别 `PRIOR`、`LEVEL`、`CONNECT_BY_ROOT`、`CONNECT_BY_ISLEAF` 和 `CONNECT_BY_ISCYCLE`。
- Dameng 支持 `START WITH`、`CONNECT BY [NOCYCLE]`、`PRIOR`、`LEVEL` 和 `CONNECT_BY_ROOT`，同时保留 `START WITH ... CONNECT BY ...` 与 `CONNECT BY ... START WITH ...` 两种句法顺序。
- Vastbase-SQLServer 仅支持已验证的基础 `CONNECT BY` 形态；本版本不将 `START WITH`、`PRIOR`、`NOCYCLE` 或 `CONNECT_BY_ROOT` 扩展到该模式。

### Query Graph 与 Patch

- `sqlparser_clause_kind_t` 追加 `SQLPARSER_CLAUSE_KIND_START_WITH = 11` 和 `SQLPARSER_CLAUSE_KIND_CONNECT_BY = 12`；层次条件继续使用既有 `fields[]`、`values[]` 与 `predicates[]`，不增加专用 hierarchy 对象。
- `sqlparser_graph_field_t` 追加 `pseudo` 和 `prior`，`sqlparser_graph_predicate_t` 追加 `nocycle`。`CONNECT_BY_ROOT` 通过既有 `target_path` 表达，不增加新 target kind、selector 或 patch 类型。
- relation、field、value 和 SELECT target 改写复用既有 selector 与 patch 机制；可准确定位源码区间的替换使用局部源码 edit。Patch 后未修改的层次子句顺序、空白、大小写和标识符定界符保持原文。

### 用例、接口与验证

- Oracle、Dameng 和 Vastbase-SQLServer 共新增 9 条 final case 和 45 个独立 patch。当前九套 fixture 共包含 2,781 条 final case 和 9,034 个 patch。
- Oracle 248 条 case / 849 个 patch、Dameng 174 条 case / 633 个 patch、Vastbase-SQLServer 601 条 case / 1,847 个 patch 的方言回归及核心 API 测试通过，原始反解析、View JSON 和 patch 反解析失败数均为 0。相关方言目标的定向 Valgrind 检查退出时为 `0 bytes in 0 blocks`，错误数为 0。
- 本版本不增加公开函数或资源所有权规则。公开枚举追加取值，公开结构体追加字段，C 调用方应使用 2.15.3 头文件重新编译。

## 2.15.2

### MERGE matched UPDATE 附属删除条件

- Oracle 与 Dameng 支持 matched UPDATE action 中的 `UPDATE SET ... [WHERE ...] DELETE WHERE ...`。附属删除条件保留在同一个 UPDATE 分支中，不生成独立 DELETE branch。
- Query Graph 的 UPDATE branch 使用 `delete_condition_selector` 定位附属删除条件；普通 action `WHERE` 继续使用 `condition_selector`，两类条件可同时存在并独立读取或替换。
- PostgreSQL 与 SQL Server 的 `WHEN MATCHED ... THEN DELETE` 继续表示独立 MERGE branch，与 Oracle/Dameng 的附属删除语义保持区分。

### Patch 与原文保留

- `sqlparser_selector_clause_sql()`、`sqlparser_selector_set_clause_sql()` 和 `SQLPARSER_PATCH_REPLACE` 支持 MERGE 分支条件与附属删除条件。
- MERGE assignment 由逗号、action `WHERE`、附属 `DELETE WHERE` 或后续 `WHEN` 明确限定时使用局部源码改写；修改 assignment 或条件后，未修改的换行、空白、大小写、标识符定界符和其他分支保持原文。
- 内部 `MergeWhenClause` 与 protobuf 追加独立 `delete_condition` 字段，字段号为 7；语法仅接受包含 `WHERE` 与条件表达式的附属 DELETE。

### API、用例与验证

- `sqlparser_selector_kind_t` 追加 `SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`；`sqlparser_graph_dml_branch_t` 追加 `delete_condition_selector` 和 `has_delete_condition_selector`。本版本不新增公开函数或资源所有权规则，C 调用方应使用 2.15.2 头文件重新编译。
- Oracle、Dameng、PostgreSQL 和 SQL Server 共新增 5 条 final case 和 17 个独立 patch。当前九套 fixture 共包含 2,772 条 final case 和 8,989 个 patch。
- 九套 case matrix 与核心 API 测试全部通过，原始反解析、View JSON 和 patch 反解析失败数均为 0。定向 Valgrind 检查退出时为 `0 bytes in 0 blocks`，错误数为 0。

## 2.15.1

### DML 结果返回宿主绑定变量

- Oracle 与 Vastbase-Oracle 支持 `INSERT`、`UPDATE`、`DELETE` 的单个 `RETURNING` 表达式与单个 `INTO` 冒号宿主绑定变量。
- Dameng 支持 `INSERT`、`DELETE` 的 `RETURNING ... INTO :bind`，以及 `UPDATE` 的 `RETURN ... INTO :bind`。
- Query Graph 使用 sink result channel 表达返回通道；结果 target 的 `sink_value` 指向 `query_graph.values[]` 中的输出绑定变量。`INSERT`、`UPDATE` 使用 `target_after` 来源，`DELETE` 使用 `target_before` 来源。

### Patch 与原文保留

- DML 输入值、返回 target 和输出绑定变量均复用既有 selector 与 `SQLPARSER_PATCH_REPLACE`，不增加专用 patch 类型。
- 返回 target 或输出绑定变量完成 patch 后，未修改的标识符定界符、大小写、空白、关键字和绑定变量拼写保持原文。
- 当前支持边界为单个返回 target 与单个冒号宿主绑定变量；多个返回 target、多个输出绑定变量和 `BULK COLLECT` 不在支持范围内。

### API、用例与验证

- `sqlparser_graph_target_t` 追加 `sink_value_index` 和 `has_sink_value`。本版本不新增公开函数、枚举或资源所有权规则；由于公开结构体布局发生变化，C 调用方应使用 2.15.1 头文件重新编译。
- Oracle、Dameng 和 Vastbase-Oracle 共新增 9 条 final case 和 27 个独立 patch。当前九套 fixture 共包含 2,767 条 final case 和 8,972 个 patch。
- 仓库示例、文档和测试数据统一使用中性 schema 名 `APP`；该命名不限制调用方 SQL 中的 schema 名称。
- 定向严格回归覆盖 841 条 case 和 2,939 个 patch，失败数为 0；受影响的核心 API、三个示例和 CLI 参数顺序检查通过。三个相关方言的定点 Valgrind 检查退出时无残留内存，错误数为 0。

## 2.15.0

### Linux AArch64 构建

- Make 配置新增 `CROSS_COMPILE`，统一选择 `CC`、`AR`、`RANLIB`、`NM` 和 `READELF`，未设置时继续使用 Linux 原生工具链。
- 新增 `scripts/build_linux_aarch64.sh`，使用独立的 `build/linux-aarch64`、`bin/linux-aarch64` 和 `lib/linux-aarch64` 输出目录完成增量交叉构建及产物检查。
- 交叉构建会检查共享库与 CLI 的 AArch64 ELF 标识、静态归档的全部成员架构、vendor 动态依赖和公开 ABI 导出。

### 自包含第三方依赖

- Linux 与 MSVC Windows 构建统一使用仓库内的 Jansson 2.15 源码，Linux 不再依赖系统 Jansson 或其 `pkg-config` 元数据。
- Jansson 与 `libpg_query` 对象直接进入 `libsqlparser.a` 和 `libsqlparser.so`；pkg-config 文件不再声明外部 Jansson 依赖。
- `libpg_query` 对象、归档及依赖文件移入顶层构建目录。编译器、归档器、调试模式、编译参数、源码集合及头文件变化均参与增量重建判断。

### 验证与兼容性

- Linux AArch64 交叉构建与原生构建均通过；原生 `make test` 的九套 case matrix 共 2,758 条用例和 8,945 个 patch，失败数均为 0。
- 两种构建方式生成的 `sqlparser_cli` 均可在 Linux AArch64 运行，对相同输入产生逐字节一致的 View JSON。
- 动态库继续导出 152 个公开符号，SONAME 保持 `libsqlparser.so.0`；本版本不新增公开 C API 或资源所有权规则。

## 2.14.5

### Query Graph 标识符定界符状态

- `sqlparser_graph_relation_t` 和 `sqlparser_graph_field_t` 新增 `quoted_identifier`，分别表示 relation 的对象名和 field 的列名是否在原始 SQL 或 patch 片段中显式使用标识符定界符。
- View JSON 的 `relations[]`、`fields[]` 以及 session identifier value 在对应 token 使用 `"..."`、MySQL 反引号或 SQL Server 方括号时输出 `quoted_identifier: true`；该字段只表示定界符是否存在，不区分定界符类型。
- 判断以输入 SQL 或 patch 片段中的精确 token 为依据。解析器内部为兼容方言生成的引号样式不会被误报为原始定界符，普通字符串字面量也不会进入该标记。

### Patch 一致性与验证

- Oracle 与 Vastbase-Oracle 的 fragment preprocess 保留精确 identifier origin。包含 bind 改写的 assignment patch 在当前 handle 和重新解析后的 View 中保持一致。
- Oracle、达梦及 Vastbase-Oracle 的 database link relation 从方言状态读取对象原始拼写；MySQL 兼容的 session identifier 同样按原始 token 判断定界符状态。
- 九套可执行方言夹具仍包含 2,758 条 final 用例和 8,945 个独立 patch，并新增 1,800 个定界符状态断言。`make test-unit` 全部通过。
- 本版本新增两个公开结构体字段，但不新增公开函数、枚举或资源所有权；query graph 返回值继续由 handle 持有。

## 2.14.4

### 连续结构化 Patch 状态一致性

- 结构化 patch 无法继续使用局部源码 edit、转入 AST fallback 时，同时清除本次调用与 handle 级的源码表面完整标记，避免后续内部反解析和重新解析恢复 patch 前的旧源码。
- Oracle 兼容的 `INSERT ALL` 在同一 handle 上新增列和值后，新增 cell 会保留在当前 AST 与源码状态中；后续独立 `replace` patch 能够按新索引定位，不再出现首次插入被静默丢弃或 `cell index is out of range`。
- 连续 patch 不要求在调用之间执行 View、deparse 或重新 parse；最终反解析保持未修改分支、绑定参数及其他原始 SQL 文本不变。

### 回归与兼容性

- Oracle、达梦和 Vastbase-Oracle 增加双分支、每分支 32 列的 `INSERT ALL` 单元回归。在同一初始 handle 上连续执行四组插列与新增 cell 替换，共八次独立 `apply_patch`，并精确校验最终 SQL 及重新解析后的稳定性。
- 本版本没有新增公开 API、枚举或结构体字段；既有函数签名、公开结构体布局和动态库 ABI 主版本保持不变。
- 九套可执行方言夹具仍包含 2,758 条 final 用例和 8,945 个独立 patch。最终代码的全量 `make test` 通过；定向 Valgrind 检查退出时为 `0 bytes in 0 blocks`，错误数为 0。

## 2.14.3

### INSERT COLUMN 局部源码改写

- 显式目标列的普通 `INSERT ... VALUES` 执行 `insert_column` 时，按原始 SQL 区间向目标列列表和每一行 VALUES 插入列名与默认值，不再触发整句 AST 反解析。
- 全部插入区间在写入前完成边界与冲突校验，多行 VALUES 使用相同列序号且保持原子性；共享源码扫描状态和有序 edit 尾部追加路径避免随行数增加产生重复扫描和移动。
- 未修改的 typed literal、时间函数、标识符、大小写、空白及其他源码文本逐字节保留；`DATE '...'` 和 `TIMESTAMP '...'` 等表达式不会因新增其他列而改写为 `CAST(...)`。

### 回归与兼容性

- 九套可执行方言夹具各增加一个 `insert_column` patch，覆盖 typed literal、`NOW()`、`CURRENT_TIMESTAMP` 和 `GETDATE()` 的原文保留。Case runner 支持严格解析的内部 INSERT 目标列 selector，既有 JSON Pointer patch 路径保持不变。
- 本版本没有新增公开 API、枚举或结构体字段；既有函数签名、公开结构体布局和动态库 ABI 主版本保持不变。
- 九套夹具共包含 2,758 条 final 用例和 8,945 个独立 patch。最终代码的全量 `make test` 及一次定向 Valgrind 检查均通过；Valgrind 退出时无残留内存且错误数为 0。

## 2.14.2

### INSERT VALUES 表面保留与 View 一致性

- 对于能够可靠定位源码区间的普通 `INSERT ... VALUES` cell，方言合法的字符串、typed literal、函数及复合表达式 patch 均可执行局部替换；patch 片段完成方言解析后，仅替换目标区间，未修改部分保持原文。
- Oracle、达梦和 Vastbase-Oracle 的 `DATE '...'`、`TIMESTAMP '...'` typed literal 在替换自身或同一语句的其他 cell 后不再退化为 `CAST(...)`；patched handle 与重新解析 handle 的 View 继续保持一致。
- surface 完整的 patched handle 可按当前 statement、VALUES 序号和行列坐标读取 cell 原文；保持局部源码表面的多 patch 使用 `source_selector` 时会先固化既有局部改写，确保后续克隆读取当前 SQL，而不是规范化后的 AST 文本。

### SQL Server INSERT OUTPUT 局部改写

- SQL Server 与 Vastbase-SQLServer 中可验证边界的简单 `INSERT ... OUTPUT ... VALUES` 支持 client、`OUTPUT INTO` 和双结果通道的局部改写；结果目标、sink relation 与 sink column 均按源码区间定位。
- 移除目标 relation 与列列表之间的主动空格插入。`t(a)`、`audit(id)`、方括号标识符、原始大小写和非常规空白在 patch 后按输入保留。

### 兼容性与验证

- 本版本没有新增公开 API、枚举或结构体字段；既有函数签名和公开结构体布局保持不变，动态库 ABI 主版本仍为 `libsqlparser.so.0`。
- 九套可执行方言夹具包含 2,758 条 final 用例和 8,936 个独立 patch。严格构建及九套 runner 对原始反解析、View JSON、patch 反解析、重新解析后的二次反解析和 patch/fresh View 一致性完成校验，全部通过。
- 针对 typed literal、current surface SQL 和多 patch `source_selector` 生命周期执行一次定向 Valgrind；2,100 次分配与 2,100 次释放全部对齐，退出时无残留内存，错误数为 0。

## 2.14.1

### MERGE INSERT 结构化定位与改写

- MERGE INSERT 的目标列与完整 VALUES cell 分别提供 `merge_insert_column` 和 `merge_insert_cell` selector；显式目标列列表提供 `insert_branch_columns` selector。根 MERGE 与嵌套 MERGE 使用独立坐标，能够在同一 statement 内唯一定位对应 DML、WHEN 分支和列。
- `SQLPARSER_PATCH_REPLACE` 支持替换单个 MERGE INSERT 目标列或完整 cell；`SQLPARSER_PATCH_INSERT_COLUMN` 与 `SQLPARSER_PATCH_DELETE_COLUMN` 通过目标列列表 selector 原子插入或删除同一位置的目标列和值。新增值继续支持 SQL、source selector、literal 和 bind 来源。
- MERGE INSERT cell 的 field、bind、literal 与 expression 语义及 `source_field`、`source_target` 来源关系保持不变；支持 MERGE 的方言模式共用同一组 selector 和 patch 规则。

### Patch 表面保留与边界修正

- SQL Server 与 Vastbase-SQLServer 控制流中的 SELECT、INSERT、UPDATE、DELETE 和 MERGE 局部 patch 保留未修改分支的原始换行、空白、括号、标识符定界符与大小写，覆盖 CTE DML、集合查询、表提示和多行 DDL 边界。
- 修正 ODBC `{fn ...}` scalar wrapper 的 SELECT target 替换区间，替换完整 target 时不会残留 `{fn ` 前缀。
- UPDATE assignment 通过首个真实 OUTPUT target 的来源位置确认 `OUTPUT` 边界；UPDATE OUTPUT target 列表按实际 `FROM` 或 `WHERE` 边界定位，避免进入结果列表或后续控制单元并触发整句规范化。

### 兼容性与验证

- `sqlparser_selector_kind_t` 追加 `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN` 和 `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL`；既有枚举值、公开函数签名和公开结构体布局保持不变。
- 九套可执行方言夹具包含 2,755 条 final 用例和 8,918 个独立 patch。全量 `make test` 对原始反解析、View JSON、patch 反解析、重新解析后的二次反解析及 patch/fresh View 一致性完成校验，全部通过。

## 2.14.0

### Patch 与反解析表面保留

- patch 对可定位的源码区间执行局部改写；未修改部分逐字节保留原始标识符大小写与定界符、关键字、注释、空白、括号和分号。patch 片段仍按所选方言解析后进入 AST，片段中显式提供的定界符不会被重复添加。
- 无显式 alias 的 relation 改名会同步更新作用域内唯一绑定的限定列、限定星号和限定赋值目标；同层歧义、内层遮蔽、显式 alias 以及 SQL Server `INSERTED`、`DELETED` 等伪关系保持不变。
- 单个 SELECT target 替换支持将多 target 片段在原位置展开，并且不继承被替换 target 的 alias。

### Query Graph 与 View

- 复合 UPDATE/MERGE assignment 通过 `rhs_fields`、`rhs_values` 和 `rhs_blocks` 表达右侧字段、值及子查询入口；直接 field、literal、bind 和 default 继续使用既有 assignment payload。
- 同一 statement 可表达多个并列根 DML。View 对单根输出 `query_graph.dml`，对多根输出 `query_graph.dmls`，并继续通过 `children` 表达嵌套 DML；数据修改 CTE 使用相同规则。
- 新增 `SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION`，命名窗口定义中的 `PARTITION BY` 可作为独立 clause 定位。

### 方言与结构边界

- 扩充 Oracle、达梦及 Vastbase-Oracle 的集合运算、多表 DML、bind 与 national literal 表面保留；集合树遍历不再依赖固定分支数量上限。
- 修正 MySQL 及 Vastbase-MySQL 的注释边界、可执行注释、索引提示、分区和 DML 尾部在解析及 patch 后的表面恢复。
- 修正 SQL Server 及 Vastbase-SQLServer 的 `OUTPUT`、MERGE、动态执行、事务批次和方括号标识符在 patch 后的表面恢复。

### 兼容性与验证

- `sqlparser_graph_dml_assignment_t` 新增三个公开 span 字段，公共结构体布局发生变化；C 调用方升级后必须使用 2.14.0 头文件重新编译。View 消费方需要同时处理互斥的 `dml` 与 `dmls` 形态。
- 九套可执行方言夹具包含 2,752 条 final 用例和 8,890 个独立 patch。runner 对原始反解析、View JSON 结构、patch 反解析、重新解析后的二次反解析以及 patch/fresh View 一致性分别校验。
- 发布候选代码完成一次 ASan、一次 UBSan、一次 Valgrind、10 轮全量回归和完整 benchmark；benchmark 共执行 530,100 次测量操作，错误操作数为 0。该数据用于稳定性验收，不表示相对历史版本的性能提升。

## 2.13.0

### MERGE 分支结构与改写

- Query Graph 按源码顺序投影 MERGE 的每个 `WHEN` 分支，并提供 action、match 类型、INSERT 目标列与行以及 UPDATE assignment 范围；有条件的分支同时提供对应的 condition selector。
- 新增 `SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION`、MERGE action/match 枚举、两个枚举名称函数和一个 MERGE 分支详情访问函数；顶层与嵌套 MERGE 的分支条件和赋值项均可通过 selector 定位。
- MERGE UPDATE assignment 保留目标字段、来源字段、source target 和 bind 的关联，可通过既有 assignment selector 与 patch 操作读取、插入、替换和删除。
- Oracle、达梦和 Vastbase-Oracle 支持 MERGE UPDATE/INSERT action 级 `WHERE`；其他方言拒绝该语法。

### INSERT VALUES 与 View 语义

- 九种方言模式的 `INSERT ... VALUES` 支持 bind、literal、独立 `DEFAULT`、函数和复合表达式混合排列，并保持逐单元格的行列坐标、selector 与全局 bind 序号。
- 表达式内部的 bind 参与全局序号计算；`SYSDATE`、`CURRENT_TIMESTAMP`、`NOW()`、`GETDATE()` 等时间表达式作为 expression cell 输出，不会被投影为字段名称。
- 新增九组共 90 条混合 VALUES 回归用例，覆盖单行、多行、表达式首尾与交错、嵌套 bind、带定界符的标识符和非常规空白。

### Patch 后的标识符与反解析

- SQL 片段 patch 解析为 AST 后，新增标识符保留片段中的大小写、定界符和转义形式；双引号、MySQL 反引号及 SQL Server 方括号不会被替换或重复添加。
- generation 为 `0` 的未改写 handle 继续逐字节返回原始 SQL。generation 大于 `0` 时根据当前 AST 生成 SQL；标识符的大小写、定界符和转义形式予以保留，节点之间的空白可能由反解析器规范化。
- 标识符拼写状态在成功改写、失败改写后的回滚、handle 克隆和销毁路径中均按既定生命周期管理，并且不会因重复失败的 patch 操作而累积。

### 性能、兼容性与验证

- MERGE 语法验证通过 O(AST) 的主 protobuf 深度优先遍历完成，并执行分支局部检查；Query Graph 在单次构建中复用 statement 级定位数据、表达式源码扫描游标和当前 SQL 缓存，减少重复树遍历、反解析和源码扫描。
- 公共 API 采用追加式扩展，既有函数签名和公共结构体布局保持不变。动态库 ABI 主版本保持为 `libsqlparser.so.0`，ABI 导出检查包含 152 个公共符号。
- 九套方言矩阵的 2535 条预期成功用例均执行 generation-`0` 逐字节反解析检查、AST 标识符拼写审计和 View 构建，并对声明的结构化期望逐字段断言。
- 发布验证覆盖 GCC 8.3 严格发布/调试构建、全量测试、install smoke、ABI、ASan、UBSan、Valgrind 和 benchmark smoke。

## 2.12.0

### 反解析与 AST 标识符

- handle generation 为 `0` 时，`sqlparser_deparse()` 在成功返回时逐字节复制原始输入 SQL，保留标识符定界符和大小写、关键字、空白、换行、注释、分号及多语句边界。
- AST 中源自 SQL identifier token 的名称值保留源 token 的字母大小写；带引号标识符的 AST 值仍为解码后的名称内容，其定界符和转义形式由 generation-`0` deparse 契约保留。
- generation 大于 `0` 时，反解析从当前 handle 状态重新生成 SQL，整个输出不适用逐字节一致性保证。

### 会话状态 Query Graph

- 支持的数据库、schema、角色、身份、事务特征和会话参数语句会投影为结构化 session action、scope、target 和 value。
- 新增 session action、scope、target kind、value kind 枚举，`sqlparser_graph_session_t`、`sqlparser_graph_session_item_t`、`sqlparser_graph_session_value_t` 以及三个 Query Graph 访问函数。
- View JSON 为具有可用 session projection 的语句输出可选 `query_graph.session`，覆盖 identifier、keyword、literal、bind 和 expression value。

### MERGE matched UPDATE 改写

- 新增 `SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT` 和 `stmt[S].merge_assignment[W][A]` selector；`W` 是 MERGE 中所有 `WHEN` 子句的绝对 0 基序号，`A` 是对应 matched UPDATE 分支内赋值项的 0 基序号。
- `update_assignment` selector API 支持 MERGE matched UPDATE 赋值项的读取、右值改写和同一 statement 内的右值克隆；`SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT` 与 `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` 支持赋值项的插入、删除和整项替换。
- Query Graph 为可改写的 MERGE matched UPDATE 赋值项输出对应 selector。

### 兼容性与验证

- 公共 API 采用追加式扩展；既有函数签名和公共结构体布局保持不变。动态库 ABI 主版本保持为 `libsqlparser.so.0`，ABI 导出检查包含 149 个公共符号。
- PostgreSQL、MySQL、Oracle、SQL Server、达梦及 Vastbase 四种兼容模式的九套用例矩阵对所有预期成功用例执行 generation-`0` 逐字节反解析检查和 AST 标识符拼写检查。
- 九套方言矩阵均包含 session 投影期望；支持 MERGE 的方言还包含 matched UPDATE assignment selector 与改写回归。
- 发布验证覆盖 GCC 8.3 严格发布/调试构建、全量测试、install smoke、ABI、ASan、UBSan、Valgrind，以及 Windows VS 2022 x64/MSVC 19.39 清理后构建和全量测试。

## 2.11.0

### 反解析

- 未被改写的数据库、schema、表名、列名、别名、函数名、索引名、约束名和 CTE 名称按输入 SQL 保留标识符拼写，包括未加引号标识符的原始大小写。
- 方言转换涉及注释、字符串、bind 和引号标识符时，反解析不会将其中的文本误识别为标识符。
- patch 新生成的节点使用自身标识符，不会继承输入 SQL 中其他同名节点的拼写。

### 兼容性与验证

- 公共 API、公共结构体、View JSON、Query Graph 和 ABI 保持不变；ABI 导出仍为 146 个公共符号。
- PostgreSQL、MySQL、Oracle、SQL Server、达梦及 Vastbase 兼容模式均增加标识符拼写回归用例。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查和单次调用性能测试。

## 2.10.1

### MySQL 方言

- 修正 `USE INDEX`、`IGNORE INDEX`、`FORCE INDEX` 及对应 `KEY` 形式的反解析位置，索引提示保持在表名或别名之后、后续查询子句之前。
- 修正索引提示与 `GROUP BY`、`HAVING`、`WINDOW`、集合运算、锁定子句、`NATURAL JOIN`、`STRAIGHT_JOIN` 和 `JOIN ... USING` 组合时的反解析顺序。
- 修正 `STRAIGHT_JOIN` 右侧关系的索引提示恢复。
- Vastbase-MySQL 同步应用上述修正。

### 兼容性与验证

- 公共 API、公共结构体、View JSON 和 ABI 保持不变；ABI 导出仍为 146 个公共符号。
- MySQL 与 Vastbase-MySQL 方言矩阵分别包含 173 条支持用例。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查以及 Windows VS 2022 x64/MSVC 19.39 全量测试。

## 2.10.0

### SQL Server 方言

- 支持显式或省略 `INTO` 的 `INSERT`，覆盖 `VALUES`、多行 `VALUES`、`SELECT`、集合查询、CTE 和 `DEFAULT VALUES`。
- 支持 `INSERT`、`UPDATE`、`DELETE` 和 `MERGE` 的 `OUTPUT` 结果通道，覆盖 `INSERTED`、`DELETED`、来源字段、`$action`、表达式、别名和 bind。
- 支持 `OUTPUT ... INTO`、sink/client 双通道、目标列列表以及外层 `INSERT` 消费内层 DML `OUTPUT` 的嵌套 DML。
- 支持 `IF...ELSE` 单语句分支、`BEGIN...END` 多语句分支、`ELSE IF` 和嵌套控制流。
- Vastbase-SQLServer 同步支持上述兼容语法。

### 结构化遍历与改写

- 增加控制流只读结构和访问函数，按源码顺序遍历 roots、nodes、branches、items 及可寻址条件 statement。
- Query Graph 支持同一 statement 内的多个 DML、嵌套 DML 父子关系、DML 结果通道和结果字段来源。
- 增加 DML 结果 target、sink relation 和 sink column selector，可继续通过 `sqlparser_apply_patch()` 执行统一改写。
- 公共 API 采用追加式扩展，既有函数签名和公共结构体布局保持不变。

### 性能与验证

- 普通非控制 SQL 不构造控制流状态；控制流状态使用单次连续分配。
- SQL Server 与 Vastbase-SQLServer 方言用例矩阵分别包含 546 条用例，其中 517 条为支持路径，29 条为错误或明确不支持路径。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查以及 Windows VS 2022 x64/MSVC 19.39 全量测试。
- ABI 导出检查包含 146 个公共符号。

## 2.9.0

### MySQL 方言

- 增加 `INSERT ... SET`、`ON DUPLICATE KEY UPDATE` 行别名、别名删除目标以及单表 `UPDATE` / `DELETE ... ORDER BY ... LIMIT` 支持。
- 增加 `STRAIGHT_JOIN`、`JOIN ... USING`、`NATURAL JOIN`、锁定读、索引提示和查询表 `PARTITION(...)` 支持。
- Vastbase-MySQL 同步支持上述兼容语法。

### Query Graph

- `JOIN ... USING` 中出现的字段通过现有 `fields[]` 和 `candidate_relations` 输出。
- 同一 CTE 的多次引用共享来源 `source_block`，未引用 CTE 保留在 Query Graph 中，递归 CTE 引用指向已注册的来源块。
- 增加 `SQLPARSER_GRAPH_INSERT_MODE_SET`，用于标识 `INSERT ... SET` 写入形态。
- 公共结构体布局保持稳定，ABI 导出符号数保持为 135。

### 性能与验证

- MySQL 扩展语法采用单次特征分类，仅在对应语法出现时执行转换流程。
- MySQL 与 Vastbase-MySQL 方言用例矩阵分别包含 156 条支持用例。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查及旧版本客户端动态库兼容测试。

## 2.8.1

### Query Graph 性能

- `sqlparser_statement_query_graph()` 构建期新增 statement 级 selector 缓存，一次遍历记录 value、name、relation 和 select target list 的 selector 索引。
- 大型 / 多层 `query_graph` 场景不再为每个 value、field、relation 或 SELECT target list 反复执行全 statement protobuf 树搜索，尤其改善 `INSERT ... SELECT`、集合查询和嵌套 SELECT。
- 保持公共 API、ABI、selector 输出格式和 `query_graph` 结构不变；同一 handle 的 query graph cache 行为不变。

### 测试与验证

- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.8.0

### Query Graph 与 DML 结构化输出

- `query_graph` 增加 predicate 数组，用于表达比较、布尔组合、`EXISTS` 和表达式谓词。
- 新增 `sqlparser_graph_predicate_t`、predicate 类型枚举、布尔枚举和对应名称函数。
- DML branch、assignment 和 cell 增加 source field / source target 关联，用于表达字段搬运、`MERGE` source lineage 和多分支 insert 来源。
- relation view 和 graph relation 增加 database link 名称输出。
- `ROWID` 等伪列可作为独立 target 输出，避免污染星号 lineage。

### 方言覆盖

- Oracle 增强 DML / SELECT 结构化输出，覆盖 alias-qualified `UPDATE`、`INSERT ALL` / `INSERT FIRST`、`MERGE` source lineage、`DISTINCT`、`ORDER BY` 和 qualified star + `ROWID`。
- MySQL 和 Vastbase-MySQL 增强 multi-table `UPDATE` / `DELETE`、`REPLACE`、insert modifier、CREATE TABLE option 和常见 HOOK_ONLY 语法覆盖。
- SQL Server 和 Vastbase-SQLServer 补齐多类基础 DDL、表达式、table/query hint、`FOR JSON`、nested `TOP`、全文谓词和官方语法覆盖统计。
- 达梦补齐 `ALTER SESSION` 参数、`TOP` 公开反解析、database link、national 字符串和多表 insert 相关结构化输出。
- PostgreSQL 和 Vastbase-PostgreSQL 增加通知、extension、national 字符串和常见可执行矩阵覆盖。

### 字符串和兼容语法

- MySQL、Oracle、PostgreSQL、达梦及 Vastbase 对应兼容模式支持 `N'...'` / `n'...'` national 字符串公开形态保真。
- Oracle、达梦及 Vastbase-Oracle 保留 `nq'...'` national q-quoted 字符串语义。
- SQL Server 和 Vastbase-SQLServer 保留 `N'...'` Unicode 字符串前缀和相关 prepared statement 形态。

### 测试与验证

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server、达梦和 Vastbase 四种兼容模式的现有 case matrix。
- 更新中英文方言支持文档、官方语法覆盖统计和 View JSON/API 文档。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.7.0

### Query Graph 操作符分类

- `sqlparser_graph_value_t` 新增 `operator_kind`，用于对 `LIKE`、`NOT LIKE`、`ILIKE`、`NOT ILIKE` 进行结构化分类。
- 新增 `sqlparser_graph_operator_kind_t`、`sqlparser_graph_operator_kind_name()`、`sqlparser_graph_operator_is_like_pattern()` 和 `sqlparser_graph_value_is_like_pattern()`。
- View JSON 的 `query_graph.values[]` 在存在 `operator` 时输出 `operator_kind`，pattern-match 操作为 `like`、`not_like`、`ilike` 或 `not_ilike`，其他操作符为 `unknown`。
- `LIKE ... ESCAPE ...` 识别复用结构化操作符分类，不再依赖调用方比较操作符字符串。

### 测试与验证

- PostgreSQL、Oracle、MySQL、SQL Server、达梦和 Vastbase 四个兼容模式的现有 case matrix 增加 pattern-match 操作符分类覆盖。
- 核心 API 回归测试覆盖 public enum、辅助函数、View JSON 输出、非 pattern 操作符边界和显式 `ESCAPE` 保留。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.6.0

### Vastbase 方言

- 新增四个显式 Vastbase 兼容模式：`vastbase-oracle`、`vastbase-mysql`、`vastbase-postgresql` 和 `vastbase-sqlserver`。
- 公共 C 枚举新增 `SQLPARSER_DIALECT_VASTBASE_ORACLE`、`SQLPARSER_DIALECT_VASTBASE_MYSQL`、`SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` 和 `SQLPARSER_DIALECT_VASTBASE_SQLSERVER`。
- CLI 支持四个 Vastbase 方言名称；`vastbase` 作为确定性别名映射到 `vastbase-oracle`，不会根据 SQL 文本自动猜测兼容模式。
- Vastbase 四个模式分别通过独立 hook 接入对应兼容模式，保留各自公开 SQL 形态、bind 规则和结构化输出规则。

### 测试与验证

- 新增四套 Vastbase case matrix，覆盖 Oracle、MySQL、PostgreSQL 和 SQL Server 兼容模式的解析、View JSON、反解析和明确不支持语法返回码。
- 新增 Vastbase CLI batch 用例和 `examples/dialect/20_vastbase_dialect.c`。
- 更新 Linux 与 Windows/MSVC 构建清单，确保新增源文件、单元测试和示例参与构建。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.5.0

### LIKE ESCAPE 结构化输出

- `sqlparser_graph_value_t` 新增 `like_escape`，用于表达 `LIKE`、`NOT LIKE`、`ILIKE`、`NOT ILIKE` 的显式 `ESCAPE` 子句。
- 新增 `sqlparser_graph_like_escape_kind_t`，区分无显式 `ESCAPE`、字面量、预编译占位符和表达式 escape。
- View JSON 的 `values[]` 在 pattern 主值上输出 `like_escape`，不把 escape 子句拆成并列业务值。
- 各方言 public deparse 保持 `LIKE pattern ESCAPE escape` 形态，不暴露 libpg_query 内部 `pg_catalog.like_escape(...)`。
- 结构化识别只接受 libpg_query 生成的 `pg_catalog.like_escape`，不会把用户 SQL 中未限定的同名函数误判为显式 `ESCAPE`。

### 测试与验证

- PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix 增加 `LIKE ESCAPE` 用例，覆盖字面量 escape、命名 bind、位置 bind、JDBC `?` bind、表达式 escape、无显式 escape 和派生表场景。
- 核心 API 回归测试同步覆盖 C 结构字段、View JSON、public deparse，以及用户自定义 `like_escape(...)` 函数边界。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.4.0

### UPDATE SET 改写

- `sqlparser_update_set_assignment_literal()` 和 selector 版本现在支持把 `UPDATE SET` 赋值右值从 bind 参数改写为 literal。
- 保持目标列、赋值项顺序和当前方言输出规则不变，仅替换赋值右值 AST 节点。
- 函数、运算表达式、字段引用、`DEFAULT` 和子查询右值继续返回 `SQLPARSER_STATUS_UNSUPPORTED`，避免生成语义不明确的 SQL。

### 测试与验证

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix，覆盖单字段、多字段、命名 bind、位置 bind 和 JDBC `?` bind 的 `UPDATE SET` 来源表达。
- 扩充核心 API 回归测试，覆盖 bind 右值改写、备份赋值插入、反解析后二次解析，以及复杂右值拒绝路径。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.2.0

### 结构化 SQL 片段改写

- 增加 `sqlparser_identifier_path_view_t`，用于向公共改写接口传入方言无关的标识符路径。
- 增加结构化 `UPDATE SET` 赋值构造接口，支持通过列路径和右值 SQL 生成、追加或替换赋值项。
- 增加结构化 `SELECT` 输出项替换接口，支持把单个输出项替换为一组列路径。
- 新增 `examples/convenience/18_structured_fragment_rewrite.c`，展示不拼接完整 SQL 文本的结构化改写流程。

### 测试与发布验证

- 扩充核心 API 和健壮性测试，覆盖结构化标识符路径、非法参数、追加赋值、替换输出项和反解析校验。
- 更新 Linux 与 Windows/MSVC 构建清单，确保新增源文件和示例参与构建。
- 发布验证覆盖 Linux 单元测试、内存泄漏检查、ASan、UBSan、ABI 导出检查以及 Windows/MSVC 测试。

## 2.0.2

### View JSON

- `query_graph.values[]` 新增 `field_match_kind`，用于区分条件值绑定的是直接字段还是函数、类型转换、表达式或 `CASE` 包裹字段
- 公共 C 结构 `sqlparser_graph_value_t` 同步暴露 `field_match_kind`，并新增 `sqlparser_graph_field_match_kind_name()` 名称辅助函数
- PostgreSQL、MySQL、Oracle、SQL Server 和达梦用例矩阵新增直接字段与表达式字段匹配回归

## 2.0.0

### View JSON

- `query_graph` 作为结构化输出主数据源，View JSON 仅在调用导出接口时按需生成
- 将预编译占位符输出从 `bind` 收敛为 `bind_key`，并新增 `bind_position` 表示整条输入 SQL 中的 bind 出现序号
- `sqlparser_graph_value_t`、`sqlparser_graph_dml_cell_t` 和 `sqlparser_graph_dml_assignment_t` 同步暴露 `bind_key`、`bind_kind`、`bind_position` 与 `bind_sql`
- View JSON、C 结构遍历和 case matrix 统一校验 `bind_key`、`bind_kind`、`bind_position`、`bind_sql` 与 `selector`
- 多语句 SQL 中 `bind_position` 按整条输入 SQL 全局递增，`bind_key` 保留方言预处理后的占位符 key
- PostgreSQL dollar-quoted 字符串内部的占位符样式文本不会参与 bind 全局计数
- `IN`、`NOT IN`、`BETWEEN` 和单字段函数包裹条件会输出字段关联的 `values[]`
- SELECT 投影内部的条件表达式会输出字段关联的 `values[]`，例如 `CASE WHEN phone = ? THEN ...`
- View JSON 省略 `query_graph` 与 DML 结构中的空数组，公共 C 结构仍通过 `count` 或 `has_*` 字段表达空集合
- MySQL `INSERT ... ON DUPLICATE KEY UPDATE`、带 ON 条件的普通/INNER/CROSS 多表 `UPDATE ... JOIN` 和 `DELETE ... JOIN` 纳入支持矩阵，View JSON 输出 DML 目标、来源、赋值和值参数映射

### 工具与基线

- CLI batch 输入收敛为顶层数组或 `items` 数组，不再保留旧 `sqls` 别名
- `libpg_query` baseline 收敛为单线程成功解析和线程首次解析，不再生成错误路径或并发路径基线

### 健壮性

- 修复 View JSON 序列化过程中 Jansson `_new` 接口失败路径的所有权处理，避免低内存场景下重复释放中间 JSON 节点

## 0.8.0

### 方言能力

- 支持 Oracle 普通 `ALTER SESSION SET <parameter> = <value>` 会话参数赋值，覆盖字符串、标识符、数字和布尔/枚举值
- 保持 Oracle `ALTER SESSION` 公开输出为原始参数名和值，不暴露内部适配前缀
- 修复 MySQL `LIMIT ?, ?` 参数化分页语句，公开反解析保持 MySQL 逗号分页形态

### 测试与覆盖

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix，覆盖更多 DDL、DML、JOIN、函数、表达式、bind、分页以及 `USE`、`SET SCHEMA` 和 `ALTER SESSION SET` 场景
- 新增 Oracle `ALTER SESSION SET NLS_DATE_FORMAT`、`NLS_DATE_LANGUAGE`、`NLS_NUMERIC_CHARACTERS`、`INSTANCE`、`ERROR_ON_OVERLAP_TIME` 回归用例
- 同步更新中英文 case matrix、方言覆盖统计和 Oracle 官方语法覆盖统计

## 0.7.0

### UPDATE SET 改写

- 增加 `UPDATE SET` 赋值项级 patch 能力，支持通过 `stmt[n].assignment[i]` 追加、删除和整项替换赋值项
- 新增 `SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT` 和 `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`
- 新增 `sqlparser_update_insert_assignment_sql()`、`sqlparser_update_delete_assignment()`、`sqlparser_update_set_assignment_full_sql()` 及对应 selector API
- 保持既有 `SQLPARSER_PATCH_REPLACE` 对 assignment 的右值改写语义不变

### 测试与文档

- 增加 `examples/patch/17_update_set_patch.c`，展示通过 `sqlparser_apply_patch()` 追加、删除和整项替换 `UPDATE SET` 赋值项
- 扩充核心 API 和健壮性回归测试，覆盖 Oracle bind 片段、非法 selector、越界索引、空 `SET` 保护和失败后 handle 可用性
- 更新中英文 API 手册、View JSON 手册、示例说明和 MSVC 示例构建清单

## 0.6.0

### View JSON 结构

- 将 `query_graph` C 结构作为结构化输出的主数据源，`sqlparser_export_view_json()` 改为按需从 `query_graph` 序列化 JSON
- 扩展 `sqlparser_graph_field_t` 和 `sqlparser_graph_dml_cell_t`，输出 bind 名称、bind 类型、原始 bind SQL、bind selector、子句编号和 SELECT 输出路径
- 增加 `sqlparser_bind_kind_t`、`sqlparser_bind_kind_name()`、`sqlparser_statement_clause()` 和 `sqlparser_clause_sql()` 公共接口
- 扩展 `sqlparser_clause_kind_t`，增加 `on`、`group_by` 和 `having` 子句类型
- View JSON 移除旧的 `target_kind`、`target_name`、`target_arg_index` 字段，统一使用有序 `target_path` 表达 SELECT 输出层级

### 语义与方言

- bind 输出区分 positional 和 named 两类，保留 `bind_sql` 用于区分 `?`、`:1`、`:name`、`$1`、`@name` 等原始 SQL 形态
- bind 右值不再重复暴露为普通 `value`，避免调用方把占位符误判为字面量值
- SELECT 输出表达式列不再暴露条件运算符和值，输出形态通过 `target_path` 表示
- `NOT IN`、`NOT LIKE`、`NOT ILIKE` 和 `NOT SIMILAR TO` 运算符保持完整公共 SQL 语义

### 测试与文档

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦用例矩阵，覆盖更多 SELECT、INSERT、UPDATE、DELETE、JOIN、函数、表达式和 bind 场景
- 增加 View JSON 公共 C 结构语义测试，验证结构体字段和 View JSON 输出一致
- 增加 bind 字段、cell bind、`clause_id` 和 `target_path` 的通用断言
- 更新中英文 API 手册和 View JSON 手册

## 0.5.0

### View JSON 语义

- 增加 SELECT 输出项语义路径，使用 `target_path` 表达函数、表达式、CASE 和嵌套输出层级
- 为 View JSON 增加字段归属子句编号，便于区分 SELECT、WHERE、JOIN/ON、ORDER BY 等位置的字段引用
- 扩充各方言的 View JSON 语义用例，覆盖函数输出、表达式输出、星号输出、多层 SELECT 和 bind 条件

## 0.4.0

### 方言能力

- 增加达梦 `SQLPARSER_DIALECT_DAMENG` 方言转换层，覆盖 `SET SCHEMA`、`MINUS`、`LIMIT`、`TOP`、bind、常见 DML/DDL、事务和权限语句
- 增加达梦公共输出规则，反解析和 View JSON 不暴露内部参数名或内部转换 SQL
- 增加 PostgreSQL、MySQL、Oracle、SQL Server 和达梦的预编译 / 参数化 SQL 语句覆盖，包含 SQL Server `sp_executesql` 与达梦 `EXEC SQL PREPARE`

### View JSON 与改写

- 增加通用 `SELECT` 输出列表读取、替换、插入和删除接口
- 增加通用 `WHERE` 条件读取、设置和 `AND` / `OR` 追加接口
- 增加通用 statement 级 `clause` selector，支持通过 `stmt[n].clause[m]` 改写 `select_list`、`where` 和 `order_by`
- View JSON 增加 `query_graph`，用于暴露可写语句级子句槽位

### 测试与文档

- 增加 `SELECT` 输出列表和 `WHERE` 条件改写示例
- 增加通用 `clause` patch 示例，覆盖 SELECT 输出列表、WHERE 条件和 ORDER BY 新增
- 示例目录按 `patch`、`convenience`、`inspect`、`dialect` 分类，推荐接入方优先使用 `patch` 示例
- 增加 PostgreSQL、MySQL、Oracle 和 SQL Server 的 WHERE 改写回归用例，覆盖全部已暴露 `where_clause` 的 PostgreSQL AST 类型
- 增加达梦方言用例矩阵、官方语法覆盖统计、CLI 批量夹具和方言示例
- 同步更新 prepared / bind 相关用例矩阵、方言覆盖统计和官方语法覆盖统计

## 0.3.0

### 方言能力

- 增加 PostgreSQL `SET search_path`、`SET LOCAL search_path` 和 `SET SCHEMA` 结构化输出
- 增加 MySQL `USE db_name` 支持
- 增加 SQL Server `USE database_name` 支持
- 增加 Oracle `ALTER SESSION SET CURRENT_SCHEMA`、`ALTER SESSION SET CONTAINER` 和 `ALTER SESSION SET CONTAINER ... SERVICE ...` 支持
- 修复多语句输入中的 `USE`、`SET SCHEMA` 和 `ALTER SESSION SET` 语句处理，确保 parse、View JSON 和 deparse 均保持公开方言形态

### View JSON 与改写

- 这些语句复用现有 `query_graph values` 结构，不新增独立 JSON 格式
- 支持通过 `stmt[n].value[m]` selector 改写对应值并还原为相应方言 SQL
- 修复 SQL Server、MySQL 和 Oracle deparse 中内部 `sqlparser_current_*` 哨兵泄露的边界问题

### 测试与文档

- 增加 MySQL、Oracle 和 SQL Server 多语句 `USE` / `ALTER SESSION SET` 回归用例
- 同步更新方言支持文档、官方语法覆盖清单和可执行用例覆盖统计

## 0.2.0

### 核心能力

- 提供稳定的 `sql -> handle -> rewrite -> deparse` 公共 C API
- 支持 `SELECT / INSERT / UPDATE / DELETE / MERGE / TRANSACTION / 常见 DDL` 的解析与结构读取
- 支持关系名、名称原子、字面量、`WHERE` 字面量、`UPDATE assignment` 和 `INSERT cell` 的精确改写
- 支持右值表达式级改写，包括 `DEFAULT` 与任意表达式 SQL
- 支持 View JSON、`query_graph` C 结构化遍历和 structured patch 写回
- 提供 View JSON 作为按需结构化导出
- 支持可配置资源限制，覆盖 SQL 输入、表达式 SQL 片段、生成输出和语句数量
- 增加方言公共框架，默认 PostgreSQL，并提供 MySQL、Oracle 与 SQL Server 方言转换层
- 收敛默认输出上限到 4MB，并减少 parse/deparse 路径中的常驻 AST 和字符串拷贝

### 发布与构建

- 固定 vendored `libpg_query` 版本并纳入仓库
- 统一发布公共头文件、静态库、动态库与 `pkg-config` 文件
- 新增严格构建、安装烟测、`valgrind` 泄漏校验、循环回归、benchmark smoke 与一键 `verify` 入口
- 构建系统按编译选项签名自动失效并重建本库对象和 vendor 产物
- 新增 `make abi-check`，校验动态库导出符号与公共头文件一致
- 新增 Linux/GCC GitHub Actions CI 门禁
- CI 增加 JSON fixture 校验和源码包 smoke
- 新增 `make dist` 源码发布包目标
- 新增 Windows/MSVC NMake 构建入口，支持生成静态库、CLI、单元测试与示例程序
- Windows/MSVC 构建使用仓库内 vendored Jansson，避免依赖外部包管理器

### 测试与性能

- 扩充通用 SQL 批量夹具，覆盖子查询、`CASE`、窗口、`ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`、`MERGE`、事务控制、常见 DDL、`GRANT/REVOKE` 与维护语句
- 增加 MySQL 方言用例矩阵，覆盖已支持语句形态和明确不支持语法
- 增加 Oracle 方言用例矩阵，覆盖已支持语句形态、公共输出规则和明确不支持语法
- 增加 SQL Server 方言用例矩阵，覆盖已支持 T-SQL 语句形态、公共输出规则和明确不支持语法
- 增加安装态 API 烟测、`valgrind` 泄漏校验与表达式改写回归
- 增加稳定性回归，覆盖畸形 SQL、参数校验、资源限制和失败改写回滚
- benchmark 增加读取链路、改写链路与 `rewrite + deparse` 单次调用统计
- 增加按能力分类的测试入口，覆盖 parse、inspect、rewrite、deparse、View JSON、CLI、install smoke 和 ABI

### 文档

- 提供中英文快速开始、API 手册、View JSON 手册、CLI 手册与架构文档
- 增加 Oracle、SQL Server 方言支持说明和 `v0.2.0` 发布说明
- 增加公开变更记录
