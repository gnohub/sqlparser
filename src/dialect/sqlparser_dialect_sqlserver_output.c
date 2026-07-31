#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_sqlserver_output.h"
#include "sqlparser_dialect_sqlserver_scan.h"

#define SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE 64U

typedef struct {
	size_t target_index;
	char marker[SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE];
} sqlparser_sqlserver_output_action_t;

typedef struct {
	sqlparser_graph_dml_result_kind_t kind;
	size_t target_offset;
	size_t target_count;
	char *sink_sql;
	char **sink_columns;
	size_t sink_column_count;
	size_t sink_column_capacity;
	sqlparser_sqlserver_output_action_t *actions;
	size_t action_count;
	size_t action_capacity;
} sqlparser_sqlserver_output_channel_t;

typedef struct {
	sqlparser_graph_dml_kind_t kind;
	size_t statement_index;
	char *top_sql;
	char *source_name;
	char *source_sql;
	char *delete_target_sql;
	int omitted_into;
	int delete_source_from;
	sqlparser_sqlserver_output_channel_t *channels;
	size_t channel_count;
	size_t channel_capacity;
	size_t parent_dml_index;
	int has_parent;
} sqlparser_sqlserver_output_dml_t;

struct sqlparser_sqlserver_output_state {
	sqlparser_sqlserver_output_dml_t *dmls;
	size_t dml_count;
	size_t dml_capacity;
	size_t max_dml_count;
};

typedef struct {
	sqlparser_sqlserver_token_t *items;
	size_t count;
	size_t capacity;
} sqlparser_sqlserver_output_tokens_t;

typedef enum {
	SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_INPUT = 1,
	SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_SOURCE_IDENTIFIER,
	SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_GENERATED
} sqlparser_sqlserver_output_origin_kind_t;

typedef struct {
	sqlparser_sqlserver_output_origin_kind_t kind;
	size_t output_offset;
	size_t output_length;
	size_t source_offset;
	size_t source_length;
} sqlparser_sqlserver_output_origin_piece_t;

typedef struct {
	size_t start;
	size_t end;
	const char *replacement;
	size_t replacement_len;
	char *owned_replacement;
	sqlparser_sqlserver_output_origin_piece_t *origin_pieces;
	size_t origin_piece_count;
	size_t origin_piece_capacity;
} sqlparser_sqlserver_output_edit_t;

typedef struct {
	sqlparser_sqlserver_output_edit_t *items;
	size_t count;
	size_t capacity;
	sqlparser_identifier_origin_map_t *origins;
} sqlparser_sqlserver_output_edits_t;

typedef struct {
	size_t start;
	size_t end;
} sqlparser_sqlserver_output_span_t;

typedef struct {
	size_t start;
	size_t end;
	char *name;
	char *parser_sql;
	sqlparser_identifier_origin_map_t *origins;
	size_t source_offset;
} sqlparser_sqlserver_output_nested_t;

static int sqlparser_sqlserver_output_ascii_contains(const char *sql, const char *word);

static sqlparser_status_t sqlparser_sqlserver_output_reserve(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	const char *message,
	sqlparser_error_t *out_error)
{
	void *next;
	size_t next_capacity;

	if (required <= *capacity) {
		return SQLPARSER_STATUS_OK;
	}
	next_capacity = *capacity == 0U ? 4U : *capacity;
	while (next_capacity < required) {
		if (next_capacity > SIZE_MAX / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, message);
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		next_capacity *= 2U;
	}
	if (item_size != 0U && next_capacity > SIZE_MAX / item_size) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, message);
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	next = realloc(*items, next_capacity * item_size);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*items = next;
	*capacity = next_capacity;
	return SQLPARSER_STATUS_OK;
}

static char *sqlparser_sqlserver_output_dup_trim(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	char *text;

	start = sqlparser_sqlserver_trim_left(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT element must not be empty");
		return NULL;
	}
	text = sqlparser_strndup(sql + start, end - start);
	if (text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return text;
}

static void sqlparser_sqlserver_output_channel_clear(sqlparser_sqlserver_output_channel_t *channel)
{
	size_t index;

	if (channel == NULL) {
		return;
	}
	free(channel->sink_sql);
	for (index = 0U; index < channel->sink_column_count; index++) {
		free(channel->sink_columns[index]);
	}
	free(channel->sink_columns);
	free(channel->actions);
	memset(channel, 0, sizeof(*channel));
}

static void sqlparser_sqlserver_output_dml_clear(sqlparser_sqlserver_output_dml_t *dml)
{
	size_t index;

	if (dml == NULL) {
		return;
	}
	free(dml->top_sql);
	free(dml->source_name);
	free(dml->source_sql);
	free(dml->delete_target_sql);
	for (index = 0U; index < dml->channel_count; index++) {
		sqlparser_sqlserver_output_channel_clear(&dml->channels[index]);
	}
	free(dml->channels);
	memset(dml, 0, sizeof(*dml));
}

void sqlparser_sqlserver_output_destroy(sqlparser_sqlserver_output_state_t *state)
{
	size_t index;

	if (state == NULL) {
		return;
	}
	for (index = 0U; index < state->dml_count; index++) {
		sqlparser_sqlserver_output_dml_clear(&state->dmls[index]);
	}
	free(state->dmls);
	free(state);
}

static void sqlparser_sqlserver_output_tokens_clear(sqlparser_sqlserver_output_tokens_t *tokens)
{
	if (tokens == NULL) {
		return;
	}
	free(tokens->items);
	memset(tokens, 0, sizeof(*tokens));
}

static void sqlparser_sqlserver_output_edits_clear(sqlparser_sqlserver_output_edits_t *edits)
{
	size_t index;

	if (edits == NULL) {
		return;
	}
	for (index = 0U; index < edits->count; index++) {
		free(edits->items[index].owned_replacement);
		free(edits->items[index].origin_pieces);
	}
	free(edits->items);
	memset(edits, 0, sizeof(*edits));
}

static sqlparser_status_t sqlparser_sqlserver_output_tokenize(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_sqlserver_output_tokens_t *out_tokens,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_status_t status;

	memset(out_tokens, 0, sizeof(*out_tokens));
	status = sqlparser_sqlserver_scanner_init(&scanner, sql, start, end, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_output_tokens_clear(out_tokens);
			return status;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_sqlserver_output_reserve(
			(void **)&out_tokens->items,
			&out_tokens->capacity,
			out_tokens->count + 1U,
			sizeof(*out_tokens->items),
			"SQL Server token count is too large",
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_output_tokens_clear(out_tokens);
			return status;
		}
		out_tokens->items[out_tokens->count++] = token;
	}
}

static sqlparser_status_t sqlparser_sqlserver_output_validate_identifier(
	const char *sql,
	size_t start,
	size_t end,
	size_t max_parts,
	const char *message,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	size_t part_count;
	int expect_identifier;
	sqlparser_status_t status;

	start = sqlparser_sqlserver_trim_left(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (start >= end || max_parts == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, message);
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_sqlserver_scanner_init(&scanner, sql, start, end, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	part_count = 0U;
	expect_identifier = 1;
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			break;
		}
		if (expect_identifier) {
			if ((token.kind != SQLPARSER_SQLSERVER_TOKEN_WORD &&
			     token.kind != SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER) ||
			    (token.kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER &&
			     token.end - token.start <= 2U) ||
			    part_count == max_parts) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, message);
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			part_count++;
			expect_identifier = 0;
		} else {
			if (token.kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL || token.symbol != '.') {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, message);
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			expect_identifier = 1;
		}
	}
	if (part_count == 0U || expect_identifier) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, message);
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_output_token_top(const sqlparser_sqlserver_token_t *token)
{
	return token != NULL && token->paren_depth == 0U &&
	       token->block_depth == 0U && token->case_depth == 0U;
}

static int sqlparser_sqlserver_output_token_identifier_equal(
	const char *sql,
	const sqlparser_sqlserver_token_t *token,
	const char *word)
{
	size_t start;
	size_t end;
	size_t index;
	size_t word_len;

	if (sql == NULL || token == NULL || word == NULL ||
	    (token->kind != SQLPARSER_SQLSERVER_TOKEN_WORD &&
	     token->kind != SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER)) {
		return 0;
	}
	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_WORD) {
		return sqlparser_sqlserver_token_word_equal(sql, token, word);
	}
	start = token->start + 1U;
	end = token->end > start ? token->end - 1U : start;
	word_len = strlen(word);
	if (end - start != word_len) {
		return 0;
	}
	for (index = 0U; index < word_len; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_sqlserver_output_identifier_token_next(
	const char *sql,
	const sqlparser_sqlserver_token_t *token,
	size_t *io_pos,
	unsigned char *out_char)
{
	size_t end;
	unsigned char closing;
	size_t pos;

	pos = *io_pos;
	closing = 0U;
	end = token->end;
	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER) {
		closing = sql[token->start] == '[' ? (unsigned char)']' : (unsigned char)'"';
		end--;
	}
	if (pos >= end) {
		return 0;
	}
	*out_char = (unsigned char)sql[pos++];
	if (closing != 0U && *out_char == closing && pos < end &&
	    (unsigned char)sql[pos] == closing) {
		pos++;
	}
	*io_pos = pos;
	return 1;
}

static int sqlparser_sqlserver_output_identifier_tokens_equal(
	const char *sql,
	const sqlparser_sqlserver_token_t *left,
	const sqlparser_sqlserver_token_t *right)
{
	size_t left_pos;
	size_t right_pos;
	unsigned char left_char;
	unsigned char right_char;
	int has_left;
	int has_right;

	if (left == NULL || right == NULL ||
	    (left->kind != SQLPARSER_SQLSERVER_TOKEN_WORD &&
	     left->kind != SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER) ||
	    (right->kind != SQLPARSER_SQLSERVER_TOKEN_WORD &&
	     right->kind != SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER)) {
		return 0;
	}
	left_pos = left->start +
		(left->kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER ? 1U : 0U);
	right_pos = right->start +
		(right->kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER ? 1U : 0U);
	for (;;) {
		has_left = sqlparser_sqlserver_output_identifier_token_next(
			sql, left, &left_pos, &left_char);
		has_right = sqlparser_sqlserver_output_identifier_token_next(
			sql, right, &right_pos, &right_char);
		if (!has_left || !has_right) {
			return has_left == has_right;
		}
		if (tolower(left_char) != tolower(right_char)) {
			return 0;
		}
	}
}

static sqlparser_graph_dml_kind_t sqlparser_sqlserver_output_dml_kind(
	const char *sql,
	const sqlparser_sqlserver_token_t *token)
{
	if (sqlparser_sqlserver_token_word_equal(sql, token, "insert")) {
		return SQLPARSER_GRAPH_DML_INSERT;
	}
	if (sqlparser_sqlserver_token_word_equal(sql, token, "update")) {
		return SQLPARSER_GRAPH_DML_UPDATE;
	}
	if (sqlparser_sqlserver_token_word_equal(sql, token, "delete")) {
		return SQLPARSER_GRAPH_DML_DELETE;
	}
	if (sqlparser_sqlserver_token_word_equal(sql, token, "merge")) {
		return SQLPARSER_GRAPH_DML_MERGE;
	}
	return 0;
}

static size_t sqlparser_sqlserver_output_find_root_dml(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens)
{
	size_t index;

	for (index = 0U; index < tokens->count; index++) {
		if (sqlparser_sqlserver_output_token_top(&tokens->items[index]) &&
		    sqlparser_sqlserver_output_dml_kind(sql, &tokens->items[index]) != 0) {
			return index;
		}
	}
	return (size_t)-1;
}

static sqlparser_status_t sqlparser_sqlserver_output_edit_add(
	sqlparser_sqlserver_output_edits_t *edits,
	size_t start,
	size_t end,
	const char *replacement,
	char *owned_replacement,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	sqlparser_sqlserver_output_edit_t *edit;

	status = sqlparser_sqlserver_output_reserve(
		(void **)&edits->items,
		&edits->capacity,
		edits->count + 1U,
		sizeof(*edits->items),
		"SQL Server rewrite edit count is too large",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(owned_replacement);
		return status;
	}
	edit = &edits->items[edits->count++];
	memset(edit, 0, sizeof(*edit));
	edit->start = start;
	edit->end = end;
	edit->replacement = owned_replacement != NULL ? owned_replacement : replacement;
	edit->replacement_len = edit->replacement != NULL ? strlen(edit->replacement) : 0U;
	edit->owned_replacement = owned_replacement;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_edit_add_origin(
	sqlparser_sqlserver_output_edits_t *edits,
	size_t edit_index,
	sqlparser_sqlserver_output_origin_kind_t kind,
	size_t output_offset,
	size_t output_length,
	size_t source_offset,
	size_t source_length,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_edit_t *edit;
	sqlparser_sqlserver_output_origin_piece_t *piece;
	sqlparser_status_t status;

	if (edits->origins == NULL || output_length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (edit_index >= edits->count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"SQL Server origin edit index is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	edit = &edits->items[edit_index];
	if (output_offset > edit->replacement_len ||
	    output_length > edit->replacement_len - output_offset ||
	    (edit->origin_piece_count > 0U &&
	     output_offset <
		     edit->origin_pieces[edit->origin_piece_count - 1U].output_offset +
			     edit->origin_pieces[edit->origin_piece_count - 1U].output_length)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"SQL Server origin edit range is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	status = sqlparser_sqlserver_output_reserve(
		(void **)&edit->origin_pieces,
		&edit->origin_piece_capacity,
		edit->origin_piece_count + 1U,
		sizeof(*edit->origin_pieces),
		"SQL Server origin edit count is too large",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	piece = &edit->origin_pieces[edit->origin_piece_count++];
	piece->kind = kind;
	piece->output_offset = output_offset;
	piece->output_length = output_length;
	piece->source_offset = source_offset;
	piece->source_length = source_length;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_output_edit_compare(const void *left, const void *right)
{
	const sqlparser_sqlserver_output_edit_t *a;
	const sqlparser_sqlserver_output_edit_t *b;

	a = (const sqlparser_sqlserver_output_edit_t *)left;
	b = (const sqlparser_sqlserver_output_edit_t *)right;
	if (a->start < b->start) {
		return -1;
	}
	if (a->start > b->start) {
		return 1;
	}
	if (a->end > b->end) {
		return -1;
	}
	if (a->end < b->end) {
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_apply_edits(
	char **io_sql,
	sqlparser_sqlserver_output_edits_t *edits,
	sqlparser_error_t *out_error)
{
	const char *sql;
	char *rewritten;
	size_t sql_len;
	size_t output_len;
	size_t source_pos;
	size_t output_pos;
	size_t index;
	sqlparser_identifier_origin_writer_t origin_writer;
	sqlparser_status_t status;

	if (edits->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	sql_len = strlen(sql);
	qsort(edits->items, edits->count, sizeof(*edits->items), sqlparser_sqlserver_output_edit_compare);
	output_len = sql_len;
	source_pos = 0U;
	for (index = 0U; index < edits->count; index++) {
		const sqlparser_sqlserver_output_edit_t *edit;
		size_t removed;

		edit = &edits->items[index];
		if (edit->start < source_pos || edit->start > edit->end || edit->end > sql_len) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "overlapping SQL Server rewrite edits");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		removed = edit->end - edit->start;
		if (output_len < removed || edit->replacement_len > SIZE_MAX - (output_len - removed)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "rewritten SQL is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		output_len = output_len - removed + edit->replacement_len;
		source_pos = edit->end;
	}
	if (output_len == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "rewritten SQL is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	rewritten = (char *)malloc(output_len + 1U);
	if (rewritten == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memset(&origin_writer, 0, sizeof(origin_writer));
	if (edits->origins != NULL) {
		status = sqlparser_identifier_origin_writer_begin(
			&origin_writer,
			edits->origins,
			sql_len,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten);
			return status;
		}
	}
	source_pos = 0U;
	output_pos = 0U;
	for (index = 0U; index < edits->count; index++) {
		const sqlparser_sqlserver_output_edit_t *edit;
		size_t copy_len;

		edit = &edits->items[index];
		copy_len = edit->start - source_pos;
		if (copy_len > 0U) {
			if (origin_writer.map != NULL) {
				status = sqlparser_identifier_origin_writer_append_input(
					&origin_writer,
					source_pos,
					copy_len,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten);
					sqlparser_identifier_origin_writer_release(
						&origin_writer);
					return status;
				}
			}
			memcpy(rewritten + output_pos, sql + source_pos, copy_len);
			output_pos += copy_len;
		}
		if (edit->replacement_len > 0U) {
			size_t piece_index;
			size_t replacement_pos;

			replacement_pos = 0U;
			for (piece_index = 0U;
			     origin_writer.map != NULL &&
			     piece_index < edit->origin_piece_count;
			     piece_index++) {
				const sqlparser_sqlserver_output_origin_piece_t *piece;

				piece = &edit->origin_pieces[piece_index];
				if (piece->output_offset > replacement_pos) {
					status = sqlparser_identifier_origin_writer_append_unknown(
						&origin_writer,
						piece->output_offset - replacement_pos,
						out_error);
					if (status != SQLPARSER_STATUS_OK) {
						free(rewritten);
						sqlparser_identifier_origin_writer_release(
							&origin_writer);
						return status;
					}
				}
				if (piece->kind ==
				    SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_INPUT) {
					status =
						sqlparser_identifier_origin_writer_append_input(
							&origin_writer,
							piece->source_offset,
							piece->source_length,
							out_error);
				} else if (piece->kind ==
					   SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_SOURCE_IDENTIFIER) {
					status =
						sqlparser_identifier_origin_writer_append_source_identifier(
							&origin_writer,
							piece->source_offset,
							piece->source_length,
							piece->output_length,
							out_error);
				} else if (piece->kind ==
					   SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_GENERATED) {
					status =
						sqlparser_identifier_origin_writer_append_generated_identifier(
							&origin_writer,
							piece->output_length,
							out_error);
				} else {
					status = sqlparser_identifier_origin_writer_append_unknown(
						&origin_writer,
						piece->output_length,
						out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten);
					sqlparser_identifier_origin_writer_release(
						&origin_writer);
					return status;
				}
				replacement_pos =
					piece->output_offset + piece->output_length;
			}
			if (origin_writer.map != NULL &&
			    replacement_pos < edit->replacement_len) {
				status = sqlparser_identifier_origin_writer_append_unknown(
					&origin_writer,
					edit->replacement_len - replacement_pos,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten);
					sqlparser_identifier_origin_writer_release(
						&origin_writer);
					return status;
				}
			}
			memcpy(rewritten + output_pos, edit->replacement, edit->replacement_len);
			output_pos += edit->replacement_len;
		}
		source_pos = edit->end;
	}
	if (source_pos < sql_len) {
		if (origin_writer.map != NULL) {
			status = sqlparser_identifier_origin_writer_append_input(
				&origin_writer,
				source_pos,
				sql_len - source_pos,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(rewritten);
				sqlparser_identifier_origin_writer_release(
					&origin_writer);
				return status;
			}
		}
		memcpy(rewritten + output_pos, sql + source_pos, sql_len - source_pos);
		output_pos += sql_len - source_pos;
	}
	rewritten[output_pos] = '\0';
	if (origin_writer.map != NULL) {
		status = sqlparser_identifier_origin_writer_commit(
			&origin_writer,
			output_pos,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten);
			sqlparser_identifier_origin_writer_release(&origin_writer);
			return status;
		}
	}
	free(*io_sql);
	*io_sql = rewritten;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_state_add_dml(
	sqlparser_sqlserver_output_state_t *state,
	sqlparser_graph_dml_kind_t kind,
	size_t statement_index,
	sqlparser_sqlserver_output_dml_t **out_dml,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	sqlparser_sqlserver_output_dml_t *dml;

	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server OUTPUT state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if ((state->max_dml_count > 0U && state->dml_count >= state->max_dml_count) ||
	    state->dml_count == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server DML count exceeds max_statement_count");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_sqlserver_output_reserve(
		(void **)&state->dmls,
		&state->dml_capacity,
		state->dml_count + 1U,
		sizeof(*state->dmls),
		"SQL Server DML count is too large",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	dml = &state->dmls[state->dml_count++];
	memset(dml, 0, sizeof(*dml));
	dml->kind = kind;
	dml->statement_index = statement_index;
	*out_dml = dml;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_dml_add_channel(
	sqlparser_sqlserver_output_dml_t *dml,
	sqlparser_graph_dml_result_kind_t kind,
	size_t target_count,
	sqlparser_sqlserver_output_channel_t **out_channel,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	sqlparser_sqlserver_output_channel_t *channel;
	size_t offset;

	if (dml->channel_count == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT channel count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_sqlserver_output_reserve(
		(void **)&dml->channels,
		&dml->channel_capacity,
		dml->channel_count + 1U,
		sizeof(*dml->channels),
		"SQL Server OUTPUT channel count is too large",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	offset = 0U;
	if (dml->channel_count > 0U) {
		const sqlparser_sqlserver_output_channel_t *previous;

		previous = &dml->channels[dml->channel_count - 1U];
		if (previous->target_count > SIZE_MAX - previous->target_offset) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT target count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		offset = previous->target_offset + previous->target_count;
	}
	channel = &dml->channels[dml->channel_count++];
	memset(channel, 0, sizeof(*channel));
	channel->kind = kind;
	channel->target_offset = offset;
	channel->target_count = target_count;
	*out_channel = channel;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_channel_add_sink_column(
	sqlparser_sqlserver_output_channel_t *channel,
	char *column,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (channel->sink_column_count == SIZE_MAX) {
		free(column);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT sink column count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_sqlserver_output_reserve(
		(void **)&channel->sink_columns,
		&channel->sink_column_capacity,
		channel->sink_column_count + 1U,
		sizeof(*channel->sink_columns),
		"SQL Server OUTPUT sink column count is too large",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(column);
		return status;
	}
	channel->sink_columns[channel->sink_column_count++] = column;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_output_ascii_span_contains(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t word_len;
	size_t pos;
	size_t index;

	if (sql == NULL || word == NULL || start > end) {
		return 0;
	}
	word_len = strlen(word);
	if (word_len == 0U || word_len > end - start) {
		return 0;
	}
	for (pos = start; pos <= end - word_len; pos++) {
		for (index = 0U; index < word_len; index++) {
			if (tolower((unsigned char)sql[pos + index]) !=
			    tolower((unsigned char)word[index])) {
				break;
			}
		}
		if (index == word_len) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_make_action_marker(
	const char *sql,
	size_t start,
	size_t end,
	char marker[SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE],
	sqlparser_error_t *out_error)
{
	unsigned long sequence;

	for (sequence = 0UL;; sequence++) {
		int length;

		length = snprintf(
			marker,
			SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE,
			"__sqlparser_dml_action_%lu__",
			sequence);
		if (length < 0 || (size_t)length >= SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server $action marker is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		if (!sqlparser_sqlserver_output_ascii_span_contains(sql, start, end, marker)) {
			return SQLPARSER_STATUS_OK;
		}
		if (sequence == ULONG_MAX) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server $action marker space is exhausted");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
	}
}

static sqlparser_status_t sqlparser_sqlserver_output_channel_set_action(
	sqlparser_sqlserver_output_channel_t *channel,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error)
{
	size_t index;
	size_t marker_len;
	sqlparser_status_t status;

	if (channel == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL Server OUTPUT channel is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	for (index = 0U; index < channel->action_count; index++) {
		if (channel->actions[index].target_index == target_index) {
			break;
		}
	}
	if (marker == NULL || marker[0] == '\0') {
		if (index < channel->action_count) {
			memmove(
				&channel->actions[index],
				&channel->actions[index + 1U],
				(channel->action_count - index - 1U) * sizeof(*channel->actions));
			channel->action_count--;
		}
		return SQLPARSER_STATUS_OK;
	}
	marker_len = strlen(marker);
	if (marker_len >= SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server $action marker is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	if (index == channel->action_count) {
		if (channel->action_count == SIZE_MAX) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server $action target count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		status = sqlparser_sqlserver_output_reserve(
			(void **)&channel->actions,
			&channel->action_capacity,
			channel->action_count + 1U,
			sizeof(*channel->actions),
			"SQL Server $action target count is too large",
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		index = channel->action_count++;
		channel->actions[index].target_index = target_index;
	}
	memcpy(channel->actions[index].marker, marker, marker_len + 1U);
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_sqlserver_output_channel_action_marker(
	const sqlparser_sqlserver_output_channel_t *channel,
	size_t target_index)
{
	size_t index;

	if (channel == NULL) {
		return NULL;
	}
	for (index = 0U; index < channel->action_count; index++) {
		if (channel->actions[index].target_index == target_index) {
			return channel->actions[index].marker;
		}
	}
	return NULL;
}

static int sqlparser_sqlserver_output_is_aggregate(
	const char *sql,
	const sqlparser_sqlserver_token_t *token)
{
	static const char *const names[] = {
		"approx_count_distinct", "avg", "checksum_agg", "count", "count_big",
		"grouping", "grouping_id", "max", "min", "stdev", "stdevp", "string_agg",
		"sum", "var", "varp"
	};
	size_t index;

	for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
		if (sqlparser_sqlserver_output_token_identifier_equal(sql, token, names[index])) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_target_sql(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t start,
	size_t end,
	sqlparser_graph_dml_kind_t dml_kind,
	sqlparser_sqlserver_output_channel_t *channel,
	size_t *out_count,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	char action_marker[SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE];
	char quoted_marker[SQLPARSER_SQLSERVER_OUTPUT_ACTION_MARKER_SIZE + 3U];
	size_t trimmed_start;
	size_t trimmed_end;
	size_t count;
	size_t action_count;
	size_t marker_len;
	size_t index;
	size_t last_top_token;
	size_t result_len;
	char *result;
	size_t source_pos;
	size_t output_pos;
	size_t target_ordinal;
	sqlparser_status_t status;

	if (channel == NULL || out_count == NULL || out_parser_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL Server OUTPUT target arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	*out_parser_sql = NULL;
	action_marker[0] = '\0';
	quoted_marker[0] = '\0';
	marker_len = 0U;
	trimmed_start = sqlparser_sqlserver_trim_left(sql, start, end);
	trimmed_end = sqlparser_sqlserver_trim_right(sql, trimmed_start, end);
	if (trimmed_start >= trimmed_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT target list must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	count = 1U;
	action_count = 0U;
	last_top_token = (size_t)-1;
	for (index = 0U; index < tokens->count; index++) {
		const sqlparser_sqlserver_token_t *token;

		token = &tokens->items[index];
		if (token->start < trimmed_start || token->end > trimmed_end) {
			continue;
		}
		if (sqlparser_sqlserver_output_token_top(token)) {
			last_top_token = index;
			if (token->kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL && token->symbol == ',') {
				if (count == SIZE_MAX) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT target count is too large");
					return SQLPARSER_STATUS_RESOURCE_LIMIT;
				}
				count++;
			}
		}
		if (sqlparser_sqlserver_token_word_equal(sql, token, "$action")) {
			if (dml_kind != SQLPARSER_GRAPH_DML_MERGE) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "$action is valid only in a SQL Server MERGE OUTPUT clause");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			action_count++;
		}
		if (sqlparser_sqlserver_token_word_equal(sql, token, "select")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "SQL Server OUTPUT does not allow subqueries");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		if (sqlparser_sqlserver_output_is_aggregate(sql, token) && index + 1U < tokens->count &&
		    tokens->items[index + 1U].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    tokens->items[index + 1U].symbol == '(' &&
		    tokens->items[index + 1U].start < trimmed_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "SQL Server OUTPUT does not allow aggregate functions");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		if (index + 1U < tokens->count &&
		    tokens->items[index + 1U].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    tokens->items[index + 1U].symbol == '.' &&
		    tokens->items[index + 1U].start < trimmed_end) {
			if (sqlparser_sqlserver_output_token_identifier_equal(sql, token, "deleted") &&
			    dml_kind == SQLPARSER_GRAPH_DML_INSERT) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "DELETED is not valid in a SQL Server INSERT OUTPUT clause");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			if (sqlparser_sqlserver_output_token_identifier_equal(sql, token, "inserted") &&
			    dml_kind == SQLPARSER_GRAPH_DML_DELETE) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "INSERTED is not valid in a SQL Server DELETE OUTPUT clause");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
		}
	}
	if (last_top_token != (size_t)-1 &&
	    tokens->items[last_top_token].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
	    tokens->items[last_top_token].symbol == ',') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT target list must not end with a comma");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	result_len = trimmed_end - trimmed_start;
	if (action_count > 0U) {
		size_t action_len;
		int quoted_len;

		status = sqlparser_sqlserver_output_make_action_marker(
			sql, trimmed_start, trimmed_end, action_marker, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		quoted_len = snprintf(quoted_marker, sizeof(quoted_marker), "\"%s\"", action_marker);
		if (quoted_len < 0 || (size_t)quoted_len >= sizeof(quoted_marker)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server $action marker is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		marker_len = (size_t)quoted_len;
		action_len = strlen("$action");
		if (marker_len < action_len ||
		    action_count > (SIZE_MAX - result_len) / (marker_len - action_len)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT target SQL is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		result_len += action_count * (marker_len - action_len);
	}
	result = (char *)malloc(result_len + 1U);
	if (result == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	source_pos = trimmed_start;
	output_pos = 0U;
	target_ordinal = 0U;
	for (index = 0U; index < tokens->count; index++) {
		const sqlparser_sqlserver_token_t *token;
		size_t copy_len;

		token = &tokens->items[index];
		if (token->start < trimmed_start || token->end > trimmed_end) {
			continue;
		}
		if (sqlparser_sqlserver_output_token_top(token) &&
		    token->kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL && token->symbol == ',') {
			target_ordinal++;
			continue;
		}
		if (!sqlparser_sqlserver_output_token_identifier_equal(sql, token, "$action")) {
			continue;
		}
		status = sqlparser_sqlserver_output_channel_set_action(
			channel, target_ordinal, action_marker, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(result);
			return status;
		}
		copy_len = token->start - source_pos;
		memcpy(result + output_pos, sql + source_pos, copy_len);
		output_pos += copy_len;
		memcpy(result + output_pos, quoted_marker, marker_len);
		output_pos += marker_len;
		source_pos = token->end;
	}
	if (source_pos < trimmed_end) {
		memcpy(result + output_pos, sql + source_pos, trimmed_end - source_pos);
		output_pos += trimmed_end - source_pos;
	}
	result[output_pos] = '\0';
	channel->target_count = count;
	*out_count = count;
	*out_parser_sql = result;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_output_boundary_word(
	const char *sql,
	const sqlparser_sqlserver_token_t *token,
	sqlparser_graph_dml_kind_t kind)
{
	if (!sqlparser_sqlserver_output_token_top(token)) {
		return 0;
	}
	switch (kind) {
		case SQLPARSER_GRAPH_DML_INSERT:
			return sqlparser_sqlserver_token_word_equal(sql, token, "values") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "select") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "default") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "exec") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "execute");
		case SQLPARSER_GRAPH_DML_UPDATE:
			return sqlparser_sqlserver_token_word_equal(sql, token, "from") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "where") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "option");
		case SQLPARSER_GRAPH_DML_DELETE:
			return sqlparser_sqlserver_token_word_equal(sql, token, "from") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "where") ||
			       sqlparser_sqlserver_token_word_equal(sql, token, "option");
		case SQLPARSER_GRAPH_DML_MERGE:
			return sqlparser_sqlserver_token_word_equal(sql, token, "option");
		default:
			return 0;
	}
}

static size_t sqlparser_sqlserver_output_find_boundary(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t output_index,
	sqlparser_graph_dml_kind_t kind)
{
	size_t index;

	for (index = output_index + 1U; index < tokens->count; index++) {
		if (sqlparser_sqlserver_output_boundary_word(sql, &tokens->items[index], kind)) {
			return index;
		}
	}
	return tokens->count;
}

static size_t sqlparser_sqlserver_output_find_word_between(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t begin,
	size_t end,
	const char *word)
{
	size_t index;

	for (index = begin; index < end; index++) {
		if (sqlparser_sqlserver_output_token_top(&tokens->items[index]) &&
		    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[index], word)) {
			return index;
		}
	}
	return (size_t)-1;
}

static int sqlparser_sqlserver_output_token_is_identifier(
	const sqlparser_sqlserver_token_t *token)
{
	return token != NULL &&
	       (token->kind == SQLPARSER_SQLSERVER_TOKEN_WORD ||
	        token->kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER);
}

static int sqlparser_sqlserver_output_delete_source_alias_keyword(
	const char *sql,
	const sqlparser_sqlserver_token_t *token)
{
	static const char *const keywords[] = {
		"cross", "full", "inner", "join", "left", "on", "option",
		"outer", "right", "where", "with"
	};
	size_t index;

	for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
		if (sqlparser_sqlserver_token_word_equal(sql, token, keywords[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_output_find_delete_alias_source(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t begin,
	size_t end,
	const sqlparser_sqlserver_token_t *target,
	size_t *out_start,
	size_t *out_end)
{
	size_t index;

	for (index = begin; index < end; index++) {
		size_t next;
		size_t alias_index;
		int source_start;

		if (!sqlparser_sqlserver_output_token_top(&tokens->items[index])) {
			continue;
		}
		source_start = index == begin;
		if (!source_start && index > begin) {
			const sqlparser_sqlserver_token_t *previous;

			previous = &tokens->items[index - 1U];
			source_start =
				(sqlparser_sqlserver_output_token_top(previous) &&
				 previous->kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
				 previous->symbol == ',') ||
				sqlparser_sqlserver_token_word_equal(sql, previous, "join") ||
				sqlparser_sqlserver_token_word_equal(sql, previous, "apply");
		}
		if (!source_start || !sqlparser_sqlserver_output_token_is_identifier(&tokens->items[index])) {
			continue;
		}

		next = index + 1U;
		while (next + 1U < end &&
		       tokens->items[next].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		       tokens->items[next].symbol == '.' &&
		       sqlparser_sqlserver_output_token_is_identifier(&tokens->items[next + 1U])) {
			next += 2U;
		}
		alias_index = (size_t)-1;
		if (next + 1U < end &&
		    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[next], "as") &&
		    sqlparser_sqlserver_output_token_is_identifier(&tokens->items[next + 1U])) {
			alias_index = next + 1U;
		} else if (next < end &&
		           sqlparser_sqlserver_output_token_is_identifier(&tokens->items[next]) &&
		           !sqlparser_sqlserver_output_delete_source_alias_keyword(sql, &tokens->items[next])) {
			alias_index = next;
		}
		if (alias_index != (size_t)-1) {
			if (sqlparser_sqlserver_output_identifier_tokens_equal(
				    sql, target, &tokens->items[alias_index])) {
				*out_start = tokens->items[index].start;
				*out_end = tokens->items[alias_index].end;
				return 1;
			}
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_prepare_delete(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t next_index,
	size_t output_index,
	sqlparser_sqlserver_output_dml_t *dml,
	sqlparser_sqlserver_output_edits_t *edits,
	sqlparser_error_t *out_error)
{
	size_t boundary_index;
	size_t target_start;
	size_t target_end;
	size_t parser_target_start;
	size_t parser_target_end;
	size_t replacement_len;
	size_t edit_index;
	char *replacement;
	sqlparser_status_t status;

	if (next_index >= output_index || output_index >= tokens->count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server DELETE target is missing");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	boundary_index = sqlparser_sqlserver_output_find_boundary(
		sql, tokens, output_index, SQLPARSER_GRAPH_DML_DELETE);
	if (boundary_index < tokens->count &&
	    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[boundary_index], "from")) {
		dml->delete_source_from = 1;
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			tokens->items[boundary_index].start,
			tokens->items[boundary_index].end,
			"USING",
			NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (sqlparser_sqlserver_token_word_equal(sql, &tokens->items[next_index], "from")) {
		return SQLPARSER_STATUS_OK;
	}

	target_start = tokens->items[next_index].start;
	target_end = sqlparser_sqlserver_trim_right(
		sql, target_start, tokens->items[output_index].start);
	status = sqlparser_sqlserver_output_validate_identifier(
		sql,
		target_start,
		target_end,
		3U,
		"SQL Server DELETE target must be a one- to three-part identifier",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	dml->delete_target_sql = sqlparser_sqlserver_output_dup_trim(
		sql, target_start, target_end, out_error);
	if (dml->delete_target_sql == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	parser_target_start = target_start;
	parser_target_end = target_end;
	if (tokens->items[next_index].end == target_end && dml->delete_source_from) {
		(void)sqlparser_sqlserver_output_find_delete_alias_source(
			sql,
			tokens,
			boundary_index + 1U,
			tokens->count,
			&tokens->items[next_index],
			&parser_target_start,
			&parser_target_end);
	}
	if (parser_target_end < parser_target_start ||
	    parser_target_end - parser_target_start > SIZE_MAX - 6U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server DELETE target is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	replacement_len = 5U + parser_target_end - parser_target_start;
	replacement = (char *)malloc(replacement_len + 1U);
	if (replacement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memcpy(replacement, "FROM ", 5U);
	memcpy(replacement + 5U, sql + parser_target_start, parser_target_end - parser_target_start);
	replacement[replacement_len] = '\0';
	edit_index = edits->count;
	status = sqlparser_sqlserver_output_edit_add(
		edits, target_start, target_end, NULL, replacement, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_edit_add_origin(
			edits,
			edit_index,
			SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_INPUT,
			5U,
			parser_target_end - parser_target_start,
			parser_target_start,
			parser_target_end - parser_target_start,
			out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_output_parse_sink(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t begin_index,
	size_t end_index,
	size_t end_pos,
	sqlparser_sqlserver_output_channel_t *channel,
	sqlparser_error_t *out_error)
{
	size_t open_index;
	size_t open_pos;
	size_t close_pos;
	size_t next_pos;
	size_t relation_end;
	size_t index;
	size_t column_start;

	if (begin_index >= end_index || begin_index >= tokens->count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT INTO requires a target");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	open_index = (size_t)-1;
	for (index = begin_index; index < end_index; index++) {
		if (tokens->items[index].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    tokens->items[index].symbol == '(' &&
		    sqlparser_sqlserver_output_token_top(&tokens->items[index])) {
			open_index = index;
			break;
		}
	}
	open_pos = open_index != (size_t)-1 ? tokens->items[open_index].start : end_pos;
	relation_end = sqlparser_sqlserver_trim_right(sql, tokens->items[begin_index].start, open_pos);
	if (sqlparser_sqlserver_output_validate_identifier(
		    sql,
		    tokens->items[begin_index].start,
		    relation_end,
		    3U,
		    "SQL Server OUTPUT INTO target must be a one- to three-part identifier",
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
	}
	channel->sink_sql = sqlparser_sqlserver_output_dup_trim(
		sql,
		tokens->items[begin_index].start,
		relation_end,
		out_error);
	if (channel->sink_sql == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	if (open_index == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_sqlserver_find_matching_paren(sql, open_pos, &close_pos, &next_pos) ||
	    close_pos >= end_pos ||
	    sqlparser_sqlserver_trim_left(sql, next_pos, end_pos) != end_pos) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT INTO column list is invalid");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	column_start = open_pos + 1U;
	for (index = open_index + 1U; index < end_index; index++) {
		char *column;

		if (tokens->items[index].start >= close_pos) {
			break;
		}
		if (tokens->items[index].kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL ||
		    tokens->items[index].symbol != ',' ||
		    tokens->items[index].paren_depth != 1U ||
		    tokens->items[index].block_depth != 0U ||
		    tokens->items[index].case_depth != 0U) {
			continue;
		}
		if (sqlparser_sqlserver_output_validate_identifier(
			    sql,
			    column_start,
			    tokens->items[index].start,
			    1U,
			    "SQL Server OUTPUT INTO column must be a single identifier",
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		column = sqlparser_sqlserver_output_dup_trim(sql, column_start, tokens->items[index].start, out_error);
		if (column == NULL) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (sqlparser_sqlserver_output_channel_add_sink_column(channel, column, out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		column_start = tokens->items[index].end;
	}
	{
		char *column;

		if (sqlparser_sqlserver_output_validate_identifier(
			    sql,
			    column_start,
			    close_pos,
			    1U,
			    "SQL Server OUTPUT INTO column must be a single identifier",
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		column = sqlparser_sqlserver_output_dup_trim(sql, column_start, close_pos, out_error);
		if (column == NULL) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		return sqlparser_sqlserver_output_channel_add_sink_column(channel, column, out_error);
	}
}

static sqlparser_status_t sqlparser_sqlserver_output_join_returning(
	char *first,
	char *second,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t first_len;
	size_t second_len;
	size_t total;
	char *sql;

	first_len = first != NULL ? strlen(first) : 0U;
	second_len = second != NULL ? strlen(second) : 0U;
	if (second_len > SIZE_MAX - 14U || first_len > SIZE_MAX - second_len - 14U) {
		free(first);
		free(second);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT target SQL is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	total = 11U + first_len + (second_len > 0U ? 2U + second_len : 0U);
	sql = (char *)malloc(total + 1U);
	if (sql == NULL) {
		free(first);
		free(second);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memcpy(sql, " RETURNING ", 11U);
	memcpy(sql + 11U, first, first_len);
	if (second_len > 0U) {
		memcpy(sql + 11U + first_len, ", ", 2U);
		memcpy(sql + 13U + first_len, second, second_len);
	}
	sql[total] = '\0';
	free(first);
	free(second);
	*out_sql = sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_add_target_origins(
	sqlparser_sqlserver_output_edits_t *edits,
	size_t edit_index,
	size_t output_offset,
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t start,
	size_t end,
	const sqlparser_sqlserver_output_channel_t *channel,
	sqlparser_error_t *out_error)
{
	const char *marker;
	size_t index;
	size_t source_pos;
	size_t output_pos;
	size_t target_ordinal;
	sqlparser_status_t status;

	if (edits->origins == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	start = sqlparser_sqlserver_trim_left(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	source_pos = start;
	output_pos = output_offset;
	target_ordinal = 0U;
	for (index = 0U; index < tokens->count; index++) {
		const sqlparser_sqlserver_token_t *token;
		size_t copy_length;
		size_t marker_length;

		token = &tokens->items[index];
		if (token->start < start || token->end > end) {
			continue;
		}
		if (sqlparser_sqlserver_output_token_top(token) &&
		    token->kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    token->symbol == ',') {
			target_ordinal++;
			continue;
		}
		if (!sqlparser_sqlserver_output_token_identifier_equal(
			    sql, token, "$action")) {
			continue;
		}
		copy_length = token->start - source_pos;
		status = sqlparser_sqlserver_output_edit_add_origin(
			edits,
			edit_index,
			SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_INPUT,
			output_pos,
			copy_length,
			source_pos,
			copy_length,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		output_pos += copy_length;
		marker = sqlparser_sqlserver_output_channel_action_marker(
			channel, target_ordinal);
		if (marker == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"SQL Server OUTPUT action origin is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		marker_length = strlen(marker) + 2U;
		status = sqlparser_sqlserver_output_edit_add_origin(
			edits,
			edit_index,
			SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_GENERATED,
			output_pos,
			marker_length,
			0U,
			0U,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		output_pos += marker_length;
		source_pos = token->end;
	}
	return sqlparser_sqlserver_output_edit_add_origin(
		edits,
		edit_index,
		SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_INPUT,
		output_pos,
		end - source_pos,
		source_pos,
		end - source_pos,
		out_error);
}

static size_t sqlparser_sqlserver_output_token_index_after(
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t index,
	size_t position)
{
	while (index < tokens->count && tokens->items[index].start < position) {
		index++;
	}
	return index;
}

static sqlparser_status_t sqlparser_sqlserver_output_parse_top(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t root_index,
	size_t *out_next_index,
	char **out_top_sql,
	size_t *out_top_start,
	size_t *out_top_end,
	sqlparser_error_t *out_error)
{
	size_t top_index;
	size_t close_pos;
	size_t next_pos;
	size_t next_index;
	size_t top_end;

	*out_next_index = root_index + 1U;
	*out_top_sql = NULL;
	*out_top_start = 0U;
	*out_top_end = 0U;
	top_index = root_index + 1U;
	if (top_index >= tokens->count ||
	    !sqlparser_sqlserver_output_token_top(&tokens->items[top_index]) ||
	    !sqlparser_sqlserver_token_word_equal(sql, &tokens->items[top_index], "top")) {
		return SQLPARSER_STATUS_OK;
	}
	if (top_index + 1U >= tokens->count ||
	    tokens->items[top_index + 1U].kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL ||
	    tokens->items[top_index + 1U].symbol != '(' ||
	    !sqlparser_sqlserver_output_token_top(&tokens->items[top_index + 1U]) ||
	    !sqlparser_sqlserver_find_matching_paren(
		    sql,
		    tokens->items[top_index + 1U].start,
		    &close_pos,
		    &next_pos)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server DML TOP requires a parenthesized expression");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	top_end = next_pos;
	next_index = sqlparser_sqlserver_output_token_index_after(tokens, top_index + 2U, next_pos);
	if (next_index < tokens->count &&
	    sqlparser_sqlserver_output_token_top(&tokens->items[next_index]) &&
	    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[next_index], "percent")) {
		top_end = tokens->items[next_index].end;
		next_index++;
	}
	*out_top_sql = sqlparser_sqlserver_output_dup_trim(
		sql,
		tokens->items[top_index].start,
		top_end,
		out_error);
	if (*out_top_sql == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_top_start = tokens->items[top_index].start;
	*out_top_end = top_end;
	*out_next_index = next_index;
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_sqlserver_output_find_output(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t root_index)
{
	size_t index;

	for (index = root_index + 1U; index < tokens->count; index++) {
		if (sqlparser_sqlserver_output_token_top(&tokens->items[index]) &&
		    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[index], "output")) {
			return index;
		}
	}
	return (size_t)-1;
}

static sqlparser_status_t sqlparser_sqlserver_output_process_clause(
	const char *sql,
	size_t statement_end,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t output_index,
	sqlparser_sqlserver_output_dml_t *dml,
	sqlparser_sqlserver_output_edits_t *edits,
	sqlparser_error_t *out_error)
{
	size_t boundary_index;
	size_t boundary_pos;
	size_t into_index;
	size_t second_output_index;
	size_t first_target_end;
	size_t code_end;
	size_t first_count;
	size_t second_count;
	size_t first_length;
	size_t edit_index;
	char *first_sql;
	char *second_sql;
	char *returning_sql;
	sqlparser_sqlserver_output_channel_t *channel;
	sqlparser_sqlserver_output_channel_t *first_channel;
	sqlparser_sqlserver_output_channel_t *second_channel;
	sqlparser_status_t status;

	code_end = tokens->count > 0U ? tokens->items[tokens->count - 1U].end : statement_end;
	boundary_index = sqlparser_sqlserver_output_find_boundary(sql, tokens, output_index, dml->kind);
	boundary_pos = boundary_index < tokens->count ?
		tokens->items[boundary_index].start :
		code_end;
	into_index = sqlparser_sqlserver_output_find_word_between(
		sql,
		tokens,
		output_index + 1U,
		boundary_index,
		"into");
	second_output_index = sqlparser_sqlserver_output_find_word_between(
		sql,
		tokens,
		output_index + 1U,
		boundary_index,
		"output");
	if (second_output_index != (size_t)-1 &&
	    (into_index == (size_t)-1 || second_output_index < into_index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server OUTPUT allows a second channel only after OUTPUT INTO");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	first_target_end = into_index != (size_t)-1 ? tokens->items[into_index].start : boundary_pos;
	first_sql = NULL;
	second_sql = NULL;
	returning_sql = NULL;
	first_count = 0U;
	second_count = 0U;
	first_length = 0U;
	first_channel = NULL;
	second_channel = NULL;
	status = sqlparser_sqlserver_output_dml_add_channel(
		dml,
		into_index != (size_t)-1 ? SQLPARSER_GRAPH_DML_RESULT_SINK : SQLPARSER_GRAPH_DML_RESULT_CLIENT,
		0U,
		&channel,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	first_channel = channel;
	status = sqlparser_sqlserver_output_target_sql(
		sql,
		tokens,
		tokens->items[output_index].end,
		first_target_end,
		dml->kind,
		channel,
		&first_count,
		&first_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (into_index != (size_t)-1) {
		size_t sink_end_index;
		size_t sink_end_pos;

		sink_end_index = second_output_index != (size_t)-1 ? second_output_index : boundary_index;
		sink_end_pos = second_output_index != (size_t)-1 ?
			tokens->items[second_output_index].start : boundary_pos;
		status = sqlparser_sqlserver_output_parse_sink(
			sql,
			tokens,
			into_index + 1U,
			sink_end_index,
			sink_end_pos,
			channel,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(first_sql);
			return status;
		}
	}
	if (second_output_index != (size_t)-1) {
		status = sqlparser_sqlserver_output_dml_add_channel(
			dml,
			SQLPARSER_GRAPH_DML_RESULT_CLIENT,
			0U,
			&channel,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(first_sql);
			return status;
		}
		second_channel = channel;
		status = sqlparser_sqlserver_output_target_sql(
			sql,
			tokens,
			tokens->items[second_output_index].end,
			boundary_pos,
			dml->kind,
			channel,
			&second_count,
			&second_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(first_sql);
			return status;
		}
	}
	first_channel = &dml->channels[0];
	if (second_output_index != (size_t)-1) {
		second_channel = &dml->channels[1];
	}
	first_length = strlen(first_sql);
	if (dml->kind == SQLPARSER_GRAPH_DML_INSERT && boundary_index < tokens->count &&
	    (sqlparser_sqlserver_token_word_equal(sql, &tokens->items[boundary_index], "exec") ||
	     sqlparser_sqlserver_token_word_equal(sql, &tokens->items[boundary_index], "execute"))) {
		free(first_sql);
		free(second_sql);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "SQL Server INSERT EXEC cannot use OUTPUT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_sqlserver_output_join_returning(first_sql, second_sql, &returning_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_sqlserver_output_edit_add(
		edits,
		tokens->items[output_index].start,
		boundary_pos,
		NULL,
		NULL,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		edit_index = edits->count;
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			code_end,
			code_end,
			NULL,
			returning_sql,
			out_error);
		returning_sql = NULL;
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_output_add_target_origins(
				edits,
				edit_index,
				11U,
				sql,
				tokens,
				tokens->items[output_index].end,
				first_target_end,
				first_channel,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK &&
		    second_output_index != (size_t)-1) {
			status = sqlparser_sqlserver_output_add_target_origins(
				edits,
				edit_index,
				13U + first_length,
				sql,
				tokens,
				tokens->items[second_output_index].end,
				boundary_pos,
				second_channel,
				out_error);
		}
	}
	free(returning_sql);
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_output_process_statement(
	const char *sql,
	size_t statement_start,
	size_t statement_end,
	size_t statement_index,
	size_t parent_dml_index,
	int has_parent,
	sqlparser_sqlserver_output_state_t **io_state,
	sqlparser_sqlserver_output_edits_t *edits,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	sqlparser_sqlserver_output_state_t *state;
	sqlparser_sqlserver_output_dml_t *dml;
	sqlparser_graph_dml_kind_t kind;
	size_t root_index;
	size_t next_index;
	size_t output_index;
	size_t top_start;
	size_t top_end;
	char *top_sql;
	int omitted_into;
	int needs_state;
	sqlparser_status_t status;

	memset(&tokens, 0, sizeof(tokens));
	status = sqlparser_sqlserver_output_tokenize(sql, statement_start, statement_end, &tokens, out_error);
	if (status != SQLPARSER_STATUS_OK || tokens.count == 0U) {
		return status;
	}
	root_index = sqlparser_sqlserver_output_find_root_dml(sql, &tokens);
	if (root_index == (size_t)-1) {
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		return SQLPARSER_STATUS_OK;
	}
	kind = sqlparser_sqlserver_output_dml_kind(sql, &tokens.items[root_index]);
	output_index = sqlparser_sqlserver_output_find_output(sql, &tokens, root_index);
	top_sql = NULL;
	top_start = 0U;
	top_end = 0U;
	next_index = root_index + 1U;
	if (output_index != (size_t)-1) {
		status = sqlparser_sqlserver_output_parse_top(
			sql,
			&tokens,
			root_index,
			&next_index,
			&top_sql,
			&top_start,
			&top_end,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_output_tokens_clear(&tokens);
			return status;
		}
	}
	omitted_into = (kind == SQLPARSER_GRAPH_DML_INSERT ||
		kind == SQLPARSER_GRAPH_DML_MERGE) &&
		(next_index >= tokens.count ||
		 !sqlparser_sqlserver_token_word_equal(sql, &tokens.items[next_index], "into"));
	needs_state = output_index != (size_t)-1 || omitted_into || top_sql != NULL;
	if (!needs_state) {
		free(top_sql);
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		return SQLPARSER_STATUS_OK;
	}
	state = *io_state;
	if (state == NULL) {
		free(top_sql);
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server OUTPUT state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	dml = NULL;
	if (!has_parent) {
		size_t index;

		for (index = 0U; index < state->dml_count; index++) {
			if (state->dmls[index].statement_index == statement_index &&
			    !state->dmls[index].has_parent) {
				dml = &state->dmls[index];
				break;
			}
		}
	}
	if (dml == NULL) {
		status = sqlparser_sqlserver_output_state_add_dml(state, kind, statement_index, &dml, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(top_sql);
			sqlparser_sqlserver_output_tokens_clear(&tokens);
			return status;
		}
		dml->parent_dml_index = parent_dml_index;
		dml->has_parent = has_parent;
	} else if (dml->kind != kind || dml->channel_count != 0U || dml->top_sql != NULL) {
		free(top_sql);
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server DML metadata is inconsistent");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	dml->top_sql = top_sql;
	dml->omitted_into = omitted_into;
	if (status == SQLPARSER_STATUS_OK && output_index != (size_t)-1 &&
	    kind == SQLPARSER_GRAPH_DML_DELETE) {
		status = sqlparser_sqlserver_output_prepare_delete(
			sql,
			&tokens,
			next_index,
			output_index,
			dml,
			edits,
			out_error);
	}
	if (top_sql != NULL) {
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_output_edit_add(edits, top_start, top_end, NULL, NULL, out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK && omitted_into) {
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			tokens.items[root_index].end,
			tokens.items[root_index].end,
			" INTO",
			NULL,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && output_index != (size_t)-1) {
		status = sqlparser_sqlserver_output_process_clause(
			sql,
			statement_end,
			&tokens,
			output_index,
			dml,
			edits,
			out_error);
	}
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return status;
}

static void sqlparser_sqlserver_output_nested_clear(
	sqlparser_sqlserver_output_nested_t *items,
	size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		free(items[index].name);
		free(items[index].parser_sql);
		sqlparser_identifier_origin_map_destroy(items[index].origins);
	}
	free(items);
}

static int sqlparser_sqlserver_output_nested_context(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t open_index)
{
	const sqlparser_sqlserver_token_t *previous;

	if (open_index == 0U) {
		return 0;
	}
	previous = &tokens->items[open_index - 1U];
	if (previous->paren_depth != 0U || previous->block_depth != 0U ||
	    previous->case_depth != 0U) {
		return 0;
	}
	return sqlparser_sqlserver_token_word_equal(sql, previous, "from") ||
	       sqlparser_sqlserver_token_word_equal(sql, previous, "join") ||
	       sqlparser_sqlserver_token_word_equal(sql, previous, "apply") ||
	       (previous->kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL && previous->symbol == ',');
}

static size_t sqlparser_sqlserver_output_nested_close_index(
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t open_index)
{
	size_t index;

	for (index = open_index + 1U; index < tokens->count; index++) {
		if (tokens->items[index].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    tokens->items[index].symbol == ')' &&
		    tokens->items[index].paren_depth == 1U &&
		    tokens->items[index].block_depth == 0U &&
		    tokens->items[index].case_depth == 0U) {
			return index;
		}
	}
	return (size_t)-1;
}

static int sqlparser_sqlserver_output_nested_has_alias(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t close_index)
{
	size_t index;

	index = close_index + 1U;
	if (index < tokens->count &&
	    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[index], "as")) {
		index++;
	}
	return index < tokens->count &&
	       tokens->items[index].paren_depth == 0U &&
	       (tokens->items[index].kind == SQLPARSER_SQLSERVER_TOKEN_WORD ||
		tokens->items[index].kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER);
}

static int sqlparser_sqlserver_output_nested_has_output(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t begin,
	size_t end)
{
	size_t index;

	for (index = begin; index < end; index++) {
		if (tokens->items[index].paren_depth == 1U &&
		    tokens->items[index].block_depth == 0U &&
		    tokens->items[index].case_depth == 0U &&
		    sqlparser_sqlserver_token_word_equal(sql, &tokens->items[index], "output")) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_nested_name(
	const char *sql,
	size_t *io_sequence,
	char **out_name,
	sqlparser_error_t *out_error)
{
	char buffer[64];
	int length;

	*out_name = NULL;
	for (;;) {
		length = snprintf(
			buffer,
			sizeof(buffer),
			"__sqlparser_dml_source_%lu__",
			(unsigned long)*io_sequence);
		if (length < 0 || (size_t)length >= sizeof(buffer)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server DML source name is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		if (*io_sequence == SIZE_MAX) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server DML source count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		(*io_sequence)++;
		if (!sqlparser_sqlserver_output_ascii_contains(sql, buffer)) {
			break;
		}
	}
	*out_name = sqlparser_strdup(buffer);
	if (*out_name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_nested_parser_sql(
	const char *sql,
	size_t start,
	size_t end,
	size_t statement_index,
	sqlparser_sqlserver_output_state_t **io_state,
	char **out_sql,
	sqlparser_identifier_origin_map_t **out_origins,
	int capture_origins,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_edits_t edits;
	char *parser_sql;
	size_t before_count;
	sqlparser_status_t status;

	*out_sql = NULL;
	*out_origins = NULL;
	parser_sql = sqlparser_strndup(sql + start, end - start);
	if (parser_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memset(&edits, 0, sizeof(edits));
	if (capture_origins) {
		status = sqlparser_identifier_origin_map_new_identity(
			end - start,
			out_origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(parser_sql);
			return status;
		}
		edits.origins = *out_origins;
	}
	before_count = *io_state != NULL ? (*io_state)->dml_count : 0U;
	status = sqlparser_sqlserver_output_process_statement(
		parser_sql,
		0U,
		strlen(parser_sql),
		statement_index,
		0U,
		1,
		io_state,
		&edits,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_apply_edits(&parser_sql, &edits, out_error);
	}
	sqlparser_sqlserver_output_edits_clear(&edits);
	if (status != SQLPARSER_STATUS_OK) {
		free(parser_sql);
		sqlparser_identifier_origin_map_destroy(*out_origins);
		*out_origins = NULL;
		return status;
	}
	if (*io_state == NULL || (*io_state)->dml_count != before_count + 1U ||
	    (*io_state)->dmls[before_count].channel_count != 1U ||
	    (*io_state)->dmls[before_count].channels[0].kind != SQLPARSER_GRAPH_DML_RESULT_CLIENT) {
		free(parser_sql);
		sqlparser_identifier_origin_map_destroy(*out_origins);
		*out_origins = NULL;
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "nested SQL Server DML requires one client OUTPUT channel");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	*out_sql = parser_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_build_nested_prefix(
	const sqlparser_sqlserver_output_nested_t *items,
	size_t count,
	int has_with,
	char **out_prefix,
	sqlparser_error_t *out_error)
{
	size_t length;
	size_t offset;
	size_t index;
	char *prefix;

	length = has_with ? 2U : 6U;
	for (index = 0U; index < count; index++) {
		size_t name_len = strlen(items[index].name);
		size_t sql_len = strlen(items[index].parser_sql);

		if (name_len > SIZE_MAX - sql_len - 7U ||
		    length > SIZE_MAX - name_len - sql_len - 7U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "nested SQL Server DML SQL is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		length += name_len + sql_len + 6U;
		if (index + 1U < count) {
			length += 2U;
		}
	}
	prefix = (char *)malloc(length + 1U);
	if (prefix == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	offset = 0U;
	if (has_with) {
		prefix[offset++] = ' ';
	} else {
		memcpy(prefix + offset, "WITH ", 5U);
		offset += 5U;
	}
	for (index = 0U; index < count; index++) {
		size_t name_len;
		size_t sql_len;

		if (index > 0U) {
			memcpy(prefix + offset, ", ", 2U);
			offset += 2U;
		}
		name_len = strlen(items[index].name);
		sql_len = strlen(items[index].parser_sql);
		memcpy(prefix + offset, items[index].name, name_len);
		offset += name_len;
		memcpy(prefix + offset, " AS (", 5U);
		offset += 5U;
		memcpy(prefix + offset, items[index].parser_sql, sql_len);
		offset += sql_len;
		prefix[offset++] = ')';
	}
	prefix[offset++] = has_with ? ',' : ' ';
	prefix[offset] = '\0';
	*out_prefix = prefix;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_add_mapped_origins(
	sqlparser_sqlserver_output_edits_t *edits,
	size_t edit_index,
	size_t output_offset,
	const char *sql,
	const sqlparser_identifier_origin_map_t *origins,
	size_t source_offset,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_identifier_origin_t origin;
	sqlparser_identifier_origin_kind_t kind;
	sqlparser_status_t status;
	size_t sql_length;

	if (edits->origins == NULL || origins == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	sql_length = strlen(sql);
	status = sqlparser_sqlserver_scanner_init(
		&scanner, sql, 0U, sql_length, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(
			&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			break;
		}
		if (token.kind != SQLPARSER_SQLSERVER_TOKEN_WORD &&
		    token.kind != SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER) {
			continue;
		}
		kind = sqlparser_identifier_origin_map_lookup(
			origins,
			token.start,
			token.end - token.start,
			&origin);
		if (kind == SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN) {
			continue;
		}
		if (kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE &&
		    origin.source_offset > SIZE_MAX - source_offset) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"SQL Server nested origin offset is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		status = sqlparser_sqlserver_output_edit_add_origin(
			edits,
			edit_index,
			kind == SQLPARSER_IDENTIFIER_ORIGIN_GENERATED ?
				SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_GENERATED :
				SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_SOURCE_IDENTIFIER,
			output_offset + token.start,
			token.end - token.start,
			kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ?
				source_offset + origin.source_offset :
				0U,
			kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ?
				origin.source_length :
				0U,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_process_nested_statement(
	const char *sql,
	size_t statement_start,
	size_t statement_end,
	size_t statement_index,
	size_t *io_sequence,
	sqlparser_sqlserver_output_state_t **io_state,
	sqlparser_sqlserver_output_edits_t *edits,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	sqlparser_sqlserver_output_nested_t *items;
	size_t item_count;
	size_t initialized_count;
	size_t item_capacity;
	size_t root_index;
	size_t index;
	int has_with;
	sqlparser_status_t status;

	memset(&tokens, 0, sizeof(tokens));
	items = NULL;
	item_count = 0U;
	initialized_count = 0U;
	item_capacity = 0U;
	status = sqlparser_sqlserver_output_tokenize(sql, statement_start, statement_end, &tokens, out_error);
	if (status != SQLPARSER_STATUS_OK || tokens.count == 0U) {
		return status;
	}
	root_index = sqlparser_sqlserver_output_find_root_dml(sql, &tokens);
	if (root_index == (size_t)-1 ||
	    sqlparser_sqlserver_output_dml_kind(sql, &tokens.items[root_index]) != SQLPARSER_GRAPH_DML_INSERT) {
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		return SQLPARSER_STATUS_OK;
	}
	has_with = sqlparser_sqlserver_token_word_equal(sql, &tokens.items[0], "with");
	for (index = 0U; index + 1U < tokens.count; index++) {
		size_t close_index;
		sqlparser_graph_dml_kind_t inner_kind;
		sqlparser_sqlserver_output_nested_t *item;
		size_t dml_global_index;

		if (tokens.items[index].kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL ||
		    tokens.items[index].symbol != '(' ||
		    tokens.items[index].paren_depth != 0U ||
		    !sqlparser_sqlserver_output_nested_context(sql, &tokens, index)) {
			continue;
		}
		inner_kind = sqlparser_sqlserver_output_dml_kind(sql, &tokens.items[index + 1U]);
		if ((int)inner_kind == 0 || tokens.items[index + 1U].paren_depth != 1U) {
			continue;
		}
		close_index = sqlparser_sqlserver_output_nested_close_index(&tokens, index);
		if (close_index == (size_t)-1 ||
		    !sqlparser_sqlserver_output_nested_has_output(sql, &tokens, index + 1U, close_index)) {
			continue;
		}
		if (!sqlparser_sqlserver_output_nested_has_alias(sql, &tokens, close_index)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "nested SQL Server DML source requires an alias");
			status = SQLPARSER_STATUS_PARSE_ERROR;
			goto done;
		}
		if (*io_state == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server OUTPUT state is missing");
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			goto done;
		}
		if (item_count == 0U) {
			sqlparser_sqlserver_output_dml_t *outer;

			status = sqlparser_sqlserver_output_state_add_dml(
				*io_state, SQLPARSER_GRAPH_DML_INSERT, statement_index, &outer, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				goto done;
			}
		}
		status = sqlparser_sqlserver_output_reserve(
			(void **)&items,
			&item_capacity,
			item_count + 1U,
			sizeof(*items),
			"nested SQL Server DML source count is too large",
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		item = &items[item_count];
		memset(item, 0, sizeof(*item));
		initialized_count = item_count + 1U;
		item->start = tokens.items[index].start;
		item->end = tokens.items[close_index].end;
		status = sqlparser_sqlserver_output_nested_name(sql, io_sequence, &item->name, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		dml_global_index = (*io_state)->dml_count;
		status = sqlparser_sqlserver_output_nested_parser_sql(
			sql,
			tokens.items[index + 1U].start,
			tokens.items[close_index].start,
			statement_index,
			io_state,
			&item->parser_sql,
			&item->origins,
			edits->origins != NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		item->source_offset = tokens.items[index + 1U].start;
		(*io_state)->dmls[dml_global_index].source_name = sqlparser_strdup(item->name);
		(*io_state)->dmls[dml_global_index].source_sql = sqlparser_strndup(
			sql + tokens.items[index + 1U].start,
			tokens.items[close_index].start -
				tokens.items[index + 1U].start);
		if ((*io_state)->dmls[dml_global_index].source_name == NULL ||
		    (*io_state)->dmls[dml_global_index].source_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			status = SQLPARSER_STATUS_NO_MEMORY;
			goto done;
		}
		{
			char *replacement;
			size_t edit_index;

			replacement = sqlparser_strdup(item->name);
			if (replacement == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				status = SQLPARSER_STATUS_NO_MEMORY;
				goto done;
			}
			edit_index = edits->count;
			status = sqlparser_sqlserver_output_edit_add(
				edits,
				item->start,
				item->end,
				NULL,
				replacement,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_output_edit_add_origin(
					edits,
					edit_index,
					SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_GENERATED,
					0U,
					strlen(item->name),
					0U,
					0U,
					out_error);
			}
		}
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		item_count++;
		index = close_index;
	}
	if (item_count > 0U) {
		char *prefix;
		size_t edit_index;
		size_t insertion_pos;
		size_t origin_offset;
		size_t item_index;

		prefix = NULL;
		status = sqlparser_sqlserver_output_build_nested_prefix(
			items, item_count, has_with, &prefix, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		insertion_pos = has_with ? tokens.items[0].end : statement_start;
		edit_index = edits->count;
		status = sqlparser_sqlserver_output_edit_add(
			edits, insertion_pos, insertion_pos, NULL, prefix, out_error);
		origin_offset = has_with ? 1U : 5U;
		for (item_index = 0U;
		     status == SQLPARSER_STATUS_OK && item_index < item_count;
		     item_index++) {
			size_t name_length;
			size_t parser_length;

			if (item_index > 0U) {
				origin_offset += 2U;
			}
			name_length = strlen(items[item_index].name);
			parser_length = strlen(items[item_index].parser_sql);
			status = sqlparser_sqlserver_output_edit_add_origin(
				edits,
				edit_index,
				SQLPARSER_SQLSERVER_OUTPUT_ORIGIN_GENERATED,
				origin_offset,
				name_length,
				0U,
				0U,
				out_error);
			origin_offset += name_length + 5U;
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_output_add_mapped_origins(
					edits,
					edit_index,
					origin_offset,
					items[item_index].parser_sql,
					items[item_index].origins,
					items[item_index].source_offset,
					out_error);
			}
			origin_offset += parser_length + 1U;
		}
	}

done:
	sqlparser_sqlserver_output_nested_clear(items, initialized_count);
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_output_preprocess_nested(
	char **io_sql,
	sqlparser_sqlserver_output_state_t **io_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	const char *sql;
	size_t sql_len;
	size_t start;
	size_t statement_index;
	size_t sequence;
	sqlparser_sqlserver_output_edits_t edits;
	sqlparser_status_t status;

	memset(&edits, 0, sizeof(edits));
	edits.origins = origins;
	sql = *io_sql;
	sql_len = strlen(sql);
	start = 0U;
	statement_index = 0U;
	sequence = 0U;
	status = SQLPARSER_STATUS_OK;
	while (start < sql_len) {
		size_t end;
		size_t content_start;
		size_t content_end;

		while (start < sql_len && (isspace((unsigned char)sql[start]) || sql[start] == ';')) {
			start++;
		}
		if (start >= sql_len) {
			break;
		}
		end = sqlparser_sqlserver_statement_end(sql, start, sql_len);
		content_start = sqlparser_sqlserver_trim_left(sql, start, end);
		content_end = sqlparser_sqlserver_trim_right(sql, content_start, end);
		if (content_start < content_end) {
			status = sqlparser_sqlserver_output_process_nested_statement(
				sql,
				content_start,
				content_end,
				statement_index,
				&sequence,
				io_state,
				&edits,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
			statement_index++;
		}
		start = end < sql_len && sql[end] == ';' ? end + 1U : end;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_apply_edits(io_sql, &edits, out_error);
	}
	sqlparser_sqlserver_output_edits_clear(&edits);
	return status;
}

static int sqlparser_sqlserver_output_ascii_prefix_equal(const char *sql, const char *word)
{
	size_t index;

	if (sql == NULL || word == NULL) {
		return 0;
	}
	for (index = 0U; word[index] != '\0'; index++) {
		if (sql[index] == '\0' ||
		    tolower((unsigned char)sql[index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return index > 0U;
}

static int sqlparser_sqlserver_output_ascii_contains(const char *sql, const char *word)
{
	size_t pos;

	if (sql == NULL || word == NULL || word[0] == '\0') {
		return 0;
	}
	for (pos = 0U; sql[pos] != '\0'; pos++) {
		if (sqlparser_sqlserver_output_ascii_prefix_equal(sql + pos, word)) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_preprocess_internal(
	char **io_sql,
	const sqlparser_limits_t *limits,
	unsigned int candidates,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	const char *sql;
	size_t sql_len;
	size_t start;
	size_t statement_index;
	sqlparser_sqlserver_output_edits_t edits;
	sqlparser_sqlserver_output_state_t *state;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || limits == NULL || out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL Server OUTPUT preprocess arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	candidates &= SQLPARSER_SQLSERVER_CANDIDATE_INSERT |
		SQLPARSER_SQLSERVER_CANDIDATE_MERGE |
		SQLPARSER_SQLSERVER_CANDIDATE_OUTPUT;
	if (candidates == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&edits, 0, sizeof(edits));
	edits.origins = origins;
	state = (sqlparser_sqlserver_output_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->max_dml_count = limits->max_statement_count;
	status = SQLPARSER_STATUS_OK;
	if ((candidates & SQLPARSER_SQLSERVER_CANDIDATE_OUTPUT) != 0U) {
		status = sqlparser_sqlserver_output_preprocess_nested(
			io_sql, &state, origins, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_output_destroy(state);
		return status;
	}
	sql = *io_sql;
	sql_len = strlen(sql);
	start = 0U;
	statement_index = 0U;
	status = SQLPARSER_STATUS_OK;
	edits.origins = origins;
	while (start < sql_len) {
		size_t end;
		size_t content_start;
		size_t content_end;

		while (start < sql_len && (isspace((unsigned char)sql[start]) || sql[start] == ';')) {
			start++;
		}
		if (start >= sql_len) {
			break;
		}
		end = sqlparser_sqlserver_statement_end(sql, start, sql_len);
		content_start = sqlparser_sqlserver_trim_left(sql, start, end);
		content_end = sqlparser_sqlserver_trim_right(sql, content_start, end);
		if (content_start < content_end) {
			status = sqlparser_sqlserver_output_process_statement(
				sql,
				content_start,
				content_end,
				statement_index,
				0U,
				0,
				&state,
				&edits,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
			statement_index++;
		}
		start = end < sql_len && sql[end] == ';' ? end + 1U : end;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_apply_edits(io_sql, &edits, out_error);
	}
	sqlparser_sqlserver_output_edits_clear(&edits);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_output_destroy(state);
		return status;
	}
	if (state != NULL && state->dml_count == 0U) {
		sqlparser_sqlserver_output_destroy(state);
		state = NULL;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_output_preprocess(
	char **io_sql,
	const sqlparser_limits_t *limits,
	unsigned int candidates,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_output_preprocess_internal(
		io_sql,
		limits,
		candidates,
		out_state,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_sqlserver_output_preprocess_identifier_origins(
	char **io_sql,
	const sqlparser_limits_t *limits,
	unsigned int candidates,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	if (origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server OUTPUT origin map must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_sqlserver_output_preprocess_internal(
		io_sql,
		limits,
		candidates,
		out_state,
		origins,
		out_error);
}

sqlparser_status_t sqlparser_sqlserver_output_clone(
	const sqlparser_sqlserver_output_state_t *state,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *clone;
	size_t dml_index;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	clone = (sqlparser_sqlserver_output_state_t *)calloc(1U, sizeof(*clone));
	if (clone == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	clone->max_dml_count = state->max_dml_count;
	status = SQLPARSER_STATUS_OK;
	for (dml_index = 0U; dml_index < state->dml_count && status == SQLPARSER_STATUS_OK; dml_index++) {
		const sqlparser_sqlserver_output_dml_t *source_dml;
		sqlparser_sqlserver_output_dml_t *dest_dml;
		size_t channel_index;

		source_dml = &state->dmls[dml_index];
		status = sqlparser_sqlserver_output_state_add_dml(
			clone,
			source_dml->kind,
			source_dml->statement_index,
			&dest_dml,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		dest_dml->omitted_into = source_dml->omitted_into;
		dest_dml->delete_source_from = source_dml->delete_source_from;
		dest_dml->parent_dml_index = source_dml->parent_dml_index;
		dest_dml->has_parent = source_dml->has_parent;
		if (source_dml->source_name != NULL) {
			dest_dml->source_name = sqlparser_strdup(source_dml->source_name);
			if (dest_dml->source_name == NULL) {
				sqlparser_sqlserver_output_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
		}
		if (source_dml->source_sql != NULL) {
			dest_dml->source_sql = sqlparser_strdup(source_dml->source_sql);
			if (dest_dml->source_sql == NULL) {
				sqlparser_sqlserver_output_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
		}
		if (source_dml->delete_target_sql != NULL) {
			dest_dml->delete_target_sql = sqlparser_strdup(source_dml->delete_target_sql);
			if (dest_dml->delete_target_sql == NULL) {
				status = SQLPARSER_STATUS_NO_MEMORY;
				sqlparser_error_set_message(out_error, status, "out of memory");
				break;
			}
		}
		if (source_dml->top_sql != NULL) {
			dest_dml->top_sql = sqlparser_strdup(source_dml->top_sql);
			if (dest_dml->top_sql == NULL) {
				status = SQLPARSER_STATUS_NO_MEMORY;
				sqlparser_error_set_message(out_error, status, "out of memory");
				break;
			}
		}
		for (channel_index = 0U; channel_index < source_dml->channel_count; channel_index++) {
			const sqlparser_sqlserver_output_channel_t *source_channel;
			sqlparser_sqlserver_output_channel_t *dest_channel;
			size_t column_index;
			size_t action_index;

			source_channel = &source_dml->channels[channel_index];
			status = sqlparser_sqlserver_output_dml_add_channel(
				dest_dml,
				source_channel->kind,
				source_channel->target_count,
				&dest_channel,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
			if (source_channel->sink_sql != NULL) {
				dest_channel->sink_sql = sqlparser_strdup(source_channel->sink_sql);
				if (dest_channel->sink_sql == NULL) {
					status = SQLPARSER_STATUS_NO_MEMORY;
					sqlparser_error_set_message(out_error, status, "out of memory");
					break;
				}
			}
			for (column_index = 0U; column_index < source_channel->sink_column_count; column_index++) {
				char *column;

				column = sqlparser_strdup(source_channel->sink_columns[column_index]);
				if (column == NULL) {
					status = SQLPARSER_STATUS_NO_MEMORY;
					sqlparser_error_set_message(out_error, status, "out of memory");
					break;
				}
				status = sqlparser_sqlserver_output_channel_add_sink_column(dest_channel, column, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					break;
				}
			}
			for (action_index = 0U;
			     action_index < source_channel->action_count && status == SQLPARSER_STATUS_OK;
			     action_index++) {
				status = sqlparser_sqlserver_output_channel_set_action(
					dest_channel,
					source_channel->actions[action_index].target_index,
					source_channel->actions[action_index].marker,
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_output_destroy(clone);
		return status;
	}
	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

size_t sqlparser_sqlserver_output_dml_count(const sqlparser_sqlserver_output_state_t *state)
{
	return state != NULL ? state->dml_count : 0U;
}

int sqlparser_sqlserver_output_dml_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	sqlparser_sqlserver_output_dml_view_t *out_dml)
{
	const sqlparser_sqlserver_output_dml_t *dml;

	if (out_dml == NULL || state == NULL || dml_index >= state->dml_count) {
		return 0;
	}
	dml = &state->dmls[dml_index];
	memset(out_dml, 0, sizeof(*out_dml));
	out_dml->kind = dml->kind;
	out_dml->statement_index = dml->statement_index;
	out_dml->channel_count = dml->channel_count;
	out_dml->parent_dml_index = dml->parent_dml_index;
	out_dml->has_parent = dml->has_parent;
	out_dml->source_name = dml->source_name;
	out_dml->source_sql = dml->source_sql;
	out_dml->has_duplicate_target_relation =
		dml->delete_target_sql != NULL && dml->delete_source_from;
	return 1;
}

int sqlparser_sqlserver_output_channel_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	sqlparser_sqlserver_output_channel_view_t *out_channel)
{
	const sqlparser_sqlserver_output_channel_t *channel;

	if (out_channel == NULL || state == NULL || dml_index >= state->dml_count ||
	    channel_index >= state->dmls[dml_index].channel_count) {
		return 0;
	}
	channel = &state->dmls[dml_index].channels[channel_index];
	memset(out_channel, 0, sizeof(*out_channel));
	out_channel->kind = channel->kind;
	out_channel->target_offset = channel->target_offset;
	out_channel->target_count = channel->target_count;
	out_channel->sink_sql = channel->sink_sql;
	out_channel->sink_column_count = channel->sink_column_count;
	return 1;
}

const char *sqlparser_sqlserver_output_sink_column_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index)
{
	if (state == NULL || dml_index >= state->dml_count ||
	    channel_index >= state->dmls[dml_index].channel_count ||
	    column_index >= state->dmls[dml_index].channels[channel_index].sink_column_count) {
		return NULL;
	}
	return state->dmls[dml_index].channels[channel_index].sink_columns[column_index];
}

const char *sqlparser_sqlserver_output_action_marker_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t target_index)
{
	if (state == NULL || dml_index >= state->dml_count ||
	    channel_index >= state->dmls[dml_index].channel_count) {
		return NULL;
	}
	return sqlparser_sqlserver_output_channel_action_marker(
		&state->dmls[dml_index].channels[channel_index], target_index);
}

static sqlparser_sqlserver_output_channel_t *sqlparser_sqlserver_output_mutable_sink_channel(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_channel_t *channel;

	if (state == NULL || dml_index >= state->dml_count ||
	    channel_index >= state->dmls[dml_index].channel_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result channel index is out of range");
		return NULL;
	}
	channel = &state->dmls[dml_index].channels[channel_index];
	if (channel->kind != SQLPARSER_GRAPH_DML_RESULT_SINK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result channel has no sink");
		return NULL;
	}
	return channel;
}

sqlparser_status_t sqlparser_sqlserver_output_adjust_target_count(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_dml_t *dml;
	sqlparser_sqlserver_output_channel_t *channel;
	size_t index;

	if (state == NULL || dml_index >= state->dml_count ||
	    channel_index >= state->dmls[dml_index].channel_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result channel index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	dml = &state->dmls[dml_index];
	channel = &dml->channels[channel_index];
	if (delta != -1 && delta != 1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target count delta is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (delta < 0 && target_index >= channel->target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (delta < 0 && channel->target_count == 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "cannot delete the last DML result target");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (delta > 0 && target_index > channel->target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target insertion index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (delta > 0 && channel->target_count == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "DML result target count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	if (delta > 0 && dml->channel_count > 0U) {
		const sqlparser_sqlserver_output_channel_t *last;

		last = &dml->channels[dml->channel_count - 1U];
		if (last->target_count > SIZE_MAX - last->target_offset ||
		    last->target_offset + last->target_count == SIZE_MAX) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "DML result target count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
	}
	for (index = 0U; index < channel->action_count;) {
		if (delta < 0 && channel->actions[index].target_index == target_index) {
			memmove(
				&channel->actions[index],
				&channel->actions[index + 1U],
				(channel->action_count - index - 1U) * sizeof(*channel->actions));
			channel->action_count--;
			continue;
		}
		if ((delta > 0 && channel->actions[index].target_index >= target_index) ||
		    (delta < 0 && channel->actions[index].target_index > target_index)) {
			channel->actions[index].target_index = delta > 0 ?
				channel->actions[index].target_index + 1U :
				channel->actions[index].target_index - 1U;
		}
		index++;
	}
	channel->target_count = delta < 0 ? channel->target_count - 1U : channel->target_count + 1U;
	for (index = channel_index + 1U; index < dml->channel_count; index++) {
		dml->channels[index].target_offset = delta < 0 ?
			dml->channels[index].target_offset - 1U :
			dml->channels[index].target_offset + 1U;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_output_set_action_marker(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error)
{
	if (state == NULL || dml_index >= state->dml_count ||
	    channel_index >= state->dmls[dml_index].channel_count ||
	    target_index >= state->dmls[dml_index].channels[channel_index].target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_sqlserver_output_channel_set_action(
		&state->dmls[dml_index].channels[channel_index], target_index, marker, out_error);
}

sqlparser_status_t sqlparser_sqlserver_output_set_sink_sql(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_channel_t *channel;
	char *copy;
	sqlparser_status_t status;

	if (sink_sql == NULL || sink_sql[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink SQL must not be empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	channel = sqlparser_sqlserver_output_mutable_sink_channel(state, dml_index, channel_index, out_error);
	if (channel == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_sqlserver_output_validate_identifier(
		sink_sql,
		0U,
		strlen(sink_sql),
		3U,
		"SQL Server OUTPUT INTO target must be a one- to three-part identifier",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	copy = sqlparser_sqlserver_output_dup_trim(sink_sql, 0U, strlen(sink_sql), out_error);
	if (copy == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	free(channel->sink_sql);
	channel->sink_sql = copy;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_output_set_sink_column(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_channel_t *channel;
	char *copy;
	sqlparser_status_t status;

	channel = sqlparser_sqlserver_output_mutable_sink_channel(state, dml_index, channel_index, out_error);
	if (channel == NULL || column_index >= channel->sink_column_count ||
	    column_sql == NULL || column_sql[0] == '\0') {
		if (channel != NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column is invalid");
		}
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_sqlserver_output_validate_identifier(
		column_sql,
		0U,
		strlen(column_sql),
		1U,
		"SQL Server OUTPUT INTO column must be a single identifier",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	copy = sqlparser_sqlserver_output_dup_trim(column_sql, 0U, strlen(column_sql), out_error);
	if (copy == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	free(channel->sink_columns[column_index]);
	channel->sink_columns[column_index] = copy;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_output_insert_sink_column(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_channel_t *channel;
	char *copy;
	sqlparser_status_t status;

	channel = sqlparser_sqlserver_output_mutable_sink_channel(state, dml_index, channel_index, out_error);
	if (channel == NULL || column_sql == NULL || column_sql[0] == '\0') {
		if (channel != NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column must not be empty");
		}
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (column_index > channel->sink_column_count) {
		column_index = channel->sink_column_count;
	}
	status = sqlparser_sqlserver_output_validate_identifier(
		column_sql,
		0U,
		strlen(column_sql),
		1U,
		"SQL Server OUTPUT INTO column must be a single identifier",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	copy = sqlparser_sqlserver_output_dup_trim(column_sql, 0U, strlen(column_sql), out_error);
	if (copy == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	if (channel->sink_column_count == SIZE_MAX) {
		free(copy);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "DML result sink column count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_sqlserver_output_reserve(
		(void **)&channel->sink_columns,
		&channel->sink_column_capacity,
		channel->sink_column_count + 1U,
		sizeof(*channel->sink_columns),
		"DML result sink column count is too large",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(copy);
		return status;
	}
	if (column_index < channel->sink_column_count) {
		memmove(
			&channel->sink_columns[column_index + 1U],
			&channel->sink_columns[column_index],
			(channel->sink_column_count - column_index) * sizeof(*channel->sink_columns));
	}
	channel->sink_columns[column_index] = copy;
	channel->sink_column_count++;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_output_delete_sink_column(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_channel_t *channel;

	channel = sqlparser_sqlserver_output_mutable_sink_channel(state, dml_index, channel_index, out_error);
	if (channel == NULL || column_index >= channel->sink_column_count) {
		if (channel != NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column index is out of range");
		}
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	free(channel->sink_columns[column_index]);
	if (column_index + 1U < channel->sink_column_count) {
		memmove(
			&channel->sink_columns[column_index],
			&channel->sink_columns[column_index + 1U],
			(channel->sink_column_count - column_index - 1U) * sizeof(*channel->sink_columns));
	}
	channel->sink_column_count--;
	channel->sink_columns[channel->sink_column_count] = NULL;
	return SQLPARSER_STATUS_OK;
}

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_sqlserver_output_text_t;

static int sqlparser_sqlserver_output_text_append(
	sqlparser_sqlserver_output_text_t *text,
	const char *value,
	size_t len)
{
	if (text == NULL || value == NULL || len > text->capacity - text->len) {
		return -1;
	}
	memcpy(text->data + text->len, value, len);
	text->len += len;
	text->data[text->len] = '\0';
	return 0;
}

static int sqlparser_sqlserver_output_text_append_cstr(
	sqlparser_sqlserver_output_text_t *text,
	const char *value)
{
	return sqlparser_sqlserver_output_text_append(text, value, strlen(value));
}

static int sqlparser_sqlserver_output_append_public_target(
	sqlparser_sqlserver_output_text_t *text,
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	sqlparser_sqlserver_output_span_t span,
	const char *action_marker)
{
	size_t start;
	size_t end;
	size_t source_pos;
	size_t index;

	start = sqlparser_sqlserver_trim_left(sql, span.start, span.end);
	end = sqlparser_sqlserver_trim_right(sql, start, span.end);
	source_pos = start;
	for (index = 0U; index < tokens->count; index++) {
		const sqlparser_sqlserver_token_t *token;

		token = &tokens->items[index];
		if (action_marker == NULL || token->start < start || token->end > end ||
		    !sqlparser_sqlserver_output_token_identifier_equal(
			    sql,
			    token,
			    action_marker)) {
			continue;
		}
		if (sqlparser_sqlserver_output_text_append(text, sql + source_pos, token->start - source_pos) != 0 ||
		    sqlparser_sqlserver_output_text_append_cstr(text, "$action") != 0) {
			return -1;
		}
		source_pos = token->end;
	}
	return source_pos < end ?
		sqlparser_sqlserver_output_text_append(text, sql + source_pos, end - source_pos) : 0;
}

static sqlparser_status_t sqlparser_sqlserver_output_returning_spans(
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t returning_index,
	size_t statement_end,
	size_t expected_count,
	sqlparser_sqlserver_output_span_t **out_spans,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_span_t *spans;
	size_t span_index;
	size_t span_start;
	size_t index;

	*out_spans = NULL;
	if (expected_count == 0U || expected_count > SIZE_MAX / sizeof(*spans)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server OUTPUT target metadata is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	spans = (sqlparser_sqlserver_output_span_t *)calloc(expected_count, sizeof(*spans));
	if (spans == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	span_index = 0U;
	span_start = tokens->items[returning_index].end;
	for (index = returning_index + 1U; index < tokens->count; index++) {
		const sqlparser_sqlserver_token_t *token;

		token = &tokens->items[index];
		if (token->start >= statement_end) {
			break;
		}
		if (!sqlparser_sqlserver_output_token_top(token) ||
		    token->kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL || token->symbol != ',') {
			continue;
		}
		if (span_index >= expected_count - 1U) {
			free(spans);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server OUTPUT target metadata does not match AST");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		spans[span_index].start = span_start;
		spans[span_index].end = token->start;
		span_index++;
		span_start = token->end;
	}
	spans[span_index].start = span_start;
	spans[span_index].end = statement_end;
	span_index++;
	if (span_index != expected_count) {
		free(spans);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server OUTPUT target metadata does not match AST");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_spans = spans;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_output_build_public_clause(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	const sqlparser_sqlserver_output_dml_t *dml,
	const sqlparser_sqlserver_output_span_t *spans,
	char **out_clause,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_text_t text;
	size_t capacity;
	size_t channel_index;
	size_t target_ordinal;

	capacity = 1U;
	for (channel_index = 0U; channel_index < dml->channel_count; channel_index++) {
		const sqlparser_sqlserver_output_channel_t *channel;
		size_t index;

		channel = &dml->channels[channel_index];
		if (capacity > SIZE_MAX - 32U) {
			goto too_large;
		}
		capacity += 32U;
		for (index = 0U; index < channel->target_count; index++) {
			const sqlparser_sqlserver_output_span_t *span;

			span = &spans[channel->target_offset + index];
			if (span->end < span->start || capacity > SIZE_MAX - (span->end - span->start) - 2U) {
				goto too_large;
			}
			capacity += span->end - span->start + 2U;
		}
		if (channel->sink_sql != NULL) {
			if (capacity > SIZE_MAX - strlen(channel->sink_sql) - 8U) {
				goto too_large;
			}
			capacity += strlen(channel->sink_sql) + 8U;
		}
		for (index = 0U; index < channel->sink_column_count; index++) {
			if (capacity > SIZE_MAX - strlen(channel->sink_columns[index]) - 2U) {
				goto too_large;
			}
			capacity += strlen(channel->sink_columns[index]) + 2U;
		}
	}
	memset(&text, 0, sizeof(text));
	text.data = (char *)malloc(capacity + 1U);
	if (text.data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	text.capacity = capacity;
	text.data[0] = '\0';
	target_ordinal = 0U;
	for (channel_index = 0U; channel_index < dml->channel_count; channel_index++) {
		const sqlparser_sqlserver_output_channel_t *channel;
		size_t index;

		channel = &dml->channels[channel_index];
		if (sqlparser_sqlserver_output_text_append_cstr(&text, " OUTPUT ") != 0) {
			goto internal_fail;
		}
		for (index = 0U; index < channel->target_count; index++) {
			const char *action_marker;

			if (index > 0U && sqlparser_sqlserver_output_text_append_cstr(&text, ", ") != 0) {
				goto internal_fail;
			}
			action_marker = sqlparser_sqlserver_output_channel_action_marker(channel, index);
			if (sqlparser_sqlserver_output_append_public_target(
				    &text,
				    sql,
				    tokens,
				    spans[target_ordinal++],
				    action_marker) != 0) {
				goto internal_fail;
			}
		}
		if (channel->kind == SQLPARSER_GRAPH_DML_RESULT_SINK) {
			if (channel->sink_sql == NULL ||
			    sqlparser_sqlserver_output_text_append_cstr(&text, " INTO ") != 0 ||
			    sqlparser_sqlserver_output_text_append_cstr(&text, channel->sink_sql) != 0) {
				goto internal_fail;
			}
			if (channel->sink_column_count > 0U) {
				if (sqlparser_sqlserver_output_text_append_cstr(&text, " (") != 0) {
					goto internal_fail;
				}
				for (index = 0U; index < channel->sink_column_count; index++) {
					if ((index > 0U && sqlparser_sqlserver_output_text_append_cstr(&text, ", ") != 0) ||
					    sqlparser_sqlserver_output_text_append_cstr(&text, channel->sink_columns[index]) != 0) {
						goto internal_fail;
					}
				}
				if (sqlparser_sqlserver_output_text_append_cstr(&text, ")") != 0) {
					goto internal_fail;
				}
			}
		}
	}
	if (sqlparser_sqlserver_output_text_append_cstr(&text, " ") != 0) {
		goto internal_fail;
	}
	*out_clause = text.data;
	return SQLPARSER_STATUS_OK;

too_large:
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server OUTPUT clause is too large");
	return SQLPARSER_STATUS_RESOURCE_LIMIT;

internal_fail:
	free(text.data);
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "failed to restore SQL Server OUTPUT clause");
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static size_t sqlparser_sqlserver_output_public_insert_position(
	const char *sql,
	const sqlparser_sqlserver_output_tokens_t *tokens,
	size_t root_index,
	size_t returning_index,
	sqlparser_graph_dml_kind_t kind)
{
	size_t index;

	if (kind == SQLPARSER_GRAPH_DML_MERGE) {
		return tokens->items[returning_index].start;
	}
	for (index = root_index + 1U; index < returning_index; index++) {
		const sqlparser_sqlserver_token_t *token;

		token = &tokens->items[index];
		if (!sqlparser_sqlserver_output_token_top(token)) {
			continue;
		}
		if (kind == SQLPARSER_GRAPH_DML_INSERT &&
		    (sqlparser_sqlserver_token_word_equal(sql, token, "values") ||
		     sqlparser_sqlserver_token_word_equal(sql, token, "select") ||
		     sqlparser_sqlserver_token_word_equal(sql, token, "default"))) {
			return token->start;
		}
		if (kind == SQLPARSER_GRAPH_DML_UPDATE &&
		    (sqlparser_sqlserver_token_word_equal(sql, token, "from") ||
		     sqlparser_sqlserver_token_word_equal(sql, token, "where"))) {
			return token->start;
		}
		if (kind == SQLPARSER_GRAPH_DML_DELETE &&
		    (sqlparser_sqlserver_token_word_equal(sql, token, "using") ||
		     sqlparser_sqlserver_token_word_equal(sql, token, "where"))) {
			return token->start;
		}
	}
	return tokens->items[returning_index].start;
}

static sqlparser_status_t sqlparser_sqlserver_output_postprocess_dml(
	const char *sql,
	size_t statement_start,
	size_t statement_end,
	const sqlparser_sqlserver_output_dml_t *dml,
	sqlparser_sqlserver_output_edits_t *edits,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	sqlparser_sqlserver_output_span_t *spans;
	size_t root_index;
	size_t returning_index;
	size_t expected_count;
	size_t channel_index;
	size_t insertion_pos;
	size_t insertion_start;
	size_t returning_start;
	size_t delete_source_index;
	size_t delete_target_end;
	char *clause;
	char *top_text;
	sqlparser_status_t status;

	memset(&tokens, 0, sizeof(tokens));
	spans = NULL;
	clause = NULL;
	top_text = NULL;
	status = sqlparser_sqlserver_output_tokenize(sql, statement_start, statement_end, &tokens, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	root_index = sqlparser_sqlserver_output_find_root_dml(sql, &tokens);
	if (root_index == (size_t)-1) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		sqlparser_error_set_message(out_error, status, "SQL Server DML statement was not found during deparse");
		goto done;
	}
	if (dml->omitted_into &&
	    (dml->kind == SQLPARSER_GRAPH_DML_INSERT ||
	     dml->kind == SQLPARSER_GRAPH_DML_MERGE)) {
		size_t into_index;

		into_index = root_index + 1U;
		if (into_index >= tokens.count ||
		    !sqlparser_sqlserver_token_word_equal(sql, &tokens.items[into_index], "into")) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(out_error, status, "SQL Server INSERT INTO restoration failed");
			goto done;
		}
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			tokens.items[into_index].start,
			sqlparser_sqlserver_trim_left(
				sql,
				tokens.items[into_index].end,
				statement_end),
			NULL,
			NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
	}
	if (dml->top_sql != NULL) {
		size_t len;

		len = strlen(dml->top_sql);
		if (len > SIZE_MAX - 2U) {
			status = SQLPARSER_STATUS_RESOURCE_LIMIT;
			sqlparser_error_set_message(out_error, status, "SQL Server TOP clause is too large");
			goto done;
		}
		top_text = (char *)malloc(len + 2U);
		if (top_text == NULL) {
			status = SQLPARSER_STATUS_NO_MEMORY;
			sqlparser_error_set_message(out_error, status, "out of memory");
			goto done;
		}
		top_text[0] = ' ';
		memcpy(top_text + 1U, dml->top_sql, len + 1U);
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			tokens.items[root_index].end,
			tokens.items[root_index].end,
			NULL,
			top_text,
			out_error);
		top_text = NULL;
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
	}
	if (dml->channel_count == 0U) {
		status = SQLPARSER_STATUS_OK;
		goto done;
	}
	returning_index = sqlparser_sqlserver_output_find_word_between(
		sql,
		&tokens,
		root_index + 1U,
		tokens.count,
		"returning");
	if (returning_index == (size_t)-1) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		sqlparser_error_set_message(out_error, status, "SQL Server OUTPUT returning list was not found during deparse");
		goto done;
	}
	expected_count = 0U;
	for (channel_index = 0U; channel_index < dml->channel_count; channel_index++) {
		if (dml->channels[channel_index].target_count > SIZE_MAX - expected_count) {
			status = SQLPARSER_STATUS_RESOURCE_LIMIT;
			sqlparser_error_set_message(out_error, status, "SQL Server OUTPUT target count is too large");
			goto done;
		}
		expected_count += dml->channels[channel_index].target_count;
	}
	status = sqlparser_sqlserver_output_returning_spans(
		&tokens,
		returning_index,
		statement_end,
		expected_count,
		&spans,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto done;
	}
	status = sqlparser_sqlserver_output_build_public_clause(
		sql,
		&tokens,
		dml,
		spans,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto done;
	}
	insertion_pos = sqlparser_sqlserver_output_public_insert_position(
		sql,
		&tokens,
		root_index,
		returning_index,
		dml->kind);
	delete_source_index = (size_t)-1;
	if (dml->delete_source_from) {
		delete_source_index = sqlparser_sqlserver_output_find_word_between(
			sql,
			&tokens,
			root_index + 1U,
			returning_index,
			"using");
		if (delete_source_index == (size_t)-1) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(out_error, status, "SQL Server DELETE source restoration failed");
			goto done;
		}
	}
	if (dml->delete_target_sql != NULL) {
		if (root_index + 1U >= tokens.count ||
		    !sqlparser_sqlserver_token_word_equal(sql, &tokens.items[root_index + 1U], "from")) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(out_error, status, "SQL Server DELETE target restoration failed");
			goto done;
		}
		delete_target_end = delete_source_index != (size_t)-1 ?
			tokens.items[delete_source_index].start : insertion_pos;
		delete_target_end = sqlparser_sqlserver_trim_right(
			sql, tokens.items[root_index + 1U].end, delete_target_end);
		if (delete_target_end <= tokens.items[root_index + 1U].end) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(out_error, status, "SQL Server DELETE target restoration failed");
			goto done;
		}
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			tokens.items[root_index + 1U].start,
			delete_target_end,
			dml->delete_target_sql,
			NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
	}
	if (delete_source_index != (size_t)-1) {
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			tokens.items[delete_source_index].start,
			tokens.items[delete_source_index].end,
			"FROM",
			NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
	}
	insertion_start = insertion_pos;
	while (insertion_start > statement_start &&
	       isspace((unsigned char)sql[insertion_start - 1U])) {
		insertion_start--;
	}
	returning_start = tokens.items[returning_index].start;
	while (returning_start > statement_start &&
	       isspace((unsigned char)sql[returning_start - 1U])) {
		returning_start--;
	}
	if (insertion_pos == tokens.items[returning_index].start) {
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			insertion_start,
			statement_end,
			NULL,
			clause,
			out_error);
		clause = NULL;
	} else {
		status = sqlparser_sqlserver_output_edit_add(
			edits,
			returning_start,
			statement_end,
			NULL,
			NULL,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_output_edit_add(
				edits,
				insertion_start,
				insertion_pos,
				NULL,
				clause,
				out_error);
			clause = NULL;
		}
	}

done:
	free(top_text);
	free(clause);
	free(spans);
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return status;
}

static int sqlparser_sqlserver_output_statement_bounds(
	const char *sql,
	size_t wanted_index,
	size_t *out_start,
	size_t *out_end)
{
	size_t sql_len;
	size_t start;
	size_t ordinal;

	sql_len = strlen(sql);
	start = 0U;
	ordinal = 0U;
	while (start < sql_len) {
		size_t end;
		size_t content_start;
		size_t content_end;

		while (start < sql_len && (isspace((unsigned char)sql[start]) || sql[start] == ';')) {
			start++;
		}
		if (start >= sql_len) {
			break;
		}
		end = sqlparser_sqlserver_statement_end(sql, start, sql_len);
		content_start = sqlparser_sqlserver_trim_left(sql, start, end);
		content_end = sqlparser_sqlserver_trim_right(sql, content_start, end);
		if (content_start < content_end) {
			if (ordinal == wanted_index) {
				*out_start = content_start;
				*out_end = content_end;
				return 1;
			}
			ordinal++;
		}
		start = end < sql_len && sql[end] == ';' ? end + 1U : end;
	}
	return 0;
}

static int sqlparser_sqlserver_output_nested_bounds(
	const char *sql,
	size_t statement_start,
	size_t statement_end,
	const char *source_name,
	size_t *out_body_start,
	size_t *out_body_end,
	size_t *out_name_start,
	size_t *out_close_end,
	size_t *out_reference_start,
	size_t *out_reference_end,
	size_t *out_with_start,
	size_t *out_root_start)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	size_t definition_index;
	size_t open_index;
	size_t close_index;
	size_t reference_index;
	size_t root_index;
	size_t index;
	int found;

	memset(&tokens, 0, sizeof(tokens));
	if (sqlparser_sqlserver_output_tokenize(
		    sql, statement_start, statement_end, &tokens, NULL) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	definition_index = (size_t)-1;
	open_index = (size_t)-1;
	close_index = (size_t)-1;
	reference_index = (size_t)-1;
	root_index = sqlparser_sqlserver_output_find_root_dml(sql, &tokens);
	for (index = 0U; index + 2U < tokens.count; index++) {
		if (tokens.items[index].paren_depth != 0U ||
		    !sqlparser_sqlserver_output_token_identifier_equal(sql, &tokens.items[index], source_name) ||
		    !sqlparser_sqlserver_token_word_equal(sql, &tokens.items[index + 1U], "as") ||
		    tokens.items[index + 2U].kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL ||
		    tokens.items[index + 2U].symbol != '(' ||
		    tokens.items[index + 2U].paren_depth != 0U) {
			continue;
		}
		definition_index = index;
		open_index = index + 2U;
		close_index = sqlparser_sqlserver_output_nested_close_index(&tokens, open_index);
		break;
	}
	if (definition_index != (size_t)-1 && close_index != (size_t)-1) {
		for (index = close_index + 1U; index < tokens.count; index++) {
			if (tokens.items[index].paren_depth == 0U &&
			    sqlparser_sqlserver_output_token_identifier_equal(
				    sql, &tokens.items[index], source_name)) {
				reference_index = index;
				break;
			}
		}
	}
	found = definition_index != (size_t)-1 && close_index != (size_t)-1 &&
		reference_index != (size_t)-1 && root_index != (size_t)-1 &&
		tokens.count > 0U && sqlparser_sqlserver_token_word_equal(sql, &tokens.items[0], "with");
	if (found) {
		if (out_body_start != NULL) {
			*out_body_start = tokens.items[open_index].end;
		}
		if (out_body_end != NULL) {
			*out_body_end = tokens.items[close_index].start;
		}
		if (out_name_start != NULL) {
			*out_name_start = tokens.items[definition_index].start;
		}
		if (out_close_end != NULL) {
			*out_close_end = tokens.items[close_index].end;
		}
		if (out_reference_start != NULL) {
			*out_reference_start = tokens.items[reference_index].start;
		}
		if (out_reference_end != NULL) {
			*out_reference_end = tokens.items[reference_index].end;
		}
		if (out_with_start != NULL) {
			*out_with_start = tokens.items[0].start;
		}
		if (out_root_start != NULL) {
			*out_root_start = tokens.items[root_index].start;
		}
	}
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return found;
}

static sqlparser_status_t sqlparser_sqlserver_output_unwrap_nested(
	char **io_sql,
	const sqlparser_sqlserver_output_dml_t *dml,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	sqlparser_sqlserver_output_edits_t edits;
	size_t statement_start;
	size_t statement_end;
	size_t body_start;
	size_t body_end;
	size_t name_start;
	size_t close_end;
	size_t reference_start;
	size_t reference_end;
	size_t with_start;
	size_t root_start;
	size_t removal_end;
	size_t index;
	char *replacement;
	size_t body_len;
	sqlparser_status_t status;

	if (!sqlparser_sqlserver_output_statement_bounds(
		    *io_sql, dml->statement_index, &statement_start, &statement_end) ||
	    !sqlparser_sqlserver_output_nested_bounds(
		    *io_sql,
		    statement_start,
		    statement_end,
		    dml->source_name,
		    &body_start,
		    &body_end,
		    &name_start,
		    &close_end,
		    &reference_start,
		    &reference_end,
		    &with_start,
		    &root_start)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "nested SQL Server DML source was not found during deparse");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	body_start = sqlparser_sqlserver_trim_left(*io_sql, body_start, body_end);
	body_end = sqlparser_sqlserver_trim_right(*io_sql, body_start, body_end);
	body_len = body_end - body_start;
	if (body_len > SIZE_MAX - 3U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "nested SQL Server DML SQL is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	replacement = (char *)malloc(body_len + 3U);
	if (replacement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	replacement[0] = '(';
	memcpy(replacement + 1U, *io_sql + body_start, body_len);
	replacement[body_len + 1U] = ')';
	replacement[body_len + 2U] = '\0';
	memset(&tokens, 0, sizeof(tokens));
	memset(&edits, 0, sizeof(edits));
	status = sqlparser_sqlserver_output_tokenize(
		*io_sql, statement_start, statement_end, &tokens, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(replacement);
		return status;
	}
	removal_end = root_start;
	for (index = 0U; index < tokens.count; index++) {
		if (tokens.items[index].start < close_end) {
			continue;
		}
		if (tokens.items[index].paren_depth == 0U &&
		    tokens.items[index].kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    tokens.items[index].symbol == ',') {
			if (index + 1U < tokens.count) {
				removal_end = tokens.items[index + 1U].start;
			}
			break;
		}
		break;
	}
	status = sqlparser_sqlserver_output_edit_add(
		&edits,
		removal_end == root_start ? with_start : name_start,
		removal_end,
		NULL,
		NULL,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_edit_add(
			&edits,
			reference_start,
			reference_end,
			NULL,
			replacement,
			out_error);
		replacement = NULL;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_apply_edits(io_sql, &edits, out_error);
	}
	free(replacement);
	sqlparser_sqlserver_output_edits_clear(&edits);
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return status;
}

sqlparser_status_t sqlparser_sqlserver_output_postprocess(
	char **io_sql,
	const sqlparser_sqlserver_output_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_edits_t edits;
	size_t dml_index;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL Server OUTPUT postprocess arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state == NULL || state->dml_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&edits, 0, sizeof(edits));
	status = SQLPARSER_STATUS_OK;
	for (dml_index = 0U; dml_index < state->dml_count; dml_index++) {
		const sqlparser_sqlserver_output_dml_t *dml;
		size_t statement_start;
		size_t statement_end;

		dml = &state->dmls[dml_index];
		if (!sqlparser_sqlserver_output_statement_bounds(
			    *io_sql,
			    dml->statement_index,
			    &statement_start,
			    &statement_end)) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(out_error, status, "SQL Server statement was not found during deparse");
			break;
		}
		if (dml->has_parent &&
		    !sqlparser_sqlserver_output_nested_bounds(
			    *io_sql,
			    statement_start,
			    statement_end,
			    dml->source_name,
			    &statement_start,
			    &statement_end,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL)) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(out_error, status, "nested SQL Server DML source was not found during deparse");
			break;
		}
		status = sqlparser_sqlserver_output_postprocess_dml(
			*io_sql,
			statement_start,
			statement_end,
			dml,
			&edits,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_apply_edits(io_sql, &edits, out_error);
	}
	sqlparser_sqlserver_output_edits_clear(&edits);
	if (status == SQLPARSER_STATUS_OK) {
		for (dml_index = 0U; dml_index < state->dml_count; dml_index++) {
			if (!state->dmls[dml_index].has_parent) {
				continue;
			}
			status = sqlparser_sqlserver_output_unwrap_nested(
				io_sql, &state->dmls[dml_index], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
		}
	}
	return status;
}

sqlparser_status_t sqlparser_sqlserver_output_preprocess_target_sql(
	const char *public_sql,
	sqlparser_graph_dml_kind_t dml_kind,
	char **out_sql,
	char **out_action_marker,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	sqlparser_sqlserver_output_channel_t channel;
	size_t count;
	sqlparser_status_t status;

	if (public_sql == NULL || out_sql == NULL || out_action_marker == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	*out_action_marker = NULL;
	memset(&tokens, 0, sizeof(tokens));
	memset(&channel, 0, sizeof(channel));
	status = sqlparser_sqlserver_output_tokenize(public_sql, 0U, strlen(public_sql), &tokens, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_output_target_sql(
			public_sql,
			&tokens,
			0U,
			strlen(public_sql),
			dml_kind,
			&channel,
			&count,
			out_sql,
			out_error);
		if (status == SQLPARSER_STATUS_OK && count != 1U) {
			free(*out_sql);
			*out_sql = NULL;
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "DML result target SQL must contain exactly one target");
			status = SQLPARSER_STATUS_UNSUPPORTED;
		}
	}
	if (status == SQLPARSER_STATUS_OK && channel.action_count > 0U) {
		*out_action_marker = sqlparser_strdup(channel.actions[0].marker);
		if (*out_action_marker == NULL) {
			free(*out_sql);
			*out_sql = NULL;
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			status = SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	sqlparser_sqlserver_output_channel_clear(&channel);
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return status;
}

sqlparser_status_t sqlparser_sqlserver_output_postprocess_target_sql(
	const char *parser_sql,
	const char *action_marker,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_tokens_t tokens;
	size_t marker_count;
	size_t sql_len;
	size_t source_pos;
	size_t output_pos;
	size_t index;
	char *result;
	sqlparser_status_t status;

	if (parser_sql == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (action_marker == NULL || action_marker[0] == '\0') {
		*out_sql = sqlparser_strdup(parser_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	memset(&tokens, 0, sizeof(tokens));
	sql_len = strlen(parser_sql);
	status = sqlparser_sqlserver_output_tokenize(parser_sql, 0U, sql_len, &tokens, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	marker_count = 0U;
	for (index = 0U; index < tokens.count; index++) {
			if (sqlparser_sqlserver_output_token_identifier_equal(
				    parser_sql,
				    &tokens.items[index],
				    action_marker)) {
			marker_count++;
		}
	}
	if (marker_count == 0U) {
		*out_sql = sqlparser_strdup(parser_sql);
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	result = (char *)malloc(sql_len + 1U);
	if (result == NULL) {
		sqlparser_sqlserver_output_tokens_clear(&tokens);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	source_pos = 0U;
	output_pos = 0U;
	for (index = 0U; index < tokens.count; index++) {
		const sqlparser_sqlserver_token_t *token;
		size_t copy_len;

		token = &tokens.items[index];
		if (!sqlparser_sqlserver_output_token_identifier_equal(
			    parser_sql,
			    token,
			    action_marker)) {
			continue;
		}
		copy_len = token->start - source_pos;
		memcpy(result + output_pos, parser_sql + source_pos, copy_len);
		output_pos += copy_len;
		memcpy(result + output_pos, "$action", strlen("$action"));
		output_pos += strlen("$action");
		source_pos = token->end;
	}
	if (source_pos < sql_len) {
		memcpy(result + output_pos, parser_sql + source_pos, sql_len - source_pos);
		output_pos += sql_len - source_pos;
	}
	result[output_pos] = '\0';
	*out_sql = result;
	sqlparser_sqlserver_output_tokens_clear(&tokens);
	return SQLPARSER_STATUS_OK;
}
