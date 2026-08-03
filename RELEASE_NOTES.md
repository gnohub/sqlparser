# v2.14.0 发布说明

`v2.14.0` 完成 patch、反解析和 View 的一致性闭环：未修改 SQL 表面在局部改写后继续保留，复合 DML assignment 可追踪右侧来源，并且同一 statement 可表达多个并列根 DML。

## Patch 与反解析

- 未修改的 SQL 片段逐字节保留原始标识符大小写与定界符、关键字、注释、空白、括号和分号；局部 patch 只替换已定位的源码区间。
- patch 值作为所选方言的 SQL 片段解析后进入 AST。片段中显式提供的双引号、MySQL 反引号或 SQL Server 方括号不会被替换或重复添加。
- relation 改名只传播到作用域内唯一绑定的限定引用。同层歧义、内层遮蔽、显式 alias 以及 SQL Server `INSERTED`、`DELETED` 等伪关系保持不变。
- 替换单个 SELECT target 时，多 target 片段在原位置展开，并且不继承旧 target 的 alias。

## Query Graph 与 View

- `sqlparser_graph_dml_assignment_t` 新增 `rhs_fields`、`rhs_values` 和 `rhs_blocks`，用于遍历复合 UPDATE/MERGE assignment 右侧的字段、值和子查询入口。
- 单个根 DML 继续输出为 `query_graph.dml`；多个并列根 DML 输出为 `query_graph.dmls`。嵌套 DML 继续位于各根的 `children`，数据修改 CTE 使用相同结构。
- 新增 `SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION`，命名窗口定义的 `PARTITION BY` 列表可独立定位。

## 方言边界

- Oracle、达梦及 Vastbase-Oracle 补充集合运算、多表 DML、bind 和 national literal 的解析、来源关系及表面保留；集合树遍历不依赖固定分支数量上限。
- MySQL 及 Vastbase-MySQL 修正普通注释、可执行注释、索引提示、表分区和 DML 尾部在 patch 路径中的恢复。
- SQL Server 及 Vastbase-SQLServer 修正 `OUTPUT`、MERGE、动态执行、事务批次和方括号标识符在 patch 路径中的恢复。

## 兼容性

- `sqlparser_graph_dml_assignment_t` 的公开布局已扩展。使用该结构体的 C 调用方必须使用 2.14.0 头文件重新编译。
- View 消费方必须同时识别互斥的 `query_graph.dml` 与 `query_graph.dmls`；不能再假定每个 statement 只有一个根 DML。
- patch 片段仍经过方言解析，不作为未经解析的字符串直接拼接。

## 发布验证

- 九套可执行方言夹具包含 2,752 条 `status = "final"` 用例和 8,890 个独立 patch。
- 每条用例校验原始 SQL 的逐字节反解析、期望 View JSON 结构和全部独立 patch；每个 patch 还校验期望 SQL、重新解析后的二次反解析以及 patch/fresh View 一致性。
- 发布候选代码完成一次 ASan、一次 UBSan、一次 Valgrind、10 轮全量回归和完整 benchmark。benchmark 共执行 530,100 次测量操作，错误操作数为 0；该结果不表示相对历史版本的性能提升。

内置 `libpg_query` 标签：`17-6.2.2`。
