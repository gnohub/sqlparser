# v2.10.0 发布说明

`v2.10.0` 增加 SQL Server DML `OUTPUT`、嵌套 DML 和 `IF...ELSE` 控制流的解析、结构化遍历与改写能力，并同步支持 Vastbase-SQLServer 兼容模式。

## 主要变化

- 支持显式或省略 `INTO` 的 SQL Server `INSERT`。
- 支持 `INSERT`、`UPDATE`、`DELETE`、`MERGE` 的 `OUTPUT` 和 `OUTPUT ... INTO`。
- Query Graph 可表达 client/sink 结果通道、`INSERTED` / `DELETED` / 来源字段及嵌套 DML 父子关系。
- 增加 DML 结果 target、sink relation 和 sink column selector，可通过统一 patch 接口改写。
- 支持单语句、多语句和嵌套形式的 `IF...ELSE`，控制条件和分支 SQL 均可通过公共 C 结构遍历。
- SQL Server 与 Vastbase-SQLServer 方言用例矩阵分别包含 546 条用例，其中 517 条为支持路径，29 条为错误或明确不支持路径。

## 兼容性

- 新增 API 和枚举值均为追加式扩展。
- 既有公共函数签名和公共结构体布局保持不变。
- View JSON 仅在对应语句中增加可选的 `control_flow`、DML `result_channels` 和 `children`。
- 动态库 ABI 主版本保持为 `libsqlparser.so.0`，ABI 导出检查包含 146 个公共符号。

## 发布验证

- GCC 8.3 严格编译与全量测试
- ASan、UBSan 与 Valgrind 内存检查
- ABI 导出检查
- Windows VS 2022 x64/MSVC 19.39 全量测试

vendored `libpg_query` tag：`17-6.2.2`。
