#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"

#define SQLPARSER_ARRAY_LEN(array_value) \
	(sizeof(array_value) / sizeof((array_value)[0]))

typedef struct {
	const char *sql;
	sqlparser_bind_kind_t kind;
} expected_bind_t;

typedef struct {
	const char *name;
	const char *sql;
	const expected_bind_t *expected;
	size_t expected_count;
} mirrored_case_t;

static sqlparser_handle_t *parse_case(
	const char *name,
	sqlparser_dialect_t dialect,
	const char *sql)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		fprintf(
			stderr,
			"FAIL [%s %s]: parse failed: status=%d message=%s\n",
			sqlparser_dialect_name(dialect),
			name,
			(int)status,
			error.message);
		sqlparser_handle_destroy(handle);
		return NULL;
	}
	return handle;
}

static int verify_cache(
	const char *name,
	sqlparser_handle_t *handle,
	const expected_bind_t *expected,
	size_t expected_count)
{
	sqlparser_bind_occurrence_cache_t *cache;
	sqlparser_bind_occurrence_cache_t *first_cache;
	sqlparser_error_t error;
	const char *current_sql;
	const char *cache_text;
	size_t current_length;
	size_t index;
	size_t previous_end;
	size_t text_cursor;
	sqlparser_status_t status;

	memset(&error, 0, sizeof(error));
	status = sqlparser_handle_ensure_bind_occurrences(handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle->bind_occurrences == NULL) {
		fprintf(
			stderr,
			"FAIL [%s]: cache build failed: status=%d message=%s\n",
			name,
			(int)status,
			error.message);
		return 1;
	}
	cache = handle->bind_occurrences;
	if (handle->bind_occurrences_generation != handle->generation ||
	    cache->count != expected_count) {
		fprintf(
			stderr,
			"FAIL [%s]: generation/count mismatch: generation=%lu "
			"cache_generation=%lu expected_count=%lu actual_count=%lu\n",
			name,
			handle->generation,
			handle->bind_occurrences_generation,
			(unsigned long)expected_count,
			(unsigned long)cache->count);
		return 1;
	}
	cache_text = (char *)cache + sizeof(*cache) +
		cache->count * sizeof(*cache->items);
	current_sql = sqlparser_effective_sql(handle);
	if (current_sql == NULL) {
		fprintf(stderr, "FAIL [%s]: current SQL is missing\n", name);
		return 1;
	}
	current_length = strlen(current_sql);
	previous_end = 0U;
	text_cursor = 0U;
	for (index = 0U; index < expected_count; index++) {
		const sqlparser_bind_occurrence_cache_item_t *item;
		const char *token_sql;
		size_t token_length;

		item = &cache->items[index];
		token_sql = cache_text + item->text_offset;
		token_length = strlen(expected[index].sql);
		if (item->kind != expected[index].kind ||
		    item->text_offset != text_cursor ||
		    item->source_start < previous_end ||
		    token_length == 0U ||
		    token_length > current_length ||
		    item->source_start > current_length - token_length ||
		    strcmp(token_sql, expected[index].sql) != 0 ||
		    memcmp(
			    current_sql + item->source_start,
			    expected[index].sql,
			    token_length) != 0) {
			fprintf(
				stderr,
				"FAIL [%s]: occurrence %lu mismatch: expected=%s "
				"actual=%s kind=%d span=%lu..%lu\n",
				name,
				(unsigned long)(index + 1U),
				expected[index].sql,
				token_sql,
				(int)item->kind,
				(unsigned long)item->source_start,
				(unsigned long)(item->source_start + token_length));
			return 1;
		}
		previous_end = item->source_start + token_length;
		text_cursor += token_length + 1U;
	}

	first_cache = cache;
	memset(&error, 0, sizeof(error));
	status = sqlparser_handle_ensure_bind_occurrences(handle, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->bind_occurrences != first_cache) {
		fprintf(
			stderr,
			"FAIL [%s]: same-generation cache was rebuilt: status=%d\n",
			name,
			(int)status);
		return 1;
	}
	return 0;
}

static int run_case(
	const char *name,
	sqlparser_dialect_t dialect,
	const char *sql,
	const expected_bind_t *expected,
	size_t expected_count)
{
	sqlparser_handle_t *handle;
	int failed;

	handle = parse_case(name, dialect, sql);
	if (handle == NULL) {
		return 1;
	}
	failed = verify_cache(name, handle, expected, expected_count);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int test_dialect_boundaries(void)
{
	static const expected_bind_t pg_expected[] = {
		{"$1", SQLPARSER_BIND_KIND_POSITIONAL},
		{"$1", SQLPARSER_BIND_KIND_POSITIONAL},
		{"$7", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t mysql_expected[] = {
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t mysql_executable_expected[] = {
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t mysql_unexpanded_expected[] = {
		{"?", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t oracle_expected[] = {
		{":pkg.arg", SQLPARSER_BIND_KIND_NAMED},
		{":1", SQLPARSER_BIND_KIND_POSITIONAL},
		{":2", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{":pkg.arg", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t vo_comment_expected[] = {
		{":between", SQLPARSER_BIND_KIND_NAMED},
		{":after", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t vo_pbe_expected[] = {
		{"$1", SQLPARSER_BIND_KIND_POSITIONAL},
		{"$2", SQLPARSER_BIND_KIND_POSITIONAL},
		{"$1", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t oracle_dynamic_expected[] = {
		{":name", SQLPARSER_BIND_KIND_NAMED},
		{":id", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t dameng_prepared_expected[] = {
		{":id", SQLPARSER_BIND_KIND_NAMED},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const sqlparser_dialect_t pg_dialects[] = {
		SQLPARSER_DIALECT_POSTGRESQL,
		SQLPARSER_DIALECT_VASTBASE_POSTGRESQL
	};
	static const sqlparser_dialect_t mysql_dialects[] = {
		SQLPARSER_DIALECT_MYSQL,
		SQLPARSER_DIALECT_VASTBASE_MYSQL
	};
	static const sqlparser_dialect_t oracle_dialects[] = {
		SQLPARSER_DIALECT_ORACLE,
		SQLPARSER_DIALECT_DAMENG,
		SQLPARSER_DIALECT_VASTBASE_ORACLE
	};
	size_t index;
	int failed;

	failed = 0;
	for (index = 0U; index < SQLPARSER_ARRAY_LEN(pg_dialects); index++) {
		failed |= run_case(
			"postgresql-boundaries-and-protected-text",
			pg_dialects[index],
			"SELECT $0, $01, $1, \"$2\", $标签$ $3 ? $标签$, $1, "
			"foo$tag$ + $7 + foo$tag$, "
			"'$4' /* $5 */, '{}'::jsonb ? 'key', "
			"'{}'::jsonb ?| ARRAY['key'], "
			"'{}'::jsonb ?& ARRAY['key']",
			pg_expected,
			SQLPARSER_ARRAY_LEN(pg_expected));
	}
	for (index = 0U; index < SQLPARSER_ARRAY_LEN(mysql_dialects); index++) {
		failed |= run_case(
			"mysql-boundaries-and-protected-text",
			mysql_dialects[index],
			"SELECT $1, ?, `?`, '?', \"?\", ? /* ? */ # ?",
			mysql_expected,
			SQLPARSER_ARRAY_LEN(mysql_expected));
		failed |= run_case(
			"mysql-whole-and-inline-executable-comments",
			mysql_dialects[index],
			"/*!50000 SELECT ?, '?', ? */; "
			"/*!500000 SELECT ? */; SELECT ? /*! + ? */",
			mysql_executable_expected,
			SQLPARSER_ARRAY_LEN(mysql_executable_expected));
		failed |= run_case(
			"mysql-unexpanded-executable-comment",
			mysql_dialects[index],
			"/*!1234 SELECT ? */; SELECT ?",
			mysql_unexpanded_expected,
			SQLPARSER_ARRAY_LEN(mysql_unexpanded_expected));
		failed |= run_case(
			"mysql-unexpanded-executable-comment-with-semicolon",
			mysql_dialects[index],
			"/*!50000 SELECT ?; SELECT ? */; SELECT ?",
			mysql_unexpanded_expected,
			SQLPARSER_ARRAY_LEN(mysql_unexpanded_expected));
		failed |= run_case(
			"mysql-user-variable-is-not-bind",
			mysql_dialects[index],
			"EXECUTE stmt USING @user_id",
			NULL,
			0U);
	}
	for (index = 0U; index < SQLPARSER_ARRAY_LEN(oracle_dialects); index++) {
		failed |= run_case(
			"oracle-boundaries-and-protected-text",
			oracle_dialects[index],
			"SELECT :pkg.arg, CAST(:1 AS NUMBER), :2.field, ?, \"?:fake\", "
			"q'[? :fake]', :pkg.arg FROM dual /* :comment */",
			oracle_expected,
			SQLPARSER_ARRAY_LEN(oracle_expected));
	}
	failed |= run_case(
		"vastbase-oracle-nested-comment",
		SQLPARSER_DIALECT_VASTBASE_ORACLE,
		"SELECT 1 /* outer /* :fake */ + :between -- */\n"
		", :after FROM dual",
		vo_comment_expected,
		SQLPARSER_ARRAY_LEN(vo_comment_expected));
	failed |= run_case(
		"vastbase-oracle-pbe-dollar-repeat",
		SQLPARSER_DIALECT_VASTBASE_ORACLE,
		"SELECT $1, COALESCE($2, 0), $1 FROM dual",
		vo_pbe_expected,
		SQLPARSER_ARRAY_LEN(vo_pbe_expected));
	failed |= run_case(
		"oracle-dynamic-sql-using-binds",
		SQLPARSER_DIALECT_ORACLE,
		"EXECUTE IMMEDIATE 'UPDATE users SET name = :fake "
		"WHERE id = ?' USING :name, :id",
		oracle_dynamic_expected,
		SQLPARSER_ARRAY_LEN(oracle_dynamic_expected));
	failed |= run_case(
		"dameng-prepared-sql-using-binds",
		SQLPARSER_DIALECT_DAMENG,
		"EXEC SQL PREPARE stmt FROM 'SELECT :fake, ? FROM users'; "
		"EXEC SQL EXECUTE stmt USING :id, ?",
		dameng_prepared_expected,
		SQLPARSER_ARRAY_LEN(dameng_prepared_expected));
	return failed;
}

static int test_sqlserver_mirrors(void)
{
	static const expected_bind_t boundary_expected[] = {
		{"@#x", SQLPARSER_BIND_KIND_NAMED},
		{"@a@b", SQLPARSER_BIND_KIND_NAMED},
		{"@1", SQLPARSER_BIND_KIND_NAMED},
		{"@value", SQLPARSER_BIND_KIND_NAMED},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL},
		{"?", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t output_expected[] = {
		{"@new_city", SQLPARSER_BIND_KIND_NAMED},
		{"@out", SQLPARSER_BIND_KIND_NAMED},
		{"@customer_id", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t prepared_expected[] = {
		{"@value", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t prepared_receiver_expected[] = {
		{"@handle", SQLPARSER_BIND_KIND_NAMED},
		{"@receiver", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t assignment_expected[] = {
		{"@value", SQLPARSER_BIND_KIND_NAMED},
		{"@next", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t anonymous_comparison_expected[] = {
		{"?", SQLPARSER_BIND_KIND_POSITIONAL}
	};
	static const expected_bind_t execute_as_expected[] = {
		{"@cookie", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t control_units_expected[] = {
		{"@enabled", SQLPARSER_BIND_KIND_NAMED},
		{"@handle", SQLPARSER_BIND_KIND_NAMED},
		{"@receiver", SQLPARSER_BIND_KIND_NAMED},
		{"@value", SQLPARSER_BIND_KIND_NAMED},
		{"@out", SQLPARSER_BIND_KIND_NAMED}
	};
	static const expected_bind_t go_expected[] = {
		{"@first", SQLPARSER_BIND_KIND_NAMED},
		{"@second", SQLPARSER_BIND_KIND_NAMED}
	};
	static const mirrored_case_t cases[] = {
		{
			"candidate-boundaries",
			"SELECT @@ROWCOUNT, [@fake], '@fake', @#x, @a@b, "
			"@1, @value, /* outer /* @fake */ tail */ "
			"?| 1, ?& 1, ?",
			boundary_expected,
			SQLPARSER_ARRAY_LEN(boundary_expected)
		},
		{
			"output-sink",
			"UPDATE dbo.customer_profiles SET city = @new_city "
			"OUTPUT @out, DELETED.city, INSERTED.city "
			"INTO @profile_audit "
			"(old_city, new_city) WHERE customer_id = @customer_id",
			output_expected,
			SQLPARSER_ARRAY_LEN(output_expected)
		},
		{
			"prepared-named-label",
			"EXEC sp_executesql N'SELECT * FROM users WHERE id=@p1', "
			"N'@p1 int', @p1=@value OUTPUT",
			prepared_expected,
			SQLPARSER_ARRAY_LEN(prepared_expected)
		},
		{
			"prepared-output-receiver",
			"EXEC sp_prepare @handle OUTPUT, "
			"@label=@receiver OUTPUT",
			prepared_receiver_expected,
			SQLPARSER_ARRAY_LEN(prepared_receiver_expected)
		},
		{
			"select-assignment-target",
			"SELECT TOP (1) @target += @value, @plain = @next",
			assignment_expected,
			SQLPARSER_ARRAY_LEN(assignment_expected)
		},
		{
			"select-anonymous-comparison",
			"SELECT TOP (1) ? = a FROM dbo.t",
			anonymous_comparison_expected,
			SQLPARSER_ARRAY_LEN(anonymous_comparison_expected)
		},
		{
			"execute-as-cookie-receiver",
			"EXECUTE AS USER = 'app_reader' WITH COOKIE INTO @cookie",
			execute_as_expected,
			SQLPARSER_ARRAY_LEN(execute_as_expected)
		},
		{
			"control-unit-role-reset",
			"IF @enabled = 1 BEGIN\n"
			"EXEC sp_prepare @handle OUTPUT, N'@p int', "
			"N'SELECT @p', @label=@receiver OUTPUT\n"
			"UPDATE dbo.t SET v=@value OUTPUT @out INTO @sink\nEND",
			control_units_expected,
			SQLPARSER_ARRAY_LEN(control_units_expected)
		},
		{
			"same-line-control-unit-role-reset",
			"IF @enabled = 1 EXEC sp_prepare @handle OUTPUT, "
			"N'@p int', N'SELECT @p', @label=@receiver OUTPUT "
			"ELSE UPDATE dbo.t SET v=@value OUTPUT @out INTO @sink",
			control_units_expected,
			SQLPARSER_ARRAY_LEN(control_units_expected)
		},
		{
			"go-global-order",
			"SELECT @first\nGO\nSELECT @second",
			go_expected,
			SQLPARSER_ARRAY_LEN(go_expected)
		}
	};
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	size_t case_index;
	size_t dialect_index;
	int failed;

	failed = 0;
	for (dialect_index = 0U;
	     dialect_index < SQLPARSER_ARRAY_LEN(dialects);
	     dialect_index++) {
		for (case_index = 0U;
		     case_index < SQLPARSER_ARRAY_LEN(cases);
		     case_index++) {
			char name[128];
			int length;

			length = snprintf(
				name,
				sizeof(name),
				"%s-%s",
				sqlparser_dialect_name(dialects[dialect_index]),
				cases[case_index].name);
			if (length < 0 || (size_t)length >= sizeof(name)) {
				fprintf(stderr, "FAIL: SQL Server test name is too long\n");
				failed = 1;
				continue;
			}
			failed |= run_case(
				name,
				dialects[dialect_index],
				cases[case_index].sql,
				cases[case_index].expected,
				cases[case_index].expected_count);
		}
	}
	return failed;
}

static int test_empty_and_long(void)
{
	static const char prefix[] = "SELECT ";
	static const char suffix[] = " FROM dual";
	expected_bind_t expected;
	char *sql;
	char *token;
	size_t name_length;
	size_t sql_length;
	int failed;

	failed = run_case(
		"empty",
		SQLPARSER_DIALECT_POSTGRESQL,
		"SELECT 1",
		NULL,
		0U);
	name_length = 1024U;
	token = (char *)malloc(name_length + 2U);
	sql_length = sizeof(prefix) - 1U + name_length + 1U +
		(sizeof(suffix) - 1U);
	sql = (char *)malloc(sql_length + 1U);
	if (token == NULL || sql == NULL) {
		free(sql);
		free(token);
		fprintf(stderr, "FAIL [long]: out of memory\n");
		return 1;
	}
	token[0] = ':';
	memset(token + 1U, 'a', name_length);
	token[name_length + 1U] = '\0';
	memcpy(sql, prefix, sizeof(prefix) - 1U);
	memcpy(sql + sizeof(prefix) - 1U, token, name_length + 1U);
	memcpy(
		sql + sizeof(prefix) - 1U + name_length + 1U,
		suffix,
		sizeof(suffix));
	expected.sql = token;
	expected.kind = SQLPARSER_BIND_KIND_NAMED;
	failed |= run_case(
		"oracle-long-bind",
		SQLPARSER_DIALECT_ORACLE,
		sql,
		&expected,
		1U);
	free(sql);
	free(token);
	return failed;
}

static int test_sqlserver_anonymous_marker_position(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	sqlparser_error_t error;
	size_t dialect_index;
	int failed;

	failed = 0;
	for (dialect_index = 0U;
	     dialect_index < SQLPARSER_ARRAY_LEN(dialects);
	     dialect_index++) {
		sqlparser_graph_value_t value;
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		size_t bind_count;
		size_t value_index;
		sqlparser_status_t status;

		handle = parse_case(
			"sqlserver-anonymous-marker-position",
			dialects[dialect_index],
			"SELECT TOP (1) ? = a FROM dbo.t");
		if (handle == NULL) {
			failed = 1;
			continue;
		}
		memset(&error, 0, sizeof(error));
		memset(&graph, 0, sizeof(graph));
		status = sqlparser_statement_query_graph(
			handle,
			0U,
			&graph,
			&error);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL [%s anonymous marker]: graph failed: "
				"status=%d message=%s\n",
				sqlparser_dialect_name(dialects[dialect_index]),
				(int)status,
				error.message);
			failed = 1;
			sqlparser_handle_destroy(handle);
			continue;
		}
		bind_count = 0U;
		for (value_index = 0U;
		     value_index < graph.value_count;
		     value_index++) {
			memset(&value, 0, sizeof(value));
			status = sqlparser_query_graph_value_at(
				&graph,
				value_index,
				&value,
				&error);
			if (status != SQLPARSER_STATUS_OK) {
				failed = 1;
				break;
			}
			if (!value.has_bind) {
				continue;
			}
			bind_count++;
			if (value.bind_kind != SQLPARSER_BIND_KIND_POSITIONAL ||
			    !value.has_bind_sql || strcmp(value.bind_sql, "?") != 0 ||
			    !value.has_bind_position || value.bind_position != 1U) {
				failed = 1;
				break;
			}
		}
		if (bind_count != 1U) {
			fprintf(
				stderr,
				"FAIL [%s anonymous marker]: expected one bind, got %lu\n",
				sqlparser_dialect_name(dialects[dialect_index]),
				(unsigned long)bind_count);
			failed = 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int test_large_dollar_token(void)
{
	static const char sql[] =
		"$999999999999999999999999999999999999999999999999999999999999";
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_POSTGRESQL,
		SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		SQLPARSER_DIALECT_VASTBASE_ORACLE
	};
	sqlparser_bind_scanner_t scanner;
	sqlparser_bind_token_t token;
	size_t index;
	size_t length;
	int failed;

	failed = 0;
	length = sizeof(sql) - 1U;
	for (index = 0U; index < SQLPARSER_ARRAY_LEN(dialects); index++) {
		sqlparser_bind_scanner_init(&scanner, dialects[index], sql);
		if (!sqlparser_bind_scanner_next(&scanner, &token) ||
		    token.start != 0U || token.end != length ||
		    token.key_start != 1U || token.key_length != length - 1U ||
		    token.kind != SQLPARSER_BIND_KIND_POSITIONAL ||
		    sqlparser_bind_scanner_next(&scanner, &token)) {
			fprintf(
				stderr,
				"FAIL [%s large-dollar]: token was truncated or rejected\n",
				sqlparser_dialect_name(dialects[index]));
			failed = 1;
		}
	}
	sqlparser_bind_scanner_init(&scanner, SQLPARSER_DIALECT_MYSQL, sql);
	if (sqlparser_bind_scanner_next(&scanner, &token)) {
		fprintf(stderr, "FAIL [mysql large-dollar]: raw dollar token accepted\n");
		failed = 1;
	}
	sqlparser_bind_scanner_init_markers(
		&scanner,
		SQLPARSER_DIALECT_MYSQL,
		sql,
		1U,
		1U);
	if (sqlparser_bind_scanner_next(&scanner, &token)) {
		fprintf(stderr, "FAIL [mysql large-dollar]: overflowing marker accepted\n");
		failed = 1;
	}
	return failed;
}

static int test_clone_is_lazy(void)
{
	static const expected_bind_t expected[] = {
		{":same", SQLPARSER_BIND_KIND_NAMED},
		{":same", SQLPARSER_BIND_KIND_NAMED}
	};
	sqlparser_bind_occurrence_cache_t *source_cache;
	sqlparser_error_t error;
	sqlparser_handle_t *clone;
	sqlparser_handle_t *source;
	sqlparser_status_t status;
	int failed;

	source = parse_case(
		"clone-lazy",
		SQLPARSER_DIALECT_ORACLE,
		"SELECT :same, :same FROM dual");
	if (source == NULL) {
		return 1;
	}
	failed = verify_cache(
		"clone-source",
		source,
		expected,
		SQLPARSER_ARRAY_LEN(expected));
	if (failed) {
		sqlparser_handle_destroy(source);
		return 1;
	}
	source_cache = source->bind_occurrences;
	clone = NULL;
	memset(&error, 0, sizeof(error));
	status = sqlparser_handle_clone(source, &clone, &error);
	if (status != SQLPARSER_STATUS_OK || clone == NULL ||
	    clone->bind_occurrences != NULL ||
	    clone->bind_occurrences_generation != 0UL) {
		fprintf(
			stderr,
			"FAIL [clone-lazy]: clone copied cache: status=%d message=%s\n",
			(int)status,
			error.message);
		failed = 1;
	} else {
		failed = verify_cache(
			"clone-built",
			clone,
			expected,
			SQLPARSER_ARRAY_LEN(expected));
		if (!failed &&
		    (clone->bind_occurrences == source_cache ||
		     source->bind_occurrences != source_cache)) {
			fprintf(
				stderr,
				"FAIL [clone-lazy]: source and clone cache ownership overlap\n");
			failed = 1;
		}
	}
	sqlparser_handle_destroy(clone);
	sqlparser_handle_destroy(source);
	return failed;
}

int main(void)
{
	int failed;

	failed = 0;
	failed |= test_dialect_boundaries();
	failed |= test_sqlserver_mirrors();
	failed |= test_empty_and_long();
	failed |= test_sqlserver_anonymous_marker_position();
	failed |= test_large_dollar_token();
	failed |= test_clone_is_lazy();
	if (failed) {
		return 1;
	}
	printf("bind occurrence internal tests passed\n");
	return 0;
}
