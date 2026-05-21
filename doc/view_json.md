# SQL View JSON 手册

SQL View JSON 是 `sqlparser` 对外输出的结构化 SQL 视图。它由 SQL View C 结构按需序列化生成，只描述 SQL 文本中实际出现的语句、对象、字段和值片段，不保存输入 SQL 副本，也不输出底层解析器的内部 JSON。

## 导出接口

```c
sqlparser_status_t sqlparser_export_view_json(
    const sqlparser_handle_t *handle,
    int pretty,
    char **out_json,
    sqlparser_error_t *out_error);
```

- `handle` 必须来自一次成功解析。
- `pretty` 非 0 时输出格式化 JSON，0 时输出紧凑 JSON。
- `out_json` 由库分配，调用方使用 `sqlparser_string_free()` 释放。

## C 结构视图

调用方也可以直接遍历 SQL View 结构，不导出 JSON：

```c
sqlparser_view_t view;
sqlparser_statement_view_t statement;
sqlparser_object_view_t object;
sqlparser_column_view_t column;

sqlparser_get_view(handle, &view, &err);
sqlparser_view_statement_at(&view, 0, &statement, &err);
sqlparser_statement_object_at(&statement, 0, &object, &err);
sqlparser_object_column_at(&object, 0, &column, &err);
```

这些 view 结构不复制 AST，也不持有 SQL 字符串副本。字段值和 `INSERT` 单元格需要 SQL 文本时，调用 `sqlparser_value_sql()` 或 `sqlparser_cell_sql()` 按需生成，并用 `sqlparser_string_free()` 释放。

## 顶层结构

顶层只包含 `statements`：

```json
{
  "statements": [
    {
      "index": 0,
      "keyword": "insert",
      "keywords": ["insert", "into", "values"],
      "clauses": [],
      "objects": []
    }
  ]
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `index` | 语句索引，0 基 |
| `keyword` | 当前语句的主关键字 |
| `keywords` | 当前语句中识别到的关键字集合 |
| `clauses` | 语句级子句数组；可写子句带 selector，只读归属子句的 selector 为 `null` |
| `objects` | SQL 中出现的表、视图或可归属对象 |

## 子句结构

`clauses` 表示 statement 级结构。当前可写子句包括 `select_list`、`set_list`、`where` 和 `order_by`。`on`、`group_by`、`having` 等只读子句用于字段归属，`selector` 为 `null`，不能直接作为 patch 目标。

```json
{
  "id": 2,
  "kind": "where",
  "selector": "stmt[0].clause[1]",
  "sql": "status = 'active'"
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `id` | 子句编号，1 基；`objects[].columns[].clause_id` 使用该编号引用子句 |
| `kind` | 子句类型，例如 `select_list`、`set_list`、`where`、`order_by`、`on` |
| `selector` | 可用于 patch 的子句 selector；只读子句为 `null` |
| `sql` | 子句内容；当前语句可新增该子句但尚未出现时为 `null` |

可写 `clauses` 负责结构级改写，例如替换 SELECT 输出列表、替换 UPDATE SET 列表、新增 WHERE、追加 WHERE 条件或新增 ORDER BY。`objects[].columns[]` 通过 `clause_id` 指向 `clauses[].id`。

## 对象结构

`objects` 中的每个对象表示 SQL 中出现的一个可归属对象：

```json
{
  "database": null,
  "schema": "public",
  "table": "users",
  "alias": "u",
  "selector": "stmt[0].relation[0]",
  "columns": [],
  "rows": []
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `database` | 数据库名；SQL 未出现时为 `null` |
| `schema` | schema 名；SQL 未出现时为 `null` |
| `table` | 表名或视图名；SQL 未出现时为 `null` |
| `alias` | 别名；SQL 未出现时为 `null` |
| `selector` | 可用于 patch 的对象 selector；没有可写节点时为 `null` |
| `columns` | 归属到该对象的字段 |
| `rows` | `INSERT ... VALUES` 的行数据 |

## 字段结构

字段只记录 SQL 中出现过的字段。未限定表别名的字段会归属到当前语句中所有可匹配对象，归属结果不是唯一性判断。

```json
{
  "name": "status",
  "keyword": "where",
  "operator": "=",
  "bind": null,
  "bind_kind": 0,
  "bind_sql": null,
  "bind_selector": null,
  "selector": "stmt[0].name[3]",
  "target_list_selector": null,
  "target_selector": null,
  "clause_id": 2,
  "target_path": [],
  "value": {
    "sql": "'active'",
    "selector": "stmt[0].value[7]"
  }
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `name` | 字段名；`SELECT *` 时为 `"*"` |
| `keyword` | 字段出现的 SQL 子句，例如 `select`、`where`、`set`、`on` |
| `operator` | 与字段关联的操作符；没有时为 `null` |
| `bind` | 字段值是预编译占位符时输出归一化 bind 名称；否则为 `null` |
| `bind_kind` | bind 类型枚举：`0` 表示无 bind，`1` 表示位置 bind，`2` 表示命名 bind |
| `bind_sql` | SQL 中出现的原始占位符文本，例如 `"?"`、`":1"`、`":id"`、`"@id"`、`"$1"`；没有 bind 时为 `null` |
| `bind_selector` | bind 对应值表达式的 selector；没有 bind 时为 `null` |
| `selector` | 字段名 selector；没有可写节点时为 `null` |
| `target_list_selector` | 字段位于 SELECT 输出列表时，指向整个 target list；否则为 `null` |
| `target_selector` | 字段位于 SELECT 输出列表时，指向单个输出项；否则为 `null` |
| `clause_id` | 字段所在 `clauses[]` 子句编号，1 基；无法稳定归属时为 `null` |
| `target_path` | 字段在 SELECT 输出项中的函数或表达式包裹路径；非 SELECT 输出字段为 `[]` |
| `value` | 与字段直接关联的 SQL 值片段；没有时为 `null` |

`value.sql` 是可回写的 SQL 片段，不做类型推断。无法安全渲染为独立 SQL 片段的复杂表达式会保持 `value: null`，字段本身仍会输出。
`value.selector` 定位的是整个值表达式，可用于把字面量、bind 参数或表达式替换为新的 SQL 片段。

预编译占位符会输出结构化 bind 字段，并保持 `value: null`。`bind` 是归一化后的名称或序号，`bind_kind` 表示命名或位置类型，`bind_sql` 保留 SQL 中的原始占位符文本。Oracle `:1` 和 JDBC 风格 `?` 都是位置 bind，但 `bind_sql` 分别保留为 `":1"` 和 `"?"`，调用方可以稳定区分来源形态。需要改写 bind 时使用 `bind_selector`。

当一个字段对应多个条件值时，会输出多条同名字段记录，每条记录关联一个具体值。例如 `status IN (:s1, :s2, :s3)` 会输出三条 `name: "status"`、`operator: "IN"` 的记录，并分别携带 `:s1`、`:s2`、`:s3` 的 bind 信息；`NOT IN`、`NOT BETWEEN`、`NOT LIKE`、`NOT ILIKE` 和 `NOT SIMILAR TO` 会在 `operator` 中保留否定语义。

`target_path` 是有序数组，按从外到内的层级排列。数组顺序就是层级关系，不需要额外的排序字段。

每个元素包含：

| 字段 | 说明 |
| --- | --- |
| `kind` | `function` 或 `expression` |
| `name` | 函数名、操作符或表达式类别；无法稳定命名时为 `null` |
| `arg_index` | 当前层中下一层表达式所在的 0 基参数或操作数序号 |

示例：

| SQL 片段 | 字段 | `target_path` |
| --- | --- | --- |
| `SELECT name FROM users` | `name` | `[]` |
| `SELECT * FROM users` | `*` | `[]` |
| `SELECT UPPER(name) FROM users` | `name` | `[{"kind":"function","name":"UPPER","arg_index":0}]` |
| `SELECT LOW(UPPER(name)) FROM users` | `name` | `[{"kind":"function","name":"LOW","arg_index":0},{"kind":"function","name":"UPPER","arg_index":0}]` |
| `SELECT CONCAT(UPPER(first_name), last_name) FROM users` | `first_name` | `[{"kind":"function","name":"CONCAT","arg_index":0},{"kind":"function","name":"UPPER","arg_index":0}]` |
| `SELECT CONCAT(UPPER(first_name), last_name) FROM users` | `last_name` | `[{"kind":"function","name":"CONCAT","arg_index":1}]` |
| `SELECT first_name || last_name FROM users` | `first_name` | `[{"kind":"expression","name":"||","arg_index":0}]` |
| `SELECT first_name || last_name FROM users` | `last_name` | `[{"kind":"expression","name":"||","arg_index":1}]` |
| `SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users` | `amount` | `[{"kind":"function","name":"SUM","arg_index":0}]` |
| `SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users` | `dept` | `[{"kind":"expression","name":"window_partition","arg_index":0}]` |
| `SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users` | `created_at` | `[{"kind":"expression","name":"window_order","arg_index":0}]` |

## INSERT 行数据

`INSERT ... VALUES` 会在目标对象的 `rows` 中输出行和单元格：

```json
{
  "index": 0,
  "cells": [
    {
      "column": "id",
      "column_index": 0,
      "sql": "1",
      "bind": null,
      "bind_kind": 0,
      "bind_sql": null,
      "bind_selector": null,
      "selector": "stmt[0].insert_cell[0][0]"
    },
    {
      "column": "name",
      "column_index": 1,
      "sql": "'bob'",
      "bind": null,
      "bind_kind": 0,
      "bind_sql": null,
      "bind_selector": null,
      "selector": "stmt[0].insert_cell[0][1]"
    }
  ]
}
```

如果 `INSERT` 没有显式列名，`column` 为 `null`，`column_index` 仍保留单元格位置。

## 结构体 Patch

结构体 patch 用于把修改写回同一个 `handle`。

```c
sqlparser_status_t sqlparser_apply_patch(
    sqlparser_handle_t *handle,
    const sqlparser_patch_list_t *patches,
    sqlparser_error_t *out_error);
```

调用方根据 SQL View JSON 或 C 结构视图取得 selector 后，填充 `sqlparser_patch_t` 数组并调用 `sqlparser_apply_patch()`。

支持的操作：

| `op` | 说明 |
| --- | --- |
| `replace` | 替换 relation、name、value、assignment、literal、where_literal、clause、insert_cell、select_target 或 select_targets |
| `insert_column` | 为 `INSERT ... VALUES` 增加一列，或向 `select_targets` 插入一个 SELECT 输出项 |
| `delete_column` | 删除 `INSERT ... VALUES` 的一列，或删除 `select_targets` 中的一个 SELECT 输出项 |
| `delete_row` | 删除 `INSERT ... VALUES` 的一行 |
| `append_condition` | 按 `AND` 或 `OR` 向 `where` 类型的 `clause` 追加条件 |
| `insert_assignment` | 向 `UPDATE SET` 插入完整赋值项，selector 使用 `stmt[n].assignment[i]` |
| `delete_assignment` | 删除 `UPDATE SET` 中的指定赋值项，selector 使用 `stmt[n].assignment[i]` |
| `replace_assignment` | 整项替换 `UPDATE SET` 赋值项，selector 使用 `stmt[n].assignment[i]` |

`delete_column` 不会生成空 `VALUES` 行；删除最后一个单元格会返回 `SQLPARSER_STATUS_UNSUPPORTED`。`delete_row` 不会删除最后一行。`delete_assignment` 不会删除最后一个 `UPDATE SET` 赋值项。

替换值示例：

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].insert_cell[1][1]";
patch.sql = "'lisi'";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

增加列示例：

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
patch.selector = "stmt[0].insert_columns";
patch.index = 2;
patch.name = "age";
patch.default_sql = "18";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

结构级子句示例：

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].clause[2]";
patch.sql = "name DESC, id ASC";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

改写 `UPDATE SET` 示例：

```c
sqlparser_patch_t patches[2];
sqlparser_patch_list_t patch_list;

memset(patches, 0, sizeof(patches));
patches[0].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
patches[0].selector = "stmt[0].assignment[1]";
patches[0].sql = "secret_orig = 'abc'";
patches[1].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
patches[1].selector = "stmt[0].assignment[0]";
patches[1].sql = "secret = 'masked'";

patch_list.items = patches;
patch_list.count = 2;
sqlparser_apply_patch(handle, &patch_list, &err);
```

删除行示例：

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_DELETE_ROW;
patch.selector = "stmt[0].insert_row[1]";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

替换 `SELECT *` 示例：

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].select_targets[0]";
patch.sql = "id, name, upper(name) AS normalized_name";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

## 示例

SQL：

```sql
INSERT INTO users (id, name) VALUES (1, 'xiaohong'), (2, 'xiaoming')
```

输出：

```json
{
  "statements": [
    {
      "index": 0,
      "keyword": "insert",
      "keywords": ["insert", "into", "values"],
      "clauses": [],
      "objects": [
        {
          "database": null,
          "schema": null,
          "table": "users",
          "alias": null,
          "selector": "stmt[0].relation[0]",
          "columns": [
            {
              "name": "id",
              "keyword": "insert",
              "operator": null,
              "bind": null,
              "bind_kind": 0,
              "bind_sql": null,
              "bind_selector": null,
              "selector": "stmt[0].name[0]",
              "target_list_selector": null,
              "target_selector": null,
              "clause_id": null,
              "target_path": [],
              "value": null
            },
            {
              "name": "name",
              "keyword": "insert",
              "operator": null,
              "bind": null,
              "bind_kind": 0,
              "bind_sql": null,
              "bind_selector": null,
              "selector": "stmt[0].name[1]",
              "target_list_selector": null,
              "target_selector": null,
              "clause_id": null,
              "target_path": [],
              "value": null
            }
          ],
          "rows": [
            {
              "index": 0,
              "cells": [
                {
                  "column": "id",
                  "column_index": 0,
                  "sql": "1",
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
                  "selector": "stmt[0].insert_cell[0][0]"
                },
                {
                  "column": "name",
                  "column_index": 1,
                  "sql": "'xiaohong'",
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
                  "selector": "stmt[0].insert_cell[0][1]"
                }
              ]
            },
            {
              "index": 1,
              "cells": [
                {
                  "column": "id",
                  "column_index": 0,
                  "sql": "2",
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
                  "selector": "stmt[0].insert_cell[1][0]"
                },
                {
                  "column": "name",
                  "column_index": 1,
                  "sql": "'xiaoming'",
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
                  "selector": "stmt[0].insert_cell[1][1]"
                }
              ]
            }
          ]
        }
      ]
    }
  ]
}
```

修改第二行 `name`：

```json
{
  "patches": [
    {
      "op": "replace",
      "selector": "stmt[0].insert_cell[1][1]",
      "sql": "'lisi'"
    }
  ]
}
```
