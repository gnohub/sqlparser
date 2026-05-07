#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser_model_internal.h"

static sqlparser_status_t sqlparser_model_append_relations(
	json_t *statement_object,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	json_t *relations;
	size_t count;
	size_t index;
	sqlparser_status_t status;

	relations = json_array();
	if (relations == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_statement_relation_count(handle, statement_index, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(relations);
		return status;
	}

	for (index = 0U; index < count; index++) {
		sqlparser_relation_view_t relation;
		sqlparser_selector_t selector;
		json_t *entry;

		memset(&relation, 0, sizeof(relation));
		status = sqlparser_statement_relation(handle, statement_index, index, &relation, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(relations);
			return status;
		}

		entry = json_object();
		if (entry == NULL) {
			json_decref(relations);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
		selector.statement_index = statement_index;
		selector.item_index = index;
		status = sqlparser_model_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(relations);
			return status;
		}

		sqlparser_json_object_set_nonempty_string(entry, "schema_name", relation.schema_name);
		sqlparser_json_object_set_nonempty_string(entry, "table_name", relation.table_name);
		sqlparser_json_object_set_nonempty_string(entry, "alias_name", relation.alias_name);
		if (json_array_append_new(relations, entry) != 0) {
			json_decref(entry);
			json_decref(relations);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	if (json_object_set_new(statement_object, "relations", relations) != 0) {
		json_decref(relations);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_names(
	json_t *statement_object,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	json_t *names;
	size_t count;
	size_t index;
	sqlparser_status_t status;

	names = json_array();
	if (names == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_statement_name_count(handle, statement_index, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(names);
		return status;
	}

	for (index = 0U; index < count; index++) {
		sqlparser_name_view_t name;
		sqlparser_selector_t selector;
		json_t *entry;

		memset(&name, 0, sizeof(name));
		status = sqlparser_statement_name(handle, statement_index, index, &name, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(names);
			return status;
		}

		entry = json_object();
		if (entry == NULL) {
			json_decref(names);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
		selector.statement_index = statement_index;
		selector.item_index = index;
		status = sqlparser_model_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(names);
			return status;
		}

		sqlparser_json_object_set_nonempty_string(entry, "owner_type", name.owner_type);
		sqlparser_json_object_set_nonempty_string(entry, "field_name", name.field_name);
		sqlparser_json_object_set_nonempty_string(entry, "value", name.value);
		if (json_array_append_new(names, entry) != 0) {
			json_decref(entry);
			json_decref(names);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	if (json_object_set_new(statement_object, "names", names) != 0) {
		json_decref(names);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_literals(
	json_t *statement_object,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	json_t *literals;
	size_t count;
	size_t index;
	sqlparser_status_t status;

	literals = json_array();
	if (literals == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_statement_literal_count(handle, statement_index, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(literals);
		return status;
	}

	for (index = 0U; index < count; index++) {
		sqlparser_literal_view_t literal;
		sqlparser_selector_t selector;
		json_t *entry;
		json_t *literal_json;

		memset(&literal, 0, sizeof(literal));
		status = sqlparser_statement_literal(handle, statement_index, index, &literal, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(literals);
			return status;
		}

		entry = json_object();
		if (entry == NULL) {
			json_decref(literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_LITERAL;
		selector.statement_index = statement_index;
		selector.item_index = index;
		status = sqlparser_model_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(literals);
			return status;
		}

		literal_json = sqlparser_model_literal_view_to_json(&literal);
		if (literal_json == NULL) {
			json_decref(entry);
			json_decref(literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		if (json_object_set_new(entry, "literal", literal_json) != 0) {
			json_decref(literal_json);
			json_decref(entry);
			json_decref(literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (json_array_append_new(literals, entry) != 0) {
			json_decref(entry);
			json_decref(literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	if (json_object_set_new(statement_object, "literals", literals) != 0) {
		json_decref(literals);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_where_literals(
	json_t *statement_object,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	json_t *where_literals;
	size_t count;
	size_t index;
	sqlparser_status_t status;

	where_literals = json_array();
	if (where_literals == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_statement_where_literal_count(handle, statement_index, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(where_literals);
		return status;
	}

	for (index = 0U; index < count; index++) {
		sqlparser_where_literal_view_t where_literal;
		sqlparser_selector_t selector;
		json_t *entry;
		json_t *literal_json;

		memset(&where_literal, 0, sizeof(where_literal));
		status = sqlparser_statement_where_literal(
			handle,
			statement_index,
			index,
			&where_literal,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(where_literals);
			return status;
		}

		entry = json_object();
		if (entry == NULL) {
			json_decref(where_literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_WHERE_LITERAL;
		selector.statement_index = statement_index;
		selector.item_index = index;
		status = sqlparser_model_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(where_literals);
			return status;
		}

		sqlparser_json_object_set_nonempty_string(entry, "table_name", where_literal.table_name);
		sqlparser_json_object_set_nonempty_string(entry, "column_name", where_literal.column_name);
		sqlparser_json_object_set_nonempty_string(entry, "operator_name", where_literal.operator_name);
		literal_json = sqlparser_model_literal_view_to_json(&where_literal.literal);
		if (literal_json == NULL) {
			json_decref(entry);
			json_decref(where_literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		if (json_object_set_new(entry, "literal", literal_json) != 0) {
			json_decref(literal_json);
			json_decref(entry);
			json_decref(where_literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (json_array_append_new(where_literals, entry) != 0) {
			json_decref(entry);
			json_decref(where_literals);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	if (json_object_set_new(statement_object, "where_literals", where_literals) != 0) {
		json_decref(where_literals);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_update_assignments(
	json_t *statement_object,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	json_t *assignments;
	size_t count;
	size_t index;
	sqlparser_status_t status;

	assignments = json_array();
	if (assignments == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_update_assignment_count(handle, statement_index, &count, out_error);
	if (status == SQLPARSER_STATUS_UNSUPPORTED) {
		status = SQLPARSER_STATUS_OK;
		count = 0U;
	} else if (status != SQLPARSER_STATUS_OK) {
		json_decref(assignments);
		return status;
	}

	for (index = 0U; index < count; index++) {
		sqlparser_assignment_view_t assignment;
		sqlparser_selector_t selector;
		json_t *entry;
		json_t *literal_json;
		char *assignment_sql;

		memset(&assignment, 0, sizeof(assignment));
		assignment_sql = NULL;
		status = sqlparser_update_assignment(handle, statement_index, index, &assignment, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(assignments);
			return status;
		}

		status = sqlparser_update_assignment_sql(
			handle,
			statement_index,
			index,
			&assignment_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(assignments);
			return status;
		}

		entry = json_object();
		if (entry == NULL) {
			sqlparser_string_free(assignment_sql);
			json_decref(assignments);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		selector.statement_index = statement_index;
		selector.item_index = index;
		status = sqlparser_model_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_string_free(assignment_sql);
			json_decref(entry);
			json_decref(assignments);
			return status;
		}

		sqlparser_json_object_set_nonempty_string(entry, "column_name", assignment.column_name);
		status = sqlparser_json_object_set_string(
			entry,
			"value_kind",
			sqlparser_value_kind_name(assignment.value_kind),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_string_free(assignment_sql);
			json_decref(entry);
			json_decref(assignments);
			return status;
		}

		status = sqlparser_json_object_set_string(entry, "sql", assignment_sql, out_error);
		sqlparser_string_free(assignment_sql);
		assignment_sql = NULL;
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(assignments);
			return status;
		}

		if (assignment.value_kind == SQLPARSER_VALUE_KIND_LITERAL) {
			literal_json = sqlparser_model_literal_view_to_json(&assignment.literal);
			if (literal_json == NULL) {
				json_decref(entry);
				json_decref(assignments);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			if (json_object_set_new(entry, "literal", literal_json) != 0) {
				json_decref(literal_json);
				json_decref(entry);
				json_decref(assignments);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
		}

		if (json_array_append_new(assignments, entry) != 0) {
			json_decref(entry);
			json_decref(assignments);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	if (json_object_set_new(statement_object, "update_assignments", assignments) != 0) {
		json_decref(assignments);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_append_insert_model(
	json_t *statement_object,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_status_t status;
	json_t *insert_object;
	json_t *columns;
	json_t *rows;
	size_t column_count;
	size_t row_count;
	size_t row_index;
	size_t column_index;

	status = sqlparser_insert_source_kind(handle, statement_index, &source_kind, out_error);
	if (status == SQLPARSER_STATUS_UNSUPPORTED) {
		return SQLPARSER_STATUS_OK;
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	insert_object = json_object();
	columns = json_array();
	rows = json_array();
	if (insert_object == NULL || columns == NULL || rows == NULL) {
		if (insert_object != NULL) {
			json_decref(insert_object);
		}
		if (columns != NULL) {
			json_decref(columns);
		}
		if (rows != NULL) {
			json_decref(rows);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	(void)json_object_set_new(
		insert_object,
		"source_kind",
		json_string(sqlparser_insert_source_kind_name(source_kind)));
	(void)json_object_set_new(insert_object, "columns", columns);
	(void)json_object_set_new(insert_object, "rows", rows);

	status = sqlparser_insert_column_count(handle, statement_index, &column_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(insert_object);
		return status;
	}

	for (column_index = 0U; column_index < column_count; column_index++) {
		const char *column_name;
		json_t *entry;

		column_name = NULL;
		status = sqlparser_insert_column_name(
			handle,
			statement_index,
			column_index,
			&column_name,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(insert_object);
			return status;
		}

		entry = json_object();
		if (entry == NULL) {
			json_decref(insert_object);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		(void)json_object_set_new(entry, "column_index", json_integer((json_int_t)column_index));
		sqlparser_json_object_set_nonempty_string(entry, "name", column_name);
		if (json_array_append_new(columns, entry) != 0) {
			json_decref(entry);
			json_decref(insert_object);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	status = sqlparser_insert_row_count(handle, statement_index, &row_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(insert_object);
		return status;
	}

	for (row_index = 0U; row_index < row_count; row_index++) {
		json_t *row_object;
		json_t *cells;

		row_object = json_object();
		cells = json_array();
		if (row_object == NULL || cells == NULL) {
			if (row_object != NULL) {
				json_decref(row_object);
			}
			if (cells != NULL) {
				json_decref(cells);
			}
			json_decref(insert_object);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		(void)json_object_set_new(row_object, "row_index", json_integer((json_int_t)row_index));
		(void)json_object_set_new(row_object, "cells", cells);

		for (column_index = 0U; column_index < column_count; column_index++) {
			sqlparser_literal_view_t literal;
			sqlparser_selector_t selector;
			json_t *cell_object;
			json_t *literal_json;
			sqlparser_value_kind_t value_kind;
			char *cell_sql;

			cell_object = json_object();
			if (cell_object == NULL) {
				json_decref(row_object);
				json_decref(insert_object);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}

			memset(&selector, 0, sizeof(selector));
			selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
			selector.statement_index = statement_index;
			selector.row_index = row_index;
			selector.column_index = column_index;
			status = sqlparser_model_json_object_set_selector(cell_object, &selector, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				json_decref(cell_object);
				json_decref(row_object);
				json_decref(insert_object);
				return status;
			}

			memset(&literal, 0, sizeof(literal));
			value_kind = SQLPARSER_VALUE_KIND_UNKNOWN;
			cell_sql = NULL;
			status = sqlparser_insert_cell_sql(
				handle,
				statement_index,
				row_index,
				column_index,
				&cell_sql,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				json_decref(cell_object);
				json_decref(row_object);
				json_decref(insert_object);
				return status;
			}

			status = sqlparser_json_object_set_string(cell_object, "sql", cell_sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_string_free(cell_sql);
				json_decref(cell_object);
				json_decref(row_object);
				json_decref(insert_object);
				return status;
			}

			status = sqlparser_insert_cell_literal(
				handle,
				statement_index,
				row_index,
				column_index,
				&literal,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				value_kind = SQLPARSER_VALUE_KIND_LITERAL;
				literal_json = sqlparser_model_literal_view_to_json(&literal);
				if (literal_json == NULL ||
				    json_object_set_new(cell_object, "literal", literal_json) != 0) {
					if (literal_json != NULL) {
						json_decref(literal_json);
					}
					sqlparser_string_free(cell_sql);
					json_decref(cell_object);
					json_decref(row_object);
					json_decref(insert_object);
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
			} else if (status == SQLPARSER_STATUS_UNSUPPORTED) {
				sqlparser_error_clear(out_error);
				if (strcmp(cell_sql, "DEFAULT") == 0) {
					value_kind = SQLPARSER_VALUE_KIND_DEFAULT;
				} else {
					value_kind = SQLPARSER_VALUE_KIND_EXPRESSION;
				}
			} else {
				sqlparser_string_free(cell_sql);
				json_decref(cell_object);
				json_decref(row_object);
				json_decref(insert_object);
				return status;
			}

			status = sqlparser_json_object_set_string(
				cell_object,
				"value_kind",
				sqlparser_value_kind_name(value_kind),
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_string_free(cell_sql);
				json_decref(cell_object);
				json_decref(row_object);
				json_decref(insert_object);
				return status;
			}

			sqlparser_string_free(cell_sql);
			cell_sql = NULL;

			if (json_array_append_new(cells, cell_object) != 0) {
				json_decref(cell_object);
				json_decref(row_object);
				json_decref(insert_object);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
		}

		if (json_array_append_new(rows, row_object) != 0) {
			json_decref(row_object);
			json_decref(insert_object);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	if (json_object_set_new(statement_object, "insert", insert_object) != 0) {
		json_decref(insert_object);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_model_build_statement_object(
	json_t *statements,
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	json_t *statement_object;
	sqlparser_statement_kind_t kind;
	const char *node_name;
	sqlparser_status_t status;

	statement_object = json_object();
	if (statement_object == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_statement_kind(handle, statement_index, &kind, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(statement_object);
		return status;
	}

	node_name = NULL;
	status = sqlparser_statement_node_name(handle, statement_index, &node_name, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(statement_object);
		return status;
	}

	(void)json_object_set_new(
		statement_object,
		"statement_index",
		json_integer((json_int_t)statement_index));
	(void)json_object_set_new(
		statement_object,
		"kind",
		json_string(sqlparser_statement_kind_name(kind)));
	(void)json_object_set_new(
		statement_object,
		"node_name",
		json_string(node_name != NULL ? node_name : ""));

	status = sqlparser_model_append_relations(statement_object, handle, statement_index, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_names(statement_object, handle, statement_index, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_literals(statement_object, handle, statement_index, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_where_literals(
			statement_object,
			handle,
			statement_index,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_update_assignments(
			statement_object,
			handle,
			statement_index,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_model_append_insert_model(
			statement_object,
			handle,
			statement_index,
			out_error);
	}

	if (status != SQLPARSER_STATUS_OK) {
		json_decref(statement_object);
		return status;
	}

	if (json_array_append_new(statements, statement_object) != 0) {
		json_decref(statement_object);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_ensure_model_json_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	const char *effective_sql;
	json_t *root;
	json_t *statements;
	char *rendered;
	size_t index;
	sqlparser_status_t status;

	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->model_json != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_ensure_current_sql_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	effective_sql = sqlparser_effective_sql(handle);
	root = json_object();
	statements = json_array();
	if (root == NULL || statements == NULL) {
		if (root != NULL) {
			json_decref(root);
		}
		if (statements != NULL) {
			json_decref(statements);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	(void)json_object_set_new(root, "schema", json_string(sqlparser_model_schema_string()));
	(void)json_object_set_new(
		root,
		"source_sql",
		json_string(handle->sql != NULL ? handle->sql : ""));
	(void)json_object_set_new(
		root,
		"current_sql",
		json_string(effective_sql != NULL ? effective_sql : ""));
	(void)json_object_set_new(
		root,
		"statement_count",
		json_integer((json_int_t)handle->statement_count));
	(void)json_object_set_new(root, "statements", statements);

	for (index = 0U; index < handle->statement_count; index++) {
		status = sqlparser_model_build_statement_object(statements, handle, index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(root);
			return status;
		}
	}

	rendered = json_dumps(root, JSON_COMPACT | JSON_ENSURE_ASCII | JSON_SORT_KEYS);
	json_decref(root);
	if (rendered == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_validate_handle_output_text(handle, rendered, "model JSON", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(rendered);
		return status;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	mutable_handle->model_json = rendered;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_export_model_json(
	const sqlparser_handle_t *handle,
	int pretty,
	char **out_json,
	sqlparser_error_t *out_error)
{
	if (out_json == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_json must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_json = NULL;
	sqlparser_error_clear(out_error);

	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (sqlparser_ensure_model_json_text(handle, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	if (!pretty) {
		*out_json = sqlparser_strdup(handle->model_json);
		if (*out_json == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	{
		json_error_t json_error;
		json_t *root;
		char *rendered;

		root = json_loads(handle->model_json, 0, &json_error);
		if (root == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"failed to parse cached model JSON");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		rendered = json_dumps(root, JSON_INDENT(2) | JSON_ENSURE_ASCII | JSON_SORT_KEYS);
		json_decref(root);
		if (rendered == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (sqlparser_validate_handle_output_text(handle, rendered, "model JSON", out_error) !=
		    SQLPARSER_STATUS_OK) {
			free(rendered);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_RESOURCE_LIMIT;
		}

		*out_json = rendered;
	}

	return SQLPARSER_STATUS_OK;
}
