#ifndef SQLPARSER_CASE_RUNNER_H
#define SQLPARSER_CASE_RUNNER_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE = 0,
	SQLPARSER_CASE_STAGE_VIEW = 1,
	SQLPARSER_CASE_STAGE_PATCH_DEPARSE = 2,
	SQLPARSER_CASE_STAGE_COUNT = 3
} sqlparser_case_stage_t;

typedef struct {
	size_t cases;
	size_t passed;
	size_t failed;
	size_t skipped;
	size_t patches;
	size_t patches_passed;
	size_t patches_failed;
	size_t patches_not_run;
	size_t runner_errors;
	size_t stage_failures[SQLPARSER_CASE_STAGE_COUNT];
} sqlparser_case_runner_stats_t;

typedef struct {
	const char *fixture_path;
	size_t case_index;
	const char *case_name;
	sqlparser_dialect_t dialect;
} sqlparser_case_context_t;

static const char *sqlparser_case_stage_name(sqlparser_case_stage_t stage)
{
	switch (stage) {
		case SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE:
			return "original-deparse";
		case SQLPARSER_CASE_STAGE_VIEW:
			return "view";
		case SQLPARSER_CASE_STAGE_PATCH_DEPARSE:
			return "patch-deparse";
		default:
			return "unknown";
	}
}

static void sqlparser_case_report_failure(
	sqlparser_case_runner_stats_t *stats,
	const sqlparser_case_context_t *context,
	sqlparser_case_stage_t stage,
	size_t patch_index,
	const char *action,
	const char *target,
	const char *detail)
{
	stats->stage_failures[stage]++;
	fprintf(
		stderr,
		"CASE FAIL fixture=%s case=%lu name=%s dialect=%s stage=%s",
		context->fixture_path,
		(unsigned long)(context->case_index + 1U),
		context->case_name,
		sqlparser_dialect_name(context->dialect),
		sqlparser_case_stage_name(stage));
	if (patch_index != SIZE_MAX) {
		fprintf(
			stderr,
			" patch=%lu action=%s target=%s",
			(unsigned long)(patch_index + 1U),
			action != NULL ? action : "-",
			target != NULL ? target : "-");
	}
	fprintf(stderr, " detail=%s\n", detail != NULL ? detail : "failure");
}

static void sqlparser_case_report_runner_error(
	sqlparser_case_runner_stats_t *stats,
	const char *fixture_path,
	size_t case_index,
	const char *case_name,
	const char *detail)
{
	stats->runner_errors++;
	fprintf(
		stderr,
		"RUNNER ERROR fixture=%s case=%lu name=%s detail=%s\n",
		fixture_path,
		(unsigned long)(case_index + 1U),
		case_name != NULL ? case_name : "-",
		detail != NULL ? detail : "invalid fixture");
}

static int sqlparser_case_key_allowed(
	const char *key,
	const char *const *allowed,
	size_t allowed_count)
{
	size_t index;

	for (index = 0U; index < allowed_count; index++) {
		if (strcmp(key, allowed[index]) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_case_object_has_only(
	json_t *object,
	const char *const *allowed,
	size_t allowed_count,
	char *detail,
	size_t detail_size)
{
	const char *key;
	json_t *value;

	json_object_foreach(object, key, value) {
		(void)value;
		if (!sqlparser_case_key_allowed(key, allowed, allowed_count)) {
			(void)snprintf(detail, detail_size, "unknown field '%s'", key);
			return 0;
		}
	}
	return 1;
}

static const char *sqlparser_case_required_string(
	json_t *object,
	const char *key)
{
	json_t *value;
	const char *text;

	value = json_object_get(object, key);
	if (!json_is_string(value)) {
		return NULL;
	}
	text = json_string_value(value);
	return text != NULL && text[0] != '\0' ? text : NULL;
}

static int sqlparser_case_validate_bind_occurrences(
	json_t *expected,
	char *detail,
	size_t detail_size)
{
	static const char *const occurrence_keys[] = {
		"position", "kind", "key", "sql"
	};
	const char *kind;
	json_int_t position;
	json_t *item;
	json_t *key;
	json_t *position_json;
	size_t index;

	if (expected == NULL) {
		return 1;
	}
	if (!json_is_array(expected)) {
		(void)snprintf(detail, detail_size, "bind_occurrences must be an array");
		return 0;
	}
	json_array_foreach(expected, index, item) {
		if (!json_is_object(item)) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu] must be an object",
				(unsigned long)index);
			return 0;
		}
		if (!sqlparser_case_object_has_only(
			    item,
			    occurrence_keys,
			    sizeof(occurrence_keys) / sizeof(occurrence_keys[0]),
			    detail,
			    detail_size)) {
			return 0;
		}
		position_json = json_object_get(item, "position");
		if (!json_is_integer(position_json) ||
		    (position = json_integer_value(position_json)) <= 0 ||
		    (uintmax_t)position > (uintmax_t)SIZE_MAX) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu].position must be a 1-based integer",
				(unsigned long)index);
			return 0;
		}
		kind = json_string_value(json_object_get(item, "kind"));
		if (kind == NULL ||
		    (strcmp(kind, "named") != 0 &&
		     strcmp(kind, "positional") != 0)) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu].kind must be 'named' or 'positional'",
				(unsigned long)index);
			return 0;
		}
		key = json_object_get(item, "key");
		if (!json_is_string(key) && !json_is_null(key)) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu].key must be a string or null",
				(unsigned long)index);
			return 0;
		}
		if (!json_is_string(json_object_get(item, "sql"))) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu].sql must be a string",
				(unsigned long)index);
			return 0;
		}
	}
	return 1;
}

static int sqlparser_case_check_bind_occurrences(
	const sqlparser_handle_t *handle,
	json_t *expected,
	char *detail,
	size_t detail_size)
{
	sqlparser_bind_occurrence_t actual;
	sqlparser_bind_occurrence_view_t occurrences;
	sqlparser_bind_kind_t expected_kind;
	sqlparser_error_t error;
	const char *expected_key;
	const char *expected_kind_name;
	const char *expected_sql;
	json_t *expected_item;
	json_t *expected_key_json;
	size_t expected_count;
	size_t index;
	size_t position;
	int status;

	if (expected == NULL) {
		return 1;
	}

	memset(&occurrences, 0, sizeof(occurrences));
	memset(&error, 0, sizeof(error));
	status = sqlparser_handle_bind_occurrences(
		handle, &occurrences, &error);
	if (status != SQLPARSER_STATUS_OK) {
		(void)snprintf(
			detail,
			detail_size,
			"bind occurrence enumeration failed code=%d message=%.160s",
			status,
			error.message);
		return 0;
	}
	expected_count = json_array_size(expected);
	if (occurrences.count != expected_count) {
		(void)snprintf(
			detail,
			detail_size,
			"bind occurrence count mismatch expected=%lu actual=%lu",
			(unsigned long)expected_count,
			(unsigned long)occurrences.count);
		return 0;
	}

	json_array_foreach(expected, index, expected_item) {
		memset(&actual, 0, sizeof(actual));
		memset(&error, 0, sizeof(error));
		status = sqlparser_bind_occurrence_at(
			&occurrences, index, &actual, &error);
		if (status != SQLPARSER_STATUS_OK) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu] access failed code=%d message=%.140s",
				(unsigned long)index,
				status,
				error.message);
			return 0;
		}
		position = (size_t)json_integer_value(
			json_object_get(expected_item, "position"));
		expected_kind_name = json_string_value(
			json_object_get(expected_item, "kind"));
		expected_kind = strcmp(expected_kind_name, "named") == 0 ?
			SQLPARSER_BIND_KIND_NAMED :
			SQLPARSER_BIND_KIND_POSITIONAL;
		expected_key_json = json_object_get(expected_item, "key");
		expected_key = json_is_null(expected_key_json) ? NULL :
			json_string_value(expected_key_json);
		expected_sql = json_string_value(
			json_object_get(expected_item, "sql"));
		if (actual.position != position ||
		    actual.kind != expected_kind ||
		    ((actual.key == NULL) != (expected_key == NULL)) ||
		    (actual.key != NULL &&
		     strcmp(actual.key, expected_key) != 0) ||
		    actual.sql == NULL || strcmp(actual.sql, expected_sql) != 0) {
			(void)snprintf(
				detail,
				detail_size,
				"bind_occurrences[%lu] mismatch",
				(unsigned long)index);
			return 0;
		}
	}
	return 1;
}

static int sqlparser_case_parse_dialect(
	const char *name,
	sqlparser_dialect_t *out_dialect)
{
	if (name == NULL || out_dialect == NULL) {
		return 0;
	}
	if (strcmp(name, "postgresql") == 0 ||
	    strcmp(name, "postgres") == 0 ||
	    strcmp(name, "pg") == 0) {
		*out_dialect = SQLPARSER_DIALECT_POSTGRESQL;
		return 1;
	}
	if (strcmp(name, "mysql") == 0) {
		*out_dialect = SQLPARSER_DIALECT_MYSQL;
		return 1;
	}
	if (strcmp(name, "oracle") == 0) {
		*out_dialect = SQLPARSER_DIALECT_ORACLE;
		return 1;
	}
	if (strcmp(name, "sqlserver") == 0 || strcmp(name, "mssql") == 0) {
		*out_dialect = SQLPARSER_DIALECT_SQLSERVER;
		return 1;
	}
	if (strcmp(name, "dameng") == 0 || strcmp(name, "dm") == 0) {
		*out_dialect = SQLPARSER_DIALECT_DAMENG;
		return 1;
	}
	if (strcmp(name, "vastbase-oracle") == 0 || strcmp(name, "vastbase") == 0) {
		*out_dialect = SQLPARSER_DIALECT_VASTBASE_ORACLE;
		return 1;
	}
	if (strcmp(name, "vastbase-mysql") == 0) {
		*out_dialect = SQLPARSER_DIALECT_VASTBASE_MYSQL;
		return 1;
	}
	if (strcmp(name, "vastbase-postgresql") == 0 ||
	    strcmp(name, "vastbase-postgres") == 0 ||
	    strcmp(name, "vastbase-pg") == 0) {
		*out_dialect = SQLPARSER_DIALECT_VASTBASE_POSTGRESQL;
		return 1;
	}
	if (strcmp(name, "vastbase-sqlserver") == 0 ||
	    strcmp(name, "vastbase-mssql") == 0) {
		*out_dialect = SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
		return 1;
	}
	return 0;
}

static int sqlparser_case_decode_pointer_token(
	const char *start,
	size_t length,
	char *token,
	char *detail,
	size_t detail_size)
{
	size_t input_index;
	size_t output_index;

	output_index = 0U;
	for (input_index = 0U; input_index < length; input_index++) {
		if (start[input_index] != '~') {
			token[output_index++] = start[input_index];
			continue;
		}
		if (input_index + 1U >= length ||
		    (start[input_index + 1U] != '0' &&
		     start[input_index + 1U] != '1')) {
			(void)snprintf(detail, detail_size, "invalid JSON Pointer escape");
			return 0;
		}
		token[output_index++] = start[input_index + 1U] == '0' ? '~' : '/';
		input_index++;
	}
	token[output_index] = '\0';
	return 1;
}

static json_t *sqlparser_case_resolve_pointer(
	json_t *root,
	const char *pointer,
	char *detail,
	size_t detail_size)
{
	const char *cursor;
	const char *separator;
	char *token;
	json_t *current;
	size_t pointer_length;

	if (root == NULL || pointer == NULL || pointer[0] != '/') {
		(void)snprintf(detail, detail_size, "target is not an absolute JSON Pointer");
		return NULL;
	}
	pointer_length = strlen(pointer);
	token = (char *)malloc(pointer_length + 1U);
	if (token == NULL) {
		(void)snprintf(detail, detail_size, "JSON Pointer allocation failed");
		return NULL;
	}

	current = root;
	cursor = pointer + 1U;
	for (;;) {
		unsigned long long array_index;
		char *end;
		size_t segment_length;

		separator = strchr(cursor, '/');
		segment_length = separator != NULL ?
			(size_t)(separator - cursor) : strlen(cursor);
		if (!sqlparser_case_decode_pointer_token(
			    cursor,
			    segment_length,
			    token,
			    detail,
			    detail_size)) {
			free(token);
			return NULL;
		}

		if (json_is_object(current)) {
			current = json_object_get(current, token);
		} else if (json_is_array(current)) {
			size_t digit_index;

			if (token[0] == '\0') {
				(void)snprintf(detail, detail_size, "empty array index in JSON Pointer");
				free(token);
				return NULL;
			}
			if (token[0] == '0' && token[1] != '\0') {
				(void)snprintf(detail, detail_size, "invalid array index '%s'", token);
				free(token);
				return NULL;
			}
			for (digit_index = 0U; token[digit_index] != '\0'; digit_index++) {
				if (token[digit_index] < '0' || token[digit_index] > '9') {
					(void)snprintf(detail, detail_size, "invalid array index '%s'", token);
					free(token);
					return NULL;
				}
			}
			errno = 0;
			end = NULL;
			array_index = strtoull(token, &end, 10);
			if (errno != 0 || end == token || *end != '\0' ||
			    array_index > (unsigned long long)SIZE_MAX ||
			    array_index >= (unsigned long long)json_array_size(current)) {
				(void)snprintf(detail, detail_size, "invalid array index '%s'", token);
				free(token);
				return NULL;
			}
			current = json_array_get(current, (size_t)array_index);
		} else {
			(void)snprintf(detail, detail_size, "JSON Pointer crosses a scalar value");
			free(token);
			return NULL;
		}

		if (current == NULL) {
			(void)snprintf(detail, detail_size, "JSON Pointer component '%s' was not found", token);
			free(token);
			return NULL;
		}
		if (separator == NULL) {
			break;
		}
		cursor = separator + 1U;
	}

	free(token);
	return current;
}

static int sqlparser_case_patch_op(
	const char *action,
	sqlparser_patch_op_t *out_op)
{
	if (action == NULL || out_op == NULL) {
		return 0;
	}
	if (strcmp(action, "replace") == 0) {
		*out_op = SQLPARSER_PATCH_REPLACE;
		return 1;
	}
	if (strcmp(action, "insert_column") == 0) {
		*out_op = SQLPARSER_PATCH_INSERT_COLUMN;
		return 1;
	}
	if (strcmp(action, "delete_column") == 0) {
		*out_op = SQLPARSER_PATCH_DELETE_COLUMN;
		return 1;
	}
	if (strcmp(action, "replace_assignment") == 0) {
		*out_op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
		return 1;
	}
	if (strcmp(action, "insert_assignment") == 0) {
		*out_op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
		return 1;
	}
	return 0;
}

static size_t sqlparser_case_first_difference(
	const char *expected,
	const char *actual)
{
	size_t index;

	for (index = 0U;
	     expected[index] != '\0' && actual[index] != '\0';
	     index++) {
		if ((unsigned char)expected[index] != (unsigned char)actual[index]) {
			return index;
		}
	}
	return index;
}

static void sqlparser_case_text_mismatch_detail(
	const char *expected,
	const char *actual,
	char *detail,
	size_t detail_size)
{
	size_t offset;

	offset = sqlparser_case_first_difference(expected, actual);
	(void)snprintf(
		detail,
		detail_size,
		"byte mismatch at offset %lu expected_length=%lu actual_length=%lu",
		(unsigned long)offset,
		(unsigned long)strlen(expected),
		(unsigned long)strlen(actual));
}

static int sqlparser_case_validate_case(
	json_t *item,
	int allow_dialect,
	char *detail,
	size_t detail_size)
{
	static const char *const fixed_keys[] = {
		"name", "sql", "view", "patch", "status", "bind_occurrences"
	};
	static const char *const mixed_keys[] = {
		"name", "sql", "view", "patch", "status", "dialect",
		"bind_occurrences"
	};
	json_t *bind_occurrences;
	json_t *patches;
	json_t *status;
	json_t *view;
	const char *status_text;

	if (!json_is_object(item)) {
		(void)snprintf(detail, detail_size, "case is not an object");
		return 0;
	}
	if (!sqlparser_case_object_has_only(
		    item,
		    allow_dialect ? mixed_keys : fixed_keys,
		    allow_dialect ?
			(sizeof(mixed_keys) / sizeof(mixed_keys[0])) :
			(sizeof(fixed_keys) / sizeof(fixed_keys[0])),
		    detail,
		    detail_size)) {
		return 0;
	}
	if (sqlparser_case_required_string(item, "name") == NULL ||
	    sqlparser_case_required_string(item, "sql") == NULL) {
		(void)snprintf(detail, detail_size, "name and sql must be non-empty strings");
		return 0;
	}
	status = json_object_get(item, "status");
	if (!json_is_string(status)) {
		(void)snprintf(detail, detail_size, "status must be a string");
		return 0;
	}
	status_text = json_string_value(status);
	if (strcmp(status_text, "final") != 0 &&
	    strcmp(status_text, "draft") != 0) {
		(void)snprintf(detail, detail_size, "status must be 'final' or 'draft'");
		return 0;
	}
	view = json_object_get(item, "view");
	if (!json_is_object(view) ||
	    !json_is_array(json_object_get(view, "statements"))) {
		(void)snprintf(detail, detail_size, "view.statements must be an array");
		return 0;
	}
	patches = json_object_get(item, "patch");
	if (patches != NULL && !json_is_array(patches)) {
		(void)snprintf(detail, detail_size, "patch must be an array when present");
		return 0;
	}
	bind_occurrences = json_object_get(item, "bind_occurrences");
	if (!sqlparser_case_validate_bind_occurrences(
		    bind_occurrences, detail, detail_size)) {
		return 0;
	}
	return 1;
}

static int sqlparser_case_prepare_patch(
	json_t *patch_json,
	json_t *expected_view,
	sqlparser_patch_t *out_patch,
	const char **out_action,
	const char **out_target,
	const char **out_expected_sql,
	char *detail,
	size_t detail_size)
{
	static const char *const normal_keys[] = {
		"action", "target", "value", "deparse", "bind_occurrences"
	};
	static const char *const insert_keys[] = {
		"action", "target", "value", "index", "deparse",
		"bind_occurrences"
	};
	static const char *const pair_insert_keys[] = {
		"action", "target", "value", "index", "name", "deparse",
		"bind_occurrences"
	};
	static const char *const delete_keys[] = {
		"action", "target", "index", "deparse", "bind_occurrences"
	};
	const char *action;
	const char *expected_sql;
	const char *name;
	const char *target;
	const char *value;
	const char *selector;
	const char *const *allowed_keys;
	size_t allowed_count;
	int pair_insert;
	json_int_t index;
	json_t *index_json;
	json_t *resolved;
	sqlparser_error_t selector_error;
	sqlparser_patch_op_t op;
	sqlparser_selector_t parsed_selector;

	*out_action = NULL;
	*out_target = NULL;
	*out_expected_sql = NULL;
	if (!json_is_object(patch_json)) {
		(void)snprintf(detail, detail_size, "patch is not an object");
		return 0;
	}
	action = sqlparser_case_required_string(patch_json, "action");
	target = sqlparser_case_required_string(patch_json, "target");
	expected_sql = sqlparser_case_required_string(patch_json, "deparse");
	*out_action = action;
	*out_target = target;
	*out_expected_sql = expected_sql;
	if (action == NULL || target == NULL || expected_sql == NULL) {
		(void)snprintf(detail, detail_size, "patch strings must be non-empty");
		return 0;
	}
	if (!sqlparser_case_patch_op(action, &op)) {
		(void)snprintf(detail, detail_size, "unsupported patch action '%s'", action);
		return 0;
	}
	if (target[0] == '/') {
		resolved = sqlparser_case_resolve_pointer(
			expected_view, target, detail, detail_size);
		if (!json_is_string(resolved) ||
		    (selector = json_string_value(resolved)) == NULL ||
		    selector[0] == '\0') {
			if (resolved != NULL) {
				(void)snprintf(detail, detail_size, "patch target does not resolve to a selector string");
			}
			return 0;
		}
	} else {
		selector = target;
	}
	memset(&selector_error, 0, sizeof(selector_error));
	memset(&parsed_selector, 0, sizeof(parsed_selector));
	if (sqlparser_selector_parse(
		    selector,
		    &parsed_selector,
		    &selector_error) != SQLPARSER_STATUS_OK) {
		(void)snprintf(
			detail,
			detail_size,
			"patch target selector is invalid: %s",
			selector_error.message);
		return 0;
	}
	pair_insert =
		op == SQLPARSER_PATCH_INSERT_COLUMN &&
		(parsed_selector.kind ==
			 SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS ||
		 parsed_selector.kind ==
			 SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS ||
		 (parsed_selector.kind ==
			  SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS &&
		  json_object_get(patch_json, "name") != NULL));
	if (pair_insert) {
		allowed_keys = pair_insert_keys;
		allowed_count = sizeof(pair_insert_keys) / sizeof(pair_insert_keys[0]);
	} else if (op == SQLPARSER_PATCH_INSERT_COLUMN) {
		allowed_keys = insert_keys;
		allowed_count = sizeof(insert_keys) / sizeof(insert_keys[0]);
	} else if (op == SQLPARSER_PATCH_DELETE_COLUMN) {
		allowed_keys = delete_keys;
		allowed_count = sizeof(delete_keys) / sizeof(delete_keys[0]);
	} else {
		allowed_keys = normal_keys;
		allowed_count = sizeof(normal_keys) / sizeof(normal_keys[0]);
	}
	if (!sqlparser_case_object_has_only(
		    patch_json,
		    allowed_keys,
		    allowed_count,
		    detail,
		    detail_size)) {
		return 0;
	}
	value = NULL;
	if (op != SQLPARSER_PATCH_DELETE_COLUMN &&
	    (value = sqlparser_case_required_string(patch_json, "value")) == NULL) {
		(void)snprintf(detail, detail_size, "patch value must be a non-empty string");
		return 0;
	}
	name = NULL;
	if (pair_insert &&
	    (name = sqlparser_case_required_string(patch_json, "name")) == NULL) {
		(void)snprintf(detail, detail_size, "insert_column name must be a non-empty string");
		return 0;
	}

	memset(out_patch, 0, sizeof(*out_patch));
	out_patch->op = op;
	out_patch->selector = selector;
	if (pair_insert) {
		out_patch->name = name;
		out_patch->default_sql = value;
	} else if (op != SQLPARSER_PATCH_DELETE_COLUMN) {
		out_patch->sql = value;
	}
	if (op == SQLPARSER_PATCH_INSERT_COLUMN ||
	    op == SQLPARSER_PATCH_DELETE_COLUMN) {
		index_json = json_object_get(patch_json, "index");
		if (!json_is_integer(index_json) ||
		    (index = json_integer_value(index_json)) < 0 ||
		    (uintmax_t)index > (uintmax_t)SIZE_MAX) {
			(void)snprintf(detail, detail_size, "%s index is invalid", action);
			return 0;
		}
		out_patch->index = (size_t)index;
	}
	if (!sqlparser_case_validate_bind_occurrences(
		    json_object_get(patch_json, "bind_occurrences"),
		    detail,
		    detail_size)) {
		return 0;
	}

	return 1;
}

static int sqlparser_case_run_patch(
	const sqlparser_case_context_t *context,
	const char *sql,
	json_t *expected_view,
	json_t *patch_json,
	size_t patch_index,
	sqlparser_case_runner_stats_t *stats)
{
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patches;
	sqlparser_patch_t patch;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *roundtrip;
	const char *action;
	const char *expected_sql;
	const char *target;
	char *actual_sql;
	char *patched_view_text;
	char *roundtrip_sql;
	char *roundtrip_view_text;
	char detail[256];
	int status;

	action = NULL;
	target = NULL;
	expected_sql = NULL;
	memset(detail, 0, sizeof(detail));
	if (!sqlparser_case_prepare_patch(
		    patch_json,
		    expected_view,
		    &patch,
		    &action,
		    &target,
		    &expected_sql,
		    detail,
		    sizeof(detail))) {
		stats->runner_errors++;
		sqlparser_case_report_failure(
			stats,
			context,
			SQLPARSER_CASE_STAGE_PATCH_DEPARSE,
			patch_index,
			action,
			target,
			detail);
		stats->patches_failed++;
		return 0;
	}

	handle = NULL;
	roundtrip = NULL;
	actual_sql = NULL;
	patched_view_text = NULL;
	roundtrip_sql = NULL;
	roundtrip_view_text = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = context->dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"parse before patch failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}

	patches.items = &patch;
	patches.count = 1U;
	memset(&error, 0, sizeof(error));
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"patch apply failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}
	if (!sqlparser_case_check_bind_occurrences(
		    handle,
		    json_object_get(patch_json, "bind_occurrences"),
		    detail,
		    sizeof(detail))) {
		goto fail;
	}

	memset(&error, 0, sizeof(error));
	status = sqlparser_deparse(handle, &actual_sql, &error);
	if (status != SQLPARSER_STATUS_OK || actual_sql == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"patched deparse failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}
	if (strcmp(expected_sql, actual_sql) != 0) {
		sqlparser_case_text_mismatch_detail(
			expected_sql, actual_sql, detail, sizeof(detail));
		goto fail;
	}

	memset(&error, 0, sizeof(error));
	status = sqlparser_parse_with_options(
		actual_sql, &options, &roundtrip, &error);
	if (status != SQLPARSER_STATUS_OK || roundtrip == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"patched SQL reparse failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}
	memset(&error, 0, sizeof(error));
	status = sqlparser_deparse(roundtrip, &roundtrip_sql, &error);
	if (status != SQLPARSER_STATUS_OK || roundtrip_sql == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"patched SQL round-trip deparse failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}
	if (strcmp(actual_sql, roundtrip_sql) != 0) {
		sqlparser_case_text_mismatch_detail(
			actual_sql, roundtrip_sql, detail, sizeof(detail));
		goto fail;
	}

	memset(&error, 0, sizeof(error));
	status = sqlparser_export_view_json(
		handle,
		0,
		&patched_view_text,
		&error);
	if (status != SQLPARSER_STATUS_OK || patched_view_text == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"patched View export failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}
	memset(&error, 0, sizeof(error));
	status = sqlparser_export_view_json(
		roundtrip,
		0,
		&roundtrip_view_text,
		&error);
	if (status != SQLPARSER_STATUS_OK || roundtrip_view_text == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"fresh View export failed code=%d message=%.180s",
			status,
			error.message);
		goto fail;
	}
	if (strcmp(patched_view_text, roundtrip_view_text) != 0) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"patched/fresh View JSON mismatch");
		goto fail;
	}

	sqlparser_string_free(roundtrip_view_text);
	sqlparser_string_free(patched_view_text);
	sqlparser_string_free(roundtrip_sql);
	sqlparser_string_free(actual_sql);
	sqlparser_handle_destroy(roundtrip);
	sqlparser_handle_destroy(handle);
	stats->patches_passed++;
	return 1;

fail:
	sqlparser_case_report_failure(
		stats,
		context,
		SQLPARSER_CASE_STAGE_PATCH_DEPARSE,
		patch_index,
		action,
		target,
		detail);
	sqlparser_string_free(roundtrip_view_text);
	sqlparser_string_free(patched_view_text);
	sqlparser_string_free(roundtrip_sql);
	sqlparser_string_free(actual_sql);
	sqlparser_handle_destroy(roundtrip);
	sqlparser_handle_destroy(handle);
	stats->patches_failed++;
	return 0;
}

static int sqlparser_case_run_one(
	json_t *item,
	const char *fixture_path,
	size_t case_index,
	sqlparser_dialect_t default_dialect,
	int allow_dialect,
	sqlparser_case_runner_stats_t *stats)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	const char *case_name;
	const char *dialect_name;
	const char *sql;
	const char *status_text;
	char *actual_sql;
	char *view_text;
	char detail[256];
	json_error_t json_error;
	json_t *actual_view;
	json_t *expected_bind_occurrences;
	json_t *dialect_json;
	json_t *expected_view;
	json_t *patch_json;
	json_t *patches;
	json_t *status_json;
	size_t patch_index;
	sqlparser_case_context_t context;
	int case_failed;
	int parse_status;

	case_name = json_is_object(item) ?
		sqlparser_case_required_string(item, "name") : NULL;
	memset(detail, 0, sizeof(detail));
	if (!sqlparser_case_validate_case(
		    item, allow_dialect, detail, sizeof(detail))) {
		sqlparser_case_report_runner_error(
			stats,
			fixture_path,
			case_index,
			case_name,
			detail);
		stats->failed++;
		return 0;
	}

	status_json = json_object_get(item, "status");
	status_text = json_string_value(status_json);
	if (strcmp(status_text, "final") != 0) {
		stats->skipped++;
		return 1;
	}

	context.fixture_path = fixture_path;
	context.case_index = case_index;
	context.case_name = case_name;
	context.dialect = default_dialect;
	dialect_json = json_object_get(item, "dialect");
	if (dialect_json != NULL) {
		if (!allow_dialect || !json_is_string(dialect_json)) {
			(void)snprintf(detail, sizeof(detail), "dialect override is not allowed here");
			sqlparser_case_report_runner_error(
				stats, fixture_path, case_index, case_name, detail);
			stats->failed++;
			return 0;
		}
		dialect_name = json_string_value(dialect_json);
		if (!sqlparser_case_parse_dialect(dialect_name, &context.dialect)) {
			(void)snprintf(detail, sizeof(detail), "unsupported dialect '%s'", dialect_name);
			sqlparser_case_report_runner_error(
				stats, fixture_path, case_index, case_name, detail);
			stats->failed++;
			return 0;
		}
	}

	sql = json_string_value(json_object_get(item, "sql"));
	expected_view = json_object_get(item, "view");
	expected_bind_occurrences = json_object_get(item, "bind_occurrences");
	patches = json_object_get(item, "patch");
	stats->patches += patches != NULL ? json_array_size(patches) : 0U;

	case_failed = 0;
	handle = NULL;
	actual_sql = NULL;
	view_text = NULL;
	actual_view = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = context.dialect;
	parse_status = sqlparser_parse_with_options(
		sql, &options, &handle, &error);
	if (parse_status != SQLPARSER_STATUS_OK || handle == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"parse failed code=%d message=%.180s",
			parse_status,
			error.message);
		sqlparser_case_report_failure(
			stats,
			&context,
			SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE,
			SIZE_MAX,
			NULL,
			NULL,
			detail);
		stats->patches_not_run +=
			patches != NULL ? json_array_size(patches) : 0U;
		case_failed = 1;
		goto done;
	}
	if (!sqlparser_case_check_bind_occurrences(
		    handle,
		    expected_bind_occurrences,
		    detail,
		    sizeof(detail))) {
		sqlparser_case_report_failure(
			stats,
			&context,
			SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE,
			SIZE_MAX,
			NULL,
			NULL,
			detail);
		case_failed = 1;
	}

	memset(&error, 0, sizeof(error));
	parse_status = sqlparser_deparse(handle, &actual_sql, &error);
	if (parse_status != SQLPARSER_STATUS_OK || actual_sql == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"deparse failed code=%d message=%.180s",
			parse_status,
			error.message);
		sqlparser_case_report_failure(
			stats,
			&context,
			SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE,
			SIZE_MAX,
			NULL,
			NULL,
			detail);
		case_failed = 1;
	} else if (strcmp(sql, actual_sql) != 0) {
		sqlparser_case_text_mismatch_detail(
			sql, actual_sql, detail, sizeof(detail));
		sqlparser_case_report_failure(
			stats,
			&context,
			SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE,
			SIZE_MAX,
			NULL,
			NULL,
			detail);
		case_failed = 1;
	}
	sqlparser_string_free(actual_sql);
	actual_sql = NULL;

	memset(&error, 0, sizeof(error));
	parse_status = sqlparser_export_view_json(
		handle, 0, &view_text, &error);
	if (parse_status != SQLPARSER_STATUS_OK || view_text == NULL) {
		(void)snprintf(
			detail,
			sizeof(detail),
			"view export failed code=%d message=%.180s",
			parse_status,
			error.message);
		sqlparser_case_report_failure(
			stats,
			&context,
			SQLPARSER_CASE_STAGE_VIEW,
			SIZE_MAX,
			NULL,
			NULL,
			detail);
		case_failed = 1;
	} else {
		memset(&json_error, 0, sizeof(json_error));
		actual_view = json_loads(
			view_text, JSON_REJECT_DUPLICATES, &json_error);
		if (actual_view == NULL) {
			(void)snprintf(
				detail,
				sizeof(detail),
				"exported view is invalid JSON line=%d text=%s",
				json_error.line,
				json_error.text);
			sqlparser_case_report_failure(
				stats,
				&context,
				SQLPARSER_CASE_STAGE_VIEW,
				SIZE_MAX,
				NULL,
				NULL,
				detail);
			case_failed = 1;
		} else if (!json_equal(expected_view, actual_view)) {
			sqlparser_case_report_failure(
				stats,
				&context,
				SQLPARSER_CASE_STAGE_VIEW,
				SIZE_MAX,
				NULL,
				NULL,
				"exact JSON mismatch");
			case_failed = 1;
		}
	}
	json_decref(actual_view);
	actual_view = NULL;
	sqlparser_string_free(view_text);
	view_text = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	if (patches != NULL) {
		json_array_foreach(patches, patch_index, patch_json) {
			if (!sqlparser_case_run_patch(
				    &context,
				    sql,
				    expected_view,
				    patch_json,
				    patch_index,
				    stats)) {
				case_failed = 1;
			}
		}
	}

done:
	sqlparser_string_free(actual_sql);
	sqlparser_string_free(view_text);
	json_decref(actual_view);
	sqlparser_handle_destroy(handle);
	if (case_failed) {
		stats->failed++;
		return 0;
	}
	stats->passed++;
	return 1;
}

static int sqlparser_case_runner_run(
	const char *fixture_path,
	sqlparser_dialect_t default_dialect,
	int allow_dialect)
{
	json_error_t error;
	json_t *item;
	json_t *items;
	json_t *root;
	sqlparser_case_runner_stats_t stats;
	size_t case_index;

	memset(&stats, 0, sizeof(stats));
	memset(&error, 0, sizeof(error));
	root = json_load_file(fixture_path, JSON_REJECT_DUPLICATES, &error);
	if (root == NULL) {
		fprintf(
			stderr,
			"RUNNER ERROR fixture=%s detail=load failed line=%d text=%s\n",
			fixture_path,
			error.line,
			error.text);
		return 1;
	}
	if (!json_is_object(root) ||
	    json_object_size(root) != 1U ||
	    !json_is_array(json_object_get(root, "items"))) {
		fprintf(
			stderr,
			"RUNNER ERROR fixture=%s detail=root must contain only an items array\n",
			fixture_path);
		json_decref(root);
		return 1;
	}

	items = json_object_get(root, "items");
	json_array_foreach(items, case_index, item) {
		stats.cases++;
		(void)sqlparser_case_run_one(
			item,
			fixture_path,
			case_index,
			default_dialect,
			allow_dialect,
			&stats);
	}
	json_decref(root);

	printf(
		"CASE RUNNER fixture=%s cases=%lu passed=%lu failed=%lu skipped=%lu "
		"patches=%lu patch_passed=%lu patch_failed=%lu patch_not_run=%lu "
		"original_deparse_failures=%lu view_failures=%lu "
		"patch_deparse_failures=%lu runner_errors=%lu\n",
		fixture_path,
		(unsigned long)stats.cases,
		(unsigned long)stats.passed,
		(unsigned long)stats.failed,
		(unsigned long)stats.skipped,
		(unsigned long)stats.patches,
		(unsigned long)stats.patches_passed,
		(unsigned long)stats.patches_failed,
		(unsigned long)stats.patches_not_run,
		(unsigned long)stats.stage_failures[
			SQLPARSER_CASE_STAGE_ORIGINAL_DEPARSE],
		(unsigned long)stats.stage_failures[
			SQLPARSER_CASE_STAGE_VIEW],
		(unsigned long)stats.stage_failures[
			SQLPARSER_CASE_STAGE_PATCH_DEPARSE],
		(unsigned long)stats.runner_errors);

	return stats.failed != 0U || stats.runner_errors != 0U ? 1 : 0;
}

#endif
