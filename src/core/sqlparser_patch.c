#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

static sqlparser_status_t sqlparser_patch_parse_selector(
	const char *selector_text,
	sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	if (selector_text == NULL || selector_text[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"patch selector must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_selector_parse(selector_text, selector, out_error);
}

static sqlparser_status_t sqlparser_patch_split_relation_name(
	const char *sql_text,
	char **out_database,
	char **out_schema,
	char **out_table,
	sqlparser_error_t *out_error)
{
	char *copy;
	char *parts[3];
	size_t count;
	char *cursor;

	if (out_database == NULL || out_schema == NULL || out_table == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_database = NULL;
	*out_schema = NULL;
	*out_table = NULL;
	if (sql_text == NULL || sql_text[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	copy = sqlparser_strdup(sql_text);
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	count = 0U;
	cursor = copy;
	while (cursor != NULL && count < 3U) {
		char *dot;

		parts[count++] = cursor;
		dot = strchr(cursor, '.');
		if (dot == NULL) {
			break;
		}
		*dot = '\0';
		cursor = dot + 1;
	}
	if (count == 3U && strchr(parts[2], '.') != NULL) {
		free(copy);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "relation SQL has too many name parts");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (count == 1U) {
		*out_table = sqlparser_strdup(parts[0]);
	} else if (count == 2U) {
		*out_schema = sqlparser_strdup(parts[0]);
		*out_table = sqlparser_strdup(parts[1]);
	} else {
		*out_database = sqlparser_strdup(parts[0]);
		*out_schema = sqlparser_strdup(parts[1]);
		*out_table = sqlparser_strdup(parts[2]);
	}
	free(copy);
	if (*out_table == NULL || (count >= 2U && *out_schema == NULL) || (count >= 3U && *out_database == NULL)) {
		free(*out_database);
		free(*out_schema);
		free(*out_table);
		*out_database = NULL;
		*out_schema = NULL;
		*out_table = NULL;
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_set_relation_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	PgQuery__RangeVar *relation;
	char *database_name;
	char *schema_name;
	char *table_name;
	sqlparser_status_t status;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_RELATION) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector kind must be relation");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	database_name = NULL;
	schema_name = NULL;
	table_name = NULL;
	status = sqlparser_patch_split_relation_name(sql_text, &database_name, &schema_name, &table_name, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		selector->statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		selector->item_index,
		NULL,
		&message,
		out_error);
	if (status == SQLPARSER_STATUS_OK && message == NULL) {
		status = SQLPARSER_STATUS_INVALID_ARGUMENT;
		sqlparser_error_set_message(out_error, status, "relation selector is out of range");
	}
	if (status == SQLPARSER_STATUS_OK) {
		relation = (PgQuery__RangeVar *)message;
		status = sqlparser_replace_proto_string(&relation->catalogname, database_name != NULL ? database_name : "", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_replace_proto_string(&relation->schemaname, schema_name != NULL ? schema_name : "", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_replace_proto_string(&relation->relname, table_name, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_handle_commit_ast(handle, out_error);
		}
	}

	free(database_name);
	free(schema_name);
	free(table_name);
	return status;
}

static int sqlparser_patch_node_is_value_expression(const PgQuery__Node *node)
{
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_A_CONST:
		case PG_QUERY__NODE__NODE_COLUMN_REF:
		case PG_QUERY__NODE__NODE_PARAM_REF:
		case PG_QUERY__NODE__NODE_A_EXPR:
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
		case PG_QUERY__NODE__NODE_FUNC_CALL:
		case PG_QUERY__NODE__NODE_TYPE_CAST:
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
		case PG_QUERY__NODE__NODE_NULL_TEST:
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
		case PG_QUERY__NODE__NODE_SUB_LINK:
		case PG_QUERY__NODE__NODE_CASE_EXPR:
		case PG_QUERY__NODE__NODE_CASE_WHEN:
		case PG_QUERY__NODE__NODE_ROW_EXPR:
		case PG_QUERY__NODE__NODE_ROW_COMPARE_EXPR:
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
		case PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION:
		case PG_QUERY__NODE__NODE_SET_TO_DEFAULT:
			return 1;
		default:
			return 0;
	}
}

static int sqlparser_patch_value_slot_is_variable_set_arg(
	const PgQuery__Node *statement,
	PgQuery__Node **value_slot)
{
	PgQuery__VariableSetStmt *set_stmt;
	size_t index;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    value_slot == NULL) {
		return 0;
	}

	set_stmt = statement->variable_set_stmt;
	for (index = 0U; index < set_stmt->n_args; index++) {
		if (&set_stmt->args[index] == value_slot) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_patch_set_value_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	PgQuery__Node **value_slot;
	PgQuery__Node *replacement;
	sqlparser_status_t status;
	char *parser_sql;
	void *dialect_state;
	int variable_set_arg;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_VALUE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector kind must be value");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	parser_sql = NULL;
	dialect_state = NULL;
	replacement = NULL;
	statement = NULL;
	value_slot = NULL;
	variable_set_arg = 0;
	status = sqlparser_get_statement_node(handle, selector->statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_statement_node_slot_by_index(
		handle,
		selector->statement_index,
		selector->item_index,
		&value_slot,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (value_slot == NULL || *value_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "value selector node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	variable_set_arg = sqlparser_patch_value_slot_is_variable_set_arg(statement, value_slot);
	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		sql_text,
		"value SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (variable_set_arg) {
		status = sqlparser_parse_variable_set_arg_node_sql(parser_sql, &replacement, out_error);
	} else {
		status = sqlparser_parse_update_assignment_node_sql(parser_sql, &replacement, out_error);
	}
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	if (!variable_set_arg && !sqlparser_patch_node_is_value_expression(*value_slot)) {
		sqlparser_free_proto_node(replacement);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "value selector does not target an expression node");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	sqlparser_free_proto_node(*value_slot);
	*value_slot = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_literal_value_from_sql(
	const char *sql_text,
	sqlparser_literal_value_t *out_value,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	sqlparser_literal_view_t view;
	sqlparser_status_t status;

	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	memset(&view, 0, sizeof(view));
	node = NULL;
	status = sqlparser_parse_insert_cell_node_sql(sql_text, &node, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (node == NULL || node->node_case != PG_QUERY__NODE__NODE_A_CONST || node->a_const == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "replacement SQL is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_fill_literal_view_from_a_const(node->a_const, &view, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		out_value->kind = view.kind;
		out_value->string_value = view.string_value;
		out_value->float_value = view.float_value;
		out_value->integer_value = view.integer_value;
		out_value->boolean_value = view.boolean_value;
		status = SQLPARSER_STATUS_OK;
		if (out_value->kind == SQLPARSER_LITERAL_KIND_STRING && out_value->string_value != NULL) {
			out_value->string_value = sqlparser_strdup(out_value->string_value);
			status = out_value->string_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		} else if (out_value->kind == SQLPARSER_LITERAL_KIND_FLOAT && out_value->float_value != NULL) {
			out_value->float_value = sqlparser_strdup(out_value->float_value);
			status = out_value->float_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
	}
	sqlparser_free_proto_node(node);
	return status;
}

static void sqlparser_patch_literal_value_clear(sqlparser_literal_value_t *value)
{
	if (value == NULL) {
		return;
	}
	if (value->kind == SQLPARSER_LITERAL_KIND_STRING) {
		free((char *)value->string_value);
	} else if (value->kind == SQLPARSER_LITERAL_KIND_FLOAT) {
		free((char *)value->float_value);
	}
	memset(value, 0, sizeof(*value));
}

static sqlparser_status_t sqlparser_patch_replace(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL || patch->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "replace patch requires selector and sql");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	switch (selector.kind) {
		case SQLPARSER_SELECTOR_KIND_RELATION:
			return sqlparser_patch_set_relation_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_NAME:
			return sqlparser_selector_set_name(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_VALUE:
			return sqlparser_patch_set_value_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
			return sqlparser_selector_set_update_assignment_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			return sqlparser_selector_set_insert_cell_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_LITERAL:
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
		{
			sqlparser_literal_value_t value;
			status = sqlparser_patch_literal_value_from_sql(patch->sql, &value, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (selector.kind == SQLPARSER_SELECTOR_KIND_LITERAL) {
				status = sqlparser_selector_set_literal(handle, &selector, &value, out_error);
			} else {
				status = sqlparser_selector_set_where_literal(handle, &selector, &value, out_error);
			}
			sqlparser_patch_literal_value_clear(&value);
			return status;
		}
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "selector kind cannot be replaced");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

static PgQuery__Node *sqlparser_patch_new_insert_column_node(const char *name, sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__ResTarget *target;

	if (name == NULL || name[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column name must not be NULL or empty");
		return NULL;
	}
	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	target = (PgQuery__ResTarget *)calloc(1U, sizeof(*target));
	if (node == NULL || target == NULL) {
		free(node);
		free(target);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	pg_query__node__init(node);
	pg_query__res_target__init(target);
	node->node_case = PG_QUERY__NODE__NODE_RES_TARGET;
	node->res_target = target;
	target->name = sqlparser_strdup(name);
	if (target->name == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	return node;
}

typedef struct {
	PgQuery__List *row_list;
	PgQuery__Node **next_items;
	size_t next_count;
	PgQuery__Node *cell_node;
} sqlparser_patch_insert_column_row_plan_t;

typedef struct {
	PgQuery__List *row_list;
	PgQuery__Node **next_items;
	size_t next_count;
	PgQuery__Node *removed_node;
} sqlparser_patch_delete_row_plan_t;

static PgQuery__Node **sqlparser_patch_alloc_node_array(size_t count, sqlparser_error_t *out_error)
{
	PgQuery__Node **items;

	if (count == 0U) {
		return NULL;
	}
	if (count > SIZE_MAX / sizeof(*items)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "node array is too large");
		return NULL;
	}
	items = (PgQuery__Node **)calloc(count, sizeof(*items));
	if (items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return items;
}

static void sqlparser_patch_copy_with_insert(
	PgQuery__Node **dest,
	PgQuery__Node **source,
	size_t count,
	size_t index,
	PgQuery__Node *node)
{
	if (index > count) {
		index = count;
	}
	if (index > 0U && source != NULL) {
		memcpy(dest, source, index * sizeof(*dest));
	}
	dest[index] = node;
	if (index < count && source != NULL) {
		memcpy(dest + index + 1U, source + index, (count - index) * sizeof(*dest));
	}
}

static void sqlparser_patch_copy_with_delete(
	PgQuery__Node **dest,
	PgQuery__Node **source,
	size_t count,
	size_t index)
{
	if (index > 0U && source != NULL) {
		memcpy(dest, source, index * sizeof(*dest));
	}
	if (index + 1U < count && source != NULL) {
		memcpy(dest + index, source + index + 1U, (count - index - 1U) * sizeof(*dest));
	}
}

static void sqlparser_patch_insert_column_plan_clear(
	sqlparser_patch_insert_column_row_plan_t *plans,
	size_t count)
{
	size_t index;

	if (plans == NULL) {
		return;
	}
	for (index = 0U; index < count; index++) {
		free(plans[index].next_items);
		sqlparser_free_proto_node(plans[index].cell_node);
	}
	free(plans);
}

static void sqlparser_patch_delete_row_plan_clear(
	sqlparser_patch_delete_row_plan_t *plans,
	size_t count)
{
	size_t index;

	if (plans == NULL) {
		return;
	}
	for (index = 0U; index < count; index++) {
		free(plans[index].next_items);
	}
	free(plans);
}

static sqlparser_status_t sqlparser_patch_insert_column(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *column_node;
	PgQuery__Node *default_node;
	PgQuery__Node **next_cols;
	sqlparser_patch_insert_column_row_plan_t *plans;
	sqlparser_status_t status;
	size_t row_index;
	size_t insert_index;
	size_t row_count;

	if (patch == NULL || patch->selector == NULL || patch->name == NULL || patch->default_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_column requires selector, name and default_sql");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_column selector must be insert_columns");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_insert_values_stmt(handle, selector.statement_index, &stmt, &values_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	column_node = NULL;
	default_node = NULL;
	next_cols = NULL;
	plans = NULL;
	row_count = values_stmt->n_values_lists;
	insert_index = patch->index > stmt->n_cols ? stmt->n_cols : patch->index;
	if (stmt->n_cols == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "insert column count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	column_node = sqlparser_patch_new_insert_column_node(patch->name, out_error);
	if (column_node == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_parse_insert_cell_node_sql(patch->default_sql, &default_node, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		return status;
	}
	next_cols = sqlparser_patch_alloc_node_array(stmt->n_cols + 1U, out_error);
	if (next_cols == NULL) {
		sqlparser_free_proto_node(column_node);
		sqlparser_free_proto_node(default_node);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_patch_copy_with_insert(next_cols, stmt->cols, stmt->n_cols, insert_index, column_node);

	plans = (sqlparser_patch_insert_column_row_plan_t *)calloc(row_count > 0U ? row_count : 1U, sizeof(*plans));
	if (plans == NULL) {
		free(next_cols);
		sqlparser_free_proto_node(column_node);
		sqlparser_free_proto_node(default_node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (row_index = 0U; row_index < row_count; row_index++) {
		PgQuery__List *row_list;
		size_t cell_index;

		if (values_stmt->values_lists[row_index] == NULL ||
		    values_stmt->values_lists[row_index]->node_case != PG_QUERY__NODE__NODE_LIST ||
		    values_stmt->values_lists[row_index]->list == NULL) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "insert row node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		row_list = values_stmt->values_lists[row_index]->list;
		cell_index = patch->index > row_list->n_items ? row_list->n_items : patch->index;
		if (row_list->n_items == SIZE_MAX) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "insert row cell count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		status = sqlparser_clone_proto_node(default_node, &plans[row_index].cell_node, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			return status;
		}
		plans[row_index].next_items = sqlparser_patch_alloc_node_array(row_list->n_items + 1U, out_error);
		if (plans[row_index].next_items == NULL) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_insert(
			plans[row_index].next_items,
			row_list->items,
			row_list->n_items,
			cell_index,
			plans[row_index].cell_node);
		plans[row_index].row_list = row_list;
		plans[row_index].next_count = row_list->n_items + 1U;
	}

	free(stmt->cols);
	stmt->cols = next_cols;
	stmt->n_cols++;
	next_cols = NULL;
	column_node = NULL;
	for (row_index = 0U; row_index < row_count; row_index++) {
		free(plans[row_index].row_list->items);
		plans[row_index].row_list->items = plans[row_index].next_items;
		plans[row_index].row_list->n_items = plans[row_index].next_count;
		plans[row_index].next_items = NULL;
		plans[row_index].cell_node = NULL;
	}

	sqlparser_free_proto_node(default_node);
	sqlparser_patch_insert_column_plan_clear(plans, row_count);
	return sqlparser_handle_commit_ast(handle, out_error);
}

static sqlparser_status_t sqlparser_patch_delete_column(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node **next_cols;
	PgQuery__Node *removed_column;
	sqlparser_patch_delete_row_plan_t *plans;
	sqlparser_status_t status;
	size_t row_index;
	size_t row_count;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column selector must be insert_columns");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_insert_values_stmt(handle, selector.statement_index, &stmt, &values_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	next_cols = NULL;
	removed_column = NULL;
	plans = NULL;
	row_count = values_stmt->n_values_lists;
	if (stmt->n_cols > 0U) {
		if (patch->index >= stmt->n_cols) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		next_cols = sqlparser_patch_alloc_node_array(stmt->n_cols - 1U, out_error);
		if (stmt->n_cols > 1U && next_cols == NULL) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_delete(next_cols, stmt->cols, stmt->n_cols, patch->index);
		removed_column = stmt->cols[patch->index];
	}
	plans = (sqlparser_patch_delete_row_plan_t *)calloc(row_count > 0U ? row_count : 1U, sizeof(*plans));
	if (plans == NULL) {
		free(next_cols);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	for (row_index = 0U; row_index < row_count; row_index++) {
		PgQuery__List *row_list;

		if (values_stmt->values_lists[row_index] == NULL ||
		    values_stmt->values_lists[row_index]->node_case != PG_QUERY__NODE__NODE_LIST ||
		    values_stmt->values_lists[row_index]->list == NULL) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "insert row node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		row_list = values_stmt->values_lists[row_index]->list;
		if (patch->index >= row_list->n_items) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (row_list->n_items <= 1U) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "cannot delete the last insert cell");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		plans[row_index].next_items = sqlparser_patch_alloc_node_array(row_list->n_items - 1U, out_error);
		if (plans[row_index].next_items == NULL) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_delete(plans[row_index].next_items, row_list->items, row_list->n_items, patch->index);
		plans[row_index].row_list = row_list;
		plans[row_index].next_count = row_list->n_items - 1U;
		plans[row_index].removed_node = row_list->items[patch->index];
	}
	if (stmt->n_cols > 0U) {
		free(stmt->cols);
		stmt->cols = next_cols;
		stmt->n_cols--;
		next_cols = NULL;
		sqlparser_free_proto_node(removed_column);
	}
	for (row_index = 0U; row_index < row_count; row_index++) {
		free(plans[row_index].row_list->items);
		plans[row_index].row_list->items = plans[row_index].next_items;
		plans[row_index].row_list->n_items = plans[row_index].next_count;
		plans[row_index].next_items = NULL;
		sqlparser_free_proto_node(plans[row_index].removed_node);
		plans[row_index].removed_node = NULL;
	}
	sqlparser_patch_delete_row_plan_clear(plans, row_count);
	return sqlparser_handle_commit_ast(handle, out_error);
}

static sqlparser_status_t sqlparser_patch_delete_row(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node **next_rows;
	PgQuery__Node *removed_row;
	sqlparser_status_t status;
	size_t row_count;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_row requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_INSERT_ROW) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_row selector must be insert_row");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_insert_values_stmt(handle, selector.statement_index, &stmt, &values_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(void)stmt;
	row_count = values_stmt->n_values_lists;
	if (selector.row_index >= row_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_row index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (row_count <= 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "cannot delete the last insert row");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	next_rows = sqlparser_patch_alloc_node_array(row_count - 1U, out_error);
	if (next_rows == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_patch_copy_with_delete(next_rows, values_stmt->values_lists, row_count, selector.row_index);
	removed_row = values_stmt->values_lists[selector.row_index];
	free(values_stmt->values_lists);
	values_stmt->values_lists = next_rows;
	values_stmt->n_values_lists--;
	sqlparser_free_proto_node(removed_row);
	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_apply_patch(
	sqlparser_handle_t *handle,
	const sqlparser_patch_list_t *patches,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || patches == NULL || (patches->count > 0U && patches->items == NULL)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and patches must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	for (index = 0U; index < patches->count; index++) {
		const sqlparser_patch_t *patch;

		patch = &patches->items[index];
		switch (patch->op) {
			case SQLPARSER_PATCH_REPLACE:
				status = sqlparser_patch_replace(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_INSERT_COLUMN:
				status = sqlparser_patch_insert_column(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_DELETE_COLUMN:
				status = sqlparser_patch_delete_column(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_DELETE_ROW:
				status = sqlparser_patch_delete_row(handle, patch, out_error);
				break;
			default:
				status = SQLPARSER_STATUS_UNSUPPORTED;
				sqlparser_error_set_message(out_error, status, "patch operation is not supported");
				break;
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}
