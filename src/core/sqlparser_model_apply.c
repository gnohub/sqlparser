#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser_model_internal.h"

static const char *sqlparser_json_string_or_null(json_t *value)
{
	if (!json_is_string(value)) {
		return NULL;
	}

	return json_string_value(value);
}

static sqlparser_status_t sqlparser_model_entry_selector(
	json_t *entry,
	const char **out_selector,
	sqlparser_error_t *out_error)
{
	const char *selector_text;

	if (out_selector == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_selector must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_selector = NULL;
	if (!json_is_object(entry)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model change entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	selector_text = sqlparser_json_string_or_null(json_object_get(entry, "selector"));
	if (selector_text == NULL || selector_text[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model change entry requires string field 'selector'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_selector = selector_text;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_index_entry(
	json_t *index,
	json_t *entry,
	sqlparser_error_t *out_error)
{
	const char *selector_text;
	sqlparser_status_t status;

	if (index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_model_entry_selector(entry, &selector_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (json_object_set(index, selector_text, entry) != 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_index_change_array(
	json_t *index,
	json_t *array,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	json_t *entry;

	if (array == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!json_is_array(array)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model change list must be an array");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	json_array_foreach(array, item_index, entry) {
		sqlparser_status_t status;

		status = sqlparser_model_index_entry(index, entry, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_index_insert_rows(
	json_t *index,
	json_t *insert_object,
	sqlparser_error_t *out_error)
{
	size_t row_index;
	json_t *rows;
	json_t *row_object;

	if (insert_object == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!json_is_object(insert_object)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert model entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	rows = json_object_get(insert_object, "rows");
	if (rows == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!json_is_array(rows)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert rows must be an array");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	json_array_foreach(rows, row_index, row_object) {
		sqlparser_status_t status;

		if (!json_is_object(row_object)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"insert row entry must be an object");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}

		status = sqlparser_model_index_change_array(
			index,
			json_object_get(row_object, "cells"),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_index_statement(
	json_t *index,
	json_t *statement_object,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (!json_is_object(statement_object)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement model entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_model_index_change_array(
		index,
		json_object_get(statement_object, "relations"),
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_index_change_array(
			index,
			json_object_get(statement_object, "names"),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_index_change_array(
			index,
			json_object_get(statement_object, "literals"),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_index_change_array(
			index,
			json_object_get(statement_object, "where_literals"),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_index_change_array(
			index,
			json_object_get(statement_object, "update_assignments"),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_index_insert_rows(
			index,
			json_object_get(statement_object, "insert"),
			out_error);
	}

	return status;
}

static sqlparser_status_t sqlparser_model_build_selector_index(
	json_t *root,
	json_t **out_index,
	sqlparser_error_t *out_error)
{
	json_t *index;
	json_t *statements;
	json_t *statement_object;
	size_t statement_index;

	if (out_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_index = NULL;
	if (!json_is_object(root)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model root must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	statements = json_object_get(root, "statements");
	if (!json_is_array(statements)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON must contain array field 'statements'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	index = json_object();
	if (index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	json_array_foreach(statements, statement_index, statement_object) {
		sqlparser_status_t status;

		status = sqlparser_model_index_statement(index, statement_object, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(index);
			return status;
		}
	}

	*out_index = index;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_changed_entry(
	json_t *changes,
	json_t *entry,
	json_t *baseline_index,
	sqlparser_error_t *out_error)
{
	const char *selector_text;
	json_t *baseline_entry;
	json_t *entry_copy;
	sqlparser_status_t status;

	if (changes == NULL || baseline_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"change collection requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_model_entry_selector(entry, &selector_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	baseline_entry = json_object_get(baseline_index, selector_text);
	if (baseline_entry != NULL && json_equal(entry, baseline_entry)) {
		return SQLPARSER_STATUS_OK;
	}

	entry_copy = json_deep_copy(entry);
	if (entry_copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (json_array_append_new(changes, entry_copy) != 0) {
		json_decref(entry_copy);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_changed_array(
	json_t *changes,
	json_t *array,
	json_t *baseline_index,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	json_t *entry;

	if (array == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!json_is_array(array)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model change list must be an array");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	json_array_foreach(array, item_index, entry) {
		sqlparser_status_t status;

		status = sqlparser_model_append_changed_entry(changes, entry, baseline_index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_changed_insert_rows(
	json_t *changes,
	json_t *insert_object,
	json_t *baseline_index,
	sqlparser_error_t *out_error)
{
	size_t row_index;
	json_t *rows;
	json_t *row_object;

	if (insert_object == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!json_is_object(insert_object)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert model entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	rows = json_object_get(insert_object, "rows");
	if (rows == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!json_is_array(rows)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert rows must be an array");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	json_array_foreach(rows, row_index, row_object) {
		sqlparser_status_t status;

		if (!json_is_object(row_object)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"insert row entry must be an object");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}

		status = sqlparser_model_append_changed_array(
			changes,
			json_object_get(row_object, "cells"),
			baseline_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_collect_statement_changes(
	json_t *changes,
	json_t *statement_object,
	json_t *baseline_index,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (!json_is_object(statement_object)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement model entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_model_append_changed_array(
		changes,
		json_object_get(statement_object, "relations"),
		baseline_index,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_changed_array(
			changes,
			json_object_get(statement_object, "names"),
			baseline_index,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_changed_array(
			changes,
			json_object_get(statement_object, "literals"),
			baseline_index,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_changed_array(
			changes,
			json_object_get(statement_object, "where_literals"),
			baseline_index,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_changed_array(
			changes,
			json_object_get(statement_object, "update_assignments"),
			baseline_index,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_changed_insert_rows(
			changes,
			json_object_get(statement_object, "insert"),
			baseline_index,
			out_error);
	}

	return status;
}

static sqlparser_status_t sqlparser_apply_relation_change(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	json_t *change,
	sqlparser_error_t *out_error)
{
	sqlparser_relation_view_t current;
	const char *schema_name;
	const char *table_name;
	json_t *value_json;
	sqlparser_status_t status;

	memset(&current, 0, sizeof(current));
	status = sqlparser_selector_relation(handle, selector, &current, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	value_json = json_object_get(change, "schema_name");
	if (json_is_null(value_json)) {
		schema_name = NULL;
	} else if (json_is_string(value_json)) {
		schema_name = json_string_value(value_json);
	} else {
		schema_name = current.schema_name;
	}

	value_json = json_object_get(change, "table_name");
	if (json_is_string(value_json)) {
		table_name = json_string_value(value_json);
	} else {
		table_name = current.table_name;
	}

	if (table_name == NULL || table_name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation change requires table_name");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (sqlparser_model_strings_equal_nullable(schema_name, current.schema_name) &&
	    sqlparser_model_strings_equal_nullable(table_name, current.table_name)) {
		return SQLPARSER_STATUS_OK;
	}

	return sqlparser_selector_set_relation_name(handle, selector, schema_name, table_name, out_error);
}

static sqlparser_status_t sqlparser_apply_name_change(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	json_t *change,
	sqlparser_error_t *out_error)
{
	sqlparser_name_view_t current;
	const char *value;
	sqlparser_status_t status;

	value = sqlparser_json_string_or_null(json_object_get(change, "value"));
	if (value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name change requires string field 'value'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&current, 0, sizeof(current));
	status = sqlparser_selector_name(handle, selector, &current, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_model_strings_equal_nullable(current.value, value)) {
		return SQLPARSER_STATUS_OK;
	}

	return sqlparser_selector_set_name(handle, selector, value, out_error);
}

static sqlparser_status_t sqlparser_apply_literal_like_change(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	json_t *change,
	sqlparser_error_t *out_error)
{
	sqlparser_literal_value_t target;
	json_t *literal_json;
	const char *sql_text;
	sqlparser_status_t status;

	memset(&target, 0, sizeof(target));
	literal_json = json_object_get(change, "literal");
	sql_text = sqlparser_json_string_or_null(json_object_get(change, "sql"));

	switch (selector->kind) {
		case SQLPARSER_SELECTOR_KIND_LITERAL:
		{
			sqlparser_literal_view_t current;

			if (!json_is_object(literal_json)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INVALID_ARGUMENT,
					"literal change requires object field 'literal'");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			status = sqlparser_model_literal_value_from_json(literal_json, &target, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			memset(&current, 0, sizeof(current));
			status = sqlparser_selector_literal(handle, selector, &current, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (sqlparser_model_literal_view_equals_value(&current, &target)) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_selector_set_literal(handle, selector, &target, out_error);
		}
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
		{
			sqlparser_where_literal_view_t current;

			if (!json_is_object(literal_json)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INVALID_ARGUMENT,
					"literal change requires object field 'literal'");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			status = sqlparser_model_literal_value_from_json(literal_json, &target, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			memset(&current, 0, sizeof(current));
			status = sqlparser_selector_where_literal(handle, selector, &current, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (sqlparser_model_literal_view_equals_value(&current.literal, &target)) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_selector_set_where_literal(handle, selector, &target, out_error);
		}
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
		{
			sqlparser_assignment_view_t current;

			memset(&current, 0, sizeof(current));
			if (json_is_object(literal_json)) {
				status = sqlparser_model_literal_value_from_json(literal_json, &target, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				status = sqlparser_selector_update_assignment(handle, selector, &current, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (current.value_kind == SQLPARSER_VALUE_KIND_LITERAL) {
					if (!sqlparser_model_literal_view_equals_value(&current.literal, &target)) {
						return sqlparser_selector_set_update_assignment_literal(
							handle,
							selector,
							&target,
							out_error);
					}
					if (sql_text == NULL) {
						return SQLPARSER_STATUS_OK;
					}
				} else if (sql_text == NULL) {
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_UNSUPPORTED,
						"assignment selector does not point to a literal assignment");
					return SQLPARSER_STATUS_UNSUPPORTED;
				}
			} else if (sql_text == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INVALID_ARGUMENT,
					"assignment change requires field 'sql' or object field 'literal'");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			if (sql_text != NULL) {
				char *current_sql;

				current_sql = NULL;
				status = sqlparser_selector_update_assignment_sql(
					handle,
					selector,
					&current_sql,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (sqlparser_model_strings_equal_nullable(current_sql, sql_text)) {
					sqlparser_string_free(current_sql);
					return SQLPARSER_STATUS_OK;
				}
				sqlparser_string_free(current_sql);
				return sqlparser_selector_set_update_assignment_sql(
					handle,
					selector,
					sql_text,
					out_error);
			}

			return SQLPARSER_STATUS_OK;
		}
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
		{
			sqlparser_literal_view_t current;

			memset(&current, 0, sizeof(current));
			if (json_is_object(literal_json)) {
				status = sqlparser_model_literal_value_from_json(literal_json, &target, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				status = sqlparser_selector_insert_cell_literal(handle, selector, &current, out_error);
				if (status == SQLPARSER_STATUS_OK) {
					if (!sqlparser_model_literal_view_equals_value(&current, &target)) {
						return sqlparser_selector_set_insert_cell_literal(
							handle,
							selector,
							&target,
							out_error);
					}
					if (sql_text == NULL) {
						return SQLPARSER_STATUS_OK;
					}
				} else if (status != SQLPARSER_STATUS_UNSUPPORTED || sql_text == NULL) {
					return status;
				}
				sqlparser_error_clear(out_error);
			} else if (sql_text == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INVALID_ARGUMENT,
					"insert cell change requires field 'sql' or object field 'literal'");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			if (sql_text != NULL) {
				char *current_sql;

				current_sql = NULL;
				status = sqlparser_selector_insert_cell_sql(
					handle,
					selector,
					&current_sql,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (sqlparser_model_strings_equal_nullable(current_sql, sql_text)) {
					sqlparser_string_free(current_sql);
					return SQLPARSER_STATUS_OK;
				}
				sqlparser_string_free(current_sql);
				return sqlparser_selector_set_insert_cell_sql(
					handle,
					selector,
					sql_text,
					out_error);
			}

			return SQLPARSER_STATUS_OK;
		}
		case SQLPARSER_SELECTOR_KIND_UNKNOWN:
		case SQLPARSER_SELECTOR_KIND_RELATION:
		case SQLPARSER_SELECTOR_KIND_NAME:
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"selector kind does not accept literal changes");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
}

static sqlparser_status_t sqlparser_apply_change_object(
	sqlparser_handle_t *handle,
	json_t *change,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	const char *selector_text;
	sqlparser_status_t status;

	if (!json_is_object(change)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"change entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	selector_text = sqlparser_json_string_or_null(json_object_get(change, "selector"));
	if (selector_text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"change entry requires string field 'selector'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&selector, 0, sizeof(selector));
	status = sqlparser_selector_parse(selector_text, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	switch (selector.kind) {
		case SQLPARSER_SELECTOR_KIND_RELATION:
			return sqlparser_apply_relation_change(handle, &selector, change, out_error);
		case SQLPARSER_SELECTOR_KIND_NAME:
			return sqlparser_apply_name_change(handle, &selector, change, out_error);
		case SQLPARSER_SELECTOR_KIND_LITERAL:
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			return sqlparser_apply_literal_like_change(handle, &selector, change, out_error);
		case SQLPARSER_SELECTOR_KIND_UNKNOWN:
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"selector kind is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
}

static int sqlparser_change_is_structural_sql(json_t *change)
{
	sqlparser_selector_t selector;
	sqlparser_error_t error;
	const char *selector_text;

	if (!json_is_object(change)) {
		return 0;
	}
	if (!json_is_string(json_object_get(change, "sql")) ||
	    json_is_object(json_object_get(change, "literal"))) {
		return 0;
	}

	selector_text = sqlparser_json_string_or_null(json_object_get(change, "selector"));
	if (selector_text == NULL) {
		return 0;
	}

	memset(&selector, 0, sizeof(selector));
	memset(&error, 0, sizeof(error));
	if (sqlparser_selector_parse(selector_text, &selector, &error) != SQLPARSER_STATUS_OK) {
		return 0;
	}

	return selector.kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT ||
	       selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL;
}

static sqlparser_status_t sqlparser_apply_change_array(
	sqlparser_handle_t *handle,
	json_t *changes,
	sqlparser_error_t *out_error)
{
	int pass;
	size_t index;
	json_t *change;

	if (!json_is_array(changes)) {
		return SQLPARSER_STATUS_OK;
	}

	for (pass = 0; pass < 2; pass++) {
		json_array_foreach(changes, index, change) {
			sqlparser_status_t status;
			int structural_sql;

			structural_sql = sqlparser_change_is_structural_sql(change);
			if ((pass == 0 && structural_sql) || (pass == 1 && !structural_sql)) {
				continue;
			}

			status = sqlparser_apply_change_object(handle, change, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_apply_change_array_transactional(
	sqlparser_handle_t *handle,
	json_t *changes,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *clone;
	sqlparser_status_t status;

	if (!json_is_array(changes)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"changes must be an array");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (json_array_size(changes) == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	clone = NULL;
	status = sqlparser_handle_clone(handle, &clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_apply_change_array(clone, changes, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_handle_replace_contents(handle, clone);
	}
	sqlparser_handle_destroy(clone);
	return status;
}

static sqlparser_status_t sqlparser_model_collect_changed_changes(
	sqlparser_handle_t *handle,
	json_t *root,
	json_t **out_changes,
	sqlparser_error_t *out_error)
{
	char *baseline_text;
	json_t *baseline_root;
	json_t *baseline_index;
	json_t *changes;
	json_t *statements;
	json_t *statement_object;
	json_error_t json_error;
	size_t index;
	sqlparser_status_t status;

	if (out_changes == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_changes must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_changes = NULL;
	baseline_text = NULL;
	baseline_root = NULL;
	baseline_index = NULL;
	changes = NULL;
	memset(&json_error, 0, sizeof(json_error));

	if (!json_is_object(root)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON root must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	statements = json_object_get(root, "statements");
	if (!json_is_array(statements)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON must contain array field 'statements'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_export_model_json(handle, 0, &baseline_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	baseline_root = json_loads(baseline_text, 0, &json_error);
	sqlparser_string_free(baseline_text);
	baseline_text = NULL;
	if (baseline_root == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to parse current model JSON");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	status = sqlparser_model_build_selector_index(baseline_root, &baseline_index, out_error);
	json_decref(baseline_root);
	baseline_root = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	changes = json_array();
	if (changes == NULL) {
		json_decref(baseline_index);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	json_array_foreach(statements, index, statement_object) {
		status = sqlparser_model_collect_statement_changes(
			changes,
			statement_object,
			baseline_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(changes);
			json_decref(baseline_index);
			return status;
		}
	}

	json_decref(baseline_index);
	*out_changes = changes;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_apply_model_json(
	sqlparser_handle_t *handle,
	const char *json_text,
	sqlparser_error_t *out_error)
{
	return sqlparser_apply_model_json_with_limits(handle, json_text, NULL, out_error);
}

sqlparser_status_t sqlparser_apply_model_json_with_limits(
	sqlparser_handle_t *handle,
	const char *json_text,
	const sqlparser_limits_t *limits,
	sqlparser_error_t *out_error)
{
	json_t *root;
	json_t *changes;
	json_t *statements;
	json_t *changed_changes;
	json_error_t json_error;
	sqlparser_status_t status;
	sqlparser_limits_t original_limits;
	sqlparser_limits_t effective_limits;

	sqlparser_error_clear(out_error);
	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (json_text == NULL || json_text[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"json_text must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	original_limits = handle->limits;
	if (limits != NULL) {
		sqlparser_limits_normalize(limits, &effective_limits);
	} else {
		effective_limits = handle->limits;
	}
	handle->limits = effective_limits;
	status = sqlparser_validate_text_limit(
		json_text,
		effective_limits.max_model_json_bytes,
		"model JSON input",
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		handle->limits = original_limits;
		return status;
	}

	root = json_loads(json_text, 0, &json_error);
	if (root == NULL) {
		handle->limits = original_limits;
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"failed to parse model JSON");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (!json_is_object(root)) {
		json_decref(root);
		handle->limits = original_limits;
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON root must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	changes = json_object_get(root, "changes");
	if (json_is_array(changes)) {
		status = sqlparser_apply_change_array_transactional(handle, changes, out_error);
		json_decref(root);
		if (status != SQLPARSER_STATUS_OK) {
			handle->limits = original_limits;
		}
		return status;
	}

	statements = json_object_get(root, "statements");
	if (!json_is_array(statements)) {
		json_decref(root);
		handle->limits = original_limits;
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON must contain 'changes' or 'statements'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	changed_changes = NULL;
	status = sqlparser_model_collect_changed_changes(
		handle,
		root,
		&changed_changes,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_apply_change_array_transactional(handle, changed_changes, out_error);
	}

	if (changed_changes != NULL) {
		json_decref(changed_changes);
	}
	json_decref(root);
	if (status != SQLPARSER_STATUS_OK) {
		handle->limits = original_limits;
	}
	return status;
}
