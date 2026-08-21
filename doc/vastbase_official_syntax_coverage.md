# Vastbase 官方语法覆盖

本文件记录 Vastbase 兼容模式与当前可执行测试矩阵的对应关系。Vastbase 在官方资料中提供 Oracle、MySQL、PostgreSQL、SQL Server 方向的兼容能力，`sqlparser` 使用四个显式方言入口分别覆盖这些模式。

## 覆盖矩阵

| 模式 | 官方资料 | 回归夹具 | 成功用例 | 预期失败用例 | 用例总数 |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_oracle_dialect_input.json` | 222 | 0 | 222 |
| `vastbase-mysql` | [反引号解释为标识符](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html) | `tests/cases/vastbase_mysql_dialect_input.json` | 263 | 0 | 263 |
| `vastbase-postgresql` | [PostgreSQL 兼容性](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281) | `tests/cases/vastbase_postgresql_dialect_input.json` | 203 | 0 | 203 |
| `vastbase-sqlserver` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_sqlserver_dialect_input.json` | 607 | 0 | 607 |

## 说明

- `vastbase` CLI 别名固定等价于 `vastbase-oracle`。
- 当前实现不根据 SQL 文本自动选择兼容模式。
- `vastbase-mysql` 的 263 条用例均为 `status = "final"`，合计 841 个独立 patch。项目兼容入口合同支持 MySQL 多目标多表 `UPDATE` 的 JOIN 链与逗号 relation 列表，每个 assignment 目标字段分别关联其写入 relation；多表形态不接受 `ORDER BY` 或 `LIMIT`。该口径来自项目可执行矩阵，不声称 Vastbase 服务端官网定义了同一语法范围。
- `vastbase-oracle` 的 222 条用例均为 `status = "final"`，合计 796 个独立 patch。项目兼容入口合同包含 `INSERT ... VALUES`、`UPDATE` 和 `DELETE` 的 `RETURNING ... INTO`：`N >= 1` 个返回 target 与严格等长的 N 个冒号宿主 bind 按 ordinal 配对；不接受 `BULK COLLECT`、非冒号 bind receiver 或不等长列表；同一 `insert_column` patch 成对插入两侧。该口径来自项目可执行矩阵，不声称 Vastbase 服务端官网定义了同一语法范围。
- `vastbase-postgresql` 的 203 条用例均为 `status = "final"`，合计 663 个独立 patch。
- `vastbase-sqlserver` 的 607 条用例均为 `status = "final"`，合计 1858 个独立 patch。项目兼容入口的成对 `insert_column` 只适用于 sink `OUTPUT ... INTO` 具有显式、非空 sink column list，且改写前 OUTPUT target 与 sink column 数严格相等的场景；操作按同一序号原子插入两侧。原本合法的不等长 `OUTPUT` 仍可解析和反解析，但不支持成对插入；client `OUTPUT` 与未显式列出 sink column 的 `OUTPUT ... INTO` 也不纳入该改写边界。3 条用例覆盖 INSERT、UPDATE、DELETE 的 8↔8 配对及头、中、尾原子插入后的 9↔9 配对。该口径来自项目可执行矩阵，不声称 Vastbase 服务端官网定义了同一语法范围。
- 四个入口的项目兼容合同均支持通过既有 `insert_column` 对 MERGE INSERT 目标列与 VALUES cell 分别增加或成对增加，并分别替换已有两侧；省略目标列清单但存在 VALUES 时输出 `target_list_selector`，可物化清单或保持省略。批内允许两侧暂时不等长；批末存在显式目标列清单时必须与 VALUES 等长，否则整批原子回滚。成对删除不变，`MERGE INSERT DEFAULT VALUES` 不在该改写范围内。该口径来自项目可执行矩阵，不声称 Vastbase 服务端官网定义了同一语法范围。
- 四个入口各有 1 条 final 用例和 2 个 patch 验证 `alias_quoted_identifier` 与 `output_quoted_identifier`。字段只对应精确来源 token；各入口仅识别既有定界符：Oracle/PostgreSQL 双引号、MySQL 反引号、SQL Server 方括号。匹配时 C 标志为 `1`，否则为 `0`，View JSON 仅在值为真时输出同名字段；不扩展到 `U&"..."`。这是项目兼容入口合同，不代表 Vastbase 服务端官网定义了同一范围。
- `vastbase-sqlserver` 的层次查询边界仅包含基础 `CONNECT BY` 条件；`START WITH`、`PRIOR`、`NOCYCLE` 和 `CONNECT_BY_ROOT` 不在该模式当前范围。
- 在该基础层次查询块内，仅无显式 `AS` 的 `CONNECT_BY_ROOT expr` 歧义形态会被拒绝；单独投影 `CONNECT_BY_ROOT` 仍为普通字段，需要别名时可使用显式 `AS` 或定界标识符。
- 当前四套夹具均只包含 `status = "final"` 用例，预期失败用例数为 0。夹具统计不代表官方语法覆盖率。
