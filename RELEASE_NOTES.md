# v2.17.7 发布说明

2.17.7 修复赋值项改写中的标识符定界信息丢失问题。

## 变更

- 通过 `name + source_selector` 新增赋值项时保留目标名称的定界形式及 `quoted_identifier`，支持后续单项或同批改写。
- 补齐达梦 SQL 片段转换中的标识符来源映射，补充 13 个方言入口的相关 JSON 回归用例。

## 接口兼容性

- 公共函数签名、结构体布局、selector 及调用规则与 2.17.6 保持一致。

## 调用规则

- 成功解析后可在同一 handle 上连续执行 apply/deparse。deparse 成功不销毁 handle；返回的 SQL 字符串独立分配，由调用方通过 `sqlparser_string_free()` 释放。
- apply 或 deparse 失败后释放内部状态，handle 只能销毁一次，不得继续读取、重试或改写。需要恢复时，使用调用方保存的 SQL 创建新 handle。
- 每次非空 apply 都使旧借用视图失效，包括同值替换及先修改后撤销；成功后必须重新获取视图。只有成功的空 patch 列表保留视图。
- 调用方 SQL 不被修改，source selector 按顺序读取当前状态。已返回的独立 SQL/JSON 字符串不受后续 handle 修改或销毁影响。
- 便捷修改接口沿用相同的失败处理要求；一个 handle 不支持并发修改。

检查入口见[测试说明](tests/README.md)，计时口径与复现命令见[基准测试说明](bench/README.md)。
