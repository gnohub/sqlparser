# 达梦官方语法覆盖统计

本文件记录达梦方言相对于达梦官方 DM_SQL 文档的覆盖统计。完整逐条清单见 [dameng_official_syntax_coverage.csv](dameng_official_syntax_coverage.csv)。

## 统计来源

- [达梦 DM_SQL 数据查询语句](https://eco.dameng.com/document/dm/zh-cn/pm/check-phrases.html)
- [达梦 DM_SQL 数据的插入、删除和修改](https://eco.dameng.com/document/dm/zh-cn/pm/insertion-deletion-modification)
- [达梦 DM_SQL 数据定义语句](https://eco.dameng.com/document/dm/zh-cn/pm/definition-statement.html)
- [达梦 DMSQL 程序中的 SQL 语句](https://eco.dameng.com/document/dm/zh-cn/pm/dm8_sql-sql-statement)
统计范围固定为当前达梦方言转换层涉及的官方语法组，包括查询、DML、常见 DDL、事务语句、表达式、权限语句和达梦专属语义。

## 分类口径

| 状态 | 含义 |
| --- | --- |
| `CURRENT` | 当前达梦方言已有代表性可执行覆盖，或可由当前 AST 安全表达。 |
| `HOOK_ONLY` | 当前尚未覆盖，但可以通过方言 hook、预处理、后处理或类型/函数映射完成。 |
| `MIXED_MODEL` | 基础形态可以通过现有 AST 和 hook 支持，但完整官方语法需要达梦专用模型。 |
| `MODEL_REQUIRED` | 需要达梦专用模型，通常涉及表变换、DMSQL 程序单元或闪回。 |
| `REFERENCE_ONLY` | 官方索引页、分类页或说明页，不作为独立实现单元统计支持率。 |

`RETURNING_INTO` 的 `CURRENT` 边界为 `INSERT`、`DELETE` 的 `RETURNING <target, ...> INTO <:bind, ...>`，以及 `UPDATE` 的 `RETURN <target, ...> INTO <:bind, ...>`。每个列表均为 `N >= 1` 项，两个列表严格等长并按序号一一配对；接收项必须是冒号宿主 bind。该边界不包含 `BULK COLLECT`、非冒号 bind 接收项或不等长列表。

`UPDATE` 的 `CURRENT` 边界包括 JOIN 链、逗号 relation 列表及混合形态的多表单目标更新；全部 SET assignment 必须指向同一个 table object。

## 统计结果

| 状态 | 语法组数 | 占全部 38 组 |
| --- | ---: | ---: |
| `CURRENT` | 33 | 86.84% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 5 | 13.16% |
| `REFERENCE_ONLY` | 0 | 0.00% |

剔除 `REFERENCE_ONLY` 后，官方可实现语法组为 38 组。其中当前已覆盖 33 组，未覆盖 5 组。

| 未覆盖分类 | 语法组数 | 占未覆盖 5 组 |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 5 | 100.00% |

## 结论

达梦 `MERGE` 的 `CURRENT` 边界包含 matched UPDATE 的 action `WHERE` 以及归属同一 UPDATE 分支的附属 `DELETE WHERE`。`insert_column` 支持 column-only、value-only、paired 三态；省略目标列列表的 not-matched INSERT 仍输出目标列表 selector，并可分别物化列列表、在保持省略时追加 VALUES cell 或替换现有 cell，显式列表继续支持 paired 添加。可执行矩阵使用 2 条用例和 6 个独立 patch 验证这些边界，其中省略列表用例的 3 个 patch 独立执行；最终列值等宽校验与失败整批回滚由核心 API 单元测试验证。

达梦层次查询的 `CURRENT` 边界包括 `START WITH` 与 `CONNECT BY` 两种源文本顺序、一元 `PRIOR` 的两种父子字段方向、`LEVEL`、`CONNECT_BY_ROOT` 和 `NOCYCLE`。可执行矩阵包含 4 条 `final` 用例和 20 个独立 patch。

达梦多表 `UPDATE` 的 `CURRENT` 边界始终只有一个写入目标。可执行矩阵包含 6 条 `final` 用例和 17 个独立 patch，覆盖首、中、末 relation 目标、同表不同 alias、JOIN 链及 JOIN/逗号混合形态。

达梦当前已覆盖常用查询、DML、DDL、事务、权限、`SET SCHEMA`、代表性会话参数设置语句、远程对象引用基础形态，以及上述 `N >= 1` 严格等长、按序配对的 `RETURN`/`RETURNING ... INTO` 形态。可执行矩阵使用 3 条用例覆盖 INSERT、UPDATE、DELETE 的 8↔8 配对及头、中、尾原子插入后的 9↔9 配对。其余 5 个语法组依赖达梦专属查询模型或程序单元语义，当前不纳入 PostgreSQL 兼容转换。
