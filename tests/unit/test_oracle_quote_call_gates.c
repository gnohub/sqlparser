/* The real view TU versus independently frozen accepted callers and a scalar
 * quote oracle. Instrumentation calls through; it never replaces a result. */
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "../../src/dialect/sqlparser_dialect_oracle_internal.h"
#include "sqlparser_reference_surface_scanner.inc"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static size_t comparisons, primitive_comparisons, removed_calls, retained_calls;
static const char *stage = "initialization";
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "quote caller gates %s line %d case=%zu: %s\n", stage, __LINE__, comparisons, #x); abort(); } } while (0)

typedef struct { int dialect, site; size_t pos, result; unsigned char byte; } event;
static event events[2][8192];
static size_t event_count[2], misses[2][4];
static int tracing, side, initial_certificate_admitted;
static int site_id(const char *name)
{
    if (strstr(name, "public_statement_span_in_sql")) return 1;
    if (strstr(name, "multi_insert_cell_source_span")) return 2;
    if (strstr(name, "view_insert_cell_source_span")) return 3;
    return 0;
}
static size_t record_call(sqlparser_dialect_t dialect, const char *sql,
                          size_t pos, size_t result, const char *name)
{
    int site = site_id(name);
    unsigned char byte;
    if (!tracing || sql == NULL) return result;
    byte = (unsigned char)sql[pos];
    /* Independent trace classification, never the current implementation predicate. Every
     * possible opener must keep its exact call order, index and return value. */
    if (byte != 0U && strchr("$qQnN-#/[`'\"", byte) != NULL) {
        event *e;
        CHECK(event_count[side] < COUNT(events[side]));
        e = &events[side][event_count[side]++];
        e->dialect = dialect; e->site = site; e->pos = pos;
        e->result = result; e->byte = byte;
    } else {
        CHECK(result == pos);
        misses[side][site]++;
    }
    return result;
}
static size_t candidate_probe(sqlparser_dialect_t d, const char *sql, size_t p, const char *name)
{ return record_call(d, sql, p, sqlparser_public_skip_quoted_or_comment(d, sql, p), name); }
static size_t reference_probe(sqlparser_dialect_t d, const char *sql, size_t p, const char *name)
{ return record_call(d, sql, p, reference_skip_quoted_or_comment(d, sql, p), name); }

/* Observe the new complete-proof path without changing its result. This
 * older test still compares every scanner call that the proof did not elide. */
static int initial_span_probe(const sqlparser_handle_t *h, size_t row, size_t column,
    size_t *ss, size_t *se, size_t *vp, size_t *cs, size_t *ce)
{
    int result = sqlparser_oracle_multi_insert_initial_cell_span(h, row, column, ss, se, vp, cs, ce);
    if (tracing && side == 1 && result) initial_certificate_admitted = 1;
    return result;
}
#define sqlparser_oracle_multi_insert_initial_cell_span initial_span_probe
#define sqlparser_public_skip_quoted_or_comment(d, s, p) candidate_probe((d), (s), (p), __func__)
#include "../../src/core/sqlparser_view.c"
#undef sqlparser_oracle_multi_insert_initial_cell_span
#undef sqlparser_public_skip_quoted_or_comment
#define sqlparser_public_skip_quoted_or_comment(d, s, p) reference_probe((d), (s), (p), __func__)
/* Frozen accepted scangates bodies. Only function linkage and the three
 * names/internal references differ; never use the current implementation byte gate here. */

static int quotegates_frozen_sqlparser_view_public_statement_span_in_sql(
	const sqlparser_handle_t *handle,
	const char *sql,
	int use_current_control_offsets,
	size_t statement_index,
	size_t *out_start,
	size_t *out_end)
{
	size_t after_go;
	size_t current;
	size_t end;
	size_t pos;
	size_t skipped;
	size_t start;
	int sqlserver_go;

	if (handle == NULL || sql == NULL || out_start == NULL ||
	    out_end == NULL) {
		return 0;
	}
	if (handle->control != NULL) {
		const sqlparser_control_unit_t *unit;

		if (statement_index >= handle->control->unit_count) {
			return 0;
		}
		unit = &handle->control->units[statement_index];
		if (use_current_control_offsets) {
			start = unit->current_offset;
			if (unit->current_length > (size_t)-1 - start) {
				return 0;
			}
			end = start + unit->current_length;
		} else {
			if (unit->source_offset > handle->sql_len ||
			    unit->source_length > handle->sql_len - unit->source_offset) {
				return 0;
			}
			start = unit->source_offset;
			end = start + unit->source_length;
		}
		start = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			start);
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
		*out_start = start;
		*out_end = end;
		return start < end;
	}

	/* Incompatible dialects make every GO probe a side-effect-free miss. */
	sqlserver_go = sqlparser_dialect_is_sqlserver_compatible(handle->dialect);
	current = 0U;
	pos = 0U;
	while (sql[pos] != '\0') {
		pos = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos);
		for (;;) {
			if (sql[pos] == ';') {
				pos++;
			} else if (sqlserver_go && sqlparser_public_sqlserver_go_at(
					   handle->dialect,
					   sql,
					   pos,
					   &after_go)) {
				pos = after_go;
			} else {
				break;
			}
			pos = sqlparser_public_skip_trivia(
				handle->dialect,
				sql,
				pos);
		}
		if (sql[pos] == '\0') {
			break;
		}
		start = pos;
		while (sql[pos] != '\0' && sql[pos] != ';') {
			if (sqlserver_go && sqlparser_public_sqlserver_go_at(
				    handle->dialect,
				    sql,
				    pos,
				    &after_go)) {
				break;
			}
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			pos = skipped != pos ? skipped : pos + 1U;
		}
		end = pos;
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
		if (start < end) {
			if (current == statement_index) {
				*out_start = start;
				*out_end = end;
				return 1;
			}
			current++;
		}
		if (sql[pos] == ';') {
			pos++;
		} else if (sqlserver_go && sqlparser_public_sqlserver_go_at(
				   handle->dialect,
				   sql,
				   pos,
				   &after_go)) {
			pos = after_go;
		}
	}
	return 0;
}

static int quotegates_frozen_sqlparser_view_multi_insert_cell_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_dialect_multi_insert_t *multi_insert,
	sqlparser_view_expression_source_cache_t *cache,
	size_t statement_start,
	size_t statement_end,
	size_t target_branch,
	size_t target_column,
	size_t *out_start,
	size_t *out_end)
{
	const sqlparser_dialect_multi_insert_branch_t *branch;
	const sqlparser_dialect_multi_insert_value_t *value;
	const char *sql;
	size_t brace_depth;
	size_t bracket_depth;
	size_t branch_index;
	size_t cell_end;
	size_t cell_index;
	size_t cell_start;
	size_t paren_depth;
	size_t pos;
	size_t skipped;
	size_t source_length;
	size_t source_start;
	size_t target_end;
	size_t target_start;
	size_t values_close;
	size_t values_open;
	size_t values_position;
	size_t target_values_position;
	int target_found;
	int validated;

	if (handle == NULL || multi_insert == NULL ||
	    multi_insert->branches == NULL ||
	    target_branch >= multi_insert->branch_count ||
	    multi_insert->source_public_sql == NULL ||
	    statement_start >= statement_end) {
		return 0;
	}
	sql = handle->sql;
	source_length = strlen(multi_insert->source_public_sql);
	if (source_length == 0U ||
	    source_length > statement_end - statement_start) {
		return 0;
	}
	source_start = statement_end - source_length;
	if (source_start <= statement_start ||
	    memcmp(
		    sql + source_start,
		    multi_insert->source_public_sql,
		    source_length) != 0 ||
	    !sqlparser_view_public_word_at(
		    sql,
		    source_start,
		    statement_end,
		    "select")) {
		return 0;
	}

	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		statement_start);
	if (!sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "insert")) {
		return 0;
	}
	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		pos + strlen("insert"));
	if (multi_insert->mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL &&
	    sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "all")) {
		pos += strlen("all");
	} else if (multi_insert->mode ==
			   SQLPARSER_DIALECT_MULTI_INSERT_FIRST &&
		   sqlparser_view_public_word_at(
			   sql,
			   pos,
			   source_start,
			   "first")) {
		pos += strlen("first");
	} else {
		return 0;
	}

	branch_index = 0U;
	validated = cache != NULL && cache->valid == 2;
	if (validated && target_branch >= cache->multi_branch_index) {
		branch_index = cache->multi_branch_index;
		pos = cache->resume;
	}
	paren_depth = 0U;
	target_end = 0U;
	target_found = 0;
	target_start = 0U;
	target_values_position = 0U;
	while (pos < source_start) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
		if (skipped != pos) {
			if (skipped > source_start) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			paren_depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (paren_depth == 0U) {
				return 0;
			}
			paren_depth--;
			pos++;
			continue;
		}
		if (paren_depth != 0U ||
		    !sqlparser_view_public_word_at(
			    sql,
			    pos,
			    source_start,
			    "values")) {
			pos++;
			continue;
		}
		values_position = pos;
		values_open = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos + strlen("values"));
		if (values_open >= source_start || sql[values_open] != '(') {
			pos += strlen("values");
			continue;
		}
		if (branch_index >= multi_insert->branch_count ||
		    (branch = &multi_insert->branches[branch_index])->ordinal !=
			    branch_index ||
		    branch->cells == NULL || branch->cell_count == 0U) {
			return 0;
		}

		cell_index = 0U;
		cell_start = sqlparser_public_skip_space(sql, values_open + 1U);
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		values_close = source_start;
		for (pos = cell_start; pos < source_start; pos++) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			if (skipped != pos) {
				if (skipped > source_start) {
					return 0;
				}
				pos = skipped - 1U;
				continue;
			}
			if (sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if ((sql[pos] != ',' && sql[pos] != ')') ||
			    paren_depth != 0U || bracket_depth != 0U ||
			    brace_depth != 0U) {
				continue;
			}
			cell_end = pos;
			while (cell_end > cell_start &&
			       isspace((unsigned char)sql[cell_end - 1U])) {
				cell_end--;
			}
			if (cell_end <= cell_start ||
			    cell_index >= branch->cell_count ||
			    (value = &branch->cells[cell_index])->public_sql == NULL ||
			    cell_end - cell_start != strlen(value->public_sql) ||
			    memcmp(
				    sql + cell_start,
				    value->public_sql,
				    cell_end - cell_start) != 0) {
				return 0;
			}
			if (branch_index == target_branch &&
			    cell_index == target_column) {
				target_start = cell_start;
				target_end = cell_end;
				target_found = 1;
				target_values_position = values_position;
			}
			cell_index++;
			if (sql[pos] == ')') {
				values_close = pos;
				break;
			}
			cell_start = sqlparser_public_skip_space(sql, pos + 1U);
			pos = cell_start - 1U;
		}
		if (values_close == source_start ||
		    cell_index != branch->cell_count) {
			return 0;
		}
		if (validated && target_found) {
			cache->resume = target_values_position;
			cache->multi_branch_index = target_branch;
			*out_start = target_start;
			*out_end = target_end;
			return 1;
		}
		branch_index++;
		paren_depth = 0U;
		pos = values_close + 1U;
	}
	if (paren_depth != 0U || branch_index != multi_insert->branch_count ||
	    !target_found) {
		return 0;
	}
	*out_start = target_start;
	*out_end = target_end;
	if (cache != NULL) {
		cache->valid = 2;
		cache->search_position = statement_start;
		cache->statement_end = statement_end;
		cache->multi_branch_index = target_branch;
		cache->resume = target_values_position;
	}
	return 1;
}

static int quotegates_frozen_sqlparser_view_insert_cell_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_surface_source_edits_t *surface_edits,
	sqlparser_view_expression_source_cache_t *cache,
	int allow_comments,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const sqlparser_dialect_multi_insert_t *multi_insert;
	const sqlparser_identifier_origin_map_t *origins;
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__Node *row_node;
	PgQuery__Node *statement;
	PgQuery__Node *value_node;
	PgQuery__SelectStmt *values_stmt;
	sqlparser_graph_insert_mode_t insert_mode;
	sqlparser_status_t status;
	size_t left_delimiter;
	size_t pos;
	size_t right_delimiter;
	size_t skipped;
	size_t statement_end;
	size_t statement_start;
	int source_status;

	if (out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell source span must not be NULL");
		return -1;
	}
	*out_start = 0U;
	*out_end = 0U;
	if (handle == NULL || handle->sql == NULL) {
		return 0;
	}
	multi_insert = sqlparser_dialect_is_oracle_or_dameng_compatible(handle->dialect) ?
		sqlparser_dialect_state_multi_insert(
			handle->dialect,
			handle->dialect_state) : NULL;
	if (multi_insert != NULL) {
		if (statement_index != 0U) return 0;
		if (cache != NULL && cache->valid == 2) {
			statement_start = cache->search_position;
			statement_end = cache->statement_end;
		} else if (!quotegates_frozen_sqlparser_view_public_statement_span_in_sql(
			    handle,
			    handle->sql,
			    0,
			    statement_index,
			    &statement_start,
			    &statement_end)) {
			return 0;
		}
		if (handle->dialect == SQLPARSER_DIALECT_DAMENG && (cache == NULL || cache->valid != 2)) {
			for (pos = statement_start; pos < statement_end;) {
				if (sqlparser_public_comment_at(
					    handle->dialect, handle->sql, pos)) {
					break;
				}
				skipped = sqlparser_public_skip_quoted_or_comment(
					handle->dialect, handle->sql, pos);
				pos = skipped > pos ? skipped : pos + 1U;
			}
			if (pos == statement_end) {
				return 0;
			}
		}
		/* The first generic scan validates every branch/cell, including its
		 * bracket/brace tokenization. Only then may current constructor spans
		 * replace repeated scans; identity alone does not prove that validation.
		 * Keep the common delimiter and comment checks below for every cell. */
		source_status = cache != NULL && cache->valid == 2 &&
			sqlparser_oracle_multi_insert_certified_cell_span(
				handle, row_index, column_index, out_start, out_end);
		if (!source_status) source_status = quotegates_frozen_sqlparser_view_multi_insert_cell_source_span(
			handle,
			multi_insert,
			cache,
			statement_start,
			statement_end,
			row_index,
			column_index,
			out_start,
			out_end);
		if (source_status <= 0) {
			return source_status;
		}
	} else {
		status = sqlparser_get_statement_node(
			handle,
			statement_index,
			&statement,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (statement->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
		    (insert_stmt = statement->insert_stmt) == NULL ||
		    sqlparser_insert_source_from_stmt(insert_stmt) !=
			    SQLPARSER_INSERT_SOURCE_VALUES ||
		    insert_stmt->select_stmt == NULL ||
		    (values_stmt = insert_stmt->select_stmt->select_stmt) == NULL) {
			return 0;
		}
		insert_mode = SQLPARSER_GRAPH_INSERT_MODE_VALUES;
		if (handle->dialect_ops != NULL &&
		    handle->dialect_ops->insert_mode != NULL) {
			insert_mode = handle->dialect_ops->insert_mode(
				handle->dialect_state,
				statement_index,
				insert_mode);
		}
		if (insert_mode != SQLPARSER_GRAPH_INSERT_MODE_VALUES &&
		    insert_mode != SQLPARSER_GRAPH_INSERT_MODE_REPLACE_VALUES) {
			return 0;
		}
		if (values_stmt->values_lists == NULL ||
		    row_index >= values_stmt->n_values_lists ||
		    (row_node = values_stmt->values_lists[row_index]) == NULL ||
		    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
		    row_node->list == NULL || row_node->list->items == NULL ||
		    column_index >= row_node->list->n_items ||
		    (value_node = row_node->list->items[column_index]) == NULL) {
			return 0;
		}
		statement_start = cache != NULL && cache->valid == 1 ? cache->statement_start : 0U;
		statement_end = cache != NULL && cache->valid == 1 ? cache->statement_end : 0U;
		origins = NULL;
		source_status = sqlparser_view_expression_source_span(
			handle,
			&origins,
			cache,
			value_node,
			surface_edits,
			out_start,
			out_end,
			out_error);
		if (source_status <= 0) {
			return source_status;
		}
		if ((statement_start >= statement_end || *out_start < statement_start || *out_end > statement_end) &&
		    !quotegates_frozen_sqlparser_view_public_statement_span_in_sql(
			    handle,
			    handle->sql,
			    0,
			    statement_index,
			    &statement_start,
			    &statement_end)) {
			*out_start = 0U;
			*out_end = 0U;
			return 0;
		}
		if (cache != NULL && cache->valid == 1) {
			cache->statement_start = statement_start;
			cache->statement_end = statement_end;
		}
	}
	if (*out_start < statement_start || *out_end > statement_end ||
	    *out_start >= *out_end) {
		*out_start = 0U;
		*out_end = 0U;
		return 0;
	}
	left_delimiter = *out_start;
	while (left_delimiter > statement_start &&
	       isspace((unsigned char)handle->sql[left_delimiter - 1U])) {
		left_delimiter--;
	}
	right_delimiter = *out_end;
	while (right_delimiter < statement_end &&
	       isspace((unsigned char)handle->sql[right_delimiter])) {
		right_delimiter++;
	}
	if (left_delimiter == statement_start ||
	    (handle->sql[left_delimiter - 1U] != '(' &&
	     handle->sql[left_delimiter - 1U] != ',') ||
	    right_delimiter >= statement_end ||
	    (handle->sql[right_delimiter] != ',' &&
	     handle->sql[right_delimiter] != ')')) {
		*out_start = 0U;
		*out_end = 0U;
		return 0;
	}
	for (pos = *out_start; pos < *out_end;) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped == pos) {
			pos++;
			continue;
		}
		if ((!allow_comments && sqlparser_public_comment_at(
			     handle->dialect, handle->sql, pos)) ||
		    skipped > *out_end) {
			*out_start = 0U;
			*out_end = 0U;
			return 0;
		}
		pos = skipped;
	}
	return 1;
}
#undef sqlparser_public_skip_quoted_or_comment

static void begin_comparison(void)
{
    event_count[0] = event_count[1] = 0U; initial_certificate_admitted = 0;
    memset(misses, 0, sizeof(misses)); tracing = 1; side = 0;
}
static void end_comparison(sqlparser_dialect_t d)
{
    size_t i, before = 0U, after = 0U;
    tracing = 0;
    {
        size_t candidate_index = 0U;
        for (i = 0U; i < event_count[0]; i++) {
            const event *a = &events[0][i], *b;
            if (initial_certificate_admitted && (a->site == 1 || a->site == 2)) continue;
            CHECK(candidate_index < event_count[1]);
            b = &events[1][candidate_index++];
            CHECK(a->dialect == b->dialect && a->site == b->site &&
                  a->pos == b->pos && a->result == b->result && a->byte == b->byte);
        }
        CHECK(candidate_index == event_count[1]);
    }
    CHECK(misses[1][1] == 0U && misses[1][2] == 0U);
    /* Dameng's separate comment-discovery loop is intentionally unchanged. */
    if (d != SQLPARSER_DIALECT_DAMENG) CHECK(misses[1][3] == 0U);
    CHECK(misses[0][0] == misses[1][0]);
    for (i = 1U; i < 4U; i++) { before += misses[0][i]; after += misses[1][i]; }
    CHECK(before >= after);
    removed_calls += before - after; retained_calls += event_count[1]; comparisons++;
}
static void same_cache(const sqlparser_view_expression_source_cache_t *a,
                       const sqlparser_view_expression_source_cache_t *b)
{
    CHECK(a->resume == b->resume && a->search_position == b->search_position &&
          a->last_location == b->last_location && a->valid == b->valid &&
          a->statement_end == b->statement_end && a->statement_start == b->statement_start);
}
static sqlparser_error_t sentinel_error(void)
{
    sqlparser_error_t e;
    memset(&e, 0, sizeof(e)); e.code = SQLPARSER_STATUS_UNSUPPORTED;
    e.cursor = 17; e.line = 23; e.column = 31; strcpy(e.message, "retained error sentinel");
    return e;
}
static void same_error(const sqlparser_error_t *a, const sqlparser_error_t *b)
{ CHECK(a->code == b->code && a->cursor == b->cursor && a->line == b->line && a->column == b->column && !strcmp(a->message, b->message)); }
static uint32_t random_state = UINT32_C(0xa7539421);
static uint32_t next_random(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }

static void primitive_text(const char *sql)
{
    int d; size_t p, n = strlen(sql);
    for (d = -1; d <= 13; d++) for (p = 0U; p <= n; p++) {
        size_t expected = reference_skip_quoted_or_comment((sqlparser_dialect_t)d, sql, p);
        size_t actual = sqlparser_view_public_quote_or_comment_byte((unsigned char)sql[p]) ?
            sqlparser_public_skip_quoted_or_comment((sqlparser_dialect_t)d, sql, p) : p;
        CHECK(expected == actual && actual >= p && actual <= n); primitive_comparisons++;
    }
}
static const char *const lexical_cases[] = {
    "", "-", "--", "--x\nnext", "-- \r\nnext", "--\001x", "#x\nnext", "/", "/*", "/*x*",
    "/*outer /*inner*/ outer*/", "/*'q[]'$$*/", "'", "''", "'a''b'", "\"a\"\"b\"", "`a``b`", "[a]]b]",
    "E'a\\'b'", "e'a\\", "xE'a\\'b'", "#E'a\\'b'", "\200E'a\\'b'", "N'a''b'", "n", "nq", "q'",
    "q'[a;,/*b*/]'", "Q'{a}'", "q'(a)'", "q'<a>'", "q'!a!'", "NQ'!a!'", "nq'[Ω中]'",
    "xnq'[a]'", "q'[unterminated", "q'\200a\200'", "q'\na\n'", "$", "$$", "$$a;$$", "$9$a$9$",
    "$tag$a$tag$", "$tag$unclosed", "x$$a$$", "#$$a$$", "\200$$a$$", "\\", "Ω中é\377", "\001\177"
};
static void primitive_cases(void)
{
    char pair[3], random[130]; size_t i, j, n;
    stage = "all offsets and exact-size prefixes";
    for (i = 0U; i < COUNT(lexical_cases); i++) for (n = 0U; n <= strlen(lexical_cases[i]); n++) {
        char *s = malloc(n + 1U); CHECK(s != NULL);
        memcpy(s, lexical_cases[i], n); s[n] = '\0'; primitive_text(s); free(s);
    }
    for (i = 1U; i <= 255U; i++) {
        pair[0] = (char)i; pair[1] = '\0'; primitive_text(pair);
        for (j = 1U; j <= 255U; j++) { pair[1] = (char)j; pair[2] = '\0'; primitive_text(pair); }
    }
    for (i = 0U; i < 512U; i++) {
        n = next_random() % 129U;
        for (j = 0U; j < n; j++) random[j] = (char)(1U + next_random() % 255U);
        random[n] = '\0'; primitive_text(random);
    }
}

static void compare_statement(const sqlparser_handle_t *h, const char *sql,
                               int current, size_t ordinal, unsigned nulls)
{
    size_t as = 117U, ae = 219U, bs = as, be = ae; int a, b;
    begin_comparison();
    b = quotegates_frozen_sqlparser_view_public_statement_span_in_sql(h, sql, current, ordinal,
        nulls & 1U ? NULL : &bs, nulls & 2U ? NULL : &be);
    side = 1;
    a = sqlparser_view_public_statement_span_in_sql(h, sql, current, ordinal,
        nulls & 1U ? NULL : &as, nulls & 2U ? NULL : &ae);
    CHECK(a == b && as == bs && ae == be); end_comparison(h ? h->dialect : (sqlparser_dialect_t)-1);
}
static void statement_cases(void)
{
    static const char *const texts[] = {
        ";;;", "select 1;select 2;", "SELECT 1\nGO\nSELECT 2", "GO 2\r\nSELECT 1; GO 0\n",
        "gopher;go_value;SELECT GO FROM x;", "SELECT q'[;GO]',NQ'{;}',E'a\\';b', $$;$$;SELECT 2",
        "SELECT [;GO],`a;`,\"b;\";/*outer /*;*/tail*/SELECT 3", "SELECT 'unterminated;GO", "GO\rSELECT 1\rGO"
    };
    sqlparser_handle_t h; sqlparser_control_state_t control; sqlparser_control_unit_t units[2];
    int d; size_t i, j, n; char random[98];
    static const char alphabet[] = "aGgOo_nNqQeE9$'\"`[]{}()/*-#; \\\n\r\t\200\377";
    stage = "statement caller and guards"; memset(&h, 0, sizeof(h));
    for (d = -1; d <= 13; d++) {
        h.dialect = (sqlparser_dialect_t)d;
        for (i = 0U; i < COUNT(texts) + COUNT(lexical_cases); i++) {
            const char *s = i < COUNT(texts) ? texts[i] : lexical_cases[i - COUNT(texts)];
            h.sql_len = strlen(s); for (j = 0U; j < 5U; j++) compare_statement(&h, s, 0, j, 0U);
        }
        for (i = 0U; i < 160U; i++) {
            n = next_random() % 97U;
            for (j = 0U; j < n; j++) random[j] = alphabet[next_random() % (sizeof(alphabet) - 1U)];
            random[n] = '\0'; h.sql_len = n;
            for (j = 0U; j < 4U; j++) compare_statement(&h, random, 0, j, 0U);
        }
        for (i = 0U; i < 4U; i++) {
            compare_statement(NULL, "GO", 0, 0U, (unsigned)i);
            compare_statement(&h, NULL, 0, 0U, (unsigned)i);
            compare_statement(&h, "GO", 0, 0U, (unsigned)i);
        }
        memset(&control, 0, sizeof(control)); memset(units, 0, sizeof(units));
        control.units = units; control.unit_count = 2U; h.control = &control;
        h.sql_len = strlen("  SELECT 1;   SELECT 2  ");
        units[0].source_length = units[0].current_length = 12U;
        units[1].source_offset = units[1].current_offset = 12U;
        units[1].source_length = units[1].current_length = h.sql_len - 12U;
        for (i = 0U; i < 4U; i++) for (j = 0U; j < 2U; j++)
            compare_statement(&h, "  SELECT 1;   SELECT 2  ", (int)j, i, 0U);
        units[0].source_offset = h.sql_len + 1U; compare_statement(&h, "  SELECT 1;   SELECT 2  ", 0, 0U, 0U);
        units[0].source_offset = 0U; units[0].source_length = h.sql_len + 1U;
        compare_statement(&h, "  SELECT 1;   SELECT 2  ", 0, 0U, 0U);
        units[0].current_offset = SIZE_MAX; units[0].current_length = 1U;
        compare_statement(&h, "  SELECT 1;   SELECT 2  ", 1, 0U, 0U); h.control = NULL;
    }
}

static void compare_multi(const sqlparser_handle_t *h, const sqlparser_dialect_multi_insert_t *multi,
                          sqlparser_view_expression_source_cache_t *a_cache,
                          sqlparser_view_expression_source_cache_t *b_cache,
                          size_t start, size_t end, size_t row, size_t column)
{
    size_t as = 117U, ae = 219U, bs = as, be = ae; int a, b;
    begin_comparison();
    b = quotegates_frozen_sqlparser_view_multi_insert_cell_source_span(h, multi, b_cache, start, end, row, column, &bs, &be);
    side = 1;
    a = sqlparser_view_multi_insert_cell_source_span(h, multi, a_cache, start, end, row, column, &as, &ae);
    CHECK(a == b && as == bs && ae == be); if (a_cache) same_cache(a_cache, b_cache);
    end_comparison(h ? h->dialect : (sqlparser_dialect_t)-1);
}
static void multi_cases(void)
{
    static const char *const values[] = {
        "12345", "CURRENT_TIMESTAMP", "'a''b'", "N'a'", "E'a\\'b'", "q'[a,)]'", "NQ'!a)!'", "$tag$x,)$tag$",
        "(1+(2))", "ARRAY[1,2]", "{1,2}", "1 /*inline*/ + 2", "1 --line\n + 2", "1 /*outer /*inner*/x*/ + 2",
        "[a]]b]", "`a``b`", "\"a\"\"b\"", "Ω中é", "q'[unclosed", "'unclosed", "/*unclosed", "$tag$unclosed",
        "]", "}", ")", "([1,2]", "'a'/*tail*/", "q'\200x\200'"
    };
    sqlparser_handle_t h; sqlparser_dialect_multi_insert_t multi;
    sqlparser_dialect_multi_insert_branch_t branches[2]; sqlparser_dialect_multi_insert_value_t cells[2][2];
    size_t i, pass, row, column; int d; char sql[1024];
    stage = "multi-branch caller, malformed boundaries and cache";
    memset(&h, 0, sizeof(h)); memset(&multi, 0, sizeof(multi));
    memset(branches, 0, sizeof(branches)); memset(cells, 0, sizeof(cells));
    multi.mode = SQLPARSER_DIALECT_MULTI_INSERT_ALL; multi.branches = branches; multi.branch_count = 2U;
    multi.source_public_sql = "SELECT 1 FROM dual";
    for (row = 0U; row < 2U; row++) { branches[row].ordinal = row; branches[row].cells = cells[row]; branches[row].cell_count = 2U; }
    cells[0][1].public_sql = "'keep'"; cells[1][0].public_sql = "'last'"; cells[1][1].public_sql = "42";
    for (d = -1; d <= 13; d++) for (i = 0U; i < COUNT(values); i++) {
        sqlparser_view_expression_source_cache_t ac = {0}, bc = {0};
        int length = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES (%s, 'keep') INTO u VALUES ('last', 42) SELECT 1 FROM dual", values[i]);
        CHECK(length >= 0 && (size_t)length < sizeof(sql)); h.sql = sql; h.sql_len = (size_t)length;
        h.dialect = (sqlparser_dialect_t)d; cells[0][0].public_sql = (char *)values[i];
        for (pass = 0U; pass < 3U; pass++) for (row = 0U; row < 3U; row++) for (column = 0U; column < 3U; column++)
            compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, pass % 2U ? 2U - row : row, column);
        compare_multi(&h, &multi, NULL, NULL, 0U, h.sql_len, 0U, 0U);
        compare_multi(NULL, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U);
        compare_multi(&h, NULL, &ac, &bc, 0U, h.sql_len, 0U, 0U);
        compare_multi(&h, &multi, &ac, &bc, h.sql_len, h.sql_len, 0U, 0U);
        multi.branches = NULL; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U); multi.branches = branches;
        multi.source_public_sql = NULL; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U); multi.source_public_sql = "SELECT 1 FROM dual";
    }
}

static sqlparser_handle_t *parse_handle(sqlparser_dialect_t d, const char *sql)
{
    sqlparser_handle_t *h = NULL; sqlparser_parse_options_t options; sqlparser_error_t e;
    sqlparser_parse_options_default(&options); options.dialect = d;
    if (sqlparser_parse_with_options(sql, &options, &h, &e) != SQLPARSER_STATUS_OK) {
        fprintf(stderr, "fixture dialect=%d SQL=%s error=%s\n", (int)d, sql, e.message); CHECK(0);
    }
    return h;
}
static sqlparser_handle_t *parse_reference_handle(sqlparser_dialect_t dialect, const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_error_t error;
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    /* Frozen scanners consume the legacy cell representation. Keep their
     * source parsing independent from the compact constructor under test. */
    CHECK(sqlparser_oracle_parse_legacy_replacement(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    return handle;
}
static int compare_insert(sqlparser_handle_t *a_handle, sqlparser_handle_t *b_handle,
                           sqlparser_view_expression_source_cache_t *ac,
                           sqlparser_view_expression_source_cache_t *bc,
                           int comments, size_t statement, size_t row, size_t column, unsigned nulls)
{
    size_t as = 117U, ae = 219U, bs = as, be = ae; int a, b;
    sqlparser_error_t a_error = sentinel_error(), b_error = a_error;
    begin_comparison();
    b = quotegates_frozen_sqlparser_view_insert_cell_source_span(b_handle, NULL, bc, comments, statement, row, column,
        nulls & 1U ? NULL : &bs, nulls & 2U ? NULL : &be, nulls & 4U ? NULL : &b_error);
    side = 1;
    a = sqlparser_view_insert_cell_source_span(a_handle, NULL, ac, comments, statement, row, column,
        nulls & 1U ? NULL : &as, nulls & 2U ? NULL : &ae, nulls & 4U ? NULL : &a_error);
    CHECK(a == b && as == bs && ae == be); same_error(&a_error, &b_error);
    if (ac) same_cache(ac, bc);
    if (a_handle && b_handle) CHECK(a_handle->generation == b_handle->generation && a_handle->failed == b_handle->failed);
    end_comparison(a_handle ? a_handle->dialect : (sqlparser_dialect_t)-1);
    return a;
}
/* Exercise the mandatory final scanner with a real, already-validated cursor.
 * Same-length, in-object fixture faults keep all pointer/index reads valid.
 * Restore the source after each check; no certificate result is overridden. */
static void final_span_boundaries(void)
{
    static const struct { const char *bytes; int comments, expected; } cases[] = {
        {"q'[xx", 0, 0}, {"nq'[x", 0, 0}, {"'abc\\", 0, 0}, {"'x''y", 0, 0},
        {"/*xxx", 1, 0}, {"/*x*/", 0, 0}, {"/*x*/", 1, 1},
        {"$a$x$", 0, 1}, {"N'xx'", 0, 1}, {"E'x\\'", 0, 0}
    };
    static const sqlparser_dialect_t dialects[] = {SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE};
    const char *sql = "INSERT ALL INTO t VALUES ('abc', 'keep') INTO u VALUES ('last') SELECT 1 FROM dual";
    size_t d, i, as, ae, bs, be;
    stage = "final cell boundary and comment checks remain mandatory";
    for (d = 0U; d < COUNT(dialects); d++) {
        sqlparser_handle_t *a = parse_handle(dialects[d], sql), *b = parse_reference_handle(dialects[d], sql);
        sqlparser_view_expression_source_cache_t ac = {0}, bc = {0};
        CHECK(compare_insert(a, b, &ac, &bc, 0, 0U, 0U, 0U, 0U) == 1);
        CHECK(ac.valid == 2 && bc.valid == 2);
        CHECK(sqlparser_oracle_multi_insert_certified_cell_span(a, 0U, 0U, &as, &ae));
        CHECK(sqlparser_oracle_multi_insert_certified_cell_span(b, 0U, 0U, &bs, &be));
        CHECK(as == bs && ae == be && ae - as == 5U);
        for (i = 0U; i < COUNT(cases); i++) {
            CHECK(strlen(cases[i].bytes) == 5U);
            memcpy(a->sql + as, cases[i].bytes, 5U); memcpy(b->sql + bs, cases[i].bytes, 5U);
            CHECK(compare_insert(a, b, &ac, &bc, cases[i].comments, 0U, 0U, 0U, 0U) == cases[i].expected);
            memcpy(a->sql + as, "'abc'", 5U); memcpy(b->sql + bs, "'abc'", 5U);
        }
        a->sql[as - 1U] = b->sql[bs - 1U] = ':';
        CHECK(compare_insert(a, b, &ac, &bc, 0, 0U, 0U, 0U, 0U) == 0);
        a->sql[as - 1U] = b->sql[bs - 1U] = '(';
        a->sql[ae] = b->sql[be] = ':';
        CHECK(compare_insert(a, b, &ac, &bc, 0, 0U, 0U, 0U, 0U) == 0);
        a->sql[ae] = b->sql[be] = ',';
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    }
}
static void insert_cases(void)
{
    static const char *const multi[] = {
        "INSERT ALL INTO t VALUES ('a', 12, 'keep') INTO u VALUES ('b', 42, 'z') SELECT 1 FROM dual",
        " /*head*/ INSERT ALL INTO t VALUES (1 /*inline*/ + 2, 'a') INTO u VALUES ('x', NULL) SELECT 1 FROM dual; /*tail*/",
        "INSERT ALL INTO t VALUES ('a', N'b') INTO u VALUES ('x', 'Ω中') SELECT 1 FROM dual",
        "INSERT ALL WHEN 1=1 THEN INTO t VALUES ('a', 1) ELSE INTO u VALUES ('b', 2) SELECT 1 FROM dual",
        "INSERT ALL INTO t VALUES ('a', ARRAY[1,2], 'b') INTO u VALUES ('c', 3) SELECT 1 FROM dual"
    };
    static const sqlparser_dialect_t dialects[] = {SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE};
    size_t d, i, pass, row, column, nulls; int comments;
    stage = "real parsed multi-insert cold, hot, descending and repeated calls";
    for (d = 0U; d < COUNT(dialects); d++) for (i = 0U; i < COUNT(multi); i++) for (comments = 0; comments <= 1; comments++) {
        sqlparser_handle_t *a = parse_handle(dialects[d], multi[i]), *b = parse_reference_handle(dialects[d], multi[i]);
        sqlparser_view_expression_source_cache_t ac = {0}, bc = {0};
        for (pass = 0U; pass < 3U; pass++) for (row = 0U; row < 3U; row++) for (column = 0U; column < 4U; column++)
            compare_insert(a, b, &ac, &bc, comments, 0U, pass % 2U ? 2U - row : row, column, 0U);
        compare_insert(a, b, NULL, NULL, comments, 0U, 0U, 0U, 0U);
        compare_insert(a, b, &ac, &bc, comments, 1U, 0U, 0U, 0U);
        for (nulls = 0U; nulls < 8U; nulls++) compare_insert(a, b, &ac, &bc, comments, 0U, 0U, 0U, (unsigned)nulls);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    }
    stage = "ordinary INSERT all dialects and top-level guards";
    for (d = 0U; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
        sqlparser_handle_t *a = parse_handle((sqlparser_dialect_t)d, "INSERT INTO t VALUES ('a''b', 12, 'Ω中')");
        sqlparser_handle_t *b = parse_reference_handle((sqlparser_dialect_t)d, "INSERT INTO t VALUES ('a''b', 12, 'Ω中')");
        sqlparser_view_expression_source_cache_t ac = {0}, bc = {0};
        for (pass = 0U; pass < 2U; pass++) for (column = 0U; column < 4U; column++)
            compare_insert(a, b, &ac, &bc, (int)pass, 0U, 0U, column, 0U);
        compare_insert(a, b, &ac, &bc, 0, 99U, 0U, 0U, 0U);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    }
    for (nulls = 0U; nulls < 8U; nulls++) {
        sqlparser_handle_t a = {0}, b = {0}; sqlparser_view_expression_source_cache_t ac = {0}, bc = {0};
        compare_insert(NULL, NULL, &ac, &bc, 0, 0U, 0U, 0U, (unsigned)nulls);
        compare_insert(&a, &b, &ac, &bc, 0, 0U, 0U, 0U, (unsigned)nulls);
    }
}
int main(void)
{
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    primitive_cases(); statement_cases(); multi_cases(); insert_cases(); final_span_boundaries();
    if (setlocale(LC_CTYPE, "C.UTF-8") != NULL) { primitive_cases(); statement_cases(); multi_cases(); }
    CHECK(removed_calls > 0U && retained_calls > 0U);
    printf("Quote caller gates: %zu frozen caller comparisons, %zu scalar offset comparisons; %zu default calls removed, %zu opener calls retained exactly\n",
        comparisons, primitive_comparisons, removed_calls, retained_calls);
    return 0;
}
