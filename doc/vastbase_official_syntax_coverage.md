# Vastbase 官方语法覆盖

本文件记录 Vastbase 兼容模式与当前可执行测试矩阵的对应关系。Vastbase 在官方资料中提供 Oracle、MySQL、PostgreSQL、SQL Server 方向的兼容能力，`sqlparser` 使用四个显式方言入口分别覆盖这些模式。

## 覆盖矩阵

| 模式 | 官方资料 | 回归夹具 | 成功用例 | 预期失败用例 | 用例总数 |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_oracle_dialect_input.json` | 217 | 0 | 217 |
| `vastbase-mysql` | [反引号解释为标识符](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html) | `tests/cases/vastbase_mysql_dialect_input.json` | 254 | 0 | 254 |
| `vastbase-postgresql` | [PostgreSQL 兼容性](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281) | `tests/cases/vastbase_postgresql_dialect_input.json` | 199 | 0 | 199 |
| `vastbase-sqlserver` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_sqlserver_dialect_input.json` | 601 | 0 | 601 |

## 说明

- `vastbase` CLI 别名固定等价于 `vastbase-oracle`。
- 当前实现不根据 SQL 文本自动选择兼容模式。
- `vastbase-oracle` 的 217 条用例均为 `status = "final"`；其中包含 `INSERT ... VALUES`、`UPDATE` 和 `DELETE` 的单个返回 target `RETURNING ... INTO :bind`，能力边界为一个结果 target 和一个冒号宿主绑定变量。
- `vastbase-sqlserver` 的层次查询边界仅包含基础 `CONNECT BY` 条件；`START WITH`、`PRIOR`、`NOCYCLE` 和 `CONNECT_BY_ROOT` 不在该模式当前范围。
- 在该基础层次查询块内，仅无显式 `AS` 的 `CONNECT_BY_ROOT expr` 歧义形态会被拒绝；单独投影 `CONNECT_BY_ROOT` 仍为普通字段，需要别名时可使用显式 `AS` 或定界标识符。
- 当前四套夹具均只包含 `status = "final"` 用例，预期失败用例数为 0。夹具统计不代表官方语法覆盖率。
