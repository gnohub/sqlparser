# v2.16.3 发布说明

`v2.16.3` 新增 handle 级完整 bind occurrence 读取接口，可按当前整段 SQL 的实际顺序取得全部真实占位符，并保留重复 occurrence。每项提供 `position`、`kind`、`key` 和完整 `sql`。

既有 Query Graph bind 字段继续用于表达语义关联；完整枚举由新接口独立提供，View JSON schema 保持不变。本次公开接口为增量新增，不影响既有调用方式。

严格增量构建、两个定向测试和九套方言矩阵全部通过，共 2,796 条 final case、9,049 个 patch。ABI/export 验证通过，共 154 个公开符号；两个定向 Valgrind 检查均为 `0 bytes in 0 blocks`、0 errors。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
