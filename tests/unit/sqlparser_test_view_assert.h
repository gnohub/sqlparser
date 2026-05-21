#ifndef SQLPARSER_TEST_VIEW_ASSERT_H
#define SQLPARSER_TEST_VIEW_ASSERT_H

#include <jansson.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void sqlparser_test_append_char(char *buffer, size_t buffer_size, size_t *length, char value)
{
	if (buffer == NULL || length == NULL || buffer_size == 0U) {
		return;
	}
	if (*length + 1U >= buffer_size) {
		return;
	}
	buffer[*length] = value;
	(*length)++;
	buffer[*length] = '\0';
}

static void sqlparser_test_append_normalized_identifier(
	char *buffer,
	size_t buffer_size,
	size_t *length,
	const char *value)
{
	const char *cursor;

	if (value == NULL) {
		return;
	}
	for (cursor = value; *cursor != '\0'; cursor++) {
		if (*cursor == '"' || *cursor == '`' || *cursor == '[' || *cursor == ']') {
			continue;
		}
		sqlparser_test_append_char(buffer, buffer_size, length, *cursor);
	}
}

static void sqlparser_test_normalize_table_name(char *buffer, size_t buffer_size, const char *value)
{
	size_t length;

	if (buffer == NULL || buffer_size == 0U) {
		return;
	}
	buffer[0] = '\0';
	length = 0U;
	sqlparser_test_append_normalized_identifier(buffer, buffer_size, &length, value);
}

static const char *sqlparser_test_string_value(json_t *value)
{
	if (!json_is_string(value)) {
		return NULL;
	}
	return json_string_value(value);
}

static int sqlparser_test_append_table_part(
	char *buffer,
	size_t buffer_size,
	size_t *length,
	int *has_part,
	json_t *object,
	const char *key)
{
	const char *value;

	value = sqlparser_test_string_value(json_object_get(object, key));
	if (value == NULL || value[0] == '\0') {
		return 0;
	}
	if (*has_part) {
		sqlparser_test_append_char(buffer, buffer_size, length, '.');
	}
	sqlparser_test_append_normalized_identifier(buffer, buffer_size, length, value);
	*has_part = 1;
	return 1;
}

static int sqlparser_test_object_table_name(json_t *object, char *buffer, size_t buffer_size)
{
	size_t length;
	int has_part;
	int has_table;

	if (!json_is_object(object) || buffer == NULL || buffer_size == 0U) {
		return 0;
	}
	buffer[0] = '\0';
	length = 0U;
	has_part = 0;
	(void)sqlparser_test_append_table_part(buffer, buffer_size, &length, &has_part, object, "database");
	(void)sqlparser_test_append_table_part(buffer, buffer_size, &length, &has_part, object, "schema");
	has_table = sqlparser_test_append_table_part(buffer, buffer_size, &length, &has_part, object, "table");
	return has_table;
}

static int sqlparser_test_find_table_in_json(json_t *node, const char *expected)
{
	char actual[1024];
	const char *key;
	json_t *value;
	size_t index;

	if (node == NULL || expected == NULL) {
		return 0;
	}
	if (json_is_object(node)) {
		if (sqlparser_test_object_table_name(node, actual, sizeof(actual)) &&
		    strcmp(actual, expected) == 0) {
			return 1;
		}
		json_object_foreach(node, key, value) {
			(void)key;
			if (sqlparser_test_find_table_in_json(value, expected)) {
				return 1;
			}
		}
		return 0;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value) {
			if (sqlparser_test_find_table_in_json(value, expected)) {
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_test_view_contains_table(const char *view_json, const char *expected_table)
{
	json_error_t error;
	json_t *root;
	char expected[1024];
	int found;

	if (view_json == NULL || expected_table == NULL) {
		return 0;
	}
	sqlparser_test_normalize_table_name(expected, sizeof(expected), expected_table);
	root = json_loads(view_json, 0, &error);
	if (root == NULL) {
		return 0;
	}
	found = sqlparser_test_find_table_in_json(root, expected);
	json_decref(root);
	return found;
}

static int sqlparser_test_fail_case(const char *case_id, const char *case_name, const char *message)
{
	fprintf(stderr,
	        "FAIL [%s %s]: %s\n",
	        case_id != NULL ? case_id : "-",
	        case_name != NULL ? case_name : "-",
	        message != NULL ? message : "view assertion failed");
	return 1;
}

static int sqlparser_test_fail_case_field(
	const char *case_id,
	const char *case_name,
	const char *field_name,
	const char *expected)
{
	fprintf(stderr,
	        "FAIL [%s %s]: missing %s value '%s'\n",
	        case_id != NULL ? case_id : "-",
	        case_name != NULL ? case_name : "-",
	        field_name != NULL ? field_name : "-",
	        expected != NULL ? expected : "null");
	return 1;
}

static int sqlparser_test_text_contains_expected(
	const char *case_id,
	const char *case_name,
	const char *text,
	const char *field_name,
	json_t *expected_value)
{
	size_t index;
	json_t *item;
	const char *expected;

	if (expected_value == NULL) {
		return 0;
	}
	if (json_is_string(expected_value)) {
		expected = json_string_value(expected_value);
		return text != NULL && expected != NULL && strstr(text, expected) != NULL ?
			0 :
			sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
	}
	if (!json_is_array(expected_value)) {
		return sqlparser_test_fail_case(case_id, case_name, "expected text assertion must be a string or array");
	}
	json_array_foreach(expected_value, index, item) {
		expected = sqlparser_test_string_value(item);
		if (expected == NULL) {
			return sqlparser_test_fail_case(case_id, case_name, "expected text assertion item must be a string");
		}
		if (text == NULL || strstr(text, expected) == NULL) {
			return sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
		}
	}
	return 0;
}

static int sqlparser_test_text_not_contains_expected(
	const char *case_id,
	const char *case_name,
	const char *text,
	const char *field_name,
	json_t *expected_value)
{
	size_t index;
	json_t *item;
	const char *expected;

	if (expected_value == NULL) {
		return 0;
	}
	if (json_is_string(expected_value)) {
		expected = json_string_value(expected_value);
		return text == NULL || expected == NULL || strstr(text, expected) == NULL ?
			0 :
			sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
	}
	if (!json_is_array(expected_value)) {
		return sqlparser_test_fail_case(case_id, case_name, "expected text assertion must be a string or array");
	}
	json_array_foreach(expected_value, index, item) {
		expected = sqlparser_test_string_value(item);
		if (expected == NULL) {
			return sqlparser_test_fail_case(case_id, case_name, "expected text assertion item must be a string");
		}
		if (text != NULL && strstr(text, expected) != NULL) {
			return sqlparser_test_fail_case_field(case_id, case_name, field_name, expected);
		}
	}
	return 0;
}

static int sqlparser_test_json_string_equals(json_t *object, const char *key, const char *expected)
{
	json_t *value;
	const char *actual;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	if (expected == NULL) {
		return json_is_null(value);
	}
	actual = sqlparser_test_string_value(value);
	return actual != NULL && strcmp(actual, expected) == 0;
}

static int sqlparser_test_json_integer_equals(json_t *object, const char *key, json_int_t expected)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return json_is_integer(value) && json_integer_value(value) == expected;
}

static int sqlparser_test_json_string_or_null_field(json_t *object, const char *key)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return json_is_string(value) || json_is_null(value);
}

static int sqlparser_test_json_expected_string_or_null_matches(
	json_t *object,
	const char *key,
	json_t *expected_value)
{
	const char *expected;

	if (expected_value == NULL) {
		return 1;
	}
	if (json_is_null(expected_value)) {
		return json_is_null(json_object_get(object, key));
	}
	expected = sqlparser_test_string_value(expected_value);
	return expected != NULL && sqlparser_test_json_string_equals(object, key, expected);
}

static int sqlparser_test_json_expected_integer_matches(
	json_t *object,
	const char *actual_key,
	json_t *expected_value)
{
	if (expected_value == NULL) {
		return 1;
	}
	return json_is_integer(expected_value) &&
		sqlparser_test_json_integer_equals(object, actual_key, json_integer_value(expected_value));
}

static int sqlparser_test_object_matches_expected_table(json_t *object, const char *expected_table)
{
	char actual[1024];
	char expected[1024];
	const char *actual_table;

	if (expected_table == NULL) {
		return 1;
	}
	if (!json_is_object(object)) {
		return 0;
	}
	sqlparser_test_normalize_table_name(expected, sizeof(expected), expected_table);
	if (sqlparser_test_object_table_name(object, actual, sizeof(actual)) &&
	    strcmp(actual, expected) == 0) {
		return 1;
	}
	actual_table = sqlparser_test_string_value(json_object_get(object, "table"));
	if (actual_table == NULL) {
		return 0;
	}
	sqlparser_test_normalize_table_name(actual, sizeof(actual), actual_table);
	return strcmp(actual, expected) == 0;
}

static json_t *sqlparser_test_find_expected_column(json_t *statement, json_t *expected_column)
{
	json_t *objects;
	json_t *object;
	size_t object_index;
	const char *expected_table;
	const char *expected_name;
	const char *expected_keyword;
	const char *expected_bind;

	if (!json_is_object(statement) || !json_is_object(expected_column)) {
		return NULL;
	}
	expected_table = sqlparser_test_string_value(json_object_get(expected_column, "table"));
	expected_name = sqlparser_test_string_value(json_object_get(expected_column, "name"));
	expected_keyword = sqlparser_test_string_value(json_object_get(expected_column, "keyword"));
	expected_bind = sqlparser_test_string_value(json_object_get(expected_column, "bind"));
	objects = json_object_get(statement, "objects");
	json_array_foreach(objects, object_index, object) {
		json_t *columns;
		json_t *column;
		size_t column_index;

		if (!sqlparser_test_object_matches_expected_table(object, expected_table)) {
			continue;
		}
		columns = json_object_get(object, "columns");
		json_array_foreach(columns, column_index, column) {
			if (expected_name != NULL && !sqlparser_test_json_string_equals(column, "name", expected_name)) {
				continue;
			}
			if (expected_keyword != NULL && !sqlparser_test_json_string_equals(column, "keyword", expected_keyword)) {
				continue;
			}
			if (expected_bind != NULL && !sqlparser_test_json_string_equals(column, "bind", expected_bind)) {
				continue;
			}
			return column;
		}
	}
	return NULL;
}

static int sqlparser_test_cell_matches_expected(
	json_t *row,
	json_t *cell,
	json_t *expected_cell)
{
	if (!json_is_object(row) || !json_is_object(cell) || !json_is_object(expected_cell)) {
		return 0;
	}
	if (!sqlparser_test_json_expected_integer_matches(row, "index", json_object_get(expected_cell, "row_index")) ||
	    !sqlparser_test_json_expected_integer_matches(cell, "column_index", json_object_get(expected_cell, "column_index"))) {
		return 0;
	}
	if (!sqlparser_test_json_expected_string_or_null_matches(cell, "column", json_object_get(expected_cell, "column")) ||
	    !sqlparser_test_json_expected_string_or_null_matches(cell, "sql", json_object_get(expected_cell, "sql")) ||
	    !sqlparser_test_json_expected_string_or_null_matches(cell, "bind", json_object_get(expected_cell, "bind")) ||
	    !sqlparser_test_json_expected_string_or_null_matches(cell, "bind_sql", json_object_get(expected_cell, "bind_sql")) ||
	    !sqlparser_test_json_expected_string_or_null_matches(cell, "bind_selector", json_object_get(expected_cell, "bind_selector"))) {
		return 0;
	}
	if (!sqlparser_test_json_expected_integer_matches(cell, "bind_kind", json_object_get(expected_cell, "bind_kind"))) {
		return 0;
	}
	return 1;
}

static json_t *sqlparser_test_find_expected_cell(json_t *statement, json_t *expected_cell)
{
	json_t *objects;
	json_t *object;
	size_t object_index;
	const char *expected_table;

	if (!json_is_object(statement) || !json_is_object(expected_cell)) {
		return NULL;
	}
	expected_table = sqlparser_test_string_value(json_object_get(expected_cell, "table"));
	objects = json_object_get(statement, "objects");
	if (!json_is_array(objects)) {
		return NULL;
	}
	json_array_foreach(objects, object_index, object) {
		json_t *rows;
		json_t *row;
		size_t row_index;

		if (!sqlparser_test_object_matches_expected_table(object, expected_table)) {
			continue;
		}
		rows = json_object_get(object, "rows");
		if (!json_is_array(rows)) {
			continue;
		}
		json_array_foreach(rows, row_index, row) {
			json_t *cells;
			json_t *cell;
			size_t cell_index;

			cells = json_object_get(row, "cells");
			if (!json_is_array(cells)) {
				continue;
			}
			json_array_foreach(cells, cell_index, cell) {
				if (sqlparser_test_cell_matches_expected(row, cell, expected_cell)) {
					return cell;
				}
			}
		}
	}
	return NULL;
}

static json_t *sqlparser_test_find_statement_clause_by_kind(json_t *statement, const char *kind)
{
	json_t *clauses;
	json_t *clause;
	size_t index;

	if (!json_is_object(statement) || kind == NULL) {
		return NULL;
	}
	clauses = json_object_get(statement, "clauses");
	json_array_foreach(clauses, index, clause) {
		if (sqlparser_test_json_string_equals(clause, "kind", kind)) {
			return clause;
		}
	}
	return NULL;
}

static json_t *sqlparser_test_find_statement_clause_by_id(json_t *statement, json_int_t clause_id)
{
	json_t *clauses;
	json_t *clause;
	size_t index;

	if (!json_is_object(statement) || clause_id <= 0) {
		return NULL;
	}
	clauses = json_object_get(statement, "clauses");
	json_array_foreach(clauses, index, clause) {
		if (sqlparser_test_json_integer_equals(clause, "id", clause_id)) {
			return clause;
		}
	}
	return NULL;
}

static int sqlparser_test_column_clause_matches(json_t *statement, json_t *column, const char *expected_kind)
{
	json_t *clauses;
	json_t *clause_id;
	json_t *clause;
	json_int_t index;

	if (expected_kind == NULL) {
		return 1;
	}
	clauses = json_object_get(statement, "clauses");
	clause_id = json_object_get(column, "clause_id");
	if (!json_is_array(clauses) || !json_is_integer(clause_id)) {
		return 0;
	}
	index = json_integer_value(clause_id);
	if (index <= 0 || (size_t)index > json_array_size(clauses)) {
		return 0;
	}
	clause = sqlparser_test_find_statement_clause_by_id(statement, index);
	if (clause == NULL) {
		return 0;
	}
	return sqlparser_test_json_string_equals(clause, "kind", expected_kind);
}

static int sqlparser_test_target_path_matches(json_t *actual, json_t *expected)
{
	size_t index;
	json_t *expected_entry;

	if (expected == NULL) {
		return 1;
	}
	if (!json_is_array(actual) || !json_is_array(expected) || json_array_size(actual) != json_array_size(expected)) {
		return 0;
	}
	json_array_foreach(expected, index, expected_entry) {
		json_t *actual_entry;
		json_t *expected_name;
		json_t *expected_arg_index;
		const char *kind;
		const char *name;

		if (!json_is_object(expected_entry)) {
			return 0;
		}
		actual_entry = json_array_get(actual, index);
		kind = sqlparser_test_string_value(json_object_get(expected_entry, "kind"));
		if (kind == NULL || !sqlparser_test_json_string_equals(actual_entry, "kind", kind)) {
			return 0;
		}
		expected_name = json_object_get(expected_entry, "name");
		if (expected_name != NULL) {
			name = json_is_null(expected_name) ? NULL : sqlparser_test_string_value(expected_name);
			if (!sqlparser_test_json_string_equals(actual_entry, "name", name)) {
				return 0;
			}
		}
		expected_arg_index = json_object_get(expected_entry, "arg_index");
		if (expected_arg_index != NULL &&
		    (!json_is_integer(expected_arg_index) ||
		     !sqlparser_test_json_integer_equals(actual_entry, "arg_index", json_integer_value(expected_arg_index)))) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_test_verify_expected_column(
	const char *case_id,
	const char *case_name,
	json_t *statement,
	json_t *expected_column)
{
	json_t *column;
	json_t *value;
	const char *expected;

	column = sqlparser_test_find_expected_column(statement, expected_column);
	if (column == NULL) {
		expected = sqlparser_test_string_value(json_object_get(expected_column, "name"));
		return sqlparser_test_fail_case_field(case_id, case_name, "columns", expected);
	}

	expected = sqlparser_test_string_value(json_object_get(expected_column, "clause_kind"));
	if (!sqlparser_test_column_clause_matches(statement, column, expected)) {
		return sqlparser_test_fail_case_field(case_id, case_name, "clause_kind", expected);
	}
	if (json_is_true(json_object_get(expected_column, "clause_id_is_null")) &&
	    !json_is_null(json_object_get(column, "clause_id"))) {
		return sqlparser_test_fail_case(case_id, case_name, "column clause_id should be null");
	}

	value = json_object_get(expected_column, "target_path");
	if (!sqlparser_test_target_path_matches(json_object_get(column, "target_path"), value)) {
		return sqlparser_test_fail_case(case_id, case_name, "target_path mismatch");
	}
	expected = sqlparser_test_string_value(json_object_get(expected_column, "operator"));
	if (expected != NULL && !sqlparser_test_json_string_equals(column, "operator", expected)) {
		return sqlparser_test_fail_case_field(case_id, case_name, "operator", expected);
	}
	expected = sqlparser_test_string_value(json_object_get(expected_column, "bind"));
	if (expected != NULL && !sqlparser_test_json_string_equals(column, "bind", expected)) {
		return sqlparser_test_fail_case_field(case_id, case_name, "bind", expected);
	}
	value = json_object_get(expected_column, "bind_kind");
	if (value != NULL &&
	    (!json_is_integer(value) ||
	     !sqlparser_test_json_integer_equals(column, "bind_kind", json_integer_value(value)))) {
		return sqlparser_test_fail_case(case_id, case_name, "bind_kind mismatch");
	}
	expected = sqlparser_test_string_value(json_object_get(expected_column, "bind_sql"));
	if (expected != NULL && !sqlparser_test_json_string_equals(column, "bind_sql", expected)) {
		return sqlparser_test_fail_case_field(case_id, case_name, "bind_sql", expected);
	}
	if (json_is_true(json_object_get(expected_column, "value_is_null")) &&
	    !json_is_null(json_object_get(column, "value"))) {
		return sqlparser_test_fail_case(case_id, case_name, "column value should be null");
	}
	return 0;
}

static int sqlparser_test_verify_expected_cell(
	const char *case_id,
	const char *case_name,
	json_t *statement,
	json_t *expected_cell)
{
	json_t *cell;
	json_t *value;
	const char *expected;

	if (!json_is_object(expected_cell)) {
		return sqlparser_test_fail_case(case_id, case_name, "expected cell must be an object");
	}
	cell = sqlparser_test_find_expected_cell(statement, expected_cell);
	if (cell == NULL) {
		expected = sqlparser_test_string_value(json_object_get(expected_cell, "bind"));
		if (expected == NULL) {
			expected = sqlparser_test_string_value(json_object_get(expected_cell, "sql"));
		}
		return sqlparser_test_fail_case_field(case_id, case_name, "cells", expected);
	}
	expected = sqlparser_test_string_value(json_object_get(expected_cell, "bind"));
	if (expected != NULL && !sqlparser_test_json_string_equals(cell, "bind", expected)) {
		return sqlparser_test_fail_case_field(case_id, case_name, "cell bind", expected);
	}
	value = json_object_get(expected_cell, "bind_kind");
	if (value != NULL &&
	    (!json_is_integer(value) ||
	     !sqlparser_test_json_integer_equals(cell, "bind_kind", json_integer_value(value)))) {
		return sqlparser_test_fail_case(case_id, case_name, "cell bind_kind mismatch");
	}
	expected = sqlparser_test_string_value(json_object_get(expected_cell, "bind_sql"));
	if (expected != NULL && !sqlparser_test_json_string_equals(cell, "bind_sql", expected)) {
		return sqlparser_test_fail_case_field(case_id, case_name, "cell bind_sql", expected);
	}
	return 0;
}

static int sqlparser_test_verify_actual_column_shape(
	const char *case_id,
	const char *case_name,
	json_t *column)
{
	if (!json_is_array(json_object_get(column, "target_path"))) {
		return sqlparser_test_fail_case(case_id, case_name, "column target_path must be an array");
	}
	if (!sqlparser_test_json_string_or_null_field(column, "bind") ||
	    !json_is_integer(json_object_get(column, "bind_kind")) ||
	    !sqlparser_test_json_string_or_null_field(column, "bind_sql") ||
	    !sqlparser_test_json_string_or_null_field(column, "bind_selector")) {
		return sqlparser_test_fail_case(case_id, case_name, "column bind fields have invalid shape");
	}
	if (json_is_null(json_object_get(column, "bind")) &&
	    !sqlparser_test_json_integer_equals(column, "bind_kind", 0)) {
		return sqlparser_test_fail_case(case_id, case_name, "non-bind column bind_kind should be 0");
	}
	if (!json_is_null(json_object_get(column, "bind")) &&
	    json_integer_value(json_object_get(column, "bind_kind")) == 0) {
		return sqlparser_test_fail_case(case_id, case_name, "bind column bind_kind should not be 0");
	}
	if (json_is_null(json_object_get(column, "bind")) &&
	    !json_is_null(json_object_get(column, "bind_sql"))) {
		return sqlparser_test_fail_case(case_id, case_name, "non-bind column bind_sql should be null");
	}
	if (json_object_get(column, "target_kind") != NULL ||
	    json_object_get(column, "target_name") != NULL ||
	    json_object_get(column, "target_arg_index") != NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "column contains removed target_* fields");
	}
	return 0;
}

static int sqlparser_test_verify_actual_cell_shape(
	const char *case_id,
	const char *case_name,
	json_t *cell)
{
	if (!sqlparser_test_json_string_or_null_field(cell, "bind") ||
	    !json_is_integer(json_object_get(cell, "bind_kind")) ||
	    !sqlparser_test_json_string_or_null_field(cell, "bind_sql") ||
	    !sqlparser_test_json_string_or_null_field(cell, "bind_selector")) {
		return sqlparser_test_fail_case(case_id, case_name, "cell bind fields have invalid shape");
	}
	if (json_is_null(json_object_get(cell, "bind")) &&
	    !sqlparser_test_json_integer_equals(cell, "bind_kind", 0)) {
		return sqlparser_test_fail_case(case_id, case_name, "non-bind cell bind_kind should be 0");
	}
	if (!json_is_null(json_object_get(cell, "bind")) &&
	    json_integer_value(json_object_get(cell, "bind_kind")) == 0) {
		return sqlparser_test_fail_case(case_id, case_name, "bind cell bind_kind should not be 0");
	}
	if (json_is_null(json_object_get(cell, "bind")) &&
	    !json_is_null(json_object_get(cell, "bind_sql"))) {
		return sqlparser_test_fail_case(case_id, case_name, "non-bind cell bind_sql should be null");
	}
	return 0;
}

static int sqlparser_test_verify_actual_columns_shape(
	const char *case_id,
	const char *case_name,
	json_t *statement)
{
	json_t *objects;
	json_t *object;
	size_t object_index;

	objects = json_object_get(statement, "objects");
	if (objects == NULL) {
		return 0;
	}
	if (!json_is_array(objects)) {
		return sqlparser_test_fail_case(case_id, case_name, "statement objects must be an array");
	}
	json_array_foreach(objects, object_index, object) {
		json_t *columns;
		json_t *column;
		size_t column_index;

		columns = json_object_get(object, "columns");
		if (columns == NULL) {
			continue;
		}
		if (!json_is_array(columns)) {
			return sqlparser_test_fail_case(case_id, case_name, "object columns must be an array");
		}
		json_array_foreach(columns, column_index, column) {
			if (sqlparser_test_verify_actual_column_shape(case_id, case_name, column) != 0) {
				return 1;
			}
		}
		if (json_is_array(json_object_get(object, "rows"))) {
			json_t *rows;
			json_t *row;
			size_t row_index;

			rows = json_object_get(object, "rows");
			json_array_foreach(rows, row_index, row) {
				json_t *cells;
				json_t *cell;
				size_t cell_index;

				cells = json_object_get(row, "cells");
				if (!json_is_array(cells)) {
					return sqlparser_test_fail_case(case_id, case_name, "row cells must be an array");
				}
				json_array_foreach(cells, cell_index, cell) {
					if (sqlparser_test_verify_actual_cell_shape(case_id, case_name, cell) != 0) {
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

static int sqlparser_test_verify_expected_clause(
	const char *case_id,
	const char *case_name,
	json_t *statement,
	json_t *expected_clause)
{
	const char *kind;
	json_t *clause;

	if (!json_is_object(expected_clause)) {
		return sqlparser_test_fail_case(case_id, case_name, "expected clause must be an object");
	}
	kind = sqlparser_test_string_value(json_object_get(expected_clause, "kind"));
	clause = sqlparser_test_find_statement_clause_by_kind(statement, kind);
	if (clause == NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "clauses", kind);
	}
	if (!json_is_integer(json_object_get(clause, "id"))) {
		return sqlparser_test_fail_case(case_id, case_name, "clause id should be an integer");
	}
	if (json_is_true(json_object_get(expected_clause, "selector_is_null")) &&
	    !json_is_null(json_object_get(clause, "selector"))) {
		return sqlparser_test_fail_case(case_id, case_name, "clause selector should be null");
	}
	return 0;
}

static int sqlparser_test_verify_statement_semantics(
	const char *case_id,
	const char *case_name,
	json_t *statement,
	json_t *expect_root)
{
	json_t *items;
	json_t *item;
	size_t index;

	if (sqlparser_test_verify_actual_columns_shape(case_id, case_name, statement) != 0) {
		return 1;
	}

	items = json_object_get(statement, "clauses");
	if (items != NULL) {
		if (!json_is_array(items)) {
			return sqlparser_test_fail_case(case_id, case_name, "statement clauses must be an array");
		}
		json_array_foreach(items, index, item) {
			if (!sqlparser_test_json_integer_equals(item, "id", (json_int_t)index + 1)) {
				return sqlparser_test_fail_case(case_id, case_name, "clause id mismatch");
			}
		}
	}

	items = json_object_get(expect_root, "clauses");
	if (items != NULL) {
		if (!json_is_array(items)) {
			return sqlparser_test_fail_case(case_id, case_name, "clauses expectation must be an array");
		}
		json_array_foreach(items, index, item) {
			if (sqlparser_test_verify_expected_clause(case_id, case_name, statement, item) != 0) {
				return 1;
			}
		}
	}

	items = json_object_get(expect_root, "columns");
	if (items != NULL) {
		if (!json_is_array(items)) {
			return sqlparser_test_fail_case(case_id, case_name, "columns expectation must be an array");
		}
		json_array_foreach(items, index, item) {
			if (sqlparser_test_verify_expected_column(case_id, case_name, statement, item) != 0) {
				return 1;
			}
		}
	}

	items = json_object_get(expect_root, "cells");
	if (items != NULL) {
		if (!json_is_array(items)) {
			return sqlparser_test_fail_case(case_id, case_name, "cells expectation must be an array");
		}
		json_array_foreach(items, index, item) {
			if (sqlparser_test_verify_expected_cell(case_id, case_name, statement, item) != 0) {
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_test_verify_view_expectations(
	const char *case_id,
	const char *case_name,
	const char *view_json,
	json_t *expect_root)
{
	json_error_t error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	int rc;

	if (expect_root == NULL) {
		return 0;
	}
	if (sqlparser_test_text_contains_expected(
		    case_id,
		    case_name,
		    view_json,
		    "view_contains",
		    json_object_get(expect_root, "view_contains")) != 0 ||
	    sqlparser_test_text_not_contains_expected(
		    case_id,
		    case_name,
		    view_json,
		    "view_not_contains",
		    json_object_get(expect_root, "view_not_contains")) != 0) {
		return 1;
	}
	if (json_object_get(expect_root, "columns") == NULL &&
	    json_object_get(expect_root, "clauses") == NULL &&
	    json_object_get(expect_root, "cells") == NULL) {
		return 0;
	}

	root = json_loads(view_json, 0, &error);
	if (root == NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON should decode");
	}
	statements = json_object_get(root, "statements");
	statement = json_is_array(statements) ? json_array_get(statements, 0U) : NULL;
	rc = json_is_object(statement) ?
		sqlparser_test_verify_statement_semantics(case_id, case_name, statement, expect_root) :
		sqlparser_test_fail_case(case_id, case_name, "view JSON statement should exist");
	json_decref(root);
	return rc;
}

#endif
