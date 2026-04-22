#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "protobuf/pg_query.pb-c.h"
#include "sqlparser_internal.h"

#ifndef SQLPARSER_VERSION_TEXT
#define SQLPARSER_VERSION_TEXT "0.1.0-dev"
#endif

#ifndef SQLPARSER_LIBPG_QUERY_TAG_TEXT
#define SQLPARSER_LIBPG_QUERY_TAG_TEXT "17-6.2.2"
#endif

extern __thread sig_atomic_t pg_query_initialized;

static void sqlparser_pg_query_shutdown(void)
{
	/*
	 * libpg_query keeps a thread-local TopMemoryContext alive for the lifetime
	 * of the process unless pg_query_exit() is called. Reinitializing that
	 * state on every API call is not stable enough for production, so we
	 * register one process-exit cleanup hook instead.
	 */
	if (pg_query_initialized != 0) {
		pg_query_exit();
		pg_query_initialized = 0;
	}
}

static void sqlparser_pg_query_register_exit_once(void)
{
	(void)atexit(sqlparser_pg_query_shutdown);
}

void sqlparser_pg_query_prepare(void)
{
	static pthread_once_t once_control = PTHREAD_ONCE_INIT;

	(void)pthread_once(&once_control, sqlparser_pg_query_register_exit_once);
}

static void sqlparser_fill_line_column(
	const char *sql,
	int cursor,
	int *line_out,
	int *column_out)
{
	int line;
	int column;
	size_t index;
	size_t stop;

	if (line_out != NULL) {
		*line_out = 0;
	}
	if (column_out != NULL) {
		*column_out = 0;
	}
	if (sql == NULL || cursor <= 0) {
		return;
	}

	line = 1;
	column = 1;
	stop = (size_t)(cursor - 1);

	for (index = 0; sql[index] != '\0' && index < stop; index++) {
		if (sql[index] == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
	}

	if (line_out != NULL) {
		*line_out = line;
	}
	if (column_out != NULL) {
		*column_out = column;
	}
}

void sqlparser_error_clear(sqlparser_error_t *out_error)
{
	if (out_error == NULL) {
		return;
	}

	memset(out_error, 0, sizeof(*out_error));
	out_error->code = SQLPARSER_STATUS_OK;
}

void sqlparser_error_set_message(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *message)
{
	const char *final_message;

	if (out_error == NULL) {
		return;
	}

	sqlparser_error_clear(out_error);
	out_error->code = code;
	final_message = message != NULL ? message : "unknown error";
	(void)snprintf(out_error->message, sizeof(out_error->message), "%s", final_message);
}

void sqlparser_error_from_pg(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *sql,
	const PgQueryError *error)
{
	if (error == NULL) {
		sqlparser_error_set_message(out_error, code, "unknown parser error");
		return;
	}

	sqlparser_error_clear(out_error);
	out_error->code = code;
	out_error->cursor = error->cursorpos;
	sqlparser_fill_line_column(sql, error->cursorpos, &out_error->line, &out_error->column);
	(void)snprintf(
		out_error->message,
		sizeof(out_error->message),
		"%s",
		error->message != NULL ? error->message : "unknown parser error");
}

char *sqlparser_strdup(const char *text)
{
	size_t len;

	if (text == NULL) {
		return NULL;
	}

	len = strlen(text);
	return sqlparser_strndup(text, len);
}

char *sqlparser_strndup(const char *text, size_t len)
{
	char *copy;

	if (text == NULL) {
		return NULL;
	}

	copy = (char *)malloc(len + 1U);
	if (copy == NULL) {
		return NULL;
	}

	if (len > 0U) {
		memcpy(copy, text, len);
	}
	copy[len] = '\0';
	return copy;
}

sqlparser_status_t sqlparser_handle_ensure_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	if (handle == NULL || handle->parse_tree.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->ast != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	handle->ast = pg_query__parse_result__unpack(
		NULL,
		handle->parse_tree.len,
		(const uint8_t *)handle->parse_tree.data);
	if (handle->ast == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to unpack parse tree protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	return SQLPARSER_STATUS_OK;
}

void sqlparser_handle_clear_ast(sqlparser_handle_t *handle)
{
	if (handle == NULL || handle->ast == NULL) {
		return;
	}

	pg_query__parse_result__free_unpacked(handle->ast, NULL);
	handle->ast = NULL;
}

void sqlparser_handle_invalidate_derived(sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}

	free(handle->current_sql);
	handle->current_sql = NULL;

	free(handle->parse_tree_json);
	handle->parse_tree_json = NULL;

	free(handle->model_json);
	handle->model_json = NULL;

	free(handle->summary.data);
	handle->summary.data = NULL;
	handle->summary.len = 0U;

	free(handle->scan.data);
	handle->scan.data = NULL;
	handle->scan.len = 0U;
}

sqlparser_status_t sqlparser_handle_commit_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	size_t packed_size;
	size_t packed_len;
	char *packed;

	if (handle == NULL || handle->ast == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	packed_size = pg_query__parse_result__get_packed_size(handle->ast);
	if (packed_size == 0U) {
		sqlparser_handle_clear_ast(handle);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to repack parse tree protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	packed = (char *)malloc(packed_size);
	if (packed == NULL) {
		sqlparser_handle_clear_ast(handle);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	packed_len = pg_query__parse_result__pack(handle->ast, (uint8_t *)packed);
	if (packed_len != packed_size) {
		free(packed);
		sqlparser_handle_clear_ast(handle);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to repack parse tree protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	free(handle->parse_tree.data);
	handle->parse_tree.data = packed;
	handle->parse_tree.len = packed_len;
	handle->statement_count = handle->ast->n_stmts;
	handle->generation++;
	sqlparser_handle_invalidate_derived(handle);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_ensure_current_sql_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	PgQueryDeparseResult deparse_result;
	sqlparser_handle_t *mutable_handle;

	if (handle == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->generation == 0UL) {
		return SQLPARSER_STATUS_OK;
	}
	if (handle->current_sql != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	sqlparser_pg_query_prepare();
	deparse_result = pg_query_deparse_protobuf(handle->parse_tree);
	if (deparse_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			handle->sql,
			deparse_result.error);
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	mutable_handle->current_sql = sqlparser_strdup(deparse_result.query);
	pg_query_free_deparse_result(deparse_result);
	if (mutable_handle->current_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_effective_sql(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return NULL;
	}

	if (handle->generation == 0UL || handle->current_sql == NULL) {
		return handle->sql;
	}

	return handle->current_sql;
}

static size_t sqlparser_detect_statement_count(const char *sql)
{
	PgQuerySplitResult split_result;
	size_t statement_count;

	statement_count = 1U;
	sqlparser_pg_query_prepare();
	split_result = pg_query_split_with_parser(sql);
	if (split_result.error == NULL && split_result.n_stmts > 0) {
		statement_count = (size_t)split_result.n_stmts;
	}
	pg_query_free_split_result(split_result);
	return statement_count;
}

static char *sqlparser_strndup_lower_ascii(const char *text, size_t len)
{
	char *copy;
	size_t index;

	copy = sqlparser_strndup(text, len);
	if (copy == NULL) {
		return NULL;
	}

	for (index = 0; index < len; index++) {
		copy[index] = (char)tolower((unsigned char)copy[index]);
	}

	return copy;
}

static int sqlparser_json_array_contains_string(json_t *array, const char *value)
{
	size_t index;

	if (array == NULL || value == NULL) {
		return 0;
	}

	for (index = 0; index < json_array_size(array); index++) {
		json_t *entry;
		const char *entry_text;

		entry = json_array_get(array, index);
		if (!json_is_string(entry)) {
			continue;
		}

		entry_text = json_string_value(entry);
		if (entry_text != NULL && strcmp(entry_text, value) == 0) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_json_array_contains_table_name(json_t *array, const char *name)
{
	size_t index;

	if (array == NULL || name == NULL || name[0] == '\0') {
		return 0;
	}

	for (index = 0; index < json_array_size(array); index++) {
		json_t *entry;
		json_t *name_json;
		const char *entry_name;

		entry = json_array_get(array, index);
		if (!json_is_object(entry)) {
			continue;
		}

		name_json = json_object_get(entry, "name");
		if (!json_is_string(name_json)) {
			continue;
		}

		entry_name = json_string_value(name_json);
		if (entry_name != NULL && strcmp(entry_name, name) == 0) {
			return 1;
		}
	}

	return 0;
}

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

static void sqlparser_json_object_set_nonempty_string(
	json_t *object,
	const char *key,
	const char *value)
{
	if (object == NULL || key == NULL || value == NULL || value[0] == '\0') {
		return;
	}

	(void)json_object_set_new(object, key, json_string(value));
}

static sqlparser_status_t sqlparser_json_object_set_string(
	json_t *object,
	const char *key,
	const char *value,
	sqlparser_error_t *out_error)
{
	json_t *string_value;

	if (object == NULL || key == NULL || value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"JSON string field requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	string_value = json_string(value);
	if (string_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	if (json_object_set_new(object, key, string_value) != 0) {
		json_decref(string_value);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static void sqlparser_json_array_append_table(
	json_t *array,
	const char *schema_name,
	const char *table_name,
	const char *context)
{
	char *full_name;
	size_t full_name_len;
	json_t *entry;

	if (array == NULL || table_name == NULL || table_name[0] == '\0') {
		return;
	}

	full_name = NULL;
	if (schema_name != NULL && schema_name[0] != '\0') {
		full_name_len = strlen(schema_name) + 1U + strlen(table_name) + 1U;
		full_name = (char *)malloc(full_name_len);
		if (full_name == NULL) {
			return;
		}

		(void)snprintf(full_name, full_name_len, "%s.%s", schema_name, table_name);
	} else {
		full_name = sqlparser_strdup(table_name);
		if (full_name == NULL) {
			return;
		}
	}

	if (sqlparser_json_array_contains_table_name(array, full_name)) {
		free(full_name);
		return;
	}

	entry = json_object();
	if (entry == NULL) {
		free(full_name);
		return;
	}

	sqlparser_json_object_set_nonempty_string(entry, "name", full_name);
	sqlparser_json_object_set_nonempty_string(entry, "schema_name", schema_name);
	sqlparser_json_object_set_nonempty_string(entry, "table_name", table_name);
	sqlparser_json_object_set_nonempty_string(entry, "context", context);
	(void)json_array_append_new(array, entry);
	free(full_name);
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

	effective_sql = sqlparser_effective_sql(handle);
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

	effective_sql = sqlparser_effective_sql(handle);
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

	effective_sql = sqlparser_effective_sql(handle);
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

const char *sqlparser_version_string(void)
{
	return SQLPARSER_VERSION_TEXT;
}

const char *sqlparser_libpg_query_tag(void)
{
	return SQLPARSER_LIBPG_QUERY_TAG_TEXT;
}

const char *sqlparser_statement_kind_name(sqlparser_statement_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_STATEMENT_KIND_SELECT:
			return "select";
		case SQLPARSER_STATEMENT_KIND_INSERT:
			return "insert";
		case SQLPARSER_STATEMENT_KIND_UPDATE:
			return "update";
		case SQLPARSER_STATEMENT_KIND_DELETE:
			return "delete";
		case SQLPARSER_STATEMENT_KIND_MERGE:
			return "merge";
		case SQLPARSER_STATEMENT_KIND_TRANSACTION:
			return "transaction";
		case SQLPARSER_STATEMENT_KIND_DDL:
			return "ddl";
		case SQLPARSER_STATEMENT_KIND_CALL:
			return "call";
		case SQLPARSER_STATEMENT_KIND_OTHER:
			return "other";
		case SQLPARSER_STATEMENT_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_insert_source_kind_name(sqlparser_insert_source_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_INSERT_SOURCE_VALUES:
			return "values";
		case SQLPARSER_INSERT_SOURCE_QUERY:
			return "query";
		case SQLPARSER_INSERT_SOURCE_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_value_kind_name(sqlparser_value_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_VALUE_KIND_LITERAL:
			return "literal";
		case SQLPARSER_VALUE_KIND_DEFAULT:
			return "default";
		case SQLPARSER_VALUE_KIND_EXPRESSION:
			return "expression";
		case SQLPARSER_VALUE_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_literal_kind_name(sqlparser_literal_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_LITERAL_KIND_NULL:
			return "null";
		case SQLPARSER_LITERAL_KIND_STRING:
			return "string";
		case SQLPARSER_LITERAL_KIND_INTEGER:
			return "integer";
		case SQLPARSER_LITERAL_KIND_FLOAT:
			return "float";
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			return "boolean";
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_selector_kind_name(sqlparser_selector_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_SELECTOR_KIND_RELATION:
			return "relation";
		case SQLPARSER_SELECTOR_KIND_NAME:
			return "name";
		case SQLPARSER_SELECTOR_KIND_LITERAL:
			return "literal";
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
			return "where_literal";
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
			return "assignment";
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			return "insert_cell";
		case SQLPARSER_SELECTOR_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

sqlparser_status_t sqlparser_parse(
	const char *sql,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	PgQueryProtobufParseResult parse_result;
	sqlparser_handle_t *handle;

	if (out_handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_handle = NULL;
	sqlparser_error_clear(out_error);

	if (sql == NULL || sql[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"sql must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_pg_query_prepare();
	parse_result = pg_query_parse_protobuf(sql);
	if (parse_result.error != NULL) {
		sqlparser_error_from_pg(out_error, SQLPARSER_STATUS_PARSE_ERROR, sql, parse_result.error);
		pg_query_free_protobuf_parse_result(parse_result);
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	handle = (sqlparser_handle_t *)calloc(1U, sizeof(*handle));
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_protobuf_parse_result(parse_result);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	handle->sql = sqlparser_strdup(sql);
	if (handle->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_protobuf_parse_result(parse_result);
		free(handle);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	handle->sql_len = strlen(sql);
	handle->statement_count = sqlparser_detect_statement_count(sql);
	handle->parse_tree.data = sqlparser_strndup(parse_result.parse_tree.data, parse_result.parse_tree.len);
	handle->parse_tree.len = parse_result.parse_tree.len;

	if (handle->parse_tree.data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_protobuf_parse_result(parse_result);
		free(handle->sql);
		free(handle);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query_free_protobuf_parse_result(parse_result);
	*out_handle = handle;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_handle_destroy(sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}

	sqlparser_handle_clear_ast(handle);
	sqlparser_handle_invalidate_derived(handle);
	free(handle->parse_tree.data);
	free(handle->sql);
	free(handle);
}

const char *sqlparser_original_sql(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return NULL;
	}

	return handle->sql;
}

size_t sqlparser_statement_count(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return 0U;
	}

	return handle->statement_count;
}

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
	} else if (strncmp(text + offset, "where_literal", 13) == 0) {
		offset += 13U;
		out_selector->kind = SQLPARSER_SELECTOR_KIND_WHERE_LITERAL;
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
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
			length = snprintf(
				buffer,
				sizeof(buffer),
				"stmt[%lu].where_literal[%lu]",
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

static int sqlparser_strings_equal_nullable(const char *left, const char *right)
{
	if (left == NULL || right == NULL) {
		return left == right;
	}

	return strcmp(left, right) == 0;
}

static int sqlparser_literal_view_equals_value(
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
			return sqlparser_strings_equal_nullable(view->string_value, value->string_value);
		case SQLPARSER_LITERAL_KIND_INTEGER:
			return view->integer_value == value->integer_value;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			return sqlparser_strings_equal_nullable(view->float_value, value->float_value);
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			return view->boolean_value == value->boolean_value;
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			return 0;
	}
}

static json_t *sqlparser_literal_view_to_json(const sqlparser_literal_view_t *view)
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

static sqlparser_status_t sqlparser_literal_value_from_json(
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

static sqlparser_status_t sqlparser_json_object_set_selector(
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
		status = sqlparser_json_object_set_selector(entry, &selector, out_error);
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
		status = sqlparser_json_object_set_selector(entry, &selector, out_error);
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
		status = sqlparser_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(literals);
			return status;
		}

		literal_json = sqlparser_literal_view_to_json(&literal);
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
		status = sqlparser_json_object_set_selector(entry, &selector, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(entry);
			json_decref(where_literals);
			return status;
		}

		sqlparser_json_object_set_nonempty_string(entry, "table_name", where_literal.table_name);
		sqlparser_json_object_set_nonempty_string(entry, "column_name", where_literal.column_name);
		sqlparser_json_object_set_nonempty_string(entry, "operator_name", where_literal.operator_name);
		literal_json = sqlparser_literal_view_to_json(&where_literal.literal);
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
		status = sqlparser_json_object_set_selector(entry, &selector, out_error);
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
			literal_json = sqlparser_literal_view_to_json(&assignment.literal);
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
			status = sqlparser_json_object_set_selector(cell_object, &selector, out_error);
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
				literal_json = sqlparser_literal_view_to_json(&literal);
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

	(void)json_object_set_new(root, "schema", json_string("sqlparser.model/v1"));
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

	mutable_handle = (sqlparser_handle_t *)handle;
	mutable_handle->model_json = rendered;
	return SQLPARSER_STATUS_OK;
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

	effective_sql = sqlparser_effective_sql(handle);
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

	*out_json = rendered;
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

		*out_json = rendered;
	}

	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_json_string_or_null(json_t *value)
{
	if (!json_is_string(value)) {
		return NULL;
	}

	return json_string_value(value);
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

	if (sqlparser_strings_equal_nullable(schema_name, current.schema_name) &&
	    sqlparser_strings_equal_nullable(table_name, current.table_name)) {
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

	if (sqlparser_strings_equal_nullable(current.value, value)) {
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
			status = sqlparser_literal_value_from_json(literal_json, &target, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			memset(&current, 0, sizeof(current));
			status = sqlparser_selector_literal(handle, selector, &current, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (sqlparser_literal_view_equals_value(&current, &target)) {
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
			status = sqlparser_literal_value_from_json(literal_json, &target, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			memset(&current, 0, sizeof(current));
			status = sqlparser_selector_where_literal(handle, selector, &current, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (sqlparser_literal_view_equals_value(&current.literal, &target)) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_selector_set_where_literal(handle, selector, &target, out_error);
		}
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
		{
			sqlparser_assignment_view_t current;

			memset(&current, 0, sizeof(current));
			if (json_is_object(literal_json)) {
				status = sqlparser_literal_value_from_json(literal_json, &target, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				status = sqlparser_selector_update_assignment(handle, selector, &current, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (current.value_kind == SQLPARSER_VALUE_KIND_LITERAL) {
					if (!sqlparser_literal_view_equals_value(&current.literal, &target)) {
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
				if (sqlparser_strings_equal_nullable(current_sql, sql_text)) {
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
				status = sqlparser_literal_value_from_json(literal_json, &target, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				status = sqlparser_selector_insert_cell_literal(handle, selector, &current, out_error);
				if (status == SQLPARSER_STATUS_OK) {
					if (!sqlparser_literal_view_equals_value(&current, &target)) {
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
				if (sqlparser_strings_equal_nullable(current_sql, sql_text)) {
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

static sqlparser_status_t sqlparser_apply_change_array(
	sqlparser_handle_t *handle,
	json_t *changes,
	sqlparser_error_t *out_error)
{
	size_t index;
	json_t *change;

	if (!json_is_array(changes)) {
		return SQLPARSER_STATUS_OK;
	}

	json_array_foreach(changes, index, change) {
		sqlparser_status_t status;

		status = sqlparser_apply_change_object(handle, change, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_apply_statement_model_object(
	sqlparser_handle_t *handle,
	json_t *statement_object,
	sqlparser_error_t *out_error)
{
	json_t *insert_object;
	json_t *rows;
	size_t row_index;
	json_t *row_object;
	sqlparser_status_t status;

	if (!json_is_object(statement_object)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement model entry must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_apply_change_array(
		handle,
		json_object_get(statement_object, "relations"),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_apply_change_array(
		handle,
		json_object_get(statement_object, "names"),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	insert_object = json_object_get(statement_object, "insert");
	if (json_is_object(insert_object)) {
		rows = json_object_get(insert_object, "rows");
		if (json_is_array(rows)) {
			json_array_foreach(rows, row_index, row_object) {
				status = sqlparser_apply_change_array(
					handle,
					json_object_get(row_object, "cells"),
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
		}
	}

	status = sqlparser_apply_change_array(
		handle,
		json_object_get(statement_object, "update_assignments"),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_apply_change_array(
		handle,
		json_object_get(statement_object, "where_literals"),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_apply_change_array(
		handle,
		json_object_get(statement_object, "literals"),
		out_error);
}

sqlparser_status_t sqlparser_apply_model_json(
	sqlparser_handle_t *handle,
	const char *json_text,
	sqlparser_error_t *out_error)
{
	json_t *root;
	json_t *changes;
	json_t *statements;
	json_error_t json_error;
	sqlparser_status_t status;
	size_t index;
	json_t *statement_object;

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

	root = json_loads(json_text, 0, &json_error);
	if (root == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"failed to parse model JSON");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (!json_is_object(root)) {
		json_decref(root);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON root must be an object");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	changes = json_object_get(root, "changes");
	if (json_is_array(changes)) {
		status = sqlparser_apply_change_array(handle, changes, out_error);
		json_decref(root);
		return status;
	}

	statements = json_object_get(root, "statements");
	if (!json_is_array(statements)) {
		json_decref(root);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"model JSON must contain 'changes' or 'statements'");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	json_array_foreach(statements, index, statement_object) {
		status = sqlparser_apply_statement_model_object(handle, statement_object, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(root);
			return status;
		}
	}

	json_decref(root);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_deparse(
	const sqlparser_handle_t *handle,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQueryDeparseResult deparse_result;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	sqlparser_error_clear(out_error);

	if (handle == NULL || handle->parse_tree.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_pg_query_prepare();
	deparse_result = pg_query_deparse_protobuf(handle->parse_tree);
	if (deparse_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			handle->sql,
			deparse_result.error);
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_sql = sqlparser_strdup(deparse_result.query);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query_free_deparse_result(deparse_result);
	return SQLPARSER_STATUS_OK;
}

void sqlparser_string_free(char *text)
{
	free(text);
}
