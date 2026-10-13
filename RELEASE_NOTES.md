# v2.17.5 发布说明

2.17.5 补充子查询谓词关联，完善 DELETE 别名处理，并修复 SQL Server 事务语句改写后的输出问题。

## 变更

- 补充 13 个方言入口中直接字段 `IN/NOT IN` 谓词的来源查询块，复用已有有序输出项、字段、值和 selector 信息；`NOT IN` 保留原有 `NOT` 父节点结构。
- 补齐 MySQL、SQL Server、达梦及相关兼容入口的单目标 DELETE 别名处理，支持多表来源中的目标定位与改写。
- 修复 SQL Server 及相关兼容入口在混合事务批次改写后丢失 `TRANSACTION` 关键字的问题。
- 补充相关 JSON 回归用例，统一 Oracle 测试目录布局并合并重复参考实现。

## 接口兼容性

- `sqlparser_graph_predicate_t` 新增 `has_source_block` 和 `source_block_index`，由现有 `sqlparser_query_graph_predicate_at()` 返回；View JSON 对应 `source_block`。
- 未新增公共函数，既有函数签名与调用规则不变。公开结构体布局已扩展，升级时需使用新头文件重新编译调用方。

## 调用规则

- 成功解析后可在同一 handle 上连续执行 apply/deparse。deparse 成功不销毁 handle；返回的 SQL 字符串独立分配，由调用方通过 `sqlparser_string_free()` 释放。
- apply 或 deparse 失败后释放内部状态，handle 只能销毁一次，不得继续读取、重试或改写。需要恢复时，使用调用方保存的 SQL 创建新 handle。
- 每次非空 apply 都使旧借用视图失效，包括同值替换及先修改后撤销；成功后必须重新获取视图。只有成功的空 patch 列表保留视图。
- 调用方 SQL 不被修改，source selector 按顺序读取当前状态。已返回的独立 SQL/JSON 字符串不受后续 handle 修改或销毁影响。
- 便捷修改接口沿用相同的失败处理要求；一个 handle 不支持并发修改。

检查入口见[测试说明](tests/README.md)，计时口径与复现命令见[基准测试说明](bench/README.md)。
