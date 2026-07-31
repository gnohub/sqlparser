# v2.13.0 发布说明

`v2.13.0` 新增 MERGE 分支的结构化投影和基于 selector 的改写能力，并明确九种方言模式下混合 `INSERT ... VALUES` 与 bind 来源关系的行为，以及 SQL 片段 patch 后标识符的反解析规则。

## 主要变化

- Query Graph 按源码顺序输出 MERGE 的全部 `WHEN` 分支，包含 action、match 类型、INSERT 目标列与行以及 UPDATE assignment 范围；有条件的分支同时包含对应的 condition selector。
- 新增 MERGE branch condition selector、action/match 枚举、两个枚举名称函数和一个 MERGE 分支详情访问函数；顶层与嵌套 MERGE 的分支条件和赋值项均可通过 selector 定位。
- MERGE UPDATE assignment 输出目标字段、来源字段、source target 和 bind 关联，可通过 assignment selector 与 patch 操作完成插入、替换和删除。
- Oracle、达梦和 Vastbase-Oracle 支持 MERGE UPDATE/INSERT action 级 `WHERE`。
- 九种方言模式的 `INSERT ... VALUES` 区分直接 bind、literal、独立 `DEFAULT` 和 expression cell；表达式内部 bind 参与全局序号，时间表达式不作为字段名称输出。
- SQL 片段 patch 中的标识符保留片段内的大小写和定界形式，包括双引号、MySQL 反引号及 SQL Server 方括号；反解析不会替换或重复添加定界符。

## 反解析契约

- generation 为 `0` 的未改写 handle 在成功反解析时与原始 SQL 逐字节一致，包括关键字、标识符、空白、换行、注释、分号和多语句边界。
- generation 大于 `0` 时，SQL 根据当前 AST 生成；标识符的大小写、定界符和转义形式予以保留，节点之间的空白可能由反解析器规范化。
- patch SQL 片段经过方言解析后进入 AST，不作为未经解析的字符串直接拼接。

## 兼容性

- 公共 API 采用追加式扩展，既有函数签名和公共结构体布局保持不变。
- 动态库 ABI 主版本保持为 `libsqlparser.so.0`。
- 新增 `sqlparser_graph_merge_action_kind_name()`、`sqlparser_graph_merge_match_kind_name()` 和 `sqlparser_query_graph_merge_branch_detail()`；ABI 导出检查包含 152 个公共符号。

## 发布验证

- 九套方言用例矩阵包含 2535 条预期成功用例；每条用例均执行 generation-`0` 逐字节反解析检查、AST 标识符拼写审计和 View 构建。
- 九套矩阵新增共 90 条混合 VALUES 用例，覆盖单行、多行、嵌套 bind、函数与复合表达式、带定界符的标识符及非常规空白。
- MERGE 回归覆盖分支顺序、条件 selector、INSERT 行、UPDATE assignment、DELETE/NOTHING action、顶层与嵌套 MERGE、来源关系以及 action 级 `WHERE`。
- GCC 8.3 严格发布与调试构建、全量测试、install smoke、包含 152 个公共符号的 ABI 导出检查、ASan、UBSan、Valgrind 和 benchmark smoke 全部通过。

内置 `libpg_query` 标签：`17-6.2.2`。
