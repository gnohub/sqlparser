#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sqlparser_internal.h"

static void sqlparser_selector_clear(sqlparser_selector_t *selector)
{
	if (selector == NULL) {
		return;
	}

	memset(selector, 0, sizeof(*selector));
	selector->kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
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
				"stmt[%lu].relation[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_NAME:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].name[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_LITERAL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].literal[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_VALUE:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].value[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].where_literal[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_WHERE:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].where[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_CLAUSE:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].clause[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].assignment[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].insert_cell[%lu][%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->row_index,
				(unsigned long)selector->column_index);
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].insert_columns",
				(unsigned long)selector->statement_index);
			break;
		case SQLPARSER_SELECTOR_KIND_INSERT_ROW:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].insert_row[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->row_index);
			break;
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGETS:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].select_targets[%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index);
			break;
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGET:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].select_target[%lu][%lu]",
				(unsigned long)selector->statement_index,
				(unsigned long)selector->item_index,
				(unsigned long)selector->column_index);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_RELATION) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be relation");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_set_relation_name(
		handle,
		selector->statement_index,
		selector->item_index,
		schema_name,
		table_name,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_NAME) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be name");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_set_name(
		handle,
		selector->statement_index,
		selector->item_index,
		value,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_LITERAL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_set_literal(
		handle,
		selector->statement_index,
		selector->item_index,
		value,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE_LITERAL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where_literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_where_set_literal(
		handle,
		selector->statement_index,
		selector->item_index,
		value,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_set_where_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		sql_text,
		out_error);
}

sqlparser_status_t sqlparser_selector_append_where_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_WHERE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be where");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_append_where_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		bool_operator,
		sql_text,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_CLAUSE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_set_clause_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		sql_text,
		out_error);
}

sqlparser_status_t sqlparser_selector_append_clause_condition(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_CLAUSE) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_statement_append_clause_condition(
		handle,
		selector->statement_index,
		selector->item_index,
		bool_operator,
		sql_text,
		out_error);
}

sqlparser_status_t sqlparser_selector_update_assignment(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_update_assignment(
		handle,
		selector->statement_index,
		selector->item_index,
		out_assignment,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_update_assignment_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_update_set_assignment_literal(
		handle,
		selector->statement_index,
		selector->item_index,
		value,
		out_error);
}

sqlparser_status_t sqlparser_selector_update_assignment_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_update_assignment_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_update_assignment_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_update_set_assignment_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		sql_text,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_insert_set_cell_literal(
		handle,
		selector->statement_index,
		selector->row_index,
		selector->column_index,
		value,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be insert_cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_insert_set_cell_sql(
		handle,
		selector->statement_index,
		selector->row_index,
		selector->column_index,
		sql_text,
		out_error);
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
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be select_target");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_select_set_target_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		selector->column_index,
		sql_text,
		out_error);
}

sqlparser_status_t sqlparser_selector_set_select_targets_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_SELECT_TARGETS) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be select_targets");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_select_set_targets_sql(
		handle,
		selector->statement_index,
		selector->item_index,
		sql_text,
		out_error);
}
