#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser_model_internal.h"

int sqlparser_model_strings_equal_nullable(const char *left, const char *right)
{
	if (left == NULL || right == NULL) {
		return left == right;
	}

	return strcmp(left, right) == 0;
}

int sqlparser_model_literal_view_equals_value(
	const sqlparser_literal_view_t *view,
	const sqlparser_literal_value_t *value)
{
	if (view == NULL || value == NULL || view->kind != value->kind) {
		return 0;
	}

	switch (view->kind) {
		case SQLPARSER_LITERAL_KIND_NULL:
			return 1;
		case SQLPARSER_LITERAL_KIND_STRING:
			return sqlparser_model_strings_equal_nullable(view->string_value, value->string_value);
		case SQLPARSER_LITERAL_KIND_INTEGER:
			return view->integer_value == value->integer_value;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			return sqlparser_model_strings_equal_nullable(view->float_value, value->float_value);
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			return view->boolean_value == value->boolean_value;
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			return 0;
	}
}

json_t *sqlparser_model_literal_view_to_json(const sqlparser_literal_view_t *view)
{
	json_t *object;

	if (view == NULL) {
		return NULL;
	}

	object = json_object();
	if (object == NULL) {
		return NULL;
	}

	(void)json_object_set_new(object, "kind", json_string(sqlparser_literal_kind_name(view->kind)));
	switch (view->kind) {
		case SQLPARSER_LITERAL_KIND_STRING:
			(void)json_object_set_new(
				object,
				"string_value",
				json_string(view->string_value != NULL ? view->string_value : ""));
			break;
		case SQLPARSER_LITERAL_KIND_INTEGER:
			(void)json_object_set_new(
				object,
				"integer_value",
				json_integer((json_int_t)view->integer_value));
			break;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			(void)json_object_set_new(
				object,
				"float_value",
				json_string(view->float_value != NULL ? view->float_value : ""));
			break;
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			(void)json_object_set_new(
				object,
				"boolean_value",
				view->boolean_value ? json_true() : json_false());
			break;
		case SQLPARSER_LITERAL_KIND_NULL:
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			break;
	}

	return object;
}

sqlparser_status_t sqlparser_model_literal_value_from_json(
	json_t *literal_json,
	sqlparser_literal_value_t *out_value,
	sqlparser_error_t *out_error)
{
	json_t *value_json;
	const char *kind_text;

	if (out_value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(out_value, 0, sizeof(*out_value));
	out_value->kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
	if (!json_is_object(literal_json)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal JSON must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	value_json = json_object_get(literal_json, "kind");
	if (!json_is_string(value_json)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal.kind must be a string");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	kind_text = json_string_value(value_json);
	if (strcmp(kind_text, "null") == 0) {
		out_value->kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(kind_text, "string") == 0) {
		value_json = json_object_get(literal_json, "string_value");
		if (!json_is_string(value_json)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"literal.string_value must be a string");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		out_value->kind = SQLPARSER_LITERAL_KIND_STRING;
		out_value->string_value = json_string_value(value_json);
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(kind_text, "integer") == 0) {
		value_json = json_object_get(literal_json, "integer_value");
		if (!json_is_integer(value_json)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"literal.integer_value must be an integer");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		out_value->kind = SQLPARSER_LITERAL_KIND_INTEGER;
		out_value->integer_value = (long long)json_integer_value(value_json);
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(kind_text, "float") == 0) {
		value_json = json_object_get(literal_json, "float_value");
		if (!json_is_string(value_json)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"literal.float_value must be a string");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		out_value->kind = SQLPARSER_LITERAL_KIND_FLOAT;
		out_value->float_value = json_string_value(value_json);
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(kind_text, "boolean") == 0) {
		value_json = json_object_get(literal_json, "boolean_value");
		if (!json_is_boolean(value_json)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"literal.boolean_value must be a boolean");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		out_value->kind = SQLPARSER_LITERAL_KIND_BOOLEAN;
		out_value->boolean_value = json_is_true(value_json) ? 1 : 0;
		return SQLPARSER_STATUS_OK;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_INVALID_ARGUMENT,
		"literal kind is not supported");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_model_json_object_set_selector(
	json_t *object,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	char *selector_text;
	sqlparser_status_t status;

	if (object == NULL || selector == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector output target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	selector_text = NULL;
	status = sqlparser_selector_format(selector, &selector_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (json_object_set_new(object, "selector", json_string(selector_text)) != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	sqlparser_string_free(selector_text);
	return SQLPARSER_STATUS_OK;
}
