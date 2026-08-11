#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

typedef struct {
	const char *name;
	sqlparser_dialect_t dialect;
	const char *input_sql;
	const char *selector;
	const char *patch_sql;
	const char *expected_sql;
} sqlparser_surface_case_t;

typedef struct {
	const char *name;
	const char *input_sql;
	sqlparser_patch_op_t fragment_op;
	const char *fragment_selector;
	size_t fragment_index;
	const char *old_fragment_sql;
	const char *current_fragment_sql;
	const char *relation_selector;
	const char *relation_sql;
	const char *current_expected_sql;
	const char *old_after_relation_expected_sql;
} sqlparser_fragment_relation_case_t;

typedef struct {
	const char *name;
	sqlparser_dialect_t dialects[2];
	size_t dialect_count;
	const char *input_sql;
	const char *target_selector;
	const char *target_sql;
	const char *local_sql;
	size_t relation_index;
	const char *new_schema;
	const char *new_table;
	const char *fallback_sql;
} sqlparser_pagination_fallback_case_t;

static int sqlparser_surface_verify_closure(
	const sqlparser_handle_t *patched,
	const char *sql,
	const char *name);

static int sqlparser_surface_apply(
	sqlparser_dialect_t dialect,
	const char *input_sql,
	const char *selector,
	const char *patch_sql,
	sqlparser_handle_t **out_handle,
	char **out_sql)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	sqlparser_status_t status;

	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	handle = NULL;
	*out_handle = NULL;
	*out_sql = NULL;
	status = sqlparser_parse_with_options(
		input_sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", error.message);
		return 1;
	}
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector;
	patch.sql = patch_sql;
	patch_list.items = &patch;
	patch_list.count = 1U;
	status = sqlparser_apply_patch(handle, &patch_list, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, out_sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "patch/deparse failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	*out_handle = handle;
	return 0;
}

static int sqlparser_surface_run_exact(
	const sqlparser_surface_case_t *test_case)
{
	sqlparser_handle_t *handle;
	char *sql;
	int failed;

	handle = NULL;
	sql = NULL;
	if (sqlparser_surface_apply(
		    test_case->dialect,
		    test_case->input_sql,
		    test_case->selector,
		    test_case->patch_sql,
		    &handle,
		    &sql) != 0) {
		fprintf(stderr, "FAIL: %s\n", test_case->name);
		return 1;
	}
	failed = strcmp(sql, test_case->expected_sql) != 0;
	if (failed) {
		fprintf(
			stderr,
			"FAIL: %s\nexpected: %s\nactual:   %s\n",
			test_case->name,
			test_case->expected_sql,
			sql);
	}
	failed |= sqlparser_surface_verify_closure(
		handle, sql, test_case->name);
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_surface_verify_closure(
	const sqlparser_handle_t *patched,
	const char *sql,
	const char *name)
{
	sqlparser_error_t error;
	sqlparser_handle_t *fresh;
	sqlparser_parse_options_t options;
	char *fresh_sql;
	char *patched_json;
	char *fresh_json;
	sqlparser_status_t status;
	int failed;

	fresh = NULL;
	fresh_sql = NULL;
	patched_json = NULL;
	fresh_json = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = sqlparser_handle_dialect(patched);
	status = sqlparser_parse_with_options(sql, &options, &fresh, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(fresh, &fresh_sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || fresh_sql == NULL ||
	    strcmp(sql, fresh_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s fresh generation-0 round trip: %s (%s)\n",
			name,
			fresh_sql != NULL ? fresh_sql : "<null>",
			error.message);
		failed = 1;
		goto cleanup;
	}
	status = sqlparser_export_view_json(patched, 0, &patched_json, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_export_view_json(fresh, 0, &fresh_json, &error);
	}
	if (status != SQLPARSER_STATUS_OK || patched_json == NULL ||
	    fresh_json == NULL || strcmp(patched_json, fresh_json) != 0) {
		fprintf(
			stderr,
			"FAIL: %s patched/fresh View and Query Graph differ: %s\n",
			name,
			error.message);
		failed = 1;
	}

cleanup:
	if (fresh_json != NULL) {
		sqlparser_string_free(fresh_json);
	}
	if (patched_json != NULL) {
		sqlparser_string_free(patched_json);
	}
	if (fresh_sql != NULL) {
		sqlparser_string_free(fresh_sql);
	}
	if (fresh != NULL) {
		sqlparser_handle_destroy(fresh);
	}
	return failed;
}

static size_t sqlparser_surface_count(const char *sql, const char *token)
{
	size_t count;
	size_t token_length;

	count = 0U;
	token_length = strlen(token);
	while ((sql = strstr(sql, token)) != NULL) {
		count++;
		sql += token_length;
	}
	return count;
}

static int sqlparser_surface_test_mysql_join_owner(void)
{
	sqlparser_handle_t *handle;
	char *sql;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = sqlparser_surface_apply(
		SQLPARSER_DIALECT_MYSQL,
		"SELECT * FROM table_a USE INDEX (idx_a) STRAIGHT_JOIN table_b USE INDEX (idx_b) ON table_a.id = table_b.id",
		"stmt[0].select_target[0][0]",
		"(SELECT x FROM inner_a JOIN inner_b ON inner_a.id = inner_b.id) AS nested_value",
		&handle,
		&sql);
	if (failed == 0 &&
	    (sqlparser_surface_count(sql, "STRAIGHT_JOIN") != 1U ||
	     strstr(sql, "inner_a JOIN inner_b") == NULL ||
	     strstr(sql, "table_a USE INDEX (idx_a)") == NULL ||
	     strstr(sql, "table_b USE INDEX (idx_b)") == NULL)) {
		fprintf(stderr, "FAIL: MySQL JOIN owner isolation: %s\n", sql);
		failed = 1;
	}
	if (handle != NULL && sql != NULL) {
		failed |= sqlparser_surface_verify_closure(
			handle, sql, "MySQL JOIN owner isolation");
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	if (handle != NULL) {
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int sqlparser_surface_test_structured_literal_owner(
	sqlparser_dialect_t dialect,
	const char *name)
{
	static const char input_sql[] =
		"SELECT \"same\" AS value_col FROM users WHERE status = n'same'";
	static const char after_literal_sql[] =
		"SELECT 'same' AS value_col FROM users WHERE status = n'same'";
	static const char after_where_literal_sql[] =
		"SELECT 'same' AS value_col FROM users WHERE status = 'same'";
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_literal_value_t replacement;
	sqlparser_parse_options_t options;
	char *sql;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	memset(&replacement, 0, sizeof(replacement));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(
		input_sql, &options, &handle, &error);
	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "same";
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_statement_set_literal(
			handle, 0U, 0U, &replacement, &error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || sql == NULL ||
	    strcmp(sql, after_literal_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s structured literal owner: %s (%s)\n",
			name,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
	} else {
		failed |= sqlparser_surface_verify_closure(handle, sql, name);
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
		sql = NULL;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_statement_where_set_literal(
			handle, 0U, 0U, &replacement, &error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_deparse(handle, &sql, &error);
		}
		if (status != SQLPARSER_STATUS_OK || sql == NULL ||
		    strcmp(sql, after_where_literal_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: %s structured WHERE literal owner: %s (%s)\n",
				name,
				sql != NULL ? sql : "<null>",
				error.message);
			failed = 1;
		} else {
			failed |= sqlparser_surface_verify_closure(
				handle, sql, name);
		}
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	if (handle != NULL) {
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int sqlparser_surface_test_public_string_fragments(
	sqlparser_dialect_t dialect,
	const char *name)
{
	static const char input_sql[] =
		"SELECT 'same' AS plain_value, n'same' AS lower_national, "
		"N'same' AS upper_national, 'path\\\\file' AS slash_value "
		"FROM users";
	static const char *const expected_targets[] = {
		"'same' AS plain_value",
		"n'same' AS lower_national",
		"N'same' AS upper_national",
		"'path\\\\file' AS slash_value"
	};
	static const char expected_sql[] =
		"SELECT 'same' AS plain_value, n'same' AS lower_national, "
		"N'same' AS upper_national, 'path\\\\file' AS slash_value "
		"FROM `patched_users`";
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	char *sql;
	size_t index;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(
		input_sql, &options, &handle, &error);
	for (index = 0U;
	     status == SQLPARSER_STATUS_OK &&
	     index < sizeof(expected_targets) / sizeof(expected_targets[0]);
	     index++) {
		status = sqlparser_select_target_sql(
			handle, 0U, 0U, index, &sql, &error);
		if (status != SQLPARSER_STATUS_OK || sql == NULL ||
		    strcmp(sql, expected_targets[index]) != 0) {
			fprintf(
				stderr,
				"FAIL: %s generation-0 target %zu: %s (%s)\n",
				name,
				index,
				sql != NULL ? sql : "<null>",
				error.message);
			failed = 1;
		}
		if (sql != NULL) {
			sqlparser_string_free(sql);
			sql = NULL;
		}
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].relation[0]";
	patch.sql = "`patched_users`";
	patch_list.items = &patch;
	patch_list.count = 1U;
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_apply_patch(handle, &patch_list, &error);
	}
	for (index = 0U;
	     status == SQLPARSER_STATUS_OK &&
	     index < sizeof(expected_targets) / sizeof(expected_targets[0]);
	     index++) {
		status = sqlparser_select_target_sql(
			handle, 0U, 0U, index, &sql, &error);
		if (status != SQLPARSER_STATUS_OK || sql == NULL ||
		    strcmp(sql, expected_targets[index]) != 0) {
			fprintf(
				stderr,
				"FAIL: %s patched target %zu: %s (%s)\n",
				name,
				index,
				sql != NULL ? sql : "<null>",
				error.message);
			failed = 1;
		}
		if (sql != NULL) {
			sqlparser_string_free(sql);
			sql = NULL;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || sql == NULL ||
	    strcmp(sql, expected_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s patched deparse: %s (%s)\n",
			name,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
	} else {
		failed |= sqlparser_surface_verify_closure(handle, sql, name);
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	if (handle != NULL) {
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int sqlparser_surface_test_set_operators(
	sqlparser_dialect_t dialect,
	const char *name)
{
	sqlparser_handle_t *handle;
	char *sql;
	const char *except_pos;
	const char *minus_pos;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = sqlparser_surface_apply(
		dialect,
		"SELECT 1 AS value_col FROM dual EXCEPT SELECT 2 AS value_col FROM dual MINUS SELECT 3 AS value_col FROM dual",
		"stmt[0].select_target[0][0]",
		"0 AS value_col",
		&handle,
		&sql);
	except_pos = failed == 0 ? strstr(sql, " EXCEPT ") : NULL;
	minus_pos = failed == 0 ? strstr(sql, " MINUS ") : NULL;
	if (failed == 0 &&
	    (except_pos == NULL || minus_pos == NULL || except_pos >= minus_pos ||
	     sqlparser_surface_count(sql, " EXCEPT ") != 1U ||
	     sqlparser_surface_count(sql, " MINUS ") != 1U)) {
		fprintf(stderr, "FAIL: %s mixed EXCEPT/MINUS owners: %s\n", name, sql);
		failed = 1;
	}
	if (handle != NULL && sql != NULL) {
		failed |= sqlparser_surface_verify_closure(handle, sql, name);
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	if (handle != NULL) {
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int sqlparser_surface_test_dblink_prune(
	sqlparser_dialect_t dialect,
	const char *name)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	char *sql;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = sqlparser_surface_apply(
		dialect,
		"SELECT (SELECT x FROM remote_one@link_one) AS first_remote, (SELECT y FROM remote_two@link_two) AS second_remote FROM dual",
		"stmt[0].select_target[0][0]",
		"0 AS first_remote",
		&handle,
		&sql);
	if (failed != 0) {
		return 1;
	}
	if (strstr(sql, "remote_one@link_one") != NULL ||
	    strstr(sql, "remote_two@link_two") == NULL) {
		fprintf(stderr, "FAIL: %s database-link prune: %s\n", name, sql);
		failed = 1;
	}
	failed |= sqlparser_surface_verify_closure(handle, sql, name);
	sqlparser_string_free(sql);
	sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].select_target[0][0]";
	patch.sql = "(SELECT z FROM remote_three@link_three) AS first_remote";
	patch_list.items = &patch;
	patch_list.count = 1U;
	status = sqlparser_apply_patch(handle, &patch_list, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK ||
	    strstr(sql != NULL ? sql : "", "remote_three@link_three") == NULL ||
	    strstr(sql != NULL ? sql : "", "remote_two@link_two") == NULL ||
	    strstr(sql != NULL ? sql : "", "remote_one@link_one") != NULL ||
	    strstr(sql != NULL ? sql : "", "sqlparser_") != NULL) {
		fprintf(
			stderr,
			"FAIL: %s database-link reinsert: %s (%s)\n",
			name,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
	}
	if (status == SQLPARSER_STATUS_OK && sql != NULL) {
		failed |= sqlparser_surface_verify_closure(handle, sql, name);
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_surface_test_set_operator_insert_delete(
	sqlparser_dialect_t dialect,
	const char *name)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	char *sql;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = sqlparser_surface_apply(
		dialect,
		"SELECT 1 AS value_col FROM dual",
		"stmt[0].select_target[0][0]",
		"(SELECT 2 FROM dual MINUS SELECT 3 FROM dual) AS value_col",
		&handle,
		&sql);
	if (failed != 0) {
		return 1;
	}
	if (sqlparser_surface_count(sql, " MINUS ") != 1U ||
	    strstr(sql, " EXCEPT ") != NULL) {
		fprintf(stderr, "FAIL: %s MINUS insertion: %s\n", name, sql);
		failed = 1;
	}
	failed |= sqlparser_surface_verify_closure(handle, sql, name);
	sqlparser_string_free(sql);
	sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].select_target[0][0]";
	patch.sql = "0 AS value_col";
	patch_list.items = &patch;
	patch_list.count = 1U;
	status = sqlparser_apply_patch(handle, &patch_list, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || sql == NULL ||
	    strstr(sql, " MINUS ") != NULL || strstr(sql, " EXCEPT ") != NULL) {
		fprintf(
			stderr,
			"FAIL: %s MINUS deletion: %s (%s)\n",
			name,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
	}
	if (status == SQLPARSER_STATUS_OK && sql != NULL) {
		failed |= sqlparser_surface_verify_closure(handle, sql, name);
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_surface_test_fragment_relation_patch_orders(void)
{
	static const char dcte_sql[] =
		"WITH inserted AS (INSERT INTO public.inbox (id, payload) VALUES "
		"(1, 10), (2, 20) RETURNING id, payload), updated AS (UPDATE "
		"public.work_items SET payload = i.payload FROM inserted AS i WHERE "
		"public.work_items.id = i.id RETURNING public.work_items.id, "
		"public.work_items.payload) SELECT u.id, u.payload FROM updated AS u";
	static const char dcte_replace_current[] =
		"WITH inserted AS (INSERT INTO public.inbox (id, payload) VALUES "
		"(1, 10), (2, 20) RETURNING id, payload), updated AS (UPDATE "
		"archive.work_items_new SET payload = i.payload FROM inserted AS i "
		"WHERE archive.work_items_new.id = i.id RETURNING "
		"archive.work_items_new.id, archive.work_items_new.payload AS "
		"changed_payload) SELECT u.id, u.payload FROM updated AS u";
	static const char dcte_replace_old[] =
		"WITH inserted AS (INSERT INTO public.inbox (id, payload) VALUES "
		"(1, 10), (2, 20) RETURNING id, payload), updated AS (UPDATE "
		"archive.work_items_new SET payload = i.payload FROM inserted AS i "
		"WHERE archive.work_items_new.id = i.id RETURNING "
		"archive.work_items_new.id, public.work_items.payload AS "
		"changed_payload) SELECT u.id, u.payload FROM updated AS u";
	static const char dcte_insert_current[] =
		"WITH inserted AS (INSERT INTO public.inbox (id, payload) VALUES "
		"(1, 10), (2, 20) RETURNING id, payload), updated AS (UPDATE "
		"archive.work_items_new SET payload = i.payload FROM inserted AS i "
		"WHERE archive.work_items_new.id = i.id RETURNING "
		"archive.work_items_new.id, archive.work_items_new.payload, "
		"archive.work_items_new.*) SELECT u.id, u.payload FROM updated AS u";
	static const char dcte_insert_old[] =
		"WITH inserted AS (INSERT INTO public.inbox (id, payload) VALUES "
		"(1, 10), (2, 20) RETURNING id, payload), updated AS (UPDATE "
		"archive.work_items_new SET payload = i.payload FROM inserted AS i "
		"WHERE archive.work_items_new.id = i.id RETURNING "
		"archive.work_items_new.id, archive.work_items_new.payload, "
		"public.work_items.*) SELECT u.id, u.payload FROM updated AS u";
	static const sqlparser_fragment_relation_case_t cases[] = {
		{
			"SELECT target replace",
			"SELECT public.accounts.id, public.accounts.* FROM "
			"public.accounts WHERE public.accounts.active = 1",
			SQLPARSER_PATCH_REPLACE,
			"stmt[0].select_target[0][0]",
			0U,
			"public.accounts.name AS renamed_name",
			"archive.accounts_new.name AS renamed_name",
			"stmt[0].relation[0]",
			"archive.accounts_new",
			"SELECT archive.accounts_new.name AS renamed_name, "
			"archive.accounts_new.* FROM archive.accounts_new WHERE "
			"archive.accounts_new.active = 1",
			"SELECT public.accounts.name AS renamed_name, "
			"archive.accounts_new.* FROM archive.accounts_new WHERE "
			"archive.accounts_new.active = 1"
		},
		{
			"SELECT target insert",
			"SELECT public.accounts.id, public.accounts.* FROM "
			"public.accounts WHERE public.accounts.active = 1",
			SQLPARSER_PATCH_INSERT_COLUMN,
			"stmt[0].select_targets[0]",
			2U,
			"public.accounts.email AS inserted_email",
			"archive.accounts_new.email AS inserted_email",
			"stmt[0].relation[0]",
			"archive.accounts_new",
			"SELECT archive.accounts_new.id, archive.accounts_new.*, "
			"archive.accounts_new.email AS inserted_email FROM "
			"archive.accounts_new WHERE archive.accounts_new.active = 1",
			"SELECT archive.accounts_new.id, archive.accounts_new.*, "
			"public.accounts.email AS inserted_email FROM "
			"archive.accounts_new WHERE archive.accounts_new.active = 1"
		},
		{
			"D=1 RETURNING target replace",
			dcte_sql,
			SQLPARSER_PATCH_REPLACE,
			"stmt[0].dml_result_target[1][0][1]",
			0U,
			"public.work_items.payload AS changed_payload",
			"archive.work_items_new.payload AS changed_payload",
			"stmt[0].relation[2]",
			"archive.work_items_new",
			dcte_replace_current,
			dcte_replace_old
		},
		{
			"D=1 RETURNING target insert",
			dcte_sql,
			SQLPARSER_PATCH_INSERT_COLUMN,
			"stmt[0].dml_result_targets[1][0]",
			2U,
			"public.work_items.*",
			"archive.work_items_new.*",
			"stmt[0].relation[2]",
			"archive.work_items_new",
			dcte_insert_current,
			dcte_insert_old
		},
		{
			"UPDATE assignment replace",
			"UPDATE public.accounts SET balance = public.accounts.balance + 1 "
			"WHERE public.accounts.id = 1 RETURNING public.accounts.id, "
			"public.accounts.*",
			SQLPARSER_PATCH_REPLACE,
			"stmt[0].assignment[0]",
			0U,
			"public.accounts.credit + 2",
			"archive.accounts_new.credit + 2",
			"stmt[0].relation[0]",
			"archive.accounts_new",
			"UPDATE archive.accounts_new SET balance = "
			"archive.accounts_new.credit + 2 WHERE archive.accounts_new.id = 1 "
			"RETURNING archive.accounts_new.id, archive.accounts_new.*",
			"UPDATE archive.accounts_new SET balance = public.accounts.credit + 2 "
			"WHERE archive.accounts_new.id = 1 RETURNING "
			"archive.accounts_new.id, archive.accounts_new.*"
		},
		{
			"UPDATE assignment insert",
			"UPDATE public.accounts SET balance = public.accounts.balance + 1 "
			"WHERE public.accounts.id = 1 RETURNING public.accounts.id, "
			"public.accounts.*",
			SQLPARSER_PATCH_INSERT_ASSIGNMENT,
			"stmt[0].assignment[1]",
			0U,
			"bonus = public.accounts.credit + 3",
			"bonus = archive.accounts_new.credit + 3",
			"stmt[0].relation[0]",
			"archive.accounts_new",
			"UPDATE archive.accounts_new SET balance = "
			"archive.accounts_new.balance + 1, bonus = "
			"archive.accounts_new.credit + 3 WHERE archive.accounts_new.id = 1 "
			"RETURNING archive.accounts_new.id, archive.accounts_new.*",
			"UPDATE archive.accounts_new SET balance = "
			"archive.accounts_new.balance + 1, bonus = public.accounts.credit + 3 "
			"WHERE archive.accounts_new.id = 1 RETURNING "
			"archive.accounts_new.id, archive.accounts_new.*"
		},
		{
			"quoted SELECT target replace",
			"SELECT \"public\".\"Accounts\".\"Id\", \"public\".\"Accounts\".* "
			"FROM \"public\".\"Accounts\" WHERE "
			"\"public\".\"Accounts\".\"Active\" = 1",
			SQLPARSER_PATCH_REPLACE,
			"stmt[0].select_target[0][0]",
			0U,
			"\"public\".\"Accounts\".\"Name\" AS \"RenamedName\"",
			"\"Archive\".\"AccountsNew\".\"Name\" AS \"RenamedName\"",
			"stmt[0].relation[0]",
			"\"Archive\".\"AccountsNew\"",
			"SELECT \"Archive\".\"AccountsNew\".\"Name\" AS \"RenamedName\", "
			"\"Archive\".\"AccountsNew\".* FROM \"Archive\".\"AccountsNew\" "
			"WHERE \"Archive\".\"AccountsNew\".\"Active\" = 1",
			"SELECT \"public\".\"Accounts\".\"Name\" AS \"RenamedName\", "
			"\"Archive\".\"AccountsNew\".* FROM \"Archive\".\"AccountsNew\" "
			"WHERE \"Archive\".\"AccountsNew\".\"Active\" = 1"
		},
		{
			"quoted SELECT target insert",
			"SELECT \"public\".\"Accounts\".\"Id\", \"public\".\"Accounts\".* "
			"FROM \"public\".\"Accounts\" WHERE "
			"\"public\".\"Accounts\".\"Active\" = 1",
			SQLPARSER_PATCH_INSERT_COLUMN,
			"stmt[0].select_targets[0]",
			2U,
			"\"public\".\"Accounts\".*",
			"\"Archive\".\"AccountsNew\".*",
			"stmt[0].relation[0]",
			"\"Archive\".\"AccountsNew\"",
			"SELECT \"Archive\".\"AccountsNew\".\"Id\", "
			"\"Archive\".\"AccountsNew\".*, \"Archive\".\"AccountsNew\".* "
			"FROM \"Archive\".\"AccountsNew\" WHERE "
			"\"Archive\".\"AccountsNew\".\"Active\" = 1",
			"SELECT \"Archive\".\"AccountsNew\".\"Id\", "
			"\"Archive\".\"AccountsNew\".*, \"public\".\"Accounts\".* "
			"FROM \"Archive\".\"AccountsNew\" WHERE "
			"\"Archive\".\"AccountsNew\".\"Active\" = 1"
		}
	};
	static const char *const order_names[] = {
		"fragment-relation split",
		"fragment-relation batch",
		"relation-current-fragment split",
		"relation-current-fragment batch",
		"relation-old-fragment split",
		"relation-old-fragment batch"
	};
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t fragment_patch;
	sqlparser_patch_t ordered[2];
	sqlparser_patch_t relation_patch;
	char scenario_name[160];
	char *sql;
	size_t case_index;
	size_t order_index;
	sqlparser_status_t status;
	int failed;
	int relation_first;
	int split;

	failed = 0;
	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		for (order_index = 0U;
		     order_index < sizeof(order_names) / sizeof(order_names[0]);
		     order_index++) {
			handle = NULL;
			sql = NULL;
			memset(&error, 0, sizeof(error));
			memset(&fragment_patch, 0, sizeof(fragment_patch));
			memset(&relation_patch, 0, sizeof(relation_patch));
			sqlparser_parse_options_default(&options);
			options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
			(void)snprintf(
				scenario_name,
				sizeof(scenario_name),
				"RG033 %s %s",
				cases[case_index].name,
				order_names[order_index]);
			status = sqlparser_parse_with_options(
				cases[case_index].input_sql,
				&options,
				&handle,
				&error);
			if (status != SQLPARSER_STATUS_OK) {
				fprintf(
					stderr,
					"FAIL: %s parse: %s\n",
					scenario_name,
					error.message);
				failed = 1;
				continue;
			}
			relation_first = order_index >= 2U;
			split = (order_index % 2U) == 0U;
			fragment_patch.op = cases[case_index].fragment_op;
			fragment_patch.selector = cases[case_index].fragment_selector;
			fragment_patch.index = cases[case_index].fragment_index;
			fragment_patch.sql = relation_first && order_index < 4U ?
				cases[case_index].current_fragment_sql :
				cases[case_index].old_fragment_sql;
			relation_patch.op = SQLPARSER_PATCH_REPLACE;
			relation_patch.selector = cases[case_index].relation_selector;
			relation_patch.sql = cases[case_index].relation_sql;
			ordered[0] = relation_first ? relation_patch : fragment_patch;
			ordered[1] = relation_first ? fragment_patch : relation_patch;
			patch_list.items = ordered;
			patch_list.count = split ? 1U : 2U;
			status = sqlparser_apply_patch(handle, &patch_list, &error);
			if (status == SQLPARSER_STATUS_OK && split) {
				patch_list.items = &ordered[1];
				status = sqlparser_apply_patch(handle, &patch_list, &error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_deparse(handle, &sql, &error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				fprintf(
					stderr,
					"FAIL: %s patch/deparse: %s\n",
					scenario_name,
					error.message);
				failed = 1;
			} else {
				const char *expected;

				expected = order_index >= 4U ?
					cases[case_index].old_after_relation_expected_sql :
					cases[case_index].current_expected_sql;
				if (sql == NULL || strcmp(sql, expected) != 0) {
					fprintf(
						stderr,
						"FAIL: %s\nexpected: %s\nactual:   %s\n",
						scenario_name,
						expected,
						sql != NULL ? sql : "<null>");
					failed = 1;
				} else {
					failed |= sqlparser_surface_verify_closure(
						handle,
						sql,
						scenario_name);
				}
			}
			if (sql != NULL) {
				sqlparser_string_free(sql);
			}
			sqlparser_handle_destroy(handle);
		}
	}
	return failed;
}

static int sqlparser_surface_expect_exact_state(
	const sqlparser_handle_t *handle,
	const char *expected_sql,
	const char *name,
	const char *step)
{
	sqlparser_error_t error;
	char scenario_name[160];
	char *sql;
	sqlparser_status_t status;
	int failed;

	sql = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	(void)snprintf(
		scenario_name,
		sizeof(scenario_name),
		"%s %s",
		name,
		step);
	status = sqlparser_deparse(handle, &sql, &error);
	if (status != SQLPARSER_STATUS_OK || sql == NULL ||
	    strcmp(sql, expected_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s\nexpected: %s\nactual:   %s (%s)\n",
			scenario_name,
			expected_sql,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
	} else {
		failed = sqlparser_surface_verify_closure(
			handle, sql, scenario_name);
	}
	if (sql != NULL) {
		sqlparser_string_free(sql);
	}
	return failed;
}

static int sqlparser_surface_test_multi_insert_public_wrappers(
	sqlparser_dialect_t dialect,
	const char *name)
{
	static const char input_sql[] =
		"INSERT ALL "
		"INTO t1 (c1, c2, c3) VALUES (source_id, 2, 3) "
		"INTO t2 (c1, c2, c3) VALUES (4, 5, 6) "
		"SELECT 1 FROM dual";
	static const char *const expected_sql[] = {
		"INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, 2, 3) "
		"INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual",
		"INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), 3) "
		"INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual",
		"INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), :indexed_name) "
		"INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual",
		"INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), :indexed_name) "
		"INTO t2 (c1, c2, c3) VALUES ('', 5, 6) SELECT 1 FROM dual",
		"INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), :indexed_name) "
		"INTO t2 (c1, c2, c3) VALUES ('', UPPER('selector'), 6) SELECT 1 FROM dual",
		"INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), :indexed_name) "
		"INTO t2 (c1, c2, c3) VALUES ('', UPPER('selector'), :7) SELECT 1 FROM dual"
	};
	sqlparser_bind_value_t bind;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_literal_value_t literal;
	sqlparser_parse_options_t options;
	sqlparser_selector_t selector;
	char long_bind_key[SQLPARSER_BIND_TEXT_CAPACITY];
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(
		input_sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s multi-insert parse: %s\n", name, error.message);
		return 1;
	}

	memset(&literal, 0, sizeof(literal));
	literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	literal.integer_value = 2147483648LL;
	status = sqlparser_insert_set_cell_literal(
		handle, 0U, 0U, 0U, &literal, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s indexed multi-insert literal: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[0], name, "indexed literal") != 0) {
		failed = 1;
		goto cleanup;
	}

	status = sqlparser_insert_set_cell_sql(
		handle, 0U, 0U, 1U, "LOWER('indexed')", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s indexed multi-insert SQL: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[1], name, "indexed SQL") != 0) {
		failed = 1;
		goto cleanup;
	}

	memset(&bind, 0, sizeof(bind));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "indexed_name";
	status = sqlparser_insert_set_cell_bind(
		handle, 0U, 0U, 2U, &bind, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s indexed multi-insert bind: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[2], name, "indexed bind") != 0) {
		failed = 1;
		goto cleanup;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = 0U;
	selector.row_index = 1U;
	selector.column_index = 0U;
	memset(&literal, 0, sizeof(literal));
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	status = sqlparser_selector_set_insert_cell_literal(
		handle, &selector, &literal, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s selector multi-insert literal: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[3], name, "selector literal") != 0) {
		failed = 1;
		goto cleanup;
	}

	selector.column_index = 1U;
	status = sqlparser_selector_set_insert_cell_sql(
		handle, &selector, "UPPER('selector')", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s selector multi-insert SQL: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[4], name, "selector SQL") != 0) {
		failed = 1;
		goto cleanup;
	}

	selector.column_index = 2U;
	bind.kind = SQLPARSER_BIND_KIND_POSITIONAL;
	bind.key = "7";
	status = sqlparser_selector_set_insert_cell_bind(
		handle, &selector, &bind, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s selector multi-insert bind: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[5], name, "selector bind") != 0) {
		failed = 1;
		goto cleanup;
	}

	memset(long_bind_key, 'a', sizeof(long_bind_key) - 1U);
	long_bind_key[sizeof(long_bind_key) - 1U] = '\0';
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = long_bind_key;
	status = sqlparser_insert_set_cell_bind(
		handle, 0U, 0U, 2U, &bind, &error);
	if (status != SQLPARSER_STATUS_RESOURCE_LIMIT) {
		fprintf(stderr, "FAIL: %s multi-insert long bind boundary: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[5], name, "long bind rollback") != 0) {
		failed = 1;
		goto cleanup;
	}
	status = sqlparser_insert_set_cell_sql(
		handle, 0U, 0U, 1U, "", &error);
	if (status != SQLPARSER_STATUS_INVALID_ARGUMENT) {
		fprintf(stderr, "FAIL: %s empty multi-insert SQL boundary: %s\n", name, error.message);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[5], name, "empty SQL rollback") != 0) {
		failed = 1;
		goto cleanup;
	}

	selector.column_index = 1U;
	status = sqlparser_selector_set_insert_cell_sql(
		handle, &selector, NULL, &error);
	if (status != SQLPARSER_STATUS_INVALID_ARGUMENT) {
		fprintf(stderr, "FAIL: %s failed multi-insert SQL was not atomic\n", name);
		failed = 1;
		goto cleanup;
	}
	if (sqlparser_surface_expect_exact_state(
		    handle, expected_sql[5], name, "invalid SQL rollback") != 0) {
		failed = 1;
	}

cleanup:
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_surface_test_oracle_pagination_target_wrapper(void)
{
	static const char input_sql[] =
		"SELECT \"APP\".\"T\".*, ROWID \"NAVICAT_ROWID\" FROM \"APP\".\"T\" "
		"OFFSET 0 ROWS FETCH NEXT 1000 ROWS ONLY";
	static const char expected_sql[] =
		"SELECT \"ABC\", ROWID \"NAVICAT_ROWID\" FROM \"APP\".\"T\" "
		"OFFSET 0 ROWS FETCH NEXT 1000 ROWS ONLY";
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_selector_t selector;
	char *sql;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	sql = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	status = sqlparser_parse_with_options(
		input_sql, &options, &handle, &error);
	if (status == SQLPARSER_STATUS_OK) {
		selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
		selector.statement_index = 0U;
		selector.item_index = 0U;
		selector.column_index = 0U;
		status = sqlparser_selector_set_select_target_sql(
			handle, &selector, "\"ABC\"", &error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || sql == NULL ||
	    strcmp(sql, expected_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: Oracle projection pagination local edit\n"
			"expected: %s\nactual:   %s (%s)\n",
			expected_sql,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_surface_run_pagination_fallback(
	const sqlparser_pagination_fallback_case_t *test_case,
	sqlparser_dialect_t dialect)
{
	sqlparser_error_t error;
	sqlparser_handle_t *fresh;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	char *fresh_sql;
	char *sql;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	fresh = NULL;
	sql = NULL;
	fresh_sql = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (sqlparser_surface_apply(
		    dialect,
		    test_case->input_sql,
		    test_case->target_selector,
		    test_case->target_sql,
		    &handle,
		    &sql) != 0) {
		fprintf(stderr, "FAIL: %s %s local target edit\n",
			sqlparser_dialect_name(dialect), test_case->name);
		failed = 1;
		goto cleanup;
	}
	if (strcmp(sql, test_case->local_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s %s local target edit\n"
			"expected: %s\nactual:   %s (%s)\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			test_case->local_sql,
			sql,
			error.message);
		failed = 1;
		goto cleanup;
	}
	sqlparser_string_free(sql);
	sql = NULL;

	status = sqlparser_statement_set_relation_name(
		handle,
		0U,
		test_case->relation_index,
		test_case->new_schema,
		test_case->new_table,
		&error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || sql == NULL ||
	    strcmp(sql, test_case->fallback_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s %s AST fallback\n"
			"expected: %s\nactual:   %s (%s)\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			test_case->fallback_sql,
			sql != NULL ? sql : "<null>",
			error.message);
		failed = 1;
		goto cleanup;
	}

	memset(&error, 0, sizeof(error));
	status = sqlparser_parse_with_options(
		sql, &options, &fresh, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(fresh, &fresh_sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || fresh_sql == NULL ||
	    strcmp(sql, fresh_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s %s fresh pagination closure: %s (%s)\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			fresh_sql != NULL ? fresh_sql : "<null>",
			error.message);
		failed = 1;
	}

cleanup:
	sqlparser_string_free(fresh_sql);
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(fresh);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_surface_test_pagination_fallbacks(void)
{
	static const char postgresql_input[] =
		"SELECT id FROM public.table_a UNION ALL SELECT id FROM public.table_b "
		"ORDER BY id LIMIT $1 OFFSET $2";
	static const char postgresql_local[] =
		"SELECT id FROM public.table_a UNION ALL SELECT patched_id FROM public.table_b "
		"ORDER BY id LIMIT $1 OFFSET $2";
	static const char postgresql_fallback[] =
		"SELECT id FROM public.table_a UNION ALL SELECT patched_id FROM public.table_b_new "
		"ORDER BY id LIMIT $1 OFFSET $2";
	static const char mysql_input[] =
		"SELECT d.id FROM (SELECT id FROM inner_table ORDER BY id LIMIT ?, ?) AS d "
		"ORDER BY d.id LIMIT ? OFFSET ?";
	static const char mysql_local[] =
		"SELECT d.id AS patched_id FROM (SELECT id FROM inner_table ORDER BY id LIMIT ?, ?) AS d "
		"ORDER BY d.id LIMIT ? OFFSET ?";
	static const char mysql_fallback[] =
		"SELECT d.id AS patched_id FROM (SELECT id FROM inner_table_new ORDER BY id LIMIT ?, ?) AS d "
		"ORDER BY d.id LIMIT ? OFFSET ?";
	static const char oracle_input[] =
		"SELECT d.id FROM (SELECT id FROM inner_table ORDER BY id "
		"FETCH FIRST :inner_limit ROWS ONLY) d UNION ALL SELECT id FROM archive_table "
		"ORDER BY id OFFSET :outer_offset ROWS FETCH NEXT :outer_limit ROWS ONLY";
	static const char oracle_local[] =
		"SELECT d.id AS patched_id FROM (SELECT id FROM inner_table ORDER BY id "
		"FETCH FIRST :inner_limit ROWS ONLY) d UNION ALL SELECT id FROM archive_table "
		"ORDER BY id OFFSET :outer_offset ROWS FETCH NEXT :outer_limit ROWS ONLY";
	static const char oracle_fallback[] =
		"SELECT d.id AS patched_id FROM (SELECT id FROM inner_table_new ORDER BY id "
		"FETCH FIRST :inner_limit ROWS ONLY) d UNION ALL SELECT id FROM archive_table "
		"ORDER BY id OFFSET :outer_offset ROWS FETCH NEXT :outer_limit ROWS ONLY";
	static const char sqlserver_input[] =
		"SELECT d.id, 'LIMIT' AS note FROM (SELECT id FROM dbo.inner_table ORDER BY id "
		"OFFSET @inner_offset ROWS) AS d "
		"ORDER BY d.id OFFSET @outer_offset ROWS FETCH NEXT @outer_limit ROWS ONLY";
	static const char sqlserver_local[] =
		"SELECT d.id AS patched_id, 'LIMIT' AS note FROM (SELECT id FROM dbo.inner_table ORDER BY id "
		"OFFSET @inner_offset ROWS) AS d "
		"ORDER BY d.id OFFSET @outer_offset ROWS FETCH NEXT @outer_limit ROWS ONLY";
	static const char sqlserver_fallback[] =
		"SELECT d.id AS patched_id, 'LIMIT' AS note FROM (SELECT id FROM dbo.inner_table_new ORDER BY id "
		"OFFSET @inner_offset ROWS) AS d "
		"ORDER BY d.id OFFSET @outer_offset ROWS FETCH NEXT @outer_limit ROWS ONLY";
	static const char dameng_input[] =
		"SELECT d.id FROM (SELECT TOP 2 id FROM inner_table ORDER BY id) d "
		"ORDER BY d.id LIMIT 2, 3";
	static const char dameng_local[] =
		"SELECT d.id AS patched_id FROM (SELECT TOP 2 id FROM inner_table ORDER BY id) d "
		"ORDER BY d.id LIMIT 2, 3";
	static const char dameng_fallback[] =
		"SELECT d.id AS patched_id FROM (SELECT TOP 2 id FROM inner_table_new ORDER BY id) d "
		"ORDER BY d.id LIMIT 3 OFFSET 2";
	static const char dameng_mixed_input[] =
		"SELECT (SELECT id FROM fetch_table ORDER BY id FETCH FIRST 1 ROWS ONLY) AS fetched_id, "
		"(SELECT TOP 2 id FROM top_table ORDER BY id) AS top_id FROM dual";
	static const char dameng_mixed_local[] =
		"SELECT 0 AS fetched_id, (SELECT TOP 2 id FROM top_table ORDER BY id) AS top_id FROM dual";
	static const char dameng_mixed_fallback[] =
		"SELECT 0 AS fetched_id, (SELECT TOP 2 id FROM top_table_new ORDER BY id) AS top_id FROM dual";
	static const sqlparser_pagination_fallback_case_t cases[] = {
		{
			"set LIMIT fallback",
			{
				SQLPARSER_DIALECT_POSTGRESQL,
				SQLPARSER_DIALECT_VASTBASE_POSTGRESQL
			},
			2U,
			postgresql_input,
			"stmt[0].select_target[1][0]",
			"patched_id",
			postgresql_local,
			1U,
			"public",
			"table_b_new",
			postgresql_fallback
		},
		{
			"nested LIMIT fallback",
			{
				SQLPARSER_DIALECT_MYSQL,
				SQLPARSER_DIALECT_VASTBASE_MYSQL
			},
			2U,
			mysql_input,
			"stmt[0].select_target[0][0]",
			"d.id AS patched_id",
			mysql_local,
			0U,
			NULL,
			"inner_table_new",
			mysql_fallback
		},
		{
			"nested set FETCH fallback",
			{
				SQLPARSER_DIALECT_ORACLE,
				SQLPARSER_DIALECT_VASTBASE_ORACLE
			},
			2U,
			oracle_input,
			"stmt[0].select_target[0][0]",
			"d.id AS patched_id",
			oracle_local,
			0U,
			NULL,
			"inner_table_new",
			oracle_fallback
		},
		{
			"nested OFFSET FETCH fallback",
			{
				SQLPARSER_DIALECT_SQLSERVER,
				SQLPARSER_DIALECT_VASTBASE_SQLSERVER
			},
			2U,
			sqlserver_input,
			"stmt[0].select_target[0][0]",
			"d.id AS patched_id",
			sqlserver_local,
			0U,
			"dbo",
			"inner_table_new",
			sqlserver_fallback
		},
		{
			"nested TOP comma LIMIT fallback",
			{
				SQLPARSER_DIALECT_DAMENG,
				SQLPARSER_DIALECT_DAMENG
			},
			1U,
			dameng_input,
			"stmt[0].select_target[0][0]",
			"d.id AS patched_id",
			dameng_local,
			0U,
			NULL,
			"inner_table_new",
			dameng_fallback
		},
		{
			"FETCH before TOP owner fallback",
			{
				SQLPARSER_DIALECT_DAMENG,
				SQLPARSER_DIALECT_DAMENG
			},
			1U,
			dameng_mixed_input,
			"stmt[0].select_target[0][0]",
			"0 AS fetched_id",
			dameng_mixed_local,
			0U,
			NULL,
			"top_table_new",
			dameng_mixed_fallback
		}
	};
	size_t dialect_index;
	size_t index;
	int failed;

	failed = 0;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		for (dialect_index = 0U;
		     dialect_index < cases[index].dialect_count;
		     dialect_index++) {
			failed |= sqlparser_surface_run_pagination_fallback(
				&cases[index],
				cases[index].dialects[dialect_index]);
		}
	}
	return failed;
}

int main(void)
{
	static const sqlparser_surface_case_t cases[] = {
		{
			"Oracle national literal owner",
			SQLPARSER_DIALECT_ORACLE,
			"SELECT 'same' AS ascii_value, N'same' AS national_value FROM dual",
			"stmt[0].select_target[0][0]",
			"0 AS ascii_value",
			"SELECT 0 AS ascii_value, N'same' AS national_value FROM dual"
		},
		{
			"Dameng national literal owner",
			SQLPARSER_DIALECT_DAMENG,
			"SELECT 'same' AS ascii_value, N'same' AS national_value FROM dual",
			"stmt[0].select_target[0][0]",
			"0 AS ascii_value",
			"SELECT 0 AS ascii_value, N'same' AS national_value FROM dual"
		},
		{
			"MySQL national literal owner",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT 'same' AS ascii_value, N'same' AS national_value FROM `users`",
			"stmt[0].select_target[0][0]",
			"0 AS ascii_value",
			"SELECT 0 AS ascii_value, N'same' AS national_value FROM `users`"
		},
		{
			"PostgreSQL national literal owner",
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT 'same' AS plain_value, N'same' AS national_value FROM users",
			"stmt[0].select_target[0][0]",
			"0 AS plain_value",
			"SELECT 0 AS plain_value, N'same' AS national_value FROM users"
		},
		{
			"Oracle base and fragment national literal owners",
			SQLPARSER_DIALECT_ORACLE,
			"SELECT 0 AS placeholder, N'keep' AS kept_value FROM dual",
			"stmt[0].select_target[0][0]",
			"N'added' AS added_value",
			"SELECT N'added' AS added_value, N'keep' AS kept_value FROM dual"
		},
		{
			"Vastbase Oracle lifecycle delegation",
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"SELECT 'same' AS ascii_value, N'same' AS national_value FROM dual",
			"stmt[0].select_target[0][0]",
			"0 AS ascii_value",
			"SELECT 0 AS ascii_value, N'same' AS national_value FROM dual"
		},
		{
			"Vastbase MySQL lifecycle delegation",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT 'same' AS ascii_value, N'same' AS national_value FROM `users`",
			"stmt[0].select_target[0][0]",
			"0 AS ascii_value",
			"SELECT 0 AS ascii_value, N'same' AS national_value FROM `users`"
		},
		{
			"MySQL mixed string surfaces",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT 'A\\'s' AS backslash_single, 'A''s' AS doubled_single, "
			"\"A\\\"B\" AS backslash_double, \"A\"\"B\" AS doubled_double "
			"FROM users",
			"stmt[0].relation[0]",
			"`patched_table`",
			"SELECT 'A\\'s' AS backslash_single, 'A''s' AS doubled_single, "
			"\"A\\\"B\" AS backslash_double, \"A\"\"B\" AS doubled_double "
			"FROM `patched_table`"
		},
		{
			"Vastbase MySQL mixed string surfaces",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT 'A\\'s' AS backslash_single, 'A''s' AS doubled_single, "
			"\"A\\\"B\" AS backslash_double, \"A\"\"B\" AS doubled_double "
			"FROM users",
			"stmt[0].relation[0]",
			"`patched_table`",
			"SELECT 'A\\'s' AS backslash_single, 'A''s' AS doubled_single, "
			"\"A\\\"B\" AS backslash_double, \"A\"\"B\" AS doubled_double "
			"FROM `patched_table`"
		},
		{
			"MySQL string surface fragment replaces recorded value",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT \"old\" AS changed_value, 'A\\'s' AS kept_value, "
			"n'same' AS lower_national FROM users",
			"stmt[0].value[1]",
			"N'patched'",
			"SELECT N'patched' AS changed_value, 'A\\'s' AS kept_value, "
			"n'same' AS lower_national FROM users"
		},
		{
			"Vastbase MySQL string surface fragment replaces recorded value",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT \"old\" AS changed_value, 'A\\'s' AS kept_value, "
			"n'same' AS lower_national FROM users",
			"stmt[0].value[1]",
			"N'patched'",
			"SELECT N'patched' AS changed_value, 'A\\'s' AS kept_value, "
			"n'same' AS lower_national FROM users"
		},
		{
			"MySQL legacy literal patch replaces recorded surface",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT \"same\" AS changed_value, n'keep' AS kept_value "
			"FROM users",
			"stmt[0].literal[0]",
			"n'same'",
			"SELECT n'same' AS changed_value, n'keep' AS kept_value "
			"FROM users"
		},
		{
			"Vastbase MySQL legacy literal patch replaces recorded surface",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT \"same\" AS changed_value, n'keep' AS kept_value "
			"FROM users",
			"stmt[0].literal[0]",
			"n'same'",
			"SELECT n'same' AS changed_value, n'keep' AS kept_value "
			"FROM users"
		},
		{
			"MySQL legacy WHERE literal patch replaces recorded surface",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE status = n'same'",
			"stmt[0].where_literal[0]",
			"\"same\"",
			"SELECT id FROM users WHERE status = \"same\""
		},
		{
			"Vastbase MySQL legacy WHERE literal patch replaces recorded surface",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM users WHERE status = n'same'",
			"stmt[0].where_literal[0]",
			"\"same\"",
			"SELECT id FROM users WHERE status = \"same\""
		},
		{
			"MySQL equal-value string surface owners",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT 'same' AS plain_value, n'same' AS lower_national, "
			"N'same' AS upper_national FROM users",
			"stmt[0].select_target[0][0]",
			"0 AS plain_value",
			"SELECT 0 AS plain_value, n'same' AS lower_national, "
			"N'same' AS upper_national FROM users"
		},
		{
			"Vastbase MySQL equal-value string surface owners",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT 'same' AS plain_value, n'same' AS lower_national, "
			"N'same' AS upper_national FROM users",
			"stmt[0].select_target[0][0]",
			"0 AS plain_value",
			"SELECT 0 AS plain_value, n'same' AS lower_national, "
			"N'same' AS upper_national FROM users"
		},
		{
			"MySQL nested string surface owners",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT \"outer\" AS outer_value, (SELECT n'inner' FROM audit_log "
			"WHERE message = 'A\\'s') AS nested_value FROM users",
			"stmt[0].relation[0]",
			"`patched_audit_log`",
			"SELECT \"outer\" AS outer_value, (SELECT n'inner' FROM "
			"`patched_audit_log` WHERE message = 'A\\'s') AS nested_value "
			"FROM users"
		},
		{
			"Vastbase MySQL nested string surface owners",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT \"outer\" AS outer_value, (SELECT n'inner' FROM audit_log "
			"WHERE message = 'A\\'s') AS nested_value FROM users",
			"stmt[0].relation[0]",
			"`patched_audit_log`",
			"SELECT \"outer\" AS outer_value, (SELECT n'inner' FROM "
			"`patched_audit_log` WHERE message = 'A\\'s') AS nested_value "
			"FROM users"
		},
		{
			"MySQL bit and hex literals before string surface",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT x'4D79' AS hex_value, b'0101' AS bit_value, "
			"\"keep\" AS text_value FROM users",
			"stmt[0].relation[0]",
			"`patched_table`",
			"SELECT x'4D79' AS hex_value, b'0101' AS bit_value, "
			"\"keep\" AS text_value FROM `patched_table`"
		},
		{
			"Vastbase MySQL bit and hex literals before string surface",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT x'4D79' AS hex_value, b'0101' AS bit_value, "
			"\"keep\" AS text_value FROM users",
			"stmt[0].relation[0]",
			"`patched_table`",
			"SELECT x'4D79' AS hex_value, b'0101' AS bit_value, "
			"\"keep\" AS text_value FROM `patched_table`"
		},
		{
			"Vastbase PostgreSQL lifecycle delegation",
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"SELECT 'same' AS plain_value, N'same' AS national_value FROM users",
			"stmt[0].select_target[0][0]",
			"0 AS plain_value",
			"SELECT 0 AS plain_value, N'same' AS national_value FROM users"
		},
		{
			"Vastbase SQL Server lifecycle delegation",
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"SELECT 'same' AS plain_value, N'same' AS national_value FROM [users]",
			"stmt[0].select_target[0][0]",
			"0 AS plain_value",
			"SELECT 0 AS plain_value, N'same' AS national_value FROM [users]"
		},
		{
			"Dameng nested TOP owner",
			SQLPARSER_DIALECT_DAMENG,
			"SELECT TOP 2 id FROM users ORDER BY id",
			"stmt[0].select_target[0][0]",
			"(SELECT TOP 3 x FROM inner_t) AS nested_id",
			"SELECT TOP 2 (SELECT TOP 3 x FROM inner_t) AS nested_id FROM users ORDER BY id"
		},
		{
			"MySQL USE INDEX owner",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT secret_name FROM mysql_plugin_policy_smoke USE INDEX (idx_secret) WHERE id = 1",
			"stmt[0].select_target[0][0]",
			"(SELECT x FROM inner_t) AS nested_value",
			"SELECT (SELECT x FROM inner_t) AS nested_value FROM mysql_plugin_policy_smoke USE INDEX (idx_secret) WHERE id = 1"
		},
		{
			"MySQL LIMIT OFFSET surface",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users LIMIT ? OFFSET ?",
			"stmt[0].select_target[0][0]",
			"`patched_id`",
			"SELECT `patched_id` FROM users LIMIT ? OFFSET ?"
		},
		{
			"Vastbase MySQL LIMIT OFFSET surface",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM users LIMIT ? OFFSET ?",
			"stmt[0].select_target[0][0]",
			"`patched_id`",
			"SELECT `patched_id` FROM users LIMIT ? OFFSET ?"
		},
		{
			"MySQL LIMIT owner before inserted subquery",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users LIMIT 5, 10",
			"stmt[0].select_target[0][0]",
			"(SELECT x FROM inner_t LIMIT 1 OFFSET 2) AS nested_id",
			"SELECT (SELECT x FROM inner_t LIMIT 1 OFFSET 2) AS nested_id FROM users LIMIT 5, 10"
		},
		{
			"Vastbase MySQL LIMIT owner before inserted subquery",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM users LIMIT 5, 10",
			"stmt[0].select_target[0][0]",
			"(SELECT x FROM inner_t LIMIT 1 OFFSET 2) AS nested_id",
			"SELECT (SELECT x FROM inner_t LIMIT 1 OFFSET 2) AS nested_id FROM users LIMIT 5, 10"
		},
		{
			"MySQL comma LIMIT fragment owner",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users LIMIT 5 OFFSET 10",
			"stmt[0].select_target[0][0]",
			"(SELECT x FROM inner_t LIMIT 1, 1) AS nested_id",
			"SELECT (SELECT x FROM inner_t LIMIT 1, 1) AS nested_id FROM users LIMIT 5 OFFSET 10"
		},
		{
			"Vastbase MySQL comma LIMIT fragment owner",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM users LIMIT 5 OFFSET 10",
			"stmt[0].select_target[0][0]",
			"(SELECT x FROM inner_t LIMIT 1, 1) AS nested_id",
			"SELECT (SELECT x FROM inner_t LIMIT 1, 1) AS nested_id FROM users LIMIT 5 OFFSET 10"
		},
		{
			"MySQL nested comma and outer OFFSET LIMIT surfaces",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT d.id FROM (SELECT id FROM users ORDER BY id LIMIT 5, 10) AS d ORDER BY d.id LIMIT 10 OFFSET 2",
			"stmt[0].select_target[1][0]",
			"`patched_id`",
			"SELECT d.id FROM (SELECT `patched_id` FROM users ORDER BY id LIMIT 5, 10) AS d ORDER BY d.id LIMIT 10 OFFSET 2"
		},
		{
			"Vastbase MySQL nested comma and outer OFFSET LIMIT surfaces",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT d.id FROM (SELECT id FROM users ORDER BY id LIMIT 5, 10) AS d ORDER BY d.id LIMIT 10 OFFSET 2",
			"stmt[0].select_target[1][0]",
			"`patched_id`",
			"SELECT d.id FROM (SELECT `patched_id` FROM users ORDER BY id LIMIT 5, 10) AS d ORDER BY d.id LIMIT 10 OFFSET 2"
		},
		{
			"MySQL UNION result LIMIT OFFSET surface",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM table_a UNION ALL SELECT id FROM table_b LIMIT ? OFFSET ?",
			"stmt[0].select_target[1][0]",
			"`patched_id`",
			"SELECT id FROM table_a UNION ALL SELECT `patched_id` FROM table_b LIMIT ? OFFSET ?"
		},
		{
			"Vastbase MySQL UNION result LIMIT OFFSET surface",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM table_a UNION ALL SELECT id FROM table_b LIMIT ? OFFSET ?",
			"stmt[0].select_target[1][0]",
			"`patched_id`",
			"SELECT id FROM table_a UNION ALL SELECT `patched_id` FROM table_b LIMIT ? OFFSET ?"
		},
		{
			"MySQL comma LIMIT semantic comment trivia",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users LIMIT /*+L0*/ 5 /*+L1*/, /*+L2*/ 10",
			"stmt[0].select_target[0][0]",
			"`patched_id`",
			"SELECT `patched_id` FROM users LIMIT /*+L0*/ 5 /*+L1*/, /*+L2*/ 10"
		},
		{
			"Vastbase MySQL comma LIMIT semantic comment trivia",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM users LIMIT /*+L0*/ 5 /*+L1*/, /*+L2*/ 10",
			"stmt[0].select_target[0][0]",
			"`patched_id`",
			"SELECT `patched_id` FROM users LIMIT /*+L0*/ 5 /*+L1*/, /*+L2*/ 10"
		},
		{
			"MySQL OFFSET LIMIT semantic comment trivia",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users LIMIT /*+L0*/ 10 /*+L1*/ OFFSET /*+L2*/ 5",
			"stmt[0].select_target[0][0]",
			"`patched_id`",
			"SELECT `patched_id` FROM users LIMIT /*+L0*/ 10 /*+L1*/ OFFSET /*+L2*/ 5"
		},
		{
			"Vastbase MySQL OFFSET LIMIT semantic comment trivia",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM users LIMIT /*+L0*/ 10 /*+L1*/ OFFSET /*+L2*/ 5",
			"stmt[0].select_target[0][0]",
			"`patched_id`",
			"SELECT `patched_id` FROM users LIMIT /*+L0*/ 10 /*+L1*/ OFFSET /*+L2*/ 5"
		},
		{
			"MySQL mixed LIMIT surfaces across statements",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM t1 LIMIT 1; SELECT id FROM t2 LIMIT 3 OFFSET 4; SELECT id FROM t3 LIMIT 5, 6",
			"stmt[1].select_target[0][0]",
			"`patched_id`",
			"SELECT id FROM t1 LIMIT 1; SELECT `patched_id` FROM t2 LIMIT 3 OFFSET 4; SELECT id FROM t3 LIMIT 5, 6"
		},
		{
			"Vastbase MySQL mixed LIMIT surfaces across statements",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT id FROM t1 LIMIT 1; SELECT id FROM t2 LIMIT 3 OFFSET 4; SELECT id FROM t3 LIMIT 5, 6",
			"stmt[1].select_target[0][0]",
			"`patched_id`",
			"SELECT id FROM t1 LIMIT 1; SELECT `patched_id` FROM t2 LIMIT 3 OFFSET 4; SELECT id FROM t3 LIMIT 5, 6"
		},
		{
			"MySQL DML LIMIT before comma LIMIT",
			SQLPARSER_DIALECT_MYSQL,
			"UPDATE jobs SET status = 'x' WHERE id = 1 LIMIT 1; SELECT id FROM users LIMIT 5, 10",
			"stmt[1].select_target[0][0]",
			"`patched_id`",
			"UPDATE jobs SET status = 'x' WHERE id = 1 LIMIT 1; SELECT `patched_id` FROM users LIMIT 5, 10"
		},
		{
			"Vastbase MySQL DML LIMIT before comma LIMIT",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"UPDATE jobs SET status = 'x' WHERE id = 1 LIMIT 1; SELECT id FROM users LIMIT 5, 10",
			"stmt[1].select_target[0][0]",
			"`patched_id`",
			"UPDATE jobs SET status = 'x' WHERE id = 1 LIMIT 1; SELECT `patched_id` FROM users LIMIT 5, 10"
		},
		{
			"MySQL LIMIT-like literal before comma LIMIT",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT 'LIMIT 1, 2' AS note, id FROM users LIMIT 5, 10",
			"stmt[0].select_target[0][1]",
			"`patched_id`",
			"SELECT 'LIMIT 1, 2' AS note, `patched_id` FROM users LIMIT 5, 10"
		},
		{
			"Vastbase MySQL LIMIT-like literal before comma LIMIT",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT 'LIMIT 1, 2' AS note, id FROM users LIMIT 5, 10",
			"stmt[0].select_target[0][1]",
			"`patched_id`",
			"SELECT 'LIMIT 1, 2' AS note, `patched_id` FROM users LIMIT 5, 10"
		},
		{
			"MySQL index hint before derived close",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT d.id FROM (SELECT id FROM table_a USE INDEX (idx_a)) AS d",
			"stmt[0].select_target[1][0]",
			"`patched_id`",
			"SELECT d.id FROM (SELECT `patched_id` FROM table_a USE INDEX (idx_a)) AS d"
		},
		{
			"Vastbase MySQL index hint before derived close",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT d.id FROM (SELECT id FROM table_a USE INDEX (idx_a)) AS d",
			"stmt[0].select_target[1][0]",
			"`patched_id`",
			"SELECT d.id FROM (SELECT `patched_id` FROM table_a USE INDEX (idx_a)) AS d"
		},
		{
			"MySQL index hint before relation comma",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT a.id FROM table_a AS a USE INDEX (idx_a), table_b AS b IGNORE INDEX (idx_b) WHERE a.id = b.id",
			"stmt[0].select_target[0][0]",
			"a.id AS selected_id",
			"SELECT a.id AS selected_id FROM table_a AS a USE INDEX (idx_a), table_b AS b IGNORE INDEX (idx_b) WHERE a.id = b.id"
		},
		{
			"Vastbase MySQL index hint before relation comma",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT a.id FROM table_a AS a USE INDEX (idx_a), table_b AS b IGNORE INDEX (idx_b) WHERE a.id = b.id",
			"stmt[0].select_target[0][0]",
			"a.id AS selected_id",
			"SELECT a.id AS selected_id FROM table_a AS a USE INDEX (idx_a), table_b AS b IGNORE INDEX (idx_b) WHERE a.id = b.id"
		},
		{
			"MySQL PARTITION and index owner",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT a.category FROM table_a PARTITION (p0, p1) AS a USE INDEX (idx_group) GROUP BY a.category",
			"stmt[0].select_target[0][0]",
			"(SELECT x FROM inner_t) AS nested_value",
			"SELECT (SELECT x FROM inner_t) AS nested_value FROM table_a PARTITION (p0, p1) AS a USE INDEX (idx_group) GROUP BY a.category"
		},
		{
			"MySQL INSERT target PARTITION owner",
			SQLPARSER_DIALECT_MYSQL,
			"INSERT INTO partitioned_orders PARTITION (p0, p1) (id, note) VALUES (1, 'before')",
			"stmt[0].insert_cell[0][1]",
			"'after'",
			"INSERT INTO partitioned_orders PARTITION (p0, p1) (id, note) VALUES (1, 'after')"
		},
		{
			"Vastbase MySQL INSERT target PARTITION owner",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"INSERT INTO partitioned_orders PARTITION (p0, p1) (id, note) VALUES (1, 'before')",
			"stmt[0].insert_cell[0][1]",
			"'after'",
			"INSERT INTO partitioned_orders PARTITION (p0, p1) (id, note) VALUES (1, 'after')"
		},
		{
			"MySQL base and fragment relation surfaces",
			SQLPARSER_DIALECT_MYSQL,
			"SELECT outer_a.id FROM outer_a PARTITION (p_outer) USE INDEX (idx_outer_a) STRAIGHT_JOIN outer_b USE INDEX (idx_outer_b) ON outer_a.id = outer_b.id",
			"stmt[0].select_target[0][0]",
			"(SELECT inner_a.id FROM inner_a PARTITION (p_inner) USE INDEX (idx_inner_a) STRAIGHT_JOIN inner_b USE INDEX (idx_inner_b) ON inner_a.id = inner_b.id) AS nested_value",
			"SELECT (SELECT inner_a.id FROM inner_a PARTITION (p_inner) USE INDEX (idx_inner_a) STRAIGHT_JOIN inner_b USE INDEX (idx_inner_b) ON inner_a.id = inner_b.id) AS nested_value FROM outer_a PARTITION (p_outer) USE INDEX (idx_outer_a) STRAIGHT_JOIN outer_b USE INDEX (idx_outer_b) ON outer_a.id = outer_b.id"
		},
		{
			"Vastbase MySQL base and fragment relation surfaces",
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT outer_a.id FROM outer_a PARTITION (p_outer) USE INDEX (idx_outer_a) STRAIGHT_JOIN outer_b USE INDEX (idx_outer_b) ON outer_a.id = outer_b.id",
			"stmt[0].select_target[0][0]",
			"(SELECT inner_a.id FROM inner_a PARTITION (p_inner) USE INDEX (idx_inner_a) STRAIGHT_JOIN inner_b USE INDEX (idx_inner_b) ON inner_a.id = inner_b.id) AS nested_value",
			"SELECT (SELECT inner_a.id FROM inner_a PARTITION (p_inner) USE INDEX (idx_inner_a) STRAIGHT_JOIN inner_b USE INDEX (idx_inner_b) ON inner_a.id = inner_b.id) AS nested_value FROM outer_a PARTITION (p_outer) USE INDEX (idx_outer_a) STRAIGHT_JOIN outer_b USE INDEX (idx_outer_b) ON outer_a.id = outer_b.id"
		}
	};
	size_t index;
	int failed;

	failed = 0;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		failed |= sqlparser_surface_run_exact(&cases[index]);
	}
	failed |= sqlparser_surface_test_mysql_join_owner();
	failed |= sqlparser_surface_test_structured_literal_owner(
		SQLPARSER_DIALECT_MYSQL,
		"MySQL");
	failed |= sqlparser_surface_test_structured_literal_owner(
		SQLPARSER_DIALECT_VASTBASE_MYSQL,
		"Vastbase MySQL");
	failed |= sqlparser_surface_test_public_string_fragments(
		SQLPARSER_DIALECT_MYSQL,
		"MySQL public string fragments");
	failed |= sqlparser_surface_test_public_string_fragments(
		SQLPARSER_DIALECT_VASTBASE_MYSQL,
		"Vastbase MySQL public string fragments");
	failed |= sqlparser_surface_test_set_operators(
		SQLPARSER_DIALECT_ORACLE, "Oracle");
	failed |= sqlparser_surface_test_set_operators(
		SQLPARSER_DIALECT_DAMENG, "Dameng");
	failed |= sqlparser_surface_test_set_operator_insert_delete(
		SQLPARSER_DIALECT_ORACLE, "Oracle");
	failed |= sqlparser_surface_test_set_operator_insert_delete(
		SQLPARSER_DIALECT_DAMENG, "Dameng");
	failed |= sqlparser_surface_test_dblink_prune(
		SQLPARSER_DIALECT_ORACLE, "Oracle");
	failed |= sqlparser_surface_test_dblink_prune(
		SQLPARSER_DIALECT_DAMENG, "Dameng");
	failed |= sqlparser_surface_test_multi_insert_public_wrappers(
		SQLPARSER_DIALECT_ORACLE, "Oracle");
	failed |= sqlparser_surface_test_multi_insert_public_wrappers(
		SQLPARSER_DIALECT_DAMENG, "Dameng");
	failed |= sqlparser_surface_test_oracle_pagination_target_wrapper();
	failed |= sqlparser_surface_test_pagination_fallbacks();
	failed |= sqlparser_surface_test_fragment_relation_patch_orders();
	return failed != 0;
}
