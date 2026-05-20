#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"

static int expect_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}

	return 0;
}

static int expect_status_ok(sqlparser_status_t status, const sqlparser_error_t *error, const char *message)
{
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s: %s\n", message, error != NULL ? error->message : "unknown");
		return 1;
	}

	return 0;
}

static int expect_deparse_reparse_ok(const sqlparser_handle_t *handle, const char *message)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t error;
	char *sql;
	int rc;

	if (handle == NULL) {
		fprintf(stderr, "FAIL: %s: handle is NULL\n", message);
		return 1;
	}

	sql = NULL;
	reparsed = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		return 1;
	}

	sqlparser_parse_options_default(&options);
	options.dialect = sqlparser_handle_dialect(handle);
	rc = sqlparser_parse_with_options(sql, &options, &reparsed, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		sqlparser_string_free(sql);
		return 1;
	}

	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(sql);
	return 0;
}

static int find_name_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *owner_type,
	const char *field_name,
	const char *value,
	size_t *out_index)
{
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	size_t count;
	size_t index;
	int rc;

	if (out_index == NULL) {
		fprintf(stderr, "FAIL: out_index must not be NULL\n");
		return 1;
	}

	*out_index = 0U;
	memset(&error, 0, sizeof(error));
	memset(&name, 0, sizeof(name));

	rc = sqlparser_statement_name_count(handle, statement_index, &count, &error);
	if (expect_status_ok(rc, &error, "statement name count should succeed") != 0) {
		return 1;
	}

	for (index = 0U; index < count; index++) {
		rc = sqlparser_statement_name(handle, statement_index, index, &name, &error);
		if (expect_status_ok(rc, &error, "statement name fetch should succeed") != 0) {
			return 1;
		}

		if ((owner_type == NULL || (name.owner_type != NULL && strcmp(name.owner_type, owner_type) == 0)) &&
		    (field_name == NULL || (name.field_name != NULL && strcmp(name.field_name, field_name) == 0)) &&
		    (value == NULL || (name.value != NULL && strcmp(name.value, value) == 0))) {
			*out_index = index;
			return 0;
		}
	}

	fprintf(stderr,
	        "FAIL: statement name not found: owner=%s field=%s value=%s\n",
	        owner_type != NULL ? owner_type : "(any)",
	        field_name != NULL ? field_name : "(any)",
	        value != NULL ? value : "(any)");
	return 1;
}

static int test_statement_kind_walk(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_statement_kind_t kind;
	const char *node_name;
	int rc;

	sql = "SELECT 1; INSERT INTO t (id) VALUES (1); UPDATE t SET id = 2; DELETE FROM t WHERE id = 2; DROP VIEW v_t; BEGIN";
	handle = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "multi statement parse should succeed") != 0) {
		return 1;
	}

	if (expect_true(sqlparser_statement_count(handle) == 6U, "statement count should be 6") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 0U, &kind, &error);
	if (expect_status_ok(rc, &error, "statement 0 kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_SELECT, "statement 0 should be SELECT") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_node_name(handle, 1U, &node_name, &error);
	if (expect_status_ok(rc, &error, "statement 1 node name should succeed") != 0 ||
	    expect_true(strcmp(node_name, "InsertStmt") == 0, "statement 1 should be InsertStmt") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 4U, &kind, &error);
	if (expect_status_ok(rc, &error, "statement 4 kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_DDL, "statement 4 should be DDL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 5U, &kind, &error);
	if (expect_status_ok(rc, &error, "statement 5 kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_TRANSACTION, "statement 5 should be transaction") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_values_literal_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_relation_view_t relation;
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	const char *column_name;
	size_t column_count;
	size_t row_count;
	int rc;

	sql = "INSERT INTO public.users (id, name, age) VALUES (1, 'bob', 18), (2, 'alice', 19)";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&relation, 0, sizeof(relation));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "insert parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_target_relation(handle, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "insert relation should succeed") != 0 ||
	    expect_true(strcmp(relation.schema_name, "public") == 0, "insert schema should be public") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "insert table should be users") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "insert source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_VALUES, "insert source should be VALUES") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_column_count(handle, 0U, &column_count, &error);
	if (expect_status_ok(rc, &error, "insert column count should succeed") != 0 ||
	    expect_true(column_count == 3U, "insert column count should be 3") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_column_name(handle, 0U, 1U, &column_name, &error);
	if (expect_status_ok(rc, &error, "insert column name should succeed") != 0 ||
	    expect_true(strcmp(column_name, "name") == 0, "column 1 should be name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "insert row count should succeed") != 0 ||
	    expect_true(row_count == 2U, "insert row count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_literal(handle, 0U, 0U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "insert cell literal should succeed") != 0 ||
	    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "insert cell kind should be string") != 0 ||
	    expect_true(strcmp(literal.string_value, "bob") == 0, "insert cell value should be bob") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "carol";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 1U, 1U, &replacement, &error);
	if (expect_status_ok(rc, &error, "insert cell mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "insert deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "carol") != NULL, "deparsed insert should contain carol") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_select_inspect(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_literal_view_t literal;
	sqlparser_where_literal_view_t where_literal;
	size_t row_count;
	size_t where_count;
	int rc;

	sql = "INSERT INTO archive_users (id, name) SELECT id, name FROM users WHERE active = true";
	handle = NULL;
	memset(&error, 0, sizeof(error));
	memset(&literal, 0, sizeof(literal));
	memset(&where_literal, 0, sizeof(where_literal));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "insert-select parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "insert-select source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_QUERY, "insert-select source should be query") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "insert-select row count should succeed") != 0 ||
	    expect_true(row_count == 0U, "insert-select row count should be 0") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "insert-select where literal count should succeed") != 0 ||
	    expect_true(where_count == 1U, "insert-select where literal count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "insert-select where literal should succeed") != 0 ||
	    expect_true(where_literal.literal.kind == SQLPARSER_LITERAL_KIND_BOOLEAN, "insert-select where literal should be boolean") != 0 ||
	    expect_true(where_literal.literal.boolean_value == 1, "insert-select where literal should be true") != 0 ||
	    expect_true(strcmp(where_literal.column_name, "active") == 0, "insert-select where column should be active") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_literal(handle, 0U, 0U, 0U, &literal, &error);
	if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED, "insert-select should reject values cell access") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_assignment_literal_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t replacement;
	sqlparser_literal_value_t where_replacement;
	sqlparser_relation_view_t relation;
	char *deparsed_sql;
	char *view_json;
	size_t assignment_count;
	size_t where_count;
	int rc;

	sql = "UPDATE public.users SET name = 'bob', age = 18 WHERE id = 1 AND status = 'active'";
	handle = NULL;
	deparsed_sql = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&replacement, 0, sizeof(replacement));
	memset(&where_replacement, 0, sizeof(where_replacement));
	memset(&relation, 0, sizeof(relation));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "update parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_target_relation(handle, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "update relation should succeed") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "update table should be users") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "update assignment count should succeed") != 0 ||
	    expect_true(assignment_count == 2U, "update assignment count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "update where literal count should succeed") != 0 ||
	    expect_true(where_count == 2U, "update where literal count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "update where literal should succeed") != 0 ||
	    expect_true(where_literal.literal.kind == SQLPARSER_LITERAL_KIND_INTEGER, "update where literal should be integer") != 0 ||
	    expect_true(where_literal.literal.integer_value == 1LL, "update where literal should be 1") != 0 ||
	    expect_true(strcmp(where_literal.column_name, "id") == 0, "update where column should be id") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "update assignment should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "name") == 0, "first assignment should be name") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_LITERAL, "first assignment should be literal") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "bob") == 0, "first assignment should be bob") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "carol";
	rc = sqlparser_update_set_assignment_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "update assignment mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	where_replacement.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	where_replacement.integer_value = 2LL;
	rc = sqlparser_statement_where_set_literal(handle, 0U, 0U, &where_replacement, &error);
	if (expect_status_ok(rc, &error, "update where mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = 'carol'") != NULL, "deparsed update should contain carol") != 0 ||
	    expect_true(strstr(deparsed_sql, "id = 2") != NULL, "deparsed update should contain id = 2") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "update view export should succeed") != 0 ||
	    expect_true(strstr(view_json, "carol") != NULL, "view JSON should contain carol") != 0 ||
	    expect_true(strstr(view_json, "\"operator\":\"=\"") != NULL, "view JSON should contain where operator") != 0 ||
	    expect_true(strstr(view_json, "\"sql\":\"2\"") != NULL, "view JSON should contain updated where value") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_assignment_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	sqlparser_selector_t selector;
	char *assignment_sql;
	char *selector_sql;
	char *deparsed_sql;
	int rc;

	sql = "UPDATE public.users SET name = upper(name), updated_at = DEFAULT WHERE id = 1";
	handle = NULL;
	assignment_sql = NULL;
	selector_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "expression update parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "expression assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "name") == 0, "expression assignment column should be name") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_EXPRESSION, "expression assignment should be expression") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment_sql(handle, 0U, 0U, &assignment_sql, &error);
	if (expect_status_ok(rc, &error, "expression assignment SQL fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment_sql, "upper(name)") == 0, "expression assignment SQL should be upper(name)") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 1U, &assignment, &error);
	if (expect_status_ok(rc, &error, "default assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "updated_at") == 0, "default assignment column should be updated_at") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_DEFAULT, "default assignment should be DEFAULT") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = 0U;
	selector.item_index = 1U;
	rc = sqlparser_selector_update_assignment_sql(handle, &selector, &selector_sql, &error);
	if (expect_status_ok(rc, &error, "selector assignment SQL fetch should succeed") != 0 ||
	    expect_true(strcmp(selector_sql, "DEFAULT") == 0, "selector assignment SQL should be DEFAULT") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_sql(handle, 0U, 0U, "lower(name)", &error);
	if (expect_status_ok(rc, &error, "expression assignment SQL mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_selector_set_update_assignment_sql(
		handle,
		&selector,
		"clock_timestamp()",
		&error);
	if (expect_status_ok(rc, &error, "selector assignment SQL mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "expression update deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = lower(name)") != NULL, "deparsed update should contain lower(name)") != 0 ||
	    expect_true(strstr(deparsed_sql, "updated_at = clock_timestamp()") != NULL, "deparsed update should contain clock_timestamp()") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_sql);
	sqlparser_string_free(assignment_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_cell_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	char *cell_sql;
	char *selector_sql;
	char *deparsed_sql;
	int rc;

	sql =
		"INSERT INTO public.users (id, name, created_at, score) "
		"VALUES (1, upper('bob'), DEFAULT, 10), (2, 'carol', DEFAULT, 20)";
	handle = NULL;
	cell_sql = NULL;
	selector_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "expression insert parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_cell_sql(handle, 0U, 0U, 1U, &cell_sql, &error);
	if (expect_status_ok(rc, &error, "expression insert cell SQL fetch should succeed") != 0 ||
	    expect_true(strstr(cell_sql, "upper(") != NULL, "expression insert cell SQL should contain upper(") != 0 ||
	    expect_true(strstr(cell_sql, "'bob'") != NULL, "expression insert cell SQL should contain 'bob'") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = 0U;
	selector.row_index = 1U;
	selector.column_index = 2U;
	rc = sqlparser_selector_insert_cell_sql(handle, &selector, &selector_sql, &error);
	if (expect_status_ok(rc, &error, "selector insert cell SQL fetch should succeed") != 0 ||
	    expect_true(strcmp(selector_sql, "DEFAULT") == 0, "selector insert cell SQL should be DEFAULT") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_set_cell_sql(handle, 0U, 0U, 1U, "lower('BOB')", &error);
	if (expect_status_ok(rc, &error, "expression insert cell mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_selector_set_insert_cell_sql(handle, &selector, "now()", &error);
	if (expect_status_ok(rc, &error, "selector insert cell mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "expression insert deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "lower(") != NULL, "deparsed insert should contain lower(") != 0 ||
	    expect_true(strstr(deparsed_sql, "'BOB'") != NULL, "deparsed insert should contain 'BOB'") != 0 ||
	    expect_true(strstr(deparsed_sql, "now()") != NULL, "deparsed insert should contain now()") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_sql);
	sqlparser_string_free(cell_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_delete_where_literal_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	size_t where_count;
	int rc;

	sql = "DELETE FROM public.users WHERE status IN ('inactive', 'blocked') AND age > 30";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "delete parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "delete where literal count should succeed") != 0 ||
	    expect_true(where_count == 3U, "delete where literal count should be 3") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "delete where literal should succeed") != 0 ||
	    expect_true(strcmp(where_literal.column_name, "status") == 0, "delete first where column should be status") != 0 ||
	    expect_true(strcmp(where_literal.operator_name, "IN") == 0, "delete first where operator should be IN") != 0 ||
	    expect_true(strcmp(where_literal.literal.string_value, "inactive") == 0, "delete first where literal should be inactive") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "archived";
	rc = sqlparser_statement_where_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "delete where mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "delete deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'archived'") != NULL, "deparsed delete should contain archived") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generic_relation_and_literal_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	sqlparser_relation_view_t relation;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	size_t relation_count;
	size_t literal_count;
	size_t name_count;
	size_t order_no_index;
	int rc;

	sql = "SELECT u.id, o.order_no FROM public.users u "
	      "JOIN public.orders o ON u.id = o.user_id "
	      "WHERE o.status = 'paid'";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&name, 0, sizeof(name));
	memset(&relation, 0, sizeof(relation));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));
	order_no_index = 0U;

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "generic select parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_relation_count(handle, 0U, &relation_count, &error);
	if (expect_status_ok(rc, &error, "generic relation count should succeed") != 0 ||
	    expect_true(relation_count == 2U, "generic relation count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_relation(handle, 0U, 1U, &relation, &error);
	if (expect_status_ok(rc, &error, "generic relation fetch should succeed") != 0 ||
	    expect_true(strcmp(relation.table_name, "orders") == 0, "second relation should be orders") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_literal_count(handle, 0U, &literal_count, &error);
	if (expect_status_ok(rc, &error, "generic literal count should succeed") != 0 ||
	    expect_true(literal_count == 1U, "generic literal count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_literal(handle, 0U, 0U, &literal, &error);
	if (expect_status_ok(rc, &error, "generic literal fetch should succeed") != 0 ||
	    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "generic literal should be string") != 0 ||
	    expect_true(strcmp(literal.string_value, "paid") == 0, "generic literal should be paid") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_name_count(handle, 0U, &name_count, &error);
	if (expect_status_ok(rc, &error, "generic name count should succeed") != 0 ||
	    expect_true(name_count > 0U, "generic name count should be greater than 0") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (find_name_index(handle, 0U, "ColumnRef", "fields", "order_no", &order_no_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_name(handle, 0U, order_no_index, &name, &error);
	if (expect_status_ok(rc, &error, "generic name fetch should succeed") != 0 ||
	    expect_true(strcmp(name.owner_type, "ColumnRef") == 0, "generic name owner should be ColumnRef") != 0 ||
	    expect_true(strcmp(name.field_name, "fields") == 0, "generic name field should be fields") != 0 ||
	    expect_true(strcmp(name.value, "order_no") == 0, "generic name value should be order_no") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_relation_name(handle, 0U, 1U, "public", "archived_orders", &error);
	if (expect_status_ok(rc, &error, "generic relation rename should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "closed";
	rc = sqlparser_statement_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "generic literal mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_name(handle, 0U, order_no_index, "order_code", &error);
	if (expect_status_ok(rc, &error, "generic name mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "generic deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "archived_orders") != NULL, "generic deparse should contain renamed relation") != 0 ||
	    expect_true(strstr(deparsed_sql, "order_code") != NULL, "generic deparse should contain renamed column") != 0 ||
	    expect_true(strstr(deparsed_sql, "'closed'") != NULL, "generic deparse should contain rewritten literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generic_name_api_on_ddl(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	char *deparsed_sql;
	size_t name_count;
	size_t view_name_index;
	int rc;

	sql = "DROP VIEW IF EXISTS public.v_orders";
	handle = NULL;
	deparsed_sql = NULL;
	view_name_index = 0U;
	memset(&error, 0, sizeof(error));
	memset(&name, 0, sizeof(name));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "ddl name parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_name_count(handle, 0U, &name_count, &error);
	if (expect_status_ok(rc, &error, "ddl name count should succeed") != 0 ||
	    expect_true(name_count == 2U, "ddl name count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (find_name_index(handle, 0U, "DropStmt", "objects", "v_orders", &view_name_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_name(handle, 0U, view_name_index, &name, &error);
	if (expect_status_ok(rc, &error, "ddl name fetch should succeed") != 0 ||
	    expect_true(strcmp(name.owner_type, "DropStmt") == 0, "ddl name owner should be DropStmt") != 0 ||
	    expect_true(strcmp(name.field_name, "objects") == 0, "ddl name field should be objects") != 0 ||
	    expect_true(strcmp(name.value, "v_orders") == 0, "ddl name value should be v_orders") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_name(handle, 0U, view_name_index, "v_orders_archive", &error);
	if (expect_status_ok(rc, &error, "ddl name mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "ddl name deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "v_orders_archive") != NULL, "ddl deparse should contain renamed view") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_selector_parse_and_format(void)
{
	sqlparser_selector_t selector;
	sqlparser_error_t error;
	char *selector_text;
	int rc;

	memset(&selector, 0, sizeof(selector));
	memset(&error, 0, sizeof(error));
	selector_text = NULL;

	rc = sqlparser_selector_parse("stmt[0].where_literal[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for where literal should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_WHERE_LITERAL, "selector kind should be where_literal") != 0 ||
	    expect_true(selector.statement_index == 0U, "selector statement index should be 0") != 0 ||
	    expect_true(selector.item_index == 1U, "selector item index should be 1") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].where_literal[1]") == 0, "selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[3].insert_cell[2][4]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for insert cell should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL, "selector kind should be insert_cell") != 0 ||
	    expect_true(selector.statement_index == 3U, "selector statement index should be 3") != 0 ||
	    expect_true(selector.row_index == 2U, "selector row index should be 2") != 0 ||
	    expect_true(selector.column_index == 4U, "selector column index should be 4") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "insert cell selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[3].insert_cell[2][4]") == 0, "insert cell selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[1].value[7]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for value should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_VALUE, "selector kind should be value") != 0 ||
	    expect_true(selector.statement_index == 1U, "selector statement index should be 1") != 0 ||
	    expect_true(selector.item_index == 7U, "selector item index should be 7") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "value selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[1].value[7]") == 0, "value selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[2].insert_columns", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for insert columns should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS, "selector kind should be insert_columns") != 0 ||
	    expect_true(selector.statement_index == 2U, "selector statement index should be 2") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "insert columns selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[2].insert_columns") == 0, "insert columns selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[2].insert_row[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for insert row should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_ROW, "selector kind should be insert_row") != 0 ||
	    expect_true(selector.statement_index == 2U, "selector statement index should be 2") != 0 ||
	    expect_true(selector.row_index == 1U, "selector row index should be 1") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "insert row selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[2].insert_row[1]") == 0, "insert row selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[0].select_targets[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for select targets should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGETS, "selector kind should be select_targets") != 0 ||
	    expect_true(selector.statement_index == 0U, "selector statement index should be 0") != 0 ||
	    expect_true(selector.item_index == 1U, "selector target list index should be 1") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "select targets selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].select_targets[1]") == 0, "select targets selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[0].select_target[1][2]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for select target should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGET, "selector kind should be select_target") != 0 ||
	    expect_true(selector.statement_index == 0U, "selector statement index should be 0") != 0 ||
	    expect_true(selector.item_index == 1U, "selector target list index should be 1") != 0 ||
	    expect_true(selector.column_index == 2U, "selector select target index should be 2") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "select target selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].select_target[1][2]") == 0, "select target selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	return 0;
}

static int test_where_clause_sql_rewrite_api(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	char *where_sql;
	char *clause_sql;
	char *deparsed_sql;
	char *view_json;
	size_t clause_count;
	size_t where_count;
	int rc;

	handle = NULL;
	where_sql = NULL;
	clause_sql = NULL;
	deparsed_sql = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	memset(patches, 0, sizeof(patches));

	rc = sqlparser_parse("SELECT id, name FROM public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "select without where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "select where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "select should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_where_sql(handle, 0U, 0U, &where_sql, &error);
	if (expect_status_ok(rc, &error, "empty select where SQL should succeed") != 0 ||
	    expect_true(where_sql == NULL, "empty select where SQL should be NULL") != 0) {
		sqlparser_string_free(where_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id = 1", &error);
	if (expect_status_ok(rc, &error, "select where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"status = 'active'",
		&error);
	if (expect_status_ok(rc, &error, "select where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "select where append should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_where_sql(handle, 0U, 0U, &where_sql, &error);
	if (expect_status_ok(rc, &error, "rewritten select where SQL should succeed") != 0 ||
	    expect_true(where_sql != NULL && strstr(where_sql, "id = 1") != NULL, "select where should contain id condition") != 0 ||
	    expect_true(strstr(where_sql, "status = 'active'") != NULL, "select where should contain appended condition") != 0) {
		sqlparser_string_free(where_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(where_sql);
	where_sql = NULL;
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "where view JSON should export") != 0 ||
	    expect_true(strstr(view_json, "\"clauses\":[") != NULL,
	                "view JSON should expose clause array") != 0 ||
	    expect_true(strstr(view_json, "\"kind\":\"select_list\"") != NULL,
	                "view JSON should expose select_list clause") != 0 ||
	    expect_true(strstr(view_json, "\"kind\":\"where\",\"selector\":\"stmt[0].clause[1]\"") != NULL,
	                "view JSON should expose where clause selector") != 0 ||
	    expect_true(strstr(view_json, "\"kind\":\"order_by\"") != NULL,
	                "view JSON should expose order_by clause") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;
	rc = sqlparser_statement_clause_count(handle, 0U, &clause_count, &error);
	if (expect_status_ok(rc, &error, "select clause count should succeed") != 0 ||
	    expect_true(clause_count == 3U, "select should expose select_list, where, and order_by clauses") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "select_list clause SQL should succeed") != 0 ||
	    expect_true(clause_sql != NULL && strstr(clause_sql, "id") != NULL && strstr(clause_sql, "name") != NULL,
	                "select_list clause should contain selected columns") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(clause_sql);
	clause_sql = NULL;
	rc = sqlparser_statement_clause_sql(handle, 0U, 2U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "empty order_by clause SQL should succeed") != 0 ||
	    expect_true(clause_sql == NULL, "empty order_by clause should be NULL") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_clause_sql(handle, 0U, 2U, "name DESC", &error);
	if (expect_status_ok(rc, &error, "order_by clause set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "order_by clause set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "order_by clause deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "ORDER BY") != NULL, "deparse should contain ORDER BY") != 0 ||
	    expect_true(strstr(deparsed_sql, "name DESC") != NULL, "deparse should contain order expression") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("UPDATE public.users SET name = 'bob'", &handle, &error);
	if (expect_status_ok(rc, &error, "update without where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_clause_count(handle, 0U, &clause_count, &error);
	if (expect_status_ok(rc, &error, "update clause count should succeed") != 0 ||
	    expect_true(clause_count == 2U, "update should expose set_list and where clauses") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "update set_list clause SQL should succeed") != 0 ||
	    expect_true(clause_sql != NULL && strstr(clause_sql, "name") != NULL && strstr(clause_sql, "'bob'") != NULL,
	                "update set_list clause should contain assignment") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(clause_sql);
	clause_sql = NULL;
	rc = sqlparser_statement_set_clause_sql(handle, 0U, 0U, "name = 'bob', age = 18", &error);
	if (expect_status_ok(rc, &error, "update set_list replacement should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "update set_list replacement should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_parse("stmt[0].clause[0]", &selector, &error);
	if (expect_status_ok(rc, &error, "set_list clause selector parse should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_CLAUSE, "set_list selector kind should be clause") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_parse("stmt[0].clause[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "clause selector parse should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_CLAUSE, "selector kind should be clause") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_append_clause_condition(
		handle,
		&selector,
		SQLPARSER_BOOL_OPERATOR_AND,
		"id = 10",
		&error);
	if (expect_status_ok(rc, &error, "update where append on empty slot should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_append_clause_condition(
		handle,
		&selector,
		SQLPARSER_BOOL_OPERATOR_OR,
		"external_id = 'u-10'",
		&error);
	if (expect_status_ok(rc, &error, "update where OR append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "update where append should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "age = 18") != NULL, "update deparse should contain added SET assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "WHERE") != NULL, "update deparse should contain WHERE") != 0 ||
	    expect_true(strstr(deparsed_sql, "external_id") != NULL, "update deparse should contain OR condition") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("DELETE FROM public.users WHERE status = 'inactive'", &handle, &error);
	if (expect_status_ok(rc, &error, "delete with where parse should succeed") != 0) {
		return 1;
	}
	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = "stmt[0].clause[0]";
	patches[0].sql = "deleted_at IS NULL";
	patches[1].op = SQLPARSER_PATCH_APPEND_CONDITION;
	patches[1].selector = "stmt[0].clause[0]";
	patches[1].bool_operator = SQLPARSER_BOOL_OPERATOR_AND;
	patches[1].sql = "status IN ('inactive', 'blocked')";
	patch_list.items = patches;
	patch_list.count = 2U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "delete where patch list should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "delete where patch should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "delete where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "deleted_at IS NULL") != NULL, "delete where should contain replacement") != 0 ||
	    expect_true(strstr(deparsed_sql, "blocked") != NULL, "delete where should contain appended condition") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("INSERT INTO public.archive_users SELECT id, name FROM public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "insert select without where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "insert select where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "insert select should expose one select where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active = TRUE", &error);
	if (expect_status_ok(rc, &error, "insert select where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "insert select where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("INSERT INTO public.users (id, name) VALUES (1, 'bob')", &handle, &error);
	if (expect_status_ok(rc, &error, "insert values parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "insert values where count should succeed") != 0 ||
	    expect_true(where_count == 0U, "insert values should not expose synthetic where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse(
		"INSERT INTO public.users (id, name) VALUES (1, 'bob') "
		"ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name",
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert on conflict parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "on conflict where count should succeed") != 0 ||
	    expect_true(where_count == 2U, "on conflict should expose infer and update where slots") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id > 0", &error);
	if (expect_status_ok(rc, &error, "on conflict infer where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 1U, "EXCLUDED.name IS NOT NULL", &error);
	if (expect_status_ok(rc, &error, "on conflict update where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "on conflict where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE VIEW public.v_users AS SELECT id FROM public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "create view parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "status = 'active'", &error);
	if (expect_status_ok(rc, &error, "create view select where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "create view where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE INDEX idx_users_name ON public.users (name)", &handle, &error);
	if (expect_status_ok(rc, &error, "create index parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active = TRUE", &error);
	if (expect_status_ok(rc, &error, "partial index where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "partial index where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("COPY public.users (id, name) FROM STDIN", &handle, &error);
	if (expect_status_ok(rc, &error, "copy from parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "copy from where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "copy from should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active IS TRUE", &error);
	if (expect_status_ok(rc, &error, "copy from where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "copy from where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("COPY public.users TO STDOUT", &handle, &error);
	if (expect_status_ok(rc, &error, "copy to parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "copy to where count should succeed") != 0 ||
	    expect_true(where_count == 0U, "copy to should not expose unsupported where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE RULE users_update_rule AS ON UPDATE TO public.users DO ALSO SELECT 1", &handle, &error);
	if (expect_status_ok(rc, &error, "create rule parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "create rule where count should succeed") != 0 ||
	    expect_true(where_count == 2U, "create rule should expose rule and action where slots") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "NEW.active IS TRUE", &error);
	if (expect_status_ok(rc, &error, "create rule condition where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 1U, "1 = 1", &error);
	if (expect_status_ok(rc, &error, "create rule action where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "create rule where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE PUBLICATION pub_users FOR TABLE public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "create publication parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "create publication where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "create publication should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active IS TRUE", &error);
	if (expect_status_ok(rc, &error, "create publication where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "create publication where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse(
		"CREATE TABLE public.room_booking ("
		"room_id integer, "
		"during tsrange, "
		"EXCLUDE USING gist (room_id WITH =, during WITH &&)"
		")",
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "create table exclusion constraint parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "exclusion constraint where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "exclusion constraint should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "room_id IS NOT NULL", &error);
	if (expect_status_ok(rc, &error, "exclusion constraint where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "exclusion constraint where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options("SELECT * FROM `users`", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "`id` = 1", &error);
	if (expect_status_ok(rc, &error, "mysql where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"`name` LIKE 'b%'",
		&error);
	if (expect_status_ok(rc, &error, "mysql where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "mysql where rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options("SELECT * FROM users", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "oracle where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id = :id", &error);
	if (expect_status_ok(rc, &error, "oracle where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"name = q'[Bob's order]'",
		&error);
	if (expect_status_ok(rc, &error, "oracle where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "oracle where rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "oracle where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, ":id") != NULL, "oracle where should restore bind name") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "oracle where should not expose internal bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options("DELETE FROM [dbo].[users]", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "sqlserver where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "[id] = @id", &error);
	if (expect_status_ok(rc, &error, "sqlserver where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"[name] = N'bob'",
		&error);
	if (expect_status_ok(rc, &error, "sqlserver where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "sqlserver where rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sqlserver where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "sqlserver where should not expose internal bind markers") != 0 ||
	    expect_true(strstr(deparsed_sql, "bob") != NULL, "sqlserver where should contain string literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}


static int test_generic_literal_api_on_ddl(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	size_t literal_count;
	int rc;

	sql = "CREATE VIEW public.v_users AS SELECT * FROM public.users WHERE status = 'active'";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "ddl parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_literal_count(handle, 0U, &literal_count, &error);
	if (expect_status_ok(rc, &error, "ddl literal count should succeed") != 0 ||
	    expect_true(literal_count == 1U, "ddl literal count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_literal(handle, 0U, 0U, &literal, &error);
	if (expect_status_ok(rc, &error, "ddl literal fetch should succeed") != 0 ||
	    expect_true(strcmp(literal.string_value, "active") == 0, "ddl literal should be active") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "archived";
	rc = sqlparser_statement_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "ddl literal mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "ddl deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'archived'") != NULL, "ddl deparse should contain archived") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_from_returning_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	sqlparser_relation_view_t relation;
	char *assignment_sql;
	char *deparsed_sql;
	size_t relation_count;
	int rc;

	sql =
		"UPDATE public.users AS u "
		"SET name = src.name, updated_at = clock_timestamp() "
		"FROM public.user_stage AS src "
		"WHERE u.id = src.id "
		"RETURNING u.id, u.name";
	handle = NULL;
	assignment_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&relation, 0, sizeof(relation));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "update-from parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_target_relation(handle, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "update-from target relation should succeed") != 0 ||
	    expect_true(strcmp(relation.schema_name, "public") == 0, "update-from target schema should be public") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "update-from target table should be users") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_relation_count(handle, 0U, &relation_count, &error);
	if (expect_status_ok(rc, &error, "update-from relation count should succeed") != 0 ||
	    expect_true(relation_count >= 2U, "update-from should expose both target and source relations") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "update-from assignment should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "name") == 0, "update-from first assignment should be name") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_EXPRESSION, "update-from assignment should be expression") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment_sql(handle, 0U, 0U, &assignment_sql, &error);
	if (expect_status_ok(rc, &error, "update-from assignment SQL should succeed") != 0 ||
	    expect_true(strcmp(assignment_sql, "src.name") == 0, "update-from assignment SQL should be src.name") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_sql(handle, 0U, 0U, "src.display_name", &error);
	if (expect_status_ok(rc, &error, "update-from assignment mutation should succeed") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update-from deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "src.display_name") != NULL, "update-from deparse should contain src.display_name") != 0 ||
	    expect_true(strstr(deparsed_sql, "RETURNING u.id, u.name") != NULL, "update-from deparse should preserve returning") != 0 ||
	    expect_true(strstr(deparsed_sql, "FROM public.user_stage src") != NULL, "update-from deparse should preserve source relation") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(assignment_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_on_conflict_returning_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_insert_source_kind_t source_kind;
	char *cell_sql;
	char *deparsed_sql;
	size_t row_count;
	int rc;

	sql =
		"INSERT INTO public.users (id, name, updated_at) "
		"VALUES (1, 'alice', DEFAULT) "
		"ON CONFLICT (id) DO UPDATE SET name = excluded.name, updated_at = now() "
		"RETURNING id, name";
	handle = NULL;
	cell_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_VALUES, "insert-on-conflict should be VALUES source") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict row count should succeed") != 0 ||
	    expect_true(row_count == 1U, "insert-on-conflict should expose one VALUES row") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_sql(handle, 0U, 0U, 2U, &cell_sql, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict cell SQL should succeed") != 0 ||
	    expect_true(strcmp(cell_sql, "DEFAULT") == 0, "insert-on-conflict third value should be DEFAULT") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_set_cell_sql(handle, 0U, 0U, 2U, "clock_timestamp()", &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict cell mutation should succeed") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "ON CONFLICT (id) DO UPDATE SET") != NULL, "insert-on-conflict deparse should preserve on conflict") != 0 ||
	    expect_true(strstr(deparsed_sql, "clock_timestamp()") != NULL, "insert-on-conflict deparse should contain clock_timestamp()") != 0 ||
	    expect_true(strstr(deparsed_sql, "RETURNING id, name") != NULL, "insert-on-conflict deparse should preserve returning") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(cell_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_merge_statement_walk(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_statement_kind_t kind;
	const char *node_name;
	char *deparsed_sql;
	int rc;

	sql =
		"MERGE INTO public.target_table AS t "
		"USING public.source_table AS s "
		"ON t.id = s.id "
		"WHEN MATCHED THEN UPDATE SET name = s.name "
		"WHEN NOT MATCHED THEN INSERT (id, name) VALUES (s.id, s.name)";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "merge parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 0U, &kind, &error);
	if (expect_status_ok(rc, &error, "merge kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_MERGE, "statement should be MERGE") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_node_name(handle, 0U, &node_name, &error);
	if (expect_status_ok(rc, &error, "merge node name should succeed") != 0 ||
	    expect_true(strcmp(node_name, "MergeStmt") == 0, "statement node should be MergeStmt") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "merge deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "MERGE INTO public.target_table t") != NULL, "merge deparse should preserve target relation") != 0 ||
	    expect_true(strstr(deparsed_sql, "WHEN NOT MATCHED THEN INSERT") != NULL, "merge deparse should preserve insert branch") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_resource_limits(void)
{
	sqlparser_limits_t limits;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *json_text;
	int rc;

	handle = NULL;
	json_text = NULL;
	memset(&error, 0, sizeof(error));
	memset(&limits, 0, sizeof(limits));

	sqlparser_limits_default(&limits);
	if (expect_true(limits.struct_size == sizeof(limits), "default limits should expose struct size") != 0 ||
	    expect_true(limits.max_sql_bytes > 0U, "default SQL byte limit should be non-zero") != 0 ||
	    expect_true(limits.max_output_bytes > 0U, "default output byte limit should be non-zero") != 0 ||
	    expect_true(limits.max_statement_count > 0U, "default statement count limit should be non-zero") != 0) {
		return 1;
	}

	limits.max_sql_bytes = 8U;
	rc = sqlparser_parse_with_limits("SELECT 123456789", &limits, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL byte limit should reject large input") != 0 ||
	    expect_true(handle == NULL, "failed limited parse should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_limits_default(&limits);
	limits.max_statement_count = 1U;
	rc = sqlparser_parse_with_limits("SELECT 1; SELECT 2", &limits, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT, "statement count limit should reject multi statement") != 0 ||
	    expect_true(handle == NULL, "failed statement limit parse should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_limits_default(&limits);
	limits.max_output_bytes = 16U;
	rc = sqlparser_parse_with_limits("SELECT 1", &limits, &handle, &error);
	if (expect_status_ok(rc, &error, "limited output parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 0, &json_text, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT, "output byte limit should reject view JSON") != 0 ||
	    expect_true(json_text == NULL, "failed output export should not return JSON") != 0) {
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_mysql_dialect_select_rewrite(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_relation_view_t relation;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	int rc;

	sql = "SELECT `u`.`id`, \"hello\" AS `label` FROM `users` AS `u` "
	      "WHERE `u`.`id` = 1 LIMIT 5, 10";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&relation, 0, sizeof(relation));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&replacement, 0, sizeof(replacement));

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql select parse should succeed") != 0) {
		return 1;
	}

	if (expect_true(sqlparser_handle_dialect(handle) == SQLPARSER_DIALECT_MYSQL, "handle dialect should be mysql") != 0 ||
	    expect_true(strcmp(sqlparser_original_sql(handle), sql) == 0, "original SQL should be preserved") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_relation(handle, 0U, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "mysql select relation should succeed") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "mysql relation should be users") != 0 ||
	    expect_true(strcmp(relation.alias_name, "u") == 0, "mysql alias should be u") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "mysql where literal should succeed") != 0 ||
	    expect_true(where_literal.literal.kind == SQLPARSER_LITERAL_KIND_INTEGER, "mysql where literal should be integer") != 0 ||
	    expect_true(where_literal.literal.integer_value == 1, "mysql where literal should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	replacement.integer_value = 2;
	rc = sqlparser_statement_where_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "mysql where rewrite should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql select deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "LIMIT 5, 10") != NULL, "mysql deparse should use offset,count LIMIT") != 0 ||
	    expect_true(strstr(deparsed_sql, "OFFSET") == NULL, "mysql deparse should not expose OFFSET for comma LIMIT") != 0 ||
	    expect_true(strstr(deparsed_sql, "2") != NULL, "mysql deparse should contain rewritten literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_mysql_dialect_insert_rewrite(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	const char *column_name;
	size_t row_count;
	int rc;

	sql = "INSERT INTO `users` (`id`, `name`) VALUES (1, \"bob\"), (2, 'alice')";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql insert parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "mysql insert source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_VALUES, "mysql insert source should be VALUES") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_column_name(handle, 0U, 1U, &column_name, &error);
	if (expect_status_ok(rc, &error, "mysql insert column name should succeed") != 0 ||
	    expect_true(strcmp(column_name, "name") == 0, "mysql insert column should be name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "mysql insert row count should succeed") != 0 ||
	    expect_true(row_count == 2U, "mysql insert row count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_literal(handle, 0U, 0U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "mysql insert cell literal should succeed") != 0 ||
	    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "mysql insert cell should be string") != 0 ||
	    expect_true(strcmp(literal.string_value, "bob") == 0, "mysql double string should normalize to string literal") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "carol";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 0U, 1U, &replacement, &error);
	if (expect_status_ok(rc, &error, "mysql insert rewrite should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql insert deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "carol") != NULL, "mysql insert deparse should contain rewritten value") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_dialect_insert_column_patch_with_question_param(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
	} cases[] = {
		{SQLPARSER_DIALECT_MYSQL, "mysql"},
		{SQLPARSER_DIALECT_ORACLE, "oracle"},
		{SQLPARSER_DIALECT_SQLSERVER, "sqlserver"},
		{SQLPARSER_DIALECT_DAMENG, "dameng"}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t object;
	sqlparser_row_view_t row;
	sqlparser_cell_view_t cell;
	char *deparsed_sql;
	char *cell_sql;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		deparsed_sql = NULL;
		cell_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(
			"INSERT INTO users (username, email, age) VALUES (?, ?, ?), (?, ?, ?)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "dialect question insert parse should succeed") != 0) {
			return 1;
		}

		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
		patch.selector = "stmt[0].insert_columns";
		patch.index = 3U;
		patch.name = "created_at";
		patch.default_sql = "?";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "insert column question patch should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		if (expect_deparse_reparse_ok(handle, "insert column question patch should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "insert column question deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, "created_at") != NULL, "patched insert should contain new column") != 0 ||
		    expect_true(strstr(deparsed_sql, "$") == NULL, "patched insert should not expose internal params") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		deparsed_sql = NULL;

		rc = sqlparser_get_view(handle, &view, &error);
		if (expect_status_ok(rc, &error, "patched insert view should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_view_statement_at(&view, 0U, &statement, &error);
		if (expect_status_ok(rc, &error, "patched insert statement should be available") != 0 ||
		    expect_true(statement.object_count == 1U, "patched insert should expose one object") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_object_at(&statement, 0U, &object, &error);
		if (expect_status_ok(rc, &error, "patched insert object should be available") != 0 ||
		    expect_true(object.column_count == 4U, "patched insert should expose four columns") != 0 ||
		    expect_true(object.row_count == 2U, "patched insert should keep two rows") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_object_row_at(&object, 1U, &row, &error);
		if (expect_status_ok(rc, &error, "patched insert row should be available") != 0 ||
		    expect_true(row.cell_count == 4U, "patched insert row should expose four cells") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_row_cell_at(&row, 3U, &cell, &error);
		if (expect_status_ok(rc, &error, "patched insert cell should be available") != 0 ||
		    expect_true(strcmp(cell.column_name, "created_at") == 0, "patched cell should belong to created_at") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_cell_sql(&cell, &cell_sql, &error);
		if (expect_status_ok(rc, &error, "patched insert cell SQL should succeed") != 0 ||
		    expect_true(strcmp(cell_sql, "?") == 0, "patched insert cell should expose question param") != 0) {
			sqlparser_string_free(cell_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_mysql_dialect_unsupported(void)
{
	const char *sqls[] = {
		"INSERT IGNORE INTO `users` (`id`) VALUES (1)",
		"INSERT INTO users(id) VALUES(1) ON DUPLICATE KEY UPDATE id = 2",
		"REPLACE INTO users(id) VALUES(1)",
		"CREATE TABLE `users` (`id` INT AUTO_INCREMENT)"
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	size_t index;
	int rc;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;

	for (index = 0U; index < sizeof(sqls) / sizeof(sqls[0]); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(sqls[index], &options, &handle, &error);
		if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED, "unsupported mysql syntax should return UNSUPPORTED") != 0 ||
		    expect_true(handle == NULL, "unsupported mysql syntax should not return handle") != 0 ||
		    expect_true(error.message[0] != '\0', "unsupported mysql syntax should provide error message") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}

	return 0;
}

static int test_sqlserver_dialect_option(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed_sql;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		"SELECT TOP (3) [id], [name] FROM [dbo].[users] WHERE [id] = @id",
		&options,
		&handle,
		&error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "sqlserver dialect should parse supported input") != 0 ||
	    expect_true(handle != NULL, "supported sqlserver parse should return handle") != 0 ||
	    expect_true(sqlparser_handle_dialect(handle) == SQLPARSER_DIALECT_SQLSERVER, "handle dialect should be sqlserver") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "sqlserver deparse should succeed") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "TOP (3)") != NULL, "sqlserver deparse should restore TOP") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "@id") != NULL, "sqlserver deparse should restore @ parameter") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "$1") == NULL, "sqlserver deparse should not expose internal parameter") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sql_view_json_and_patch_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *view_json;
	char *deparsed_sql;
	json_error_t json_error;
	json_t *root;
	json_t *statements;
	json_t *objects;
	json_t *rows;
	json_t *cells;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t object;
	sqlparser_column_view_t column;
	sqlparser_row_view_t row;
	sqlparser_cell_view_t cell;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	char *cell_sql;
	int rc;

	sql = "INSERT INTO public.users (id, name) VALUES (1, 'bob'), (2, 'alice')";
	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	cell_sql = NULL;
	root = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "sql view parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 1, &view_json, &error);
	if (expect_status_ok(rc, &error, "sql view export should succeed") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"statements\"") != NULL, "sql view should contain statements") != 0 ||
	    expect_true(strstr(view_json, "\"source_sql\"") == NULL, "sql view should not contain source_sql") != 0 ||
	    expect_true(strstr(view_json, "\"literal\"") == NULL, "sql view should not contain literal typing") != 0 ||
	    expect_true(strstr(view_json, "\"values\":") == NULL, "sql view should not contain unused values arrays") != 0 ||
	    expect_true(strstr(view_json, "stmt[0].insert_cell[1][1]") != NULL, "sql view should contain insert cell selector") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	root = json_loads(view_json, 0, &json_error);
	if (expect_true(root != NULL, "sql view JSON should decode") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	statements = json_object_get(root, "statements");
	objects = json_object_get(json_array_get(statements, 0), "objects");
	rows = json_object_get(json_array_get(objects, 0), "rows");
	cells = json_object_get(json_array_get(rows, 1), "cells");
	if (expect_true(json_array_size(statements) == 1U, "sql view statement count should be 1") != 0 ||
	    expect_true(strcmp(json_string_value(json_object_get(json_array_get(objects, 0), "table")), "users") == 0, "sql view table should be users") != 0 ||
	    expect_true(json_array_size(rows) == 2U, "sql view row count should be 2") != 0 ||
	    expect_true(strcmp(json_string_value(json_object_get(json_array_get(cells, 1), "sql")), "'alice'") == 0, "sql view cell should expose SQL text") != 0) {
		json_decref(root);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	json_decref(root);
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_get_view(handle, &view, &error);
	if (expect_status_ok(rc, &error, "sql view should be available") != 0 ||
	    expect_true(view.statement_count == 1U, "sql view statement count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_view_statement_at(&view, 0U, &statement, &error);
	if (expect_status_ok(rc, &error, "sql view statement should be available") != 0 ||
	    expect_true(statement.keyword_count >= 3U, "insert statement should expose keywords") != 0 ||
	    expect_true(statement.object_count == 1U, "insert statement should expose target object") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_object_at(&statement, 0U, &object, &error);
	if (expect_status_ok(rc, &error, "sql view object should be available") != 0 ||
	    expect_true(strcmp(object.table_name, "users") == 0, "sql view object table should be users") != 0 ||
	    expect_true(object.column_count == 2U, "insert object should expose two columns") != 0 ||
	    expect_true(object.row_count == 2U, "insert object should expose two rows") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_object_column_at(&object, 1U, &column, &error);
	if (expect_status_ok(rc, &error, "sql view column should be available") != 0 ||
	    expect_true(strcmp(column.name, "name") == 0, "second insert column should be name") != 0 ||
	    expect_true(column.has_selector != 0, "insert column should expose selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_object_row_at(&object, 1U, &row, &error);
	if (expect_status_ok(rc, &error, "sql view row should be available") != 0 ||
	    expect_true(row.cell_count == 2U, "insert row should expose two cells") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_row_cell_at(&row, 1U, &cell, &error);
	if (expect_status_ok(rc, &error, "sql view cell should be available") != 0 ||
	    expect_true(strcmp(cell.column_name, "name") == 0, "cell should carry column name") != 0 ||
	    expect_true(cell.has_selector != 0, "cell should expose selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_cell_sql(&cell, &cell_sql, &error);
	if (expect_status_ok(rc, &error, "sql view cell SQL should be available") != 0 ||
	    expect_true(strcmp(cell_sql, "'alice'") == 0, "cell SQL should preserve original value SQL") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(cell_sql);
	cell_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = "stmt[0].insert_cell[1][1]";
	patches[0].sql = "'carol'";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].insert_columns";
	patches[1].index = 2U;
	patches[1].name = "age";
	patches[1].default_sql = "18";
	patch_list.items = patches;
	patch_list.count = 2U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "sql view patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sql view patched deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'carol'") != NULL, "patched SQL should contain carol") != 0 ||
	    expect_true(strstr(deparsed_sql, "age") != NULL, "patched SQL should contain added column") != 0 ||
	    expect_true(strstr(deparsed_sql, "18") != NULL, "patched SQL should contain default value") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_DELETE_ROW;
	patches[0].selector = "stmt[0].insert_row[0]";
	patch_list.items = patches;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "sql view delete row patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sql view delete row deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'bob'") == NULL, "delete row should remove first row") != 0 ||
	    expect_true(strstr(deparsed_sql, "'carol'") != NULL, "delete row should keep remaining row") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_DELETE_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 2U;
	patch_list.items = patches;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "sql view delete column patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sql view delete column deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "age") == NULL, "delete column should remove added column") != 0 ||
	    expect_true(strstr(deparsed_sql, "18") == NULL, "delete column should remove added value") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int json_string_is(json_t *object, const char *key, const char *expected)
{
	const char *value;

	if (!json_is_object(object) || key == NULL || expected == NULL) {
		return 0;
	}
	value = json_string_value(json_object_get(object, key));
	return value != NULL && strcmp(value, expected) == 0;
}

static int json_integer_is(json_t *object, const char *key, json_int_t expected)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return json_is_integer(value) && json_integer_value(value) == expected;
}

static int json_key_is_null(json_t *object, const char *key)
{
	return json_is_object(object) && json_is_null(json_object_get(object, key));
}

static int json_array_length_is(json_t *object, const char *key, size_t expected)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return json_is_array(value) && json_array_size(value) == expected;
}

static int json_target_path_entry_is(
	json_t *column,
	size_t index,
	const char *kind,
	const char *name,
	json_int_t arg_index)
{
	json_t *path;
	json_t *entry;

	if (!json_is_object(column) || kind == NULL) {
		return 0;
	}
	path = json_object_get(column, "target_path");
	if (!json_is_array(path) || index >= json_array_size(path)) {
		return 0;
	}
	entry = json_array_get(path, index);
	if (!json_string_is(entry, "kind", kind)) {
		return 0;
	}
	if (name != NULL) {
		if (!json_string_is(entry, "name", name)) {
			return 0;
		}
	} else if (!json_key_is_null(entry, "name")) {
		return 0;
	}
	return json_integer_is(entry, "arg_index", arg_index);
}

static int json_array_contains_string_value(json_t *array, const char *expected)
{
	json_t *item;
	size_t index;

	if (!json_is_array(array) || expected == NULL) {
		return 0;
	}
	json_array_foreach(array, index, item)
	{
		if (json_is_string(item) && strcmp(json_string_value(item), expected) == 0) {
			return 1;
		}
	}
	return 0;
}

static int expect_view_clause_ids(json_t *statement)
{
	json_t *clauses;
	json_t *clause;
	size_t index;

	clauses = json_object_get(statement, "clauses");
	if (expect_true(json_is_array(clauses), "view clauses should be an array") != 0) {
		return 1;
	}
	json_array_foreach(clauses, index, clause)
	{
		if (expect_true(json_integer_is(clause, "id", (json_int_t)index + 1), "view clause id should match clause_id numbering") != 0) {
			return 1;
		}
	}
	return 0;
}

static json_t *find_view_column_json(
	json_t *statement,
	const char *table_name,
	const char *column_name,
	const char *keyword,
	size_t skip)
{
	json_t *objects;
	json_t *object;
	size_t object_index;

	if (!json_is_object(statement) || column_name == NULL) {
		return NULL;
	}
	objects = json_object_get(statement, "objects");
	json_array_foreach(objects, object_index, object)
	{
		json_t *columns;
		json_t *column;
		const char *object_table;
		size_t column_index;

		object_table = json_string_value(json_object_get(object, "table"));
		if (table_name != NULL && (object_table == NULL || strcmp(object_table, table_name) != 0)) {
			continue;
		}
		columns = json_object_get(object, "columns");
		json_array_foreach(columns, column_index, column)
		{
			if (!json_string_is(column, "name", column_name)) {
				continue;
			}
			if (keyword != NULL && !json_string_is(column, "keyword", keyword)) {
				continue;
			}
			if (skip > 0U) {
				skip--;
				continue;
			}
			return column;
		}
	}
	return NULL;
}

static int view_json_parse_statement(
	sqlparser_dialect_t dialect,
	const char *sql,
	json_t **out_root,
	json_t **out_statement)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	json_error_t json_error;
	char *view_json;
	int rc;

	if (out_root == NULL || out_statement == NULL) {
		fprintf(stderr, "FAIL: output JSON arguments must not be NULL\n");
		return 1;
	}
	*out_root = NULL;
	*out_statement = NULL;
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "view semantics parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	sqlparser_handle_destroy(handle);
	if (expect_status_ok(rc, &error, "view semantics JSON export should succeed") != 0) {
		return 1;
	}
	*out_root = json_loads(view_json, 0, &json_error);
	sqlparser_string_free(view_json);
	if (expect_true(*out_root != NULL, "view semantics JSON should decode") != 0) {
		return 1;
	}
	*out_statement = json_array_get(json_object_get(*out_root, "statements"), 0U);
	if (expect_true(json_is_object(*out_statement), "view semantics statement should exist") != 0) {
		json_decref(*out_root);
		*out_root = NULL;
		*out_statement = NULL;
		return 1;
	}
	if (expect_view_clause_ids(*out_statement) != 0) {
		json_decref(*out_root);
		*out_root = NULL;
		*out_statement = NULL;
		return 1;
	}
	return 0;
}

static int expect_view_column_shape(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *table_name,
	const char *column_name,
	const char *keyword,
	const char *path_kind,
	const char *path_name,
	int has_path_arg_index,
	json_int_t path_arg_index)
{
	json_t *root;
	json_t *statement;
	json_t *column;
	int failed;

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(dialect, sql, &root, &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, table_name, column_name, keyword, 0U);
	failed = expect_true(column != NULL, "view column shape should exist") != 0;
	if (!failed && path_kind != NULL &&
	    strcmp(path_kind, "direct_column") != 0 &&
	    strcmp(path_kind, "qualified_star") != 0 &&
	    strcmp(path_kind, "star") != 0 &&
	    strcmp(path_kind, "not_output") != 0) {
		failed = expect_true(
			json_target_path_entry_is(column, 0U, path_kind, path_name, has_path_arg_index ? path_arg_index : 0),
			"view column target_path mismatch") != 0;
	} else if (!failed) {
		failed = expect_true(json_array_length_is(column, "target_path", 0U), "view column target_path should be empty") != 0;
	}
	json_decref(root);
	return failed ? 1 : 0;
}

static int test_sql_view_bind_fields(void)
{
	struct bind_case {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *set_bind;
		const char *where_bind;
		const char *set_value_sql;
	};
	static const struct bind_case cases[] = {
		{ SQLPARSER_DIALECT_POSTGRESQL, "UPDATE servers SET ip = $1 WHERE id = $2", "\"bind\":\"1\"", "\"bind\":\"2\"", "\"sql\":\"$1\"" },
		{ SQLPARSER_DIALECT_MYSQL, "UPDATE servers SET ip = ? WHERE id = ?", "\"bind\":\"1\"", "\"bind\":\"2\"", "\"sql\":\"?\"" },
		{ SQLPARSER_DIALECT_ORACLE, "UPDATE SERVERS SET IP = :aaa WHERE ID = :id", "\"bind\":\"aaa\"", "\"bind\":\"id\"", "\"sql\":\":aaa\"" },
		{ SQLPARSER_DIALECT_SQLSERVER, "UPDATE dbo.servers SET ip = @aaa WHERE id = @id", "\"bind\":\"aaa\"", "\"bind\":\"id\"", "\"sql\":\"@aaa\"" },
		{ SQLPARSER_DIALECT_DAMENG, "UPDATE servers SET ip = :aaa WHERE id = :id", "\"bind\":\"aaa\"", "\"bind\":\"id\"", "\"sql\":\":aaa\"" }
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_handle_t *handle;
		char *view_json;
		char *deparsed_sql;
		const char *replacement_set_list;
		int rc;

		handle = NULL;
		view_json = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "bind field parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "bind field view JSON should export") != 0 ||
		    expect_true(strstr(view_json, "\"kind\":\"set_list\"") != NULL, "UPDATE should expose set_list clause") != 0 ||
		    expect_true(strstr(view_json, cases[index].set_bind) != NULL, "SET bind should be exposed without marker") != 0 ||
		    expect_true(strstr(view_json, cases[index].where_bind) != NULL, "WHERE bind should be exposed without marker") != 0 ||
		    expect_true(strstr(view_json, "\"bind_selector\":\"stmt[0].assignment[0]\"") != NULL, "SET bind selector should target assignment") != 0 ||
		    expect_true(strstr(view_json, cases[index].set_value_sql) == NULL, "bind columns should not expose placeholder as value SQL") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(view_json);
		view_json = NULL;

		replacement_set_list = "ip = :aaa, host = :host";
		if (cases[index].dialect == SQLPARSER_DIALECT_POSTGRESQL) {
			replacement_set_list = "ip = $1, host = $3";
		} else if (cases[index].dialect == SQLPARSER_DIALECT_MYSQL) {
			replacement_set_list = "ip = ?, host = ?";
		} else if (cases[index].dialect == SQLPARSER_DIALECT_SQLSERVER) {
			replacement_set_list = "ip = @aaa, host = @host";
		}
		rc = sqlparser_statement_set_clause_sql(handle, 0U, 0U, replacement_set_list, &error);
		if (expect_status_ok(rc, &error, "bind SET list replacement should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "bind SET list replacement should produce parseable SQL") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "bind SET list deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, "host") != NULL, "bind SET list deparse should contain added assignment") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	{
		const char *sql =
			"SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD,WORKER, "
			"WEBSITE,MSDEPLOYPORT, \"UID\", PWD, KEY_ENCRYPTION, MODIFYTIME "
			"FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) "
			"WHERE RN > :startRow";
		sqlparser_handle_t *handle;
		char *view_json;
		int rc;

		handle = NULL;
		view_json = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_ORACLE;
		rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "oracle rownum pagination bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "oracle rownum pagination bind view JSON should export") != 0 ||
		    expect_true(strstr(view_json, "\"table\":\"servers\"") != NULL, "oracle rownum pagination should expose base table") != 0 ||
		    expect_true(strstr(view_json, "\"name\":\"UID\"") != NULL, "oracle rownum pagination should preserve quoted identifier case") != 0 ||
		    expect_true(strstr(view_json, "\"name\":\"*\"") != NULL, "oracle rownum pagination should expose scoped star") != 0 ||
		    expect_true(strstr(view_json, "\"bind\":\"endRow\"") != NULL, "oracle rownum pagination should expose endRow bind") != 0 ||
		    expect_true(strstr(view_json, "\"bind\":\"startRow\"") != NULL, "oracle rownum pagination should expose startRow bind") != 0 ||
		    expect_true(strstr(view_json, "\"sql\":\":endRow\"") == NULL, "oracle rownum bind should not be exposed as value SQL") != 0 ||
		    expect_true(strstr(view_json, "\"sql\":\":startRow\"") == NULL, "oracle rownum bind should not be exposed as value SQL") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_sql_view_column_semantics_json(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *table_name;
		const char *bind_name;
	} dialect_cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT name, UPPER(name), first_name || last_name FROM users WHERE id = 1 ORDER BY created_at",
			"users",
			NULL
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT name, UPPER(name), CONCAT(first_name, last_name) FROM users WHERE id = ? ORDER BY created_at",
			"users",
			"1"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT name, UPPER(name), first_name || last_name FROM KDES.USERS WHERE id = :id ORDER BY created_at",
			"users",
			"id"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [name], UPPER([name]), [first_name] + [last_name] FROM [dbo].[users] WHERE [id] = @id ORDER BY [created_at]",
			"users",
			"id"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT name, UPPER(name), first_name || last_name FROM KDES.USERS WHERE id = :id ORDER BY created_at",
			"users",
			"id"
		}
	};
	json_t *root;
	json_t *statement;
	json_t *column;
	json_t *clauses;
	json_t *on_clause;
	size_t index;

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT name, UPPER(name), first_name || last_name, CASE WHEN state = 1 THEN name END, * "
		    "FROM users WHERE id = 1 ORDER BY created_at",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "name", "select", 0U);
	if (expect_true(column != NULL, "direct SELECT column should exist") != 0 ||
	    expect_true(json_integer_is(column, "clause_id", 1), "direct SELECT column clause_id should be 1") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "direct SELECT column target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "name", "select", 1U);
	if (expect_true(column != NULL, "function SELECT column should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 1U), "function target_path should contain one entry") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "UPPER", 0), "function target_path should be UPPER") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "first_name", "select", 0U);
	if (expect_true(column != NULL, "expression first column should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "||", 0), "expression first target_path should be || arg 0") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "last_name", "select", 0U);
	if (expect_true(column != NULL, "expression second column should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "||", 1), "expression second target_path should be || arg 1") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "state", "select", 0U);
	if (expect_true(column != NULL, "CASE expression condition column should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "case_when", 0), "CASE target_path should start with case_when") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "*", "select", 0U);
	if (expect_true(column != NULL, "unqualified star column should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "star target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "where", 0U);
	if (expect_true(column != NULL, "WHERE column should exist") != 0 ||
	    expect_true(json_integer_is(column, "clause_id", 2), "WHERE column clause_id should be 2") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "WHERE target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "created_at", "order", 0U);
	if (expect_true(column != NULL, "ORDER BY column should exist") != 0 ||
	    expect_true(json_integer_is(column, "clause_id", 3), "ORDER BY column clause_id should be 3") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT u.* FROM public.users u",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "*", "select", 0U);
	if (expect_true(column != NULL, "qualified star column should exist") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT name FROM (SELECT name FROM users WHERE id = 1) x",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "name", "select", 0U);
	if (expect_true(column != NULL, "outer direct column should exist") != 0 ||
	    expect_true(json_is_integer(json_object_get(column, "clause_id")), "outer direct column clause_id should be set") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "name", "select", 1U);
	if (expect_true(column != NULL, "inner direct column should exist") != 0 ||
	    expect_true(json_is_integer(json_object_get(column, "clause_id")), "inner direct column clause_id should be set") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = 'paid'",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	clauses = json_object_get(statement, "clauses");
	on_clause = NULL;
	json_array_foreach(clauses, index, on_clause)
	{
		if (json_string_is(on_clause, "kind", "on")) {
			break;
		}
		on_clause = NULL;
	}
	if (expect_true(on_clause != NULL, "JOIN ON clause should be exposed in JSON") != 0 ||
	    expect_true(json_key_is_null(on_clause, "selector"), "JOIN ON clause selector should be null") != 0 ||
	    expect_true(json_string_value(json_object_get(on_clause, "sql")) != NULL, "JOIN ON clause SQL should be available") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "on", 0U);
	if (expect_true(column != NULL, "JOIN ON left column should exist") != 0 ||
	    expect_true(json_is_integer(json_object_get(column, "clause_id")), "JOIN ON left column clause_id should be set") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "orders", "user_id", "on", 0U);
	if (expect_true(column != NULL, "JOIN ON right column should exist") != 0 ||
	    expect_true(json_is_integer(json_object_get(column, "clause_id")), "JOIN ON right column clause_id should be set") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT dept, COUNT(id) FROM users GROUP BY dept HAVING COUNT(id) > 1",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "dept", "group", 0U);
	if (expect_true(column != NULL, "GROUP BY column should exist") != 0 ||
	    expect_true(json_is_integer(json_object_get(column, "clause_id")), "GROUP BY clause_id should be set") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "having", 0U);
	if (expect_true(column != NULL, "HAVING column should exist") != 0 ||
	    expect_true(json_is_integer(json_object_get(column, "clause_id")), "HAVING clause_id should be set") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT CONCAT(UPPER(first_name), last_name) FROM users",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "first_name", "select", 0U);
	if (expect_true(column != NULL, "nested function first arg should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 2U), "nested function should expose full target_path") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "CONCAT", 0), "nested function outer path should be CONCAT arg 0") != 0 ||
	    expect_true(json_target_path_entry_is(column, 1U, "function", "UPPER", 0), "nested function inner path should be UPPER arg 0") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "last_name", "select", 0U);
	if (expect_true(column != NULL, "function second arg should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 1U), "second arg should expose one target_path entry") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "CONCAT", 1), "second arg path should be CONCAT arg 1") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT DISTINCT LOW(UPPER(name)) FROM table1",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	if (expect_true(
		    json_array_contains_string_value(json_object_get(statement, "keywords"), "distinct"),
		    "SELECT DISTINCT should expose distinct keyword") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "table1", "name", "select", 0U);
	if (expect_true(column != NULL, "distinct nested function column should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 2U), "distinct nested function should expose full target_path") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "LOW", 0), "distinct nested function outer path should be LOW") != 0 ||
	    expect_true(json_target_path_entry_is(column, 1U, "function", "UPPER", 0), "distinct nested function inner path should be UPPER") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	if (expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT UPPER(name) || '_x' FROM users",
		    "users",
		    "name",
		    "select",
		    "expression",
		    "||",
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT COALESCE(name, fallback_name) FROM users",
		    "users",
		    "name",
		    "select",
		    "function",
		    "COALESCE",
		    1,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT COALESCE(name, fallback_name) FROM users",
		    "users",
		    "fallback_name",
		    "select",
		    "function",
		    "COALESCE",
		    1,
		    1) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT CAST(age AS text) FROM users",
		    "users",
		    "age",
		    "select",
		    "function",
		    "CAST",
		    1,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT GREATEST(age, score) FROM users",
		    "users",
		    "score",
		    "select",
		    "function",
		    "GREATEST",
		    1,
		    1) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT name COLLATE \"C\" FROM users",
		    "users",
		    "name",
		    "select",
		    "expression",
		    "collate",
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT ARRAY[id, age] FROM users",
		    "users",
		    "id",
		    "select",
		    "expression",
		    "array",
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT ROW(id, age) FROM users",
		    "users",
		    "age",
		    "select",
		    "expression",
		    "row",
		    1,
		    1) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT name IS NULL FROM users",
		    "users",
		    "name",
		    "select",
		    "expression",
		    "is_null",
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT active IS TRUE FROM users",
		    "users",
		    "active",
		    "select",
		    "expression",
		    "boolean_test",
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT COUNT(id) FILTER (WHERE status = 'paid') FROM users",
		    "users",
		    "id",
		    "select",
		    "function",
		    "COUNT",
		    1,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users",
		    "users",
		    "amount",
		    "select",
		    "function",
		    "SUM",
		    1,
		    0) != 0) {
		return 1;
	}

	if (expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "UPDATE users SET name = 'bob', age = age + 1 WHERE id = 1",
		    "users",
		    "name",
		    "set",
		    "not_output",
		    NULL,
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "UPDATE users SET name = 'bob', age = age + 1 WHERE id = 1",
		    "users",
		    "age",
		    "set",
		    "not_output",
		    NULL,
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "UPDATE users SET name = 'bob', age = age + 1 WHERE id = 1",
		    "users",
		    "id",
		    "where",
		    "not_output",
		    NULL,
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "DELETE FROM users WHERE status = 'inactive' AND age > 30",
		    "users",
		    "status",
		    "where",
		    "not_output",
		    NULL,
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "INSERT INTO users (id, name, age) VALUES (1, 'bob', 18)",
		    "users",
		    "id",
		    "insert",
		    "not_output",
		    NULL,
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "CREATE VIEW active_users AS SELECT id, name FROM users WHERE status = 'active'",
		    "users",
		    "status",
		    "where",
		    "not_output",
		    NULL,
		    0,
		    0) != 0 ||
	    expect_view_column_shape(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "INSERT INTO audit_users (id, name) SELECT id, name FROM users WHERE status = 'active'",
		    "users",
		    "name",
		    "select",
		    "direct_column",
		    NULL,
		    0,
		    0) != 0) {
		return 1;
	}

	for (index = 0U; index < sizeof(dialect_cases) / sizeof(dialect_cases[0]); index++) {
		root = NULL;
		statement = NULL;
		if (view_json_parse_statement(
			    dialect_cases[index].dialect,
			    dialect_cases[index].sql,
			    &root,
			    &statement) != 0) {
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "name", "select", 0U);
		if (expect_true(column != NULL, "dialect direct column should exist") != 0) {
			json_decref(root);
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "name", "select", 1U);
		if (expect_true(column != NULL, "dialect function column should exist") != 0 ||
		    expect_true(json_target_path_entry_is(column, 0U, "function", "UPPER", 0), "dialect function target_path should be UPPER") != 0) {
			json_decref(root);
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "id", "where", 0U);
		if (expect_true(column != NULL, "dialect WHERE column should exist") != 0) {
			json_decref(root);
			return 1;
		}
		if (dialect_cases[index].bind_name != NULL &&
		    (expect_true(json_string_is(column, "bind", dialect_cases[index].bind_name), "dialect bind should be exposed without marker") != 0 ||
		     expect_true(json_key_is_null(column, "value"), "dialect bind value should be null") != 0)) {
			json_decref(root);
			return 1;
		}
		json_decref(root);
	}

	return 0;
}

static int test_sql_view_attribution_and_values(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t users_object;
	sqlparser_object_view_t orders_object;
	sqlparser_column_view_t column;
	sqlparser_value_view_t value;
	char *value_sql;
	char *selector_text;
	char *deparsed_sql;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	size_t index;
	int saw_users_id;
	int saw_order_no;
	int saw_order_status;
	int rc;

	sql = "SELECT u.id, o.order_no FROM app.users u JOIN sales.orders o ON u.id = o.user_id WHERE o.status = 'paid'";
	handle = NULL;
	value_sql = NULL;
	selector_text = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "sql view attribution parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_get_view(handle, &view, &error);
	if (expect_status_ok(rc, &error, "sql view attribution should be available") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_view_statement_at(&view, 0U, &statement, &error);
	if (expect_status_ok(rc, &error, "sql view attribution statement should be available") != 0 ||
	    expect_true(statement.object_count == 2U, "join should expose two table objects") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_object_at(&statement, 0U, &users_object, &error);
	if (expect_status_ok(rc, &error, "users object should be available") != 0 ||
	    expect_true(strcmp(users_object.schema_name, "app") == 0, "users schema should be app") != 0 ||
	    expect_true(strcmp(users_object.table_name, "users") == 0, "users table should be users") != 0 ||
	    expect_true(strcmp(users_object.alias_name, "u") == 0, "users alias should be u") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_object_at(&statement, 1U, &orders_object, &error);
	if (expect_status_ok(rc, &error, "orders object should be available") != 0 ||
	    expect_true(strcmp(orders_object.schema_name, "sales") == 0, "orders schema should be sales") != 0 ||
	    expect_true(strcmp(orders_object.table_name, "orders") == 0, "orders table should be orders") != 0 ||
	    expect_true(strcmp(orders_object.alias_name, "o") == 0, "orders alias should be o") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	saw_users_id = 0;
	for (index = 0U; index < users_object.column_count; index++) {
		rc = sqlparser_object_column_at(&users_object, index, &column, &error);
		if (expect_status_ok(rc, &error, "users column should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (strcmp(column.name, "id") == 0) {
			saw_users_id = 1;
		}
	}
	saw_order_no = 0;
	saw_order_status = 0;
	for (index = 0U; index < orders_object.column_count; index++) {
		rc = sqlparser_object_column_at(&orders_object, index, &column, &error);
		if (expect_status_ok(rc, &error, "orders column should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (strcmp(column.name, "order_no") == 0) {
			saw_order_no = 1;
		}
		if (strcmp(column.name, "status") == 0) {
			saw_order_status = 1;
			if (expect_true(column.value_count == 1U, "where column should carry one value") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			rc = sqlparser_column_value_at(&column, 0U, &value, &error);
			if (expect_status_ok(rc, &error, "where column value should be available") != 0 ||
			    expect_true(value.has_selector != 0, "where value should expose patch selector") != 0 ||
			    expect_true(value.selector.kind == SQLPARSER_SELECTOR_KIND_VALUE, "where value selector should be value") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			rc = sqlparser_value_sql(handle, &value, &value_sql, &error);
			if (expect_status_ok(rc, &error, "where value SQL should be available") != 0 ||
			    expect_true(strcmp(value_sql, "'paid'") == 0, "where value SQL should preserve literal SQL") != 0) {
				sqlparser_string_free(value_sql);
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_string_free(value_sql);
			value_sql = NULL;
			rc = sqlparser_selector_format(&value.selector, &selector_text, &error);
			if (expect_status_ok(rc, &error, "where value selector should format") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		}
	}
	if (expect_true(saw_users_id != 0, "users object should include id column") != 0 ||
	    expect_true(saw_order_no != 0, "orders object should include order_no column") != 0 ||
	    expect_true(saw_order_status != 0, "orders object should include status column") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector_text;
	patch.sql = "'done'";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "where value patch should succeed") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "where value patched deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'done'") != NULL, "where value patch should appear in deparse") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_text);
	deparsed_sql = NULL;
	selector_text = NULL;
	sqlparser_handle_destroy(handle);

	sql = "UPDATE users SET name = 'bob' WHERE id = 1";
	handle = NULL;
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "sql view update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_get_view(handle, &view, &error);
	if (expect_status_ok(rc, &error, "sql view update should be available") != 0 ||
	    expect_status_ok(sqlparser_view_statement_at(&view, 0U, &statement, &error), &error, "sql view update statement should be available") != 0 ||
	    expect_status_ok(sqlparser_statement_object_at(&statement, 0U, &users_object, &error), &error, "sql view update object should be available") != 0 ||
	    expect_true(users_object.column_count >= 2U, "update object should expose set and where columns") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_object_column_at(&users_object, 0U, &column, &error);
	if (expect_status_ok(rc, &error, "update set column should be available") != 0 ||
	    expect_true(strcmp(column.name, "name") == 0, "update set column should be name") != 0 ||
	    expect_true(column.value_count == 1U, "update set column should carry value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_column_value_at(&column, 0U, &value, &error);
	if (expect_status_ok(rc, &error, "update value should be available") != 0 ||
	    expect_true(value.has_selector != 0, "update value should expose patch selector") != 0 ||
	    expect_true(value.selector.kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT, "update value selector should replace assignment") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_value_sql(handle, &value, &value_sql, &error);
	if (expect_status_ok(rc, &error, "update value SQL should be available") != 0 ||
	    expect_true(strcmp(value_sql, "'bob'") == 0, "update value SQL should preserve assignment SQL") != 0) {
		sqlparser_string_free(value_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(value_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_select_target_list_patch_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t object;
	sqlparser_column_view_t column;
	sqlparser_selector_t selector;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	sqlparser_parse_options_t options;
	char *target_sql;
	char *selector_text;
	char *view_json;
	char *deparsed_sql;
	size_t list_count;
	size_t target_count;
	int rc;

	sql = "SELECT * FROM public.users";
	handle = NULL;
	target_sql = NULL;
	selector_text = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "select target parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_select_target_list_count(handle, 0U, &list_count, &error);
	if (expect_status_ok(rc, &error, "select target list count should succeed") != 0 ||
	    expect_true(list_count == 1U, "select should expose one target list") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
	if (expect_status_ok(rc, &error, "select target count should succeed") != 0 ||
	    expect_true(target_count == 1U, "select star should expose one target") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "select target SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "*") == 0, "select star target SQL should be *") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;

	rc = sqlparser_get_view(handle, &view, &error);
	if (expect_status_ok(rc, &error, "select view should be available") != 0 ||
	    expect_status_ok(sqlparser_view_statement_at(&view, 0U, &statement, &error), &error, "select statement view should be available") != 0 ||
	    expect_status_ok(sqlparser_statement_object_at(&statement, 0U, &object, &error), &error, "select object should be available") != 0 ||
	    expect_status_ok(sqlparser_object_column_at(&object, 0U, &column, &error), &error, "select star column should be available") != 0 ||
	    expect_true(strcmp(column.name, "*") == 0, "select star column should be *") != 0 ||
	    expect_true(column.has_target_list_selector != 0, "select star should expose target list selector") != 0 ||
	    expect_true(column.has_target_selector != 0, "select star should expose target selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_format(&column.target_list_selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "select target list selector should format") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].select_targets[0]") == 0, "select target list selector mismatch") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(selector_text);
	selector_text = NULL;

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "select view JSON should export") != 0 ||
	    expect_true(strstr(view_json, "\"target_list_selector\":\"stmt[0].select_targets[0]\"") != NULL,
	                "select view JSON should expose target list selector") != 0 ||
	    expect_true(strstr(view_json, "\"target_selector\":\"stmt[0].select_target[0][0]\"") != NULL,
	                "select view JSON should expose target selector") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_select_set_targets_sql(handle, 0U, 0U, "id, name, created_at", &error);
	if (expect_status_ok(rc, &error, "select target list replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select target list replace should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_insert_target_sql(handle, 0U, 0U, 2U, "upper(name) AS upper_name", &error);
	if (expect_status_ok(rc, &error, "select target insert should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select target insert should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_delete_target(handle, 0U, 0U, 3U, &error);
	if (expect_status_ok(rc, &error, "select target delete should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select target delete should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_set_target_sql(handle, 0U, 0U, 0U, "users.id AS user_id", &error);
	if (expect_status_ok(rc, &error, "select single target replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select single target replace should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
	if (expect_status_ok(rc, &error, "patched select target count should succeed") != 0 ||
	    expect_true(target_count == 3U, "patched select should expose three targets") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 1U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "patched select target SQL should succeed") != 0 ||
	    expect_true(strstr(target_sql, "name") != NULL, "patched select target should contain name") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "patched select deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "users.id AS user_id") != NULL, "patched select should contain user_id target") != 0 ||
	    expect_true(strstr(deparsed_sql, "upper(name) AS upper_name") != NULL, "patched select should contain inserted expression") != 0 ||
	    expect_true(strstr(deparsed_sql, "created_at") == NULL, "patched select should remove created_at") != 0 ||
	    expect_true(strstr(deparsed_sql, "SELECT *") == NULL, "patched select should remove star") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT u.* FROM public.users u";
	handle = NULL;
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "qualified star parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "qualified star SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "u.*") == 0, "qualified star SQL should be u.*") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT * FROM `users`";
	handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql select target parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_select_set_targets_sql(handle, 0U, 0U, "`id`, `name`", &error);
	if (expect_status_ok(rc, &error, "mysql select target replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "mysql select target replace should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql select target deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "id, name") != NULL, "mysql deparse should contain rewritten targets") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "mysql deparse should not expose internal state") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT * FROM users";
	handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "oracle select target parse should succeed") != 0) {
		return 1;
	}
	selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGETS;
	selector.statement_index = 0U;
	selector.item_index = 0U;
	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "oracle select targets selector should format") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector_text;
	patch.sql = ":id AS id, q'[Bob's order]' AS label";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	sqlparser_string_free(selector_text);
	selector_text = NULL;
	if (expect_status_ok(rc, &error, "oracle select target patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "oracle select target patch should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "oracle select target deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, ":id AS id") != NULL, "oracle deparse should restore named bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "oracle deparse should not expose internal bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT * FROM [dbo].[users]";
	handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "sqlserver select target parse should succeed") != 0) {
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].select_targets[0]";
	patch.index = 1U;
	patch.sql = "[name] AS [display_name]";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "sqlserver select target insert patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "sqlserver select target patch should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sqlserver select target deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name AS display_name") != NULL, "sqlserver deparse should contain inserted target") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "sqlserver deparse should not expose internal bind markers") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sql_view_set_operation_attribution(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t users_object;
	sqlparser_object_view_t archive_object;
	sqlparser_column_view_t column;
	char *view_json;
	int rc;

	sql = "SELECT u.id FROM users u UNION ALL SELECT a.id FROM archived_users a ORDER BY id";
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "set operation parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_get_view(handle, &view, &error);
	if (expect_status_ok(rc, &error, "set operation view should be available") != 0 ||
	    expect_status_ok(sqlparser_view_statement_at(&view, 0U, &statement, &error), &error, "set operation statement should be available") != 0 ||
	    expect_true(statement.object_count == 2U, "set operation should expose both table objects") != 0 ||
	    expect_true(statement.clause_count == 5U, "set operation should expose operand clauses and one top-level order_by") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_object_at(&statement, 0U, &users_object, &error);
	if (expect_status_ok(rc, &error, "set operation users object should be available") != 0 ||
	    expect_true(users_object.column_count > 0U, "set operation users object should expose columns") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_object_column_at(&users_object, 0U, &column, &error);
	if (expect_status_ok(rc, &error, "set operation users column should be available") != 0 ||
	    expect_true(strcmp(column.name, "id") == 0, "set operation users column should be id") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_object_at(&statement, 1U, &archive_object, &error);
	if (expect_status_ok(rc, &error, "set operation archive object should be available") != 0 ||
	    expect_true(archive_object.column_count > 0U, "set operation archive object should expose columns") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_object_column_at(&archive_object, 0U, &column, &error);
	if (expect_status_ok(rc, &error, "set operation archive column should be available") != 0 ||
	    expect_true(strcmp(column.name, "id") == 0, "set operation archive column should be id") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "set operation view export should succeed") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"users\"") != NULL, "set operation view should include users") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"archived_users\"") != NULL, "set operation view should include archived_users") != 0 ||
	    expect_true(strstr(view_json, "\"selector\":\"stmt[0].clause[4]\",\"sql\":\"id\"") != NULL,
	                "set operation view should expose top-level order_by") != 0 ||
	    expect_true(strstr(view_json, "\"name\":\"id\"") != NULL, "set operation view should include id columns") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_session_context_view_and_patch(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *keyword;
		const char *column_name;
		const char *initial_value;
		const char *replacement_sql;
		const char *deparse_contains;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET search_path TO app_schema",
			"set",
			"search_path",
			"app_schema",
			"next_schema",
			"SET search_path TO next_schema"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"USE analytics",
			"use",
			"DATABASE",
			"analytics",
			"warehouse",
			"USE warehouse"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [AdventureWorks2022]",
			"use",
			"DATABASE",
			"[AdventureWorks2022]",
			"[ReportingDB]",
			"USE [ReportingDB]"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"alter_session",
			"CURRENT_SCHEMA",
			"kdes",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = app"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CONTAINER=PDB1",
			"alter_session",
			"CONTAINER",
			"pdb1",
			"PDB2",
			"ALTER SESSION SET CONTAINER = pdb2"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SET SCHEMA KDES",
			"alter_session",
			"CURRENT_SCHEMA",
			"kdes",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = app"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"alter_session",
			"CURRENT_SCHEMA",
			"kdes",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = app"
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_parse_options_t options;
		sqlparser_error_t error;
		sqlparser_handle_t *handle;
		sqlparser_view_t view;
		sqlparser_statement_view_t statement;
		sqlparser_object_view_t object;
		sqlparser_column_view_t column;
		sqlparser_value_view_t value;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *value_sql;
		char *selector_text;
		char *deparsed_sql;
		int rc;

		handle = NULL;
		value_sql = NULL;
		selector_text = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "session context parse should succeed") != 0) {
			return 1;
		}

		rc = sqlparser_get_view(handle, &view, &error);
		if (expect_status_ok(rc, &error, "session context view should be available") != 0 ||
		    expect_true(view.statement_count == 1U, "session context statement count should be 1") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_view_statement_at(&view, 0U, &statement, &error);
		if (expect_status_ok(rc, &error, "session context statement should be available") != 0 ||
		    expect_true(strcmp(statement.keyword, cases[index].keyword) == 0, "session context keyword mismatch") != 0 ||
		    expect_true(statement.object_count == 1U, "session context should expose one object") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_object_at(&statement, 0U, &object, &error);
		if (expect_status_ok(rc, &error, "session context object should be available") != 0 ||
		    expect_true(object.table_name == NULL, "session context object table should be NULL") != 0 ||
		    expect_true(object.column_count == 1U, "session context should expose one parameter") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_object_column_at(&object, 0U, &column, &error);
		if (expect_status_ok(rc, &error, "session context column should be available") != 0 ||
		    expect_true(strcmp(column.name, cases[index].column_name) == 0, "session context column name mismatch") != 0 ||
		    expect_true(column.value_count == 1U, "session context column should expose one value") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_column_value_at(&column, 0U, &value, &error);
		if (expect_status_ok(rc, &error, "session context value should be available") != 0 ||
		    expect_true(value.has_selector != 0, "session context value should expose selector") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_value_sql(handle, &value, &value_sql, &error);
		if (expect_status_ok(rc, &error, "session context value SQL should be available") != 0 ||
		    expect_true(strcmp(value_sql, cases[index].initial_value) == 0, "session context value SQL mismatch") != 0) {
			sqlparser_string_free(value_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(value_sql);
		value_sql = NULL;

		rc = sqlparser_selector_format(&value.selector, &selector_text, &error);
		if (expect_status_ok(rc, &error, "session context selector format should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = selector_text;
		patch.sql = cases[index].replacement_sql;
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		sqlparser_string_free(selector_text);
		selector_text = NULL;
		if (expect_status_ok(rc, &error, "session context patch should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "session context deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].deparse_contains) != NULL, "session context deparse mismatch") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_oracle_container_service_view_and_patch(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_view_t view;
	sqlparser_statement_view_t statement;
	sqlparser_object_view_t object;
	sqlparser_column_view_t column;
	sqlparser_value_view_t value;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *value_sql;
	char *selector_text;
	char *deparsed_sql;
	int rc;

	handle = NULL;
	value_sql = NULL;
	selector_text = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"ALTER SESSION SET CONTAINER=PDB1 SERVICE=APP_SVC",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "oracle container service parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_get_view(handle, &view, &error);
	if (expect_status_ok(rc, &error, "oracle container service view should be available") != 0 ||
	    expect_status_ok(sqlparser_view_statement_at(&view, 0U, &statement, &error), &error, "oracle container service statement should be available") != 0 ||
	    expect_status_ok(sqlparser_statement_object_at(&statement, 0U, &object, &error), &error, "oracle container service object should be available") != 0 ||
	    expect_true(object.column_count == 2U, "oracle container service should expose CONTAINER and SERVICE") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_object_column_at(&object, 0U, &column, &error);
	if (expect_status_ok(rc, &error, "oracle container column should be available") != 0 ||
	    expect_true(strcmp(column.name, "CONTAINER") == 0, "oracle container column name mismatch") != 0 ||
	    expect_true(column.value_count == 1U, "oracle container should expose one value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_object_column_at(&object, 1U, &column, &error);
	if (expect_status_ok(rc, &error, "oracle service column should be available") != 0 ||
	    expect_true(strcmp(column.name, "SERVICE") == 0, "oracle service column name mismatch") != 0 ||
	    expect_true(column.value_count == 1U, "oracle service should expose one value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_column_value_at(&column, 0U, &value, &error);
	if (expect_status_ok(rc, &error, "oracle service value should be available") != 0 ||
	    expect_true(value.has_selector != 0, "oracle service value should expose selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_value_sql(handle, &value, &value_sql, &error);
	if (expect_status_ok(rc, &error, "oracle service value SQL should be available") != 0 ||
	    expect_true(strcmp(value_sql, "app_svc") == 0, "oracle service value SQL mismatch") != 0) {
		sqlparser_string_free(value_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(value_sql);

	rc = sqlparser_selector_format(&value.selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "oracle service selector format should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector_text;
	patch.sql = "REPORT_SVC";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	sqlparser_string_free(selector_text);
	if (expect_status_ok(rc, &error, "oracle service patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "oracle service deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "ALTER SESSION SET CONTAINER = pdb1 SERVICE = report_svc") != NULL,
	                "oracle service deparse mismatch") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

int main(void)
{
	if (test_statement_kind_walk() != 0) {
		return 1;
	}
	if (test_insert_values_literal_mutation() != 0) {
		return 1;
	}
	if (test_insert_select_inspect() != 0) {
		return 1;
	}
	if (test_update_assignment_literal_mutation() != 0) {
		return 1;
	}
	if (test_update_assignment_sql_mutation() != 0) {
		return 1;
	}
	if (test_insert_cell_sql_mutation() != 0) {
		return 1;
	}
	if (test_delete_where_literal_mutation() != 0) {
		return 1;
	}
	if (test_generic_relation_and_literal_api() != 0) {
		return 1;
	}
	if (test_generic_name_api_on_ddl() != 0) {
		return 1;
	}
	if (test_selector_parse_and_format() != 0) {
		return 1;
	}
	if (test_where_clause_sql_rewrite_api() != 0) {
		return 1;
	}
	if (test_sql_view_json_and_patch_api() != 0) {
		return 1;
	}
	if (test_sql_view_bind_fields() != 0) {
		return 1;
	}
	if (test_sql_view_column_semantics_json() != 0) {
		return 1;
	}
	if (test_sql_view_attribution_and_values() != 0) {
		return 1;
	}
	if (test_select_target_list_patch_api() != 0) {
		return 1;
	}
	if (test_sql_view_set_operation_attribution() != 0) {
		return 1;
	}
	if (test_generic_literal_api_on_ddl() != 0) {
		return 1;
	}
	if (test_update_from_returning_sql_mutation() != 0) {
		return 1;
	}
	if (test_insert_on_conflict_returning_sql_mutation() != 0) {
		return 1;
	}
	if (test_merge_statement_walk() != 0) {
		return 1;
	}
	if (test_resource_limits() != 0) {
		return 1;
	}
	if (test_mysql_dialect_select_rewrite() != 0) {
		return 1;
	}
	if (test_mysql_dialect_insert_rewrite() != 0) {
		return 1;
	}
	if (test_dialect_insert_column_patch_with_question_param() != 0) {
		return 1;
	}
	if (test_mysql_dialect_unsupported() != 0) {
		return 1;
	}
	if (test_sqlserver_dialect_option() != 0) {
		return 1;
	}
	if (test_session_context_view_and_patch() != 0) {
		return 1;
	}
	if (test_oracle_container_service_view_and_patch() != 0) {
		return 1;
	}

	return 0;
}
