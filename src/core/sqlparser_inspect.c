#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_internal.h"

static const char *sqlparser_summary_context_name(PgQuery__SummaryResult__Context context)
{
	switch (context) {
		case PG_QUERY__SUMMARY_RESULT__CONTEXT__Select:
			return "select";
		case PG_QUERY__SUMMARY_RESULT__CONTEXT__DML:
			return "dml";
		case PG_QUERY__SUMMARY_RESULT__CONTEXT__DDL:
			return "ddl";
		case PG_QUERY__SUMMARY_RESULT__CONTEXT__Call:
			return "call";
		case PG_QUERY__SUMMARY_RESULT__CONTEXT__None:
		default:
			return "none";
	}
}

static sqlparser_status_t sqlparser_ensure_summary(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	PgQuerySummaryParseResult summary_result;
	sqlparser_handle_t *mutable_handle;
	const char *effective_sql;
	sqlparser_status_t status;

	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->summary.data != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_ensure_current_sql_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	effective_sql = sqlparser_effective_parser_sql(handle);
	mutable_handle = (sqlparser_handle_t *)handle;
	sqlparser_pg_query_prepare();
	summary_result = pg_query_summary(effective_sql, 0, -1);
	if (summary_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			effective_sql,
			summary_result.error);
		pg_query_free_summary_parse_result(summary_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	mutable_handle->summary.data =
		sqlparser_strndup(summary_result.summary.data, summary_result.summary.len);
	mutable_handle->summary.len = summary_result.summary.len;
	if (mutable_handle->summary.data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_summary_parse_result(summary_result);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query_free_summary_parse_result(summary_result);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_ensure_scan(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	PgQueryScanResult scan_result;
	sqlparser_handle_t *mutable_handle;
	const char *effective_sql;
	sqlparser_status_t status;

	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->scan.data != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_ensure_current_sql_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	effective_sql = sqlparser_effective_parser_sql(handle);
	mutable_handle = (sqlparser_handle_t *)handle;
	sqlparser_pg_query_prepare();
	scan_result = pg_query_scan(effective_sql);
	if (scan_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			effective_sql,
			scan_result.error);
		pg_query_free_scan_result(scan_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	mutable_handle->scan.data = sqlparser_strndup(scan_result.pbuf.data, scan_result.pbuf.len);
	mutable_handle->scan.len = scan_result.pbuf.len;
	if (mutable_handle->scan.data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_scan_result(scan_result);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query_free_scan_result(scan_result);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_ensure_parse_tree_json_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	PgQueryParseResult parse_result;
	sqlparser_handle_t *mutable_handle;
	const char *effective_sql;
	sqlparser_status_t status;

	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->parse_tree_json != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_ensure_current_sql_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	effective_sql = sqlparser_effective_parser_sql(handle);
	mutable_handle = (sqlparser_handle_t *)handle;
	sqlparser_pg_query_prepare();
	parse_result = pg_query_parse(effective_sql);
	if (parse_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			effective_sql,
			parse_result.error);
		pg_query_free_parse_result(parse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	status = sqlparser_validate_handle_output_text(handle, parse_result.parse_tree, "parse tree JSON", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_parse_result(parse_result);
		return status;
	}

	mutable_handle->parse_tree_json = sqlparser_strdup(parse_result.parse_tree);
	pg_query_free_parse_result(parse_result);
	if (mutable_handle->parse_tree_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

typedef struct {
	json_t *selected_columns;
	json_t *join_columns;
	json_t *where_columns;
	json_t *insert_columns;
	json_t *update_columns;
	json_t *all_referenced_columns;
} sqlparser_column_views_t;

static void sqlparser_extract_statement_columns(json_t *stmt_node, sqlparser_column_views_t *views);
static const char *sqlparser_json_string_node_value(json_t *node);

static int sqlparser_extract_name_list_table_parts(
	json_t *items,
	const char **schema_name_out,
	const char **table_name_out)
{
	size_t count;
	json_t *item;
	const char *table_name;
	const char *schema_name;

	if (schema_name_out == NULL || table_name_out == NULL || !json_is_array(items)) {
		return 0;
	}

	*schema_name_out = NULL;
	*table_name_out = NULL;

	count = json_array_size(items);
	if (count == 0U) {
		return 0;
	}

	item = json_array_get(items, count - 1U);
	table_name = sqlparser_json_string_node_value(item);
	if (table_name == NULL || table_name[0] == '\0') {
		return 0;
	}

	schema_name = NULL;
	if (count >= 2U) {
		item = json_array_get(items, count - 2U);
		schema_name = sqlparser_json_string_node_value(item);
	}

	*schema_name_out = schema_name;
	*table_name_out = table_name;
	return 1;
}

static int sqlparser_drop_stmt_targets_relation(const char *remove_type)
{
	if (remove_type == NULL) {
		return 0;
	}

	return strcmp(remove_type, "OBJECT_TABLE") == 0 ||
		strcmp(remove_type, "OBJECT_VIEW") == 0 ||
		strcmp(remove_type, "OBJECT_MATVIEW") == 0 ||
		strcmp(remove_type, "OBJECT_FOREIGN_TABLE") == 0;
}

static const char *sqlparser_json_string_node_value(json_t *node)
{
	json_t *string_object;
	json_t *sval;

	if (!json_is_object(node)) {
		return NULL;
	}

	string_object = json_object_get(node, "String");
	if (!json_is_object(string_object)) {
		return NULL;
	}

	sval = json_object_get(string_object, "sval");
	if (!json_is_string(sval)) {
		return NULL;
	}

	return json_string_value(sval);
}

static int sqlparser_json_is_star_node(json_t *node)
{
	if (!json_is_object(node)) {
		return 0;
	}

	return json_object_get(node, "A_Star") != NULL;
}

static int sqlparser_json_array_contains_column(
	json_t *array,
	const char *schema_name,
	const char *table_name,
	const char *column_name)
{
	size_t index;

	if (array == NULL || column_name == NULL || column_name[0] == '\0') {
		return 0;
	}

	for (index = 0; index < json_array_size(array); index++) {
		json_t *entry;
		json_t *schema_json;
		json_t *table_json;
		json_t *column_json;
		const char *entry_schema;
		const char *entry_table;
		const char *entry_column;

		entry = json_array_get(array, index);
		if (!json_is_object(entry)) {
			continue;
		}

		schema_json = json_object_get(entry, "schema_name");
		table_json = json_object_get(entry, "table_name");
		column_json = json_object_get(entry, "column");

		entry_schema = json_is_string(schema_json) ? json_string_value(schema_json) : NULL;
		entry_table = json_is_string(table_json) ? json_string_value(table_json) : NULL;
		entry_column = json_is_string(column_json) ? json_string_value(column_json) : NULL;

		if (entry_column == NULL || strcmp(entry_column, column_name) != 0) {
			continue;
		}

		if (((entry_schema == NULL && schema_name == NULL) ||
			 (entry_schema != NULL && schema_name != NULL && strcmp(entry_schema, schema_name) == 0)) &&
			((entry_table == NULL && table_name == NULL) ||
			 (entry_table != NULL && table_name != NULL && strcmp(entry_table, table_name) == 0))) {
			return 1;
		}
	}

	return 0;
}

static void sqlparser_json_array_append_column(
	json_t *array,
	const char *schema_name,
	const char *table_name,
	const char *column_name)
{
	json_t *entry;

	if (array == NULL || column_name == NULL || column_name[0] == '\0') {
		return;
	}

	if (sqlparser_json_array_contains_column(array, schema_name, table_name, column_name)) {
		return;
	}

	entry = json_object();
	if (entry == NULL) {
		return;
	}

	sqlparser_json_object_set_nonempty_string(entry, "schema_name", schema_name);
	sqlparser_json_object_set_nonempty_string(entry, "table_name", table_name);
	sqlparser_json_object_set_nonempty_string(entry, "column", column_name);
	(void)json_array_append_new(array, entry);
}

static void sqlparser_collect_column(
	json_t *specific_columns,
	json_t *all_columns,
	const char *schema_name,
	const char *table_name,
	const char *column_name)
{
	sqlparser_json_array_append_column(all_columns, schema_name, table_name, column_name);
	sqlparser_json_array_append_column(specific_columns, schema_name, table_name, column_name);
}

static void sqlparser_collect_named_column(
	json_t *specific_columns,
	json_t *all_columns,
	const char *column_name)
{
	sqlparser_collect_column(specific_columns, all_columns, NULL, NULL, column_name);
}

static void sqlparser_collect_column_ref_fields(
	json_t *fields,
	json_t *specific_columns,
	json_t *all_columns)
{
	size_t count;
	const char *schema_name;
	const char *table_name;
	const char *column_name;
	json_t *field;

	if (!json_is_array(fields)) {
		return;
	}

	count = json_array_size(fields);
	if (count == 0U) {
		return;
	}

	schema_name = NULL;
	table_name = NULL;
	column_name = NULL;

	field = json_array_get(fields, count - 1U);
	column_name = sqlparser_json_string_node_value(field);
	if (column_name == NULL && sqlparser_json_is_star_node(field)) {
		column_name = "*";
	}
	if (column_name == NULL) {
		return;
	}

	if (count >= 2U) {
		field = json_array_get(fields, count - 2U);
		table_name = sqlparser_json_string_node_value(field);
	}

	if (count >= 3U) {
		field = json_array_get(fields, count - 3U);
		schema_name = sqlparser_json_string_node_value(field);
	}

	sqlparser_collect_column(specific_columns, all_columns, schema_name, table_name, column_name);
}

static void sqlparser_walk_column_refs(
	json_t *node,
	json_t *specific_columns,
	json_t *all_columns)
{
	if (node == NULL) {
		return;
	}

	if (json_is_array(node)) {
		size_t index;

		for (index = 0; index < json_array_size(node); index++) {
			sqlparser_walk_column_refs(json_array_get(node, index), specific_columns, all_columns);
		}
		return;
	}

	if (json_is_object(node)) {
		json_t *column_ref;
		const char *key;
		json_t *value;

		column_ref = json_object_get(node, "ColumnRef");
		if (json_is_object(column_ref)) {
			sqlparser_collect_column_ref_fields(
				json_object_get(column_ref, "fields"),
				specific_columns,
				all_columns);
			return;
		}

		json_object_foreach(node, key, value)
		{
			(void)key;
			sqlparser_walk_column_refs(value, specific_columns, all_columns);
		}
	}
}

static void sqlparser_extract_from_item(json_t *node, sqlparser_column_views_t *views)
{
	json_t *join_expr;
	json_t *range_subselect;
	json_t *range_table_sample;

	if (!json_is_object(node)) {
		return;
	}

	join_expr = json_object_get(node, "JoinExpr");
	if (json_is_object(join_expr)) {
		sqlparser_extract_from_item(json_object_get(join_expr, "larg"), views);
		sqlparser_extract_from_item(json_object_get(join_expr, "rarg"), views);
		sqlparser_walk_column_refs(
			json_object_get(join_expr, "quals"),
			views->join_columns,
			views->all_referenced_columns);
		return;
	}

	range_subselect = json_object_get(node, "RangeSubselect");
	if (json_is_object(range_subselect)) {
		sqlparser_extract_statement_columns(json_object_get(range_subselect, "subquery"), views);
		return;
	}

	range_table_sample = json_object_get(node, "RangeTableSample");
	if (json_is_object(range_table_sample)) {
		sqlparser_extract_from_item(json_object_get(range_table_sample, "relation"), views);
	}
}

static void sqlparser_extract_select_stmt(json_t *select_stmt, sqlparser_column_views_t *views)
{
	json_t *target_list;
	json_t *from_clause;
	size_t index;

	target_list = json_object_get(select_stmt, "targetList");
	if (json_is_array(target_list)) {
		for (index = 0; index < json_array_size(target_list); index++) {
			json_t *entry;
			json_t *res_target;

			entry = json_array_get(target_list, index);
			res_target = json_object_get(entry, "ResTarget");
			if (json_is_object(res_target)) {
				sqlparser_walk_column_refs(
					json_object_get(res_target, "val"),
					views->selected_columns,
					views->all_referenced_columns);
			}
		}
	}

	from_clause = json_object_get(select_stmt, "fromClause");
	if (json_is_array(from_clause)) {
		for (index = 0; index < json_array_size(from_clause); index++) {
			sqlparser_extract_from_item(json_array_get(from_clause, index), views);
		}
	}

	sqlparser_walk_column_refs(
		json_object_get(select_stmt, "whereClause"),
		views->where_columns,
		views->all_referenced_columns);

	sqlparser_extract_statement_columns(json_object_get(select_stmt, "larg"), views);
	sqlparser_extract_statement_columns(json_object_get(select_stmt, "rarg"), views);
}

static void sqlparser_extract_insert_stmt(json_t *insert_stmt, sqlparser_column_views_t *views)
{
	json_t *cols;
	size_t index;

	cols = json_object_get(insert_stmt, "cols");
	if (json_is_array(cols)) {
		for (index = 0; index < json_array_size(cols); index++) {
			json_t *entry;
			json_t *res_target;
			json_t *name_json;

			entry = json_array_get(cols, index);
			res_target = json_object_get(entry, "ResTarget");
			if (!json_is_object(res_target)) {
				continue;
			}

			name_json = json_object_get(res_target, "name");
			if (json_is_string(name_json)) {
				sqlparser_collect_named_column(
					views->insert_columns,
					views->all_referenced_columns,
					json_string_value(name_json));
			}
		}
	}

	sqlparser_extract_statement_columns(json_object_get(insert_stmt, "selectStmt"), views);
}

static void sqlparser_extract_update_stmt(json_t *update_stmt, sqlparser_column_views_t *views)
{
	json_t *target_list;
	json_t *from_clause;
	size_t index;

	target_list = json_object_get(update_stmt, "targetList");
	if (json_is_array(target_list)) {
		for (index = 0; index < json_array_size(target_list); index++) {
			json_t *entry;
			json_t *res_target;
			json_t *name_json;

			entry = json_array_get(target_list, index);
			res_target = json_object_get(entry, "ResTarget");
			if (!json_is_object(res_target)) {
				continue;
			}

			name_json = json_object_get(res_target, "name");
			if (json_is_string(name_json)) {
				sqlparser_collect_named_column(
					views->update_columns,
					views->all_referenced_columns,
					json_string_value(name_json));
			}

			sqlparser_walk_column_refs(
				json_object_get(res_target, "val"),
				NULL,
				views->all_referenced_columns);
		}
	}

	from_clause = json_object_get(update_stmt, "fromClause");
	if (json_is_array(from_clause)) {
		for (index = 0; index < json_array_size(from_clause); index++) {
			sqlparser_extract_from_item(json_array_get(from_clause, index), views);
		}
	}

	sqlparser_walk_column_refs(
		json_object_get(update_stmt, "whereClause"),
		views->where_columns,
		views->all_referenced_columns);
}

static void sqlparser_extract_delete_stmt(json_t *delete_stmt, sqlparser_column_views_t *views)
{
	json_t *using_clause;
	size_t index;

	using_clause = json_object_get(delete_stmt, "usingClause");
	if (json_is_array(using_clause)) {
		for (index = 0; index < json_array_size(using_clause); index++) {
			sqlparser_extract_from_item(json_array_get(using_clause, index), views);
		}
	}

	sqlparser_walk_column_refs(
		json_object_get(delete_stmt, "whereClause"),
		views->where_columns,
		views->all_referenced_columns);
}

static void sqlparser_extract_merge_when_clause(json_t *merge_when_clause, sqlparser_column_views_t *views)
{
	json_t *command_type_json;
	json_t *target_list;
	json_t *values;
	const char *command_type;
	size_t index;

	if (!json_is_object(merge_when_clause)) {
		return;
	}

	command_type_json = json_object_get(merge_when_clause, "commandType");
	command_type = json_is_string(command_type_json) ? json_string_value(command_type_json) : NULL;

	target_list = json_object_get(merge_when_clause, "targetList");
	if (json_is_array(target_list)) {
		for (index = 0; index < json_array_size(target_list); index++) {
			json_t *entry;
			json_t *res_target;
			json_t *name_json;

			entry = json_array_get(target_list, index);
			res_target = json_object_get(entry, "ResTarget");
			if (!json_is_object(res_target)) {
				continue;
			}

			name_json = json_object_get(res_target, "name");
			if (json_is_string(name_json)) {
				if (command_type != NULL && strcmp(command_type, "CMD_UPDATE") == 0) {
					sqlparser_collect_named_column(
						views->update_columns,
						views->all_referenced_columns,
						json_string_value(name_json));
				} else if (command_type != NULL && strcmp(command_type, "CMD_INSERT") == 0) {
					sqlparser_collect_named_column(
						views->insert_columns,
						views->all_referenced_columns,
						json_string_value(name_json));
				}
			}

			sqlparser_walk_column_refs(
				json_object_get(res_target, "val"),
				NULL,
				views->all_referenced_columns);
		}
	}

	values = json_object_get(merge_when_clause, "values");
	if (json_is_array(values)) {
		sqlparser_walk_column_refs(values, NULL, views->all_referenced_columns);
	}

	sqlparser_walk_column_refs(
		json_object_get(merge_when_clause, "condition"),
		views->where_columns,
		views->all_referenced_columns);
}

static void sqlparser_extract_merge_stmt(json_t *merge_stmt, sqlparser_column_views_t *views)
{
	json_t *when_clauses;
	size_t index;

	if (!json_is_object(merge_stmt)) {
		return;
	}

	sqlparser_walk_column_refs(
		json_object_get(merge_stmt, "joinCondition"),
		views->join_columns,
		views->all_referenced_columns);

	when_clauses = json_object_get(merge_stmt, "mergeWhenClauses");
	if (!json_is_array(when_clauses)) {
		return;
	}

	for (index = 0; index < json_array_size(when_clauses); index++) {
		json_t *entry;
		json_t *merge_when_clause;

		entry = json_array_get(when_clauses, index);
		merge_when_clause = json_object_get(entry, "MergeWhenClause");
		if (json_is_object(merge_when_clause)) {
			sqlparser_extract_merge_when_clause(merge_when_clause, views);
		}
	}
}

static void sqlparser_extract_drop_stmt_tables(json_t *drop_stmt, json_t *tables)
{
	json_t *objects;
	json_t *remove_type_json;
	const char *remove_type;
	size_t index;

	if (!json_is_object(drop_stmt) || !json_is_array(tables)) {
		return;
	}

	remove_type_json = json_object_get(drop_stmt, "removeType");
	remove_type = json_is_string(remove_type_json) ? json_string_value(remove_type_json) : NULL;
	if (!sqlparser_drop_stmt_targets_relation(remove_type)) {
		return;
	}

	objects = json_object_get(drop_stmt, "objects");
	if (!json_is_array(objects)) {
		return;
	}

	for (index = 0; index < json_array_size(objects); index++) {
		json_t *entry;
		json_t *list_node;
		const char *schema_name;
		const char *table_name;

		entry = json_array_get(objects, index);
		if (!json_is_object(entry)) {
			continue;
		}

		list_node = json_object_get(entry, "List");
		if (!json_is_object(list_node)) {
			continue;
		}

		schema_name = NULL;
		table_name = NULL;
		if (!sqlparser_extract_name_list_table_parts(
				json_object_get(list_node, "items"),
				&schema_name,
				&table_name)) {
			continue;
		}

		sqlparser_json_array_append_table(tables, schema_name, table_name, "ddl");
	}
}

static void sqlparser_extract_statement_columns(json_t *stmt_node, sqlparser_column_views_t *views)
{
	json_t *statement;

	if (!json_is_object(stmt_node)) {
		return;
	}

	statement = json_object_get(stmt_node, "SelectStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_select_stmt(statement, views);
		return;
	}

	statement = json_object_get(stmt_node, "InsertStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_insert_stmt(statement, views);
		return;
	}

	statement = json_object_get(stmt_node, "UpdateStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_update_stmt(statement, views);
		return;
	}

	statement = json_object_get(stmt_node, "DeleteStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_delete_stmt(statement, views);
		return;
	}

	statement = json_object_get(stmt_node, "MergeStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_merge_stmt(statement, views);
		return;
	}

	statement = json_object_get(stmt_node, "ViewStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_statement_columns(json_object_get(statement, "query"), views);
		return;
	}

	statement = json_object_get(stmt_node, "CreateTableAsStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_statement_columns(json_object_get(statement, "query"), views);
	}
}

static void sqlparser_extract_range_var_table(json_t *node, json_t *tables, const char *context)
{
	json_t *range_var;
	json_t *schema_name_json;
	json_t *table_name_json;
	const char *schema_name;
	const char *table_name;

	if (!json_is_object(node) || !json_is_array(tables)) {
		return;
	}

	range_var = json_object_get(node, "RangeVar");
	if (json_is_object(range_var)) {
		node = range_var;
	}

	schema_name_json = json_object_get(node, "schemaname");
	table_name_json = json_object_get(node, "relname");
	schema_name = json_is_string(schema_name_json) ? json_string_value(schema_name_json) : NULL;
	table_name = json_is_string(table_name_json) ? json_string_value(table_name_json) : NULL;
	sqlparser_json_array_append_table(tables, schema_name, table_name, context);
}

static void sqlparser_extract_merge_stmt_tables(json_t *merge_stmt, json_t *tables)
{
	if (!json_is_object(merge_stmt) || !json_is_array(tables)) {
		return;
	}

	sqlparser_extract_range_var_table(json_object_get(merge_stmt, "relation"), tables, "dml");
	sqlparser_extract_range_var_table(json_object_get(merge_stmt, "sourceRelation"), tables, "dml");
}

static void sqlparser_extract_additional_tables_from_statement(json_t *stmt_node, json_t *tables)
{
	json_t *statement;

	if (!json_is_object(stmt_node) || !json_is_array(tables)) {
		return;
	}

	statement = json_object_get(stmt_node, "DropStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_drop_stmt_tables(statement, tables);
	}

	statement = json_object_get(stmt_node, "MergeStmt");
	if (json_is_object(statement)) {
		sqlparser_extract_merge_stmt_tables(statement, tables);
	}
}

static void sqlparser_extract_columns_from_parse_tree(
	json_t *parse_tree_root,
	sqlparser_column_views_t *views)
{
	json_t *stmts;
	size_t index;

	if (!json_is_object(parse_tree_root)) {
		return;
	}

	stmts = json_object_get(parse_tree_root, "stmts");
	if (!json_is_array(stmts)) {
		return;
	}

	for (index = 0; index < json_array_size(stmts); index++) {
		json_t *stmt_entry;

		stmt_entry = json_array_get(stmts, index);
		if (!json_is_object(stmt_entry)) {
			continue;
		}

		sqlparser_extract_statement_columns(json_object_get(stmt_entry, "stmt"), views);
	}
}

static void sqlparser_extract_additional_tables_from_parse_tree(
	json_t *parse_tree_root,
	json_t *tables)
{
	json_t *stmts;
	size_t index;

	if (!json_is_object(parse_tree_root) || !json_is_array(tables)) {
		return;
	}

	stmts = json_object_get(parse_tree_root, "stmts");
	if (!json_is_array(stmts)) {
		return;
	}

	for (index = 0; index < json_array_size(stmts); index++) {
		json_t *stmt_entry;

		stmt_entry = json_array_get(stmts, index);
		if (!json_is_object(stmt_entry)) {
			continue;
		}

		sqlparser_extract_additional_tables_from_statement(json_object_get(stmt_entry, "stmt"), tables);
	}
}


sqlparser_status_t sqlparser_export_parse_tree_json(
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

	if (sqlparser_ensure_parse_tree_json_text(handle, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	if (!pretty) {
		*out_json = sqlparser_strdup(handle->parse_tree_json);
		if (*out_json == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	} else {
		json_error_t json_error;
		json_t *root;
		char *rendered;

		root = json_loads(handle->parse_tree_json, 0, &json_error);
		if (root == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"failed to parse libpg_query JSON output");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		rendered = json_dumps(root, JSON_INDENT(2) | JSON_SORT_KEYS | JSON_ENSURE_ASCII);
		json_decref(root);
		if (rendered == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (sqlparser_validate_handle_output_text(handle, rendered, "parse tree JSON", out_error) !=
		    SQLPARSER_STATUS_OK) {
			free(rendered);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_RESOURCE_LIMIT;
		}

		*out_json = rendered;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_export_summary_json(
	const sqlparser_handle_t *handle,
	int pretty,
	char **out_json,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	PgQuery__SummaryResult *summary;
	PgQuery__ScanResult *scan;
	const char *effective_sql;
	size_t effective_sql_len;
	json_t *parse_tree_root;
	json_t *root;
	json_t *keywords;
	json_t *statement_types;
	json_t *tables;
	json_t *aliases;
	json_t *cte_names;
	json_t *functions;
	json_t *filter_columns;
	json_t *selected_columns;
	json_t *join_columns;
	json_t *where_columns;
	json_t *insert_columns;
	json_t *update_columns;
	json_t *all_referenced_columns;
	sqlparser_column_views_t column_views;
	json_error_t json_error;
	size_t index;
	char *rendered;
	size_t flags;

	if (out_json == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_json must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_json = NULL;
	sqlparser_error_clear(out_error);

	status = sqlparser_ensure_summary(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_ensure_current_sql_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	effective_sql = sqlparser_effective_parser_sql(handle);
	effective_sql_len = effective_sql != NULL ? strlen(effective_sql) : 0U;

	status = sqlparser_ensure_scan(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_ensure_parse_tree_json_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	summary = pg_query__summary_result__unpack(
		NULL,
		handle->summary.len,
		(const uint8_t *)handle->summary.data);
	if (summary == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to unpack summary protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	scan = pg_query__scan_result__unpack(
		NULL,
		handle->scan.len,
		(const uint8_t *)handle->scan.data);
	if (scan == NULL) {
		pg_query__summary_result__free_unpacked(summary, NULL);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to unpack scan protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	parse_tree_root = json_loads(handle->parse_tree_json, 0, &json_error);
	if (parse_tree_root == NULL) {
		pg_query__summary_result__free_unpacked(summary, NULL);
		pg_query__scan_result__free_unpacked(scan, NULL);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to parse cached parse tree JSON");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	root = json_object();
	keywords = json_array();
	statement_types = json_array();
	tables = json_array();
	aliases = json_object();
	cte_names = json_array();
	functions = json_array();
	filter_columns = json_array();
	selected_columns = json_array();
	join_columns = json_array();
	where_columns = json_array();
	insert_columns = json_array();
	update_columns = json_array();
	all_referenced_columns = json_array();
	if (root == NULL ||
		keywords == NULL ||
		statement_types == NULL ||
		tables == NULL ||
		aliases == NULL ||
		cte_names == NULL ||
		functions == NULL ||
		filter_columns == NULL ||
		selected_columns == NULL ||
		join_columns == NULL ||
		where_columns == NULL ||
		insert_columns == NULL ||
		update_columns == NULL ||
		all_referenced_columns == NULL) {
		pg_query__summary_result__free_unpacked(summary, NULL);
		pg_query__scan_result__free_unpacked(scan, NULL);
		json_decref(parse_tree_root);
		if (root != NULL) {
			json_decref(root);
		}
		if (keywords != NULL) {
			json_decref(keywords);
		}
		if (statement_types != NULL) {
			json_decref(statement_types);
		}
		if (tables != NULL) {
			json_decref(tables);
		}
		if (aliases != NULL) {
			json_decref(aliases);
		}
		if (cte_names != NULL) {
			json_decref(cte_names);
		}
		if (functions != NULL) {
			json_decref(functions);
		}
		if (filter_columns != NULL) {
			json_decref(filter_columns);
		}
		if (selected_columns != NULL) {
			json_decref(selected_columns);
		}
		if (join_columns != NULL) {
			json_decref(join_columns);
		}
		if (where_columns != NULL) {
			json_decref(where_columns);
		}
		if (insert_columns != NULL) {
			json_decref(insert_columns);
		}
		if (update_columns != NULL) {
			json_decref(update_columns);
		}
		if (all_referenced_columns != NULL) {
			json_decref(all_referenced_columns);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	(void)json_object_set_new(root, "statement_count", json_integer((json_int_t)handle->statement_count));
	(void)json_object_set_new(root, "keywords", keywords);
	(void)json_object_set_new(root, "statement_types", statement_types);
	(void)json_object_set_new(root, "tables", tables);
	(void)json_object_set_new(root, "aliases", aliases);
	(void)json_object_set_new(root, "cte_names", cte_names);
	(void)json_object_set_new(root, "functions", functions);
	(void)json_object_set_new(root, "filter_columns", filter_columns);
	(void)json_object_set_new(root, "selected_columns", selected_columns);
	(void)json_object_set_new(root, "join_columns", join_columns);
	(void)json_object_set_new(root, "where_columns", where_columns);
	(void)json_object_set_new(root, "insert_columns", insert_columns);
	(void)json_object_set_new(root, "update_columns", update_columns);
	(void)json_object_set_new(root, "all_referenced_columns", all_referenced_columns);

	column_views.selected_columns = selected_columns;
	column_views.join_columns = join_columns;
	column_views.where_columns = where_columns;
	column_views.insert_columns = insert_columns;
	column_views.update_columns = update_columns;
	column_views.all_referenced_columns = all_referenced_columns;
	sqlparser_extract_columns_from_parse_tree(parse_tree_root, &column_views);

	for (index = 0; index < scan->n_tokens; index++) {
		PgQuery__ScanToken *token;
		size_t start;
		size_t end;
		size_t len;
		char *keyword;

		token = scan->tokens[index];
		if (token->keyword_kind == PG_QUERY__KEYWORD_KIND__NO_KEYWORD) {
			continue;
		}
		if (token->start < 0 || token->end < token->start) {
			continue;
		}

		start = (size_t)token->start;
		end = (size_t)token->end;
		if (end > effective_sql_len || start >= effective_sql_len) {
			continue;
		}

		len = end - start;
		if (len == 0U) {
			continue;
		}

		keyword = sqlparser_strndup_lower_ascii(effective_sql + start, len);
		if (keyword == NULL) {
			continue;
		}
		if (handle->dialect_ops != NULL && handle->dialect_ops->summary_keyword != NULL) {
			const char *dialect_keyword;
			char *normalized_keyword;

			dialect_keyword = handle->dialect_ops->summary_keyword(keyword, handle->dialect_state);
			if (dialect_keyword != NULL) {
				normalized_keyword = sqlparser_strdup(dialect_keyword);
				if (normalized_keyword == NULL) {
					free(keyword);
					continue;
				}
				free(keyword);
				keyword = normalized_keyword;
			}
		}

		if (!sqlparser_json_array_contains_string(keywords, keyword)) {
			(void)json_array_append_new(keywords, json_string(keyword));
		}
		free(keyword);
	}

	for (index = 0; index < summary->n_statement_types; index++) {
		(void)json_array_append_new(statement_types, json_string(summary->statement_types[index]));
	}

	for (index = 0; index < summary->n_tables; index++) {
		PgQuery__SummaryResult__Table *table;
		json_t *entry;

		table = summary->tables[index];
		entry = json_object();
		if (entry == NULL) {
			continue;
		}

		sqlparser_json_object_set_nonempty_string(entry, "name", table->name);
		sqlparser_json_object_set_nonempty_string(entry, "schema_name", table->schema_name);
		sqlparser_json_object_set_nonempty_string(entry, "table_name", table->table_name);
		(void)json_object_set_new(entry, "context", json_string(sqlparser_summary_context_name(table->context)));
		(void)json_array_append_new(tables, entry);
	}

	sqlparser_extract_additional_tables_from_parse_tree(parse_tree_root, tables);

	for (index = 0; index < summary->n_aliases; index++) {
		PgQuery__SummaryResult__AliasesEntry *alias_entry;

		alias_entry = summary->aliases[index];
		if (alias_entry->key == NULL || alias_entry->key[0] == '\0') {
			continue;
		}

		(void)json_object_set_new(
			aliases,
			alias_entry->key,
			json_string(alias_entry->value != NULL ? alias_entry->value : ""));
	}

	for (index = 0; index < summary->n_cte_names; index++) {
		(void)json_array_append_new(cte_names, json_string(summary->cte_names[index]));
	}

	for (index = 0; index < summary->n_functions; index++) {
		PgQuery__SummaryResult__Function *function;
		json_t *entry;

		function = summary->functions[index];
		entry = json_object();
		if (entry == NULL) {
			continue;
		}

		sqlparser_json_object_set_nonempty_string(entry, "name", function->name);
		sqlparser_json_object_set_nonempty_string(entry, "function_name", function->function_name);
		sqlparser_json_object_set_nonempty_string(entry, "schema_name", function->schema_name);
		(void)json_object_set_new(entry, "context", json_string(sqlparser_summary_context_name(function->context)));
		(void)json_array_append_new(functions, entry);
	}

	for (index = 0; index < summary->n_filter_columns; index++) {
		PgQuery__SummaryResult__FilterColumn *column;
		json_t *entry;

		column = summary->filter_columns[index];
		entry = json_object();
		if (entry == NULL) {
			continue;
		}

		sqlparser_json_object_set_nonempty_string(entry, "schema_name", column->schema_name);
		sqlparser_json_object_set_nonempty_string(entry, "table_name", column->table_name);
		sqlparser_json_object_set_nonempty_string(entry, "column", column->column);
		(void)json_array_append_new(filter_columns, entry);
	}

	if (summary->truncated_query != NULL && summary->truncated_query[0] != '\0') {
		(void)json_object_set_new(root, "truncated_query", json_string(summary->truncated_query));
	}

	flags = JSON_ENSURE_ASCII | JSON_SORT_KEYS;
	if (pretty) {
		flags |= JSON_INDENT(2);
	} else {
		flags |= JSON_COMPACT;
	}

	rendered = json_dumps(root, flags);
	pg_query__summary_result__free_unpacked(summary, NULL);
	pg_query__scan_result__free_unpacked(scan, NULL);
	json_decref(parse_tree_root);
	json_decref(root);
	if (rendered == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_validate_handle_output_text(handle, rendered, "summary JSON", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(rendered);
		return status;
	}

	*out_json = rendered;
	return SQLPARSER_STATUS_OK;
}
