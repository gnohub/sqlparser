# v2.16.2 发布说明

`v2.16.2` 将 Oracle、Vastbase-Oracle 兼容入口与 Dameng 的 DML 结果接收列表从单对扩展为 `N >= 1` 个 target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 一一对应。Oracle-family 使用 `RETURNING ... INTO`；Dameng 的 `INSERT`、`DELETE` 使用 `RETURNING`，`UPDATE` 使用 `RETURN`。Query Graph 继续使用既有 sink result channel，每个 target 的 `sink_value` 指向同序号输出 bind；没有增加公开 View 字段。

既有 `SQLPARSER_PATCH_INSERT_COLUMN` 现在可对 `dml_result_targets` 执行原子成对插入：`index` 指定两侧共同位置，`default_sql` 提供 target SQL，`name` 提供 receiver。Oracle、Dameng 与 Vastbase-Oracle 使用冒号 bind receiver；SQL Server 与 Vastbase-SQLServer 对带显式非空 sink column list、且改写前两侧等长的 `OUTPUT ... INTO` 使用 sink column receiver。任何失败都由事务候选回滚，原 handle 与 generation 保持不变。

本版本的边界如下：

- Oracle 与 Vastbase-Oracle 的 `INSERT`、`UPDATE`、`DELETE` 支持等长 `RETURNING ... INTO`；Dameng 的 `INSERT`、`DELETE` 支持等长 `RETURNING ... INTO`，`UPDATE` 支持等长 `RETURN ... INTO`。
- `BULK COLLECT`、非冒号 bind receiver、空列表和不等长 target/bind 列表仍不支持。
- SQL Server-family 原本合法的不等长 `OUTPUT ... INTO` 仍可解析和反解析，但 paired patch 仅适用于显式非空且改写前等长的 sink column list；client `OUTPUT` 和无显式列列表的通道不适用。
- PostgreSQL-family 没有与 client `RETURNING` target list 对应的 SQL receiver list，因此保持 target-only 改写；MySQL-family 不增加 DML `RETURNING INTO`。
- Vastbase-Oracle 与 Vastbase-SQLServer 的行为属于本项目兼容入口合同，不声明 Vastbase 服务端提供相同的官方语法范围。

五个适用入口共新增 15 条 final case 和 15 个 paired patch：每个入口以 `INSERT`、`UPDATE`、`DELETE` 三条 8 对用例覆盖头、中、尾插入，patch 后严格变为 9 对。九套 fixture 当前合计 2,796 条 final case 和 9,049 个 patch。远端严格核心 API 测试及五套受影响方言矩阵全部通过；五套矩阵合计 1,876 条 case 和 5,996 个 patch。六项定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。

本版本没有新增公开函数、公开枚举、公开结构体字段、View JSON 字段或资源所有权规则。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
