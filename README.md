# sqlparser 快速开始

[English](./README.en.md)

`sqlparser` 是一个通用 SQL 解析、改写与反解析库，提供稳定的 C API，用于解析 SQL、读取结构信息、执行受控改写，并将结果重新生成 SQL。

解析内核基于仓库内固定版本的 `libpg_query`：

- tag: `17-6.2.2`
- commit: `7be1aed1f1f968a36cf541319f71e845850f0381`

## 功能概览

本版本提供以下能力：

- `sql -> handle`
- 语句类型与节点名称识别
- 表、名称原子、字面量的遍历与改写
- `INSERT`、`UPDATE`、`WHERE` 结构读取与精确改写，支持新增 WHERE 与追加条件
- `SELECT` 输出列表读取、替换、插入与删除
- `selector` 解析、格式化与定位
- 方言选项，默认 PostgreSQL，并提供 MySQL、Oracle、SQL Server、达梦方言转换层
- 常见预编译 / 参数化 SQL 语句解析、View JSON 和反解析
- 可配置资源限制，覆盖 SQL 输入、生成输出与语句数量
- View JSON 导出、C 结构化遍历与结构体 patch 写回
- `handle -> sql`

## 公共产物

- 头文件：`include/sqlparser/sqlparser.h`
- 静态库：`lib/libsqlparser.a`
- 动态库：`lib/libsqlparser.so`
- CLI：`bin/sqlparser_cli`
- Windows MSVC 静态库：`build\msvc\lib\sqlparser.lib`
- Windows MSVC CLI：`build\msvc\bin\sqlparser_cli.exe`

## 构建依赖

Linux:

- GCC 8.3 或更新版本，并支持 `gnu11`
- GNU Make
- `pkg-config`
- `jansson`

Windows:

- Visual Studio 2022 x64
- MSVC 19.39 或更新版本
- NMake

Windows 构建使用仓库内的 vendored Jansson，不需要额外安装 JSON 库。

## Linux 构建

```bash
make all
```

常用目标：

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

## Windows 构建

在 x64 Native Tools Command Prompt for VS 2022 中执行：

```bat
nmake /F Makefile.msvc test
```

常用目标：

- `nmake /F Makefile.msvc all`
- `nmake /F Makefile.msvc static`
- `nmake /F Makefile.msvc cli`
- `nmake /F Makefile.msvc examples`
- `nmake /F Makefile.msvc test`
- `nmake /F Makefile.msvc clean`

## 最小接入示例

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

指定非默认方言时使用 `sqlparser_parse_with_options()`：

```c
sqlparser_parse_options_t options;

sqlparser_parse_options_default(&options);
options.dialect = SQLPARSER_DIALECT_MYSQL;
```

Oracle、SQL Server 与达梦方言同样通过 `options.dialect` 显式指定：

```c
options.dialect = SQLPARSER_DIALECT_ORACLE;
options.dialect = SQLPARSER_DIALECT_SQLSERVER;
options.dialect = SQLPARSER_DIALECT_DAMENG;
```

示例编译方式：

```bash
gcc -std=gnu11 demo.c -I./include -L./lib -lsqlparser -ljansson -o demo
```

如果已经安装到系统目录，也可以通过 `pkg-config` 获取编译参数：

```bash
gcc -std=gnu11 demo.c $(pkg-config --cflags --libs sqlparser) -o demo
```

## 命令行工具

直接解析一条 SQL：

```bash
./bin/sqlparser_cli "SELECT id, name FROM public.users WHERE id = 42"
```

导出 View JSON：

```bash
./bin/sqlparser_cli --mode view "SELECT id, name FROM public.users WHERE id = 42"
```

批量处理 JSON 文件中的 SQL 列表：

```bash
./bin/sqlparser_cli \
  --batch-file ./sql_batch.json \
  --output ./sqlparser_batch_result.json
```

## 示例

示例程序位于 `examples/`：

- `examples/patch/`：推荐接入方式，统一使用 `sqlparser_apply_patch()`。
- `examples/convenience/`：便捷接口示例。
- `examples/inspect/`：结构读取和遍历示例。
- `examples/dialect/`：方言调用示例。

示例说明见 [examples/README.zh-CN.md](./examples/README.zh-CN.md)。

## 文档

- [文档目录](./doc/README.md)
- [项目概览与架构](./doc/sqlparser_architecture.md)
- [PostgreSQL 方言支持](./doc/postgresql_dialect_support.md)
- [MySQL 方言支持](./doc/mysql_dialect_support.md)
- [Oracle 方言支持](./doc/oracle_dialect_support.md)
- [SQL Server 方言支持](./doc/sqlserver_dialect_support.md)
- [达梦方言支持](./doc/dameng_dialect_support.md)
- [方言覆盖统计](./doc/dialect_coverage.md)
- [API 手册](./doc/api_reference.md)
- [View JSON 手册](./doc/view_json.md)
- [CLI 手册](./doc/cli_guide.md)
- [libpg_query 集成说明](./doc/libpg_query_analysis.md)
- [发布说明](./RELEASE_NOTES.md)
- [变更记录](./CHANGELOG.md)

## 测试与性能

- 测试说明见 [tests/README.md](./tests/README.md)
- 基准测试说明见 [bench/README.md](./bench/README.md)

## 许可证

- 项目许可证见 [LICENSE](./LICENSE)
- 第三方许可证说明见 [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)
