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
	if (expect_true(strcmp(sqlparser_bind_kind_name(SQLPARSER_BIND_KIND_POSITIONAL), "positional") == 0,
	                "bind kind name should be positional") != 0 ||
	    expect_true(strcmp(sqlparser_bind_kind_name(SQLPARSER_BIND_KIND_NAMED), "named") == 0,
	                "bind kind name should be named") != 0 ||
	    expect_true(strcmp(sqlparser_clause_kind_name(SQLPARSER_CLAUSE_KIND_GROUP_BY), "group_by") == 0,
	                "clause kind name should be group_by") != 0) {
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
	    expect_true(strstr(view_json, "\"integer_value\":2") != NULL, "view JSON should contain updated where value") != 0) {
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

static int test_update_assignment_list_patch_api(void)
{
	sqlparser_handle_t *handle;
	sqlparser_handle_t *guard_handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	char *deparsed_sql;
	size_t assignment_count;
	int rc;

	handle = NULL;
	guard_handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));

	rc = sqlparser_parse(
		"UPDATE public.users SET secret = 'qz$...', status = 'old' WHERE id = 1",
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "update assignment-list parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_update_insert_assignment_sql(handle, 0U, 1U, "secret_orig = 'abc'", &error);
	if (expect_status_ok(rc, &error, "update assignment insert should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "update assignment count after insert should succeed") != 0 ||
	    expect_true(assignment_count == 3U, "update assignment insert should add one item") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 1U, &assignment, &error);
	if (expect_status_ok(rc, &error, "inserted update assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "secret_orig") == 0, "inserted assignment column should match") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_LITERAL, "inserted assignment should be literal") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "abc") == 0, "inserted assignment value should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_full_sql(handle, 0U, 2U, "status_text = 'active'", &error);
	if (expect_status_ok(rc, &error, "update assignment full replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 2U, &assignment, &error);
	if (expect_status_ok(rc, &error, "replaced update assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "status_text") == 0, "replaced assignment column should match") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "active") == 0, "replaced assignment value should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_delete_assignment(handle, 0U, 2U, &error);
	if (expect_status_ok(rc, &error, "update assignment delete should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "update assignment count after delete should succeed") != 0 ||
	    expect_true(assignment_count == 2U, "update assignment delete should remove one item") != 0 ||
	    expect_deparse_reparse_ok(handle, "update assignment-list mutation should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update assignment-list deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "secret_orig = 'abc'") != NULL, "deparse should contain inserted assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "status_text") == NULL, "deparse should not contain deleted assignment") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("UPDATE t SET only_col = 1 WHERE id = 1", &guard_handle, &error);
	if (expect_status_ok(rc, &error, "single assignment update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_update_delete_assignment(guard_handle, 0U, 0U, &error);
	if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED, "delete last update assignment should be rejected") != 0) {
		sqlparser_handle_destroy(guard_handle);
		return 1;
	}
	sqlparser_handle_destroy(guard_handle);
	return 0;
}

static int test_update_assignment_list_apply_patch(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_patch_t patches[3];
	sqlparser_patch_list_t patch_list;
	sqlparser_assignment_view_t assignment;
	char *deparsed_sql;
	size_t assignment_count;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(patches, 0, sizeof(patches));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"UPDATE SERVERS SET IP = :ip, STATUS = :status WHERE ID = :id",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "oracle update assignment-list parse should succeed") != 0) {
		return 1;
	}

	patches[0].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patches[0].selector = "stmt[0].assignment[1]";
	patches[0].sql = "HOST = :host";
	patches[1].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patches[1].selector = "stmt[0].assignment[2]";
	patches[1].sql = "PORT = :port";
	patches[2].op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patches[2].selector = "stmt[0].assignment[0]";
	patch_list.items = patches;
	patch_list.count = 3U;

	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "update assignment-list patch should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "update assignment-list patch should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "patched update assignment count should succeed") != 0 ||
	    expect_true(assignment_count == 2U, "patched update should contain two assignments") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "patched first update assignment should be readable") != 0 ||
	    expect_true(assignment.column_name != NULL, "patched first assignment column should be present") != 0 ||
	    expect_true(strcmp(assignment.column_name, "HOST") == 0 || strcmp(assignment.column_name, "host") == 0,
	                "patched first assignment column should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "patched oracle update deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "HOST = :host") != NULL || strstr(deparsed_sql, "host = :host") != NULL,
	                "patched update should contain HOST bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "PORT = :port") != NULL || strstr(deparsed_sql, "port = :port") != NULL,
	                "patched update should contain PORT bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "IP = :ip") == NULL && strstr(deparsed_sql, "ip = :ip") == NULL,
	                "patched update should remove IP assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "patched update should not expose parser binds") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_structured_update_assignment_from_assignment_value(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
		const char *sql;
		const char *insert_selector_text;
		const char *source_selector_text;
		const char *target_part0;
		const char *target_part1;
		const char *expect_column;
		const char *expect_value_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-literal",
			"UPDATE public.users SET phone = '13800000000' WHERE id = 1",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"'13800000000'"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-subquery",
			"UPDATE public.users SET phone = (SELECT phone FROM backup WHERE id = 1) WHERE id = 2",
			"stmt[0].assignment[1]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"SELECT phone"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"mysql-question",
			"UPDATE users SET name = ?, phone = ?, email = ? WHERE id = ?",
			"stmt[0].assignment[1]",
			"stmt[0].assignment[1]",
			"phone_backup",
			NULL,
			"phone_backup",
			"?"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"mysql-update-join-question",
			"UPDATE users u JOIN profiles p ON u.id = p.user_id SET u.phone = ? WHERE p.id = ?",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"?"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"oracle-named-bind",
			"UPDATE users SET phone = :phone WHERE id = :id",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			":phone"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"sqlserver-named-bind",
			"UPDATE dbo.users SET phone = @phone WHERE id = @id",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"@phone"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"dameng-question",
			"UPDATE users SET phone = ? WHERE id = ?",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"?"
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t insert_selector;
	sqlparser_selector_t source_selector;
	sqlparser_identifier_path_view_t target;
	sqlparser_assignment_view_t assignment;
	char *assignment_sql;
	char *deparsed_sql;
	size_t assignment_count;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const char *target_parts[2];
		size_t target_part_count;

		handle = NULL;
		assignment_sql = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		memset(&insert_selector, 0, sizeof(insert_selector));
		memset(&source_selector, 0, sizeof(source_selector));
		memset(&target, 0, sizeof(target));
		memset(&assignment, 0, sizeof(assignment));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "structured update parse should succeed") != 0) {
			return 1;
		}

		rc = sqlparser_selector_parse(cases[index].insert_selector_text, &insert_selector, &error);
		if (expect_status_ok(rc, &error, "structured update insert selector should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_selector_parse(cases[index].source_selector_text, &source_selector, &error);
		if (expect_status_ok(rc, &error, "structured update source selector should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		target_parts[0] = cases[index].target_part0;
		target_parts[1] = cases[index].target_part1;
		target_part_count = cases[index].target_part1 != NULL ? 2U : 1U;
		target.parts = target_parts;
		target.part_count = target_part_count;
		rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
			handle,
			&insert_selector,
			&target,
			&source_selector,
			&error);
		if (expect_status_ok(rc, &error, "structured update assignment insert should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "structured update assignment insert should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
		if (expect_status_ok(rc, &error, "structured update assignment count should succeed") != 0 ||
		    expect_true(assignment_count >= 2U, "structured update should insert an assignment") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment(handle, 0U, insert_selector.item_index, &assignment, &error);
		if (expect_status_ok(rc, &error, "structured inserted assignment should be readable") != 0 ||
		    expect_true(assignment.column_name != NULL &&
		                strcmp(assignment.column_name, cases[index].expect_column) == 0,
		                "structured inserted assignment column mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment_sql(handle, 0U, insert_selector.item_index, &assignment_sql, &error);
		if (expect_status_ok(rc, &error, "structured inserted assignment SQL should be readable") != 0 ||
		    expect_true(assignment_sql != NULL &&
		                strstr(assignment_sql, cases[index].expect_value_sql) != NULL,
		                "structured inserted assignment value should match source RHS") != 0) {
			sqlparser_string_free(assignment_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(assignment_sql);
		assignment_sql = NULL;

		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "structured update deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].expect_column) != NULL,
		                "structured update deparse should contain target column") != 0 ||
		    expect_true(strstr(deparsed_sql, "$") == NULL,
		                "structured update deparse should not expose internal bind markers") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

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

static int test_insert_cell_bind_mutation(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_selector_t selector;
	sqlparser_bind_value_t bind;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	char *deparsed_sql;
	size_t cell_index;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (:1, :2)",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert bind mutation parse should succeed") != 0) {
		return 1;
	}

	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_new";
	rc = sqlparser_insert_set_cell_bind(handle, 0U, 0U, 1U, &bind, &error);
	if (expect_status_ok(rc, &error, "insert set cell bind should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = 0U;
	selector.row_index = 0U;
	selector.column_index = 0U;
	bind.kind = SQLPARSER_BIND_KIND_POSITIONAL;
	bind.key = "3";
	rc = sqlparser_selector_set_insert_cell_bind(handle, &selector, &bind, &error);
	if (expect_status_ok(rc, &error, "selector set insert cell bind should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "insert bind mutation deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, ":3") != NULL, "deparsed insert should contain positional bind") != 0 ||
	    expect_true(strstr(deparsed_sql, ":secret_new") != NULL, "deparsed insert should contain named bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "deparsed insert should not expose internal bind markers") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert bind mutation graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert bind mutation dml should be available") != 0 ||
	    expect_true(dml.rows.count == 2U, "insert bind mutation should expose two cells") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 0U, &cell_index, &error), &error, "first bind cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "first bind cell should be available") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && cell.has_bind != 0 && strcmp(cell.bind, "3") == 0, "first bind cell key mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 1U, &cell_index, &error), &error, "second bind cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "second bind cell should be available") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED && cell.has_bind != 0 && strcmp(cell.bind, "secret_new") == 0, "second bind cell key mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

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
	    expect_true(strstr(view_json, "\"query_graph\"") != NULL,
	                "view JSON should expose query graph") != 0 ||
	    expect_true(strstr(view_json, "\"clauses\"") == NULL,
	                "view JSON should not expose old clause array") != 0 ||
	    expect_true(strstr(view_json, "\"clause\":\"select_list\"") != NULL,
	                "query graph should expose select_list field clause") != 0 ||
	    expect_true(strstr(view_json, "\"clause\":\"where\"") != NULL,
	                "query graph should expose where field/value clause") != 0) {
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
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_column_t column;
	sqlparser_graph_dml_cell_t cell;
	char *deparsed_sql;
	size_t cell_index;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		deparsed_sql = NULL;
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

		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "patched insert graph should succeed") != 0 ||
		    expect_true(graph.has_dml != 0, "patched insert should expose dml graph") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_dml(&graph, &dml, &error);
		if (expect_status_ok(rc, &error, "patched insert dml should be available") != 0 ||
		    expect_true(dml.target_columns.count == 4U, "patched insert should expose four target columns") != 0 ||
		    expect_true(dml.rows.count == 8U, "patched insert should expose eight value cells") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_dml_column_at(&graph, 3U, &column, &error);
		if (expect_status_ok(rc, &error, "patched insert column should be available") != 0 ||
		    expect_true(strcmp(column.column_name, "created_at") == 0, "patched insert column should be created_at") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_span_index_at(&graph, dml.rows, 7U, &cell_index, &error);
		if (expect_status_ok(rc, &error, "patched insert cell index should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error);
		if (expect_status_ok(rc, &error, "patched insert cell should be available") != 0 ||
		    expect_true(cell.row_index == 1U, "patched insert cell row mismatch") != 0 ||
		    expect_true(cell.column_ordinal == 3U, "patched insert cell column mismatch") != 0 ||
		    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "patched insert cell should be a bind") != 0 ||
		    expect_true(cell.has_bind_sql != 0 && strcmp(cell.bind_sql, "?") == 0, "patched insert cell should expose question bind SQL") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_mysql_dialect_unsupported(void)
{
	const char *sqls[] = {
		"INSERT IGNORE INTO `users` (`id`) VALUES (1)",
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

static int json_string_is(json_t *object, const char *key, const char *expected);

static int test_query_graph_json_and_patch_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *view_json;
	char *deparsed_sql;
	json_error_t json_error;
	json_t *root;
	json_t *statement;
	json_t *graph_json;
	json_t *dml_json;
	json_t *rows_json;
	json_t *literal_json;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	int rc;

	sql = "INSERT INTO public.users (id, name) VALUES (1, 'bob'), (2, 'alice')";
	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	root = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 1, &view_json, &error);
	if (expect_status_ok(rc, &error, "query graph JSON export should succeed") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"query_graph\"") != NULL, "view JSON should contain query_graph") != 0 ||
	    expect_true(strstr(view_json, "\"objects\"") == NULL, "view JSON should not contain old objects") != 0 ||
	    expect_true(strstr(view_json, "\"clauses\"") == NULL, "view JSON should not contain old clauses") != 0 ||
	    expect_true(strstr(view_json, "\"sql\":") == NULL, "view JSON should not store per-node SQL text") != 0 ||
	    expect_true(strstr(view_json, "stmt[0].insert_cell[1][1]") != NULL, "view JSON should contain insert cell selector") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	root = json_loads(view_json, 0, &json_error);
	if (expect_true(root != NULL, "query graph JSON should decode") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	statement = json_array_get(json_object_get(root, "statements"), 0);
	graph_json = json_object_get(statement, "query_graph");
	dml_json = json_object_get(graph_json, "dml");
	rows_json = json_object_get(dml_json, "rows");
	literal_json = json_object_get(json_array_get(rows_json, 3), "literal");
	if (expect_true(json_is_object(graph_json), "query graph object should exist") != 0 ||
	    expect_true(json_array_size(json_object_get(dml_json, "target_columns")) == 2U, "insert graph should expose two target columns") != 0 ||
	    expect_true(json_array_size(rows_json) == 4U, "insert graph should expose four value cells") != 0 ||
	    expect_true(json_string_is(literal_json, "kind", "string"), "literal kind should be string") != 0 ||
	    expect_true(json_string_is(literal_json, "string_value", "alice"), "literal value should be alice") != 0) {
		json_decref(root);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	json_decref(root);
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph C view should be available") != 0 ||
	    expect_true(graph.has_dml != 0, "insert graph should expose dml") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 2U, "insert dml target column count mismatch") != 0 ||
	    expect_true(dml.rows.count == 4U, "insert dml row cell count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

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
	if (expect_status_ok(rc, &error, "query graph patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "query graph patched deparse should succeed") != 0 ||
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
	if (expect_status_ok(rc, &error, "query graph delete row patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "query graph delete row deparse should succeed") != 0 ||
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
	if (expect_status_ok(rc, &error, "query graph delete column patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "query graph delete column deparse should succeed") != 0 ||
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
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return value == NULL || json_is_null(value);
}

static int json_array_length_is(json_t *object, const char *key, size_t expected)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	if (value == NULL && expected == 0U) {
		return 1;
	}
	return json_is_array(value) && json_array_size(value) == expected;
}

static int json_optional_array_is_valid(json_t *object, const char *key)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return value == NULL || json_is_array(value);
}

static const char *json_empty_array_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (json_is_array(value) && json_array_size(value) == 0U) {
				return key;
			}
			nested_key = json_empty_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = json_empty_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
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

static int expect_query_graph_shape(json_t *statement)
{
	json_t *graph;
	const char *empty_array_key;

	graph = json_object_get(statement, "query_graph");
	if (expect_true(json_is_object(graph), "view JSON should expose query_graph") != 0 ||
	    expect_true(json_object_get(statement, "objects") == NULL, "view JSON should not expose old objects") != 0 ||
	    expect_true(json_object_get(statement, "clauses") == NULL, "view JSON should not expose old clauses") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "blocks"), "query_graph blocks should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "relations"), "query_graph relations should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "targets"), "query_graph targets should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "fields"), "query_graph fields should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "values"), "query_graph values should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "sets"), "query_graph sets should be an array when present") != 0) {
		return 1;
	}
	empty_array_key = json_empty_array_key(graph);
	if (expect_true(empty_array_key == NULL, "query_graph should omit empty arrays") != 0) {
		return 1;
	}
	return 0;
}

static const char *graph_clause_name_from_keyword(const char *keyword)
{
	if (keyword == NULL) {
		return NULL;
	}
	if (strcmp(keyword, "select") == 0) {
		return "select_list";
	}
	if (strcmp(keyword, "order") == 0) {
		return "order_by";
	}
	if (strcmp(keyword, "group") == 0) {
		return "group_by";
	}
	if (strcmp(keyword, "set") == 0) {
		return "set_list";
	}
	return keyword;
}

static json_t *find_view_column_json(
	json_t *statement,
	const char *table_name,
	const char *column_name,
	const char *keyword,
	size_t skip)
{
	json_t *graph;
	json_t *fields;
	json_t *relations;
	json_t *field;
	const char *clause_name;
	size_t field_index;

	if (!json_is_object(statement) || column_name == NULL) {
		return NULL;
	}
	graph = json_object_get(statement, "query_graph");
	fields = json_object_get(graph, "fields");
	relations = json_object_get(graph, "relations");
	clause_name = graph_clause_name_from_keyword(keyword);
	json_array_foreach(fields, field_index, field)
	{
		json_t *relation_ref;
		json_t *relation;
		const char *object_table;

		if (!json_string_is(field, "column", column_name)) {
			continue;
		}
		if (clause_name != NULL && !json_string_is(field, "clause", clause_name)) {
			continue;
		}
		if (table_name != NULL) {
			relation_ref = json_object_get(field, "relation");
			if (!json_is_integer(relation_ref)) {
				continue;
			}
			relation = json_array_get(relations, (size_t)json_integer_value(relation_ref));
			object_table = json_string_value(json_object_get(relation, "table"));
			if (object_table == NULL || strcmp(object_table, table_name) != 0) {
				continue;
			}
		}
		if (skip > 0U) {
			skip--;
			continue;
		}
		return field;
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
	if (expect_query_graph_shape(*out_statement) != 0) {
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

static int test_query_graph_bind_fields(void)
{
	struct bind_case {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *set_bind_key;
		const char *where_bind_key;
		sqlparser_bind_kind_t set_bind_kind;
		sqlparser_bind_kind_t where_bind_kind;
		size_t set_bind_position;
		size_t where_bind_position;
		const char *set_bind_sql;
		const char *where_bind_sql;
	};
	static const struct bind_case cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "UPDATE servers SET ip = $1 WHERE id = $2", "1", "2", SQLPARSER_BIND_KIND_POSITIONAL, SQLPARSER_BIND_KIND_POSITIONAL, 1U, 2U, "$1", "$2"},
		{SQLPARSER_DIALECT_MYSQL, "UPDATE servers SET ip = ? WHERE id = ?", "1", "2", SQLPARSER_BIND_KIND_POSITIONAL, SQLPARSER_BIND_KIND_POSITIONAL, 1U, 2U, "?", "?"},
		{SQLPARSER_DIALECT_ORACLE, "UPDATE SERVERS SET IP = :aaa WHERE ID = :id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, ":aaa", ":id"},
		{SQLPARSER_DIALECT_SQLSERVER, "UPDATE dbo.servers SET ip = @aaa WHERE id = @id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, "@aaa", "@id"},
		{SQLPARSER_DIALECT_DAMENG, "UPDATE servers SET ip = :aaa WHERE id = :id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, ":aaa", ":id"}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_handle_t *handle;
		char *deparsed_sql;
		const char *replacement_set_list;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_assignment_t assignment;
		sqlparser_graph_value_t where_value;
		int rc;

		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "bind graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "bind graph should be available") != 0 ||
		    expect_true(graph.has_dml != 0, "UPDATE graph should expose dml") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "UPDATE dml should be available") != 0 ||
		    expect_true(dml.assignments.count == 1U, "UPDATE should expose one assignment") != 0 ||
		    expect_true(graph.value_count == 1U, "UPDATE should expose one WHERE value") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "assignment should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &where_value, &error), &error, "WHERE value should be available") != 0 ||
		    expect_true(assignment.has_bind != 0 && strcmp(assignment.bind, cases[index].set_bind_key) == 0, "SET bind key mismatch") != 0 ||
		    expect_true(where_value.has_bind != 0 && strcmp(where_value.bind, cases[index].where_bind_key) == 0, "WHERE bind key mismatch") != 0 ||
		    expect_true(assignment.bind_kind == cases[index].set_bind_kind, "SET bind kind mismatch") != 0 ||
		    expect_true(where_value.bind_kind == cases[index].where_bind_kind, "WHERE bind kind mismatch") != 0 ||
		    expect_true(assignment.has_bind_position != 0 && assignment.bind_position == cases[index].set_bind_position, "SET bind position mismatch") != 0 ||
		    expect_true(where_value.has_bind_position != 0 && where_value.bind_position == cases[index].where_bind_position, "WHERE bind position mismatch") != 0 ||
		    expect_true(assignment.has_bind_sql != 0 && strcmp(assignment.bind_sql, cases[index].set_bind_sql) == 0, "SET bind SQL mismatch") != 0 ||
		    expect_true(where_value.has_bind_sql != 0 && strcmp(where_value.bind_sql, cases[index].where_bind_sql) == 0, "WHERE bind SQL mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

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
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t first;
		sqlparser_graph_value_t second;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_ORACLE;
		rc = sqlparser_parse_with_options("SELECT abc FROM table1 WHERE abc LIKE :1 AND def LIKE ?", &options, &handle, &error);
		if (expect_status_ok(rc, &error, "oracle mixed bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "oracle mixed bind graph should be available") != 0 ||
		    expect_true(graph.value_count == 2U, "oracle mixed bind should expose two values") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &first, &error), &error, "oracle first bind should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 1U, &second, &error), &error, "oracle second bind should be available") != 0 ||
		    expect_true(first.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && strcmp(first.bind, "1") == 0, "oracle :1 bind key mismatch") != 0 ||
		    expect_true(first.has_bind_position != 0 && first.bind_position == 1U, "oracle :1 bind position mismatch") != 0 ||
		    expect_true(first.has_bind_sql != 0 && strcmp(first.bind_sql, ":1") == 0, "oracle :1 bind SQL mismatch") != 0 ||
		    expect_true(second.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && strcmp(second.bind, "2") == 0, "oracle ? bind key mismatch") != 0 ||
		    expect_true(second.has_bind_position != 0 && second.bind_position == 2U, "oracle ? bind position mismatch") != 0 ||
		    expect_true(second.has_bind_sql != 0 && strcmp(second.bind_sql, "?") == 0, "oracle ? bind SQL mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_ORACLE;
		rc = sqlparser_parse_with_options(
			"SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD,WORKER, "
			"WEBSITE,MSDEPLOYPORT, \"UID\", PWD, KEY_ENCRYPTION, MODIFYTIME "
			"FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) "
			"WHERE RN > :startRow",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "oracle rownum pagination bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "oracle rownum pagination graph should be available") != 0 ||
		    expect_true(graph.relation_count >= 2U, "oracle rownum pagination should expose base and derived relations") != 0 ||
		    expect_true(graph.target_count >= 15U, "oracle rownum pagination should expose output targets") != 0 ||
		    expect_true(graph.value_count == 1U, "oracle rownum pagination should only expose field-bound values") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "oracle rownum field value should be available") != 0 ||
		    expect_true(value.bind_kind == SQLPARSER_BIND_KIND_NAMED && strcmp(value.bind, "startRow") == 0, "oracle startRow bind mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_SQLSERVER;
		rc = sqlparser_parse_with_options(
			"SELECT TOP (?) [id] FROM [dbo].[users] WHERE [name] LIKE ? ORDER BY [id]",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "sqlserver TOP bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "sqlserver TOP bind graph should be available") != 0 ||
		    expect_true(graph.value_count == 1U, "TOP bind should not enter field values") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "sqlserver WHERE value should be available") != 0 ||
		    expect_true(value.has_bind_position != 0 && value.bind_position == 2U, "WHERE bind should keep global position after TOP") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_assignment_t assignment;
		sqlparser_graph_value_t value;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_MYSQL;
		rc = sqlparser_parse_with_options("UPDATE users SET a = ? WHERE id = ?; UPDATE users SET b = ? WHERE id = ?", &options, &handle, &error);
		if (expect_status_ok(rc, &error, "multi-statement bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 1U, &graph, &error);
		if (expect_status_ok(rc, &error, "second statement graph should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "second statement dml should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "second statement assignment should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "second statement WHERE value should be available") != 0 ||
		    expect_true(assignment.has_bind_position != 0 && assignment.bind_position == 3U, "second SET bind position should be global") != 0 ||
		    expect_true(value.has_bind_position != 0 && value.bind_position == 4U, "second WHERE bind position should be global") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int expect_query_graph_value_bind(
	const sqlparser_query_graph_view_t *graph,
	size_t value_index,
	const char *column_name,
	sqlparser_bind_kind_t bind_kind,
	const char *bind_key,
	size_t bind_position,
	const char *bind_sql)
{
	sqlparser_error_t error;
	sqlparser_graph_value_t value;
	sqlparser_graph_field_t field;
	int rc;

	memset(&error, 0, sizeof(error));
	rc = sqlparser_query_graph_value_at(graph, value_index, &value, &error);
	if (expect_status_ok(rc, &error, "query graph value should be available") != 0 ||
	    expect_true(value.has_field != 0, "query graph value should be attached to a field") != 0 ||
	    expect_true(value.kind == SQLPARSER_GRAPH_VALUE_BIND, "query graph value should be a bind") != 0) {
		return 1;
	}
	rc = sqlparser_query_graph_field_at(graph, value.field_index, &field, &error);
	if (expect_status_ok(rc, &error, "query graph value field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, column_name) == 0, "query graph value field column mismatch") != 0 ||
	    expect_true(value.bind_kind == bind_kind, "query graph value bind kind mismatch") != 0 ||
	    expect_true(value.has_bind != 0 && strcmp(value.bind, bind_key) == 0, "query graph value bind key mismatch") != 0 ||
	    expect_true(value.has_bind_position != 0 && value.bind_position == bind_position, "query graph value bind position mismatch") != 0 ||
	    expect_true(value.has_bind_sql != 0 && strcmp(value.bind_sql, bind_sql) == 0, "query graph value bind SQL mismatch") != 0) {
		fprintf(stderr,
		        "value[%zu] column=%s bind_key=%s bind_sql=%s expected_sql=%s position=%zu expected_position=%zu\n",
		        value_index,
		        field.column_name != NULL ? field.column_name : "(null)",
		        value.has_bind ? value.bind : "(none)",
		        value.has_bind_sql ? value.bind_sql : "(none)",
		        bind_sql,
		        value.has_bind_position ? value.bind_position : 0U,
		        bind_position);
		return 1;
	}
	return 0;
}

static int expect_query_graph_select_value_binds(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t expected_value_count,
	const char *column_name,
	sqlparser_bind_kind_t bind_kind,
	const char *first_key,
	const char *second_key,
	const char *first_sql,
	const char *second_sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph value-list parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph value-list graph should be available") != 0 ||
	    expect_true(graph.value_count == expected_value_count, "query graph value-list count mismatch") != 0 ||
	    expect_query_graph_value_bind(&graph, 0U, column_name, bind_kind, first_key, 1U, first_sql) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expected_value_count > 1U &&
	    expect_query_graph_value_bind(&graph, 1U, column_name, bind_kind, second_key, 2U, second_sql) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_no_field_values(
	sqlparser_dialect_t dialect,
	const char *sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph no-value parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph no-value graph should be available") != 0 ||
	    expect_true(graph.value_count == 0U, "query graph should not expose projection-only bind as field value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_update_set_and_value_list(
	sqlparser_dialect_t dialect,
	const char *sql,
	sqlparser_bind_kind_t bind_kind,
	const char *set_key,
	const char *first_where_key,
	const char *second_where_key,
	const char *set_sql,
	const char *first_where_sql,
	const char *second_where_sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_assignment_t assignment;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph update value-list parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph update value-list graph should be available") != 0 ||
	    expect_true(graph.value_count == 2U, "query graph update WHERE should expose two values") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "query graph update dml should be available") != 0 ||
	    expect_true(dml.assignments.count == 1U, "query graph update should expose one assignment") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "query graph update assignment should be available") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_BIND, "query graph update assignment should be bind") != 0 ||
	    expect_true(assignment.bind_kind == bind_kind, "query graph update assignment bind kind mismatch") != 0 ||
	    expect_true(assignment.has_bind != 0 && strcmp(assignment.bind, set_key) == 0, "query graph update assignment bind key mismatch") != 0 ||
	    expect_true(assignment.has_bind_position != 0 && assignment.bind_position == 1U, "query graph update assignment bind position mismatch") != 0 ||
	    expect_true(assignment.has_bind_sql != 0 && strcmp(assignment.bind_sql, set_sql) == 0, "query graph update assignment bind SQL mismatch") != 0 ||
	    expect_query_graph_value_bind(&graph, 0U, "phone", bind_kind, first_where_key, 2U, first_where_sql) != 0 ||
	    expect_query_graph_value_bind(&graph, 1U, "phone", bind_kind, second_where_key, 3U, second_where_sql) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_condition_value_lists(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *select_in_sql;
		const char *select_not_in_sql;
		const char *select_between_sql;
		const char *select_func_sql;
		const char *select_case_sql;
		const char *update_sql;
		const char *delete_sql;
		sqlparser_bind_kind_t bind_kind;
		const char *key1;
		const char *key2;
		const char *key3;
		const char *sql1;
		const char *sql2;
		const char *sql3;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM public.users WHERE phone IN ($1, $2)",
			"SELECT id FROM public.users WHERE phone NOT IN ($1, $2)",
			"SELECT id FROM public.users WHERE created_at BETWEEN $1 AND $2",
			"SELECT id FROM public.users WHERE UPPER(email) = $1",
			"SELECT CASE WHEN phone = $1 THEN name ELSE email END AS v FROM public.users",
			"UPDATE public.users SET note = $1 WHERE phone IN ($2, $3)",
			"DELETE FROM public.users WHERE email IN ($1, $2)",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"1",
			"2",
			"3",
			"$1",
			"$2",
			"$3"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE phone IN (?, ?)",
			"SELECT id FROM users WHERE phone NOT IN (?, ?)",
			"SELECT id FROM users WHERE created_at BETWEEN ? AND ?",
			"SELECT id FROM users WHERE UPPER(email) = ?",
			"SELECT CASE WHEN phone = ? THEN name ELSE email END AS v FROM users",
			"UPDATE users SET note = ? WHERE phone IN (?, ?)",
			"DELETE FROM users WHERE email IN (?, ?)",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"1",
			"2",
			"3",
			"?",
			"?",
			"?"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE phone NOT IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE created_at BETWEEN :from_time AND :to_time",
			"SELECT id FROM users WHERE UPPER(email) = :email",
			"SELECT CASE WHEN phone = :phone1 THEN name ELSE email END AS v FROM users",
			"UPDATE users SET note = :note WHERE phone IN (:phone1, :phone2)",
			"DELETE FROM users WHERE email IN (:email1, :email2)",
			SQLPARSER_BIND_KIND_NAMED,
			"phone1",
			"phone2",
			"phone2",
			":phone1",
			":phone2",
			":phone2"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE [phone] IN (@phone1, @phone2)",
			"SELECT [id] FROM [dbo].[users] WHERE [phone] NOT IN (@phone1, @phone2)",
			"SELECT [id] FROM [dbo].[users] WHERE [created_at] BETWEEN @from_time AND @to_time",
			"SELECT [id] FROM [dbo].[users] WHERE UPPER([email]) = @email",
			"SELECT CASE WHEN [phone] = @phone1 THEN [name] ELSE [email] END AS [v] FROM [dbo].[users]",
			"UPDATE [dbo].[users] SET [note] = @note WHERE [phone] IN (@phone1, @phone2)",
			"DELETE FROM [dbo].[users] WHERE [email] IN (@email1, @email2)",
			SQLPARSER_BIND_KIND_NAMED,
			"phone1",
			"phone2",
			"phone2",
			"@phone1",
			"@phone2",
			"@phone2"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM users WHERE phone IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE phone NOT IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE created_at BETWEEN :from_time AND :to_time",
			"SELECT id FROM users WHERE UPPER(email) = :email",
			"SELECT CASE WHEN phone = :phone1 THEN name ELSE email END AS v FROM users",
			"UPDATE users SET note = :note WHERE phone IN (:phone1, :phone2)",
			"DELETE FROM users WHERE email IN (:email1, :email2)",
			SQLPARSER_BIND_KIND_NAMED,
			"phone1",
			"phone2",
			"phone2",
			":phone1",
			":phone2",
			":phone2"
		}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
	} non_predicate_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "SELECT phone + $1 AS v FROM public.users"},
		{SQLPARSER_DIALECT_MYSQL, "SELECT phone + ? AS v FROM users"},
		{SQLPARSER_DIALECT_ORACLE, "SELECT phone + :delta AS v FROM users"},
		{SQLPARSER_DIALECT_SQLSERVER, "SELECT [phone] + @delta AS [v] FROM [dbo].[users]"},
		{SQLPARSER_DIALECT_DAMENG, "SELECT phone + :delta AS v FROM users"}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_in_sql,
			    2U,
			    "phone",
			    cases[index].bind_kind,
			    cases[index].key1,
			    cases[index].key2,
			    cases[index].sql1,
			    cases[index].sql2) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_not_in_sql,
			    2U,
			    "phone",
			    cases[index].bind_kind,
			    cases[index].key1,
			    cases[index].key2,
			    cases[index].sql1,
			    cases[index].sql2) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_between_sql,
			    2U,
			    "created_at",
			    cases[index].bind_kind,
			    strcmp(cases[index].key1, "phone1") == 0 ? "from_time" : cases[index].key1,
			    strcmp(cases[index].key2, "phone2") == 0 ? "to_time" : cases[index].key2,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":from_time" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@from_time" : cases[index].sql1),
			    strcmp(cases[index].sql2, ":phone2") == 0 ? ":to_time" : (strcmp(cases[index].sql2, "@phone2") == 0 ? "@to_time" : cases[index].sql2)) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_func_sql,
			    1U,
			    "email",
			    cases[index].bind_kind,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "email" : cases[index].key1,
			    cases[index].key2,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":email" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@email" : cases[index].sql1),
			    cases[index].sql2) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_case_sql,
			    1U,
			    "phone",
			    cases[index].bind_kind,
			    cases[index].key1,
			    cases[index].key2,
			    cases[index].sql1,
			    cases[index].sql2) != 0 ||
		    expect_query_graph_update_set_and_value_list(
			    cases[index].dialect,
			    cases[index].update_sql,
			    cases[index].bind_kind,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "note" : cases[index].key1,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "phone1" : cases[index].key2,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "phone2" : cases[index].key3,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":note" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@note" : cases[index].sql1),
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? cases[index].sql1 : cases[index].sql2,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? cases[index].sql2 : cases[index].sql3) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].delete_sql,
			    2U,
			    "email",
			    cases[index].bind_kind,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "email1" : cases[index].key1,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "email2" : cases[index].key2,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":email1" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@email1" : cases[index].sql1),
			    strcmp(cases[index].sql2, ":phone2") == 0 ? ":email2" : (strcmp(cases[index].sql2, "@phone2") == 0 ? "@email2" : cases[index].sql2)) != 0) {
			return 1;
		}
	}
	for (index = 0U; index < sizeof(non_predicate_cases) / sizeof(non_predicate_cases[0]); index++) {
		if (expect_query_graph_no_field_values(
			    non_predicate_cases[index].dialect,
			    non_predicate_cases[index].sql) != 0) {
			return 1;
		}
	}
	return 0;
}

static int expect_query_graph_single_value_match_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_field_match_kind_t expected_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_value_t value;
	sqlparser_graph_field_t field;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph field-match parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph field-match graph should be available") != 0 ||
	    expect_true(graph.value_count == 1U, "query graph field-match should expose one value") != 0 ||
	    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "query graph field-match value should be available") != 0 ||
	    expect_true(value.has_field != 0, "query graph field-match value should be attached to a field") != 0 ||
	    expect_true(value.field_match_kind == expected_kind, "query graph field-match kind mismatch") != 0 ||
	    expect_true(strcmp(sqlparser_graph_field_match_kind_name(value.field_match_kind),
	                       sqlparser_graph_field_match_kind_name(expected_kind)) == 0,
	                "query graph field-match name mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, value.field_index, &field, &error), &error, "query graph field-match field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, column_name) == 0, "query graph field-match column mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_field_match_kind_semantics(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *direct_sql;
		const char *function_sql;
		const char *cast_sql;
		const char *expression_sql;
		const char *case_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM public.users WHERE secret = $1",
			"SELECT id FROM public.users WHERE UPPER(secret) = $1",
			"SELECT id FROM public.users WHERE CAST(secret AS text) = $1",
			"SELECT id FROM public.users WHERE secret || 'x' = $1",
			"SELECT id FROM public.users WHERE CASE WHEN 1 = 1 THEN secret END = $1"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE secret = ?",
			"SELECT id FROM users WHERE UPPER(secret) = ?",
			"SELECT id FROM users WHERE CAST(secret AS CHAR) = ?",
			"SELECT id FROM users WHERE CONCAT(secret, 'x') = ?",
			"SELECT id FROM users WHERE CASE WHEN 1 = 1 THEN secret END = ?"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE UPPER(SECRET) = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CAST(SECRET AS VARCHAR(32)) = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET || 'x' = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN 1 = 1 THEN SECRET END = :secret"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE UPPER([secret]) = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE CAST([secret] AS VARCHAR(32)) = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] + 'x' = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE CASE WHEN 1 = 1 THEN [secret] END = @secret"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE UPPER(secret) = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE CAST(secret AS VARCHAR(32)) = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret || 'x' = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN 1 = 1 THEN secret END = :secret"
		}
	};
	size_t index;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *view_json;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].direct_sql,
			    "secret",
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].function_sql,
			    "secret",
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].cast_sql,
			    "secret",
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].expression_sql,
			    "secret",
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].case_sql,
			    "secret",
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0) {
			return 1;
		}
	}
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(
		"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :plain_secret AND UPPER(SECRET) = :upper_secret",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "query graph field-match JSON parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "query graph field-match JSON export should succeed") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"field_match_kind\":\"direct_field\"") != NULL,
	                "view JSON should expose direct field match") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"field_match_kind\":\"expression_field\"") != NULL,
	                "view JSON should expose expression field match") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_column_value(
	const sqlparser_query_graph_view_t *graph,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind,
	int require_no_direct_bind)
{
	sqlparser_error_t error;
	size_t index;

	memset(&error, 0, sizeof(error));
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		sqlparser_graph_field_t field;
		int rc;

		rc = sqlparser_query_graph_value_at(graph, index, &value, &error);
		if (expect_status_ok(rc, &error, "query graph value scan should succeed") != 0) {
			return 1;
		}
		if (!value.has_field ||
		    value.kind != value_kind ||
		    value.field_match_kind != field_match_kind) {
			continue;
		}
		rc = sqlparser_query_graph_field_at(graph, value.field_index, &field, &error);
		if (expect_status_ok(rc, &error, "query graph value field scan should succeed") != 0) {
			return 1;
		}
		if (field.column_name == NULL || strcmp(field.column_name, column_name) != 0) {
			continue;
		}
		if (require_no_direct_bind &&
		    (value.has_bind || value.has_bind_sql || value.has_bind_position || value.has_selector)) {
			fprintf(stderr, "FAIL: expression value for %s must not expose direct bind or selector\n", column_name);
			return 1;
		}
		return 0;
	}
	fprintf(stderr,
	        "FAIL: query graph value not found: column=%s kind=%s field_match_kind=%s\n",
	        column_name,
	        sqlparser_graph_value_kind_name(value_kind),
	        sqlparser_graph_field_match_kind_name(field_match_kind));
	return 1;
}

static int expect_query_graph_column_value_absent(
	const sqlparser_query_graph_view_t *graph,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind)
{
	sqlparser_error_t error;
	size_t index;

	memset(&error, 0, sizeof(error));
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		sqlparser_graph_field_t field;
		int rc;

		rc = sqlparser_query_graph_value_at(graph, index, &value, &error);
		if (expect_status_ok(rc, &error, "query graph value scan should succeed") != 0) {
			return 1;
		}
		if (!value.has_field ||
		    value.kind != value_kind ||
		    value.field_match_kind != field_match_kind) {
			continue;
		}
		rc = sqlparser_query_graph_field_at(graph, value.field_index, &field, &error);
		if (expect_status_ok(rc, &error, "query graph value field scan should succeed") != 0) {
			return 1;
		}
		if (field.column_name != NULL && strcmp(field.column_name, column_name) == 0) {
			fprintf(stderr,
			        "FAIL: unexpected query graph value: column=%s kind=%s field_match_kind=%s\n",
			        column_name,
			        sqlparser_graph_value_kind_name(value_kind),
			        sqlparser_graph_field_match_kind_name(field_match_kind));
			return 1;
		}
	}
	return 0;
}

static int expect_query_graph_condition_value_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind,
	int require_no_direct_bind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph condition parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph condition graph should be available") != 0 ||
	    expect_query_graph_column_value(
		    &graph,
		    column_name,
		    value_kind,
		    field_match_kind,
		    require_no_direct_bind) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_condition_value_absent(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph condition parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph condition graph should be available") != 0 ||
	    expect_query_graph_column_value_absent(&graph, column_name, value_kind, field_match_kind) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_insert_cell_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	size_t fallback_column_ordinal,
	sqlparser_graph_value_kind_t value_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	size_t column_ordinal;
	size_t index;
	int found_column;
	int found_cell;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph insert parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph insert graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "query graph insert dml should be available") != 0 ||
	    expect_true(dml.kind == SQLPARSER_GRAPH_DML_INSERT, "query graph dml should be insert") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	column_ordinal = fallback_column_ordinal;
	found_column = column_name == NULL ? 1 : 0;
	for (index = 0U; !found_column && index < dml.target_columns.count; index++) {
		size_t column_index;
		sqlparser_graph_dml_column_t column;

		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.target_columns, index, &column_index, &error),
		                     &error,
		                     "query graph insert column span should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_column_at(&graph, column_index, &column, &error),
		                     &error,
		                     "query graph insert column should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (column.column_name != NULL && strcmp(column.column_name, column_name) == 0) {
			column_ordinal = column.ordinal;
			found_column = 1;
		}
	}
	if (!found_column) {
		fprintf(stderr, "FAIL: insert target column not found: %s\n", column_name);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	found_cell = 0;
	for (index = 0U; index < dml.rows.count; index++) {
		size_t cell_index;
		sqlparser_graph_dml_cell_t cell;

		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, index, &cell_index, &error),
		                     &error,
		                     "query graph insert row span should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error),
		                     &error,
		                     "query graph insert cell should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cell.column_ordinal == column_ordinal) {
			if (cell.kind != value_kind) {
				fprintf(stderr,
				        "FAIL: insert cell kind mismatch: expected=%s actual=%s\n",
				        sqlparser_graph_value_kind_name(value_kind),
				        sqlparser_graph_value_kind_name(cell.kind));
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (value_kind == SQLPARSER_GRAPH_VALUE_EXPRESSION &&
			    (cell.has_bind || cell.has_bind_sql || cell.has_bind_position)) {
				fprintf(stderr, "FAIL: expression insert cell must not expose direct bind\n");
				sqlparser_handle_destroy(handle);
				return 1;
			}
			found_cell = 1;
		}
	}
	if (!found_cell) {
		fprintf(stderr, "FAIL: insert cell ordinal not found: %zu\n", column_ordinal);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_update_assignment_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	size_t index;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph update graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "query graph update dml should be available") != 0 ||
	    expect_true(dml.kind == SQLPARSER_GRAPH_DML_UPDATE, "query graph dml should be update") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (index = 0U; index < dml.assignments.count; index++) {
		size_t assignment_index;
		sqlparser_graph_dml_assignment_t assignment;
		sqlparser_graph_field_t field;

		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.assignments, index, &assignment_index, &error),
		                     &error,
		                     "query graph update assignment span should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, assignment_index, &assignment, &error),
		                     &error,
		                     "query graph update assignment should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_field_at(&graph, assignment.target_field_index, &field, &error),
		                     &error,
		                     "query graph update target field should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.column_name == NULL || strcmp(field.column_name, column_name) != 0) {
			continue;
		}
		if (assignment.value_kind != value_kind) {
			fprintf(stderr,
			        "FAIL: update assignment kind mismatch: expected=%s actual=%s\n",
			        sqlparser_graph_value_kind_name(value_kind),
			        sqlparser_graph_value_kind_name(assignment.value_kind));
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (value_kind == SQLPARSER_GRAPH_VALUE_EXPRESSION &&
		    (assignment.has_bind || assignment.has_bind_sql || assignment.has_bind_position)) {
			fprintf(stderr, "FAIL: expression update assignment must not expose direct bind\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
		return 0;
	}
	fprintf(stderr, "FAIL: update target column not found: %s\n", column_name);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_query_graph_expression_field_value_semantics(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *field_case_sql;
		const char *field_multi_func_sql;
		const char *field_multi_operator_sql;
		const char *nested_field_sql;
		const char *value_func_sql;
		const char *value_operator_sql;
		const char *value_cast_sql;
		const char *value_case_sql;
		const char *insert_expr_sql;
		const char *insert_no_column_expr_sql;
		const char *update_expr_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM public.users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = $1",
			"SELECT id FROM public.users WHERE COALESCE(secret, id) = $1",
			"SELECT id FROM public.users WHERE secret || id = $1",
			"SELECT id FROM (SELECT id, secret FROM public.users) s WHERE UPPER(s.secret) = $1",
			"SELECT id FROM public.users WHERE secret = UPPER($1)",
			"SELECT id FROM public.users WHERE secret = $1 || 'x'",
			"SELECT id FROM public.users WHERE secret = CAST($1 AS text)",
			"SELECT id FROM public.users WHERE secret = CASE WHEN id = 1 THEN $1 END",
			"INSERT INTO public.users (id, secret) VALUES (1, UPPER($1))",
			"INSERT INTO public.users VALUES (1, UPPER($1))",
			"UPDATE public.users SET secret = UPPER($1) WHERE id = 1"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = ?",
			"SELECT id FROM users WHERE CONCAT(secret, id) = ?",
			"SELECT id FROM users WHERE secret + id = ?",
			"SELECT id FROM (SELECT id, secret FROM users) s WHERE UPPER(s.secret) = ?",
			"SELECT id FROM users WHERE secret = UPPER(?)",
			"SELECT id FROM users WHERE secret = CONCAT(?, 'x')",
			"SELECT id FROM users WHERE secret = CAST(? AS CHAR)",
			"SELECT id FROM users WHERE secret = CASE WHEN id = 1 THEN ? END",
			"INSERT INTO users (id, secret) VALUES (1, UPPER(?))",
			"INSERT INTO users VALUES (1, UPPER(?))",
			"UPDATE users SET secret = UPPER(?) WHERE id = 1"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN ID = 1 THEN SECRET ELSE BACKUP_SECRET END = :v",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE NVL(SECRET, ID) = :v",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET || ID = :v",
			"SELECT ID FROM (SELECT ID, SECRET FROM KDES.DBP_CRYPTO_TEST) s WHERE UPPER(s.SECRET) = :v",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = UPPER(:v)",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :v || 'x'",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = CAST(:v AS VARCHAR(32))",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = CASE WHEN ID = 1 THEN :v END",
			"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (1, UPPER(:v))",
			"INSERT INTO KDES.DBP_CRYPTO_TEST VALUES (1, UPPER(:v))",
			"UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = UPPER(:v) WHERE ID = 1"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE CASE WHEN [id] = 1 THEN [secret] ELSE [backup_secret] END = @v",
			"SELECT [id] FROM [dbo].[users] WHERE CONCAT([secret], [id]) = @v",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] + [id] = @v",
			"SELECT [id] FROM (SELECT [id], [secret] FROM [dbo].[users]) [s] WHERE UPPER([s].[secret]) = @v",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = UPPER(@v)",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = @v + 'x'",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = CAST(@v AS VARCHAR(32))",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = CASE WHEN [id] = 1 THEN @v END",
			"INSERT INTO [dbo].[users] ([id], [secret]) VALUES (1, UPPER(@v))",
			"INSERT INTO [dbo].[users] VALUES (1, UPPER(@v))",
			"UPDATE [dbo].[users] SET [secret] = UPPER(@v) WHERE [id] = 1"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = :v",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE NVL(secret, id) = :v",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret || id = :v",
			"SELECT id FROM (SELECT id, secret FROM KDES.DBP_CRYPTO_TEST) s WHERE UPPER(s.secret) = :v",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = UPPER(:v)",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = :v || 'x'",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = CAST(:v AS VARCHAR(32))",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = CASE WHEN id = 1 THEN :v END",
			"INSERT INTO KDES.DBP_CRYPTO_TEST (id, secret) VALUES (1, UPPER(:v))",
			"INSERT INTO KDES.DBP_CRYPTO_TEST VALUES (1, UPPER(:v))",
			"UPDATE KDES.DBP_CRYPTO_TEST SET secret = UPPER(:v) WHERE id = 1"
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_case_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_absent(
			    cases[index].dialect,
			    cases[index].field_case_sql,
			    "id",
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_multi_func_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_multi_func_sql,
			    "id",
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_multi_operator_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].nested_field_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_func_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_operator_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_cast_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_case_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_insert_cell_kind(
			    cases[index].dialect,
			    cases[index].insert_expr_sql,
			    "secret",
			    1U,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION) != 0 ||
		    expect_query_graph_insert_cell_kind(
			    cases[index].dialect,
			    cases[index].insert_no_column_expr_sql,
			    NULL,
			    1U,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION) != 0 ||
		    expect_query_graph_update_assignment_kind(
			    cases[index].dialect,
			    cases[index].update_expr_sql,
			    "secret",
			    SQLPARSER_GRAPH_VALUE_EXPRESSION) != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_query_graph_column_semantics_json(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *table_name;
	} dialect_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "SELECT name, UPPER(name), first_name || last_name FROM users WHERE id = 1 ORDER BY created_at", "users"},
		{SQLPARSER_DIALECT_MYSQL, "SELECT name, UPPER(name), CONCAT(first_name, last_name) FROM users WHERE id = ? ORDER BY created_at", "users"},
		{SQLPARSER_DIALECT_ORACLE, "SELECT name, UPPER(name), first_name || last_name FROM KDES.USERS WHERE id = :id ORDER BY created_at", "users"},
		{SQLPARSER_DIALECT_SQLSERVER, "SELECT [name], UPPER([name]), [first_name] + [last_name] FROM [dbo].[users] WHERE [id] = @id ORDER BY [created_at]", "users"},
		{SQLPARSER_DIALECT_DAMENG, "SELECT name, UPPER(name), first_name || last_name FROM KDES.USERS WHERE id = :id ORDER BY created_at", "users"}
	};
	json_t *root;
	json_t *statement;
	json_t *column;
	json_t *graph;
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
	graph = json_object_get(statement, "query_graph");
	column = find_view_column_json(statement, "users", "name", "select", 0U);
	if (expect_true(column != NULL, "direct SELECT field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "direct SELECT field target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "name", "select", 1U);
	if (expect_true(column != NULL, "function SELECT field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 1U), "function target_path should contain one entry") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "UPPER", 0), "function target_path should be UPPER") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "first_name", "select", 0U);
	if (expect_true(column != NULL, "expression first field should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "||", 0), "expression first target_path should be || arg 0") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "last_name", "select", 0U);
	if (expect_true(column != NULL, "expression second field should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "||", 1), "expression second target_path should be || arg 1") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "state", "select", 0U);
	if (expect_true(column != NULL, "CASE expression condition field should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "case_when", 0), "CASE target_path should start with case_when") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "where", 0U);
	if (expect_true(column != NULL, "WHERE field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "WHERE target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "created_at", "order", 0U);
	if (expect_true(column != NULL, "ORDER BY field should exist") != 0) {
		json_decref(root);
		return 1;
	}
		{
			char *graph_text;
			int has_star;

			graph_text = json_dumps(graph, JSON_COMPACT);
			has_star = graph_text != NULL && strstr(graph_text, "\"kind\":\"star\"") != NULL;
			free(graph_text);
			if (expect_true(has_star, "query_graph should expose star target") != 0) {
				json_decref(root);
				return 1;
			}
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
	column = find_view_column_json(statement, "users", "id", "on", 0U);
	if (expect_true(column != NULL, "JOIN ON left field should exist") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "orders", "user_id", "on", 0U);
	if (expect_true(column != NULL, "JOIN ON right field should exist") != 0) {
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
	if (expect_true(column != NULL, "GROUP BY field should exist") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "having", 0U);
	if (expect_true(column != NULL, "HAVING field should exist") != 0) {
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
	if (view_json_parse_statement(SQLPARSER_DIALECT_POSTGRESQL, "SELECT DISTINCT LOW(UPPER(name)) FROM table1", &root, &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "table1", "name", "select", 0U);
	if (expect_true(column != NULL, "distinct nested function field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 2U), "distinct nested function should expose full target_path") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "LOW", 0), "distinct nested function outer path should be LOW") != 0 ||
	    expect_true(json_target_path_entry_is(column, 1U, "function", "UPPER", 0), "distinct nested function inner path should be UPPER") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	if (expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT UPPER(name) || '_x' FROM users", "users", "name", "select", "expression", "||", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT COALESCE(name, fallback_name) FROM users", "users", "name", "select", "function", "COALESCE", 1, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT COALESCE(name, fallback_name) FROM users", "users", "fallback_name", "select", "function", "COALESCE", 1, 1) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT CAST(age AS text) FROM users", "users", "age", "select", "function", "CAST", 1, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT GREATEST(age, score) FROM users", "users", "score", "select", "function", "GREATEST", 1, 1) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT name COLLATE \"C\" FROM users", "users", "name", "select", "expression", "collate", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT ARRAY[id, age] FROM users", "users", "id", "select", "expression", "array", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT ROW(id, age) FROM users", "users", "age", "select", "expression", "row", 1, 1) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT name IS NULL FROM users", "users", "name", "select", "expression", "is_null", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT active IS TRUE FROM users", "users", "active", "select", "expression", "boolean_test", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users", "users", "amount", "select", "function", "SUM", 1, 0) != 0) {
		return 1;
	}

	for (index = 0U; index < sizeof(dialect_cases) / sizeof(dialect_cases[0]); index++) {
		root = NULL;
		statement = NULL;
		if (view_json_parse_statement(dialect_cases[index].dialect, dialect_cases[index].sql, &root, &statement) != 0) {
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "name", "select", 0U);
		if (expect_true(column != NULL, "dialect direct field should exist") != 0) {
			json_decref(root);
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "name", "select", 1U);
		if (expect_true(column != NULL, "dialect function field should exist") != 0 ||
		    expect_true(json_target_path_entry_is(column, 0U, "function", "UPPER", 0), "dialect function target_path should be UPPER") != 0) {
			json_decref(root);
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "id", "where", 0U);
		if (expect_true(column != NULL, "dialect WHERE field should exist") != 0) {
			json_decref(root);
			return 1;
		}
		json_decref(root);
	}

	return 0;
}

static int test_query_graph_public_struct_semantics(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_field_t field;
	sqlparser_graph_value_t value;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	size_t cell_index;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options("SELECT UPPER(name) FROM users WHERE id = :id", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph struct semantic parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph should be available") != 0 ||
	    expect_true(graph.relation_count == 1U, "query graph should expose one relation") != 0 ||
	    expect_true(graph.field_count >= 2U, "query graph should expose SELECT and WHERE fields") != 0 ||
	    expect_true(graph.value_count == 1U, "query graph should expose one WHERE value") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 0U, &relation, &error), &error, "relation should be available") != 0 ||
	    expect_true(strcmp(relation.object_name, "users") == 0, "relation table should be users") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, 0U, &field, &error), &error, "SELECT field should be available") != 0 ||
	    expect_true(strcmp(field.column_name, "name") == 0, "SELECT field should be name") != 0 ||
	    expect_true(field.has_target != 0, "SELECT field should point to target") != 0 ||
	    expect_true(field.target_path_count == 1U, "SELECT function field target_path count mismatch") != 0 ||
	    expect_true(strcmp(field.target_path[0].kind, "function") == 0, "SELECT function target_path kind mismatch") != 0 ||
	    expect_true(strcmp(field.target_path[0].name, "UPPER") == 0, "SELECT function target_path name mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "WHERE value should be available") != 0 ||
	    expect_true(value.has_bind != 0 && strcmp(value.bind, "id") == 0, "WHERE bind key mismatch") != 0 ||
	    expect_true(value.bind_kind == SQLPARSER_BIND_KIND_NAMED, "WHERE bind kind mismatch") != 0 ||
	    expect_true(value.has_bind_position != 0 && value.bind_position == 1U, "WHERE bind position mismatch") != 0 ||
	    expect_true(value.has_bind_sql != 0 && strcmp(value.bind_sql, ":id") == 0, "WHERE bind SQL mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse_with_options("INSERT INTO users (id, name) VALUES (:id, ?)", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph insert bind parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert dml should be available") != 0 ||
	    expect_true(dml.rows.count == 2U, "insert should expose two cells") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 0U, &cell_index, &error), &error, "first insert cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "first insert cell should be available") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "id") == 0, "insert named bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED, "insert named bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "insert named bind position mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 1U, &cell_index, &error), &error, "second insert cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "second insert cell should be available") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "2") == 0, "insert positional bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL, "insert positional bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 2U, "insert positional bind position mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_target_value(
	const sqlparser_query_graph_view_t *graph,
	size_t target_index,
	sqlparser_graph_target_kind_t target_kind,
	sqlparser_graph_value_kind_t value_kind,
	const char *bind_key,
	sqlparser_bind_kind_t bind_kind,
	size_t bind_position,
	const char *bind_sql,
	sqlparser_literal_kind_t literal_kind,
	const char *literal_string,
	long long literal_integer)
{
	sqlparser_error_t error;
	sqlparser_graph_target_t target;
	sqlparser_graph_value_t value;
	int rc;

	memset(&error, 0, sizeof(error));
	rc = sqlparser_query_graph_target_at(graph, target_index, &target, &error);
	if (expect_status_ok(rc, &error, "target should be available") != 0 ||
	    expect_true(target.kind == target_kind, "target kind mismatch") != 0 ||
	    expect_true(target.has_value != 0, "target should reference a value") != 0) {
		return 1;
	}
	rc = sqlparser_query_graph_value_at(graph, target.value_index, &value, &error);
	if (expect_status_ok(rc, &error, "target value should be available") != 0 ||
	    expect_true(value.kind == value_kind, "target value kind mismatch") != 0 ||
	    expect_true(value.clause == SQLPARSER_CLAUSE_KIND_SELECT_LIST, "target value clause mismatch") != 0 ||
	    expect_true(value.has_field == 0, "target value should not be field-bound") != 0) {
		return 1;
	}
	if (value_kind == SQLPARSER_GRAPH_VALUE_BIND) {
		if (expect_true(value.has_bind != 0 && strcmp(value.bind, bind_key) == 0, "target bind key mismatch") != 0 ||
		    expect_true(value.bind_kind == bind_kind, "target bind kind mismatch") != 0 ||
		    expect_true(value.has_bind_position != 0 && value.bind_position == bind_position, "target bind position mismatch") != 0 ||
		    expect_true(value.has_bind_sql != 0 && strcmp(value.bind_sql, bind_sql) == 0, "target bind SQL mismatch") != 0) {
			return 1;
		}
	} else if (value_kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
		if (expect_true(value.literal.kind == literal_kind, "target literal kind mismatch") != 0) {
			return 1;
		}
		if (literal_kind == SQLPARSER_LITERAL_KIND_STRING &&
		    expect_true(value.literal.string_value != NULL && strcmp(value.literal.string_value, literal_string) == 0, "target literal string mismatch") != 0) {
			return 1;
		}
		if (literal_kind == SQLPARSER_LITERAL_KIND_INTEGER &&
		    expect_true(value.literal.integer_value == literal_integer, "target literal integer mismatch") != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_insert_select_target_values(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *verify_handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_set_t set_item;
	sqlparser_bind_value_t bind;
	sqlparser_literal_value_t literal;
	sqlparser_patch_t patches[3];
	sqlparser_patch_list_t patch_list;
	char *sql;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	verify_handle = NULL;
	sql = NULL;

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT 960001, 'a' FROM DUAL UNION ALL SELECT 960002, 'b' FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select literal parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select literal graph should be available") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select literal should expose four source targets") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select literal should expose four target values") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select literal dml should be available") != 0 ||
	    expect_true(dml.has_source_block != 0, "insert-select literal should expose source block") != 0 ||
	    expect_query_graph_target_value(&graph, 0U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 960001LL) != 0 ||
	    expect_query_graph_target_value(&graph, 1U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_STRING, "a", 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 2U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 960002LL) != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_STRING, "b", 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT 960001, 'a' FROM DUAL UNION SELECT 960002, 'b' FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select UNION parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select UNION graph should be available") != 0 ||
	    expect_true(graph.set_count == 1U, "insert-select UNION should expose one set") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "insert-select UNION set should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_UNION, "insert-select UNION set kind mismatch") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "insert-select UNION branch count mismatch") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select UNION source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select UNION source value count mismatch") != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_STRING, "b", 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT :1, :2 FROM DUAL INTERSECT SELECT :3, :4 FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select INTERSECT parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select INTERSECT graph should be available") != 0 ||
	    expect_true(graph.set_count == 1U, "insert-select INTERSECT should expose one set") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "insert-select INTERSECT set should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_INTERSECT, "insert-select INTERSECT set kind mismatch") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "insert-select INTERSECT branch count mismatch") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select INTERSECT source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select INTERSECT source value count mismatch") != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "4", SQLPARSER_BIND_KIND_POSITIONAL, 4U, ":4", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT :id1, :secret1 FROM DUAL MINUS SELECT :id2, :secret2 FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select MINUS parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS graph should be available") != 0 ||
	    expect_true(graph.set_count == 1U, "insert-select MINUS should expose one set") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "insert-select MINUS set should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_EXCEPT, "insert-select MINUS set kind mismatch") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "insert-select MINUS branch count mismatch") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select MINUS source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select MINUS source value count mismatch") != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "secret2", SQLPARSER_BIND_KIND_NAMED, 4U, ":secret2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patches, 0, sizeof(patches));
	memset(&bind, 0, sizeof(bind));
	memset(&literal, 0, sizeof(literal));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "name_copy_right";
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "left-copy";
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 2U;
	patches[0].name = "NAME_COPY";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].select_targets[0]";
	patches[1].index = 2U;
	patches[1].literal = &literal;
	patches[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[2].selector = "stmt[0].select_targets[1]";
	patches[2].index = 2U;
	patches[2].bind = &bind;
	patch_list.items = patches;
	patch_list.count = 3U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS structured target patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS structured target deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "MINUS") != NULL, "insert-select MINUS should remain MINUS after patch") != 0 ||
	    expect_true(strstr(sql, "'left-copy'") != NULL, "insert-select MINUS patched literal missing") != 0 ||
	    expect_true(strstr(sql, ":name_copy_right") != NULL, "insert-select MINUS patched bind missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_parse_with_options(sql, &options, &verify_handle, &error);
	sqlparser_string_free(sql);
	sql = NULL;
	if (expect_status_ok(rc, &error, "insert-select MINUS patched SQL should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(verify_handle);
	verify_handle = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS patched graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select MINUS patched dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 3U, "insert-select MINUS patched target column count mismatch") != 0 ||
	    expect_true(graph.target_count == 6U, "insert-select MINUS patched source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 6U, "insert-select MINUS patched source value count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT :1, :2 FROM DUAL UNION ALL SELECT :id2, :secret2 FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select bind parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select bind graph should be available") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select bind should expose four source targets") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select bind should expose four target values") != 0 ||
	    expect_query_graph_target_value(&graph, 0U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "1", SQLPARSER_BIND_KIND_POSITIONAL, 1U, ":1", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 1U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "2", SQLPARSER_BIND_KIND_POSITIONAL, 2U, ":2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 2U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "id2", SQLPARSER_BIND_KIND_NAMED, 3U, ":id2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "secret2", SQLPARSER_BIND_KIND_NAMED, 4U, ":secret2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patches, 0, sizeof(patches));
	memset(&bind, 0, sizeof(bind));
	memset(&literal, 0, sizeof(literal));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_copy1";
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "copy2";
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 2U;
	patches[0].name = "SECRET_COPY";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].select_targets[0]";
	patches[1].index = 2U;
	patches[1].bind = &bind;
	patches[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[2].selector = "stmt[0].select_targets[1]";
	patches[2].index = 2U;
	patches[2].literal = &literal;
	patch_list.items = patches;
	patch_list.count = 3U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "insert-select structured target patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "insert-select structured target deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "secret_copy") != NULL || strstr(sql, "SECRET_COPY") != NULL, "insert-select patched target column missing") != 0 ||
	    expect_true(strstr(sql, ":secret_copy1") != NULL, "insert-select patched bind missing") != 0 ||
	    expect_true(strstr(sql, "'copy2'") != NULL, "insert-select patched literal missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_parse_with_options(sql, &options, &verify_handle, &error);
	sqlparser_string_free(sql);
	sql = NULL;
	if (expect_status_ok(rc, &error, "insert-select patched SQL should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(verify_handle);
	verify_handle = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select patched graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select patched dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 3U, "insert-select patched target column count mismatch") != 0 ||
	    expect_true(graph.target_count == 6U, "insert-select patched source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 6U, "insert-select patched source value count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 3U;
	patches[0].name = "ID_COPY";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].select_targets[0]";
	patches[1].index = 3U;
	patches[1].source_selector = "stmt[0].select_target[0][0]";
	patches[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[2].selector = "stmt[0].select_targets[1]";
	patches[2].index = 3U;
	patches[2].source_selector = "stmt[0].select_target[1][0]";
	patch_list.items = patches;
	patch_list.count = 3U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "insert-select source target clone patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select cloned graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select cloned dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 4U, "insert-select cloned target column count mismatch") != 0 ||
	    expect_true(graph.target_count == 8U, "insert-select cloned source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 8U, "insert-select cloned source value count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_oracle_multi_insert_query_graph_and_patch(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_target_t target;
	sqlparser_graph_field_t field;
	sqlparser_bind_value_t bind;
	sqlparser_literal_value_t literal;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *sql;
	size_t index;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT ALL "
		"INTO KDES.t1 (id, secret) VALUES (1, 'a') "
		"INTO KDES.t2 (id, phone) VALUES (2, :phone2) "
		"SELECT 1 FROM dual",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT ALL dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_ALL, "Oracle INSERT ALL mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Oracle INSERT ALL branch count mismatch") != 0 ||
	    expect_true(dml.has_source_block != 0, "Oracle INSERT ALL source block missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_span_index_at(&graph, dml.branches, 1U, &index, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL second branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT ALL second branch should be available") != 0 ||
	    expect_true(branch.ordinal == 1U, "Oracle INSERT ALL second branch ordinal mismatch") != 0 ||
	    expect_true(branch.target_columns.count == 2U, "Oracle INSERT ALL second branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 2U, "Oracle INSERT ALL second branch cell count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, branch.target_relation_index, &relation, &error), &error, "Oracle INSERT ALL second relation should be available") != 0 ||
	    expect_true(relation.schema_name != NULL && strcmp(relation.schema_name, "KDES") == 0, "Oracle INSERT ALL second relation schema mismatch") != 0 ||
	    expect_true(relation.object_name != NULL && strcmp(relation.object_name, "t2") == 0, "Oracle INSERT ALL second relation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL bind cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle INSERT ALL bind cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "Oracle INSERT ALL bind cell kind mismatch") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "phone2") == 0, "Oracle INSERT ALL named bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED, "Oracle INSERT ALL named bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "Oracle INSERT ALL bind position mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&bind, 0, sizeof(bind));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_new";
	rc = sqlparser_insert_set_cell_bind(handle, 0U, 0U, 1U, &bind, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch bind replacement should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&literal, 0, sizeof(literal));
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "phone-new";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 1U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch literal replacement should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL patched deparse should succeed") != 0 ||
	    expect_true(strstr(sql, ":secret_new") != NULL, "Oracle INSERT ALL patched bind missing") != 0 ||
	    expect_true(strstr(sql, "'phone-new'") != NULL, "Oracle INSERT ALL patched literal missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	memset(&patch, 0, sizeof(patch));
	memset(&patch_list, 0, sizeof(patch_list));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_branch_columns[0]";
	patch.index = 2U;
	patch.name = "secret_copy";
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_copy";
	patch.bind = &bind;
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch column insertion should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch column deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "secret_copy") != NULL, "Oracle INSERT ALL inserted branch column missing") != 0 ||
	    expect_true(strstr(sql, ":secret_copy") != NULL, "Oracle INSERT ALL inserted branch bind missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL patched graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT ALL patched dml should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT ALL patched first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT ALL patched first branch should be available") != 0 ||
	    expect_true(branch.target_columns.count == 3U, "Oracle INSERT ALL patched branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 3U, "Oracle INSERT ALL patched branch cell count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_branch_columns[0]";
	patch.index = 3U;
	patch.name = "secret_clone";
	patch.source_selector = "stmt[0].insert_cell[0][1]";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch cell clone should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL cloned graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT ALL cloned dml should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT ALL cloned first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT ALL cloned first branch should be available") != 0 ||
	    expect_true(branch.target_columns.count == 4U, "Oracle INSERT ALL cloned branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 4U, "Oracle INSERT ALL cloned branch cell count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT ALL "
		"WHEN flag = 1 THEN INTO t1 (id, flag_copy) VALUES (:1, flag) "
		"WHEN flag = 2 THEN INTO t2 (id, flag_copy) VALUES (:2, flag) "
		"SELECT flag FROM src",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle conditional INSERT ALL parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle conditional INSERT ALL graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle conditional INSERT ALL dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_ALL, "Oracle conditional INSERT ALL mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Oracle conditional INSERT ALL branch count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle conditional INSERT ALL first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle conditional INSERT ALL first branch should be available") != 0 ||
	    expect_true(branch.has_condition_selector != 0, "Oracle conditional INSERT ALL condition selector missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 0U, &index, &error), &error, "Oracle conditional INSERT ALL bind cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle conditional INSERT ALL bind cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "Oracle conditional INSERT ALL bind cell kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "Oracle conditional INSERT ALL bind position mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error), &error, "Oracle conditional INSERT ALL source cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle conditional INSERT ALL source cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle conditional INSERT ALL source cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Oracle conditional INSERT ALL source target missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_selector_clause_sql(handle, &branch.condition_selector, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle conditional INSERT ALL condition selector should read SQL") != 0 ||
	    expect_true(sql != NULL && strcmp(sql, "flag = 1") == 0, "Oracle conditional INSERT ALL condition SQL mismatch") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN amount > 100 THEN INTO big_orders (id, amount) VALUES (order_id, amount) "
		"ELSE INTO small_orders (id, amount) VALUES (order_id, amount) "
		"SELECT id AS order_id, amount FROM orders",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST direct source fields should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST direct source graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT FIRST direct source dml should be available") != 0 ||
	    expect_true(dml.has_source_block != 0, "Oracle INSERT FIRST direct source block missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT FIRST direct source branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT FIRST direct source branch should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 0U, &index, &error), &error, "Oracle INSERT FIRST direct source first cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle INSERT FIRST direct source first cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle INSERT FIRST direct source first cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Oracle INSERT FIRST direct source first cell target missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, cell.source_target_index, &target, &error), &error, "Oracle INSERT FIRST direct source first target should be available") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_FIELD, "Oracle INSERT FIRST direct source first target kind mismatch") != 0 ||
	    expect_true(target.output_name != NULL && strcmp(target.output_name, "order_id") == 0, "Oracle INSERT FIRST direct source first target name mismatch") != 0 ||
	    expect_true(target.has_field != 0, "Oracle INSERT FIRST direct source first target field missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error, "Oracle INSERT FIRST direct source first field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "id") == 0, "Oracle INSERT FIRST direct source first field mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error), &error, "Oracle INSERT FIRST direct source second cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle INSERT FIRST direct source second cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle INSERT FIRST direct source second cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Oracle INSERT FIRST direct source second cell target missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, cell.source_target_index, &target, &error), &error, "Oracle INSERT FIRST direct source second target should be available") != 0 ||
	    expect_true(target.has_field != 0, "Oracle INSERT FIRST direct source second target field missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error, "Oracle INSERT FIRST direct source second field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "amount") == 0, "Oracle INSERT FIRST direct source second field mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN flag = 1 THEN INTO t1 (id, secret) VALUES (:1, :2) "
		"WHEN flag = 2 THEN INTO t2 (id, phone) VALUES (:3, :4) "
		"SELECT flag FROM src",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT FIRST dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_FIRST, "Oracle INSERT FIRST mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Oracle INSERT FIRST branch count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT FIRST first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT FIRST first branch should be available") != 0 ||
	    expect_true(branch.has_condition_selector != 0, "Oracle INSERT FIRST condition selector missing") != 0 ||
	    expect_true(branch.condition_selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION, "Oracle INSERT FIRST condition selector kind mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_selector_clause_sql(handle, &branch.condition_selector, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST condition selector should read SQL") != 0 ||
	    expect_true(sql != NULL && strcmp(sql, "flag = 1") == 0, "Oracle INSERT FIRST condition SQL mismatch") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_attribution_and_values(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_relation_t users_relation;
	sqlparser_graph_relation_t orders_relation;
	sqlparser_graph_field_t field;
	sqlparser_graph_value_t value;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_assignment_t assignment;
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
	selector_text = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph attribution parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph attribution should be available") != 0 ||
	    expect_true(graph.relation_count == 2U, "join should expose two relations") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 0U, &users_relation, &error), &error, "users relation should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 1U, &orders_relation, &error), &error, "orders relation should be available") != 0 ||
	    expect_true(strcmp(users_relation.schema_name, "app") == 0, "users schema should be app") != 0 ||
	    expect_true(strcmp(users_relation.object_name, "users") == 0, "users table should be users") != 0 ||
	    expect_true(strcmp(users_relation.alias_name, "u") == 0, "users alias should be u") != 0 ||
	    expect_true(strcmp(orders_relation.schema_name, "sales") == 0, "orders schema should be sales") != 0 ||
	    expect_true(strcmp(orders_relation.object_name, "orders") == 0, "orders table should be orders") != 0 ||
	    expect_true(strcmp(orders_relation.alias_name, "o") == 0, "orders alias should be o") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	saw_users_id = 0;
	saw_order_no = 0;
	saw_order_status = 0;
	for (index = 0U; index < graph.field_count; index++) {
		rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
		if (expect_status_ok(rc, &error, "field should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.has_relation && field.relation_index == 0U && strcmp(field.column_name, "id") == 0) {
			saw_users_id = 1;
		}
		if (field.has_relation && field.relation_index == 1U && strcmp(field.column_name, "order_no") == 0) {
			saw_order_no = 1;
		}
		if (field.has_relation && field.relation_index == 1U && strcmp(field.column_name, "status") == 0) {
			saw_order_status = 1;
		}
	}
	if (expect_true(saw_users_id != 0, "users relation should include id field") != 0 ||
	    expect_true(saw_order_no != 0, "orders relation should include order_no field") != 0 ||
	    expect_true(saw_order_status != 0, "orders relation should include status field") != 0 ||
	    expect_true(graph.value_count == 1U, "WHERE literal should expose one graph value") != 0 ||
	    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "WHERE literal value should be available") != 0 ||
	    expect_true(value.kind == SQLPARSER_GRAPH_VALUE_LITERAL, "WHERE value should be literal") != 0 ||
	    expect_true(value.literal.kind == SQLPARSER_LITERAL_KIND_STRING, "WHERE literal kind should be string") != 0 ||
	    expect_true(strcmp(value.literal.string_value, "paid") == 0, "WHERE literal value mismatch") != 0 ||
	    expect_true(value.has_selector != 0 && value.selector.kind == SQLPARSER_SELECTOR_KIND_VALUE, "WHERE value selector should be value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_format(&value.selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "WHERE value selector should format") != 0) {
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
	if (expect_status_ok(rc, &error, "WHERE value patch should succeed") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "WHERE value patched deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'done'") != NULL, "WHERE value patch should appear in deparse") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_text);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse("UPDATE users SET name = 'bob' WHERE id = 1", &handle, &error);
	if (expect_status_ok(rc, &error, "query graph update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph update should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "update dml should be available") != 0 ||
	    expect_true(dml.assignments.count == 1U, "update should expose one assignment") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "update assignment should be available") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_LITERAL, "update assignment should carry literal") != 0 ||
	    expect_true(assignment.literal.kind == SQLPARSER_LITERAL_KIND_STRING, "update assignment literal should be string") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "bob") == 0, "update assignment literal mismatch") != 0 ||
	    expect_true(assignment.has_selector != 0 && assignment.selector.kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT, "update assignment selector should replace assignment") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_select_target_list_patch_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_target_t target;
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

	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "select graph should be available") != 0 ||
	    expect_true(graph.target_count == 1U, "select star should expose one graph target") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, 0U, &target, &error), &error, "select star target should be available") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_STAR, "select star target kind mismatch") != 0 ||
	    expect_true(target.has_target_list_selector != 0, "select star should expose target list selector") != 0 ||
	    expect_true(target.has_selector != 0, "select star should expose target selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_format(&target.target_list_selector, &selector_text, &error);
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
	    expect_true(strstr(view_json, "\"selector\":\"stmt[0].select_target[0][0]\"") != NULL,
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

static int test_structured_select_target_column_replacement(void)
{
	static const char *pg_id[] = {"id"};
	static const char *pg_name[] = {"name"};
	static const char *pg_phone[] = {"phone"};
	static const char *qualified_id[] = {"u", "id"};
	static const char *qualified_name[] = {"u", "name"};
	static const char *qualified_phone[] = {"u", "phone"};
	static const char *mysql_reserved[] = {"select"};
	static const char *mysql_cn[] = {"中文列"};
	static const char *mysql_space[] = {"has space"};
	static const char *mysql_case[] = {"CaseSensitive"};
	static const char *sqlserver_schema_id[] = {"dbo", "users", "id"};
	static const char *sqlserver_schema_phone[] = {"dbo", "users", "phone"};
	static const char *oracle_outer_id[] = {"d", "id"};
	static const char *oracle_outer_phone[] = {"d", "phone"};
	static const char *view_id[] = {"id"};
	static const char *view_phone[] = {"phone"};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
		const char *sql;
		sqlparser_identifier_path_view_t columns[4];
		size_t column_count;
		size_t expected_target_count;
		const char *must_contain;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-star",
			"SELECT * FROM public.users",
			{{pg_id, 1U}, {pg_name, 1U}, {pg_phone, 1U}, {NULL, 0U}},
			3U,
			3U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-qualified-star",
			"SELECT u.* FROM public.users u",
			{{qualified_id, 2U}, {qualified_name, 2U}, {qualified_phone, 2U}, {NULL, 0U}},
			3U,
			3U,
			"u.phone"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-join-qualified-star",
			"SELECT u.*, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id",
			{{qualified_id, 2U}, {qualified_name, 2U}, {qualified_phone, 2U}, {NULL, 0U}},
			3U,
			4U,
			"o.order_no"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-derived-nested-star",
			"SELECT * FROM (SELECT * FROM (SELECT id, phone FROM users) x) y",
			{{pg_id, 1U}, {pg_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-union-wrapper-star",
			"SELECT * FROM (SELECT id, phone FROM users UNION ALL SELECT id, phone FROM archived_users) u",
			{{pg_id, 1U}, {pg_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"UNION ALL"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-view-star",
			"SELECT * FROM user_view",
			{{view_id, 1U}, {view_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"mysql-identifier-boundaries",
			"SELECT * FROM `users`",
			{{mysql_reserved, 1U}, {mysql_cn, 1U}, {mysql_space, 1U}, {mysql_case, 1U}},
			4U,
			4U,
			"CaseSensitive"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"oracle-rownum-wrapper-star",
			"SELECT * FROM (SELECT ROWNUM rn, u.* FROM users u) d",
			{{oracle_outer_id, 2U}, {oracle_outer_phone, 2U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"d.phone"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"sqlserver-schema-qualified",
			"SELECT * FROM [dbo].[users] ORDER BY [id] OFFSET 0 ROWS",
			{{sqlserver_schema_id, 3U}, {sqlserver_schema_phone, 3U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"dameng-star",
			"SELECT * FROM users",
			{{pg_id, 1U}, {pg_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	char *deparsed_sql;
	char *target_sql;
	size_t target_count;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		deparsed_sql = NULL;
		target_sql = NULL;
		memset(&error, 0, sizeof(error));
		memset(&selector, 0, sizeof(selector));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "structured select parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_selector_parse("stmt[0].select_target[0][0]", &selector, &error);
		if (expect_status_ok(rc, &error, "structured select target selector should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_selector_replace_select_target_with_columns(
			handle,
			&selector,
			cases[index].columns,
			cases[index].column_count,
			&error);
		if (expect_status_ok(rc, &error, "structured select replacement should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "structured select replacement should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
		if (expect_status_ok(rc, &error, "structured select target count should succeed") != 0 ||
		    expect_true(target_count == cases[index].expected_target_count,
		                "structured select target count mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
		if (expect_status_ok(rc, &error, "structured select first target SQL should succeed") != 0 ||
		    expect_true(target_sql != NULL && strcmp(target_sql, "*") != 0,
		                "structured select first target should not remain star") != 0) {
			sqlparser_string_free(target_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(target_sql);
		target_sql = NULL;

		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "structured select deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].must_contain) != NULL,
		                "structured select deparse should contain expected text") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_query_graph_set_operation_attribution(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_set_t set_item;
	sqlparser_graph_field_t field;
	char *view_json;
	size_t index;
	int saw_users;
	int saw_archived_users;
	int saw_order_field;
	int rc;

	sql = "SELECT u.id FROM users u UNION ALL SELECT a.id FROM archived_users a ORDER BY id";
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "set operation parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "set operation graph should be available") != 0 ||
	    expect_true(graph.relation_count == 2U, "set operation should expose both table relations") != 0 ||
	    expect_true(graph.set_count == 1U, "set operation should expose one set node") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "set node should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_UNION_ALL, "set kind should be UNION ALL") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "set operation should expose two branch blocks") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	saw_users = 0;
	saw_archived_users = 0;
	for (index = 0U; index < graph.relation_count; index++) {
		rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
		if (expect_status_ok(rc, &error, "set relation should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (relation.object_name != NULL && strcmp(relation.object_name, "users") == 0) {
			saw_users = 1;
		}
		if (relation.object_name != NULL && strcmp(relation.object_name, "archived_users") == 0) {
			saw_archived_users = 1;
		}
	}
	saw_order_field = 0;
	for (index = 0U; index < graph.field_count; index++) {
		rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
		if (expect_status_ok(rc, &error, "set field should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.clause == SQLPARSER_CLAUSE_KIND_ORDER_BY &&
		    field.column_name != NULL &&
		    strcmp(field.column_name, "id") == 0) {
			saw_order_field = 1;
		}
	}
	if (expect_true(saw_users != 0, "set operation graph should include users") != 0 ||
	    expect_true(saw_archived_users != 0, "set operation graph should include archived_users") != 0 ||
	    expect_true(saw_order_field != 0, "set operation graph should expose top-level order_by field") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "set operation view export should succeed") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"users\"") != NULL, "set operation view should include users") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"archived_users\"") != NULL, "set operation view should include archived_users") != 0 ||
	    expect_true(strstr(view_json, "\"sets\"") != NULL, "set operation view should expose sets") != 0 ||
	    expect_true(strstr(view_json, "\"clause\":\"order_by\"") != NULL, "set operation view should expose top-level order_by") != 0 ||
	    expect_true(strstr(view_json, "\"column\":\"id\"") != NULL, "set operation view should include id fields") != 0 ||
	    expect_true(strstr(view_json, "\"objects\"") == NULL, "set operation view should not expose old objects") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_strict_contract_edges(void)
{
	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_relation_t relation;
		sqlparser_graph_block_t block;
		size_t index;
		int saw_cte_relation;
		int saw_cte_block;
		int saw_base_users;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"WITH active_users AS (SELECT id FROM public.users WHERE status = $1) "
			"SELECT au.id FROM active_users au",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "CTE query graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "CTE query graph should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		saw_cte_relation = 0;
		saw_cte_block = 0;
		saw_base_users = 0;
		for (index = 0U; index < graph.relation_count; index++) {
			rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
			if (expect_status_ok(rc, &error, "CTE relation should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (relation.kind == SQLPARSER_GRAPH_REL_CTE &&
			    relation.has_source_block != 0 &&
			    relation.object_name != NULL &&
			    strcmp(relation.object_name, "active_users") == 0) {
				saw_cte_relation = 1;
				rc = sqlparser_query_graph_block_at(&graph, relation.source_block_index, &block, &error);
				if (expect_status_ok(rc, &error, "CTE source block should be readable") != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
				if (block.kind == SQLPARSER_GRAPH_BLOCK_CTE) {
					saw_cte_block = 1;
				}
			}
			if (relation.kind == SQLPARSER_GRAPH_REL_BASE &&
			    relation.object_name != NULL &&
			    strcmp(relation.object_name, "users") == 0) {
				saw_base_users = 1;
			}
		}
		if (expect_true(saw_cte_relation != 0, "query graph should expose CTE relation") != 0 ||
		    expect_true(saw_cte_block != 0, "query graph should expose CTE source block") != 0 ||
		    expect_true(saw_base_users != 0, "query graph should expose CTE inner base table") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_field_t field;
		size_t index;
		int saw_ambiguous_id;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"SELECT id FROM users u JOIN orders o ON u.id = o.user_id WHERE id = 1",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "ambiguous field query graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "ambiguous field graph should be available") != 0 ||
		    expect_true(graph.relation_count == 2U, "ambiguous field graph should expose two relations") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		saw_ambiguous_id = 0;
		for (index = 0U; index < graph.field_count; index++) {
			rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
			if (expect_status_ok(rc, &error, "ambiguous field should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (field.column_name != NULL &&
			    strcmp(field.column_name, "id") == 0 &&
			    field.has_relation == 0 &&
			    field.candidate_relations.count == 2U) {
				saw_ambiguous_id = 1;
			}
		}
		if (expect_true(saw_ambiguous_id != 0, "unqualified ambiguous field should expose candidate relations") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_relation_t relation;
		sqlparser_graph_field_t field;
		size_t users_relation_index;
		size_t users_block_index;
		size_t index;
		int saw_outer_reference;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"SELECT (SELECT dm.name FROM dict dm WHERE dm.id = u.dict_id) AS dict_name "
			"FROM users u WHERE u.id = $1",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "correlated subquery graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "correlated subquery graph should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		users_relation_index = (size_t)-1;
		users_block_index = 0U;
		for (index = 0U; index < graph.relation_count; index++) {
			rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
			if (expect_status_ok(rc, &error, "correlated relation should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (relation.object_name != NULL && strcmp(relation.object_name, "users") == 0) {
				users_relation_index = index;
				users_block_index = relation.block_index;
			}
		}
		if (expect_true(users_relation_index != (size_t)-1, "outer users relation should be present") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		saw_outer_reference = 0;
		for (index = 0U; index < graph.field_count; index++) {
			rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
			if (expect_status_ok(rc, &error, "correlated field should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (field.has_relation != 0 &&
			    field.relation_index == users_relation_index &&
			    field.block_index != users_block_index &&
			    field.column_name != NULL &&
			    strcmp(field.column_name, "dict_id") == 0) {
				saw_outer_reference = 1;
			}
		}
		if (expect_true(saw_outer_reference != 0, "correlated subquery should point to outer relation") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		struct pagination_case {
			sqlparser_dialect_t dialect;
			const char *sql;
			size_t value_count;
			size_t bind_position;
		};
		static const struct pagination_case cases[] = {
			{
				SQLPARSER_DIALECT_POSTGRESQL,
				"SELECT id FROM users WHERE name LIKE $1 ORDER BY id LIMIT $2 OFFSET $3",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_MYSQL,
				"SELECT id FROM users WHERE name LIKE ? ORDER BY id LIMIT ? OFFSET ?",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_ORACLE,
				"SELECT id FROM users WHERE name LIKE :pattern ORDER BY id FETCH FIRST :limit ROWS ONLY",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_SQLSERVER,
				"SELECT [id] FROM [dbo].[users] WHERE [name] LIKE @pattern ORDER BY [id] OFFSET @offset ROWS FETCH NEXT @limit ROWS ONLY",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_DAMENG,
				"SELECT id FROM users WHERE name LIKE ? ORDER BY id LIMIT ? OFFSET ?",
				1U,
				1U
			}
		};
		sqlparser_parse_options_t options;
		size_t case_index;

		for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
			sqlparser_handle_t *handle;
			sqlparser_error_t error;
			sqlparser_query_graph_view_t graph;
			sqlparser_graph_value_t value;
			int rc;

			handle = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = cases[case_index].dialect;
			rc = sqlparser_parse_with_options(cases[case_index].sql, &options, &handle, &error);
			if (expect_status_ok(rc, &error, "pagination bind parse should succeed") != 0) {
				return 1;
			}
			rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
			if (expect_status_ok(rc, &error, "pagination graph should be available") != 0 ||
			    expect_true(graph.value_count == cases[case_index].value_count, "pagination binds should not enter values") != 0 ||
			    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "pagination WHERE value should be readable") != 0 ||
			    expect_true(value.has_bind_position != 0 && value.bind_position == cases[case_index].bind_position,
			                "pagination WHERE bind position mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_handle_destroy(handle);
		}
	}

	return 0;
}

static int test_session_context_patch_api(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *selector;
		const char *replacement_sql;
		const char *deparse_contains;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET search_path TO app_schema",
			"stmt[0].value[0]",
			"next_schema",
			"SET search_path TO next_schema"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"USE analytics",
			"stmt[0].value[0]",
			"warehouse",
			"USE warehouse"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [AdventureWorks2022]",
			"stmt[0].value[0]",
			"[ReportingDB]",
			"USE [ReportingDB]"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"stmt[0].value[0]",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = app"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CONTAINER=PDB1",
			"stmt[0].value[0]",
			"PDB2",
			"ALTER SESSION SET CONTAINER = pdb2"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SET SCHEMA KDES",
			"stmt[0].value[0]",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = app"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"stmt[0].value[0]",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = app"
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
			sqlparser_parse_options_t options;
			sqlparser_error_t error;
			sqlparser_handle_t *handle;
			sqlparser_patch_t patch;
			sqlparser_patch_list_t patch_list;
			char *deparsed_sql;
			int rc;

			handle = NULL;
			deparsed_sql = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "session context parse should succeed") != 0) {
				return 1;
			}

			memset(&patch, 0, sizeof(patch));
			patch.op = SQLPARSER_PATCH_REPLACE;
			patch.selector = cases[index].selector;
			patch.sql = cases[index].replacement_sql;
			patch_list.items = &patch;
			patch_list.count = 1U;
			rc = sqlparser_apply_patch(handle, &patch_list, &error);
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

static int test_session_context_quoted_identifier_literal_api(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *expected_value;
		int expected_quoted_identifier;
	} cases[] = {
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"kdes",
			0
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=\"KdesMixed\"",
			"KdesMixed",
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD'",
			"YYYY-MM-DD",
			0
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"kdes",
			0
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=\"KdesMixed\"",
			"KdesMixed",
			1
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_parse_options_t options;
		sqlparser_error_t error;
		sqlparser_handle_t *handle;
		sqlparser_literal_view_t literal;
		size_t literal_count;
		int rc;

		handle = NULL;
		literal_count = 0U;
		memset(&error, 0, sizeof(error));
		memset(&literal, 0, sizeof(literal));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "session context quoted identifier parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_literal_count(handle, 0U, &literal_count, &error);
		if (expect_status_ok(rc, &error, "session context literal count should succeed") != 0 ||
		    expect_true(literal_count == 1U, "session context should expose one literal") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_literal(handle, 0U, 0U, &literal, &error);
		if (expect_status_ok(rc, &error, "session context literal read should succeed") != 0 ||
		    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "session context literal should be string") != 0 ||
		    expect_true(literal.string_value != NULL, "session context string value should be present") != 0 ||
		    expect_true(literal.quoted_identifier == cases[index].expected_quoted_identifier,
		                "session context quoted identifier flag mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].expected_value != NULL &&
		    expect_true(strcmp(literal.string_value, cases[index].expected_value) == 0,
		                "session context literal value mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_oracle_container_service_patch_api(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *deparsed_sql;
	int rc;

	handle = NULL;
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

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].value[1]";
	patch.sql = "REPORT_SVC";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
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
	if (test_update_assignment_list_patch_api() != 0) {
		return 1;
	}
	if (test_update_assignment_list_apply_patch() != 0) {
		return 1;
	}
	if (test_structured_update_assignment_from_assignment_value() != 0) {
		return 1;
	}
	if (test_insert_cell_sql_mutation() != 0) {
		return 1;
	}
	if (test_insert_cell_bind_mutation() != 0) {
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
	if (test_query_graph_json_and_patch_api() != 0) {
		return 1;
	}
	if (test_query_graph_bind_fields() != 0) {
		return 1;
	}
	if (test_query_graph_condition_value_lists() != 0) {
		return 1;
	}
	if (test_query_graph_field_match_kind_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_expression_field_value_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_column_semantics_json() != 0) {
		return 1;
	}
	if (test_query_graph_public_struct_semantics() != 0) {
		return 1;
	}
	if (test_insert_select_target_values() != 0) {
		return 1;
	}
	if (test_oracle_multi_insert_query_graph_and_patch() != 0) {
		return 1;
	}
	if (test_query_graph_attribution_and_values() != 0) {
		return 1;
	}
	if (test_select_target_list_patch_api() != 0) {
		return 1;
	}
	if (test_structured_select_target_column_replacement() != 0) {
		return 1;
	}
	if (test_query_graph_set_operation_attribution() != 0) {
		return 1;
	}
	if (test_query_graph_strict_contract_edges() != 0) {
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
	if (test_session_context_patch_api() != 0) {
		return 1;
	}
	if (test_session_context_quoted_identifier_literal_api() != 0) {
		return 1;
	}
	if (test_oracle_container_service_patch_api() != 0) {
		return 1;
	}

	return 0;
}
