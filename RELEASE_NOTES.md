# v2.16.9 发布说明

Query Graph relation 新增 database、schema 和 database link 三个分段定界标志，DML column 新增目标列定界标志。每个标志仅依据对应名称段的精确来源 token；View JSON 仅在值为 `true` 时输出同名字段。

该状态覆盖普通 SELECT、INSERT、UPDATE、DELETE、MERGE，以及普通 INSERT、MERGE INSERT、`INSERT ALL/FIRST`、MySQL SET 形式和 SQL Server `OUTPUT ... INTO` sink 的目标列。Oracle、Dameng 与 Vastbase-Oracle 的 multi-table INSERT database-link 分支同时完整投影 object、link 及对应定界状态。分支 link 信息由方言状态内部持有并随 clone/destroy 深拷贝和释放，调用方所有权规则不变。

本版本新增公开结构体字段，但未新增公开导出符号。在 x86_64 与 AArch64 的 64 位布局中，相关结构体的旧字段 offset 和 `sizeof` 保持不变；该结论不适用于 32 位布局。新增 62 条 final case 和 103 个 patch 后，九套 fixture 合计 2,902 条 final case 和 9,266 个 patch。完整 `make test`、identifier 与 core API 定向测试、九套方言矩阵及相关 Valgrind 检查均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
