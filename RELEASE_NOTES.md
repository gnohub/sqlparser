# v2.14.4 发布说明

`v2.14.4` 是 `v2.14.3` 的补丁版本，修正结构化 patch 转入 AST fallback 后 handle 仍保留旧源码表面状态，导致后续连续 patch 丢失新增节点的问题。

## 连续结构化 Patch

- 当结构化 patch 无法使用局部源码 edit 时，fallback 路径会同步清除本次调用与 handle 级的源码表面完整标记，保证 AST 与当前源码状态一致。
- 修复前，Oracle 兼容的 `INSERT ALL` 使用 `source_selector` 新增列和值后，内部反解析和重新解析可能恢复 patch 前的旧源码。首次调用表面返回成功，但新增 cell 实际丢失，后续替换该 cell 会报告 `cell index is out of range`。
- 修复后，同一 handle 可以连续完成插列和新增 cell 替换，调用之间不需要执行 View、deparse 或重新 parse。最终反解析仅体现指定修改，并保留未修改分支、绑定参数及其他原始 SQL 文本。

## 回归覆盖

- Oracle、达梦和 Vastbase-Oracle 使用相同的表驱动单元回归。
- 每条回归语句包含两个 `INSERT ALL` 分支，每个分支包含 32 个目标列和值。
- 从未修改的 handle 开始，连续执行四次 `insert_column` 和四次新增 cell `replace`，共八次独立 `apply_patch`。
- 测试校验每次调用的状态与 generation、最终 SQL 的精确文本，以及重新解析后的反解析稳定性。

## 兼容性

- 本版本没有新增公开 API、枚举或结构体字段。
- 既有函数签名和公开结构体布局保持不变；动态库 ABI 主版本仍为 `libsqlparser.so.0`。

## 发布验证

- 九套可执行方言夹具仍包含 2,758 条 `status = "final"` 用例和 8,945 个独立 patch。
- 最终代码在远端完成全量 `make test`，退出码为 0。
- 定向 Valgrind 检查共执行 1,018,764 次分配和 1,018,764 次释放，退出时为 `0 bytes in 0 blocks`，错误数为 0。

内置 `libpg_query` 标签：`17-6.2.2`。
