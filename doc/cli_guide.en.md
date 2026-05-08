# CLI Guide

`sqlparser_cli` is the command-line utility shipped with the repository. It is
used to:

- quickly inspect how one SQL statement parses
- export SQL View JSON
- check single-statement deparse output
- process a JSON file containing multiple SQL statements

Regular integrations should use `view`, which reports statements, objects,
columns, value fragments, and selectors.

The binary is generated at:

```text
bin/sqlparser_cli
```

## 1. Build

```bash
make cli
```

To build the full project, including examples and tests:

```bash
make all
```

## 2. Basic Invocation

Command form:

```bash
./bin/sqlparser_cli [--mode view|deparse|all] [--dialect postgresql|mysql|oracle|sqlserver] [--compact] [--file PATH] [SQL]
```

It can also read SQL from standard input:

```bash
echo "SELECT 1" | ./bin/sqlparser_cli --mode view
```

Show help:

```bash
./bin/sqlparser_cli --help
```

## 3. Input Forms

### 3.1 Inline SQL

```bash
./bin/sqlparser_cli "SELECT id, name FROM public.users WHERE id = 42"
```

### 3.2 SQL from a File

```bash
./bin/sqlparser_cli --file ./tests/cases/sample.sql
```

### 3.3 SQL from Standard Input

```bash
cat ./tests/cases/sample.sql | ./bin/sqlparser_cli --mode deparse
```

## 4. Output Modes

`--mode` accepts:

| Mode | Output |
| --- | --- |
| `view` | SQL View JSON |
| `deparse` | deparsed SQL |
| `all` | SQL View JSON and deparsed SQL |

Examples:

```bash
./bin/sqlparser_cli --mode view "SELECT 1"
./bin/sqlparser_cli --mode deparse "SELECT 1"
```

## 5. Dialect Selection

The default dialect is `postgresql`. Use `--dialect` for other dialects:

```bash
./bin/sqlparser_cli --dialect oracle --mode view \
  "SELECT q'[Bob's order]' AS label FROM dual"
```

Batch JSON can set a default dialect at the root or override it per item:

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

## 6. JSON Formatting

Pretty JSON is the default.

Use `--compact` for compact JSON:

```bash
./bin/sqlparser_cli --mode view --compact "SELECT 1"
```

## 7. Batch Processing

Batch mode processes a JSON file containing multiple SQL entries and emits one
aggregated result document.

Command form:

```bash
./bin/sqlparser_cli \
  --batch-file ./tests/cases/sql_batch_input.json \
  --output /tmp/sqlparser_batch_result.json
```

Notes:

- `--batch-file` cannot be combined with `--file` or inline SQL.
- `--output` currently requires `--batch-file`.

### 7.1 Supported Batch Input Shapes

The batch input JSON can be:

1. a top-level array
2. a top-level object with an `items` array
3. a top-level object with a `sqls` array

Array items can be either:

- a raw SQL string
- an object: `{"name":"case-name","dialect":"oracle","sql":"..."}`

Example:

```json
[
  "SELECT 1",
  {
    "name": "update-user",
    "sql": "UPDATE public.users SET name = 'carol' WHERE id = 1"
  }
]
```

Or:

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

### 7.2 Batch Output Shape

The batch result JSON contains:

| Field | Meaning |
| --- | --- |
| `mode` | selected execution mode |
| `dialect` | default batch dialect |
| `source_file` | input file path |
| `total` | total item count |
| `succeeded` | successful item count |
| `failed` | failed item count |
| `has_failures` | whether any item failed |
| `items` | per-SQL result objects |

Each result item contains at least:

| Field | Meaning |
| --- | --- |
| `index` | 1-based item index |
| `name` | optional case name |
| `dialect` | dialect used for this item |
| `sql` | original SQL |
| `ok` | success flag |

Failed items include an `error` object with:

- `stage`
- `code`
- `cursor`
- `line`
- `column`
- `message`

Successful items include mode-dependent fields such as:

- `view`
- `deparse_sql`

## 8. Common Uses

### 8.1 Inspect the SQL View of a Multi-Table Query

```bash
./bin/sqlparser_cli --mode view \
  "SELECT u.id, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid'"
```

### 8.2 Export SQL View JSON

```bash
./bin/sqlparser_cli --mode view \
  "UPDATE public.users SET name = upper(name), updated_at = DEFAULT WHERE id = 1"
```

### 8.3 Check Deparse Output

```bash
./bin/sqlparser_cli --mode deparse \
  "INSERT INTO public.users (id, name) VALUES (1, 'bob')"
```

## 9. Related Documents

- [Quick Start](../README.en.md)
- [API Reference](./api_reference.en.md)
- [SQL View JSON Guide](./view_json.en.md)
