# v2.16.14 发布说明

完善 SQL 注释边界处理：Oracle、Vastbase Oracle、KingbaseES Oracle 和达梦的 `INSERT ALL/FIRST` 识别前导及结构关键字间注释；SQL Server 系入口识别 `SELECT` 与 `TOP` 间注释及 `GO` 批分隔符同一行的注释。MySQL 入口保留整句可执行注释两侧的普通注释，并正确处理 `USE` 尾随注释；PostgreSQL 系入口在首个 SELECT target 前含嵌套注释时保留 target patch 后的原文。

上述能力限定于 SQL 解析、反解析和 patch，不声明优化器提示或可执行注释在数据库服务端的执行效果。未新增公开 API、结构体字段、View JSON 字段或所有权规则。

13 套 fixture 新增 32 条 final case 和 56 个 patch，合计 3,269 条 final case 和 10,305 个 patch。远端完整 `make test`、13 套用例矩阵及相关定向测试通过；32 条新增用例的 Valgrind 检查未报内存错误或泄漏。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
