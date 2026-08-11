# v2.16.0 发布说明

`v2.16.0` 收紧 fallback Deparser、Patch 成功路径和 Query Graph 缓存的内存生命周期，降低多投影 SQL 的瞬时分配量与返回时保留量，不改变解析、反解析、Patch 和公共 Query Graph 语义。

## Fallback Deparser

- 在未启用 pretty-print 和 commas-start-of-line 的 non-pretty fallback 反解析路径中，不再为逗号创建 `DeparseStatePart`，而是直接写入与原合并结果相同的 `", "`；两条格式化分支保持不变。

Linux x86_64 上的 allocator payload 实测如下：

| 投影数 | 修改前累计请求量 | 修改后累计请求量 | 修改前峰值存活量 | 修改后峰值存活量 | 修改后返回时保留量 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 30,970 B | 6,394 B | 24,640 B | 5,310 B | 128 B |
| 256 | 610,810 B | 152,058 B | 516,160 B | 117,208 B | 2,112 B |

16 和 256 个投影的 non-pretty 输出与修改前逐字节一致，且均可重新解析。

## Patch AST 生命周期

- Patch 仍在 candidate handle 上执行，保留批量操作的失败原子性。全部 patch item 成功且确认非 no-op 后，在 candidate 移交给原 handle 之前释放已解包 AST。
- packed tree、surface SQL、generation、持久化 identifier/dialect 语义和重绑定规则保持不变。释放 AST 时同时解绑 AST 关联的内部状态；后续调用 AST 或 Query Graph accessor 时，依旧从当前 packed tree 惰性重建并重绑。
- control condition 渲染在读取 AST 前先获取当前 statement node，确保 patch 后惰性 AST 已重建。fallback deparse 只在方言后处理需要 AST 且当前 AST 不存在时重建并绑定；为此重建的 AST 在所有成功和失败出口都会释放，保持 patch 返回时较低的保留量。
- 失败和语义 no-op 路径不清理原 handle 已发布的 AST/Graph view，SQL、generation、packed tree 与方言状态均保持不变。

Linux x86_64 上的 allocator payload 实测如下：

| 场景 | 修改前 peak | 修改后 peak | 修改前 retained | 修改后 retained |
| --- | ---: | ---: | ---: | ---: |
| 单项 replace | 3,284 B | 3,284 B | 1,700 B | 364 B |
| 同一 handle 连续 4 次 apply | 4,807 B | 3,471 B | 1,505 B | 169 B |

连续四次 apply 后的保留量没有逐次增长。直接 `sqlparser_apply_patch()` 与统一进入该事务链的便捷改写接口共享该生命周期。

## Query Graph 紧凑缓存

- 公开 `sqlparser_graph_target_t` 和 `sqlparser_graph_value_t` 保持不变。缓存内部使用私有紧凑记录，公共 accessor 在读取时还原 index、statement、selector 和完整 value 内容。
- Linux LP64 下 target 记录由 224 B 降至 104 B，value 记录由公共布局的 800 B 降至 88 B。literal 与 bind 共用联合载荷，可推导的 index、statement 和 selector 字段不再重复存储。
- 删除 target/value 缓存无调用方的空记录构造路径。意外的空 target/value source 或空 target identifier 现在明确返回 `SQLPARSER_STATUS_INTERNAL_ERROR`。这些检查仅处理内部一致性错误，公开 API 布局和成功路径行为不变。
- bind 及 escape bind 文本由 Graph cache 唯一拥有的连续 NUL 结尾文本池保存，内部记录只保存相对 offset。`LIKE ... ESCAPE` 改为按 value index 排序的 56 B 稀疏侧数组，没有 escape 的 value 不承担固定记录。
- target、value、block 和普通 index pool 初始容量从 16 收紧为 4，selector 构建缓存从 8 起步。全部 statement 构建完成后，target、value、value text、LIKE ESCAPE 和 index pool 各执行最多一次 best-effort 收缩；收缩分配失败不影响已成功的 Graph。

`SELECT 1,2,3,4` 的 Query Graph cache 请求 payload 由 18,400 B 降至 1,872 B。相同测量口径下，选定投影规模的完整 Query Graph cache 最终实测如下：

| 投影数 | v2.16.0 请求量 |
| ---: | ---: |
| 100 | 20.66 KiB |
| 129 | 26.30 KiB |
| 200 | 40.19 KiB |
| 256 | 51.12 KiB |

## API 与兼容性

- 本版本不新增或删除公开函数、公开枚举、公开结构体字段或资源所有权规则。View JSON schema、selector 输出和 Patch 调用方式不变。
- 动态库仍导出 152 个公共符号，SONAME 保持 `libsqlparser.so.0`。C 调用方无需因本次缓存内部调整修改源码。

## 验证

- 严格构建通过。全量 `make test` 的九组 case matrix 共完成 2,781/2,781 条 case 和 9,034/9,034 个 patch，unit、example 和 CLI 目标全部通过。
- Deparser 定向回归覆盖 16/256 个投影的 non-pretty 输出、pretty-print 和 commas-start-of-line；修改前后输出逐字节一致且可重新解析。
- Patch 定向回归覆盖连续成功、故障注入下的回滚、语义 no-op、便捷入口、AST/Graph 重新获取和分页 fallback；SQL、packed tree、generation 与派生缓存相关断言全部通过。
- Query Graph 定向回归覆盖公开 target/value accessor、全部可达 value 类型、`LIKE ... ESCAPE`、多 statement/嵌套 block、长 bind 文本、分配失败重试与收缩失败。受影响 View JSON 与修改前逐字节一致。
- 联合生命周期验证在配置的超时时间内完成 24 条完整链路：串行 8 条，以及 4 个独立 handle 线程各 4 条。每条链路均执行 AST/Graph 获取、成功 Patch、旧 Graph generation 失效、AST/Graph 重建、fallback deparse 和重新解析后的公开 Graph 语义核对。Memcheck 为 23,129 次分配/23,129 次释放、`0 bytes in 0 blocks`、`ERROR SUMMARY: 0`；Helgrind 为 0 errors，无超时或死锁。
- 核心 API Memcheck 为 1,098,789 次分配/1,098,789 次释放、`0 bytes in 0 blocks`、0 errors；SQL Server surface 生命周期 Memcheck 为 130,586 次分配/130,586 次释放、`0 bytes in 0 blocks`、0 errors。
- 完整 `make verify-valgrind` 通过，全部 unit、方言矩阵、example、CLI batch 和 install smoke 均为 `0 bytes in 0 blocks`、`ERROR SUMMARY: 0`。ABI 检查保持 152 个公共符号，install smoke 确认版本文本和 `sqlparser_version_string()` 均为 `2.16.0`。

内置 `libpg_query` 标签：`17-6.2.2`。

内置 Jansson 版本：`2.15`。
