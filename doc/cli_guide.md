# CLI 手册

`sqlparser_cli` 是仓库内提供的命令行工具，用于：

- 快速验证一条 SQL 的解析结果
- 导出 SQL View JSON
- 做单条 SQL 反解析检查
- 批量处理 JSON 文件中的 SQL 列表

常规接入优先使用 `view`，该模式输出语句、对象、字段、值片段和 selector。

可执行文件默认位于：

```text
bin/sqlparser_cli
```

## 1. 构建

```bash
make cli
```

如果需要同时构建全部示例和测试：

```bash
make all
```

## 2. 基本调用方式

命令格式：

```bash
./bin/sqlparser_cli [--mode view|deparse|all] [--dialect postgresql|mysql|oracle|sqlserver|dameng] [--compact] [--file PATH] [SQL]
```

也可以从标准输入读取 SQL：

```bash
echo "SELECT 1" | ./bin/sqlparser_cli --mode view
```

查看帮助：

```bash
./bin/sqlparser_cli --help
```

## 3. 输入方式

### 3.1 直接传入 SQL

```bash
./bin/sqlparser_cli "SELECT id, name FROM public.users WHERE id = 42"
```

### 3.2 通过文件读取

```bash
./bin/sqlparser_cli --file ./tests/cases/sample.sql
```

### 3.3 通过标准输入读取

```bash
cat ./tests/cases/sample.sql | ./bin/sqlparser_cli --mode deparse
```

## 4. 输出模式

`--mode` 支持以下取值：

| mode | 输出内容 |
| --- | --- |
| `view` | SQL View JSON |
| `deparse` | 反解析 SQL |
| `all` | 输出 SQL View JSON 和反解析 SQL |

示例：

```bash
./bin/sqlparser_cli --mode view "SELECT 1"
./bin/sqlparser_cli --mode deparse "SELECT 1"
```

## 5. 方言选择

默认方言为 `postgresql`。需要解析其他方言时，通过 `--dialect` 指定：

```bash
./bin/sqlparser_cli --dialect oracle --mode view \
  "SELECT q'[Bob's order]' AS label FROM dual"
```

```bash
./bin/sqlparser_cli --dialect dameng --mode view \
  "SET SCHEMA KDES; SELECT TOP 2 id, name FROM users WHERE id = :id"
```

批量 JSON 可以在顶层设置默认方言，也可以在单条 SQL 上覆盖：

```json
{
  "dialect": "oracle",
  "items": [
    {
      "name": "oracle-q-string",
      "sql": "SELECT q'[Bob's order]' AS label FROM dual"
    }
  ]
}
```

## 6. JSON 格式控制

默认输出格式化 JSON。

如果需要紧凑 JSON，可加 `--compact`：

```bash
./bin/sqlparser_cli --mode view --compact "SELECT 1"
```

## 7. 批量处理

批量模式用于把一个 JSON 文件中的多条 SQL 依次处理并输出聚合结果。

命令格式：

```bash
./bin/sqlparser_cli \
  --batch-file ./tests/cases/sql_batch_input.json \
  --output /tmp/sqlparser_batch_result.json
```

说明：

- `--batch-file` 与 `--file`、内联 SQL 不能同时使用
- `--output` 当前只在 `--batch-file` 模式下可用

### 7.1 支持的批量输入结构

批量输入 JSON 支持三种顶层形式：

1. 顶层数组
2. 顶层对象，包含 `items` 数组
3. 顶层对象，包含 `sqls` 数组

数组元素支持两种写法：

- 直接写 SQL 字符串
- 写成对象：`{"name":"case-name","dialect":"oracle","sql":"..."}`

`dialect` 支持 `postgresql`、`mysql`、`oracle`、`sqlserver`、`dameng`。

示例：

```json
[
  "SELECT 1",
  {
    "name": "update-user",
    "sql": "UPDATE public.users SET name = 'carol' WHERE id = 1"
  }
]
```

或：

```json
{
  "items": [
    {
      "name": "select-basic",
      "sql": "SELECT id FROM public.users"
    },
    {
      "name": "insert-basic",
      "sql": "INSERT INTO public.users (id, name) VALUES (1, 'bob')"
    }
  ]
}
```

### 7.2 批量输出结构

批量结果 JSON 顶层字段包括：

| 字段 | 说明 |
| --- | --- |
| `mode` | 当前执行模式 |
| `dialect` | 批量默认方言 |
| `source_file` | 输入文件路径 |
| `total` | 总条目数 |
| `succeeded` | 成功条目数 |
| `failed` | 失败条目数 |
| `has_failures` | 是否存在失败 |
| `items` | 每条 SQL 的结果对象 |

每个结果项至少包含：

| 字段 | 说明 |
| --- | --- |
| `index` | 1 基序号 |
| `name` | 可选，用例名称 |
| `dialect` | 当前条目实际使用的方言 |
| `sql` | 原始 SQL |
| `ok` | 是否成功 |

失败项会带 `error` 对象，包含：

- `stage`
- `code`
- `cursor`
- `line`
- `column`
- `message`

成功项会根据 `mode` 带不同字段，例如：

- `view`
- `deparse_sql`

## 8. 常见用途

### 8.1 快速查看多表查询视图

```bash
./bin/sqlparser_cli --mode view \
  "SELECT u.id, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid'"
```

### 8.2 导出 SQL View JSON

```bash
./bin/sqlparser_cli --mode view \
  "UPDATE public.users SET name = upper(name), updated_at = DEFAULT WHERE id = 1"
```

### 8.3 检查反解析结果

```bash
./bin/sqlparser_cli --mode deparse \
  "INSERT INTO public.users (id, name) VALUES (1, 'bob')"
```

## 9. 相关文档

- [快速开始](../README.md)
- [API 手册](./api_reference.md)
- [SQL View JSON 手册](./view_json.md)
