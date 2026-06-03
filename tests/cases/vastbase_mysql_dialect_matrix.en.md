# Vastbase MySQL Compatibility Case Matrix

Executable fixture: `tests/cases/vastbase_mysql_dialect_input.json`. The unit test verifies parsing, View JSON, deparse output, and explicitly unsupported syntax return codes case by case.

| ID | Case | SQL | Status |
| --- | --- | --- | --- |
| `VM001` | `vastbase-mysql-select-limit-comma` | SELECT `u`.`id`, "hello" AS `label` FROM `users` AS `u` WHERE `u`.`id` = 1 LIMIT 5, 10 | covered |
| `VM002` | `vastbase-mysql-select-join` | SELECT `u`.`id`, `u`.`name`, `o`.`order_no` FROM `users` `u` JOIN `orders` `o` ON `u`.`id` = `o`.`user_id` WHERE `o`.`status` = "paid" | covered |
| `VM003` | `vastbase-mysql-hash-comment` | SELECT 1 # mysql line comment<br> | covered |
| `VM004` | `vastbase-mysql-insert-values-multi-row` | INSERT INTO `users` (`id`, `name`) VALUES (1, "bob"), (2, 'alice') | covered |
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
| `VMU001` | `vastbase-mysql-insert-ignore` | INSERT IGNORE INTO `users` (`id`) VALUES (1) | explicitly unsupported |
| `VMU002` | `vastbase-mysql-insert-delayed` | INSERT DELAYED INTO `users` (`id`) VALUES (1) | explicitly unsupported |
| `VMU003` | `vastbase-mysql-insert-low-priority` | INSERT LOW_PRIORITY INTO `users` (`id`) VALUES (1) | explicitly unsupported |
| `VMU004` | `vastbase-mysql-insert-high-priority` | INSERT HIGH_PRIORITY INTO `users` (`id`) VALUES (1) | explicitly unsupported |
| `VMU005` | `vastbase-mysql-on-duplicate-key` | INSERT INTO users(id, phone) VALUES (?, ?) ON DUPLICATE KEY UPDATE phone = ? | covered |
| `VMU006` | `vastbase-mysql-replace-into` | REPLACE INTO `users` (`id`) VALUES (1) | explicitly unsupported |
| `VMU007` | `vastbase-mysql-update-ignore` | UPDATE IGNORE `users` SET `id` = 2 WHERE `id` = 1 | explicitly unsupported |
| `VMU008` | `vastbase-mysql-delete-ignore` | DELETE IGNORE FROM `users` WHERE `id` = 1 | explicitly unsupported |
| `VMU009` | `vastbase-mysql-update-join` | UPDATE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | covered |
| `VMU010` | `vastbase-mysql-delete-join` | DELETE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | covered |
| `VMU010A` | `vastbase-mysql-update-join-on-bind` | UPDATE users u JOIN orders o ON u.phone = ? SET u.name = ? WHERE o.id = ? | covered |
| `VMU010B` | `vastbase-mysql-delete-join-on-bind` | DELETE u FROM users u JOIN orders o ON u.phone = ? WHERE o.id = ? | covered |
| `VMU011` | `vastbase-mysql-auto-increment` | CREATE TABLE `users` (`id` INT AUTO_INCREMENT) | explicitly unsupported |
| `VMU012` | `vastbase-mysql-unsigned` | CREATE TABLE `users` (`id` INT UNSIGNED) | explicitly unsupported |
| `VMU013` | `vastbase-mysql-zerofill` | CREATE TABLE `users` (`id` INT ZEROFILL) | explicitly unsupported |
| `VMU014` | `vastbase-mysql-table-engine` | CREATE TABLE `users` (`id` INT) ENGINE=InnoDB | explicitly unsupported |
| `VMU015` | `vastbase-mysql-table-charset` | CREATE TABLE `users` (`id` INT) DEFAULT CHARSET=utf8mb4 | explicitly unsupported |
| `VMU016` | `vastbase-mysql-table-character-set` | CREATE TABLE `users` (`id` INT) CHARACTER SET=utf8mb4 | explicitly unsupported |
| `VMU017` | `vastbase-mysql-table-collate` | CREATE TABLE `users` (`id` INT) COLLATE=utf8mb4_bin | explicitly unsupported |
| `VMU018` | `vastbase-mysql-update-left-join` | UPDATE users u LEFT JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ? | explicitly unsupported |
| `VMU019` | `vastbase-mysql-delete-left-join` | DELETE u FROM users u LEFT JOIN orders o ON u.id=o.user_id WHERE u.phone = ? | explicitly unsupported |
| `VMU020` | `vastbase-mysql-update-join-source-assignment` | UPDATE users u JOIN orders o ON u.id=o.user_id SET o.shipping_phone = ? WHERE u.id = ? | explicitly unsupported |
| `VMU021` | `vastbase-mysql-delete-join-source-target` | DELETE o FROM users u JOIN orders o ON u.id=o.user_id WHERE o.phone = ? | explicitly unsupported |
