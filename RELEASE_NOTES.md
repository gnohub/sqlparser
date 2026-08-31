# v2.16.11 发布说明

Query Graph 对 CTE 显式列名进行按序映射：来源 block 直接包含可枚举 targets 时，显式名称与 quoted 状态按 ordinal 覆盖 target 输出。重复引用共享同一覆盖结果，DML `source_target` 使用覆盖后的名称解析；PostgreSQL 合法短列表只覆盖对应前缀。

SET/recursive CTE branch 保留自身输出，星号不展开或猜测映射，无法建立完整直接对应关系时保持原边界。该实现不新增公开 API、结构体字段、字符串分配或所有权规则。

新增 40 条 final case 和 46 个 patch 后，九套 fixture 合计 3,008 条 final case 和 9,425 个 patch。完整 `make test`、九套方言矩阵、core/identifier 定向测试及 identifier Valgrind 均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
