# API 手册

本文档说明 `sqlparser` 公共 C API 的主要类型、生命周期规则、结构化读取接口和改写接口。

## 概述

`sqlparser` 以 `sqlparser_handle_t` 为中心提供能力。标准流程如下：

1. 调用 `sqlparser_parse()` 或 `sqlparser_parse_with_options()` 解析 SQL。
2. 通过语句接口、selector 接口或 `query_graph` 接口读取结构信息。
3. 使用 selector、细粒度改写函数或 `sqlparser_apply_patch()` 修改 AST。
4. 调用 `sqlparser_deparse()` 生成改写后的 SQL。
5. 使用 `sqlparser_handle_destroy()` 释放 handle。

View JSON 是 `query_graph` 的按需 JSON 序列化，主要用于回归测试、集成验证和跨语言查看结果。业务代码优先使用 C 结构接口，不需要为了改写 SQL 先生成 JSON。

## 头文件与链接

公共头文件：

```c
#include "sqlparser/sqlparser.h"
```

公共库文件：

- `lib/libsqlparser.a`
- `lib/libsqlparser.so`

## 快速示例

```c
#include <stdio.h>
#include "sqlparser/sqlparser.h"

int main(void)
{
    const char *sql = "UPDATE public.users SET name = 'bob' WHERE id = 1";
    sqlparser_handle_t *handle = NULL;
    sqlparser_error_t err;
    sqlparser_literal_value_t value;
    char *out_sql = NULL;

    if (sqlparser_parse(sql, &handle, &err) != SQLPARSER_STATUS_OK) {
        printf("parse failed: %s\n", err.message);
        return 1;
    }

    value.kind = SQLPARSER_LITERAL_KIND_STRING;
    value.string_value = "carol";
    value.float_value = NULL;
    value.integer_value = 0;
    value.boolean_value = 0;

    if (sqlparser_update_set_assignment_literal(handle, 0, 0, &value, &err) != SQLPARSER_STATUS_OK) {
        printf("rewrite failed: %s\n", err.message);
        sqlparser_handle_destroy(handle);
        return 1;
    }

    if (sqlparser_deparse(handle, &out_sql, &err) != SQLPARSER_STATUS_OK) {
        printf("deparse failed: %s\n", err.message);
        sqlparser_handle_destroy(handle);
        return 1;
    }

    printf("%s\n", out_sql);

    sqlparser_string_free(out_sql);
    sqlparser_handle_destroy(handle);
    return 0;
}
```

## 返回状态与错误对象

大多数 API 返回 `sqlparser_status_t`。

| 状态码 | 说明 |
| --- | --- |
| `SQLPARSER_STATUS_OK` | 操作成功 |
| `SQLPARSER_STATUS_INVALID_ARGUMENT` | 参数非法 |
| `SQLPARSER_STATUS_NO_MEMORY` | 内存分配失败 |
| `SQLPARSER_STATUS_PARSE_ERROR` | SQL 解析失败 |
| `SQLPARSER_STATUS_INTERNAL_ERROR` | 内部处理失败 |
| `SQLPARSER_STATUS_UNSUPPORTED` | 当前语句形态不支持该操作 |
| `SQLPARSER_STATUS_RESOURCE_LIMIT` | 输入、输出或语句数量超过资源限制 |

`sqlparser_error_t` 字段：

| 字段 | 说明 |
| --- | --- |
| `code` | 状态码 |
| `cursor` | 字符位置 |
| `line` | 行号 |
| `column` | 列号 |
| `message` | 错误消息 |

## 核心类型

### handle

`sqlparser_handle_t` 表示一次解析后的长期对象。内部保存原始 SQL、当前语法树、方言状态和按需生成的派生缓存。

### 常用枚举

| 枚举类型 | 说明 |
| --- | --- |
| `sqlparser_statement_kind_t` | 语句类型 |
| `sqlparser_insert_source_kind_t` | `INSERT` 数据来源 |
| `sqlparser_value_kind_t` | 精细值读取接口中的值类型 |
| `sqlparser_bind_kind_t` | 预编译占位符类型 |
| `sqlparser_graph_field_match_kind_t` | query graph 条件值的字段匹配形态 |
| `sqlparser_graph_operator_kind_t` | query graph value 的结构化操作符分类 |
| `sqlparser_graph_like_escape_kind_t` | query graph 中显式 `LIKE ... ESCAPE ...` 的 escape 形态 |
| `sqlparser_graph_predicate_kind_t` | query graph predicate 节点类型 |
| `sqlparser_graph_predicate_bool_t` | query graph boolean predicate 连接类型 |
| `sqlparser_graph_session_action_t` | 会话状态操作类型 |
| `sqlparser_graph_session_scope_t` | 会话状态作用域 |
| `sqlparser_graph_session_target_kind_t` | 会话状态目标类型 |
| `sqlparser_graph_session_value_kind_t` | 会话状态值类型 |
| `sqlparser_control_node_kind_t` | 控制流节点类型 |
| `sqlparser_control_item_kind_t` | 控制流条目引用类型 |
| `sqlparser_graph_dml_result_kind_t` | DML 结果通道类型 |
| `sqlparser_graph_dml_reference_kind_t` | DML 结果字段来源类型 |
| `sqlparser_literal_kind_t` | 字面量类型 |
| `sqlparser_selector_kind_t` | selector 类型 |
| `sqlparser_clause_kind_t` | query graph 与 clause patch 使用的子句类型 |
| `sqlparser_dialect_t` | SQL 方言类型 |

`sqlparser_identifier_path_view_t` 用于向结构化改写接口传入标识符路径。

| 字段 | 说明 |
| --- | --- |
| `parts` | 标识符分段数组，由调用方持有，库只在调用期间读取 |
| `part_count` | 标识符分段数量，必须大于 `0` |

`part_count = 1` 表示单列名，例如 `phone_backup`；`part_count = 2` 表示限定列名，例如 `u.phone`；更长路径按当前 handle 的方言规则反解析。每个分段必须非空，调用方不需要传入引号字符。

`sqlparser_bind_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_BIND_KIND_NONE` | `0` | 没有 bind |
| `SQLPARSER_BIND_KIND_POSITIONAL` | `1` | 位置 bind，例如 `?`、`:1`、`$1` |
| `SQLPARSER_BIND_KIND_NAMED` | `2` | 命名 bind，例如 `:name`、`@name` |

query graph 中既有的 bind 字段规则：

- `bind_key` 按 `bind_kind` 解释；命名 bind 为名称，匿名 `?` 为全局序号字符串，显式编号 bind 保留 SQL 中的编号字符串。
- `bind_position` 是同一 handle 当前公开 SQL 中的 bind 出现序号，从 1 开始；多语句输入不按 statement 重置。
- `bind_sql` 保留当前公开 SQL 中的完整占位符文本。

`sqlparser_graph_value_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_VALUE_LITERAL` | `1` | 字面量值 |
| `SQLPARSER_GRAPH_VALUE_BIND` | `2` | 预编译占位符 |
| `SQLPARSER_GRAPH_VALUE_DEFAULT` | `3` | `DEFAULT` |
| `SQLPARSER_GRAPH_VALUE_EXPRESSION` | `4` | 函数、运算符、`CASE` 等表达式 |
| `SQLPARSER_GRAPH_VALUE_FIELD` | `5` | 值侧直接引用 SQL 中的字段，例如 field-to-field predicate、DML assignment RHS 或 source query 输出字段 |

`sqlparser_graph_like_escape_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_NONE` | `0` | 没有显式 `ESCAPE` 子句 |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL` | `1` | `ESCAPE` 是字面量 |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_BIND` | `2` | `ESCAPE` 是预编译占位符 |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION` | `3` | `ESCAPE` 是函数、运算符或其他表达式 |

`sqlparser_graph_field_match_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN` | `0` | 未关联字段或无法稳定判断 |
| `SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD` | `1` | 条件左侧是直接字段，例如 `secret = ?` |
| `SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD` | `2` | 条件左侧字段位于函数、类型转换、表达式或 `CASE` 中，例如 `UPPER(secret) = ?` |

`sqlparser_graph_operator_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_OPERATOR_UNKNOWN` | `0` | 未分类或不是 pattern-match 操作符 |
| `SQLPARSER_GRAPH_OPERATOR_LIKE` | `1` | `LIKE` |
| `SQLPARSER_GRAPH_OPERATOR_NOT_LIKE` | `2` | `NOT LIKE` |
| `SQLPARSER_GRAPH_OPERATOR_ILIKE` | `3` | `ILIKE` |
| `SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE` | `4` | `NOT ILIKE` |

`sqlparser_graph_predicate_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_PREDICATE_UNKNOWN` | `0` | 未分类谓词 |
| `SQLPARSER_GRAPH_PREDICATE_COMPARISON` | `1` | 字段和值、字段和字段之间的比较 |
| `SQLPARSER_GRAPH_PREDICATE_BOOL` | `2` | `AND`、`OR`、`NOT` 组合谓词 |
| `SQLPARSER_GRAPH_PREDICATE_EXISTS` | `3` | `EXISTS` 谓词 |
| `SQLPARSER_GRAPH_PREDICATE_EXPRESSION` | `4` | 无法安全拆分为字段和值侧的表达式谓词 |

`sqlparser_graph_predicate_bool_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_NONE` | `0` | 非 boolean 谓词 |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_AND` | `1` | `AND` |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_OR` | `2` | `OR` |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_NOT` | `3` | `NOT` |

`sqlparser_clause_kind_t`：

| 枚举 | JSON 名称 | 说明 |
| --- | --- | --- |
| `SQLPARSER_CLAUSE_KIND_SELECT_LIST` | `select_list` | SELECT 输出列表 |
| `SQLPARSER_CLAUSE_KIND_WHERE` | `where` | WHERE 条件 |
| `SQLPARSER_CLAUSE_KIND_ORDER_BY` | `order_by` | ORDER BY 排序 |
| `SQLPARSER_CLAUSE_KIND_SET_LIST` | `set_list` | UPDATE SET 列表 |
| `SQLPARSER_CLAUSE_KIND_ON` | `on` | JOIN 或 MERGE ON 条件 |
| `SQLPARSER_CLAUSE_KIND_GROUP_BY` | `group_by` | GROUP BY 分组 |
| `SQLPARSER_CLAUSE_KIND_HAVING` | `having` | HAVING 条件 |
| `SQLPARSER_CLAUSE_KIND_DML_RESULT` | `dml_result` | DML 结果输出列表 |
| `SQLPARSER_CLAUSE_KIND_CONDITION` | `condition` | 控制流条件表达式 |
| `SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION` | `window_partition` | 命名窗口定义的 PARTITION BY 列表 |
| `SQLPARSER_CLAUSE_KIND_START_WITH` | `start_with` | 层次查询起始条件 |
| `SQLPARSER_CLAUSE_KIND_CONNECT_BY` | `connect_by` | 层次查询递归连接条件 |

两个层次查询枚举值追加在既有编号之后：`SQLPARSER_CLAUSE_KIND_START_WITH = 11`，`SQLPARSER_CLAUSE_KIND_CONNECT_BY = 12`。

`sqlparser_graph_dml_result_kind_t`：

| 枚举 | 说明 |
| --- | --- |
| `SQLPARSER_GRAPH_DML_RESULT_CLIENT` | 返回给调用端的结果通道 |
| `SQLPARSER_GRAPH_DML_RESULT_SINK` | 由目标 relation 或 host bind 接收的结果通道 |

`sqlparser_graph_dml_reference_kind_t`：

| 枚举 | 说明 |
| --- | --- |
| `SQLPARSER_GRAPH_DML_REFERENCE_TARGET_BEFORE` | 目标行修改前的字段，例如 SQL Server `DELETED.id` |
| `SQLPARSER_GRAPH_DML_REFERENCE_TARGET_AFTER` | 目标行修改后的字段，例如 SQL Server `INSERTED.id` |
| `SQLPARSER_GRAPH_DML_REFERENCE_SOURCE` | DML 来源 relation 的字段 |

## 资源限制与解析选项

`sqlparser_limits_t` 默认限制为：SQL 输入 4MB、生成输出 4MB、单次解析 64 条语句。

| 字段 | 说明 |
| --- | --- |
| `struct_size` | 结构体大小，由 `sqlparser_limits_default()` 填充 |
| `max_sql_bytes` | SQL 输入和表达式 SQL 片段最大字节数 |
| `max_output_bytes` | 生成 SQL 或 JSON 输出最大字节数 |
| `max_statement_count` | 单次解析允许的最大语句数量 |

`sqlparser_parse_options_t`：

| 字段 | 说明 |
| --- | --- |
| `struct_size` | 结构体大小，由 `sqlparser_parse_options_default()` 填充 |
| `dialect` | SQL 方言；默认是 `SQLPARSER_DIALECT_POSTGRESQL` |
| `limits` | 资源限制 |
| `flags` | 保留字段，当前保持为 `0` |

已定义方言：

| 方言 | 说明 |
| --- | --- |
| `SQLPARSER_DIALECT_POSTGRESQL` | 默认方言 |
| `SQLPARSER_DIALECT_MYSQL` | MySQL 方言转换层 |
| `SQLPARSER_DIALECT_ORACLE` | Oracle 方言转换层 |
| `SQLPARSER_DIALECT_SQLSERVER` | SQL Server 方言转换层 |
| `SQLPARSER_DIALECT_DAMENG` | 达梦方言转换层 |
| `SQLPARSER_DIALECT_VASTBASE_ORACLE` | Vastbase Oracle 兼容模式 |
| `SQLPARSER_DIALECT_VASTBASE_MYSQL` | Vastbase MySQL 兼容模式 |
| `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` | Vastbase PostgreSQL 兼容模式 |
| `SQLPARSER_DIALECT_VASTBASE_SQLSERVER` | Vastbase SQL Server 兼容模式 |

## 生命周期与线程模型

- `sqlparser_parse()` 返回的 handle 由 `sqlparser_handle_destroy()` 释放。
- `sqlparser_deparse()`、`sqlparser_export_view_json()` 和渲染类函数返回的字符串由 `sqlparser_string_free()` 释放。
- C 结构视图中的字符串均为 borrowed pointer，归属 handle，不允许调用方释放。
- 成功且实际改变 handle 的 patch 或 AST 修改后，旧的 borrowed pointer、selector 读取结果、bind occurrence view 和 query graph view 均失效。
- 同一个 handle 不支持并发读写，也不保证多线程只读并发安全；推荐一个线程独占一个 handle。

## 版本与名称辅助函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_version_string()` | 返回库版本字符串 |
| `sqlparser_libpg_query_tag()` | 返回仓库内固定 `libpg_query` 版本 tag |
| `sqlparser_statement_kind_name()` | 返回语句类型名称 |
| `sqlparser_insert_source_kind_name()` | 返回 `INSERT` 数据来源名称 |
| `sqlparser_value_kind_name()` | 返回值类型名称 |
| `sqlparser_bind_kind_name()` | 返回 bind 类型名称 |
| `sqlparser_literal_kind_name()` | 返回字面量类型名称 |
| `sqlparser_selector_kind_name()` | 返回 selector 类型名称 |
| `sqlparser_clause_kind_name()` | 返回子句类型名称 |
| `sqlparser_graph_block_kind_name()` | 返回 query graph block 类型名称 |
| `sqlparser_graph_relation_kind_name()` | 返回 query graph relation 类型名称 |
| `sqlparser_graph_target_kind_name()` | 返回 query graph target 类型名称 |
| `sqlparser_graph_value_kind_name()` | 返回 query graph value 类型名称 |
| `sqlparser_graph_field_match_kind_name()` | 返回 query graph 字段匹配形态名称 |
| `sqlparser_graph_operator_kind_name()` | 返回 query graph 操作符分类名称 |
| `sqlparser_graph_set_kind_name()` | 返回 query graph set 类型名称 |
| `sqlparser_graph_dml_kind_name()` | 返回 query graph DML 类型名称 |
| `sqlparser_graph_predicate_kind_name()` | 返回 query graph predicate 类型名称 |
| `sqlparser_graph_predicate_bool_name()` | 返回 query graph boolean predicate 连接类型名称 |
| `sqlparser_graph_operator_is_like_pattern()` | 判断操作符分类是否为 `LIKE`、`NOT LIKE`、`ILIKE` 或 `NOT ILIKE` |
| `sqlparser_graph_value_is_like_pattern()` | 判断 query graph value 是否为 pattern-match 的 pattern 值 |
| `sqlparser_dialect_name()` | 返回方言名称 |
| `sqlparser_bool_operator_name()` | 返回布尔连接符名称 |

## 解析与句柄管理

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_limits_default()` | 填充默认资源限制 |
| `sqlparser_parse_options_default()` | 填充默认解析选项 |
| `sqlparser_parse()` | 使用默认选项解析 SQL |
| `sqlparser_parse_with_limits()` | 使用指定资源限制解析 SQL |
| `sqlparser_parse_with_options()` | 使用指定方言和资源限制解析 SQL |
| `sqlparser_handle_destroy()` | 释放 handle |
| `sqlparser_original_sql()` | 返回原始输入 SQL |
| `sqlparser_handle_dialect()` | 返回 handle 使用的方言 |
| `sqlparser_statement_count()` | 返回语句数量 |

## 完整 bind occurrence 读取

`sqlparser_handle_bind_occurrences()` 返回当前 handle 中所有真实占位符的只读 view；`sqlparser_bind_occurrence_at()` 使用 0 基索引读取一项。列表按整段当前公开 SQL 中的实际出现顺序排列，重复占位符不合并，多语句输入中的 `position` 不按 statement 重置。初始 handle 以输入 SQL 为准；成功改写后以当前 `sqlparser_deparse()` 对应的 SQL 为准重新构建。

| 类型 | 字段 | 说明 |
| --- | --- | --- |
| `sqlparser_bind_occurrence_view_t` | `handle` | 结果所属 handle |
|  | `generation` | 构建该 view 时的 handle generation |
|  | `count` | occurrence 数量；没有占位符时为 `0` |
| `sqlparser_bind_occurrence_t` | `position` | 整段 SQL 中从 `1` 开始的全局出现序号 |
|  | `kind` | `SQLPARSER_BIND_KIND_NAMED` 或 `SQLPARSER_BIND_KIND_POSITIONAL` |
|  | `key` | 不含前缀的命名或数字 key；匿名 `?` 为 `NULL` |
|  | `sql` | 当前公开 SQL 中完整且不截断的占位符 token |

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_handle_bind_occurrences()` | 取得 handle 级 occurrence view |
| `sqlparser_bind_occurrence_at()` | 按 0 基索引读取 occurrence |

任何成功且实际改变 handle 的改写都会推进 generation，使旧 view 及其 item 中的 `key`、`sql` 指针失效；失败或 effective no-op 改写不使其失效。`key` 和 `sql` 是 handle 持有的 borrowed NUL 字符串，调用方不得释放；销毁 handle 后也不得继续访问。空列表是 `count = 0` 的成功结果，对其读取任何索引均为越界。NULL handle/view/输出指针、过期 view 或越界索引返回 `SQLPARSER_STATUS_INVALID_ARGUMENT`；构建失败返回对应错误且不提供部分结果。

该列表独立于 query graph，覆盖成功解析 SQL 中函数、CAST、CASE、运算表达式、分页、子查询、DML、MERGE 和结果通道等位置的真实占位符。字符串、注释和定界标识符中的相似文本不计入。View JSON 不包含这份完整 occurrence 列表，不能从其中的语义 bind 子集反推完整结果。

九个方言入口的公开 token 边界如下；表中规则描述本项目解析入口，不表示兼容数据库服务端的能力声明。

| 方言入口 | 计入的 token | 主要排除边界 |
| --- | --- | --- |
| PostgreSQL | `$[1-9][0-9]*` | `?`、`$0`、标识符相邻形式、dollar quote、字符串、注释和定界标识符 |
| MySQL | 代码区中的 `?` | `$n`、`@` 变量及保护区；仅项目已展开的整 statement executable comment 正文按代码处理 |
| Oracle | `?`、`:[0-9]+`、`:<name>(.<name>)*` | `:=`、`::`、`$n`、保护区及非 bind 冒号文本 |
| SQL Server | `?`、`@<name>` | `@@`、保护区、`OUTPUT INTO` sink、`EXEC` 参数 label 和赋值目标等非值角色 |
| Dameng | `?`、`:[0-9]+`、`:<name>(.<name>)*` | 与 Oracle 相同 |
| Vastbase-Oracle | Oracle 形式及正数 `$n` | 其余与 Oracle 相同；`$n` 仅表示本项目入口合同 |
| Vastbase-MySQL | 与 MySQL 相同 | 与 MySQL 相同 |
| Vastbase-PostgreSQL | 与 PostgreSQL 相同 | 与 PostgreSQL 相同 |
| Vastbase-SQLServer | 与 SQL Server 相同 | 与 SQL Server 相同 |

Oracle、Dameng 兼容入口的每段 `name` 为 `[A-Za-z_][A-Za-z0-9_$#]*`。SQL Server 兼容入口中，`@` 后首字符可为字母、数字、`_` 或 `#`，后续字符可为字母、数字、`_`、`$`、`#` 或 `@`。名称和数字 key 保留 SQL 中的原始大小写与字节；相同 kind/key 只表达相同语义 key，不合并 occurrence。每个匿名 `?` 也单独保留，通过各自的 `position` 区分。

## 语句级访问

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_kind()` | 返回指定语句的逻辑类型 |
| `sqlparser_statement_node_name()` | 返回底层节点名称 |
| `sqlparser_statement_target_relation()` | 返回语句主目标对象 |

MySQL 与 Vastbase-MySQL 的多目标 UPDATE 没有单一主目标，`sqlparser_statement_target_relation()` 返回 `SQLPARSER_STATUS_UNSUPPORTED`。Dameng 多表 UPDATE 要求全部 SET assignment 指向同一个 table object，该函数返回此唯一目标。

控制流中的条件表达式和分支 SQL 都是可寻址 statement unit。条件 unit 的类型为 `SQLPARSER_STATEMENT_KIND_CONDITION`，节点名称为 `ConditionExpr`；分支 SQL 保持自身语句类型。现有 `stmt[n]...` selector 可直接读取和修改这些 unit。

## 控制流只读遍历

`sqlparser_handle_control_flow()` 返回控制流的只读拓扑。普通 SQL 返回成功和空 view；包含控制语句的 handle 通过 roots、nodes、branches 和 items 表达有序结构。

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_handle_control_flow()` | 获取 handle 的控制流 view |
| `sqlparser_control_span_index_at()` | 从控制流 span 读取第 N 个索引 |
| `sqlparser_control_node_at()` | 读取控制节点 |
| `sqlparser_control_branch_at()` | 读取有条件或无条件分支 |
| `sqlparser_control_item_at()` | 读取分支中的 statement 或嵌套节点引用 |

| 结构 | 说明 |
| --- | --- |
| `sqlparser_control_flow_view_t` | roots span、节点/分支/条目数量及 generation |
| `sqlparser_control_node_t` | 节点类型和有序 branch span；当前节点类型为 `SQLPARSER_CONTROL_NODE_IF` |
| `sqlparser_control_branch_t` | 可选条件 statement 索引和有序 item span |
| `sqlparser_control_item_t` | `SQLPARSER_CONTROL_ITEM_STATEMENT` 或 `SQLPARSER_CONTROL_ITEM_NODE` 引用 |

roots、`node.branches` 和 `branch.items` 都是索引池 span，必须通过 `sqlparser_control_span_index_at()` 读取，不能把 `offset` 直接当作对象索引。view 借用 handle 内存；handle 修改后 generation 变化，旧 view 失效。

```c
sqlparser_control_flow_view_t flow;
sqlparser_control_item_t root;
size_t root_item_index;

sqlparser_handle_control_flow(handle, &flow, &err);
sqlparser_control_span_index_at(
    &flow, flow.roots, 0, &root_item_index, &err);
sqlparser_control_item_at(&flow, root_item_index, &root, &err);
```

## 通用读取与细粒度改写

### Relation

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_relation_count()` | 返回 relation 数量 |
| `sqlparser_statement_relation()` | 读取指定 relation |
| `sqlparser_statement_set_relation_name()` | 改写指定 relation 的 schema 或 table 名称 |

当 relation 没有显式 alias 时，名称改写会同步更新作用域解析中唯一绑定到该 relation 的限定列引用、限定星号和限定赋值目标。显式 alias、同层歧义、内层遮蔽以及 SQL Server `INSERTED`、`DELETED` 等伪关系引用保持不变。原引用的限定层级可以随新 relation 路径收缩，但不会因新路径变长而扩张；relation selector 的 replace 使用相同规则。

### Name

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_name_count()` | 返回名称原子数量 |
| `sqlparser_statement_name()` | 读取指定名称原子 |
| `sqlparser_statement_set_name()` | 改写指定名称原子 |

对于源自输入 identifier token 的未加引号标识符，AST 名称值保留 token 文本及其大小写，因此 `abc`、`DDD` 分别仍为 `abc`、`DDD`。带引号标识符的 `value` 保留解码后的名称文本及其大小写，不包含双引号、反引号或方括号定界符。仅 generation 为 `0` 的 deparse 保证保留定界符和转义字节。Name API 只暴露明确支持读取和改写的标识符原子；关键字、操作符、结构控制值、字面量、payload 等其他 AST 字符串字段不作为 name 暴露。

### Literal

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_literal_count()` | 返回 literal 数量 |
| `sqlparser_statement_literal()` | 读取指定 literal |
| `sqlparser_statement_set_literal()` | 改写指定 literal |

`sqlparser_literal_view_t.quoted_identifier` 为 `1` 时，表示字符串 literal 来源于带引号标识符 token，例如 `ALTER SESSION SET CURRENT_SCHEMA="AppMixed"` 中的 schema 值。普通字符串字面量和未加引号标识符该字段为 `0`。

### INSERT

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_insert_source_kind()` | 返回 `INSERT` 数据来源 |
| `sqlparser_insert_column_count()` | 返回目标列数量 |
| `sqlparser_insert_column_name()` | 读取目标列名称 |
| `sqlparser_insert_row_count()` | 返回 `VALUES` 行数 |
| `sqlparser_insert_cell_literal()` | 读取指定单元格字面量 |
| `sqlparser_insert_set_cell_literal()` | 改写指定单元格字面量 |
| `sqlparser_insert_cell_sql()` | 读取指定单元格右值 SQL |
| `sqlparser_insert_set_cell_sql()` | 改写指定单元格右值 SQL |

### SELECT 输出列表

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_select_target_list_count()` | 返回语句中的 SELECT target list 数量 |
| `sqlparser_select_target_count()` | 返回指定 target list 的输出项数量 |
| `sqlparser_select_target_sql()` | 读取指定输出项 SQL |
| `sqlparser_select_set_target_sql()` | 使用完整 target SQL 替换指定输出位置；不继承原 target 的别名；多输出项片段在该位置展开 |
| `sqlparser_select_set_targets_sql()` | 替换整个 SELECT 输出列表 |
| `sqlparser_select_insert_target_sql()` | 在 SELECT 输出列表中插入输出项 |
| `sqlparser_select_delete_target()` | 删除 SELECT 输出项 |

`target_list_index` 用于区分同一语句中的多个 `SelectStmt`，例如子查询、CTE 或集合运算分支。

### UPDATE 与 WHERE

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_update_assignment_count()` | 返回 `SET` 赋值项数量 |
| `sqlparser_update_assignment()` | 读取指定赋值项 |
| `sqlparser_update_set_assignment_literal()` | 将赋值项右值 literal 或 bind 改写为 literal |
| `sqlparser_update_assignment_sql()` | 读取赋值项右值 SQL |
| `sqlparser_update_set_assignment_sql()` | 改写赋值项右值 SQL |
| `sqlparser_update_insert_assignment_sql()` | 插入完整 `SET` 赋值项 |
| `sqlparser_update_delete_assignment()` | 删除指定 `SET` 赋值项 |
| `sqlparser_update_set_assignment_full_sql()` | 整项替换指定 `SET` 赋值项 |
| `sqlparser_statement_where_literal_count()` | 返回 WHERE literal 数量 |
| `sqlparser_statement_where_literal()` | 读取指定 WHERE literal |
| `sqlparser_statement_where_set_literal()` | 改写指定 WHERE literal |
| `sqlparser_statement_where_count()` | 返回可写 WHERE 槽位数量 |
| `sqlparser_statement_where_sql()` | 读取 WHERE 条件 SQL |
| `sqlparser_statement_set_where_sql()` | 设置或替换 WHERE 条件 SQL |
| `sqlparser_statement_append_where_sql()` | 按 `AND` 或 `OR` 追加 WHERE 条件 |

### 通用子句

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_clause_count()` | 返回可写 statement 级子句数量 |
| `sqlparser_statement_clause()` | 读取指定子句视图 |
| `sqlparser_statement_clause_sql()` | 读取指定子句 SQL |
| `sqlparser_statement_set_clause_sql()` | 设置或替换指定子句 SQL |
| `sqlparser_statement_append_clause_condition()` | 按 `AND` 或 `OR` 向 `where` 子句追加条件 |
| `sqlparser_clause_sql()` | 通过 `sqlparser_clause_view_t` 渲染子句 SQL |

通用子句接口只负责结构级改写；字段、值和来源关系通过 `query_graph` 读取。

## Selector 接口

selector 用于把可读或可写对象表示为稳定文本路径或结构体。

常见 selector：

```text
stmt[0].relation[0]
stmt[0].name[3]
stmt[0].value[4]
stmt[0].literal[1]
stmt[0].where_literal[0]
stmt[0].clause[0]
stmt[0].assignment[0]
stmt[0].assignment[1][0]
stmt[0].merge_assignment[1][0]
stmt[0].merge_assignment[2][1][0]
stmt[0].merge_branch_condition[1]
stmt[0].merge_branch_condition[2][1]
stmt[0].merge_delete_condition[1]
stmt[0].merge_delete_condition[2][1]
stmt[0].merge_insert_column[1][2]
stmt[0].merge_insert_column[2][1][2]
stmt[0].merge_insert_cell[1][2]
stmt[0].merge_insert_cell[2][1][2]
stmt[0].insert_cell[1][2]
stmt[0].insert_branch_columns[0]
stmt[0].insert_branch_columns[2][1]
stmt[0].insert_branch_condition[0]
stmt[0].select_targets[0]
stmt[0].select_target[0][1]
stmt[0].dml_result_targets[0][0]
stmt[0].dml_result_target[0][0][1]
stmt[0].dml_result_sink[0][0]
stmt[0].dml_result_sink_columns[0][0]
stmt[0].dml_result_sink_column[0][0][1]
```

`stmt[S].assignment[A]` 定位 statement `S` 的根 `UPDATE` 或根 `INSERT` 冲突更新列表中的第 `A` 个赋值项；冲突更新包括 PostgreSQL `ON CONFLICT DO UPDATE` 和 MySQL `ON DUPLICATE KEY UPDATE`。嵌套 `UPDATE` 使用 `stmt[S].assignment[D][A]`，其中 `D` 是当前 statement 内 0 基 DML 序号，`A` 是目标列表内 0 基赋值序号。解析后的 `SQLPARSER_SELECTOR_KIND_ASSIGNMENT` 中，根形式的 `row_index` 为 `0`，嵌套形式保存 `D + 1`，`item_index` 保存 `A`；加一编码用于区分根形式与嵌套 DML `D = 0`。根 MERGE 的 matched UPDATE action 使用 `stmt[S].merge_assignment[W][A]`；嵌套 MERGE 使用 `stmt[S].merge_assignment[D][W][A]`。`W` 是目标 MERGE 中所有 `WHEN` 子句的绝对 0 基序号，不是仅对 UPDATE action 重新编号；`W` 必须指向 `WHEN MATCHED ... THEN UPDATE`。解析后 MERGE selector 的 `kind` 为 `SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT`；根 MERGE 的 `row_index` 为 `0`，嵌套 MERGE 的 `row_index` 保存 `D`，`item_index` 保存 `W`，`column_index` 保存 `A`。

MERGE 分支条件使用 `stmt[S].merge_branch_condition[W]`；嵌套 MERGE 使用 `stmt[S].merge_branch_condition[D][W]`。其 `kind` 为 `SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION`，坐标含义与 MERGE assignment selector 中的 `D`、`W` 相同。无条件分支没有 condition selector。

Oracle/Dameng matched UPDATE 分支附属的 `DELETE WHERE` 条件使用 `stmt[S].merge_delete_condition[W]`；嵌套 MERGE 使用 `stmt[S].merge_delete_condition[D][W]`。其 `kind` 为 `SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`，`D`、`W` 与 MERGE assignment selector 含义相同。该 selector 只定位同一 UPDATE action 的附属删除条件，不表示独立 DELETE 分支。PostgreSQL 和 SQL Server 的 `WHEN MATCHED ... THEN DELETE` 仍使用独立 MERGE branch 与 `merge_action_kind = delete`。

`sqlparser_selector_clause_sql()` 返回条件表达式，不包含 `DELETE WHERE` 关键字。`sqlparser_selector_set_clause_sql()` 和 `SQLPARSER_PATCH_REPLACE` 接收的也是不带 `WHERE` 的条件表达式。

MERGE INSERT 的单个目标列使用 `stmt[S].merge_insert_column[W][C]`，完整 VALUES cell 使用 `stmt[S].merge_insert_cell[W][C]`；对应 kind 分别为 `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN` 和 `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL`。嵌套 MERGE 分别使用 `stmt[S].merge_insert_column[D][W][C]` 和 `stmt[S].merge_insert_cell[D][W][C]`。具有 VALUES 的 INSERT 分支复用 `SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS` 作为目标列表 selector，文本形式为 `stmt[S].insert_branch_columns[W]`；嵌套 MERGE 使用 `stmt[S].insert_branch_columns[D][W]`。`D`、`W` 的含义与 MERGE assignment selector 相同，`C` 是 INSERT 分支内的 0 基列序号。解析后的根 MERGE selector 使用 `row_index = 0`，嵌套 MERGE 的 `row_index` 保存 `D`，`item_index` 保存 `W`，单项 selector 的 `column_index` 保存 `C`。省略目标列列表时不输出单列 selector，但具有 VALUES 时仍输出 cell selector 和目标列表 selector；后者用于物化目标列列表或单独增加 VALUES cell。省略目标列列表的 `INSERT DEFAULT VALUES` 分支为 0 列、0 行，不输出这些 selector；显式目标列列表的 DEFAULT VALUES 分支仍可能输出既有单列和目标列表 selector。

`sqlparser_selector_update_assignment()`、`sqlparser_selector_update_assignment_sql()`、`sqlparser_selector_set_update_assignment_*()`、`sqlparser_selector_insert_update_assignment_*()` 和 `sqlparser_selector_delete_update_assignment()` 均接受 `assignment` 与 `merge_assignment` selector。

### selector 解析与读取

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_parse()` | 文本转 `sqlparser_selector_t` |
| `sqlparser_selector_format()` | `sqlparser_selector_t` 转文本 |
| `sqlparser_selector_relation()` | 通过 selector 读取 relation |
| `sqlparser_selector_name()` | 通过 selector 读取 name |
| `sqlparser_selector_literal()` | 通过 selector 读取 literal |
| `sqlparser_selector_where_literal()` | 通过 selector 读取 WHERE literal |
| `sqlparser_selector_where_sql()` | 通过 selector 读取 WHERE 条件 SQL |
| `sqlparser_selector_clause()` | 通过 selector 读取通用子句视图 |
| `sqlparser_selector_clause_sql()` | 通过 selector 读取通用子句 SQL、Oracle/Dameng `INSERT ALL/FIRST` 分支条件 SQL、MERGE 分支条件 SQL 和 MERGE 附属 DELETE 条件 SQL |
| `sqlparser_selector_update_assignment()` | 通过 selector 读取 assignment |
| `sqlparser_selector_update_assignment_sql()` | 通过 selector 读取 assignment 右值 SQL |
| `sqlparser_selector_insert_cell_literal()` | 通过 selector 读取 INSERT cell literal |
| `sqlparser_selector_insert_cell_sql()` | 通过 selector 读取 INSERT 单元格右值 SQL |
| `sqlparser_selector_select_target_sql()` | 通过 selector 读取 SELECT 输出项 SQL |

### selector 改写

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | 按通用 relation 绑定规则改写 relation 名称 |
| `sqlparser_selector_set_name()` | 改写名称原子 |
| `sqlparser_selector_set_literal()` | 改写 literal |
| `sqlparser_selector_set_where_literal()` | 改写 WHERE literal |
| `sqlparser_selector_set_where_sql()` | 设置或替换 WHERE 条件 |
| `sqlparser_selector_append_where_sql()` | 向 WHERE 追加条件 |
| `sqlparser_selector_set_clause_sql()` | 设置或替换通用子句、MERGE 分支条件或 MERGE 附属 DELETE 条件 |
| `sqlparser_selector_append_clause_condition()` | 向 `where` 类型子句追加条件 |
| `sqlparser_selector_set_update_assignment_literal()` | 将 assignment 右值 literal 或 bind 改写为 literal |
| `sqlparser_selector_set_update_assignment_sql()` | 改写 assignment 右值 SQL |
| `sqlparser_selector_insert_update_assignment_sql()` | 插入完整 `SET` 赋值项 |
| `sqlparser_selector_insert_update_assignment_from_assignment_value()` | 用结构化目标列和已有 assignment 右值克隆插入 `SET` 赋值项 |
| `sqlparser_selector_delete_update_assignment()` | 删除 `SET` 赋值项 |
| `sqlparser_selector_set_update_assignment_full_sql()` | 整项替换 `SET` 赋值项 |
| `sqlparser_selector_set_insert_cell_literal()` | 改写 INSERT 单元格 literal |
| `sqlparser_selector_set_insert_cell_sql()` | 改写 INSERT 单元格右值 SQL |
| `sqlparser_selector_set_select_target_sql()` | 使用完整 target SQL 替换 SELECT 指定输出位置；不继承原 target 的别名；多输出项片段在该位置展开 |
| `sqlparser_selector_set_select_targets_sql()` | 改写 SELECT 整个输出列表 |
| `sqlparser_selector_replace_select_target_with_columns()` | 用结构化列列表替换一个 SELECT 输出项 |

### 结构化 SQL 片段改写

`sqlparser_apply_patch()` 是推荐的统一改写入口。既有 statement、selector 和结构化便捷改写函数继续保留并执行各自的公开参数校验；转换为 patch 后，共享原子失败回滚、handle generation 更新和派生缓存失效规则。

结构化改写接口使用 selector 定位目标，将 `sqlparser_identifier_path_view_t` 等结构化输入按 handle 方言渲染，并通过同一 patch 事务应用；需要复用已有 assignment 值时，在事务候选上克隆对应节点。调用方只提供标识符分段和源 selector，不需要拼接 SQL 片段，也不需要传入 quote 字符。

`sqlparser_selector_insert_update_assignment_from_assignment_value()` 用于向根或嵌套 `UPDATE`、根 `INSERT` 冲突更新列表或 MERGE matched UPDATE action 插入新赋值项。函数会克隆 `source_assignment_selector` 指向的 assignment 右值，并以 `target` 作为新 assignment 左侧；插入位置 selector 和来源 selector 均可使用 `assignment` 或 `merge_assignment`。两个 selector 必须指向同一 statement，否则函数返回 `SQLPARSER_STATUS_UNSUPPORTED`：

```c
const char *backup_parts[] = {"phone_backup"};
sqlparser_identifier_path_view_t target;
sqlparser_selector_t insert_selector;
sqlparser_selector_t source_selector;

target.parts = backup_parts;
target.part_count = 1;

sqlparser_selector_parse("stmt[0].assignment[0]", &insert_selector, &err);
sqlparser_selector_parse("stmt[0].assignment[0]", &source_selector, &err);
sqlparser_selector_insert_update_assignment_from_assignment_value(
    handle,
    &insert_selector,
    &target,
    &source_selector,
    &err);
```

`sqlparser_update_set_assignment_literal()` 和 `sqlparser_selector_set_update_assignment_literal()` 只替换 assignment 右值并保留左侧目标列。原右值为 literal 或 bind 时可替换为 `sqlparser_literal_value_t`；原右值为函数、运算表达式、字段引用、`DEFAULT` 或子查询时返回 `SQLPARSER_STATUS_UNSUPPORTED`。

`sqlparser_selector_replace_select_target_with_columns()` 用于把一个 SELECT 输出项替换为多个结构化列 target，常用于将 `*` 或 `alias.*` 展开为调用方已经计算好的列列表：

```c
const char *id_parts[] = {"u", "id"};
const char *name_parts[] = {"u", "name"};
sqlparser_identifier_path_view_t columns[2];
sqlparser_selector_t target_selector;

columns[0].parts = id_parts;
columns[0].part_count = 2;
columns[1].parts = name_parts;
columns[1].part_count = 2;

sqlparser_selector_parse("stmt[0].select_target[0][0]", &target_selector, &err);
sqlparser_selector_replace_select_target_with_columns(
    handle,
    &target_selector,
    columns,
    2,
    &err);
```

两个接口的输入数组均为 borrowed view，库不会在 handle 中保存调用方指针。失败时返回错误状态，并保持原 handle 不变。

## query_graph C 结构化遍历

`query_graph` 提供查询块、关系、输出项、字段引用、条件、DML 写入、会话状态和值绑定的结构化访问。

### 获取入口

```c
sqlparser_status_t sqlparser_statement_query_graph(
    const sqlparser_handle_t *handle,
    size_t statement_index,
    sqlparser_query_graph_view_t *out_graph,
    sqlparser_error_t *out_error);
```

`sqlparser_query_graph_view_t` 包含当前 statement 的计数和根 block 信息。view 不拥有内存，生命周期与 handle 和当前 generation 一致。

### 读取函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_query_graph_span_index_at()` | 从 span 中读取第 N 个全局索引 |
| `sqlparser_query_graph_block_at()` | 读取 query block |
| `sqlparser_query_graph_relation_at()` | 读取 relation |
| `sqlparser_query_graph_target_at()` | 读取 SELECT target |
| `sqlparser_query_graph_field_at()` | 读取字段 occurrence |
| `sqlparser_query_graph_value_at()` | 读取 query graph value |
| `sqlparser_query_graph_set_at()` | 读取集合运算节点 |
| `sqlparser_query_graph_predicate_at()` | 读取 WHERE、ON、HAVING、START WITH 或 CONNECT BY 谓词节点 |
| `sqlparser_query_graph_session()` | 读取当前语句的会话状态操作 |
| `sqlparser_query_graph_session_item_at()` | 读取会话状态目标 |
| `sqlparser_query_graph_session_value_at()` | 读取会话状态值 |
| `sqlparser_query_graph_dml()` | 读取当前 statement 中索引 0 的 DML 写入结构 |
| `sqlparser_query_graph_dml_count()` | 读取当前 statement 的 DML 数量 |
| `sqlparser_query_graph_dml_at()` | 按 0 基索引读取 DML |
| `sqlparser_query_graph_dml_parent()` | 读取嵌套 DML 的父 DML 索引 |
| `sqlparser_query_graph_dml_result_count()` | 读取指定 DML 的结果通道数量 |
| `sqlparser_query_graph_dml_result_at()` | 读取指定 DML 的结果通道 |
| `sqlparser_query_graph_dml_reference_at()` | 读取结果通道中的字段来源关系 |
| `sqlparser_query_graph_dml_branch_at()` | 读取 DML 分支，包括 Oracle/Dameng multi-table INSERT 分支和 MERGE `WHEN` 分支 |
| `sqlparser_query_graph_merge_branch_detail()` | 读取 MERGE 分支的 action、match 类型和 branch assignment span；非 MERGE 分支返回 `SQLPARSER_STATUS_UNSUPPORTED` |
| `sqlparser_query_graph_dml_column_at()` | 读取 INSERT 目标列 |
| `sqlparser_query_graph_dml_cell_at()` | 读取 INSERT VALUES 单元格 |
| `sqlparser_query_graph_dml_assignment_at()` | 读取 UPDATE/MERGE 赋值项 |

### 主要结构

| 结构体 | 说明 |
| --- | --- |
| `sqlparser_graph_block_t` | 查询块，持有 relation、target 和 predicate span |
| `sqlparser_graph_relation_t` | SQL 中出现的 base、derived、cte 或 dual relation；`database_quoted_identifier`、`schema_quoted_identifier`、`quoted_identifier`、`alias_quoted_identifier` 和 `link_quoted_identifier` 分别表示 database、schema、对象名、relation alias 和 database link 的定界符状态 |
| `sqlparser_graph_target_t` | 查询或 DML 结果输出项；`output_quoted_identifier` 表示 `output_name` 对应 token 的定界符状态 |
| `sqlparser_graph_field_t` | SQL 中出现的字段 occurrence；`quoted_identifier` 表示列名 token 是否显式使用支持的标识符定界符，`pseudo` / `prior` 表示层次查询 occurrence 语义 |
| `sqlparser_graph_value_t` | query graph 中的 literal、bind、default、expression 或 field 值 |
| `sqlparser_graph_set_t` | `UNION`、`UNION ALL`、`INTERSECT`、`EXCEPT/MINUS` 分支关系 |
| `sqlparser_graph_predicate_t` | WHERE、ON、HAVING、START WITH、CONNECT BY 中的比较、组合、EXISTS 或表达式谓词；`nocycle` 标记 CONNECT BY 根谓词 |
| `sqlparser_graph_session_t` | 当前语句的会话状态操作和 item 数量 |
| `sqlparser_graph_session_item_t` | 会话状态作用域、目标和 value span |
| `sqlparser_graph_session_value_t` | 会话状态的标识符、关键字、字面量、bind 或表达式值 |
| `sqlparser_graph_dml_t` | INSERT、UPDATE、DELETE、MERGE 写入结构 |
| `sqlparser_graph_dml_result_t` | DML 结果通道、输出 block、relation-backed sink 的可选 relation 和 columns，以及字段来源 span |
| `sqlparser_graph_dml_reference_t` | 一个结果 target 对目标行或来源 relation 的字段引用 |
| `sqlparser_graph_dml_branch_t` | DML 分支的公共结构，包括目标 relation、目标列、行、分支条件、可选 MERGE 附属 DELETE 条件和分支序号 |
| `sqlparser_graph_dml_column_t` | INSERT 显式目标列或 relation-backed DML 结果 sink 目标列；`quoted_identifier` 表示 `column_name` 对应 token 的定界符状态 |
| `sqlparser_graph_dml_cell_t` | INSERT VALUES 单元格；Oracle/Dameng multi-table INSERT 中可通过 `source_target_index` 关联末尾 source query 输出项 |
| `sqlparser_graph_dml_assignment_t` | UPDATE/MERGE 赋值项 |

### 归属规则

- `sqlparser_graph_relation_t.database_quoted_identifier`、`schema_quoted_identifier`、`quoted_identifier`、`alias_quoted_identifier` 和 `link_quoted_identifier` 分别仅对应 `database_name`、`schema_name`、`object_name`、`alias_name` 和 `link_name`。前两个和 `link_quoted_identifier` 的 C 类型为 `unsigned char`；已有的对象名和 alias 字段语义不变。
- `sqlparser_graph_field_t.quoted_identifier` 仍仅对应字段 occurrence 的 `column_name`；`sqlparser_graph_target_t.output_quoted_identifier` 仍对应 `output_name`。存在显式输出 alias 时只依据 alias token；没有显式 alias 且 `output_name` 由直接字段继承时依据该字段 token。显式 alias 的状态优先于底层字段，其他情况为 `0`。
- `sqlparser_graph_dml_column_t.quoted_identifier` 的 C 类型为 `int`，仅对应目标列的 `column_name`。该字段用于普通 INSERT、MERGE INSERT 分支、Oracle、Dameng 与 Vastbase-Oracle `INSERT ALL/FIRST` 分支和 SQL Server `OUTPUT ... INTO` relation-backed sink 中的目标列。
- 上述标志只在各自的精确来源 token 使用 `"..."`、MySQL 反引号或 SQL Server `[...]` 时为 `1`，否则为 `0`；它们不区分定界符类型。PostgreSQL `U&"..."`、普通单引号字符串和解析器内部生成的引号样式不会使这些标志置为 `1`。
- relation 的五个组件标志覆盖普通 SELECT/INSERT/UPDATE/DELETE/MERGE relation、multi-table INSERT 分支目标、relation-backed DML 结果 sink 和远程对象。Oracle、Dameng 和 Vastbase-Oracle 的 `INSERT ALL ... INTO ...@link` 分支目标 relation 会完整投影 `object_name`、`link_name` 及对应的定界符标志。
- 这些标量字段不产生需要调用方释放的独立分配；query graph 的所有权和生命周期规则不变。
- 在 x86_64 和 AArch64 的 64 位布局中，relation 的三个新 `unsigned char` 字段占用 2.16.8 `sqlparser_graph_relation_t` 的既有 padding，`sqlparser_graph_dml_column_t.quoted_identifier` 占用该结构的既有尾部 padding；两个结构的 `sizeof` 和全部旧字段 offset 保持不变。该结论不适用于 32 位布局，不能视为全平台 ABI 不变声明。
- `sqlparser_graph_relation_t.link_name` 表达远程对象引用中的 database link；SQL 未出现时为 `NULL`。
- `relations[].source_block_index` 表达派生表或 CTE 来源。
- 同一个 CTE 定义只构建一个来源 block；多次引用共享该 `source_block_index`，未被引用的 CTE 定义仍保留在 graph 中。
- `targets[].star_relations` 表达 `*` 或 `alias.*` 覆盖的 relation。
- `targets[].source_block_index` 表达星号或子查询 target 的来源 block。
- `sets[].branch_blocks` 表达集合运算左右分支。
- `predicates[]` 表达 `WHERE`、`ON`、`HAVING`、`START WITH`、`CONNECT BY` 中的谓词树；`children` span 表达 `AND`、`OR`、`NOT` 的子谓词。
- 层次查询字段和值复用 `fields[]`、`values[]` 和 `predicates[]`。`START WITH` 与 `CONNECT BY` occurrence 分别使用 `SQLPARSER_CLAUSE_KIND_START_WITH` 和 `SQLPARSER_CLAUSE_KIND_CONNECT_BY`；同一 SELECT 中的条件相关 occurrence 按 `WHERE`、`START WITH`、`CONNECT BY`、`GROUP BY` / `HAVING`、窗口与 `ORDER BY` 的语义遍历顺序构建。selector 仍来自通用 AST descriptor 遍历，应作为不透明定位路径使用，不能按源文本子句顺序推导序号。
- 当前查询块存在 `CONNECT BY` 时，未加标识符定界符的 `LEVEL`、`CONNECT_BY_ISLEAF` 和 `CONNECT_BY_ISCYCLE` 使用 `sqlparser_graph_field_t.pseudo = 1`，不关联 relation；对应 SELECT pseudo target 通过 `field_index` 回指该字段。带定界符的 `"LEVEL"` 和不含 `CONNECT BY` 的查询块保持普通字段语义，嵌套 SELECT 不继承外层层次上下文。
- `PRIOR` 透明标记其操作数内的字段 occurrence，设置 `sqlparser_graph_field_t.prior = 1`，且仅适用于当前 `CONNECT BY` 条件。`CONNECT_BY_ROOT` 的 SELECT target 仍为 `SQLPARSER_GRAPH_TARGET_EXPRESSION`；其底层字段通过单个 `target_path` entry 表达，`kind = "operator"`、`name = "CONNECT_BY_ROOT"`、`arg_index = 0`。
- `CONNECT BY NOCYCLE` 在 CONNECT BY 根 predicate 上设置 `sqlparser_graph_predicate_t.nocycle = 1`。该能力不增加 hierarchy 专用对象、selector、patch 类型或公开函数；字段、值、relation 与 SELECT target 改写继续使用既有 selector 和 patch 类型。
- `field = literal/bind` 谓词通过 `left_field_index + value_index` 表达；`field = field` 谓词通过 `left_field_index + right_field_index` 表达，并在 `values[]` 中以 `SQLPARSER_GRAPH_VALUE_FIELD` 记录右侧来源字段。
- 字段引用如果不能仅凭 SQL 唯一归属，`has_relation` 为 0，`candidate_relations` 给出当前 scope 候选 relation。
- `sqlparser_graph_dml_t.insert_mode` 区分 `VALUES`、`SELECT`、`INSERT ALL`、`INSERT FIRST`、MySQL `INSERT ... SET` 以及 `REPLACE` 的 `VALUES`、`SELECT`、`SET` 形态。
- MySQL 与 Vastbase-MySQL 的多目标 UPDATE 设置 `sqlparser_graph_dml_t.has_target_relation = 0`；每个 assignment 的 `target_field_index` 指向具有独立 relation 归属的目标字段。Dameng 多表 UPDATE 始终只有一个写入目标并设置 `has_target_relation = 1`。
- `sqlparser_query_graph_dml_count()` 和 `sqlparser_query_graph_dml_at()` 用于遍历同一 statement 内的全部 DML；`sqlparser_query_graph_dml()` 是读取索引 0 的兼容简写。多个无父 DML 可以并列存在，使用 `sqlparser_query_graph_dml_parent()` 区分根节点和嵌套节点。
- `sqlparser_query_graph_dml_parent()` 表达嵌套 DML 的父子关系；没有父 DML 时 `out_has_parent` 为 0。
- `sqlparser_graph_dml_result_t.kind` 区分 client 和 sink 通道；sink 可以由 relation 或 host bind 接收。仅 relation-backed sink 设置 `has_sink_relation = 1`，并通过 `sink_relation_index` 和可选 `sink_columns` 指向写入目标。
- host-bind sink 不设置 relation 关联。对应的 `sqlparser_graph_target_t` 设置 `has_sink_value = 1`，此时 `sink_value_index` 可传给 `sqlparser_query_graph_value_at()` 读取输出 bind；该 `sqlparser_graph_value_t` 的既有 `selector` 可作为 `SQLPARSER_PATCH_REPLACE` 的目标，不引入新的 selector 类型。
- `sqlparser_graph_dml_result_t.references` 中的索引通过 `sqlparser_query_graph_span_index_at()` 读取，再传给 `sqlparser_query_graph_dml_reference_at()`。每个 reference 关联一个结果 target，并标明 `target_before`、`target_after` 或 `source` 来源。
- DML 结果 target 使用 `stmt[S].dml_result_target[D][C][T]` selector；通道 target 列表、sink relation、sink columns 列表和单列分别使用 `dml_result_targets`、`dml_result_sink`、`dml_result_sink_columns` 和 `dml_result_sink_column` selector。
- `sqlparser_graph_dml_t.branches` 用于 Oracle/Dameng multi-table INSERT 和 MERGE。每个 branch 持有独立 target relation、target columns、rows、branch kind 和可选 condition selector；condition selector 可通过 `sqlparser_selector_clause_sql()` 读取原始条件 SQL。Oracle/Dameng matched UPDATE 分支可另外设置 `has_delete_condition_selector = 1` 和 `delete_condition_selector`，用于定位同一 UPDATE action 的附属 `DELETE WHERE` 条件。
- 对于解析成功的 MERGE，每个 `WHEN` 子句对应一个 branch，`ordinal` 是所有 `WHEN` 子句中的绝对 0 基序号。通过 `sqlparser_query_graph_merge_branch_detail()` 读取该 branch 的 action、match 类型和 assignment span。INSERT action 的 cell `row_index` 等于该绝对 `WHEN` 序号，`column_ordinal` 是 VALUES 中的 0 基位置；省略目标列列表时 `target_columns` 可以为空而 `rows` 非空。UPDATE action 的 assignment 同时出现在父 DML assignment span 和 branch detail assignment span 中，二者引用同一 assignment 索引。DELETE 和 NOTHING action 不携带 target columns、rows 或 assignments。
- Oracle/Dameng multi-table INSERT branch cell 如果直接引用末尾 source query 输出字段，`sqlparser_graph_dml_cell_t.kind` 为 `SQLPARSER_GRAPH_VALUE_FIELD`，并通过 `has_source_target/source_target_index` 指向对应 `targets[]` 项。
- `UPDATE` 和 `MERGE` assignment 的右侧如果是直接字段引用，`sqlparser_graph_dml_assignment_t.value_kind` 为 `SQLPARSER_GRAPH_VALUE_FIELD`，并通过 `has_source_field/source_field_index` 指向来源字段；来源字段可唯一匹配派生 source query 输出项时，同时提供 `has_source_target/source_target_index`。
- 仅当 `sqlparser_graph_dml_assignment_t.value_kind` 为 `SQLPARSER_GRAPH_VALUE_EXPRESSION` 时使用 `rhs_fields`、`rhs_values` 和 `rhs_blocks`。`rhs_fields` 和 `rhs_values` 分别归属右侧表达式在当前 assignment block 内的 field 和 value occurrence；`rhs_blocks` 归属从右侧表达式出发、不跨越另一层子查询边界即可到达的子查询入口 block。三个 span 的索引均通过 `sqlparser_query_graph_span_index_at()` 读取，再分别传给 `sqlparser_query_graph_field_at()`、`sqlparser_query_graph_value_at()` 或 `sqlparser_query_graph_block_at()`；子查询内部语义从入口 block 继续遍历。
- `value_kind` 为 `SQLPARSER_GRAPH_VALUE_FIELD`、`SQLPARSER_GRAPH_VALUE_LITERAL`、`SQLPARSER_GRAPH_VALUE_BIND` 或 `SQLPARSER_GRAPH_VALUE_DEFAULT` 时，assignment 继续使用既有 payload，三个 `rhs_*` span 的 `count` 均为 `0`。
- `values[]` 记录与字段或 SELECT target 关联的应用侧值，以及由复合 DML assignment 的 `rhs_values` 归属的右侧 literal、bind 和 default occurrence。仅通过 `rhs_values` 归属的值可以没有关联字段；`LIMIT/OFFSET`、`ROWNUM` 等分页或伪列 bind 不进入 `values[]`。
- `sqlparser_graph_value_t.field_match_kind` 仅在 `has_field` 为真时有效，用于区分 `secret = ?` 这类直接字段匹配和 `UPPER(secret) = ?` 这类表达式字段匹配。
- `sqlparser_graph_value_t.operator_kind` 是基于已归一操作符的结构化分类；调用方判断 pattern-match 语义时应使用 `sqlparser_graph_value_is_like_pattern()` 或枚举值，不需要比较 `operator_name` 字符串。
- 字段侧表达式包含多个字段时，每个可定位字段各输出一条 `expression_field` value 关系。
- 对于谓词，值侧是函数、CAST、运算符、数组、ROW 或 CASE 表达式时，关联字段的 value 使用 `SQLPARSER_GRAPH_VALUE_EXPRESSION`，不将该谓词表达式内部的 bind/literal 暴露为 direct value。复合 DML assignment 右侧表达式不适用该限制，其内部值通过 `rhs_values` 归属。
- `LIKE`、`NOT LIKE`、`ILIKE`、`NOT ILIKE` 带显式 `ESCAPE` 时，pattern 对应的 `sqlparser_graph_value_t.like_escape` 保存 escape 结构；没有显式 `ESCAPE` 时 `kind` 为 `SQLPARSER_GRAPH_LIKE_ESCAPE_NONE`。反解析输出保持公开 SQL 形态，例如 `LIKE pattern ESCAPE escape`。

### 会话状态

受支持的数据库、Schema、角色、身份、事务特征和会话参数语句会投影到 `sqlparser_graph_session_t`。当前语句没有可用的 session 投影时，`sqlparser_query_graph_session()` 返回成功，`action` 为 `SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN`，`item_count` 为 `0`。

当 `sqlparser_graph_session_value_t.kind` 为 `SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER` 时，`literal.quoted_identifier` 使用相同的精确 token 规则表达定界符状态。普通单引号字符串和解析器内部为方言兼容生成的引号样式不会设置该标记。Query graph 返回的字符串和结构仍由 handle 持有，调用方不释放。

```c
sqlparser_query_graph_view_t graph;
sqlparser_graph_session_t session;
sqlparser_graph_session_item_t item;
sqlparser_graph_session_value_t value;
size_t item_index;
size_t value_index;

sqlparser_statement_query_graph(handle, 0, &graph, &err);
sqlparser_query_graph_session(&graph, &session, &err);

for (item_index = 0; item_index < session.item_count; item_index++) {
    sqlparser_query_graph_session_item_at(
        &graph, item_index, &item, &err);

    for (value_index = 0; value_index < item.value_count; value_index++) {
        sqlparser_query_graph_session_value_at(
            &graph, item.value_offset + value_index, &value, &err);
    }
}
```

`action` 表示 `set`、`reset`、`switch`、`discard` 等操作。item 的 `scope` 表示 `session`、`local` 或 `transaction`，`target_kind` 表示 parameter、database、schema、role 等目标。`name` 可以是 SQL 中的显式名称，也可以是 `timezone`、`search_path` 这类规范化语义名称；没有可用名称时为 `NULL`，规范化名称不保证逐字出现在 SQL 中。

每个 value 还可以带可选的 `name`，用于标记同一 item 内具有独立语义的值，例如 MySQL `SET NAMES ... COLLATE ...` 中第二个 value 的 `name` 为 `collation`。没有可用的独立语义标签时，该字段为 `NULL`。

value 的其他字段按 `kind` 使用：

| `kind` | 有效字段 |
| --- | --- |
| `SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER` | `text` |
| `SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD` | `text` |
| `SQLPARSER_GRAPH_SESSION_VALUE_LITERAL` | `literal` |
| `SQLPARSER_GRAPH_SESSION_VALUE_BIND` | `bind_key`、`bind_kind`、`bind_sql`、`bind_position`、`has_bind_position` |
| `SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION` | `text` |

`bind_position` 从 `1` 开始，按同一 handle 中绑定参数的 SQL 出现顺序全局编号。返回结构体中的借用字符串指针，以及 item 的 value span 所引用的 graph 数据，仅在所属 query graph view 有效期内可用。

## JSON 导出与 Patch

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_export_view_json()` | 按需导出 View JSON |
| `sqlparser_apply_patch()` | 应用结构体 patch |

`sqlparser_export_view_json()` 的 `pretty` 参数：

- `0`：紧凑 JSON
- 非 `0`：格式化 JSON

`sqlparser_apply_patch()` 接收 `sqlparser_patch_list_t`。每个 patch 使用 selector 定位可写节点。

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].assignment[0]";
patch.sql = "'carol'";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

支持的 patch 操作：

| 操作 | 说明 |
| --- | --- |
| `SQLPARSER_PATCH_REPLACE` | 替换 relation、name、value、assignment、literal、where_literal、clause、MERGE 分支条件、MERGE 附属 DELETE 条件、insert_cell、MERGE INSERT 目标列或完整 cell、select_target 或 select_targets |
| `SQLPARSER_PATCH_INSERT_COLUMN` | 给普通 `INSERT ... VALUES` 单独增加列名或成对增加列和值、给 `INSERT ... SELECT` 增加目标列、给 Oracle/Dameng `INSERT ALL/FIRST` 的显式 VALUES branch 单独增加列名或成对增加列和值、给 MERGE INSERT 单独增加目标列、单独增加 value 或成对增加两者、向 `select_targets` 插入 SELECT 输出项，或向成对 DML 结果列表插入 target 和 receiver |
| `SQLPARSER_PATCH_DELETE_COLUMN` | 删除 `INSERT ... VALUES` 列、删除 `INSERT ... SELECT` 目标列、成对删除 MERGE INSERT 目标列和值，或删除 SELECT 输出项 |
| `SQLPARSER_PATCH_DELETE_ROW` | 删除 `INSERT ... VALUES` 行 |
| `SQLPARSER_PATCH_APPEND_CONDITION` | 按 `AND` 或 `OR` 向 `where` 子句追加条件 |
| `SQLPARSER_PATCH_INSERT_ASSIGNMENT` | 向根或嵌套 `UPDATE`、根 `INSERT` 冲突更新列表或 MERGE matched UPDATE action 插入赋值项 |
| `SQLPARSER_PATCH_DELETE_ASSIGNMENT` | 从根或嵌套 `UPDATE`、根 `INSERT` 冲突更新列表或 MERGE matched UPDATE action 删除赋值项 |
| `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` | 整项替换根或嵌套 `UPDATE`、根 `INSERT` 冲突更新列表或 MERGE matched UPDATE action 的赋值项 |

只有候选结果相对当前 handle 发生实际变化时，`sqlparser_apply_patch()` 才提交并将 generation 递增一次，旧 query graph view 随之失效。空 patch 列表或结果无实际变化的调用不递增；任一 patch 失败时整批不提交。

普通单表 `INSERT ... VALUES` 使用 `stmt[S].insert_columns` selector。若 `SQLPARSER_PATCH_INSERT_COLUMN` 仅提供非空 `name`、`index`，且不提供 `sql`、`default_sql`、`source_selector`、`literal` 或 `bind`，操作只插入目标列名，不修改任何 VALUES row。若同时从 `default_sql`、`source_selector`、`literal` 或 `bind` 中恰好提供一个值来源，则保持既有成对行为，在每个 VALUES row 的同一位置插入 cell。调用方可在同一个 patch list 中组合多个 name-only 列 patch、成对插入和 `REPLACE insert_cell`；批次中间允许暂时不等长，但提交前每个 VALUES row 的 cell 数必须等于显式列数，否则整批返回 `SQLPARSER_STATUS_INVALID_ARGUMENT` 并保持原 handle 不变。name-only VALUES 模式不适用于 `DEFAULT VALUES` 或 MySQL `INSERT ... SET`；`INSERT ... SELECT` 既有的目标列插入语义不变。

Oracle、Dameng 与 Vastbase-Oracle 兼容入口当前已建模的 `INSERT ALL/FIRST` 显式 VALUES branch 使用 `stmt[S].insert_branch_columns[B]` selector，其中 `B` 是 branch 序号。相同的 name-only payload 只增加该 branch 的列名，不修改 cells、其他 branch 或 source SELECT；提供一个值来源时保持现有成对插入。同一个 patch list 可分别修改多个 branch，并与 `REPLACE insert_cell` 组合；提交前每个被 name-only patch 触及的 branch 都必须满足列数与 cell 数相等，否则整批原子回滚。当前边界不包括省略 branch `VALUES` 或 branch 多 tuple；MERGE INSERT 使用下述独立规则。

三个 assignment patch 操作的目标 selector 均可使用 `stmt[S].assignment[A]`、`stmt[S].assignment[D][A]`、`stmt[S].merge_assignment[W][A]` 或 `stmt[S].merge_assignment[D][W][A]`。

MERGE INSERT 以 `insert_branch_columns` selector 作为 `SQLPARSER_PATCH_INSERT_COLUMN` 的目标，并接受三种载荷：只提供非空 `name` 时在目标列列表的 `index` 处增加列；不提供 `name`、但从 `default_sql`、`source_selector`、`literal` 或 `bind` 中恰好提供一个值来源时，在 VALUES 的 `index` 处增加 cell；同时提供两者时，在两侧的同一 `index` 成对增加。省略目标列列表但具有 VALUES 时仍可使用该 selector：name-only 操作会物化列列表，value-only 操作保持列表省略。

同一个 patch batch 可组合三种插入与单项替换，处理中允许列和值暂时不等长。批末对本批次触及且最终具有显式目标列列表的每个分支校验列数和值数相等；不相等时返回 `SQLPARSER_STATUS_INVALID_ARGUMENT` 并整批原子回滚。最终仍省略目标列列表的分支允许 value-only 插入，不执行显式列等宽校验。单项替换以 `merge_insert_column` 或 `merge_insert_cell` selector 作为 `SQLPARSER_PATCH_REPLACE` 的目标：前者通过 `sql` 提供标识符，后者通过 `sql`、`source_selector`、`literal` 或 `bind` 之一提供新值。

`SQLPARSER_PATCH_DELETE_COLUMN` 仍按列值对删除：使用同一 `insert_branch_columns` selector 和 `index`，并要求删除前存在等长的显式目标列与 VALUES 列表；省略列表、索引无效或删除最后一对时操作失败。`MERGE INSERT DEFAULT VALUES` 没有 VALUES 列表，因此三态插入和成对删除均返回 `SQLPARSER_STATUS_UNSUPPORTED`。省略目标列列表的 DEFAULT VALUES 分支为 0 列、0 行，不输出目标列表 selector；显式目标列列表时可能输出既有 selector，但该 selector 不会使上述操作可用。上述合同适用于本项目九个方言入口中成功解析的 MERGE，不表示对应数据库服务端均原生提供该语法。

对具有显式成对接收端的 DML 结果通道，以 `dml_result_targets` 列表 selector 作为 `SQLPARSER_PATCH_INSERT_COLUMN` 的目标。`index` 指定 target 与 receiver 的同位插入位置，`default_sql` 提供新 target SQL，`name` 提供对应 receiver。Oracle、Dameng 和 Vastbase-Oracle 兼容模式的 receiver 是冒号 bind；SQL Server 和 Vastbase SQL Server 兼容模式的 receiver 是显式 sink column。`sqlparser_apply_patch()` 在同一事务中原子插入两侧；两侧数量不等、索引或 receiver 非法、或载荷字段组合无效时操作失败，handle 保持不变。

`sqlparser_patch_t` 的值来源字段互斥：`sql`、`default_sql`、`source_selector`、`literal`、`bind` 中同一位置只能提供一种。`source_selector` 支持克隆已有 `insert_cell`、`merge_insert_cell`、`select_target` 或 assignment 的 SQL 片段；克隆 assignment 时同样接受 `assignment` 和 `merge_assignment` 两种 selector。`literal` 和 `bind` 由库按当前方言渲染，调用方不需要拼接占位符文本。

## Deparse 与字符串释放

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_deparse()` | 反解析当前 AST，生成 SQL 字符串 |
| `sqlparser_string_free()` | 释放库返回的字符串 |

`sqlparser_deparse()` 调用成功且 handle generation 为 `0` 时，返回值与输入 SQL 按字节一致，包括标识符引用形式、大小写、关键字、空白、换行、注释、分号和多语句边界。generation 大于 `0` 时，接口根据当前 handle 状态生成 SQL，整体不适用逐字节一致性保证。必须完整反解析 AST 时，空白、大小写及 `ROW` / `ROWS` 等拼写可能规范化；已建模的分页语法家族仍按所选方言输出。

## 常见使用模式

### 字段和值归属

1. 调用 `sqlparser_statement_query_graph()`。
2. 遍历 `relations`、`fields`、`values` 和 DML 结构。
3. 根据结构化字段执行调用方规则。

### selector 驱动改写

1. 通过 query graph 或 View JSON 获取 selector。
2. 构造 `sqlparser_patch_t`。
3. 调用 `sqlparser_apply_patch()`。
4. 调用 `sqlparser_deparse()`。
5. 使用同一方言重新解析生成 SQL，验证语法有效。

### SELECT 输出列表改写

1. 使用 `query_graph.targets[]` 找到目标输出项或 `target_list_selector`。
2. 使用 `sqlparser_apply_patch()` 或 SELECT target 接口增删改输出项。
3. 反解析并重新解析验证。

## 相关示例

| 示例 | 说明 |
| --- | --- |
| `examples/patch/08_view_patch.c` | View JSON 导出、patch、回放 |
| `examples/patch/13_select_target_patch.c` | 通过 patch 展开 `SELECT *`、插入输出列和删除输出列 |
| `examples/patch/14_where_patch.c` | 通过 patch 新增 WHERE 并追加条件 |
| `examples/patch/15_insert_columns_patch.c` | 通过 patch 增加和删除 `INSERT ... VALUES` 字段 |
| `examples/patch/16_clause_patch.c` | 通过通用 clause patch 改写 SELECT 输出列表、WHERE 和 ORDER BY |
| `examples/patch/17_update_set_patch.c` | 通过 patch 追加、删除和整项替换 `UPDATE SET` 赋值项 |
| `examples/patch/19_oracle_multi_insert_patch.c` | 通过 patch 改写 Oracle `INSERT ALL` 分支列和值 |
| `examples/convenience/18_structured_fragment_rewrite.c` | 结构化 UPDATE assignment 插入和 SELECT `*` 展开 |
| `examples/inspect/01_select_inspect.c` | SELECT 读取与多表关联信息 |
| `examples/inspect/03_insert_select_inspect.c` | `INSERT ... SELECT` 结构读取 |
| `examples/inspect/07_multi_statement_walk.c` | 多语句输入遍历 |
| `examples/dialect/10_mysql_dialect.c` | MySQL 方言解析与 patch 改写 |
| `examples/dialect/11_oracle_dialect.c` | Oracle 方言解析与改写 |
| `examples/dialect/12_sqlserver_dialect.c` | SQL Server `IF ... ELSE` 控制流、DML 返回通道与反解析 |
| `examples/dialect/17_dameng_dialect.c` | 达梦方言解析与反解析 |
| `examples/dialect/20_vastbase_dialect.c` | Vastbase 兼容模式解析与反解析 |
