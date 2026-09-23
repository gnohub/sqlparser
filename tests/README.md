# 测试说明

`tests/` 目录用于记录和验证 `sqlparser` 的功能覆盖。

## 目录结构

- `tests/unit/`
  单元测试与接口回归测试。
- `tests/cases/`
  记录具名 SQL 用例、批量夹具和补充说明。

## 测试组成

测试由以下部分组成：

- 单元测试
- 批量 SQL 夹具验证
- 示例程序烟测
- 安装态 API 烟测
- 严格编译与 sanitizer 门禁
- `valgrind` 泄漏校验
- 长时间循环回归
- 稳定性与异常输入回归

## 执行方式

```bash
make test
```

`make test` 会同时执行：

- 单元测试程序
- CLI 批量夹具检查
- `examples/` 下的示例程序

常用质量门禁入口：

- `make test-parse`
- `make test-inspect`
- `make test-rewrite`
- `make test-deparse`
- `make test-view-json`
- `make test-cli`
- `make test-install`
- `make test-abi`
- `make verify-release`
- `make verify-debug`
- `make verify-asan`
- `make verify-ubsan`
- `make verify-valgrind`
- `make test-loop LOOP=50`
- `make verify`

## 字符串方言输出与改写回归

`tests/unit/test_string_literal_surface.c` 通过库 API 验证字符串值、整句 SQL、表达式片段和来源复制，自动纳入 `make test`。共 2,276 组组合，覆盖 13 个方言入口；失败返回非零。

```bash
make bin/test_string_literal_surface
./bin/test_string_literal_surface
./bin/test_string_literal_surface sqlserver set-target
./bin/test_string_literal_surface mysql patch-literal
./bin/test_string_literal_surface oracle copy-target
./bin/test_string_literal_surface postgresql
```

- 10 组字符串覆盖普通文本、单个/连续/末尾反斜杠、路径、单引号与反斜杠的两种顺序、Unicode、字面上的 `\n`/`\t`/`\r`，以及字符串内容中的 `E'…'`。
- SELECT 覆盖单 target、selector、target list、patch `sql`/`literal`、直接 literal setter、只读片段和 `source_selector` 复制；同时检查 INSERT cell、UPDATE assignment、WHERE literal、函数参数及注释/定界别名保护。
- 非 PostgreSQL 入口另覆盖 `N'…'` 的读取、替换和复制。批量用例检查“替换 → 复制 → 再替换 → 再复制”的按序取值；回滚用例校验末项 selector 越界错误码，以及 SQL、完整 View 和 generation 保持不变。
- 期望字符串 SQL 独立列出，不调用被测渲染器生成。每组先解析期望 SQL 并检查其字符串值，再执行改写；结果检查整句和片段的精确文本、语义值、generation、完整 View，以及输出重解析后的值和 View。

输出按入口对应的语法族验收：MySQL 系使用其反斜杠转义规则；Oracle、达梦和 SQL Server 系使用普通或 national 字符串形式。Vastbase/Kingbase 兼容入口检查对应语法族的库输出约定，不验证服务端对额外语法的支持。PostgreSQL 系接受普通字符串和语义等价的 `E'…'` 形式。

语法依据：[PostgreSQL 字符串常量](https://www.postgresql.org/docs/16/sql-syntax-lexical.html#SQL-SYNTAX-STRINGS-ESCAPE)、[MySQL 字符串字面量](https://dev.mysql.com/doc/refman/8.0/en/string-literals.html)、[Oracle 字面量](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/Literals.html)、[SQL Server 常量](https://learn.microsoft.com/en-us/sql/t-sql/data-types/constants-transact-sql)。

## 批量 patch 回归与性能回放

`test_patch_batch` 验证批内顺序取值、重复修改、插删索引、混合操作、bind、注释、资源限制和失败回滚。性能模式将全部修改放入一个 patch list，只调用一次 `sqlparser_apply_patch()`；分别记录 parse、apply、deparse 耗时，并核对结果值。性能数据不作为依赖机器速度的测试阈值。

```bash
make bin/test_patch_batch
./bin/test_patch_batch
./bin/test_patch_batch --bench oracle 50
./bin/test_patch_batch --bench update 500
./bin/test_patch_batch --bench update-copy 250
./bin/test_patch_batch --bench expression 500
./bin/test_patch_batch --bench update 500 50
./bin/test_patch_batch --bench expression 500 50
```

`oracle 50` 为 50 分支、每行 16 列的 `INSERT ALL`，包含 250 次原值复制插列和 250 次模拟密文替换，输出逐分支核对 21 列和值。`update-copy 250` 对 250 项赋值分别追加备份并替换，共 500 个 patch。`update` 和 `expression` 的可选最后一个参数只改变 patch 数，保持输入 SQL 大小不变。

### 批处理路径基线

默认运行还包括 237 组路径正向检查及其 237 组失败回滚检查、83 组依赖边界正向检查和 86 组回滚检查。覆盖 13 个方言入口；`MERGE` 仅用于支持它的 10 个入口，`INSERT ALL/FIRST` 仅用于 Oracle、Vastbase Oracle、KingbaseES Oracle 和达梦入口。检查完整 View、输出重解析后的 View、generation、来源读取顺序、bind 生命周期、表达式/参数索引移动、注释以及中间修改超限后的回滚。重复替换额外检查已引入的注释保留，以及中间片段错误不能被后续替换掩盖。伪列检查引入表达式后的编号变化。

`patch_batch_oracle_insert_all.sql` 固定样例去掉末尾换行后为 27,530 字节，包含 50 个分支、每分支 16 列。`--fixture` 从查询图读取第 2～6 列的字符串值，通过 `sqlparser_selector_format()` 构造 250 项替换；一次提交所选前缀，核对全部 800 个值和列名，并比较修改后的 View 与输出重解析后的 View。默认测试执行完整 250 项替换。

```bash
make -j4 bin/test_patch_batch bin/test_patch_batch_counts SHOW_WARNING=0
./bin/test_patch_batch
./bin/test_patch_batch_counts --profile-all
./bin/test_patch_batch --fixture tests/cases/patch_batch_oracle_insert_all.sql 250
./bin/test_patch_batch --profile insert_copy postgresql 250 50
./bin/test_patch_batch --profile expr_raw postgresql 100 100
./bin/test_patch_batch --profile mixed_update_expr oracle 200 200
/usr/bin/time -f max_rss_kib=%M ./bin/test_patch_batch --profile multi_comment_all dameng 50 50
```

`--profile 场景 方言 size [patch_count]` 中，`size` 固定候选修改点及输入规模，`patch_count` 只控制实际提交的前缀，默认等于 `size`；混合场景的 `size` 必须为偶数。场景包括：

| 分组 | 场景 |
| --- | --- |
| 对照 | `insert_cell`、`update_literal`、`where_literal`、`select_target`、`merge_cell`、`expr_literal` |
| 来源复制 | `insert_copy`：连续读取未修改的源值 |
| 函数及表达式 | `expr_raw`、`expr_float`、`expr_bind`、`expr_field`、`expr_whole`、`expr_insert`、`expr_delete`、`expr_repeat` |
| 混合修改 | `mixed_update_expr`：交替替换 UPDATE 赋值和函数参数字面量 |
| 多分支插入 | `multi_all`、`multi_first`、`multi_raw`、`multi_float`、`multi_bind`、`multi_copy`、`multi_comment_all`、`multi_comment_first` |

基线使用 `v2.16.15`（`05590bb5994200efb6f12bfb72a3e35c0f338f75`）的原始生产代码，采集日期为 2026-09-23，环境为 Linux x86_64 / KVM、Intel Xeon Platinum 8168、GCC 4.8.5，编译参数为 `-std=gnu11 -fPIC -O2 -w -pthread`，静态链接 `libsqlparser.a`：

- [计时基线](../bench/baselines/patch_batch_v2.16.15.csv)：45 个参数组合，每组 3 次独立进程运行，共 135 条原始记录；`arguments` 列可直接用于复跑。
- [调用次数基线](../bench/baselines/patch_batch_v2.16.15_counts.csv)：13 个方言入口的 237 组小规模路径检查，每组一次 API 调用、6 项 patch。

正常测试二进制用于计时；可选的 `_counts` 二进制仅通过链接包装统计 `sqlparser_apply_patch()` 期间的 `sqlparser_parse_with_options()`、handle clone 和 `sqlparser_deparse()` 调用，不修改生产源码。整句解析次数不包括初始解析、结果校验和局部表达式片段解析；clone/deparse 调用数不代表全部内存复制或 AST 序列化次数。CSV 中的调用次数来自单独的计数运行，不取计数运行的耗时。

时间单位为秒。固定样例另列查询图读取及 patch 构造耗时；生成式场景的 SQL 和 patch 准备不在计时阶段内，对应 `construct_seconds` 留空。`max_rss_kib` 是 GNU time 采集的整个样例进程峰值 RSS，包含校验，不是 apply 独占内存或泄漏指标。功能检查没有硬编码耗时阈值或旧版重解析次数；优化后的性能验收应使用相同参数对比基线，并保留存在来源依赖、索引变化或重叠修改时必要的同步。

优化后的实现已通过完整 `make test`、ABI 检查及普通版和计数版的定向测试；批量 patch 回归的 Valgrind 检查为 0 errors、退出时 0 bytes in 0 blocks。

批量原文编辑复用现有编辑列表；读取前序结果、改变表达式/参数编号或涉及原文边界时按需同步。标准字符串与十进制数值仍进行片段解析校验，但不为校验复制整份方言状态；原有输入和输出上限继续生效。多分支插入的位置扫描使用批次内栈上游标，SQL 或方言状态变化后清空，不增加常驻 SQL/AST 缓存。

## 用例文件

常用测试文件包括：

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_vastbase_oracle_dialect_case_matrix.c`
- `tests/unit/test_vastbase_mysql_dialect_case_matrix.c`
- `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c`
- `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_kingbase_oracle_dialect_case_matrix.c`
- `tests/unit/test_kingbase_mysql_dialect_case_matrix.c`
- `tests/unit/test_kingbase_postgresql_dialect_case_matrix.c`
- `tests/unit/test_kingbase_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_robustness.c`
- `tests/unit/test_stability.c`
- `tests/install/install_smoke.c`
- `tests/cases/sql_batch_input.json`
- `tests/cases/mysql_dialect_input.json`
- `tests/cases/oracle_dialect_input.json`
- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/dameng_dialect_input.json`
- `tests/cases/vastbase_oracle_dialect_input.json`
- `tests/cases/vastbase_mysql_dialect_input.json`
- `tests/cases/vastbase_postgresql_dialect_input.json`
- `tests/cases/vastbase_sqlserver_dialect_input.json`
- `tests/cases/kingbase_oracle_dialect_input.json`
- `tests/cases/kingbase_mysql_dialect_input.json`
- `tests/cases/kingbase_postgresql_dialect_input.json`
- `tests/cases/kingbase_sqlserver_dialect_input.json`
- `tests/verify_cli_batch.py`

## 覆盖范围

测试覆盖的主要内容包括：

- parse / deparse 基础链路
- 资源限制，包括 SQL 输入、生成输出和语句数量
- 语句类型与节点识别
- `SELECT / INSERT / UPDATE / DELETE / MERGE`
- 多语句输入
- `ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`
- 常见 DDL、事务控制、`GRANT / REVOKE` 与维护语句
- JSON 导出
- selector 与结构体 patch 回放
- `SELECT` 输出列表替换、插入、删除和改写后二次解析校验
- 结构化 SQL 片段改写，包括克隆 UPDATE assignment 右值插入新赋值项，以及用结构化列列表替换 SELECT 输出项
- `WHERE` 条件新增、替换、AND/OR 追加和改写后二次解析校验，覆盖 `SELECT`、`UPDATE`、`DELETE`、`INSERT ... SELECT`、`ON CONFLICT`、`VIEW`、`INDEX`、`COPY FROM`、`CREATE RULE`、`CREATE PUBLICATION` 和排他约束
- MySQL 方言转换层的解析、反解析和明确不支持语法返回码
- Oracle 方言转换层的解析、反解析和明确不支持语法返回码
- SQL Server 方言转换层的解析、反解析和明确不支持语法返回码
- 达梦方言转换层的解析、反解析和明确不支持语法返回码
- Vastbase 四个显式兼容模式的解析、反解析和明确不支持语法返回码
- KingbaseES 四个显式兼容入口的解析、反解析和 patch 回放；每种模式只保留统一入口，不按 V8/V9 分派
- 公共 API 空指针、越界访问、错误 selector、错误 patch、畸形输入和重复解析的抗崩溃回归
- 参数校验、资源限制、畸形 SQL、失败改写回滚和方言公共输出稳定性

## 用例矩阵

- [SQL 用例矩阵](./cases/sql_case_matrix.md)
- [MySQL 方言用例矩阵](./cases/mysql_dialect_matrix.md)
- [Oracle 方言用例矩阵](./cases/oracle_dialect_matrix.md)
- [SQL Server 方言用例矩阵](./cases/sqlserver_dialect_matrix.md)
- [达梦方言用例矩阵](./cases/dameng_dialect_matrix.md)
- [Vastbase Oracle 兼容模式用例矩阵](./cases/vastbase_oracle_dialect_matrix.md)
- [Vastbase MySQL 兼容模式用例矩阵](./cases/vastbase_mysql_dialect_matrix.md)
- [Vastbase PostgreSQL 兼容模式用例矩阵](./cases/vastbase_postgresql_dialect_matrix.md)
- [Vastbase SQL Server 兼容模式用例矩阵](./cases/vastbase_sqlserver_dialect_matrix.md)
- [KingbaseES Oracle 兼容入口用例矩阵](./cases/kingbase_oracle_dialect_matrix.md)
- [KingbaseES MySQL 兼容入口用例矩阵](./cases/kingbase_mysql_dialect_matrix.md)
- [KingbaseES PostgreSQL 兼容入口用例矩阵](./cases/kingbase_postgresql_dialect_matrix.md)
- [KingbaseES SQL Server 兼容入口用例矩阵](./cases/kingbase_sqlserver_dialect_matrix.md)
