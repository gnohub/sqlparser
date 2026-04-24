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
- `make test-model-json`
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
- `tests/install/install_smoke.c`
- `tests/cases/sql_batch_input.json`
- `tests/verify_cli_batch.py`

## 覆盖范围

测试覆盖的主要内容包括：

- parse / deparse 基础链路
- 资源限制，包括 SQL 输入、模型 JSON 输入、生成输出和语句数量
- 语句类型与节点识别
- `SELECT / INSERT / UPDATE / DELETE / MERGE`
- 多语句输入
- `ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`
- 常见 DDL、事务控制、`GRANT / REVOKE` 与维护语句
- JSON 导出
- selector 与模型 JSON 回放

## 用例矩阵

- [SQL 用例矩阵](./cases/sql_case_matrix.md)
