# v2.10.1 发布说明

`v2.10.1` 修正 MySQL 与 Vastbase-MySQL 索引提示的反解析顺序。

## 主要变化

- `USE INDEX`、`IGNORE INDEX`、`FORCE INDEX` 及对应 `KEY` 形式保持在表名或别名之后、后续查询子句之前。
- 修正索引提示与分组、窗口、集合运算、锁定及 JOIN 子句组合时的位置。
- 修正 `STRAIGHT_JOIN` 右侧关系的索引提示恢复。
- MySQL 与 Vastbase-MySQL 方言矩阵分别包含 173 条支持用例。

## 兼容性

- 公共 API、公共结构体和 View JSON 保持不变。
- 动态库 ABI 主版本保持为 `libsqlparser.so.0`，ABI 导出检查包含 146 个公共符号。

## 发布验证

- GCC 8.3 严格编译与全量测试
- ASan、UBSan 与 Valgrind 内存检查
- ABI 导出检查
- Windows VS 2022 x64/MSVC 19.39 全量测试

内置 `libpg_query` 版本：`17-6.2.2`。
