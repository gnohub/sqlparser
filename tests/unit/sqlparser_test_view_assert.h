#ifndef SQLPARSER_TEST_VIEW_ASSERT_H
#define SQLPARSER_TEST_VIEW_ASSERT_H

#include <jansson.h>
#include <stddef.h>
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

#endif
