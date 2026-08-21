# v2.16.8 发布说明

`SQLPARSER_PATCH_INSERT_COLUMN` 对 MERGE INSERT 支持 name-only、value-only 和 name + value 三种载荷，可分别增加目标列、增加 VALUES cell，或在同一位置成对增加；既有目标列和 VALUES cell 可继续分别替换。省略目标列清单但具有 VALUES 的分支现在输出既有 `target_list_selector`，可物化目标列清单或保持清单省略并单独增加 cell。

同一 patch batch 中允许列和值暂时不等长；本批次触及且最终具有显式目标列清单的分支在提交前校验等宽，失败时整批原子回滚。删除仍保持列值成对，`MERGE INSERT DEFAULT VALUES` 不适用三态插入。合同覆盖九个项目方言入口中成功解析的 MERGE，不表示对应数据库服务端均原生提供该语法。

本版本未新增公开 API、枚举、结构体字段或 View JSON 字段，仅扩展既有 selector 的输出范围。新增 9 条 final case 和 27 个 patch 后，九套 fixture 合计 2,840 条 final case 和 9,163 个 patch。远端完整 `make test`、定向核心 API、九套方言矩阵及 Valgrind 均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
