# v2.16.4 发布说明

根 `INSERT` 冲突更新列表使用 `stmt[S].assignment[A]`，嵌套 `UPDATE` 赋值列表使用 `stmt[S].assignment[D][A]`。既有 assignment 插入、整项替换和删除 patch 现可作用于 MySQL / Vastbase-MySQL `ON DUPLICATE KEY UPDATE`、PostgreSQL / Vastbase-PostgreSQL `ON CONFLICT DO UPDATE` 与 data-modifying CTE 中的嵌套 `UPDATE`，以及 SQL Server / Vastbase-SQLServer 带 `OUTPUT` 的嵌套 `UPDATE`。

View JSON schema、公开 C 声明和资源所有权规则未变；assignment selector 输出范围扩展，其中部分 MySQL 冲突更新项由旧 value selector 改为 assignment selector。`INSERT ... SET` 的字段/值成对改写属于既有能力，本版本仅补充回归覆盖。

远端完整 `make test` 通过；其中六套受影响方言矩阵共 2,158 条 final case、6,798 个 patch。PostgreSQL、MySQL、SQL Server 三套基础方言矩阵的定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。新增 10 条 final case 和 28 个 patch 后，九套 fixture 合计 2,806 条 final case 和 9,077 个 patch。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
