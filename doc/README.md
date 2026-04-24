# 文档索引

`doc/` 目录收录 `sqlparser` 的项目文档和接口手册。

## 文档列表

- [项目概览与架构](./sqlparser_architecture.md)
  说明项目定位、分层结构、数据模型和扩展边界。
- [兼容性策略](./compatibility_policy.md)
  说明公共 API、ABI、selector 与模型 JSON 的兼容性边界。
- [API 手册](./api_reference.md)
  说明公共头文件中的主要类型、生命周期规则和函数分组。
- [模型 JSON 手册](./model_json.md)
  说明稳定模型 JSON 的结构、patch 形式和编辑规则。
- [CLI 手册](./cli_guide.md)
  说明命令行工具的运行方式、批量输入格式和输出结构。
- [libpg_query 集成说明](./libpg_query_analysis.md)
  说明解析内核的固定版本、集成方式和维护边界。

## 相关资料

- [快速开始](../README.md)
- [变更记录](../CHANGELOG.md)
- [示例说明](../examples/README.zh-CN.md)
- [测试说明](../tests/README.md)
- [基准测试说明](../bench/README.md)
