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
	char *parse_tree_json;
	size_t assignment_count;
	size_t where_count;
	int rc;

	sql = "UPDATE public.users SET name = 'bob', age = 18 WHERE id = 1 AND status = 'active'";
	handle = NULL;
	deparsed_sql = NULL;
	parse_tree_json = NULL;
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

	rc = sqlparser_export_parse_tree_json(handle, 0, &parse_tree_json, &error);
	if (expect_status_ok(rc, &error, "update parse tree export should succeed") != 0 ||
	    expect_true(strstr(parse_tree_json, "carol") != NULL, "parse tree JSON should contain carol") != 0 ||
	    expect_true(strstr(parse_tree_json, "\"ival\":{\"ival\":2}") != NULL, "parse tree JSON should contain updated where integer") != 0) {
		sqlparser_string_free(parse_tree_json);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(parse_tree_json);
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
	return 0;
}

static int test_model_json_patch_roundtrip(void)
{
	const char *sql;
	const char *patch_json;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *model_json;
	char *deparsed_sql;
	int rc;

	sql = "UPDATE public.users SET name = 'bob' WHERE id = 1";
	patch_json =
		"{\"changes\":["
		"{\"selector\":\"stmt[0].assignment[0]\",\"literal\":{\"kind\":\"string\",\"string_value\":\"carol\"}},"
		"{\"selector\":\"stmt[0].where_literal[0]\",\"literal\":{\"kind\":\"integer\",\"integer_value\":2}}"
		"]}";
	handle = NULL;
	model_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "model roundtrip parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_export_model_json(handle, 1, &model_json, &error);
	if (expect_status_ok(rc, &error, "model export should succeed") != 0 ||
	    expect_true(strstr(model_json, "sqlparser.model/v1") != NULL, "model export should contain schema") != 0 ||
	    expect_true(strstr(model_json, "stmt[0].assignment[0]") != NULL, "model export should contain assignment selector") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_apply_model_json(handle, model_json, &error);
	if (expect_status_ok(rc, &error, "applying unchanged model should succeed") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "deparse after unchanged model import should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = 'bob'") != NULL, "unchanged model import should preserve bob") != 0 ||
	    expect_true(strstr(deparsed_sql, "id = 1") != NULL, "unchanged model import should preserve id = 1") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	rc = sqlparser_apply_model_json(handle, patch_json, &error);
	if (expect_status_ok(rc, &error, "model patch import should succeed") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "deparse after model patch should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = 'carol'") != NULL, "patched model should rewrite assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "id = 2") != NULL, "patched model should rewrite where literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(model_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_model_json_sql_patch_roundtrip(void)
{
	const char *sql;
	const char *patch_json;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *model_json;
	char *deparsed_sql;
	int rc;

	sql =
		"UPDATE public.users SET name = upper(name), updated_at = DEFAULT WHERE id = 1; "
		"INSERT INTO public.audit_log (id, payload, created_at) "
		"VALUES (1, json_build_object('k', 'v'), DEFAULT)";
	patch_json =
		"{\"changes\":["
		"{\"selector\":\"stmt[0].assignment[0]\",\"sql\":\"lower(name)\"},"
		"{\"selector\":\"stmt[1].insert_cell[0][2]\",\"sql\":\"clock_timestamp()\"}"
		"]}";
	handle = NULL;
	model_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "SQL model roundtrip parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_export_model_json(handle, 1, &model_json, &error);
	if (expect_status_ok(rc, &error, "SQL model export should succeed") != 0 ||
	    expect_true(strstr(model_json, "\"sql\": \"upper(name)\"") != NULL, "model export should contain expression assignment SQL") != 0 ||
	    expect_true(strstr(model_json, "\"sql\": \"DEFAULT\"") != NULL, "model export should contain DEFAULT SQL") != 0 ||
	    expect_true(strstr(model_json, "\"value_kind\": \"expression\"") != NULL, "model export should contain expression kind") != 0 ||
	    expect_true(strstr(model_json, "\"value_kind\": \"default\"") != NULL, "model export should contain default kind") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_apply_model_json(handle, model_json, &error);
	if (expect_status_ok(rc, &error, "applying unchanged SQL model should succeed") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "deparse after unchanged SQL model import should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = upper(name)") != NULL, "unchanged SQL model import should preserve upper(name)") != 0 ||
	    expect_true(strstr(deparsed_sql, "INSERT INTO public.audit_log") != NULL, "unchanged SQL model import should preserve insert statement") != 0 ||
	    expect_true(strstr(deparsed_sql, "json_build_object(") != NULL, "unchanged SQL model import should preserve JSON expression") != 0 ||
	    expect_true(strstr(deparsed_sql, "DEFAULT") != NULL, "unchanged SQL model import should preserve DEFAULT") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	rc = sqlparser_apply_model_json(handle, patch_json, &error);
	if (expect_status_ok(rc, &error, "SQL model patch import should succeed") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "deparse after SQL model patch should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = lower(name)") != NULL, "SQL model patch should rewrite assignment expression") != 0 ||
	    expect_true(strstr(deparsed_sql, "clock_timestamp()") != NULL, "SQL model patch should rewrite insert cell expression") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(model_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_model_json_full_import(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *model_json;
	char *edited_model_json;
	char *deparsed_sql;
	json_t *root;
	json_t *statements;
	json_t *statement;
	json_t *names;
	json_t *name_entry;
	json_error_t json_error;
	size_t index;
	int rc;

	sql = "DROP VIEW IF EXISTS public.v_orders";
	handle = NULL;
	model_json = NULL;
	edited_model_json = NULL;
	deparsed_sql = NULL;
	root = NULL;
	memset(&error, 0, sizeof(error));
	memset(&json_error, 0, sizeof(json_error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "full model import parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_export_model_json(handle, 0, &model_json, &error);
	if (expect_status_ok(rc, &error, "full model export should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	root = json_loads(model_json, 0, &json_error);
	if (expect_true(root != NULL, "model JSON should decode") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	statements = json_object_get(root, "statements");
	statement = json_array_get(statements, 0);
	names = json_object_get(statement, "names");
	json_array_foreach(names, index, name_entry) {
		json_t *value_json;
		const char *value;

		value_json = json_object_get(name_entry, "value");
		value = json_is_string(value_json) ? json_string_value(value_json) : NULL;
		if (value != NULL && strcmp(value, "v_orders") == 0) {
			(void)json_object_set_new(name_entry, "value", json_string("v_orders_archive"));
		}
	}

	edited_model_json = json_dumps(root, JSON_COMPACT | JSON_ENSURE_ASCII | JSON_SORT_KEYS);
	json_decref(root);
	root = NULL;
	if (expect_true(edited_model_json != NULL, "edited model JSON should render") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_apply_model_json(handle, edited_model_json, &error);
	if (expect_status_ok(rc, &error, "full model import should succeed") != 0) {
		free(edited_model_json);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "deparse after full model import should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "v_orders_archive") != NULL, "full model import should rewrite ddl object name") != 0) {
		sqlparser_string_free(deparsed_sql);
		free(edited_model_json);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	free(edited_model_json);
	sqlparser_string_free(model_json);
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
	if (test_model_json_patch_roundtrip() != 0) {
		return 1;
	}
	if (test_model_json_sql_patch_roundtrip() != 0) {
		return 1;
	}
	if (test_model_json_full_import() != 0) {
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

	return 0;
}
