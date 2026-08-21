# v2.16.7 发布说明

普通单表 `INSERT ... VALUES` 的既有 `SQLPARSER_PATCH_INSERT_COLUMN` 支持仅增加列名，并可在同一个 patch list 中与成对插入及 `REPLACE insert_cell` 组合；提交前每个 row 必须满足列值等长，否则整批回滚。Oracle、Dameng 与 Vastbase-Oracle 兼容入口对当前已建模的 `INSERT ALL/FIRST` 显式单 VALUES branch 提供相同的 branch 级能力，其他 branch 与 source SELECT 保持不变。

MERGE INSERT 仍要求列值成对插入；`DEFAULT VALUES`、MySQL `INSERT ... SET`、省略 branch `VALUES` 和 branch 多 tuple 不在本次范围。实现复用既有 patch operation、selector 与事务候选，未新增公开 API、枚举、View JSON schema 或持久状态。九套 fixture 统计保持 2,831 条 final case 和 9,136 个 patch。功能代码完成后，远端完整 `make test`、定向核心 API 测试及 Valgrind 均通过，Valgrind 为 `0 bytes in 0 blocks`、0 errors。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
