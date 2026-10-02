# 测试说明

## 全流程性能回归覆盖

`tools/sqlparser_pipeline_bench.c` 使用显式 MySQL 方言，每次重新解析 SQL、
构建 QueryGraph、遍历真实 selector 并构造 patch，一次提交整批修改，最后取得
新分配的 SQL。完整输出逐字节比较与对象销毁放在计时之外，销毁单独报告。
5,000 行固定样例及复现命令见
[`bench/README.md`](../bench/README.md)。

新增或扩展的回归程序：

- `test_protobuf_node`、`test_protobuf_fastpath`：精确描述符约束、通用与快速路径
  字节一致性、未知与重复 wire 字段、畸形输入、缓冲区编码、分配失败及确定性模糊测试
- `test_mysql_validation_observer`：存活树校验与解包回退一致性、解析上下文生命周期、
  解析与方言错误优先级、语句数限制，以及错误输出为 NULL 时的畸形 SQL
- `test_mysql_scanner_differential`：掩码、引号与注释、原文位置映射、语句边界、
  缺失关键词快速判定及可用 locale 下的行为
- `test_surface_scanner_differential`：逐位置比较各方言原文扫描，覆盖引号、
  注释、畸形输入、随机字节及 locale
- `test_insert_graph_fast_paths`：原生字面量与回退图构建、selector 边界、图扩容、
  关系绑定、改写与失败后失效清理
- `test_insert_string_batch`、`test_ascii_string_validation`：SQL 与类型化字符串
  替换、原文保留、片段限制及整批失败后失效清理
- `test_validation_arena`：序列化回退路径的分配失败清理
- `test_distinct_handle_concurrency`：13 个方言入口上的独立 handle 并发；
  `./bin/test_distinct_handle_concurrency 20` 覆盖 1,040 次小样例及 80 次大批量
  MySQL 生命周期，不表示同一个 handle 可并发修改

GNU 链接器构建启用 observer 和分配包装检查；不提供 pthreads 的 Windows
并发用例会明确跳过。性能数据属于诊断结果，不作为依赖机器速度的单元测试阈值。


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

## 内存检查

Linux AArch64、Valgrind 3.27.1 下执行了以下检查：

- 生命周期和借用输入：`test_patch_lifecycle`、`test_direct_wire_lifecycle`、`test_patch_graph_borrowed`。
- 方言状态和结构修改：`test_mutation_dialect_state`、`test_patch_structural_rows`、`test_patch_batch`。
- 编码、转换和分配失败：`test_protobuf_output_oom`、`test_protobuf_scalar_lifetime`、`test_parser_conversion_lifetime`、`test_validation_arena`、`test_mysql_validation_observer`、`test_protobuf_fastpath`。
- 线程与独立 handle：`test_pg_query_thread_lifecycle`、`test_distinct_handle_concurrency`（默认 2 轮）。
- 完整批量流程：`sqlparser_pipeline_bench 5000 1 0 literal mysql` 与 `sqlparser_pipeline_bench 5000 1 0 replace mysql`。

以上 16 项均为 0 错误，退出时 0 bytes in 0 blocks，未使用抑制规则。GNU 链接包装启用了生命周期、直接编码、输出分配和校验回退的故障注入；可选的额外 mutation/structural 分配扫描未单独启用。这不是完整测试套件逐项运行 Valgrind 的结果，也不代表进程峰值内存。

检查使用 `--leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --track-origins=yes --error-exitcode=99`，日志与正常计时分开。完整 Valgrind 套件入口仍为 `make verify-valgrind`。

## 原位 patch 生命周期

`test_patch_lifecycle` 在全部 13 个方言入口中，每个 handle 仅解析一次，连续进行
8 轮批量 apply/deparse。每次成功的非空批次都使 generation 增加一次，并使旧的
借用 View 失效，包括同值替换和先修改再还原；空列表保持 generation 和 View。
generation 大于零时，deparse 可以规范化原本未改变的 SQL。COPY 同值修改用例
改为精确检查 `FORMAT CSV, HEADER true`，保留表名和选项检查，不再要求输入的
关键词/布尔值大小写。

apply 或 deparse 失败后，handle 不可继续使用，调用者只需销毁它。测试仍检查
selector、无效片段和资源限制的具体错误码，并验证后续覆盖不能隐藏中间错误。
调用者输入缓冲区在成功和失败后都保持不变；已分配的独立 SQL 输出在后续修改、
失败和 handle 销毁后仍然有效。后项 patch 引用旧 handle 内的字符串时，必须在
前项修改使借用存储失效之前完成快照。

GNU 链接包装测试逐个注入分配失败，覆盖普通 apply、deparse、MySQL 快速字符串
批次和同一个 handle 上第二轮快速批次。失败必须进入可安全销毁的终止状态；
可选缓存分配失败后若成功回退，仍需逐字节核对完整 SQL。专门的序列化及输出
分配错误继续断言 `NO_MEMORY`；历史 protobuf 解包路径的通用分配错误可能报告
`INTERNAL_ERROR`。`test_patch_batch_counts` 在既有场景矩阵中还检查 apply 期间
整 handle 克隆次数为零。

```bash
make -j4 bin/test_patch_lifecycle bin/test_patch_batch_counts SHOW_WARNING=0
./bin/test_patch_lifecycle
./bin/test_patch_batch_counts
```

## 字符串方言输出与改写回归

`tests/unit/test_string_literal_surface.c` 通过库 API 验证字符串值、整句 SQL、表达式片段和来源复制，自动纳入 `make test`。共 2,367 组组合，覆盖 13 个方言入口；失败返回非零。

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
- 非 PostgreSQL 入口另覆盖 `N'…'` 的读取、替换和复制。批量用例检查“替换 → 复制 → 再替换 → 再复制”的按序取值；失败用例保留末项 selector 越界错误码断言，并检查 handle 已失效、拒绝再次使用且可以安全销毁。
- `typed-string-boundary` 的 91 组组合检查控制字节、Unicode、非 UTF-8 字节及其与引号/反斜杠的组合，保留原有字节行为；同时检查空字符串指针和无效 SQL 片段不能被后续替换掩盖。
- 常规字符串矩阵的期望 SQL 独立列出，不调用被测渲染器生成。每组先解析期望 SQL 并检查其字符串值，再执行改写；结果检查整句和片段的精确文本、语义值、generation、完整 View，以及输出重解析后的值和 View。

输出按入口对应的语法族验收：MySQL 系使用其反斜杠转义规则；Oracle、达梦和 SQL Server 系使用普通或 national 字符串形式。Vastbase/Kingbase 兼容入口检查对应语法族的库输出约定，不验证服务端对额外语法的支持。PostgreSQL 系接受普通字符串和语义等价的 `E'…'` 形式。

语法依据：[PostgreSQL 字符串常量](https://www.postgresql.org/docs/16/sql-syntax-lexical.html#SQL-SYNTAX-STRINGS-ESCAPE)、[MySQL 字符串字面量](https://dev.mysql.com/doc/refman/8.0/en/string-literals.html)、[Oracle 字面量](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/Literals.html)、[SQL Server 常量](https://learn.microsoft.com/en-us/sql/t-sql/data-types/constants-transact-sql)。

## 批量 patch 回归与性能回放

`test_patch_batch` 验证批内顺序取值、重复修改、插删索引、混合操作、bind、注释、资源限制和失败后失效清理。性能模式将全部修改放入一个 patch list，只调用一次 `sqlparser_apply_patch()`；分别记录 parse、apply、deparse 耗时，并核对结果值。性能数据不作为依赖机器速度的测试阈值。

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

默认运行包括 267 组路径正向检查及其 267 组失败后失效清理检查、83 组依赖边界正向检查和 86 组失败清理检查，以及 85 组原位 AST 批处理边界正向检查和 36 组失败清理检查。覆盖 13 个方言入口；`MERGE` 仅用于支持它的 10 个入口，`INSERT ALL/FIRST` 仅用于 Oracle、Vastbase Oracle、KingbaseES Oracle 和达梦入口。检查完整 View、输出重解析后的 View、generation、来源读取顺序、bind 生命周期、表达式/参数索引移动、注释以及中间修改超限后的失败清理。重复替换额外检查已引入的注释保留，以及中间片段错误不能被后续替换掩盖。伪列检查引入表达式后的编号变化。

另含 39 组节点查找正向检查及其 39 组失败清理检查，可通过 `--lookup-boundaries` 单独运行。覆盖括号、CAST、嵌套函数和子查询中的 WHERE 字面量编号、赋值混合修改、乱序及重复替换、跨语句编号与失败后失效清理。

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
| 普通批量插入 | `insert_rows`、`insert_rows_quoted`：每行两列，只替换第二列 |
| MERGE UPDATE | `merge_assignment`：匹配分支的赋值字面量替换 |
| 来源复制 | `insert_copy`：连续读取未修改的源值 |
| 函数及表达式 | `expr_raw`、`expr_float`、`expr_bind`、`expr_field`、`expr_whole`、`expr_insert`、`expr_delete`、`expr_repeat` |
| 混合修改 | `mixed_update_expr`：交替替换 UPDATE 赋值和函数参数字面量 |
| 多分支插入 | `multi_all`、`multi_first`、`multi_raw`、`multi_float`、`multi_bind`、`multi_copy`、`multi_comment_all`、`multi_comment_first` |

基线使用 `v2.16.15`（`05590bb5994200efb6f12bfb72a3e35c0f338f75`）的原始生产代码，采集日期为 2026-09-23，环境为 Linux x86_64 / KVM、Intel Xeon Platinum 8168、GCC 4.8.5，编译参数为 `-std=gnu11 -fPIC -O2 -w -pthread`，静态链接 `libsqlparser.a`：

- [计时基线](../bench/baselines/patch_batch_v2.16.15.csv)：45 个参数组合，每组 3 次独立进程运行，共 135 条原始记录；`arguments` 列可直接用于复跑。
- [调用次数基线](../bench/baselines/patch_batch_v2.16.15_counts.csv)：13 个方言入口的 237 组小规模路径检查，每组一次 API 调用、6 项 patch。

正常测试二进制用于计时；可选的 `_counts` 二进制通过链接包装统计 `sqlparser_apply_patch()` 期间的整句解析、handle clone、deparse、两种 AST 提交入口和共享 surface visitor 调用，不修改生产源码。整句解析次数不包括初始解析、结果校验和局部表达式片段解析；clone/deparse 调用数不代表全部内存复制或 AST 序列化次数。`ast_commits` 与 `state_commits` 分别记录跨目标文件调用的提交入口，不等同于实际整树校验次数；`surface_visits` 和 `surface_root_visits` 不包含 SQL Server 的独立 walker。CSV 中的调用次数来自单独的计数运行，不取计数运行的耗时。

时间单位为秒。固定样例另列查询图读取及 patch 构造耗时；生成式场景的 SQL 和 patch 准备不在计时阶段内，对应 `construct_seconds` 留空。`max_rss_kib` 是 GNU time 采集的整个样例进程峰值 RSS，包含校验，不是 apply 独占内存或泄漏指标。功能检查没有硬编码耗时阈值或旧版重解析次数；优化后的性能验收应使用相同参数对比基线，并保留存在来源依赖、索引变化或重叠修改时必要的同步。

2.16.16 的优化实现已通过完整 `make test`、ABI 检查及普通版和计数版的定向测试；批量 patch 回归的 Valgrind 检查为 0 errors、退出时 0 bytes in 0 blocks。

批量原文编辑复用现有编辑列表；读取前序结果、改变表达式/参数编号或涉及原文边界时按需同步。库渲染的 typed STRING 保留输入上限检查，不再为片段校验建立临时 AST；原始 SQL 片段及其它类型保留原有校验路径。原有输入和输出上限继续生效。多分支插入的位置扫描使用批次内栈上游标，SQL 或方言状态变化后清空，不增加常驻 SQL/AST 缓存。

### 原位 AST 批处理回归

`insert_rows` 精确生成 500 行时，每行包含 `ID` 和 `SECRET_VALUE`，将 `small-secret-NNNN` 替换为 `other-secret-NNNN`。`insert_rows_quoted` 使用对应方言的表名和列名定界符。两种场景都检查完整 View 和逐字节 deparse，并可通过最后一个参数只修改前缀行，验证其它行未变。

多行场景覆盖 PostgreSQL、MySQL、SQL Server、达梦及对应兼容入口，共 10 个入口；Oracle、Vastbase Oracle 和 Kingbase Oracle 使用已有单行多列 `insert_cell` 场景。`merge_assignment` 覆盖非 MySQL 的 10 个入口。默认路径矩阵使用 6 项修改，500 项用以下命令专项回放：

```bash
./bin/test_patch_batch --profile insert_rows mysql 500 500
./bin/test_patch_batch --profile insert_rows mysql 5000 5000
./bin/test_patch_batch_counts --profile insert_rows mysql 500 500
./bin/test_patch_batch --profile insert_rows_quoted mysql 500 125
./bin/test_patch_batch --profile insert_cell oracle 500 500
./bin/test_patch_batch --profile update_literal mysql 500 500
./bin/test_patch_batch --profile where_literal mysql 500 500
./bin/test_patch_batch --profile select_target mysql 500 500
./bin/test_patch_batch --profile merge_cell oracle 500 500
./bin/test_patch_batch --profile merge_assignment oracle 500 500
```

原位批处理边界用例检查定界符、单引号/反斜杠、未修改的 bind 和 national 字符串、重复修改及来源读取顺序、多行之间的来源复制、非法 selector 失败清理，以及中间无效的层次表达式不能被后续覆盖掩盖。已有混合操作、资源限制、INSERT ALL/FIRST 和函数参数场景继续作为回归对照。

额外边界检查 national 字符串跨类型替换、同单元格覆盖后复制、函数参数变成 bind 后的 literal 编号，以及原位替换允许的超限中间值恢复行为。多行生成器支持最多 5,000 行，不修改库的资源上限。

可合并的字符串替换复用批量原文编辑，批末一次重建解析状态；来源依赖、类型或结构变化时按需同步。普通 VALUES 复用栈上游标保存语句边界，顺序修改不再遍历全部已有编辑；不增加公开字段或常驻 AST 缓存。

2.16.17 基线下，500 行 MySQL 场景的单次普通版 apply 实测约 1.77 秒；独立计数运行记录 500 次 AST 提交、2,000 次共享 surface 遍历、0 次批内整句重解析。该数据用于后续同机同参数比较，不作为固定耗时或内部调用次数的通过条件。

2.16.18 的同机对比中，500 行场景 apply 约 25 毫秒，5,000 行场景约 269 毫秒；每个批次仅调用一次 patch API，结果完成完整 View 和逐字节 SQL 对账。5,000 行批次记录 1 次整句重解析、0 次逐项 AST 提交和共享 surface 遍历。耗时不包含初始解析、View 导出和 deparse。

### 2.16.19 性能回归

UPDATE 和 WHERE 的字符串批量替换复用已定位的节点，避免通过全局编号重复查找；节点仅在当前解析状态内使用，重建后重新定位，不增加公开字段或常驻缓存。

INSERT 批量字符串替换直接读取已有节点类型，避免仅为判断类型而构造字面量视图。库生成的字符串片段保留输入上限检查，不再为片段校验构建临时 AST；公共字面量读取、quoted 标志和批末整句解析不变。

MySQL、Vastbase MySQL 和 KingbaseES MySQL 的关键词预检查合并为一次原文扫描，保留原有语法判定与报错顺序。CREATE 和 DML 预处理按已知语句类型跳过不适用流程；可保守确定为单语句时，在分段扫描前跳过整个流程。WITH、未知前缀及复杂边界保留原路径，ON DUPLICATE 判断不变。

同机、同参数的普通版单次 apply 实测如下，均通过完整 View 和 SQL 结果检查；耗时不包含初始解析、View 导出和 deparse，不作为固定性能阈值：

| MySQL 场景 | patch 数 | 2.16.18 | 2.16.19 |
| --- | ---: | ---: | ---: |
| UPDATE 赋值替换 | 500 | 283.439 ms | 8.106 ms |
| WHERE 字面量替换 | 500 | 1,020.311 ms | 22.150 ms |
| INSERT 逐行替换 | 5,000 | 269.921 ms | 105.435 ms |

`test_dialect_surface_state --mysql-guard` 覆盖 3 个 MySQL 入口，共 9 组正向和 24 组拒绝检查，检查关键词保护、词边界、大小写、后段语句及嵌套语句。`--mysql-dispatch` 的 21 组检查覆盖多语句空段、普通和可执行注释、尾部空白、REPLACE SET、WITH UPDATE/DELETE、后段 CREATE 扩展及字符串内分号；核对精确 SQL、完整 View、relation 定界标志和 selector 编号。两组用例在优化前后均通过。

批量 patch、字符串输出、surface 状态及 3 个 MySQL 用例矩阵的相关回归通过。5,000 行 INSERT 和 MySQL 语句分派用例的 Valgrind 检查均为 0 errors、退出时 0 bytes in 0 blocks。

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
- 参数校验、资源限制、畸形 SQL、失败改写失败清理和方言公共输出稳定性

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
