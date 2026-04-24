# sqlparser 快速开始

`sqlparser` 是一个通用 SQL 解析、改写与反解析库，提供稳定的 C API，用于解析 SQL、读取结构信息、执行受控改写，并将结果重新生成 SQL。

当前解析内核基于仓库内固定版本的 `libpg_query`：

- tag: `17-6.2.2`
- commit: `7be1aed1f1f968a36cf541319f71e845850f0381`

## 功能概览

本版本提供以下能力：

- `sql -> handle`
- 语句类型与节点名称识别
- 表、名称原子、字面量的遍历与改写
- `INSERT`、`UPDATE`、`WHERE` 结构读取与精确改写
- `selector` 解析、格式化与定位
- `parse tree JSON` 导出
- `summary JSON` 导出
- 稳定模型 JSON 导出与导入
- `handle -> sql`

## 公共产物

- 头文件：`include/sqlparser/sqlparser.h`
- 静态库：`lib/libsqlparser.a`
- 动态库：`lib/libsqlparser.so`
- CLI：`bin/sqlparser_cli`

## 构建依赖

- Linux
- GCC 8.3 或更新版本，并支持 `gnu11`
- GNU Make
- `pkg-config`
- `jansson`

## 构建

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
- `make install PREFIX=/usr/local`

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

导出摘要 JSON：

```bash
./bin/sqlparser_cli --mode summary "SELECT id, name FROM public.users WHERE id = 42"
```

导出稳定模型 JSON：

```bash
./bin/sqlparser_cli --mode model "SELECT id, name FROM public.users WHERE id = 42"
```

批量处理 JSON 文件中的 SQL 列表：

```bash
./bin/sqlparser_cli \
  --batch-file ./tests/cases/sql_batch_input.json \
  --output /tmp/sqlparser_batch_result.json
```

## 示例

示例程序位于 `examples/`：

- `01_select_inspect.c`
- `02_insert_values_replace_literal.c`
- `03_insert_select_inspect.c`
- `04_update_replace_assignment.c`
- `05_delete_inspect.c`
- `06_ddl_inspect.c`
- `07_multi_statement_walk.c`
- `08_model_roundtrip.c`
- `09_expression_rewrite.c`

示例说明见 [examples/README.zh-CN.md](./examples/README.zh-CN.md)。

## 文档

- [文档目录](./doc/README.md)
- [项目概览与架构](./doc/sqlparser_architecture.md)
- [兼容性策略](./doc/compatibility_policy.md)
- [API 手册](./doc/api_reference.md)
- [模型 JSON 手册](./doc/model_json.md)
- [CLI 手册](./doc/cli_guide.md)
- [libpg_query 集成说明](./doc/libpg_query_analysis.md)
- [变更记录](./CHANGELOG.md)

## 测试与性能

- 测试说明见 [tests/README.md](./tests/README.md)
- 基准测试说明见 [bench/README.md](./bench/README.md)

## 许可证

- 项目许可证见 [LICENSE](./LICENSE)
- 第三方许可证说明见 [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)
