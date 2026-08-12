# v2.16.1 发布说明

`v2.16.1` 将 Query Graph 的 DML cell 内部记录由 456 B 压缩至 80 B。20,000 个 literal cell 的保留量由 14.659 MiB 降至 1.685 MiB，下降 88.504%。公开 API、View JSON 和 DML 语义保持不变。

相关回归、五项定向 Valgrind Memcheck 和 152 个公共符号的 ABI 检查均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
