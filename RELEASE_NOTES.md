# v2.16.19 发布说明

优化 UPDATE、WHERE 批量字符串替换的节点定位、INSERT 值类型读取及库生成字符串片段的校验，减少重复查找和临时解析树构建。优化 MySQL、Vastbase MySQL 和 KingbaseES MySQL 入口的预处理扫描，提前跳过不适用的语句处理流程。

保持修改顺序、来源读取和整批失败回滚语义。公开 API、公开结构体布局和资源上限不变，不增加常驻 AST 缓存。

与 2.16.18 同机对比，MySQL 5,000 行、5,000 项字符串替换的一次 `sqlparser_apply_patch()` 耗时由约 270 毫秒降至 105 毫秒。实际收益取决于 SQL 和 patch 组合。

相关回归通过；5,000 行 INSERT 和 MySQL 语句分派用例的 Valgrind 检查未报内存错误或泄漏。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
