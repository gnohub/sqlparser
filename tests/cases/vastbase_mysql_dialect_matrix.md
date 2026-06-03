# Vastbase MySQL 兼容模式用例矩阵

可执行夹具：`tests/cases/vastbase_mysql_dialect_input.json`。单元测试会逐条验证解析、View JSON、反解析输出和明确不支持语法返回码。

| ID | 用例 | SQL | 状态 |
| --- | --- | --- | --- |
| `VM001` | `vastbase-mysql-select-limit-comma` | SELECT `u`.`id`, "hello" AS `label` FROM `users` AS `u` WHERE `u`.`id` = 1 LIMIT 5, 10 | 已覆盖 |
| `VM002` | `vastbase-mysql-select-join` | SELECT `u`.`id`, `u`.`name`, `o`.`order_no` FROM `users` `u` JOIN `orders` `o` ON `u`.`id` = `o`.`user_id` WHERE `o`.`status` = "paid" | 已覆盖 |
| `VM003` | `vastbase-mysql-hash-comment` | SELECT 1 # mysql line comment<br> | 已覆盖 |
| `VM004` | `vastbase-mysql-insert-values-multi-row` | INSERT INTO `users` (`id`, `name`) VALUES (1, "bob"), (2, 'alice') | 已覆盖 |
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
| `VMU001` | `vastbase-mysql-insert-ignore` | INSERT IGNORE INTO `users` (`id`) VALUES (1) | 明确不支持 |
| `VMU002` | `vastbase-mysql-insert-delayed` | INSERT DELAYED INTO `users` (`id`) VALUES (1) | 明确不支持 |
| `VMU003` | `vastbase-mysql-insert-low-priority` | INSERT LOW_PRIORITY INTO `users` (`id`) VALUES (1) | 明确不支持 |
| `VMU004` | `vastbase-mysql-insert-high-priority` | INSERT HIGH_PRIORITY INTO `users` (`id`) VALUES (1) | 明确不支持 |
| `VMU005` | `vastbase-mysql-on-duplicate-key` | INSERT INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | 已覆盖 |
| `VMU006` | `vastbase-mysql-replace-into` | REPLACE INTO `users` (`id`) VALUES (1) | 明确不支持 |
| `VMU007` | `vastbase-mysql-update-ignore` | UPDATE IGNORE `users` SET `id` = 2 WHERE `id` = 1 | 明确不支持 |
| `VMU008` | `vastbase-mysql-delete-ignore` | DELETE IGNORE FROM `users` WHERE `id` = 1 | 明确不支持 |
| `VMU009` | `vastbase-mysql-update-join` | UPDATE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | 已覆盖 |
| `VMU010` | `vastbase-mysql-delete-join` | DELETE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | 已覆盖 |
| `VMU010A` | `vastbase-mysql-update-join-on-bind` | UPDATE users u JOIN orders o ON u.phone = ? SET u.name = ? WHERE o.id = ? | 已覆盖 |
| `VMU010B` | `vastbase-mysql-delete-join-on-bind` | DELETE u FROM users u JOIN orders o ON u.phone = ? WHERE o.id = ? | 已覆盖 |
| `VMU011` | `vastbase-mysql-auto-increment` | CREATE TABLE `users` (`id` INT AUTO_INCREMENT) | 明确不支持 |
| `VMU012` | `vastbase-mysql-unsigned` | CREATE TABLE `users` (`id` INT UNSIGNED) | 明确不支持 |
| `VMU013` | `vastbase-mysql-zerofill` | CREATE TABLE `users` (`id` INT ZEROFILL) | 明确不支持 |
| `VMU014` | `vastbase-mysql-table-engine` | CREATE TABLE `users` (`id` INT) ENGINE=InnoDB | 明确不支持 |
| `VMU015` | `vastbase-mysql-table-charset` | CREATE TABLE `users` (`id` INT) DEFAULT CHARSET=utf8mb4 | 明确不支持 |
| `VMU016` | `vastbase-mysql-table-character-set` | CREATE TABLE `users` (`id` INT) CHARACTER SET=utf8mb4 | 明确不支持 |
| `VMU017` | `vastbase-mysql-table-collate` | CREATE TABLE `users` (`id` INT) COLLATE=utf8mb4_bin | 明确不支持 |
| `VMU018` | `vastbase-mysql-update-left-join` | UPDATE users u LEFT JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | 明确不支持 |
| `VMU019` | `vastbase-mysql-delete-left-join` | DELETE u FROM users u LEFT JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | 明确不支持 |
| `VMU020` | `vastbase-mysql-update-join-source-assignment` | UPDATE users u JOIN orders o ON u.id=o.user_id SET o.shipping_phone = ? WHERE u.id = ? | 明确不支持 |
| `VMU021` | `vastbase-mysql-delete-join-source-target` | DELETE o FROM users u JOIN orders o ON u.id=o.user_id WHERE o.phone = ? | 明确不支持 |
