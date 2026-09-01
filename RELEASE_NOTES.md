# v2.16.12 发布说明

Query Graph 对 `WHERE`、`ON` 与 `HAVING` predicate 的 RHS 输出 function 或 opaque expression。function 提供规范化名称、有序 literal/bind/field/expression 参数及 selector；opaque expression 支持整体替换。

`expression` 与 `expression_arg` selector 支持替换，`expression_args` 支持函数参数插入和删除。函数统一按可变参数处理，仅校验 selector、索引及结果 SQL 可解析性，不校验函数签名、参数数量或参数类型。

新增 expression/argument 只读 API、3 类 selector 和 2 类 patch operation。既有公开结构体布局与所有权规则不变，公开符号共 162 个。

新增 54 条 final case 和 196 个 patch 后，九套 fixture 合计 3,062 条 final case 和 9,621 个 patch。完整 `make test`、九套方言矩阵、CLI 和 ABI 导出检查均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
