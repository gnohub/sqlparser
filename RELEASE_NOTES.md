# v2.12.0 发布说明

`v2.12.0` 强化未改写 SQL 的无损 round-trip，并增加会话状态结构化遍历和 MERGE matched UPDATE assignment 改写。

## 主要变化

- handle generation 为 `0` 时，`sqlparser_deparse()` 成功返回的 SQL 与原始输入逐字节一致，完整保留标识符定界符和大小写、关键字、空白、换行、注释、分号及多语句边界。
- AST 中源自 SQL identifier token 的名称值保留源 token 的字母大小写；带引号标识符仍使用解码后的 AST 名称内容。
- Query Graph 新增 session action、item 和 value 只读访问，View JSON 同步输出可选 `query_graph.session`。
- session 投影覆盖受支持的数据库、schema、角色、身份、事务特征和会话参数语句，以及 identifier、keyword、literal、bind 和 expression value。
- 新增 `stmt[S].merge_assignment[W][A]` selector，支持读取和改写 `WHEN MATCHED ... THEN UPDATE` 赋值项。
- `update_assignment` selector API 支持 MERGE matched UPDATE 赋值项的读取、右值改写和同一 statement 内的右值克隆；三个 assignment patch 操作支持赋值项的插入、删除和整项替换。

## 兼容性

- 公共 API 采用追加式扩展；既有函数签名和公共结构体布局保持不变。
- 动态库 ABI 主版本保持为 `libsqlparser.so.0`；新增三个 session Query Graph 导出函数，ABI 导出检查包含 149 个公共符号。
- generation 大于 `0` 时，反解析从当前 handle 状态生成 SQL，整个输出不适用 generation-`0` 的逐字节一致性保证。

## 发布验证

- 九套方言用例矩阵对所有预期成功用例执行 generation-`0` 逐字节反解析检查和 AST 标识符拼写检查。
- 九套方言矩阵均包含 session 投影期望。
- 支持 MERGE 的方言覆盖 matched UPDATE assignment selector 的解析、读取、插入、删除、替换和右值克隆正向回归；核心 API 测试覆盖非法分支、索引越界、非法赋值片段和删除最后一个赋值项的失败原子性。
- GCC 8.3 严格发布/调试构建、全量测试、install smoke、149 符号 ABI、ASan、UBSan 和 Valgrind 全部通过。
- Windows VS 2022 x64/MSVC 19.39 清理后构建与全量测试通过。

内置 `libpg_query` 版本：`17-6.2.2`。
