#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

#define SQLPARSER_ARRAY_LEN(array_value) (sizeof(array_value) / sizeof((array_value)[0]))

typedef struct {
	sqlparser_dialect_t dialect;
	const char *sql;
} sqlparser_robustness_case_t;

static int expect_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}
	return 0;
}

static int expect_status(
	sqlparser_status_t actual,
	sqlparser_status_t expected,
	const sqlparser_error_t *error,
	const char *message)
{
	if (actual != expected) {
		fprintf(stderr,
		        "FAIL: %s: expected=%d actual=%d message=%s\n",
		        message,
		        (int)expected,
		        (int)actual,
		        error != NULL ? error->message : "unknown");
		return 1;
	}
	return 0;
}

static int expect_ok(sqlparser_status_t actual, const sqlparser_error_t *error, const char *message)
{
	return expect_status(actual, SQLPARSER_STATUS_OK, error, message);
}

static int expect_not_ok(sqlparser_status_t actual, const char *message)
{
	if (actual == SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s: expected non-OK status\n", message);
		return 1;
	}
	return 0;
}

static sqlparser_status_t parse_with_dialect(
	const char *sql,
	sqlparser_dialect_t dialect,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	sqlparser_parse_options_t options;

	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	return sqlparser_parse_with_options(sql, &options, out_handle, out_error);
}

static int verify_deparse_is_parseable(
	sqlparser_handle_t *handle,
	sqlparser_dialect_t dialect,
	const char *message)
{
	sqlparser_handle_t *roundtrip_handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *sql_text;

	roundtrip_handle = NULL;
	sql_text = NULL;
	memset(&error, 0, sizeof(error));

	status = sqlparser_deparse(handle, &sql_text, &error);
	if (expect_ok(status, &error, message) != 0 ||
	    expect_true(sql_text != NULL && sql_text[0] != '\0', "deparse should return non-empty SQL") != 0) {
		sqlparser_string_free(sql_text);
		return 1;
	}

	status = parse_with_dialect(sql_text, dialect, &roundtrip_handle, &error);
	if (expect_ok(status, &error, "deparsed SQL should parse again") != 0 ||
	    expect_true(roundtrip_handle != NULL, "roundtrip parse should return a handle") != 0) {
		sqlparser_handle_destroy(roundtrip_handle);
		sqlparser_string_free(sql_text);
		return 1;
	}

	sqlparser_handle_destroy(roundtrip_handle);
	sqlparser_string_free(sql_text);
	return 0;
}

static int test_null_safe_entry_points(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *text;

	handle = NULL;
	text = NULL;
	memset(&error, 0, sizeof(error));

	sqlparser_limits_default(NULL);
	sqlparser_parse_options_default(NULL);
	sqlparser_handle_destroy(NULL);
	sqlparser_string_free(NULL);

	if (expect_true(sqlparser_original_sql(NULL) == NULL, "NULL handle original SQL should be NULL") != 0 ||
	    expect_true(sqlparser_statement_count(NULL) == 0U, "NULL handle statement count should be zero") != 0 ||
	    expect_true(sqlparser_handle_dialect(NULL) == SQLPARSER_DIALECT_POSTGRESQL, "NULL handle dialect should be default") != 0) {
		return 1;
	}

	status = sqlparser_parse(NULL, &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL SQL should be rejected") != 0 ||
	    expect_true(handle == NULL, "NULL SQL should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_parse("", &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "empty SQL should be rejected") != 0 ||
	    expect_true(handle == NULL, "empty SQL should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_parse("SELECT 1", NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL out_handle should be rejected") != 0) {
		return 1;
	}

	status = sqlparser_deparse(NULL, &text, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle deparse should be rejected") != 0 ||
	    expect_true(text == NULL, "failed deparse should not return SQL") != 0) {
		sqlparser_string_free(text);
		return 1;
	}

	status = sqlparser_deparse(NULL, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL deparse output should be rejected") != 0) {
		return 1;
	}

	status = sqlparser_export_view_json(NULL, 0, &text, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle JSON export should be rejected") != 0 ||
	    expect_true(text == NULL, "failed JSON export should not return text") != 0) {
		sqlparser_string_free(text);
		return 1;
	}

	status = sqlparser_export_view_json(NULL, 0, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL JSON output should be rejected") != 0) {
		return 1;
	}

	return 0;
}

static int test_selector_validation(void)
{
	static const char *invalid_selectors[] = {
		NULL,
		"",
		"statement[0].name[0]",
		"stmt",
		"stmt.name[0]",
		"stmt[].name[0]",
		"stmt[-1].name[0]",
		"stmt[0]",
		"stmt[0].unknown[0]",
		"stmt[0].name",
		"stmt[0].insert_cell[0]",
		"stmt[0].select_target[0]",
		"stmt[0].name[0] trailing",
		"stmt[0].name[999999999999999999999999999999999999]"
	};
	sqlparser_selector_t selector;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *selector_text;
	size_t index;

	selector_text = NULL;
	memset(&error, 0, sizeof(error));

	for (index = 0U; index < SQLPARSER_ARRAY_LEN(invalid_selectors); index++) {
		memset(&selector, 0, sizeof(selector));
		status = sqlparser_selector_parse(invalid_selectors[index], &selector, &error);
		if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "invalid selector should be rejected") != 0) {
			fprintf(stderr, "FAIL: invalid selector index %lu\n", (unsigned long)index);
			return 1;
		}
	}

	status = sqlparser_selector_parse("stmt[0].relation[0]", NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL selector output should be rejected") != 0) {
		return 1;
	}

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	status = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "unknown selector kind should not format") != 0 ||
	    expect_true(selector_text == NULL, "failed selector format should not return text") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	status = sqlparser_selector_format(NULL, &selector_text, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL selector should not format") != 0 ||
	    expect_true(selector_text == NULL, "NULL selector format should not return text") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
	status = sqlparser_selector_format(&selector, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL selector format output should be rejected") != 0) {
		return 1;
	}

	return 0;
}

static int test_view_access_validation(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t object;
	sqlparser_object_view_t temp_object;
	sqlparser_column_view_t column;
	sqlparser_column_view_t temp_column;
	sqlparser_row_view_t row;
	sqlparser_row_view_t temp_row;
	sqlparser_cell_view_t cell;
	sqlparser_cell_view_t temp_cell;
	const char *keyword;
	char *cell_sql;

	handle = NULL;
	keyword = NULL;
	cell_sql = NULL;
	memset(&error, 0, sizeof(error));

	status = sqlparser_parse(
		"INSERT INTO public.users (id, name) VALUES (1, 'bob'), (2, 'alice')",
		&handle,
		&error);
	if (expect_ok(status, &error, "insert statement should parse") != 0 ||
	    expect_true(handle != NULL, "insert parse should return handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_get_view(NULL, &view, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle view should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_get_view(handle, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL view output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_get_view(handle, &view, &error);
	if (expect_ok(status, &error, "view should be created") != 0 ||
	    expect_true(view.statement_count == 1U, "view should expose one statement") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_view_statement_at(NULL, 0U, &statement, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL view statement lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_view_statement_at(&view, 9U, &statement, &error);
	if (expect_not_ok(status, "out-of-range statement lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_view_statement_at(&view, 0U, &statement, &error);
	if (expect_ok(status, &error, "statement view should be created") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_keyword_at(&statement, 0U, &keyword, &error);
	if (expect_ok(status, &error, "keyword lookup should succeed") != 0 ||
	    expect_true(keyword != NULL && strcmp(keyword, "insert") == 0, "keyword should be insert") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_keyword_at(NULL, 0U, &keyword, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL statement keyword lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_keyword_at(&statement, 99U, &keyword, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "out-of-range keyword lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_keyword_at(&statement, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL keyword output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_object_at(&statement, 0U, &object, &error);
	if (expect_ok(status, &error, "object lookup should succeed") != 0 ||
	    expect_true(object.column_count == 2U, "insert object should expose two columns") != 0 ||
	    expect_true(object.row_count == 2U, "insert object should expose two rows") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_object_at(NULL, 0U, &temp_object, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL statement object lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_object_at(&statement, 9U, &temp_object, &error);
	if (expect_not_ok(status, "out-of-range object lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_object_column_at(&object, 0U, &column, &error);
	if (expect_ok(status, &error, "column lookup should succeed") != 0 ||
	    expect_true(column.name != NULL && strcmp(column.name, "id") == 0, "first column should be id") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_object_column_at(NULL, 0U, &temp_column, &error);
	if (expect_not_ok(status, "NULL object column lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_object_column_at(&object, 99U, &temp_column, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "out-of-range column lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_object_row_at(&object, 0U, &row, &error);
	if (expect_ok(status, &error, "row lookup should succeed") != 0 ||
	    expect_true(row.cell_count == 2U, "insert row should expose two cells") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_object_row_at(NULL, 0U, &temp_row, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL object row lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_object_row_at(&object, 99U, &temp_row, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "out-of-range row lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_row_cell_at(&row, 0U, &cell, &error);
	if (expect_ok(status, &error, "cell lookup should succeed") != 0 ||
	    expect_true(cell.has_selector, "cell should expose selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_row_cell_at(NULL, 0U, &temp_cell, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL row cell lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_row_cell_at(&row, 99U, &temp_cell, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "out-of-range cell lookup should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_cell_sql(&cell, &cell_sql, &error);
	if (expect_ok(status, &error, "cell SQL should be readable") != 0 ||
	    expect_true(cell_sql != NULL && strcmp(cell_sql, "1") == 0, "first cell SQL should be 1") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(cell_sql);
	cell_sql = NULL;

	status = sqlparser_cell_sql(NULL, &cell_sql, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL cell SQL lookup should be rejected") != 0 ||
	    expect_true(cell_sql == NULL, "failed cell SQL lookup should not return text") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_cell_sql(&cell, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL cell SQL output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int find_clause_index(
	const sqlparser_handle_t *handle,
	sqlparser_clause_kind_t kind,
	size_t *out_clause_index)
{
	sqlparser_error_t error;
	sqlparser_status_t status;
	sqlparser_clause_view_t clause;
	size_t clause_count;
	size_t clause_index;

	memset(&error, 0, sizeof(error));
	clause_count = 0U;
	status = sqlparser_statement_clause_count(handle, 0U, &clause_count, &error);
	if (expect_ok(status, &error, "clause count should be readable") != 0) {
		return 1;
	}

	for (clause_index = 0U; clause_index < clause_count; clause_index++) {
		status = sqlparser_statement_clause(handle, 0U, clause_index, &clause, &error);
		if (expect_ok(status, &error, "clause view should be readable") != 0) {
			return 1;
		}
		if (clause.kind == kind) {
			*out_clause_index = clause_index;
			return 0;
		}
	}

	fprintf(stderr, "FAIL: expected clause kind %d was not found\n", (int)kind);
	return 1;
}

static int verify_patch_failure_preserves_handle(sqlparser_handle_t *handle, sqlparser_dialect_t dialect)
{
	if (verify_deparse_is_parseable(handle, dialect, "handle should remain parseable after failed patch") != 0) {
		return 1;
	}
	return 0;
}

static int test_patch_and_clause_validation(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	sqlparser_clause_view_t clause;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patches;
	size_t clause_count;
	size_t where_clause_index;
	char selector_text[64];
	char *clause_sql;

	handle = NULL;
	clause_sql = NULL;
	memset(&error, 0, sizeof(error));

	status = sqlparser_parse(
		"SELECT id, name FROM public.users WHERE id = 1 ORDER BY name",
		&handle,
		&error);
	if (expect_ok(status, &error, "select statement should parse") != 0 ||
	    expect_true(handle != NULL, "select parse should return handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	clause_count = 0U;
	status = sqlparser_statement_clause_count(NULL, 0U, &clause_count, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle clause count should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause_count(handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL clause count output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause_count(handle, 0U, &clause_count, &error);
	if (expect_ok(status, &error, "clause count should succeed") != 0 ||
	    expect_true(clause_count >= 3U, "select should expose select-list, where, and order-by clauses") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause(handle, 99U, 0U, &clause, &error);
	if (expect_not_ok(status, "out-of-range statement clause lookup should fail") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause(handle, 0U, 99U, &clause, &error);
	if (expect_not_ok(status, "out-of-range clause lookup should fail") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause(handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL clause output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause_sql(handle, 0U, 99U, &clause_sql, &error);
	if (expect_not_ok(status, "out-of-range clause SQL lookup should fail") != 0 ||
	    expect_true(clause_sql == NULL, "failed clause SQL lookup should not return text") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_clause_sql(handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL clause SQL output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_set_clause_sql(handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL clause replacement SQL should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_set_clause_sql(handle, 0U, 99U, "x", &error);
	if (expect_not_ok(status, "out-of-range clause replacement should fail") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_append_clause_condition(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"name <> 'x'",
		&error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "append condition on non-WHERE clause should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (find_clause_index(handle, SQLPARSER_CLAUSE_KIND_WHERE, &where_clause_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	memset(&patches, 0, sizeof(patches));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].select_targets[0]";
	patch.sql = "id, name, upper(name) AS normalized_name";
	patches.items = &patch;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_ok(status, &error, "select target replacement patch should succeed") != 0 ||
	    verify_deparse_is_parseable(handle, SQLPARSER_DIALECT_POSTGRESQL, "select target replacement should deparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	(void)snprintf(selector_text, sizeof(selector_text), "stmt[0].clause[%lu]", (unsigned long)where_clause_index);
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch.selector = selector_text;
	patch.sql = "name <> 'blocked'";
	patch.bool_operator = SQLPARSER_BOOL_OPERATOR_AND;
	patches.items = &patch;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_ok(status, &error, "WHERE append-condition patch should succeed") != 0 ||
	    verify_deparse_is_parseable(handle, SQLPARSER_DIALECT_POSTGRESQL, "WHERE append-condition should deparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_apply_patch(handle, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL patch list should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	patches.items = NULL;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "patch list with NULL items should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].name[999]";
	patch.sql = "renamed";
	patches.items = &patch;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_not_ok(status, "out-of-range replacement patch should fail") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].select_targets[0]";
	patch.sql = NULL;
	patches.items = &patch;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "replace patch without SQL should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch.selector = "stmt[0].select_targets[0]";
	patch.sql = "id = 2";
	patch.bool_operator = SQLPARSER_BOOL_OPERATOR_AND;
	patches.items = &patch;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "append condition with wrong selector should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = (sqlparser_patch_op_t)999;
	patch.selector = "stmt[0].select_targets[0]";
	patch.sql = "id";
	patches.items = &patch;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_status(status, SQLPARSER_STATUS_UNSUPPORTED, &error, "unknown patch op should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	patches.items = NULL;
	patches.count = 0U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_ok(status, &error, "empty patch list should be a no-op") != 0 ||
	    verify_deparse_is_parseable(handle, SQLPARSER_DIALECT_POSTGRESQL, "empty patch list should keep handle valid") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_statement_api_validation(void)
{
	sqlparser_handle_t *select_handle;
	sqlparser_handle_t *insert_handle;
	sqlparser_handle_t *update_handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	sqlparser_statement_kind_t kind;
	sqlparser_relation_view_t relation;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t value;
	sqlparser_assignment_view_t assignment;
	sqlparser_insert_source_kind_t insert_source_kind;
	const char *name;
	char *sql_text;
	size_t count;

	select_handle = NULL;
	insert_handle = NULL;
	update_handle = NULL;
	name = NULL;
	sql_text = NULL;
	memset(&error, 0, sizeof(error));
	memset(&value, 0, sizeof(value));
	value.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	value.integer_value = 7;

	status = sqlparser_parse("SELECT id, name FROM users WHERE id = 1", &select_handle, &error);
	if (expect_ok(status, &error, "select should parse") != 0) {
		return 1;
	}
	status = sqlparser_parse("INSERT INTO users (id, name) VALUES (1, 'bob')", &insert_handle, &error);
	if (expect_ok(status, &error, "insert should parse") != 0) {
		sqlparser_handle_destroy(select_handle);
		return 1;
	}
	status = sqlparser_parse("UPDATE users SET name = 'bob' WHERE id = 1", &update_handle, &error);
	if (expect_ok(status, &error, "update should parse") != 0) {
		sqlparser_handle_destroy(update_handle);
		sqlparser_handle_destroy(insert_handle);
		sqlparser_handle_destroy(select_handle);
		return 1;
	}

	status = sqlparser_statement_kind(NULL, 0U, &kind, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle statement kind should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_kind(select_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL statement kind output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_kind(select_handle, 99U, &kind, &error);
	if (expect_not_ok(status, "out-of-range statement kind should be rejected") != 0) {
		goto fail;
	}

	status = sqlparser_statement_relation_count(NULL, 0U, &count, &error);
	if (expect_not_ok(status, "NULL handle relation count should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_relation_count(select_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL relation count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_relation(select_handle, 0U, 99U, &relation, &error);
	if (expect_not_ok(status, "out-of-range relation lookup should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_relation(select_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL relation output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_set_relation_name(select_handle, 0U, 99U, NULL, "t", &error);
	if (expect_not_ok(status, "out-of-range relation rewrite should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}

	status = sqlparser_insert_source_kind(select_handle, 0U, &insert_source_kind, &error);
	if (expect_not_ok(status, "insert source on SELECT should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_source_kind(insert_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL insert source output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_column_count(insert_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL insert column count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_column_name(insert_handle, 0U, 99U, &name, &error);
	if (expect_not_ok(status, "out-of-range insert column lookup should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_row_count(insert_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL insert row count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_cell_literal(insert_handle, 0U, 9U, 0U, &literal, &error);
	if (expect_not_ok(status, "out-of-range insert literal lookup should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_cell_literal(insert_handle, 0U, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL insert literal output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_set_cell_literal(insert_handle, 0U, 0U, 0U, NULL, &error);
	if (expect_not_ok(status, "NULL insert literal value should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(insert_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}
	status = sqlparser_insert_cell_sql(insert_handle, 0U, 0U, 99U, &sql_text, &error);
	if (expect_not_ok(status, "out-of-range insert cell SQL should be rejected") != 0 ||
	    expect_true(sql_text == NULL, "failed insert cell SQL should not return text") != 0) {
		goto fail;
	}
	status = sqlparser_insert_cell_sql(insert_handle, 0U, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL insert cell SQL output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_insert_set_cell_sql(insert_handle, 0U, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL insert replacement SQL should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(insert_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}

	status = sqlparser_select_target_count(select_handle, 0U, 99U, &count, &error);
	if (expect_not_ok(status, "out-of-range select target count should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_select_target_count(select_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL select target count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_select_target_sql(select_handle, 0U, 0U, 99U, &sql_text, &error);
	if (expect_not_ok(status, "out-of-range select target SQL should be rejected") != 0 ||
	    expect_true(sql_text == NULL, "failed select target SQL should not return text") != 0) {
		goto fail;
	}
	status = sqlparser_select_set_target_sql(select_handle, 0U, 0U, 99U, "id", &error);
	if (expect_not_ok(status, "out-of-range select target replacement should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}
	status = sqlparser_select_set_targets_sql(select_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL select target list SQL should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}
	status = sqlparser_select_delete_target(select_handle, 0U, 0U, 99U, &error);
	if (expect_not_ok(status, "out-of-range select target delete should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}

	status = sqlparser_update_assignment_count(update_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL update assignment count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_update_assignment(update_handle, 0U, 99U, &assignment, &error);
	if (expect_not_ok(status, "out-of-range update assignment lookup should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_update_assignment(update_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL update assignment output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_update_set_assignment_literal(update_handle, 0U, 0U, NULL, &error);
	if (expect_not_ok(status, "NULL update assignment value should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(update_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}
	status = sqlparser_update_assignment_sql(update_handle, 0U, 99U, &sql_text, &error);
	if (expect_not_ok(status, "out-of-range update assignment SQL should be rejected") != 0 ||
	    expect_true(sql_text == NULL, "failed update assignment SQL should not return text") != 0) {
		goto fail;
	}
	status = sqlparser_update_assignment_sql(update_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL update assignment SQL output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_update_set_assignment_sql(update_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL update assignment replacement SQL should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(update_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}

	status = sqlparser_statement_where_count(select_handle, 99U, &count, &error);
	if (expect_not_ok(status, "out-of-range where count should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_where_literal_count(select_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL where literal count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_where_sql(select_handle, 0U, 99U, &sql_text, &error);
	if (expect_not_ok(status, "out-of-range where SQL should be rejected") != 0 ||
	    expect_true(sql_text == NULL, "failed where SQL should not return text") != 0) {
		goto fail;
	}
	status = sqlparser_statement_set_where_sql(select_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL where replacement SQL should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}
	status = sqlparser_statement_append_where_sql(select_handle, 0U, 0U, SQLPARSER_BOOL_OPERATOR_AND, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL where append SQL should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}

	status = sqlparser_statement_literal_count(select_handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL literal count output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_literal(select_handle, 0U, 99U, &literal, &error);
	if (expect_not_ok(status, "out-of-range literal lookup should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_literal(select_handle, 0U, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL literal output should be rejected") != 0) {
		goto fail;
	}
	status = sqlparser_statement_set_literal(select_handle, 0U, 0U, NULL, &error);
	if (expect_not_ok(status, "NULL literal value should be rejected") != 0 ||
	    verify_patch_failure_preserves_handle(select_handle, SQLPARSER_DIALECT_POSTGRESQL) != 0) {
		goto fail;
	}

	sqlparser_handle_destroy(update_handle);
	sqlparser_handle_destroy(insert_handle);
	sqlparser_handle_destroy(select_handle);
	return 0;

fail:
	sqlparser_string_free(sql_text);
	sqlparser_handle_destroy(update_handle);
	sqlparser_handle_destroy(insert_handle);
	sqlparser_handle_destroy(select_handle);
	return 1;
}

static int expect_parse_failure(const sqlparser_robustness_case_t *test_case, size_t index)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));

	status = parse_with_dialect(test_case->sql, test_case->dialect, &handle, &error);
	if (expect_not_ok(status, "malformed SQL should not parse") != 0 ||
	    expect_true(handle == NULL, "failed parse should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		fprintf(stderr, "FAIL: malformed case index %lu\n", (unsigned long)index);
		return 1;
	}

	return 0;
}

static int exercise_parse_no_crash(
	const char *sql,
	sqlparser_dialect_t dialect,
	const char *message)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *json_text;
	char *deparsed_sql;

	handle = NULL;
	json_text = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	status = parse_with_dialect(sql, dialect, &handle, &error);
	if (status == SQLPARSER_STATUS_OK) {
		if (expect_true(handle != NULL, message) != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		status = sqlparser_export_view_json(handle, 0, &json_text, &error);
		if (status == SQLPARSER_STATUS_OK &&
		    expect_true(json_text != NULL, "successful JSON export should return text") != 0) {
			sqlparser_string_free(json_text);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (status != SQLPARSER_STATUS_OK &&
		    expect_true(json_text == NULL, "failed JSON export should not return text") != 0) {
			sqlparser_string_free(json_text);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		status = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (status == SQLPARSER_STATUS_OK &&
		    expect_true(deparsed_sql != NULL && deparsed_sql[0] != '\0', "successful deparse should return text") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_string_free(json_text);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (status != SQLPARSER_STATUS_OK &&
		    expect_true(deparsed_sql == NULL, "failed deparse should not return text") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_string_free(json_text);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 0;
	}

	if (expect_true(handle == NULL, "failed parse should leave output handle NULL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	return 0;
}

static int test_malformed_and_stress_inputs(void)
{
	static const sqlparser_robustness_case_t malformed_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "SELECT * FROM"},
		{SQLPARSER_DIALECT_POSTGRESQL, "INSERT INTO t (id) VALUES ("},
		{SQLPARSER_DIALECT_POSTGRESQL, "UPDATE t SET WHERE id = 1"},
		{SQLPARSER_DIALECT_POSTGRESQL, "DELETE FROM WHERE id = 1"},
		{SQLPARSER_DIALECT_POSTGRESQL, "CREATE TABLE t ("},
		{SQLPARSER_DIALECT_MYSQL, "SELECT * FROM `unterminated"},
		{SQLPARSER_DIALECT_MYSQL, "INSERT INTO `t` VALUES ("},
		{SQLPARSER_DIALECT_ORACLE, "SELECT q'[unterminated' FROM dual"},
		{SQLPARSER_DIALECT_ORACLE, "ALTER SESSION SET"},
		{SQLPARSER_DIALECT_SQLSERVER, "SELECT [unterminated"},
		{SQLPARSER_DIALECT_SQLSERVER, "UPDATE [dbo].[t] SET WHERE [id] = 1"}
	};
	char invalid_utf8[] = {'S', 'E', 'L', 'E', 'C', 'T', ' ', (char)0xff, 0};
	char *long_unclosed_quote;
	char *deep_parens;
	size_t index;

	for (index = 0U; index < SQLPARSER_ARRAY_LEN(malformed_cases); index++) {
		if (expect_parse_failure(&malformed_cases[index], index) != 0) {
			return 1;
		}
	}

	if (exercise_parse_no_crash(invalid_utf8, SQLPARSER_DIALECT_POSTGRESQL, "invalid byte input should not crash") != 0) {
		return 1;
	}

	long_unclosed_quote = (char *)malloc(4096U);
	if (long_unclosed_quote == NULL) {
		fprintf(stderr, "FAIL: failed to allocate long malformed SQL\n");
		return 1;
	}
	memset(long_unclosed_quote, 'a', 4095U);
	memcpy(long_unclosed_quote, "SELECT '", 8U);
	long_unclosed_quote[4095U] = '\0';
	if (exercise_parse_no_crash(long_unclosed_quote, SQLPARSER_DIALECT_POSTGRESQL, "long malformed SQL should not crash") != 0) {
		free(long_unclosed_quote);
		return 1;
	}
	free(long_unclosed_quote);

	deep_parens = (char *)malloc(4096U);
	if (deep_parens == NULL) {
		fprintf(stderr, "FAIL: failed to allocate nested malformed SQL\n");
		return 1;
	}
	memset(deep_parens, '(', 4095U);
	memcpy(deep_parens, "SELECT ", 7U);
	deep_parens[4095U] = '\0';
	if (exercise_parse_no_crash(deep_parens, SQLPARSER_DIALECT_POSTGRESQL, "deeply nested malformed SQL should not crash") != 0) {
		free(deep_parens);
		return 1;
	}
	free(deep_parens);

	for (index = 0U; index < 256U; index++) {
		char sql_buffer[256];

		if ((index % 4U) == 0U) {
			(void)snprintf(sql_buffer, sizeof(sql_buffer), "SELECT %lu AS v", (unsigned long)index);
		} else if ((index % 4U) == 1U) {
			(void)snprintf(sql_buffer, sizeof(sql_buffer), "SELECT 'unterminated_%lu", (unsigned long)index);
		} else if ((index % 4U) == 2U) {
			(void)snprintf(sql_buffer, sizeof(sql_buffer), "INSERT INTO t (id, name) VALUES (%lu, 'n_%lu')", (unsigned long)index, (unsigned long)index);
		} else {
			(void)snprintf(sql_buffer, sizeof(sql_buffer), "UPDATE t SET name = 'n_%lu' WHERE id = %lu", (unsigned long)index, (unsigned long)index);
		}
		if (exercise_parse_no_crash(sql_buffer, SQLPARSER_DIALECT_POSTGRESQL, "generated robustness SQL should not crash") != 0) {
			fprintf(stderr, "FAIL: generated robustness index %lu\n", (unsigned long)index);
			return 1;
		}
	}

	return 0;
}

int main(void)
{
	if (test_null_safe_entry_points() != 0 ||
	    test_selector_validation() != 0 ||
	    test_view_access_validation() != 0 ||
	    test_patch_and_clause_validation() != 0 ||
	    test_statement_api_validation() != 0 ||
	    test_malformed_and_stress_inputs() != 0) {
		return 1;
	}

	return 0;
}
