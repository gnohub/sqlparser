# v2.16.13 发布说明

新增 KingbaseES PostgreSQL、Oracle、MySQL 和 SQL Server 四个公开方言枚举及 CLI 入口。PostgreSQL、Oracle 和 MySQL 合并 V8/V9 语法基线，SQL Server 使用 V9R4 兼容基线；入口不接收、检测或分派服务端版本。

四个入口最大限度复用既有基础方言能力。KingbaseES Oracle 仅增加薄 preprocess 分支以支持 plain `RETURNING`，Oracle `RETURNING INTO` 的状态与成对改写边界保持不变；Oracle 词法扫描同时正确保护 dollar-quoted literal 内的 `?`。

公开 API 仅新增 4 个 dialect 枚举值，不新增公开函数、结构体字段、View JSON 字段或所有权规则，公开导出符号保持 162 个。

新增 4 套 final fixture，合计 175 条 case 和 628 个 patch。13 套 fixture 当前合计 3,237 条 final case 和 10,249 个 patch。远端完整 `make test`、13 套方言矩阵和四个 KingbaseES CLI 入口验证均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
