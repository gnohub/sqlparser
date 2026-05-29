# 示例说明

`examples/` 目录包含基于公共头文件 `sqlparser/sqlparser.h` 编写的示例程序，用于展示解析、结构读取、统一 patch 改写、便捷接口改写和方言调用方式。

## 目录结构

- `examples/patch/`
  推荐接入方式，统一使用 `sqlparser_apply_patch()` 完成修改。
- `examples/convenience/`
  便捷接口示例，适合调用方已经持有具体索引或 selector 结构体的场景。
- `examples/inspect/`
  结构读取和遍历示例。
- `examples/dialect/`
  方言解析、View JSON、patch 改写和反解析示例。

## 推荐改写示例

- `examples/patch/08_view_patch.c`
  展示 View JSON 的导出、结构体 patch 应用和回写 SQL。
- `examples/patch/13_select_target_patch.c`
  展示通过 `sqlparser_apply_patch()` 展开 `SELECT *`、插入输出列和删除输出列。
- `examples/patch/14_where_patch.c`
  展示通过 `sqlparser_apply_patch()` 新增 `WHERE`，以及按 `AND` 或 `OR` 追加条件。
- `examples/patch/15_insert_columns_patch.c`
  展示通过 `sqlparser_apply_patch()` 增加和删除 `INSERT ... VALUES` 字段。
- `examples/patch/16_clause_patch.c`
  展示通过通用 `clause` patch 改写 SELECT 输出列表、WHERE 条件和 ORDER BY。
- `examples/patch/17_update_set_patch.c`
  展示通过 `sqlparser_apply_patch()` 追加、删除和整项替换 `UPDATE SET` 赋值项。

## 便捷接口示例

- `examples/convenience/02_insert_values_replace_literal.c`
  展示 `INSERT ... VALUES` 的列读取、单元格定位和字面量替换。
- `examples/convenience/04_update_replace_assignment.c`
  展示 `UPDATE` 语句中 `SET` 赋值项和 `WHERE` 条件的读取与改写。
- `examples/convenience/05_delete_inspect.c`
  展示 `DELETE` 语句的目标表读取和条件字面量改写。
- `examples/convenience/06_ddl_inspect.c`
  展示 DDL 语句的节点识别、名称原子遍历和对象名改写。
- `examples/convenience/09_expression_rewrite.c`
  展示如何读取和改写 `UPDATE assignment`、`INSERT cell` 的任意表达式或 `DEFAULT`。
- `examples/convenience/13_select_target_rewrite.c`
  展示 SELECT 输出列表的便捷接口改写。
- `examples/convenience/14_where_convenience.c`
  展示 WHERE 条件的便捷接口改写。
- `examples/convenience/18_structured_fragment_rewrite.c`
  展示结构化 identifier path 改写：克隆 UPDATE assignment 右值插入备份列，以及将 `SELECT *` 替换为调用方给定的列列表。

## 读取与方言示例

- `examples/inspect/01_select_inspect.c`
  展示 `SELECT` 语句的解析、表信息读取、名称原子遍历和 `WHERE` 字面量读取。
- `examples/inspect/03_insert_select_inspect.c`
  展示 `INSERT ... SELECT` 的结构检查方式。
- `examples/inspect/07_multi_statement_walk.c`
  展示多语句输入的遍历方式。
- `examples/dialect/10_mysql_dialect.c`
  展示如何显式指定 MySQL 方言，并改写 `INSERT ... VALUES` 的单元格。
- `examples/dialect/11_oracle_dialect.c`
  展示如何显式指定 Oracle 方言，导出 View JSON，并在反解析时还原 Oracle bind 占位符。
- `examples/dialect/12_sqlserver_dialect.c`
  展示如何显式指定 SQL Server 方言，导出 View JSON，并在反解析时还原 SQL Server 参数和 `TOP` 语法。
- `examples/dialect/17_dameng_dialect.c`
  展示如何显式指定达梦方言，导出 View JSON，并在反解析时还原 bind、`SET SCHEMA` 和 `TOP` 语法。

## 构建

```bash
make examples
```

## 运行

例如运行 `UPDATE SET` patch 改写示例：

```bash
./bin/examples/patch/17_update_set_patch
```

运行结构化 SQL 片段改写示例：

```bash
./bin/examples/convenience/18_structured_fragment_rewrite
```

## 说明

- 示例程序使用公共 API，不依赖私有头文件。
- 接入层建议优先参考 `examples/patch/`。
- 示例代码中的中文注释用于说明关键调用步骤和资源释放顺序。
- 需要更完整的接口说明时，请参考 [API 手册](../doc/api_reference.md)。
