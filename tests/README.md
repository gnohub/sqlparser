# 测试说明

`tests/` 目录用于记录和验证 `sqlparser` 的功能覆盖。

## 目录结构

- `tests/unit/`
  单元测试与接口回归测试。
- `tests/cases/`
  记录具名 SQL 用例、批量夹具和补充说明。

## 测试组成

当前测试由以下部分组成：

- 单元测试
- 批量 SQL 夹具验证
- 示例程序烟测

## 执行方式

```bash
make test
```

`make test` 会同时执行：

- 单元测试程序
- CLI 批量夹具检查
- `examples/` 下的示例程序

## 用例文件

常用测试文件包括：

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/cases/sql_batch_input.json`
- `tests/cases/phase1_cases.md`

## 覆盖范围

当前测试覆盖的主要内容包括：

- parse / deparse 基础链路
- 语句类型与节点识别
- `SELECT / INSERT / UPDATE / DELETE`
- 多语句输入
- 常见 DDL 语句
- JSON 导出
- selector 与模型 JSON 回放
