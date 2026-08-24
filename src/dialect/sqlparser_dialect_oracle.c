#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_bind_internal.h"
#include "sqlparser_dialect_ast_surface_internal.h"
#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_minus_internal.h"
#include "sqlparser_dialect_national_literal_internal.h"
#include "sqlparser_dialect_oracle_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_oracle_buffer_t;

typedef struct {
	char *parser_object_name;
	char *public_object_name;
	char *public_link_name;
	char *public_object_sql;
	char *public_link_sql;
	PgQuery__RangeVar *owner;
} sqlparser_oracle_dblink_relation_t;

typedef struct {
	char **bind_names;
	size_t bind_count;
	size_t bind_capacity;
	sqlparser_dialect_prepared_binds_t prepared_binds;
	sqlparser_dialect_national_literals_t national_literals;
	size_t bind_occurrence_count;
	sqlparser_oracle_dblink_relation_t *dblink_relations;
	size_t dblink_count;
	size_t dblink_capacity;
	size_t next_dblink_id;
	sqlparser_dialect_minuses_t minuses;
	sqlparser_dialect_multi_insert_t *multi_insert;
	sqlparser_dialect_returning_into_state_t returning_into;
} sqlparser_oracle_state_t;

static int sqlparser_oracle_is_ident_start(unsigned char c);
static int sqlparser_oracle_is_ident_char(unsigned char c);

static void sqlparser_oracle_dblink_relation_clear(
	sqlparser_oracle_dblink_relation_t *relation)
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

static void sqlparser_oracle_buffer_release(sqlparser_oracle_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
}

static sqlparser_status_t sqlparser_oracle_buffer_reserve(
	sqlparser_oracle_buffer_t *buffer,
	size_t extra,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t required;
	size_t next_capacity;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
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

static sqlparser_status_t sqlparser_oracle_buffer_append_mem(
	sqlparser_oracle_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"append data must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_oracle_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_buffer_append_char(
	sqlparser_oracle_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_oracle_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_oracle_buffer_append_cstr(
	sqlparser_oracle_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_oracle_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static char *sqlparser_oracle_buffer_take(sqlparser_oracle_buffer_t *buffer)
{
	char *data;

	if (buffer == NULL) {
		return NULL;
	}

	data = buffer->data;
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
	return data;
}

static void sqlparser_oracle_relation_clear(sqlparser_dialect_multi_insert_relation_t *relation)
{
	if (relation == NULL) {
		return;
	}
	free(relation->database_name);
	free(relation->schema_name);
	free(relation->table_name);
	free(relation->link_name);
	free(relation->link_sql);
	free(relation->sql);
	memset(relation, 0, sizeof(*relation));
}

static void sqlparser_oracle_column_clear(sqlparser_dialect_multi_insert_column_t *column)
{
	if (column == NULL) {
		return;
	}
	free(column->name);
	free(column->sql);
	memset(column, 0, sizeof(*column));
}

static void sqlparser_oracle_value_clear(sqlparser_dialect_multi_insert_value_t *value)
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

static void sqlparser_oracle_multi_insert_branch_clear(sqlparser_dialect_multi_insert_branch_t *branch)
{
	size_t index;

	if (branch == NULL) {
		return;
	}
	sqlparser_oracle_relation_clear(&branch->relation);
	for (index = 0U; index < branch->column_count; index++) {
		sqlparser_oracle_column_clear(&branch->columns[index]);
	}
	free(branch->columns);
	for (index = 0U; index < branch->cell_count; index++) {
		sqlparser_oracle_value_clear(&branch->cells[index]);
	}
	free(branch->cells);
	free(branch->condition_public_sql);
	free(branch->condition_parser_sql);
	memset(branch, 0, sizeof(*branch));
}

static void sqlparser_oracle_multi_insert_destroy(sqlparser_dialect_multi_insert_t *multi)
{
	size_t branch_index;

	if (multi == NULL) {
		return;
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		sqlparser_oracle_multi_insert_branch_clear(&multi->branches[branch_index]);
	}
	free(multi->branches);
	free(multi->source_public_sql);
	free(multi->source_parser_sql);
	free(multi);
}

static sqlparser_status_t sqlparser_oracle_buffer_finish(
	sqlparser_oracle_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_oracle_buffer_reserve(buffer, 0U, out_error);
}

static sqlparser_status_t sqlparser_oracle_buffer_reserve_input(
	sqlparser_oracle_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t input_len;
	size_t required;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	input_len = input != NULL ? strlen(input) : 0U;
	if (input_len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	required = input_len + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next = (char *)realloc(buffer->data, required);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = required;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_oracle_state_destroy(void *state)
{
	sqlparser_oracle_state_t *oracle_state;
	size_t index;

	oracle_state = (sqlparser_oracle_state_t *)state;
	if (oracle_state == NULL) {
		return;
	}

	for (index = 0U; index < oracle_state->bind_count; index++) {
		free(oracle_state->bind_names[index]);
	}
	free(oracle_state->bind_names);
	sqlparser_dialect_prepared_binds_clear(
		&oracle_state->prepared_binds);
	sqlparser_dialect_national_literals_clear(
		&oracle_state->national_literals);
	sqlparser_dialect_minuses_clear(&oracle_state->minuses);
	for (index = 0U; index < oracle_state->dblink_count; index++) {
		sqlparser_oracle_dblink_relation_clear(
			&oracle_state->dblink_relations[index]);
	}
	free(oracle_state->dblink_relations);
	sqlparser_oracle_multi_insert_destroy(oracle_state->multi_insert);
	sqlparser_dialect_returning_into_state_clear(
		&oracle_state->returning_into);
	free(oracle_state);
}

static sqlparser_status_t sqlparser_oracle_state_new(
	sqlparser_oracle_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = (sqlparser_oracle_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_store_national_literal(
	sqlparser_oracle_state_t *state,
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

static int sqlparser_oracle_national_literal_matches(
	const sqlparser_oracle_state_t *state,
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

static size_t sqlparser_oracle_identifier_token_end(const char *sql, size_t start)
{
	size_t pos;

	if (sql == NULL || sql[start] == '\0') {
		return start;
	}
	if (sql[start] == '"') {
		pos = start + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '"') {
				if (sql[pos + 1U] == '"') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return start;
	}
	if (!sqlparser_oracle_is_ident_start((unsigned char)sql[start])) {
		return start;
	}
	pos = start + 1U;
	while (sqlparser_oracle_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_oracle_identifier_unquote(
	const char *sql,
	size_t start,
	size_t end,
	char **out_name,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
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
				if (sqlparser_oracle_buffer_append_char(&out, '"', out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
				pos += 2U;
				continue;
			}
			break;
		}
		if (sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name = sqlparser_oracle_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_oracle_dblink_relation_t *sqlparser_oracle_state_find_dblink_relation(
	const sqlparser_oracle_state_t *state,
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

static sqlparser_status_t sqlparser_oracle_state_append_dblink_relation(
	sqlparser_oracle_state_t *state,
	const char *parser_object_name,
	const char *public_object_name,
	const char *public_link_name,
	const char *public_object_sql,
	size_t public_object_sql_len,
	const char *public_link_sql,
	size_t public_link_sql_len,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_dblink_relation_t copy;
	sqlparser_oracle_dblink_relation_t *next;
	sqlparser_oracle_dblink_relation_t *relation;
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
		sqlparser_oracle_dblink_relation_clear(&copy);
		sqlparser_error_set_message(
			out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (state->dblink_count == state->dblink_capacity) {
		next_capacity = state->dblink_capacity == 0U ? 4U : state->dblink_capacity * 2U;
		if (next_capacity < state->dblink_capacity) {
			sqlparser_oracle_dblink_relation_clear(&copy);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_oracle_dblink_relation_t *)realloc(
			state->dblink_relations,
			next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_oracle_dblink_relation_clear(&copy);
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

static int sqlparser_oracle_public_identifier_is_empty(
	const char *sql,
	size_t start,
	size_t end)
{
	return sql == NULL || start >= end || (sql[start] == '"' && end == start + 2U);
}

static sqlparser_status_t sqlparser_oracle_try_copy_database_link_relation(
	const char *input_sql,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_oracle_state_t *state,
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
		ident_end = sqlparser_oracle_identifier_token_end(input_sql, ident_start);
		if (ident_end == ident_start ||
		    sqlparser_oracle_public_identifier_is_empty(input_sql, ident_start, ident_end)) {
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
	link_end = sqlparser_oracle_identifier_token_end(input_sql, link_start);
	if (link_end == link_start ||
	    sqlparser_oracle_public_identifier_is_empty(input_sql, link_start, link_end)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid Oracle database link name");
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
		"sqlparser_oracle_dblink_%zu",
		state->next_dblink_id);
	if (marker_len <= 0 || (size_t)marker_len >= sizeof(marker)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "database link marker is too long");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	object_name = NULL;
	link_name = NULL;
	status = sqlparser_oracle_identifier_unquote(
		input_sql,
		part_start[part_count - 1U],
		part_end[part_count - 1U],
		&object_name,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_identifier_unquote(input_sql, link_start, link_end, &link_name, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(object_name);
		free(link_name);
		return status;
	}

	status = sqlparser_oracle_state_append_dblink_relation(
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
		status = sqlparser_oracle_buffer_append_mem(
			out,
			input_sql + start,
			part_start[part_count - 1U] - start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(out, marker, out_error);
	}
	free(object_name);
	free(link_name);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = link_end;
	return SQLPARSER_STATUS_OK;
}

int sqlparser_oracle_state_has_multi_insert(const void *state)
{
	const sqlparser_oracle_state_t *oracle_state;

	oracle_state = (const sqlparser_oracle_state_t *)state;
	return oracle_state != NULL &&
		oracle_state->multi_insert != NULL &&
		oracle_state->multi_insert->mode != SQLPARSER_DIALECT_MULTI_INSERT_NONE;
}

const sqlparser_dialect_multi_insert_t *sqlparser_oracle_state_multi_insert(const void *state)
{
	const sqlparser_oracle_state_t *oracle_state;

	oracle_state = (const sqlparser_oracle_state_t *)state;
	return oracle_state != NULL ? oracle_state->multi_insert : NULL;
}

const sqlparser_dialect_returning_into_state_t *
sqlparser_oracle_state_returning_into(const void *state)
{
	const sqlparser_oracle_state_t *oracle_state;

	oracle_state = (const sqlparser_oracle_state_t *)state;
	return oracle_state != NULL ? &oracle_state->returning_into : NULL;
}

static int sqlparser_oracle_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_';
}

static int sqlparser_oracle_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#';
}

static int sqlparser_oracle_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_oracle_is_ident_char(prev) && !sqlparser_oracle_is_ident_char(next);
}

static int sqlparser_oracle_ascii_word_equal(const char *text, size_t pos, const char *word)
{
	size_t index;
	size_t len;

	if (text == NULL || word == NULL) {
		return 0;
	}

	len = strlen(word);
	for (index = 0U; index < len; index++) {
		if (text[pos + index] == '\0') {
			return 0;
		}
		if (tolower((unsigned char)text[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}

	return sqlparser_oracle_is_word_boundary(text, pos, len);
}

static size_t sqlparser_oracle_skip_quoted_or_comment_span(
	const char *sql,
	size_t index);

static size_t sqlparser_oracle_skip_hierarchy_trivia(
	const char *input,
	size_t pos)
{
	size_t next;

	for (;;) {
		while (isspace((unsigned char)input[pos])) {
			pos++;
		}
		if (!((input[pos] == '-' && input[pos + 1U] == '-') ||
		      (input[pos] == '/' && input[pos + 1U] == '*'))) {
			return pos;
		}
		next = sqlparser_oracle_skip_quoted_or_comment_span(input, pos);
		if (next <= pos) {
			return pos;
		}
		pos = next;
	}
}

static const char *sqlparser_oracle_hierarchy_internal_keyword(
	const char *input,
	size_t pos,
	size_t *out_source_length)
{
	size_t next;

	if (sqlparser_oracle_ascii_word_equal(input, pos, "connect_by_root")) {
		*out_source_length = strlen("connect_by_root");
		return SQLPARSER_INTERNAL_HIERARCHY_CONNECT_BY_ROOT;
	}
	if (sqlparser_oracle_ascii_word_equal(input, pos, "connect")) {
		next = sqlparser_oracle_skip_hierarchy_trivia(
			input,
			pos + strlen("connect"));
		if (sqlparser_oracle_ascii_word_equal(input, next, "by")) {
			*out_source_length = strlen("connect");
			return SQLPARSER_INTERNAL_HIERARCHY_CONNECT_BY;
		}
	}
	if (sqlparser_oracle_ascii_word_equal(input, pos, "start")) {
		next = sqlparser_oracle_skip_hierarchy_trivia(
			input,
			pos + strlen("start"));
		if (sqlparser_oracle_ascii_word_equal(input, next, "with")) {
			*out_source_length = strlen("start");
			return SQLPARSER_INTERNAL_HIERARCHY_START_WITH;
		}
	}
	if (sqlparser_oracle_ascii_word_equal(input, pos, "nocycle")) {
		*out_source_length = strlen("nocycle");
		return SQLPARSER_INTERNAL_HIERARCHY_NOCYCLE;
	}
	if (sqlparser_oracle_ascii_word_equal(input, pos, "prior")) {
		*out_source_length = strlen("prior");
		return SQLPARSER_INTERNAL_HIERARCHY_PRIOR;
	}
	*out_source_length = 0U;
	return NULL;
}

static size_t sqlparser_oracle_trim_left(const char *text, size_t start, size_t end)
{
	while (start < end && isspace((unsigned char)text[start])) {
		start++;
	}
	return start;
}

static size_t sqlparser_oracle_trim_right(const char *text, size_t start, size_t end)
{
	(void)start;
	while (end > start && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	return end;
}

static size_t sqlparser_oracle_q_quote_prefix_len(const char *text)
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

static int sqlparser_oracle_is_n_string_literal(const char *text)
{
	return text != NULL &&
		(text[0] == 'n' || text[0] == 'N') &&
		text[1] == '\'';
}

static int sqlparser_oracle_copy_quoted_or_comment(
	const char *sql,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	quote = sql[*index];
	if (quote == '\'' || quote == '"') {
		pos = *index;
		if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos++;
					if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) !=
					    SQLPARSER_STATUS_OK) {
						return -1;
					}
				} else {
					pos++;
					break;
				}
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	if (sql[*index] == '-' && sql[*index + 1U] == '-') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '\n') {
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	if (sql[*index] == '/' && sql[*index + 1U] == '*') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				pos++;
				if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) !=
				    SQLPARSER_STATUS_OK) {
					return -1;
				}
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	return 0;
}

static sqlparser_status_t sqlparser_oracle_copy_n_string_literal(
	const char *input,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t literal_index;
	int copied;

	if (!sqlparser_oracle_is_n_string_literal(input + *index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid Oracle national string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	literal_index = *index + 1U;
	copied = sqlparser_oracle_copy_quoted_or_comment(input, &literal_index, out, out_error);
	if (copied < 0) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	if (copied == 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Oracle national string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	*index = literal_index;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_mask_non_code(
	const char *sql,
	char **out_masked,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t index;

	len = strlen(sql);
	masked = sqlparser_strndup(sql, len);
	if (masked == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (index = 0U; index < len; index++) {
		size_t q_prefix_len;

		q_prefix_len = sqlparser_oracle_q_quote_prefix_len(masked + index);
		if (q_prefix_len > 0U) {
			char open_delim;
			char close_delim;
			size_t prefix_pos;

			for (prefix_pos = 0U; prefix_pos <= q_prefix_len; prefix_pos++) {
				masked[index + prefix_pos] = ' ';
			}
			index += q_prefix_len + 1U;
			if (index >= len) {
				break;
			}
			open_delim = masked[index];
			close_delim = open_delim;
			if (open_delim == '[') {
				close_delim = ']';
			} else if (open_delim == '{') {
				close_delim = '}';
			} else if (open_delim == '(') {
				close_delim = ')';
			} else if (open_delim == '<') {
				close_delim = '>';
			}
			masked[index] = ' ';
			index++;
			while (index < len) {
				if (masked[index] == close_delim && masked[index + 1U] == '\'') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index++;
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '\'') {
			index++;
			while (index < len) {
				if (masked[index] == '\'' && masked[index + 1U] == '\'') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '\'') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '"') {
			index++;
			while (index < len) {
				if (masked[index] == '"' && masked[index + 1U] == '"') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '"') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '-' && masked[index + 1U] == '-') {
			while (index < len && masked[index] != '\n') {
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '/' && masked[index + 1U] == '*') {
			masked[index] = ' ';
			masked[index + 1U] = ' ';
			index += 2U;
			while (index < len) {
				if (masked[index] == '*' && masked[index + 1U] == '/') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index++;
					break;
				}
				masked[index] = ' ';
				index++;
			}
		}
	}

	for (index = 0U; index < len; index++) {
		masked[index] = (char)tolower((unsigned char)masked[index]);
	}

	*out_masked = masked;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_contains_phrase(const char *masked, const char *phrase)
{
	size_t phrase_len;
	size_t last_phrase_pos;
	int needs_left_boundary;
	int needs_right_boundary;
	size_t pos;

	if (masked == NULL || phrase == NULL || phrase[0] == '\0') {
		return 0;
	}

	phrase_len = strlen(phrase);
	last_phrase_pos = phrase_len;
	while (last_phrase_pos > 0U && isspace((unsigned char)phrase[last_phrase_pos - 1U])) {
		last_phrase_pos--;
	}
	if (last_phrase_pos == 0U) {
		return 0;
	}

	needs_left_boundary = sqlparser_oracle_is_ident_char((unsigned char)phrase[0]);
	needs_right_boundary = sqlparser_oracle_is_ident_char((unsigned char)phrase[last_phrase_pos - 1U]);

	for (pos = 0U; masked[pos] != '\0'; pos++) {
		size_t text_pos;
		size_t phrase_pos;
		int matched;

		if (needs_left_boundary && pos > 0U &&
		    sqlparser_oracle_is_ident_char((unsigned char)masked[pos - 1U])) {
			continue;
		}

		text_pos = pos;
		phrase_pos = 0U;
		matched = 1;
		while (phrase[phrase_pos] != '\0') {
			if (isspace((unsigned char)phrase[phrase_pos])) {
				int saw_space;

				saw_space = 0;
				while (isspace((unsigned char)phrase[phrase_pos])) {
					phrase_pos++;
				}
				while (isspace((unsigned char)masked[text_pos])) {
					saw_space = 1;
					text_pos++;
				}
				if (!saw_space) {
					matched = 0;
					break;
				}
				continue;
			}
			if (masked[text_pos] != phrase[phrase_pos]) {
				matched = 0;
				break;
			}
			text_pos++;
			phrase_pos++;
		}

		if (matched &&
		    (!needs_right_boundary || !sqlparser_oracle_is_ident_char((unsigned char)masked[text_pos]))) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_oracle_raw_contains_word_span(const char *sql, const char *word, size_t word_len)
{
	size_t pos;

	if (sql == NULL || word == NULL || word_len == 0U) {
		return 0;
	}

	for (pos = 0U; sql[pos] != '\0'; pos++) {
		size_t index;

		if (pos > 0U && sqlparser_oracle_is_ident_char((unsigned char)sql[pos - 1U])) {
			continue;
		}

		for (index = 0U; index < word_len; index++) {
			if (sql[pos + index] == '\0') {
				break;
			}
			if (tolower((unsigned char)sql[pos + index]) !=
			    tolower((unsigned char)word[index])) {
				break;
			}
		}
		if (index == word_len &&
		    !sqlparser_oracle_is_ident_char((unsigned char)sql[pos + word_len])) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_oracle_raw_contains_word(const char *sql, const char *word)
{
	return sqlparser_oracle_raw_contains_word_span(sql, word, word != NULL ? strlen(word) : 0U);
}

static int sqlparser_oracle_raw_may_contain_phrase(const char *sql, const char *phrase)
{
	size_t pos;
	int saw_token;

	if (sql == NULL || phrase == NULL) {
		return 0;
	}

	pos = 0U;
	saw_token = 0;
	while (phrase[pos] != '\0') {
		size_t start;
		size_t len;

		while (phrase[pos] != '\0' &&
		       !sqlparser_oracle_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		start = pos;
		while (phrase[pos] != '\0' &&
		       sqlparser_oracle_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		len = pos - start;
		if (len == 0U) {
			continue;
		}

		saw_token = 1;
		if (!sqlparser_oracle_raw_contains_word_span(sql, phrase + start, len)) {
			return 0;
		}
	}

	return saw_token;
}

static int sqlparser_oracle_starts_with_word(const char *masked, const char *word)
{
	size_t pos;

	if (masked == NULL || word == NULL) {
		return 0;
	}

	pos = 0U;
	while (isspace((unsigned char)masked[pos])) {
		pos++;
	}

	return sqlparser_oracle_ascii_word_equal(masked, pos, word);
}

static sqlparser_status_t sqlparser_oracle_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"log errors",
		"pivot",
		"unpivot",
		"match_recognize",
		"model",
		"as of scn",
		"as of timestamp",
		"versions between",
		"create package",
		"create or replace package",
		"create procedure",
		"create or replace procedure",
		"create function",
		"create or replace function",
		"create trigger",
		"create or replace trigger",
		"alter session",
		"alter system",
		"database link",
	};
	char *masked;
	sqlparser_status_t status;
	size_t index;
	int needs_mask;

	needs_mask =
		sqlparser_oracle_raw_contains_word(sql, "begin") ||
		sqlparser_oracle_raw_contains_word(sql, "declare") ||
		strstr(sql, "(+)") != NULL;
	for (index = 0U; !needs_mask &&
	     index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_oracle_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
		}
	}
	if (!needs_mask) {
		return SQLPARSER_STATUS_OK;
	}

	masked = NULL;
	status = sqlparser_oracle_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_oracle_starts_with_word(masked, "begin") ||
	    sqlparser_oracle_starts_with_word(masked, "declare")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported Oracle syntax: PL/SQL block");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (strstr(masked, "(+)") != NULL) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported Oracle syntax: legacy outer join operator (+)");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_oracle_contains_phrase(masked, unsupported_phrases[index])) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"unsupported Oracle syntax: %s",
				unsupported_phrases[index]);
			free(masked);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, message);
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	free(masked);
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_oracle_session_value_token_end(
	const char *input_sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error);

static size_t sqlparser_oracle_identifier_token_end_bounded(
	const char *sql,
	size_t start,
	size_t end);

static sqlparser_status_t sqlparser_oracle_preprocess_alter_session_switch(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *sentinel_name;
	size_t start;
	size_t end;
	size_t pos;
	size_t param_start;
	size_t param_end;
	size_t value_start;
	size_t value_end;
	size_t service_start;
	size_t service_end;
	int has_service;
	int is_generic_session_param;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_oracle_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_oracle_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_oracle_trim_right(input_sql, start, end);
	}
	pos = start;
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "alter")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("alter"), end);
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "session")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("session"), end);
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "set")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("set"), end);
	param_start = pos;
	while (pos < end && sqlparser_oracle_is_ident_char((unsigned char)input_sql[pos])) {
		pos++;
	}
	param_end = pos;
	if (param_start == param_end) {
		return SQLPARSER_STATUS_OK;
	}

	sentinel_name = NULL;
	is_generic_session_param = 0;
	if (param_end - param_start == strlen("current_schema") &&
	    sqlparser_oracle_ascii_word_equal(input_sql, param_start, "current_schema")) {
		sentinel_name = SQLPARSER_INTERNAL_CURRENT_SCHEMA;
	} else if (param_end - param_start == strlen("container") &&
	           sqlparser_oracle_ascii_word_equal(input_sql, param_start, "container")) {
		sentinel_name = SQLPARSER_INTERNAL_CURRENT_DATABASE;
	} else {
		is_generic_session_param = 1;
	}

	pos = sqlparser_oracle_trim_left(input_sql, pos, end);
	if (pos >= end || input_sql[pos] != '=') {
		return SQLPARSER_STATUS_OK;
	}
	pos++;
	value_start = sqlparser_oracle_trim_left(input_sql, pos, end);
	value_end = sqlparser_oracle_session_value_token_end(input_sql, value_start, end, out_error);
	if (value_end == 0U) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET requires a value");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (!is_generic_session_param &&
	    (input_sql[value_start] == '\'' ||
	     sqlparser_oracle_identifier_token_end_bounded(input_sql, value_start, end) != value_end)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION schema or container requires an identifier");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	has_service = 0;
	service_start = 0U;
	service_end = 0U;
	pos = sqlparser_oracle_trim_left(input_sql, value_end, end);
	if (pos < end) {
		if (is_generic_session_param ||
		    strcmp(sentinel_name, SQLPARSER_INTERNAL_CURRENT_DATABASE) != 0 ||
		    !sqlparser_oracle_ascii_word_equal(input_sql, pos, "service")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported ALTER SESSION SET suffix");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("service"), end);
		if (pos >= end || input_sql[pos] != '=') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CONTAINER SERVICE requires '='");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos++;
		service_start = sqlparser_oracle_trim_left(input_sql, pos, end);
		service_end = sqlparser_oracle_session_value_token_end(input_sql, service_start, end, out_error);
		if (service_end == 0U) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (service_start >= service_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CONTAINER SERVICE requires a value");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (input_sql[service_start] == '\'' ||
		    sqlparser_oracle_identifier_token_end_bounded(input_sql, service_start, end) != service_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CONTAINER SERVICE requires an identifier");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_oracle_trim_left(input_sql, service_end, end);
		if (pos != end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported ALTER SESSION SET CONTAINER suffix");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		has_service = 1;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_char(&out, '"', out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_cstr(
			&out,
			SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_mem(
			&out,
			input_sql + param_start,
			param_end - param_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_char(&out, '"', out_error);
	}
	if (status == SQLPARSER_STATUS_OK && !is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_cstr(&out, sentinel_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_mem(
			&out,
			input_sql + value_start,
			value_end - value_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_cstr(&out, ", ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_mem(
			&out,
			input_sql + service_start,
			service_end - service_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_append_internal_string_literal(
	sqlparser_oracle_buffer_t *out,
	const char *input_sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (input_sql[pos] == '\'') {
			status = sqlparser_oracle_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_oracle_buffer_append_char(out, input_sql[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_oracle_buffer_append_char(out, '\'', out_error);
}

static sqlparser_status_t sqlparser_oracle_preprocess_execute_immediate(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t start;
	size_t end;
	size_t pos;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_oracle_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_oracle_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_oracle_trim_right(input_sql, start, end);
	}
	if (!sqlparser_oracle_ascii_word_equal(input_sql, start, "execute")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, start + strlen("execute"), end);
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "immediate")) {
		return SQLPARSER_STATUS_OK;
	}
	value_start = sqlparser_oracle_trim_left(input_sql, pos + strlen("immediate"), end);
	value_end = sqlparser_oracle_trim_right(input_sql, value_start, end);
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXECUTE IMMEDIATE requires SQL text");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_append_internal_string_literal(&out, input_sql, value_start, value_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_oracle_session_value_token_end(
	const char *input_sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;
	size_t q_prefix_len;

	if (start >= end) {
		return start;
	}

	q_prefix_len = sqlparser_oracle_q_quote_prefix_len(input_sql + start);
	if (q_prefix_len > 0U) {
		pos = sqlparser_oracle_skip_quoted_or_comment_span(
			input_sql,
			start);
		if (pos <= end) {
			return pos;
		}
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"unterminated ALTER SESSION value");
		return 0U;
	}
	quote = input_sql[start];
	if (quote == '\'' || quote == '"') {
		pos = start + 1U;
		while (pos < end) {
			if (input_sql[pos] == quote) {
				if (pos + 1U < end && input_sql[pos + 1U] == quote) {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated ALTER SESSION value");
		return 0U;
	}

	pos = start;
	while (pos < end && !isspace((unsigned char)input_sql[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_oracle_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	char open_delim;
	char close_delim;
	size_t q_prefix_len;
	size_t pos;

	q_prefix_len = sqlparser_oracle_q_quote_prefix_len(sql + index);
	if (q_prefix_len > 0U) {
		pos = index + q_prefix_len + 1U;
		if (sql[pos] == '\0') {
			return pos;
		}
		open_delim = sql[pos];
		close_delim = open_delim;
		if (open_delim == '[') {
			close_delim = ']';
		} else if (open_delim == '{') {
			close_delim = '}';
		} else if (open_delim == '(') {
			close_delim = ')';
		} else if (open_delim == '<') {
			close_delim = '>';
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sql[pos] == close_delim && sql[pos + 1U] == '\'') {
				return pos + 2U;
			}
			pos++;
		}
		return pos;
	}

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

static size_t sqlparser_oracle_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, index);
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

static size_t sqlparser_oracle_skip_leading_trivia(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t skipped;

	pos = start;
	for (;;) {
		while (pos < end && isspace((unsigned char)sql[pos])) {
			pos++;
		}
		if (pos >= end ||
		    !((sql[pos] == '-' && pos + 1U < end && sql[pos + 1U] == '-') ||
		      (sql[pos] == '/' && pos + 1U < end && sql[pos + 1U] == '*'))) {
			return pos;
		}
		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped <= pos) {
			return pos;
		}
		pos = skipped < end ? skipped : end;
	}
}

static size_t sqlparser_oracle_skip_space_bounded(const char *sql, size_t pos, size_t end)
{
	while (pos < end && isspace((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static int sqlparser_oracle_word_at_bounded(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word)
{
	size_t index;
	size_t word_len;
	unsigned char prev;
	unsigned char next;

	if (sql == NULL || word == NULL || pos >= end) {
		return 0;
	}
	word_len = strlen(word);
	if (word_len == 0U || pos + word_len > end) {
		return 0;
	}
	for (index = 0U; index < word_len; index++) {
		if (tolower((unsigned char)sql[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	prev = pos == 0U ? 0U : (unsigned char)sql[pos - 1U];
	next = pos + word_len >= end ? 0U : (unsigned char)sql[pos + word_len];
	return !sqlparser_oracle_is_ident_char(prev) && !sqlparser_oracle_is_ident_char(next);
}

static size_t sqlparser_oracle_identifier_token_end_bounded(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (sql == NULL || start >= end) {
		return start;
	}
	if (sql[start] == '"') {
		pos = start + 1U;
		while (pos < end) {
			if (sql[pos] == '"') {
				if (pos + 1U < end && sql[pos + 1U] == '"') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return start;
	}
	if (!sqlparser_oracle_is_ident_start((unsigned char)sql[start])) {
		return start;
	}
	pos = start + 1U;
	while (pos < end && sqlparser_oracle_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_oracle_multipart_identifier_end_bounded(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t next;
	size_t part_count;

	pos = sqlparser_oracle_identifier_token_end_bounded(sql, start, end);
	if (pos <= start) {
		return start;
	}
	part_count = 1U;
	while (pos < end && sql[pos] == '.') {
		next = sqlparser_oracle_identifier_token_end_bounded(sql, pos + 1U, end);
		if (next <= pos + 1U) {
			break;
		}
		part_count++;
		pos = next;
		if (part_count >= 3U) {
			break;
		}
	}
	return pos;
}

static int sqlparser_oracle_find_word_bounded(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	size_t scan;
	size_t skipped;

	if (sql == NULL || word == NULL) {
		return 0;
	}
	scan = pos;
	while (scan < end) {
		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, scan);
		if (skipped > scan) {
			scan = skipped > end ? end : skipped;
			continue;
		}
		if (sqlparser_oracle_word_at_bounded(sql, scan, end, word)) {
			if (out_pos != NULL) {
				*out_pos = scan;
			}
			return 1;
		}
		scan++;
	}
	return 0;
}

static int sqlparser_oracle_create_synonym_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t name_start;
	size_t name_end;
	size_t for_pos;
	size_t tail_start;
	size_t target_start;

	name_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	name_end = sqlparser_oracle_multipart_identifier_end_bounded(sql, name_start, end);
	if (name_end <= name_start) {
		return 0;
	}
	tail_start = sqlparser_oracle_skip_space_bounded(sql, name_end, end);
	if (!sqlparser_oracle_find_word_bounded(sql, tail_start, end, "for", &for_pos)) {
		return 0;
	}
	if (tail_start < for_pos && !sqlparser_oracle_word_at_bounded(sql, tail_start, for_pos, "sharing")) {
		return 0;
	}
	target_start = sqlparser_oracle_skip_space_bounded(sql, for_pos + strlen("for"), end);
	return target_start < end;
}

static int sqlparser_oracle_drop_synonym_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t name_start;
	size_t name_end;
	size_t tail_start;

	name_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	name_end = sqlparser_oracle_multipart_identifier_end_bounded(sql, name_start, end);
	if (name_end <= name_start) {
		return 0;
	}
	tail_start = sqlparser_oracle_skip_space_bounded(sql, name_end, end);
	if (tail_start >= end) {
		return 1;
	}
	if (!sqlparser_oracle_word_at_bounded(sql, tail_start, end, "force")) {
		return 0;
	}
	tail_start = sqlparser_oracle_skip_space_bounded(sql, tail_start + strlen("force"), end);
	return tail_start >= end;
}

static int sqlparser_oracle_explain_plan_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t for_pos;
	size_t statement_start;

	if (!sqlparser_oracle_find_word_bounded(sql, pos, end, "for", &for_pos)) {
		return 0;
	}
	statement_start = sqlparser_oracle_skip_space_bounded(sql, for_pos + strlen("for"), end);
	return statement_start < end;
}

static int sqlparser_oracle_consume_word_bounded(
	const char *sql,
	size_t *pos,
	size_t end,
	const char *word)
{
	size_t scan;

	if (sql == NULL || pos == NULL || word == NULL) {
		return 0;
	}
	scan = sqlparser_oracle_skip_space_bounded(sql, *pos, end);
	if (!sqlparser_oracle_word_at_bounded(sql, scan, end, word)) {
		return 0;
	}
	*pos = scan + strlen(word);
	return 1;
}

static int sqlparser_oracle_tail_words_equal(
	const char *sql,
	size_t pos,
	size_t end,
	const char *const *words,
	size_t word_count)
{
	size_t index;

	for (index = 0U; index < word_count; index++) {
		if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, words[index])) {
			return 0;
		}
	}
	return sqlparser_oracle_skip_space_bounded(sql, pos, end) == end;
}

static int sqlparser_oracle_session_raw_is_complete(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t q_prefix_len;
	size_t skipped;
	size_t depth;

	depth = 0U;
	pos = start;
	while (pos < end) {
		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			char close_delimiter;
			char open_delimiter;
			size_t delimiter_pos;

			if (skipped > end) {
				return 0;
			}
			if ((sql[pos] == '\'' || sql[pos] == '"') &&
			    (skipped <= pos + 1U || sql[skipped - 1U] != sql[pos])) {
				return 0;
			}
			q_prefix_len = sqlparser_oracle_q_quote_prefix_len(sql + pos);
			if (q_prefix_len > 0U) {
				delimiter_pos = pos + q_prefix_len + 1U;
				if (delimiter_pos >= end) {
					return 0;
				}
				open_delimiter = sql[delimiter_pos];
				close_delimiter = open_delimiter;
				if (open_delimiter == '[') {
					close_delimiter = ']';
				} else if (open_delimiter == '{') {
					close_delimiter = '}';
				} else if (open_delimiter == '(') {
					close_delimiter = ')';
				} else if (open_delimiter == '<') {
					close_delimiter = '>';
				}
				if (skipped < delimiter_pos + 3U ||
				    sql[skipped - 2U] != close_delimiter ||
				    sql[skipped - 1U] != '\'') {
					return 0;
				}
			}
			if (sql[pos] == '/' && sql[pos + 1U] == '*' &&
			    (skipped <= pos + 3U || sql[skipped - 2U] != '*' || sql[skipped - 1U] != '/')) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
		}
		pos++;
	}
	return depth == 0U;
}

static int sqlparser_oracle_multi_session_assignments_are_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t assignment_count;
	size_t parameter_end;
	size_t value_end;
	size_t value_start;

	assignment_count = 0U;
	while (pos < end) {
		pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
		parameter_end = sqlparser_oracle_identifier_token_end_bounded(sql, pos, end);
		if (parameter_end <= pos) {
			return 0;
		}
		pos = sqlparser_oracle_skip_space_bounded(sql, parameter_end, end);
		if (pos >= end || sql[pos] != '=') {
			return 0;
		}
		value_start = sqlparser_oracle_skip_space_bounded(sql, pos + 1U, end);
		value_end = sqlparser_oracle_session_value_token_end(
			sql, value_start, end, NULL);
		if (value_end <= value_start ||
		    (sql[value_start] != '\'' && sql[value_start] != '"' &&
		     sql[value_end - 1U] == ',')) {
			return 0;
		}
		assignment_count++;
		pos = sqlparser_oracle_skip_space_bounded(sql, value_end, end);
	}
	return assignment_count >= 2U;
}

static int sqlparser_oracle_alter_session_raw_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	static const char *const advise_commit[] = {"advise", "commit"};
	static const char *const advise_rollback[] = {"advise", "rollback"};
	static const char *const advise_nothing[] = {"advise", "nothing"};
	static const char *const disable_resumable[] = {"disable", "resumable"};
	static const char *const enable_commit[] = {"enable", "commit", "in", "procedure"};
	static const char *const disable_commit[] = {"disable", "commit", "in", "procedure"};
	static const char *const enable_guard[] = {"enable", "guard"};
	static const char *const disable_guard[] = {"disable", "guard"};
	static const char *const enable_shard_ddl[] = {"enable", "shard", "ddl"};
	static const char *const disable_shard_ddl[] = {"disable", "shard", "ddl"};
	static const char *const sync_primary[] = {"sync", "with", "primary"};
	size_t scan;
	size_t token_end;

	if (!sqlparser_oracle_session_raw_is_complete(sql, pos, end)) {
		return 0;
	}
	scan = pos;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "set")) {
		size_t value_start;

		value_start = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		if (sqlparser_oracle_word_at_bounded(sql, value_start, end, "current_schema") ||
		    sqlparser_oracle_word_at_bounded(sql, value_start, end, "container")) {
			return 0;
		}
		if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "row") &&
		    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "archival") &&
		    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "visibility")) {
			scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
			if (scan >= end || sql[scan] != '=') {
				return 0;
			}
			scan++;
			return (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "all") ||
			        sqlparser_oracle_consume_word_bounded(sql, &scan, end, "active")) &&
				sqlparser_oracle_skip_space_bounded(sql, scan, end) == end;
		}
		scan = pos;
		if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "set") &&
		    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "default") &&
		    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "collation")) {
			scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
			if (scan >= end || sql[scan] != '=') {
				return 0;
			}
			value_start = sqlparser_oracle_skip_space_bounded(sql, scan + 1U, end);
			token_end = sqlparser_oracle_identifier_token_end_bounded(sql, value_start, end);
			return token_end > value_start &&
				sqlparser_oracle_skip_space_bounded(sql, token_end, end) == end;
		}
		return sqlparser_oracle_multi_session_assignments_are_supported(
			sql, value_start, end);
	}
	if (sqlparser_oracle_tail_words_equal(
		    sql, pos, end, advise_commit, sizeof(advise_commit) / sizeof(advise_commit[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, advise_rollback, sizeof(advise_rollback) / sizeof(advise_rollback[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, advise_nothing, sizeof(advise_nothing) / sizeof(advise_nothing[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, disable_resumable, sizeof(disable_resumable) / sizeof(disable_resumable[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, enable_commit, sizeof(enable_commit) / sizeof(enable_commit[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, disable_commit, sizeof(disable_commit) / sizeof(disable_commit[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, enable_guard, sizeof(enable_guard) / sizeof(enable_guard[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, disable_guard, sizeof(disable_guard) / sizeof(disable_guard[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, enable_shard_ddl, sizeof(enable_shard_ddl) / sizeof(enable_shard_ddl[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, disable_shard_ddl, sizeof(disable_shard_ddl) / sizeof(disable_shard_ddl[0])) ||
	    sqlparser_oracle_tail_words_equal(
		    sql, pos, end, sync_primary, sizeof(sync_primary) / sizeof(sync_primary[0]))) {
		return 1;
	}

	scan = pos;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "close") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "database") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "link")) {
		scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		token_end = sqlparser_oracle_multipart_identifier_end_bounded(sql, scan, end);
		return token_end > scan &&
			sqlparser_oracle_skip_space_bounded(sql, token_end, end) == end;
	}

	scan = pos;
	if ((sqlparser_oracle_consume_word_bounded(sql, &scan, end, "enable") ||
	     sqlparser_oracle_consume_word_bounded(sql, &scan, end, "disable")) &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "parallel") &&
	    (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "dml") ||
	     sqlparser_oracle_consume_word_bounded(sql, &scan, end, "ddl") ||
	     sqlparser_oracle_consume_word_bounded(sql, &scan, end, "query"))) {
		return sqlparser_oracle_skip_space_bounded(sql, scan, end) == end;
	}

	scan = pos;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "force") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "parallel") &&
	    (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "dml") ||
	     sqlparser_oracle_consume_word_bounded(sql, &scan, end, "ddl") ||
	     sqlparser_oracle_consume_word_bounded(sql, &scan, end, "query"))) {
		if (sqlparser_oracle_skip_space_bounded(sql, scan, end) == end) {
			return 1;
		}
		if (!sqlparser_oracle_consume_word_bounded(sql, &scan, end, "parallel")) {
			return 0;
		}
		scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		token_end = scan;
		while (token_end < end && isdigit((unsigned char)sql[token_end])) {
			token_end++;
		}
		return token_end > scan &&
			sqlparser_oracle_skip_space_bounded(sql, token_end, end) == end;
	}

	scan = pos;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "enable") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "resumable")) {
		if (sqlparser_oracle_skip_space_bounded(sql, scan, end) == end) {
			return 1;
		}
		if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "timeout")) {
			scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
			token_end = scan;
			while (token_end < end && isdigit((unsigned char)sql[token_end])) {
				token_end++;
			}
			if (token_end == scan) {
				return 0;
			}
			scan = token_end;
		}
		if (sqlparser_oracle_skip_space_bounded(sql, scan, end) == end) {
			return 1;
		}
		if (!sqlparser_oracle_consume_word_bounded(sql, &scan, end, "name")) {
			return 0;
		}
		scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		if (scan >= end ||
		    (sql[scan] != '\'' &&
		     sqlparser_oracle_q_quote_prefix_len(sql + scan) == 0U)) {
			return 0;
		}
		token_end = sqlparser_oracle_session_value_token_end(
			sql, scan, end, NULL);
		return token_end > scan && token_end <= end &&
			sqlparser_oracle_skip_space_bounded(sql, token_end, end) == end;
	}

	return 0;
}

static sqlparser_status_t sqlparser_oracle_build_internal_raw_statement(
	const char *internal_name,
	const char *input_sql,
	size_t start,
	size_t end,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_status_t status;

	if (internal_name == NULL || input_sql == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal statement arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	memset(&out, 0, sizeof(out));

	status = sqlparser_oracle_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, internal_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_append_internal_string_literal(&out, input_sql, start, end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_preprocess_raw_statement(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t end;
	size_t pos;
	const char *internal_name;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_oracle_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_oracle_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_oracle_trim_right(input_sql, start, end);
	}
	if (start >= end) {
		return SQLPARSER_STATUS_OK;
	}

	internal_name = NULL;
	if (sqlparser_oracle_word_at_bounded(input_sql, start, end, "create")) {
		pos = sqlparser_oracle_skip_space_bounded(input_sql, start + strlen("create"), end);
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "or")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("or"), end);
			if (!sqlparser_oracle_word_at_bounded(input_sql, pos, end, "replace")) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("replace"), end);
		}
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "editionable") ||
		    sqlparser_oracle_word_at_bounded(input_sql, pos, end, "noneditionable")) {
			pos = sqlparser_oracle_skip_space_bounded(
				input_sql,
				pos + (tolower((unsigned char)input_sql[pos]) == 'n' ?
					strlen("noneditionable") :
					strlen("editionable")),
				end);
		}
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "public")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("public"), end);
		}
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "synonym")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("synonym"), end);
			if (sqlparser_oracle_create_synonym_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM;
			}
		}
	} else if (sqlparser_oracle_word_at_bounded(input_sql, start, end, "drop")) {
		pos = sqlparser_oracle_skip_space_bounded(input_sql, start + strlen("drop"), end);
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "public")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("public"), end);
		}
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "synonym")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("synonym"), end);
			if (sqlparser_oracle_drop_synonym_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM;
			}
		}
	} else if (sqlparser_oracle_word_at_bounded(input_sql, start, end, "explain")) {
		pos = sqlparser_oracle_skip_space_bounded(input_sql, start + strlen("explain"), end);
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "plan")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("plan"), end);
			if (sqlparser_oracle_explain_plan_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN;
			}
		}
	} else if (sqlparser_oracle_word_at_bounded(input_sql, start, end, "alter")) {
		pos = sqlparser_oracle_skip_space_bounded(input_sql, start + strlen("alter"), end);
		if (sqlparser_oracle_word_at_bounded(input_sql, pos, end, "session")) {
			pos = sqlparser_oracle_skip_space_bounded(input_sql, pos + strlen("session"), end);
			if (sqlparser_oracle_alter_session_raw_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT;
			}
		}
	}
	if (internal_name == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	return sqlparser_oracle_build_internal_raw_statement(
		internal_name,
		input_sql,
		start,
		end,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_rewrite_alter_session_switches(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	char *statement_sql;
	char *rewritten_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	len = strlen(input_sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_oracle_statement_end(input_sql, segment_start);
		leading_end = sqlparser_oracle_skip_leading_trivia(
			input_sql, segment_start, statement_end);
		statement_sql = sqlparser_strndup(
			input_sql + leading_end, statement_end - leading_end);
		if (statement_sql == NULL) {
			sqlparser_oracle_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_oracle_preprocess_raw_statement(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_oracle_preprocess_alter_session_switch(statement_sql, &rewritten_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_oracle_preprocess_execute_immediate(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_oracle_buffer_reserve_input(&out, input_sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_oracle_buffer_append_mem(&out, input_sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
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
	status = sqlparser_oracle_buffer_append_mem(&out, input_sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_append_pg_string_char(
	sqlparser_oracle_buffer_t *out,
	char value,
	sqlparser_error_t *out_error)
{
	if (value == '\'') {
		return sqlparser_oracle_buffer_append_cstr(out, "''", out_error);
	}

	return sqlparser_oracle_buffer_append_char(out, value, out_error);
}

static sqlparser_status_t sqlparser_oracle_copy_q_string_literal(
	const char *input,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char open_delim;
	char close_delim;
	size_t prefix_len;
	size_t pos;
	sqlparser_status_t status;

	prefix_len = sqlparser_oracle_q_quote_prefix_len(input + *index);
	if (prefix_len == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"invalid Oracle q-quoted string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	open_delim = input[*index + prefix_len + 1U];
	if (open_delim == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"unterminated Oracle q-quoted string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	close_delim = open_delim;
	if (open_delim == '[') {
		close_delim = ']';
	} else if (open_delim == '{') {
		close_delim = '}';
	} else if (open_delim == '(') {
		close_delim = ')';
	} else if (open_delim == '<') {
		close_delim = '>';
	}

	status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + prefix_len + 2U;
	while (input[pos] != '\0') {
		if (input[pos] == close_delim && input[pos + 1U] == '\'') {
			status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 2U;
			return SQLPARSER_STATUS_OK;
		}

		status = sqlparser_oracle_append_pg_string_char(out, input[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated Oracle q-quoted string literal");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_oracle_state_find_or_add_bind(
	sqlparser_oracle_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char *name_copy;
	char **next;
	size_t index;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind state arguments must not be NULL");
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

static sqlparser_status_t sqlparser_oracle_state_append_bind(
	sqlparser_oracle_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char *name_copy;
	char **next;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind state arguments must not be NULL");
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

static sqlparser_status_t sqlparser_oracle_append_pg_param(
	sqlparser_oracle_buffer_t *out,
	size_t param_index,
	sqlparser_error_t *out_error)
{
	char text[32];

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	return sqlparser_oracle_buffer_append_cstr(out, text, out_error);
}

static sqlparser_status_t sqlparser_oracle_copy_bind_placeholder(
	const char *input,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_oracle_state_t *state,
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
	} else if (sqlparser_oracle_is_ident_start((unsigned char)input[end])) {
		end++;
		while (sqlparser_oracle_is_ident_char((unsigned char)input[end])) {
			end++;
		}
		while (input[end] == '.' &&
		       sqlparser_oracle_is_ident_start((unsigned char)input[end + 1U])) {
			end += 2U;
			while (sqlparser_oracle_is_ident_char((unsigned char)input[end])) {
				end++;
			}
		}
	} else {
		return sqlparser_oracle_buffer_append_char(out, input[(*index)++], out_error);
	}

	status = sqlparser_oracle_state_find_or_add_bind(
		state,
		input + start,
		end - start,
		&param_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_oracle_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	state->bind_occurrence_count++;
	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_copy_question_placeholder(
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_oracle_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t param_index;
	sqlparser_status_t status;

	status = sqlparser_oracle_state_append_bind(state, "?", 1U, &param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_oracle_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	state->bind_occurrence_count++;
	(*index)++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_preprocess_text(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_dialect_returning_into_clause_t returning_into;
	sqlparser_status_t status;
	size_t index;
	int has_returning_into;

	memset(&out, 0, sizeof(out));
	memset(&returning_into, 0, sizeof(returning_into));
	status = sqlparser_oracle_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	has_returning_into = 0;
	while (input_sql[index] != '\0') {
		int copied;
		size_t q_prefix_len;

		q_prefix_len = sqlparser_oracle_q_quote_prefix_len(input_sql + index);
		if (q_prefix_len > 0U) {
			size_t literal_start;

			literal_start = out.len;
			status = sqlparser_oracle_copy_q_string_literal(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			if (q_prefix_len == 2U) {
				status = sqlparser_oracle_store_national_literal(
					state,
					out.data + literal_start,
					out.len - literal_start,
					state->national_literals.literal_count,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return status;
				}
			}
			state->national_literals.literal_count++;
			continue;
		}

		if (sqlparser_oracle_is_n_string_literal(input_sql + index)) {
			size_t literal_start;

			literal_start = out.len;
			status = sqlparser_oracle_copy_n_string_literal(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			status = sqlparser_oracle_store_national_literal(
				state,
				out.data + literal_start,
				out.len - literal_start,
				state->national_literals.literal_count,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			state->national_literals.literal_count++;
			continue;
		}

		if (sqlparser_oracle_is_ident_start((unsigned char)input_sql[index])) {
			const char *internal_keyword;
			size_t source_length;

			internal_keyword =
				sqlparser_oracle_hierarchy_internal_keyword(
					input_sql,
					index,
					&source_length);
			if (internal_keyword != NULL) {
				status = sqlparser_oracle_buffer_append_cstr(
					&out,
					internal_keyword,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return status;
				}
				index += source_length;
				continue;
			}
		}

		if (sqlparser_oracle_is_ident_start((unsigned char)input_sql[index]) || input_sql[index] == '"') {
			size_t before;

			before = index;
			status = sqlparser_oracle_try_copy_database_link_relation(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			if (index != before) {
				continue;
			}
		}

		{
			size_t quoted_start;

			quoted_start = index;
			copied = sqlparser_oracle_copy_quoted_or_comment(input_sql, &index, &out, out_error);
			if (copied > 0 && input_sql[quoted_start] == '\'') {
				state->national_literals.literal_count++;
			}
		}
		if (copied < 0) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (has_returning_into && index == returning_into.into_start) {
			status = sqlparser_oracle_buffer_append_mem(
				&out, ",   ", strlen("into"), out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index += strlen("into");
				has_returning_into = 0;
			}
		} else if (sqlparser_oracle_ascii_word_equal(
			   input_sql, index, "returning") &&
			   sqlparser_dialect_returning_into_clause_at(
				   SQLPARSER_DIALECT_ORACLE,
				   input_sql,
				   index,
				   0,
				   &returning_into)) {
			status = sqlparser_dialect_returning_into_state_append(
				&state->returning_into,
				returning_into.statement_index,
				input_sql + index,
				returning_into.keyword_end - index,
				input_sql + returning_into.into_start,
				returning_into.pair_count,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_mem(
					&out,
					input_sql + index,
					returning_into.keyword_end - index,
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				index = returning_into.keyword_end;
				has_returning_into = 1;
			}
		} else if (input_sql[index] == '?') {
			status = sqlparser_oracle_copy_question_placeholder(&index, &out, state, out_error);
		} else if (input_sql[index] == ':' && input_sql[index + 1U] != '=' &&
		    input_sql[index + 1U] != ':' && input_sql[index + 1U] != '\0') {
			status = sqlparser_oracle_copy_bind_placeholder(input_sql, &index, &out, state, out_error);
		} else if (sqlparser_oracle_ascii_word_equal(input_sql, index, "minus")) {
			status = sqlparser_dialect_minuses_append(
				&state->minuses,
				state->minuses.except_count,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(
					&out, "EXCEPT", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				state->minuses.except_count++;
				index += 5U;
			}
		} else if (sqlparser_oracle_ascii_word_equal(input_sql, index, "except")) {
			status = sqlparser_oracle_buffer_append_mem(
				&out, input_sql + index, 6U, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				state->minuses.except_count++;
				index += 6U;
			}
		} else {
			status = sqlparser_oracle_buffer_append_char(&out, input_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_oracle_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_trimmed_slice_dup(
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
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	*out_text = sqlparser_strndup(sql + start, end - start);
	if (*out_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_find_matching_paren(
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
	pos = open_pos + 1U;
	depth = 1U;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
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

static int sqlparser_oracle_find_top_level_word(
	const char *sql,
	size_t start,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || word == NULL || out_pos == NULL) {
		return 0;
	}
	pos = start;
	depth = 0U;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
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
		if (depth == 0U && sqlparser_oracle_ascii_word_equal(sql, pos, word)) {
			*out_pos = pos;
			return 1;
		}
		pos++;
	}
	return 0;
}

static sqlparser_status_t sqlparser_oracle_identifier_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	char **out_name,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t pos;

	if (out_name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "identifier output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_name = NULL;
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "identifier must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sql[start] != '"') {
		return sqlparser_oracle_trimmed_slice_dup(sql, start, end, out_name, out_error);
	}

	memset(&out, 0, sizeof(out));
	pos = start + 1U;
	while (pos < end) {
		if (sql[pos] == '"') {
			if (pos + 1U < end && sql[pos + 1U] == '"') {
				if (sqlparser_oracle_buffer_append_char(&out, '"', out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
				pos += 2U;
				continue;
			}
			break;
		}
		if (sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name = sqlparser_oracle_buffer_take(&out);
	return *out_name != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_relation_from_sql(
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
	size_t object_end;
	size_t link_start;
	size_t link_end;
	int has_link;
	sqlparser_status_t status;

	if (relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(relation, 0, sizeof(*relation));
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "INSERT target relation must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sqlparser_oracle_trimmed_slice_dup(sql, start, end, &relation->sql, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}

	object_end = end;
	link_start = 0U;
	link_end = 0U;
	has_link = 0;
	count = 0U;
	part_start[count] = start;
	pos = start;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '@') {
			object_end = sqlparser_oracle_trim_right(sql, start, pos);
			link_start = sqlparser_oracle_trim_left(sql, pos + 1U, end);
			link_end = sqlparser_oracle_identifier_token_end(sql, link_start);
			if (object_end <= start || link_end <= link_start ||
			    link_end != end ||
			    sqlparser_oracle_public_identifier_is_empty(
				    sql, link_start, link_end)) {
				sqlparser_oracle_relation_clear(relation);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid Oracle database link name");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			has_link = 1;
			break;
		}
		if (sql[pos] == '.') {
			if (count >= 2U) {
				sqlparser_oracle_relation_clear(relation);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Oracle relation has too many name parts");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			part_end[count] = pos;
			count++;
			part_start[count] = pos + 1U;
		}
		pos++;
	}
	part_end[count] = object_end;
	count++;

	status = SQLPARSER_STATUS_OK;
	if (count == 1U) {
		status = sqlparser_oracle_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->table_name, out_error);
	} else if (count == 2U) {
		status = sqlparser_oracle_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->schema_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_identifier_from_sql(
				sql, part_start[1], part_end[1],
				&relation->table_name, out_error);
		}
	} else {
		status = sqlparser_oracle_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->database_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_identifier_from_sql(
				sql, part_start[1], part_end[1],
				&relation->schema_name, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_identifier_from_sql(
				sql, part_start[2], part_end[2],
				&relation->table_name, out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK && has_link) {
		status = sqlparser_oracle_identifier_unquote(
			sql, link_start, link_end, &relation->link_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			relation->link_sql = sqlparser_strndup(
				sql + link_start, link_end - link_start);
			if (relation->link_sql == NULL) {
				sqlparser_error_set_message(
					out_error, SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				status = SQLPARSER_STATUS_NO_MEMORY;
			}
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_relation_clear(relation);
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_parse_column_list(
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
		skipped = !at_end ? sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
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
			item_end = sqlparser_oracle_trim_right(sql, item_start, pos);
			item_start = sqlparser_oracle_trim_left(sql, item_start, item_end);
			if (item_start >= item_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "column list contains an empty item");
				goto fail;
			}
			if (sqlparser_oracle_trimmed_slice_dup(sql, item_start, item_end, &columns[count].sql, out_error) != SQLPARSER_STATUS_OK ||
			    sqlparser_oracle_identifier_from_sql(sql, item_start, item_end, &columns[count].name, out_error) != SQLPARSER_STATUS_OK) {
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
			free(columns[index].name);
			free(columns[index].sql);
		}
		free(columns);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static int sqlparser_oracle_text_is_integer_literal(const char *text)
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

static int sqlparser_oracle_text_is_float_literal(const char *text)
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

static int sqlparser_oracle_ascii_text_equal(const char *left, const char *right)
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

static sqlparser_status_t sqlparser_oracle_unquote_string_literal(
	const char *text,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
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
			if (sqlparser_oracle_buffer_append_char(&out, '\'', out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			pos += 2U;
			continue;
		}
		if (sqlparser_oracle_buffer_append_char(&out, text[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_value = sqlparser_oracle_buffer_take(&out);
	return *out_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_value_fill_literal(
	sqlparser_dialect_multi_insert_value_t *value,
	sqlparser_error_t *out_error)
{
	long long integer_value;
	char *endptr;

	if (value == NULL || value->public_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_ascii_text_equal(value->public_sql, "null")) {
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}
	if (value->public_sql[0] == '\'') {
		if (sqlparser_oracle_unquote_string_literal(value->public_sql, &value->literal_string_value, out_error) != SQLPARSER_STATUS_OK) {
			return SQLPARSER_STATUS_OK;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value->literal.string_value = value->literal_string_value;
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_text_is_integer_literal(value->public_sql)) {
		endptr = NULL;
		integer_value = strtoll(value->public_sql, &endptr, 10);
		if (endptr != NULL && *endptr == '\0') {
			value->has_literal = 1;
			value->literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value->literal.integer_value = integer_value;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_text_is_float_literal(value->public_sql)) {
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

static sqlparser_status_t sqlparser_oracle_parse_value_item(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_bind_token_t bind_token;
	const char *key_start;
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
	status = sqlparser_oracle_trimmed_slice_dup(sql, start, end, &out_value->public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	occurrence_before = state->bind_occurrence_count;
	status = sqlparser_oracle_preprocess_text(out_value->public_sql, state, &out_value->parser_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_value_clear(out_value);
		return status;
	}
	sql_len = strlen(out_value->public_sql);
	if (sqlparser_bind_token_exact(
		    SQLPARSER_DIALECT_ORACLE,
		    out_value->public_sql,
		    sql_len,
		    0U,
		    sql_len,
		    &bind_token) &&
	    state->bind_occurrence_count == occurrence_before + 1U) {
		bind_kind = bind_token.kind;
		key_start = bind_token.key_length > 0U ?
			out_value->public_sql + bind_token.key_start :
			NULL;
		key_len = bind_token.key_length;
		out_value->has_bind = 1;
		out_value->bind_kind = bind_kind;
		if (bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && key_start == NULL) {
			(void)snprintf(out_value->bind, sizeof(out_value->bind), "%lu", (unsigned long)(occurrence_before + 1U));
		} else {
			if (key_len >= sizeof(out_value->bind)) {
				sqlparser_oracle_value_clear(out_value);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind key is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			memcpy(out_value->bind, key_start, key_len);
			out_value->bind[key_len] = '\0';
		}
		if (sql_len >= sizeof(out_value->bind_sql)) {
			sqlparser_oracle_value_clear(out_value);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		memcpy(out_value->bind_sql, out_value->public_sql, sql_len);
		out_value->bind_sql[sql_len] = '\0';
		out_value->bind_position = occurrence_before + 1U;
		out_value->has_bind_position = 1;
	} else {
		status = sqlparser_oracle_value_fill_literal(out_value, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_value_clear(out_value);
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_parse_value_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
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
		skipped = !at_end ? sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
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
			if (sqlparser_oracle_parse_value_item(sql, item_start, pos, state, &values[count], out_error) != SQLPARSER_STATUS_OK) {
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
			sqlparser_oracle_value_clear(&values[index]);
		}
		free(values);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_multi_insert_add_branch(
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

static sqlparser_status_t sqlparser_oracle_parse_multi_insert_into(
	const char *sql,
	size_t *io_pos,
	size_t end,
	sqlparser_oracle_state_t *state,
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
	pos = sqlparser_oracle_trim_left(sql, *io_pos, end);
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "into")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_oracle_trim_left(sql, pos + strlen("into"), end);
	relation_start = pos;
	columns_open = end;
	values_pos = end;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			columns_open = pos;
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
			values_pos = pos;
			break;
		}
		pos++;
	}
	if (columns_open == end && values_pos == end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	relation_end = columns_open != end ? columns_open : values_pos;
	status = sqlparser_oracle_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!sqlparser_oracle_find_matching_paren(sql, columns_open, end, &columns_close)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_oracle_parse_column_list(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			return status;
		}
		pos = sqlparser_oracle_trim_left(sql, columns_close + 1U, end);
	} else {
		pos = values_pos;
	}
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_oracle_trim_left(sql, pos + strlen("values"), end);
	if (pos >= end || sql[pos] != '(') {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list expected '('");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	values_open = pos;
	if (!sqlparser_oracle_find_matching_paren(sql, values_open, end, &values_close)) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_oracle_parse_value_list(
		sql,
		values_open + 1U,
		values_close,
		state,
		&branch.cells,
		&branch.cell_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	if (condition_public_sql != NULL) {
		branch.condition_public_sql = sqlparser_strdup(condition_public_sql);
		branch.condition_parser_sql = sqlparser_strdup(condition_parser_sql != NULL ? condition_parser_sql : condition_public_sql);
		if (branch.condition_public_sql == NULL || branch.condition_parser_sql == NULL) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		branch.has_condition = 1;
	}
	branch.is_else = is_else;
	branch.condition_group_id = condition_group_id;
	status = sqlparser_oracle_multi_insert_add_branch(multi, &branch, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	*io_pos = values_close + 1U;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_is_multi_insert_start(
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
	pos = sqlparser_oracle_trim_left(sql, 0U, len);
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "insert")) {
		return 0;
	}
	pos = sqlparser_oracle_trim_left(sql, pos + strlen("insert"), len);
	if (sqlparser_oracle_ascii_word_equal(sql, pos, "all")) {
		if (out_mode != NULL) {
			*out_mode = SQLPARSER_DIALECT_MULTI_INSERT_ALL;
		}
		return 1;
	}
	if (sqlparser_oracle_ascii_word_equal(sql, pos, "first")) {
		if (out_mode != NULL) {
			*out_mode = SQLPARSER_DIALECT_MULTI_INSERT_FIRST;
		}
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_oracle_parse_multi_insert(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_t *multi;
	sqlparser_dialect_multi_insert_mode_t mode;
	sqlparser_oracle_buffer_t parser;
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
	if (!sqlparser_oracle_is_multi_insert_start(input_sql, &mode)) {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	len = strlen(input_sql);
	pos = sqlparser_oracle_trim_left(input_sql, 0U, len);
	end = sqlparser_oracle_trim_right(input_sql, pos, len);
	if (end > pos && input_sql[end - 1U] == ';') {
		end = sqlparser_oracle_trim_right(input_sql, pos, end - 1U);
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("insert"), end);
	pos = sqlparser_oracle_trim_left(
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
		pos = sqlparser_oracle_trim_left(input_sql, pos, end);
		if (pos >= end) {
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "when")) {
			size_t condition_start;
			size_t then_pos;
			char *condition_public;
			char *condition_parser;

			condition_start = sqlparser_oracle_trim_left(input_sql, pos + strlen("when"), end);
			if (!sqlparser_oracle_find_top_level_word(input_sql, condition_start, end, "then", &then_pos)) {
				sqlparser_oracle_multi_insert_destroy(multi);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT WHEN is missing THEN");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			condition_public = NULL;
			condition_parser = NULL;
			status = sqlparser_oracle_trimmed_slice_dup(
				input_sql,
				condition_start,
				then_pos,
				&condition_public,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_preprocess_text(condition_public, state, &condition_parser, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(condition_public);
				free(condition_parser);
				sqlparser_oracle_multi_insert_destroy(multi);
				return status;
			}
			pos = sqlparser_oracle_trim_left(input_sql, then_pos + strlen("then"), end);
			condition_group_id++;
			do {
				status = sqlparser_oracle_parse_multi_insert_into(
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
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_trim_left(input_sql, pos, end);
			} while (pos < end &&
			         sqlparser_oracle_ascii_word_equal(input_sql, pos, "into"));
			free(condition_public);
			free(condition_parser);
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "else")) {
			pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("else"), end);
			condition_group_id++;
			while (pos < end && sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
				status = sqlparser_oracle_parse_multi_insert_into(
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
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_trim_left(input_sql, pos, end);
			}
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
			status = sqlparser_oracle_parse_multi_insert_into(
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
				sqlparser_oracle_multi_insert_destroy(multi);
				return status;
			}
			continue;
		}
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO, WHEN, ELSE, or SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	pos = sqlparser_oracle_trim_left(input_sql, pos, end);
	if (multi->branch_count == 0U || pos >= end || !sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT requires branches and a source SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_oracle_trimmed_slice_dup(input_sql, pos, end, &multi->source_public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_preprocess_text(multi->source_public_sql, state, &multi->source_parser_sql, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}

	memset(&parser, 0, sizeof(parser));
	status = sqlparser_oracle_buffer_append_cstr(&parser, "INSERT INTO sqlparser_oracle_multi_insert_source ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&parser, multi->source_parser_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&parser, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&parser);
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}

	sqlparser_oracle_multi_insert_destroy(state->multi_insert);
	state->multi_insert = multi;
	*out_parser_sql = sqlparser_oracle_buffer_take(&parser);
	return *out_parser_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_param_to_bind(
	const char *sql,
	size_t *index,
	const sqlparser_oracle_state_t *state,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (!isdigit((unsigned char)sql[pos])) {
		return sqlparser_oracle_buffer_append_char(out, sql[(*index)++], out_error);
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
		return sqlparser_oracle_buffer_append_cstr(out, state->bind_names[value - 1UL], out_error);
	}

	return sqlparser_oracle_buffer_append_char(out, sql[(*index)++], out_error);
}

static size_t sqlparser_oracle_quoted_literal_end(const char *sql, size_t start)
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

static size_t sqlparser_oracle_skip_optional_timestamp_zone(const char *sql, size_t pos)
{
	size_t scan;
	size_t word_pos;

	scan = pos;
	while (isspace((unsigned char)sql[scan])) {
		scan++;
	}

	if (sqlparser_oracle_ascii_word_equal(sql, scan, "without") ||
	    sqlparser_oracle_ascii_word_equal(sql, scan, "with")) {
		word_pos = scan;
		while (sqlparser_oracle_is_ident_char((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		while (isspace((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		if (sqlparser_oracle_ascii_word_equal(sql, word_pos, "time")) {
			while (sqlparser_oracle_is_ident_char((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			while (isspace((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			if (sqlparser_oracle_ascii_word_equal(sql, word_pos, "zone")) {
				while (sqlparser_oracle_is_ident_char((unsigned char)sql[word_pos])) {
					word_pos++;
				}
				return word_pos;
			}
		}
	}

	return pos;
}

static int sqlparser_oracle_copy_cast_literal(
	const char *sql,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
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

	literal_end = sqlparser_oracle_quoted_literal_end(sql, *index);
	if (literal_end == 0U || sql[literal_end] != ':' || sql[literal_end + 1U] != ':') {
		return 0;
	}

	prefix = NULL;
	cast_name_pos = literal_end + 2U;
	cast_end = cast_name_pos;
	if (sqlparser_oracle_ascii_word_equal(sql, cast_name_pos, "date")) {
		prefix = "DATE ";
		cast_end += strlen("date");
	} else if (sqlparser_oracle_ascii_word_equal(sql, cast_name_pos, "timestamp")) {
		prefix = "TIMESTAMP ";
		cast_end += strlen("timestamp");
		cast_end = sqlparser_oracle_skip_optional_timestamp_zone(sql, cast_end);
	}
	if (prefix == NULL) {
		return 0;
	}

	status = sqlparser_oracle_buffer_append_cstr(out, prefix, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	status = sqlparser_oracle_buffer_append_mem(out, sql + *index, literal_end - *index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}

	*index = cast_end;
	return 1;
}

static int sqlparser_oracle_marker_token_matches(
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
	if (pos > 0U && sqlparser_oracle_is_ident_char((unsigned char)sql[pos - 1U])) {
		return 0;
	}
	return !sqlparser_oracle_is_ident_char((unsigned char)sql[pos + len]);
}

static const sqlparser_oracle_dblink_relation_t *sqlparser_oracle_state_find_dblink_marker_at(
	const sqlparser_oracle_state_t *state,
	const char *sql,
	size_t pos)
{
	size_t index;

	if (state == NULL || sql == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->dblink_count; index++) {
		if (sqlparser_oracle_marker_token_matches(sql, pos, state->dblink_relations[index].parser_object_name)) {
			return &state->dblink_relations[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_oracle_restore_database_links(
	char **io_sql,
	const sqlparser_oracle_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *sql;
	size_t index;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->dblink_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_reserve_input(&out, sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	index = 0U;
	while (sql[index] != '\0') {
		const sqlparser_oracle_dblink_relation_t *relation;
		int copied;

		copied = sqlparser_oracle_copy_quoted_or_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		relation = sqlparser_oracle_state_find_dblink_marker_at(state, sql, index);
		if (relation != NULL) {
			status = sqlparser_oracle_buffer_append_cstr(&out, relation->public_object_sql, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_char(&out, '@', out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, relation->public_link_sql, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			index += strlen(relation->parser_object_name);
			continue;
		}

		status = sqlparser_oracle_buffer_append_char(&out, sql[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		index++;
	}

	status = sqlparser_oracle_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_oracle_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_postprocess_text(
	const char *core_sql,
	const sqlparser_oracle_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_status_t status;
	size_t index;
	size_t literal_ordinal;
	size_t except_ordinal;

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_reserve_input(&out, core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	literal_ordinal = 0U;
	except_ordinal = 0U;
	while (core_sql[index] != '\0') {
		int copied;

		copied = sqlparser_oracle_copy_cast_literal(core_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			literal_ordinal++;
			continue;
		}

		if (core_sql[index] == '\'') {
			size_t literal_end;

			literal_end = sqlparser_oracle_quoted_literal_end(core_sql, index);
			if (literal_end > index &&
			    sqlparser_oracle_national_literal_matches(state, literal_ordinal, core_sql, index, literal_end)) {
				status = sqlparser_oracle_buffer_append_char(&out, 'N', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return status;
				}
			}
			copied = sqlparser_oracle_copy_quoted_or_comment(core_sql, &index, &out, out_error);
			if (copied < 0) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			if (copied > 0) {
				literal_ordinal++;
				continue;
			}
		} else {
			copied = sqlparser_oracle_copy_quoted_or_comment(core_sql, &index, &out, out_error);
			if (copied < 0) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			if (copied > 0) {
				continue;
			}
		}

		if (core_sql[index] == '$') {
			status = sqlparser_oracle_param_to_bind(core_sql, &index, state, &out, out_error);
		} else if (sqlparser_oracle_ascii_word_equal(core_sql, index, "except")) {
			status = sqlparser_oracle_buffer_append_cstr(
				&out,
				sqlparser_dialect_minuses_match(
					state != NULL ? &state->minuses : NULL,
					except_ordinal) ? "MINUS" : "EXCEPT",
				out_error);
			except_ordinal++;
			index += 6U;
		} else if (sqlparser_oracle_ascii_word_equal(core_sql, index, "truncate")) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "TRUNCATE TABLE ", out_error);
			index += 8U;
			while (isspace((unsigned char)core_sql[index])) {
				index++;
			}
			if (sqlparser_oracle_ascii_word_equal(core_sql, index, "table")) {
				index += 5U;
				while (isspace((unsigned char)core_sql[index])) {
					index++;
				}
			}
		} else {
			status = sqlparser_oracle_buffer_append_char(&out, core_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_oracle_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_render_multi_insert(
	const sqlparser_dialect_multi_insert_t *multi,
	const char *postprocessed_core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
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
	    !sqlparser_oracle_find_top_level_word(postprocessed_core_sql, 0U, core_len, "select", &source_pos)) {
		postprocessed_core_sql = multi->source_public_sql;
		source_pos = 0U;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "INSERT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(
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
		status = sqlparser_oracle_buffer_append_char(&out, ' ', out_error);
		if (status == SQLPARSER_STATUS_OK && branch->has_condition && starts_group) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "WHEN ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, branch->condition_public_sql, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, " THEN ", out_error);
			}
		} else if (status == SQLPARSER_STATUS_OK && branch->is_else && starts_group) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "ELSE ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "INTO ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, branch->relation.sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && branch->column_count > 0U) {
			status = sqlparser_oracle_buffer_append_cstr(&out, " (", out_error);
			for (index = 0U; status == SQLPARSER_STATUS_OK && index < branch->column_count; index++) {
				if (index > 0U) {
					status = sqlparser_oracle_buffer_append_cstr(&out, ", ", out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_oracle_buffer_append_cstr(&out, branch->columns[index].sql, out_error);
				}
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_char(&out, ')', out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, " VALUES (", out_error);
		}
		for (index = 0U; status == SQLPARSER_STATUS_OK && index < branch->cell_count; index++) {
			if (index > 0U) {
				status = sqlparser_oracle_buffer_append_cstr(&out, ", ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, branch->cells[index].public_sql, out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_char(&out, ')', out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_char(&out, ' ', out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, postprocessed_core_sql + source_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	return *out_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static const char *sqlparser_oracle_find_top_level_comma(const char *start, const char *end)
{
	const char *pos;
	char quote;

	pos = start;
	while (pos < end) {
		if (*pos == '\'' || *pos == '"') {
			quote = *pos++;
			while (pos < end) {
				if (*pos == quote) {
					if (pos + 1 < end && *(pos + 1) == quote) {
						pos += 2;
						continue;
					}
					pos++;
					break;
				}
				pos++;
			}
			continue;
		}
		if (*pos == ',') {
			return pos;
		}
		pos++;
	}
	return NULL;
}

static sqlparser_status_t sqlparser_oracle_buffer_append_session_value(
	sqlparser_oracle_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	int can_unquote;
	int first_char;
	sqlparser_status_t status;

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
			    (first_char && !sqlparser_oracle_is_ident_start((unsigned char)*pos)) ||
			    (!first_char && !sqlparser_oracle_is_ident_char((unsigned char)*pos))) {
				can_unquote = 0;
				break;
			}
			first_char = 0;
			pos++;
		}
		if (can_unquote && !first_char) {
			return sqlparser_oracle_buffer_append_mem(
				out,
				value_start + 1,
				(size_t)(value_end - value_start - 2),
				out_error);
		}
	}

	status = sqlparser_oracle_buffer_append_mem(
		out,
		value_start,
		(size_t)(value_end - value_start),
		out_error);
	return status;
}

static sqlparser_status_t sqlparser_oracle_buffer_append_session_parameter_value(
	sqlparser_oracle_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	if (value_start < value_end && *value_start == '"' && *(value_end - 1) == '"') {
		status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos = value_start + 1;
		while (pos < value_end - 1) {
			if (*pos == '"' && pos + 1 < value_end - 1 && *(pos + 1) == '"') {
				status = sqlparser_oracle_buffer_append_char(out, '"', out_error);
				pos += 2;
			} else if (*pos == '\'') {
				status = sqlparser_oracle_buffer_append_cstr(out, "''", out_error);
				pos++;
			} else {
				status = sqlparser_oracle_buffer_append_char(out, *pos, out_error);
				pos++;
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		return sqlparser_oracle_buffer_append_char(out, '\'', out_error);
	}

	return sqlparser_oracle_buffer_append_session_value(out, value_start, value_end, out_error);
}

static sqlparser_status_t sqlparser_oracle_postprocess_session_switch(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *parameter_name;
	const char *prefix;
	const char *name_start;
	const char *name_end;
	const char *value_start;
	const char *value_end;
	const char *service_start;
	const char *service_end;
	const char *comma;
	size_t start;
	size_t end;
	size_t prefix_len;
	size_t pos;
	size_t parameter_name_len;
	int has_service;
	int is_generic_session_param;
	int requires_unquoted_value;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	start = sqlparser_oracle_trim_left(core_sql, 0U, strlen(core_sql));
	end = sqlparser_oracle_trim_right(core_sql, start, strlen(core_sql));
	parameter_name = NULL;
	parameter_name_len = 0U;
	is_generic_session_param = 0;
	prefix = "SET " SQLPARSER_INTERNAL_CURRENT_SCHEMA " TO ";
	prefix_len = strlen(prefix);
	if (end - start >= prefix_len && strncmp(core_sql + start, prefix, prefix_len) == 0) {
		parameter_name = "CURRENT_SCHEMA";
		parameter_name_len = strlen(parameter_name);
		value_start = core_sql + start + prefix_len;
	} else {
		prefix = "SET " SQLPARSER_INTERNAL_CURRENT_DATABASE " TO ";
		prefix_len = strlen(prefix);
		if (end - start >= prefix_len && strncmp(core_sql + start, prefix, prefix_len) == 0) {
			parameter_name = "CONTAINER";
			parameter_name_len = strlen(parameter_name);
			value_start = core_sql + start + prefix_len;
		} else {
			prefix = "SET ";
			prefix_len = strlen(prefix);
			if (end - start < prefix_len || strncmp(core_sql + start, prefix, prefix_len) != 0) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_trim_left(core_sql, start + prefix_len, end);
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
			prefix = SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX;
			prefix_len = strlen(prefix);
			if ((size_t)(name_end - name_start) <= prefix_len ||
			    strncmp(name_start, prefix, prefix_len) != 0) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_trim_left(core_sql, pos, end);
			if (!sqlparser_oracle_ascii_word_equal(core_sql, pos, "to")) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_trim_left(core_sql, pos + strlen("to"), end);
			if (pos >= end) {
				return SQLPARSER_STATUS_OK;
			}
			parameter_name = name_start + prefix_len;
			parameter_name_len = (size_t)(name_end - parameter_name);
			value_start = core_sql + pos;
			is_generic_session_param = 1;
		}
	}

	value_end = core_sql + end;
	if (value_start >= value_end) {
		return SQLPARSER_STATUS_OK;
	}
	has_service = 0;
	service_start = NULL;
	service_end = NULL;
	if (strcmp(parameter_name, "CONTAINER") == 0) {
		comma = sqlparser_oracle_find_top_level_comma(value_start, value_end);
		if (comma != NULL) {
			value_end = core_sql + sqlparser_oracle_trim_right(core_sql, (size_t)(value_start - core_sql), (size_t)(comma - core_sql));
			service_start = core_sql + sqlparser_oracle_trim_left(core_sql, (size_t)(comma + 1 - core_sql), end);
			service_end = core_sql + end;
			if (service_start < service_end) {
				has_service = 1;
			}
		}
	}
	requires_unquoted_value =
		is_generic_session_param &&
		(sqlparser_oracle_ascii_word_equal(parameter_name, 0U, "edition") ||
		 sqlparser_oracle_ascii_word_equal(parameter_name, 0U, "standby_max_data_delay"));

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "ALTER SESSION SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_mem(&out, parameter_name, parameter_name_len, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = is_generic_session_param && !requires_unquoted_value ?
			sqlparser_oracle_buffer_append_session_parameter_value(
				&out,
				value_start,
				value_end,
				out_error) :
			sqlparser_oracle_buffer_append_session_value(
				&out,
				value_start,
				value_end,
				out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " SERVICE = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_session_value(
			&out,
			service_start,
			service_end,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t pos;
	size_t len;
	size_t token_start;
	size_t token_end;
	char quote;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal argument output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	len = strlen(sql);
	pos = sqlparser_oracle_trim_left(sql, *index, len);
	if (sql[pos] == '\'' || sql[pos] == '"') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_oracle_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_oracle_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_oracle_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_oracle_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal Oracle prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = sqlparser_oracle_trim_right(sql, token_start, pos);
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal Oracle prepared argument");
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

static sqlparser_status_t sqlparser_oracle_postprocess_execute_immediate(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *prefix;
	char *value;
	size_t start;
	size_t end;
	size_t prefix_len;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	start = sqlparser_oracle_trim_left(core_sql, 0U, strlen(core_sql));
	end = sqlparser_oracle_trim_right(core_sql, start, strlen(core_sql));
	prefix = "SET " SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE " TO ";
	prefix_len = strlen(prefix);
	if (end - start < prefix_len || strncmp(core_sql + start, prefix, prefix_len) != 0) {
		return SQLPARSER_STATUS_OK;
	}
	index = start + prefix_len;
	value = NULL;
	status = sqlparser_oracle_read_internal_string_arg(core_sql, &index, &value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(value);
		return status;
	}
	index = sqlparser_oracle_trim_left(core_sql, index, end);
	if (index != end) {
		free(value);
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "EXECUTE IMMEDIATE ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, value, out_error);
	}
	free(value);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_postprocess_raw_statement(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	static const char *const internal_names[] = {
		SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM,
		SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM,
		SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN,
		SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT
	};
	char *value;
	const char *prefix;
	size_t start;
	size_t end;
	size_t prefix_len;
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

	start = sqlparser_oracle_trim_left(core_sql, 0U, strlen(core_sql));
	end = sqlparser_oracle_trim_right(core_sql, start, strlen(core_sql));
	for (spec_index = 0U; spec_index < sizeof(internal_names) / sizeof(internal_names[0]); spec_index++) {
		prefix = "SET ";
		prefix_len = strlen(prefix);
		if (end - start < prefix_len ||
		    strncmp(core_sql + start, prefix, prefix_len) != 0) {
			continue;
		}
		index = start + prefix_len;
		if (end - index < strlen(internal_names[spec_index]) ||
		    strncmp(core_sql + index, internal_names[spec_index], strlen(internal_names[spec_index])) != 0) {
			continue;
		}
		index += strlen(internal_names[spec_index]);
		index = sqlparser_oracle_trim_left(core_sql, index, end);
		if (!sqlparser_oracle_ascii_word_equal(core_sql, index, "to")) {
			continue;
		}
		index = sqlparser_oracle_trim_left(core_sql, index + strlen("to"), end);
		value = NULL;
		status = sqlparser_oracle_read_internal_string_arg(core_sql, &index, &value, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(value);
			return status;
		}
		index = sqlparser_oracle_trim_left(core_sql, index, end);
		if (index != end) {
			free(value);
			return SQLPARSER_STATUS_OK;
		}
		*out_sql = value;
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_rewrite_session_switches(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *sql;
	char *statement_sql;
	char *rewritten_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;

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
		statement_end = sqlparser_oracle_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_oracle_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_oracle_postprocess_session_switch(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_oracle_postprocess_execute_immediate(statement_sql, &rewritten_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_oracle_postprocess_raw_statement(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_oracle_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = sqlparser_oracle_trim_left(sql, segment_start, statement_end);
			status = sqlparser_oracle_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
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
	status = sqlparser_oracle_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_oracle_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_origin_error(
	sqlparser_error_t *out_error,
	const char *message)
{
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_INTERNAL_ERROR,
		message);
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static int sqlparser_oracle_origin_span_equal(
	const char *left,
	size_t left_offset,
	const char *right,
	size_t right_offset,
	size_t length)
{
	return length == 0U ||
		memcmp(left + left_offset, right + right_offset, length) == 0;
}

static sqlparser_status_t sqlparser_oracle_origin_append_rewritten_statement(
	sqlparser_identifier_origin_writer_t *writer,
	const char *source_sql,
	size_t source_offset,
	size_t source_length,
	const char *rewritten_sql,
	sqlparser_error_t *out_error)
{
	size_t rewritten_length;
	size_t rewritten_pos;
	size_t identifier_end;
	size_t source_pos;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	rewritten_length = strlen(rewritten_sql);
	if (rewritten_length < 4U ||
	    memcmp(rewritten_sql, "SET ", 4U) != 0) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found an invalid statement rewrite");
	}
	status = sqlparser_identifier_origin_writer_append_unknown(
		writer,
		4U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	rewritten_pos = 4U;
	identifier_end = sqlparser_oracle_identifier_token_end_bounded(
		rewritten_sql,
		rewritten_pos,
		rewritten_length);
	if (identifier_end == rewritten_pos) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay could not read a generated identifier");
	}
	status = sqlparser_identifier_origin_writer_append_generated_identifier(
		writer,
		identifier_end - rewritten_pos,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	rewritten_pos = identifier_end;
	if (rewritten_length - rewritten_pos < 3U ||
	    memcmp(rewritten_sql + rewritten_pos, " = ", 3U) != 0) {
		return sqlparser_identifier_origin_writer_append_unknown(
			writer,
			rewritten_length - rewritten_pos,
			out_error);
	}
	status = sqlparser_identifier_origin_writer_append_unknown(
		writer,
		3U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	rewritten_pos += 3U;

	source_pos = 0U;
	while (source_pos < source_length && source_sql[source_pos] != '=') {
		source_pos++;
	}
	if (source_pos >= source_length) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay could not find an ALTER SESSION value");
	}
	value_start = sqlparser_oracle_trim_left(
		source_sql,
		source_pos + 1U,
		source_length);
	value_end = sqlparser_oracle_session_value_token_end(
		source_sql,
		value_start,
		source_length,
		out_error);
	if (value_end <= value_start ||
	    value_end - value_start > rewritten_length - rewritten_pos ||
	    !sqlparser_oracle_origin_span_equal(
		    source_sql,
		    value_start,
		    rewritten_sql,
		    rewritten_pos,
		    value_end - value_start)) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found a changed ALTER SESSION value");
	}
	status = sqlparser_identifier_origin_writer_append_input(
		writer,
		source_offset + value_start,
		value_end - value_start,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	rewritten_pos += value_end - value_start;
	if (rewritten_pos == rewritten_length) {
		return SQLPARSER_STATUS_OK;
	}
	if (rewritten_length - rewritten_pos < 2U ||
	    memcmp(rewritten_sql + rewritten_pos, ", ", 2U) != 0) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found an invalid ALTER SESSION suffix");
	}
	status = sqlparser_identifier_origin_writer_append_unknown(
		writer,
		2U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	rewritten_pos += 2U;

	source_pos = sqlparser_oracle_trim_left(
		source_sql,
		value_end,
		source_length);
	if (!sqlparser_oracle_ascii_word_equal(
		    source_sql,
		    source_pos,
		    "service")) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay could not find ALTER SESSION SERVICE");
	}
	source_pos = sqlparser_oracle_trim_left(
		source_sql,
		source_pos + strlen("service"),
		source_length);
	if (source_pos >= source_length || source_sql[source_pos] != '=') {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found an invalid ALTER SESSION SERVICE");
	}
	value_start = sqlparser_oracle_trim_left(
		source_sql,
		source_pos + 1U,
		source_length);
	value_end = sqlparser_oracle_session_value_token_end(
		source_sql,
		value_start,
		source_length,
		out_error);
	if (value_end <= value_start ||
	    value_end - value_start != rewritten_length - rewritten_pos ||
	    !sqlparser_oracle_origin_span_equal(
		    source_sql,
		    value_start,
		    rewritten_sql,
		    rewritten_pos,
		    value_end - value_start)) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found a changed ALTER SESSION SERVICE");
	}
	return sqlparser_identifier_origin_writer_append_input(
		writer,
		source_offset + value_start,
		value_end - value_start,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_replay_statement_rewrites(
	const char *input_sql,
	const char *rewritten_output,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_writer_t writer;
	char *statement_sql;
	char *statement_rewrite;
	sqlparser_status_t status;
	size_t input_length;
	size_t output_length;
	size_t segment_start;
	size_t statement_end;
	size_t leading_end;
	size_t copy_start;
	size_t output_pos;

	memset(&writer, 0, sizeof(writer));
	input_length = strlen(input_sql);
	output_length = strlen(rewritten_output);
	status = sqlparser_identifier_origin_writer_begin(
		&writer,
		origins,
		input_length,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	segment_start = 0U;
	copy_start = 0U;
	output_pos = 0U;
	while (segment_start < input_length) {
		statement_end = sqlparser_oracle_statement_end(
			input_sql,
			segment_start);
		leading_end = sqlparser_oracle_skip_leading_trivia(
			input_sql,
			segment_start,
			statement_end);
		statement_sql = sqlparser_strndup(
			input_sql + leading_end,
			statement_end - leading_end);
		if (statement_sql == NULL) {
			sqlparser_identifier_origin_writer_release(&writer);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		statement_rewrite = NULL;
		status = sqlparser_oracle_preprocess_raw_statement(
			statement_sql,
			&statement_rewrite,
			out_error);
		if (status == SQLPARSER_STATUS_OK && statement_rewrite == NULL) {
			status = sqlparser_oracle_preprocess_alter_session_switch(
				statement_sql,
				&statement_rewrite,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && statement_rewrite == NULL) {
			status = sqlparser_oracle_preprocess_execute_immediate(
				statement_sql,
				&statement_rewrite,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(statement_rewrite);
			free(statement_sql);
			sqlparser_identifier_origin_writer_release(&writer);
			return status;
		}
		if (statement_rewrite != NULL) {
			size_t copied_length;
			size_t rewrite_length;

			copied_length = leading_end - copy_start;
			rewrite_length = strlen(statement_rewrite);
			if (output_pos > output_length ||
			    copied_length > output_length - output_pos ||
			    !sqlparser_oracle_origin_span_equal(
				    input_sql,
				    copy_start,
				    rewritten_output,
				    output_pos,
				    copied_length) ||
			    rewrite_length >
				    output_length - output_pos - copied_length ||
			    memcmp(
				    rewritten_output + output_pos + copied_length,
				    statement_rewrite,
				    rewrite_length) != 0) {
				free(statement_rewrite);
				free(statement_sql);
				sqlparser_identifier_origin_writer_release(&writer);
				return sqlparser_oracle_origin_error(
					out_error,
					"Oracle identifier origin replay differs from statement rewrite");
			}
			status = sqlparser_identifier_origin_writer_append_input(
				&writer,
				copy_start,
				copied_length,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status =
					sqlparser_oracle_origin_append_rewritten_statement(
						&writer,
						statement_sql,
						leading_end,
						statement_end - leading_end,
						statement_rewrite,
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(statement_rewrite);
				free(statement_sql);
				sqlparser_identifier_origin_writer_release(&writer);
				return status;
			}
			output_pos += copied_length + rewrite_length;
			copy_start = statement_end;
		}
		free(statement_rewrite);
		free(statement_sql);
		if (statement_end >= input_length) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (output_pos > output_length ||
	    input_length - copy_start > output_length - output_pos ||
	    !sqlparser_oracle_origin_span_equal(
		    input_sql,
		    copy_start,
		    rewritten_output,
		    output_pos,
		    input_length - copy_start)) {
		sqlparser_identifier_origin_writer_release(&writer);
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay differs from rewritten SQL");
	}
	status = sqlparser_identifier_origin_writer_append_input(
		&writer,
		copy_start,
		input_length - copy_start,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_commit(
			&writer,
			output_length,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_writer_release(&writer);
	}
	return status;
}

static size_t sqlparser_oracle_origin_q_literal_end(
	const char *sql,
	size_t start)
{
	size_t prefix_length;
	size_t pos;
	char open_delimiter;
	char close_delimiter;

	prefix_length = sqlparser_oracle_q_quote_prefix_len(sql + start);
	if (prefix_length == 0U ||
	    sql[start + prefix_length + 1U] == '\0') {
		return start;
	}
	open_delimiter = sql[start + prefix_length + 1U];
	close_delimiter = open_delimiter;
	if (open_delimiter == '[') {
		close_delimiter = ']';
	} else if (open_delimiter == '{') {
		close_delimiter = '}';
	} else if (open_delimiter == '(') {
		close_delimiter = ')';
	} else if (open_delimiter == '<') {
		close_delimiter = '>';
	}
	pos = start + prefix_length + 2U;
	while (sql[pos] != '\0') {
		if (sql[pos] == close_delimiter && sql[pos + 1U] == '\'') {
			return pos + 2U;
		}
		pos++;
	}
	return start;
}

static int sqlparser_oracle_origin_dblink_span(
	const char *sql,
	size_t start,
	size_t *out_prefix_end,
	size_t *out_end)
{
	size_t pos;
	size_t part_start[4];
	size_t part_count;
	size_t link_start;
	size_t link_end;

	pos = start;
	part_count = 0U;
	for (;;) {
		size_t identifier_end;

		if (part_count >= sizeof(part_start) / sizeof(part_start[0])) {
			return 0;
		}
		part_start[part_count] = pos;
		identifier_end = sqlparser_oracle_identifier_token_end(sql, pos);
		if (identifier_end == pos ||
		    sqlparser_oracle_public_identifier_is_empty(
			    sql,
			    pos,
			    identifier_end)) {
			return 0;
		}
		part_count++;
		pos = identifier_end;
		if (sql[pos] != '.') {
			break;
		}
		pos++;
	}
	if (sql[pos] != '@') {
		return 0;
	}
	link_start = pos + 1U;
	link_end = sqlparser_oracle_identifier_token_end(sql, link_start);
	if (link_end == link_start ||
	    sqlparser_oracle_public_identifier_is_empty(
		    sql,
		    link_start,
		    link_end)) {
		return 0;
	}
	*out_prefix_end = part_start[part_count - 1U];
	*out_end = link_end;
	return 1;
}

static size_t sqlparser_oracle_origin_bind_end(
	const char *sql,
	size_t start)
{
	size_t end;

	end = start + 1U;
	if (isdigit((unsigned char)sql[end])) {
		while (isdigit((unsigned char)sql[end])) {
			end++;
		}
		return end;
	}
	if (!sqlparser_oracle_is_ident_start((unsigned char)sql[end])) {
		return start;
	}
	end++;
	while (sqlparser_oracle_is_ident_char((unsigned char)sql[end])) {
		end++;
	}
	while (sql[end] == '.' &&
	       sqlparser_oracle_is_ident_start((unsigned char)sql[end + 1U])) {
		end += 2U;
		while (sqlparser_oracle_is_ident_char((unsigned char)sql[end])) {
			end++;
		}
	}
	return end;
}

static size_t sqlparser_oracle_origin_pg_parameter_end(
	const char *sql,
	size_t start,
	size_t length)
{
	size_t end;

	if (start + 1U >= length || sql[start] != '$' ||
	    !isdigit((unsigned char)sql[start + 1U])) {
		return start;
	}
	end = start + 2U;
	while (end < length && isdigit((unsigned char)sql[end])) {
		end++;
	}
	return end;
}

static sqlparser_status_t sqlparser_oracle_origin_flush_input(
	sqlparser_identifier_origin_writer_t *writer,
	size_t input_map_offset,
	size_t *input_start,
	size_t *length,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (*length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origin_writer_append_input(
		writer,
		input_map_offset + *input_start,
		*length,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		*input_start = 0U;
		*length = 0U;
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_replay_preprocess_range(
	sqlparser_identifier_origin_writer_t *writer,
	const char *input_sql,
	size_t input_map_offset,
	const char *parser_sql,
	size_t parser_length,
	sqlparser_error_t *out_error)
{
	size_t input_pos;
	size_t parser_pos;
	size_t pending_start;
	size_t pending_length;

	input_pos = 0U;
	parser_pos = 0U;
	pending_start = 0U;
	pending_length = 0U;
	while (input_sql[input_pos] != '\0') {
		size_t q_prefix_length;
		size_t input_end;
		size_t parser_end;
		size_t prefix_end;
		sqlparser_status_t status;

		q_prefix_length =
			sqlparser_oracle_q_quote_prefix_len(input_sql + input_pos);
		if (q_prefix_length > 0U) {
			input_end = sqlparser_oracle_origin_q_literal_end(
				input_sql,
				input_pos);
			parser_end = parser_pos < parser_length ?
				sqlparser_oracle_skip_quoted_or_comment_span(
					parser_sql,
					parser_pos) :
				parser_pos;
			if (input_end == input_pos || parser_end <= parser_pos ||
			    parser_sql[parser_pos] != '\'' ||
			    parser_end > parser_length) {
				return sqlparser_oracle_origin_error(
					out_error,
					"Oracle identifier origin replay could not match a q-quoted literal");
			}
			status = sqlparser_oracle_origin_flush_input(
				writer,
				input_map_offset,
				&pending_start,
				&pending_length,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status =
					sqlparser_identifier_origin_writer_append_unknown(
						writer,
						parser_end - parser_pos,
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			input_pos = input_end;
			parser_pos = parser_end;
			continue;
		}

		if (sqlparser_oracle_is_n_string_literal(input_sql + input_pos)) {
			input_end = sqlparser_oracle_skip_quoted_or_comment_span(
				input_sql,
				input_pos + 1U);
			parser_end = parser_pos < parser_length ?
				sqlparser_oracle_skip_quoted_or_comment_span(
					parser_sql,
					parser_pos) :
				parser_pos;
			if (input_end <= input_pos + 1U ||
			    parser_end <= parser_pos ||
			    parser_sql[parser_pos] != '\'' ||
			    parser_end > parser_length) {
				return sqlparser_oracle_origin_error(
					out_error,
					"Oracle identifier origin replay could not match a national literal");
			}
			status = sqlparser_oracle_origin_flush_input(
				writer,
				input_map_offset,
				&pending_start,
				&pending_length,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status =
					sqlparser_identifier_origin_writer_append_unknown(
						writer,
						parser_end - parser_pos,
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			input_pos = input_end;
			parser_pos = parser_end;
			continue;
		}

		if (sqlparser_oracle_is_ident_start(
			    (unsigned char)input_sql[input_pos])) {
			const char *internal_keyword;
			size_t source_length;
			size_t internal_length;

			internal_keyword =
				sqlparser_oracle_hierarchy_internal_keyword(
					input_sql,
					input_pos,
					&source_length);
			if (internal_keyword != NULL) {
				internal_length = strlen(internal_keyword);
				if (parser_pos > parser_length ||
				    internal_length > parser_length - parser_pos ||
				    memcmp(
					    parser_sql + parser_pos,
					    internal_keyword,
					    internal_length) != 0) {
					return sqlparser_oracle_origin_error(
						out_error,
						"Oracle identifier origin replay could not match a hierarchy keyword");
				}
				status = sqlparser_oracle_origin_flush_input(
					writer,
					input_map_offset,
					&pending_start,
					&pending_length,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_source_identifier(
							writer,
							input_map_offset + input_pos,
							source_length,
							internal_length,
							out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				input_pos += source_length;
				parser_pos += internal_length;
				continue;
			}
		}

		if (sqlparser_oracle_is_ident_start(
			    (unsigned char)input_sql[input_pos]) ||
		    input_sql[input_pos] == '"') {
			size_t dblink_end;

			if (sqlparser_oracle_origin_dblink_span(
				    input_sql,
				    input_pos,
				    &prefix_end,
				    &dblink_end)) {
				size_t prefix_length;

				prefix_length = prefix_end - input_pos;
				if (parser_pos > parser_length ||
				    prefix_length > parser_length - parser_pos ||
				    !sqlparser_oracle_origin_span_equal(
					    input_sql,
					    input_pos,
					    parser_sql,
					    parser_pos,
					    prefix_length)) {
					return sqlparser_oracle_origin_error(
						out_error,
						"Oracle identifier origin replay found a changed database-link qualifier");
				}
				status = sqlparser_oracle_origin_flush_input(
					writer,
					input_map_offset,
					&pending_start,
					&pending_length,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_input(
							writer,
							input_map_offset +
								input_pos,
							prefix_length,
							out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				parser_pos += prefix_length;
				parser_end =
					sqlparser_oracle_identifier_token_end_bounded(
						parser_sql,
						parser_pos,
						parser_length);
				if (parser_end == parser_pos ||
				    parser_end - parser_pos <
					    strlen("sqlparser_oracle_dblink_") + 1U ||
				    memcmp(
					    parser_sql + parser_pos,
					    "sqlparser_oracle_dblink_",
					    strlen("sqlparser_oracle_dblink_")) != 0) {
					return sqlparser_oracle_origin_error(
						out_error,
						"Oracle identifier origin replay could not match a database-link marker");
				}
				status =
					sqlparser_identifier_origin_writer_append_generated_identifier(
						writer,
						parser_end - parser_pos,
						out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				input_pos = dblink_end;
				parser_pos = parser_end;
				continue;
			}
		}

		input_end = sqlparser_oracle_skip_quoted_or_comment_span(
			input_sql,
			input_pos);
		if (input_end > input_pos) {
			if (parser_pos > parser_length ||
			    input_end - input_pos > parser_length - parser_pos ||
			    !sqlparser_oracle_origin_span_equal(
				    input_sql,
				    input_pos,
				    parser_sql,
				    parser_pos,
				    input_end - input_pos)) {
				return sqlparser_oracle_origin_error(
					out_error,
					"Oracle identifier origin replay found changed quoted text or comment");
			}
			if (pending_length == 0U) {
				pending_start = input_pos;
			}
			pending_length += input_end - input_pos;
			parser_pos += input_end - input_pos;
			input_pos = input_end;
			continue;
		}

		if (input_sql[input_pos] == '?') {
			parser_end = sqlparser_oracle_origin_pg_parameter_end(
				parser_sql,
				parser_pos,
				parser_length);
			if (parser_end == parser_pos) {
				return sqlparser_oracle_origin_error(
					out_error,
					"Oracle identifier origin replay could not match a question-mark bind");
			}
			status = sqlparser_oracle_origin_flush_input(
				writer,
				input_map_offset,
				&pending_start,
				&pending_length,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status =
					sqlparser_identifier_origin_writer_append_source_identifier(
						writer,
						input_map_offset + input_pos,
						1U,
						parser_end - parser_pos,
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			input_pos++;
			parser_pos = parser_end;
			continue;
		}
		if (input_sql[input_pos] == ':' &&
		    input_sql[input_pos + 1U] != '=' &&
		    input_sql[input_pos + 1U] != ':' &&
		    input_sql[input_pos + 1U] != '\0') {
			input_end = sqlparser_oracle_origin_bind_end(
				input_sql,
				input_pos);
			if (input_end > input_pos) {
				parser_end =
					sqlparser_oracle_origin_pg_parameter_end(
						parser_sql,
						parser_pos,
						parser_length);
				if (parser_end == parser_pos) {
					return sqlparser_oracle_origin_error(
						out_error,
						"Oracle identifier origin replay could not match a named bind");
				}
				status = sqlparser_oracle_origin_flush_input(
					writer,
					input_map_offset,
					&pending_start,
					&pending_length,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_source_identifier(
							writer,
							input_map_offset + input_pos,
							input_end - input_pos,
							parser_end - parser_pos,
							out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				input_pos = input_end;
				parser_pos = parser_end;
				continue;
			}
		}
		if (sqlparser_oracle_ascii_word_equal(
			    input_sql,
			    input_pos,
			    "into") &&
		    parser_pos <= parser_length &&
		    parser_length - parser_pos >= strlen("into") &&
		    parser_sql[parser_pos] == ',' &&
		    parser_sql[parser_pos + 1U] == ' ' &&
		    parser_sql[parser_pos + 2U] == ' ' &&
		    parser_sql[parser_pos + 3U] == ' ') {
			status = sqlparser_oracle_origin_flush_input(
				writer,
				input_map_offset,
				&pending_start,
				&pending_length,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status =
					sqlparser_identifier_origin_writer_append_unknown(
						writer,
						strlen("into"),
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			input_pos += strlen("into");
			parser_pos += strlen("into");
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(
			    input_sql,
			    input_pos,
			    "minus") &&
		    parser_pos <= parser_length &&
		    parser_length - parser_pos >= strlen("EXCEPT") &&
		    memcmp(
			    parser_sql + parser_pos,
			    "EXCEPT",
			    strlen("EXCEPT")) == 0) {
			status = sqlparser_oracle_origin_flush_input(
				writer,
				input_map_offset,
				&pending_start,
				&pending_length,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status =
					sqlparser_identifier_origin_writer_append_unknown(
						writer,
						strlen("EXCEPT"),
						out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			input_pos += strlen("minus");
			parser_pos += strlen("EXCEPT");
			continue;
		}
		if (parser_pos >= parser_length ||
		    input_sql[input_pos] != parser_sql[parser_pos]) {
			return sqlparser_oracle_origin_error(
				out_error,
				"Oracle identifier origin replay differs from lexical preprocess");
		}
		if (pending_length == 0U) {
			pending_start = input_pos;
		}
		pending_length++;
		input_pos++;
		parser_pos++;
	}
	if (parser_pos != parser_length) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay left unmatched parser SQL");
	}
	return sqlparser_oracle_origin_flush_input(
		writer,
		input_map_offset,
		&pending_start,
		&pending_length,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_replay_preprocess_text(
	const char *input_sql,
	const char *parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_writer_t writer;
	sqlparser_status_t status;

	memset(&writer, 0, sizeof(writer));
	status = sqlparser_identifier_origin_writer_begin(
		&writer,
		origins,
		strlen(input_sql),
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_replay_preprocess_range(
			&writer,
			input_sql,
			0U,
			parser_sql,
			strlen(parser_sql),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_commit(
			&writer,
			strlen(parser_sql),
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_writer_release(&writer);
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_replay_multi_insert(
	const char *input_sql,
	const char *parser_sql,
	const sqlparser_oracle_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	static const char prefix[] =
		"INSERT INTO sqlparser_oracle_multi_insert_source ";
	static const char syntax_prefix[] = "INSERT INTO ";
	static const char generated_name[] =
		"sqlparser_oracle_multi_insert_source";
	const sqlparser_dialect_multi_insert_t *multi;
	sqlparser_identifier_origin_writer_t writer;
	sqlparser_status_t status;
	size_t input_length;
	size_t source_length;
	size_t source_start;
	size_t source_end;
	size_t parser_length;

	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || multi->source_public_sql == NULL ||
	    multi->source_parser_sql == NULL) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay is missing multi-insert state");
	}
	parser_length = strlen(parser_sql);
	if (parser_length < sizeof(prefix) - 1U ||
	    memcmp(parser_sql, prefix, sizeof(prefix) - 1U) != 0 ||
	    strcmp(
		    parser_sql + sizeof(prefix) - 1U,
		    multi->source_parser_sql) != 0) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found invalid multi-insert parser SQL");
	}
	input_length = strlen(input_sql);
	source_length = strlen(multi->source_public_sql);
	source_end = sqlparser_oracle_trim_right(
		input_sql,
		0U,
		input_length);
	if (source_end > 0U && input_sql[source_end - 1U] == ';') {
		source_end = sqlparser_oracle_trim_right(
			input_sql,
			0U,
			source_end - 1U);
	}
	if (source_length > source_end) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay could not locate multi-insert source SELECT");
	}
	source_start = source_end - source_length;
	if (memcmp(
		    input_sql + source_start,
		    multi->source_public_sql,
		    source_length) != 0) {
		return sqlparser_oracle_origin_error(
			out_error,
			"Oracle identifier origin replay found a changed multi-insert source SELECT");
	}

	memset(&writer, 0, sizeof(writer));
	status = sqlparser_identifier_origin_writer_begin(
		&writer,
		origins,
		input_length,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_append_unknown(
			&writer,
			sizeof(syntax_prefix) - 1U,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status =
			sqlparser_identifier_origin_writer_append_generated_identifier(
				&writer,
				sizeof(generated_name) - 1U,
				out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_append_unknown(
			&writer,
			1U,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_replay_preprocess_range(
			&writer,
			multi->source_public_sql,
			source_start,
			multi->source_parser_sql,
			strlen(multi->source_parser_sql),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_commit(
			&writer,
			parser_length,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_writer_release(&writer);
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *state;
	char *rewritten_sql;
	const char *preprocess_input;
	sqlparser_status_t status;

	(void)limits;

	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect preprocess output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;

	if (input_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = NULL;
	status = sqlparser_oracle_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	rewritten_sql = NULL;
	status = sqlparser_oracle_rewrite_alter_session_switches(input_sql, &rewritten_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(state);
		return status;
	}
	preprocess_input = rewritten_sql != NULL ? rewritten_sql : input_sql;
	status = sqlparser_dialect_returning_into_validate(
		SQLPARSER_DIALECT_ORACLE,
		preprocess_input,
		0,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(rewritten_sql);
		sqlparser_oracle_state_destroy(state);
		return status;
	}

	if (sqlparser_oracle_is_multi_insert_start(preprocess_input, NULL)) {
		status = sqlparser_oracle_parse_multi_insert(preprocess_input, state, out_parser_sql, out_error);
		free(rewritten_sql);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_state_destroy(state);
			return status;
		}
		*out_state = state;
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_oracle_reject_unsupported(preprocess_input, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(rewritten_sql);
		sqlparser_oracle_state_destroy(state);
		return status;
	}

	status = sqlparser_oracle_preprocess_text(preprocess_input, state, out_parser_sql, out_error);
	free(rewritten_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(state);
		return status;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_oracle_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *state;
	char *rewritten_sql;
	const char *preprocess_input;
	sqlparser_status_t status;

	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (input_sql == NULL || out_parser_sql == NULL ||
	    out_state == NULL || origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"Oracle identifier origin preprocess arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_oracle_preprocess(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state = (sqlparser_oracle_state_t *)*out_state;
	rewritten_sql = NULL;
	status = sqlparser_oracle_rewrite_alter_session_switches(
		input_sql,
		&rewritten_sql,
		out_error);
	preprocess_input = rewritten_sql != NULL ? rewritten_sql : input_sql;
	if (status == SQLPARSER_STATUS_OK && rewritten_sql != NULL) {
		status = sqlparser_oracle_replay_statement_rewrites(
			input_sql,
			rewritten_sql,
			origins,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && state->multi_insert != NULL) {
		status = sqlparser_oracle_replay_multi_insert(
			preprocess_input,
			*out_parser_sql,
			state,
			origins,
			out_error);
	} else if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_replay_preprocess_text(
			preprocess_input,
			*out_parser_sql,
			origins,
			out_error);
	}
	free(rewritten_sql);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_parser_sql);
		*out_parser_sql = NULL;
		sqlparser_oracle_state_destroy(*out_state);
		*out_state = NULL;
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	public_sql = NULL;
	status = sqlparser_oracle_postprocess_text(
		core_sql,
		(const sqlparser_oracle_state_t *)state,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_oracle_state_has_multi_insert(state)) {
		char *multi_sql;

		multi_sql = NULL;
		status = sqlparser_oracle_render_multi_insert(
			sqlparser_oracle_state_multi_insert(state),
			public_sql,
			&multi_sql,
			out_error);
		free(public_sql);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		public_sql = multi_sql;
	}

	status = sqlparser_oracle_rewrite_session_switches(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	status = sqlparser_oracle_restore_database_links(&public_sql, (const sqlparser_oracle_state_t *)state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	status = sqlparser_dialect_returning_into_postprocess(
		SQLPARSER_DIALECT_ORACLE,
		&public_sql,
		sqlparser_oracle_state_returning_into(state),
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

static sqlparser_status_t sqlparser_oracle_postprocess_fragment(
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
	status = sqlparser_oracle_postprocess_text(
		core_sql,
		(const sqlparser_oracle_state_t *)state,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_oracle_restore_database_links(
		&public_sql,
		(const sqlparser_oracle_state_t *)state,
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

static sqlparser_status_t sqlparser_oracle_preprocess_fragment(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	(void)statement_index;
	if (out_parser_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL fragment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"Oracle dialect state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	sqlparser_dialect_national_literals_begin_fragment(
		&((sqlparser_oracle_state_t *)state)->national_literals);
	sqlparser_dialect_minuses_begin_fragment(
		&((sqlparser_oracle_state_t *)state)->minuses);

	return sqlparser_oracle_preprocess_text(
		input_sql,
		(sqlparser_oracle_state_t *)state,
		out_parser_sql,
		out_error);
}

sqlparser_status_t sqlparser_oracle_preprocess_fragment_identifier_origins(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin map must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_oracle_preprocess_fragment(
		input_sql,
		state,
		statement_index,
		out_parser_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_replay_preprocess_text(
			input_sql,
			*out_parser_sql,
			origins,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK && out_parser_sql != NULL) {
		free(*out_parser_sql);
		*out_parser_sql = NULL;
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_value_clone(
	const sqlparser_dialect_multi_insert_value_t *source,
	sqlparser_dialect_multi_insert_value_t *target,
	sqlparser_error_t *out_error)
{
	if (source == NULL || target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value clone arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(target, 0, sizeof(*target));
	*target = *source;
	target->public_sql = sqlparser_strdup(source->public_sql);
	target->parser_sql = sqlparser_strdup(source->parser_sql);
	target->literal_string_value = sqlparser_strdup(source->literal_string_value);
	target->literal_float_value = sqlparser_strdup(source->literal_float_value);
	if ((source->public_sql != NULL && target->public_sql == NULL) ||
	    (source->parser_sql != NULL && target->parser_sql == NULL) ||
	    (source->literal_string_value != NULL && target->literal_string_value == NULL) ||
	    (source->literal_float_value != NULL && target->literal_float_value == NULL)) {
		sqlparser_oracle_value_clear(target);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (target->literal.kind == SQLPARSER_LITERAL_KIND_STRING) {
		target->literal.string_value = target->literal_string_value;
	} else if (target->literal.kind == SQLPARSER_LITERAL_KIND_FLOAT) {
		target->literal.float_value = target->literal_float_value;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_multi_insert_clone(
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
	clone->source_public_sql = sqlparser_strdup(source->source_public_sql);
	clone->source_parser_sql = sqlparser_strdup(source->source_parser_sql);
	if ((source->source_public_sql != NULL && clone->source_public_sql == NULL) ||
	    (source->source_parser_sql != NULL && clone->source_parser_sql == NULL)) {
		sqlparser_oracle_multi_insert_destroy(clone);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (source->branch_count > 0U) {
		clone->branches = (sqlparser_dialect_multi_insert_branch_t *)calloc(source->branch_count, sizeof(*clone->branches));
		if (clone->branches == NULL) {
			sqlparser_oracle_multi_insert_destroy(clone);
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
		dst_branch->relation.database_name = sqlparser_strdup(src_branch->relation.database_name);
		dst_branch->relation.schema_name = sqlparser_strdup(src_branch->relation.schema_name);
		dst_branch->relation.table_name = sqlparser_strdup(src_branch->relation.table_name);
		dst_branch->relation.link_name = sqlparser_strdup(src_branch->relation.link_name);
		dst_branch->relation.link_sql = sqlparser_strdup(src_branch->relation.link_sql);
		dst_branch->relation.sql = sqlparser_strdup(src_branch->relation.sql);
		if ((src_branch->relation.database_name != NULL && dst_branch->relation.database_name == NULL) ||
		    (src_branch->relation.schema_name != NULL && dst_branch->relation.schema_name == NULL) ||
		    (src_branch->relation.table_name != NULL && dst_branch->relation.table_name == NULL) ||
		    (src_branch->relation.link_name != NULL && dst_branch->relation.link_name == NULL) ||
		    (src_branch->relation.link_sql != NULL && dst_branch->relation.link_sql == NULL) ||
		    (src_branch->relation.sql != NULL && dst_branch->relation.sql == NULL)) {
			sqlparser_oracle_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (src_branch->column_count > 0U) {
			dst_branch->columns = (sqlparser_dialect_multi_insert_column_t *)calloc(src_branch->column_count, sizeof(*dst_branch->columns));
			if (dst_branch->columns == NULL) {
				sqlparser_oracle_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			dst_branch->column_count = src_branch->column_count;
			for (index = 0U; index < src_branch->column_count; index++) {
				dst_branch->columns[index].name = sqlparser_strdup(src_branch->columns[index].name);
				dst_branch->columns[index].sql = sqlparser_strdup(src_branch->columns[index].sql);
				if ((src_branch->columns[index].name != NULL && dst_branch->columns[index].name == NULL) ||
				    (src_branch->columns[index].sql != NULL && dst_branch->columns[index].sql == NULL)) {
					sqlparser_oracle_multi_insert_destroy(clone);
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
			}
		}
		if (src_branch->cell_count > 0U) {
			dst_branch->cells = (sqlparser_dialect_multi_insert_value_t *)calloc(src_branch->cell_count, sizeof(*dst_branch->cells));
			if (dst_branch->cells == NULL) {
				sqlparser_oracle_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			dst_branch->cell_count = src_branch->cell_count;
			for (index = 0U; index < src_branch->cell_count; index++) {
				if (sqlparser_oracle_value_clone(&src_branch->cells[index], &dst_branch->cells[index], out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_multi_insert_destroy(clone);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
			}
		}
		dst_branch->has_condition = src_branch->has_condition;
		dst_branch->is_else = src_branch->is_else;
		dst_branch->condition_group_id = src_branch->condition_group_id;
		dst_branch->condition_public_sql = sqlparser_strdup(src_branch->condition_public_sql);
		dst_branch->condition_parser_sql = sqlparser_strdup(src_branch->condition_parser_sql);
		if ((src_branch->condition_public_sql != NULL && dst_branch->condition_public_sql == NULL) ||
		    (src_branch->condition_parser_sql != NULL && dst_branch->condition_parser_sql == NULL)) {
			sqlparser_oracle_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	*out_clone = clone;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_bind_key_is_digits(const char *key)
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

static int sqlparser_oracle_bind_key_is_identifier(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0') {
		return 0;
	}
	if (!sqlparser_oracle_is_ident_start((unsigned char)key[0])) {
		return 0;
	}
	for (index = 1U; key[index] != '\0'; index++) {
		if (!sqlparser_oracle_is_ident_char((unsigned char)key[index])) {
			return 0;
		}
	}
	return 1;
}

sqlparser_status_t sqlparser_oracle_render_bind_value(
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
			if (!sqlparser_oracle_bind_key_is_digits(bind->key)) {
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
		if (!sqlparser_oracle_bind_key_is_identifier(bind->key)) {
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

sqlparser_status_t sqlparser_oracle_render_literal_value(
	const sqlparser_literal_value_t *value,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
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
		status = sqlparser_oracle_buffer_append_char(&out, '\'', out_error);
		for (index = 0U; status == SQLPARSER_STATUS_OK && text[index] != '\0'; index++) {
			if (text[index] == '\'') {
				status = sqlparser_oracle_buffer_append_cstr(&out, "''", out_error);
			} else {
				status = sqlparser_oracle_buffer_append_char(&out, text[index], out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_char(&out, '\'', out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_oracle_buffer_take(&out);
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

sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *replacement;
	sqlparser_oracle_state_t *state;
	sqlparser_dialect_multi_insert_t *multi;
	sqlparser_dialect_multi_insert_branch_t *branch;
	sqlparser_parse_options_t options;
	char *public_sql;
	sqlparser_status_t status;

	if (handle == NULL || sql_text == NULL || sql_text[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert cell replacement arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	replacement = NULL;
	public_sql = NULL;
	state = (sqlparser_oracle_state_t *)handle->dialect_state;
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
	options.dialect = handle->dialect;
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

sqlparser_status_t sqlparser_oracle_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_oracle_state_t *state;
	const sqlparser_dialect_multi_insert_t *multi;
	const sqlparser_dialect_multi_insert_branch_t *branch;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	state = (const sqlparser_oracle_state_t *)handle->dialect_state;
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

sqlparser_status_t sqlparser_oracle_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_oracle_state_t *state;
	const sqlparser_dialect_multi_insert_t *multi;
	const sqlparser_dialect_multi_insert_branch_t *branch;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	state = (const sqlparser_oracle_state_t *)handle->dialect_state;
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

sqlparser_status_t sqlparser_oracle_multi_insert_insert_column_sql(
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
	sqlparser_oracle_state_t *state;
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
	int has_cell;

	sqlparser_error_clear(out_error);
	has_cell = cell_sql != NULL;
	if (handle == NULL || column_sql == NULL || column_sql[0] == '\0' ||
	    (has_cell && cell_sql[0] == '\0')) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert column insertion arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT branch");
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
	state = (sqlparser_oracle_state_t *)candidate->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (branch->column_count == SIZE_MAX ||
	    (has_cell && branch->cell_count == SIZE_MAX)) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "multi insert branch is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	status = sqlparser_oracle_trimmed_slice_dup(column_sql, 0U, strlen(column_sql), &new_column.sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_identifier_from_sql(
			column_sql,
			0U,
			strlen(column_sql),
			&new_column.name,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_cell) {
		status = sqlparser_oracle_parse_value_item(
			cell_sql,
			0U,
			strlen(cell_sql),
			state,
			&new_cell,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_column_clear(&new_column);
		sqlparser_oracle_value_clear(&new_cell);
		sqlparser_handle_destroy(candidate);
		return status;
	}

	next_columns = (sqlparser_dialect_multi_insert_column_t *)calloc(branch->column_count + 1U, sizeof(*next_columns));
	if (has_cell) {
		next_cells = (sqlparser_dialect_multi_insert_value_t *)calloc(
			branch->cell_count + 1U, sizeof(*next_cells));
	}
	if (next_columns == NULL || (has_cell && next_cells == NULL)) {
		free(next_columns);
		free(next_cells);
		sqlparser_oracle_column_clear(&new_column);
		sqlparser_oracle_value_clear(&new_cell);
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	column_insert_index = column_index > branch->column_count ? branch->column_count : column_index;
	for (index = 0U; index < column_insert_index; index++) {
		next_columns[index] = branch->columns[index];
	}
	next_columns[column_insert_index] = new_column;
	memset(&new_column, 0, sizeof(new_column));
	for (index = column_insert_index; index < branch->column_count; index++) {
		next_columns[index + 1U] = branch->columns[index];
	}
	if (has_cell) {
		cell_insert_index = column_index > branch->cell_count ?
			branch->cell_count : column_index;
		for (index = 0U; index < cell_insert_index; index++) {
			next_cells[index] = branch->cells[index];
		}
		next_cells[cell_insert_index] = new_cell;
		memset(&new_cell, 0, sizeof(new_cell));
		for (index = cell_insert_index; index < branch->cell_count; index++) {
			next_cells[index + 1U] = branch->cells[index];
		}
	}
	free(branch->columns);
	branch->columns = next_columns;
	branch->column_count++;
	next_columns = NULL;
	if (has_cell) {
		free(branch->cells);
		branch->cells = next_cells;
		branch->cell_count++;
		next_cells = NULL;
	}

	candidate->generation++;
	status = sqlparser_deparse(candidate, &public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_parse_options_default(&options);
		options.dialect = handle->dialect;
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

static sqlparser_status_t sqlparser_oracle_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_oracle_state_t *source;
	sqlparser_oracle_state_t *clone;
	size_t index;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	source = (const sqlparser_oracle_state_t *)state;
	status = sqlparser_oracle_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->bind_occurrence_count = source->bind_occurrence_count;
	clone->next_dblink_id = source->next_dblink_id;

	for (index = 0U; index < source->bind_count; index++) {
		size_t param_index;

		status = sqlparser_oracle_state_append_bind(
			clone,
			source->bind_names[index],
			strlen(source->bind_names[index]),
			&param_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_state_destroy(clone);
			return status;
		}
		(void)param_index;
	}

	status = sqlparser_dialect_national_literals_clone(
		&source->national_literals,
		&clone->national_literals,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(clone);
		return status;
	}
	status = sqlparser_dialect_minuses_clone(
		&source->minuses,
		&clone->minuses,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(clone);
		return status;
	}

	for (index = 0U; index < source->dblink_count; index++) {
		status = sqlparser_oracle_state_append_dblink_relation(
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
			sqlparser_oracle_state_destroy(clone);
			return status;
		}
	}

	status = sqlparser_oracle_multi_insert_clone(source->multi_insert, &clone->multi_insert, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(clone);
		return status;
	}
	status = sqlparser_dialect_returning_into_state_clone(
		&source->returning_into,
		&clone->returning_into,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(clone);
		return status;
	}

	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_oracle_relation_object_name(
	const void *state,
	const char *parser_object_name,
	const char **out_spelling)
{
	const sqlparser_oracle_dblink_relation_t *relation;

	if (out_spelling != NULL) {
		*out_spelling = NULL;
	}
	relation = sqlparser_oracle_state_find_dblink_relation(
		(const sqlparser_oracle_state_t *)state,
		parser_object_name);
	if (relation != NULL && out_spelling != NULL) {
		*out_spelling = relation->public_object_sql;
	}
	return relation != NULL ? relation->public_object_name : NULL;
}

static const char *sqlparser_oracle_relation_link_name(
	const void *state,
	const char *parser_object_name)
{
	const sqlparser_oracle_dblink_relation_t *relation;

	relation = sqlparser_oracle_state_find_dblink_relation(
		(const sqlparser_oracle_state_t *)state,
		parser_object_name);
	return relation != NULL ? relation->public_link_name : NULL;
}

static const char *sqlparser_oracle_relation_link_sql(
	const void *state,
	const char *parser_object_name)
{
	const sqlparser_oracle_dblink_relation_t *relation;

	relation = sqlparser_oracle_state_find_dblink_relation(
		(const sqlparser_oracle_state_t *)state,
		parser_object_name);
	return relation != NULL ? relation->public_link_sql : NULL;
}

static sqlparser_status_t sqlparser_oracle_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	const PgQuery__AConst *literal_owner,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_dialect_national_literal_t *literal;
	const sqlparser_oracle_state_t *oracle_state;
	size_t literal_end;
	sqlparser_oracle_buffer_t out;
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

	oracle_state = (const sqlparser_oracle_state_t *)state;
	literal = oracle_state != NULL ?
		sqlparser_dialect_national_literals_find_owner(
			&oracle_state->national_literals, literal_owner) :
		NULL;
	literal_end = core_sql[0] == '\'' ? sqlparser_oracle_quoted_literal_end(core_sql, 0U) : 0U;
	if (literal_end > 0U &&
	    core_sql[literal_end] == '\0' &&
	    literal != NULL &&
	    sqlparser_oracle_national_literal_matches(
		    oracle_state, literal->ordinal, core_sql, 0U, literal_end)) {
		memset(&out, 0, sizeof(out));
		status = sqlparser_oracle_buffer_append_char(&out, 'N', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, core_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_oracle_buffer_take(&out);
		return SQLPARSER_STATUS_OK;
	}

	*out_sql = sqlparser_strdup(core_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static const PgQuery__VariableSetStmt *sqlparser_oracle_session_set_statement(
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

static int sqlparser_oracle_session_name_has_prefix(
	const char *name,
	const char *prefix,
	const char **out_suffix)
{
	size_t prefix_length;

	if (name == NULL || prefix == NULL || out_suffix == NULL) {
		return 0;
	}
	prefix_length = strlen(prefix);
	if (strncmp(name, prefix, prefix_length) != 0 || name[prefix_length] == '\0') {
		return 0;
	}
	*out_suffix = name + prefix_length;
	return 1;
}

static sqlparser_status_t sqlparser_oracle_session_emit_ast_item(
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
		const char *value_name;

		value_name = target_kind == SQLPARSER_GRAPH_SESSION_TARGET_CONTAINER && index == 1U ?
			"service" : NULL;
		if (emitter->add_ast_value(
			    emitter->context,
			    item_index,
			    value_name,
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

static int sqlparser_oracle_session_span_equal_word(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t length;

	if (sql == NULL || word == NULL || end < start) {
		return 0;
	}
	length = strlen(word);
	if (end - start != length) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_oracle_session_span_is_keyword(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const keywords[] = {
		"active", "all", "commit", "dbtimezone", "default", "deferred",
		"entry", "false", "immediate", "local", "none", "nothing",
		"rollback", "serializable", "true"
	};
	size_t index;

	for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
		if (sqlparser_oracle_session_span_equal_word(sql, start, end, keywords[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_oracle_session_span_is_integer(
	const char *sql,
	size_t start,
	size_t end,
	long long *out_value)
{
	unsigned long long value;
	unsigned long long limit;
	unsigned int digit;
	int negative;

	if (sql == NULL || out_value == NULL || start >= end) {
		return 0;
	}
	negative = 0;
	if (sql[start] == '+' || sql[start] == '-') {
		negative = sql[start] == '-';
		start++;
		if (start == end) {
			return 0;
		}
	}
	value = 0U;
	limit = negative ? (unsigned long long)LLONG_MAX + 1U : (unsigned long long)LLONG_MAX;
	while (start < end) {
		if (!isdigit((unsigned char)sql[start])) {
			return 0;
		}
		digit = (unsigned int)(sql[start] - '0');
		if (value > (limit - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		start++;
	}
	*out_value = negative ?
		(value == (unsigned long long)LLONG_MAX + 1U ? LLONG_MIN : -(long long)value) :
		(long long)value;
	return 1;
}

static char *sqlparser_oracle_session_decode_string(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t q_prefix_len;
	size_t pos;
	sqlparser_status_t status;

	if (sql == NULL || end <= start) {
		return NULL;
	}
	q_prefix_len = sqlparser_oracle_q_quote_prefix_len(sql + start);
	if (q_prefix_len > 0U &&
	    end - start >= q_prefix_len + 4U &&
	    sqlparser_oracle_skip_quoted_or_comment_span(sql, start) == end) {
		char *decoded;

		decoded = sqlparser_strndup(
			sql + start + q_prefix_len + 2U,
			end - start - q_prefix_len - 4U);
		if (decoded == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
		}
		return decoded;
	}
	if (end - start < 2U ||
	    sql[start] != '\'' ||
	    sql[end - 1U] != '\'') {
		return NULL;
	}
	memset(&out, 0, sizeof(out));
	for (pos = start + 1U; pos < end - 1U; pos++) {
		if (sql[pos] == '\'' && pos + 1U < end - 1U && sql[pos + 1U] == '\'') {
			pos++;
		}
		status = sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return NULL;
		}
	}
	status = sqlparser_oracle_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return NULL;
	}
	return sqlparser_oracle_buffer_take(&out);
}

static sqlparser_status_t sqlparser_oracle_session_emit_span_value(
	const char *sql,
	size_t start,
	size_t end,
	size_t item_index,
	const char *name,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;
	char *decoded;
	long long integer_value;

	start = sqlparser_oracle_skip_space_bounded(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session value is empty");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? strlen(name) : 0U;
	decoded = sqlparser_oracle_session_decode_string(sql, start, end, out_error);
	if (decoded != NULL) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value.literal.string_value = decoded;
		if (emitter->add_value(
			    emitter->context,
			    item_index,
			    &value,
			    out_error) != SQLPARSER_STATUS_OK) {
			free(decoded);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		free(decoded);
		return SQLPARSER_STATUS_OK;
	}
	if (out_error != NULL &&
	    (out_error->code == SQLPARSER_STATUS_NO_MEMORY ||
	     out_error->code == SQLPARSER_STATUS_RESOURCE_LIMIT)) {
		return out_error->code;
	}
	sqlparser_error_clear(out_error);
	if (sqlparser_oracle_session_span_is_integer(sql, start, end, &integer_value)) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
		value.literal.integer_value = integer_value;
	} else {
		value.kind = sqlparser_oracle_session_span_is_keyword(sql, start, end) ?
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD :
			(sqlparser_oracle_identifier_token_end_bounded(sql, start, end) == end ?
				SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER :
				SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION);
		value.text = sql + start;
		value.text_length = end - start;
	}
	return emitter->add_value(
		emitter->context,
		item_index,
		&value,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_session_add_raw_item(
	const char *sql,
	size_t name_start,
	size_t name_end,
	sqlparser_graph_session_target_kind_t target_kind,
	const sqlparser_dialect_session_emitter_t *emitter,
	size_t *out_item_index,
	sqlparser_error_t *out_error)
{
	name_start = sqlparser_oracle_skip_space_bounded(sql, name_start, name_end);
	name_end = sqlparser_oracle_trim_right(sql, name_start, name_end);
	return emitter->add_item(
		emitter->context,
		SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		target_kind,
		name_start < name_end ? sql + name_start : NULL,
		name_start < name_end ? name_end - name_start : 0U,
		out_item_index,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_session_project_raw_set(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	size_t name_start;
	size_t name_end;
	size_t scan;
	size_t value_start;
	size_t value_end;

	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	name_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	scan = name_start;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "row") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "archival") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "visibility")) {
		name_end = sqlparser_oracle_trim_right(sql, name_start, scan);
		scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		if (scan >= end || sql[scan] != '=') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session assignment is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		scan++;
		value_start = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		if (sqlparser_oracle_session_add_raw_item(
			    sql,
			    name_start,
			    name_end,
			    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			    emitter,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_oracle_session_emit_span_value(
			sql, value_start, end, item_index, NULL, emitter, out_error);
	}
	scan = name_start;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "default") &&
	    sqlparser_oracle_consume_word_bounded(sql, &scan, end, "collation")) {
		name_end = sqlparser_oracle_trim_right(sql, name_start, scan);
		scan = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		if (scan >= end || sql[scan] != '=') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session assignment is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		scan++;
		value_start = sqlparser_oracle_skip_space_bounded(sql, scan, end);
		if (sqlparser_oracle_session_add_raw_item(
			    sql,
			    name_start,
			    name_end,
			    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			    emitter,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_oracle_session_emit_span_value(
			sql, value_start, end, item_index, NULL, emitter, out_error);
	}

	pos = name_start;
	while (pos < end) {
		name_start = pos;
		name_end = sqlparser_oracle_identifier_token_end_bounded(sql, name_start, end);
		if (name_end <= name_start) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session parameter is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		scan = sqlparser_oracle_skip_space_bounded(sql, name_end, end);
		if (scan >= end || sql[scan] != '=') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session assignment is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		value_start = sqlparser_oracle_skip_space_bounded(sql, scan + 1U, end);
		value_end = sqlparser_oracle_session_value_token_end(
			sql, value_start, end, out_error);
		if (value_end <= value_start ||
		    sqlparser_oracle_session_add_raw_item(
			    sql,
			    name_start,
			    name_end,
			    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			    emitter,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_oracle_session_emit_span_value(
			    sql,
			    value_start,
			    value_end,
			    item_index,
			    NULL,
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pos = sqlparser_oracle_skip_space_bounded(sql, value_end, end);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_session_project_parallel(
	const char *sql,
	size_t parallel_start,
	size_t pos,
	size_t end,
	sqlparser_graph_session_action_t action,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	size_t name_end;
	size_t value_start;

	if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, "dml") &&
	    !sqlparser_oracle_consume_word_bounded(sql, &pos, end, "ddl") &&
	    !sqlparser_oracle_consume_word_bounded(sql, &pos, end, "query")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle parallel session target is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	name_end = sqlparser_oracle_trim_right(sql, parallel_start, pos);
	if (emitter->set_action(emitter->context, action, out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_oracle_session_add_raw_item(
		    sql,
		    parallel_start,
		    name_end,
		    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
		    emitter,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	if (pos == end) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, "parallel")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle parallel degree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	value_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	return sqlparser_oracle_session_emit_span_value(
		sql, value_start, end, item_index, "degree", emitter, out_error);
}

static sqlparser_status_t sqlparser_oracle_session_project_resumable(
	const char *sql,
	size_t resumable_start,
	size_t pos,
	size_t end,
	sqlparser_graph_session_action_t action,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	size_t token_end;

	if (emitter->set_action(emitter->context, action, out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_oracle_session_add_raw_item(
		    sql,
		    resumable_start,
		    resumable_start + strlen("resumable"),
		    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
		    emitter,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	if (pos == end) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "timeout")) {
		pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
		token_end = pos;
		while (token_end < end && isdigit((unsigned char)sql[token_end])) {
			token_end++;
		}
		if (sqlparser_oracle_session_emit_span_value(
			    sql,
			    pos,
			    token_end,
			    item_index,
			    "timeout",
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pos = sqlparser_oracle_skip_space_bounded(sql, token_end, end);
	}
	if (pos == end) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, "name")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle resumable name is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	token_end = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
	return sqlparser_oracle_session_emit_span_value(
		sql, pos, token_end, item_index, "name", emitter, out_error);
}

static sqlparser_status_t sqlparser_oracle_session_project_raw_action(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_action_t action;
	size_t item_index;
	size_t name_start;
	size_t scan;

	if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "advise")) {
		if (emitter->set_action(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_ACTION_ADVISE,
			    out_error) != SQLPARSER_STATUS_OK ||
		    emitter->add_item(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			    SQLPARSER_GRAPH_SESSION_TARGET_TRANSACTION,
			    NULL,
			    0U,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
		return sqlparser_oracle_session_emit_span_value(
			sql, pos, end, item_index, NULL, emitter, out_error);
	}
	if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "close")) {
		if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, "database") ||
		    !sqlparser_oracle_consume_word_bounded(sql, &pos, end, "link")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle close session target is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		name_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
		if (emitter->set_action(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_ACTION_CLOSE,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_oracle_session_add_raw_item(
			sql,
			name_start,
			end,
			SQLPARSER_GRAPH_SESSION_TARGET_DATABASE_LINK,
			emitter,
			NULL,
			out_error);
	}
	if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "sync")) {
		if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, "with")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle sync session target is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		name_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
		if (emitter->set_action(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_ACTION_SYNC,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_oracle_session_add_raw_item(
			sql,
			name_start,
			end,
			SQLPARSER_GRAPH_SESSION_TARGET_OBJECT,
			emitter,
			NULL,
			out_error);
	}

	action = SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN;
	if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "enable")) {
		action = SQLPARSER_GRAPH_SESSION_ACTION_ENABLE;
	} else if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "disable")) {
		action = SQLPARSER_GRAPH_SESSION_ACTION_DISABLE;
	} else if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "force")) {
		action = SQLPARSER_GRAPH_SESSION_ACTION_FORCE;
	}
	if (action == SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session action is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	name_start = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	scan = name_start;
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "parallel")) {
		return sqlparser_oracle_session_project_parallel(
			sql, name_start, scan, end, action, emitter, out_error);
	}
	if (sqlparser_oracle_consume_word_bounded(sql, &scan, end, "resumable")) {
		return sqlparser_oracle_session_project_resumable(
			sql, name_start, scan, end, action, emitter, out_error);
	}
	if (emitter->set_action(emitter->context, action, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return sqlparser_oracle_session_add_raw_item(
		sql,
		name_start,
		end,
		SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
		emitter,
		NULL,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_session_project_raw(
	const char *sql,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t end;
	size_t pos;

	end = strlen(sql);
	pos = sqlparser_oracle_skip_space_bounded(sql, 0U, end);
	if (!sqlparser_oracle_consume_word_bounded(sql, &pos, end, "alter") ||
	    !sqlparser_oracle_consume_word_bounded(sql, &pos, end, "session")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Oracle session statement is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_oracle_skip_space_bounded(sql, pos, end);
	if (sqlparser_oracle_consume_word_bounded(sql, &pos, end, "set")) {
		return sqlparser_oracle_session_project_raw_set(
			sql, pos, end, emitter, out_error);
	}
	return sqlparser_oracle_session_project_raw_action(
		sql, pos, end, emitter, out_error);
}

static const char *sqlparser_oracle_session_raw_argument(
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

static sqlparser_status_t sqlparser_oracle_project_session(
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

	(void)handle;
	(void)state;
	(void)statement_index;
	if (emitter == NULL || emitter->set_action == NULL ||
	    emitter->add_item == NULL || emitter->add_value == NULL ||
	    emitter->add_ast_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph emitter is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	set_stmt = sqlparser_oracle_session_set_statement(statement);
	if (set_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(set_stmt->name, SQLPARSER_INTERNAL_CURRENT_SCHEMA) == 0) {
		return sqlparser_oracle_session_emit_ast_item(
			set_stmt,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			emitter,
			out_error);
	}
	if (strcmp(set_stmt->name, SQLPARSER_INTERNAL_CURRENT_DATABASE) == 0) {
		return sqlparser_oracle_session_emit_ast_item(
			set_stmt,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_TARGET_CONTAINER,
			NULL,
			emitter,
			out_error);
	}
	if (sqlparser_oracle_session_name_has_prefix(
		    set_stmt->name,
		    SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX,
		    &name)) {
		return sqlparser_oracle_session_emit_ast_item(
			set_stmt,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			name,
			emitter,
			out_error);
	}
	raw_sql = sqlparser_oracle_session_raw_argument(
		set_stmt,
		SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT);
	return raw_sql != NULL ?
		sqlparser_oracle_session_project_raw(raw_sql, emitter, out_error) :
		SQLPARSER_STATUS_OK;
}

typedef struct {
	sqlparser_oracle_state_t *state;
} sqlparser_oracle_dblink_bind_t;

typedef struct {
	sqlparser_oracle_state_t *state;
	size_t source_count;
} sqlparser_oracle_dblink_clone_t;

static int sqlparser_oracle_dblink_name_compare(
	const void *left,
	const void *right)
{
	const sqlparser_oracle_dblink_relation_t *left_relation;
	const sqlparser_oracle_dblink_relation_t *right_relation;

	left_relation = (const sqlparser_oracle_dblink_relation_t *)left;
	right_relation = (const sqlparser_oracle_dblink_relation_t *)right;
	return strcmp(
		left_relation->parser_object_name,
		right_relation->parser_object_name);
}

static size_t sqlparser_oracle_dblink_name_lower_bound(
	const sqlparser_oracle_state_t *state,
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

static void sqlparser_oracle_dblink_bind_visit(
	PgQuery__RangeVar *relation,
	size_t statement_index,
	void *context)
{
	sqlparser_oracle_dblink_bind_t *bind;
	size_t index;

	(void)statement_index;
	if (relation == NULL || relation->relname == NULL) {
		return;
	}
	bind = (sqlparser_oracle_dblink_bind_t *)context;
	index = sqlparser_oracle_dblink_name_lower_bound(
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

static sqlparser_status_t sqlparser_oracle_bind_dblink_state(
	sqlparser_oracle_state_t *state,
	const PgQuery__ParseResult *ast,
	size_t statement_index,
	ProtobufCMessage *const *roots,
	size_t root_count,
	int require_all,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_oracle_dblink_bind_t bind;
	size_t index;

	if (state == NULL || state->dblink_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (state->dblink_count > 1U) {
		qsort(
			state->dblink_relations,
			state->dblink_count,
			sizeof(*state->dblink_relations),
			sqlparser_oracle_dblink_name_compare);
	}
	for (index = 0U; index < state->dblink_count; index++) {
		state->dblink_relations[index].owner = NULL;
	}
	memset(&bind, 0, sizeof(bind));
	bind.state = state;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &bind;
	visitor.relation = sqlparser_oracle_dblink_bind_visit;
	visitor.insert_relation = sqlparser_oracle_dblink_bind_visit;
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
					"Oracle database-link AST owner is missing");
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_oracle_reconcile_dblink_state(
	sqlparser_oracle_state_t *state,
	const PgQuery__ParseResult *ast)
{
	size_t read_index;
	size_t write_index;

	if (state == NULL || ast == NULL || state->dblink_count == 0U) {
		return;
	}
	(void)sqlparser_oracle_bind_dblink_state(
		state, ast, 0U, NULL, 0U, 0, NULL);
	write_index = 0U;
	for (read_index = 0U; read_index < state->dblink_count; read_index++) {
		if (state->dblink_relations[read_index].owner == NULL) {
			sqlparser_oracle_dblink_relation_clear(
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

static sqlparser_status_t sqlparser_oracle_dblink_clone_visit(
	const PgQuery__RangeVar *source,
	PgQuery__RangeVar *clone,
	void *context,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_dblink_clone_t *clone_context;
	sqlparser_oracle_dblink_relation_t *record;
	sqlparser_status_t status;
	size_t index;

	if (source == NULL || source->relname == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	clone_context = (sqlparser_oracle_dblink_clone_t *)context;
	index = sqlparser_oracle_dblink_name_lower_bound(
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
	status = sqlparser_oracle_state_append_dblink_relation(
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

static sqlparser_status_t sqlparser_oracle_clone_dblink_owners(
	sqlparser_oracle_state_t *state,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_clone_visitor_t visitor;
	sqlparser_oracle_dblink_clone_t clone_context;
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
			sqlparser_oracle_dblink_name_compare);
	}
	memset(&clone_context, 0, sizeof(clone_context));
	clone_context.state = state;
	clone_context.source_count = original_count;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &clone_context;
	visitor.relation = sqlparser_oracle_dblink_clone_visit;
	status = sqlparser_dialect_ast_surface_clone(
		source_root,
		(ProtobufCMessage *)clone_root,
		&visitor,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		while (state->dblink_count > original_count) {
			state->dblink_count--;
			sqlparser_oracle_dblink_relation_clear(
				&state->dblink_relations[state->dblink_count]);
		}
		if (status == SQLPARSER_STATUS_INTERNAL_ERROR) {
			sqlparser_error_set_message(
				out_error,
				status,
				"cloned Oracle database-link AST shape is inconsistent");
		}
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_bind_ast_state(
	void *state,
	const PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *oracle_state;
	sqlparser_status_t status;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	oracle_state = (sqlparser_oracle_state_t *)state;
	if (ast == NULL) {
		sqlparser_dialect_prepared_binds_clear(
			&oracle_state->prepared_binds);
	}
	status = sqlparser_dialect_national_literals_bind_ast(
		&oracle_state->national_literals,
		ast,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_minuses_bind_ast(
			&oracle_state->minuses, ast, out_error);
	}
	return status == SQLPARSER_STATUS_OK ?
		sqlparser_oracle_bind_dblink_state(
			oracle_state,
			ast,
			0U,
			NULL,
			0U,
			ast != NULL,
			out_error) :
		status;
}

static sqlparser_status_t sqlparser_oracle_bind_fragment_ast_state(
	void *state,
	const PgQuery__ParseResult *base_ast,
	size_t statement_index,
	size_t parser_fragment_offset,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *oracle_state;
	sqlparser_status_t status;

	(void)parser_fragment_offset;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	oracle_state = (sqlparser_oracle_state_t *)state;
	status = sqlparser_dialect_national_literals_bind_fragment(
		&oracle_state->national_literals,
		base_ast,
		roots,
		root_count,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_minuses_bind_fragment(
			&oracle_state->minuses,
			base_ast,
			roots,
			root_count,
			out_error);
	}
	return status == SQLPARSER_STATUS_OK ?
		sqlparser_oracle_bind_dblink_state(
			oracle_state,
			base_ast,
			statement_index,
			roots,
			root_count,
			1,
			out_error) :
		status;
}

static void sqlparser_oracle_reconcile_ast_state(
	void *state,
	const PgQuery__ParseResult *ast)
{
	if (state != NULL) {
		sqlparser_oracle_state_t *oracle_state;

		oracle_state = (sqlparser_oracle_state_t *)state;
		sqlparser_dialect_reconcile_binds(
			&oracle_state->prepared_binds,
			&oracle_state->bind_names,
			&oracle_state->bind_count,
			&oracle_state->bind_capacity,
			&oracle_state->bind_occurrence_count);
		sqlparser_dialect_national_literals_reconcile(
			&oracle_state->national_literals,
			ast);
		sqlparser_dialect_minuses_reconcile(
			&oracle_state->minuses, ast);
		sqlparser_oracle_reconcile_dblink_state(oracle_state, ast);
	}
}

static sqlparser_status_t sqlparser_oracle_clone_ast_state(
	void *state,
	size_t statement_index,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *oracle_state;
	sqlparser_status_t status;
	size_t original_minus_count;
	size_t original_national_count;

	(void)statement_index;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	oracle_state = (sqlparser_oracle_state_t *)state;
	original_minus_count = oracle_state->minuses.count;
	original_national_count = oracle_state->national_literals.count;
	status = sqlparser_dialect_minuses_clone_owners(
		&oracle_state->minuses,
		source_root,
		clone_root,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_national_literals_clone_owners(
			&oracle_state->national_literals,
			source_root,
			clone_root,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_clone_dblink_owners(
			oracle_state, source_root, clone_root, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		oracle_state->minuses.count = original_minus_count;
		while (oracle_state->national_literals.count >
		       original_national_count) {
			oracle_state->national_literals.count--;
			free(oracle_state->national_literals.items[
				oracle_state->national_literals.count].sql);
			free(oracle_state->national_literals.items[
				oracle_state->national_literals.count].surface_sql);
		}
	}
	return status;
}

static sqlparser_status_t sqlparser_oracle_prepare_ast_state(
	void *state,
	PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *oracle_state;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	oracle_state = (sqlparser_oracle_state_t *)state;
	return sqlparser_dialect_prepare_binds(
		oracle_state->bind_names,
		oracle_state->bind_count,
		ast,
		&oracle_state->prepared_binds,
		out_error);
}

static const sqlparser_dialect_ops_t SQLPARSER_ORACLE_OPS = {
	SQLPARSER_DIALECT_ORACLE,
	"oracle",
	sqlparser_oracle_preprocess,
	sqlparser_oracle_preprocess_fragment,
	sqlparser_oracle_postprocess_deparse,
	sqlparser_oracle_clone_state,
	sqlparser_oracle_state_destroy,
	sqlparser_oracle_postprocess_literal_fragment,
	NULL,
	NULL,
	sqlparser_oracle_relation_object_name,
	sqlparser_oracle_relation_link_name,
	sqlparser_oracle_postprocess_fragment,
	NULL,
	NULL,
	sqlparser_oracle_project_session,
	sqlparser_oracle_bind_ast_state,
	sqlparser_oracle_bind_fragment_ast_state,
	sqlparser_oracle_reconcile_ast_state,
	sqlparser_oracle_clone_ast_state,
	sqlparser_oracle_prepare_ast_state,
	sqlparser_oracle_relation_link_sql
};

const sqlparser_dialect_ops_t *sqlparser_dialect_oracle_ops(void)
{
	return &SQLPARSER_ORACLE_OPS;
}
