# View JSON 手册

View JSON 是 `sqlparser` 对 `query_graph` 结构的按需 JSON 导出，主要用于回归测试、集成验证和跨语言查看解析结果。业务代码优先使用公共 C 结构接口读取 `sqlparser_query_graph_view_t`，不需要为了改写 SQL 先生成 JSON。

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
- JSON 在调用导出函数时临时生成；解析阶段不会默认构造 JSON 字符串。

## 顶层结构

```json
{
  "statements": [
    {
      "index": 0,
      "keyword": "select",
      "query_graph": {
        "root": 0,
        "blocks": [
          {
            "kind": "select",
            "relations": [0],
            "targets": [0]
          }
        ],
        "relations": [
          {
            "block": 0,
            "kind": "base",
            "table": "users"
          }
        ],
        "targets": [
          {
            "block": 0,
            "ordinal": 0,
            "kind": "field",
            "name": "id",
            "field": 0
          }
        ],
        "fields": [
          {
            "block": 0,
            "clause": "select_list",
            "relation": 0,
            "column": "id",
            "target": 0
          }
        ]
      }
    }
	  ]
}
```

每条语句固定包含：

| 字段 | 说明 |
| --- | --- |
| `index` | 语句索引，0 基 |
| `keyword` | 当前语句主关键字 |
| `query_graph` | 当前语句的结构化查询图 |

`query_graph` 不展开数据库元数据，不推断 SQL 中没有出现的真实字段。它只表达当前 SQL 文本可见的查询块、关系、输出项、字段引用、值、集合运算和 DML 写入结构。

JSON 只输出有意义的可选字段。公共 C 结构中由 `has_*` 或 `count` 表达不存在的字段，在 JSON 中默认省略，不输出 `null` 或空数组。

## query_graph

| 字段 | 说明 |
| --- | --- |
| `root` | 根查询块编号；没有查询块时省略 |
| `blocks` | 查询块数组，包含普通 SELECT、派生表、CTE、集合运算和标量子查询；非空时存在 |
| `relations` | SQL 中出现的基础表、派生表或 CTE 引用；非空时存在 |
| `targets` | SELECT 输出项、星号输出、DML 输出来源等；非空时存在 |
| `fields` | SQL 文本中出现的字段引用 occurrence；非空时存在 |
| `values` | 与字段关联的字面量、bind、DEFAULT 值；分页或伪列 bind 不进入该数组；非空时存在 |
| `sets` | `UNION`、`UNION ALL`、`INTERSECT`、`EXCEPT/MINUS` 等集合运算；非空时存在 |
| `dml` | `INSERT`、`UPDATE`、`DELETE` 等写入结构；仅 DML 语句存在 |

数组中的编号均为当前语句内的 0 基索引。`relations[].source_block`、`targets[].source_block`、`targets[].star_relations` 和 `sets[].branches` 可组合表达派生表、星号和集合运算的来源链路。

## 派生表和星号

示例：

```sql
SELECT *
FROM (
  SELECT ROWNUM, *
  FROM (
    SELECT *
    FROM (
      SELECT o.*, ROWNUM AS rnum
      FROM (
        SELECT x.id FROM users x
        UNION
        SELECT y.id FROM archived_users y
      ) o
    )
  ) b
) d
```

对应关系通过 `query_graph` 表达：

- `relations[].alias = "d"` 的 `source_block` 指向内部查询块。
- `relations[].alias = "b"` 的 `source_block` 继续指向下一层查询块。
- `relations[].alias = "o"` 的 `source_block` 指向集合运算结果块。
- 最外层 `SELECT *` 在 `targets[]` 中表现为 `kind = "star"`，`star_relations` 指向 `d`。
- `b` 层的 `*` 同样通过 `star_relations` 指向 `b`，并通过 `source_block` 进入下一层。
- `o.*` 表现为 `kind = "qualified_star"`，`source_block` 指向 `UNION` 结果块。

这种表达不展开 `*` 对应的真实字段，也不会把同一个字面 occurrence 复制到多个对象下。调用方需要追踪来源时沿 `relation -> source_block -> target -> set branch` 逐层读取即可。

## relation

```json
{
  "block": 0,
  "kind": "base",
  "schema": "public",
  "table": "users",
  "alias": "u",
  "selector": "stmt[0].relation[0]"
}
```

| 字段 | 说明 |
| --- | --- |
| `block` | 该关系所在查询块 |
| `kind` | `base`、`derived`、`cte` 等 |
| `database` | SQL 中出现的数据库名；未出现时省略 |
| `schema` | SQL 中出现的 schema；未出现时省略 |
| `table` | SQL 中出现的表名；派生表没有表名时省略 |
| `alias` | SQL 中出现的别名；未出现时省略 |
| `source_block` | 派生表或 CTE 指向的查询块；没有来源块时省略 |
| `selector` | 可用于 patch 的关系 selector；没有可写节点时省略 |

## target

```json
{
  "block": 0,
  "ordinal": 0,
  "kind": "field",
  "name": "id",
  "field": 2,
  "selector": "stmt[0].select_target[0][0]",
  "target_list_selector": "stmt[0].select_targets[0]"
}
```

| 字段 | 说明 |
| --- | --- |
| `block` | 输出项所在查询块 |
| `ordinal` | 输出项在当前 SELECT 列表中的序号 |
| `kind` | `field`、`star`、`qualified_star`、`literal`、`subquery`、`pseudo`、`expression` |
| `name` | 输出名或别名；没有时省略 |
| `field` | 直接字段输出对应的 `fields[]` 索引；不适用时省略 |
| `star_relations` | `*` 或 `alias.*` 覆盖的 relation 索引；非星号输出时省略 |
| `source_block` | 派生输出进入的来源查询块；没有时省略 |
| `selector` | 单个输出项 selector；没有可写节点时省略 |
| `target_list_selector` | 当前 SELECT 输出列表 selector；没有可写节点时省略 |

## field

```json
{
  "block": 0,
  "clause": "where",
  "relation": 0,
  "column": "status",
  "selector": "stmt[0].name[5]"
}
```

| 字段 | 说明 |
| --- | --- |
| `block` | 字段所在查询块 |
| `clause` | 字段出现的子句，例如 `select_list`、`where`、`on`、`order_by` |
| `relation` | 稳定归属到的 relation 索引；无法唯一归属时省略 |
| `candidate_relations` | 未限定字段在多 relation 作用域下的候选 relation 索引；没有候选列表时省略 |
| `column` | 字段名；`*` 由 `targets[]` 表达，不作为普通 field 输出 |
| `target` | 字段属于 SELECT 输出项时对应的 target 索引；否则省略 |
| `selector` | 字段名 selector；没有可写节点时省略 |
| `target_path` | 字段在输出表达式中的有序路径；直接字段或非输出字段时省略 |

`target_path` 按从外到内排列。例如 `LOWER(UPPER(name))` 的 `name` 路径为 `LOWER -> UPPER`；`CONCAT(a, b)` 中 `a` 和 `b` 会分别带不同的 `arg_index`。

函数调用不会作为独立的 target kind 输出。`SELECT UPPER(name)` 这类场景中，target kind 为 `expression`，字段 `name` 的函数层级由 `fields[].target_path` 表达。

## value

```json
{
  "block": 0,
  "clause": "where",
  "operator": "=",
  "field": 1,
  "field_match_kind": "direct_field",
  "kind": "bind",
  "bind_key": "id",
  "bind_kind": 2,
  "bind_sql": ":id",
  "bind_position": 1,
  "selector": "stmt[0].value[6]"
}
```

| 字段 | 说明 |
| --- | --- |
| `block` | 值所在查询块 |
| `clause` | 值出现的子句 |
| `operator` | 与值关联的操作符；没有时省略 |
| `field` | 关联字段索引；无法关联字段的分页或伪列值不会进入 `values[]` |
| `field_match_kind` | 字段匹配形态；`direct_field` 表示直接字段，`expression_field` 表示字段位于函数、类型转换、表达式或 `CASE` 中 |
| `kind` | `literal`、`bind`、`default`、`expression` |
| `bind_key` | 预编译占位符 key；没有 bind 时省略 |
| `bind_kind` | `0` 无 bind，`1` 位置 bind，`2` 命名 bind |
| `bind_sql` | SQL 中出现的原始占位符文本；没有 bind 时省略 |
| `bind_position` | 整条输入 SQL 中第几个 bind occurrence，1 基；没有 bind 时省略 |
| `selector` | 值 selector；没有可写节点时省略 |
| `literal` | 字面量结构；非字面量时省略 |

字符串 literal 来源于带引号标识符 token 时，`literal` 对象会输出 `quoted_identifier: true`。普通字符串字面量和未加引号标识符不输出该字段。

多语句 SQL 中，`bind_position` 按整条输入 SQL 全局递增，不按 statement 重置。

`WHERE`、`JOIN ... ON`、`HAVING` 以及 SELECT 投影内部的条件表达式中，可稳定归属到单个字段的 `IN`、`NOT IN`、`BETWEEN`、普通比较和单字段函数包裹条件会输出字段关联值。`field_match_kind` 用于区分 `secret = ?` 这类直接字段匹配和 `UPPER(secret) = ?`、`CAST(secret AS ...) = ?`、`secret || 'x' = ?`、`CASE ... THEN secret END = ?` 这类表达式字段匹配。右侧表达式包含其他字段引用时不会强行归属，避免产生错误字段和值关系。

## 改写

View JSON 中的 `selector` 可用于构造 `sqlparser_patch_t`，再调用 `sqlparser_apply_patch()` 统一改写。改写完成后应调用 `sqlparser_deparse()` 生成 SQL，并重新解析验证输出是否仍是合法 SQL。
