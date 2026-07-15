# Vastbase MySQL Compatibility Case Matrix

Executable fixture: `tests/cases/vastbase_mysql_dialect_input.json`. The unit test verifies parsing, View JSON, deparse output, and explicitly unsupported syntax return codes case by case.

| ID | Case | SQL | Status |
| --- | --- | --- | --- |
| `VM001` | `vastbase-mysql-select-limit-comma` | SELECT `u`.`id`, "hello" AS `label` FROM `users` AS `u` WHERE `u`.`id` = 1 LIMIT 5, 10 | covered |
| `VM002` | `vastbase-mysql-select-join` | SELECT `u`.`id`, `u`.`name`, `o`.`order_no` FROM `users` `u` JOIN `orders` `o` ON `u`.`id` = `o`.`user_id` WHERE `o`.`status` = "paid" | covered |
| `VM003` | `vastbase-mysql-hash-comment` | SELECT 1 # mysql line comment<br> | covered |
| `VM004` | `vastbase-mysql-insert-values-multi-row` | INSERT INTO `users` (`id`, `name`) VALUES (1, "bob"), (2, 'alice') | covered |
| `VM004N` | `vastbase-mysql-national-string-literal` | SELECT "prefix" AS prefix_value, N'Alice''s order' AS label FROM `users` WHERE `name` = n'Bob' | covered |
| `VM004NA` | `vastbase-mysql-national-string-duplicate-literal` | SELECT 'same' AS ascii_value, N'same' AS national_value FROM `users` | covered |
| `VM005` | `vastbase-mysql-insert-select` | INSERT INTO `archive_users` (`id`, `name`) SELECT `id`, `name` FROM `users` WHERE `status` = "inactive" | covered |
| `VM006` | `vastbase-mysql-update-basic` | UPDATE `users` SET `name` = "carol", `status` = 'active' WHERE `id` = 1 | covered |
| `VM007` | `vastbase-mysql-delete-conditional` | DELETE FROM `users` WHERE `id` = 1 AND `status` = "inactive" | covered |
| `VM008` | `vastbase-mysql-create-table-basic` | CREATE TABLE `users` (`id` INT, `name` VARCHAR(64)) | covered |
| `VM009` | `vastbase-mysql-alter-table-add-column` | ALTER TABLE `users` ADD COLUMN `age` INT | covered |
| `VM010` | `vastbase-mysql-create-view` | CREATE VIEW `v_users` AS SELECT `id`, `name` FROM `users` | covered |
| `VM011` | `vastbase-mysql-drop-table` | DROP TABLE `users` | covered |
| `VM012` | `vastbase-mysql-start-transaction` | START TRANSACTION; COMMIT | covered |
| `VM013` | `vastbase-mysql-unsupported-keywords-in-string` | SELECT 'INSERT IGNORE' AS msg FROM `users` | covered |
| `VM014` | `vastbase-mysql-unsupported-keywords-in-comment` | SELECT `id` FROM `users` /* ON DUPLICATE KEY UPDATE */ WHERE `id` = 1 | covered |
| `VM014Q` | `vastbase-mysql-unsupported-keywords-in-quoted-identifiers` | SELECT `unsigned`, `auto_increment`, `engine` FROM `users` | covered |
| `VM015` | `vastbase-mysql-use-database` | USE analytics | covered |
| `VM016` | `vastbase-mysql-use-quoted-database` | USE `analytics-prod` | covered |
| `VM017` | `vastbase-mysql-use-database-in-multi-statement` | USE `analytics-prod`; SELECT * FROM `users` | covered |
| `VM018` | `vastbase-mysql-insert-question-params` | INSERT INTO users (username, email, age) VALUES (?, ?, ?) | covered |
| `VM019` | `vastbase-mysql-update-question-params` | UPDATE users SET email = ? WHERE username = ? | covered |
| `VM020` | `vastbase-mysql-prepare-from-literal` | PREPARE stmt FROM 'SELECT * FROM users WHERE id = ?' | covered |
| `VM021` | `vastbase-mysql-execute-using` | EXECUTE stmt USING @user_id | covered |
| `VM022` | `vastbase-mysql-deallocate-prepare` | DEALLOCATE PREPARE stmt | covered |
| `VM023` | `vastbase-mysql-drop-prepare` | DROP PREPARE stmt | covered |
| `VM024` | `vastbase-mysql-select-question-params` | SELECT id, name FROM users WHERE id = ? AND status = ? | covered |
| `VM025` | `vastbase-mysql-select-in-question-params` | SELECT id FROM users WHERE status IN (?, ?, ?) | covered |
| `VM026` | `vastbase-mysql-select-limit-question-params` | SELECT id FROM users WHERE name LIKE ? ORDER BY id LIMIT ? OFFSET ? | covered |
| `VM027` | `vastbase-mysql-insert-named-columns-question-params` | INSERT INTO users (id, name, status) VALUES (?, ?, ?) | covered |
| `VM028` | `vastbase-mysql-insert-multi-row-question-params` | INSERT INTO users (id, name) VALUES (?, ?), (?, ?) | covered |
| `VM029` | `vastbase-mysql-update-multi-question-params` | UPDATE users SET name = ?, status = ? WHERE id = ? | covered |
| `VM030` | `vastbase-mysql-delete-question-params` | DELETE FROM users WHERE id = ? AND status = ? | covered |
| `VM031` | `vastbase-mysql-prepare-insert-literal` | PREPARE stmt FROM 'INSERT INTO users (id, name) VALUES (?, ?)' | covered |
| `VM032` | `vastbase-mysql-prepare-from-user-variable` | PREPARE stmt FROM @sql_text | covered |
| `VM033` | `vastbase-mysql-execute-using-multiple-vars` | EXECUTE stmt USING @id, @name | covered |
| `VM034` | `vastbase-mysql-view-concat-function` | SELECT CONCAT(first_name, last_name) FROM users WHERE id = ? | covered |
| `VM035` | `vastbase-mysql-view-case-expression` | SELECT CASE WHEN state = 1 THEN name ELSE fallback_name END FROM users | covered |
| `VM035A` | `vastbase-mysql-view-case-predicate-bind` | SELECT CASE WHEN phone = ? THEN name ELSE email END AS v FROM users | covered |
| `VM036` | `vastbase-mysql-view-group-having-order` | SELECT dept, COUNT(id) FROM users GROUP BY dept HAVING COUNT(id) > 1 ORDER BY dept | covered |
| `VM037` | `vastbase-mysql-view-update-question-binds` | UPDATE users SET ip = ? WHERE id = ? | covered |
| `VM038` | `vastbase-mysql-view-join-on` | SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = ? | covered |
| `VM039` | `vastbase-mysql-select-between-question-params` | SELECT id FROM users WHERE age BETWEEN ? AND ? | covered |
| `VM040` | `vastbase-mysql-select-not-in-question-params` | SELECT id FROM users WHERE status NOT IN (?, ?) | covered |
| `VM041` | `vastbase-mysql-select-not-between-question-params` | SELECT id FROM users WHERE age NOT BETWEEN ? AND ? | covered |
| `VM042` | `vastbase-mysql-select-not-like-question-param` | SELECT id FROM users WHERE name NOT LIKE ? | covered |
| `VM043` | `vastbase-mysql-select-distinct-like-param` | SELECT DISTINCT `name` FROM `table1` WHERE `name` LIKE ? | covered |
| `VM044` | `vastbase-mysql-select-left-join-alias-star` | SELECT u.*, o.`order_no` FROM `users` u LEFT JOIN `orders` o ON u.`id` = o.`user_id` WHERE o.`status` = ? | covered |
| `VM045` | `vastbase-mysql-delete-in-question-params` | DELETE FROM `users` WHERE `email` IN (?, ?) | covered |
| `VM046` | `vastbase-mysql-update-in-question-params` | UPDATE `users` SET `note` = ? WHERE `phone` IN (?, ?) | covered |
| `VM047` | `vastbase-mysql-select-derived-table-filter` | SELECT t.`id`, t.`phone` FROM (SELECT `id`, `phone` FROM `users` WHERE `status` = ?) t WHERE t.`phone` = ? | covered |
| `VM054` | `vastbase-mysql-select-derived-query-graph` | SELECT s.`inner_name` AS `outer_name` FROM (SELECT `id`, `name` AS `inner_name` FROM `users` WHERE `age` <= ?) s WHERE s.`inner_name` LIKE ? | covered |
| `VM048` | `vastbase-mysql-select-json-extract` | SELECT JSON_EXTRACT(`extra`, '$.phone') AS phone_json FROM `users` WHERE `id` = ? | covered |
| `VM049` | `vastbase-mysql-create-table-if-not-exists` | CREATE TABLE IF NOT EXISTS `users` (`id` INT, `name` VARCHAR(64), `phone` TEXT) | covered |
| `VM050` | `vastbase-mysql-drop-view-if-exists` | DROP VIEW IF EXISTS `user_view` | covered |
| `VM051` | `vastbase-mysql-select-order-by-ordinal` | SELECT `id`, `phone` FROM `users` ORDER BY 1 | covered |
| `VM052` | `vastbase-mysql-limit-comma-question-params` | SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ? | covered |
| `VM053` | `vastbase-mysql-multi-statement-global-bind-position` | UPDATE `users` SET `a` = ? WHERE `b` = ?; UPDATE `users` SET `c` = ? WHERE `d` = ? | covered |
| `VM055` | `vastbase-mysql-select-reference-002` | SELECT `id`, `name`, `age` FROM `users`; | covered |
| `VM056` | `vastbase-mysql-select-reference-003` | SELECT `id`, `name`, `age` FROM `viq-test`.`users`; | covered |
| `VM057` | `vastbase-mysql-select-reference-006` | SELECT u.`id` AS user_id, u.`name` AS user_name FROM `viq-test`.`users` u; | covered |
| `VM058` | `vastbase-mysql-select-reference-008` | SELECT * FROM `viq-test`.`products` WHERE `price` > 100 AND `category` = 'Electronics'; | covered |
| `VM059` | `vastbase-mysql-select-reference-010` | SELECT * FROM logs LIMIT 10; | covered |
| `VM060` | `vastbase-mysql-select-reference-012` | SELECT * FROM users WHERE id IN (SELECT id FROM users WHERE email = ?); | covered |
| `VM061` | `vastbase-mysql-select-reference-014` | SELECT a.id, a.name FROM (SELECT id, name FROM `viq-test`.`users` WHERE `active` = 1) a; | covered |
| `VM062` | `vastbase-mysql-select-reference-016` | SELECT id, name FROM (SELECT name, id, age FROM `viq-test`.`users`); | covered |
| `VM063` | `vastbase-mysql-select-reference-022` | SELECT b.* FROM (SELECT a.* FROM (SELECT id, name FROM `viq-test`.`users`) a WHERE id > 10) b WHERE name LIKE 'A%'; | covered |
| `VM064` | `vastbase-mysql-select-reference-023` | SELECT b.* FROM (SELECT a.* FROM (SELECT `id`, `name` FROM `viq-test`.`users`) a WHERE id > 10) b WHERE name LIKE 'A%'; | covered |
| `VM065` | `vastbase-mysql-select-reference-025` | select * from (select id as aa, department as cc, name as bb FROM (select e.*,rownum as row_num from `viq-test`.`employees_uuid` e where rownum <= 100) where row_num >= 1 and (id = 1 or id=2)) where aa = 1; | covered |
| `VM066` | `vastbase-mysql-select-reference-027` | select b.* from (select id as `aa`, `department` as cc, name as bb FROM (select e.*,rownum as row_num from `viq-test`.`employees_uuid` e where rownum <= 100) a where row_num >= 1 and (id = 1 or id=2)) b where aa = 1; | covered |
| `VM067` | `vastbase-mysql-select-reference-029` | SELECT * FROM (SELECT e.*, `ROWNUM` rn FROM `viq-test`.`employees_uuid` e WHERE `ROWNUM` <= 100) WHERE rn > 50; | covered |
| `VM068` | `vastbase-mysql-update-join-target-table-qualified` | UPDATE users JOIN orders o ON users.id=o.user_id SET users.phone = ? WHERE o.shipping_phone = ? | covered |
| `VM069` | `vastbase-mysql-field-match-kind-direct-and-expression` | SELECT id FROM users WHERE secret = ? AND UPPER(secret) = ? | covered |
| `VM070` | `vastbase-mysql-expression-field-case-expression-value` | SELECT id FROM users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = ? | covered |
| `VM071` | `vastbase-mysql-expression-field-multi-field-expression-value` | SELECT id FROM users WHERE CONCAT(secret, id) = ? AND secret + id = ? | covered |
| `VM072` | `vastbase-mysql-expression-field-value-side-expression` | SELECT id FROM users WHERE secret = UPPER(?) AND secret = CONCAT(?, 'x') AND secret = CAST(? AS CHAR) | covered |
| `VM073` | `vastbase-mysql-expression-field-dml-expression-values` | INSERT INTO users (id, secret) VALUES (1, UPPER(?)); UPDATE users SET secret = CONCAT(?, 'x') WHERE id = 1 | covered |
| `VM074` | `vastbase-mysql-update-bind-rhs-crypto-source` | UPDATE `dbp_crypto_test` SET `secret` = ? WHERE `id` = ? | covered |
| `VM075` | `vastbase-mysql-update-multiple-bind-rhs-crypto-source` | UPDATE `dbp_crypto_test` SET `phone` = ?, `secret` = ? WHERE `id` = ? | covered |
| `VM076` | `vastbase-mysql-like-escape-literal` | SELECT `id` FROM `users` WHERE `name` LIKE 'A!_%' ESCAPE '!' | covered |
| `VM077` | `vastbase-mysql-like-escape-question-binds` | SELECT `id` FROM `users` WHERE `name` LIKE ? ESCAPE ? | covered |
| `VM078` | `vastbase-mysql-not-like-escape-literal` | SELECT `id` FROM `users` WHERE `name` NOT LIKE ? ESCAPE '!' | covered |
| `VM079` | `vastbase-mysql-like-without-explicit-escape` | SELECT `id` FROM `users` WHERE `name` LIKE ? | covered |
| `VM089` | `vastbase-mysql-update-join-source-field-graph` | UPDATE users u JOIN orders o ON u.id=o.user_id SET u.name = o.customer_name WHERE o.active = ? | covered |
| `VM090` | `vastbase-mysql-insert-select-source-block-graph` | INSERT INTO users_archive (id, email) SELECT u.id, u.email FROM users u WHERE u.active = ? | covered |
| `VM091` | `vastbase-mysql-with-cte-select` | WITH cte AS (SELECT id FROM users WHERE active = ?) SELECT id FROM cte | covered |
| `VM092` | `vastbase-mysql-window-row-number` | SELECT id, ROW_NUMBER() OVER (PARTITION BY status ORDER BY id) AS rn FROM users | covered |
| `VM093` | `vastbase-mysql-common-scalar-functions` | SELECT LOWER(name) AS lower_name, COALESCE(email, 'n/a') AS email_value FROM users WHERE id = ? | covered |
| `VM094` | `vastbase-mysql-common-data-types` | CREATE TABLE users (id BIGINT, amount DECIMAL(10,2), created_at DATETIME, active BOOLEAN) | covered |
| `VM095` | `vastbase-mysql-rollback` | ROLLBACK | covered |
| `VM096` | `vastbase-mysql-json-contains-function-predicate` | SELECT * FROM users WHERE JSON_CONTAINS(tags, ?) | covered |
| `VM097` | `vastbase-mysql-select-alias-order-by-lineage` | SELECT u.email AS e FROM users u ORDER BY u.email | covered |
| `VM098` | `vastbase-mysql-create-table-column-and-table-options` | CREATE TABLE `users` (`id` INT UNSIGNED AUTO_INCREMENT, `score` INT ZEROFILL, `name` VARCHAR(64)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin | covered |
| `VM099` | `vastbase-mysql-create-table-official-column-attributes` | CREATE TABLE `users` (`id` INT UNSIGNED AUTO_INCREMENT PRIMARY KEY COMMENT 'pk', `name` VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin INVISIBLE, `score` INT GENERATED ALWAYS AS (`id` + 1) STORED, `parent_id` INT REFERENCES `parents` (`id`) ON DELETE CASCADE) | covered |
| `VM100` | `vastbase-mysql-create-table-numeric-table-options` | CREATE TABLE `users` (`id` INT) AUTO_INCREMENT=100 AVG_ROW_LENGTH=64 CHECKSUM=1 DELAY_KEY_WRITE=1 KEY_BLOCK_SIZE=8 MAX_ROWS=1000 MIN_ROWS=1 PACK_KEYS=DEFAULT STATS_AUTO_RECALC=DEFAULT STATS_PERSISTENT=1 STATS_SAMPLE_PAGES=16 | covered |
| `VM101` | `vastbase-mysql-create-table-string-and-storage-table-options` | CREATE TABLE `users` (`id` INT) COMMENT='users table' COMPRESSION='ZLIB' CONNECTION='connect_string' DATA DIRECTORY='/tmp/data' INDEX DIRECTORY='/tmp/index' ENCRYPTION='Y' ENGINE_ATTRIBUTE='{"tier":"hot"}' SECONDARY_ENGINE_ATTRIBUTE='{}' INSERT_METHOD=NO PASSWORD='legacy' ROW_FORMAT=COMPRESSED TABLESPACE=innodb_file_per_table | covered |
| `VM102` | `vastbase-mysql-create-table-partition-options` | CREATE TABLE `users` (`id` INT, `created_at` DATE) ENGINE=InnoDB PARTITION BY HASH(`id`) PARTITIONS 4 | covered |
| `VM103` | `vastbase-mysql-create-temporary-table-options` | CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_users` (`id` INT VISIBLE, `token` VARCHAR(64) COMMENT 'session token') ENGINE=MEMORY DEFAULT CHARACTER SET=utf8mb4 | covered |
| `VM104-VM106` | INSERT extensions | ON DUPLICATE KEY UPDATE, row aliases, and INSERT SET | covered |
| `VM107-VM109` | DELETE/UPDATE extensions | aliased delete targets, ORDER BY, and LIMIT | covered |
| `VM110-VM115` | locking reads, STRAIGHT_JOIN, and index hints | parsing, common relation/field graph, and public SQL restoration | covered |
| `VM116-VM118` | index-hint positions and list boundaries | comma table lists, multiple indexes, and empty USE INDEX | covered |
| `VM119-VM121` | JOIN semantics | nested JOIN, USING, and NATURAL | covered |
| `VM122-VM125` | locks, DML binds, and nested STRAIGHT_JOIN | NOWAIT, SKIP LOCKED, ORDER/LIMIT bind isolation | covered |
| `VM126` | row-alias column list | ON DUPLICATE KEY UPDATE row-alias column sources | covered |
| `VM127` | index hints inside and outside a CTE | CTE source blocks, nested relations, and hint restoration positions | covered |
| `VM128` | table partition with index hints | qualified tables, aliases, and public SQL restoration | covered |
| `VM129-VM135` | index hints at query-tail boundaries | `HAVING`, `WINDOW`, set operations, and locking clauses | covered |
| `VM136-VM140` | index hints with JOIN and scope combinations | `NATURAL JOIN`, `STRAIGHT_JOIN`, `USING`, aliases, and multiple scoped hints | covered |
| `VM141-VM145` | nested and identifier boundaries for index hints | CTEs, multiple statements, partitioned tables, reserved-word aliases, and derived tables | covered |
| `VMU001` | `vastbase-mysql-insert-ignore` | INSERT IGNORE INTO `users` (`id`) VALUES (1) | covered |
| `VMU002` | `vastbase-mysql-insert-delayed` | INSERT DELAYED INTO `users` (`id`) VALUES (1) | covered |
| `VMU003` | `vastbase-mysql-insert-low-priority` | INSERT LOW_PRIORITY INTO `users` (`id`) VALUES (1) | covered |
| `VMU004` | `vastbase-mysql-insert-high-priority` | INSERT HIGH_PRIORITY INTO `users` (`id`) VALUES (1) | covered |
| `VMU004A` | `vastbase-mysql-insert-low-priority-ignore` | INSERT LOW_PRIORITY IGNORE INTO `users` (`id`, `phone`) VALUES (?, ?) | covered |
| `VMU004B` | `vastbase-mysql-insert-high-priority-ignore-select` | INSERT HIGH_PRIORITY IGNORE INTO `users` (`id`) SELECT `id` FROM `backup_users` | covered |
| `VMU004C` | `vastbase-mysql-insert-ignore-on-duplicate-key` | INSERT IGNORE INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | covered |
| `VMU005` | `vastbase-mysql-on-duplicate-key` | INSERT INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | covered |
| `VMU006` | `vastbase-mysql-replace-into` | REPLACE INTO `users` (`id`) VALUES (1) | covered |
| `VMU006A` | `vastbase-mysql-replace-low-priority-multi-row` | REPLACE LOW_PRIORITY INTO `users` (`id`, `phone`) VALUES (?, ?), (?, ?) | covered |
| `VMU006B` | `vastbase-mysql-replace-delayed-select` | REPLACE DELAYED INTO `users` (`id`, `phone`) SELECT `id`, `phone` FROM `backup_users` WHERE `active` = ? | covered |
| `VMU006C` | `vastbase-mysql-replace-set` | REPLACE INTO `users` SET `id` = ?, `phone` = ? | covered |
| `VMU006D` | `vastbase-mysql-replace-without-into` | REPLACE `users` (`id`) VALUES (1) | covered |
| `VMU006E` | `vastbase-mysql-replace-table-source` | REPLACE INTO `users` (`id`, `phone`) TABLE `backup_users` | covered |
| `VMU007` | `vastbase-mysql-update-ignore` | UPDATE IGNORE `users` SET `id` = ? WHERE `id` = ? | covered |
| `VMU008` | `vastbase-mysql-delete-ignore` | DELETE IGNORE FROM `users` WHERE `id` = ? | covered |
| `VMU008A` | `vastbase-mysql-update-low-priority-ignore-join` | UPDATE LOW_PRIORITY IGNORE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | covered |
| `VMU008B` | `vastbase-mysql-delete-low-priority-quick-ignore-join` | DELETE LOW_PRIORITY QUICK IGNORE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | covered |
| `VMU009` | `vastbase-mysql-update-join` | UPDATE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | covered |
| `VMU010` | `vastbase-mysql-delete-join` | DELETE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | covered |
| `VMU010A` | `vastbase-mysql-update-join-on-bind` | UPDATE users u JOIN orders o ON u.phone = ? SET u.name = ? WHERE o.id = ? | covered |
| `VMU010B` | `vastbase-mysql-delete-join-on-bind` | DELETE u FROM users u JOIN orders o ON u.phone = ? WHERE o.id = ? | covered |
| `VMU011` | `vastbase-mysql-auto-increment` | CREATE TABLE `users` (`id` INT AUTO_INCREMENT) | covered |
| `VMU012` | `vastbase-mysql-unsigned` | CREATE TABLE `users` (`id` INT UNSIGNED) | covered |
| `VMU013` | `vastbase-mysql-zerofill` | CREATE TABLE `users` (`id` INT ZEROFILL) | covered |
| `VMU014` | `vastbase-mysql-table-engine` | CREATE TABLE `users` (`id` INT) ENGINE=InnoDB | covered |
| `VMU015` | `vastbase-mysql-table-charset` | CREATE TABLE `users` (`id` INT) DEFAULT CHARSET=utf8mb4 | covered |
| `VMU016` | `vastbase-mysql-table-character-set` | CREATE TABLE `users` (`id` INT) CHARACTER SET=utf8mb4 | covered |
| `VMU017` | `vastbase-mysql-table-collate` | CREATE TABLE `users` (`id` INT) COLLATE=utf8mb4_bin | covered |
| `VMU018` | `vastbase-mysql-update-left-join` | UPDATE users u LEFT JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | covered |
| `VMU019` | `vastbase-mysql-delete-left-join` | DELETE u FROM users u LEFT JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | covered |
| `VMU020` | `vastbase-mysql-update-join-source-assignment` | UPDATE users u JOIN orders o ON u.id=o.user_id SET o.shipping_phone = ? WHERE u.id = ? | covered |
| `VMU021` | `vastbase-mysql-delete-join-source-target` | DELETE o FROM users u JOIN orders o ON u.id=o.user_id WHERE o.phone = ? | covered |
