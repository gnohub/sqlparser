#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_ast_surface_internal.h"
#include "sqlparser_dialect_bind_internal.h"
#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_dameng_internal.h"
#include "sqlparser_dialect_minus_internal.h"
#include "sqlparser_dialect_national_literal_internal.h"

typedef struct {
	char *clause;
	size_t limit_ordinal;
	PgQuery__SelectStmt *owner;
} sqlparser_dameng_top_restore_t;

typedef struct {
	char *parser_object_name;
	char *public_object_name;
	char *public_link_name;
	char *public_object_sql;
	char *public_link_sql;
	PgQuery__RangeVar *owner;
} sqlparser_dameng_dblink_relation_t;

typedef struct {
	char **bind_names;
	size_t bind_count;
	size_t bind_capacity;
	sqlparser_dialect_prepared_binds_t prepared_binds;
	sqlparser_dameng_top_restore_t *top_restores;
	size_t top_count;
	size_t top_capacity;
	size_t top_fragment_start;
	size_t fragment_limit_base;
	sqlparser_dialect_national_literals_t national_literals;
	size_t limit_count;
	size_t bind_occurrence_count;
	sqlparser_dialect_minuses_t minuses;
	sqlparser_dialect_multi_insert_t *multi_insert;
	sqlparser_dameng_dblink_relation_t *dblink_relations;
	size_t dblink_count;
	size_t dblink_capacity;
	size_t next_dblink_id;
	sqlparser_dialect_returning_into_state_t returning_into;
} sqlparser_dameng_state_t;

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
	const char *origin_input;
	size_t origin_input_length;
	size_t origin_input_base;
	sqlparser_identifier_origin_writer_t origin_writer;
	size_t pending_input_offset;
	size_t pending_input_length;
} sqlparser_dameng_buffer_t;

typedef struct {
	size_t first_start;
	size_t first_end;
	size_t second_start;
	size_t second_end;
	int valid;
} sqlparser_dameng_limit_origin_t;

typedef struct {
	char *limit;
	char *top_clause;
	sqlparser_dameng_limit_origin_t origin;
	size_t depth;
} sqlparser_dameng_pending_top_t;

typedef struct {
	sqlparser_dameng_pending_top_t *items;
	size_t count;
	size_t capacity;
} sqlparser_dameng_pending_tops_t;

static int sqlparser_dameng_is_multi_insert_start(
	const char *sql,
	sqlparser_dialect_multi_insert_mode_t *out_mode);
static sqlparser_status_t sqlparser_dameng_parse_multi_insert(
	const char *input_sql,
	sqlparser_dameng_state_t *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

static void sqlparser_dameng_dblink_relation_clear(
	sqlparser_dameng_dblink_relation_t *relation)
{
	if (relation == NULL) {
		return;
	}
	free(relation->parser_object_name);
	free(relation->public_object_name);
	free(relation->public_link_name);
	free(relation->public_object_sql);
	free(relation->public_link_sql);
	memset(relation, 0, sizeof(*relation));
}

static void sqlparser_dameng_buffer_release(sqlparser_dameng_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}
	free(buffer->data);
	sqlparser_identifier_origin_writer_release(&buffer->origin_writer);
	memset(buffer, 0, sizeof(*buffer));
}

static sqlparser_status_t sqlparser_dameng_buffer_begin_origin(
	sqlparser_dameng_buffer_t *buffer,
	const char *input,
	sqlparser_identifier_origin_map_t *origins,
	size_t map_input_length,
	size_t input_base,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t input_length;

	if (origins == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (buffer == NULL || input == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"Dameng identifier origin input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	input_length = strlen(input);
	if (input_base > map_input_length ||
	    input_length > map_input_length - input_base) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"Dameng identifier origin input span is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	status = sqlparser_identifier_origin_writer_begin(
		&buffer->origin_writer,
		origins,
		map_input_length,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->origin_input = input;
	buffer->origin_input_length = input_length;
	buffer->origin_input_base = input_base;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_buffer_flush_input_origin(
	sqlparser_dameng_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL ||
	    buffer->pending_input_length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origin_writer_append_input(
		&buffer->origin_writer,
		buffer->pending_input_offset,
		buffer->pending_input_length,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		buffer->pending_input_length = 0U;
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_buffer_commit_origin(
	sqlparser_dameng_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_buffer_flush_input_origin(
		buffer,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_identifier_origin_writer_commit(
		&buffer->origin_writer,
		buffer->len,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		buffer->origin_input = NULL;
		buffer->origin_input_length = 0U;
		buffer->origin_input_base = 0U;
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_buffer_reserve(
	sqlparser_dameng_buffer_t *buffer,
	size_t extra,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t required;
	size_t next_capacity;

	if (buffer == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (extra > ((size_t)-1) - buffer->len - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	required = buffer->len + extra + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next_capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
	while (next_capacity < required) {
		if (next_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}

	next = (char *)realloc(buffer->data, next_capacity);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = next_capacity;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_buffer_append_raw_mem(
	sqlparser_dameng_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "append data must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_dameng_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_buffer_append_mem(
	sqlparser_dameng_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		status = sqlparser_dameng_buffer_flush_input_origin(
			buffer,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_identifier_origin_writer_append_unknown(
			&buffer->origin_writer,
			len,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_dameng_buffer_append_raw_mem(
		buffer,
		data,
		len,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_input_mem(
	sqlparser_dameng_buffer_t *buffer,
	const char *input,
	size_t input_offset,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t map_offset;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		if (input != buffer->origin_input ||
		    input_offset > buffer->origin_input_length ||
		    len > buffer->origin_input_length - input_offset) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"Dameng identifier origin input changed during preprocess");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		map_offset = buffer->origin_input_base + input_offset;
		if (buffer->pending_input_length > 0U &&
		    map_offset ==
			    buffer->pending_input_offset +
				    buffer->pending_input_length) {
			if (len > SIZE_MAX - buffer->pending_input_length) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_RESOURCE_LIMIT,
					"Dameng identifier origin span is too large");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			buffer->pending_input_length += len;
		} else {
			status = sqlparser_dameng_buffer_flush_input_origin(
				buffer,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			buffer->pending_input_offset = map_offset;
			buffer->pending_input_length = len;
		}
	}
	return sqlparser_dameng_buffer_append_raw_mem(
		buffer,
		input != NULL ? input + input_offset : NULL,
		len,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_source_identifier(
	sqlparser_dameng_buffer_t *buffer,
	const char *input,
	size_t input_offset,
	size_t input_length,
	const char *output,
	size_t output_length,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		if (input != buffer->origin_input ||
		    input_offset > buffer->origin_input_length ||
		    input_length >
			    buffer->origin_input_length - input_offset) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"Dameng source identifier span is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = sqlparser_dameng_buffer_flush_input_origin(
			buffer,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status =
			sqlparser_identifier_origin_writer_append_source_identifier(
				&buffer->origin_writer,
				buffer->origin_input_base + input_offset,
				input_length,
				output_length,
				out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_dameng_buffer_append_raw_mem(
		buffer,
		output,
		output_length,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_record_generated_identifier(
	sqlparser_dameng_buffer_t *buffer,
	size_t length,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_buffer_flush_input_origin(
		buffer,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_identifier_origin_writer_append_generated_identifier(
		&buffer->origin_writer,
		length,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_generated_identifier(
	sqlparser_dameng_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t length;

	length = text != NULL ? strlen(text) : 0U;
	status = sqlparser_dameng_buffer_record_generated_identifier(
		buffer,
		length,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_dameng_buffer_append_raw_mem(
		buffer,
		text,
		length,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_input_char(
	sqlparser_dameng_buffer_t *buffer,
	const char *input,
	size_t input_offset,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_append_input_mem(
		buffer,
		input,
		input_offset,
		1U,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_char(
	sqlparser_dameng_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_cstr(
	sqlparser_dameng_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_finish(
	sqlparser_dameng_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_reserve(buffer, 0U, out_error);
}

static char *sqlparser_dameng_buffer_take(sqlparser_dameng_buffer_t *buffer)
{
	char *data;

	if (buffer == NULL) {
		return NULL;
	}
	data = buffer->data;
	memset(buffer, 0, sizeof(*buffer));
	return data;
}

static void sqlparser_dameng_relation_clear(sqlparser_dialect_multi_insert_relation_t *relation)
{
	if (relation == NULL) {
		return;
	}
	free(relation->database_name);
	free(relation->schema_name);
	free(relation->table_name);
	free(relation->sql);
	memset(relation, 0, sizeof(*relation));
}

static void sqlparser_dameng_column_clear(sqlparser_dialect_multi_insert_column_t *column)
{
	if (column == NULL) {
		return;
	}
	free(column->name);
	free(column->sql);
	memset(column, 0, sizeof(*column));
}

static void sqlparser_dameng_value_clear(sqlparser_dialect_multi_insert_value_t *value)
{
	if (value == NULL) {
		return;
	}
	free(value->public_sql);
	free(value->parser_sql);
	free(value->literal_string_value);
	free(value->literal_float_value);
	memset(value, 0, sizeof(*value));
}

static void sqlparser_dameng_multi_insert_branch_clear(sqlparser_dialect_multi_insert_branch_t *branch)
{
	size_t index;

	if (branch == NULL) {
		return;
	}
	sqlparser_dameng_relation_clear(&branch->relation);
	for (index = 0U; index < branch->column_count; index++) {
		sqlparser_dameng_column_clear(&branch->columns[index]);
	}
	free(branch->columns);
	for (index = 0U; index < branch->cell_count; index++) {
		sqlparser_dameng_value_clear(&branch->cells[index]);
	}
	free(branch->cells);
	free(branch->condition_public_sql);
	free(branch->condition_parser_sql);
	memset(branch, 0, sizeof(*branch));
}

static void sqlparser_dameng_multi_insert_destroy(sqlparser_dialect_multi_insert_t *multi)
{
	size_t branch_index;

	if (multi == NULL) {
		return;
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		sqlparser_dameng_multi_insert_branch_clear(&multi->branches[branch_index]);
	}
	free(multi->branches);
	free(multi->source_public_sql);
	free(multi->source_parser_sql);
	free(multi);
}

static sqlparser_status_t sqlparser_dameng_buffer_reserve_input(
	sqlparser_dameng_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	size_t len;

	len = input != NULL ? strlen(input) : 0U;
	return sqlparser_dameng_buffer_reserve(buffer, len, out_error);
}

static void sqlparser_dameng_state_destroy(void *state)
{
	sqlparser_dameng_state_t *dameng_state;
	size_t index;

	dameng_state = (sqlparser_dameng_state_t *)state;
	if (dameng_state == NULL) {
		return;
	}
	for (index = 0U; index < dameng_state->bind_count; index++) {
		free(dameng_state->bind_names[index]);
	}
	for (index = 0U; index < dameng_state->top_count; index++) {
		free(dameng_state->top_restores[index].clause);
	}
	for (index = 0U; index < dameng_state->dblink_count; index++) {
		sqlparser_dameng_dblink_relation_clear(
			&dameng_state->dblink_relations[index]);
	}
	free(dameng_state->bind_names);
	sqlparser_dialect_prepared_binds_clear(
		&dameng_state->prepared_binds);
	free(dameng_state->top_restores);
	sqlparser_dialect_national_literals_clear(
		&dameng_state->national_literals);
	sqlparser_dialect_minuses_clear(&dameng_state->minuses);
	free(dameng_state->dblink_relations);
	sqlparser_dameng_multi_insert_destroy(dameng_state->multi_insert);
	sqlparser_dialect_returning_into_state_clear(
		&dameng_state->returning_into);
	free(dameng_state);
}

static sqlparser_status_t sqlparser_dameng_state_new(
	sqlparser_dameng_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	state = (sqlparser_dameng_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_store_national_literal(
	sqlparser_dameng_state_t *state,
	const char *literal,
	size_t len,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	return state != NULL ?
		sqlparser_dialect_national_literals_append(
			&state->national_literals,
			literal,
			len,
			ordinal,
			out_error) :
		SQLPARSER_STATUS_INVALID_ARGUMENT;
}

static int sqlparser_dameng_national_literal_matches(
	const sqlparser_dameng_state_t *state,
	size_t ordinal,
	const char *sql,
	size_t start,
	size_t end)
{
	return state != NULL &&
		sqlparser_dialect_national_literals_match(
			&state->national_literals,
			ordinal,
			sql,
			start,
			end);
}

static int sqlparser_dameng_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_';
}

static int sqlparser_dameng_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#';
}

static int sqlparser_dameng_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_dameng_is_ident_char(prev) && !sqlparser_dameng_is_ident_char(next);
}

static int sqlparser_dameng_ascii_word_equal(const char *text, size_t pos, const char *word)
{
	size_t index;
	size_t len;

	if (text == NULL || word == NULL) {
		return 0;
	}
	len = strlen(word);
	for (index = 0U; index < len; index++) {
		if (text[pos + index] == '\0' ||
		    tolower((unsigned char)text[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return sqlparser_dameng_is_word_boundary(text, pos, len);
}

static size_t sqlparser_dameng_skip_trivia(const char *text, size_t pos);

static const char *sqlparser_dameng_hierarchy_internal_keyword(
	const char *input,
	size_t pos,
	size_t *out_source_length)
{
	size_t next;

	if (sqlparser_dameng_ascii_word_equal(input, pos, "connect_by_root")) {
		*out_source_length = strlen("connect_by_root");
		return SQLPARSER_INTERNAL_HIERARCHY_CONNECT_BY_ROOT;
	}
	if (sqlparser_dameng_ascii_word_equal(input, pos, "connect")) {
		next = sqlparser_dameng_skip_trivia(
			input,
			pos + strlen("connect"));
		if (sqlparser_dameng_ascii_word_equal(input, next, "by")) {
			*out_source_length = strlen("connect");
			return SQLPARSER_INTERNAL_HIERARCHY_CONNECT_BY;
		}
	}
	if (sqlparser_dameng_ascii_word_equal(input, pos, "start")) {
		next = sqlparser_dameng_skip_trivia(
			input,
			pos + strlen("start"));
		if (sqlparser_dameng_ascii_word_equal(input, next, "with")) {
			*out_source_length = strlen("start");
			return SQLPARSER_INTERNAL_HIERARCHY_START_WITH;
		}
	}
	if (sqlparser_dameng_ascii_word_equal(input, pos, "nocycle")) {
		*out_source_length = strlen("nocycle");
		return SQLPARSER_INTERNAL_HIERARCHY_NOCYCLE;
	}
	if (sqlparser_dameng_ascii_word_equal(input, pos, "prior")) {
		*out_source_length = strlen("prior");
		return SQLPARSER_INTERNAL_HIERARCHY_PRIOR;
	}
	*out_source_length = 0U;
	return NULL;
}

static int sqlparser_dameng_ascii_span_equal(const char *text, size_t start, size_t end, const char *word)
{
	size_t index;
	size_t len;

	if (text == NULL || word == NULL || end < start) {
		return 0;
	}
	len = strlen(word);
	if (end - start != len) {
		return 0;
	}
	for (index = 0U; index < len; index++) {
		if (tolower((unsigned char)text[start + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_dameng_skip_space(const char *text, size_t pos)
{
	while (text[pos] != '\0' && isspace((unsigned char)text[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_dameng_skip_quoted_or_comment_span(const char *sql, size_t index);

static size_t sqlparser_dameng_skip_trivia(const char *text, size_t pos)
{
	size_t next;

	for (;;) {
		pos = sqlparser_dameng_skip_space(text, pos);
		if (!((text[pos] == '-' && text[pos + 1U] == '-') ||
		      (text[pos] == '/' && text[pos + 1U] == '*'))) {
			return pos;
		}
		next = sqlparser_dameng_skip_quoted_or_comment_span(text, pos);
		if (next == pos) {
			return pos;
		}
		pos = next;
	}
}

static size_t sqlparser_dameng_trim_right(const char *text, size_t start, size_t end)
{
	while (end > start && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	return end;
}

static size_t sqlparser_dameng_q_quote_prefix_len(const char *text)
{
	if (text == NULL) {
		return 0U;
	}
	if ((text[0] == 'q' || text[0] == 'Q') && text[1] == '\'') {
		return 1U;
	}
	if ((text[0] == 'n' || text[0] == 'N') &&
	    (text[1] == 'q' || text[1] == 'Q') &&
	    text[2] == '\'') {
		return 2U;
	}
	return 0U;
}

static int sqlparser_dameng_is_n_string_literal(const char *text)
{
	return text != NULL &&
		(text[0] == 'n' || text[0] == 'N') &&
		text[1] == '\'';
}

static char sqlparser_dameng_q_quote_close_char(char open_char)
{
	switch (open_char) {
		case '[':
			return ']';
		case '{':
			return '}';
		case '(':
			return ')';
		case '<':
			return '>';
		default:
			return open_char;
	}
}

static void sqlparser_dameng_scan_previous_significant(
	const char *text,
	size_t end,
	size_t *scan_pos,
	size_t *last_pos)
{
	size_t next;
	size_t prefix_len;

	while (*scan_pos < end) {
		if (isspace((unsigned char)text[*scan_pos])) {
			(*scan_pos)++;
			continue;
		}
		prefix_len = sqlparser_dameng_q_quote_prefix_len(
			text + *scan_pos);
		if (prefix_len > 0U) {
			char close_char;

			next = *scan_pos + prefix_len + 2U;
			close_char = sqlparser_dameng_q_quote_close_char(
				text[next - 1U]);
			while (next < end &&
			       !(text[next] == close_char &&
			         text[next + 1U] == '\'')) {
				next++;
			}
			if (next < end) {
				next += 2U;
			}
			*last_pos = *scan_pos;
			*scan_pos = next;
			continue;
		}
		next = sqlparser_dameng_skip_quoted_or_comment_span(
			text,
			*scan_pos);
		if (next != *scan_pos) {
			if (text[*scan_pos] == '\'' || text[*scan_pos] == '"') {
				*last_pos = *scan_pos;
			}
			*scan_pos = next;
			continue;
		}
		*last_pos = *scan_pos;
		(*scan_pos)++;
	}
}

static int sqlparser_dameng_copy_q_quote(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t prefix_len;
	size_t pos;
	char close_char;

	prefix_len = sqlparser_dameng_q_quote_prefix_len(input + *index);
	if (prefix_len == 0U) {
		return 0;
	}
	pos = *index;
	if (sqlparser_dameng_buffer_append_char(out, '\'', out_error) != SQLPARSER_STATUS_OK) {
		return -1;
	}
	pos += prefix_len + 2U;
	close_char = sqlparser_dameng_q_quote_close_char(input[pos - 1U]);
	while (input[pos] != '\0') {
		if (input[pos] == close_char && input[pos + 1U] == '\'') {
			if (sqlparser_dameng_buffer_append_char(out, '\'', out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			*index = pos + 2U;
			return 1;
		}
		if (input[pos] == '\'' &&
		    sqlparser_dameng_buffer_append_char(out, '\'', out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (sqlparser_dameng_buffer_append_char(out, input[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		pos++;
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng q-quoted string literal");
	return -1;
}

static int sqlparser_dameng_copy_quoted_or_comment(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	if (input == NULL || index == NULL || out == NULL) {
		return 0;
	}

	if (input[*index] == '-' && input[*index + 1U] == '-') {
		while (input[*index] != '\0') {
			if (sqlparser_dameng_buffer_append_input_char(
				    out,
				    input,
				    *index,
				    out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (input[*index] == '\n') {
				(*index)++;
				break;
			}
			(*index)++;
		}
		return 1;
	}

	if (input[*index] == '/' && input[*index + 1U] == '*') {
		if (sqlparser_dameng_buffer_append_input_mem(
			    out,
			    input,
			    *index,
			    2U,
			    out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		*index += 2U;
		while (input[*index] != '\0') {
			if (input[*index] == '*' && input[*index + 1U] == '/') {
				if (sqlparser_dameng_buffer_append_input_mem(
					    out,
					    input,
					    *index,
					    2U,
					    out_error) != SQLPARSER_STATUS_OK) {
					return -1;
				}
				*index += 2U;
				return 1;
			}
			if (sqlparser_dameng_buffer_append_input_char(
				    out,
				    input,
				    *index,
				    out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			(*index)++;
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng block comment");
		return -1;
	}

	if (sqlparser_dameng_q_quote_prefix_len(input + *index) > 0U) {
		return sqlparser_dameng_copy_q_quote(input, index, out, out_error);
	}

	if (input[*index] == 'N' || input[*index] == 'n') {
		if (input[*index + 1U] != '\'') {
			return 0;
		}
		if (sqlparser_dameng_buffer_append_input_char(
			    out,
			    input,
			    *index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		(*index)++;
	}

	if (input[*index] != '\'' && input[*index] != '"') {
		return 0;
	}

	quote = input[*index];
	pos = *index;
	pos++;
	while (input[pos] != '\0') {
		if (input[pos] == quote) {
			if (input[pos + 1U] == quote) {
				pos += 2U;
				continue;
			} else {
				pos++;
				if (quote == '"') {
					if (sqlparser_dameng_buffer_append_source_identifier(
						    out,
						    input,
						    *index,
						    pos - *index,
						    input + *index,
						    pos - *index,
						    out_error) !=
					    SQLPARSER_STATUS_OK) {
						return -1;
					}
				} else if (sqlparser_dameng_buffer_append_input_mem(
						   out,
						   input,
						   *index,
						   pos - *index,
						   out_error) !=
					   SQLPARSER_STATUS_OK) {
					return -1;
				}
				*index = pos;
				return 1;
			}
		}
		pos++;
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng quoted literal");
	return -1;
}

static sqlparser_status_t sqlparser_dameng_copy_n_string_literal(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t literal_index;
	int copied;

	if (!sqlparser_dameng_is_n_string_literal(input + *index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid Dameng national string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	literal_index = *index + 1U;
	copied = sqlparser_dameng_copy_quoted_or_comment(input, &literal_index, out, out_error);
	if (copied < 0) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (copied == 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng national string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	*index = literal_index;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_state_find_or_add_bind(
	sqlparser_dameng_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char **next;
	char *name_copy;
	size_t index;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind state arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	for (index = 0U; index < state->bind_count; index++) {
		if (strlen(state->bind_names[index]) == len &&
		    strncmp(state->bind_names[index], name, len) == 0) {
			*out_param_index = index + 1U;
			return SQLPARSER_STATUS_OK;
		}
	}

	if (state->bind_count == state->bind_capacity) {
		next_capacity = state->bind_capacity == 0U ? 8U : state->bind_capacity * 2U;
		if (next_capacity < state->bind_capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(state->bind_names, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->bind_names = next;
		state->bind_capacity = next_capacity;
	}

	name_copy = sqlparser_strndup(name, len);
	if (name_copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->bind_names[state->bind_count] = name_copy;
	state->bind_count++;
	*out_param_index = state->bind_count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_state_append_bind(
	sqlparser_dameng_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char **next;
	char *name_copy;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind state arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (state->bind_count == state->bind_capacity) {
		next_capacity = state->bind_capacity == 0U ? 8U : state->bind_capacity * 2U;
		if (next_capacity < state->bind_capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(state->bind_names, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->bind_names = next;
		state->bind_capacity = next_capacity;
	}

	name_copy = sqlparser_strndup(name, len);
	if (name_copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->bind_names[state->bind_count] = name_copy;
	state->bind_count++;
	*out_param_index = state->bind_count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_state_append_top_clause(
	sqlparser_dameng_state_t *state,
	const char *text,
	size_t len,
	size_t limit_ordinal,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_top_restore_t *next;
	char *copy;
	size_t next_capacity;

	if (state == NULL || text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "TOP state arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state->top_count == state->top_capacity) {
		next_capacity = state->top_capacity == 0U ? 4U : state->top_capacity * 2U;
		if (next_capacity < state->top_capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_dameng_top_restore_t *)realloc(state->top_restores, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->top_restores = next;
		state->top_capacity = next_capacity;
	}

	copy = sqlparser_strndup(text, len);
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->top_restores[state->top_count].clause = copy;
	state->top_restores[state->top_count].limit_ordinal = limit_ordinal;
	state->top_restores[state->top_count].owner = NULL;
	state->top_count++;
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_dameng_identifier_token_end(const char *sql, size_t pos)
{
	if (sql == NULL || sql[pos] == '\0') {
		return pos;
	}
	if (sql[pos] == '"') {
		pos++;
		while (sql[pos] != '\0') {
			if (sql[pos] == '"' && sql[pos + 1U] == '"') {
				pos += 2U;
				continue;
			}
			if (sql[pos] == '"') {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}
	if (!sqlparser_dameng_is_ident_start((unsigned char)sql[pos])) {
		return pos;
	}
	pos++;
	while (sqlparser_dameng_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_dameng_identifier_unquote(
	const char *sql,
	size_t start,
	size_t end,
	char **out_name,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	size_t pos;

	if (out_name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "identifier output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_name = NULL;
	if (sql == NULL || start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "identifier must not be empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (sql[start] != '"') {
		*out_name = sqlparser_strndup(sql + start, end - start);
		if (*out_name == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	pos = start + 1U;
	while (pos + 1U <= end && sql[pos] != '\0') {
		if (sql[pos] == '"') {
			if (pos + 1U < end && sql[pos + 1U] == '"') {
				if (sqlparser_dameng_buffer_append_char(&out, '"', out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_buffer_release(&out);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
				pos += 2U;
				continue;
			}
			break;
		}
		if (sqlparser_dameng_buffer_append_char(&out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_dameng_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name = sqlparser_dameng_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dameng_dblink_relation_t *sqlparser_dameng_state_find_dblink_relation(
	const sqlparser_dameng_state_t *state,
	const char *parser_object_name)
{
	size_t index;

	if (state == NULL || parser_object_name == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->dblink_count; index++) {
		if (state->dblink_relations[index].parser_object_name != NULL &&
		    strcmp(state->dblink_relations[index].parser_object_name, parser_object_name) == 0) {
			return &state->dblink_relations[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_dameng_state_append_dblink_relation(
	sqlparser_dameng_state_t *state,
	const char *parser_object_name,
	const char *public_object_name,
	const char *public_link_name,
	const char *public_object_sql,
	size_t public_object_sql_len,
	const char *public_link_sql,
	size_t public_link_sql_len,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_dblink_relation_t copy;
	sqlparser_dameng_dblink_relation_t *next;
	sqlparser_dameng_dblink_relation_t *relation;
	size_t next_capacity;

	if (state == NULL || parser_object_name == NULL || public_object_name == NULL ||
	    public_link_name == NULL || public_object_sql == NULL || public_link_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "database link relation arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&copy, 0, sizeof(copy));
	copy.parser_object_name = sqlparser_strdup(parser_object_name);
	copy.public_object_name = sqlparser_strdup(public_object_name);
	copy.public_link_name = sqlparser_strdup(public_link_name);
	copy.public_object_sql = sqlparser_strndup(
		public_object_sql, public_object_sql_len);
	copy.public_link_sql = sqlparser_strndup(
		public_link_sql, public_link_sql_len);
	if (copy.parser_object_name == NULL || copy.public_object_name == NULL ||
	    copy.public_link_name == NULL || copy.public_object_sql == NULL ||
	    copy.public_link_sql == NULL) {
		sqlparser_dameng_dblink_relation_clear(&copy);
		sqlparser_error_set_message(
			out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (state->dblink_count == state->dblink_capacity) {
		next_capacity = state->dblink_capacity == 0U ? 4U : state->dblink_capacity * 2U;
		if (next_capacity < state->dblink_capacity) {
			sqlparser_dameng_dblink_relation_clear(&copy);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_dameng_dblink_relation_t *)realloc(
			state->dblink_relations,
			next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_dameng_dblink_relation_clear(&copy);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->dblink_relations = next;
		state->dblink_capacity = next_capacity;
	}

	relation = &state->dblink_relations[state->dblink_count];
	*relation = copy;
	state->dblink_count++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_public_identifier_is_empty(
	const char *sql,
	size_t start,
	size_t end)
{
	return sql == NULL || start >= end || (sql[start] == '"' && end == start + 2U);
}

static sqlparser_status_t sqlparser_dameng_try_copy_database_link_relation(
	const char *input_sql,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	char marker[64];
	char *object_name;
	char *link_name;
	size_t start;
	size_t pos;
	size_t part_start[4];
	size_t part_end[4];
	size_t part_count;
	size_t at_pos;
	size_t link_start;
	size_t link_end;
	int marker_len;
	sqlparser_status_t status;

	if (input_sql == NULL || index == NULL || out == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "database link scan arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = *index;
	pos = start;
	part_count = 0U;
	for (;;) {
		size_t ident_start;
		size_t ident_end;

		if (part_count >= sizeof(part_start) / sizeof(part_start[0])) {
			return SQLPARSER_STATUS_OK;
		}
		ident_start = pos;
		ident_end = sqlparser_dameng_identifier_token_end(input_sql, ident_start);
		if (ident_end == ident_start ||
		    sqlparser_dameng_public_identifier_is_empty(input_sql, ident_start, ident_end)) {
			return SQLPARSER_STATUS_OK;
		}
		part_start[part_count] = ident_start;
		part_end[part_count] = ident_end;
		part_count++;
		pos = ident_end;
		if (input_sql[pos] != '.') {
			break;
		}
		pos++;
	}
	if (input_sql[pos] != '@') {
		return SQLPARSER_STATUS_OK;
	}

	at_pos = pos;
	link_start = at_pos + 1U;
	link_end = sqlparser_dameng_identifier_token_end(input_sql, link_start);
	if (link_end == link_start ||
	    sqlparser_dameng_public_identifier_is_empty(input_sql, link_start, link_end)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid Dameng database link name");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	if (state->next_dblink_id == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"database link marker range is exhausted");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	state->next_dblink_id++;
	marker_len = snprintf(
		marker,
		sizeof(marker),
		"sqlparser_dameng_dblink_%zu",
		state->next_dblink_id);
	if (marker_len <= 0 || (size_t)marker_len >= sizeof(marker)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "database link marker is too long");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	object_name = NULL;
	link_name = NULL;
	status = sqlparser_dameng_identifier_unquote(
		input_sql,
		part_start[part_count - 1U],
		part_end[part_count - 1U],
		&object_name,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_identifier_unquote(input_sql, link_start, link_end, &link_name, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(object_name);
		free(link_name);
		return status;
	}

	status = sqlparser_dameng_state_append_dblink_relation(
		state,
		marker,
		object_name,
		link_name,
		input_sql + part_start[part_count - 1U],
		part_end[part_count - 1U] - part_start[part_count - 1U],
		input_sql + link_start,
		link_end - link_start,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_input_mem(
			out,
			input_sql,
			start,
			part_start[part_count - 1U] - start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_generated_identifier(
			out,
			marker,
			out_error);
	}
	free(object_name);
	free(link_name);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = link_end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_append_pg_param(
	sqlparser_dameng_buffer_t *out,
	size_t param_index,
	const char *input,
	size_t source_start,
	size_t source_length,
	sqlparser_error_t *out_error)
{
	char text[32];
	size_t text_length;

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	text_length = strlen(text);
	return sqlparser_dameng_buffer_append_source_identifier(
		out,
		input,
		source_start,
		source_length,
		text,
		text_length,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_copy_bind_placeholder(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t end;
	size_t param_index;
	sqlparser_status_t status;

	start = *index;
	end = start + 1U;
	if (isdigit((unsigned char)input[end])) {
		while (isdigit((unsigned char)input[end])) {
			end++;
		}
	} else if (sqlparser_dameng_is_ident_start((unsigned char)input[end])) {
		end++;
		while (sqlparser_dameng_is_ident_char((unsigned char)input[end])) {
			end++;
		}
		while (input[end] == '.' &&
		       sqlparser_dameng_is_ident_start((unsigned char)input[end + 1U])) {
			end += 2U;
			while (sqlparser_dameng_is_ident_char((unsigned char)input[end])) {
				end++;
			}
		}
	} else {
		status = sqlparser_dameng_buffer_append_input_char(
			out,
			input,
			*index,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			(*index)++;
		}
		return status;
	}

	status = sqlparser_dameng_state_find_or_add_bind(state, input + start, end - start, &param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_append_pg_param(
		out,
		param_index,
		input,
		start,
		end - start,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state->bind_occurrence_count++;
	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_copy_question_placeholder(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t param_index;
	size_t start;
	sqlparser_status_t status;

	start = *index;
	status = sqlparser_dameng_state_append_bind(state, "?", 1U, &param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_append_pg_param(
		out,
		param_index,
		input,
		start,
		1U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state->bind_occurrence_count++;
	(*index)++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_parse_unsigned_token(
	const char *input,
	size_t *pos,
	size_t *out_start,
	size_t *out_end)
{
	size_t start;

	*pos = sqlparser_dameng_skip_space(input, *pos);
	start = *pos;
	while (isdigit((unsigned char)input[*pos])) {
		(*pos)++;
	}
	if (*pos == start) {
		return 0;
	}
	*out_start = start;
	*out_end = *pos;
	return 1;
}

static sqlparser_status_t sqlparser_dameng_append_limit_clause(
	sqlparser_dameng_buffer_t *out,
	const char *input,
	size_t first_start,
	size_t first_end,
	size_t second_start,
	size_t second_end,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_dameng_buffer_append_cstr(out, " LIMIT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_input_mem(
			out,
			input,
			second_start,
			second_end - second_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && first_end > first_start) {
		status = sqlparser_dameng_buffer_append_cstr(out, " OFFSET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK && first_end > first_start) {
		status = sqlparser_dameng_buffer_append_input_mem(
			out,
			input,
			first_start,
			first_end - first_start,
			out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_limit_comma(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t first_start;
	size_t first_end;
	size_t second_start;
	size_t second_end;
	size_t after_first;

	if (!sqlparser_dameng_ascii_word_equal(input, *index, "limit")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = *index + strlen("limit");
	if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &first_start, &first_end)) {
		return SQLPARSER_STATUS_OK;
	}
	after_first = sqlparser_dameng_skip_space(input, pos);
	if (input[after_first] != ',') {
		return SQLPARSER_STATUS_OK;
	}
	pos = after_first + 1U;
	if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &second_start, &second_end)) {
		return SQLPARSER_STATUS_OK;
	}
	*index = pos;
	return sqlparser_dameng_append_limit_clause(out, input, first_start, first_end, second_start, second_end, out_error);
}

static sqlparser_status_t sqlparser_dameng_parse_top_clause(
	const char *input,
	size_t top_pos,
	size_t *out_pos,
	char **out_limit,
	char **out_top_clause,
	sqlparser_dameng_limit_origin_t *out_origin,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t limit;
	size_t pos;
	size_t first_start;
	size_t first_end;
	size_t second_start;
	size_t second_end;
	size_t after_first;
	size_t public_end;
	int has_offset;
	sqlparser_status_t status;

	if (out_pos == NULL || out_limit == NULL || out_top_clause == NULL ||
	    out_origin == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "TOP parse arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&limit, 0, sizeof(limit));
	memset(out_origin, 0, sizeof(*out_origin));
	*out_limit = NULL;
	*out_top_clause = NULL;
	pos = top_pos;
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "top")) {
		return SQLPARSER_STATUS_OK;
	}
	pos += strlen("top");
	if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &first_start, &first_end)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP expression");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	after_first = sqlparser_dameng_skip_space(input, pos);
	second_start = 0U;
	second_end = 0U;
	has_offset = 0;
	if (input[after_first] == ',') {
		pos = after_first + 1U;
		if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &second_start, &second_end)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP offset");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		has_offset = 1;
	} else {
		second_start = first_start;
		second_end = first_end;
		first_start = 0U;
		first_end = 0U;
		pos = after_first;
	}

	if (sqlparser_dameng_ascii_word_equal(input, pos, "percent")) {
		if (has_offset) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP offset modifiers");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		pos += strlen("percent");
		pos = sqlparser_dameng_skip_space(input, pos);
	}
	if (sqlparser_dameng_ascii_word_equal(input, pos, "with")) {
		size_t ties_pos;

		if (has_offset) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP offset modifiers");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		ties_pos = sqlparser_dameng_skip_space(input, pos + strlen("with"));
		if (!sqlparser_dameng_ascii_word_equal(input, ties_pos, "ties")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported Dameng syntax: TOP WITH");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = ties_pos + strlen("ties");
	}

	public_end = sqlparser_dameng_trim_right(input, top_pos, pos);
	*out_top_clause = sqlparser_strndup(input + top_pos, public_end - top_pos);
	if (*out_top_clause == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_dameng_append_limit_clause(
		&limit,
		input,
		first_start,
		first_end,
		second_start,
		second_end,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_top_clause);
		*out_top_clause = NULL;
		sqlparser_dameng_buffer_release(&limit);
		return status;
	}
	status = sqlparser_dameng_buffer_finish(&limit, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_top_clause);
		*out_top_clause = NULL;
		sqlparser_dameng_buffer_release(&limit);
		return status;
	}
	*out_limit = sqlparser_dameng_buffer_take(&limit);
	if (*out_limit == NULL) {
		free(*out_top_clause);
		*out_top_clause = NULL;
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_pos = pos;
	out_origin->first_start = first_start;
	out_origin->first_end = first_end;
	out_origin->second_start = second_start;
	out_origin->second_end = second_end;
	out_origin->valid = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_append_pending_limit(
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	const char *input,
	char **pending_limit,
	char **pending_top_clause,
	sqlparser_dameng_limit_origin_t *pending_origin,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (pending_limit == NULL || *pending_limit == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (out != NULL && out->origin_writer.map != NULL &&
	    input != NULL && pending_origin != NULL &&
	    pending_origin->valid) {
		status = sqlparser_dameng_append_limit_clause(
			out,
			input,
			pending_origin->first_start,
			pending_origin->first_end,
			pending_origin->second_start,
			pending_origin->second_end,
			out_error);
	} else {
		status = sqlparser_dameng_buffer_append_cstr(
			out,
			*pending_limit,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && state != NULL) {
		state->limit_count++;
		if (pending_top_clause != NULL && *pending_top_clause != NULL) {
			status = sqlparser_dameng_state_append_top_clause(
				state,
				*pending_top_clause,
				strlen(*pending_top_clause),
				state->limit_count,
				out_error);
		}
	}
	free(*pending_limit);
	*pending_limit = NULL;
	if (pending_top_clause != NULL) {
		free(*pending_top_clause);
		*pending_top_clause = NULL;
	}
	if (pending_origin != NULL) {
		memset(pending_origin, 0, sizeof(*pending_origin));
	}
	return status;
}

static void sqlparser_dameng_pending_tops_release(
	sqlparser_dameng_pending_tops_t *pending_tops)
{
	if (pending_tops == NULL) {
		return;
	}
	while (pending_tops->count > 0U) {
		pending_tops->count--;
		free(pending_tops->items[pending_tops->count].limit);
		free(pending_tops->items[pending_tops->count].top_clause);
	}
	free(pending_tops->items);
	memset(pending_tops, 0, sizeof(*pending_tops));
}

static sqlparser_status_t sqlparser_dameng_pending_tops_push(
	sqlparser_dameng_pending_tops_t *pending_tops,
	char *limit,
	char *top_clause,
	const sqlparser_dameng_limit_origin_t *origin,
	size_t depth,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_pending_top_t *next;
	size_t next_capacity;

	if (pending_tops->count == pending_tops->capacity) {
		next_capacity = pending_tops->capacity == 0U ?
			4U : pending_tops->capacity * 2U;
		if (next_capacity < pending_tops->capacity ||
		    next_capacity > SIZE_MAX / sizeof(*next)) {
			free(limit);
			free(top_clause);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_dameng_pending_top_t *)realloc(
			pending_tops->items,
			next_capacity * sizeof(*next));
		if (next == NULL) {
			free(limit);
			free(top_clause);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		pending_tops->items = next;
		pending_tops->capacity = next_capacity;
	}

	pending_tops->items[pending_tops->count].limit = limit;
	pending_tops->items[pending_tops->count].top_clause = top_clause;
	pending_tops->items[pending_tops->count].origin = *origin;
	pending_tops->items[pending_tops->count].depth = depth;
	pending_tops->count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_pending_tops_flush_depth(
	sqlparser_dameng_pending_tops_t *pending_tops,
	size_t depth,
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	const char *input,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_pending_top_t *pending_top;
	sqlparser_status_t status;

	if (pending_tops->count == 0U ||
	    pending_tops->items[pending_tops->count - 1U].depth != depth) {
		return SQLPARSER_STATUS_OK;
	}
	pending_top = &pending_tops->items[pending_tops->count - 1U];
	status = sqlparser_dameng_append_pending_limit(
		out,
		state,
		input,
		&pending_top->limit,
		&pending_top->top_clause,
		&pending_top->origin,
		out_error);
	pending_tops->count--;
	return status;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_set_schema(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	if (out_rewritten != NULL) {
		*out_rewritten = 0;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, *index, "set")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, *index + strlen("set"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "schema")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("schema"));
	value_start = pos;
	while (input[pos] != '\0' && input[pos] != ';') {
		pos++;
	}
	value_end = sqlparser_dameng_trim_right(input, value_start, pos);
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SET SCHEMA requires a value");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_generated_identifier(
			out,
			SQLPARSER_INTERNAL_CURRENT_SCHEMA,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_input_mem(
			out,
			input,
			value_start,
			value_end - value_start,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	if (out_rewritten != NULL) {
		*out_rewritten = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_is_supported_session_parameter(const char *input, size_t start, size_t end)
{
	static const char *const supported_params[] = {
		"nls_date_language",
		"nls_date_format",
		"nls_timestamp_format",
		"nls_timestamp_tz_format",
		"nls_time_format",
		"nls_time_tz_format",
		"nls_sort",
		"case_sensitive"
	};
	size_t index;

	for (index = 0U; index < sizeof(supported_params) / sizeof(supported_params[0]); index++) {
		if (sqlparser_dameng_ascii_span_equal(input, start, end, supported_params[index])) {
			return 1;
		}
	}
	return 0;
}

static size_t sqlparser_dameng_session_value_token_end(
	const char *input,
	size_t start,
	sqlparser_error_t *out_error)
{
	size_t pos;
	char quote;

	if (input == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return 0U;
	}
	pos = start;
	if (input[pos] == '\'' || input[pos] == '"') {
		quote = input[pos];
		pos++;
		while (input[pos] != '\0') {
			if (input[pos] == quote) {
				if (input[pos + 1U] == quote) {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated ALTER SESSION SET value");
		return 0U;
	}

	while (input[pos] != '\0' && input[pos] != ';' && !isspace((unsigned char)input[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_dameng_session_skip_space(
	const char *input,
	size_t pos,
	size_t end)
{
	while (pos < end && isspace((unsigned char)input[pos])) {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_dameng_append_internal_string_literal(
	sqlparser_dameng_buffer_t *out,
	const char *input,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error);

static size_t sqlparser_dameng_statement_end(const char *sql, size_t start);

static sqlparser_status_t sqlparser_dameng_append_raw_session_statement(
	const char *input,
	size_t start,
	size_t end,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_generated_identifier(
			out, SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_append_internal_string_literal(
			out, input, start, end, out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_alter_session(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t param_start;
	size_t param_end;
	size_t value_start;
	size_t value_end;
	size_t statement_start;
	size_t statement_end;
	int is_current_schema;
	int is_session_parameter;
	sqlparser_status_t status;

	if (out_rewritten != NULL) {
		*out_rewritten = 0;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, *index, "alter")) {
		return SQLPARSER_STATUS_OK;
	}
	statement_start = *index;
	statement_end = sqlparser_dameng_statement_end(input, statement_start);
	statement_end = sqlparser_dameng_trim_right(input, statement_start, statement_end);
	pos = sqlparser_dameng_skip_space(input, *index + strlen("alter"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "session")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("session"));
	if (sqlparser_dameng_ascii_word_equal(input, pos, "enable") ||
	    sqlparser_dameng_ascii_word_equal(input, pos, "disable")) {
		pos = sqlparser_dameng_session_skip_space(
			input,
			pos + (sqlparser_dameng_ascii_word_equal(input, pos, "enable") ?
				strlen("enable") :
				strlen("disable")),
			statement_end);
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "parallel")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_session_skip_space(
			input, pos + strlen("parallel"), statement_end);
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "dml")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_session_skip_space(
			input, pos + strlen("dml"), statement_end);
		if (pos != statement_end) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_dameng_append_raw_session_statement(
			input, statement_start, statement_end, out, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*index = statement_end;
		if (out_rewritten != NULL) {
			*out_rewritten = 1;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "set")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("set"));
	if (input[pos] == '\'') {
		param_start = pos;
		param_end = sqlparser_dameng_session_value_token_end(input, param_start, out_error);
		if (param_end == 0U) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (param_end <= param_start + 2U ||
		    !sqlparser_dameng_ascii_span_equal(
			    input, param_start + 1U, param_end - 1U, "hagr_hash_size")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_session_skip_space(input, param_end, statement_end);
		if (input[pos] != '=') {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_session_skip_space(input, pos + 1U, statement_end);
		value_start = pos;
		while (pos < statement_end && isdigit((unsigned char)input[pos])) {
			pos++;
		}
		if (value_start == pos) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_session_skip_space(input, pos, statement_end);
		if (pos < statement_end) {
			if (!sqlparser_dameng_ascii_word_equal(input, pos, "purge")) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_dameng_session_skip_space(
				input, pos + strlen("purge"), statement_end);
		}
		if (pos != statement_end) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_dameng_append_raw_session_statement(
			input, statement_start, statement_end, out, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*index = statement_end;
		if (out_rewritten != NULL) {
			*out_rewritten = 1;
		}
		return SQLPARSER_STATUS_OK;
	}
	param_start = pos;
	while (sqlparser_dameng_is_ident_char((unsigned char)input[pos])) {
		pos++;
	}
	param_end = pos;
	if (param_start == param_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET requires a parameter name");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	is_current_schema = sqlparser_dameng_ascii_span_equal(input, param_start, param_end, "current_schema");
	is_session_parameter = sqlparser_dameng_is_supported_session_parameter(input, param_start, param_end);
	if (!is_current_schema && !is_session_parameter) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: alter session parameter");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	pos = sqlparser_dameng_skip_space(input, pos);
	if (input[pos] != '=') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET requires '='");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_dameng_skip_space(input, pos + 1U);
	value_start = pos;
	value_end = sqlparser_dameng_session_value_token_end(input, value_start, out_error);
	if (value_end == 0U) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET requires a value");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_dameng_skip_space(input, value_end);
	if (input[pos] != '\0' && input[pos] != ';') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported ALTER SESSION SET suffix");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK && is_current_schema) {
		status = sqlparser_dameng_buffer_append_generated_identifier(
			out,
			SQLPARSER_INTERNAL_CURRENT_SCHEMA,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_session_parameter) {
		status = sqlparser_dameng_buffer_record_generated_identifier(
			out,
			2U +
				strlen(SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX) +
				param_end - param_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_session_parameter) {
		status = sqlparser_dameng_buffer_append_raw_mem(
			out,
			"\"",
			1U,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_session_parameter) {
		status = sqlparser_dameng_buffer_append_raw_mem(
			out,
			SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX,
			strlen(SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_session_parameter) {
		status = sqlparser_dameng_buffer_append_raw_mem(
			out,
			input + param_start,
			param_end - param_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_session_parameter) {
		status = sqlparser_dameng_buffer_append_raw_mem(
			out,
			"\"",
			1U,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_input_mem(
			out,
			input,
			value_start,
			value_end - value_start,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	if (out_rewritten != NULL) {
		*out_rewritten = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_append_internal_string_literal(
	sqlparser_dameng_buffer_t *out,
	const char *input,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_dameng_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (input[pos] == '\'') {
			status = sqlparser_dameng_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_dameng_buffer_append_char(out, input[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_dameng_buffer_append_char(out, '\'', out_error);
}

static size_t sqlparser_dameng_statement_token_end(const char *input, size_t pos)
{
	while (input[pos] != '\0' && input[pos] != ';' && !isspace((unsigned char)input[pos])) {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_exec_sql_prepared(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	const char *internal_name;
	size_t pos;
	size_t name_start;
	size_t name_end;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	if (out_rewritten != NULL) {
		*out_rewritten = 0;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, *index, "exec")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, *index + strlen("exec"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "sql")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("sql"));
	internal_name = NULL;
	value_start = 0U;
	value_end = 0U;
	if (sqlparser_dameng_ascii_word_equal(input, pos, "prepare")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("prepare"));
		name_start = pos;
		name_end = sqlparser_dameng_statement_token_end(input, pos);
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_dameng_skip_space(input, name_end);
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "from")) {
			return SQLPARSER_STATUS_OK;
		}
		value_start = sqlparser_dameng_skip_space(input, pos + strlen("from"));
		pos = value_start;
		while (input[pos] != '\0' && input[pos] != ';') {
			pos++;
		}
		value_end = sqlparser_dameng_trim_right(input, value_start, pos);
		if (value_start >= value_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL PREPARE FROM requires SQL text");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		internal_name = SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE;
	} else if (sqlparser_dameng_ascii_word_equal(input, pos, "execute")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("execute"));
		name_start = pos;
		name_end = sqlparser_dameng_statement_token_end(input, pos);
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL EXECUTE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_dameng_skip_space(input, name_end);
		if (sqlparser_dameng_ascii_word_equal(input, pos, "using")) {
			value_start = sqlparser_dameng_skip_space(input, pos + strlen("using"));
			pos = value_start;
			while (input[pos] != '\0' && input[pos] != ';') {
				pos++;
			}
			value_end = sqlparser_dameng_trim_right(input, value_start, pos);
			if (value_start >= value_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL EXECUTE USING requires parameters");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
		} else {
			while (input[pos] != '\0' && input[pos] != ';') {
				if (!isspace((unsigned char)input[pos])) {
					return SQLPARSER_STATUS_OK;
				}
				pos++;
			}
		}
		internal_name = SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE;
	} else if (sqlparser_dameng_ascii_word_equal(input, pos, "deallocate")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("deallocate"));
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "prepare")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_skip_space(input, pos + strlen("prepare"));
		name_start = pos;
		name_end = sqlparser_dameng_statement_token_end(input, pos);
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL DEALLOCATE PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_dameng_skip_space(input, name_end);
		while (input[pos] != '\0' && input[pos] != ';') {
			if (!isspace((unsigned char)input[pos])) {
				return SQLPARSER_STATUS_OK;
			}
			pos++;
		}
		internal_name = SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE;
	} else {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_generated_identifier(
			out,
			internal_name,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_append_internal_string_literal(out, input, name_start, name_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && value_start < value_end) {
		status = sqlparser_dameng_buffer_append_cstr(out, ", ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_append_internal_string_literal(out, input, value_start, value_end, out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	if (out_rewritten != NULL) {
		*out_rewritten = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_dameng_skip_ident_word(const char *input, size_t pos)
{
	while (sqlparser_dameng_is_ident_char((unsigned char)input[pos])) {
		pos++;
	}
	return pos;
}

static int sqlparser_dameng_create_targets_procedure(const char *input, size_t pos)
{
	pos = sqlparser_dameng_skip_space(input, sqlparser_dameng_skip_ident_word(input, pos));
	if (sqlparser_dameng_ascii_word_equal(input, pos, "or")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("or"));
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "replace")) {
			return 0;
		}
		pos = sqlparser_dameng_skip_space(input, pos + strlen("replace"));
	}
	return sqlparser_dameng_ascii_word_equal(input, pos, "procedure");
}

static sqlparser_status_t sqlparser_dameng_reject_unsupported_at(
	const char *input,
	size_t index,
	sqlparser_error_t *out_error)
{
	if (sqlparser_dameng_ascii_word_equal(input, index, "begin")) {
		size_t next;

		next = sqlparser_dameng_skip_space(input, index + strlen("begin"));
		if (input[next] != '\0' && input[next] != ';' &&
		    !sqlparser_dameng_ascii_word_equal(input, next, "work") &&
		    !sqlparser_dameng_ascii_word_equal(input, next, "transaction")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}
	if (sqlparser_dameng_ascii_word_equal(input, index, "pivot") ||
	    sqlparser_dameng_ascii_word_equal(input, index, "procedure") ||
	    sqlparser_dameng_ascii_word_equal(input, index, "returning")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_dameng_ascii_word_equal(input, index, "create") &&
	    sqlparser_dameng_create_targets_procedure(input, index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: CREATE PROCEDURE");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_preprocess_text_internal(
	const char *input_sql,
	sqlparser_dameng_state_t *state,
	char **out_sql,
	sqlparser_identifier_origin_map_t *origins,
	size_t origin_map_input_length,
	size_t origin_input_base,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	sqlparser_dameng_pending_tops_t pending_tops;
	sqlparser_dialect_returning_into_clause_t returning_into;
	size_t index;
	size_t paren_depth;
	size_t significant_scan_pos;
	size_t previous_significant_pos;
	sqlparser_status_t status;
	int has_returning_into;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "Dameng dialect state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_begin_origin(
		&out,
		input_sql,
		origins,
		origin_map_input_length,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&pending_tops, 0, sizeof(pending_tops));
	memset(&returning_into, 0, sizeof(returning_into));
	index = 0U;
	paren_depth = 0U;
	significant_scan_pos = 0U;
	previous_significant_pos = SIZE_MAX;
	has_returning_into = 0;
	while (input_sql[index] != '\0') {
		int copied;
		size_t q_prefix_len;
		size_t current_timestamp_end;

		q_prefix_len = sqlparser_dameng_q_quote_prefix_len(input_sql + index);
		if (q_prefix_len > 0U) {
			size_t literal_start;

			literal_start = out.len;
			copied = sqlparser_dameng_copy_q_quote(input_sql, &index, &out, out_error);
			if (copied < 0) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
			}
			if (q_prefix_len == 2U) {
				status = sqlparser_dameng_store_national_literal(
					state,
					out.data + literal_start,
					out.len - literal_start,
					state->national_literals.literal_count,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_pending_tops_release(&pending_tops);
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
			}
			state->national_literals.literal_count++;
			continue;
		}

		if (sqlparser_dameng_is_n_string_literal(input_sql + index)) {
			size_t literal_start;

			literal_start = out.len;
			status = sqlparser_dameng_copy_n_string_literal(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			status = sqlparser_dameng_store_national_literal(
				state,
				out.data + literal_start,
				out.len - literal_start,
				state->national_literals.literal_count,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			state->national_literals.literal_count++;
			continue;
		}

		if (sqlparser_dameng_is_ident_start((unsigned char)input_sql[index])) {
			const char *internal_keyword;
			size_t source_length;

			internal_keyword =
				sqlparser_dameng_hierarchy_internal_keyword(
					input_sql,
					index,
					&source_length);
			if (internal_keyword != NULL) {
				status =
					sqlparser_dameng_buffer_append_source_identifier(
						&out,
						input_sql,
						index,
						source_length,
						internal_keyword,
						strlen(internal_keyword),
						out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_pending_tops_release(
						&pending_tops);
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
				index += source_length;
				continue;
			}
		}

		if (input_sql[index] == '"' ||
		    sqlparser_dameng_is_ident_start((unsigned char)input_sql[index])) {
			size_t before_dblink;

			before_dblink = index;
			status = sqlparser_dameng_try_copy_database_link_relation(
				input_sql,
				&index,
				&out,
				state,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			if (index != before_dblink) {
				continue;
			}
		}

		{
			size_t quoted_start;

			quoted_start = index;
			copied = sqlparser_dameng_copy_quoted_or_comment(input_sql, &index, &out, out_error);
			if (copied > 0 && input_sql[quoted_start] == '\'') {
				state->national_literals.literal_count++;
			}
		}
		if (copied < 0) {
			sqlparser_dameng_pending_tops_release(&pending_tops);
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (copied > 0) {
			continue;
		}
		if (has_returning_into && index == returning_into.into_start) {
			status = sqlparser_dameng_buffer_append_cstr(
				&out,
				returning_into.uses_return_keyword ? "," : ",   ",
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index += strlen("into");
				has_returning_into = 0;
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			continue;
		}
		if ((sqlparser_dameng_ascii_word_equal(
			     input_sql, index, "returning") ||
		     sqlparser_dameng_ascii_word_equal(
			     input_sql, index, "return")) &&
		    sqlparser_dialect_returning_into_clause_at(
			    SQLPARSER_DIALECT_DAMENG,
			    input_sql,
			    index,
			    1,
			    &returning_into)) {
			status = sqlparser_dialect_returning_into_state_append(
				&state->returning_into,
				returning_into.statement_index,
				input_sql + index,
				returning_into.keyword_end - index,
				input_sql + returning_into.into_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = returning_into.uses_return_keyword ?
					sqlparser_dameng_buffer_append_cstr(
						&out, "RETURNING", out_error) :
					sqlparser_dameng_buffer_append_input_mem(
						&out,
						input_sql,
						index,
						returning_into.keyword_end - index,
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			index = returning_into.keyword_end;
			has_returning_into = 1;
			continue;
		}

		status = sqlparser_dameng_reject_unsupported_at(input_sql, index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_pending_tops_release(&pending_tops);
			sqlparser_dameng_buffer_release(&out);
			return status;
		}

		current_timestamp_end = 0U;
		if (sqlparser_dameng_ascii_word_equal(
			    input_sql,
			    index,
			    "current_timestamp")) {
			size_t open_pos;
			size_t close_pos;

			sqlparser_dameng_scan_previous_significant(
				input_sql,
				index,
				&significant_scan_pos,
				&previous_significant_pos);
			if (previous_significant_pos == SIZE_MAX ||
			    input_sql[previous_significant_pos] != '.') {
				open_pos = sqlparser_dameng_skip_trivia(
					input_sql,
					index + strlen("current_timestamp"));
				if (input_sql[open_pos] == '(') {
					close_pos = sqlparser_dameng_skip_trivia(
						input_sql,
						open_pos + 1U);
					if (input_sql[close_pos] == ')') {
						current_timestamp_end =
							close_pos + 1U;
					}
				}
			}
		}
		if (current_timestamp_end != 0U) {
			status = sqlparser_dameng_buffer_append_input_mem(
				&out,
				input_sql,
				index,
				strlen("current_timestamp"),
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_char(
					&out,
					' ',
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				index = current_timestamp_end;
			}
		} else if (input_sql[index] == '(') {
			paren_depth++;
			status = sqlparser_dameng_buffer_append_input_char(
				&out,
				input_sql,
				index,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		} else if (input_sql[index] == ')') {
			status = sqlparser_dameng_pending_tops_flush_depth(
				&pending_tops,
				paren_depth,
				&out,
				state,
				input_sql,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			if (paren_depth > 0U) {
				paren_depth--;
			}
			status = sqlparser_dameng_buffer_append_input_char(
				&out,
				input_sql,
				index,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		} else if (input_sql[index] == ';') {
			status = sqlparser_dameng_pending_tops_flush_depth(
				&pending_tops,
				paren_depth,
				&out,
				state,
				input_sql,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_input_char(
					&out,
					input_sql,
					index,
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "set")) {
			int rewritten;

			rewritten = 0;
			status = sqlparser_dameng_try_rewrite_set_schema(input_sql, &index, &out, &rewritten, out_error);
			if (status == SQLPARSER_STATUS_OK && !rewritten) {
				status = sqlparser_dameng_buffer_append_input_char(
					&out,
					input_sql,
					index,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					index++;
				}
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "alter")) {
			int rewritten;

			rewritten = 0;
			status = sqlparser_dameng_try_rewrite_alter_session(input_sql, &index, &out, &rewritten, out_error);
			if (status == SQLPARSER_STATUS_OK && !rewritten) {
				status = sqlparser_dameng_buffer_append_input_char(
					&out,
					input_sql,
					index,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					index++;
				}
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "exec")) {
			int rewritten;

			rewritten = 0;
			status = sqlparser_dameng_try_rewrite_exec_sql_prepared(input_sql, &index, &out, &rewritten, out_error);
			if (status == SQLPARSER_STATUS_OK && !rewritten) {
				status = sqlparser_dameng_buffer_append_input_char(
					&out,
					input_sql,
					index,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					index++;
				}
			}
		} else if (input_sql[index] == '?') {
			status = sqlparser_dameng_copy_question_placeholder(
				input_sql,
				&index,
				&out,
				state,
				out_error);
		} else if (input_sql[index] == ':' && input_sql[index + 1U] != '=' &&
		           input_sql[index + 1U] != ':' && input_sql[index + 1U] != '\0') {
			status = sqlparser_dameng_copy_bind_placeholder(input_sql, &index, &out, state, out_error);
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "minus")) {
			status = sqlparser_dialect_minuses_append(
				&state->minuses,
				state->minuses.except_count,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(
					&out, "EXCEPT", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				state->minuses.except_count++;
				index += strlen("minus");
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "except")) {
			status = sqlparser_dameng_buffer_append_input_mem(
				&out,
				input_sql,
				index,
				strlen("except"),
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				state->minuses.except_count++;
				index += strlen("except");
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "limit")) {
			size_t before;

			before = index;
			status = sqlparser_dameng_try_rewrite_limit_comma(input_sql, &index, &out, out_error);
			if (status == SQLPARSER_STATUS_OK && index != before) {
				state->limit_count++;
			}
			if (status == SQLPARSER_STATUS_OK && index == before) {
				state->limit_count++;
				status = sqlparser_dameng_buffer_append_input_char(
					&out,
					input_sql,
					index,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					index++;
				}
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "select")) {
			size_t after_select;
			size_t top_pos;

			status = sqlparser_dameng_buffer_append_input_mem(
				&out,
				input_sql,
				index,
				strlen("select"),
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_pending_tops_release(&pending_tops);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			after_select = index + strlen("select");
			top_pos = sqlparser_dameng_skip_space(input_sql, after_select);
			if (sqlparser_dameng_ascii_word_equal(input_sql, top_pos, "top")) {
				char *top_limit;
				char *top_clause;
				sqlparser_dameng_limit_origin_t top_origin;
				size_t next_pos;

				if (pending_tops.count > 0U &&
				    pending_tops.items[pending_tops.count - 1U].depth >=
					    paren_depth) {
					sqlparser_dameng_pending_tops_release(&pending_tops);
					sqlparser_dameng_buffer_release(&out);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_UNSUPPORTED,
						"unsupported Dameng syntax: ambiguous TOP at the same nesting depth");
					return SQLPARSER_STATUS_UNSUPPORTED;
				}
				if (sqlparser_dameng_buffer_append_input_mem(
					    &out,
					    input_sql,
					    after_select,
					    top_pos - after_select,
					    out_error) !=
					SQLPARSER_STATUS_OK) {
					sqlparser_dameng_pending_tops_release(&pending_tops);
					sqlparser_dameng_buffer_release(&out);
					return out_error != NULL ?
						out_error->code :
						SQLPARSER_STATUS_NO_MEMORY;
				}
				top_limit = NULL;
				top_clause = NULL;
				memset(&top_origin, 0, sizeof(top_origin));
				next_pos = top_pos;
				status = sqlparser_dameng_parse_top_clause(
					input_sql,
					top_pos,
					&next_pos,
					&top_limit,
					&top_clause,
					&top_origin,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(top_limit);
					free(top_clause);
					sqlparser_dameng_pending_tops_release(&pending_tops);
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
				status = sqlparser_dameng_pending_tops_push(
					&pending_tops,
					top_limit,
					top_clause,
					&top_origin,
					paren_depth,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_pending_tops_release(&pending_tops);
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
				index = sqlparser_dameng_skip_space(input_sql, next_pos);
			} else {
				index = after_select;
			}
		} else {
			status = sqlparser_dameng_buffer_append_input_char(
				&out,
				input_sql,
				index,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_pending_tops_release(&pending_tops);
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
	}

	status = SQLPARSER_STATUS_OK;
	while (status == SQLPARSER_STATUS_OK && pending_tops.count > 0U) {
		status = sqlparser_dameng_pending_tops_flush_depth(
			&pending_tops,
			pending_tops.items[pending_tops.count - 1U].depth,
			&out,
			state,
			input_sql,
			out_error);
	}
	sqlparser_dameng_pending_tops_release(&pending_tops);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_commit_origin(
			&out,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_preprocess_text(
	const char *input_sql,
	sqlparser_dameng_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_preprocess_text_internal(
		input_sql,
		state,
		out_sql,
		NULL,
		input_sql != NULL ? strlen(input_sql) : 0U,
		0U,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_preprocess_internal(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *state;
	sqlparser_status_t status;

	(void)limits;
	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = NULL;
	status = sqlparser_dameng_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (sqlparser_dameng_is_multi_insert_start(input_sql, NULL)) {
		status = sqlparser_dameng_parse_multi_insert(
			input_sql,
			state,
			out_parser_sql,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_state_destroy(state);
			return status;
		}
		*out_state = state;
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_preprocess_text_internal(
		input_sql,
		state,
		out_parser_sql,
		origins,
		strlen(input_sql),
		0U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_state_destroy(state);
		return status;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_preprocess_internal(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_dameng_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin map must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_dameng_preprocess_internal(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

static sqlparser_status_t sqlparser_dameng_preprocess_fragment(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *dameng_state;

	(void)statement_index;
	if (out_parser_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng dialect state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	dameng_state = (sqlparser_dameng_state_t *)state;
	dameng_state->top_fragment_start = dameng_state->top_count;
	dameng_state->fragment_limit_base = dameng_state->limit_count;
	sqlparser_dialect_national_literals_begin_fragment(
		&dameng_state->national_literals);
	sqlparser_dialect_minuses_begin_fragment(&dameng_state->minuses);

	return sqlparser_dameng_preprocess_text(
		input_sql, dameng_state, out_parser_sql, out_error);
}

static sqlparser_status_t sqlparser_dameng_param_to_bind(
	const char *sql,
	size_t *index,
	const sqlparser_dameng_state_t *state,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (!isdigit((unsigned char)sql[pos])) {
		return sqlparser_dameng_buffer_append_char(out, sql[(*index)++], out_error);
	}

	value = 0UL;
	while (isdigit((unsigned char)sql[pos])) {
		unsigned int digit;

		digit = (unsigned int)(sql[pos] - '0');
		if (value > (((unsigned long)-1) - digit) / 10UL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "parameter index overflow");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		value = value * 10UL + digit;
		pos++;
	}

	if (state != NULL && value > 0UL && (size_t)value <= state->bind_count) {
		*index = pos;
		return sqlparser_dameng_buffer_append_cstr(out, state->bind_names[value - 1UL], out_error);
	}

	return sqlparser_dameng_buffer_append_char(out, sql[(*index)++], out_error);
}

static size_t sqlparser_dameng_quoted_literal_end(const char *sql, size_t start)
{
	size_t pos;

	if (sql == NULL || sql[start] != '\'') {
		return 0U;
	}
	pos = start + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == '\'' && sql[pos + 1U] == '\'') {
			pos += 2U;
			continue;
		}
		if (sql[pos] == '\'') {
			return pos + 1U;
		}
		pos++;
	}
	return 0U;
}

static size_t sqlparser_dameng_skip_optional_timestamp_zone(const char *sql, size_t pos)
{
	size_t scan;
	size_t word_pos;

	scan = pos;
	while (isspace((unsigned char)sql[scan])) {
		scan++;
	}
	if (sqlparser_dameng_ascii_word_equal(sql, scan, "without") ||
	    sqlparser_dameng_ascii_word_equal(sql, scan, "with")) {
		word_pos = scan;
		while (sqlparser_dameng_is_ident_char((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		while (isspace((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		if (sqlparser_dameng_ascii_word_equal(sql, word_pos, "time")) {
			while (sqlparser_dameng_is_ident_char((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			while (isspace((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			if (sqlparser_dameng_ascii_word_equal(sql, word_pos, "zone")) {
				while (sqlparser_dameng_is_ident_char((unsigned char)sql[word_pos])) {
					word_pos++;
				}
				return word_pos;
			}
		}
	}
	return pos;
}

static int sqlparser_dameng_copy_cast_literal(
	const char *sql,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	size_t literal_end;
	size_t cast_name_pos;
	size_t cast_end;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL || sql[*index] != '\'') {
		return 0;
	}
	literal_end = sqlparser_dameng_quoted_literal_end(sql, *index);
	if (literal_end == 0U || sql[literal_end] != ':' || sql[literal_end + 1U] != ':') {
		return 0;
	}

	prefix = NULL;
	cast_name_pos = literal_end + 2U;
	cast_end = cast_name_pos;
	if (sqlparser_dameng_ascii_word_equal(sql, cast_name_pos, "date")) {
		prefix = "DATE ";
		cast_end += strlen("date");
	} else if (sqlparser_dameng_ascii_word_equal(sql, cast_name_pos, "timestamp")) {
		prefix = "TIMESTAMP ";
		cast_end += strlen("timestamp");
		cast_end = sqlparser_dameng_skip_optional_timestamp_zone(sql, cast_end);
	}
	if (prefix == NULL) {
		return 0;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, prefix, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	status = sqlparser_dameng_buffer_append_mem(out, sql + *index, literal_end - *index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	*index = cast_end;
	return 1;
}

static int sqlparser_dameng_marker_token_matches(
	const char *sql,
	size_t pos,
	const char *marker)
{
	size_t len;

	if (sql == NULL || marker == NULL || marker[0] == '\0') {
		return 0;
	}
	len = strlen(marker);
	if (strncmp(sql + pos, marker, len) != 0) {
		return 0;
	}
	if (pos > 0U && sqlparser_dameng_is_ident_char((unsigned char)sql[pos - 1U])) {
		return 0;
	}
	return !sqlparser_dameng_is_ident_char((unsigned char)sql[pos + len]);
}

static const sqlparser_dameng_dblink_relation_t *sqlparser_dameng_state_find_dblink_marker_at(
	const sqlparser_dameng_state_t *state,
	const char *sql,
	size_t pos)
{
	size_t index;

	if (state == NULL || sql == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->dblink_count; index++) {
		if (sqlparser_dameng_marker_token_matches(sql, pos, state->dblink_relations[index].parser_object_name)) {
			return &state->dblink_relations[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_dameng_restore_database_links(
	char **io_sql,
	const sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	const char *sql;
	size_t index;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->dblink_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_reserve_input(&out, sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	index = 0U;
	while (sql[index] != '\0') {
		const sqlparser_dameng_dblink_relation_t *relation;
		int copied;

		copied = sqlparser_dameng_copy_quoted_or_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		relation = sqlparser_dameng_state_find_dblink_marker_at(state, sql, index);
		if (relation != NULL) {
			status = sqlparser_dameng_buffer_append_cstr(&out, relation->public_object_sql, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_char(&out, '@', out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, relation->public_link_sql, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			index += strlen(relation->parser_object_name);
			continue;
		}

		status = sqlparser_dameng_buffer_append_char(&out, sql[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		index++;
	}

	status = sqlparser_dameng_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_dameng_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_postprocess_text(
	const char *core_sql,
	const sqlparser_dameng_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	sqlparser_status_t status;
	size_t index;
	size_t literal_ordinal;
	size_t except_ordinal;

	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_reserve_input(&out, core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	literal_ordinal = 0U;
	except_ordinal = 0U;
	while (core_sql[index] != '\0') {
		int copied;

		copied = sqlparser_dameng_copy_cast_literal(core_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			literal_ordinal++;
			continue;
		}

		if (core_sql[index] == '\'') {
			size_t literal_end;

			literal_end = sqlparser_dameng_quoted_literal_end(core_sql, index);
			if (literal_end > index &&
			    sqlparser_dameng_national_literal_matches(state, literal_ordinal, core_sql, index, literal_end)) {
				status = sqlparser_dameng_buffer_append_char(&out, 'N', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
			}
			copied = sqlparser_dameng_copy_quoted_or_comment(core_sql, &index, &out, out_error);
			if (copied < 0) {
				sqlparser_dameng_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			if (copied > 0) {
				literal_ordinal++;
				continue;
			}
		} else {
			copied = sqlparser_dameng_copy_quoted_or_comment(core_sql, &index, &out, out_error);
			if (copied < 0) {
				sqlparser_dameng_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			if (copied > 0) {
				continue;
			}
		}

		if (core_sql[index] == '$') {
			status = sqlparser_dameng_param_to_bind(core_sql, &index, state, &out, out_error);
		} else if (sqlparser_dameng_ascii_word_equal(core_sql, index, "except")) {
			status = sqlparser_dameng_buffer_append_cstr(
				&out,
				sqlparser_dialect_minuses_match(
					state != NULL ? &state->minuses : NULL,
					except_ordinal) ? "MINUS" : "EXCEPT",
				out_error);
			except_ordinal++;
			index += strlen("except");
		} else if (sqlparser_dameng_ascii_word_equal(core_sql, index, "truncate")) {
			status = sqlparser_dameng_buffer_append_cstr(&out, "TRUNCATE TABLE ", out_error);
			index += strlen("truncate");
			while (isspace((unsigned char)core_sql[index])) {
				index++;
			}
			if (sqlparser_dameng_ascii_word_equal(core_sql, index, "table")) {
				index += strlen("table");
				while (isspace((unsigned char)core_sql[index])) {
					index++;
				}
			}
		} else {
			status = sqlparser_dameng_buffer_append_char(&out, core_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_dameng_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_dameng_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	size_t pos;

	quote = sql[index];
	if (quote == '\'' || quote == '"') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == quote && sql[pos + 1U] == quote) {
				pos += 2U;
				continue;
			}
			if (sql[pos] == quote) {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}
	if (sql[index] == '-' && sql[index + 1U] == '-') {
		pos = index + 2U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sql[index] == '/' && sql[index + 1U] == '*') {
		pos = index + 2U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				return pos + 2U;
			}
			pos++;
		}
		return pos;
	}
	return index;
}

static size_t sqlparser_dameng_token_end(const char *sql, size_t pos)
{
	while (sql[pos] != '\0' &&
	       !isspace((unsigned char)sql[pos]) &&
	       sql[pos] != ';' &&
	       sql[pos] != ')') {
		pos++;
	}
	return pos;
}

static int sqlparser_dameng_find_next_limit(
	const char *sql,
	size_t start_pos,
	size_t *out_limit_pos,
	size_t *out_limit_depth)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || out_limit_pos == NULL || out_limit_depth == NULL) {
		return 0;
	}

	pos = start_pos;
	depth = 0U;
	while (sql[pos] != '\0') {
		size_t next;

		next = sqlparser_dameng_skip_quoted_or_comment_span(sql, pos);
		if (next > pos) {
			pos = next;
			continue;
		}

		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')' && depth > 0U) {
			depth--;
		} else if (sqlparser_dameng_ascii_word_equal(sql, pos, "limit")) {
			*out_limit_pos = pos;
			*out_limit_depth = depth;
			return 1;
		}
		pos++;
	}
	return 0;
}

static int sqlparser_dameng_find_select_for_limit(
	const char *sql,
	size_t limit_pos,
	size_t limit_depth,
	size_t *out_select_pos)
{
	size_t pos;
	size_t depth;
	size_t select_pos;

	if (sql == NULL || out_select_pos == NULL) {
		return 0;
	}

	pos = 0U;
	depth = 0U;
	select_pos = (size_t)-1;
	while (sql[pos] != '\0' && pos < limit_pos) {
		size_t next;

		next = sqlparser_dameng_skip_quoted_or_comment_span(sql, pos);
		if (next > pos) {
			pos = next;
			continue;
		}

		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')' && depth > 0U) {
			depth--;
		} else if (depth == limit_depth && sqlparser_dameng_ascii_word_equal(sql, pos, "select")) {
			select_pos = pos;
		}
		pos++;
	}

	if (select_pos == (size_t)-1) {
		return 0;
	}
	*out_select_pos = select_pos;
	return 1;
}

static size_t sqlparser_dameng_select_top_insert_pos(const char *sql, size_t select_pos)
{
	size_t pos;

	pos = sqlparser_dameng_skip_space(sql, select_pos + strlen("select"));
	if (sqlparser_dameng_ascii_word_equal(sql, pos, "all")) {
		pos = sqlparser_dameng_skip_space(sql, pos + strlen("all"));
	} else if (sqlparser_dameng_ascii_word_equal(sql, pos, "distinct")) {
		pos = sqlparser_dameng_skip_space(sql, pos + strlen("distinct"));
	} else if (sqlparser_dameng_ascii_word_equal(sql, pos, "unique")) {
		pos = sqlparser_dameng_skip_space(sql, pos + strlen("unique"));
	}
	return pos;
}

static size_t sqlparser_dameng_limit_clause_end(const char *sql, size_t limit_pos)
{
	size_t pos;

	pos = sqlparser_dameng_skip_space(sql, limit_pos + strlen("limit"));
	pos = sqlparser_dameng_token_end(sql, pos);
	pos = sqlparser_dameng_skip_space(sql, pos);
	if (sqlparser_dameng_ascii_word_equal(sql, pos, "offset")) {
		pos = sqlparser_dameng_skip_space(sql, pos + strlen("offset"));
		pos = sqlparser_dameng_token_end(sql, pos);
	}
	return pos;
}

static sqlparser_status_t sqlparser_dameng_apply_top_public(
	char **io_sql,
	const sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	char *sql;
	size_t search_pos;
	size_t top_index;
	size_t limit_ordinal;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->top_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	search_pos = 0U;
	limit_ordinal = 0U;
	for (top_index = 0U; top_index < state->top_count; top_index++) {
		sqlparser_dameng_buffer_t out;
		size_t limit_pos;
		size_t limit_depth;
		size_t select_pos;
		size_t insert_pos;
		size_t limit_end;
		size_t top_len;

		do {
			if (!sqlparser_dameng_find_next_limit(sql, search_pos, &limit_pos, &limit_depth)) {
				return SQLPARSER_STATUS_OK;
			}
			limit_ordinal++;
			search_pos = limit_pos + strlen("limit");
		} while (limit_ordinal < state->top_restores[top_index].limit_ordinal);

		if (!sqlparser_dameng_find_select_for_limit(sql, limit_pos, limit_depth, &select_pos)) {
			search_pos = limit_pos + strlen("limit");
			continue;
		}

		insert_pos = sqlparser_dameng_select_top_insert_pos(sql, select_pos);
		limit_end = sqlparser_dameng_limit_clause_end(sql, limit_pos);
		while (limit_pos > insert_pos && isspace((unsigned char)sql[limit_pos - 1U])) {
			limit_pos--;
		}

		memset(&out, 0, sizeof(out));
		if (sqlparser_dameng_buffer_append_mem(&out, sql, insert_pos, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_dameng_buffer_append_cstr(&out, state->top_restores[top_index].clause, out_error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_dameng_buffer_append_char(&out, ' ', out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_dameng_buffer_append_mem(&out, sql + insert_pos, limit_pos - insert_pos, out_error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_dameng_buffer_append_cstr(&out, sql + limit_end, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_dameng_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}

		free(sql);
		sql = sqlparser_dameng_buffer_take(&out);
		*io_sql = sql;
		top_len = strlen(state->top_restores[top_index].clause);
		search_pos = insert_pos + top_len + 1U;
	}

	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_dameng_trim_left(const char *text, size_t start, size_t end)
{
	while (start < end && isspace((unsigned char)text[start])) {
		start++;
	}
	return start;
}

static sqlparser_status_t sqlparser_dameng_trimmed_slice_dup(
	const char *sql,
	size_t start,
	size_t end,
	char **out_text,
	sqlparser_error_t *out_error)
{
	if (out_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "slice output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_text = NULL;
	start = sqlparser_dameng_trim_left(sql, start, end);
	end = sqlparser_dameng_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL slice must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	*out_text = sqlparser_strndup(sql + start, end - start);
	if (*out_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_find_matching_paren(
	const char *sql,
	size_t open_pos,
	size_t end,
	size_t *out_close)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || out_close == NULL || open_pos >= end || sql[open_pos] != '(') {
		return 0;
	}
	depth = 1U;
	pos = open_pos + 1U;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_dameng_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			depth--;
			if (depth == 0U) {
				*out_close = pos;
				return 1;
			}
		}
		pos++;
	}
	return 0;
}

static int sqlparser_dameng_find_top_level_word(
	const char *sql,
	size_t start,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	size_t pos;
	size_t depth;

	depth = 0U;
	pos = start;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_dameng_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (depth == 0U && sqlparser_dameng_ascii_word_equal(sql, pos, word)) {
			if (out_pos != NULL) {
				*out_pos = pos;
			}
			return 1;
		}
		pos++;
	}
	return 0;
}

static sqlparser_status_t sqlparser_dameng_identifier_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	char **out_name,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	size_t pos;

	if (out_name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "identifier output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_name = NULL;
	start = sqlparser_dameng_trim_left(sql, start, end);
	end = sqlparser_dameng_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "identifier must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sql[start] != '"') {
		return sqlparser_dameng_trimmed_slice_dup(sql, start, end, out_name, out_error);
	}

	memset(&out, 0, sizeof(out));
	pos = start + 1U;
	while (pos < end) {
		if (sql[pos] == '"') {
			if (pos + 1U < end && sql[pos + 1U] == '"') {
				if (sqlparser_dameng_buffer_append_char(&out, '"', out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_buffer_release(&out);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
				pos += 2U;
				continue;
			}
			break;
		}
		if (sqlparser_dameng_buffer_append_char(&out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_dameng_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name = sqlparser_dameng_buffer_take(&out);
	return *out_name != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_dameng_relation_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_relation_t *relation,
	sqlparser_error_t *out_error)
{
	size_t part_start[3];
	size_t part_end[3];
	size_t count;
	size_t pos;

	if (relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(relation, 0, sizeof(*relation));
	start = sqlparser_dameng_trim_left(sql, start, end);
	end = sqlparser_dameng_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "INSERT target relation must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sqlparser_dameng_trimmed_slice_dup(sql, start, end, &relation->sql, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}

	count = 0U;
	part_start[count] = start;
	pos = start;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_dameng_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '.') {
			if (count >= 2U) {
				sqlparser_dameng_relation_clear(relation);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Dameng relation has too many name parts");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			part_end[count] = pos;
			count++;
			part_start[count] = pos + 1U;
		}
		pos++;
	}
	part_end[count] = end;
	count++;

	if (count == 1U) {
		return sqlparser_dameng_identifier_from_sql(sql, part_start[0], part_end[0], &relation->table_name, out_error);
	}
	if (count == 2U) {
		if (sqlparser_dameng_identifier_from_sql(sql, part_start[0], part_end[0], &relation->schema_name, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_dameng_identifier_from_sql(sql, part_start[1], part_end[1], &relation->table_name, out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_relation_clear(relation);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_dameng_identifier_from_sql(sql, part_start[0], part_end[0], &relation->database_name, out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_dameng_identifier_from_sql(sql, part_start[1], part_end[1], &relation->schema_name, out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_dameng_identifier_from_sql(sql, part_start[2], part_end[2], &relation->table_name, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_relation_clear(relation);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_parse_column_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_column_t **out_columns,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;

	if (out_columns == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column list outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_columns = NULL;
	*out_count = 0U;
	columns = NULL;
	count = 0U;
	capacity = 0U;
	item_start = start;
	pos = start;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? sqlparser_dameng_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (at_end || sql[pos] == ',') {
			size_t item_end;
			sqlparser_dialect_multi_insert_column_t *next;

			if (count == capacity) {
				size_t next_capacity;

				next_capacity = capacity == 0U ? 4U : capacity * 2U;
				if (next_capacity < capacity) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				next = (sqlparser_dialect_multi_insert_column_t *)realloc(columns, next_capacity * sizeof(*next));
				if (next == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				columns = next;
				capacity = next_capacity;
			}
			memset(&columns[count], 0, sizeof(columns[count]));
			item_end = sqlparser_dameng_trim_right(sql, item_start, pos);
			item_start = sqlparser_dameng_trim_left(sql, item_start, item_end);
			if (item_start >= item_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "column list contains an empty item");
				goto fail;
			}
			if (sqlparser_dameng_trimmed_slice_dup(sql, item_start, item_end, &columns[count].sql, out_error) != SQLPARSER_STATUS_OK ||
			    sqlparser_dameng_identifier_from_sql(sql, item_start, item_end, &columns[count].name, out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			count++;
			item_start = pos + 1U;
		}
		pos++;
	}

	*out_columns = columns;
	*out_count = count;
	return SQLPARSER_STATUS_OK;

fail:
	if (columns != NULL) {
		size_t index;

		for (index = 0U; index < count; index++) {
			sqlparser_dameng_column_clear(&columns[index]);
		}
		free(columns);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static int sqlparser_dameng_public_bind_token(
	const char *sql,
	sqlparser_bind_kind_t *out_kind,
	const char **out_key_start,
	size_t *out_key_len,
	const char **out_sql_start,
	size_t *out_sql_len)
{
	size_t len;

	if (sql == NULL || out_kind == NULL || out_key_start == NULL || out_key_len == NULL ||
	    out_sql_start == NULL || out_sql_len == NULL) {
		return 0;
	}
	len = strlen(sql);
	if (len == 1U && sql[0] == '?') {
		*out_kind = SQLPARSER_BIND_KIND_POSITIONAL;
		*out_key_start = NULL;
		*out_key_len = 0U;
		*out_sql_start = sql;
		*out_sql_len = 1U;
		return 1;
	}
	if (len > 1U && sql[0] == ':' && sql[1] != '=' && sql[1] != ':') {
		size_t pos;

		pos = 1U;
		if (isdigit((unsigned char)sql[pos])) {
			while (isdigit((unsigned char)sql[pos])) {
				pos++;
			}
			if (pos == len) {
				*out_kind = SQLPARSER_BIND_KIND_POSITIONAL;
				*out_key_start = sql + 1U;
				*out_key_len = len - 1U;
				*out_sql_start = sql;
				*out_sql_len = len;
				return 1;
			}
		} else if (sqlparser_dameng_is_ident_start((unsigned char)sql[pos])) {
			pos++;
			while (sqlparser_dameng_is_ident_char((unsigned char)sql[pos])) {
				pos++;
			}
			if (pos == len) {
				*out_kind = SQLPARSER_BIND_KIND_NAMED;
				*out_key_start = sql + 1U;
				*out_key_len = len - 1U;
				*out_sql_start = sql;
				*out_sql_len = len;
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_dameng_text_is_integer_literal(const char *text)
{
	size_t pos;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
	if (!isdigit((unsigned char)text[pos])) {
		return 0;
	}
	while (isdigit((unsigned char)text[pos])) {
		pos++;
	}
	return text[pos] == '\0';
}

static int sqlparser_dameng_text_is_float_literal(const char *text)
{
	size_t pos;
	int saw_digit;
	int saw_dot;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
	saw_digit = 0;
	saw_dot = 0;
	while (text[pos] != '\0') {
		if (isdigit((unsigned char)text[pos])) {
			saw_digit = 1;
			pos++;
			continue;
		}
		if (text[pos] == '.' && !saw_dot) {
			saw_dot = 1;
			pos++;
			continue;
		}
		return 0;
	}
	return saw_digit && saw_dot;
}

static int sqlparser_dameng_ascii_text_equal(const char *left, const char *right)
{
	size_t index;

	if (left == NULL || right == NULL) {
		return 0;
	}
	for (index = 0U; left[index] != '\0' && right[index] != '\0'; index++) {
		if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index])) {
			return 0;
		}
	}
	return left[index] == '\0' && right[index] == '\0';
}

static sqlparser_status_t sqlparser_dameng_unquote_string_literal(
	const char *text,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	size_t len;
	size_t pos;

	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	len = text != NULL ? strlen(text) : 0U;
	if (len < 2U || text[0] != '\'' || text[len - 1U] != '\'') {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	memset(&out, 0, sizeof(out));
	pos = 1U;
	while (pos + 1U < len) {
		if (text[pos] == '\'' && pos + 1U < len - 1U && text[pos + 1U] == '\'') {
			if (sqlparser_dameng_buffer_append_char(&out, '\'', out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			pos += 2U;
			continue;
		}
		if (sqlparser_dameng_buffer_append_char(&out, text[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_dameng_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_value = sqlparser_dameng_buffer_take(&out);
	return *out_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_dameng_value_fill_literal(
	sqlparser_dialect_multi_insert_value_t *value,
	sqlparser_error_t *out_error)
{
	long long integer_value;
	char *endptr;

	if (value == NULL || value->public_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_dameng_ascii_text_equal(value->public_sql, "null")) {
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}
	if (value->public_sql[0] == '\'') {
		if (sqlparser_dameng_unquote_string_literal(value->public_sql, &value->literal_string_value, out_error) != SQLPARSER_STATUS_OK) {
			return SQLPARSER_STATUS_OK;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value->literal.string_value = value->literal_string_value;
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_dameng_text_is_integer_literal(value->public_sql)) {
		endptr = NULL;
		integer_value = strtoll(value->public_sql, &endptr, 10);
		if (endptr != NULL && *endptr == '\0') {
			value->has_literal = 1;
			value->literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value->literal.integer_value = integer_value;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_dameng_text_is_float_literal(value->public_sql)) {
		value->literal_float_value = sqlparser_strdup(value->public_sql);
		if (value->literal_float_value == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_FLOAT;
		value->literal.float_value = value->literal_float_value;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_parse_value_item(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dameng_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	sqlparser_error_t *out_error)
{
	const char *key_start;
	const char *sql_start;
	size_t key_len;
	size_t sql_len;
	sqlparser_bind_kind_t bind_kind;
	size_t occurrence_before;
	sqlparser_status_t status;

	if (out_value == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	status = sqlparser_dameng_trimmed_slice_dup(sql, start, end, &out_value->public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	occurrence_before = state->bind_occurrence_count;
	status = sqlparser_dameng_preprocess_text(out_value->public_sql, state, &out_value->parser_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_value_clear(out_value);
		return status;
	}
	if (sqlparser_dameng_public_bind_token(
		    out_value->public_sql,
		    &bind_kind,
		    &key_start,
		    &key_len,
		    &sql_start,
		    &sql_len) &&
	    state->bind_occurrence_count == occurrence_before + 1U) {
		out_value->has_bind = 1;
		out_value->bind_kind = bind_kind;
		if (bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && key_start == NULL) {
			(void)snprintf(out_value->bind, sizeof(out_value->bind), "%lu", (unsigned long)(occurrence_before + 1U));
		} else {
			if (key_len >= sizeof(out_value->bind)) {
				sqlparser_dameng_value_clear(out_value);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind key is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			memcpy(out_value->bind, key_start, key_len);
			out_value->bind[key_len] = '\0';
		}
		if (sql_len >= sizeof(out_value->bind_sql)) {
			sqlparser_dameng_value_clear(out_value);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		memcpy(out_value->bind_sql, sql_start, sql_len);
		out_value->bind_sql[sql_len] = '\0';
		out_value->bind_position = occurrence_before + 1U;
		out_value->has_bind_position = 1;
	} else {
		status = sqlparser_dameng_value_fill_literal(out_value, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_value_clear(out_value);
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_parse_value_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dameng_state_t *state,
	sqlparser_dialect_multi_insert_value_t **out_values,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_value_t *values;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
	size_t depth;

	if (out_values == NULL || out_count == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value list arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_values = NULL;
	*out_count = 0U;
	values = NULL;
	count = 0U;
	capacity = 0U;
	item_start = start;
	pos = start;
	depth = 0U;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? sqlparser_dameng_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (!at_end && sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (!at_end && sql[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (at_end || (depth == 0U && sql[pos] == ',')) {
			sqlparser_dialect_multi_insert_value_t *next;

			if (count == capacity) {
				size_t next_capacity;

				next_capacity = capacity == 0U ? 4U : capacity * 2U;
				if (next_capacity < capacity) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				next = (sqlparser_dialect_multi_insert_value_t *)realloc(values, next_capacity * sizeof(*next));
				if (next == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				values = next;
				capacity = next_capacity;
			}
			if (sqlparser_dameng_parse_value_item(sql, item_start, pos, state, &values[count], out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			count++;
			item_start = pos + 1U;
		}
		pos++;
	}

	*out_values = values;
	*out_count = count;
	return SQLPARSER_STATUS_OK;

fail:
	if (values != NULL) {
		size_t index;

		for (index = 0U; index < count; index++) {
			sqlparser_dameng_value_clear(&values[index]);
		}
		free(values);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_dameng_multi_insert_add_branch(
	sqlparser_dialect_multi_insert_t *multi,
	const sqlparser_dialect_multi_insert_branch_t *source,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_branch_t *next;

	if (multi == NULL || source == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert branch arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (multi->branch_count == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "too many INSERT branches");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	next = (sqlparser_dialect_multi_insert_branch_t *)realloc(
		multi->branches,
		(multi->branch_count + 1U) * sizeof(*next));
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	multi->branches = next;
	multi->branches[multi->branch_count] = *source;
	multi->branches[multi->branch_count].ordinal = multi->branch_count;
	multi->branch_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_parse_multi_insert_into(
	const char *sql,
	size_t *io_pos,
	size_t end,
	sqlparser_dameng_state_t *state,
	sqlparser_dialect_multi_insert_t *multi,
	const char *condition_public_sql,
	const char *condition_parser_sql,
	int is_else,
	size_t condition_group_id,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_branch_t branch;
	size_t pos;
	size_t relation_start;
	size_t relation_end;
	size_t columns_open;
	size_t columns_close;
	size_t values_pos;
	size_t values_open;
	size_t values_close;
	sqlparser_status_t status;

	memset(&branch, 0, sizeof(branch));
	pos = sqlparser_dameng_trim_left(sql, *io_pos, end);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "into")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT expected INTO");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_dameng_trim_left(sql, pos + strlen("into"), end);
	relation_start = pos;
	columns_open = end;
	values_pos = end;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_dameng_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			columns_open = pos;
			break;
		}
		if (sqlparser_dameng_ascii_word_equal(sql, pos, "values")) {
			values_pos = pos;
			break;
		}
		pos++;
	}
	if (columns_open == end && values_pos == end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	relation_end = columns_open != end ? columns_open : values_pos;
	status = sqlparser_dameng_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!sqlparser_dameng_find_matching_paren(sql, columns_open, end, &columns_close)) {
			sqlparser_dameng_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_dameng_parse_column_list(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_multi_insert_branch_clear(&branch);
			return status;
		}
		pos = sqlparser_dameng_trim_left(sql, columns_close + 1U, end);
	} else {
		pos = values_pos;
	}
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "values")) {
		sqlparser_dameng_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_dameng_trim_left(sql, pos + strlen("values"), end);
	if (pos >= end || sql[pos] != '(') {
		sqlparser_dameng_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT VALUES list expected '('");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	values_open = pos;
	if (!sqlparser_dameng_find_matching_paren(sql, values_open, end, &values_close)) {
		sqlparser_dameng_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_dameng_parse_value_list(
		sql,
		values_open + 1U,
		values_close,
		state,
		&branch.cells,
		&branch.cell_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_multi_insert_branch_clear(&branch);
		return status;
	}
	if (condition_public_sql != NULL) {
		branch.condition_public_sql = sqlparser_strdup(condition_public_sql);
		branch.condition_parser_sql = sqlparser_strdup(condition_parser_sql != NULL ? condition_parser_sql : condition_public_sql);
		if (branch.condition_public_sql == NULL || branch.condition_parser_sql == NULL) {
			sqlparser_dameng_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		branch.has_condition = 1;
	}
	branch.is_else = is_else;
	branch.condition_group_id = condition_group_id;
	status = sqlparser_dameng_multi_insert_add_branch(multi, &branch, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_multi_insert_branch_clear(&branch);
		return status;
	}
	*io_pos = values_close + 1U;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_is_multi_insert_start(
	const char *sql,
	sqlparser_dialect_multi_insert_mode_t *out_mode)
{
	size_t len;
	size_t pos;

	if (out_mode != NULL) {
		*out_mode = SQLPARSER_DIALECT_MULTI_INSERT_NONE;
	}
	if (sql == NULL) {
		return 0;
	}
	len = strlen(sql);
	pos = sqlparser_dameng_trim_left(sql, 0U, len);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "insert")) {
		return 0;
	}
	pos = sqlparser_dameng_trim_left(sql, pos + strlen("insert"), len);
	if (sqlparser_dameng_ascii_word_equal(sql, pos, "all")) {
		if (out_mode != NULL) {
			*out_mode = SQLPARSER_DIALECT_MULTI_INSERT_ALL;
		}
		return 1;
	}
	if (sqlparser_dameng_ascii_word_equal(sql, pos, "first")) {
		if (out_mode != NULL) {
			*out_mode = SQLPARSER_DIALECT_MULTI_INSERT_FIRST;
		}
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_dameng_parse_multi_insert(
	const char *input_sql,
	sqlparser_dameng_state_t *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_t *multi;
	sqlparser_dialect_multi_insert_mode_t mode;
	sqlparser_dameng_buffer_t parser;
	size_t len;
	size_t end;
	size_t pos;
	size_t condition_group_id;
	sqlparser_status_t status;

	if (out_parser_sql == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert parser arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (!sqlparser_dameng_is_multi_insert_start(input_sql, &mode)) {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	len = strlen(input_sql);
	pos = sqlparser_dameng_trim_left(input_sql, 0U, len);
	end = sqlparser_dameng_trim_right(input_sql, pos, len);
	if (end > pos && input_sql[end - 1U] == ';') {
		end = sqlparser_dameng_trim_right(input_sql, pos, end - 1U);
	}
	pos = sqlparser_dameng_trim_left(input_sql, pos + strlen("insert"), end);
	pos = sqlparser_dameng_trim_left(
		input_sql,
		pos + (mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL ? strlen("all") : strlen("first")),
		end);

	multi = (sqlparser_dialect_multi_insert_t *)calloc(1U, sizeof(*multi));
	if (multi == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	multi->mode = mode;
	condition_group_id = 0U;

	while (pos < end) {
		pos = sqlparser_dameng_trim_left(input_sql, pos, end);
		if (pos >= end) {
			break;
		}
		if (sqlparser_dameng_ascii_word_equal(input_sql, pos, "select")) {
			break;
		}
		if (sqlparser_dameng_ascii_word_equal(input_sql, pos, "when")) {
			size_t condition_start;
			size_t then_pos;
			char *condition_public;
			char *condition_parser;

			condition_start = sqlparser_dameng_trim_left(input_sql, pos + strlen("when"), end);
			if (!sqlparser_dameng_find_top_level_word(input_sql, condition_start, end, "then", &then_pos)) {
				sqlparser_dameng_multi_insert_destroy(multi);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT WHEN is missing THEN");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			condition_public = NULL;
			condition_parser = NULL;
			status = sqlparser_dameng_trimmed_slice_dup(
				input_sql,
				condition_start,
				then_pos,
				&condition_public,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_preprocess_text(condition_public, state, &condition_parser, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(condition_public);
				free(condition_parser);
				sqlparser_dameng_multi_insert_destroy(multi);
				return status;
			}
			pos = sqlparser_dameng_trim_left(input_sql, then_pos + strlen("then"), end);
			condition_group_id++;
			do {
				status = sqlparser_dameng_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					condition_public,
					condition_parser,
					0,
					condition_group_id,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(condition_public);
					free(condition_parser);
					sqlparser_dameng_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_dameng_trim_left(input_sql, pos, end);
			} while (pos < end &&
			         sqlparser_dameng_ascii_word_equal(input_sql, pos, "into"));
			free(condition_public);
			free(condition_parser);
			continue;
		}
		if (sqlparser_dameng_ascii_word_equal(input_sql, pos, "else")) {
			pos = sqlparser_dameng_trim_left(input_sql, pos + strlen("else"), end);
			condition_group_id++;
			while (pos < end && sqlparser_dameng_ascii_word_equal(input_sql, pos, "into")) {
				status = sqlparser_dameng_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					NULL,
					NULL,
					1,
					condition_group_id,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_dameng_trim_left(input_sql, pos, end);
			}
			continue;
		}
		if (sqlparser_dameng_ascii_word_equal(input_sql, pos, "into")) {
			status = sqlparser_dameng_parse_multi_insert_into(
				input_sql,
				&pos,
				end,
				state,
				multi,
				NULL,
				NULL,
				0,
				0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_multi_insert_destroy(multi);
				return status;
			}
			continue;
		}
		sqlparser_dameng_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT expected INTO, WHEN, ELSE, or SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	pos = sqlparser_dameng_trim_left(input_sql, pos, end);
	if (multi->branch_count == 0U || pos >= end || !sqlparser_dameng_ascii_word_equal(input_sql, pos, "select")) {
		sqlparser_dameng_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Dameng multi-table INSERT requires branches and a source SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_dameng_trimmed_slice_dup(input_sql, pos, end, &multi->source_public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_preprocess_text_internal(
			multi->source_public_sql,
			state,
			&multi->source_parser_sql,
			origins,
			len,
			pos,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_multi_insert_destroy(multi);
		return status;
	}

	memset(&parser, 0, sizeof(parser));
	status = sqlparser_dameng_buffer_begin_origin(
		&parser,
		multi->source_parser_sql,
		origins,
		strlen(multi->source_parser_sql),
		0U,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(
			&parser,
			"INSERT INTO ",
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_generated_identifier(
			&parser,
			"sqlparser_dameng_multi_insert_source",
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(
			&parser,
			" ",
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_input_mem(
			&parser,
			multi->source_parser_sql,
			0U,
			strlen(multi->source_parser_sql),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&parser, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_commit_origin(
			&parser,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&parser);
		sqlparser_dameng_multi_insert_destroy(multi);
		return status;
	}

	sqlparser_dameng_multi_insert_destroy(state->multi_insert);
	state->multi_insert = multi;
	*out_parser_sql = sqlparser_dameng_buffer_take(&parser);
	return *out_parser_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_dameng_render_multi_insert(
	const sqlparser_dialect_multi_insert_t *multi,
	const char *postprocessed_core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	size_t source_pos;
	size_t core_len;
	size_t branch_index;
	sqlparser_status_t status;

	if (multi == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert render arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	core_len = postprocessed_core_sql != NULL ? strlen(postprocessed_core_sql) : 0U;
	if (postprocessed_core_sql == NULL ||
	    !sqlparser_dameng_find_top_level_word(postprocessed_core_sql, 0U, core_len, "select", &source_pos)) {
		postprocessed_core_sql = multi->source_public_sql;
		source_pos = 0U;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_append_cstr(&out, "INSERT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(
			&out,
			multi->mode == SQLPARSER_DIALECT_MULTI_INSERT_FIRST ? "FIRST" : "ALL",
			out_error);
	}
	for (branch_index = 0U; status == SQLPARSER_STATUS_OK && branch_index < multi->branch_count; branch_index++) {
		const sqlparser_dialect_multi_insert_branch_t *branch;
		const sqlparser_dialect_multi_insert_branch_t *previous_branch;
		int starts_group;
		size_t index;

		branch = &multi->branches[branch_index];
		previous_branch = branch_index > 0U ? &multi->branches[branch_index - 1U] : NULL;
		starts_group = previous_branch == NULL ||
			previous_branch->condition_group_id != branch->condition_group_id ||
			previous_branch->has_condition != branch->has_condition ||
			previous_branch->is_else != branch->is_else;
		status = sqlparser_dameng_buffer_append_char(&out, ' ', out_error);
		if (status == SQLPARSER_STATUS_OK && branch->has_condition && starts_group) {
			status = sqlparser_dameng_buffer_append_cstr(&out, "WHEN ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, branch->condition_public_sql, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, " THEN ", out_error);
			}
		} else if (status == SQLPARSER_STATUS_OK && branch->is_else && starts_group) {
			status = sqlparser_dameng_buffer_append_cstr(&out, "ELSE ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_cstr(&out, "INTO ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_cstr(&out, branch->relation.sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && branch->column_count > 0U) {
			status = sqlparser_dameng_buffer_append_cstr(&out, " (", out_error);
			for (index = 0U; status == SQLPARSER_STATUS_OK && index < branch->column_count; index++) {
				if (index > 0U) {
					status = sqlparser_dameng_buffer_append_cstr(&out, ", ", out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_dameng_buffer_append_cstr(&out, branch->columns[index].sql, out_error);
				}
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_char(&out, ')', out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_cstr(&out, " VALUES (", out_error);
		}
		for (index = 0U; status == SQLPARSER_STATUS_OK && index < branch->cell_count; index++) {
			if (index > 0U) {
				status = sqlparser_dameng_buffer_append_cstr(&out, ", ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, branch->cells[index].public_sql, out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_char(&out, ')', out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_char(&out, ' ', out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(&out, postprocessed_core_sql + source_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	return *out_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_dameng_buffer_append_session_value(
	sqlparser_dameng_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	int can_unquote;
	int first_char;

	if (value_start < value_end && *value_start == '"' && *(value_end - 1) == '"') {
		can_unquote = 1;
		first_char = 1;
		pos = value_start + 1;
		while (pos < value_end - 1) {
			if (*pos == '"' && pos + 1 < value_end - 1 && *(pos + 1) == '"') {
				can_unquote = 0;
				break;
			}
			if (isupper((unsigned char)*pos) ||
			    (first_char && !sqlparser_dameng_is_ident_start((unsigned char)*pos)) ||
			    (!first_char && !sqlparser_dameng_is_ident_char((unsigned char)*pos))) {
				can_unquote = 0;
				break;
			}
			first_char = 0;
			pos++;
		}
		if (can_unquote && !first_char) {
			return sqlparser_dameng_buffer_append_mem(
				out,
				value_start + 1,
				(size_t)(value_end - value_start - 2),
				out_error);
		}
	}

	return sqlparser_dameng_buffer_append_mem(
		out,
		value_start,
		(size_t)(value_end - value_start),
		out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_session_parameter_value(
	sqlparser_dameng_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	if (value_start < value_end && *value_start == '"' && *(value_end - 1) == '"') {
		status = sqlparser_dameng_buffer_append_char(out, '\'', out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos = value_start + 1;
		while (pos < value_end - 1) {
			if (*pos == '"' && pos + 1 < value_end - 1 && *(pos + 1) == '"') {
				status = sqlparser_dameng_buffer_append_char(out, '"', out_error);
				pos += 2;
			} else if (*pos == '\'') {
				status = sqlparser_dameng_buffer_append_cstr(out, "''", out_error);
				pos++;
			} else {
				status = sqlparser_dameng_buffer_append_char(out, *pos, out_error);
				pos++;
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		return sqlparser_dameng_buffer_append_char(out, '\'', out_error);
	}

	return sqlparser_dameng_buffer_append_session_value(out, value_start, value_end, out_error);
}

static size_t sqlparser_dameng_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_dameng_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			break;
		}
		index++;
	}
	return index;
}

static sqlparser_status_t sqlparser_dameng_postprocess_session_switch(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	const char *parameter_name;
	const char *name_start;
	const char *name_end;
	const char *prefix;
	const char *prefix_to;
	const char *prefix_eq;
	const char *value_start;
	const char *value_end;
	size_t start;
	size_t end;
	size_t prefix_len;
	size_t pos;
	size_t parameter_name_len;
	int is_session_parameter;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	start = sqlparser_dameng_skip_space(core_sql, 0U);
	end = sqlparser_dameng_trim_right(core_sql, start, strlen(core_sql));
	parameter_name = NULL;
	parameter_name_len = 0U;
	value_start = NULL;
	is_session_parameter = 0;
	prefix_to = "SET " SQLPARSER_INTERNAL_CURRENT_SCHEMA " TO ";
	prefix_eq = "SET " SQLPARSER_INTERNAL_CURRENT_SCHEMA " = ";
	prefix_len = strlen(prefix_to);
	if (end - start >= prefix_len && strncmp(core_sql + start, prefix_to, prefix_len) == 0) {
		parameter_name = "CURRENT_SCHEMA";
		parameter_name_len = strlen(parameter_name);
		value_start = core_sql + start + prefix_len;
	} else {
		prefix_len = strlen(prefix_eq);
		if (end - start >= prefix_len && strncmp(core_sql + start, prefix_eq, prefix_len) == 0) {
			parameter_name = "CURRENT_SCHEMA";
			parameter_name_len = strlen(parameter_name);
			value_start = core_sql + start + prefix_len;
		} else {
			prefix = "SET ";
			prefix_len = strlen(prefix);
			if (end - start < prefix_len || strncmp(core_sql + start, prefix, prefix_len) != 0) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_dameng_skip_space(core_sql, start + prefix_len);
			if (pos >= end) {
				return SQLPARSER_STATUS_OK;
			}
			if (core_sql[pos] == '"') {
				name_start = core_sql + pos + 1U;
				pos++;
				while (pos < end) {
					if (core_sql[pos] == '"') {
						if (pos + 1U < end && core_sql[pos + 1U] == '"') {
							pos += 2U;
							continue;
						}
						break;
					}
					pos++;
				}
				if (pos >= end) {
					return SQLPARSER_STATUS_OK;
				}
				name_end = core_sql + pos;
				pos++;
			} else {
				name_start = core_sql + pos;
				while (pos < end && !isspace((unsigned char)core_sql[pos])) {
					pos++;
				}
				name_end = core_sql + pos;
			}
			prefix = SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX;
			prefix_len = strlen(prefix);
			if ((size_t)(name_end - name_start) <= prefix_len ||
			    strncmp(name_start, prefix, prefix_len) != 0) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_dameng_skip_space(core_sql, pos);
			if (sqlparser_dameng_ascii_word_equal(core_sql, pos, "to")) {
				pos = sqlparser_dameng_skip_space(core_sql, pos + strlen("to"));
			} else if (pos < end && core_sql[pos] == '=') {
				pos = sqlparser_dameng_skip_space(core_sql, pos + 1U);
			} else {
				return SQLPARSER_STATUS_OK;
			}
			if (pos >= end) {
				return SQLPARSER_STATUS_OK;
			}
			parameter_name = name_start + prefix_len;
			parameter_name_len = (size_t)(name_end - parameter_name);
			value_start = core_sql + pos;
			is_session_parameter = 1;
		}
	}

	if ((size_t)(value_start - core_sql) >= end) {
		return SQLPARSER_STATUS_OK;
	}
	value_end = core_sql + end;
	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_append_cstr(&out, "ALTER SESSION SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_mem(&out, parameter_name, parameter_name_len, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = is_session_parameter ?
			sqlparser_dameng_buffer_append_session_parameter_value(
				&out,
				value_start,
				value_end,
				out_error) :
			sqlparser_dameng_buffer_append_session_value(
				&out,
				value_start,
				value_end,
				out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	size_t pos;
	size_t token_start;
	size_t token_end;
	char quote;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal argument output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	pos = sqlparser_dameng_skip_space(sql, *index);
	if (sql[pos] == '\'' || sql[pos] == '"') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_dameng_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_dameng_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_dameng_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_dameng_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_dameng_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal Dameng prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = sqlparser_dameng_trim_right(sql, token_start, pos);
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal Dameng prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	*out_value = sqlparser_strndup(sql + token_start, token_end - token_start);
	if (*out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_internal_set_prefix(
	const char *sql,
	const char *internal_name,
	size_t *out_pos)
{
	size_t pos;
	size_t len;

	pos = sqlparser_dameng_skip_space(sql, 0U);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "set")) {
		return 0;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + strlen("set"));
	len = strlen(internal_name);
	if (strncmp(sql + pos, internal_name, len) != 0 ||
	    sqlparser_dameng_is_ident_char((unsigned char)sql[pos + len])) {
		return 0;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + len);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "to") && sql[pos] != '=') {
		return 0;
	}
	pos = sql[pos] == '=' ? pos + 1U : pos + strlen("to");
	*out_pos = pos;
	return 1;
}

static sqlparser_status_t sqlparser_dameng_postprocess_internal_statement(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	static const struct {
		const char *internal_name;
		const char *prefix;
		const char *middle;
		int needs_second_arg;
	} specs[] = {
		{SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE, "EXEC SQL PREPARE ", " FROM ", 1},
		{SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE, "EXEC SQL EXECUTE ", " USING ", 0},
		{SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE, "EXEC SQL DEALLOCATE PREPARE ", NULL, 0},
		{SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT, "", NULL, 0}
	};
	sqlparser_dameng_buffer_t out;
	char *arg0;
	char *arg1;
	size_t pos;
	size_t index;
	size_t spec_index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	for (spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); spec_index++) {
		if (!sqlparser_dameng_internal_set_prefix(core_sql, specs[spec_index].internal_name, &pos)) {
			continue;
		}
		arg0 = NULL;
		arg1 = NULL;
		index = pos;
		status = sqlparser_dameng_read_internal_string_arg(core_sql, &index, &arg0, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(arg0);
			return status;
		}
		index = sqlparser_dameng_skip_space(core_sql, index);
		if (core_sql[index] == ',') {
			index++;
			status = sqlparser_dameng_read_internal_string_arg(core_sql, &index, &arg1, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(arg0);
				free(arg1);
				return status;
			}
			index = sqlparser_dameng_skip_space(core_sql, index);
		}
		if (core_sql[index] != '\0') {
			free(arg0);
			free(arg1);
			return SQLPARSER_STATUS_OK;
		}
		if (specs[spec_index].needs_second_arg && arg1 == NULL) {
			free(arg0);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "prepared statement SQL argument is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		memset(&out, 0, sizeof(out));
		status = sqlparser_dameng_buffer_append_cstr(&out, specs[spec_index].prefix, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_cstr(&out, arg0, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && arg1 != NULL && specs[spec_index].middle != NULL) {
			status = sqlparser_dameng_buffer_append_cstr(&out, specs[spec_index].middle, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, arg1, out_error);
			}
		}
		free(arg0);
		free(arg1);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_dameng_buffer_take(&out);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_rewrite_session_switches(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	const char *sql;
	char *statement_sql;
	char *rewritten_sql;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_dameng_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_dameng_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_dameng_postprocess_session_switch(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_dameng_postprocess_internal_statement(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_dameng_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = sqlparser_dameng_skip_space(sql, segment_start);
			status = sqlparser_dameng_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_dameng_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

int sqlparser_dameng_state_has_multi_insert(const void *state)
{
	const sqlparser_dameng_state_t *dameng_state;

	dameng_state = (const sqlparser_dameng_state_t *)state;
	return dameng_state != NULL &&
		dameng_state->multi_insert != NULL &&
		dameng_state->multi_insert->mode != SQLPARSER_DIALECT_MULTI_INSERT_NONE;
}

const sqlparser_dialect_multi_insert_t *sqlparser_dameng_state_multi_insert(const void *state)
{
	const sqlparser_dameng_state_t *dameng_state;

	dameng_state = (const sqlparser_dameng_state_t *)state;
	return dameng_state != NULL ? dameng_state->multi_insert : NULL;
}

const sqlparser_dialect_returning_into_state_t *
sqlparser_dameng_state_returning_into(const void *state)
{
	const sqlparser_dameng_state_t *dameng_state;

	dameng_state = (const sqlparser_dameng_state_t *)state;
	return dameng_state != NULL ? &dameng_state->returning_into : NULL;
}

static sqlparser_status_t sqlparser_dameng_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	public_sql = NULL;
	status = sqlparser_dameng_postprocess_text(core_sql, (const sqlparser_dameng_state_t *)state, &public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_restore_database_links(&public_sql, (const sqlparser_dameng_state_t *)state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	if (sqlparser_dameng_state_has_multi_insert(state)) {
		char *multi_sql;

		multi_sql = NULL;
		status = sqlparser_dameng_render_multi_insert(
			sqlparser_dameng_state_multi_insert(state),
			public_sql,
			&multi_sql,
			out_error);
		free(public_sql);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		public_sql = multi_sql;
	}
	status = sqlparser_dameng_apply_top_public(&public_sql, (const sqlparser_dameng_state_t *)state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	status = sqlparser_dameng_rewrite_session_switches(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	status = sqlparser_dialect_returning_into_postprocess(
		SQLPARSER_DIALECT_DAMENG,
		&public_sql,
		sqlparser_dameng_state_returning_into(state),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	status = sqlparser_dialect_rewrite_like_escape(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_postprocess_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	sqlparser_fragment_context_t fragment_context,
	ProtobufCMessage *const *roots,
	size_t root_count,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_status_t status;

	(void)statement_index;
	(void)fragment_context;
	(void)roots;
	(void)root_count;
	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"core SQL fragment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	public_sql = NULL;
	status = sqlparser_dameng_postprocess_text(
		core_sql,
		(const sqlparser_dameng_state_t *)state,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_restore_database_links(
		&public_sql,
		(const sqlparser_dameng_state_t *)state,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_rewrite_like_escape(&public_sql, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}

	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_multi_insert_clone(
	const sqlparser_dialect_multi_insert_t *source,
	sqlparser_dialect_multi_insert_t **out_clone,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_t *clone;
	size_t branch_index;

	if (out_clone == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert clone output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_clone = NULL;
	if (source == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	clone = (sqlparser_dialect_multi_insert_t *)calloc(1U, sizeof(*clone));
	if (clone == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	clone->mode = source->mode;
	if (source->source_public_sql != NULL) {
		clone->source_public_sql = sqlparser_strdup(source->source_public_sql);
	}
	if (source->source_parser_sql != NULL) {
		clone->source_parser_sql = sqlparser_strdup(source->source_parser_sql);
	}
	if ((source->source_public_sql != NULL && clone->source_public_sql == NULL) ||
	    (source->source_parser_sql != NULL && clone->source_parser_sql == NULL)) {
		sqlparser_dameng_multi_insert_destroy(clone);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (source->branch_count > 0U) {
		clone->branches = (sqlparser_dialect_multi_insert_branch_t *)calloc(source->branch_count, sizeof(*clone->branches));
		if (clone->branches == NULL) {
			sqlparser_dameng_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->branch_count = source->branch_count;
	}
	for (branch_index = 0U; branch_index < source->branch_count; branch_index++) {
		const sqlparser_dialect_multi_insert_branch_t *src_branch;
		sqlparser_dialect_multi_insert_branch_t *dst_branch;
		size_t index;

		src_branch = &source->branches[branch_index];
		dst_branch = &clone->branches[branch_index];
		dst_branch->ordinal = src_branch->ordinal;
		if (src_branch->relation.database_name != NULL) {
			dst_branch->relation.database_name = sqlparser_strdup(src_branch->relation.database_name);
		}
		if (src_branch->relation.schema_name != NULL) {
			dst_branch->relation.schema_name = sqlparser_strdup(src_branch->relation.schema_name);
		}
		if (src_branch->relation.table_name != NULL) {
			dst_branch->relation.table_name = sqlparser_strdup(src_branch->relation.table_name);
		}
		if (src_branch->relation.sql != NULL) {
			dst_branch->relation.sql = sqlparser_strdup(src_branch->relation.sql);
		}
		if ((src_branch->relation.database_name != NULL && dst_branch->relation.database_name == NULL) ||
		    (src_branch->relation.schema_name != NULL && dst_branch->relation.schema_name == NULL) ||
		    (src_branch->relation.table_name != NULL && dst_branch->relation.table_name == NULL) ||
		    (src_branch->relation.sql != NULL && dst_branch->relation.sql == NULL)) {
			sqlparser_dameng_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (src_branch->column_count > 0U) {
			dst_branch->columns = (sqlparser_dialect_multi_insert_column_t *)calloc(src_branch->column_count, sizeof(*dst_branch->columns));
			if (dst_branch->columns == NULL) {
				sqlparser_dameng_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			dst_branch->column_count = src_branch->column_count;
		}
		for (index = 0U; index < src_branch->column_count; index++) {
			if (src_branch->columns[index].name != NULL) {
				dst_branch->columns[index].name = sqlparser_strdup(src_branch->columns[index].name);
			}
			if (src_branch->columns[index].sql != NULL) {
				dst_branch->columns[index].sql = sqlparser_strdup(src_branch->columns[index].sql);
			}
			if ((src_branch->columns[index].name != NULL && dst_branch->columns[index].name == NULL) ||
			    (src_branch->columns[index].sql != NULL && dst_branch->columns[index].sql == NULL)) {
				sqlparser_dameng_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
		}
		if (src_branch->cell_count > 0U) {
			dst_branch->cells = (sqlparser_dialect_multi_insert_value_t *)calloc(src_branch->cell_count, sizeof(*dst_branch->cells));
			if (dst_branch->cells == NULL) {
				sqlparser_dameng_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			dst_branch->cell_count = src_branch->cell_count;
		}
		for (index = 0U; index < src_branch->cell_count; index++) {
			dst_branch->cells[index] = src_branch->cells[index];
			dst_branch->cells[index].public_sql = src_branch->cells[index].public_sql != NULL ?
				sqlparser_strdup(src_branch->cells[index].public_sql) :
				NULL;
			dst_branch->cells[index].parser_sql = src_branch->cells[index].parser_sql != NULL ?
				sqlparser_strdup(src_branch->cells[index].parser_sql) :
				NULL;
			dst_branch->cells[index].literal_string_value = src_branch->cells[index].literal_string_value != NULL ?
				sqlparser_strdup(src_branch->cells[index].literal_string_value) :
				NULL;
			dst_branch->cells[index].literal_float_value = src_branch->cells[index].literal_float_value != NULL ?
				sqlparser_strdup(src_branch->cells[index].literal_float_value) :
				NULL;
			if ((src_branch->cells[index].public_sql != NULL && dst_branch->cells[index].public_sql == NULL) ||
			    (src_branch->cells[index].parser_sql != NULL && dst_branch->cells[index].parser_sql == NULL) ||
			    (src_branch->cells[index].literal_string_value != NULL && dst_branch->cells[index].literal_string_value == NULL) ||
			    (src_branch->cells[index].literal_float_value != NULL && dst_branch->cells[index].literal_float_value == NULL)) {
				sqlparser_dameng_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			if (dst_branch->cells[index].literal.kind == SQLPARSER_LITERAL_KIND_STRING) {
				dst_branch->cells[index].literal.string_value = dst_branch->cells[index].literal_string_value;
			} else if (dst_branch->cells[index].literal.kind == SQLPARSER_LITERAL_KIND_FLOAT) {
				dst_branch->cells[index].literal.float_value = dst_branch->cells[index].literal_float_value;
			}
		}
		dst_branch->has_condition = src_branch->has_condition;
		dst_branch->is_else = src_branch->is_else;
		dst_branch->condition_group_id = src_branch->condition_group_id;
		if (src_branch->condition_public_sql != NULL) {
			dst_branch->condition_public_sql = sqlparser_strdup(src_branch->condition_public_sql);
		}
		if (src_branch->condition_parser_sql != NULL) {
			dst_branch->condition_parser_sql = sqlparser_strdup(src_branch->condition_parser_sql);
		}
		if ((src_branch->condition_public_sql != NULL && dst_branch->condition_public_sql == NULL) ||
		    (src_branch->condition_parser_sql != NULL && dst_branch->condition_parser_sql == NULL)) {
			sqlparser_dameng_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	*out_clone = clone;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_bind_key_is_digits(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0') {
		return 0;
	}
	for (index = 0U; key[index] != '\0'; index++) {
		if (!isdigit((unsigned char)key[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_dameng_bind_key_is_identifier(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0' || !sqlparser_dameng_is_ident_start((unsigned char)key[0])) {
		return 0;
	}
	for (index = 1U; key[index] != '\0'; index++) {
		if (!sqlparser_dameng_is_ident_char((unsigned char)key[index])) {
			return 0;
		}
	}
	return 1;
}

sqlparser_status_t sqlparser_dameng_render_bind_value(
	const sqlparser_bind_value_t *bind,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char buffer[SQLPARSER_BIND_SQL_CAPACITY];

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (bind == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (bind->kind == SQLPARSER_BIND_KIND_POSITIONAL) {
		if (bind->key == NULL || bind->key[0] == '\0') {
			*out_sql = sqlparser_strdup("?");
		} else {
			if (!sqlparser_dameng_bind_key_is_digits(bind->key)) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "positional bind key must be numeric");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			if (snprintf(buffer, sizeof(buffer), ":%s", bind->key) >= (int)sizeof(buffer)) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			*out_sql = sqlparser_strdup(buffer);
		}
	} else if (bind->kind == SQLPARSER_BIND_KIND_NAMED) {
		if (!sqlparser_dameng_bind_key_is_identifier(bind->key)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "named bind key is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (snprintf(buffer, sizeof(buffer), ":%s", bind->key) >= (int)sizeof(buffer)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		*out_sql = sqlparser_strdup(buffer);
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind kind must be positional or named");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dameng_render_literal_value(
	const sqlparser_literal_value_t *value,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	char buffer[64];
	const char *text;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (value->kind == SQLPARSER_LITERAL_KIND_NULL) {
		*out_sql = sqlparser_strdup("NULL");
	} else if (value->kind == SQLPARSER_LITERAL_KIND_INTEGER) {
		(void)snprintf(buffer, sizeof(buffer), "%lld", value->integer_value);
		*out_sql = sqlparser_strdup(buffer);
	} else if (value->kind == SQLPARSER_LITERAL_KIND_FLOAT) {
		if (value->float_value == NULL || value->float_value[0] == '\0') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "float literal value must not be empty");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_sql = sqlparser_strdup(value->float_value);
	} else if (value->kind == SQLPARSER_LITERAL_KIND_BOOLEAN) {
		*out_sql = sqlparser_strdup(value->boolean_value ? "TRUE" : "FALSE");
	} else if (value->kind == SQLPARSER_LITERAL_KIND_STRING) {
		text = value->string_value != NULL ? value->string_value : "";
		memset(&out, 0, sizeof(out));
		status = sqlparser_dameng_buffer_append_char(&out, '\'', out_error);
		for (index = 0U; status == SQLPARSER_STATUS_OK && text[index] != '\0'; index++) {
			if (text[index] == '\'') {
				status = sqlparser_dameng_buffer_append_cstr(&out, "''", out_error);
			} else {
				status = sqlparser_dameng_buffer_append_char(&out, text[index], out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_char(&out, '\'', out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_dameng_buffer_take(&out);
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "literal kind is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dameng_multi_insert_set_cell_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *replacement;
	sqlparser_dameng_state_t *state;
	sqlparser_dialect_multi_insert_t *multi;
	sqlparser_dialect_multi_insert_branch_t *branch;
	sqlparser_parse_options_t options;
	char *public_sql;
	sqlparser_status_t status;

	if (handle == NULL || sql_text == NULL || sql_text[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert cell replacement arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement_index != 0U || !sqlparser_dameng_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a Dameng multi-table INSERT cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	replacement = NULL;
	public_sql = NULL;
	state = (sqlparser_dameng_state_t *)handle->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (column_index >= branch->cell_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	free(branch->cells[column_index].public_sql);
	branch->cells[column_index].public_sql = sqlparser_strdup(sql_text);
	if (branch->cells[column_index].public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	handle->generation++;
	status = sqlparser_deparse(handle, &public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_DAMENG;
	options.limits = handle->limits;
	status = sqlparser_parse_with_options(public_sql, &options, &replacement, out_error);
	free(public_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	replacement->generation = handle->generation;
	sqlparser_handle_replace_contents(handle, replacement);
	sqlparser_handle_destroy(replacement);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dameng_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_dameng_state_t *state;
	const sqlparser_dialect_multi_insert_t *multi;
	const sqlparser_dialect_multi_insert_branch_t *branch;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || statement_index != 0U || !sqlparser_dameng_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a Dameng multi-table INSERT cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	state = (const sqlparser_dameng_state_t *)handle->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (column_index >= branch->cell_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = sqlparser_strdup(branch->cells[column_index].public_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dameng_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_dameng_state_t *state;
	const sqlparser_dialect_multi_insert_t *multi;
	const sqlparser_dialect_multi_insert_branch_t *branch;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || statement_index != 0U || !sqlparser_dameng_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a Dameng multi-table INSERT condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	state = (const sqlparser_dameng_state_t *)handle->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (!branch->has_condition || branch->condition_public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch has no condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = sqlparser_strdup(branch->condition_public_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dameng_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *candidate;
	sqlparser_handle_t *replacement;
	sqlparser_dameng_state_t *state;
	sqlparser_dialect_multi_insert_t *multi;
	sqlparser_dialect_multi_insert_branch_t *branch;
	sqlparser_dialect_multi_insert_column_t new_column;
	sqlparser_dialect_multi_insert_value_t new_cell;
	sqlparser_dialect_multi_insert_column_t *next_columns;
	sqlparser_dialect_multi_insert_value_t *next_cells;
	sqlparser_parse_options_t options;
	char *public_sql;
	size_t column_insert_index;
	size_t cell_insert_index;
	size_t index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || column_sql == NULL || column_sql[0] == '\0' ||
	    cell_sql == NULL || cell_sql[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert column insertion arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement_index != 0U || !sqlparser_dameng_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a Dameng multi-table INSERT branch");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&new_column, 0, sizeof(new_column));
	memset(&new_cell, 0, sizeof(new_cell));
	candidate = NULL;
	replacement = NULL;
	next_columns = NULL;
	next_cells = NULL;
	public_sql = NULL;

	status = sqlparser_handle_clone(handle, &candidate, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state = (sqlparser_dameng_state_t *)candidate->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (branch->column_count == SIZE_MAX || branch->cell_count == SIZE_MAX) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "multi insert branch is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	status = sqlparser_dameng_trimmed_slice_dup(column_sql, 0U, strlen(column_sql), &new_column.sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_identifier_from_sql(
			column_sql,
			0U,
			strlen(column_sql),
			&new_column.name,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_parse_value_item(
			cell_sql,
			0U,
			strlen(cell_sql),
			state,
			&new_cell,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_column_clear(&new_column);
		sqlparser_dameng_value_clear(&new_cell);
		sqlparser_handle_destroy(candidate);
		return status;
	}

	next_columns = (sqlparser_dialect_multi_insert_column_t *)calloc(branch->column_count + 1U, sizeof(*next_columns));
	next_cells = (sqlparser_dialect_multi_insert_value_t *)calloc(branch->cell_count + 1U, sizeof(*next_cells));
	if (next_columns == NULL || next_cells == NULL) {
		free(next_columns);
		free(next_cells);
		sqlparser_dameng_column_clear(&new_column);
		sqlparser_dameng_value_clear(&new_cell);
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	column_insert_index = column_index > branch->column_count ? branch->column_count : column_index;
	cell_insert_index = column_index > branch->cell_count ? branch->cell_count : column_index;
	for (index = 0U; index < column_insert_index; index++) {
		next_columns[index] = branch->columns[index];
	}
	next_columns[column_insert_index] = new_column;
	memset(&new_column, 0, sizeof(new_column));
	for (index = column_insert_index; index < branch->column_count; index++) {
		next_columns[index + 1U] = branch->columns[index];
	}
	for (index = 0U; index < cell_insert_index; index++) {
		next_cells[index] = branch->cells[index];
	}
	next_cells[cell_insert_index] = new_cell;
	memset(&new_cell, 0, sizeof(new_cell));
	for (index = cell_insert_index; index < branch->cell_count; index++) {
		next_cells[index + 1U] = branch->cells[index];
	}
	free(branch->columns);
	free(branch->cells);
	branch->columns = next_columns;
	branch->column_count++;
	branch->cells = next_cells;
	branch->cell_count++;
	next_columns = NULL;
	next_cells = NULL;

	candidate->generation++;
	status = sqlparser_deparse(candidate, &public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_DAMENG;
		options.limits = handle->limits;
		status = sqlparser_parse_with_options(public_sql, &options, &replacement, out_error);
	}
	free(public_sql);
	sqlparser_handle_destroy(candidate);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	replacement->generation = handle->generation + 1UL;
	sqlparser_handle_replace_contents(handle, replacement);
	sqlparser_handle_destroy(replacement);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_dameng_state_t *source;
	sqlparser_dameng_state_t *clone;
	size_t index;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	source = (const sqlparser_dameng_state_t *)state;
	status = sqlparser_dameng_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->bind_occurrence_count = source->bind_occurrence_count;
	clone->next_dblink_id = source->next_dblink_id;
	clone->limit_count = source->limit_count;
	for (index = 0U; index < source->bind_count; index++) {
		size_t param_index;

		status = sqlparser_dameng_state_append_bind(
			clone,
			source->bind_names[index],
			strlen(source->bind_names[index]),
			&param_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_state_destroy(clone);
			return status;
		}
		(void)param_index;
	}
	for (index = 0U; index < source->top_count; index++) {
		status = sqlparser_dameng_state_append_top_clause(
			clone,
			source->top_restores[index].clause,
			strlen(source->top_restores[index].clause),
			source->top_restores[index].limit_ordinal,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_state_destroy(clone);
			return status;
		}
	}
	clone->top_fragment_start = clone->top_count;
	clone->fragment_limit_base = clone->limit_count;
	status = sqlparser_dialect_national_literals_clone(
		&source->national_literals,
		&clone->national_literals,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_state_destroy(clone);
		return status;
	}
	status = sqlparser_dialect_minuses_clone(
		&source->minuses,
		&clone->minuses,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_state_destroy(clone);
		return status;
	}
	for (index = 0U; index < source->dblink_count; index++) {
		status = sqlparser_dameng_state_append_dblink_relation(
			clone,
			source->dblink_relations[index].parser_object_name,
			source->dblink_relations[index].public_object_name,
			source->dblink_relations[index].public_link_name,
			source->dblink_relations[index].public_object_sql,
			strlen(source->dblink_relations[index].public_object_sql),
			source->dblink_relations[index].public_link_sql,
			strlen(source->dblink_relations[index].public_link_sql),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_state_destroy(clone);
			return status;
		}
	}
	status = sqlparser_dameng_multi_insert_clone(source->multi_insert, &clone->multi_insert, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_state_destroy(clone);
		return status;
	}
	status = sqlparser_dialect_returning_into_state_clone(
		&source->returning_into,
		&clone->returning_into,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_state_destroy(clone);
		return status;
	}
	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_dameng_relation_object_name(
	const void *state,
	const char *parser_object_name,
	const char **out_spelling)
{
	const sqlparser_dameng_dblink_relation_t *relation;

	if (out_spelling != NULL) {
		*out_spelling = NULL;
	}
	relation = sqlparser_dameng_state_find_dblink_relation(
		(const sqlparser_dameng_state_t *)state,
		parser_object_name);
	if (relation != NULL && out_spelling != NULL) {
		*out_spelling = relation->public_object_sql;
	}
	return relation != NULL ? relation->public_object_name : NULL;
}

static const char *sqlparser_dameng_relation_link_name(
	const void *state,
	const char *parser_object_name)
{
	const sqlparser_dameng_dblink_relation_t *relation;

	relation = sqlparser_dameng_state_find_dblink_relation(
		(const sqlparser_dameng_state_t *)state,
		parser_object_name);
	return relation != NULL ? relation->public_link_name : NULL;
}

static sqlparser_status_t sqlparser_dameng_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	const PgQuery__AConst *literal_owner,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_dialect_national_literal_t *literal;
	const sqlparser_dameng_state_t *dameng_state;
	size_t literal_end;
	sqlparser_dameng_buffer_t out;
	sqlparser_status_t status;

	(void)statement_index;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	dameng_state = (const sqlparser_dameng_state_t *)state;
	literal = dameng_state != NULL ?
		sqlparser_dialect_national_literals_find_owner(
			&dameng_state->national_literals, literal_owner) :
		NULL;
	literal_end = core_sql[0] == '\'' ? sqlparser_dameng_quoted_literal_end(core_sql, 0U) : 0U;
	if (literal_end > 0U &&
	    core_sql[literal_end] == '\0' &&
	    literal != NULL &&
	    sqlparser_dameng_national_literal_matches(
		    dameng_state, literal->ordinal, core_sql, 0U, literal_end)) {
		memset(&out, 0, sizeof(out));
		status = sqlparser_dameng_buffer_append_char(&out, 'N', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_cstr(&out, core_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_dameng_buffer_take(&out);
		return SQLPARSER_STATUS_OK;
	}

	*out_sql = sqlparser_strdup(core_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static const PgQuery__VariableSetStmt *sqlparser_dameng_session_set_statement(
	const PgQuery__Node *statement)
{
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    statement->variable_set_stmt->name == NULL) {
		return NULL;
	}
	return statement->variable_set_stmt;
}

static const char *sqlparser_dameng_session_raw_argument(
	const PgQuery__VariableSetStmt *set_stmt,
	const char *internal_name)
{
	const PgQuery__Node *arg;

	if (set_stmt == NULL || internal_name == NULL ||
	    strcmp(set_stmt->name, internal_name) != 0 ||
	    set_stmt->n_args != 1U || set_stmt->args == NULL) {
		return NULL;
	}
	arg = set_stmt->args[0];
	if (arg == NULL ||
	    arg->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    arg->a_const == NULL ||
	    arg->a_const->val_case != PG_QUERY__A__CONST__VAL_SVAL ||
	    arg->a_const->sval == NULL) {
		return NULL;
	}
	return arg->a_const->sval->sval;
}

static sqlparser_status_t sqlparser_dameng_session_emit_ast_item(
	const PgQuery__VariableSetStmt *set_stmt,
	sqlparser_graph_session_action_t action,
	sqlparser_graph_session_target_kind_t target_kind,
	const char *name,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;
	const char *text;
	size_t item_index;
	size_t index;

	if (emitter->set_action(emitter->context, action, out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    target_kind,
		    name,
		    name != NULL ? strlen(name) : 0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	for (index = 0U; index < set_stmt->n_args; index++) {
		if (emitter->add_ast_value(
			    emitter->context,
			    item_index,
			    NULL,
			    set_stmt->args[index],
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	text = NULL;
	if (set_stmt->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_SET_DEFAULT) {
		text = "DEFAULT";
	} else if (set_stmt->kind == PG_QUERY__VARIABLE_SET_KIND__VAR_SET_CURRENT) {
		text = "CURRENT";
	}
	if (set_stmt->n_args == 0U && text != NULL) {
		memset(&value, 0, sizeof(value));
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD;
		value.text = text;
		value.text_length = strlen(text);
		return emitter->add_value(
			emitter->context,
			item_index,
			&value,
			out_error);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_session_emit_text(
	const sqlparser_dialect_session_emitter_t *emitter,
	size_t item_index,
	const char *name,
	sqlparser_graph_session_value_kind_t kind,
	const char *text,
	size_t text_length,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;

	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? strlen(name) : 0U;
	value.kind = kind;
	value.text = text;
	value.text_length = text_length;
	return emitter->add_value(
		emitter->context,
		item_index,
		&value,
		out_error);
}

static int sqlparser_dameng_session_parse_integer(
	const char *sql,
	size_t start,
	size_t end,
	long long *out_value)
{
	unsigned long long value;
	unsigned int digit;

	if (sql == NULL || out_value == NULL || start >= end) {
		return 0;
	}
	value = 0U;
	while (start < end) {
		digit = (unsigned int)(sql[start] - '0');
		if (value > ((unsigned long long)LLONG_MAX - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		start++;
	}
	*out_value = (long long)value;
	return 1;
}

static sqlparser_status_t sqlparser_dameng_session_project_raw(
	const char *sql,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_action_t action;
	sqlparser_dialect_session_value_t value;
	long long integer_value;
	size_t end;
	size_t item_index;
	size_t name_start;
	size_t name_end;
	size_t pos;
	size_t value_start;
	size_t value_end;

	end = strlen(sql);
	pos = sqlparser_dameng_skip_space(sql, 0U);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "alter")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng session statement is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + strlen("alter"));
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "session")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng session statement is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + strlen("session"));
	action = SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN;
	if (sqlparser_dameng_ascii_word_equal(sql, pos, "enable")) {
		action = SQLPARSER_GRAPH_SESSION_ACTION_ENABLE;
	} else if (sqlparser_dameng_ascii_word_equal(sql, pos, "disable")) {
		action = SQLPARSER_GRAPH_SESSION_ACTION_DISABLE;
	}
	if (action != SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN) {
		pos = sqlparser_dameng_skip_space(
			sql,
			pos + (action == SQLPARSER_GRAPH_SESSION_ACTION_ENABLE ?
				strlen("enable") : strlen("disable")));
		name_start = pos;
		name_end = sqlparser_dameng_trim_right(sql, name_start, end);
		if (emitter->set_action(emitter->context, action, out_error) != SQLPARSER_STATUS_OK ||
		    emitter->add_item(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			    sql + name_start,
			    name_end - name_start,
			    NULL,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "set")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng session action is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + strlen("set"));
	name_start = pos + 1U;
	name_end = sqlparser_dameng_session_value_token_end(sql, pos, out_error);
	if (name_end <= name_start + 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng session parameter is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	name_end--;
	pos = sqlparser_dameng_skip_space(sql, name_end + 1U);
	if (pos >= end || sql[pos] != '=') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng session assignment is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	value_start = sqlparser_dameng_skip_space(sql, pos + 1U);
	value_end = value_start;
	while (value_end < end && isdigit((unsigned char)sql[value_end])) {
		value_end++;
	}
	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
		    sql + name_start,
		    name_end - name_start,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	memset(&value, 0, sizeof(value));
	if (sqlparser_dameng_session_parse_integer(
		    sql, value_start, value_end, &integer_value)) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
		value.literal.integer_value = integer_value;
	} else {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION;
		value.text = sql + value_start;
		value.text_length = value_end - value_start;
	}
	if (emitter->add_value(
		    emitter->context,
		    item_index,
		    &value,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_dameng_skip_space(sql, value_end);
	return pos < end ?
		sqlparser_dameng_session_emit_text(
			emitter,
			item_index,
			"purge",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			sql + pos,
			end - pos,
			out_error) :
		SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_project_session(
	const sqlparser_handle_t *handle,
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	const PgQuery__VariableSetStmt *set_stmt;
	const char *name;
	const char *raw_sql;
	size_t prefix_length;

	(void)handle;
	(void)state;
	(void)statement_index;
	if (emitter == NULL || emitter->set_action == NULL ||
	    emitter->add_item == NULL || emitter->add_value == NULL ||
	    emitter->add_ast_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph emitter is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	set_stmt = sqlparser_dameng_session_set_statement(statement);
	if (set_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(set_stmt->name, SQLPARSER_INTERNAL_CURRENT_SCHEMA) == 0) {
		return sqlparser_dameng_session_emit_ast_item(
			set_stmt,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			emitter,
			out_error);
	}
	prefix_length = strlen(SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX);
	if (strncmp(
		    set_stmt->name,
		    SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX,
		    prefix_length) == 0 &&
	    set_stmt->name[prefix_length] != '\0') {
		name = set_stmt->name + prefix_length;
		return sqlparser_dameng_session_emit_ast_item(
			set_stmt,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			name,
			emitter,
			out_error);
	}
	if (sqlparser_dameng_ascii_word_equal(set_stmt->name, 0U, "timezone")) {
		return sqlparser_dameng_session_emit_ast_item(
			set_stmt,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"TIME_ZONE",
			emitter,
			out_error);
	}
	raw_sql = sqlparser_dameng_session_raw_argument(
		set_stmt,
		SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT);
	return raw_sql != NULL ?
		sqlparser_dameng_session_project_raw(raw_sql, emitter, out_error) :
		SQLPARSER_STATUS_OK;
}

typedef struct {
	sqlparser_dameng_state_t *state;
	size_t record_end;
	size_t ordinal_base;
	size_t seen;
	size_t next_record;
} sqlparser_dameng_top_bind_t;

typedef struct {
	sqlparser_dameng_state_t *state;
	size_t seen;
} sqlparser_dameng_top_reconcile_t;

typedef struct {
	sqlparser_dameng_state_t *state;
	size_t source_record_count;
} sqlparser_dameng_top_clone_t;

static int sqlparser_dameng_top_owner_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dameng_top_restore_t *left_restore;
	const sqlparser_dameng_top_restore_t *right_restore;
	uintptr_t left_owner;
	uintptr_t right_owner;

	left_restore = (const sqlparser_dameng_top_restore_t *)left;
	right_restore = (const sqlparser_dameng_top_restore_t *)right;
	left_owner = (uintptr_t)left_restore->owner;
	right_owner = (uintptr_t)right_restore->owner;
	return left_owner < right_owner ? -1 : left_owner > right_owner;
}

static int sqlparser_dameng_top_ordinal_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dameng_top_restore_t *left_restore;
	const sqlparser_dameng_top_restore_t *right_restore;

	left_restore = (const sqlparser_dameng_top_restore_t *)left;
	right_restore = (const sqlparser_dameng_top_restore_t *)right;
	return left_restore->limit_ordinal < right_restore->limit_ordinal ?
		-1 : left_restore->limit_ordinal > right_restore->limit_ordinal;
}

static sqlparser_dameng_top_restore_t *sqlparser_dameng_top_find_owner(
	sqlparser_dameng_state_t *state,
	const PgQuery__SelectStmt *owner,
	size_t count)
{
	sqlparser_dameng_top_restore_t key;

	if (count == 0U) {
		return NULL;
	}
	memset(&key, 0, sizeof(key));
	key.owner = (PgQuery__SelectStmt *)owner;
	return (sqlparser_dameng_top_restore_t *)bsearch(
		&key,
		state->top_restores,
		count,
		sizeof(*state->top_restores),
		sqlparser_dameng_top_owner_compare);
}

static void sqlparser_dameng_top_bind_visit(
	PgQuery__SelectStmt *select,
	size_t statement_index,
	void *context)
{
	sqlparser_dameng_top_bind_t *bind;
	size_t ordinal;

	(void)select;
	(void)statement_index;
	bind = (sqlparser_dameng_top_bind_t *)context;
	ordinal = bind->ordinal_base + bind->seen + 1U;
	while (bind->next_record < bind->record_end &&
	       bind->state->top_restores[bind->next_record].limit_ordinal <
		       ordinal) {
		bind->next_record++;
	}
	if (bind->next_record < bind->record_end &&
	    bind->state->top_restores[bind->next_record].limit_ordinal ==
		    ordinal) {
		bind->state->top_restores[bind->next_record].owner = select;
		bind->next_record++;
	}
	bind->seen++;
}

static sqlparser_status_t sqlparser_dameng_top_bind_roots(
	sqlparser_dameng_state_t *state,
	size_t record_start,
	size_t record_end,
	size_t ordinal_base,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_dameng_top_bind_t bind;

	if (record_start >= record_end) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&bind, 0, sizeof(bind));
	bind.state = state;
	bind.record_end = record_end;
	bind.ordinal_base = ordinal_base;
	bind.next_record = record_start;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &bind;
	visitor.select_limit = sqlparser_dameng_top_bind_visit;
	sqlparser_dialect_ast_surface_visit_roots(
		roots, root_count, 0U, &visitor);
	if (bind.next_record != record_end) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"Dameng TOP AST owner is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_dameng_top_reconcile_visit(
	PgQuery__SelectStmt *select,
	size_t statement_index,
	void *context)
{
	sqlparser_dameng_top_reconcile_t *reconcile;
	sqlparser_dameng_top_restore_t *restore;

	(void)statement_index;
	reconcile = (sqlparser_dameng_top_reconcile_t *)context;
	reconcile->seen++;
	restore = sqlparser_dameng_top_find_owner(
		reconcile->state,
		select,
		reconcile->state->top_count);
	if (restore != NULL) {
		restore->limit_ordinal = reconcile->seen;
	}
}

static void sqlparser_dameng_top_reconcile(
	sqlparser_dameng_state_t *state,
	const PgQuery__ParseResult *ast)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_dameng_top_reconcile_t reconcile;
	size_t read_index;
	size_t write_index;

	if (state == NULL || ast == NULL) {
		return;
	}
	if (state->top_count > 1U) {
		qsort(
			state->top_restores,
			state->top_count,
			sizeof(*state->top_restores),
			sqlparser_dameng_top_owner_compare);
	}
	for (read_index = 0U; read_index < state->top_count; read_index++) {
		state->top_restores[read_index].limit_ordinal = SIZE_MAX;
	}
	memset(&reconcile, 0, sizeof(reconcile));
	reconcile.state = state;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &reconcile;
	visitor.select_limit = sqlparser_dameng_top_reconcile_visit;
	sqlparser_dialect_ast_surface_visit(ast, &visitor);
	write_index = 0U;
	for (read_index = 0U; read_index < state->top_count; read_index++) {
		if (state->top_restores[read_index].limit_ordinal == SIZE_MAX) {
			free(state->top_restores[read_index].clause);
			continue;
		}
		if (write_index != read_index) {
			state->top_restores[write_index] =
				state->top_restores[read_index];
		}
		write_index++;
	}
	state->top_count = write_index;
	if (state->top_count > 1U) {
		qsort(
			state->top_restores,
			state->top_count,
			sizeof(*state->top_restores),
			sqlparser_dameng_top_ordinal_compare);
	}
	state->limit_count = reconcile.seen;
	state->top_fragment_start = state->top_count;
	state->fragment_limit_base = state->limit_count;
}

static sqlparser_status_t sqlparser_dameng_top_clone_visit(
	const PgQuery__SelectStmt *source,
	PgQuery__SelectStmt *clone,
	void *context,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_top_clone_t *clone_context;
	sqlparser_dameng_top_restore_t *restore;
	sqlparser_status_t status;

	clone_context = (sqlparser_dameng_top_clone_t *)context;
	restore = sqlparser_dameng_top_find_owner(
		clone_context->state,
		source,
		clone_context->source_record_count);
	if (restore == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_state_append_top_clause(
		clone_context->state,
		restore->clause,
		strlen(restore->clause),
		SIZE_MAX,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		clone_context->state->top_restores[
			clone_context->state->top_count - 1U].owner = clone;
	}
	return status;
}

typedef struct {
	sqlparser_dameng_state_t *state;
} sqlparser_dameng_dblink_bind_t;

typedef struct {
	sqlparser_dameng_state_t *state;
	size_t source_count;
} sqlparser_dameng_dblink_clone_t;

static int sqlparser_dameng_dblink_name_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dameng_dblink_relation_t *left_relation;
	const sqlparser_dameng_dblink_relation_t *right_relation;

	left_relation = (const sqlparser_dameng_dblink_relation_t *)left;
	right_relation = (const sqlparser_dameng_dblink_relation_t *)right;
	return strcmp(
		left_relation->parser_object_name,
		right_relation->parser_object_name);
}

static size_t sqlparser_dameng_dblink_name_lower_bound(
	const sqlparser_dameng_state_t *state,
	const char *parser_object_name,
	size_t count)
{
	size_t left;
	size_t right;

	left = 0U;
	right = count;
	while (left < right) {
		size_t middle;
		int comparison;

		middle = left + (right - left) / 2U;
		comparison = strcmp(
			state->dblink_relations[middle].parser_object_name,
			parser_object_name);
		if (comparison < 0) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	return left;
}

static void sqlparser_dameng_dblink_bind_visit(
	PgQuery__RangeVar *relation,
	size_t statement_index,
	void *context)
{
	sqlparser_dameng_dblink_bind_t *bind;
	size_t index;

	(void)statement_index;
	if (relation == NULL || relation->relname == NULL) {
		return;
	}
	bind = (sqlparser_dameng_dblink_bind_t *)context;
	index = sqlparser_dameng_dblink_name_lower_bound(
		bind->state,
		relation->relname,
		bind->state->dblink_count);
	while (index < bind->state->dblink_count &&
	       strcmp(
		       bind->state->dblink_relations[index].parser_object_name,
		       relation->relname) == 0) {
		if (bind->state->dblink_relations[index].owner == NULL) {
			bind->state->dblink_relations[index].owner = relation;
			return;
		}
		index++;
	}
}

static sqlparser_status_t sqlparser_dameng_bind_dblink_state(
	sqlparser_dameng_state_t *state,
	const PgQuery__ParseResult *ast,
	size_t statement_index,
	ProtobufCMessage *const *roots,
	size_t root_count,
	int require_all,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_dameng_dblink_bind_t bind;
	size_t index;

	if (state == NULL || state->dblink_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (state->dblink_count > 1U) {
		qsort(
			state->dblink_relations,
			state->dblink_count,
			sizeof(*state->dblink_relations),
			sqlparser_dameng_dblink_name_compare);
	}
	for (index = 0U; index < state->dblink_count; index++) {
		state->dblink_relations[index].owner = NULL;
	}
	memset(&bind, 0, sizeof(bind));
	bind.state = state;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &bind;
	visitor.relation = sqlparser_dameng_dblink_bind_visit;
	visitor.insert_relation = sqlparser_dameng_dblink_bind_visit;
	if (ast != NULL) {
		sqlparser_dialect_ast_surface_visit(ast, &visitor);
	}
	if (roots != NULL && root_count > 0U) {
		sqlparser_dialect_ast_surface_visit_roots(
			roots, root_count, statement_index, &visitor);
	}
	if (require_all) {
		for (index = 0U; index < state->dblink_count; index++) {
			if (state->dblink_relations[index].owner == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"Dameng database-link AST owner is missing");
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_dameng_reconcile_dblink_state(
	sqlparser_dameng_state_t *state,
	const PgQuery__ParseResult *ast)
{
	size_t read_index;
	size_t write_index;

	if (state == NULL || ast == NULL || state->dblink_count == 0U) {
		return;
	}
	(void)sqlparser_dameng_bind_dblink_state(
		state, ast, 0U, NULL, 0U, 0, NULL);
	write_index = 0U;
	for (read_index = 0U; read_index < state->dblink_count; read_index++) {
		if (state->dblink_relations[read_index].owner == NULL) {
			sqlparser_dameng_dblink_relation_clear(
				&state->dblink_relations[read_index]);
			continue;
		}
		if (write_index != read_index) {
			state->dblink_relations[write_index] =
				state->dblink_relations[read_index];
		}
		write_index++;
	}
	state->dblink_count = write_index;
}

static sqlparser_status_t sqlparser_dameng_dblink_clone_visit(
	const PgQuery__RangeVar *source,
	PgQuery__RangeVar *clone,
	void *context,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_dblink_clone_t *clone_context;
	sqlparser_dameng_dblink_relation_t *record;
	sqlparser_status_t status;
	size_t index;

	if (source == NULL || source->relname == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	clone_context = (sqlparser_dameng_dblink_clone_t *)context;
	index = sqlparser_dameng_dblink_name_lower_bound(
		clone_context->state,
		source->relname,
		clone_context->source_count);
	record = NULL;
	while (index < clone_context->source_count &&
	       strcmp(
		       clone_context->state->dblink_relations[index]
			       .parser_object_name,
		       source->relname) == 0) {
		if (clone_context->state->dblink_relations[index].owner == source) {
			record = &clone_context->state->dblink_relations[index];
			break;
		}
		index++;
	}
	if (record == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_state_append_dblink_relation(
		clone_context->state,
		record->parser_object_name,
		record->public_object_name,
		record->public_link_name,
		record->public_object_sql,
		strlen(record->public_object_sql),
		record->public_link_sql,
		strlen(record->public_link_sql),
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		clone_context->state->dblink_relations[
			clone_context->state->dblink_count - 1U].owner = clone;
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_clone_dblink_owners(
	sqlparser_dameng_state_t *state,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_clone_visitor_t visitor;
	sqlparser_dameng_dblink_clone_t clone_context;
	sqlparser_status_t status;
	size_t original_count;

	if (state == NULL || source_root == NULL || clone_root == NULL ||
	    state->dblink_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	original_count = state->dblink_count;
	if (original_count > 1U) {
		qsort(
			state->dblink_relations,
			original_count,
			sizeof(*state->dblink_relations),
			sqlparser_dameng_dblink_name_compare);
	}
	memset(&clone_context, 0, sizeof(clone_context));
	clone_context.state = state;
	clone_context.source_count = original_count;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &clone_context;
	visitor.relation = sqlparser_dameng_dblink_clone_visit;
	status = sqlparser_dialect_ast_surface_clone(
		source_root,
		(ProtobufCMessage *)clone_root,
		&visitor,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		while (state->dblink_count > original_count) {
			state->dblink_count--;
			sqlparser_dameng_dblink_relation_clear(
				&state->dblink_relations[state->dblink_count]);
		}
		if (status == SQLPARSER_STATUS_INTERNAL_ERROR) {
			sqlparser_error_set_message(
				out_error,
				status,
				"cloned Dameng database-link AST shape is inconsistent");
		}
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_bind_ast_state(
	void *state,
	const PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *dameng_state;
	ProtobufCMessage *roots[1];
	sqlparser_status_t status;
	size_t index;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	dameng_state = (sqlparser_dameng_state_t *)state;
	if (ast == NULL) {
		sqlparser_dialect_prepared_binds_clear(
			&dameng_state->prepared_binds);
		for (index = 0U; index < dameng_state->top_count; index++) {
			dameng_state->top_restores[index].owner = NULL;
		}
	}
	status = sqlparser_dialect_national_literals_bind_ast(
		&dameng_state->national_literals,
		ast,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_minuses_bind_ast(
			&dameng_state->minuses, ast, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (ast == NULL) {
		return sqlparser_dameng_bind_dblink_state(
			dameng_state, NULL, 0U, NULL, 0U, 0, out_error);
	}
	roots[0] = (ProtobufCMessage *)ast;
	status = sqlparser_dameng_top_bind_roots(
		dameng_state,
		0U,
		dameng_state->top_count,
		0U,
		roots,
		1U,
		out_error);
	return status == SQLPARSER_STATUS_OK ?
		sqlparser_dameng_bind_dblink_state(
			dameng_state,
			ast,
			0U,
			NULL,
			0U,
			1,
			out_error) :
		status;
}

static sqlparser_status_t sqlparser_dameng_bind_fragment_ast_state(
	void *state,
	const PgQuery__ParseResult *base_ast,
	size_t statement_index,
	size_t parser_fragment_offset,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *dameng_state;
	ProtobufCMessage *base_roots[1];
	sqlparser_status_t status;

	(void)parser_fragment_offset;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	dameng_state = (sqlparser_dameng_state_t *)state;
	status = sqlparser_dialect_national_literals_bind_fragment(
		&dameng_state->national_literals,
		base_ast,
		roots,
		root_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dialect_minuses_bind_fragment(
		&dameng_state->minuses,
		base_ast,
		roots,
		root_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (dameng_state->top_fragment_start > dameng_state->top_count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"Dameng TOP fragment checkpoint is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	base_roots[0] = (ProtobufCMessage *)base_ast;
	status = sqlparser_dameng_top_bind_roots(
		dameng_state,
		0U,
		dameng_state->top_fragment_start,
		0U,
		base_roots,
		base_ast != NULL ? 1U : 0U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_top_bind_roots(
		dameng_state,
		dameng_state->top_fragment_start,
		dameng_state->top_count,
		dameng_state->fragment_limit_base,
		roots,
		root_count,
		out_error);
	return status == SQLPARSER_STATUS_OK ?
		sqlparser_dameng_bind_dblink_state(
			dameng_state,
			base_ast,
			statement_index,
			roots,
			root_count,
			1,
			out_error) :
		status;
}

static void sqlparser_dameng_reconcile_ast_state(
	void *state,
	const PgQuery__ParseResult *ast)
{
	sqlparser_dameng_state_t *dameng_state;

	if (state == NULL) {
		return;
	}
	dameng_state = (sqlparser_dameng_state_t *)state;
	sqlparser_dialect_reconcile_binds(
		&dameng_state->prepared_binds,
		&dameng_state->bind_names,
		&dameng_state->bind_count,
		&dameng_state->bind_capacity,
		&dameng_state->bind_occurrence_count);
	sqlparser_dialect_national_literals_reconcile(
		&dameng_state->national_literals,
		ast);
	sqlparser_dialect_minuses_reconcile(&dameng_state->minuses, ast);
	sqlparser_dameng_top_reconcile(dameng_state, ast);
	sqlparser_dameng_reconcile_dblink_state(dameng_state, ast);
}

static sqlparser_status_t sqlparser_dameng_clone_ast_state(
	void *state,
	size_t statement_index,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_clone_visitor_t visitor;
	sqlparser_dameng_top_clone_t clone_context;
	sqlparser_dameng_state_t *dameng_state;
	sqlparser_status_t status;
	size_t original_top_count;
	size_t original_minus_count;
	size_t original_national_count;

	(void)statement_index;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	dameng_state = (sqlparser_dameng_state_t *)state;
	original_top_count = dameng_state->top_count;
	original_minus_count = dameng_state->minuses.count;
	original_national_count = dameng_state->national_literals.count;
	if (original_top_count > 1U) {
		qsort(
			dameng_state->top_restores,
			original_top_count,
			sizeof(*dameng_state->top_restores),
			sqlparser_dameng_top_owner_compare);
	}
	memset(&clone_context, 0, sizeof(clone_context));
	clone_context.state = dameng_state;
	clone_context.source_record_count = original_top_count;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &clone_context;
	visitor.select_limit = sqlparser_dameng_top_clone_visit;
	status = sqlparser_dialect_ast_surface_clone(
		source_root,
		(ProtobufCMessage *)clone_root,
		&visitor,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_minuses_clone_owners(
			&dameng_state->minuses,
			source_root,
			clone_root,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_national_literals_clone_owners(
			&dameng_state->national_literals,
			source_root,
			clone_root,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_clone_dblink_owners(
			dameng_state, source_root, clone_root, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		dameng_state->minuses.count = original_minus_count;
		while (dameng_state->national_literals.count >
		       original_national_count) {
			dameng_state->national_literals.count--;
			free(dameng_state->national_literals.items[
				dameng_state->national_literals.count].sql);
			free(dameng_state->national_literals.items[
				dameng_state->national_literals.count].surface_sql);
		}
		while (dameng_state->top_count > original_top_count) {
			dameng_state->top_count--;
			free(dameng_state->top_restores[
				dameng_state->top_count].clause);
		}
		if (status == SQLPARSER_STATUS_INTERNAL_ERROR) {
			sqlparser_error_set_message(
				out_error,
				status,
				"cloned Dameng TOP AST shape is inconsistent");
		}
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_prepare_ast_state(
	void *state,
	PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *dameng_state;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	dameng_state = (sqlparser_dameng_state_t *)state;
	return sqlparser_dialect_prepare_binds(
		dameng_state->bind_names,
		dameng_state->bind_count,
		ast,
		&dameng_state->prepared_binds,
		out_error);
}

static const sqlparser_dialect_ops_t SQLPARSER_DAMENG_OPS = {
	SQLPARSER_DIALECT_DAMENG,
	"dameng",
	sqlparser_dameng_preprocess,
	sqlparser_dameng_preprocess_fragment,
	sqlparser_dameng_postprocess_deparse,
	sqlparser_dameng_clone_state,
	sqlparser_dameng_state_destroy,
	sqlparser_dameng_postprocess_literal_fragment,
	NULL,
	NULL,
	sqlparser_dameng_relation_object_name,
	sqlparser_dameng_relation_link_name,
	sqlparser_dameng_postprocess_fragment,
	NULL,
	NULL,
	sqlparser_dameng_project_session,
	sqlparser_dameng_bind_ast_state,
	sqlparser_dameng_bind_fragment_ast_state,
	sqlparser_dameng_reconcile_ast_state,
	sqlparser_dameng_clone_ast_state,
	sqlparser_dameng_prepare_ast_state
};

const sqlparser_dialect_ops_t *sqlparser_dialect_dameng_ops(void)
{
	return &SQLPARSER_DAMENG_OPS;
}
