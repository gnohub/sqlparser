# 文档索引

`doc/` 目录收录 `sqlparser` 的项目文档和接口手册。

## 文档列表

- [项目概览与架构](./sqlparser_architecture.md)
  说明项目定位、分层结构、数据模型和扩展边界。
- [PostgreSQL 方言支持](./postgresql_dialect_support.md)
  说明默认 PostgreSQL 方言的支持范围、输出规则和回归边界。
- [MySQL 方言支持](./mysql_dialect_support.md)
  说明 MySQL 方言转换层的支持范围、明确不支持范围和输出规则。
- [Oracle 方言支持](./oracle_dialect_support.md)
  说明 Oracle 方言转换层的支持范围、明确不支持范围和输出规则。
- [SQL Server 方言支持](./sqlserver_dialect_support.md)
  说明 SQL Server 方言转换层的支持范围、明确不支持范围和输出规则。
- [达梦方言支持](./dameng_dialect_support.md)
  说明达梦方言转换层的支持范围、明确不支持范围和输出规则。
- [方言覆盖统计](./dialect_coverage.md)
  汇总 PostgreSQL、MySQL、Oracle、SQL Server 和达梦的可执行回归覆盖情况。
- [PostgreSQL 官方语法覆盖统计](./postgresql_official_syntax_coverage.md)
  记录 PostgreSQL 默认方言相对于官方 SQL Commands 的覆盖情况。
- [MySQL 官方语法覆盖统计](./mysql_official_syntax_coverage.md)
  记录 MySQL 方言相对于 MySQL 8.4 官方文档的覆盖情况。
- [Oracle 官方语法覆盖统计](./oracle_official_syntax_coverage.md)
  记录 Oracle 方言相对于 Oracle Database SQL Language Reference 的覆盖情况。
- [SQL Server 官方语法覆盖统计](./sqlserver_official_syntax_coverage.md)
  记录 SQL Server 方言相对于 Microsoft Transact-SQL Reference 的覆盖情况。
- [达梦官方语法覆盖统计](./dameng_official_syntax_coverage.md)
  记录达梦方言相对于达梦官方 DM_SQL 文档的覆盖情况。
- [API 手册](./api_reference.md)
  说明公共头文件中的主要类型、生命周期规则和函数分组。
- [View JSON 手册](./view_json.md)
  说明 View JSON 的结构、结构体 patch 形式和编辑规则。
- [CLI 手册](./cli_guide.md)
  说明命令行工具的运行方式、批量输入格式和输出结构。
- [libpg_query 集成说明](./libpg_query_analysis.md)
  说明解析内核的固定版本、集成方式和维护边界。

## 相关资料

- [快速开始](../README.md)
- [发布说明](../RELEASE_NOTES.md)
- [变更记录](../CHANGELOG.md)
- [示例说明](../examples/README.zh-CN.md)
- [测试说明](../tests/README.md)
- [基准测试说明](../bench/README.md)
