# View JSON 手册

View JSON 是 `sqlparser` 对语句 query graph 和控制流拓扑的按需 JSON 导出，主要用于回归测试、集成验证和跨语言查看解析结果。业务代码优先使用公共 C 结构接口，不需要为了改写 SQL 先生成 JSON。

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

`query_graph` 表达当前 SQL 中的查询块、关系、输出项、字段引用、值、集合运算和 DML 写入结构。

顶层 `control_flow` 仅在输入包含控制语句时存在，表达 statement unit 之间的分支和嵌套关系。

JSON 只输出有意义的可选字段。公共 C 结构中由 `has_*` 或 `count` 表达不存在的字段，在 JSON 中默认省略，不输出 `null` 或空数组。

## control_flow

示例：

```sql
IF @enabled = 1 SELECT id FROM users ELSE SELECT id FROM archived_users
```

对应的控制流结构：

```json
{
  "control_flow": {
    "roots": [{"kind": "node", "index": 0}],
    "nodes": [{"kind": "if", "branches": [0, 1]}],
    "branches": [
      {
        "condition_statement": 0,
        "items": [{"kind": "statement", "index": 1}]
      },
      {
        "items": [{"kind": "statement", "index": 2}]
      }
    ]
  }
}
```

| 字段 | 说明 |
| --- | --- |
| `roots` | 顶层 statement 或控制节点引用，保持源码顺序 |
| `nodes` | 控制节点数组；`kind = "if"` 的 `branches` 保存有序分支索引 |
| `branches` | 分支数组；条件分支包含 `condition_statement`，无条件 `ELSE` 分支不包含该字段 |
| `items` | 分支中的 statement 或嵌套控制节点引用，保持源码顺序 |

`kind = "statement"` 的 `index` 指向 `statements[]` 中的 statement unit；`kind = "node"` 的 `index` 指向 `nodes[]`。条件 statement 的 `keyword` 为 `condition`，其 query graph 根 block 类型和字段、值的 clause 类型均为 `condition`。嵌套 `IF` 通过 branch item 引用另一个 node，不复制子树。

## query_graph

| 字段 | 说明 |
| --- | --- |
| `root` | 根查询块编号；没有查询块时省略 |
| `blocks` | 查询块数组，包含普通 SELECT、派生表、CTE、集合运算和标量子查询；非空时存在 |
| `relations` | SQL 中出现的基础表、派生表或 CTE 引用；非空时存在 |
| `targets` | SELECT 输出项、星号输出、DML 输出来源等；非空时存在 |
| `fields` | SQL 文本中出现的字段引用 occurrence；非空时存在 |
| `values` | 与字段或 SELECT target 关联的值，以及复合 DML assignment 右侧表达式中的 literal、bind 和 DEFAULT occurrence；分页或伪列 bind 不进入该数组；非空时存在 |
| `sets` | `UNION`、`UNION ALL`、`INTERSECT`、`EXCEPT/MINUS` 等集合运算；非空时存在 |
| `predicates` | `WHERE`、`ON`、`HAVING`、`START WITH`、`CONNECT BY` 等条件中的谓词树节点；非空时存在 |
| `session` | 数据库、Schema、角色、身份、事务特征或会话参数操作；仅具有会话状态语义时存在 |
| `dml` | 唯一根 DML 及其嵌套 DML；当前 statement 恰有一个根 DML 时存在 |
| `dmls` | 根 DML 数组，每个元素包含其嵌套 DML；当前 statement 有多个根 DML 时存在 |

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

每个 SQL 字面 occurrence 只输出一次。来源链路可沿 `relation -> source_block -> target -> set branch` 逐层读取。

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
| `database_quoted_identifier` | `database` 对应的精确 token 显式使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时为 `true`；否则省略 |
| `schema` | SQL 中出现的 schema；未出现时省略 |
| `schema_quoted_identifier` | `schema` 对应的精确 token 显式使用上述三类定界符时为 `true`；否则省略 |
| `table` | SQL 中出现的表名；派生表没有表名时省略 |
| `quoted_identifier` | `table` 对应的对象名 token 显式使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时为 `true`；否则省略 |
| `alias` | SQL 中出现的别名；未出现时省略 |
| `alias_quoted_identifier` | `alias` 对应的精确 token 显式使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时为 `true`；否则省略 |
| `link` | 远程对象引用中的 database link 名称；未出现时省略 |
| `link_quoted_identifier` | `link` 对应的精确 token 显式使用上述三类定界符时为 `true`；否则省略 |
| `source_block` | 派生表或 CTE 指向的查询块；没有来源块时省略 |
| `selector` | 可用于 patch 的关系 selector；没有可写节点时省略 |

同一个 CTE 定义只生成一个来源 block。多次 CTE 引用共享 `source_block`，未被引用的 CTE 定义也保留在 `blocks[]` 中。

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
| `kind` | `field`、`star`、`qualified_star`、`literal`、`bind`、`subquery`、`pseudo`、`expression` |
| `name` | 输出名或别名；没有时省略 |
| `output_quoted_identifier` | 有显式输出 alias 时表示 alias token 的定界符状态；没有显式 alias 且 `name` 由直接字段继承时表示字段 token 的状态。仅为 `true` 时输出 |
| `field` | 直接字段输出或层次伪列输出对应的 `fields[]` 索引；不适用时省略 |
| `value` | literal 或 bind 输出项对应的 `values[]` 索引；不适用时省略 |
| `sink_value` | host-bind sink 中接收该 DML 结果 target 的 `values[]` 输出 bind 索引；不适用时省略 |
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
| `clause` | 字段出现的子句，例如 `select_list`、`where`、`start_with`、`connect_by`、`on`、`order_by` |
| `relation` | 稳定归属到的 relation 索引；无法唯一归属时省略 |
| `candidate_relations` | 未限定字段在多 relation 作用域下的候选 relation 索引；没有候选列表时省略 |
| `column` | 字段名；`*` 由 `targets[]` 表达，不作为普通 field 输出 |
| `quoted_identifier` | `column` token 显式使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时为 `true`；否则省略 |
| `pseudo` | 当前层次查询块中的未定界伪列 occurrence 为 `true`；否则省略 |
| `prior` | 字段 occurrence 位于 `CONNECT BY` 的 `PRIOR` 操作数内时为 `true`；否则省略 |
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
  "operator_kind": "unknown",
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
| `operator_kind` | 操作符结构化分类；有 `operator` 时输出，pattern-match 可为 `like`、`not_like`、`ilike`、`not_ilike`，其他操作符为 `unknown` |
| `field` | 关联字段索引；没有字段归属时省略。复合 DML assignment 右侧表达式中的值通过 `rhs_values` 归属，不要求输出 `field`；分页或伪列值仍不进入 `values[]` |
| `source_field` | 值为字段引用时的来源字段索引；不适用时省略 |
| `field_match_kind` | 字段匹配形态；`direct_field` 表示直接字段，`expression_field` 表示字段位于函数、类型转换、表达式或 `CASE` 中 |
| `kind` | `literal`、`bind`、`default`、`expression`、`field` |
| `bind_key` | 预编译占位符 key；没有 bind 时省略 |
| `bind_kind` | `0` 无 bind，`1` 位置 bind，`2` 命名 bind |
| `bind_sql` | SQL 中出现的原始占位符文本；没有 bind 时省略 |
| `bind_position` | 整条输入 SQL 中第几个 bind occurrence，1 基；没有 bind 时省略 |
| `selector` | 值 selector；没有可写节点时省略 |
| `literal` | 字面量结构；非字面量时省略 |
| `like_escape` | `LIKE` / `NOT LIKE` / `ILIKE` / `NOT ILIKE` 的显式 ESCAPE 结构；没有显式 ESCAPE 时省略 |

字符串 literal 来源于带引号标识符 token 时，`literal` 对象会输出 `quoted_identifier: true`。普通字符串字面量和未加引号标识符不输出该字段。

多语句 SQL 中，`bind_position` 按整条输入 SQL 全局递增，不按 statement 重置。

`WHERE`、`JOIN ... ON`、`HAVING` 以及 SELECT 投影内部的条件表达式中，`IN`、`NOT IN`、`BETWEEN` 和普通比较会输出字段关联值。`field_match_kind` 用于区分 `secret = ?` 这类直接字段匹配和 `UPPER(secret) = ?`、`CAST(secret AS ...) = ?`、`secret || id = ?`、`CASE ... THEN secret END = ?` 这类表达式字段匹配。字段侧表达式包含多个字段时，每个可定位字段各输出一条 `expression_field` 关系。

`LIKE ... ESCAPE ...` 中，`values[]` 的主值仍表示 pattern 右值，`operator_kind` 表示 pattern-match 操作符分类，`like_escape` 只表示显式 escape 子句。`kind` 可为 `literal`、`bind` 或 `expression`；bind escape 同样输出 `bind_key`、`bind_kind`、`bind_sql` 和 `bind_position`。反解析输出保持 `LIKE pattern ESCAPE escape` 形态。

```json
{
  "operator": "LIKE",
  "operator_kind": "like",
  "kind": "bind",
  "bind_key": "pattern",
  "bind_kind": 2,
  "bind_sql": ":pattern",
  "bind_position": 1,
  "like_escape": {
    "kind": "bind",
    "bind_key": "escape_char",
    "bind_kind": 2,
    "bind_sql": ":escape_char",
    "bind_position": 2
  }
}
```

对于谓词，如果值侧本身是函数、类型转换、运算符、数组、ROW 或 CASE 表达式，例如 `secret = UPPER(?)`、`secret = ? || 'x'`、`secret = CAST(? AS CHAR)`，`values[]` 输出关联到 `secret` 的 `kind=expression`，不将该谓词表达式内部的 bind 或 literal 暴露为 direct value。该规则不适用于复合 DML assignment 右侧表达式；其内部值通过 `rhs_values` 归属。

## predicate

```json
{
  "block": 0,
  "clause": "where",
  "kind": "comparison",
  "bool_operator": "none",
  "operator": "=",
  "operator_kind": "unknown",
  "left_field": 1,
  "value": 0
}
```

| 字段 | 说明 |
| --- | --- |
| `block` | 谓词所在查询块 |
| `clause` | 谓词所在子句，例如 `where`、`on`、`having`、`start_with`、`connect_by` |
| `kind` | `comparison`、`bool`、`exists`、`expression`、`unknown` |
| `bool_operator` | `bool` 谓词的组合类型：`and`、`or`、`not`；其他类型为 `none` |
| `nocycle` | `CONNECT BY NOCYCLE` 的根谓词为 `true`；否则省略 |
| `operator` | 比较操作符；非比较谓词时省略 |
| `operator_kind` | 操作符结构化分类；非比较谓词时省略 |
| `left_field` | 比较左侧字段索引；没有稳定字段侧时省略 |
| `right_field` | field-to-field 比较右侧字段索引；不适用时省略 |
| `value` | 比较右侧 literal、bind、DEFAULT、field 或 expression 对应的 `values[]` 索引；不适用时省略 |
| `children` | `AND`、`OR`、`NOT` 的子谓词索引数组；非组合谓词时省略 |

`field = literal/bind` 使用 `left_field + value` 表达；`field = field` 使用 `left_field + right_field`，并在 `values[]` 中以 `kind = "field"` 记录来源字段。无法安全拆分字段和值两侧的条件会保留为 `kind = "expression"`，避免把复杂表达式误判为直接字段传递。

### 层次查询表示

- `START WITH` 与 `CONNECT BY` 不生成独立对象，其字段、值和谓词分别进入既有 `fields[]`、`values[]`、`predicates[]`，并使用 `clause = "start_with"` 或 `clause = "connect_by"`。
- 当前查询块包含 `CONNECT BY` 时，未使用标识符定界符的 `LEVEL`、`CONNECT_BY_ISLEAF`、`CONNECT_BY_ISCYCLE` 作为 relationless field 输出 `pseudo: true`。这些伪列位于 SELECT 列表时，`targets[].kind` 为 `pseudo`，`targets[].field` 回指唯一的 field occurrence。带定界符的 `"LEVEL"` 和不含 `CONNECT BY` 的查询块保持普通字段语义；嵌套 SELECT 不继承外层层次上下文。
- `PRIOR` 透明作用于其完整操作数，操作数内每个字段 occurrence 输出 `prior: true`；该标记不传播到嵌套 SELECT。
- `CONNECT_BY_ROOT` 不增加 target kind。SELECT target 保持 `kind = "expression"`，其底层字段在 `target_path` 中输出 `{ "kind": "operator", "name": "CONNECT_BY_ROOT", "arg_index": 0 }`。
- `NOCYCLE` 只在 `CONNECT BY` 根 predicate 上输出 `nocycle: true`。
- 同一 SELECT 的条件相关 occurrence 按 `where`、`start_with`、`connect_by`、`group_by` / `having`、窗口、`order_by` 的语义遍历顺序进入 View 数组。selector 仍由通用 AST descriptor 遍历生成，必须作为不透明定位路径使用，不能按源文本子句顺序推导序号。

## session

`query_graph.session` 表达当前语句的会话状态操作。

```json
{
  "action": "set",
  "items": [
    {
      "scope": "session",
      "target_kind": "parameter",
      "name": "NLS_DATE_LANGUAGE",
      "values": [
        {
          "kind": "identifier",
          "text": "ENGLISH"
        }
      ]
    }
  ]
}
```

| 字段 | 说明 |
| --- | --- |
| `action` | `set`、`reset`、`switch`、`discard`、`enable`、`disable`、`force`、`advise`、`close`、`sync`、`assume` 或 `revert` |
| `items` | 本次操作涉及的会话状态目标；至少包含一个元素 |

item 字段：

| 字段 | 说明 |
| --- | --- |
| `scope` | `session`、`local` 或 `transaction` |
| `target_kind` | `parameter`、`variable`、`database`、`schema`、`container`、`role`、`authorization`、`login`、`user`、`transaction`、`session_context`、`database_link`、`object`、`constraint` 或 `all` |
| `name` | 参数、变量或对象的显式名称或规范化语义名称；没有可用名称时省略，规范化名称不保证逐字出现在 SQL 中 |
| `values` | 目标值数组；没有值时省略 |

value 的 `kind` 为 `identifier`、`keyword`、`literal`、`bind` 或 `expression`。标识符、关键字和表达式使用 `text`；字面量使用 `literal`；bind 使用 `bind_key`、`bind_kind`、`bind_sql`，其 `bind_position` 从 `1` 开始，按同一 handle 内各 statement 中的 SQL 出现顺序编号。`identifier` 的原始 token 显式使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时输出 `quoted_identifier: true`。各类 value 均可包含可选 `name`，用于区分同一 item 内具有独立语义的值；例如 `SET NAMES ... COLLATE ...` 的 collation value 使用 `"name": "collation"`。没有可用的独立语义标签时省略该字段。

Relation 中 `database_quoted_identifier`、`schema_quoted_identifier`、`quoted_identifier`、`alias_quoted_identifier` 和 `link_quoted_identifier` 分别仅对应 `database`、`schema`、`table`、`alias` 和 `link`。Field 中 `quoted_identifier` 仍只对应列名；target 的 `output_quoted_identifier` 归属规则不变。DML 目标列对象中的 `quoted_identifier` 仅对应该对象的 `column`。

`output_quoted_identifier` 优先描述显式输出 alias；没有显式 alias 时，仅当 `name` 由直接字段继承才描述字段 token。所有这类标志只表示各自的精确来源 token 是否使用 `"..."`、MySQL 反引号或 SQL Server `[...]`，不区分定界符类型，且仅为 `true` 时输出。PostgreSQL `U&"..."`、普通单引号字符串和解析器内部生成的引号样式不会产生这些字段。

## DML

`query_graph.dml` 和 `query_graph.dmls[]` 的元素表达写入目标、目标列、行值、赋值项、来源查询和结果通道。单个根 DML 使用 `dml`；多个并列根 DML 使用 `dmls`。数据修改 CTE 即使位于 SELECT statement 中也按该规则输出。嵌套 DML 通过各根元素的 `children` 递归表达。

常见字段：

| 字段 | 说明 |
| --- | --- |
| `kind` | `insert`、`update`、`delete`、`merge` |
| `insert_mode` | INSERT 写入形态：`values`、`select`、`all`、`first`、`set`、`replace_values`、`replace_select`、`replace_set` |
| `target_relation` | 写入目标 relation 索引；没有稳定目标时省略 |
| `target_columns` | INSERT 显式目标列对象；没有列列表时省略 |
| `rows` | `INSERT ... VALUES` 或 Oracle/Dameng multi-table INSERT branch 的 cell 索引数组 |
| `source_block` | `INSERT ... SELECT` 或 Oracle/Dameng multi-table INSERT 末尾 source query 的 block 索引 |
| `branches` | Oracle/Dameng `INSERT ALL/FIRST` 的 INTO 分支，或 MERGE 的有序 `WHEN` 分支；没有分支时省略 |
| `result_channels` | DML 结果通道数组；没有结果输出时省略 |
| `children` | 以当前 DML 为父节点的嵌套 DML 数组；没有嵌套 DML 时省略 |

`target_columns`、`branches[].target_columns` 和 `result_channels[].sink_columns` 中的目标列对象共用同一结构：

| 字段 | 说明 |
| --- | --- |
| `ordinal` | 目标列在当前列列表中的 0 基序号 |
| `column` | 目标列名 |
| `quoted_identifier` | `column` 的精确来源 token 使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时为 `true`；否则省略 |
| `selector` | 单列 selector；没有可写节点时省略 |

该 `quoted_identifier` 覆盖普通 INSERT、MERGE INSERT 分支、Oracle、Dameng 与 Vastbase-Oracle `INSERT ALL/FIRST` 分支和 SQL Server `OUTPUT ... INTO` relation-backed sink，并沿用上述 true-only 和 `U&"..."` 排除规则。

MySQL 与 Vastbase-MySQL 的多目标 UPDATE 省略 `dml.target_relation`；每个 assignment 的 `target_field` 指向 `fields[]` 中具有独立 relation 的目标字段。对应的 `sqlparser_statement_target_relation()` 返回 `SQLPARSER_STATUS_UNSUPPORTED`。Dameng 多表 UPDATE 要求全部 SET assignment 指向同一个 table object，因此始终输出唯一的 `dml.target_relation`。

结果通道字段：

| 字段 | 说明 |
| --- | --- |
| `kind` | `client` 表示返回结果，`sink` 表示由 relation 或 host bind 接收结果 |
| `block` | 该通道输出 target 所在的 `dml_result` block 索引 |
| `sink_relation` | sink relation 索引；仅 relation-backed sink 存在 |
| `sink_columns` | relation-backed sink 的目标列对象；没有显式列列表时省略 |
| `references` | 输出 target 对目标行或来源 relation 的字段引用；非空时存在 |

relation-backed sink 通过 `sink_relation` 和可选 `sink_columns` 表达写入目标。host-bind sink 不输出这两个字段；其结果 target 通过 `sink_value` 指向 `query_graph.values[]` 中的输出 bind。该 value 使用既有 value `selector`，可作为 `SQLPARSER_PATCH_REPLACE` 的目标，不引入新的 selector 类型。Oracle 与 Vastbase-Oracle 兼容模式的 `RETURNING ... INTO`，以及 Dameng 的 `RETURN`/`RETURNING ... INTO` 使用该表示；N 个结果 target 与 N 个 host bind 按序对应，第 i 个 target 的 `sink_value` 指向第 i 个输出 bind。

每个 `references[]` 元素包含结果 `target` 索引、可选 `field` 索引、`relation` 索引和 `kind`。`kind` 取值为 `target_before`、`target_after` 或 `source`。SQL Server `DELETED.id`、`INSERTED.id` 和来源表字段分别使用这三种类型。

结果 target、sink relation 和 sink column 的 selector 可直接用于 `sqlparser_apply_patch()`：

```text
stmt[0].dml_result_target[0][0][0]
stmt[0].dml_result_sink[0][0]
stmt[0].dml_result_sink_column[0][0][0]
```

Oracle/Dameng multi-table INSERT 的每个 branch 包含独立的 `target_relation`、`target_columns`、`rows` 和 `branch_kind`。`branch_kind` 取值为 `unconditional`、`when` 或 `else`；`WHEN` 条件通过 `condition_selector` 定位，该 selector 可通过 `sqlparser_selector_clause_sql()` 读取原始条件 SQL。Oracle、Dameng 和 Vastbase-Oracle 的 `INSERT ALL ... INTO ...@link` 分支目标 relation 恢复完整投影：`table`、`link` 以及对应的 `quoted_identifier` 和 `link_quoted_identifier` 都依据精确来源 token 输出。

branch cell 的 `kind` 可为 `literal`、`bind`、`default`、`expression` 或 `field`。当 `VALUES (id)` 这类 cell 直接引用末尾 source query 的输出字段时，`kind` 为 `field`，并通过 `source_target` 指向 `targets[]` 中对应的 source query 输出项；如果该 target 是直接字段，调用方可继续读取 `targets[].field` 定位到 `fields[]`。

对于解析成功的 MERGE，`branches[]` 按 `WHEN` 子句在源 SQL 中的出现顺序排列。每个分支的 `ordinal` 是相对于该 MERGE 全部 `WHEN` 子句的 0 基绝对序号 `W`。每个分支包含 `merge_action_kind`（`insert`、`update`、`delete` 或 `nothing`）和 `merge_match_kind`（`matched`、`not_matched_by_target` 或 `not_matched_by_source`）。INSERT 分支通过 `target_columns` 和 `rows` 表达写入值；具有 VALUES 且省略目标列列表时 `target_columns` 不存在，但 `rows` 仍存在，且每个 cell 的 `row` 等于绝对 `W`、`column` 按值顺序从 0 连续编号。省略目标列列表的 DEFAULT VALUES 分支同时省略 `target_columns` 和 `rows`。UPDATE 分支通过 `assignments` 引用父 DML assignment；DELETE 和 NOTHING 分支省略 `target_columns`、`rows` 和 `assignments`。带条件的分支包含 `condition_selector`，无条件分支不包含该字段。Oracle/Dameng matched UPDATE 的附属 `DELETE WHERE` 条件使用同一 branch 上的 `delete_condition_selector`；该 branch 的 `merge_action_kind` 仍为 `update`，不另行生成 DELETE branch。PostgreSQL 和 SQL Server 的 `WHEN MATCHED ... THEN DELETE` 则是 `merge_action_kind = delete` 的独立 branch，不输出 `delete_condition_selector`。

MERGE INSERT 的每个 `target_columns[]` 对象包含单列 `selector`，每个 `rows[]` cell 包含完整表达式 `selector`。根 MERGE 分别使用 `stmt[S].merge_insert_column[W][C]` 和 `stmt[S].merge_insert_cell[W][C]`；嵌套 MERGE 在 `W` 前增加当前 statement 内的 DML 索引 `D`。具有 VALUES 的分支还包含 `target_list_selector`，其形式为 `stmt[S].insert_branch_columns[W]` 或嵌套形式 `stmt[S].insert_branch_columns[D][W]`；省略目标列列表时仍输出该 selector，以便物化列表或单独增加 cell。省略目标列列表的 `INSERT DEFAULT VALUES` 分支为 0 列、0 行，不输出该 selector；显式目标列列表的 DEFAULT VALUES 分支仍可能输出既有单列和目标列表 selector。

单列和完整 cell selector 可分别用于 `SQLPARSER_PATCH_REPLACE`。目标列表 selector 可用于 `SQLPARSER_PATCH_INSERT_COLUMN` 的三种载荷：name-only 在 `index` 处增加目标列，value-only 在 `index` 处增加 VALUES cell，name + value 在两侧同位增加。批次中间允许列值暂时不等长；本批次触及且最终具有显式目标列列表的分支必须在提交前等宽，否则整批原子回滚。最终仍省略列表时允许 value-only。`SQLPARSER_PATCH_DELETE_COLUMN` 继续成对删除，并要求删除前存在等长的显式列表；省略列表不支持该删除。DEFAULT VALUES 没有 VALUES 列表，因此三态插入和成对删除均不支持；显式列表产生的 selector 也不会使这两类操作可用。

以上是本项目九个方言入口对成功解析 MERGE 的 View 与 patch 合同，不表示对应数据库服务端均原生提供该语法。

`UPDATE`、`INSERT` 冲突更新和 `MERGE` 的 assignment 使用 `target_field` 指向被写入字段。赋值右侧为直接字段引用时，`kind` 为 `field`，`source_field` 指向来源字段；来源字段来自派生表且可唯一匹配 source query 输出项时，同时输出 `source_target`。

assignment 的 `kind` 为 `expression` 时，`rhs_fields` 和 `rhs_values` 分别列出右侧表达式在当前 assignment block 内对应的 `fields[]` 和 `values[]` 索引；`rhs_blocks` 列出从右侧表达式出发、不跨越另一层子查询边界即可到达的子查询入口 `blocks[]` 索引。空列表省略。`rhs_blocks` 不重复收录这些子查询内部的 block；内部 relation、target、field、value、predicate 和 set 语义从入口 block 继续遍历。右侧为直接 `field`、`literal`、`bind` 或 `default` 时，继续使用 assignment 的既有 payload，不输出三个 `rhs_*` 列表。

根 `UPDATE` 或根 `INSERT` 冲突更新 assignment 的 `selector` 形如 `stmt[S].assignment[A]`；嵌套 `UPDATE` 使用 `stmt[S].assignment[D][A]`。`D` 是当前 statement 内的 0 基 DML 序号，`A` 是目标列表内赋值项的 0 基序号。根 MERGE matched UPDATE action 的 assignment 使用 `stmt[S].merge_assignment[W][A]`；嵌套 MERGE 使用 `stmt[S].merge_assignment[D][W][A]`。`W` 是目标 MERGE 中所有 `WHEN` 子句的绝对 0 基序号。MERGE 分支条件对应使用 `stmt[S].merge_branch_condition[W]` 或嵌套形式 `stmt[S].merge_branch_condition[D][W]`。Oracle/Dameng 附属 DELETE 条件对应使用 `stmt[S].merge_delete_condition[W]` 或 `stmt[S].merge_delete_condition[D][W]`，selector kind 为 `SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`。两类条件均可通过 `sqlparser_selector_clause_sql()` 读取原文，并通过 `sqlparser_selector_set_clause_sql()` 或 `SQLPARSER_PATCH_REPLACE` 替换。assignment selector 可用于 assignment selector API、`SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT`、`SQLPARSER_PATCH_REPLACE_ASSIGNMENT` 以及 patch 的 `source_selector`。

## 改写

View JSON 中的 `selector` 可用于构造 `sqlparser_patch_t`，再调用 `sqlparser_apply_patch()` 统一改写。改写完成后调用 `sqlparser_deparse()` 生成 SQL。
