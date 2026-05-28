#ifndef SQLPARSER_TEST_VIEW_ASSERT_H
#define SQLPARSER_TEST_VIEW_ASSERT_H

#include <jansson.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *sqlparser_test_string_value(json_t *value)
{
	if (!json_is_string(value)) {
		return NULL;
	}
	return json_string_value(value);
}

static const char *sqlparser_test_forbidden_query_graph_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (strcmp(key, "source_columns") == 0 ||
			    strcmp(key, "expressions") == 0 ||
			    strcmp(key, "binds") == 0 ||
			    strcmp(key, "sql") == 0 ||
			    strcmp(key, "status") == 0 ||
			    strcmp(key, "reason") == 0 ||
			    strcmp(key, "diagnostics") == 0 ||
			    strcmp(key, "contract") == 0 ||
			    strcmp(key, "version") == 0 ||
			    strcmp(key, "message") == 0 ||
			    strcmp(key, "debug") == 0 ||
			    strcmp(key, "log") == 0) {
				return key;
			}
			nested_key = sqlparser_test_forbidden_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = sqlparser_test_forbidden_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static const char *sqlparser_test_null_query_graph_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_null(node)) {
		return "array item";
	}
	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (json_is_null(value)) {
				return key;
			}
			nested_key = sqlparser_test_null_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = sqlparser_test_null_query_graph_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static const char *sqlparser_test_empty_query_graph_array_key(json_t *node)
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
			nested_key = sqlparser_test_empty_query_graph_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = sqlparser_test_empty_query_graph_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static int sqlparser_test_optional_array_is_valid(json_t *object, const char *key)
{
	json_t *value;

	value = json_object_get(object, key);
	return value == NULL || json_is_array(value);
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
	json_array_foreach(expected_value, index, item)
	{
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
	json_array_foreach(expected_value, index, item)
	{
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

static int sqlparser_test_verify_query_graph_shape(
	const char *case_id,
	const char *case_name,
	json_t *statement)
{
	json_t *graph;
	const char *forbidden_key;
	const char *null_key;
	const char *empty_array_key;

	if (!json_is_object(statement)) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement should be an object");
	}
	if (!json_is_integer(json_object_get(statement, "index"))) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement index should be an integer");
	}
	if (!json_is_string(json_object_get(statement, "keyword"))) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement keyword should be a string");
	}
	if (json_object_get(statement, "keywords") != NULL ||
	    json_object_get(statement, "clauses") != NULL ||
	    json_object_get(statement, "objects") != NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement contains removed fields");
	}
	graph = json_object_get(statement, "query_graph");
	if (!json_is_object(graph)) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statement should expose query_graph");
	}
	if (!sqlparser_test_optional_array_is_valid(graph, "blocks") ||
	    !sqlparser_test_optional_array_is_valid(graph, "relations") ||
	    !sqlparser_test_optional_array_is_valid(graph, "targets") ||
	    !sqlparser_test_optional_array_is_valid(graph, "fields") ||
	    !sqlparser_test_optional_array_is_valid(graph, "values") ||
	    !sqlparser_test_optional_array_is_valid(graph, "sets")) {
		return sqlparser_test_fail_case(case_id, case_name, "query_graph arrays have invalid shape");
	}
	if (json_object_get(graph, "objects") != NULL ||
	    json_object_get(graph, "clauses") != NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "query_graph contains removed fields");
	}
	forbidden_key = sqlparser_test_forbidden_query_graph_key(graph);
	if (forbidden_key != NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "query_graph contains forbidden key", forbidden_key);
	}
	null_key = sqlparser_test_null_query_graph_key(graph);
	if (null_key != NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "query_graph contains null field", null_key);
	}
	empty_array_key = sqlparser_test_empty_query_graph_array_key(graph);
	if (empty_array_key != NULL) {
		return sqlparser_test_fail_case_field(case_id, case_name, "query_graph contains empty array", empty_array_key);
	}
	return 0;
}

static int sqlparser_test_verify_view_shape(
	const char *case_id,
	const char *case_name,
	const char *view_json)
{
	json_error_t error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	size_t index;

	root = json_loads(view_json, 0, &error);
	if (root == NULL) {
		return sqlparser_test_fail_case(case_id, case_name, "view JSON should decode");
	}
	statements = json_object_get(root, "statements");
	if (!json_is_array(statements)) {
		json_decref(root);
		return sqlparser_test_fail_case(case_id, case_name, "view JSON statements should be an array");
	}
	json_array_foreach(statements, index, statement)
	{
		if (sqlparser_test_verify_query_graph_shape(case_id, case_name, statement) != 0) {
			json_decref(root);
			return 1;
		}
	}
	json_decref(root);
	return 0;
}

static int sqlparser_test_verify_view_expectations(
	const char *case_id,
	const char *case_name,
	const char *view_json,
	json_t *expect_root)
{
	if (expect_root == NULL) {
		return sqlparser_test_verify_view_shape(case_id, case_name, view_json);
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
	return sqlparser_test_verify_view_shape(case_id, case_name, view_json);
}

#endif
