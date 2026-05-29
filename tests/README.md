# 测试说明

`tests/` 目录用于记录和验证 `sqlparser` 的功能覆盖。

## 目录结构

- `tests/unit/`
  单元测试与接口回归测试。
- `tests/cases/`
  记录具名 SQL 用例、批量夹具和补充说明。

## 测试组成

测试由以下部分组成：

- 单元测试
- 批量 SQL 夹具验证
- 示例程序烟测
- 安装态 API 烟测
- 严格编译与 sanitizer 门禁
- `valgrind` 泄漏校验
- 长时间循环回归
- 稳定性与异常输入回归

## 执行方式

```bash
make test
```

`make test` 会同时执行：

- 单元测试程序
- CLI 批量夹具检查
- `examples/` 下的示例程序

常用质量门禁入口：

- `make test-parse`
- `make test-inspect`
- `make test-rewrite`
- `make test-deparse`
- `make test-view-json`
- `make test-cli`
- `make test-install`
- `make test-abi`
- `make verify-release`
- `make verify-debug`
- `make verify-asan`
- `make verify-ubsan`
- `make verify-valgrind`
- `make test-loop LOOP=50`
- `make verify`

## 用例文件

常用测试文件包括：

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_robustness.c`
- `tests/unit/test_stability.c`
- `tests/install/install_smoke.c`
- `tests/cases/sql_batch_input.json`
- `tests/cases/mysql_dialect_input.json`
- `tests/cases/oracle_dialect_input.json`
- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/dameng_dialect_input.json`
- `tests/verify_cli_batch.py`

## 覆盖范围

测试覆盖的主要内容包括：

- parse / deparse 基础链路
- 资源限制，包括 SQL 输入、生成输出和语句数量
- 语句类型与节点识别
- `SELECT / INSERT / UPDATE / DELETE / MERGE`
- 多语句输入
- `ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`
- 常见 DDL、事务控制、`GRANT / REVOKE` 与维护语句
- JSON 导出
- selector 与结构体 patch 回放
- `SELECT` 输出列表替换、插入、删除和改写后二次解析校验
- 结构化 SQL 片段改写，包括克隆 UPDATE assignment 右值插入新赋值项，以及用结构化列列表替换 SELECT 输出项
- `WHERE` 条件新增、替换、AND/OR 追加和改写后二次解析校验，覆盖 `SELECT`、`UPDATE`、`DELETE`、`INSERT ... SELECT`、`ON CONFLICT`、`VIEW`、`INDEX`、`COPY FROM`、`CREATE RULE`、`CREATE PUBLICATION` 和排他约束
- MySQL 方言转换层的解析、反解析和明确不支持语法返回码
- Oracle 方言转换层的解析、反解析和明确不支持语法返回码
- SQL Server 方言转换层的解析、反解析和明确不支持语法返回码
- 达梦方言转换层的解析、反解析和明确不支持语法返回码
- 公共 API 空指针、越界访问、错误 selector、错误 patch、畸形输入和重复解析的抗崩溃回归
- 参数校验、资源限制、畸形 SQL、失败改写回滚和方言公共输出稳定性

## 用例矩阵

- [SQL 用例矩阵](./cases/sql_case_matrix.md)
- [MySQL 方言用例矩阵](./cases/mysql_dialect_matrix.md)
- [Oracle 方言用例矩阵](./cases/oracle_dialect_matrix.md)
- [SQL Server 方言用例矩阵](./cases/sqlserver_dialect_matrix.md)
- [达梦方言用例矩阵](./cases/dameng_dialect_matrix.md)
