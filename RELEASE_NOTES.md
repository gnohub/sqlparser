# v2.16.5 发布说明

MySQL 与 Vastbase-MySQL 兼容入口支持 JOIN 链和逗号表列表中的多个写入目标。每个 assignment 通过既有 `target_field` 与 field `relation` 表达写入归属；混合目标不输出单一 `dml.target_relation`。

Dameng 支持 JOIN 和逗号表列表形式的多表 UPDATE，但全部 assignment 必须指向同一个 table object。跨目标、未知或歧义限定符明确返回 `SQLPARSER_STATUS_UNSUPPORTED`。既有 assignment、relation patch 和事务回滚规则保持不变。

本版本未新增公开 C 声明、View JSON 字段或资源所有权规则。新增 16 条 final case 和 41 个 patch 后，九套 fixture 合计 2,822 条 final case 和 9,118 个 patch。远端完整 `make test` 通过；核心 API 与三套受影响方言矩阵的定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
