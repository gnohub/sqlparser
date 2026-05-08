# v0.2.0 发布说明

`v0.2.0` 是 `sqlparser` 的正式发布版本，面向 SQL 解析、结构化读取、受控改写和反解析链路。

## 主要变化

- 增加 Oracle 方言转换层，覆盖可安全映射到当前 AST 的常用 Oracle SQL。
- 增加 SQL Server 方言转换层，覆盖可安全映射到当前 AST 的常用 T-SQL。
- 增加 Oracle 和 SQL Server 示例、CLI 方言参数、批量 JSON 方言字段和完整回归矩阵。
- 保持方言公共输出形态，deparse 和 SQL View JSON 不暴露内部 `$N`、`EXCEPT` 等转换细节。
- SQL View 提供 JSON 导出和 C 结构化遍历两种读取方式，structured patch 只接受明确的 `patches` 数组。
- 增强表达式片段改写，支持 Oracle bind 写回，并保证失败改写不提交到 handle。
- 收敛默认输出上限到 4MB，并减少 parse/deparse 路径中的常驻 AST 和字符串拷贝。
- 增加稳定性测试，覆盖畸形 SQL、参数校验、资源限制、失败改写回滚和多方言公共输出稳定性。
- 增强 CI 发布门禁，覆盖 JSON fixture 校验、Linux GCC 验证、ABI 检查、benchmark smoke 和源码包 smoke。
- 修复版本文件变更后的增量构建失效问题，确保运行期版本字符串与 `VERSION` 一致。

## 方言支持边界

Oracle 方言支持范围见 [Oracle 方言支持](./doc/oracle_dialect_support.md)。

当前 Oracle 矩阵包含 58 条用例：39 条支持路径，19 条明确不支持路径。明确不支持的 Oracle 专属语义返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle。

SQL Server 方言支持范围见 [SQL Server 方言支持](./doc/sqlserver_dialect_support.md)。

当前 SQL Server 矩阵包含 56 条用例：41 条支持路径，15 条明确不支持路径。明确不支持的 SQL Server 专属语义返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle。

## 发布验证

本版本的发布门禁包括：

- `git diff --check`
- `jq empty tests/cases/*.json`
- `make verify LOOP=5`
- `make install-smoke`
- `make abi-check`
- `make dist`
- Windows MSVC：`nmake /F Makefile.msvc test`

Linux 发布验证包含 release/debug 构建、sanitizer、valgrind、循环回归、CLI batch、安装态 API smoke 和 benchmark smoke。

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- SQL View JSON：通过 `sqlparser_export_view_json()` 按需导出
- SQL View C 结构化遍历：通过 `sqlparser_get_view()` 和相关 view API 按需读取
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
