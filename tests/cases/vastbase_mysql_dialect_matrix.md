# Vastbase MySQL 兼容模式用例矩阵

可执行夹具为 `tests/cases/vastbase_mysql_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。

## 事务特征规范语义值回归

以下 4 条 final 用例覆盖 Vastbase MySQL 兼容模式的常用事务隔离级别和访问模式。原始 SQL 反解析必须逐字节保持；View 必须按输入顺序输出去除 trivia 后的规范关键字值。session 语义值不提供 selector，因此这些用例不设置 patch。

| ID | 用例 | SQL | 验证重点 |
| --- | --- | --- | --- |
| `VM-TX001` | `vastbase-mysql-session-transaction-commented-read-uncommitted` | ALTER/*command*/SESSION SET TRANSACTION ISOLATION/*name*/LEVEL READ/*value*/UNCOMMITTED; | `READ UNCOMMITTED` 规范值及单事务特征 |
| `VM-TX002` | `vastbase-mysql-session-characteristics-commented-repeatable-read-write` | ALTER SESSION SET SESSION/*scope*/CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE/*value*/READ, READ/*mode*/WRITE; | `REPEATABLE READ`、`READ WRITE` 及 session characteristics 入口 |
| `VM-TX003` | `vastbase-mysql-session-transaction-commented-serializable-read-only` | ALTER SESSION SET TRANSACTION ISOLATION LEVEL SERIALIZABLE/*tail*/, READ/*mode*/ONLY; | `SERIALIZABLE`、`READ ONLY` 及逗号前注释 |
| `VM-TX004` | `vastbase-mysql-session-transaction-commented-option-order` | ALTER SESSION SET TRANSACTION read/*mode*/write, ISOLATION/*name*/LEVEL read/*value*/committed; | 输入选项顺序、小写原文保留及 `READ COMMITTED` 规范值 |

## 矩阵统计与 session 回归

夹具包含 267 条 `status = "final"` 用例和 854 个独立 patch；其中 3 条用例及其 11 个 patch 含完整 bind occurrence 断言，44 条用例的期望 View 包含非空 session 投影。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session action、item scope、target kind、name、value 类型、规范文本及顺序均属于比较范围。

## MERGE INSERT 独立列值改写回归

以下 final 用例定义项目 `vastbase-mysql` 兼容入口合同，不声称 Vastbase 服务端官网定义了相同语法范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VM263` | `vastbase-mysql-merge-omitted-insert-column-value-independent` | final | 3 | 省略目标列清单时输出 `target_list_selector`；分别验证仅增加目标列并物化清单、仅增加 VALUES cell 且保持清单省略，以及替换已有 cell |

## 定界别名状态回归

`vastbase-mysql-quoted-alias-output-flags` 及其 2 个 output alias patch 验证 Query Graph 字段合同：relation alias 的精确来源 token 使用反引号时输出 `alias_quoted_identifier: true`；target 的 `output_name` 来源于带反引号的显式 alias，或无显式 alias 时来源于带反引号的直接字段 token，则输出 `output_quoted_identifier: true`。未定界来源不输出对应字段。该合同属于项目兼容入口，不代表 Vastbase 服务端官方语法范围。

## 定界关系分段与 DML 列状态回归

以下 final 用例以同名定界/未定界标识符对照验证 relation 的 database、schema、object 分段状态和 DML 目标列状态；5 个独立 patch 覆盖 relation 整体替换、MERGE INSERT 目标列替换与插入，以及 `INSERT ... SET` 定界目标列插入，并要求 patch handle 与重新解析 handle 的标志一致。该用例定义项目 `vastbase-mysql` 兼容入口合同，不声称 Vastbase 服务端官网定义了相同语法范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VM264` | `vastbase-mysql-quoted-identifier-segment-and-dml-column-inventory` | final | 5 | 多语句 SELECT、INSERT、UPDATE、DELETE、MERGE、`INSERT ... SET` 与 `REPLACE ... SET` 覆盖 `database_quoted_identifier`、`schema_quoted_identifier`、relation `quoted_identifier` 及 DML column `quoted_identifier`；未定界同名分段不输出对应 View 字段 |

## DDL relation 投影回归

以下 3 条 final 用例定义项目 `vastbase-mysql` 兼容入口的 DDL Query Graph 合同：DDL 根块使用 `kind = "ddl"`，relation 以 `ddl_role = "target"` 或 `"reference"` 区分操作对象与引用对象，并保留 database/object 来源分段的反引号定界状态。查询驱动的 CREATE 对象通过 `source_block` 指向独立 SELECT 块。语法形态以夹具中已验证的 MySQL 兼容语法为边界；该合同属于项目兼容入口，不声称 Vastbase 服务端官网定义了相同范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VM265` | `vastbase-mysql-ddl-relation-direct-inventory` | final | 6 | CREATE/ALTER TABLE FK target/reference、CREATE INDEX、多对象 DROP TABLE/VIEW、单对象 TRUNCATE 与 RENAME 旧对象；relation patch 重算反引号定界状态 |
| `VM266` | `vastbase-mysql-ddl-relation-query-backed-inventory` | final | 2 | CREATE VIEW 与 CTAS 的 DDL target、SELECT 来源及 `source_block` 关联 |
| `VM267` | `vastbase-mysql-ddl-drop-table-quoted-same-spelling` | final | 0 | 普通语句与 statement-level executable comment 内的 quoted/unquoted 同名 DROP target 均无 selector，schema/object 反引号状态按精确来源 token 分别输出 |

## 完整绑定占位符 occurrence 回归

以下 3 条 final 用例定义项目 `vastbase-mysql` 兼容入口的 handle 级 occurrence 合同，不作为 Vastbase 服务端官方能力声明。runner 对输入及每个 patch 后的公开 SQL 逐项断言 `position`、`kind`、`key` 和 `sql`；匿名 `?` 每次出现均单独返回，多语句编号不重置，字符串、普通注释、非语句级 executable comment 和反引号标识符中的伪问号不计入。

| 用例 | 根 occurrence | Patch | 基础入口关系 | 验证重点 |
| --- | ---: | ---: | --- | --- |
| `vastbase-mysql-multi-statement-global-bind-position` | 4 | 5 | 除用例名外，逐字段镜像 `mysql-multi-statement-global-bind-position` | 多语句、语句级 executable comment、普通/内联注释及复杂表达式改写后的连续匿名位置 |
| `vastbase-mysql-insert-bind-mixed-three-rows` | 9 | 3 | 对应基础 MySQL 用例；本入口使用 `CONVERT(?, BIGINT)`，基础入口使用 `CAST(? AS SIGNED)`，occurrence 合同相同 | 三行 VALUES 中函数、转换和 CASE 内的 9 个匿名 occurrence；删除头部或尾部 bind 后重编号 |
| `vastbase-mysql-update-multiple-target-three-table-bind-order` | 12 | 3 | 镜像基础 MySQL 多目标 UPDATE 用例 | 三表 JOIN 的 ON/SET/WHERE occurrence 顺序及 assignment patch 后重编号 |

| ID | 用例 | SQL | 状态 |
| --- | --- | --- | --- |
| `VM001` | `vastbase-mysql-select-limit-comma` | SELECT `u`.`id`, "hello" AS `label` FROM `users` AS `u` WHERE `u`.`id` = 1 LIMIT 5, 10 | 已覆盖 |
| `VM002` | `vastbase-mysql-select-join` | SELECT `u`.`id`, `u`.`name`, `o`.`order_no` FROM `users` `u` JOIN `orders` `o` ON `u`.`id` = `o`.`user_id` WHERE `o`.`status` = "paid" | 已覆盖 |
| `VM003` | `vastbase-mysql-hash-comment` | SELECT 1 # mysql line comment<br> | 已覆盖 |
| `VM004` | `vastbase-mysql-insert-values-multi-row` | INSERT INTO `users` (`id`, `name`) VALUES (1, "bob"), (2, 'alice') | 已覆盖 |
| `VM004N` | `vastbase-mysql-national-string-literal` | SELECT "prefix" AS prefix_value, N'Alice''s order' AS label FROM `users` WHERE `name` = n'Bob' | 已覆盖 |
| `VM004NA` | `vastbase-mysql-national-string-duplicate-literal` | SELECT 'same' AS ascii_value, N'same' AS national_value FROM `users` | 已覆盖 |
| `VM005` | `vastbase-mysql-insert-select` | INSERT INTO `archive_users` (`id`, `name`) SELECT `id`, `name` FROM `users` WHERE `status` = "inactive" | 已覆盖 |
| `VM006` | `vastbase-mysql-update-basic` | UPDATE `users` SET `name` = "carol", `status` = 'active' WHERE `id` = 1 | 已覆盖 |
| `VM007` | `vastbase-mysql-delete-conditional` | DELETE FROM `users` WHERE `id` = 1 AND `status` = "inactive" | 已覆盖 |
| `VM008` | `vastbase-mysql-create-table-basic` | CREATE TABLE `users` (`id` INT, `name` VARCHAR(64)) | 已覆盖 |
| `VM009` | `vastbase-mysql-alter-table-add-column` | ALTER TABLE `users` ADD COLUMN `age` INT | 已覆盖 |
| `VM010` | `vastbase-mysql-create-view` | CREATE VIEW `v_users` AS SELECT `id`, `name` FROM `users` | 已覆盖 |
| `VM011` | `vastbase-mysql-drop-table` | DROP TABLE `users` | 已覆盖 |
| `VM012` | `vastbase-mysql-start-transaction` | START TRANSACTION; COMMIT | 已覆盖 |
| `VM013` | `vastbase-mysql-unsupported-keywords-in-string` | SELECT 'INSERT IGNORE' AS msg FROM `users` | 已覆盖 |
| `VM014` | `vastbase-mysql-unsupported-keywords-in-comment` | SELECT `id` FROM `users` /* ON DUPLICATE KEY UPDATE */ WHERE `id` = 1 | 已覆盖 |
| `VM014Q` | `vastbase-mysql-unsupported-keywords-in-quoted-identifiers` | SELECT `unsigned`, `auto_increment`, `engine` FROM `users` | 已覆盖 |
| `VM015` | `vastbase-mysql-use-database` | USE analytics | 已覆盖 |
| `VM016` | `vastbase-mysql-use-quoted-database` | USE `analytics-prod` | 已覆盖 |
| `VM017` | `vastbase-mysql-use-database-in-multi-statement` | USE `analytics-prod`; SELECT * FROM `users` | 已覆盖 |
| `VM018` | `vastbase-mysql-insert-question-params` | INSERT INTO users (username, email, age) VALUES (?, ?, ?) | 已覆盖 |
| `VM019` | `vastbase-mysql-update-question-params` | UPDATE users SET email = ? WHERE username = ? | 已覆盖 |
| `VM020` | `vastbase-mysql-prepare-from-literal` | PREPARE stmt FROM 'SELECT * FROM users WHERE id = ?' | 已覆盖 |
| `VM021` | `vastbase-mysql-execute-using` | EXECUTE stmt USING @user_id | 已覆盖 |
| `VM022` | `vastbase-mysql-deallocate-prepare` | DEALLOCATE PREPARE stmt | 已覆盖 |
| `VM023` | `vastbase-mysql-drop-prepare` | DROP PREPARE stmt | 已覆盖 |
| `VM024` | `vastbase-mysql-select-question-params` | SELECT id, name FROM users WHERE id = ? AND status = ? | 已覆盖 |
| `VM025` | `vastbase-mysql-select-in-question-params` | SELECT id FROM users WHERE status IN (?, ?, ?) | 已覆盖 |
| `VM026` | `vastbase-mysql-select-limit-question-params` | SELECT id FROM users WHERE name LIKE ? ORDER BY id LIMIT ? OFFSET ? | 已覆盖 |
| `VM027` | `vastbase-mysql-insert-named-columns-question-params` | INSERT INTO users (id, name, status) VALUES (?, ?, ?) | 已覆盖 |
| `VM028` | `vastbase-mysql-insert-multi-row-question-params` | INSERT INTO users (id, name) VALUES (?, ?), (?, ?) | 已覆盖 |
| `VM029` | `vastbase-mysql-update-multi-question-params` | UPDATE users SET name = ?, status = ? WHERE id = ? | 已覆盖 |
| `VM030` | `vastbase-mysql-delete-question-params` | DELETE FROM users WHERE id = ? AND status = ? | 已覆盖 |
| `VM031` | `vastbase-mysql-prepare-insert-literal` | PREPARE stmt FROM 'INSERT INTO users (id, name) VALUES (?, ?)' | 已覆盖 |
| `VM032` | `vastbase-mysql-prepare-from-user-variable` | PREPARE stmt FROM @sql_text | 已覆盖 |
| `VM033` | `vastbase-mysql-execute-using-multiple-vars` | EXECUTE stmt USING @id, @name | 已覆盖 |
| `VM034` | `vastbase-mysql-view-concat-function` | SELECT CONCAT(first_name, last_name) FROM users WHERE id = ? | 已覆盖 |
| `VM035` | `vastbase-mysql-view-case-expression` | SELECT CASE WHEN state = 1 THEN name ELSE fallback_name END FROM users | 已覆盖 |
| `VM035A` | `vastbase-mysql-view-case-predicate-bind` | SELECT CASE WHEN phone = ? THEN name ELSE email END AS v FROM users | 已覆盖 |
| `VM036` | `vastbase-mysql-view-group-having-order` | SELECT dept, COUNT(id) FROM users GROUP BY dept HAVING COUNT(id) > 1 ORDER BY dept | 已覆盖 |
| `VM037` | `vastbase-mysql-view-update-question-binds` | UPDATE users SET ip = ? WHERE id = ? | 已覆盖 |
| `VM038` | `vastbase-mysql-view-join-on` | SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = ? | 已覆盖 |
| `VM039` | `vastbase-mysql-select-between-question-params` | SELECT id FROM users WHERE age BETWEEN ? AND ? | 已覆盖 |
| `VM040` | `vastbase-mysql-select-not-in-question-params` | SELECT id FROM users WHERE status NOT IN (?, ?) | 已覆盖 |
| `VM041` | `vastbase-mysql-select-not-between-question-params` | SELECT id FROM users WHERE age NOT BETWEEN ? AND ? | 已覆盖 |
| `VM042` | `vastbase-mysql-select-not-like-question-param` | SELECT id FROM users WHERE name NOT LIKE ? | 已覆盖 |
| `VM043` | `vastbase-mysql-select-distinct-like-param` | SELECT DISTINCT `name` FROM `table1` WHERE `name` LIKE ? | 已覆盖 |
| `VM044` | `vastbase-mysql-select-left-join-alias-star` | SELECT u.*, o.`order_no` FROM `users` u LEFT JOIN `orders` o ON u.`id` = o.`user_id` WHERE o.`status` = ? | 已覆盖 |
| `VM045` | `vastbase-mysql-delete-in-question-params` | DELETE FROM `users` WHERE `email` IN (?, ?) | 已覆盖 |
| `VM046` | `vastbase-mysql-update-in-question-params` | UPDATE `users` SET `note` = ? WHERE `phone` IN (?, ?) | 已覆盖 |
| `VM047` | `vastbase-mysql-select-derived-table-filter` | SELECT t.`id`, t.`phone` FROM (SELECT `id`, `phone` FROM `users` WHERE `status` = ?) t WHERE t.`phone` = ? | 已覆盖 |
| `VM054` | `vastbase-mysql-select-derived-query-graph` | SELECT s.`inner_name` AS `outer_name` FROM (SELECT `id`, `name` AS `inner_name` FROM `users` WHERE `age` <= ?) s WHERE s.`inner_name` LIKE ? | 已覆盖 |
| `VM048` | `vastbase-mysql-select-json-extract` | SELECT JSON_EXTRACT(`extra`, '$.phone') AS phone_json FROM `users` WHERE `id` = ? | 已覆盖 |
| `VM049` | `vastbase-mysql-create-table-if-not-exists` | CREATE TABLE IF NOT EXISTS `users` (`id` INT, `name` VARCHAR(64), `phone` TEXT) | 已覆盖 |
| `VM050` | `vastbase-mysql-drop-view-if-exists` | DROP VIEW IF EXISTS `user_view` | 已覆盖 |
| `VM051` | `vastbase-mysql-select-order-by-ordinal` | SELECT `id`, `phone` FROM `users` ORDER BY 1 | 已覆盖 |
| `VM052` | `vastbase-mysql-limit-comma-question-params` | SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ? | 已覆盖 |
| `VM053` | `vastbase-mysql-multi-statement-global-bind-position` | UPDATE `users` SET `a` = ? WHERE `b` = ?; UPDATE `users` SET `c` = ? WHERE `d` = ? | 已覆盖 |
| `VM055` | `vastbase-mysql-select-reference-002` | SELECT `id`, `name`, `age` FROM `users`; | 已覆盖 |
| `VM056` | `vastbase-mysql-select-reference-003` | SELECT `id`, `name`, `age` FROM `viq-test`.`users`; | 已覆盖 |
| `VM057` | `vastbase-mysql-select-reference-006` | SELECT u.`id` AS user_id, u.`name` AS user_name FROM `viq-test`.`users` u; | 已覆盖 |
| `VM058` | `vastbase-mysql-select-reference-008` | SELECT * FROM `viq-test`.`products` WHERE `price` > 100 AND `category` = 'Electronics'; | 已覆盖 |
| `VM059` | `vastbase-mysql-select-reference-010` | SELECT * FROM logs LIMIT 10; | 已覆盖 |
| `VM060` | `vastbase-mysql-select-reference-012` | SELECT * FROM users WHERE id IN (SELECT id FROM users WHERE email = ?); | 已覆盖 |
| `VM061` | `vastbase-mysql-select-reference-014` | SELECT a.id, a.name FROM (SELECT id, name FROM `viq-test`.`users` WHERE `active` = 1) a; | 已覆盖 |
| `VM062` | `vastbase-mysql-select-reference-016` | SELECT id, name FROM (SELECT name, id, age FROM `viq-test`.`users`); | 已覆盖 |
| `VM063` | `vastbase-mysql-select-reference-022` | SELECT b.* FROM (SELECT a.* FROM (SELECT id, name FROM `viq-test`.`users`) a WHERE id > 10) b WHERE name LIKE 'A%'; | 已覆盖 |
| `VM064` | `vastbase-mysql-select-reference-023` | SELECT b.* FROM (SELECT a.* FROM (SELECT `id`, `name` FROM `viq-test`.`users`) a WHERE id > 10) b WHERE name LIKE 'A%'; | 已覆盖 |
| `VM065` | `vastbase-mysql-select-reference-025` | select * from (select id as aa, department as cc, name as bb FROM (select e.*,rownum as row_num from `viq-test`.`employees_uuid` e where rownum <= 100) where row_num >= 1 and (id = 1 or id=2)) where aa = 1; | 已覆盖 |
| `VM066` | `vastbase-mysql-select-reference-027` | select b.* from (select id as `aa`, `department` as cc, name as bb FROM (select e.*,rownum as row_num from `viq-test`.`employees_uuid` e where rownum <= 100) a where row_num >= 1 and (id = 1 or id=2)) b where aa = 1; | 已覆盖 |
| `VM067` | `vastbase-mysql-select-reference-029` | SELECT * FROM (SELECT e.*, `ROWNUM` rn FROM `viq-test`.`employees_uuid` e WHERE `ROWNUM` <= 100) WHERE rn > 50; | 已覆盖 |
| `VM068` | `vastbase-mysql-update-join-target-table-qualified` | UPDATE users JOIN orders o ON users.id=o.user_id SET users.phone = ? WHERE o.shipping_phone = ? | 已覆盖 |
| `VM069` | `vastbase-mysql-field-match-kind-direct-and-expression` | SELECT id FROM users WHERE secret = ? AND UPPER(secret) = ? | 已覆盖 |
| `VM070` | `vastbase-mysql-expression-field-case-expression-value` | SELECT id FROM users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = ? | 已覆盖 |
| `VM071` | `vastbase-mysql-expression-field-multi-field-expression-value` | SELECT id FROM users WHERE CONCAT(secret, id) = ? AND secret + id = ? | 已覆盖 |
| `VM072` | `vastbase-mysql-expression-field-value-side-expression` | SELECT id FROM users WHERE secret = UPPER(?) AND secret = CONCAT(?, 'x') AND secret = CAST(? AS CHAR) | 已覆盖 |
| `VM073` | `vastbase-mysql-expression-field-dml-expression-values` | INSERT INTO users (id, secret) VALUES (1, UPPER(?)); UPDATE users SET secret = CONCAT(?, 'x') WHERE id = 1 | 已覆盖 |
| `VM074` | `vastbase-mysql-update-bind-rhs-crypto-source` | UPDATE `dbp_crypto_test` SET `secret` = ? WHERE `id` = ? | 已覆盖 |
| `VM075` | `vastbase-mysql-update-multiple-bind-rhs-crypto-source` | UPDATE `dbp_crypto_test` SET `phone` = ?, `secret` = ? WHERE `id` = ? | 已覆盖 |
| `VM076` | `vastbase-mysql-like-escape-literal` | SELECT `id` FROM `users` WHERE `name` LIKE 'A!_%' ESCAPE '!' | 已覆盖 |
| `VM077` | `vastbase-mysql-like-escape-question-binds` | SELECT `id` FROM `users` WHERE `name` LIKE ? ESCAPE ? | 已覆盖 |
| `VM078` | `vastbase-mysql-not-like-escape-literal` | SELECT `id` FROM `users` WHERE `name` NOT LIKE ? ESCAPE '!' | 已覆盖 |
| `VM079` | `vastbase-mysql-like-without-explicit-escape` | SELECT `id` FROM `users` WHERE `name` LIKE ? | 已覆盖 |
| `VM089` | `vastbase-mysql-update-join-source-field-graph` | UPDATE users u JOIN orders o ON u.id=o.user_id SET u.name = o.customer_name WHERE o.active = ? | 已覆盖 |
| `VM090` | `vastbase-mysql-insert-select-source-block-graph` | INSERT INTO users_archive (id, email) SELECT u.id, u.email FROM users u WHERE u.active = ? | 已覆盖 |
| `VM091` | `vastbase-mysql-with-cte-select` | WITH cte AS (SELECT id FROM users WHERE active = ?) SELECT id FROM cte | 已覆盖 |
| `VM092` | `vastbase-mysql-window-row-number` | SELECT id, ROW_NUMBER() OVER (PARTITION BY status ORDER BY id) AS rn FROM users | 已覆盖 |
| `VM093` | `vastbase-mysql-common-scalar-functions` | SELECT LOWER(name) AS lower_name, COALESCE(email, 'n/a') AS email_value FROM users WHERE id = ? | 已覆盖 |
| `VM094` | `vastbase-mysql-common-data-types` | CREATE TABLE users (id BIGINT, amount DECIMAL(10,2), created_at DATETIME, active BOOLEAN) | 已覆盖 |
| `VM095` | `vastbase-mysql-rollback` | ROLLBACK | 已覆盖 |
| `VM096` | `vastbase-mysql-json-contains-function-predicate` | SELECT * FROM users WHERE JSON_CONTAINS(tags, ?) | 已覆盖 |
| `VM097` | `vastbase-mysql-select-alias-order-by-lineage` | SELECT u.email AS e FROM users u ORDER BY u.email | 已覆盖 |
| `VM098` | `vastbase-mysql-create-table-column-and-table-options` | CREATE TABLE `users` (`id` INT UNSIGNED AUTO_INCREMENT, `score` INT ZEROFILL, `name` VARCHAR(64)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin | 已覆盖 |
| `VM099` | `vastbase-mysql-create-table-official-column-attributes` | CREATE TABLE `users` (`id` INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT 'pk', `name` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin INVISIBLE, `score` INT GENERATED ALWAYS AS (`id` + 1) STORED, `parent_id` INT REFERENCES `parents` (`id`) ON DELETE CASCADE) | 已覆盖 |
| `VM100` | `vastbase-mysql-create-table-numeric-table-options` | CREATE TABLE `users` (`id` INT) AUTO_INCREMENT=100 AVG_ROW_LENGTH=64 CHECKSUM=1 DELAY_KEY_WRITE=1 KEY_BLOCK_SIZE=8 MAX_ROWS=1000 MIN_ROWS=1 PACK_KEYS=DEFAULT STATS_AUTO_RECALC=DEFAULT STATS_PERSISTENT=1 STATS_SAMPLE_PAGES=16 | 已覆盖 |
| `VM101` | `vastbase-mysql-create-table-string-and-storage-table-options` | CREATE TABLE `users` (`id` INT) COMMENT='users table' COMPRESSION='ZLIB' CONNECTION='connect_string' DATA DIRECTORY='/tmp/data' INDEX DIRECTORY='/tmp/index' ENCRYPTION='Y' ENGINE_ATTRIBUTE='{"tier":"hot"}' SECONDARY_ENGINE_ATTRIBUTE='{}' INSERT_METHOD=NO PASSWORD='legacy' ROW_FORMAT=COMPRESSED TABLESPACE=innodb_file_per_table | 已覆盖 |
| `VM102` | `vastbase-mysql-create-table-partition-options` | CREATE TABLE `users` (`id` INT, `created_at` DATE) ENGINE=InnoDB PARTITION BY HASH(`id`) PARTITIONS 4 | 已覆盖 |
| `VM103` | `vastbase-mysql-create-temporary-table-options` | CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_users` (`id` INT VISIBLE, `token` VARCHAR(64) COMMENT 'session token') ENGINE=MEMORY DEFAULT CHARACTER SET=utf8mb4 | 已覆盖 |
| `VM104-VM106` | INSERT 扩展 | ON DUPLICATE KEY UPDATE、row alias、INSERT SET | 已覆盖 |
| `VM106A` | `vastbase-mysql-insert-set-paired-column-patch` | `INSERT ... SET` 成对字段和值 | `insert_columns` 原子插入和删除同位字段值对 |
| `VM107-VM109` | DELETE/UPDATE 扩展 | 别名删除目标、ORDER BY、LIMIT | 已覆盖 |
| `VM110-VM115` | locking read、STRAIGHT_JOIN、index hint | 解析、通用 relation/field 图和公开 SQL 恢复 | 已覆盖 |
| `VM116-VM118` | index hint 位置和列表边界 | 逗号表列表、多索引、空 USE INDEX | 已覆盖 |
| `VM119-VM121` | JOIN 语义 | 嵌套 JOIN、USING、NATURAL | 已覆盖 |
| `VM122-VM125` | lock、DML bind、嵌套 STRAIGHT_JOIN | NOWAIT、SKIP LOCKED、ORDER/LIMIT bind 隔离 | 已覆盖 |
| `VM126` | row alias column list | ON DUPLICATE KEY UPDATE row alias 列来源 | 已覆盖 |
| `VM127` | CTE 内外 index hint | CTE 来源块、嵌套 relation 和提示恢复位置 | 已覆盖 |
| `VM128` | table partition + index hint | 限定表名、别名和公开 SQL 恢复 | 已覆盖 |
| `VM129-VM135` | 索引提示与查询尾部边界 | `HAVING`、`WINDOW`、集合运算和锁定子句 | 已覆盖 |
| `VM136-VM140` | 索引提示与 JOIN / 作用域组合 | `NATURAL JOIN`、`STRAIGHT_JOIN`、`USING`、别名和多作用域索引提示 | 已覆盖 |
| `VM141-VM145` | 索引提示的嵌套与标识符边界 | CTE、多语句、分区表、保留字别名和派生表 | 已覆盖 |
| `VM228-VM231` | UPDATE/DELETE 尾部结构回归 | 仅 ORDER BY、仅 LIMIT、DELETE bind 隔离、别名限定的多排序键 | 已覆盖 |
| `VM232` | `vastbase-mysql-cte-update-nested-order-limit-bind-isolation` | CTE、`USE INDEX FOR ORDER BY`、相关标量子查询排序及 assignment/WHERE/ORDER/LIMIT 混合 bind | index hint 与真实 DML 尾部边界、CTE 来源块、相关字段、前三个语义 bind 和 8 个 patch 均已精确覆盖；LIMIT bind 不进入 View |
| `VM233` | `vastbase-mysql-update-join-compound-on-where-or-bind-order` | 复合 `ON AND`、`WHERE OR` 及 ON/SET/WHERE 混合 bind | ON/WHERE 独立布尔根、bind 1/2/3/4 顺序及字段、关系、值、assignment patch 均精确覆盖 |
| `VM234` | `vastbase-mysql-delete-left-join-where-three-and` | `LEFT JOIN` + 三项 `WHERE AND` | ON 比较与 WHERE AND 根独立，三个 WHERE 子节点及字段、值、关系 patch 均精确覆盖 |
| `VM235` | `vastbase-mysql-delete-right-join-compound-on-no-where` | `RIGHT JOIN` + 复合 ON，无 WHERE | 删除目标、反向 relation、ON AND 根和 bind 归属均精确覆盖，不生成虚假 WHERE |
| `VM236` | `vastbase-mysql-update-join-where-or-and-precedence` | 未加外层括号的 `WHERE a OR b AND c` | WHERE OR/AND 优先级树与 ON 分离，assignment、字段、值、关系 patch 均保持该结构 |
| `VM237` | `vastbase-mysql-update-join-user-wrapper-name-where` | WHERE 中调用与内部 marker 同名的普通函数 | 用户函数保持 WHERE expression predicate，仅内部 wrapper 归入 ON；字段和 assignment patch 精确覆盖 |
| `VM238` | `vastbase-mysql-named-window-partition-order-independent-selectors` | 命名窗口同时包含 `PARTITION BY` 与 `ORDER BY` | 定义字段分别归类为 `window_partition` 和 `order_by`；同名 SELECT 字段与窗口分区字段具有独立 selector，并通过逐项 patch 验证 |
| `VM239` | `vastbase-mysql-named-window-reused-multiple-order-fields` | 两个窗口函数复用同一命名窗口，窗口定义包含两个排序字段 | 命名窗口定义只遍历一次，两个排序字段各自进入 Query Graph 并可独立 patch |
| `VM240` | `vastbase-mysql-named-window-inheritance-partition-order` | 基础窗口定义分区，派生窗口继承后补充排序 | 两个窗口定义按物理定义顺序遍历，分区字段与继承窗口排序字段保持独立归属和 selector |
| `VM241` | `vastbase-mysql-named-window-frame-and-query-order` | 命名窗口分区、排序、ROWS frame 与查询级 `ORDER BY` 组合 | 窗口排序和查询排序各自定位；frame 原文、View 语义及全部 patch 结果精确验证 |
| `VM247` | `vastbase-mysql-derived-mixed-limit-surfaces` | 派生查询使用 `LIMIT offset, count`，外层查询使用 `LIMIT count OFFSET offset` | 两层 LIMIT 的原始表面形式分别保留；内外层 relation、target 及外层 target 插入 patch 均精确反解析 |
| `VM248` | `vastbase-mysql-union-result-limit-offset` | `UNION ALL` 结果使用参数化 `LIMIT count OFFSET offset` | 集合根及两个分支的 View 归属保持正确；relation 与两侧 target patch 后仍逐字节保留 OFFSET 形式 |
| `VM249` | `vastbase-mysql-limit-comma-comment-trivia` | `LIMIT offset, count` 三个 token 间隙穿插普通块注释 | 原始 SQL 可解析且 generation-0 逐字节保留；target patch 的普通注释回放保持正确期望，并纳入 RG016 闭环 |
| `VM250` | `vastbase-mysql-string-quote-escape-surfaces` | 单引号与双引号字符串混用反斜杠、重复引号转义 | View 中四个等价字符串值及 selector 逐项准确；原始反解析和 relation、target、value、insert patch 后未修改字面量均逐字节保留 |
| `VM251` | `vastbase-mysql-string-common-backslash-escapes` | 换行、制表符和反斜杠的常用字符串转义 | View 保存解码后的字符串语义；原始转义拼写及 patch fragment 的引号、national 前缀和反斜杠逐字节保留 |
| `VM252` | `vastbase-mysql-string-equal-value-surfaces` | 普通字符串、`n` 与 `N` 前缀字符串具有相同值 | View 中三个 value 独立定位；按 AST owner 保留各自表面拼写，替换或插入单个节点不会串用其他节点的拼写 |
| `VM253` | `vastbase-mysql-string-nested-surface-owners` | 外层双引号字符串、内层 `n` 字符串和 WHERE 转义字符串 | 嵌套 block、relation、field、target 和 value 归属完整；跨层 patch 后所有未修改字符串仍逐字节保留 |
| `VM254` | `vastbase-mysql-merge-insert-structured-pair-rewrite` | MERGE INSERT 目标列与 VALUES cell 结构化改写 | 明确目标列、来源字段与表达式 cell 的独立定位，验证反引号目标列、完整 cell、列值对成对插入和删除 |
| `VM255` | `vastbase-mysql-update-multiple-target-inner-join` | 两表 `INNER JOIN`、交错双目标 assignment | assignment relation 归属及插入、替换、删除 patch |
| `VM256` | `vastbase-mysql-update-multiple-target-three-table-bind-order` | 三表 JOIN、三目标 assignment、12 个 `?` | ON/SET/WHERE occurrence 顺序及 patch 后重编号 |
| `VM257` | `vastbase-mysql-update-multiple-target-four-relation-comma-list` | 四 relation 逗号列表、三目标 assignment | 逗号 relation、目标字段归属及 assignment patch |
| `VM258` | `vastbase-mysql-update-multiple-target-four-table-mixed-join` | 四表 INNER/LEFT 混合 JOIN、四目标 assignment | JOIN 链、逐 assignment relation 和末项替换 |
| `VM259` | `vastbase-mysql-update-multiple-target-quoted-identifiers` | schema-qualified 反引号对象与双目标 assignment | quoted relation/field 归属及 relation、assignment patch |
| `VMU001` | `vastbase-mysql-insert-ignore` | INSERT IGNORE INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU002` | `vastbase-mysql-insert-delayed` | INSERT DELAYED INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU003` | `vastbase-mysql-insert-low-priority` | INSERT LOW_PRIORITY INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU004` | `vastbase-mysql-insert-high-priority` | INSERT HIGH_PRIORITY INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU004A` | `vastbase-mysql-insert-low-priority-ignore` | INSERT LOW_PRIORITY IGNORE INTO `users` (`id`, `phone`) VALUES (?, ?) | 已覆盖 |
| `VMU004B` | `vastbase-mysql-insert-high-priority-ignore-select` | INSERT HIGH_PRIORITY IGNORE INTO `users` (`id`) SELECT `id` FROM `backup_users` | 已覆盖 |
| `VMU004C` | `vastbase-mysql-insert-ignore-on-duplicate-key` | INSERT IGNORE INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | 已覆盖 |
| `VMU005` | `vastbase-mysql-on-duplicate-key` | INSERT INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | 已覆盖 |
| `VMU005A` | `vastbase-mysql-on-duplicate-key-assignment-patch` | 双赋值 `ON DUPLICATE KEY UPDATE` | 根 `assignment[A]` 按序定位；插入、整项替换和删除 3 个 patch 保持兼容语法 |
| `VMU006` | `vastbase-mysql-replace-into` | REPLACE INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU006A` | `vastbase-mysql-replace-low-priority-multi-row` | REPLACE LOW_PRIORITY INTO `users` (`id`, `phone`) VALUES (?, ?), (?, ?) | 已覆盖 |
| `VMU006B` | `vastbase-mysql-replace-delayed-select` | REPLACE DELAYED INTO `users` (`id`, `phone`) SELECT `id`, `phone` FROM `backup_users` WHERE `active` = ? | 已覆盖 |
| `VMU006C` | `vastbase-mysql-replace-set` | REPLACE INTO `users` SET `id` = ?, `phone` = ? | 已覆盖 |
| `VMU006D` | `vastbase-mysql-replace-without-into` | REPLACE `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU006E` | `vastbase-mysql-replace-table-source` | REPLACE INTO `users` (`id`, `phone`) TABLE `backup_users` | 已覆盖 |
| `VMU007` | `vastbase-mysql-update-ignore` | UPDATE IGNORE `users` SET `id` = ? WHERE `id` = ? | 已覆盖 |
| `VMU008` | `vastbase-mysql-delete-ignore` | DELETE IGNORE FROM `users` WHERE `id` = ? | 已覆盖 |
| `VMU008A` | `vastbase-mysql-update-low-priority-ignore-join` | UPDATE LOW_PRIORITY IGNORE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | 已覆盖 |
| `VMU008B` | `vastbase-mysql-delete-low-priority-quick-ignore-join` | DELETE LOW_PRIORITY QUICK IGNORE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | 已覆盖 |
| `VMU009` | `vastbase-mysql-update-join` | UPDATE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | 已覆盖 |
| `VMU010` | `vastbase-mysql-delete-join` | DELETE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | 已覆盖 |
| `VMU010A` | `vastbase-mysql-update-join-on-bind` | UPDATE users u JOIN orders o ON u.phone = ? SET u.name = ? WHERE o.id = ? | 已覆盖 |
| `VMU010B` | `vastbase-mysql-delete-join-on-bind` | DELETE u FROM users u JOIN orders o ON u.phone = ? WHERE o.id = ? | 已覆盖 |
| `VMU011` | `vastbase-mysql-auto-increment` | CREATE TABLE `users` (`id` INT AUTO_INCREMENT) | 已覆盖 |
| `VMU012` | `vastbase-mysql-unsigned` | CREATE TABLE `users` (`id` INT UNSIGNED) | 已覆盖 |
| `VMU013` | `vastbase-mysql-zerofill` | CREATE TABLE `users` (`id` INT ZEROFILL) | 已覆盖 |
| `VMU014` | `vastbase-mysql-table-engine` | CREATE TABLE `users` (`id` INT) ENGINE=InnoDB | 已覆盖 |
| `VMU015` | `vastbase-mysql-table-charset` | CREATE TABLE `users` (`id` INT) DEFAULT CHARSET=utf8mb4 | 已覆盖 |
| `VMU016` | `vastbase-mysql-table-character-set` | CREATE TABLE `users` (`id` INT) CHARACTER SET=utf8mb4 | 已覆盖 |
| `VMU017` | `vastbase-mysql-table-collate` | CREATE TABLE `users` (`id` INT) COLLATE=utf8mb4_bin | 已覆盖 |
| `VMU018` | `vastbase-mysql-update-left-join` | UPDATE users u LEFT JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | 已覆盖 |
| `VMU019` | `vastbase-mysql-delete-left-join` | DELETE u FROM users u LEFT JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | 已覆盖 |
| `VMU020` | `vastbase-mysql-update-join-source-assignment` | UPDATE users u JOIN orders o ON u.id=o.user_id SET o.shipping_phone = ? WHERE u.id = ? | 已覆盖 |
| `VMU021` | `vastbase-mysql-delete-join-source-target` | DELETE o FROM users u JOIN orders o ON u.id=o.user_id WHERE o.phone = ? | 已覆盖 |

`VM255` 至 `VM259` 定义项目 `vastbase-mysql` 入口的多目标多表 UPDATE 合同；多表形态不接受 `ORDER BY` 或 `LIMIT`。该矩阵不声称 Vastbase 服务端官网定义了相同语法范围。

## INSERT VALUES 回归：bind 与表达式混合

以下 10 条用例覆盖 Vastbase B/MySQL 兼容模式下的 prepared statement、驱动 SQL 模板、多行 VALUES、复合表达式和反引号标识符。数据库侧执行 `?` 参数模板时还要求启用 `vb_enable_bcompat_mode`。

未加引号的 `?` 只按 prepared statement 或驱动 SQL 模板中的数据值参数处理，不能替代表名、列名或关键字；含该参数标记的 SQL 必须经过 prepare/bind 流程执行。`DEFAULT` 只作为独立 cell。表达式 cell 保持 `kind=expression`，其内部 `?` 仍计入全局 bind 序号，后续直接 bind 的 `bind_position` 用于验证表达式内 bind 已纳入全局计数；时间函数名不得出现在 `query_graph.fields[].column` 中。

| ID | 用例 | VALUES 形态 | 验证重点 |
| --- | --- | --- | --- |
| `VM-BM001` | `vastbase-mysql-insert-bind-mixed-bare-time` | 三个直接 `?` + `NOW()` | 直接 bind cell 的 key、kind、SQL、位置、selector 及尾部时间表达式 |
| `VM-BM002` | `vastbase-mysql-insert-bind-mixed-expression-first` | `CONVERT(?, BIGINT)`、`?`、`CURRENT_TIMESTAMP`、`?` | 首位表达式、嵌套 bind 全局序号和后续直接 bind |
| `VM-BM003` | `vastbase-mysql-insert-bind-mixed-interleaved-functions` | `?`、`NOW()`、`?`、`COALESCE(?, 'fallback')`、`?` | bind 与表达式交错、`NOW` 不出现在 `query_graph.fields[].column` 中、嵌套 bind 计数 |
| `VM-BM004` | `vastbase-mysql-insert-bind-mixed-null` | `?`、`NULL`、`CONVERT(?, BIGINT)`、`?` | NULL literal、表达式和直接 bind cell 独立分类 |
| `VM-BM005` | `vastbase-mysql-insert-bind-mixed-default` | `?`、`DEFAULT`、`CONVERT(?, BIGINT)`、`?` | 独立 DEFAULT cell 和嵌套 bind 全局序号 |
| `VM-BM006` | `vastbase-mysql-insert-bind-mixed-literal` | `?`、字符串 literal、包含 `?` 的 `CASE` 表达式、`?` | 字符串 literal、CASE 表达式和后续 bind 位置 |
| `VM-BM007` | `vastbase-mysql-insert-bind-mixed-coalesce-time` | `?`、`COALESCE(?, 'fallback')`、`CURRENT_TIMESTAMP`、`?` | COALESCE 内部 bind、独立时间表达式和直接 bind |
| `VM-BM008` | `vastbase-mysql-insert-bind-mixed-case-time` | `?`、包含 `?` 的 `CASE` 表达式、`NOW()`、`?` | CASE 表达式内 bind、独立时间表达式和直接 bind |
| `VM-BM009` | `vastbase-mysql-insert-bind-mixed-three-rows` | 三行中交错 bind、CONVERT/COALESCE/CASE 表达式和时间表达式 | 逐 cell selector 及跨行连续的全局 bind 序号 |
| `VM-BM010` | `vastbase-mysql-insert-bind-mixed-quoted-irregular-whitespace` | schema-qualified 反引号标识符、不规则空白、三个直接 `?` + 时间表达式 | quoted identifier、逐字节保留输入 SQL，并与期望 cell 对象逐字段一致 |
