# v2.14.2 发布说明

`v2.14.2` 是 `v2.14.1` 的补丁版本，修正 `INSERT ... VALUES` typed literal 与 SQL Server `INSERT ... OUTPUT` 在局部 patch 后的源码表面退化问题。

## INSERT VALUES 表面保留

- 对于能够可靠定位源码区间的普通 `INSERT ... VALUES` cell，方言合法的字符串、typed literal、函数及复合表达式 patch 均可执行局部替换；patch 片段完成方言解析后，仅替换目标区间，未修改部分保持原文。
- Oracle、达梦和 Vastbase-Oracle 的 `DATE '...'` 与 `TIMESTAMP '...'` 在替换自身或其他 cell 后保持 typed literal 形式，不再改写为 `CAST(...)`。
- patched handle 的 expression cell 从当前 surface SQL 读取。在保持局部源码表面的多 patch 中，`source_selector` 会在读取前固化既有局部改写，确保克隆的是当前 SQL 文本。
- patch 后 SQL 重新解析时，反解析结果与 patch 输出逐字节一致；patched/fresh View 继续保持一致。

## SQL Server INSERT OUTPUT

- SQL Server 与 Vastbase-SQLServer 中边界可验证的简单 `INSERT ... OUTPUT ... VALUES` 使用局部源码改写，覆盖 client、`OUTPUT INTO` 和双结果通道。
- OUTPUT target、sink relation 和 sink column 均通过实际源码区间定位，避免因结果通道组合而回退整句 AST 反解析。
- 目标 relation 与列列表之间不再主动增加空格；`t(a)`、`audit(id)`、方括号标识符、原始大小写及非常规空白均按输入保留。

## 兼容性

- 本版本没有新增公开 API、枚举或结构体字段。
- 既有函数签名和公开结构体布局保持不变；动态库 ABI 主版本仍为 `libsqlparser.so.0`。

## 发布验证

- 九套可执行方言夹具包含 2,758 条 `status = "final"` 用例和 8,936 个独立 patch。
- 远端严格构建和九套 runner 全部通过；原始反解析、View、patch 反解析、重新解析后的二次反解析以及 patch/fresh View 均为 0 失败。
- typed literal、current surface SQL 与多 patch `source_selector` 路径完成一次定向 Valgrind 检查；退出时为 `0 bytes in 0 blocks`，错误数为 0。

内置 `libpg_query` 标签：`17-6.2.2`。
