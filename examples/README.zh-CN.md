# 示例说明

`examples/` 目录包含基于公共头文件 `sqlparser/sqlparser.h` 编写的示例程序，用于展示解析、结构读取、受控改写和反解析的基本用法。

## 示例列表

- `examples/01_select_inspect.c`
  展示 `SELECT` 语句的解析、表信息读取、名称原子遍历和 `WHERE` 字面量读取。
- `examples/02_insert_values_replace_literal.c`
  展示 `INSERT ... VALUES` 的列读取、单元格定位和字面量替换。
- `examples/03_insert_select_inspect.c`
  展示 `INSERT ... SELECT` 的结构检查方式。
- `examples/04_update_replace_assignment.c`
  展示 `UPDATE` 语句中 `SET` 赋值项和 `WHERE` 条件的读取与改写。
- `examples/05_delete_inspect.c`
  展示 `DELETE` 语句的目标表读取和条件字面量改写。
- `examples/06_ddl_inspect.c`
  展示 DDL 语句的节点识别、名称原子遍历和对象名改写。
- `examples/07_multi_statement_walk.c`
  展示多语句输入的遍历方式。
- `examples/08_model_roundtrip.c`
  展示稳定模型 JSON 的导出、patch 应用和回写 SQL。

## 构建

```bash
make examples
```

## 运行

例如运行 `INSERT ... VALUES` 改写示例：

```bash
./bin/examples/02_insert_values_replace_literal
```

## 说明

- 示例程序使用公共 API，不依赖私有头文件。
- 示例代码中的中文注释用于说明关键调用步骤和资源释放顺序。
- 需要更完整的接口说明时，请参考 [API 手册](../doc/api_reference.md)。
