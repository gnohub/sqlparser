# v2.14.3 发布说明

`v2.14.3` 是 `v2.14.2` 的补丁版本，修正普通 `INSERT ... VALUES` 执行 `insert_column` 后未修改表达式被整句 AST 反解析改写的问题。

## INSERT COLUMN 表面保留

- 对显式目标列的普通 `INSERT ... VALUES`，`insert_column` 按原始 SQL 区间分别向目标列列表和每一行 VALUES 插入列名与默认值，不再为完成该操作反解析整棵 AST。
- 所有目标区间会在写入前完成定位、边界和冲突校验；多行 VALUES 的列和值保持同一插入位置，失败时不会产生部分改写。
- 未修改文本逐字节保留。`DATE '...'`、`TIMESTAMP '...'`、`NOW()`、`CURRENT_TIMESTAMP` 和 `GETDATE()` 等原始表达式不会因新增其他列而改写为 `CAST(...)` 或其他规范化形式。
- 多行规划复用表达式源码扫描状态，已排序的 source edit 使用尾部追加路径，避免随 VALUES 行数增加而重复扫描或移动既有 edit。

## 回归夹具

- 九套可执行方言夹具各增加一个 `insert_column` 回归 patch，覆盖 typed literal 及各方言时间函数，并对 patch 后 SQL 执行精确文本校验。
- Case runner 支持经过严格解析的内部 selector 直接定位 INSERT 目标列列表；既有 JSON Pointer patch 路径保持不变。

## 兼容性

- 本版本没有新增公开 API、枚举或结构体字段。
- 既有函数签名和公开结构体布局保持不变；动态库 ABI 主版本仍为 `libsqlparser.so.0`。

## 发布验证

- 九套可执行方言夹具包含 2,758 条 `status = "final"` 用例和 8,945 个独立 patch。
- 最终代码在远端完成全量 `make test`；原始反解析、View、patch 反解析、重新解析后的二次反解析以及 patch/fresh View 检查全部通过。
- 最终代码完成一次定向 Valgrind 检查；947,143 次分配与释放全部对齐，退出时为 `0 bytes in 0 blocks`，错误数为 0。

内置 `libpg_query` 标签：`17-6.2.2`。
