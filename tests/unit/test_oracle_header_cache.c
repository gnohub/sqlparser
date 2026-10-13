/* Ordinary header-template/root differential; immutable input owners stay
 * alive for every cache use. No allocation hooks, faults, or clone calls. */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
/* Constructor-gates header/root path: complete original scans and ordinary
 * constructors, with only private definition/call names changed. The frozen
 * root and INTO entry never use the current implementation header cache.
 */

static int headercache_frozen_list_bounds_append(
	sqlparser_oracle_list_bounds_t *bounds, size_t end)
{
	if (bounds->count >= SQLPARSER_ORACLE_LIST_BOUND_CAPACITY ||
	    end < bounds->start ||
	    (bounds->count != 0U && end <= bounds->ends[bounds->count - 1U])) {
		bounds->count = 0U;
		return 0;
	}
	bounds->ends[bounds->count++] = end;
	return 1;
}

static int headercache_frozen_list_bounds_usable(
	const sqlparser_oracle_list_bounds_t *bounds,
	int kind, size_t start, size_t close)
{
	return bounds != NULL && bounds->complete && bounds->kind == kind &&
		bounds->start == start && bounds->close == close &&
		bounds->count != 0U &&
		bounds->count <= SQLPARSER_ORACLE_LIST_BOUND_CAPACITY &&
		bounds->ends[bounds->count - 1U] == close;
}

static int headercache_frozen_find_matching_paren_with_bounds(
	const char *sql,
	size_t open_pos,
	size_t end,
	size_t *out_close,
	int kind,
	sqlparser_oracle_list_bounds_t *bounds)
{
	size_t pos;
	size_t depth;
	int collect;
	int name_state = 0;

	if (bounds != NULL) {
		bounds->complete = 0;
		bounds->count = 0U;
		bounds->start = 0U;
		bounds->close = 0U;
		bounds->kind = kind;
	}
	if (sql == NULL || out_close == NULL || open_pos >= end || sql[open_pos] != '(') {
		return 0;
	}
	collect = bounds != NULL &&
		(kind == SQLPARSER_ORACLE_LIST_COLUMNS || kind == SQLPARSER_ORACLE_LIST_VALUES);
	pos = open_pos + 1U;
	if (bounds != NULL) bounds->start = pos;
	depth = 1U;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_quote_or_comment_may_start((unsigned char)sql[pos]) ?
			sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			/* Keep every original helper jump, even when it crosses end.
			 * Only ordinary quoted VALUES text is admitted by this cache. */
			if (collect && (kind == SQLPARSER_ORACLE_LIST_COLUMNS ||
			    sql[pos] != '\'' || skipped > end)) {
				collect = 0;
				bounds->count = 0U;
			}
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			depth--;
			if (depth == 0U) {
				*out_close = pos;
				if (collect && kind == SQLPARSER_ORACLE_LIST_COLUMNS && name_state == 0) {
					collect = 0;
					bounds->count = 0U;
				}
				if (collect && headercache_frozen_list_bounds_append(bounds, pos)) {
					bounds->close = pos;
					bounds->complete = 1;
				}
				return 1;
			}
		}
		if (collect && kind == SQLPARSER_ORACLE_LIST_COLUMNS) {
			/* The legacy column splitter has no parenthesis-depth condition.
			 * Admit only wholly safe names, so its proof would stay unchanged. */
			if (sql[pos] == ',') {
				if (name_state == 0 || !headercache_frozen_list_bounds_append(bounds, pos))
					collect = 0;
				name_state = 0;
			} else if (!sqlparser_oracle_span_name_byte((unsigned char)sql[pos], 0, &name_state)) {
				collect = 0;
			}
			if (!collect) bounds->count = 0U;
		} else if (collect) {
			unsigned char ch = (unsigned char)sql[pos];
			/* Complex quote/comment forms and locale-sensitive unquoted text
			 * retain the complete original second scan. Round depth is shared. */
			if (ch >= 0x80U || ch == '$' || ch == '[' || ch == ']' ||
			    ch == '{' || ch == '}' || ch == '\\' || ch == '`' || ch == '"') {
				collect = 0;
				bounds->count = 0U;
			} else if (ch == ',' && depth == 1U) {
				collect = headercache_frozen_list_bounds_append(bounds, pos);
			}
		}
		pos++;
	}
	if (bounds != NULL) bounds->count = 0U;
	return 0;
}

static sqlparser_status_t headercache_frozen_parse_column_list_with_bounds(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_column_t **out_columns,
	size_t *out_count,
	int *span_safe,
	const sqlparser_oracle_list_bounds_t *bounds,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
	size_t boundary_index = 0U;
	int use_bounds;
	int name_state = 0;

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
	use_bounds = headercache_frozen_list_bounds_usable(
		bounds, SQLPARSER_ORACLE_LIST_COLUMNS, start, end);
	while (pos <= end) {
		int at_end;
		size_t skipped;

		if (use_bounds) {
			pos = bounds->ends[boundary_index++];
			at_end = pos == end;
		} else {
			at_end = pos == end;
			skipped = !at_end ? sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
			if (skipped > pos) {
				*span_safe = 0;
				pos = skipped;
				continue;
			}
			if (!at_end && sql[pos] != ',' &&
			    !sqlparser_oracle_span_name_byte((unsigned char)sql[pos], 0, &name_state)) *span_safe = 0;
		}
		if (at_end || sql[pos] == ',') {
			size_t item_end;
			sqlparser_dialect_multi_insert_column_t *next;

			/* A cached column list proved every original name check safe.
			 * Never promote an already-false caller proof. */
			if (!use_bounds && name_state == 0) *span_safe = 0;
			name_state = 0;
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
				free(columns[count].name);
				free(columns[count].sql);
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

static sqlparser_status_t headercache_frozen_parse_multi_insert_into(
	const char *sql,
	size_t *io_pos,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_t *multi,
	const char *condition_public_sql,
	const char *condition_parser_sql,
	int is_else,
	size_t condition_group_id,
	int *span_safe,
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
	int relation_name_state = 0;
	sqlparser_oracle_list_bounds_t list_bounds;

	memset(&branch, 0, sizeof(branch));
	pos = sqlparser_oracle_span_trim_left(sql, *io_pos, end, span_safe);
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "into")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_oracle_span_trim_left(sql, pos + strlen("into"), end, span_safe);
	relation_start = pos;
	columns_open = end;
	values_pos = end;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_quote_or_comment_may_start((unsigned char)sql[pos]) ?
			sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			*span_safe = 0;
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
		if (!sqlparser_oracle_span_name_byte((unsigned char)sql[pos], 1, &relation_name_state))
			*span_safe = 0;
		pos++;
	}
	if (relation_name_state == 0) *span_safe = 0;
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
		if (!headercache_frozen_find_matching_paren_with_bounds(sql, columns_open, end, &columns_close,
			SQLPARSER_ORACLE_LIST_COLUMNS, &list_bounds)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = headercache_frozen_parse_column_list_with_bounds(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			span_safe,
			&list_bounds,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			return status;
		}
		pos = sqlparser_oracle_span_trim_left(sql, columns_close + 1U, end, span_safe);
	} else {
		pos = values_pos;
	}
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (pos <= UINT32_MAX) branch.oracle_values_position = (uint32_t)pos;
	else *span_safe = 0;
	pos = sqlparser_oracle_span_trim_left(sql, pos + strlen("values"), end, span_safe);
	if (pos >= end || sql[pos] != '(') {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list expected '('");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	values_open = pos;
	if (!headercache_frozen_find_matching_paren_with_bounds(sql, values_open, end, &values_close,
		SQLPARSER_ORACLE_LIST_VALUES, &list_bounds)) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (multi->oracle_spans_complete && multi->oracle_span_count < UINT32_MAX)
		branch.oracle_span_base = (uint32_t)multi->oracle_span_count;
	status = sqlparser_oracle_parse_value_list_with_bounds(
		sql,
		values_open + 1U,
		values_close,
		state,
		multi,
		&branch.cells,
		&branch.cell_count,
		span_safe,
		&list_bounds,
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
	if (condition_public_sql != NULL || is_else || condition_group_id != 0U) {
		multi->oracle_spans_identity = 0;
		*span_safe = 0;
	}
	status = sqlparser_oracle_multi_insert_add_branch(multi, &branch, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	*io_pos = values_close + 1U;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t headercache_frozen_parse_multi_insert(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_parser_sql,
	int nested_block_comments,
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
	int span_safe;

	if (out_parser_sql == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert parser arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (!sqlparser_oracle_is_multi_insert_start(input_sql, &mode, nested_block_comments)) {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	span_safe = mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL;
	len = strlen(input_sql);
	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, 0U, len, nested_block_comments, &span_safe);
	end = sqlparser_oracle_span_trim_right(input_sql, pos, len, &span_safe);
	if (end > pos && input_sql[end - 1U] == ';') {
		end = sqlparser_oracle_span_trim_right(input_sql, pos, end - 1U, &span_safe);
	}
	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
		input_sql, pos + strlen("insert"), end, nested_block_comments, &span_safe);
	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
		input_sql,
		pos + (mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL ? strlen("all") : strlen("first")),
		end,
		nested_block_comments, &span_safe);

	multi = (sqlparser_dialect_multi_insert_t *)calloc(1U, sizeof(*multi));
	if (multi == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	multi->mode = mode;
	multi->oracle_spans_complete = len <= UINT32_MAX;
	multi->oracle_spans_identity = mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL;
	multi->oracle_outer_identity = 1;
	condition_group_id = 0U;

	while (pos < end) {
		pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
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

			condition_start = sqlparser_oracle_skip_leading_trivia_with_span_proof(
				input_sql, pos + strlen("when"), end, nested_block_comments, &span_safe);
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
			pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
				input_sql, then_pos + strlen("then"), end, nested_block_comments, &span_safe);
			condition_group_id++;
			do {
				status = headercache_frozen_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					condition_public,
					condition_parser,
					0,
					condition_group_id,
					&span_safe,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(condition_public);
					free(condition_parser);
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
			} while (pos < end &&
			         sqlparser_oracle_ascii_word_equal(input_sql, pos, "into"));
			free(condition_public);
			free(condition_parser);
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "else")) {
			pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
				input_sql, pos + strlen("else"), end, nested_block_comments, &span_safe);
			condition_group_id++;
			while (pos < end && sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
				status = headercache_frozen_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					NULL,
					NULL,
					1,
					condition_group_id,
					&span_safe,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
			}
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
			status = headercache_frozen_parse_multi_insert_into(
				input_sql,
				&pos,
				end,
				state,
				multi,
				NULL,
				NULL,
				0,
				0U,
				&span_safe,
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

	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
	if (multi->branch_count == 0U || pos >= end || !sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT requires branches and a source SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_oracle_trimmed_slice_dup(input_sql, pos, end, &multi->source_public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_preprocess_text_with_span_proof(multi->source_public_sql, state, &multi->source_parser_sql, &span_safe, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}
	if (multi->oracle_spans_complete) {
		/* Full constructor input was bounded before any uint32 conversion. */
		multi->oracle_source_start = (uint32_t)pos;
		multi->oracle_source_length = (uint32_t)(end - pos);
		if (strcmp(multi->source_public_sql, multi->source_parser_sql) != 0)
			multi->oracle_spans_identity = 0;
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

	/* Publish only a complete proof. Optional span OOM never changes parsing. */
	multi->oracle_generic_spans_equivalent = span_safe &&
		multi->oracle_spans_complete && multi->oracle_spans_identity &&
		sqlparser_oracle_span_ascii_ctype();
	sqlparser_oracle_multi_insert_destroy(state->multi_insert);
	state->multi_insert = multi;
	*out_parser_sql = sqlparser_oracle_buffer_take(&parser);
	return *out_parser_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *fixture = "initialization", *active_locale = "C";
static size_t branch_checks, root_checks, matches, locale_checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "header cache line %d locale=%s case=%s: %s\n", __LINE__, active_locale, fixture, #x); abort(); } } while (0)
/* Standalone normal field/ownership comparisons; no old harness include. */
static sqlparser_error_t seeded_error(void)
{
    sqlparser_error_t e;
    memset(&e, 0, sizeof(e)); e.code = SQLPARSER_STATUS_UNSUPPORTED;
    e.cursor = 17; e.line = 19; e.column = 23;
    strcpy(e.message, "retained header-cache sentinel");
    return e;
}

static void same_error(const sqlparser_error_t *a, const sqlparser_error_t *b)
{
    CHECK(a->code == b->code && a->cursor == b->cursor &&
          a->line == b->line && a->column == b->column);
    CHECK(memcmp(a->message, b->message, sizeof(a->message)) == 0);
}

static void same_text(const char *a, const char *b)
{
    CHECK((a == NULL) == (b == NULL));
    if (a) CHECK(strcmp(a, b) == 0);
}

static void same_value(const sqlparser_dialect_multi_insert_value_t *a,
                       const sqlparser_dialect_multi_insert_value_t *b)
{
    same_text(a->public_sql, b->public_sql); same_text(a->parser_sql, b->parser_sql);
    CHECK(a->has_bind == b->has_bind && a->bind_kind == b->bind_kind &&
          a->bind_position == b->bind_position && a->has_bind_position == b->has_bind_position &&
          a->has_literal == b->has_literal);
    CHECK(memcmp(a->bind, b->bind, sizeof(a->bind)) == 0);
    CHECK(memcmp(a->bind_sql, b->bind_sql, sizeof(a->bind_sql)) == 0);
    same_text(a->literal_string_value, b->literal_string_value);
    same_text(a->literal_float_value, b->literal_float_value);
    CHECK(a->literal.kind == b->literal.kind && a->literal.integer_value == b->literal.integer_value &&
          a->literal.boolean_value == b->literal.boolean_value &&
          a->literal.quoted_identifier == b->literal.quoted_identifier);
    same_text(a->literal.string_value, b->literal.string_value);
    same_text(a->literal.float_value, b->literal.float_value);
    if (a->literal.string_value) CHECK(a->literal.string_value == a->literal_string_value);
    if (a->literal.float_value) CHECK(a->literal.float_value == a->literal_float_value);
    if (b->literal.string_value) CHECK(b->literal.string_value == b->literal_string_value);
    if (b->literal.float_value) CHECK(b->literal.float_value == b->literal_float_value);
}

static void same_state(const sqlparser_oracle_state_t *a, const sqlparser_oracle_state_t *b)
{
    const sqlparser_dialect_national_literals_t *an = &a->national_literals, *bn = &b->national_literals;
    const sqlparser_dialect_prepared_binds_t *ap = &a->prepared_binds, *bp = &b->prepared_binds;
    size_t i;
    CHECK(a->bind_count == b->bind_count && a->bind_capacity == b->bind_capacity &&
          a->bind_occurrence_count == b->bind_occurrence_count);
    for (i = 0; i < a->bind_count; i++) same_text(a->bind_names[i], b->bind_names[i]);
    CHECK(ap->count == bp->count && ap->capacity == bp->capacity &&
          ap->occurrence_count == bp->occurrence_count && ap->valid == bp->valid);
    for (i = 0; i < ap->count; i++) same_text(ap->names[i], bp->names[i]);
    CHECK(an->count == bn->count && an->capacity == bn->capacity &&
          an->literal_count == bn->literal_count && an->fragment_start == bn->fragment_start &&
          an->fragment_literal_base == bn->fragment_literal_base);
    for (i = 0; i < an->count; i++) {
        same_text(an->items[i].sql, bn->items[i].sql);
        same_text(an->items[i].surface_sql, bn->items[i].surface_sql);
        CHECK(an->items[i].ordinal == bn->items[i].ordinal);
        CHECK(an->items[i].owner == NULL && bn->items[i].owner == NULL);
    }
    CHECK(a->dblink_count == b->dblink_count && a->dblink_capacity == b->dblink_capacity &&
          a->next_dblink_id == b->next_dblink_id);
    for (i = 0; i < a->dblink_count; i++) {
        same_text(a->dblink_relations[i].parser_object_name, b->dblink_relations[i].parser_object_name);
        same_text(a->dblink_relations[i].public_object_name, b->dblink_relations[i].public_object_name);
        same_text(a->dblink_relations[i].public_link_name, b->dblink_relations[i].public_link_name);
        same_text(a->dblink_relations[i].public_object_sql, b->dblink_relations[i].public_object_sql);
        same_text(a->dblink_relations[i].public_link_sql, b->dblink_relations[i].public_link_sql);
    }
    CHECK(a->minuses.count == b->minuses.count && a->minuses.capacity == b->minuses.capacity &&
          a->minuses.except_count == b->minuses.except_count &&
          a->minuses.fragment_start == b->minuses.fragment_start &&
          a->minuses.fragment_except_base == b->minuses.fragment_except_base);
    for (i = 0; i < a->minuses.count; i++) CHECK(a->minuses.items[i].ordinal == b->minuses.items[i].ordinal);
}

static void same_relation(const sqlparser_dialect_multi_insert_relation_t *a,
                          const sqlparser_dialect_multi_insert_relation_t *b)
{
    same_text(a->database_name, b->database_name); same_text(a->schema_name, b->schema_name);
    same_text(a->table_name, b->table_name); same_text(a->link_name, b->link_name);
    same_text(a->link_sql, b->link_sql); same_text(a->sql, b->sql);
}

static void owned_relation(const sqlparser_dialect_multi_insert_relation_t *r)
{
    const char *parts[] = {r->database_name, r->schema_name, r->table_name, r->link_name, r->link_sql, r->sql};
    size_t i, j;
    for (i = 0; i < COUNT(parts); i++) for (j = i + 1U; j < COUNT(parts); j++)
        if (parts[i] && parts[j]) CHECK(parts[i] != parts[j]);
}

static void same_multi(const sqlparser_dialect_multi_insert_t *a, const sqlparser_dialect_multi_insert_t *b)
{
    size_t i, j;
    CHECK(a->branch_count == b->branch_count && a->branch_capacity == b->branch_capacity &&
          a->oracle_span_count == b->oracle_span_count && a->oracle_span_capacity == b->oracle_span_capacity &&
          a->oracle_spans_complete == b->oracle_spans_complete && a->oracle_spans_identity == b->oracle_spans_identity &&
          a->oracle_generic_spans_equivalent == b->oracle_generic_spans_equivalent);
    for (i = 0; i < a->branch_count; i++) {
        const sqlparser_dialect_multi_insert_branch_t *x = &a->branches[i], *y = &b->branches[i];
        CHECK(x->ordinal == y->ordinal && x->column_count == y->column_count && x->cell_count == y->cell_count &&
              x->has_condition == y->has_condition && x->is_else == y->is_else &&
              x->condition_group_id == y->condition_group_id && x->oracle_span_base == y->oracle_span_base &&
              x->oracle_values_position == y->oracle_values_position);
        same_relation(&x->relation, &y->relation); owned_relation(&x->relation); owned_relation(&y->relation);
        same_text(x->condition_public_sql, y->condition_public_sql); same_text(x->condition_parser_sql, y->condition_parser_sql);
        for (j = 0; j < x->column_count; j++) { same_text(x->columns[j].name, y->columns[j].name); same_text(x->columns[j].sql, y->columns[j].sql); }
        for (j = 0; j < x->cell_count; j++) same_value(&x->cells[j], &y->cells[j]);
    }
    for (i = 0; i < a->oracle_span_count; i++) CHECK(
        a->oracle_spans[i].source_start == b->oracle_spans[i].source_start &&
        a->oracle_spans[i].source_length == b->oracle_spans[i].source_length &&
        a->oracle_spans[i].lexical_flags == b->oracle_spans[i].lexical_flags);
}
static sqlparser_oracle_state_t *new_state(int spans)
{
    sqlparser_oracle_state_t *s = NULL; sqlparser_error_t e = seeded_error();
    CHECK(sqlparser_oracle_state_new(&s, &e) == SQLPARSER_STATUS_OK);
    s->multi_insert = calloc(1U, sizeof(*s->multi_insert)); CHECK(s->multi_insert);
    s->multi_insert->oracle_spans_complete = spans;
    s->multi_insert->oracle_spans_identity = 1; s->multi_insert->oracle_generic_spans_equivalent = 1;
    return s;
}

static void same_all(const sqlparser_oracle_state_t *a, const sqlparser_oracle_state_t *b)
{
    const sqlparser_dialect_multi_insert_t *x = a->multi_insert, *y = b->multi_insert;
    size_t i, j;
    same_state(a, b); CHECK((x == NULL) == (y == NULL)); if (!x) return;
    same_multi(x, y);
    CHECK(x->mode == y->mode && x->oracle_source_start == y->oracle_source_start &&
        x->oracle_source_length == y->oracle_source_length && x->oracle_outer_identity == y->oracle_outer_identity);
    same_text(x->source_public_sql, y->source_public_sql); same_text(x->source_parser_sql, y->source_parser_sql);
    for (i = 1U; i < x->branch_count; i++) {
        CHECK(x->branches[i].relation.sql != x->branches[i - 1U].relation.sql);
        CHECK(y->branches[i].relation.sql != y->branches[i - 1U].relation.sql);
        for (j = 0; j < x->branches[i].column_count && j < x->branches[i - 1U].column_count; j++) {
            CHECK(x->branches[i].columns[j].sql != x->branches[i - 1U].columns[j].sql);
            CHECK(x->branches[i].columns[j].name != x->branches[i - 1U].columns[j].name);
            CHECK(y->branches[i].columns[j].sql != y->branches[i - 1U].columns[j].sql);
            CHECK(y->branches[i].columns[j].name != y->branches[i - 1U].columns[j].name);
        }
    }
}

static void verify_match(const sqlparser_oracle_multi_insert_header_cache_t *cache, const char *sql, size_t pos, size_t end)
{
    sqlparser_oracle_list_bounds_t actual, reference;
    size_t open = SIZE_MAX - 7U, close = open, expected = open, relation_start;
    int safe = 1, hit;
    relation_start = sqlparser_oracle_span_trim_left(sql, pos, end, &safe);
    relation_start = sqlparser_oracle_span_trim_left(sql, relation_start + 4U, end, &safe);
    memset(&actual, 0x5a, sizeof(actual)); reference = actual;
    hit = sqlparser_oracle_multi_insert_header_cache_match(cache, sql, relation_start, end, &open, &close, &actual);
    if (hit) {
        size_t i;
        CHECK(headercache_frozen_find_matching_paren_with_bounds(sql, open, end, &expected, SQLPARSER_ORACLE_LIST_COLUMNS, &reference));
        CHECK(close == expected && actual.count == reference.count && actual.start == reference.start &&
            actual.close == reference.close && actual.kind == reference.kind && actual.complete == reference.complete);
        for (i = 0; i < actual.count; i++) CHECK(actual.ends[i] == reference.ends[i]);
        matches++;
    } else {
        CHECK(open == SIZE_MAX - 7U && close == SIZE_MAX - 7U);
        CHECK(!memcmp(&actual, &reference, sizeof(actual)));
    }
}

static void compare_into(sqlparser_oracle_state_t *a, sqlparser_oracle_state_t *b,
                         sqlparser_oracle_multi_insert_header_cache_t *cache, const char *sql,
                         size_t start, size_t end, int *span_safe, int null_error, int condition)
{
    sqlparser_error_t ae = seeded_error(), be = ae;
    sqlparser_status_t as, bs;
    size_t ap = start, bp = start;
    int af = *span_safe, bf = af, aerrno, berrno;
    fixture = sql;
    if (cache && !condition) verify_match(cache, sql, start, end);
    errno = ERANGE;
    bs = headercache_frozen_parse_multi_insert_into(sql, &bp, end, b, b->multi_insert,
        condition == 1 ? ":p > 0" : NULL, condition == 1 ? "$1 > 0" : NULL, condition == 2, condition ? 3U : 0U, &bf, null_error ? NULL : &be);
    berrno = errno; errno = ERANGE;
    if (condition) as = sqlparser_oracle_parse_multi_insert_into(sql, &ap, end, a, a->multi_insert,
        condition == 1 ? ":p > 0" : NULL, condition == 1 ? "$1 > 0" : NULL, condition == 2, 3U, &af, null_error ? NULL : &ae);
    else as = sqlparser_oracle_parse_multi_insert_into_with_header_cache(sql, &ap, end, a, a->multi_insert,
        NULL, NULL, 0, 0U, &af, cache, NULL, null_error ? NULL : &ae);
    aerrno = errno;
    CHECK(as == bs && ap == bp && af == bf && aerrno == berrno); same_error(&ae, &be); same_all(a, b);
    *span_safe = af; branch_checks++;
}

static void sequences(void)
{
    static const struct { const char *sql; size_t start; } input[] = {
        {"into \"quoted\"(a,b) values (1,2)", 0U},
        {"into t(a,b) VALUE (1,2)", 0U},
        {"into t(a,b) values (N'n',:p)", 0U},
        {"padding ; into t(a,b) values ('next',2)", 10U},
        {"into t( a,b) values (1,2)", 0U},
        {"into u(a,b) values (1,2)", 0U},
        {"into t(a,b) VALUE (1,2)", 0U},
        {"into t(a,b) values 1,2", 0U},
        {"into t(a,b) values (1,2", 0U},
        {"into t(a", 0U},
        {"into t(a,b)) values (1,2)", 0U},
        {"into t(a,b) /*after header*/ values (1,2)", 0U},
        {"into t(a,b) values (q'[x]',coalesce(:p,1))", 0U},
        {"into t() values (1)", 0U},
        {"into t(a,) values (1,2)", 0U},
        {"into t(a,\"b\") values (1,2)", 0U},
        {"into t/*comment*/(a,b) values (1,2)", 0U},
        {"into t@link(a,b) values (1,2)", 0U},
        {"into t#x(a,b) values (1,2)", 0U},
        {"into \335t(a,b) values (1,2)", 0U},
        {"into myvalues(a,b) values (1,2)", 0U},
        {"into t values (1,2)", 0U},
        {"into t(a,b) values ('after',42)", 0U}
    };
    int enabled, spans, null_error, seed;
    size_t i;
    for (enabled = 0; enabled <= 1; enabled++) for (spans = 0; spans <= 1; spans++)
    for (null_error = 0; null_error <= 1; null_error++) for (seed = 0; seed <= 1; seed++) {
        sqlparser_oracle_state_t *a = new_state(spans), *b = new_state(spans);
        sqlparser_oracle_multi_insert_header_cache_t cache;
        int safe = seed, use_cache = enabled && sqlparser_oracle_span_ascii_ctype();
        memset(&cache, 0, sizeof(cache));
        for (i = 0; i < COUNT(input); i++) {
            compare_into(a, b, use_cache ? &cache : NULL, input[i].sql, input[i].start, strlen(input[i].sql), &safe, null_error, 0);
            if (i < 2U) CHECK(cache.header == NULL); /* Failed current implementation cannot publish. */
            if (i >= 2U && use_cache) CHECK(cache.header == strstr(input[2].sql, "t(a,b)") && cache.column_bounds.count == 2U);
        }
        compare_into(a, b, &cache, input[3].sql, input[3].start, strlen(input[3].sql), &safe, null_error, 1);
        compare_into(a, b, &cache, input[3].sql, input[3].start, strlen(input[3].sql), &safe, null_error, 2);
        sqlparser_oracle_state_destroy(a); sqlparser_oracle_state_destroy(b);
    }
}

static char *make_branch(size_t columns, int alternate)
{
    char *s = malloc(4096U); size_t i, used = 0; int n;
    CHECK(s); n = snprintf(s, 4096U, alternate ? "padding ; into t(" : "into t("); CHECK(n > 0); used = (size_t)n;
    for (i = 0; i < columns; i++) {
        n = snprintf(s + used, 4096U - used, "%sc%zu", i ? "," : "", i); CHECK(n > 0 && (size_t)n < 4096U - used); used += (size_t)n;
    }
    n = snprintf(s + used, 4096U - used, ") values ("); CHECK(n > 0); used += (size_t)n;
    for (i = 0; i < columns; i++) { n = snprintf(s + used, 4096U - used, "%s%zu", i ? "," : "", alternate ? i + 1U : i); CHECK(n > 0); used += (size_t)n; }
    strcpy(s + used, ")"); return s;
}

static void capacities(void)
{
    static const size_t sizes[] = {1U, 4U, 31U, 32U, 33U};
    size_t i;
    for (i = 0; i < COUNT(sizes); i++) {
        char *first = make_branch(sizes[i], 0), *second = make_branch(sizes[i], 1);
        sqlparser_oracle_state_t *a = new_state(1), *b = new_state(1);
        sqlparser_oracle_multi_insert_header_cache_t cache;
        int safe = 0, eligible = sqlparser_oracle_span_ascii_ctype();
        memset(&cache, 0, sizeof(cache));
        compare_into(a, b, eligible ? &cache : NULL, first, 0U, strlen(first), &safe, 0, 0);
        CHECK((cache.header != NULL) == (eligible && sizes[i] <= 32U));
        compare_into(a, b, eligible ? &cache : NULL, second, 10U, strlen(second), &safe, 0, 0);
        /* Both input allocations stayed immutable until the final cache use. */
        memset(first, '#', strlen(first)); memset(second, '#', strlen(second)); same_all(a, b);
        free(first); free(second); sqlparser_oracle_state_destroy(a); sqlparser_oracle_state_destroy(b);
    }
    fixture = "capacity cases complete";
}

static void roots(void)
{
    static const char *const sqls[] = {
        "insert all into t(a,b) values ('x',1) into t(a,b) values ('y',2) select 1 from dual",
        "insert all into \"q\"(a,b) values ('x',1) into t(a,b) values ('y',2) into t(a,b) values ('z',3) select 1 from dual",
        "insert all into t(a,b) values (:p,N'x') into t(a,b) values (:p,'y') select 1 from dual",
        "insert first when 1=1 then into t(a) values (1) into t(a) values (2) else into t(a) values (3) select 1 from dual",
        "insert all when 1=1 then into t(a) values (1) else into t(a) values (2) select 1 from dual",
        "insert all into t(a,b) values (1,2) into t(a,b) VALUE (3,4) select 1 from dual",
        "insert all into t(a,b) values (1,2) into t(a,b) values (3,4 select 1 from dual",
        "insert all into t(a,b) values (1,2) into t(a,b) values (3,4)",
        "insert all into t(a,b) values (5,6) into t(a,b) values (7,8) select 1 from dual"
    };
    sqlparser_oracle_state_t *a = new_state(1), *b = new_state(1);
    size_t i; int null_error, nested;
    for (nested = 0; nested <= 1; nested++) for (null_error = 0; null_error <= 1; null_error++) for (i = 0; i < COUNT(sqls); i++) {
        sqlparser_error_t ae = seeded_error(), be = ae;
        sqlparser_status_t as, bs; char *ap = NULL, *bp = NULL;
        fixture = sqls[i];
        bs = headercache_frozen_parse_multi_insert(sqls[i], b, &bp, nested, null_error ? NULL : &be);
        as = sqlparser_oracle_parse_multi_insert(sqls[i], a, &ap, nested, null_error ? NULL : &ae);
        CHECK(as == bs); same_error(&ae, &be); same_text(ap, bp); same_all(a, b);
        free(ap); free(bp); root_checks++;
    }
    sqlparser_oracle_state_destroy(a); sqlparser_oracle_state_destroy(b);
}

int main(void)
{
    static const char *const names[] = {"C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "tr_TR", "en_US", "English_United States.1252", "Turkish_Turkey.1254"};
    size_t i;
    for (i = 0; i < COUNT(names); i++) {
        if (!setlocale(LC_CTYPE, names[i])) { CHECK(i != 0U); continue; }
        active_locale = names[i]; locale_checks++; sequences(); capacities(); roots();
    }
    CHECK(setlocale(LC_CTYPE, "C")); CHECK(matches > 0U);
    printf("Oracle header cache: %zu branch, %zu full-root comparisons, %zu independently checked relocated header matches; %zu locales passed\n",
        branch_checks, root_checks, matches, locale_checks);
    return 0;
}
