# v2.16.6 发布说明

Query Graph 新增 relation `alias_quoted_identifier` 和 target `output_quoted_identifier`，分别标识 alias 与 output name 是否使用标识符定界符；View JSON 仅在值为 `true` 时输出对应字段。显式 output alias 优先；没有显式 alias 且 output name 直接来自字段时继承字段定界状态。支持双引号、反引号和方括号形式，`U&` 前缀不单独计入。

本版本未新增公开导出符号、动态分配或资源所有权规则。受支持的 x86_64 与 AArch64 布局检查中，相关结构体既有成员 offset 与 `sizeof` 保持不变。新增 9 条 final case 和 18 个 patch 后，九套 fixture 合计 2,831 条 final case 和 9,136 个 patch。远端完整 `make test` 通过；ABI/export 检查保持 154 个公开符号，identifier 定向 Valgrind 检查为 `0 bytes in 0 blocks`、0 errors。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
