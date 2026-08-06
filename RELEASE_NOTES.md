# v2.14.5 发布说明

`v2.14.5` 为 query graph 增加标识符定界符状态，并修复 Oracle 兼容模式下 assignment patch 经片段预处理后与重新解析 View 不一致的问题。

## 标识符定界符状态

- `sqlparser_graph_relation_t.quoted_identifier` 表示 relation 的对象名 token 是否显式使用标识符定界符。
- `sqlparser_graph_field_t.quoted_identifier` 表示 field 的列名 token 是否显式使用标识符定界符。
- View JSON 在对应 `relations[]`、`fields[]` 项中按需输出 `quoted_identifier: true`。Session value 的 `kind` 为 `identifier` 时也可输出同名字段；C 接口通过 `sqlparser_graph_session_value_t.literal.quoted_identifier` 读取。
- 当前标记识别 `"..."`、MySQL 反引号和 SQL Server `[...]`。它只表示定界符是否存在，不区分定界符类型；单引号字符串不属于标识符定界符。
- Relation 标记只对应对象名，不表示 database、schema 或 alias 的定界状态；field 标记只对应列名。

## 原始 token 与 Patch 一致性

- 标记必须能够追溯到原始 SQL 或 patch 片段中的精确 token。解析器为方言兼容生成的引号样式不作为原始定界符输出。
- Oracle 与 Vastbase-Oracle 在 fragment preprocess 中回放 identifier origin。即使 assignment 右侧的 `:1` 等 bind 被转换为内部形式，左侧显式定界列仍保留来源信息。
- `replace_assignment` 等 patch 生成的当前 handle View 与反解析后重新 parse 的 View 使用相同判断规则，不会出现 patch 后缺少标记、重新解析后新增标记的差异。
- Oracle、达梦及 Vastbase-Oracle 的 database link relation 使用方言状态保存的对象原始拼写；MySQL 兼容的 session identifier 使用原始 token 判断定界符状态。

## 接口与所有权

- 本版本新增 `sqlparser_graph_relation_t.quoted_identifier` 和 `sqlparser_graph_field_t.quoted_identifier` 两个公开结构体字段。
- 没有新增公开函数或枚举。Query graph 返回值继续是 handle 持有的 borrowed view，不新增释放接口或调用方资源所有权。
- 标记字段为整数布尔值；未使用显式定界符时为 `0`，View JSON 省略对应键。

## 验证

- 九套可执行方言夹具仍包含 2,758 条 `status = "final"` 用例和 8,945 个独立 patch。
- 夹具新增 1,800 个精确断言：910 个 relation、876 个 field、14 个 session identifier value。
- 远端 `make test-unit` 完成，九套 fixture 的原始反解析、View、patch 反解析及 patched/fresh View 对比全部通过。
- 资源所有权与复杂度审核确认：origin cache 由 handle 统一释放，Oracle fragment replay 为单次线性扫描，没有新增常驻缓存或平方级路径。

内置 `libpg_query` 标签：`17-6.2.2`。
