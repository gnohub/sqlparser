# Vastbase SQL Server Compatibility Case Matrix

Executable fixture: `tests/cases/vastbase_sqlserver_dialect_input.json`. The unit test verifies parsing, View JSON, deparse output, and explicitly unsupported syntax return codes case by case.

| ID | Case | SQL | Status |
| --- | --- | --- | --- |
| `VS001` | `vastbase-sqlserver-select-bracket-param` | SELECT [u].[id], [u].[name] FROM [dbo].[users] AS [u] WHERE [u].[id] = @id | covered |
| `VS002` | `vastbase-sqlserver-select-top` | SELECT TOP (5) [id], [name] FROM [dbo].[users] ORDER BY [id] | covered |
| `VS003` | `vastbase-sqlserver-offset-fetch` | SELECT [id] FROM [dbo].[users] ORDER BY [id] OFFSET 10 ROWS FETCH NEXT 5 ROWS ONLY | covered |
| `VS004` | `vastbase-sqlserver-cte` | WITH active_users AS (SELECT [id], [name] FROM [dbo].[users] WHERE [status] = 'active') SELECT [id], [name] FROM active_users | covered |
| `VS005` | `vastbase-sqlserver-join` | SELECT [u].[id], [o].[order_no] FROM [dbo].[users] [u] JOIN [dbo].[orders] [o] ON [u].[id] = [o].[user_id] WHERE [o].[status] = @status | covered |
| `VS006` | `vastbase-sqlserver-left-join` | SELECT [u].[id], [p].[phone] FROM [dbo].[users] AS [u] LEFT JOIN [dbo].[phones] AS [p] ON [u].[id] = [p].[user_id] | covered |
| `VS007` | `vastbase-sqlserver-insert-values-param` | INSERT INTO [dbo].[users] ([id], [name]) VALUES (@id, @name) | covered |
| `VS008` | `vastbase-sqlserver-insert-values-multi-row` | INSERT INTO [dbo].[users] ([id], [name]) VALUES (1, 'bob'), (2, 'alice') | covered |
| `VS009` | `vastbase-sqlserver-insert-select` | INSERT INTO [dbo].[archive_users] ([id], [name]) SELECT [id], [name] FROM [dbo].[users] WHERE [status] = 'inactive' | covered |
| `VS010` | `vastbase-sqlserver-update-basic` | UPDATE [dbo].[users] SET [name] = @name, [status] = 'active' WHERE [id] = @id | covered |
| `VS011` | `vastbase-sqlserver-delete-conditional` | DELETE FROM [dbo].[users] WHERE [id] = @id AND [status] = 'inactive' | covered |
| `VS012` | `vastbase-sqlserver-unicode-string` | SELECT N'Bob''s order' AS label FROM [dbo].[orders] WHERE [customer_name] = N'Alice' | covered |
| `VS013` | `vastbase-sqlserver-jdbc-question-params` | SELECT [id] FROM [dbo].[users] WHERE [id] = ? AND [status] = ? | covered |
| `VS014` | `vastbase-sqlserver-temp-table` | SELECT [id] FROM #active_users WHERE [status] = 'active' | covered |
| `VS015` | `vastbase-sqlserver-functions` | SELECT ISNULL([name], 'unknown') AS [name], GETDATE() AS [now_value], NEWID() AS [row_id] FROM [dbo].[users] | covered |
| `VS016` | `vastbase-sqlserver-row-number` | SELECT ROW_NUMBER() OVER (PARTITION BY [status] ORDER BY [id]) AS rn, [id] FROM [dbo].[users] | covered |
| `VS017` | `vastbase-sqlserver-case-expression` | SELECT CASE WHEN [status] = 'active' THEN 1 ELSE 0 END AS active_flag FROM [dbo].[users] | covered |
| `VS018` | `vastbase-sqlserver-union-all` | SELECT [id] FROM [dbo].[users] UNION ALL SELECT [id] FROM [dbo].[archive_users] | covered |
| `VS019` | `vastbase-sqlserver-except` | SELECT [id] FROM [dbo].[users] EXCEPT SELECT [id] FROM [dbo].[archive_users] | covered |
| `VS020` | `vastbase-sqlserver-intersect` | SELECT [id] FROM [dbo].[users] INTERSECT SELECT [id] FROM [dbo].[active_users] | covered |
| `VS021` | `vastbase-sqlserver-create-table-identity` | CREATE TABLE [dbo].[users] ([id] INT IDENTITY(1,1) PRIMARY KEY, [name] NVARCHAR(64), [active] BIT) | covered |
| `VS022` | `vastbase-sqlserver-create-view` | CREATE VIEW [dbo].[v_users] AS SELECT [id], [name] FROM [dbo].[users] | covered |
| `VS023` | `vastbase-sqlserver-alter-table-add` | ALTER TABLE [dbo].[users] ADD [age] INT | covered |
| `VS024` | `vastbase-sqlserver-create-index` | CREATE INDEX [ix_users_name] ON [dbo].[users] ([name]) | covered |
| `VS025` | `vastbase-sqlserver-drop-table` | DROP TABLE IF EXISTS [dbo].[users] | covered |
| `VS026` | `vastbase-sqlserver-truncate-table` | TRUNCATE TABLE [dbo].[users] | covered |
| `VS027` | `vastbase-sqlserver-transaction` | BEGIN TRANSACTION; UPDATE [dbo].[users] SET [status] = 'active' WHERE [id] = @id; COMMIT TRANSACTION | covered |
| `VS028` | `vastbase-sqlserver-save-transaction` | SAVE TRANSACTION sp1 | covered |
| `VS029` | `vastbase-sqlserver-grant-revoke` | GRANT SELECT ON [dbo].[users] TO [app_role]; REVOKE SELECT ON [dbo].[users] FROM [app_role] | covered |
| `VS030` | `vastbase-sqlserver-go-separator` | SELECT [id] FROM [dbo].[users]<br>GO<br>SELECT [id] FROM [dbo].[orders] | covered |
| `VS031` | `vastbase-sqlserver-numeric-types` | CREATE TABLE [dbo].[payments] ([id] BIGINT IDENTITY, [amount] DECIMAL(18,2), [created_at] DATETIME2) | covered |
| `VS032` | `vastbase-sqlserver-in-list-params` | SELECT [id] FROM [dbo].[users] WHERE [id] IN (@id1, @id2, @id3) | covered |
| `VS033` | `vastbase-sqlserver-cast-date` | SELECT CAST('2024-01-01' AS DATE) AS created_date FROM [dbo].[users] | covered |
| `VS034` | `vastbase-sqlserver-quoted-identifier` | SELECT [Order Detail].[Order ID] FROM [dbo].[Order Detail] WHERE [Order Detail].[Order ID] = @order_id | covered |
| `VS035` | `vastbase-sqlserver-merge-basic` | MERGE INTO [dbo].[users] AS [t] USING [dbo].[staging_users] AS [s] ON [t].[id] = [s].[id] WHEN MATCHED THEN UPDATE SET [name] = [s].[name] WHEN NOT MATCHED BY TARGET THEN INSERT ([id], [name]) VALUES ([s].[id], [s].[name]); | covered |
| `VS036` | `vastbase-sqlserver-top-parameter` | SELECT TOP (@row_count) [id] FROM [dbo].[users] ORDER BY [id] | covered |
| `VS037` | `vastbase-sqlserver-cte-top-main-select` | WITH latest_users AS (SELECT [id] FROM [dbo].[users] WHERE [status] = 'active') SELECT TOP (3) [id] FROM latest_users ORDER BY [id] | covered |
| `VS038` | `vastbase-sqlserver-unicode-duplicate-literal` | SELECT 'same' AS ascii_value, N'same' AS unicode_value FROM [dbo].[users] | covered |
| `VS042` | `vastbase-sqlserver-unsupported-keywords-in-string` | SELECT 'OUTPUT @table EXEC' AS label FROM [dbo].[users] | covered |
| `VS043` | `vastbase-sqlserver-unsupported-keywords-in-comment` | SELECT [id] FROM [dbo].[users] /* OUTPUT inserted.id */ WHERE [id] = @id | covered |
| `VSU001` | `vastbase-sqlserver-top-percent-unsupported` | SELECT TOP (10) PERCENT [id] FROM [dbo].[users] | explicitly unsupported |
| `VSU002` | `vastbase-sqlserver-top-with-ties-unsupported` | SELECT TOP (10) WITH TIES [id] FROM [dbo].[users] ORDER BY [score] | explicitly unsupported |
| `VSU003` | `vastbase-sqlserver-output-unsupported` | INSERT INTO [dbo].[users] ([id]) OUTPUT inserted.[id] VALUES (1) | explicitly unsupported |
| `VSU004` | `vastbase-sqlserver-table-hint-unsupported` | SELECT [id] FROM [dbo].[users] WITH (NOLOCK) | explicitly unsupported |
| `VSU005` | `vastbase-sqlserver-cross-apply-unsupported` | SELECT [u].[id] FROM [dbo].[users] [u] CROSS APPLY [dbo].[fn_orders]([u].[id]) [o] | explicitly unsupported |
| `VSU006` | `vastbase-sqlserver-pivot-unsupported` | SELECT * FROM [dbo].[sales] PIVOT (SUM([amount]) FOR [month] IN ([Jan], [Feb])) AS [p] | explicitly unsupported |
| `VSU007` | `vastbase-sqlserver-for-json-unsupported` | SELECT [id] FROM [dbo].[users] FOR JSON PATH | explicitly unsupported |
| `VSU008` | `vastbase-sqlserver-query-hint-unsupported` | SELECT [id] FROM [dbo].[users] OPTION (RECOMPILE) | explicitly unsupported |
| `VSU009` | `vastbase-sqlserver-declare-unsupported` | DECLARE @id INT = 1; SELECT @id | explicitly unsupported |
| `VSU010` | `vastbase-sqlserver-exec-unsupported` | EXEC [dbo].[rebuild_user_cache] | explicitly unsupported |
| `VSU011` | `vastbase-sqlserver-create-procedure-unsupported` | CREATE PROCEDURE [dbo].[p] AS SELECT 1 | explicitly unsupported |
| `VS039` | `vastbase-sqlserver-system-variable` | SELECT @@ROWCOUNT | covered |
| `VS040` | `vastbase-sqlserver-binary-literal` | SELECT 0xDEADBEEF AS payload | covered |
| `VS041` | `vastbase-sqlserver-convert-style` | SELECT CONVERT(VARCHAR(10), [created_at], 120) FROM [dbo].[users] | covered |
| `VS044` | `vastbase-sqlserver-use-database` | USE [AdventureWorks2022] | covered |
| `VS045` | `vastbase-sqlserver-use-database-official-example` | USE AdventureWorks2022 | covered |
| `VS046` | `vastbase-sqlserver-use-database-in-multi-statement` | USE [AdventureWorks2022]; SELECT * FROM [dbo].[users] | covered |
| `VS047` | `vastbase-sqlserver-sp-prepare` | EXEC sp_prepare @handle OUTPUT, N'@p1 int', N'SELECT * FROM users WHERE id=@p1' | covered |
| `VS048` | `vastbase-sqlserver-sp-execute` | EXEC sp_execute @handle, 42 | covered |
| `VS049` | `vastbase-sqlserver-sp-prepexec` | EXEC sp_prepexec @handle OUTPUT, N'@p1 int', N'SELECT * FROM users WHERE id=@p1', 42 | covered |
| `VS050` | `vastbase-sqlserver-sp-unprepare` | EXEC sp_unprepare @handle | covered |
| `VS051` | `vastbase-sqlserver-sp-executesql` | EXEC sp_executesql N'SELECT * FROM users WHERE id=@p1', N'@p1 int', @p1=42 | covered |
| `VS052` | `vastbase-sqlserver-select-multiple-named-params` | SELECT [id], [name] FROM [dbo].[users] WHERE [id] = @id AND [status] = @status | covered |
| `VS053` | `vastbase-sqlserver-select-in-named-params` | SELECT [id] FROM [dbo].[users] WHERE [status] IN (@s1, @s2, @s3) | covered |
| `VS054` | `vastbase-sqlserver-top-named-param-like` | SELECT TOP (@limit) [id] FROM [dbo].[users] WHERE [name] LIKE @pattern ORDER BY [id] | covered |
| `VS055` | `vastbase-sqlserver-insert-multiple-named-params` | INSERT INTO [dbo].[users] ([id], [name], [status]) VALUES (@id, @name, @status) | covered |
| `VS056` | `vastbase-sqlserver-insert-multi-row-question-params` | INSERT INTO [dbo].[users] ([id], [name]) VALUES (?, ?), (?, ?) | covered |
| `VS057` | `vastbase-sqlserver-update-question-params-expanded` | UPDATE [dbo].[users] SET [name] = ?, [status] = ? WHERE [id] = ? | covered |
| `VS058` | `vastbase-sqlserver-delete-question-params` | DELETE FROM [dbo].[users] WHERE [id] = ? AND [status] = ? | covered |
| `VS059` | `vastbase-sqlserver-sp-prepare-insert` | EXEC sp_prepare @handle OUTPUT, N'@id int, @name nvarchar(50)', N'INSERT INTO users(id, name) VALUES(@id, @name)' | covered |
| `VS060` | `vastbase-sqlserver-sp-execute-named-values` | EXEC sp_execute @handle, @id = 1, @name = N'bob' | covered |
| `VS061` | `vastbase-sqlserver-sp-executesql-update` | EXEC sp_executesql N'UPDATE users SET name=@name WHERE id=@id', N'@name nvarchar(50), @id int', @name=N'bob', @id=1 | covered |
| `VS062` | `vastbase-sqlserver-view-top-direct-and-bind` | SELECT TOP (@limit) [id], [name] FROM [dbo].[users] WHERE [id] = @id ORDER BY [id] | covered |
| `VS063` | `vastbase-sqlserver-view-plus-expression` | SELECT [first_name] + [last_name] FROM [dbo].[users] WHERE [id] = @id | covered |
| `VS064` | `vastbase-sqlserver-view-case-expression` | SELECT CASE WHEN [state] = 1 THEN [name] ELSE [fallback_name] END FROM [dbo].[users] | covered |
| `VS065` | `vastbase-sqlserver-view-group-having-order` | SELECT [dept], COUNT([id]) FROM [dbo].[users] GROUP BY [dept] HAVING COUNT([id]) > 1 ORDER BY [dept] | covered |
| `VS066` | `vastbase-sqlserver-view-update-at-binds` | UPDATE [dbo].[servers] SET [ip] = @aaa WHERE [id] = @id | covered |
| `VS067` | `vastbase-sqlserver-select-between-named-params` | SELECT [id] FROM [dbo].[users] WHERE [age] BETWEEN @min_age AND @max_age | covered |
| `VS068` | `vastbase-sqlserver-select-not-in-named-params` | SELECT [id] FROM [dbo].[users] WHERE [status] NOT IN (@s1, @s2) | covered |
| `VS069` | `vastbase-sqlserver-select-not-between-named-params` | SELECT [id] FROM [dbo].[users] WHERE [age] NOT BETWEEN @min_age AND @max_age | covered |
| `VS070` | `vastbase-sqlserver-select-not-like-named-param` | SELECT [id] FROM [dbo].[users] WHERE [name] NOT LIKE @name_pattern | covered |
| `VS071` | `vastbase-sqlserver-select-distinct-like-param` | SELECT DISTINCT [name] FROM [dbo].[table1] WHERE [name] LIKE @name | covered |
| `VS072` | `vastbase-sqlserver-delete-in-named-params` | DELETE FROM [dbo].[users] WHERE [email] IN (@email1, @email2) | covered |
| `VS073` | `vastbase-sqlserver-update-exists-subquery` | UPDATE [dbo].[users] SET [status] = @status WHERE EXISTS (SELECT 1 FROM [dbo].[orders] o WHERE o.[user_id] = [users].[id] AND o.[phone] = @phone) | covered |
| `VS074` | `vastbase-sqlserver-insert-without-column-list` | INSERT INTO [dbo].[users] VALUES (?, ?, ?) | covered |
| `VS075` | `vastbase-sqlserver-select-derived-table-filter` | SELECT t.[id], t.[phone] FROM (SELECT [id], [phone] FROM [dbo].[users] WHERE [status] = @status) t WHERE t.[phone] = @phone | covered |
| `VS076` | `vastbase-sqlserver-select-json-value` | SELECT JSON_VALUE([extra], '$.phone') AS [phone_json] FROM [dbo].[users] WHERE [id] = @id | covered |
| `VS077` | `vastbase-sqlserver-select-order-by-ordinal` | SELECT [id], [phone] FROM [dbo].[users] ORDER BY 1 | covered |
| `VS078` | `vastbase-sqlserver-offset-fetch-named-params` | SELECT [id] FROM [dbo].[users] ORDER BY [id] OFFSET @offset ROWS FETCH NEXT @limit ROWS ONLY | covered |
| `VS079` | `vastbase-sqlserver-select-left-join-alias-star` | SELECT u.*, o.[order_no] FROM [dbo].[users] u LEFT JOIN [dbo].[orders] o ON u.[id] = o.[user_id] WHERE o.[status] = @status | covered |
| `VS080` | `vastbase-sqlserver-create-view-join-aggregate` | CREATE VIEW [dbo].[v_user_orders] AS SELECT u.[id], COUNT(o.[id]) AS [order_count] FROM [dbo].[users] u JOIN [dbo].[orders] o ON u.[id] = o.[user_id] GROUP BY u.[id] | covered |
| `VS081` | `vastbase-sqlserver-top-question-bind-order` | SELECT TOP (?) [id] FROM [dbo].[users] WHERE [name] LIKE ? ORDER BY [id] | covered |
| `VS082` | `vastbase-sqlserver-multi-statement-global-bind-position` | UPDATE [dbo].[users] SET [a] = ? WHERE [b] = ?; UPDATE [dbo].[users] SET [c] = ? WHERE [d] = ? | covered |
| `VS083` | `vastbase-sqlserver-select-derived-query-graph` | SELECT s.[name] AS [outer_name] FROM (SELECT [id], [name] FROM [dbo].[users] WHERE [age] <= @age) s WHERE s.[name] LIKE @name | covered |
| `VS084` | `vastbase-sqlserver-merge-bind-query-graph` | MERGE INTO target t USING source s ON t.id=s.id WHEN MATCHED THEN UPDATE SET phone = ? WHEN NOT MATCHED THEN INSERT (id, phone) VALUES (?, ?) | covered |
| `VS085` | `vastbase-sqlserver-field-match-kind-direct-and-expression` | SELECT [id] FROM [dbo].[users] WHERE [secret] = @plain_secret AND UPPER([secret]) = @upper_secret | covered |
| `VS086` | `vastbase-sqlserver-expression-field-case-expression-value` | SELECT [id] FROM [dbo].[users] WHERE CASE WHEN [id] = 1 THEN [secret] ELSE [backup_secret] END = @v | covered |
| `VS087` | `vastbase-sqlserver-expression-field-multi-field-expression-value` | SELECT [id] FROM [dbo].[users] WHERE CONCAT([secret], [id]) = @v1 AND [secret] + [id] = @v2 | covered |
| `VS088` | `vastbase-sqlserver-expression-field-value-side-expression` | SELECT [id] FROM [dbo].[users] WHERE [secret] = UPPER(@v1) AND [secret] = @v2 + 'x' AND [secret] = CAST(@v3 AS VARCHAR(32)) | covered |
| `VS089` | `vastbase-sqlserver-expression-field-dml-expression-values` | INSERT INTO [dbo].[users] ([id], [secret]) VALUES (1, UPPER(@v1)); UPDATE [dbo].[users] SET [secret] = @v2 + 'x' WHERE [id] = 1 | covered |
| `VS090` | `vastbase-sqlserver-update-named-bind-rhs-crypto-source` | UPDATE [dbo].[dbp_crypto_test] SET [secret] = @secret_value WHERE [id] = @id | covered |
| `VS091` | `vastbase-sqlserver-update-multiple-bind-rhs-crypto-source` | UPDATE [dbo].[dbp_crypto_test] SET [phone] = @phone_value, [secret] = @secret_value WHERE [id] = @id | covered |
| `VS092` | `vastbase-sqlserver-like-escape-literal` | SELECT [id] FROM [dbo].[users] WHERE [name] LIKE 'A!_%' ESCAPE '!' | covered |
| `VS093` | `vastbase-sqlserver-not-like-escape-named-bind` | SELECT [id] FROM [dbo].[users] WHERE [name] NOT LIKE @pattern ESCAPE @escape_char | covered |
| `VS094` | `vastbase-sqlserver-like-escape-question-bind` | SELECT [id] FROM [dbo].[users] WHERE [name] LIKE ? ESCAPE ? | covered |
| `VS095` | `vastbase-sqlserver-like-without-explicit-escape` | SELECT [id] FROM [dbo].[users] WHERE [name] LIKE @pattern | covered |
| `VSU015` | `vastbase-sqlserver-table-variable-unsupported` | SELECT [id] FROM @users | explicitly unsupported |
| `VSU016` | `vastbase-sqlserver-merge-by-source-unsupported` | MERGE INTO [dbo].[users] AS [t] USING [dbo].[staging_users] AS [s] ON [t].[id] = [s].[id] WHEN NOT MATCHED BY SOURCE THEN DELETE; | explicitly unsupported |
| `VSU017` | `vastbase-sqlserver-top-offset-fetch-unsupported` | SELECT TOP (10) [id] FROM [dbo].[users] ORDER BY [id] OFFSET 20 ROWS FETCH NEXT 10 ROWS ONLY | explicitly unsupported |
| `VSU018` | `vastbase-sqlserver-nested-top-unsupported` | SELECT [id] FROM (SELECT TOP (2) [id] FROM [dbo].[users]) AS [u] | explicitly unsupported |
| `VSH001` | `vastbase-sqlserver-hook-constants-transact-sql` | INSERT INTO [dbo].[binlog] ([payload]) VALUES (0xDEADBEEF) | covered |
| `VSH002` | `vastbase-sqlserver-hook-datetimeoffset-transact-sql` | CREATE TABLE [dbo].[events] ([created_at] DATETIMEOFFSET(7)) | covered |
| `VSH003` | `vastbase-sqlserver-hook-nondeterministic-convert-date-literals` | SELECT CONVERT(DATETIME, '01-02-2024', 101) AS [converted_at] FROM [dbo].[users] | covered |
| `VSH004` | `vastbase-sqlserver-hook-ai-functions-transact-sql` | SELECT AI_FUNCTIONS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH005` | `vastbase-sqlserver-hook-any-value-transact-sql` | SELECT ANY_VALUE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH006` | `vastbase-sqlserver-hook-app-name-transact-sql` | SELECT APP_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH007` | `vastbase-sqlserver-hook-applock-mode-transact-sql` | SELECT APPLOCK_MODE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH008` | `vastbase-sqlserver-hook-applock-test-transact-sql` | SELECT APPLOCK_TEST(1) AS [v] FROM [dbo].[users] | covered |
| `VSH009` | `vastbase-sqlserver-hook-approx-percentile-cont-transact-sql` | SELECT APPROX_PERCENTILE_CONT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH010` | `vastbase-sqlserver-hook-approx-percentile-disc-transact-sql` | SELECT APPROX_PERCENTILE_DISC(1) AS [v] FROM [dbo].[users] | covered |
| `VSH011` | `vastbase-sqlserver-hook-assemblyproperty-transact-sql` | SELECT ASSEMBLYPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH012` | `vastbase-sqlserver-hook-asymkey-id-transact-sql` | SELECT ASYMKEY_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH013` | `vastbase-sqlserver-hook-asymkeyproperty-transact-sql` | SELECT ASYMKEYPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH014` | `vastbase-sqlserver-hook-base64-decode-transact-sql` | SELECT BASE64_DECODE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH015` | `vastbase-sqlserver-hook-base64-encode-transact-sql` | SELECT BASE64_ENCODE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH016` | `vastbase-sqlserver-hook-binary-checksum-transact-sql` | SELECT BINARY_CHECKSUM(1) AS [v] FROM [dbo].[users] | covered |
| `VSH017` | `vastbase-sqlserver-hook-bit-count-transact-sql` | SELECT BIT_COUNT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH018` | `vastbase-sqlserver-hook-cast-and-convert-transact-sql` | SELECT CONVERT(INT, '42', 0) AS [v] FROM [dbo].[users] | covered |
| `VSH019` | `vastbase-sqlserver-hook-cert-id-transact-sql` | SELECT CERT_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH020` | `vastbase-sqlserver-hook-certencoded-transact-sql` | SELECT CERTENCODED(1) AS [v] FROM [dbo].[users] | covered |
| `VSH021` | `vastbase-sqlserver-hook-certprivatekey-transact-sql` | SELECT CERTPRIVATEKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH022` | `vastbase-sqlserver-hook-certproperty-transact-sql` | SELECT CERTPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH023` | `vastbase-sqlserver-hook-checksum-transact-sql` | SELECT CHECKSUM(1) AS [v] FROM [dbo].[users] | covered |
| `VSH024` | `vastbase-sqlserver-hook-col-length-transact-sql` | SELECT COL_LENGTH(1) AS [v] FROM [dbo].[users] | covered |
| `VSH025` | `vastbase-sqlserver-hook-col-name-transact-sql` | SELECT COL_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH026` | `vastbase-sqlserver-hook-collation-functions-collationproperty-transact-sql` | SELECT COLLATIONPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH027` | `vastbase-sqlserver-hook-collation-functions-tertiary-weights-transact-sql` | SELECT TERTIARY_WEIGHTS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH028` | `vastbase-sqlserver-hook-columnproperty-transact-sql` | SELECT COLUMNPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH029` | `vastbase-sqlserver-hook-columns-updated-transact-sql` | SELECT COLUMNS_UPDATED(1) AS [v] FROM [dbo].[users] | covered |
| `VSH030` | `vastbase-sqlserver-hook-compress-transact-sql` | SELECT COMPRESS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH031` | `vastbase-sqlserver-hook-connectionproperty-transact-sql` | SELECT CONNECTIONPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH032` | `vastbase-sqlserver-hook-connections-transact-sql` | SELECT @@CONNECTIONS AS [v] FROM [dbo].[users] | covered |
| `VSH033` | `vastbase-sqlserver-hook-context-info-transact-sql` | SELECT CONTEXT_INFO(1) AS [v] FROM [dbo].[users] | covered |
| `VSH034` | `vastbase-sqlserver-hook-cpu-busy-transact-sql` | SELECT @@CPU_BUSY AS [v] FROM [dbo].[users] | covered |
| `VSH035` | `vastbase-sqlserver-hook-crypt-gen-random-transact-sql` | SELECT CRYPT_GEN_RANDOM(1) AS [v] FROM [dbo].[users] | covered |
| `VSH036` | `vastbase-sqlserver-hook-current-date-transact-sql` | SELECT CURRENT_DATE AS [v] FROM [dbo].[users] | covered |
| `VSH037` | `vastbase-sqlserver-hook-current-request-id-transact-sql` | SELECT CURRENT_REQUEST_ID() AS [v] FROM [dbo].[users] | covered |
| `VSH038` | `vastbase-sqlserver-hook-current-timestamp-transact-sql` | SELECT CURRENT_TIMESTAMP AS [v] FROM [dbo].[users] | covered |
| `VSH039` | `vastbase-sqlserver-hook-current-timezone-id-transact-sql` | SELECT CURRENT_TIMEZONE_ID() AS [v] FROM [dbo].[users] | covered |
| `VSH040` | `vastbase-sqlserver-hook-current-timezone-transact-sql` | SELECT CURRENT_TIMEZONE() AS [v] FROM [dbo].[users] | covered |
| `VSH041` | `vastbase-sqlserver-hook-current-transaction-id-transact-sql` | SELECT CURRENT_TRANSACTION_ID() AS [v] FROM [dbo].[users] | covered |
| `VSH042` | `vastbase-sqlserver-hook-current-user-transact-sql` | SELECT CURRENT_USER AS [v] FROM [dbo].[users] | covered |
| `VSH043` | `vastbase-sqlserver-hook-cursor-rows-transact-sql` | SELECT CURSOR_ROWS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH044` | `vastbase-sqlserver-hook-cursor-status-transact-sql` | SELECT CURSOR_STATUS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH045` | `vastbase-sqlserver-hook-database-principal-id-transact-sql` | SELECT DATABASE_PRINCIPAL_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH046` | `vastbase-sqlserver-hook-databasepropertyex-transact-sql` | SELECT DATABASEPROPERTYEX(1) AS [v] FROM [dbo].[users] | covered |
| `VSH047` | `vastbase-sqlserver-hook-date-bucket-transact-sql` | SELECT DATE_BUCKET(day, 1, [created_at]) AS [v] FROM [dbo].[users] | covered |
| `VSH048` | `vastbase-sqlserver-hook-datefirst-transact-sql` | SELECT @@DATEFIRST AS [v] FROM [dbo].[users] | covered |
| `VSH049` | `vastbase-sqlserver-hook-datefromparts-transact-sql` | SELECT DATEFROMPARTS(2024, 1, 2) AS [v] FROM [dbo].[users] | covered |
| `VSH050` | `vastbase-sqlserver-hook-datetime2fromparts-transact-sql` | SELECT DATETIME2FROMPARTS(2024, 1, 2, 3, 4, 5, 0, 7) AS [v] FROM [dbo].[users] | covered |
| `VSH051` | `vastbase-sqlserver-hook-datetimefromparts-transact-sql` | SELECT DATETIMEFROMPARTS(2024, 1, 2, 3, 4, 5, 0) AS [v] FROM [dbo].[users] | covered |
| `VSH052` | `vastbase-sqlserver-hook-datetimeoffsetfromparts-transact-sql` | SELECT DATETIMEOFFSETFROMPARTS(2024, 1, 2, 3, 4, 5, 0, -8, 0, 7) AS [v] FROM [dbo].[users] | covered |
| `VSH053` | `vastbase-sqlserver-hook-datetrunc-transact-sql` | SELECT DATETRUNC(day, [created_at]) AS [v] FROM [dbo].[users] | covered |
| `VSH054` | `vastbase-sqlserver-hook-db-id-transact-sql` | SELECT DB_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH055` | `vastbase-sqlserver-hook-db-name-transact-sql` | SELECT DB_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH056` | `vastbase-sqlserver-hook-dbts-transact-sql` | SELECT @@DBTS AS [v] FROM [dbo].[users] | covered |
| `VSH057` | `vastbase-sqlserver-hook-decompress-transact-sql` | SELECT DECOMPRESS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH058` | `vastbase-sqlserver-hook-decryptbyasymkey-transact-sql` | SELECT DECRYPTBYASYMKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH059` | `vastbase-sqlserver-hook-decryptbycert-transact-sql` | SELECT DECRYPTBYCERT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH060` | `vastbase-sqlserver-hook-decryptbykey-transact-sql` | SELECT DECRYPTBYKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH061` | `vastbase-sqlserver-hook-decryptbykeyautoasymkey-transact-sql` | SELECT DECRYPTBYKEYAUTOASYMKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH062` | `vastbase-sqlserver-hook-decryptbykeyautocert-transact-sql` | SELECT DECRYPTBYKEYAUTOCERT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH063` | `vastbase-sqlserver-hook-decryptbypassphrase-transact-sql` | SELECT DECRYPTBYPASSPHRASE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH064` | `vastbase-sqlserver-hook-edge-id-from-parts-transact-sql` | SELECT EDGE_ID_FROM_PARTS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH065` | `vastbase-sqlserver-hook-edit-distance-similarity-transact-sql` | SELECT EDIT_DISTANCE_SIMILARITY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH066` | `vastbase-sqlserver-hook-edit-distance-transact-sql` | SELECT EDIT_DISTANCE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH067` | `vastbase-sqlserver-hook-encryptbyasymkey-transact-sql` | SELECT ENCRYPTBYASYMKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH068` | `vastbase-sqlserver-hook-encryptbycert-transact-sql` | SELECT ENCRYPTBYCERT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH069` | `vastbase-sqlserver-hook-encryptbykey-transact-sql` | SELECT ENCRYPTBYKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH070` | `vastbase-sqlserver-hook-encryptbypassphrase-transact-sql` | SELECT ENCRYPTBYPASSPHRASE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH071` | `vastbase-sqlserver-hook-eomonth-transact-sql` | SELECT EOMONTH(1) AS [v] FROM [dbo].[users] | covered |
| `VSH072` | `vastbase-sqlserver-hook-error-line-transact-sql` | SELECT ERROR_LINE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH073` | `vastbase-sqlserver-hook-error-message-transact-sql` | SELECT ERROR_MESSAGE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH074` | `vastbase-sqlserver-hook-error-number-transact-sql` | SELECT ERROR_NUMBER(1) AS [v] FROM [dbo].[users] | covered |
| `VSH075` | `vastbase-sqlserver-hook-error-procedure-transact-sql` | SELECT ERROR_PROCEDURE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH076` | `vastbase-sqlserver-hook-error-severity-transact-sql` | SELECT ERROR_SEVERITY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH077` | `vastbase-sqlserver-hook-error-state-transact-sql` | SELECT ERROR_STATE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH078` | `vastbase-sqlserver-hook-eventdata-transact-sql` | SELECT EVENTDATA(1) AS [v] FROM [dbo].[users] | covered |
| `VSH079` | `vastbase-sqlserver-hook-fetch-status-transact-sql` | SELECT @@FETCH_STATUS AS [v] FROM [dbo].[users] | covered |
| `VSH080` | `vastbase-sqlserver-hook-file-id-transact-sql` | SELECT FILE_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH081` | `vastbase-sqlserver-hook-file-idex-transact-sql` | SELECT FILE_IDEX(1) AS [v] FROM [dbo].[users] | covered |
| `VSH082` | `vastbase-sqlserver-hook-file-name-transact-sql` | SELECT FILE_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH083` | `vastbase-sqlserver-hook-filegroup-id-transact-sql` | SELECT FILEGROUP_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH084` | `vastbase-sqlserver-hook-filegroup-name-transact-sql` | SELECT FILEGROUP_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH085` | `vastbase-sqlserver-hook-filegroupproperty-transact-sql` | SELECT FILEGROUPPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH086` | `vastbase-sqlserver-hook-fileproperty-transact-sql` | SELECT FILEPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH087` | `vastbase-sqlserver-hook-filepropertyex-transact-sql` | SELECT FILEPROPERTYEX(1) AS [v] FROM [dbo].[users] | covered |
| `VSH088` | `vastbase-sqlserver-hook-format-transact-sql` | SELECT FORMAT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH089` | `vastbase-sqlserver-hook-formatmessage-transact-sql` | SELECT FORMATMESSAGE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH090` | `vastbase-sqlserver-hook-fulltextcatalogproperty-transact-sql` | SELECT FULLTEXTCATALOGPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH091` | `vastbase-sqlserver-hook-fulltextserviceproperty-transact-sql` | SELECT FULLTEXTSERVICEPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH092` | `vastbase-sqlserver-hook-get-bit-transact-sql` | SELECT GET_BIT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH093` | `vastbase-sqlserver-hook-get-filestream-transaction-context-transact-sql` | SELECT GET_FILESTREAM_TRANSACTION_CONTEXT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH094` | `vastbase-sqlserver-hook-getansinull-transact-sql` | SELECT GETANSINULL(1) AS [v] FROM [dbo].[users] | covered |
| `VSH095` | `vastbase-sqlserver-hook-graph-id-from-edge-id-transact-sql` | SELECT GRAPH_ID_FROM_EDGE_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH096` | `vastbase-sqlserver-hook-graph-id-from-node-id-transact-sql` | SELECT GRAPH_ID_FROM_NODE_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH097` | `vastbase-sqlserver-hook-has-dbaccess-transact-sql` | SELECT HAS_DBACCESS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH098` | `vastbase-sqlserver-hook-has-perms-by-name-transact-sql` | SELECT HAS_PERMS_BY_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH099` | `vastbase-sqlserver-hook-hashbytes-transact-sql` | SELECT HASHBYTES(1) AS [v] FROM [dbo].[users] | covered |
| `VSH100` | `vastbase-sqlserver-hook-host-id-transact-sql` | SELECT HOST_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH101` | `vastbase-sqlserver-hook-host-name-transact-sql` | SELECT HOST_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH102` | `vastbase-sqlserver-hook-ident-current-transact-sql` | SELECT IDENT_CURRENT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH103` | `vastbase-sqlserver-hook-ident-incr-transact-sql` | SELECT IDENT_INCR(1) AS [v] FROM [dbo].[users] | covered |
| `VSH104` | `vastbase-sqlserver-hook-ident-seed-transact-sql` | SELECT IDENT_SEED(1) AS [v] FROM [dbo].[users] | covered |
| `VSH105` | `vastbase-sqlserver-hook-idle-transact-sql` | SELECT @@IDLE AS [v] FROM [dbo].[users] | covered |
| `VSH106` | `vastbase-sqlserver-hook-index-col-transact-sql` | SELECT INDEX_COL(1) AS [v] FROM [dbo].[users] | covered |
| `VSH107` | `vastbase-sqlserver-hook-indexkey-property-transact-sql` | SELECT INDEXKEY_PROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH108` | `vastbase-sqlserver-hook-indexproperty-transact-sql` | SELECT INDEXPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH109` | `vastbase-sqlserver-hook-io-busy-transact-sql` | SELECT @@IO_BUSY AS [v] FROM [dbo].[users] | covered |
| `VSH110` | `vastbase-sqlserver-hook-is-member-transact-sql` | SELECT IS_MEMBER(1) AS [v] FROM [dbo].[users] | covered |
| `VSH111` | `vastbase-sqlserver-hook-is-objectsigned-transact-sql` | SELECT IS_OBJECTSIGNED(1) AS [v] FROM [dbo].[users] | covered |
| `VSH112` | `vastbase-sqlserver-hook-is-rolemember-transact-sql` | SELECT IS_ROLEMEMBER(1) AS [v] FROM [dbo].[users] | covered |
| `VSH113` | `vastbase-sqlserver-hook-is-srvrolemember-transact-sql` | SELECT IS_SRVROLEMEMBER(1) AS [v] FROM [dbo].[users] | covered |
| `VSH114` | `vastbase-sqlserver-hook-isdate-transact-sql` | SELECT ISDATE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH115` | `vastbase-sqlserver-hook-jaro-winkler-distance-transact-sql` | SELECT JARO_WINKLER_DISTANCE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH116` | `vastbase-sqlserver-hook-jaro-winkler-similarity-transact-sql` | SELECT JARO_WINKLER_SIMILARITY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH117` | `vastbase-sqlserver-hook-json-array-transact-sql` | SELECT JSON_ARRAY(1, 2) AS [v] FROM [dbo].[users] | covered |
| `VSH118` | `vastbase-sqlserver-hook-json-arrayagg-transact-sql` | SELECT JSON_ARRAYAGG([id]) AS [v] FROM [dbo].[users] | covered |
| `VSH119` | `vastbase-sqlserver-hook-json-contains-transact-sql` | SELECT JSON_CONTAINS('{"a":1}', '1', '$.a') AS [v] FROM [dbo].[users] | covered |
| `VSH120` | `vastbase-sqlserver-hook-json-modify-transact-sql` | SELECT JSON_MODIFY('{"a":1}', '$.a', 2) AS [v] FROM [dbo].[users] | covered |
| `VSH121` | `vastbase-sqlserver-hook-json-object-transact-sql` | SELECT JSON_OBJECT('id', [id]) AS [v] FROM [dbo].[users] | covered |
| `VSH122` | `vastbase-sqlserver-hook-json-objectagg-transact-sql` | SELECT JSON_OBJECTAGG([name]: [id]) AS [v] FROM [dbo].[users] | covered |
| `VSH123` | `vastbase-sqlserver-hook-json-path-exists-transact-sql` | SELECT JSON_PATH_EXISTS('{"a":1}', '$.a') AS [v] FROM [dbo].[users] | covered |
| `VSH124` | `vastbase-sqlserver-hook-json-query-transact-sql` | SELECT JSON_QUERY('{"a":1}', '$') AS [v] FROM [dbo].[users] | covered |
| `VSH125` | `vastbase-sqlserver-hook-json-value-transact-sql` | SELECT JSON_VALUE('{"a":1}', '$.a') AS [v] FROM [dbo].[users] | covered |
| `VSH126` | `vastbase-sqlserver-hook-key-guid-transact-sql` | SELECT KEY_GUID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH127` | `vastbase-sqlserver-hook-key-id-transact-sql` | SELECT KEY_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH128` | `vastbase-sqlserver-hook-key-name-transact-sql` | SELECT KEY_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH129` | `vastbase-sqlserver-hook-langid-transact-sql` | SELECT LANGID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH130` | `vastbase-sqlserver-hook-language-transact-sql` | SELECT LANGUAGE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH131` | `vastbase-sqlserver-hook-left-shift-transact-sql` | SELECT LEFT_SHIFT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH132` | `vastbase-sqlserver-hook-lock-timeout-transact-sql` | SELECT @@LOCK_TIMEOUT AS [v] FROM [dbo].[users] | covered |
| `VSH133` | `vastbase-sqlserver-hook-logical-functions-choose-transact-sql` | SELECT CHOOSE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH134` | `vastbase-sqlserver-hook-logical-functions-greatest-transact-sql` | SELECT GREATEST(1) AS [v] FROM [dbo].[users] | covered |
| `VSH135` | `vastbase-sqlserver-hook-logical-functions-iif-transact-sql` | SELECT IIF(1) AS [v] FROM [dbo].[users] | covered |
| `VSH136` | `vastbase-sqlserver-hook-logical-functions-least-transact-sql` | SELECT LEAST(1) AS [v] FROM [dbo].[users] | covered |
| `VSH137` | `vastbase-sqlserver-hook-loginproperty-transact-sql` | SELECT LOGINPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH138` | `vastbase-sqlserver-hook-max-connections-transact-sql` | SELECT @@MAX_CONNECTIONS AS [v] FROM [dbo].[users] | covered |
| `VSH139` | `vastbase-sqlserver-hook-max-precision-transact-sql` | SELECT @@MAX_PRECISION AS [v] FROM [dbo].[users] | covered |
| `VSH140` | `vastbase-sqlserver-hook-min-active-rowversion-transact-sql` | SELECT MIN_ACTIVE_ROWVERSION(1) AS [v] FROM [dbo].[users] | covered |
| `VSH141` | `vastbase-sqlserver-hook-nestlevel-transact-sql` | SELECT NESTLEVEL(1) AS [v] FROM [dbo].[users] | covered |
| `VSH142` | `vastbase-sqlserver-hook-newsequentialid-transact-sql` | SELECT NEWSEQUENTIALID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH143` | `vastbase-sqlserver-hook-node-id-from-parts-transact-sql` | SELECT NODE_ID_FROM_PARTS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH144` | `vastbase-sqlserver-hook-object-definition-transact-sql` | SELECT OBJECT_DEFINITION(1) AS [v] FROM [dbo].[users] | covered |
| `VSH145` | `vastbase-sqlserver-hook-object-id-from-edge-id-transact-sql` | SELECT OBJECT_ID_FROM_EDGE_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH146` | `vastbase-sqlserver-hook-object-id-from-node-id-transact-sql` | SELECT OBJECT_ID_FROM_NODE_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH147` | `vastbase-sqlserver-hook-object-id-transact-sql` | SELECT OBJECT_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH148` | `vastbase-sqlserver-hook-object-name-transact-sql` | SELECT OBJECT_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH149` | `vastbase-sqlserver-hook-object-schema-name-transact-sql` | SELECT OBJECT_SCHEMA_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH150` | `vastbase-sqlserver-hook-objectproperty-transact-sql` | SELECT OBJECTPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH151` | `vastbase-sqlserver-hook-objectpropertyex-transact-sql` | SELECT OBJECTPROPERTYEX(1) AS [v] FROM [dbo].[users] | covered |
| `VSH152` | `vastbase-sqlserver-hook-odbc-scalar-functions-transact-sql` | SELECT {fn NOW()} AS [v] FROM [dbo].[users] | covered |
| `VSH153` | `vastbase-sqlserver-hook-options-transact-sql` | SELECT @@OPTIONS AS [v] FROM [dbo].[users] | covered |
| `VSH154` | `vastbase-sqlserver-hook-original-db-name-transact-sql` | SELECT ORIGINAL_DB_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH155` | `vastbase-sqlserver-hook-original-login-transact-sql` | SELECT ORIGINAL_LOGIN(1) AS [v] FROM [dbo].[users] | covered |
| `VSH156` | `vastbase-sqlserver-hook-pack-received-transact-sql` | SELECT @@PACK_RECEIVED AS [v] FROM [dbo].[users] | covered |
| `VSH157` | `vastbase-sqlserver-hook-pack-sent-transact-sql` | SELECT @@PACK_SENT AS [v] FROM [dbo].[users] | covered |
| `VSH158` | `vastbase-sqlserver-hook-packet-errors-transact-sql` | SELECT @@PACKET_ERRORS AS [v] FROM [dbo].[users] | covered |
| `VSH159` | `vastbase-sqlserver-hook-parse-transact-sql` | SELECT PARSE('42' AS INT USING 'en-US') AS [v] FROM [dbo].[users] | covered |
| `VSH160` | `vastbase-sqlserver-hook-parsename-transact-sql` | SELECT PARSENAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH161` | `vastbase-sqlserver-hook-partition-transact-sql` | SELECT PARTITION(1) AS [v] FROM [dbo].[users] | covered |
| `VSH162` | `vastbase-sqlserver-hook-permissions-transact-sql` | SELECT PERMISSIONS(1) AS [v] FROM [dbo].[users] | covered |
| `VSH163` | `vastbase-sqlserver-hook-procid-transact-sql` | SELECT @@PROCID AS [v] FROM [dbo].[users] | covered |
| `VSH164` | `vastbase-sqlserver-hook-product-aggregate-transact-sql` | SELECT PRODUCT_AGGREGATE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH165` | `vastbase-sqlserver-hook-pwdcompare-transact-sql` | SELECT PWDCOMPARE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH166` | `vastbase-sqlserver-hook-pwdencrypt-transact-sql` | SELECT PWDENCRYPT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH167` | `vastbase-sqlserver-hook-regexp-count-transact-sql` | SELECT REGEXP_COUNT([name], 'a') AS [v] FROM [dbo].[users] | covered |
| `VSH168` | `vastbase-sqlserver-hook-regexp-instr-transact-sql` | SELECT REGEXP_INSTR([name], 'a') AS [v] FROM [dbo].[users] | covered |
| `VSH169` | `vastbase-sqlserver-hook-regexp-like-transact-sql` | SELECT REGEXP_LIKE([name], 'a') AS [v] FROM [dbo].[users] | covered |
| `VSH170` | `vastbase-sqlserver-hook-regexp-matches-transact-sql` | SELECT REGEXP_MATCHES([name], 'a') AS [v] FROM [dbo].[users] | covered |
| `VSH171` | `vastbase-sqlserver-hook-regexp-replace-transact-sql` | SELECT REGEXP_REPLACE([name], 'a', 'b') AS [v] FROM [dbo].[users] | covered |
| `VSH172` | `vastbase-sqlserver-hook-regexp-substr-transact-sql` | SELECT REGEXP_SUBSTR([name], 'a') AS [v] FROM [dbo].[users] | covered |
| `VSH173` | `vastbase-sqlserver-hook-remserver-transact-sql` | SELECT @@REMSERVER AS [v] FROM [dbo].[users] | covered |
| `VSH174` | `vastbase-sqlserver-hook-replication-functions-publishingservername` | SELECT PUBLISHINGSERVERNAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH175` | `vastbase-sqlserver-hook-right-shift-transact-sql` | SELECT RIGHT_SHIFT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH176` | `vastbase-sqlserver-hook-rowcount-big-transact-sql` | SELECT ROWCOUNT_BIG(1) AS [v] FROM [dbo].[users] | covered |
| `VSH177` | `vastbase-sqlserver-hook-rowcount-transact-sql` | SELECT @@ROWCOUNT AS [v] FROM [dbo].[users] | covered |
| `VSH178` | `vastbase-sqlserver-hook-schema-id-transact-sql` | SELECT SCHEMA_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH179` | `vastbase-sqlserver-hook-schema-name-transact-sql` | SELECT SCHEMA_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH180` | `vastbase-sqlserver-hook-servername-transact-sql` | SELECT SERVERNAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH181` | `vastbase-sqlserver-hook-serverproperty-transact-sql` | SELECT SERVERPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH182` | `vastbase-sqlserver-hook-servicename-transact-sql` | SELECT SERVICENAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH183` | `vastbase-sqlserver-hook-session-context-transact-sql` | SELECT SESSION_CONTEXT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH184` | `vastbase-sqlserver-hook-session-id-transact-sql` | SELECT SESSION_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH185` | `vastbase-sqlserver-hook-session-user-transact-sql` | SELECT SESSION_USER AS [v] FROM [dbo].[users] | covered |
| `VSH186` | `vastbase-sqlserver-hook-sessionproperty-transact-sql` | SELECT SESSIONPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH187` | `vastbase-sqlserver-hook-set-bit-transact-sql` | SELECT SET_BIT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH188` | `vastbase-sqlserver-hook-signbyasymkey-transact-sql` | SELECT SIGNBYASYMKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH189` | `vastbase-sqlserver-hook-signbycert-transact-sql` | SELECT SIGNBYCERT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH190` | `vastbase-sqlserver-hook-smalldatetimefromparts-transact-sql` | SELECT SMALLDATETIMEFROMPARTS(2024, 1, 2, 3, 4) AS [v] FROM [dbo].[users] | covered |
| `VSH191` | `vastbase-sqlserver-hook-spid-transact-sql` | SELECT @@SPID AS [v] FROM [dbo].[users] | covered |
| `VSH192` | `vastbase-sqlserver-hook-sql-variant-property-transact-sql` | SELECT SQL_VARIANT_PROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH193` | `vastbase-sqlserver-hook-stats-date-transact-sql` | SELECT STATS_DATE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH194` | `vastbase-sqlserver-hook-string-escape-transact-sql` | SELECT STRING_ESCAPE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH195` | `vastbase-sqlserver-hook-suser-id-transact-sql` | SELECT SUSER_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH196` | `vastbase-sqlserver-hook-suser-name-transact-sql` | SELECT SUSER_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH197` | `vastbase-sqlserver-hook-suser-sid-transact-sql` | SELECT SUSER_SID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH198` | `vastbase-sqlserver-hook-suser-sname-transact-sql` | SELECT SUSER_SNAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH199` | `vastbase-sqlserver-hook-switchoffset-transact-sql` | SELECT SWITCHOFFSET(SYSDATETIMEOFFSET(), '-08:00') AS [v] FROM [dbo].[users] | covered |
| `VSH200` | `vastbase-sqlserver-hook-symkeyproperty-transact-sql` | SELECT SYMKEYPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH201` | `vastbase-sqlserver-hook-system-user-transact-sql` | SELECT SYSTEM_USER AS [v] FROM [dbo].[users] | covered |
| `VSH202` | `vastbase-sqlserver-hook-textsize-transact-sql` | SELECT @@TEXTSIZE AS [v] FROM [dbo].[users] | covered |
| `VSH203` | `vastbase-sqlserver-hook-timefromparts-transact-sql` | SELECT TIMEFROMPARTS(3, 4, 5, 0, 7) AS [v] FROM [dbo].[users] | covered |
| `VSH204` | `vastbase-sqlserver-hook-timeticks-transact-sql` | SELECT @@TIMETICKS AS [v] FROM [dbo].[users] | covered |
| `VSH205` | `vastbase-sqlserver-hook-todatetimeoffset-transact-sql` | SELECT TODATETIMEOFFSET(GETDATE(), '-08:00') AS [v] FROM [dbo].[users] | covered |
| `VSH206` | `vastbase-sqlserver-hook-total-errors-transact-sql` | SELECT @@TOTAL_ERRORS AS [v] FROM [dbo].[users] | covered |
| `VSH207` | `vastbase-sqlserver-hook-total-read-transact-sql` | SELECT @@TOTAL_READ AS [v] FROM [dbo].[users] | covered |
| `VSH208` | `vastbase-sqlserver-hook-total-write-transact-sql` | SELECT @@TOTAL_WRITE AS [v] FROM [dbo].[users] | covered |
| `VSH209` | `vastbase-sqlserver-hook-trancount-transact-sql` | SELECT @@TRANCOUNT AS [v] FROM [dbo].[users] | covered |
| `VSH210` | `vastbase-sqlserver-hook-translate-transact-sql` | SELECT TRANSLATE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH211` | `vastbase-sqlserver-hook-trigger-nestlevel-transact-sql` | SELECT TRIGGER_NESTLEVEL(1) AS [v] FROM [dbo].[users] | covered |
| `VSH212` | `vastbase-sqlserver-hook-try-cast-transact-sql` | SELECT TRY_CAST('42' AS INT) AS [v] FROM [dbo].[users] | covered |
| `VSH213` | `vastbase-sqlserver-hook-try-convert-transact-sql` | SELECT TRY_CONVERT(INT, '42', 0) AS [v] FROM [dbo].[users] | covered |
| `VSH214` | `vastbase-sqlserver-hook-try-parse-transact-sql` | SELECT TRY_PARSE('42' AS INT USING 'en-US') AS [v] FROM [dbo].[users] | covered |
| `VSH215` | `vastbase-sqlserver-hook-type-id-transact-sql` | SELECT TYPE_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH216` | `vastbase-sqlserver-hook-type-name-transact-sql` | SELECT TYPE_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH217` | `vastbase-sqlserver-hook-typeproperty-transact-sql` | SELECT TYPEPROPERTY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH218` | `vastbase-sqlserver-hook-unistr-transact-sql` | SELECT UNISTR('abc') AS [v] FROM [dbo].[users] | covered |
| `VSH219` | `vastbase-sqlserver-hook-user-id-transact-sql` | SELECT USER_ID(1) AS [v] FROM [dbo].[users] | covered |
| `VSH220` | `vastbase-sqlserver-hook-user-name-transact-sql` | SELECT USER_NAME(1) AS [v] FROM [dbo].[users] | covered |
| `VSH221` | `vastbase-sqlserver-hook-user-transact-sql` | SELECT USER AS [v] FROM [dbo].[users] | covered |
| `VSH222` | `vastbase-sqlserver-hook-vector-distance-transact-sql` | SELECT VECTOR_DISTANCE('[1,2]', '[2,3]') AS [v] FROM [dbo].[users] | covered |
| `VSH223` | `vastbase-sqlserver-hook-vector-norm-transact-sql` | SELECT VECTOR_NORM('[1,2]') AS [v] FROM [dbo].[users] | covered |
| `VSH224` | `vastbase-sqlserver-hook-vector-normalize-transact-sql` | SELECT VECTOR_NORMALIZE('[1,2]') AS [v] FROM [dbo].[users] | covered |
| `VSH225` | `vastbase-sqlserver-hook-vectorproperty-transact-sql` | SELECT VECTORPROPERTY('[1,2]', 'dimensions') AS [v] FROM [dbo].[users] | covered |
| `VSH226` | `vastbase-sqlserver-hook-verifysignedbyasymkey-transact-sql` | SELECT VERIFYSIGNEDBYASYMKEY(1) AS [v] FROM [dbo].[users] | covered |
| `VSH227` | `vastbase-sqlserver-hook-verifysignedbycert-transact-sql` | SELECT VERIFYSIGNEDBYCERT(1) AS [v] FROM [dbo].[users] | covered |
| `VSH228` | `vastbase-sqlserver-hook-version-transact-sql-configuration-functions` | SELECT @@VERSION AS [v] FROM [dbo].[users] | covered |
| `VSH229` | `vastbase-sqlserver-hook-version-transact-sql-metadata-functions` | SELECT VERSION_TRANSACT_SQL(1) AS [v] FROM [dbo].[users] | covered |
| `VSH230` | `vastbase-sqlserver-hook-xact-state-transact-sql` | SELECT XACT_STATE(1) AS [v] FROM [dbo].[users] | covered |
| `VSH231` | `vastbase-sqlserver-hook-collation-precedence-transact-sql` | SELECT [name] COLLATE Latin1_General_CI_AS AS [name] FROM [dbo].[users] | covered |
| `VSH232` | `vastbase-sqlserver-hook-collations` | SELECT [name] COLLATE Latin1_General_CI_AS AS [name] FROM [dbo].[users] | covered |
| `VSH233` | `vastbase-sqlserver-hook-rename-transact-sql` | RENAME OBJECT [dbo].[old_users] TO [new_users] | covered |
| `VSH234` | `vastbase-sqlserver-hook-sql-server-collation-name-transact-sql` | SELECT [name] COLLATE Latin1_General_CI_AS AS [name] FROM [dbo].[users] | covered |
| `VSH235` | `vastbase-sqlserver-hook-windows-collation-name-transact-sql` | SELECT [name] COLLATE Latin1_General_CI_AS AS [name] FROM [dbo].[users] | covered |
