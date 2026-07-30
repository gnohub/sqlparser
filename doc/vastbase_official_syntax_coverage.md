# Vastbase 官方语法覆盖

本文件记录 Vastbase 兼容模式与当前可执行测试矩阵的对应关系。Vastbase 在官方资料中提供 Oracle、MySQL、PostgreSQL、SQL Server 方向的兼容能力，`sqlparser` 使用四个显式方言入口分别覆盖这些模式。

## 覆盖矩阵

| 模式 | 官方资料 | 回归夹具 | 成功用例 | 预期失败用例 | 用例总数 |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_oracle_dialect_input.json` | 188 | 21 | 209 |
| `vastbase-mysql` | [反引号解释为标识符](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html) | `tests/cases/vastbase_mysql_dialect_input.json` | 210 | 9 | 219 |
| `vastbase-postgresql` | [PostgreSQL 兼容性](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281) | `tests/cases/vastbase_postgresql_dialect_input.json` | 174 | 10 | 184 |
| `vastbase-sqlserver` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_sqlserver_dialect_input.json` | 547 | 38 | 585 |

## 说明

- `vastbase` CLI 别名固定等价于 `vastbase-oracle`。
- 当前实现不根据 SQL 文本自动选择兼容模式。
- 预期失败用例包括非法 SQL，以及当前方言到 AST 的映射尚未表示其必要语义的用例。该夹具统计不代表官方语法覆盖率。
