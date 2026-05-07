#include <ctype.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif
#include <stdint.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <jansson.h>

#include "protobuf/pg_query.pb-c.h"
#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_internal.h"

#ifndef SQLPARSER_VERSION_TEXT
#define SQLPARSER_VERSION_TEXT "0.1.0-dev"
#endif

#ifndef SQLPARSER_LIBPG_QUERY_TAG_TEXT
#define SQLPARSER_LIBPG_QUERY_TAG_TEXT "17-6.2.2"
#endif

#ifndef SQLPARSER_MODEL_SCHEMA_TEXT
#define SQLPARSER_MODEL_SCHEMA_TEXT "sqlparser.model/v1"
#endif

#ifndef SQLPARSER_DEFAULT_MAX_SQL_BYTES
#define SQLPARSER_DEFAULT_MAX_SQL_BYTES (4U * 1024U * 1024U)
#endif

#ifndef SQLPARSER_DEFAULT_MAX_MODEL_JSON_BYTES
#define SQLPARSER_DEFAULT_MAX_MODEL_JSON_BYTES (16U * 1024U * 1024U)
#endif

#ifndef SQLPARSER_DEFAULT_MAX_OUTPUT_BYTES
#define SQLPARSER_DEFAULT_MAX_OUTPUT_BYTES (64U * 1024U * 1024U)
#endif

#ifndef SQLPARSER_DEFAULT_MAX_STATEMENT_COUNT
#define SQLPARSER_DEFAULT_MAX_STATEMENT_COUNT 64U
#endif

#if defined(_MSC_VER) && !defined(__thread)
#define __thread __declspec(thread)
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

#if defined(_WIN32)
static BOOL CALLBACK sqlparser_pg_query_register_exit_once_win(
	PINIT_ONCE init_once,
	PVOID parameter,
	PVOID *context)
{
	(void)init_once;
	(void)parameter;
	(void)context;
	sqlparser_pg_query_register_exit_once();
	return TRUE;
}

void sqlparser_pg_query_prepare(void)
{
	static INIT_ONCE once_control = INIT_ONCE_STATIC_INIT;

	(void)InitOnceExecuteOnce(&once_control, sqlparser_pg_query_register_exit_once_win, NULL, NULL);
}
#else
void sqlparser_pg_query_prepare(void)
{
	static pthread_once_t once_control = PTHREAD_ONCE_INIT;

	(void)pthread_once(&once_control, sqlparser_pg_query_register_exit_once);
}
#endif

static void sqlparser_limits_set_defaults(sqlparser_limits_t *out_limits)
{
	if (out_limits == NULL) {
		return;
	}

	out_limits->max_sql_bytes = SQLPARSER_DEFAULT_MAX_SQL_BYTES;
	out_limits->max_model_json_bytes = SQLPARSER_DEFAULT_MAX_MODEL_JSON_BYTES;
	out_limits->max_output_bytes = SQLPARSER_DEFAULT_MAX_OUTPUT_BYTES;
	out_limits->max_statement_count = SQLPARSER_DEFAULT_MAX_STATEMENT_COUNT;
	out_limits->struct_size = sizeof(*out_limits);
}

void sqlparser_limits_default(sqlparser_limits_t *out_limits)
{
	sqlparser_limits_set_defaults(out_limits);
}

void sqlparser_limits_normalize(
	const sqlparser_limits_t *limits,
	sqlparser_limits_t *out_limits)
{
	sqlparser_limits_t defaults;

	if (out_limits == NULL) {
		return;
	}

	sqlparser_limits_set_defaults(&defaults);
	if (limits == NULL) {
		*out_limits = defaults;
		return;
	}

	*out_limits = *limits;
	if (out_limits->struct_size == 0U) {
		out_limits->struct_size = defaults.struct_size;
	}
	if (out_limits->max_sql_bytes == 0U) {
		out_limits->max_sql_bytes = defaults.max_sql_bytes;
	}
	if (out_limits->max_model_json_bytes == 0U) {
		out_limits->max_model_json_bytes = defaults.max_model_json_bytes;
	}
	if (out_limits->max_output_bytes == 0U) {
		out_limits->max_output_bytes = defaults.max_output_bytes;
	}
	if (out_limits->max_statement_count == 0U) {
		out_limits->max_statement_count = defaults.max_statement_count;
	}
}

static void sqlparser_parse_options_set_defaults(sqlparser_parse_options_t *out_options)
{
	if (out_options == NULL) {
		return;
	}

	memset(out_options, 0, sizeof(*out_options));
	out_options->struct_size = sizeof(*out_options);
	out_options->dialect = SQLPARSER_DIALECT_POSTGRESQL;
	sqlparser_limits_set_defaults(&out_options->limits);
	out_options->flags = 0U;
}

void sqlparser_parse_options_default(sqlparser_parse_options_t *out_options)
{
	sqlparser_parse_options_set_defaults(out_options);
}

static void sqlparser_parse_options_normalize(
	const sqlparser_parse_options_t *options,
	sqlparser_parse_options_t *out_options)
{
	sqlparser_parse_options_t defaults;
	size_t copy_size;

	if (out_options == NULL) {
		return;
	}

	sqlparser_parse_options_set_defaults(&defaults);
	if (options == NULL) {
		*out_options = defaults;
		return;
	}

	*out_options = defaults;
	copy_size = options->struct_size;
	if (copy_size == 0U || copy_size > sizeof(*out_options)) {
		copy_size = sizeof(*out_options);
	}
	memcpy(out_options, options, copy_size);
	if (out_options->struct_size == 0U) {
		out_options->struct_size = defaults.struct_size;
	}
	sqlparser_limits_normalize(&out_options->limits, &out_options->limits);
}

sqlparser_status_t sqlparser_validate_text_limit(
	const char *text,
	size_t max_bytes,
	const char *field_name,
	size_t *out_len,
	sqlparser_error_t *out_error)
{
	size_t len;
	const char *name;

	if (out_len != NULL) {
		*out_len = 0U;
	}
	if (text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"text must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	name = field_name != NULL ? field_name : "text";
	len = 0U;
	while (text[len] != '\0') {
		if (max_bytes > 0U && len >= max_bytes) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"%s exceeds configured byte limit (%lu bytes)",
				name,
				(unsigned long)max_bytes);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, message);
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		len++;
	}

	if (out_len != NULL) {
		*out_len = len;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_validate_handle_sql_input(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error)
{
	if (handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_validate_text_limit(
		text,
		handle->limits.max_sql_bytes,
		field_name,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_validate_handle_output_text(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error)
{
	if (handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_validate_text_limit(
		text,
		handle->limits.max_output_bytes,
		field_name,
		NULL,
		out_error);
}

static sqlparser_status_t sqlparser_validate_statement_count_limit(
	const sqlparser_limits_t *limits,
	size_t statement_count,
	sqlparser_error_t *out_error)
{
	size_t max_statement_count;

	if (limits == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	max_statement_count = limits->max_statement_count;
	if (max_statement_count > 0U && statement_count > max_statement_count) {
		char message[256];

		(void)snprintf(
			message,
			sizeof(message),
			"statement count exceeds configured limit (%lu statements)",
			(unsigned long)max_statement_count);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, message);
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	return SQLPARSER_STATUS_OK;
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

static sqlparser_status_t sqlparser_protobuf_copy(
	PgQueryProtobuf *dst,
	const PgQueryProtobuf *src,
	sqlparser_error_t *out_error)
{
	if (dst == NULL || src == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"protobuf copy requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	dst->data = NULL;
	dst->len = 0U;
	if (src->len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (src->data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"protobuf data is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	dst->data = (char *)malloc(src->len);
	if (dst->data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memcpy(dst->data, src->data, src->len);
	dst->len = src->len;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_protobuf_release(PgQueryProtobuf *value)
{
	if (value == NULL) {
		return;
	}

	free(value->data);
	value->data = NULL;
	value->len = 0U;
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

	free(handle->current_parser_sql);
	handle->current_parser_sql = NULL;

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

static void sqlparser_handle_release_contents(sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}

	sqlparser_handle_clear_ast(handle);
	sqlparser_handle_invalidate_derived(handle);
	sqlparser_protobuf_release(&handle->parse_tree);
	if (handle->dialect_ops != NULL && handle->dialect_ops->destroy_state != NULL && handle->dialect_state != NULL) {
		handle->dialect_ops->destroy_state(handle->dialect_state);
	}
	handle->dialect_state = NULL;
	free(handle->sql);
	free(handle->parser_sql);
	handle->sql = NULL;
	handle->parser_sql = NULL;
	handle->sql_len = 0U;
	handle->parser_sql_len = 0U;
	handle->statement_count = 0U;
	handle->generation = 0UL;
	handle->dialect = SQLPARSER_DIALECT_POSTGRESQL;
	handle->dialect_ops = NULL;
}

sqlparser_status_t sqlparser_handle_clone(
	const sqlparser_handle_t *source,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *clone;
	sqlparser_status_t status;

	if (out_handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_handle = NULL;
	if (source == NULL || source->sql == NULL || source->parser_sql == NULL || source->parse_tree.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	clone = (sqlparser_handle_t *)calloc(1U, sizeof(*clone));
	if (clone == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	clone->dialect = source->dialect;
	clone->dialect_ops = source->dialect_ops;

	clone->sql = sqlparser_strdup(source->sql);
	if (clone->sql == NULL) {
		sqlparser_handle_destroy(clone);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	clone->parser_sql = sqlparser_strdup(source->parser_sql);
	if (clone->parser_sql == NULL) {
		sqlparser_handle_destroy(clone);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	if (source->dialect_state != NULL) {
		if (source->dialect_ops == NULL || source->dialect_ops->clone_state == NULL) {
			sqlparser_handle_destroy(clone);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"dialect state cannot be cloned");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = source->dialect_ops->clone_state(source->dialect_state, &clone->dialect_state, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_handle_destroy(clone);
			return status;
		}
	}

	status = sqlparser_protobuf_copy(&clone->parse_tree, &source->parse_tree, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(clone);
		return status;
	}

	clone->sql_len = source->sql_len;
	clone->parser_sql_len = source->parser_sql_len;
	clone->statement_count = source->statement_count;
	clone->limits = source->limits;
	clone->generation = source->generation;
	*out_handle = clone;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_handle_replace_contents(
	sqlparser_handle_t *target,
	sqlparser_handle_t *source)
{
	if (target == NULL || source == NULL) {
		return;
	}

	sqlparser_handle_release_contents(target);
	*target = *source;
	memset(source, 0, sizeof(*source));
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

	if (sqlparser_validate_statement_count_limit(&handle->limits, handle->ast->n_stmts, out_error) !=
	    SQLPARSER_STATUS_OK) {
		free(packed);
		sqlparser_handle_clear_ast(handle);
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	free(handle->parse_tree.data);
	handle->parse_tree.data = packed;
	handle->parse_tree.len = packed_len;
	handle->statement_count = handle->ast->n_stmts;
	handle->generation++;
	sqlparser_handle_invalidate_derived(handle);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_ensure_current_sql_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	PgQueryDeparseResult deparse_result;
	sqlparser_handle_t *mutable_handle;
	sqlparser_status_t status;
	char *public_sql;

	if (handle == NULL || handle->sql == NULL || handle->parser_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (handle->generation == 0UL) {
		return SQLPARSER_STATUS_OK;
	}
	if (handle->current_sql != NULL && handle->current_parser_sql != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	free(mutable_handle->current_sql);
	mutable_handle->current_sql = NULL;
	free(mutable_handle->current_parser_sql);
	mutable_handle->current_parser_sql = NULL;

	sqlparser_pg_query_prepare();
	deparse_result = pg_query_deparse_protobuf(handle->parse_tree);
	if (deparse_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			handle->parser_sql,
			deparse_result.error);
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	status = sqlparser_validate_handle_output_text(handle, deparse_result.query, "current SQL", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_deparse_result(deparse_result);
		return status;
	}

	mutable_handle->current_parser_sql = sqlparser_strdup(deparse_result.query);
	if (mutable_handle->current_parser_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	public_sql = NULL;
	if (handle->dialect_ops != NULL && handle->dialect_ops->postprocess_deparse != NULL) {
		status = handle->dialect_ops->postprocess_deparse(
			deparse_result.query,
			handle->dialect_state,
			&public_sql,
			out_error);
	} else {
		public_sql = sqlparser_strdup(deparse_result.query);
		status = public_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
	}
	pg_query_free_deparse_result(deparse_result);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_validate_handle_output_text(handle, public_sql, "deparse output", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	mutable_handle->current_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

const char *sqlparser_effective_sql(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return NULL;
	}

	if (handle->generation == 0UL || handle->current_sql == NULL) {
		return handle->sql;
	}

	return handle->current_sql;
}

const char *sqlparser_effective_parser_sql(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return NULL;
	}

	if (handle->generation == 0UL || handle->current_parser_sql == NULL) {
		return handle->parser_sql;
	}

	return handle->current_parser_sql;
}

sqlparser_status_t sqlparser_postprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	const char *core_sql,
	const char *field_name,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	char *public_sql;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;

	if (handle == NULL || core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle and core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	public_sql = NULL;
	if (handle->dialect_ops != NULL && handle->dialect_ops->postprocess_deparse != NULL) {
		status = handle->dialect_ops->postprocess_deparse(
			core_sql,
			handle->dialect_state,
			&public_sql,
			out_error);
	} else {
		public_sql = sqlparser_strdup(core_sql);
		status = public_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_validate_handle_output_text(
		handle,
		public_sql,
		field_name != NULL ? field_name : "SQL fragment",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}

	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_handle_discard_dialect_state(
	const sqlparser_handle_t *handle,
	void *state)
{
	if (state == NULL || handle == NULL || state == handle->dialect_state) {
		return;
	}
	if (handle->dialect_ops != NULL && handle->dialect_ops->destroy_state != NULL) {
		handle->dialect_ops->destroy_state(state);
	}
}

void sqlparser_handle_adopt_dialect_state(
	sqlparser_handle_t *handle,
	void *state)
{
	if (handle == NULL || state == NULL || state == handle->dialect_state) {
		return;
	}

	if (handle->dialect_ops != NULL && handle->dialect_ops->destroy_state != NULL) {
		handle->dialect_ops->destroy_state(handle->dialect_state);
	}
	handle->dialect_state = state;
}

sqlparser_status_t sqlparser_preprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	const char *public_sql,
	const char *field_name,
	char **out_parser_sql,
	void **out_dialect_state,
	sqlparser_error_t *out_error)
{
	void *candidate_state;
	sqlparser_status_t status;

	if (out_parser_sql == NULL || out_dialect_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_dialect_state = NULL;

	status = sqlparser_validate_handle_sql_input(
		handle,
		public_sql,
		field_name != NULL ? field_name : "SQL fragment",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (handle->dialect_ops == NULL || handle->dialect_ops->preprocess_fragment == NULL) {
		*out_parser_sql = sqlparser_strdup(public_sql);
		if (*out_parser_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	candidate_state = NULL;
	if (handle->dialect_ops->clone_state != NULL) {
		status = handle->dialect_ops->clone_state(handle->dialect_state, &candidate_state, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	status = handle->dialect_ops->preprocess_fragment(
		public_sql,
		candidate_state,
		out_parser_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	if (*out_parser_sql == NULL) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"dialect fragment output is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	status = sqlparser_validate_text_limit(
		*out_parser_sql,
		handle->limits.max_sql_bytes,
		"parser SQL fragment",
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_parser_sql);
		*out_parser_sql = NULL;
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}

	*out_dialect_state = candidate_state;
	return SQLPARSER_STATUS_OK;
}

const char *sqlparser_version_string(void)
{
	return SQLPARSER_VERSION_TEXT;
}

const char *sqlparser_libpg_query_tag(void)
{
	return SQLPARSER_LIBPG_QUERY_TAG_TEXT;
}

const char *sqlparser_model_schema_string(void)
{
	return SQLPARSER_MODEL_SCHEMA_TEXT;
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

const char *sqlparser_dialect_name(sqlparser_dialect_t dialect)
{
	const sqlparser_dialect_ops_t *ops;

	ops = sqlparser_dialect_get_ops(dialect);
	if (ops != NULL && ops->name != NULL) {
		return ops->name;
	}

	switch (dialect) {
		case SQLPARSER_DIALECT_ORACLE:
			return "oracle";
		case SQLPARSER_DIALECT_SQLSERVER:
			return "sqlserver";
		case SQLPARSER_DIALECT_MYSQL:
			return "mysql";
		case SQLPARSER_DIALECT_POSTGRESQL:
			return "postgresql";
		default:
			return "unknown";
	}
}

sqlparser_status_t sqlparser_parse(
	const char *sql,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	return sqlparser_parse_with_options(sql, NULL, out_handle, out_error);
}

sqlparser_status_t sqlparser_parse_with_limits(
	const char *sql,
	const sqlparser_limits_t *limits,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	sqlparser_parse_options_t options;

	sqlparser_parse_options_set_defaults(&options);
	sqlparser_limits_normalize(limits, &options.limits);
	return sqlparser_parse_with_options(sql, &options, out_handle, out_error);
}

sqlparser_status_t sqlparser_parse_with_options(
	const char *sql,
	const sqlparser_parse_options_t *options,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	PgQueryProtobufParseResult parse_result;
	sqlparser_handle_t *handle;
	sqlparser_status_t status;
	sqlparser_parse_options_t effective_options;
	const sqlparser_dialect_ops_t *dialect_ops;
	size_t sql_len;
	size_t parser_sql_len;
	char *parser_sql;
	void *dialect_state;

	if (out_handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_parse_options_normalize(options, &effective_options);
	*out_handle = NULL;
	sqlparser_error_clear(out_error);
	parser_sql = NULL;
	dialect_state = NULL;

	if (sql == NULL || sql[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"sql must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_validate_text_limit(
		sql,
		effective_options.limits.max_sql_bytes,
		"SQL input",
		&sql_len,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	dialect_ops = sqlparser_dialect_get_ops(effective_options.dialect);
	if (dialect_ops == NULL || dialect_ops->preprocess == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"SQL dialect is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = dialect_ops->preprocess(sql, &effective_options.limits, &parser_sql, &dialect_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (dialect_ops->destroy_state != NULL && dialect_state != NULL) {
			dialect_ops->destroy_state(dialect_state);
		}
		free(parser_sql);
		return status;
	}
	status = sqlparser_validate_text_limit(
		parser_sql,
		effective_options.limits.max_sql_bytes,
		"parser SQL input",
		&parser_sql_len,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (dialect_ops->destroy_state != NULL && dialect_state != NULL) {
			dialect_ops->destroy_state(dialect_state);
		}
		free(parser_sql);
		return status;
	}

	sqlparser_pg_query_prepare();
	parse_result = pg_query_parse_protobuf(parser_sql);
	if (parse_result.error != NULL) {
		sqlparser_error_from_pg(out_error, SQLPARSER_STATUS_PARSE_ERROR, parser_sql, parse_result.error);
		pg_query_free_protobuf_parse_result(parse_result);
		if (dialect_ops->destroy_state != NULL && dialect_state != NULL) {
			dialect_ops->destroy_state(dialect_state);
		}
		free(parser_sql);
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	handle = (sqlparser_handle_t *)calloc(1U, sizeof(*handle));
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_protobuf_parse_result(parse_result);
		if (dialect_ops->destroy_state != NULL && dialect_state != NULL) {
			dialect_ops->destroy_state(dialect_state);
		}
		free(parser_sql);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	handle->sql = sqlparser_strdup(sql);
	if (handle->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		pg_query_free_protobuf_parse_result(parse_result);
		if (dialect_ops->destroy_state != NULL && dialect_state != NULL) {
			dialect_ops->destroy_state(dialect_state);
		}
		free(parser_sql);
		free(handle);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	handle->parser_sql = parser_sql;
	parser_sql = NULL;
	handle->sql_len = sql_len;
	handle->parser_sql_len = parser_sql_len;
	handle->limits = effective_options.limits;
	handle->dialect = effective_options.dialect;
	handle->dialect_ops = dialect_ops;
	handle->dialect_state = dialect_state;
	dialect_state = NULL;
	status = sqlparser_protobuf_copy(&handle->parse_tree, &parse_result.parse_tree, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_protobuf_parse_result(parse_result);
		sqlparser_handle_destroy(handle);
		return status;
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
		pg_query_free_protobuf_parse_result(parse_result);
		sqlparser_handle_destroy(handle);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	handle->statement_count = handle->ast->n_stmts;
	status = sqlparser_validate_statement_count_limit(&handle->limits, handle->statement_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_protobuf_parse_result(parse_result);
		sqlparser_handle_destroy(handle);
		return status;
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

	sqlparser_handle_release_contents(handle);
	free(handle);
}

const char *sqlparser_original_sql(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return NULL;
	}

	return handle->sql;
}

sqlparser_dialect_t sqlparser_handle_dialect(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return SQLPARSER_DIALECT_POSTGRESQL;
	}

	return handle->dialect;
}

size_t sqlparser_statement_count(const sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return 0U;
	}

	return handle->statement_count;
}


sqlparser_status_t sqlparser_deparse(
	const sqlparser_handle_t *handle,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQueryDeparseResult deparse_result;
	char *public_sql;
	sqlparser_status_t status;

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
			handle->parser_sql != NULL ? handle->parser_sql : handle->sql,
			deparse_result.error);
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	status = sqlparser_validate_handle_output_text(handle, deparse_result.query, "deparse output", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_deparse_result(deparse_result);
		return status;
	}

	public_sql = NULL;
	if (handle->dialect_ops != NULL && handle->dialect_ops->postprocess_deparse != NULL) {
		status = handle->dialect_ops->postprocess_deparse(
			deparse_result.query,
			handle->dialect_state,
			&public_sql,
			out_error);
	} else {
		public_sql = sqlparser_strdup(deparse_result.query);
		status = public_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_deparse_result(deparse_result);
		return status;
	}
	status = sqlparser_validate_handle_output_text(handle, public_sql, "deparse output", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		pg_query_free_deparse_result(deparse_result);
		return status;
	}

	*out_sql = public_sql;
	pg_query_free_deparse_result(deparse_result);
	return SQLPARSER_STATUS_OK;
}

void sqlparser_string_free(char *text)
{
	free(text);
}
