#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"

static int fail_case(const char *case_id, const char *case_name, const char *message)
{
	fprintf(stderr, "FAIL [%s %s]: %s\n", case_id != NULL ? case_id : "-", case_name, message);
	return 1;
}

static int fail_case_field(
	const char *case_id,
	const char *case_name,
	const char *field_name,
	const char *expected)
{
	fprintf(stderr,
	        "FAIL [%s %s]: missing %s value '%s'\n",
	        case_id != NULL ? case_id : "-",
	        case_name,
	        field_name,
	        expected);
	return 1;
}

static int expect_contains_text(const char *case_id, const char *case_name, const char *text, const char *expected)
{
	if (text == NULL || expected == NULL || strstr(text, expected) == NULL) {
		return fail_case_field(case_id, case_name, "text", expected);
	}
	return 0;
}

static int expect_not_contains_text(const char *case_id, const char *case_name, const char *text, const char *expected)
{
	if (text != NULL && expected != NULL && strstr(text, expected) != NULL) {
		return fail_case(case_id, case_name, "text contained an internal SQL fragment");
	}
	return 0;
}

static int verify_sqlserver_fragment_rewrite_paths(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *fragment_sql;
	char *deparse_sql;
	int status;

	handle = NULL;
	fragment_sql = NULL;
	deparse_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

	status = sqlparser_parse_with_options(
		"UPDATE [dbo].[users] SET [name] = @name WHERE [id] = @id",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [SF001 sqlserver-fragment-update]: parse failed: %s\n", error.message);
		return 1;
	}

	status = sqlparser_update_assignment_sql(handle, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_contains_text("SF001", "sqlserver-fragment-update", fragment_sql, "@name") != 0 ||
	    expect_not_contains_text("SF001", "sqlserver-fragment-update", fragment_sql, "$1") != 0) {
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return fail_case("SF001", "sqlserver-fragment-update", "assignment fragment read failed");
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, "@new_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return fail_case("SF001", "sqlserver-fragment-update", "assignment fragment rewrite failed");
	}
	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_contains_text("SF001", "sqlserver-fragment-update", deparse_sql, "@new_name") != 0 ||
	    expect_not_contains_text("SF001", "sqlserver-fragment-update", deparse_sql, "$") != 0) {
		sqlparser_string_free(deparse_sql);
		sqlparser_handle_destroy(handle);
		return fail_case("SF001", "sqlserver-fragment-update", "deparse after rewrite failed");
	}

	sqlparser_string_free(deparse_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static size_t sqlserver_text_occurrences(
	const char *text,
	const char *needle)
{
	size_t count;
	size_t needle_len;
	const char *match;

	if (text == NULL || needle == NULL || needle[0] == '\0') {
		return 0U;
	}
	count = 0U;
	needle_len = strlen(needle);
	match = text;
	while ((match = strstr(match, needle)) != NULL) {
		count++;
		match += needle_len;
	}
	return count;
}

static int verify_sqlserver_closure(
	const char *case_id,
	const char *case_name,
	sqlparser_handle_t *handle,
	const char *expected_sql,
	int exact_view,
	char **out_sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *reparsed;
	json_error_t json_error;
	json_t *current_view_root;
	json_t *reparsed_view_root;
	char *current_sql;
	char *current_view;
	char *reparsed_sql;
	char *reparsed_view;
	const char *failure;
	int status;

	if (out_sql != NULL) {
		*out_sql = NULL;
	}
	reparsed = NULL;
	current_view_root = NULL;
	reparsed_view_root = NULL;
	current_sql = NULL;
	current_view = NULL;
	reparsed_sql = NULL;
	reparsed_view = NULL;
	failure = NULL;
	memset(&error, 0, sizeof(error));
	memset(&json_error, 0, sizeof(json_error));
	status = sqlparser_deparse(handle, &current_sql, &error);
	if (status != SQLPARSER_STATUS_OK || current_sql == NULL ||
	    current_sql[0] == '\0') {
		failure = "deparse failed";
		goto done;
	}
	if (expected_sql != NULL && strcmp(current_sql, expected_sql) != 0) {
		failure = "deparse is not byte-exact";
		goto done;
	}
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(
		current_sql, &options, &reparsed, &error);
	if (status != SQLPARSER_STATUS_OK || reparsed == NULL) {
		failure = "deparsed SQL does not reparse";
		goto done;
	}
	status = sqlparser_deparse(reparsed, &reparsed_sql, &error);
	if (status != SQLPARSER_STATUS_OK || reparsed_sql == NULL ||
	    strcmp(reparsed_sql, current_sql) != 0) {
		failure = "reparsed generation-0 SQL is not byte-exact";
		goto done;
	}
	status = sqlparser_export_view_json(handle, 0, &current_view, &error);
	if (status != SQLPARSER_STATUS_OK || current_view == NULL) {
		failure = "View export failed";
		goto done;
	}
	if (!exact_view) {
		current_view_root = json_loads(current_view, 0, &json_error);
		if (current_view_root == NULL) {
			failure = "View JSON is invalid";
			goto done;
		}
	}
	status = sqlparser_export_view_json(reparsed, 0, &reparsed_view, &error);
	if (status != SQLPARSER_STATUS_OK || reparsed_view == NULL) {
		failure = "reparsed View export failed";
		goto done;
	}
	if (exact_view) {
		if (strcmp(current_view, reparsed_view) != 0) {
			failure = "deparse/reparse changed View JSON";
			goto done;
		}
	} else {
		reparsed_view_root = json_loads(reparsed_view, 0, &json_error);
		if (reparsed_view_root == NULL ||
		    !json_equal(current_view_root, reparsed_view_root)) {
			failure = "deparse/reparse changed View semantics";
			goto done;
		}
	}

done:
	if (failure != NULL) {
		fprintf(
			stderr,
			"FAIL [%s %s]: %s%s%s\n",
			case_id,
			case_name,
			failure,
			error.message[0] != '\0' ? ": " : "",
			error.message[0] != '\0' ? error.message : "");
		if (current_sql != NULL) {
			fprintf(stderr, "actual SQL: %s\n", current_sql);
		}
		if (expected_sql != NULL) {
			fprintf(stderr, "expected SQL: %s\n", expected_sql);
		}
	}
	if (failure == NULL && out_sql != NULL) {
		*out_sql = current_sql;
		current_sql = NULL;
	}
	json_decref(current_view_root);
	json_decref(reparsed_view_root);
	sqlparser_string_free(current_view);
	sqlparser_string_free(reparsed_sql);
	sqlparser_string_free(reparsed_view);
	sqlparser_string_free(current_sql);
	sqlparser_handle_destroy(reparsed);
	return failure != NULL ? 1 : 0;
}

static int verify_sqlserver_surface_tokens(
	const char *case_id,
	const char *case_name,
	const char *sql,
	size_t expected_surface_count,
	int expect_drop_surface,
	int expect_added_surface)
{
	static const char *const keep_tokens[] = {
		"TOP (2)",
		"TRY_CONVERT(BIT, {fn ABS(-2)}, 0)",
		"WITH (NOLOCK)",
		"N'keep'",
		"[keep_value]"
	};
	static const char *const drop_tokens[] = {
		"TOP (1)",
		"TRY_CAST({fn ABS(-1)} AS BIT)",
		"WITH (HOLDLOCK)",
		"N'drop'",
		"[drop_value]"
	};
	static const char *const added_tokens[] = {
		"TOP (9)",
		"CONVERT(BIT, {fn ABS(-9)}, 1)",
		"WITH (FORCESEEK)",
		"N'added'",
		"[added_value]"
	};
	size_t index;

	for (index = 0U;
	     index < sizeof(keep_tokens) / sizeof(keep_tokens[0]);
	     index++) {
		if (strstr(sql, keep_tokens[index]) == NULL) {
			return fail_case_field(
				case_id, case_name, "kept surface", keep_tokens[index]);
		}
	}
	for (index = 0U;
	     index < sizeof(drop_tokens) / sizeof(drop_tokens[0]);
	     index++) {
		if ((strstr(sql, drop_tokens[index]) != NULL) !=
		    expect_drop_surface) {
			return fail_case(
				case_id, case_name, "removed surface state leaked or was lost");
		}
	}
	for (index = 0U;
	     index < sizeof(added_tokens) / sizeof(added_tokens[0]);
	     index++) {
		if ((strstr(sql, added_tokens[index]) != NULL) !=
		    expect_added_surface) {
			return fail_case(
				case_id, case_name, "inserted surface state leaked or was lost");
		}
	}
	if (sqlserver_text_occurrences(sql, "TOP (") !=
			expected_surface_count ||
	    sqlserver_text_occurrences(sql, "{fn ABS(") !=
			expected_surface_count ||
	    sqlserver_text_occurrences(sql, "BIT") !=
			expected_surface_count ||
	    sqlserver_text_occurrences(sql, " WITH (") !=
			expected_surface_count ||
	    sqlserver_text_occurrences(sql, "N'") !=
			expected_surface_count) {
		return fail_case(
			case_id, case_name, "surface owner multiplicity mismatch");
	}
	return 0;
}

static int verify_sqlserver_surface_owner_lifecycle(void)
{
	static const char base_sql[] =
		"SELECT "
		"(SELECT TOP (1) TRY_CAST({fn ABS(-1)} AS BIT) "
		"FROM [dbo].[drop_src] WITH (HOLDLOCK) "
		"WHERE [tag] = N'drop') AS [drop_value], "
		"(SELECT TOP (2) TRY_CONVERT(BIT, {fn ABS(-2)}, 0) "
		"FROM [dbo].[keep_src] WITH (NOLOCK) "
		"WHERE [tag] = N'keep') AS [keep_value]";
	static const char added_target[] =
		"(SELECT TOP (9) CONVERT(BIT, {fn ABS(-9)}, 1) "
		"FROM [dbo].[added_src] WITH (FORCESEEK) "
		"WHERE [tag] = N'added') AS [added_value]";
	static const struct {
		const char *case_id;
		const char *case_name;
		int operation;
		size_t expected_surface_count;
		int expect_drop_surface;
		int expect_added_surface;
	} cases[] = {
		{"SSL001", "sqlserver-surface-owner-front-insert", 1, 3U, 1, 1},
		{"SSL002", "sqlserver-surface-owner-front-replace", 2, 1U, 0, 0},
		{"SSL003", "sqlserver-surface-owner-front-delete", 3, 1U, 0, 0}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *deparsed_sql;
	size_t index;
	int status;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_SQLSERVER;
		status = sqlparser_parse_with_options(
			base_sql, &options, &handle, &error);
		if (status != SQLPARSER_STATUS_OK || handle == NULL) {
			fprintf(
				stderr,
				"FAIL [%s %s]: parse failed: %s\n",
				cases[index].case_id,
				cases[index].case_name,
				error.message);
			return 1;
		}
		if (cases[index].operation == 1) {
			status = sqlparser_select_insert_target_sql(
				handle, 0U, 0U, 0U, added_target, &error);
		} else if (cases[index].operation == 2) {
			status = sqlparser_select_set_target_sql(
				handle, 0U, 0U, 0U, "0 AS [replacement]", &error);
		} else {
			status = sqlparser_select_delete_target(
				handle, 0U, 0U, 0U, &error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL [%s %s]: patch failed: %s\n",
				cases[index].case_id,
				cases[index].case_name,
				error.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (verify_sqlserver_closure(
			    cases[index].case_id,
			    cases[index].case_name,
			    handle,
			    NULL,
			    0,
			    &deparsed_sql) != 0 ||
		    verify_sqlserver_surface_tokens(
			    cases[index].case_id,
			    cases[index].case_name,
			    deparsed_sql,
			    cases[index].expected_surface_count,
			    cases[index].expect_drop_surface,
			    cases[index].expect_added_surface) != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int verify_sqlserver_insert_default_clone_state(void)
{
	static const char default_sql[] =
		"(SELECT TOP (7) TRY_CAST({fn ABS(?)} AS BIT) "
		"FROM [dbo].[clone_src] WITH (NOLOCK) "
		"WHERE [tag] = N'clone')";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patches;
	char *deparsed_sql;
	size_t row_count;
	size_t column_count;
	int status;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(
		"INSERT INTO [dbo].[dst] ([id]) VALUES (1), (2), (3)",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		fprintf(stderr, "FAIL [SSL004 sqlserver-default-clone]: parse failed: %s\n", error.message);
		return 1;
	}
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_columns";
	patch.index = 1U;
	patch.name = "computed";
	patch.default_sql = default_sql;
	patches.items = &patch;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    sqlparser_insert_row_count(handle, 0U, &row_count, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_insert_column_count(handle, 0U, &column_count, &error) !=
		    SQLPARSER_STATUS_OK ||
	    row_count != 3U || column_count != 2U) {
		fprintf(stderr, "FAIL [SSL004 sqlserver-default-clone]: patch/readback failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (verify_sqlserver_closure(
		    "SSL004", "sqlserver-default-clone", handle, NULL, 0, &deparsed_sql) != 0 ||
	    sqlserver_text_occurrences(deparsed_sql, "TOP (7)") != 3U ||
	    sqlserver_text_occurrences(deparsed_sql, "TRY_CAST(") != 3U ||
	    sqlserver_text_occurrences(deparsed_sql, "{fn ABS(") != 3U ||
	    sqlserver_text_occurrences(deparsed_sql, " AS BIT)") != 3U ||
	    sqlserver_text_occurrences(deparsed_sql, "?") != 3U ||
	    sqlserver_text_occurrences(deparsed_sql, "WITH (NOLOCK)") != 3U ||
	    sqlserver_text_occurrences(deparsed_sql, "N'clone'") != 3U ||
	    strstr(deparsed_sql, "$") != NULL) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return fail_case(
			"SSL004",
			"sqlserver-default-clone",
			"cloned default surface or bind state mismatch");
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_assignment_value_clone_state(void)
{
	static const char sql[] =
		"UPDATE [dbo].[dst] SET [value_a] = "
		"(SELECT TOP (5) TRY_CAST({fn ABS(?)} AS BIT) "
		"FROM [dbo].[clone_src] WITH (NOLOCK) "
		"WHERE [tag] = N'clone'), [value_b] = 0";
	const char *target_parts[1];
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_selector_t insert_selector;
	sqlparser_selector_t source_selector;
	sqlparser_identifier_path_view_t target;
	char *deparsed_sql;
	size_t assignment_count;
	int status;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&insert_selector, 0, sizeof(insert_selector));
	memset(&source_selector, 0, sizeof(source_selector));
	memset(&target, 0, sizeof(target));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    sqlparser_selector_parse(
		    "stmt[0].assignment[1]", &insert_selector, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_selector_parse(
		    "stmt[0].assignment[0]", &source_selector, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [SSL005 sqlserver-assignment-clone]: setup failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	target_parts[0] = "value_clone";
	target.parts = target_parts;
	target.part_count = 1U;
	status = sqlparser_selector_insert_update_assignment_from_assignment_value(
		handle,
		&insert_selector,
		&target,
		&source_selector,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    sqlparser_update_assignment_count(
		    handle, 0U, &assignment_count, &error) !=
		    SQLPARSER_STATUS_OK ||
	    assignment_count != 3U) {
		fprintf(stderr, "FAIL [SSL005 sqlserver-assignment-clone]: clone/readback failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (verify_sqlserver_closure(
		    "SSL005", "sqlserver-assignment-clone", handle, NULL, 0, &deparsed_sql) != 0 ||
	    sqlserver_text_occurrences(deparsed_sql, "TOP (5)") != 2U ||
	    sqlserver_text_occurrences(deparsed_sql, "TRY_CAST(") != 2U ||
	    sqlserver_text_occurrences(deparsed_sql, "{fn ABS(") != 2U ||
	    sqlserver_text_occurrences(deparsed_sql, " AS BIT)") != 2U ||
	    sqlserver_text_occurrences(deparsed_sql, "?") != 2U ||
	    sqlserver_text_occurrences(deparsed_sql, "WITH (NOLOCK)") != 2U ||
	    sqlserver_text_occurrences(deparsed_sql, "N'clone'") != 2U ||
	    strstr(deparsed_sql, "value_clone") == NULL ||
	    strstr(deparsed_sql, "$") != NULL) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return fail_case(
			"SSL005",
			"sqlserver-assignment-clone",
			"cloned assignment surface or bind state mismatch");
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_surface_state_failure_rollback(void)
{
	static const char sql[] =
		"SELECT (SELECT TOP (2) TRY_CAST({fn ABS(-2)} AS BIT) "
		"FROM [dbo].[keep_src] WITH (NOLOCK) "
		"WHERE [tag] = N'keep') AS [keep_value]";
	static const char replacement[] =
		"(SELECT TOP (9) TRY_CAST({fn ABS(?)} AS BIT) "
		"FROM [dbo].[added_src] WITH (FORCESEEK) "
		"WHERE [tag] = N'added') AS [added_value]";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch_items[2];
	sqlparser_patch_list_t patches;
	char *baseline_sql;
	char *baseline_view;
	char *after_sql;
	char *after_view;
	size_t iteration;
	int status;

	handle = NULL;
	baseline_sql = NULL;
	baseline_view = NULL;
	after_sql = NULL;
	after_view = NULL;
	memset(&error, 0, sizeof(error));
	memset(patch_items, 0, sizeof(patch_items));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    sqlparser_deparse(handle, &baseline_sql, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(handle, 0, &baseline_view, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [SSL006 sqlserver-surface-rollback]: setup failed: %s\n", error.message);
		sqlparser_string_free(baseline_sql);
		sqlparser_string_free(baseline_view);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	patch_items[0].op = SQLPARSER_PATCH_REPLACE;
	patch_items[0].selector = "stmt[0].select_target[0][0]";
	patch_items[0].sql = replacement;
	patch_items[1].op = SQLPARSER_PATCH_REPLACE;
	patch_items[1].selector = "stmt[0].relation[0]";
	patch_items[1].sql = "[a] + [b]";
	patches.items = patch_items;
	patches.count = 2U;
	for (iteration = 0U; iteration < 16U; iteration++) {
		status = sqlparser_apply_patch(handle, &patches, &error);
		if (status == SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &after_sql, &error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_export_view_json(handle, 0, &after_view, &error) !=
			    SQLPARSER_STATUS_OK ||
		    after_sql == NULL || after_view == NULL ||
		    strcmp(after_sql, baseline_sql) != 0 ||
		    strcmp(after_view, baseline_view) != 0) {
			fprintf(
				stderr,
				"FAIL [SSL006 sqlserver-surface-rollback]: iteration %lu changed the handle: %s\n",
				(unsigned long)iteration,
				error.message);
			sqlparser_string_free(after_sql);
			sqlparser_string_free(after_view);
			sqlparser_string_free(baseline_sql);
			sqlparser_string_free(baseline_view);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(after_sql);
		sqlparser_string_free(after_view);
		after_sql = NULL;
		after_view = NULL;
	}
	if (verify_sqlserver_closure(
		    "SSL006", "sqlserver-surface-rollback", handle, NULL, 0, NULL) != 0) {
		sqlparser_string_free(baseline_sql);
		sqlparser_string_free(baseline_view);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(baseline_sql);
	sqlparser_string_free(baseline_view);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_partition_generated_identifier(void)
{
	static const char sql[] =
		"SELECT 1 AS \"sqlparser_sqlserver_partition_prefix:$PARTITION\", "
		"[id] FROM [dbo].[users]";
	static const char expected[] =
		"SELECT 1 AS \"sqlparser_sqlserver_partition_prefix:$PARTITION\", "
		"[id], $PARTITION.[Range PF]([id]) AS [p] FROM [dbo].[users]";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *deparsed_sql;
	int status;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(
		sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		fprintf(
			stderr,
			"FAIL [SSL007 sqlserver-partition-generated-identifier]: parse failed: %s\n",
			error.message);
		return 1;
	}
	status = sqlparser_select_insert_target_sql(
		handle,
		0U,
		0U,
		2U,
		"$PARTITION.[Range PF]([id]) AS [p]",
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL007",
		    "sqlserver-partition-generated-identifier",
		    handle,
		    NULL,
		    0,
		    &deparsed_sql) != 0 ||
	    deparsed_sql == NULL || strcmp(deparsed_sql, expected) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL007 sqlserver-partition-generated-identifier]: %s\n",
			deparsed_sql != NULL ? deparsed_sql : error.message);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_partition_scalar_ordinals(void)
{
	static const char generation_zero_sql[] =
		"SELECT $PARTITION.RangePF1/*keep*/([id]) AS [p], "
		"{fn UCASE([name])} AS [n], "
		"TRY_CONVERT(int, [value], 1) AS [v] FROM [dbo].[users]";
	static const char patch_sql[] =
		"SELECT $PARTITION.RangePF1([id]) AS [p], "
		"{fn UCASE([name])} AS [n], "
		"TRY_CONVERT(int, [value], 1) AS [v] FROM [dbo].[users]";
	static const char expected[] =
		"SELECT $PARTITION.RangePF1([id]) AS [p], "
		"{fn UCASE([name])} AS [n], "
		"TRY_CONVERT(int, [value], 1) AS [v], [extra] "
		"FROM [dbo].[users]";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *deparsed_sql;
	int status;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(
		generation_zero_sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		fprintf(
			stderr,
			"FAIL [SSL008 sqlserver-partition-scalar-ordinals]: generation-0 parse failed: %s\n",
			error.message);
		return 1;
	}
	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparsed_sql == NULL ||
	    strcmp(deparsed_sql, generation_zero_sql) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL008 sqlserver-partition-scalar-ordinals]: generation-0 deparse mismatch: %s\n",
			deparsed_sql != NULL ? deparsed_sql : error.message);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	deparsed_sql = NULL;
	handle = NULL;
	memset(&error, 0, sizeof(error));
	status = sqlparser_parse_with_options(
		patch_sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		fprintf(
			stderr,
			"FAIL [SSL008 sqlserver-partition-scalar-ordinals]: patch parse failed: %s\n",
			error.message);
		return 1;
	}
	status = sqlparser_select_insert_target_sql(
		handle, 0U, 0U, 3U, "[extra]", &error);
	if (status != SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL008",
		    "sqlserver-partition-scalar-ordinals",
		    handle,
		    NULL,
		    0,
		    &deparsed_sql) != 0 ||
	    deparsed_sql == NULL ||
	    strcmp(deparsed_sql, expected) != 0 ||
	    strstr(
		    deparsed_sql,
		    "sqlparser_sqlserver_partition_prefix:") != NULL) {
		fprintf(
			stderr,
			"FAIL [SSL008 sqlserver-partition-scalar-ordinals]: %s\n",
			deparsed_sql != NULL ? deparsed_sql : error.message);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_odbc_scalar_closure(void)
{
	enum {
		SQLSERVER_ODBC_PATCH_INSERT_TARGET = 0,
		SQLSERVER_ODBC_PATCH_SET_INSERT_CELL = 1
	};
	static const struct {
		const char *case_id;
		const char *case_name;
		const char *sql;
		const char *patch_sql;
		const char *expected_sql;
		size_t target_index;
		int patch_kind;
	} cases[] = {
		{
			"SSL009",
			"sqlserver-odbc-select-parenthesis",
			"SELECT({fn ABS(-1)}) AS [odbc]",
			"[extra]",
			"SELECT({fn ABS(-1)}) AS [odbc], [extra]",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL010",
			"sqlserver-odbc-where-parenthesis",
			"SELECT [id] FROM [dbo].[t] WHERE(1 = 1) AND {fn ABS([id])} > 0",
			"[extra]",
			"SELECT [id], [extra] FROM [dbo].[t] WHERE(1 = 1) AND {fn ABS([id])} > 0",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL011",
			"sqlserver-odbc-exists-parenthesis",
			"SELECT [t].[id] FROM [dbo].[t] [t] WHERE EXISTS(SELECT 1 FROM [dbo].[s] [s] WHERE [s].[id] = {fn ABS([t].[id])})",
			"[extra]",
			"SELECT [t].[id], [extra] FROM [dbo].[t] [t] WHERE EXISTS(SELECT 1 FROM [dbo].[s] [s] WHERE [s].[id] = {fn ABS([t].[id])})",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL012",
			"sqlserver-odbc-join-on-parenthesis",
			"SELECT [a].[id] FROM [dbo].[a] [a] JOIN [dbo].[b] [b] ON({fn ABS([a].[id])} = [b].[id])",
			"[extra]",
			"SELECT [a].[id], [extra] FROM [dbo].[a] [a] JOIN [dbo].[b] [b] ON({fn ABS([a].[id])} = [b].[id])",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL013",
			"sqlserver-odbc-insert-column-parenthesis",
			"INSERT INTO dbo.t(v) VALUES ({fn ABS(-1)})",
			"{fn ABS(-2)}",
			"INSERT INTO dbo.t(v) VALUES ({fn ABS(-2)})",
			0U,
			SQLSERVER_ODBC_PATCH_SET_INSERT_CELL
		},
		{
			"SSL014",
			"sqlserver-odbc-cast-type-modifier",
			"SELECT CAST(1 AS decimal(10,2)) AS [n], {fn ABS(-1)} AS [odbc]",
			"[extra]",
			"SELECT CAST(1 AS decimal(10,2)) AS [n], {fn ABS(-1)} AS [odbc], [extra]",
			2U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL015",
			"sqlserver-plain-abs-before-odbc-abs",
			"SELECT ABS(-2) AS [plain], {fn ABS(-1)} AS [odbc]",
			"[extra]",
			"SELECT ABS(-2) AS [plain], {fn ABS(-1)} AS [odbc], [extra]",
			2U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL016",
			"sqlserver-abs-table-name",
			"INSERT INTO dbo.ABS(v) VALUES ({fn ABS(-1)})",
			"{fn ABS(-2)}",
			"INSERT INTO dbo.ABS(v) VALUES ({fn ABS(-2)})",
			0U,
			SQLSERVER_ODBC_PATCH_SET_INSERT_CELL
		},
		{
			"SSL017",
			"sqlserver-one-odbc-among-abs-calls",
			"SELECT ABS(-3) AS [a], {fn ABS(-2)} AS [b], ABS(-1) AS [c]",
			"[extra]",
			"SELECT ABS(-3) AS [a], {fn ABS(-2)} AS [b], ABS(-1) AS [c], [extra]",
			3U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL018",
			"sqlserver-odbc-wrapper-trivia",
			"SELECT { FN /*lead*/ ABS(-1) /*tail*/ } AS [v]",
			"[extra]",
			"SELECT { FN /*lead*/ ABS(-1) /*tail*/ } AS [v], [extra]",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL019",
			"sqlserver-nested-odbc-functions",
			"SELECT {fn ABS({fn POWER(-2, 3)})} AS [v]",
			"[extra]",
			"SELECT {fn ABS({fn POWER(-2, 3)})} AS [v], [extra]",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL020",
			"sqlserver-convert-odbc-function",
			"SELECT CONVERT(int, {fn ABS(-1)}, 0) AS [v]",
			"[extra]",
			"SELECT CONVERT(int, {fn ABS(-1)}, 0) AS [v], [extra]",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL021",
			"sqlserver-try-convert-odbc-function",
			"SELECT TRY_CONVERT(int, {fn ABS(-2)}, 0) AS [v]",
			"[extra]",
			"SELECT TRY_CONVERT(int, {fn ABS(-2)}, 0) AS [v], [extra]",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		},
		{
			"SSL022",
			"sqlserver-convert-nested-odbc-functions",
			"SELECT CONVERT(int, {fn ABS({fn POWER(-2, 3)})}, 0) AS [v]",
			"[extra]",
			"SELECT CONVERT(int, {fn ABS({fn POWER(-2, 3)})}, 0) AS [v], [extra]",
			1U,
			SQLSERVER_ODBC_PATCH_INSERT_TARGET
		}
	};
	sqlparser_parse_options_t options;
	size_t index;
	int failed;

	failed = 0;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_error_t error;
		sqlparser_handle_t *handle;
		int status;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		status = sqlparser_parse_with_options(
			cases[index].sql, &options, &handle, &error);
		if (status != SQLPARSER_STATUS_OK || handle == NULL) {
			fprintf(
				stderr,
				"FAIL [%s %s]: generation-0 parse failed: %s\n",
				cases[index].case_id,
				cases[index].case_name,
				error.message);
			sqlparser_handle_destroy(handle);
			failed = 1;
			continue;
		}
		if (verify_sqlserver_closure(
			    cases[index].case_id,
			    cases[index].case_name,
			    handle,
			    cases[index].sql,
			    1,
			    NULL) != 0) {
			sqlparser_handle_destroy(handle);
			failed = 1;
			continue;
		}

		memset(&error, 0, sizeof(error));
		if (cases[index].patch_kind ==
		    SQLSERVER_ODBC_PATCH_SET_INSERT_CELL) {
			status = sqlparser_insert_set_cell_sql(
				handle,
				0U,
				0U,
				0U,
				cases[index].patch_sql,
				&error);
		} else {
			status = sqlparser_select_insert_target_sql(
				handle,
				0U,
				0U,
				cases[index].target_index,
				cases[index].patch_sql,
				&error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL [%s %s]: independent patch failed: %s\n",
				cases[index].case_id,
				cases[index].case_name,
				error.message);
			failed = 1;
		} else if (verify_sqlserver_closure(
				   cases[index].case_id,
				   cases[index].case_name,
				   handle,
				   cases[index].expected_sql,
				   1,
				   NULL) != 0) {
			failed = 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int verify_sqlserver_control_non_target_rebind(void)
{
	static const char sql[] =
		"IF EXISTS (SELECT {fn ABS([id])} FROM [dbo].[a] WITH (NOLOCK) "
		"WHERE [tag] = N'keep') "
		"SELECT {fn UCASE([name])} AS [kept] "
		"ELSE SELECT 0 AS [changed]";
	static const char expected[] =
		"IF EXISTS (SELECT {fn ABS([id])} FROM [dbo].[a] WITH (NOLOCK) "
		"WHERE [tag] = N'keep') "
		"SELECT {fn UCASE([name])} AS [kept] "
		"ELSE SELECT 0 AS [changed], [extra]";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	int status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    verify_sqlserver_closure(
		    "SSL024",
		    "sqlserver-control-non-target-rebind",
		    handle,
		    sql,
		    1,
		    NULL) != 0 ||
	    sqlparser_select_insert_target_sql(
		    handle, 2U, 0U, 1U, "[extra]", &error) !=
		    SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL024",
		    "sqlserver-control-non-target-rebind",
		    handle,
		    expected,
		    1,
		    NULL) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL024 sqlserver-control-non-target-rebind]: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_fragment_context_surfaces(void)
{
	static const char order_sql[] =
		"SELECT {fn UCASE([name])} AS [kept] FROM [dbo].[t] "
		"ORDER BY [id]";
	static const char order_fragment[] =
		"{fn ABS({fn POWER([rank], 2)})} DESC, "
		"{fn UCASE([name])} ASC";
	static const char order_expected[] =
		"SELECT {fn UCASE([name])} AS [kept] FROM [dbo].[t] "
		"ORDER BY {fn ABS({fn POWER([rank], 2)})} DESC, "
		"{fn UCASE([name])} ASC";
	static const char update_sql[] =
		"UPDATE [dbo].[t] SET [a] = {fn ABS(-1)}, [b] = 0 "
		"WHERE [id] = {fn POWER(2, 3)}";
	static const char update_fragment[] =
		"[a] = {fn ABS({fn POWER(-2, 3)})}, "
		"[b] = {fn UCASE([name])}";
	static const char update_expected[] =
		"UPDATE [dbo].[t] SET [a] = {fn ABS({fn POWER(-2, 3)})}, "
		"[b] = {fn UCASE([name])} WHERE [id] = {fn POWER(2, 3)}";
	static const struct {
		const char *case_id;
		const char *case_name;
		const char *sql;
		const char *fragment;
		const char *expected;
		size_t clause_index;
	} cases[] = {
		{
			"SSL025",
			"sqlserver-order-by-fragment-context",
			order_sql,
			order_fragment,
			order_expected,
			2U
		},
		{
			"SSL026",
			"sqlserver-update-set-fragment-context",
			update_sql,
			update_fragment,
			update_expected,
			0U
		}
	};
	sqlparser_parse_options_t options;
	size_t index;
	int failed;

	failed = 0;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_error_t error;
		sqlparser_handle_t *handle;
		char *fragment;
		int status;

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		status = sqlparser_parse_with_options(
			cases[index].sql, &options, &handle, &error);
		if (status == SQLPARSER_STATUS_OK && handle != NULL) {
			status = sqlparser_statement_set_clause_sql(
				handle,
				0U,
				cases[index].clause_index,
				cases[index].fragment,
				&error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_statement_clause_sql(
				handle,
				0U,
				cases[index].clause_index,
				&fragment,
				&error);
		}
		if (status != SQLPARSER_STATUS_OK || fragment == NULL ||
		    strcmp(fragment, cases[index].fragment) != 0 ||
		    verify_sqlserver_closure(
			    cases[index].case_id,
			    cases[index].case_name,
			    handle,
			    cases[index].expected,
			    1,
			    NULL) != 0) {
			fprintf(
				stderr,
				"FAIL [%s %s]: fragment mismatch: %s\n",
				cases[index].case_id,
				cases[index].case_name,
				error.message);
			failed = 1;
		}
		sqlparser_string_free(fragment);
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int verify_sqlserver_nested_sibling_patch_surfaces(void)
{
	static const char sql[] =
		"SELECT [id] FROM [dbo].[t] WHERE {fn ABS([id])} > 0";
	static const char patch_sql[] =
		"{fn ABS({fn POWER(-2, 3)})} + {fn UCASE([name])} AS [patched]";
	static const char expected[] =
		"SELECT [id], {fn ABS({fn POWER(-2, 3)})} + "
		"{fn UCASE([name])} AS [patched] FROM [dbo].[t] "
		"WHERE {fn ABS([id])} > 0";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	int status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    sqlparser_select_insert_target_sql(
		    handle, 0U, 0U, 1U, patch_sql, &error) !=
		    SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL027",
		    "sqlserver-nested-sibling-patch-surfaces",
		    handle,
		    expected,
		    1,
		    NULL) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL027 sqlserver-nested-sibling-patch-surfaces]: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_table_hint_anchors(void)
{
	enum {
		SQLSERVER_HINT_PATCH_TARGET = 0,
		SQLSERVER_HINT_PATCH_ASSIGNMENT = 1,
		SQLSERVER_HINT_PATCH_CELL = 2
	};
	static const struct {
		const char *case_id;
		const char *case_name;
		const char *sql;
		const char *patch_sql;
		const char *expected;
		int patch_kind;
	} cases[] = {
		{
			"SSL028",
			"sqlserver-join-table-hint-anchor",
			"SELECT [u].[id] FROM [dbo].[users] AS [u] WITH (NOLOCK) "
			"JOIN [dbo].[orders] AS [o] WITH (FORCESEEK) "
			"ON [u].[id] = [o].[user_id]",
			"[extra]",
			"SELECT [u].[id], [extra] FROM [dbo].[users] AS [u] "
			"WITH (NOLOCK) JOIN [dbo].[orders] AS [o] WITH (FORCESEEK) "
			"ON [u].[id] = [o].[user_id]",
			SQLSERVER_HINT_PATCH_TARGET
		},
		{
			"SSL029",
			"sqlserver-update-table-hint-anchor",
			"UPDATE [t] WITH (UPDLOCK) SET [value] = 1 "
			"FROM [dbo].[target] AS [t] JOIN [dbo].[source] AS [s] "
			"WITH (NOLOCK) ON [t].[id] = [s].[id]",
			"{fn ABS(-2)}",
			"UPDATE [t] WITH (UPDLOCK) SET [value] = {fn ABS(-2)} "
			"FROM [dbo].[target] AS [t] JOIN [dbo].[source] AS [s] "
			"WITH (NOLOCK) ON [t].[id] = [s].[id]",
			SQLSERVER_HINT_PATCH_ASSIGNMENT
		},
		{
			"SSL030",
			"sqlserver-insert-table-hint-anchor",
			"INSERT INTO dbo.t WITH (TABLOCK) (a) OUTPUT INSERTED.id "
			"VALUES (1)",
			"{fn ABS(-2)}",
			"INSERT INTO dbo.t WITH (TABLOCK) (a) OUTPUT INSERTED.id "
			"VALUES ({fn ABS(-2)})",
			SQLSERVER_HINT_PATCH_CELL
		}
	};
	sqlparser_parse_options_t options;
	size_t index;
	int failed;

	failed = 0;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_error_t error;
		sqlparser_handle_t *handle;
		int status;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		status = sqlparser_parse_with_options(
			cases[index].sql, &options, &handle, &error);
		if (status == SQLPARSER_STATUS_OK && handle != NULL) {
			if (cases[index].patch_kind ==
			    SQLSERVER_HINT_PATCH_TARGET) {
				status = sqlparser_select_insert_target_sql(
					handle,
					0U,
					0U,
					1U,
					cases[index].patch_sql,
					&error);
			} else if (cases[index].patch_kind ==
				   SQLSERVER_HINT_PATCH_ASSIGNMENT) {
				status = sqlparser_update_set_assignment_sql(
					handle,
					0U,
					0U,
					cases[index].patch_sql,
					&error);
			} else {
				status = sqlparser_insert_set_cell_sql(
					handle,
					0U,
					0U,
					0U,
					cases[index].patch_sql,
					&error);
			}
		}
		if (status != SQLPARSER_STATUS_OK ||
		    verify_sqlserver_closure(
			    cases[index].case_id,
			    cases[index].case_name,
			    handle,
			    cases[index].expected,
			    1,
			    NULL) != 0) {
			fprintf(
				stderr,
				"FAIL [%s %s]: %s\n",
				cases[index].case_id,
				cases[index].case_name,
				error.message);
			failed = 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return failed;
}

static int verify_sqlserver_scratch_surface_projection(void)
{
	static const char sql[] =
		"SELECT {fn ABS([id])} AS [v], @@ROWCOUNT AS [rows] FROM #temp";
	static const char expected[] =
		"SELECT {fn ABS([id])} AS [v], @@ROWCOUNT AS [rows], "
		"{fn UCASE([name])} AS [n] FROM #temp";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	int status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    verify_sqlserver_closure(
		    "SSL031",
		    "sqlserver-scratch-surface-projection",
		    handle,
		    sql,
		    1,
		    NULL) != 0 ||
	    sqlparser_select_insert_target_sql(
		    handle,
		    0U,
		    0U,
		    2U,
		    "{fn UCASE([name])} AS [n]",
		    &error) != SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL031",
		    "sqlserver-scratch-surface-projection",
		    handle,
		    expected,
		    1,
		    NULL) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL031 sqlserver-scratch-surface-projection]: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_unicode_literal_owner_lookup(void)
{
	static const char tail[] = " FROM [dbo].[t]";
	static const char inserted[] = ", [extra]";
	const size_t literal_count = 129U;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	const char *tail_pos;
	char *expected;
	char *sql;
	size_t capacity;
	size_t expected_length;
	size_t index;
	size_t pos;
	int status;

	capacity = literal_count * 40U + sizeof(tail);
	sql = (char *)malloc(capacity);
	expected = NULL;
	if (sql == NULL) {
		return fail_case(
			"SSL032",
			"sqlserver-unicode-literal-owner-lookup",
			"out of memory");
	}
	pos = 0U;
	for (index = 0U; index < literal_count; index++) {
		int written;

		written = snprintf(
			sql + pos,
			capacity - pos,
			index == 0U ?
				"SELECT N'v%03lu' AS [c%03lu]" :
				", N'v%03lu' AS [c%03lu]",
			(unsigned long)index,
			(unsigned long)index);
		if (written < 0 || (size_t)written >= capacity - pos) {
			free(sql);
			return fail_case(
				"SSL032",
				"sqlserver-unicode-literal-owner-lookup",
				"SQL fixture is too large");
		}
		pos += (size_t)written;
	}
	memcpy(sql + pos, tail, sizeof(tail));
	tail_pos = strstr(sql, tail);
	if (tail_pos == NULL) {
		free(sql);
		return fail_case(
			"SSL032",
			"sqlserver-unicode-literal-owner-lookup",
			"SQL fixture tail is missing");
	}
	expected_length = strlen(sql) + sizeof(inserted) - 1U;
	expected = (char *)malloc(expected_length + 1U);
	if (expected == NULL) {
		free(sql);
		return fail_case(
			"SSL032",
			"sqlserver-unicode-literal-owner-lookup",
			"out of memory");
	}
	pos = (size_t)(tail_pos - sql);
	memcpy(expected, sql, pos);
	memcpy(expected + pos, inserted, sizeof(inserted) - 1U);
	memcpy(
		expected + pos + sizeof(inserted) - 1U,
		tail_pos,
		strlen(tail_pos) + 1U);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    verify_sqlserver_closure(
		    "SSL032",
		    "sqlserver-unicode-literal-owner-lookup",
		    handle,
		    sql,
		    1,
		    NULL) != 0 ||
	    sqlparser_select_insert_target_sql(
		    handle,
		    0U,
		    0U,
		    literal_count,
		    "[extra]",
		    &error) != SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL032",
		    "sqlserver-unicode-literal-owner-lookup",
		    handle,
		    expected,
		    1,
		    NULL) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL032 sqlserver-unicode-literal-owner-lookup]: %s\n",
			error.message);
		free(expected);
		free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	free(expected);
	free(sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_view_rebind_after_ast_clear(void)
{
	static const char sql[] =
		"SELECT TOP (2) TRY_CAST({fn ABS(-2)} AS BIT) AS [v] "
		"FROM #temp WITH (NOLOCK) WHERE [tag] = N'keep'";
	static const char expected[] =
		"SELECT TOP (2) TRY_CAST({fn ABS(-2)} AS BIT) AS [v], [extra] "
		"FROM #temp WITH (NOLOCK) WHERE [tag] = N'keep'";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *first_view;
	char *second_view;
	int status;

	handle = NULL;
	first_view = NULL;
	second_view = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status == SQLPARSER_STATUS_OK && handle != NULL) {
		status = sqlparser_export_view_json(
			handle, 0, &first_view, &error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_export_view_json(
			handle, 0, &second_view, &error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    (first_view == NULL || second_view == NULL ||
	     strcmp(first_view, second_view) != 0)) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_select_insert_target_sql(
			handle, 0U, 0U, 1U, "[extra]", &error);
	}
	if (status != SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL033",
		    "sqlserver-view-rebind-after-ast-clear",
		    handle,
		    expected,
		    1,
		    NULL) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL033 sqlserver-view-rebind-after-ast-clear]: %s\n",
			error.message);
		sqlparser_string_free(first_view);
		sqlparser_string_free(second_view);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(first_view);
	sqlparser_string_free(second_view);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int verify_sqlserver_deep_odbc_nesting(void)
{
	static const char prefix[] = "SELECT ";
	static const char wrapper_prefix[] = "{fn ABS(";
	static const char wrapper_suffix[] = ")}";
	static const char tail[] = " AS [v]";
	static const char inserted[] = ", [extra]";
	const size_t depth = 129U;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *expected;
	char *sql;
	size_t expected_length;
	size_t index;
	size_t pos;
	size_t sql_length;
	int status;

	sql_length = (sizeof(prefix) - 1U) +
		depth * ((sizeof(wrapper_prefix) - 1U) +
			 (sizeof(wrapper_suffix) - 1U)) +
		2U + (sizeof(tail) - 1U);
	expected_length = sql_length + (sizeof(inserted) - 1U);
	sql = (char *)malloc(sql_length + 1U);
	expected = (char *)malloc(expected_length + 1U);
	if (sql == NULL || expected == NULL) {
		free(sql);
		free(expected);
		return fail_case(
			"SSL023",
			"sqlserver-deep-odbc-nesting",
			"out of memory");
	}
	pos = 0U;
	memcpy(sql + pos, prefix, sizeof(prefix) - 1U);
	pos += sizeof(prefix) - 1U;
	for (index = 0U; index < depth; index++) {
		memcpy(
			sql + pos,
			wrapper_prefix,
			sizeof(wrapper_prefix) - 1U);
		pos += sizeof(wrapper_prefix) - 1U;
	}
	memcpy(sql + pos, "-1", 2U);
	pos += 2U;
	for (index = 0U; index < depth; index++) {
		memcpy(
			sql + pos,
			wrapper_suffix,
			sizeof(wrapper_suffix) - 1U);
		pos += sizeof(wrapper_suffix) - 1U;
	}
	memcpy(sql + pos, tail, sizeof(tail));
	memcpy(expected, sql, sql_length);
	memcpy(
		expected + sql_length,
		inserted,
		sizeof(inserted));

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL ||
	    verify_sqlserver_closure(
		    "SSL023",
		    "sqlserver-deep-odbc-nesting",
		    handle,
		    sql,
		    1,
		    NULL) != 0 ||
	    sqlparser_select_insert_target_sql(
		    handle, 0U, 0U, 1U, "[extra]", &error) !=
		    SQLPARSER_STATUS_OK ||
	    verify_sqlserver_closure(
		    "SSL023",
		    "sqlserver-deep-odbc-nesting",
		    handle,
		    expected,
		    1,
		    NULL) != 0) {
		fprintf(
			stderr,
			"FAIL [SSL023 sqlserver-deep-odbc-nesting]: %s\n",
			error.message);
		free(expected);
		free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	free(expected);
	free(sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

int main(void)
{
	int failed;

	failed = 0;
	if (verify_sqlserver_fragment_rewrite_paths() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_surface_owner_lifecycle() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_insert_default_clone_state() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_assignment_value_clone_state() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_surface_state_failure_rollback() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_partition_generated_identifier() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_partition_scalar_ordinals() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_odbc_scalar_closure() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_control_non_target_rebind() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_fragment_context_surfaces() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_nested_sibling_patch_surfaces() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_table_hint_anchors() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_scratch_surface_projection() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_unicode_literal_owner_lookup() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_view_rebind_after_ast_clear() != 0) {
		failed = 1;
	}
	if (verify_sqlserver_deep_odbc_nesting() != 0) {
		failed = 1;
	}
	return failed;
}
