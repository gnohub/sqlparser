#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_multi_insert_internal.h"

sqlparser_status_t sqlparser_selector_apply_single_patch(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_list_t patches;
	sqlparser_patch_t item;
	char *selector_text;
	sqlparser_status_t status;

	if (patch == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"patch must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	selector_text = NULL;
	status = sqlparser_selector_format(selector, &selector_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	item = *patch;
	item.selector = selector_text;
	patches.items = &item;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, out_error);
	free(selector_text);
	return status;
}

static sqlparser_status_t sqlparser_selector_render_identifier_path_list_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_path_view_t *paths,
	size_t count,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char **items;
	char *sql;
	size_t index;
	size_t offset;
	size_t output_size;
	sqlparser_status_t status;

	*out_sql = NULL;
	if (count > SIZE_MAX / sizeof(*items)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"identifier path list is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	items = (char **)calloc(count, sizeof(*items));
	if (items == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	output_size = 1U;
	status = SQLPARSER_STATUS_OK;
	for (index = 0U; index < count; index++) {
		size_t item_length;
		size_t separator_length;

		status = sqlparser_render_identifier_path_sql(
			handle, &paths[index], &items[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		item_length = strlen(items[index]);
		separator_length = index > 0U ? 2U : 0U;
		if (separator_length > SIZE_MAX - output_size ||
		    item_length >
			    SIZE_MAX - output_size - separator_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"identifier path SQL is too large");
			status = SQLPARSER_STATUS_RESOURCE_LIMIT;
			break;
		}
		output_size += separator_length + item_length;
	}

	sql = NULL;
	if (status == SQLPARSER_STATUS_OK) {
		sql = (char *)malloc(output_size);
		if (sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			status = SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		offset = 0U;
		for (index = 0U; index < count; index++) {
			size_t item_length;

			if (index > 0U) {
				memcpy(sql + offset, ", ", 2U);
				offset += 2U;
			}
			item_length = strlen(items[index]);
			memcpy(sql + offset, items[index], item_length);
			offset += item_length;
		}
		sql[offset] = '\0';
		*out_sql = sql;
	}
	for (index = 0U; index < count; index++) {
		free(items[index]);
	}
	free(items);
	return status;
}

static void sqlparser_selector_clear(sqlparser_selector_t *selector)
{
	if (selector == NULL) {
		return;
	}

	memset(selector, 0, sizeof(*selector));
	selector->kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
}

static sqlparser_status_t sqlparser_selector_validate_identifier_path(
	const sqlparser_identifier_path_view_t *path,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (path == NULL || path->parts == NULL || path->part_count == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier path must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	for (index = 0U; index < path->part_count; index++) {
		if (path->parts[index] == NULL || path->parts[index][0] == '\0') {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"identifier path part must not be NULL or empty");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_selector_validate_identifier_path_array(
	const sqlparser_identifier_path_view_t *paths,
	size_t count,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	if (paths == NULL || count == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "columns must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	for (index = 0U; index < count; index++) {
		status = sqlparser_selector_validate_identifier_path(&paths[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_selector_parse_index(
	const char *text,
	size_t *offset,
	size_t *out_value,
	sqlparser_error_t *out_error)
{
	unsigned long long value;
	size_t index;

	if (text == NULL || offset == NULL || out_value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector parser received invalid arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	index = *offset;
	if (text[index] != '[') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector is missing '['");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	index++;
	if (!isdigit((unsigned char)text[index])) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector index must be numeric");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	value = 0ULL;
	while (isdigit((unsigned char)text[index])) {
		unsigned digit;

		digit = (unsigned)(text[index] - '0');
		if (value > ((((unsigned long long)SIZE_MAX) - digit) / 10ULL)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"selector index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		value = value * 10ULL + (unsigned long long)digit;
		index++;
	}

	if (text[index] != ']') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector is missing ']'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value = (size_t)value;
	*offset = index + 1U;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_selector_parse(
	const char *text,
	sqlparser_selector_t *out_selector,
	sqlparser_error_t *out_error)
{
	size_t offset;
	sqlparser_status_t status;

	if (out_selector == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_selector must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_selector_clear(out_selector);
	sqlparser_error_clear(out_error);
	if (text == NULL || text[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector text must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (strncmp(text, "stmt", 4) != 0) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector must start with 'stmt'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	offset = 4U;
	status = sqlparser_selector_parse_index(text, &offset, &out_selector->statement_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (text[offset] != '.' || text[offset + 1U] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector is missing item kind");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	offset++;

	if (strncmp(text + offset, "relation", 8) == 0) {
		offset += 8U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_RELATION;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "name", 4) == 0) {
		offset += 4U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_NAME;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "literal", 7) == 0) {
		offset += 7U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_LITERAL;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "value", 5) == 0) {
		offset += 5U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_VALUE;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "where_literal", 13) == 0) {
		offset += 13U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_WHERE_LITERAL;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "where", 5) == 0) {
		offset += 5U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_WHERE;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "clause", 6) == 0) {
		offset += 6U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_CLAUSE;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "assignment", 10) == 0) {
		offset += 10U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "merge_assignment", 16) == 0) {
		size_t first_index;

		offset += 16U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->column_index,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && text[offset] == '[') {
			first_index = out_selector->item_index;
			out_selector->row_index = first_index;
			out_selector->item_index = out_selector->column_index;
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->column_index,
				out_error);
		}
	} else if (strncmp(text + offset, "merge_insert_column", 19) == 0 ||
		   strncmp(text + offset, "merge_insert_cell", 17) == 0) {
		size_t first_index;
		int is_column;

		is_column = strncmp(text + offset, "merge_insert_column", 19) == 0;
		offset += is_column ? 19U : 17U;
		out_selector->kind = is_column ?
			SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN :
			SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->column_index,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && text[offset] == '[') {
			first_index = out_selector->item_index;
			out_selector->row_index = first_index;
			out_selector->item_index = out_selector->column_index;
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->column_index,
				out_error);
		}
	} else if (strncmp(text + offset, "merge_branch_condition", 22) == 0) {
		size_t first_index;

		offset += 22U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK && text[offset] == '[') {
			first_index = out_selector->item_index;
			out_selector->row_index = first_index;
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->item_index,
				out_error);
		}
	} else if (strncmp(text + offset, "merge_delete_condition", 22) == 0) {
		size_t first_index;

		offset += 22U;
		out_selector->kind =
			SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION;
		status = sqlparser_selector_parse_index(
			text,
			&offset,
			&out_selector->item_index,
			out_error);
		if (status == SQLPARSER_STATUS_OK && text[offset] == '[') {
			first_index = out_selector->item_index;
			out_selector->row_index = first_index;
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->item_index,
				out_error);
		}
	} else if (strncmp(text + offset, "insert_cell", 11) == 0) {
		offset += 11U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->column_index,
				out_error);
		}
	} else if (strncmp(text + offset, "insert_columns", 14) == 0) {
		offset += 14U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS;
		status = SQLPARSER_STATUS_OK;
	} else if (strncmp(text + offset, "insert_branch_columns", 21) == 0) {
		size_t first_index;

		offset += 21U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK && text[offset] == '[') {
			first_index = out_selector->item_index;
			out_selector->row_index = first_index;
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->item_index,
				out_error);
		}
	} else if (strncmp(text + offset, "insert_branch_condition", 23) == 0) {
		offset += 23U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "insert_row", 10) == 0) {
		offset += 10U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_INSERT_ROW;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
	} else if (strncmp(text + offset, "select_targets", 14) == 0) {
		offset += 14U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGETS;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
	} else if (strncmp(text + offset, "select_target", 13) == 0) {
		offset += 13U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(
				text,
				&offset,
				&out_selector->column_index,
				out_error);
		}
	} else if (strncmp(text + offset, "dml_result_targets", 18) == 0) {
		offset += 18U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
		}
	} else if (strncmp(text + offset, "dml_result_target", 17) == 0) {
		offset += 17U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->column_index, out_error);
		}
	} else if (strncmp(text + offset, "dml_result_sink_columns", 23) == 0) {
		offset += 23U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
		}
	} else if (strncmp(text + offset, "dml_result_sink_column", 22) == 0) {
		offset += 22U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->column_index, out_error);
		}
	} else if (strncmp(text + offset, "dml_result_sink", 15) == 0) {
		offset += 15U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK;
		status = sqlparser_selector_parse_index(text, &offset, &out_selector->item_index, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_selector_parse_index(text, &offset, &out_selector->row_index, out_error);
		}
	} else {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind is not supported");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (text[offset] != '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector has trailing characters");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_selector_format(
	const sqlparser_selector_t *selector,
	char **out_text,
	sqlparser_error_t *out_error)
{
	char buffer[128];
	int length;

	if (out_text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_text must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_text = NULL;
	sqlparser_error_clear(out_error);
	if (selector == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	switch (selector->kind) {
		case SQLPARSER_SELECTOR_KIND_RELATION:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].relation[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_NAME:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].name[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_LITERAL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].literal[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_VALUE:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].value[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].where_literal[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_WHERE:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].where[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_CLAUSE:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].clause[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].assignment[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT:
			if (selector->row_index == 0U) {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].merge_assignment[%zu][%zu]",
					selector->statement_index,
					selector->item_index,
					selector->column_index);
			} else {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].merge_assignment[%zu][%zu][%zu]",
					selector->statement_index,
					selector->row_index,
					selector->item_index,
					selector->column_index);
			}
			break;
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN:
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL:
			if (selector->row_index == 0U) {
				length = snprintf(
					buffer,
					sizeof(buffer),
					selector->kind == SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN ?
						"stmt[%zu].merge_insert_column[%zu][%zu]" :
						"stmt[%zu].merge_insert_cell[%zu][%zu]",
					selector->statement_index,
					selector->item_index,
					selector->column_index);
			} else {
				length = snprintf(
					buffer,
					sizeof(buffer),
					selector->kind == SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN ?
						"stmt[%zu].merge_insert_column[%zu][%zu][%zu]" :
						"stmt[%zu].merge_insert_cell[%zu][%zu][%zu]",
					selector->statement_index,
					selector->row_index,
					selector->item_index,
					selector->column_index);
			}
			break;
		case SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION:
			if (selector->row_index == 0U) {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].merge_branch_condition[%zu]",
					selector->statement_index,
					selector->item_index);
			} else {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].merge_branch_condition[%zu][%zu]",
					selector->statement_index,
					selector->row_index,
					selector->item_index);
			}
			break;
		case SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION:
			if (selector->row_index == 0U) {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].merge_delete_condition[%zu]",
					selector->statement_index,
					selector->item_index);
			} else {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].merge_delete_condition[%zu][%zu]",
					selector->statement_index,
					selector->row_index,
					selector->item_index);
			}
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].insert_cell[%zu][%zu]",
				selector->statement_index,
				selector->row_index,
				selector->column_index);
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].insert_columns",
				selector->statement_index);
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS:
			if (selector->row_index == 0U) {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].insert_branch_columns[%zu]",
					selector->statement_index,
					selector->item_index);
			} else {
				length = snprintf(
					buffer,
					sizeof(buffer),
					"stmt[%zu].insert_branch_columns[%zu][%zu]",
					selector->statement_index,
					selector->row_index,
					selector->item_index);
			}
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].insert_branch_condition[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_ROW:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].insert_row[%zu]",
				selector->statement_index,
				selector->row_index);
			break;
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGETS:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].select_targets[%zu]",
				selector->statement_index,
				selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGET:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].select_target[%zu][%zu]",
				selector->statement_index,
				selector->item_index,
				selector->column_index);
			break;
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].dml_result_targets[%zu][%zu]",
				selector->statement_index,
				selector->item_index,
				selector->row_index);
			break;
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].dml_result_target[%zu][%zu][%zu]",
				selector->statement_index,
				selector->item_index,
				selector->row_index,
				selector->column_index);
			break;
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].dml_result_sink[%zu][%zu]",
				selector->statement_index,
				selector->item_index,
				selector->row_index);
			break;
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].dml_result_sink_columns[%zu][%zu]",
				selector->statement_index,
				selector->item_index,
				selector->row_index);
			break;
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%zu].dml_result_sink_column[%zu][%zu][%zu]",
				selector->statement_index,
				selector->item_index,
				selector->row_index,
				selector->column_index);
			break;
		case SQLPARSER_SELECTOR_KIND_UNKNOWN:
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"selector kind is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (length < 0 || (size_t)length >= sizeof(buffer)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to format selector");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_text = sqlparser_strdup(buffer);
	if (*out_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_selector_relation(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_RELATION) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be relation");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_relation(
		handle,
		selector->statement_index,
		selector->item_index,
		out_relation,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_relation_name(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	size_t source_encoding;
	sqlparser_status_t status;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_RELATION) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be relation");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_statement_relation_patch_source_encoding(
		handle,
		selector->statement_index,
		selector->item_index,
		schema_name,
		table_name,
		&source_encoding,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.name = table_name;
	patch.default_sql = schema_name;
	patch.index = source_encoding;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_name(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_name_view_t *out_name,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_NAME) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be name");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_name(
		handle,
		selector->statement_index,
		selector->item_index,
		out_name,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_name(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *value,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_NAME) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be name");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.default_sql = value;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_literal(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_LITERAL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_literal(
		handle,
		selector->statement_index,
		selector->item_index,
		out_literal,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_LITERAL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.literal = value;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_where_literal(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_where_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE_LITERAL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where_literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_where_literal(
		handle,
		selector->statement_index,
		selector->item_index,
		out_literal,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_where_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE_LITERAL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where_literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.literal = value;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_where_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_where_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_where_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_append_where_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (bool_operator != SQLPARSER_BOOL_OPERATOR_AND &&
	    bool_operator != SQLPARSER_BOOL_OPERATOR_OR) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bool_operator must be AND or OR");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch.sql = sql_text;
	patch.bool_operator = bool_operator;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_clause(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_clause_view_t *out_clause,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_CLAUSE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_clause(
		handle,
		selector->statement_index,
		selector->item_index,
		out_clause,
		out_error);
}

sqlparser_status_t sqlparser_selector_clause_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (selector != NULL && selector->kind == SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION) {
		return sqlparser_dialect_multi_insert_condition_sql(
			handle,
			selector->statement_index,
			selector->item_index,
			out_sql,
			out_error);
	}
	if (selector != NULL && selector->kind == SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION) {
		return sqlparser_merge_branch_condition_sql(
			handle,
			selector->statement_index,
			selector->row_index,
			selector->item_index,
			out_sql,
			out_error);
	}
	if (selector != NULL &&
	    selector->kind ==
		    SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION) {
		return sqlparser_merge_delete_condition_sql(
			handle,
			selector->statement_index,
			selector->row_index,
			selector->item_index,
			out_sql,
			out_error);
	}
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_CLAUSE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_clause_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_clause_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_CLAUSE &&
	     selector->kind !=
		     SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
	     selector->kind !=
		     SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_append_clause_condition(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_CLAUSE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (bool_operator != SQLPARSER_BOOL_OPERATOR_AND &&
	    bool_operator != SQLPARSER_BOOL_OPERATOR_OR) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bool_operator must be AND or OR");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch.sql = sql_text;
	patch.bool_operator = bool_operator;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_update_assignment(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error)
{
	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_assignment_by_selector(handle, selector, out_assignment, out_error);
}

sqlparser_status_t sqlparser_selector_set_update_assignment_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.literal = value;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_update_assignment_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_assignment_sql_by_selector(
		handle, selector, out_sql, out_error);
}

sqlparser_status_t sqlparser_selector_set_update_assignment_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_insert_update_assignment_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patch.sql = assignment_sql;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_insert_update_assignment_from_assignment_value(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *insert_selector,
	const sqlparser_identifier_path_view_t *target,
	const sqlparser_selector_t *source_assignment_selector,
	sqlparser_error_t *out_error)
{
	char *source_selector_text;
	char *target_sql;
	sqlparser_patch_t patch;
	sqlparser_status_t status;

	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (insert_selector == NULL ||
	    (insert_selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     insert_selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT) ||
	    source_assignment_selector == NULL ||
	    (source_assignment_selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     source_assignment_selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selectors must be assignment selectors");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (insert_selector->statement_index != source_assignment_selector->statement_index) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"assignment value clone across statements is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_selector_validate_identifier_path(target, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	target_sql = NULL;
	status = sqlparser_render_identifier_path_sql(
		handle, target, &target_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	source_selector_text = NULL;
	status = sqlparser_selector_format(
		source_assignment_selector,
		&source_selector_text,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(target_sql);
		return status;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patch.name = target_sql;
	patch.source_selector = source_selector_text;
	status = sqlparser_selector_apply_single_patch(
		handle, insert_selector, &patch, out_error);
	free(source_selector_text);
	free(target_sql);
	return status;
}

sqlparser_status_t sqlparser_selector_delete_update_assignment(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_set_update_assignment_full_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patch.sql = assignment_sql;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_insert_cell_literal(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_insert_cell_literal(
		handle,
		selector->statement_index,
		selector->row_index,
		selector->column_index,
		out_literal,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_insert_cell_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	char *rendered_sql;
	sqlparser_literal_view_t current_literal;
	sqlparser_patch_t patch;
	sqlparser_status_t status;
	int multi_insert;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	multi_insert =
		handle != NULL &&
		sqlparser_dialect_state_has_multi_insert(
			handle->dialect,
			handle->dialect_state);
	rendered_sql = NULL;
	if (multi_insert) {
		status = sqlparser_dialect_multi_insert_render_literal_value(
			handle, value, &rendered_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	} else {
		memset(&current_literal, 0, sizeof(current_literal));
		status = sqlparser_selector_insert_cell_literal(
			handle, selector, &current_literal, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = rendered_sql;
	patch.literal = multi_insert ? NULL : value;
	status = sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
	free(rendered_sql);
	return status;
}

sqlparser_status_t sqlparser_selector_insert_cell_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_insert_cell_sql(
		handle,
		selector->statement_index,
		selector->row_index,
		selector->column_index,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_insert_cell_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_set_insert_cell_bind(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_bind_value_t *bind,
	sqlparser_error_t *out_error)
{
	char *rendered_sql;
	sqlparser_patch_t patch;
	sqlparser_status_t status;
	int multi_insert;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	multi_insert =
		handle != NULL &&
		sqlparser_dialect_state_has_multi_insert(
			handle->dialect,
			handle->dialect_state);
	rendered_sql = NULL;
	if (multi_insert) {
		status = sqlparser_dialect_multi_insert_render_bind_value(
			handle, bind, &rendered_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = rendered_sql;
	patch.bind = multi_insert ? NULL : bind;
	status = sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
	free(rendered_sql);
	return status;
}

sqlparser_status_t sqlparser_selector_select_target_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be select_target");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_select_target_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		selector->column_index,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_select_target_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be select_target");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_set_select_targets_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_SELECT_TARGETS) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be select_targets");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle, selector, &patch, out_error);
}

sqlparser_status_t sqlparser_selector_replace_select_target_with_columns(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *target_selector,
	const sqlparser_identifier_path_view_t *columns,
	size_t column_count,
	sqlparser_error_t *out_error)
{
	char *target_sql;
	sqlparser_patch_t patch;
	sqlparser_status_t status;

	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (target_selector == NULL || target_selector->kind != SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be select_target");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_selector_validate_identifier_path_array(columns, column_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	target_sql = NULL;
	status = sqlparser_selector_render_identifier_path_list_sql(
		handle,
		columns,
		column_count,
		&target_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = target_sql;
	status = sqlparser_selector_apply_single_patch(
		handle, target_selector, &patch, out_error);
	free(target_sql);
	return status;
}
