# Vastbase MySQL 兼容模式用例矩阵

可执行夹具：`tests/cases/vastbase_mysql_dialect_input.json`。单元测试会逐条验证解析、View JSON、反解析输出和明确不支持语法返回码。

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
| `VMU001` | `vastbase-mysql-insert-ignore` | INSERT IGNORE INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU002` | `vastbase-mysql-insert-delayed` | INSERT DELAYED INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU003` | `vastbase-mysql-insert-low-priority` | INSERT LOW_PRIORITY INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU004` | `vastbase-mysql-insert-high-priority` | INSERT HIGH_PRIORITY INTO `users` (`id`) VALUES (1) | 已覆盖 |
| `VMU004A` | `vastbase-mysql-insert-low-priority-ignore` | INSERT LOW_PRIORITY IGNORE INTO `users` (`id`, `phone`) VALUES (?, ?) | 已覆盖 |
| `VMU004B` | `vastbase-mysql-insert-high-priority-ignore-select` | INSERT HIGH_PRIORITY IGNORE INTO `users` (`id`) SELECT `id` FROM `backup_users` | 已覆盖 |
| `VMU004C` | `vastbase-mysql-insert-ignore-on-duplicate-key` | INSERT IGNORE INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | 已覆盖 |
| `VMU005` | `vastbase-mysql-on-duplicate-key` | INSERT INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | 已覆盖 |
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
