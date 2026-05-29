# sqlparser Quick Start

[简体中文](./README.md)

`sqlparser` is a generic SQL parse, rewrite, and deparse library with a stable C API. It parses SQL into a reusable handle, exposes structural inspection APIs, supports controlled mutation, and deparses the rewritten structure back to SQL.

The parser kernel is based on a pinned vendored `libpg_query` version:

- tag: `17-6.2.2`
- commit: `7be1aed1f1f968a36cf541319f71e845850f0381`

## Features

This release provides:

- `sql -> handle`
- statement kind and node-name inspection
- statement-wide relation, name, and literal traversal and rewrite
- `INSERT`, `UPDATE`, and `WHERE` structural views, including WHERE insertion and condition append
- `SELECT` output-list read, replace, insert, and delete operations
- structured SQL fragment rewrites for UPDATE assignment insertion and SELECT output-column expansion from identifier paths
- stable selector parse / format / lookup
- dialect options with PostgreSQL as the default and MySQL / Oracle / SQL Server / Dameng conversion layers
- common prepared / parameterized SQL statement parsing, View JSON, and deparse output
- configurable resource limits for SQL input, generated output, and statement count
- View JSON export, C structured traversal, and structured patch write-back
- `handle -> sql`

## Public Artifacts

- header: `include/sqlparser/sqlparser.h`
- static library: `lib/libsqlparser.a`
- shared library: `lib/libsqlparser.so`
- CLI: `bin/sqlparser_cli`
- Windows MSVC static library: `build\msvc\lib\sqlparser.lib`
- Windows MSVC CLI: `build\msvc\bin\sqlparser_cli.exe`

## Build Dependencies

Linux:

- GCC 8.3 or later with `gnu11` support
- GNU Make
- `pkg-config`
- `jansson`

Windows:

- Visual Studio 2022 x64
- MSVC 19.39 or later
- NMake

The Windows build uses the vendored Jansson source included in this repository.

## Linux Build

```bash
make all
```

Common targets:

- `make static`
- `make shared`
- `make abi-check`
- `make examples`
- `make test`
- `make test-loop LOOP=50`
- `make verify-ci`
- `make verify`
- `make bench-build`
- `make bench-smoke`
- `make dist`
- `make install PREFIX=/usr/local`

## Windows Build

Run from an x64 Native Tools Command Prompt for VS 2022:

```bat
nmake /F Makefile.msvc test
```

Common targets:

- `nmake /F Makefile.msvc all`
- `nmake /F Makefile.msvc static`
- `nmake /F Makefile.msvc cli`
- `nmake /F Makefile.msvc examples`
- `nmake /F Makefile.msvc test`
- `nmake /F Makefile.msvc clean`

## Minimal Integration Example

```c
#include <stdio.h>
#include "sqlparser/sqlparser.h"

int main(void)
{
    sqlparser_handle_t *handle = NULL;
    sqlparser_error_t err;
    char *rewritten_sql = NULL;

    if (sqlparser_parse("SELECT id, name FROM public.users WHERE id = 42", &handle, &err)
        != SQLPARSER_STATUS_OK) {
        printf("parse failed: %s\n", err.message);
        return 1;
    }

    if (sqlparser_deparse(handle, &rewritten_sql, &err) != SQLPARSER_STATUS_OK) {
        printf("deparse failed: %s\n", err.message);
        sqlparser_handle_destroy(handle);
        return 1;
    }

    printf("%s\n", rewritten_sql);
    sqlparser_string_free(rewritten_sql);
    sqlparser_handle_destroy(handle);
    return 0;
}
```

Use `sqlparser_parse_with_options()` to select a non-default dialect:

```c
sqlparser_parse_options_t options;

sqlparser_parse_options_default(&options);
options.dialect = SQLPARSER_DIALECT_MYSQL;
```

Oracle, SQL Server, and Dameng SQL can be selected explicitly through `options.dialect`:

```c
options.dialect = SQLPARSER_DIALECT_ORACLE;
options.dialect = SQLPARSER_DIALECT_SQLSERVER;
options.dialect = SQLPARSER_DIALECT_DAMENG;
```

Compile the example with:

```bash
gcc -std=gnu11 demo.c -I./include -L./lib -lsqlparser -ljansson -o demo
```

After installation, `pkg-config` can be used as well:

```bash
gcc -std=gnu11 demo.c $(pkg-config --cflags --libs sqlparser) -o demo
```

## CLI

Parse a single SQL statement:

```bash
./bin/sqlparser_cli "SELECT id, name FROM public.users WHERE id = 42"
```

Export View JSON:

```bash
./bin/sqlparser_cli --mode view "SELECT id, name FROM public.users WHERE id = 42"
```

Process a JSON file containing multiple SQL statements:

```bash
./bin/sqlparser_cli \
  --batch-file ./sql_batch.json \
  --output ./sqlparser_batch_result.json
```

## Examples

Example programs are available under `examples/`:

- `examples/patch/`: recommended integration style through `sqlparser_apply_patch()`.
- `examples/convenience/`: fine-grained convenience API examples.
- `examples/inspect/`: structural inspection and traversal examples.
- `examples/dialect/`: dialect usage examples.

See [examples/README.md](./examples/README.md) for details.

## Documentation

- [Documentation Index](./doc/README.en.md)
- [Project Overview and Architecture](./doc/sqlparser_architecture.en.md)
- [PostgreSQL Dialect Support](./doc/postgresql_dialect_support.en.md)
- [MySQL Dialect Support](./doc/mysql_dialect_support.en.md)
- [Oracle Dialect Support](./doc/oracle_dialect_support.en.md)
- [SQL Server Dialect Support](./doc/sqlserver_dialect_support.en.md)
- [Dameng Dialect Support](./doc/dameng_dialect_support.en.md)
- [Dialect Coverage](./doc/dialect_coverage.en.md)
- [API Reference](./doc/api_reference.en.md)
- [View JSON Guide](./doc/view_json.en.md)
- [CLI Guide](./doc/cli_guide.en.md)
- [libpg_query Integration](./doc/libpg_query_analysis.en.md)
- [Release Notes](./RELEASE_NOTES.en.md)
- [Changelog](./CHANGELOG.en.md)

## Tests and Benchmarks

- test notes: [tests/README.en.md](./tests/README.en.md)
- benchmark notes: [bench/README.en.md](./bench/README.en.md)

## License

- project license: [LICENSE](./LICENSE)
- third-party notices: [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)
