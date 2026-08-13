#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "../dialect/sqlparser_dialect_sqlserver_scan.h"
#include "sqlparser_internal.h"

typedef struct {
	size_t output_start;
	size_t output_end;
	const char *source;
	size_t source_length;
	size_t source_start;
	size_t source_end;
} sqlparser_surface_edit_t;

typedef struct {
	size_t body_end;
	size_t last_semantic_end;
	size_t length;
	size_t semantic_count;
	int has_batch_separator;
} sqlparser_terminal_scan_t;

typedef struct {
	sqlparser_dialect_t dialect;
	const char *sql;
	size_t length;
	size_t pos;
	int line_prefix;
} sqlparser_statement_scan_t;

typedef struct {
	size_t start;
	size_t end;
} sqlparser_statement_span_t;

void sqlparser_surface_source_edits_release(
	sqlparser_surface_source_edits_t *edits)
{
	size_t index;

	if (edits == NULL) {
		return;
	}
	for (index = 0U; index < edits->count; index++) {
		free(edits->items[index].replacement);
	}
	free(edits->items);
	memset(edits, 0, sizeof(*edits));
}

sqlparser_status_t sqlparser_surface_source_edits_clone(
	const sqlparser_surface_source_edits_t *source,
	sqlparser_surface_source_edits_t *out_edits,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (source == NULL || out_edits == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source edit arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_edits, 0, sizeof(*out_edits));
	if (source->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (source->items == NULL ||
	    source->count > SIZE_MAX / sizeof(*out_edits->items)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"source edit list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	out_edits->items = (sqlparser_surface_source_edit_t *)calloc(
		source->count,
		sizeof(*out_edits->items));
	if (out_edits->items == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	out_edits->capacity = source->count;
	for (index = 0U; index < source->count; index++) {
		const sqlparser_surface_source_edit_t *source_edit;
		sqlparser_surface_source_edit_t *edit;

		source_edit = &source->items[index];
		edit = &out_edits->items[index];
		if (source_edit->replacement == NULL ||
		    source_edit->replacement_length == SIZE_MAX) {
			sqlparser_surface_source_edits_release(out_edits);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"source edit list is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		edit->replacement = sqlparser_strndup(
			source_edit->replacement,
			source_edit->replacement_length);
		if (edit->replacement == NULL) {
			out_edits->count = index;
			sqlparser_surface_source_edits_release(out_edits);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		edit->source_start = source_edit->source_start;
		edit->source_end = source_edit->source_end;
		edit->replacement_length = source_edit->replacement_length;
		out_edits->count++;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_surface_source_edits_overlap(
	const sqlparser_surface_source_edit_t *left,
	size_t right_start,
	size_t right_end)
{
	if (left->source_start == left->source_end) {
		return left->source_start >= right_start &&
			left->source_start <= right_end;
	}
	if (right_start == right_end) {
		return right_start >= left->source_start &&
			right_start <= left->source_end;
	}
	return left->source_start < right_end &&
		right_start < left->source_end;
}

sqlparser_status_t sqlparser_surface_source_edits_insert(
	sqlparser_surface_source_edits_t *edits,
	size_t source_start,
	size_t source_end,
	const char *replacement,
	size_t replacement_length,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	char *copy;
	size_t capacity;
	size_t index;
	sqlparser_surface_source_edit_t *next;

	if (out_supported != NULL) {
		*out_supported = 0;
	}
	if (edits == NULL || replacement == NULL ||
	    out_supported == NULL || source_start > source_end ||
	    replacement_length == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source edit arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (edits->count > 0U &&
	    edits->items[edits->count - 1U].source_start < source_start) {
		index = edits->count;
	} else {
		index = 0U;
		while (index < edits->count &&
		       edits->items[index].source_start < source_start) {
			index++;
		}
	}
	if ((index > 0U &&
	     sqlparser_surface_source_edits_overlap(
		     &edits->items[index - 1U],
		     source_start,
		     source_end)) ||
	    (index < edits->count &&
	     sqlparser_surface_source_edits_overlap(
		     &edits->items[index],
		     source_start,
		     source_end))) {
		return SQLPARSER_STATUS_OK;
	}
	copy = sqlparser_strndup(replacement, replacement_length);
	if (copy == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (edits->count == edits->capacity) {
		capacity = edits->capacity == 0U ? 4U : edits->capacity * 2U;
		if (capacity < edits->capacity ||
		    capacity > SIZE_MAX / sizeof(*edits->items)) {
			free(copy);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_surface_source_edit_t *)realloc(
			edits->items,
			capacity * sizeof(*next));
		if (next == NULL) {
			free(copy);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		edits->items = next;
		edits->capacity = capacity;
	}
	if (index < edits->count) {
		memmove(
			&edits->items[index + 1U],
			&edits->items[index],
			(edits->count - index) * sizeof(*edits->items));
	}
	edits->items[index].source_start = source_start;
	edits->items[index].source_end = source_end;
	edits->items[index].replacement = copy;
	edits->items[index].replacement_length = replacement_length;
	edits->count++;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_surface_ranges_equal(
	const char *left,
	size_t left_start,
	size_t left_end,
	const char *right,
	size_t right_start,
	size_t right_end);

int sqlparser_public_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch == '$' || ch == '#' ||
		ch >= 0x80U;
}

static int sqlparser_public_dollar_tag_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch >= 0x80U;
}

static size_t sqlparser_public_skip_dollar_quote(
	const char *sql,
	size_t index)
{
	size_t body;
	size_t delimiter_length;
	size_t tag_end;

	if (sql == NULL || sql[index] != '$' ||
	    (index > 0U &&
	     (isalnum((unsigned char)sql[index - 1U]) ||
	      sql[index - 1U] == '_' || sql[index - 1U] == '$' ||
	      (unsigned char)sql[index - 1U] >= 0x80U))) {
		return index;
	}
	tag_end = index + 1U;
	while (sqlparser_public_dollar_tag_char_is_ident(
		       (unsigned char)sql[tag_end])) {
		tag_end++;
	}
	if (sql[tag_end] != '$') {
		return index;
	}
	delimiter_length = tag_end - index + 1U;
	body = tag_end + 1U;
	while (sql[body] != '\0') {
		if (strncmp(sql + body, sql + index, delimiter_length) == 0) {
			return body + delimiter_length;
		}
		body++;
	}
	return index;
}

static size_t sqlparser_public_skip_oracle_q_quote(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t index)
{
	char close;
	char open;
	size_t pos;

	if (!sqlparser_dialect_is_oracle_or_dameng_compatible(dialect) ||
	    sql == NULL) {
		return index;
	}
	pos = index;
	if ((sql[pos] == 'n' || sql[pos] == 'N') &&
	    (sql[pos + 1U] == 'q' || sql[pos + 1U] == 'Q')) {
		pos++;
	}
	if ((sql[pos] != 'q' && sql[pos] != 'Q') ||
	    sql[pos + 1U] != '\'' || sql[pos + 2U] == '\0') {
		return index;
	}
	open = sql[pos + 2U];
	switch (open) {
		case '[':
			close = ']';
			break;
		case '{':
			close = '}';
			break;
		case '(':
			close = ')';
			break;
		case '<':
			close = '>';
			break;
		default:
			close = open;
			break;
	}
	pos += 3U;
	while (sql[pos] != '\0') {
		if (sql[pos] == close && sql[pos + 1U] == '\'') {
			return pos + 2U;
		}
		pos++;
	}
	return pos;
}

static int sqlparser_public_nested_comments(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		dialect == SQLPARSER_DIALECT_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL ||
		dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
}

size_t sqlparser_public_skip_quoted_or_comment(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t index)
{
	char quote;
	size_t depth;
	size_t pos;

	if (sql == NULL || sql[index] == '\0') {
		return index;
	}
	if (dialect == SQLPARSER_DIALECT_POSTGRESQL ||
	    dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL) {
		pos = sqlparser_public_skip_dollar_quote(sql, index);
		if (pos != index) {
			return pos;
		}
	}
	pos = sqlparser_public_skip_oracle_q_quote(dialect, sql, index);
	if (pos != index) {
		return pos;
	}
	if (sql[index] == '-' && sql[index + 1U] == '-') {
		if (sqlparser_dialect_is_mysql_compatible(dialect) &&
		    sql[index + 2U] != '\0' &&
		    !isspace((unsigned char)sql[index + 2U]) &&
		    !iscntrl((unsigned char)sql[index + 2U])) {
			return index;
		}
		pos = index + 2U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sqlparser_dialect_is_mysql_compatible(dialect) &&
	    sql[index] == '#') {
		pos = index + 1U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sql[index] == '/' && sql[index + 1U] == '*') {
		pos = index + 2U;
		depth = 1U;
		while (sql[pos] != '\0' && sql[pos + 1U] != '\0') {
			if (sqlparser_public_nested_comments(dialect) &&
			    sql[pos] == '/' && sql[pos + 1U] == '*') {
				depth++;
				pos += 2U;
				continue;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				depth--;
				pos += 2U;
				if (depth == 0U) {
					return pos;
				}
				continue;
			}
			pos++;
		}
		return sql[pos] == '\0' ? pos : pos + 1U;
	}
	if (sqlparser_dialect_is_sqlserver_compatible(dialect) &&
	    sql[index] == '[') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == ']') {
				if (sql[pos + 1U] == ']') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}
	if (sql[index] != '\'' && sql[index] != '"' &&
	    (!sqlparser_dialect_is_mysql_compatible(dialect) ||
	     sql[index] != '`')) {
		return index;
	}

	quote = sql[index];
	pos = index + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == '\\' &&
		    (sqlparser_dialect_is_mysql_compatible(dialect) ||
		     (quote == '\'' && index > 0U &&
		      (sql[index - 1U] == 'e' || sql[index - 1U] == 'E') &&
		      (index == 1U ||
		       !sqlparser_public_char_is_ident(
			       (unsigned char)sql[index - 2U])))) &&
		    sql[pos + 1U] != '\0') {
			pos += 2U;
			continue;
		}
		if (sql[pos] == quote) {
			if (sql[pos + 1U] == quote) {
				pos += 2U;
				continue;
			}
			return pos + 1U;
		}
		pos++;
	}
	return pos;
}

int sqlparser_public_comment_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos)
{
	if (sql[pos] == '/' && sql[pos + 1U] == '*') {
		return 1;
	}
	if (sql[pos] == '-' && sql[pos + 1U] == '-') {
		return !sqlparser_dialect_is_mysql_compatible(dialect) ||
			sql[pos + 2U] == '\0' ||
			isspace((unsigned char)sql[pos + 2U]) ||
			iscntrl((unsigned char)sql[pos + 2U]);
	}
	return sqlparser_dialect_is_mysql_compatible(dialect) &&
		sql[pos] == '#';
}

static int sqlparser_public_semantic_comment_kind_at(
	const char *sql,
	size_t pos)
{
	if (sql[pos] == '/' && sql[pos + 1U] == '*' &&
	    (sql[pos + 2U] == '+' || sql[pos + 2U] == '!')) {
		return sql[pos + 2U] == '+' ? 1 : 2;
	}
	if (sql[pos] == '-' && sql[pos + 1U] == '-' &&
	    sql[pos + 2U] == '+') {
		return 3;
	}
	return 0;
}

size_t sqlparser_public_skip_trivia(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos)
{
	size_t skipped;

	while (sql[pos] != '\0') {
		if (isspace((unsigned char)sql[pos])) {
			pos++;
			continue;
		}
		if (!sqlparser_public_comment_at(dialect, sql, pos)) {
			break;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			dialect,
			sql,
			pos);
		if (skipped == pos) {
			break;
		}
		pos = skipped;
	}
	return pos;
}

size_t sqlparser_public_skip_space(const char *sql, size_t pos)
{
	while (sql[pos] != '\0' && isspace((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

int sqlparser_public_sqlserver_go_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos,
	size_t *out_after)
{
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect) ||
	    sql == NULL) {
		return 0;
	}
	return sqlparser_sqlserver_line_is_go(sql, pos, out_after);
}

static int sqlparser_public_line_prefix_after(
	const char *sql,
	size_t start,
	size_t end,
	int line_prefix)
{
	size_t pos;

	for (pos = start; pos < end; pos++) {
		if (sql[pos] == '\r' || sql[pos] == '\n') {
			line_prefix = 1;
		} else if (!isspace((unsigned char)sql[pos])) {
			line_prefix = 0;
		}
	}
	return line_prefix;
}

static int sqlparser_public_terminal_scan(
	sqlparser_dialect_t dialect,
	const char *sql,
	sqlparser_terminal_scan_t *out_scan)
{
	size_t after_go;
	size_t pos;
	size_t skipped;
	int line_prefix;
	int semantic_kind;

	if (sql == NULL || out_scan == NULL) {
		return 0;
	}
	memset(out_scan, 0, sizeof(*out_scan));
	pos = 0U;
	line_prefix = 1;
	while (sql[pos] != '\0') {
		if (isspace((unsigned char)sql[pos])) {
			if (sql[pos] == '\r' || sql[pos] == '\n') {
				line_prefix = 1;
			}
			pos++;
			continue;
		}
		if (sqlparser_public_comment_at(dialect, sql, pos)) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				dialect,
				sql,
				pos);
			semantic_kind = sqlparser_public_semantic_comment_kind_at(
				sql,
				pos);
			if (semantic_kind != 0) {
				out_scan->semantic_count++;
				out_scan->last_semantic_end = skipped;
			}
			line_prefix = sqlparser_public_line_prefix_after(
				sql,
				pos,
				skipped,
				line_prefix);
			pos = skipped;
			continue;
		}
		if (sql[pos] == ';') {
			line_prefix = 0;
			pos++;
			continue;
		}
		if (line_prefix && (sql[pos] == 'g' || sql[pos] == 'G') &&
		    sqlparser_public_sqlserver_go_at(
			    dialect,
			    sql,
			    pos,
			    &after_go)) {
			out_scan->has_batch_separator = 1;
			pos = after_go;
			line_prefix = 1;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			dialect,
			sql,
			pos);
		skipped = skipped != pos ? skipped : pos + 1U;
		line_prefix = sqlparser_public_line_prefix_after(
			sql,
			pos,
			skipped,
			line_prefix);
		pos = skipped;
		out_scan->body_end = pos;
		out_scan->semantic_count = 0U;
		out_scan->last_semantic_end = 0U;
	}
	out_scan->length = pos;
	return out_scan->body_end > 0U || out_scan->semantic_count > 0U;
}

static int sqlparser_public_next_semantic(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t length,
	size_t *in_out_pos,
	size_t *out_start,
	size_t *out_end,
	int *out_kind)
{
	size_t pos;
	size_t skipped;
	int kind;

	if (sql == NULL || in_out_pos == NULL || out_start == NULL ||
	    out_end == NULL || out_kind == NULL) {
		return 0;
	}
	pos = *in_out_pos;
	while (pos < length) {
		if (sqlparser_public_comment_at(dialect, sql, pos)) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				dialect,
				sql,
				pos);
			kind = sqlparser_public_semantic_comment_kind_at(sql, pos);
			if (kind != 0) {
				*out_start = pos;
				*out_end = skipped;
				*out_kind = kind;
				*in_out_pos = skipped;
				return 1;
			}
			pos = skipped;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			dialect,
			sql,
			pos);
		pos = skipped != pos ? skipped : pos + 1U;
	}
	*in_out_pos = pos;
	return 0;
}

static void sqlparser_public_statement_scan_init(
	sqlparser_statement_scan_t *scan,
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t length)
{
	memset(scan, 0, sizeof(*scan));
	scan->dialect = dialect;
	scan->sql = sql;
	scan->length = length;
	scan->line_prefix = 1;
}

static int sqlparser_public_statement_scan_next(
	sqlparser_statement_scan_t *scan,
	sqlparser_statement_span_t *out_span)
{
	size_t after_go;
	size_t body_end;
	size_t body_start;
	size_t pos;
	size_t skipped;

	if (scan == NULL || scan->sql == NULL || out_span == NULL) {
		return 0;
	}
	while (scan->pos < scan->length) {
		body_start = SIZE_MAX;
		body_end = 0U;
		pos = scan->pos;
		while (pos < scan->length) {
			if (isspace((unsigned char)scan->sql[pos])) {
				if (scan->sql[pos] == '\r' || scan->sql[pos] == '\n') {
					scan->line_prefix = 1;
				}
				pos++;
				continue;
			}
			if (sqlparser_public_comment_at(
				    scan->dialect,
				    scan->sql,
				    pos)) {
				skipped = sqlparser_public_skip_quoted_or_comment(
					scan->dialect,
					scan->sql,
					pos);
				scan->line_prefix = sqlparser_public_line_prefix_after(
					scan->sql,
					pos,
					skipped,
					scan->line_prefix);
				pos = skipped;
				continue;
			}
			if (scan->sql[pos] == ';') {
				pos++;
				scan->line_prefix = 0;
				break;
			}
			if (scan->line_prefix &&
			    (scan->sql[pos] == 'g' || scan->sql[pos] == 'G') &&
			    sqlparser_public_sqlserver_go_at(
				    scan->dialect,
				    scan->sql,
				    pos,
				    &after_go)) {
				pos = after_go;
				scan->line_prefix = 1;
				break;
			}
			skipped = sqlparser_public_skip_quoted_or_comment(
				scan->dialect,
				scan->sql,
				pos);
			skipped = skipped != pos ? skipped : pos + 1U;
			if (body_start == SIZE_MAX) {
				body_start = pos;
			}
			body_end = skipped;
			scan->line_prefix = sqlparser_public_line_prefix_after(
				scan->sql,
				pos,
				skipped,
				scan->line_prefix);
			pos = skipped;
		}
		scan->pos = pos;
		if (body_start != SIZE_MAX) {
			out_span->start = body_start;
			out_span->end = body_end;
			return 1;
		}
	}
	return 0;
}

static int sqlparser_public_statement_bodies_compatible(
	const char *source,
	const sqlparser_statement_span_t *source_span,
	const char *output,
	const sqlparser_statement_span_t *output_span)
{
	size_t source_end;
	size_t source_pos;
	size_t output_end;
	size_t output_pos;

	if (source == NULL || source_span == NULL || output == NULL ||
	    output_span == NULL || source_span->start >= source_span->end ||
	    output_span->start >= output_span->end) {
		return 0;
	}
	source_pos = source_span->start;
	output_pos = output_span->start;
	if (!sqlparser_public_char_is_ident((unsigned char)source[source_pos]) ||
	    !sqlparser_public_char_is_ident((unsigned char)output[output_pos])) {
		return source[source_pos] == output[output_pos];
	}
	source_end = source_pos;
	while (source_end < source_span->end &&
	       sqlparser_public_char_is_ident((unsigned char)source[source_end])) {
		source_end++;
	}
	output_end = output_pos;
	while (output_end < output_span->end &&
	       sqlparser_public_char_is_ident((unsigned char)output[output_end])) {
		output_end++;
	}
	if (source_end - source_pos != output_end - output_pos) {
		return 0;
	}
	while (source_pos < source_end) {
		if (tolower((unsigned char)source[source_pos]) !=
		    tolower((unsigned char)output[output_pos])) {
			return 0;
		}
		source_pos++;
		output_pos++;
	}
	return 1;
}

static int sqlparser_surface_plan_gap_edits(
	sqlparser_dialect_t dialect,
	const char *source,
	size_t source_length,
	size_t source_start,
	size_t source_end,
	const char *output,
	size_t output_length,
	size_t output_start,
	size_t output_end,
	sqlparser_surface_edit_t *edits,
	size_t edit_capacity,
	size_t *in_out_edit_count)
{
	size_t output_comment_end;
	size_t output_comment_start;
	size_t output_cursor;
	size_t output_scan_pos;
	size_t source_comment_end;
	size_t source_comment_start;
	size_t source_cursor;
	size_t source_scan_pos;
	int output_found;
	int output_kind;
	int source_found;
	int source_kind;

	if (source == NULL || output == NULL || in_out_edit_count == NULL ||
	    source_start > source_end ||
	    source_end > source_length || output_start > output_end ||
	    output_end > output_length) {
		return 0;
	}
	source_cursor = source_start;
	output_cursor = output_start;
	source_scan_pos = source_start;
	output_scan_pos = output_start;
	for (;;) {
		source_found = sqlparser_public_next_semantic(
			dialect,
			source,
			source_end,
			&source_scan_pos,
			&source_comment_start,
			&source_comment_end,
			&source_kind);
		output_found = sqlparser_public_next_semantic(
			dialect,
			output,
			output_end,
			&output_scan_pos,
			&output_comment_start,
			&output_comment_end,
			&output_kind);
		if (source_found != output_found ||
		    (source_found && source_kind != output_kind)) {
			return 0;
		}
		if (!source_found) {
			break;
		}
		if (!sqlparser_surface_ranges_equal(
			    source,
			    source_cursor,
			    source_comment_start,
			    output,
			    output_cursor,
			    output_comment_start)) {
			if (*in_out_edit_count == SIZE_MAX ||
			    (edits != NULL && *in_out_edit_count >= edit_capacity)) {
				return 0;
			}
			if (edits != NULL) {
				edits[*in_out_edit_count].output_start = output_cursor;
				edits[*in_out_edit_count].output_end = output_comment_start;
				edits[*in_out_edit_count].source = source;
				edits[*in_out_edit_count].source_length = source_length;
				edits[*in_out_edit_count].source_start = source_cursor;
				edits[*in_out_edit_count].source_end = source_comment_start;
			}
			(*in_out_edit_count)++;
		}
		source_cursor = source_comment_end;
		output_cursor = output_comment_end;
	}
	if (!sqlparser_surface_ranges_equal(
		    source,
		    source_cursor,
		    source_end,
		    output,
		    output_cursor,
		    output_end)) {
		if (*in_out_edit_count == SIZE_MAX ||
		    (edits != NULL && *in_out_edit_count >= edit_capacity)) {
			return 0;
		}
		if (edits != NULL) {
			edits[*in_out_edit_count].output_start = output_cursor;
			edits[*in_out_edit_count].output_end = output_end;
			edits[*in_out_edit_count].source = source;
			edits[*in_out_edit_count].source_length = source_length;
			edits[*in_out_edit_count].source_start = source_cursor;
			edits[*in_out_edit_count].source_end = source_end;
		}
		(*in_out_edit_count)++;
	}
	return 1;
}

static int sqlparser_surface_ranges_equal(
	const char *left,
	size_t left_start,
	size_t left_end,
	const char *right,
	size_t right_start,
	size_t right_end)
{
	size_t left_length;
	size_t right_length;

	left_length = left_end - left_start;
	right_length = right_end - right_start;
	return left_length == right_length &&
		(left_length == 0U ||
		 memcmp(left + left_start, right + right_start, left_length) == 0);
}

static sqlparser_status_t sqlparser_surface_apply_edits(
	char **in_out_sql,
	size_t output_length,
	const sqlparser_surface_edit_t *edits,
	size_t edit_count,
	size_t limit,
	sqlparser_error_t *out_error)
{
	const char *output;
	char *next;
	size_t edit_index;
	size_t final_length;
	size_t output_cursor;
	size_t write_cursor;

	if (in_out_sql == NULL || *in_out_sql == NULL ||
	    (edit_count > 0U && edits == NULL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"surface edit arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (edit_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	output = *in_out_sql;
	if (output_length > limit) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"deparse output exceeds configured byte limit");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	final_length = output_length;
	output_cursor = 0U;
	for (edit_index = 0U; edit_index < edit_count; edit_index++) {
		const sqlparser_surface_edit_t *edit;
		size_t removed_length;
		size_t replacement_length;

		edit = &edits[edit_index];
		if (edit->source == NULL || edit->output_start < output_cursor ||
		    edit->output_end < edit->output_start ||
		    edit->output_end > output_length ||
		    edit->source_start > edit->source_length ||
		    edit->source_end < edit->source_start ||
		    edit->source_end > edit->source_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"surface edit interval is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		removed_length = edit->output_end - edit->output_start;
		replacement_length = edit->source_end - edit->source_start;
		if (removed_length > final_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"surface edit interval is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		final_length -= removed_length;
		if (replacement_length > limit - final_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"deparse output exceeds configured byte limit");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		final_length += replacement_length;
		output_cursor = edit->output_end;
	}
	if (final_length == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"deparse output exceeds configured byte limit");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	next = (char *)malloc(final_length + 1U);
	if (next == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	output_cursor = 0U;
	write_cursor = 0U;
	for (edit_index = 0U; edit_index < edit_count; edit_index++) {
		const sqlparser_surface_edit_t *edit;
		size_t copy_length;

		edit = &edits[edit_index];
		copy_length = edit->output_start - output_cursor;
		if (copy_length > 0U) {
			memcpy(next + write_cursor, output + output_cursor, copy_length);
			write_cursor += copy_length;
		}
		copy_length = edit->source_end - edit->source_start;
		if (copy_length > 0U) {
			memcpy(
				next + write_cursor,
				edit->source + edit->source_start,
				copy_length);
			write_cursor += copy_length;
		}
		output_cursor = edit->output_end;
	}
	if (output_cursor < output_length) {
		memcpy(
			next + write_cursor,
			output + output_cursor,
			output_length - output_cursor);
		write_cursor += output_length - output_cursor;
	}
	if (write_cursor != final_length) {
		free(next);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"surface edit output length is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	next[write_cursor] = '\0';
	free(*in_out_sql);
	*in_out_sql = next;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_surface_plan_statement_gap_edits(
	const sqlparser_handle_t *handle,
	size_t source_length,
	const char *output,
	size_t output_length,
	sqlparser_surface_edit_t *edits,
	size_t edit_capacity,
	size_t *out_edit_count)
{
	sqlparser_statement_scan_t output_statements;
	sqlparser_statement_scan_t source_statements;
	sqlparser_statement_span_t output_span;
	sqlparser_statement_span_t source_span;
	size_t edit_count;
	size_t output_cursor;
	size_t source_cursor;
	int output_found;
	int saw_body;
	int source_found;

	if (handle == NULL || handle->sql == NULL || output == NULL ||
	    out_edit_count == NULL) {
		return 0;
	}
	sqlparser_public_statement_scan_init(
		&source_statements,
		handle->dialect,
		handle->sql,
		source_length);
	sqlparser_public_statement_scan_init(
		&output_statements,
		handle->dialect,
		output,
		output_length);
	edit_count = 0U;
	source_cursor = 0U;
	output_cursor = 0U;
	saw_body = 0;
	for (;;) {
		source_found = sqlparser_public_statement_scan_next(
			&source_statements,
			&source_span);
		output_found = sqlparser_public_statement_scan_next(
			&output_statements,
			&output_span);
		if (source_found != output_found) {
			return 0;
		}
		if (!source_found) {
			break;
		}
		if (!sqlparser_public_statement_bodies_compatible(
			    handle->sql,
			    &source_span,
			    output,
			    &output_span) ||
		    !sqlparser_surface_plan_gap_edits(
			    handle->dialect,
			    handle->sql,
			    source_length,
			    source_cursor,
			    source_span.start,
			    output,
			    output_length,
			    output_cursor,
			    output_span.start,
			    edits,
			    edit_capacity,
			    &edit_count)) {
			return 0;
		}
		source_cursor = source_span.end;
		output_cursor = output_span.end;
		saw_body = 1;
	}
	if (!saw_body ||
	    !sqlparser_surface_plan_gap_edits(
		    handle->dialect,
		    handle->sql,
		    source_length,
		    source_cursor,
		    source_length,
		    output,
		    output_length,
		    output_cursor,
		    output_length,
		    edits,
		    edit_capacity,
		    &edit_count)) {
		return 0;
	}
	*out_edit_count = edit_count;
	return 1;
}

static sqlparser_status_t sqlparser_surface_try_restore_statement_gaps(
	const sqlparser_handle_t *handle,
	const sqlparser_terminal_scan_t *source_scan,
	const sqlparser_terminal_scan_t *output_scan,
	char **in_out_sql,
	int *out_handled,
	sqlparser_error_t *out_error)
{
	sqlparser_surface_edit_t *edits;
	size_t edit_count;
	size_t planned_count;
	sqlparser_status_t status;

	*out_handled = 0;
	if (sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	    !source_scan->has_batch_separator) {
		return SQLPARSER_STATUS_OK;
	}
	edit_count = 0U;
	if (!sqlparser_surface_plan_statement_gap_edits(
		    handle,
		    source_scan->length,
		    *in_out_sql,
		    output_scan->length,
		    NULL,
		    0U,
		    &edit_count)) {
		return SQLPARSER_STATUS_OK;
	}
	if (edit_count == 0U) {
		*out_handled = 1;
		return SQLPARSER_STATUS_OK;
	}
	if (edit_count > SIZE_MAX / sizeof(*edits)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"statement source envelope has too many intervals");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	edits = (sqlparser_surface_edit_t *)malloc(
		edit_count * sizeof(*edits));
	if (edits == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	planned_count = 0U;
	if (!sqlparser_surface_plan_statement_gap_edits(
		    handle,
		    source_scan->length,
		    *in_out_sql,
		    output_scan->length,
		    edits,
		    edit_count,
		    &planned_count) ||
	    planned_count != edit_count) {
		free(edits);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"statement source envelope plan is unstable");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	status = sqlparser_surface_apply_edits(
		in_out_sql,
		output_scan->length,
		edits,
		edit_count,
		handle->limits.max_output_bytes,
		out_error);
	free(edits);
	if (status == SQLPARSER_STATUS_OK) {
		*out_handled = 1;
	}
	return status;
}

static sqlparser_status_t sqlparser_surface_restore_source_edits(
	const sqlparser_handle_t *handle,
	char **in_out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_surface_source_edits_t *edits;
	char *output;
	size_t final_length;
	size_t index;
	size_t source_cursor;
	size_t write_cursor;

	edits = &handle->surface_source_edits;
	final_length = handle->sql_len;
	if (final_length > handle->limits.max_output_bytes) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"deparse output exceeds configured byte limit");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	source_cursor = 0U;
	for (index = 0U; index < edits->count; index++) {
		const sqlparser_surface_source_edit_t *edit;
		size_t removed_length;

		edit = &edits->items[index];
		if (edit->replacement == NULL ||
		    edit->source_start < source_cursor ||
		    edit->source_end < edit->source_start ||
		    edit->source_end > handle->sql_len) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"source edit interval is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		removed_length = edit->source_end - edit->source_start;
		if (removed_length > final_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"source edit interval is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		final_length -= removed_length;
		if (edit->replacement_length >
		    handle->limits.max_output_bytes - final_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"deparse output exceeds configured byte limit");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		final_length += edit->replacement_length;
		source_cursor = edit->source_end;
	}
	if (final_length > handle->limits.max_output_bytes ||
	    final_length == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"deparse output exceeds configured byte limit");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	output = (char *)malloc(final_length + 1U);
	if (output == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	source_cursor = 0U;
	write_cursor = 0U;
	for (index = 0U; index < edits->count; index++) {
		const sqlparser_surface_source_edit_t *edit;
		size_t copy_length;

		edit = &edits->items[index];
		copy_length = edit->source_start - source_cursor;
		if (copy_length > 0U) {
			memcpy(
				output + write_cursor,
				handle->sql + source_cursor,
				copy_length);
			write_cursor += copy_length;
		}
		if (edit->replacement_length > 0U) {
			memcpy(
				output + write_cursor,
				edit->replacement,
				edit->replacement_length);
			write_cursor += edit->replacement_length;
		}
		source_cursor = edit->source_end;
	}
	if (source_cursor < handle->sql_len) {
		memcpy(
			output + write_cursor,
			handle->sql + source_cursor,
			handle->sql_len - source_cursor);
		write_cursor += handle->sql_len - source_cursor;
	}
	if (write_cursor != final_length) {
		free(output);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"source edit output length is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	output[write_cursor] = '\0';
	free(*in_out_sql);
	*in_out_sql = output;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_restore_source_envelope(
	const sqlparser_handle_t *handle,
	char **in_out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_surface_edit_t single_edit;
	sqlparser_surface_edit_t *edits;
	sqlparser_terminal_scan_t output_scan;
	sqlparser_terminal_scan_t source_scan;
	size_t edit_count;
	size_t output_cursor;
	size_t planned_count;
	size_t source_cursor;
	int statement_handled;
	sqlparser_status_t status;

	if (handle == NULL || handle->sql == NULL || in_out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source envelope arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (handle->surface_source_complete) {
		return sqlparser_surface_restore_source_edits(
			handle,
			in_out_sql,
			out_error);
	}
	if (*in_out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source envelope SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (!sqlparser_public_terminal_scan(
		    handle->dialect,
		    handle->sql,
		    &source_scan) ||
	    !sqlparser_public_terminal_scan(
		    handle->dialect,
		    *in_out_sql,
		    &output_scan) ||
	    source_scan.length != handle->sql_len) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"source envelope has no statement body");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	status = sqlparser_surface_try_restore_statement_gaps(
		handle,
		&source_scan,
		&output_scan,
		in_out_sql,
		&statement_handled,
		out_error);
	if (status != SQLPARSER_STATUS_OK || statement_handled) {
		return status;
	}
	if (sqlparser_surface_ranges_equal(
		    handle->sql,
		    source_scan.body_end,
		    source_scan.length,
		    *in_out_sql,
		    output_scan.body_end,
		    output_scan.length)) {
		return SQLPARSER_STATUS_OK;
	}

	edits = NULL;
	edit_count = 0U;
	if (source_scan.semantic_count == output_scan.semantic_count &&
	    source_scan.semantic_count > 0U &&
	    sqlparser_surface_plan_gap_edits(
		    handle->dialect,
		    handle->sql,
		    source_scan.length,
		    source_scan.body_end,
		    source_scan.length,
		    *in_out_sql,
		    output_scan.length,
		    output_scan.body_end,
		    output_scan.length,
		    NULL,
		    0U,
		    &edit_count)) {
		if (edit_count == 0U) {
			return SQLPARSER_STATUS_OK;
		}
		if (edit_count > SIZE_MAX / sizeof(*edits)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"source envelope has too many intervals");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		edits = (sqlparser_surface_edit_t *)malloc(
			edit_count * sizeof(*edits));
		if (edits == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		planned_count = 0U;
		if (!sqlparser_surface_plan_gap_edits(
			    handle->dialect,
			    handle->sql,
			    source_scan.length,
			    source_scan.body_end,
			    source_scan.length,
			    *in_out_sql,
			    output_scan.length,
			    output_scan.body_end,
			    output_scan.length,
			    edits,
			    edit_count,
			    &planned_count) ||
		    planned_count != edit_count) {
			free(edits);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"source envelope plan is unstable");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}

	if (edits == NULL) {
		source_cursor = source_scan.last_semantic_end > 0U ?
			source_scan.last_semantic_end : source_scan.body_end;
		output_cursor = output_scan.last_semantic_end > 0U ?
			output_scan.last_semantic_end : output_scan.body_end;
		if (sqlparser_surface_ranges_equal(
			    handle->sql,
			    source_cursor,
			    source_scan.length,
			    *in_out_sql,
			    output_cursor,
			    output_scan.length)) {
			return SQLPARSER_STATUS_OK;
		}
		single_edit.output_start = output_cursor;
		single_edit.output_end = output_scan.length;
		single_edit.source = handle->sql;
		single_edit.source_length = source_scan.length;
		single_edit.source_start = source_cursor;
		single_edit.source_end = source_scan.length;
		edits = &single_edit;
		edit_count = 1U;
	}
	status = sqlparser_surface_apply_edits(
		in_out_sql,
		output_scan.length,
		edits,
		edit_count,
		handle->limits.max_output_bytes,
		out_error);
	if (edits != &single_edit) {
		free(edits);
	}
	return status;
}
