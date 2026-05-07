# 兼容性策略

本文档说明 `sqlparser` 对公共 API、ABI、selector 与模型 JSON 的兼容性边界。

## 版本来源

- 版本字符串以仓库根目录 `VERSION` 文件为准
- 运行期可通过 `sqlparser_version_string()` 读取版本文本
- 解析内核版本可通过 `sqlparser_libpg_query_tag()` 读取固定的 `libpg_query` tag
- 模型 schema 可通过 `sqlparser_model_schema_string()` 读取

## 公共 API

- 对外公共头文件为 `include/sqlparser/sqlparser.h`
- 头文件中导出的符号视为公共 C API
- 同一主版本内，已发布函数、枚举值和结构体字段保持兼容
- 需要破坏兼容性的调整通过提升主版本处理

## v0.2 公共面

`v0.2.0-dev` 的公共面包括：

- `sqlparser_parse()`、`sqlparser_parse_with_limits()`、`sqlparser_parse_with_options()`
- `sqlparser_handle_t` 生命周期管理
- 语句、关系、名称原子、字面量、`WHERE` 字面量、`UPDATE assignment` 和 `INSERT cell` 访问接口
- selector 解析、格式化、读取与改写接口
- `parse tree JSON`、`summary JSON`、模型 JSON 和 deparse 输出接口
- 资源限制、版本字符串、模型 schema 和方言名称辅助接口

`SQLPARSER_DIALECT_SQLSERVER` 是保留枚举，当前返回 `SQLPARSER_STATUS_UNSUPPORTED`。MySQL 与 Oracle 方言按各自用例矩阵定义支持边界。

## ABI 与动态库

- 动态库以 `libsqlparser.so.<major>` 发布
- `SONAME` 主版本变更表示 ABI 不兼容
- 同一 `SONAME` 主版本内，二进制接口保持兼容

## selector 语义

- selector 文本是稳定定位路径
- 已发布 selector kind 及其语法形态在同一主版本内保持兼容
- 新增 selector kind 以向后兼容方式扩展，不改变已有 selector 的含义

## 模型 JSON

- 当前模型 schema 固定为 `sqlparser.model/v1`
- 同一 schema 下，既有字段语义保持兼容
- 需要破坏兼容性的模型变更通过新的 schema 名称发布
- 调用方应保留 `selector` 字段原值，不应擅自改写路径文本

## SQL 输出

- `sqlparser_deparse()` 保证语义等价，不保证词法逐字符保持原样
- 输出可能规范化关键字大小写、别名写法、可省略关键字和空白
- 对需要做精确比对的场景，应按语义或稳定字段做校验，而不是依赖原始文本格式

## 依赖边界

- 仓库内 vendored `libpg_query` 使用固定版本
- 版本升级采用显式评估与迁移，不在补丁版本中隐式切换解析内核
