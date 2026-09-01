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

#include "protobuf/pg_query.pb-c.h"
#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_ast_internal.h"
#include "sqlparser_bind_occurrence_internal.h"
#include "sqlparser_control_internal.h"
#include "sqlparser_internal.h"

#ifndef SQLPARSER_VERSION_TEXT
#define SQLPARSER_VERSION_TEXT "2.16.0"
#endif

#ifndef SQLPARSER_LIBPG_QUERY_TAG_TEXT
#define SQLPARSER_LIBPG_QUERY_TAG_TEXT "17-6.2.2"
#endif

#ifndef SQLPARSER_DEFAULT_MAX_SQL_BYTES
#define SQLPARSER_DEFAULT_MAX_SQL_BYTES (4U * 1024U * 1024U)
#endif

#ifndef SQLPARSER_DEFAULT_MAX_OUTPUT_BYTES
#define SQLPARSER_DEFAULT_MAX_OUTPUT_BYTES (4U * 1024U * 1024U)
#endif

#ifndef SQLPARSER_DEFAULT_MAX_STATEMENT_COUNT
#define SQLPARSER_DEFAULT_MAX_STATEMENT_COUNT 64U
#endif

#if defined(_MSC_VER) && !defined(__thread)
#define __thread __declspec(thread)
#endif

extern __thread sig_atomic_t pg_query_initialized;

static sqlparser_status_t sqlparser_validate_dialect_statements(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);

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

void sqlparser_handle_remove_identifier_mutation(
	sqlparser_handle_t *handle,
	size_t mutation_index)
{
	if (handle == NULL ||
	    mutation_index >= handle->identifier_mutation_count) {
		return;
	}
	free(handle->identifier_mutations[mutation_index].original);
	free(handle->identifier_mutations[mutation_index].spelling);
	if (mutation_index + 1U < handle->identifier_mutation_count) {
		memmove(
			&handle->identifier_mutations[mutation_index],
			&handle->identifier_mutations[mutation_index + 1U],
			(handle->identifier_mutation_count -
			 mutation_index - 1U) *
				sizeof(*handle->identifier_mutations));
	}
	handle->identifier_mutation_count--;
}

int sqlparser_proto_location_is_identifier_spelling(int32_t location)
{
	return location >= SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_BASE &&
		location <= SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_LAST;
}

static void sqlparser_mark_identifier_spelling_groups(
	const ProtobufCMessage *message,
	unsigned char *used,
	size_t group_count)
{
	const ProtobufCMessageDescriptor *descriptor;
	const uint8_t *base;
	unsigned index;

	if (message == NULL || message->descriptor == NULL) {
		return;
	}
	descriptor = message->descriptor;
	base = (const uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) !=
			    (int)field->id) {
			continue;
		}
		if (field->type == PROTOBUF_C_TYPE_INT32 &&
		    field->label != PROTOBUF_C_LABEL_REPEATED &&
		    strcmp(field->name, "location") == 0) {
			int32_t location;

			location = *(const int32_t *)(base + field->offset);
			if (sqlparser_proto_location_is_identifier_spelling(
				    location)) {
				uint32_t payload;
				size_t group_index;

				payload = (uint32_t)(
					(int64_t)location -
					(int64_t)
						SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_BASE);
				group_index =
					payload >>
					SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENT_BITS;
				if (group_index < group_count) {
					used[group_index >> 3U] |=
						(unsigned char)(
							1U <<
							(group_index & 7U));
				}
			}
			continue;
		}
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}
		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage *const *items;
			size_t item_index;

			item_count =
				*(const size_t *)(base + field->quantifier_offset);
			items =
				*(ProtobufCMessage *const *const *)
					(base + field->offset);
			for (item_index = 0U;
			     items != NULL && item_index < item_count;
			     item_index++) {
				sqlparser_mark_identifier_spelling_groups(
					items[item_index],
					used,
					group_count);
			}
		} else {
			sqlparser_mark_identifier_spelling_groups(
				*(ProtobufCMessage *const *)
					(base + field->offset),
				used,
				group_count);
		}
	}
}

static void sqlparser_handle_sweep_identifier_spellings_for_message(
	sqlparser_handle_t *handle,
	const ProtobufCMessage *message)
{
	sqlparser_identifier_spelling_t *next;
	unsigned char *used;
	size_t first_free;
	size_t group_index;
	size_t used_size;

	if (handle == NULL || message == NULL ||
	    handle->identifier_spelling_count == 0U ||
	    handle->identifier_spelling_build_group != SIZE_MAX) {
		return;
	}
	used_size =
		(handle->identifier_spelling_count + 7U) / 8U;
	used = (unsigned char *)calloc(used_size, 1U);
	if (used == NULL) {
		return;
	}
	sqlparser_mark_identifier_spelling_groups(
		message,
		used,
		handle->identifier_spelling_count);
	first_free = handle->identifier_spelling_count;
	for (group_index = 0U;
	     group_index < handle->identifier_spelling_count;
	     group_index++) {
		size_t part_index;

		if ((used[group_index >> 3U] &
		     (unsigned char)(1U << (group_index & 7U))) != 0U) {
			continue;
		}
		for (part_index = 0U;
		     part_index <
			     handle->identifier_spellings[group_index].part_count;
		     part_index++) {
			free(handle->identifier_spellings[group_index]
				     .parts[part_index]);
		}
		memset(
			&handle->identifier_spellings[group_index],
			0,
			sizeof(*handle->identifier_spellings));
		if (first_free == handle->identifier_spelling_count) {
			first_free = group_index;
		}
	}
	free(used);
	while (handle->identifier_spelling_count > 0U &&
	       handle->identifier_spellings
			       [handle->identifier_spelling_count - 1U]
				       .part_count == 0U) {
		handle->identifier_spelling_count--;
	}
	if (handle->identifier_spelling_count == 0U) {
		free(handle->identifier_spellings);
		handle->identifier_spellings = NULL;
		handle->identifier_spelling_capacity = 0U;
		handle->identifier_spelling_free_group = 0U;
		return;
	}
	if (handle->identifier_spelling_capacity >
	    handle->identifier_spelling_count * 2U) {
		next = (sqlparser_identifier_spelling_t *)realloc(
			handle->identifier_spellings,
			handle->identifier_spelling_count * sizeof(*next));
		if (next != NULL) {
			handle->identifier_spellings = next;
			handle->identifier_spelling_capacity =
				handle->identifier_spelling_count;
		}
	}
	handle->identifier_spelling_free_group =
		first_free < handle->identifier_spelling_count ?
			first_free :
			handle->identifier_spelling_count;
}

void sqlparser_handle_sweep_identifier_spellings(
	sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}
	sqlparser_handle_sweep_identifier_spellings_for_message(
		handle,
		(const ProtobufCMessage *)handle->ast);
}

void sqlparser_handle_begin_identifier_spelling(
	sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}
	handle->identifier_spelling_build_group = SIZE_MAX;
	handle->identifier_spelling_last_location =
		SQLPARSER_PROTO_LOCATION_GENERATED;
}

int32_t sqlparser_handle_append_identifier_spelling(
	sqlparser_handle_t *handle,
	const char *spelling,
	size_t spelling_length)
{
	sqlparser_identifier_spelling_t *group;
	sqlparser_identifier_spelling_t *next;
	char *copy;
	size_t capacity;
	size_t component_index;
	size_t group_index;
	uint32_t payload;

	if (handle == NULL || spelling == NULL ||
	    handle->identifier_spelling_status != SQLPARSER_STATUS_OK) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (handle->identifier_spelling_build_group == SIZE_MAX) {
		group_index =
			handle->identifier_spelling_free_group <=
					handle->identifier_spelling_count ?
				handle->identifier_spelling_free_group :
				0U;
		while (group_index < handle->identifier_spelling_count &&
		       handle->identifier_spellings[group_index].part_count !=
			       0U) {
			group_index++;
		}
		if (group_index < handle->identifier_spelling_count) {
			handle->identifier_spelling_build_group = group_index;
			handle->identifier_spelling_free_group =
				group_index + 1U;
		} else {
			if (handle->identifier_spelling_count >
			    SQLPARSER_PROTO_IDENTIFIER_SPELLING_GROUP_MAX) {
				handle->identifier_spelling_status =
					SQLPARSER_STATUS_RESOURCE_LIMIT;
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			if (handle->identifier_spelling_count ==
			    handle->identifier_spelling_capacity) {
				capacity =
					handle->identifier_spelling_capacity == 0U ?
						8U :
						handle->identifier_spelling_capacity * 2U;
				if (capacity <
					    handle->identifier_spelling_capacity ||
				    capacity >
					    SQLPARSER_PROTO_IDENTIFIER_SPELLING_GROUP_MAX +
						    1U ||
				    capacity >
					    SIZE_MAX /
						    sizeof(*handle->identifier_spellings)) {
					handle->identifier_spelling_status =
						SQLPARSER_STATUS_NO_MEMORY;
					return SQLPARSER_PROTO_LOCATION_GENERATED;
				}
				next = (sqlparser_identifier_spelling_t *)realloc(
					handle->identifier_spellings,
					capacity * sizeof(*next));
				if (next == NULL) {
					handle->identifier_spelling_status =
						SQLPARSER_STATUS_NO_MEMORY;
					return SQLPARSER_PROTO_LOCATION_GENERATED;
				}
				handle->identifier_spellings = next;
				handle->identifier_spelling_capacity =
					capacity;
			}
			handle->identifier_spelling_build_group =
				handle->identifier_spelling_count++;
			handle->identifier_spelling_free_group =
				handle->identifier_spelling_count;
		}
		memset(
			&handle->identifier_spellings
				[handle->identifier_spelling_build_group],
			0,
			sizeof(*handle->identifier_spellings));
	}
	group = &handle->identifier_spellings
		[handle->identifier_spelling_build_group];
	if (group->part_count >=
	    SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENTS) {
		handle->identifier_spelling_status =
			SQLPARSER_STATUS_RESOURCE_LIMIT;
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	copy = sqlparser_strndup(spelling, spelling_length);
	if (copy == NULL) {
		handle->identifier_spelling_status = SQLPARSER_STATUS_NO_MEMORY;
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	component_index = group->part_count++;
	group->parts[component_index] = copy;
	payload =
		(uint32_t)(
			handle->identifier_spelling_build_group <<
			SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENT_BITS) |
		(uint32_t)component_index;
	handle->identifier_spelling_last_location = (int32_t)(
		(int64_t)SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_BASE +
		(int64_t)payload);
	return handle->identifier_spelling_last_location;
}

sqlparser_status_t sqlparser_handle_finish_identifier_spelling(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (handle == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = handle->identifier_spelling_status;
	handle->identifier_spelling_status = SQLPARSER_STATUS_OK;
	handle->identifier_spelling_build_group = SIZE_MAX;
	handle->identifier_spelling_last_location =
		SQLPARSER_PROTO_LOCATION_GENERATED;
	if (status == SQLPARSER_STATUS_NO_MEMORY) {
		sqlparser_error_set_message(
			out_error,
			status,
			"out of memory");
	} else if (status != SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(
			out_error,
			status,
			"generated identifier spelling is too large");
	}
	return status;
}

int sqlparser_handle_identifier_spelling(
	const sqlparser_handle_t *handle,
	int32_t location,
	size_t component_index,
	const char **out_spelling,
	size_t *out_length)
{
	const sqlparser_identifier_spelling_t *group;
	uint32_t payload;
	size_t base_component;
	size_t group_index;

	if (out_spelling != NULL) {
		*out_spelling = NULL;
	}
	if (out_length != NULL) {
		*out_length = 0U;
	}
	if (handle == NULL ||
	    !sqlparser_proto_location_is_identifier_spelling(location)) {
		return 0;
	}
	payload = (uint32_t)(
		(int64_t)location -
		(int64_t)SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_BASE);
	base_component =
		payload &
		SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENT_MASK;
	group_index =
		payload >>
		SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENT_BITS;
	if (group_index >= handle->identifier_spelling_count ||
	    component_index >
		    SIZE_MAX - base_component) {
		return 0;
	}
	component_index += base_component;
	group = &handle->identifier_spellings[group_index];
	if (component_index >= group->part_count ||
	    group->parts[component_index] == NULL) {
		return 0;
	}
	if (out_spelling != NULL) {
		*out_spelling = group->parts[component_index];
	}
	if (out_length != NULL) {
		*out_length = strlen(group->parts[component_index]);
	}
	return 1;
}

sqlparser_status_t sqlparser_handle_rebind_identifier_mutations(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	size_t index;

	(void)out_error;
	if (handle == NULL || handle->ast == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	index = 0U;
	while (index < handle->identifier_mutation_count) {
		sqlparser_identifier_mutation_t *mutation;
		sqlparser_string_search_t search;
		char **slot;

		mutation = &handle->identifier_mutations[index];
		if (mutation->raw_statement_index >= handle->ast->n_stmts ||
		    handle->ast->stmts == NULL ||
		    handle->ast->stmts[mutation->raw_statement_index] == NULL ||
		    handle->ast->stmts[mutation->raw_statement_index]->stmt ==
			    NULL) {
			sqlparser_handle_remove_identifier_mutation(handle, index);
			continue;
		}
		memset(&search, 0, sizeof(search));
		if (mutation->slot != NULL) {
			search.match_slot = mutation->slot;
			search.match_value = mutation->value;
		} else if (mutation->value != NULL) {
			search.match_value = mutation->value;
		} else {
			search.want_target = 1;
			search.target_index = mutation->string_index;
		}
		(void)sqlparser_walk_message_strings(
			(ProtobufCMessage *)
				handle->ast->stmts
					[mutation->raw_statement_index]
					->stmt,
			&search);
		slot = search.target_slot;
		if (slot == NULL && mutation->slot != NULL &&
		    mutation->value != NULL) {
			memset(&search, 0, sizeof(search));
			search.match_value = mutation->value;
			(void)sqlparser_walk_message_strings(
				(ProtobufCMessage *)
					handle->ast->stmts
						[mutation->raw_statement_index]
						->stmt,
				&search);
			slot = search.target_slot;
		}
		if (slot == NULL) {
			sqlparser_handle_remove_identifier_mutation(handle, index);
			continue;
		}
		mutation->string_index = search.target_index;
		mutation->slot = slot;
		mutation->value = *slot;
		index++;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_handle_ensure_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

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

	status = sqlparser_handle_rebind_identifier_mutations(
		handle,
		out_error);
	if (status == SQLPARSER_STATUS_OK &&
	    handle->dialect_ops != NULL &&
	    handle->dialect_ops->bind_ast_state != NULL) {
		status = handle->dialect_ops->bind_ast_state(
			handle->dialect_state,
			handle->ast,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_clear_ast(handle);
	}
	return status;
}

void sqlparser_handle_clear_ast(sqlparser_handle_t *handle)
{
	size_t index;

	if (handle == NULL) {
		return;
	}
	for (index = 0U;
	     index < handle->identifier_mutation_count;
	     index++) {
		handle->identifier_mutations[index].slot = NULL;
		handle->identifier_mutations[index].value = NULL;
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->bind_ast_state != NULL) {
		(void)handle->dialect_ops->bind_ast_state(
			handle->dialect_state,
			NULL,
			NULL);
	}
	if (handle->ast == NULL) {
		return;
	}

	pg_query__parse_result__free_unpacked(handle->ast, NULL);
	handle->ast = NULL;
}

static void sqlparser_handle_discard_ast_changes(
	sqlparser_handle_t *handle)
{
	PgQuery__ParseResult *persisted_ast;

	if (handle == NULL) {
		return;
	}
	sqlparser_handle_clear_ast(handle);
	if (handle->identifier_spelling_count == 0U ||
	    handle->parse_tree.data == NULL) {
		return;
	}
	persisted_ast = pg_query__parse_result__unpack(
		NULL,
		handle->parse_tree.len,
		(const uint8_t *)handle->parse_tree.data);
	if (persisted_ast == NULL) {
		return;
	}
	sqlparser_handle_sweep_identifier_spellings_for_message(
		handle,
		(const ProtobufCMessage *)persisted_ast);
	pg_query__parse_result__free_unpacked(persisted_ast, NULL);
}

static void sqlparser_handle_clear_current_sql(sqlparser_handle_t *handle)
{
	char *current_sql;
	char *current_parser_sql;

	if (handle == NULL) {
		return;
	}

	current_sql = handle->current_sql;
	current_parser_sql = handle->current_parser_sql;
	free(current_sql);
	handle->current_sql = NULL;

	if (current_parser_sql != current_sql) {
		free(current_parser_sql);
	}
	handle->current_parser_sql = NULL;
}

static void sqlparser_handle_clear_bind_occurrences(
	sqlparser_handle_t *handle);

void sqlparser_handle_invalidate_derived(sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}

	sqlparser_handle_clear_current_sql(handle);
	sqlparser_handle_clear_query_graph(handle);
	sqlparser_handle_clear_bind_occurrences(handle);
}

void sqlparser_handle_clear_query_graph(sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}
	sqlparser_query_graph_cache_release(handle->query_graph);
	handle->query_graph = NULL;
	handle->query_graph_generation = 0UL;
}

static void sqlparser_handle_clear_bind_occurrences(
	sqlparser_handle_t *handle)
{
	if (handle == NULL) {
		return;
	}
	free(handle->bind_occurrences);
	handle->bind_occurrences = NULL;
	handle->bind_occurrences_generation = 0UL;
}

sqlparser_status_t sqlparser_handle_ensure_bind_occurrences(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	sqlparser_bind_occurrence_cache_t *cache;
	sqlparser_bind_scanner_t scanner;
	sqlparser_bind_role_cursor_t role_cursor;
	sqlparser_bind_token_t token;
	sqlparser_handle_t *mutable_handle;
	const char *sql;
	size_t allocation_size;
	size_t count;
	size_t index;
	size_t items_size;
	size_t sql_length;
	size_t text_cursor;
	size_t text_size;
	char *text;
	sqlparser_status_t status;

	if (handle == NULL || handle->sql == NULL ||
	    handle->parse_tree.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (handle->bind_occurrences != NULL &&
	    handle->bind_occurrences_generation == handle->generation) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_ensure_current_sql_text(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	sql = sqlparser_effective_sql(handle);
	if (sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"current SQL is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	sql_length = strlen(sql);
	count = 0U;
	text_size = 0U;
	sqlparser_bind_scanner_init(&scanner, handle->dialect, sql);
	sqlparser_bind_role_cursor_init(&role_cursor, handle, sql);
	while (sqlparser_bind_scanner_next(&scanner, &token)) {
		size_t token_length;

		if (!sqlparser_bind_role_cursor_accept(&role_cursor, &token)) {
			continue;
		}
		if (token.start >= token.end || token.end > sql_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"bind scanner returned an invalid token span");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		token_length = token.end - token.start;
		if (count == SIZE_MAX || token_length == SIZE_MAX ||
		    text_size > SIZE_MAX - token_length - 1U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"bind occurrence cache is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		count++;
		text_size += token_length + 1U;
	}
	if (count > (SIZE_MAX - sizeof(*cache)) / sizeof(*cache->items)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"bind occurrence cache is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	items_size = count * sizeof(*cache->items);
	if (text_size > SIZE_MAX - sizeof(*cache) - items_size) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"bind occurrence cache is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	allocation_size = sizeof(*cache) + items_size + text_size;
	cache = (sqlparser_bind_occurrence_cache_t *)malloc(allocation_size);
	if (cache == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	cache->count = count;
	text = (char *)cache + sizeof(*cache) + items_size;
	index = 0U;
	text_cursor = 0U;
	sqlparser_bind_scanner_init(&scanner, handle->dialect, sql);
	sqlparser_bind_role_cursor_init(&role_cursor, handle, sql);
	while (sqlparser_bind_scanner_next(&scanner, &token)) {
		size_t token_length;

		if (!sqlparser_bind_role_cursor_accept(&role_cursor, &token)) {
			continue;
		}
		if (index >= count || token.start >= token.end ||
		    token.end > sql_length) {
			free(cache);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"bind scanner result changed while building cache");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		token_length = token.end - token.start;
		cache->items[index].source_start = token.start;
		cache->items[index].text_offset = text_cursor;
		cache->items[index].kind = token.kind;
		memcpy(text + text_cursor, sql + token.start, token_length);
		text[text_cursor + token_length] = '\0';
		text_cursor += token_length + 1U;
		index++;
	}
	if (index != count || text_cursor != text_size) {
		free(cache);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bind scanner result changed while building cache");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	sqlparser_handle_clear_bind_occurrences(mutable_handle);
	mutable_handle->bind_occurrences = cache;
	mutable_handle->bind_occurrences_generation = handle->generation;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_handle_bind_occurrences(
	const sqlparser_handle_t *handle,
	sqlparser_bind_occurrence_view_t *out_occurrences,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (out_occurrences == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_occurrences must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_occurrences, 0, sizeof(*out_occurrences));
	if (handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_handle_ensure_bind_occurrences(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (handle->bind_occurrences == NULL ||
	    handle->bind_occurrences_generation != handle->generation) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"bind occurrence cache is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	out_occurrences->handle = handle;
	out_occurrences->generation = handle->generation;
	out_occurrences->count = handle->bind_occurrences->count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_bind_occurrence_at(
	const sqlparser_bind_occurrence_view_t *occurrences,
	size_t occurrence_index,
	sqlparser_bind_occurrence_t *out_occurrence,
	sqlparser_error_t *out_error)
{
	const sqlparser_bind_occurrence_cache_t *cache;
	const sqlparser_bind_occurrence_cache_item_t *cache_item;
	const char *text;
	size_t items_size;

	sqlparser_error_clear(out_error);
	if (out_occurrence == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_occurrence must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_occurrence, 0, sizeof(*out_occurrence));
	if (occurrences == NULL || occurrences->handle == NULL ||
	    occurrences->generation != occurrences->handle->generation) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind occurrence view is invalid or stale");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	cache = occurrences->handle->bind_occurrences;
	if (cache == NULL ||
	    occurrences->handle->bind_occurrences_generation !=
		    occurrences->generation ||
	    occurrences->count != cache->count ||
	    occurrence_index >= occurrences->count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind occurrence index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	cache_item = &cache->items[occurrence_index];
	items_size = cache->count * sizeof(*cache->items);
	text = (const char *)cache + sizeof(*cache) + items_size +
		cache_item->text_offset;
	out_occurrence->position = occurrence_index + 1U;
	out_occurrence->kind = cache_item->kind;
	out_occurrence->sql = text;
	out_occurrence->key = text[0] == '?' ? NULL : text + 1U;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_handle_release_contents(sqlparser_handle_t *handle)
{
	char *sql;
	char *parser_sql;
	size_t mutation_index;

	if (handle == NULL) {
		return;
	}

	sqlparser_handle_clear_ast(handle);
	sqlparser_handle_invalidate_derived(handle);
	sqlparser_surface_source_edits_release(
		&handle->surface_source_edits);
	handle->surface_source_complete = 0;
	sqlparser_protobuf_release(&handle->parse_tree);
	if (handle->control != NULL) {
		sqlparser_control_state_release(handle->control);
		handle->control = NULL;
	}
	if (handle->dialect_ops != NULL && handle->dialect_ops->destroy_state != NULL && handle->dialect_state != NULL) {
		handle->dialect_ops->destroy_state(handle->dialect_state);
	}
	handle->dialect_state = NULL;
	for (mutation_index = 0U;
	     mutation_index < handle->identifier_mutation_count;
	     mutation_index++) {
		free(handle->identifier_mutations[mutation_index].original);
		free(handle->identifier_mutations[mutation_index].spelling);
	}
	free(handle->identifier_mutations);
	handle->identifier_mutations = NULL;
	handle->identifier_mutation_count = 0U;
	handle->identifier_mutation_capacity = 0U;
	for (mutation_index = 0U;
	     mutation_index < handle->identifier_spelling_count;
	     mutation_index++) {
		size_t part_index;

		for (part_index = 0U;
		     part_index < handle->identifier_spellings[mutation_index].part_count;
		     part_index++) {
			free(handle->identifier_spellings[mutation_index]
				     .parts[part_index]);
		}
	}
	free(handle->identifier_spellings);
	handle->identifier_spellings = NULL;
	handle->identifier_spelling_count = 0U;
	handle->identifier_spelling_capacity = 0U;
	handle->identifier_spelling_free_group = 0U;
	handle->identifier_spelling_build_group = SIZE_MAX;
	handle->identifier_spelling_last_location =
		SQLPARSER_PROTO_LOCATION_GENERATED;
	handle->identifier_spelling_status = SQLPARSER_STATUS_OK;
	sqlparser_identifier_origin_map_destroy(handle->identifier_origins);
	handle->identifier_origins = NULL;
	sql = handle->sql;
	parser_sql = handle->parser_sql;
	free(sql);
	if (parser_sql != sql) {
		free(parser_sql);
	}
	handle->sql = NULL;
	handle->parser_sql = NULL;
	handle->sql_len = 0U;
	handle->parser_sql_len = 0U;
	handle->statement_count = 0U;
	handle->generation = 0UL;
	handle->query_graph_generation = 0UL;
	handle->bind_occurrences_generation = 0UL;
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
	clone->limits = source->limits;
	clone->generation = source->generation;
	clone->statement_count = source->statement_count;
	clone->surface_source_complete = source->surface_source_complete;

	clone->sql = sqlparser_strdup(source->sql);
	if (clone->sql == NULL) {
		sqlparser_handle_destroy(clone);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	if (source->sql == source->parser_sql || strcmp(source->sql, source->parser_sql) == 0) {
		clone->parser_sql = clone->sql;
	} else {
		clone->parser_sql = sqlparser_strdup(source->parser_sql);
		if (clone->parser_sql == NULL) {
			sqlparser_handle_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
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
	status = sqlparser_surface_source_edits_clone(
		&source->surface_source_edits,
		&clone->surface_source_edits,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(clone);
		return status;
	}
	if (source->identifier_mutation_count > 0U) {
		size_t mutation_index;

		if (source->identifier_mutation_count >
		    SIZE_MAX / sizeof(*clone->identifier_mutations)) {
			sqlparser_handle_destroy(clone);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->identifier_mutations =
			(sqlparser_identifier_mutation_t *)calloc(
				source->identifier_mutation_count,
				sizeof(*clone->identifier_mutations));
		if (clone->identifier_mutations == NULL) {
			sqlparser_handle_destroy(clone);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->identifier_mutation_capacity =
			source->identifier_mutation_count;
		for (mutation_index = 0U;
		     mutation_index < source->identifier_mutation_count;
		     mutation_index++) {
			clone->identifier_mutations[mutation_index] =
				source->identifier_mutations[mutation_index];
			clone->identifier_mutations[mutation_index].slot = NULL;
			clone->identifier_mutations[mutation_index].value = NULL;
			clone->identifier_mutations[mutation_index].original =
				sqlparser_strdup(
					source->identifier_mutations
						[mutation_index]
						.original);
			if (clone->identifier_mutations[mutation_index].original ==
			    NULL) {
				clone->identifier_mutation_count =
					mutation_index;
				sqlparser_handle_destroy(clone);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			clone->identifier_mutations[mutation_index].spelling =
				source->identifier_mutations[mutation_index].spelling !=
						NULL ?
					sqlparser_strdup(
						source->identifier_mutations
							[mutation_index]
							.spelling) :
					NULL;
			if (source->identifier_mutations[mutation_index].spelling !=
				    NULL &&
			    clone->identifier_mutations[mutation_index].spelling ==
				    NULL) {
				clone->identifier_mutation_count =
					mutation_index + 1U;
				sqlparser_handle_destroy(clone);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			clone->identifier_mutation_count++;
		}
	}
	if (source->identifier_spelling_count > 0U) {
		size_t group_index;

		clone->identifier_spellings =
			(sqlparser_identifier_spelling_t *)calloc(
				source->identifier_spelling_count,
				sizeof(*clone->identifier_spellings));
		if (clone->identifier_spellings == NULL) {
			sqlparser_handle_destroy(clone);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->identifier_spelling_capacity =
			source->identifier_spelling_count;
		for (group_index = 0U;
		     group_index < source->identifier_spelling_count;
		     group_index++) {
			size_t part_index;

			clone->identifier_spellings[group_index].alias_style =
				source->identifier_spellings[group_index]
					.alias_style;
			for (part_index = 0U;
			     part_index <
				     source->identifier_spellings[group_index].part_count;
			     part_index++) {
				clone->identifier_spellings[group_index]
					.parts[part_index] =
					sqlparser_strdup(
						source->identifier_spellings[group_index]
							.parts[part_index]);
				if (clone->identifier_spellings[group_index]
					    .parts[part_index] == NULL) {
					clone->identifier_spelling_count =
						group_index + 1U;
					clone->identifier_spellings[group_index]
						.part_count =
						part_index;
					sqlparser_handle_destroy(clone);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_NO_MEMORY,
						"out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				clone->identifier_spellings[group_index].part_count++;
			}
			clone->identifier_spelling_count++;
		}
	}
	clone->identifier_spelling_free_group =
		source->identifier_spelling_free_group <=
				clone->identifier_spelling_count ?
			source->identifier_spelling_free_group :
			clone->identifier_spelling_count;
	clone->identifier_spelling_build_group = SIZE_MAX;
	clone->identifier_spelling_last_location =
		SQLPARSER_PROTO_LOCATION_GENERATED;
	if (source->control != NULL) {
		status = sqlparser_control_state_clone(source->control, &clone->limits, &clone->control, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_handle_destroy(clone);
			return status;
		}
	}

	clone->sql_len = source->sql_len;
	clone->parser_sql_len = source->parser_sql_len;
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
	sqlparser_handle_clear_bind_occurrences(target);
}

sqlparser_status_t sqlparser_handle_commit_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	size_t packed_size;
	size_t packed_len;
	char *packed;
	sqlparser_status_t status;

	if (handle == NULL || handle->ast == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sqlparser_handle_clear_query_graph(handle);
	status = sqlparser_validate_dialect_statements(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_ast_changes(handle);
		return status;
	}
	if (handle->control != NULL && handle->ast->n_stmts != handle->control->unit_count) {
		sqlparser_handle_discard_ast_changes(handle);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "control units do not match parser statements");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->prepare_ast_state != NULL) {
		status = handle->dialect_ops->prepare_ast_state(
			handle->dialect_state,
			handle->ast,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_handle_discard_ast_changes(handle);
			return status;
		}
	}
	packed_size = pg_query__parse_result__get_packed_size(handle->ast);
	if (packed_size == 0U) {
		sqlparser_handle_discard_ast_changes(handle);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to repack parse tree protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	packed = (char *)malloc(packed_size);
	if (packed == NULL) {
		sqlparser_handle_discard_ast_changes(handle);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	packed_len = pg_query__parse_result__pack(handle->ast, (uint8_t *)packed);
	if (packed_len != packed_size) {
		free(packed);
		sqlparser_handle_discard_ast_changes(handle);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to repack parse tree protobuf");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	if (sqlparser_validate_statement_count_limit(&handle->limits, handle->ast->n_stmts, out_error) !=
	    SQLPARSER_STATUS_OK) {
		free(packed);
		sqlparser_handle_discard_ast_changes(handle);
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_handle_rebind_identifier_mutations(
		handle,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(packed);
		sqlparser_handle_discard_ast_changes(handle);
		return status;
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->reconcile_ast_state != NULL) {
		handle->dialect_ops->reconcile_ast_state(
			handle->dialect_state,
			handle->ast);
	}

	free(handle->parse_tree.data);
	handle->parse_tree.data = packed;
	handle->parse_tree.len = packed_len;
	handle->statement_count = handle->control != NULL ? handle->control->unit_count : handle->ast->n_stmts;
	handle->generation++;
	sqlparser_surface_source_edits_release(
		&handle->surface_source_edits);
	handle->surface_source_complete = 0;
	sqlparser_handle_sweep_identifier_spellings(handle);
	sqlparser_handle_invalidate_derived(handle);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_handle_commit_ast_with_dialect_state(
	sqlparser_handle_t *handle,
	void *state,
	sqlparser_error_t *out_error)
{
	void *previous_state;
	sqlparser_status_t status;

	if (handle == NULL || state == NULL || state == handle->dialect_state) {
		return sqlparser_handle_commit_ast(handle, out_error);
	}

	previous_state = handle->dialect_state;
	handle->dialect_state = state;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		if (previous_state != NULL &&
		    handle->dialect_ops != NULL &&
		    handle->dialect_ops->destroy_state != NULL) {
			handle->dialect_ops->destroy_state(previous_state);
		}
		return SQLPARSER_STATUS_OK;
	}

	handle->dialect_state = previous_state;
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->bind_ast_state != NULL) {
		(void)handle->dialect_ops->bind_ast_state(
			previous_state,
			handle->ast,
			NULL);
	}
	sqlparser_handle_discard_dialect_state(handle, state);
	return status;
}

typedef struct {
	size_t source_start;
	size_t source_end;
	size_t source_gap;
	size_t output_gap;
	size_t output_position;
	int output_has_previous;
	int output_has_next;
} sqlparser_semantic_comment_t;

typedef struct {
	const char *sql;
	const PgQuery__ScanToken *token;
	size_t start;
	size_t end;
	size_t ordinal;
	int ascii_word;
} sqlparser_semantic_token_t;

typedef struct {
	const sqlparser_semantic_token_t *previous;
	const sqlparser_semantic_token_t *next;
	size_t gap;
} sqlparser_semantic_gap_t;

static int sqlparser_sql_has_semantic_comment_marker(const char *sql)
{
	size_t index;

	if (sql == NULL) {
		return 0;
	}
	for (index = 0U; sql[index] != '\0'; index++) {
		if (sql[index + 1U] == '\0' || sql[index + 2U] == '\0') {
			return 0;
		}
		if ((sql[index] == '/' && sql[index + 1U] == '*' &&
		     (sql[index + 2U] == '+' || sql[index + 2U] == '!')) ||
		    (sql[index] == '-' && sql[index + 1U] == '-' &&
		     sql[index + 2U] == '+')) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_scan_token_span(
	const PgQuery__ScanToken *token,
	size_t sql_length,
	size_t *out_start,
	size_t *out_end)
{
	size_t start;
	size_t end;

	if (token == NULL || token->start < 0 || token->end < token->start) {
		return 0;
	}
	start = (size_t)token->start;
	end = (size_t)token->end;
	if (end > sql_length) {
		return 0;
	}
	if (out_start != NULL) {
		*out_start = start;
	}
	if (out_end != NULL) {
		*out_end = end;
	}
	return 1;
}

static int sqlparser_scan_token_is_comment(const PgQuery__ScanToken *token)
{
	return token != NULL &&
		(token->token == PG_QUERY__TOKEN__SQL_COMMENT ||
		 token->token == PG_QUERY__TOKEN__C_COMMENT);
}

static int sqlparser_scan_token_is_semantic_comment(
	const char *sql,
	size_t sql_length,
	const PgQuery__ScanToken *token)
{
	size_t start;
	size_t end;

	if (sql == NULL || !sqlparser_scan_token_is_comment(token) ||
	    !sqlparser_scan_token_span(token, sql_length, &start, &end) ||
	    end - start < 3U) {
		return 0;
	}
	if (token->token == PG_QUERY__TOKEN__C_COMMENT) {
		return sql[start] == '/' && sql[start + 1U] == '*' &&
			(sql[start + 2U] == '+' || sql[start + 2U] == '!');
	}
	return sql[start] == '-' && sql[start + 1U] == '-' &&
		sql[start + 2U] == '+';
}

static int sqlparser_ascii_word_byte(unsigned char value, int first)
{
	return (value >= 'A' && value <= 'Z') ||
		(value >= 'a' && value <= 'z') || value == '_' ||
		(!first && ((value >= '0' && value <= '9') || value == '$'));
}

static int sqlparser_scan_token_is_ascii_word(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t index;

	if (start >= end ||
	    !sqlparser_ascii_word_byte((unsigned char)sql[start], 1)) {
		return 0;
	}
	for (index = start + 1U; index < end; index++) {
		if (!sqlparser_ascii_word_byte((unsigned char)sql[index], 0)) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_semantic_token_compare_value(
	const sqlparser_semantic_token_t *left,
	const sqlparser_semantic_token_t *right)
{
	size_t left_length;
	size_t right_length;
	size_t index;

	if (left->token->token != right->token->token) {
		return left->token->token < right->token->token ? -1 : 1;
	}
	if (left->token->keyword_kind != right->token->keyword_kind) {
		return left->token->keyword_kind < right->token->keyword_kind ?
			-1 : 1;
	}
	if (left->token->keyword_kind != PG_QUERY__KEYWORD_KIND__NO_KEYWORD) {
		return 0;
	}
	left_length = left->end - left->start;
	right_length = right->end - right->start;
	if (left_length != right_length) {
		return left_length < right_length ? -1 : 1;
	}
	if (left->ascii_word != right->ascii_word) {
		return left->ascii_word < right->ascii_word ? -1 : 1;
	}
	if (left->ascii_word) {
		for (index = 0U; index < left_length; index++) {
			unsigned char left_value;
			unsigned char right_value;

			left_value =
				(unsigned char)left->sql[left->start + index];
			right_value =
				(unsigned char)right->sql[right->start + index];
			if (left_value >= 'A' && left_value <= 'Z') {
				left_value = (unsigned char)(left_value - 'A' + 'a');
			}
			if (right_value >= 'A' && right_value <= 'Z') {
				right_value = (unsigned char)(right_value - 'A' + 'a');
			}
			if (left_value != right_value) {
				return left_value < right_value ? -1 : 1;
			}
		}
		return 0;
	}
	return memcmp(
		left->sql + left->start,
		right->sql + right->start,
		left_length);
}

static int sqlparser_semantic_token_compare(
	const void *left,
	const void *right)
{
	return sqlparser_semantic_token_compare_value(
		(const sqlparser_semantic_token_t *)left,
		(const sqlparser_semantic_token_t *)right);
}

static int sqlparser_semantic_gap_compare_value(
	const sqlparser_semantic_gap_t *left,
	const sqlparser_semantic_gap_t *right)
{
	int comparison;

	comparison = sqlparser_semantic_token_compare_value(
		left->previous, right->previous);
	if (comparison != 0) {
		return comparison;
	}
	return sqlparser_semantic_token_compare_value(left->next, right->next);
}

static int sqlparser_semantic_gap_compare(
	const void *left,
	const void *right)
{
	return sqlparser_semantic_gap_compare_value(
		(const sqlparser_semantic_gap_t *)left,
		(const sqlparser_semantic_gap_t *)right);
}

static int sqlparser_collect_semantic_tokens(
	const char *sql,
	size_t sql_length,
	const PgQuery__ScanResult *scan,
	sqlparser_semantic_token_t *tokens,
	size_t token_count)
{
	size_t ordinal;
	size_t index;

	ordinal = 0U;
	for (index = 0U;
	     scan != NULL && index < scan->n_tokens;
	     index++) {
		const PgQuery__ScanToken *token;
		size_t start;
		size_t end;

		token = scan->tokens[index];
		if (sqlparser_scan_token_is_comment(token)) {
			continue;
		}
		if (ordinal >= token_count ||
		    !sqlparser_scan_token_span(
			    token, sql_length, &start, &end)) {
			return 0;
		}
		tokens[ordinal].sql = sql;
		tokens[ordinal].token = token;
		tokens[ordinal].start = start;
		tokens[ordinal].end = end;
		tokens[ordinal].ordinal = ordinal;
		tokens[ordinal].ascii_word =
			sqlparser_scan_token_is_ascii_word(sql, start, end);
		ordinal++;
	}
	return ordinal == token_count;
}

static int sqlparser_find_unique_semantic_token(
	const sqlparser_semantic_token_t *tokens,
	size_t token_count,
	const sqlparser_semantic_token_t *key,
	size_t *out_ordinal)
{
	size_t left;
	size_t right;
	size_t first;

	left = 0U;
	right = token_count;
	while (left < right) {
		size_t middle;

		middle = left + (right - left) / 2U;
		if (sqlparser_semantic_token_compare_value(
			    &tokens[middle], key) < 0) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	first = left;
	if (first >= token_count ||
	    sqlparser_semantic_token_compare_value(&tokens[first], key) != 0 ||
	    (first + 1U < token_count &&
	     sqlparser_semantic_token_compare_value(
		     &tokens[first + 1U], key) == 0)) {
		return 0;
	}
	*out_ordinal = tokens[first].ordinal;
	return 1;
}

static int sqlparser_find_unique_semantic_gap(
	const sqlparser_semantic_gap_t *gaps,
	size_t gap_count,
	const sqlparser_semantic_gap_t *key,
	size_t *out_gap)
{
	size_t left;
	size_t right;
	size_t first;

	left = 0U;
	right = gap_count;
	while (left < right) {
		size_t middle;

		middle = left + (right - left) / 2U;
		if (sqlparser_semantic_gap_compare_value(
			    &gaps[middle], key) < 0) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	first = left;
	if (first >= gap_count ||
	    sqlparser_semantic_gap_compare_value(&gaps[first], key) != 0 ||
	    (first + 1U < gap_count &&
	     sqlparser_semantic_gap_compare_value(&gaps[first + 1U], key) == 0)) {
		return 0;
	}
	*out_gap = gaps[first].gap;
	return 1;
}

static int sqlparser_semantic_gap_position(
	const sqlparser_semantic_token_t *tokens,
	size_t token_count,
	size_t sql_length,
	size_t gap,
	size_t *out_position,
	int *out_has_previous,
	int *out_has_next)
{
	if (gap > token_count) {
		return 0;
	}
	*out_position = gap < token_count ?
		tokens[gap].start :
		(token_count > 0U ? tokens[token_count - 1U].end : 0U);
	if (*out_position > sql_length) {
		return 0;
	}
	*out_has_previous = gap > 0U;
	*out_has_next = gap < token_count;
	return 1;
}

static sqlparser_status_t sqlparser_restore_semantic_comments(
	const sqlparser_handle_t *handle,
	char **in_out_sql,
	sqlparser_error_t *out_error)
{
	PgQueryScanResult source_scan_result;
	PgQueryScanResult output_scan_result;
	PgQuery__ScanResult *source_scan;
	PgQuery__ScanResult *output_scan;
	sqlparser_semantic_comment_t *comments;
	sqlparser_semantic_token_t *source_tokens;
	sqlparser_semantic_token_t *output_tokens;
	sqlparser_semantic_token_t *output_matches;
	sqlparser_semantic_gap_t *gap_matches;
	const char *source_sql;
	const char *output_sql;
	char *restored;
	size_t source_length;
	size_t output_length;
	size_t source_non_comment_count;
	size_t output_non_comment_count;
	size_t semantic_comment_count;
	size_t common_prefix;
	size_t common_suffix;
	size_t output_middle_end;
	size_t output_match_count;
	size_t gap_match_count;
	size_t source_index;
	size_t output_index;
	size_t source_gap;
	size_t comment_index;
	size_t capacity;
	size_t restored_length;
	size_t output_cursor;
	sqlparser_status_t status;

	if (handle == NULL || in_out_sql == NULL || *in_out_sql == NULL ||
	    handle->parser_sql == NULL ||
	    !sqlparser_sql_has_semantic_comment_marker(handle->parser_sql)) {
		return SQLPARSER_STATUS_OK;
	}

	source_sql = handle->parser_sql;
	output_sql = *in_out_sql;
	source_length = handle->parser_sql_len;
	output_length = strlen(output_sql);
	memset(&source_scan_result, 0, sizeof(source_scan_result));
	memset(&output_scan_result, 0, sizeof(output_scan_result));
	source_scan = NULL;
	output_scan = NULL;
	comments = NULL;
	source_tokens = NULL;
	output_tokens = NULL;
	output_matches = NULL;
	gap_matches = NULL;
	restored = NULL;
	status = SQLPARSER_STATUS_INTERNAL_ERROR;

	source_scan_result = pg_query_scan(source_sql);
	if (source_scan_result.error != NULL ||
	    source_scan_result.pbuf.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to scan semantic comments in source SQL");
		goto cleanup;
	}
	source_scan = pg_query__scan_result__unpack(
		NULL,
		source_scan_result.pbuf.len,
		(const uint8_t *)source_scan_result.pbuf.data);
	if (source_scan == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to unpack source SQL tokens");
		goto cleanup;
	}

	semantic_comment_count = 0U;
	source_non_comment_count = 0U;
	for (source_index = 0U;
	     source_index < source_scan->n_tokens;
	     source_index++) {
		const PgQuery__ScanToken *token;

		token = source_scan->tokens[source_index];
		if (sqlparser_scan_token_is_semantic_comment(
			    source_sql,
			    source_length,
			    token)) {
			semantic_comment_count++;
		}
		if (!sqlparser_scan_token_is_comment(token)) {
			source_non_comment_count++;
		}
	}
	if (semantic_comment_count == 0U) {
		status = SQLPARSER_STATUS_OK;
		goto cleanup;
	}
	if (semantic_comment_count > SIZE_MAX / sizeof(*comments) ||
	    source_non_comment_count > SIZE_MAX / sizeof(*source_tokens)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}
	comments = (sqlparser_semantic_comment_t *)malloc(
		semantic_comment_count * sizeof(*comments));
	source_tokens = source_non_comment_count > 0U ?
		(sqlparser_semantic_token_t *)malloc(
			source_non_comment_count * sizeof(*source_tokens)) : NULL;
	if (comments == NULL ||
	    (source_non_comment_count > 0U && source_tokens == NULL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}

	source_gap = 0U;
	comment_index = 0U;
	for (source_index = 0U;
	     source_index < source_scan->n_tokens;
	     source_index++) {
		const PgQuery__ScanToken *token;

		token = source_scan->tokens[source_index];
		if (sqlparser_scan_token_is_semantic_comment(
			    source_sql, source_length, token)) {
			if (!sqlparser_scan_token_span(
				    token,
				    source_length,
				    &comments[comment_index].source_start,
				    &comments[comment_index].source_end)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"semantic comment source span is invalid");
				goto cleanup;
			}
			comments[comment_index].source_gap = source_gap;
			comment_index++;
		}
		if (!sqlparser_scan_token_is_comment(token)) {
			size_t start;
			size_t end;

			if (source_gap >= source_non_comment_count ||
			    !sqlparser_scan_token_span(
				    token, source_length, &start, &end)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"source SQL token span is invalid");
				goto cleanup;
			}
			source_tokens[source_gap].sql = source_sql;
			source_tokens[source_gap].token = token;
			source_tokens[source_gap].start = start;
			source_tokens[source_gap].end = end;
			source_tokens[source_gap].ordinal = source_gap;
			source_tokens[source_gap].ascii_word =
				sqlparser_scan_token_is_ascii_word(
					source_sql, start, end);
			source_gap++;
		}
	}
	if (source_gap != source_non_comment_count ||
	    comment_index != semantic_comment_count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"source SQL token span is invalid");
		goto cleanup;
	}

	output_scan_result = pg_query_scan(output_sql);
	if (output_scan_result.error != NULL ||
	    output_scan_result.pbuf.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to scan forced deparse SQL");
		goto cleanup;
	}
	output_scan = pg_query__scan_result__unpack(
		NULL,
		output_scan_result.pbuf.len,
		(const uint8_t *)output_scan_result.pbuf.data);
	if (output_scan == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to unpack forced deparse SQL tokens");
		goto cleanup;
	}
	output_non_comment_count = 0U;
	for (output_index = 0U;
	     output_index < output_scan->n_tokens;
	     output_index++) {
		const PgQuery__ScanToken *token;

		token = output_scan->tokens[output_index];
		if (sqlparser_scan_token_is_semantic_comment(
			    output_sql,
			    output_length,
			    token)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"forced deparse produced an unexpected semantic comment");
			goto cleanup;
		}
		if (!sqlparser_scan_token_is_comment(token)) {
			output_non_comment_count++;
		}
	}
	if (output_non_comment_count > SIZE_MAX / sizeof(*output_tokens)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}
	output_tokens = output_non_comment_count > 0U ?
		(sqlparser_semantic_token_t *)malloc(
			output_non_comment_count * sizeof(*output_tokens)) : NULL;
	if ((output_non_comment_count > 0U && output_tokens == NULL) ||
	    !sqlparser_collect_semantic_tokens(
		    output_sql,
		    output_length,
		    output_scan,
		    output_tokens,
		    output_non_comment_count)) {
		if (output_non_comment_count > 0U && output_tokens == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			status = SQLPARSER_STATUS_NO_MEMORY;
		} else {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"forced deparse token span is invalid");
		}
		goto cleanup;
	}

	common_prefix = 0U;
	while (common_prefix < source_non_comment_count &&
	       common_prefix < output_non_comment_count) {
		if (sqlparser_semantic_token_compare_value(
			    &source_tokens[common_prefix],
			    &output_tokens[common_prefix]) != 0) {
			break;
		}
		common_prefix++;
	}

	common_suffix = 0U;
	while (common_suffix < source_non_comment_count - common_prefix &&
	       common_suffix < output_non_comment_count - common_prefix) {
		if (sqlparser_semantic_token_compare_value(
			    &source_tokens[
				    source_non_comment_count - common_suffix - 1U],
			    &output_tokens[
				    output_non_comment_count - common_suffix - 1U]) != 0) {
			break;
		}
		common_suffix++;
	}

	output_middle_end = output_non_comment_count - common_suffix;
	output_match_count = output_middle_end - common_prefix;
	if (output_match_count > SIZE_MAX / sizeof(*output_matches)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}
	output_matches = output_match_count > 0U ?
		(sqlparser_semantic_token_t *)malloc(
			output_match_count * sizeof(*output_matches)) : NULL;
	if (output_match_count > 0U && output_matches == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}
	if (output_match_count > 0U) {
		memcpy(
			output_matches,
			output_tokens + common_prefix,
			output_match_count * sizeof(*output_matches));
	}
	if (output_match_count > 1U) {
		qsort(
			output_matches,
			output_match_count,
			sizeof(*output_matches),
			sqlparser_semantic_token_compare);
	}

	gap_match_count = 0U;
	if (output_non_comment_count > 1U) {
		size_t first_gap;
		size_t last_gap;

		first_gap = common_prefix > 0U ? common_prefix : 1U;
		last_gap = output_middle_end < output_non_comment_count ?
			output_middle_end : output_non_comment_count - 1U;
		if (first_gap <= last_gap) {
			gap_match_count = last_gap - first_gap + 1U;
			if (gap_match_count > SIZE_MAX / sizeof(*gap_matches)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				status = SQLPARSER_STATUS_NO_MEMORY;
				goto cleanup;
			}
			gap_matches = (sqlparser_semantic_gap_t *)malloc(
				gap_match_count * sizeof(*gap_matches));
			if (gap_matches == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				status = SQLPARSER_STATUS_NO_MEMORY;
				goto cleanup;
			}
			for (output_index = 0U;
			     output_index < gap_match_count;
			     output_index++) {
				size_t gap;

				gap = first_gap + output_index;
				gap_matches[output_index].previous =
					&output_tokens[gap - 1U];
				gap_matches[output_index].next =
					&output_tokens[gap];
				gap_matches[output_index].gap = gap;
			}
			if (gap_match_count > 1U) {
				qsort(
					gap_matches,
					gap_match_count,
					sizeof(*gap_matches),
					sqlparser_semantic_gap_compare);
			}
		}
	}

	capacity = output_length;
	for (comment_index = 0U;
	     comment_index < semantic_comment_count;
	     comment_index++) {
		sqlparser_semantic_comment_t *comment;
		size_t source_middle_end;
		size_t comment_length;

		comment = &comments[comment_index];
		source_middle_end =
			source_non_comment_count - common_suffix;
		if (comment->source_gap <= common_prefix) {
			comment->output_gap = comment->source_gap;
		} else if (comment->source_gap >= source_middle_end) {
			comment->output_gap =
				output_non_comment_count -
				(source_non_comment_count -
				 comment->source_gap);
		} else {
			const sqlparser_semantic_token_t *anchor;
			const sqlparser_semantic_token_t *previous_anchor;
			sqlparser_semantic_gap_t gap_key;
			size_t matched_ordinal;

			anchor = &source_tokens[comment->source_gap];
			previous_anchor =
				&source_tokens[comment->source_gap - 1U];
			gap_key.previous = previous_anchor;
			gap_key.next = anchor;
			if (sqlparser_find_unique_semantic_gap(
				    gap_matches,
				    gap_match_count,
				    &gap_key,
				    &matched_ordinal)) {
				comment->output_gap = matched_ordinal;
			} else if (sqlparser_find_unique_semantic_token(
				    output_matches,
				    output_match_count,
				    anchor,
				    &matched_ordinal)) {
				comment->output_gap = matched_ordinal;
			} else {
				if (sqlparser_find_unique_semantic_token(
					    output_matches,
					    output_match_count,
					    previous_anchor,
					    &matched_ordinal)) {
					comment->output_gap = matched_ordinal + 1U;
				} else {
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_INTERNAL_ERROR,
						"semantic comment position is ambiguous after patch");
					goto cleanup;
				}
			}
		}
		if (comment_index > 0U &&
		    comment->output_gap < comments[comment_index - 1U].output_gap) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"semantic comment order changed during deparse");
			goto cleanup;
		}
		if (!sqlparser_semantic_gap_position(
			    output_tokens,
			    output_non_comment_count,
			    output_length,
			    comment->output_gap,
			    &comment->output_position,
			    &comment->output_has_previous,
			    &comment->output_has_next)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"semantic comment output position is invalid");
			goto cleanup;
		}
		if (comment_index > 0U &&
		    comment->output_position <
			    comments[comment_index - 1U].output_position) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"semantic comment position changed during deparse");
			goto cleanup;
		}
		comment_length = comment->source_end - comment->source_start;
		if (capacity > SIZE_MAX - 2U ||
		    comment_length > SIZE_MAX - capacity - 2U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			status = SQLPARSER_STATUS_NO_MEMORY;
			goto cleanup;
		}
		capacity += comment_length + 2U;
	}
	if (capacity == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}
	restored = (char *)malloc(capacity + 1U);
	if (restored == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		status = SQLPARSER_STATUS_NO_MEMORY;
		goto cleanup;
	}

	restored_length = 0U;
	output_cursor = 0U;
	for (comment_index = 0U;
	     comment_index < semantic_comment_count;
	     comment_index++) {
		const sqlparser_semantic_comment_t *comment;
		size_t comment_length;
		int line_comment;

		comment = &comments[comment_index];
		if (comment->output_position > output_cursor) {
			memcpy(
				restored + restored_length,
				output_sql + output_cursor,
				comment->output_position - output_cursor);
			restored_length +=
				comment->output_position - output_cursor;
			output_cursor = comment->output_position;
		}
		if (!comment->output_has_next &&
		    comment->output_has_previous && restored_length > 0U &&
		    !isspace((unsigned char)restored[restored_length - 1U])) {
			restored[restored_length++] = ' ';
		}
		comment_length = comment->source_end - comment->source_start;
		memcpy(
			restored + restored_length,
			source_sql + comment->source_start,
			comment_length);
		restored_length += comment_length;
		line_comment =
			source_sql[comment->source_start] == '-';
		if (comment->output_has_next) {
			if (line_comment &&
			    (restored_length == 0U ||
			     (restored[restored_length - 1U] != '\n' &&
			      restored[restored_length - 1U] != '\r'))) {
				if (comment->source_end < source_length &&
				    source_sql[comment->source_end] == '\r') {
					restored[restored_length++] = '\r';
					if (comment->source_end + 1U < source_length &&
					    source_sql[comment->source_end + 1U] == '\n') {
						restored[restored_length++] = '\n';
					}
				} else if (comment->source_end < source_length &&
					   source_sql[comment->source_end] == '\n') {
					restored[restored_length++] = '\n';
				} else {
					restored[restored_length++] = '\n';
				}
			} else if (!line_comment &&
				   (restored_length == 0U ||
				    !isspace((unsigned char)
					     restored[restored_length - 1U]))) {
				restored[restored_length++] = ' ';
			}
		}
	}
	if (output_cursor < output_length) {
		memcpy(
			restored + restored_length,
			output_sql + output_cursor,
			output_length - output_cursor);
		restored_length += output_length - output_cursor;
	}
	restored[restored_length] = '\0';
	free(*in_out_sql);
	*in_out_sql = restored;
	restored = NULL;
	status = SQLPARSER_STATUS_OK;

cleanup:
	free(restored);
	free(gap_matches);
	free(output_matches);
	free(output_tokens);
	free(source_tokens);
	free(comments);
	if (source_scan != NULL) {
		pg_query__scan_result__free_unpacked(source_scan, NULL);
	}
	if (output_scan != NULL) {
		pg_query__scan_result__free_unpacked(output_scan, NULL);
	}
	pg_query_free_scan_result(source_scan_result);
	pg_query_free_scan_result(output_scan_result);
	return status;
}

static sqlparser_status_t sqlparser_control_surface_advance(
	const sqlparser_handle_t *handle,
	size_t source_limit,
	int include_insert_at_limit,
	size_t output_length,
	size_t *in_out_edit_index,
	size_t *in_out_source_cursor,
	size_t *in_out_output_cursor,
	sqlparser_error_t *out_error)
{
	const sqlparser_surface_source_edits_t *edits;
	size_t edit_index;
	size_t output_cursor;
	size_t source_cursor;

	edits = &handle->surface_source_edits;
	edit_index = *in_out_edit_index;
	source_cursor = *in_out_source_cursor;
	output_cursor = *in_out_output_cursor;
	while (edit_index < edits->count) {
		const sqlparser_surface_source_edit_t *edit;
		size_t copy_length;

		edit = &edits->items[edit_index];
		if (edit->source_start > source_limit ||
		    (edit->source_start == source_limit &&
		     (!include_insert_at_limit ||
		      edit->source_end != source_limit))) {
			break;
		}
		if (edit->replacement == NULL ||
		    edit->source_start < source_cursor ||
		    edit->source_end < edit->source_start ||
		    edit->source_end > source_limit) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"control surface edit crosses a unit boundary");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		copy_length = edit->source_start - source_cursor;
		if (copy_length > output_length - output_cursor ||
		    edit->replacement_length >
			    output_length - output_cursor - copy_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"control surface output length is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		output_cursor += copy_length + edit->replacement_length;
		source_cursor = edit->source_end;
		edit_index++;
	}
	if (source_cursor > source_limit ||
	    source_limit - source_cursor > output_length - output_cursor) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"control surface output range is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	output_cursor += source_limit - source_cursor;
	*in_out_edit_index = edit_index;
	*in_out_source_cursor = source_limit;
	*in_out_output_cursor = output_cursor;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_control_project_surface_units(
	sqlparser_handle_t *handle,
	size_t output_length,
	sqlparser_error_t *out_error)
{
	size_t edit_index;
	size_t output_cursor;
	size_t source_cursor;
	size_t unit_index;
	sqlparser_status_t status;

	edit_index = 0U;
	source_cursor = 0U;
	output_cursor = 0U;
	for (unit_index = 0U;
	     unit_index < handle->control->unit_count;
	     unit_index++) {
		sqlparser_control_unit_t *unit;
		size_t unit_end;

		unit = &handle->control->units[unit_index];
		if (unit->source_offset > handle->sql_len ||
		    unit->source_length >
			    handle->sql_len - unit->source_offset) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"control unit source range is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = sqlparser_control_surface_advance(
			handle,
			unit->source_offset,
			0,
			output_length,
			&edit_index,
			&source_cursor,
			&output_cursor,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		unit->current_offset = output_cursor;
		unit_end = unit->source_offset + unit->source_length;
		status = sqlparser_control_surface_advance(
			handle,
			unit_end,
			1,
			output_length,
			&edit_index,
			&source_cursor,
			&output_cursor,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		unit->current_length =
			output_cursor - unit->current_offset;
	}
	status = sqlparser_control_surface_advance(
		handle,
		handle->sql_len,
		1,
		output_length,
		&edit_index,
		&source_cursor,
		&output_cursor,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (edit_index != handle->surface_source_edits.count ||
	    output_cursor != output_length) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"control surface projection is incomplete");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
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
	if (handle->surface_source_complete) {
		if (handle->current_sql != NULL) {
			return SQLPARSER_STATUS_OK;
		}
		mutable_handle = (sqlparser_handle_t *)handle;
		public_sql = NULL;
		status = sqlparser_restore_source_envelope(
			handle,
			&public_sql,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_validate_handle_output_text(
				handle,
				public_sql,
				"current SQL",
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && handle->control != NULL) {
			status = sqlparser_control_project_surface_units(
				mutable_handle,
				strlen(public_sql),
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(public_sql);
			return status;
		}
		mutable_handle->current_sql = public_sql;
		return SQLPARSER_STATUS_OK;
	}
	if ((handle->control != NULL && handle->current_sql != NULL) ||
	    (handle->control == NULL && handle->current_sql != NULL && handle->current_parser_sql != NULL)) {
		return SQLPARSER_STATUS_OK;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	sqlparser_handle_clear_current_sql(mutable_handle);
	if (handle->control != NULL) {
		public_sql = NULL;
		status = sqlparser_control_build_public_sql(handle, &public_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		mutable_handle->current_sql = public_sql;
		return SQLPARSER_STATUS_OK;
	}

	sqlparser_pg_query_prepare();
	deparse_result = sqlparser_deparse_protobuf_for_handle(
		handle,
		handle->parse_tree,
		0U,
		0U,
		handle->parser_sql_len);
	if (deparse_result.error != NULL || deparse_result.query == NULL) {
		if (deparse_result.error != NULL) {
			sqlparser_error_from_pg(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				handle->parser_sql,
				deparse_result.error);
		} else {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"failed to deparse current SQL");
		}
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	status = sqlparser_restore_semantic_comments(
		handle,
		&deparse_result.query,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_deparse_result(deparse_result);
		return status;
	}
	status = sqlparser_validate_handle_output_text(handle, deparse_result.query, "current SQL", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_deparse_result(deparse_result);
		return status;
	}

	if (handle->dialect_ops == NULL || handle->dialect_ops->postprocess_deparse == NULL) {
		status = sqlparser_restore_source_envelope(
			handle,
			&deparse_result.query,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			pg_query_free_deparse_result(deparse_result);
			return status;
		}
		mutable_handle->current_parser_sql = deparse_result.query;
		mutable_handle->current_sql = mutable_handle->current_parser_sql;
		deparse_result.query = NULL;
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_OK;
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
	status = sqlparser_restore_source_envelope(
		handle,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		pg_query_free_deparse_result(deparse_result);
		return status;
	}
	mutable_handle->current_parser_sql = deparse_result.query;
	deparse_result.query = NULL;
	pg_query_free_deparse_result(deparse_result);
	status = sqlparser_validate_handle_output_text(handle, public_sql, "deparse output", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		sqlparser_handle_clear_current_sql(mutable_handle);
		return status;
	}
	if (public_sql != NULL &&
	    mutable_handle->current_parser_sql != NULL &&
	    strcmp(public_sql, mutable_handle->current_parser_sql) == 0) {
		free(public_sql);
		mutable_handle->current_sql = mutable_handle->current_parser_sql;
	} else {
		mutable_handle->current_sql = public_sql;
	}
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

	if (handle->control != NULL || handle->generation == 0UL || handle->current_parser_sql == NULL) {
		return handle->parser_sql;
	}

	return handle->current_parser_sql;
}

sqlparser_status_t sqlparser_postprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *core_sql,
	sqlparser_fragment_context_t fragment_context,
	ProtobufCMessage *const *roots,
	size_t root_count,
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
	if (handle->dialect_ops != NULL && handle->dialect_ops->postprocess_fragment != NULL) {
		status = handle->dialect_ops->postprocess_fragment(
			core_sql,
			handle->dialect_state,
			statement_index,
			fragment_context,
			roots,
			root_count,
			&public_sql,
			out_error);
	} else if (handle->dialect_ops != NULL && handle->dialect_ops->postprocess_deparse != NULL) {
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

sqlparser_status_t sqlparser_handle_adopt_dialect_state(
	sqlparser_handle_t *handle,
	void *state,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (handle == NULL || state == NULL || state == handle->dialect_state) {
		return SQLPARSER_STATUS_OK;
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->bind_ast_state != NULL) {
		status = handle->dialect_ops->bind_ast_state(
			state, handle->ast, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->reconcile_ast_state != NULL) {
		handle->dialect_ops->reconcile_ast_state(state, handle->ast);
	}

	if (handle->dialect_ops != NULL && handle->dialect_ops->destroy_state != NULL) {
		handle->dialect_ops->destroy_state(handle->dialect_state);
	}
	handle->dialect_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_preprocess_handle_sql_fragment_internal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *public_sql,
	const char *field_name,
	char **out_parser_sql,
	void **out_dialect_state,
	sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error)
{
	void *candidate_state;
	sqlparser_identifier_origin_map_t *origins;
	size_t dameng_target_relation_index;
	int dameng_multi_update_fragment;
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
	if (out_origins != NULL) {
		*out_origins = NULL;
	}

	status = sqlparser_validate_handle_sql_input(
		handle,
		public_sql,
		field_name != NULL ? field_name : "SQL fragment",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	origins = NULL;
	if (out_origins != NULL) {
		status = sqlparser_identifier_origin_map_new_identity(
			strlen(public_sql),
			&origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	if (handle->dialect_ops == NULL || handle->dialect_ops->preprocess_fragment == NULL) {
		*out_parser_sql = sqlparser_strdup(public_sql);
		if (*out_parser_sql == NULL) {
			sqlparser_identifier_origin_map_destroy(origins);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (out_origins != NULL) {
			*out_origins = origins;
		}
		return SQLPARSER_STATUS_OK;
	}

	candidate_state = NULL;
	if (handle->dialect_ops->clone_state != NULL) {
		status = handle->dialect_ops->clone_state(handle->dialect_state, &candidate_state, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_identifier_origin_map_destroy(origins);
			return status;
		}
	}

	dameng_target_relation_index = 0U;
	dameng_multi_update_fragment = 0;
	if (handle->dialect == SQLPARSER_DIALECT_DAMENG &&
	    field_name != NULL &&
	    (strcmp(field_name, "update assignment SQL") == 0 ||
	     strcmp(field_name, "update SET list SQL") == 0 ||
	     strcmp(field_name, "assignment target SQL") == 0) &&
	    sqlparser_dameng_statement_multi_update_target_index(
		    candidate_state,
		    statement_index,
		    &dameng_target_relation_index)) {
		status =
			sqlparser_dameng_preprocess_multi_update_assignment_fragment(
				public_sql,
				candidate_state,
				statement_index,
				strcmp(field_name, "assignment target SQL") == 0,
				out_parser_sql,
				out_error);
		dameng_multi_update_fragment = 1;
	} else if (origins != NULL &&
	    handle->dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER) {
		status =
			sqlparser_vastbase_sqlserver_preprocess_fragment_identifier_origins(
				public_sql,
				candidate_state,
				statement_index,
				out_parser_sql,
				origins,
				out_error);
	} else if (origins != NULL &&
		   sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
		status =
			sqlparser_sqlserver_preprocess_fragment_identifier_origins(
				public_sql,
				candidate_state,
				statement_index,
				out_parser_sql,
				origins,
				out_error);
	} else if (origins != NULL &&
		   sqlparser_dialect_is_mysql_compatible(handle->dialect)) {
		status =
			sqlparser_mysql_preprocess_fragment_identifier_origins(
				public_sql,
				candidate_state,
				statement_index,
				out_parser_sql,
				origins,
				out_error);
	} else if (origins != NULL &&
		   sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		status =
			sqlparser_oracle_preprocess_fragment_identifier_origins(
				public_sql,
				candidate_state,
				statement_index,
				out_parser_sql,
				origins,
				out_error);
	} else {
		status = handle->dialect_ops->preprocess_fragment(
			public_sql,
			candidate_state,
			statement_index,
			out_parser_sql,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_map_destroy(origins);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	if (*out_parser_sql == NULL) {
		sqlparser_identifier_origin_map_destroy(origins);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"dialect fragment output is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (origins != NULL &&
	    (dameng_multi_update_fragment ||
	     (!sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	      !sqlparser_dialect_is_mysql_compatible(handle->dialect) &&
	      !sqlparser_dialect_is_oracle_compatible(handle->dialect))) &&
	    strcmp(*out_parser_sql, public_sql) != 0) {
		sqlparser_identifier_origin_map_destroy(origins);
		origins = NULL;
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
		sqlparser_identifier_origin_map_destroy(origins);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}

	*out_dialect_state = candidate_state;
	if (out_origins != NULL) {
		*out_origins = origins;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_preprocess_handle_sql_fragment_with_origins(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *public_sql,
	const char *field_name,
	char **out_parser_sql,
	void **out_dialect_state,
	sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error)
{
	if (out_origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"fragment identifier origins output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_preprocess_handle_sql_fragment_internal(
		handle,
		statement_index,
		public_sql,
		field_name,
		out_parser_sql,
		out_dialect_state,
		out_origins,
		out_error);
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
		case SQLPARSER_STATEMENT_KIND_CONDITION:
			return "condition";
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

const char *sqlparser_bind_kind_name(sqlparser_bind_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_BIND_KIND_POSITIONAL:
			return "positional";
		case SQLPARSER_BIND_KIND_NAMED:
			return "named";
		case SQLPARSER_BIND_KIND_NONE:
		default:
			return "none";
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
		case SQLPARSER_SELECTOR_KIND_VALUE:
			return "value";
		case SQLPARSER_SELECTOR_KIND_EXPRESSION:
			return "expression";
		case SQLPARSER_SELECTOR_KIND_EXPRESSION_ARG:
			return "expression_arg";
		case SQLPARSER_SELECTOR_KIND_EXPRESSION_ARGS:
			return "expression_args";
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
			return "where_literal";
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
			return "assignment";
		case SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT:
			return "merge_assignment";
		case SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION:
			return "merge_branch_condition";
		case SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION:
			return "merge_delete_condition";
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN:
			return "merge_insert_column";
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL:
			return "merge_insert_cell";
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			return "insert_cell";
		case SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS:
			return "insert_columns";
		case SQLPARSER_SELECTOR_KIND_INSERT_ROW:
			return "insert_row";
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGETS:
			return "select_targets";
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGET:
			return "select_target";
		case SQLPARSER_SELECTOR_KIND_WHERE:
			return "where";
		case SQLPARSER_SELECTOR_KIND_CLAUSE:
			return "clause";
		case SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS:
			return "insert_branch_columns";
		case SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION:
			return "insert_branch_condition";
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS:
			return "dml_result_targets";
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET:
			return "dml_result_target";
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK:
			return "dml_result_sink";
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS:
			return "dml_result_sink_columns";
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN:
			return "dml_result_sink_column";
		case SQLPARSER_SELECTOR_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_bool_operator_name(sqlparser_bool_operator_t bool_operator)
{
	switch (bool_operator) {
		case SQLPARSER_BOOL_OPERATOR_AND:
			return "and";
		case SQLPARSER_BOOL_OPERATOR_OR:
			return "or";
		default:
			return "unknown";
	}
}

const char *sqlparser_clause_kind_name(sqlparser_clause_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_CLAUSE_KIND_SELECT_LIST:
			return "select_list";
		case SQLPARSER_CLAUSE_KIND_WHERE:
			return "where";
		case SQLPARSER_CLAUSE_KIND_ORDER_BY:
			return "order_by";
		case SQLPARSER_CLAUSE_KIND_SET_LIST:
			return "set_list";
		case SQLPARSER_CLAUSE_KIND_ON:
			return "on";
		case SQLPARSER_CLAUSE_KIND_GROUP_BY:
			return "group_by";
		case SQLPARSER_CLAUSE_KIND_HAVING:
			return "having";
		case SQLPARSER_CLAUSE_KIND_DML_RESULT:
			return "dml_result";
		case SQLPARSER_CLAUSE_KIND_CONDITION:
			return "condition";
		case SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION:
			return "window_partition";
		case SQLPARSER_CLAUSE_KIND_START_WITH:
			return "start_with";
		case SQLPARSER_CLAUSE_KIND_CONNECT_BY:
			return "connect_by";
		case SQLPARSER_CLAUSE_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_block_kind_name(sqlparser_graph_block_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_BLOCK_SELECT:
			return "select";
		case SQLPARSER_GRAPH_BLOCK_SCALAR_SUBQUERY:
			return "scalar_subquery";
		case SQLPARSER_GRAPH_BLOCK_CTE:
			return "cte";
		case SQLPARSER_GRAPH_BLOCK_SET:
			return "set";
		case SQLPARSER_GRAPH_BLOCK_DML_RESULT:
			return "dml_result";
		case SQLPARSER_GRAPH_BLOCK_CONDITION:
			return "condition";
		case SQLPARSER_GRAPH_BLOCK_DDL:
			return "ddl";
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_ddl_relation_role_name(sqlparser_graph_ddl_relation_role_t role)
{
	switch (role) {
		case SQLPARSER_GRAPH_DDL_RELATION_TARGET:
			return "target";
		case SQLPARSER_GRAPH_DDL_RELATION_REFERENCE:
			return "reference";
		case SQLPARSER_GRAPH_DDL_RELATION_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_relation_kind_name(sqlparser_graph_relation_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_REL_BASE:
			return "base";
		case SQLPARSER_GRAPH_REL_DERIVED:
			return "derived";
		case SQLPARSER_GRAPH_REL_CTE:
			return "cte";
		case SQLPARSER_GRAPH_REL_DUAL:
			return "dual";
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_target_kind_name(sqlparser_graph_target_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_TARGET_FIELD:
			return "field";
		case SQLPARSER_GRAPH_TARGET_STAR:
			return "star";
		case SQLPARSER_GRAPH_TARGET_QUALIFIED_STAR:
			return "qualified_star";
		case SQLPARSER_GRAPH_TARGET_LITERAL:
			return "literal";
		case SQLPARSER_GRAPH_TARGET_PSEUDO:
			return "pseudo";
		case SQLPARSER_GRAPH_TARGET_SUBQUERY:
			return "subquery";
		case SQLPARSER_GRAPH_TARGET_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_TARGET_BIND:
			return "bind";
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_value_kind_name(sqlparser_graph_value_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_VALUE_LITERAL:
			return "literal";
		case SQLPARSER_GRAPH_VALUE_BIND:
			return "bind";
		case SQLPARSER_GRAPH_VALUE_DEFAULT:
			return "default";
		case SQLPARSER_GRAPH_VALUE_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_VALUE_FIELD:
			return "field";
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_expression_kind_name(sqlparser_graph_expression_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_EXPRESSION_FUNCTION:
			return "function";
		case SQLPARSER_GRAPH_EXPRESSION_OPAQUE:
			return "opaque";
		case SQLPARSER_GRAPH_EXPRESSION_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_expression_argument_kind_name(
	sqlparser_graph_expression_argument_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_EXPRESSION_ARGUMENT_LITERAL:
			return "literal";
		case SQLPARSER_GRAPH_EXPRESSION_ARGUMENT_BIND:
			return "bind";
		case SQLPARSER_GRAPH_EXPRESSION_ARGUMENT_FIELD:
			return "field";
		case SQLPARSER_GRAPH_EXPRESSION_ARGUMENT_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_EXPRESSION_ARGUMENT_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_field_match_kind_name(sqlparser_graph_field_match_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD:
			return "direct_field";
		case SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD:
			return "expression_field";
		case SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_operator_kind_name(sqlparser_graph_operator_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_OPERATOR_LIKE:
			return "like";
		case SQLPARSER_GRAPH_OPERATOR_NOT_LIKE:
			return "not_like";
		case SQLPARSER_GRAPH_OPERATOR_ILIKE:
			return "ilike";
		case SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE:
			return "not_ilike";
		case SQLPARSER_GRAPH_OPERATOR_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_set_kind_name(sqlparser_graph_set_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_SET_UNION:
			return "union";
		case SQLPARSER_GRAPH_SET_UNION_ALL:
			return "union_all";
		case SQLPARSER_GRAPH_SET_INTERSECT:
			return "intersect";
		case SQLPARSER_GRAPH_SET_EXCEPT:
			return "except";
		default:
			return "unknown";
	}
}

int sqlparser_graph_operator_is_like_pattern(sqlparser_graph_operator_kind_t kind)
{
	return kind == SQLPARSER_GRAPH_OPERATOR_LIKE ||
		kind == SQLPARSER_GRAPH_OPERATOR_NOT_LIKE ||
		kind == SQLPARSER_GRAPH_OPERATOR_ILIKE ||
		kind == SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE;
}

int sqlparser_graph_value_is_like_pattern(const sqlparser_graph_value_t *value)
{
	return value != NULL && sqlparser_graph_operator_is_like_pattern(value->operator_kind);
}

const char *sqlparser_graph_dml_kind_name(sqlparser_graph_dml_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_DML_INSERT:
			return "insert";
		case SQLPARSER_GRAPH_DML_UPDATE:
			return "update";
		case SQLPARSER_GRAPH_DML_DELETE:
			return "delete";
		case SQLPARSER_GRAPH_DML_MERGE:
			return "merge";
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_insert_mode_name(sqlparser_graph_insert_mode_t mode)
{
	switch (mode) {
		case SQLPARSER_GRAPH_INSERT_MODE_VALUES:
			return "values";
		case SQLPARSER_GRAPH_INSERT_MODE_SELECT:
			return "select";
		case SQLPARSER_GRAPH_INSERT_MODE_ALL:
			return "all";
		case SQLPARSER_GRAPH_INSERT_MODE_FIRST:
			return "first";
		case SQLPARSER_GRAPH_INSERT_MODE_REPLACE_VALUES:
			return "replace_values";
		case SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SELECT:
			return "replace_select";
		case SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SET:
			return "replace_set";
		case SQLPARSER_GRAPH_INSERT_MODE_SET:
			return "set";
		case SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_dml_branch_kind_name(sqlparser_graph_dml_branch_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_DML_BRANCH_WHEN:
			return "when";
		case SQLPARSER_GRAPH_DML_BRANCH_ELSE:
			return "else";
		case SQLPARSER_GRAPH_DML_BRANCH_UNCONDITIONAL:
		default:
			return "unconditional";
	}
}

const char *sqlparser_graph_merge_action_kind_name(sqlparser_graph_merge_action_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_MERGE_ACTION_INSERT:
			return "insert";
		case SQLPARSER_GRAPH_MERGE_ACTION_UPDATE:
			return "update";
		case SQLPARSER_GRAPH_MERGE_ACTION_DELETE:
			return "delete";
		case SQLPARSER_GRAPH_MERGE_ACTION_NOTHING:
			return "nothing";
		case SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_merge_match_kind_name(sqlparser_graph_merge_match_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_MERGE_MATCH_MATCHED:
			return "matched";
		case SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_SOURCE:
			return "not_matched_by_source";
		case SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET:
			return "not_matched_by_target";
		case SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_predicate_kind_name(sqlparser_graph_predicate_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_PREDICATE_COMPARISON:
			return "comparison";
		case SQLPARSER_GRAPH_PREDICATE_BOOL:
			return "bool";
		case SQLPARSER_GRAPH_PREDICATE_EXISTS:
			return "exists";
		case SQLPARSER_GRAPH_PREDICATE_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_PREDICATE_UNKNOWN:
		default:
			return "unknown";
	}
}

const char *sqlparser_graph_predicate_bool_name(sqlparser_graph_predicate_bool_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_PREDICATE_BOOL_AND:
			return "and";
		case SQLPARSER_GRAPH_PREDICATE_BOOL_OR:
			return "or";
		case SQLPARSER_GRAPH_PREDICATE_BOOL_NOT:
			return "not";
		case SQLPARSER_GRAPH_PREDICATE_BOOL_NONE:
		default:
			return "none";
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
		case SQLPARSER_DIALECT_DAMENG:
			return "dameng";
		case SQLPARSER_DIALECT_VASTBASE_ORACLE:
			return "vastbase-oracle";
		case SQLPARSER_DIALECT_VASTBASE_MYSQL:
			return "vastbase-mysql";
		case SQLPARSER_DIALECT_VASTBASE_POSTGRESQL:
			return "vastbase-postgresql";
		case SQLPARSER_DIALECT_VASTBASE_SQLSERVER:
			return "vastbase-sqlserver";
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

static int sqlparser_merge_condition_has_action_where(
	const PgQuery__Node *condition)
{
	size_t index;

	if (condition == NULL ||
	    condition->node_case != PG_QUERY__NODE__NODE_BOOL_EXPR ||
	    condition->bool_expr == NULL) {
		return 0;
	}
	if (condition->bool_expr->location ==
	    POSTGRES_DEPARSE_MERGE_ACTION_WHERE_LOCATION) {
		return 1;
	}
	for (index = 0U;
	     index < condition->bool_expr->n_args;
	     index++) {
		if (sqlparser_merge_condition_has_action_where(
			    condition->bool_expr->args != NULL ?
				    condition->bool_expr->args[index] :
				    NULL)) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_validate_merge_stmt(
	const PgQuery__MergeStmt *merge_stmt,
	sqlparser_dialect_t dialect,
	sqlparser_error_t *out_error)
{
	size_t insert_count;
	size_t when_index;

	if (merge_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	insert_count = 0U;
	for (when_index = 0U;
	     when_index < merge_stmt->n_merge_when_clauses;
	     when_index++) {
		PgQuery__Node *when_node;

		when_node = merge_stmt->merge_when_clauses != NULL ?
			merge_stmt->merge_when_clauses[when_index] :
			NULL;
		if (when_node != NULL &&
		    when_node->node_case ==
			    PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE &&
		    when_node->merge_when_clause != NULL) {
			PgQuery__Node *condition;
			PgQuery__MergeWhenClause *when_clause;

			when_clause = when_node->merge_when_clause;
			if (when_clause->command_type ==
			    PG_QUERY__CMD_TYPE__CMD_INSERT) {
				insert_count++;
			}
			condition = when_clause->condition;
			if (dialect != SQLPARSER_DIALECT_ORACLE &&
			    dialect != SQLPARSER_DIALECT_DAMENG &&
			    dialect != SQLPARSER_DIALECT_VASTBASE_ORACLE &&
			    sqlparser_merge_condition_has_action_where(
				    condition)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"MERGE action WHERE is not supported for this dialect");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			if (when_clause->delete_condition != NULL &&
			    (dialect != SQLPARSER_DIALECT_ORACLE &&
			     dialect != SQLPARSER_DIALECT_DAMENG)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"MERGE UPDATE DELETE WHERE is not supported for this dialect");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			if (when_clause->delete_condition != NULL &&
			    (when_clause->command_type !=
				     PG_QUERY__CMD_TYPE__CMD_UPDATE ||
			     when_clause->match_kind !=
				     PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_MATCHED)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"MERGE DELETE WHERE requires a matched UPDATE action");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
		}
	}
	if (dialect != SQLPARSER_DIALECT_POSTGRESQL &&
	    insert_count > 1U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"multiple MERGE INSERT actions are not supported for this dialect");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return SQLPARSER_STATUS_OK;
}

typedef enum {
	SQLPARSER_HIERARCHY_EXPRESSION_NONE = 0,
	SQLPARSER_HIERARCHY_EXPRESSION_TARGET,
	SQLPARSER_HIERARCHY_EXPRESSION_CONNECT_BY
} sqlparser_hierarchy_expression_context_t;

static int sqlparser_dialect_supports_connect_by(
	sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_ORACLE ||
		dialect == SQLPARSER_DIALECT_DAMENG ||
		dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
}

static int sqlparser_dialect_supports_hierarchy_operators(
	sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_ORACLE ||
		dialect == SQLPARSER_DIALECT_DAMENG;
}

static sqlparser_status_t sqlparser_vastbase_connect_by_root_target(
	const sqlparser_handle_t *handle,
	const PgQuery__ResTarget *target,
	int *out_unsupported,
	sqlparser_error_t *out_error)
{
	static const char keyword[] = "connect_by_root";
	const PgQuery__ColumnRef *column_ref;
	const char *name;
	char *spelling;
	size_t index;
	size_t spelling_length;
	int alias_known;
	int explicit_as;
	int quoted;
	sqlparser_status_t status;

	*out_unsupported = 0;
	if (target == NULL || target->name == NULL || target->name[0] == '\0' ||
	    target->val == NULL ||
	    target->val->node_case != PG_QUERY__NODE__NODE_COLUMN_REF) {
		return SQLPARSER_STATUS_OK;
	}
	column_ref = target->val->column_ref;
	if (column_ref == NULL || column_ref->n_fields != 1U ||
	    column_ref->fields == NULL ||
	    !sqlparser_node_string_value(column_ref->fields[0], &name) ||
	    name == NULL || strlen(name) != sizeof(keyword) - 1U) {
		return SQLPARSER_STATUS_OK;
	}
	for (index = 0U; index < sizeof(keyword) - 1U; index++) {
		if (tolower((unsigned char)name[index]) != keyword[index]) {
			return SQLPARSER_STATUS_OK;
		}
	}
	spelling = NULL;
	status = sqlparser_resolve_identifier_component_spelling(
		handle,
		column_ref->location,
		0U,
		name,
		&spelling,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	spelling_length = strlen(spelling);
	quoted = spelling[0] == '"' || spelling[0] == '`' ||
		spelling[0] == '[' ||
		(spelling_length > 2U &&
		 (spelling[0] == 'U' || spelling[0] == 'u') &&
		 spelling[1] == '&' && spelling[2] == '"');
	free(spelling);
	if (quoted) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_res_target_alias_style(
		handle,
		target,
		&alias_known,
		&explicit_as,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!alias_known) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"CONNECT_BY_ROOT target alias style is unavailable");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_unsupported = !explicit_as;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_validate_hierarchy_select(
	const PgQuery__SelectStmt *select_stmt,
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *target_node;
	size_t target_index;
	int unsupported;
	sqlparser_dialect_t dialect;
	sqlparser_status_t status;
	int has_connect_by;
	int has_hierarchy;

	if (select_stmt == NULL || handle == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	dialect = handle->dialect;
	has_connect_by = select_stmt->connect_by_clause != NULL;
	has_hierarchy = has_connect_by ||
		select_stmt->start_with_clause != NULL ||
		select_stmt->connect_by_no_cycle ||
		select_stmt->connect_by_first;
	if (has_hierarchy && !sqlparser_dialect_supports_connect_by(dialect)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"hierarchical query is not supported for this dialect");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if ((select_stmt->start_with_clause != NULL ||
	     select_stmt->connect_by_no_cycle) &&
	    !has_connect_by) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"hierarchical query modifier requires CONNECT BY");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (select_stmt->connect_by_first &&
	    (dialect != SQLPARSER_DIALECT_DAMENG ||
	     select_stmt->start_with_clause == NULL ||
	     !has_connect_by)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"CONNECT BY before START WITH is only supported for Dameng");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER &&
	    (select_stmt->start_with_clause != NULL ||
	     select_stmt->connect_by_no_cycle)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"Vastbase SQL Server mode supports CONNECT BY only");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER &&
	    has_connect_by) {
		for (target_index = 0U;
		     select_stmt->target_list != NULL &&
		     target_index < select_stmt->n_target_list;
		     target_index++) {
			target_node = select_stmt->target_list[target_index];
			if (target_node == NULL ||
			    target_node->node_case !=
				    PG_QUERY__NODE__NODE_RES_TARGET) {
				continue;
			}
			status = sqlparser_vastbase_connect_by_root_target(
				handle,
				target_node->res_target,
				&unsupported,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (unsupported) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"CONNECT_BY_ROOT is not supported for this dialect");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_validate_hierarchy_operator(
	const PgQuery__AExpr *a_expr,
	sqlparser_dialect_t dialect,
	int hierarchical_select,
	sqlparser_hierarchy_expression_context_t expression_context,
	sqlparser_error_t *out_error)
{
	const char *operator_name;
	int is_connect_by_root;
	int is_prior;

	operator_name = sqlparser_a_expr_operator_name(a_expr);
	is_prior = operator_name != NULL &&
		strcmp(operator_name, "PRIOR") == 0;
	is_connect_by_root = operator_name != NULL &&
		strcmp(operator_name, "CONNECT_BY_ROOT") == 0;
	if (!is_prior && !is_connect_by_root) {
		return SQLPARSER_STATUS_OK;
	}
	if (a_expr->kind != PG_QUERY__A__EXPR__KIND__AEXPR_OP ||
	    a_expr->n_name != 1U || a_expr->name == NULL ||
	    a_expr->lexpr != NULL || a_expr->rexpr == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"hierarchical operator has an invalid AST shape");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (!sqlparser_dialect_supports_hierarchy_operators(dialect)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"hierarchical operator is not supported for this dialect");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (!hierarchical_select ||
	    (is_prior &&
	     expression_context != SQLPARSER_HIERARCHY_EXPRESSION_CONNECT_BY) ||
	    (is_connect_by_root &&
	     expression_context != SQLPARSER_HIERARCHY_EXPRESSION_TARGET)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			is_prior ?
				"PRIOR is only valid in the current CONNECT BY clause" :
				"CONNECT_BY_ROOT is only valid in the current hierarchical SELECT list");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_validate_dialect_message(
	const ProtobufCMessage *message,
	const sqlparser_handle_t *handle,
	int hierarchical_select,
	sqlparser_hierarchy_expression_context_t expression_context,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	const uint8_t *base;
	unsigned field_index;
	sqlparser_status_t status;

	if (message == NULL || message->descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (message->descriptor == &pg_query__merge_stmt__descriptor) {
		status = sqlparser_validate_merge_stmt(
			(const PgQuery__MergeStmt *)message,
			handle->dialect,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (message->descriptor == &pg_query__select_stmt__descriptor) {
		const PgQuery__SelectStmt *select_stmt;

		select_stmt = (const PgQuery__SelectStmt *)message;
		status = sqlparser_validate_hierarchy_select(
			select_stmt,
			handle,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		hierarchical_select = select_stmt->connect_by_clause != NULL;
		expression_context = SQLPARSER_HIERARCHY_EXPRESSION_NONE;
	} else if (message->descriptor == &pg_query__a__expr__descriptor) {
		status = sqlparser_validate_hierarchy_operator(
			(const PgQuery__AExpr *)message,
			handle->dialect,
			hierarchical_select,
			expression_context,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	descriptor = message->descriptor;
	base = (const uint8_t *)message;
	for (field_index = 0U;
	     field_index < descriptor->n_fields;
	     field_index++) {
		const ProtobufCFieldDescriptor *field;
		sqlparser_hierarchy_expression_context_t child_context;

		field = &descriptor->fields[field_index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE ||
		    ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		     *(const int *)(base + field->quantifier_offset) !=
			     (int)field->id)) {
			continue;
		}
		child_context = expression_context;
		if (descriptor == &pg_query__select_stmt__descriptor) {
			if (strcmp(field->name, "target_list") == 0) {
				child_context =
					SQLPARSER_HIERARCHY_EXPRESSION_TARGET;
			} else if (strcmp(field->name, "connect_by_clause") == 0) {
				child_context =
					SQLPARSER_HIERARCHY_EXPRESSION_CONNECT_BY;
			} else {
				child_context =
					SQLPARSER_HIERARCHY_EXPRESSION_NONE;
			}
		}
		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage *const *items;
			size_t item_index;

			item_count =
				*(const size_t *)(base + field->quantifier_offset);
			items =
				*(ProtobufCMessage *const *const *)
					(base + field->offset);
			for (item_index = 0U;
			     items != NULL && item_index < item_count;
			     item_index++) {
				status = sqlparser_validate_dialect_message(
					items[item_index],
					handle,
					hierarchical_select,
					child_context,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
		} else {
			status = sqlparser_validate_dialect_message(
				*(ProtobufCMessage *const *)
					(base + field->offset),
				handle,
				hierarchical_select,
				child_context,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_validate_dialect_statements(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	if (handle == NULL ||
	    handle->ast == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	return sqlparser_validate_dialect_message(
		(const ProtobufCMessage *)handle->ast,
		handle,
		0,
		SQLPARSER_HIERARCHY_EXPRESSION_NONE,
		out_error);
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
	sqlparser_control_state_t *control_state;

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
	control_state = NULL;

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
	parse_result =
		sqlparser_parse_protobuf_preserving_identifier_spelling(
			parser_sql);
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

	handle->parser_sql = parser_sql;
	parser_sql = NULL;
	if (strcmp(sql, handle->parser_sql) == 0) {
		handle->sql = handle->parser_sql;
	} else {
		handle->sql = sqlparser_strdup(sql);
		if (handle->sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			pg_query_free_protobuf_parse_result(parse_result);
			if (dialect_ops->destroy_state != NULL && dialect_state != NULL) {
				dialect_ops->destroy_state(dialect_state);
			}
			free(handle->parser_sql);
			free(handle);
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	handle->sql_len = sql_len;
	handle->parser_sql_len = parser_sql_len;
	handle->limits = effective_options.limits;
	handle->dialect = effective_options.dialect;
	handle->dialect_ops = dialect_ops;
	handle->dialect_state = dialect_state;
	dialect_state = NULL;
	handle->parse_tree = parse_result.parse_tree;
	parse_result.parse_tree.data = NULL;
	parse_result.parse_tree.len = 0U;

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
	status = sqlparser_validate_dialect_statements(
		handle,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_protobuf_parse_result(parse_result);
		sqlparser_handle_destroy(handle);
		return status;
	}
	status = sqlparser_validate_statement_count_limit(&handle->limits, handle->statement_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		pg_query_free_protobuf_parse_result(parse_result);
		sqlparser_handle_destroy(handle);
		return status;
	}
	if (dialect_ops->take_control_state != NULL) {
		control_state = dialect_ops->take_control_state(handle->dialect_state);
		if (control_state != NULL) {
			status = sqlparser_control_state_attach(handle, control_state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_control_state_release(control_state);
				pg_query_free_protobuf_parse_result(parse_result);
				sqlparser_handle_destroy(handle);
				return status;
			}
			control_state = NULL;
		}
	}
	sqlparser_handle_clear_ast(handle);

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
	sqlparser_handle_t *mutable_handle;
	char *public_sql;
	sqlparser_status_t status;
	int clear_ast_after_deparse;

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
	if (handle->generation == 0UL) {
		status = sqlparser_validate_handle_output_text(
			handle,
			handle->sql,
			"deparse output",
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*out_sql = sqlparser_strdup(handle->sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (handle->control != NULL || handle->current_sql != NULL) {
		status = sqlparser_ensure_current_sql_text(handle, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*out_sql = sqlparser_strdup(handle->current_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (handle->surface_source_complete) {
		status = sqlparser_restore_source_envelope(
			handle,
			out_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_validate_handle_output_text(
			handle,
			*out_sql,
			"deparse output",
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(*out_sql);
			*out_sql = NULL;
		}
		return status;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	clear_ast_after_deparse = 0;
	public_sql = NULL;
	sqlparser_pg_query_prepare();
	deparse_result = sqlparser_deparse_protobuf_for_handle(
		handle,
		handle->parse_tree,
		0U,
		0U,
		handle->parser_sql_len);
	if (deparse_result.error != NULL || deparse_result.query == NULL) {
		if (deparse_result.error != NULL) {
			sqlparser_error_from_pg(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				handle->parser_sql,
				deparse_result.error);
		} else {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"failed to deparse SQL");
		}
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		goto cleanup;
	}

	status = sqlparser_restore_semantic_comments(
		handle,
		&deparse_result.query,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto cleanup;
	}
	status = sqlparser_validate_handle_output_text(
		handle,
		deparse_result.query,
		"deparse output",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto cleanup;
	}
	if (handle->dialect_ops == NULL ||
	    handle->dialect_ops->postprocess_deparse == NULL) {
		status = sqlparser_restore_source_envelope(
			handle,
			&deparse_result.query,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			*out_sql = deparse_result.query;
			deparse_result.query = NULL;
		}
		goto cleanup;
	}

	if (handle->ast == NULL &&
	    handle->dialect_ops->bind_ast_state != NULL) {
		status = sqlparser_handle_ensure_ast(mutable_handle, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto cleanup;
		}
		clear_ast_after_deparse = 1;
	}
	status = handle->dialect_ops->postprocess_deparse(
		deparse_result.query,
		handle->dialect_state,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto cleanup;
	}
	status = sqlparser_restore_source_envelope(
		handle,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto cleanup;
	}
	status = sqlparser_validate_handle_output_text(
		handle,
		public_sql,
		"deparse output",
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		*out_sql = public_sql;
		public_sql = NULL;
	}
cleanup:
	free(public_sql);
	pg_query_free_deparse_result(deparse_result);
	if (clear_ast_after_deparse) {
		sqlparser_handle_clear_ast(mutable_handle);
	}
	return status;
}

void sqlparser_string_free(char *text)
{
	free(text);
}
