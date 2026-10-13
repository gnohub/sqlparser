/* Ordinary constructor call-gate differential tests; no fault/clone hooks. */
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#define LISTBOUNDS_FROZEN_HELPERS_ONLY
#include "sqlparser_reference_oracle_list_scanners.inc"
#undef LISTBOUNDS_FROZEN_HELPERS_ONLY
/* Frozen accepted statementend constructor paths with complete collector
 * timing. Only function/call names change. The lexical closure is separately
 * frozen. Source SHA-256: b201312887bf893929daa45c3dff380595267ccf2d8163997af15804739928fc
 */

static int constructorgates_frozen_list_bounds_append(
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

static int constructorgates_frozen_find_matching_paren_with_bounds(
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

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
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
				if (collect && constructorgates_frozen_list_bounds_append(bounds, pos)) {
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
				if (name_state == 0 || !constructorgates_frozen_list_bounds_append(bounds, pos))
					collect = 0;
				name_state = 0;
			} else if (!listbounds_frozen_span_name_byte((unsigned char)sql[pos], 0, &name_state)) {
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
				collect = constructorgates_frozen_list_bounds_append(bounds, pos);
			}
		}
		pos++;
	}
	if (bounds != NULL) bounds->count = 0U;
	return 0;
}

static sqlparser_status_t constructorgates_frozen_identifier_from_sql(
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

static sqlparser_status_t constructorgates_frozen_relation_from_sql(
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

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
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
		status = constructorgates_frozen_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->table_name, out_error);
	} else if (count == 2U) {
		status = constructorgates_frozen_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->schema_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = constructorgates_frozen_identifier_from_sql(
				sql, part_start[1], part_end[1],
				&relation->table_name, out_error);
		}
	} else {
		status = constructorgates_frozen_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->database_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = constructorgates_frozen_identifier_from_sql(
				sql, part_start[1], part_end[1],
				&relation->schema_name, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = constructorgates_frozen_identifier_from_sql(
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

static sqlparser_status_t constructorgates_frozen_parse_multi_insert_into(
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

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
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
		if (!listbounds_frozen_span_name_byte((unsigned char)sql[pos], 1, &relation_name_state))
			*span_safe = 0;
		pos++;
	}
	if (relation_name_state == 0) *span_safe = 0;
	if (columns_open == end && values_pos == end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	relation_end = columns_open != end ? columns_open : values_pos;
	status = constructorgates_frozen_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!constructorgates_frozen_find_matching_paren_with_bounds(sql, columns_open, end, &columns_close,
			SQLPARSER_ORACLE_LIST_COLUMNS, &list_bounds)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_oracle_parse_column_list_with_bounds(
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
	if (!constructorgates_frozen_find_matching_paren_with_bounds(sql, values_open, end, &values_close,
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
#ifndef ORACLE_CONSTRUCTORGATES_CASES_H
#define ORACLE_CONSTRUCTORGATES_CASES_H
static const char *const constructorgates_cases[] = {
    "t", "_t", "T9", "a.b", "a.b.c", "a.b.c.d", "a.b.c.d.e",
    "", " ", "\t\r\n", ".", "..", ".t", "t.", "a..b", "a...b", "a.b.",
    " t ", " a.b ", "a .b", "a. b", "a . b", "a . b . c", "a\t.\r\nb",
    "a b", "a b.c", "a. b c", "a . . b", " a\v.\fb ",
    "1t", "a.2b", "$a", "a$b", "#a", "a#b", "a_b.c_2", "a-b", "a+b",
    "\"a.b\"", "\"a\".\"b\"", "\"a\"\"b\".c", "\"\"", "\"a\".\"\"", "\"broken",
    "'a.b'", "q'[a.b]'", "$$a.b$$", "`a.b`", "[a.b]",
    "a/* . */.b", "a. /* . */ b", "/*head*/a", "a/*tail*/", "a-- .\n.b", "a/*broken",
    "t@link", "a.b@remote", "a.b.c@remote", "t@\"a.b\"", "t @ link", "@link", "t@",
    "t@a.b", "t@@link", "t@\"\"", "t@link extra", "a.b.c.d@remote",
    "\xce\xa9", "a.\xe4\xb8\xad", "a\240.b", "\335.t", "a.\377",
    "values", "valuesx", "myvalues", "_values", "into", "select", "\"values\"", "t values"
};
static const char *const constructorgates_locales[] = {
    "C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "tr_TR", "en_US",
    "English_United States.1252", "Turkish_Turkey.1254"
};
#endif
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *fixture = "initialization", *active_locale = "C";
static size_t match_checks, branch_checks, locale_checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "constructor gates %s:%d locale=%s case=%s: %s\n", __FILE__, __LINE__, active_locale, fixture, #x); abort(); } } while (0)

static sqlparser_error_t seeded_error(void)
{
    sqlparser_error_t e;
    memset(&e, 0, sizeof(e)); e.code = SQLPARSER_STATUS_UNSUPPORTED;
    e.cursor = 17; e.line = 19; e.column = 23;
    strcpy(e.message, "retained relation-facts sentinel");
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

static sqlparser_oracle_state_t *new_state(int complete)
{
    sqlparser_oracle_state_t *s = NULL;
    sqlparser_error_t e = seeded_error();
    CHECK(sqlparser_oracle_state_new(&s, &e) == SQLPARSER_STATUS_OK);
    s->multi_insert = calloc(1U, sizeof(*s->multi_insert)); CHECK(s->multi_insert);
    s->multi_insert->oracle_spans_complete = complete;
    s->multi_insert->oracle_spans_identity = 1;
    s->multi_insert->oracle_generic_spans_equivalent = 1;
    return s;
}

static void compare_into(sqlparser_oracle_state_t *a, sqlparser_oracle_state_t *b,
                         const char *text, size_t offset, int *span_safe, int null_error, int condition)
{
    sqlparser_error_t ae = seeded_error(), be = ae;
    sqlparser_status_t as, bs;
    size_t ap = offset, bp = offset, length = strlen(text);
    int af = *span_safe, bf = af;
    char sql[4096];
    fixture = text; CHECK(offset + length + 1U < sizeof(sql));
    memset(sql, '~', offset); memcpy(sql + offset, text, length + 1U);
    bs = constructorgates_frozen_parse_multi_insert_into(sql, &bp, offset + length, b, b->multi_insert,
        condition ? ":p > 0" : NULL, condition ? "$1 > 0" : NULL, 0, condition ? 3U : 0U, &bf, null_error ? NULL : &be);
    as = sqlparser_oracle_parse_multi_insert_into(sql, &ap, offset + length, a, a->multi_insert,
        condition ? ":p > 0" : NULL, condition ? "$1 > 0" : NULL, 0, condition ? 3U : 0U, &af, null_error ? NULL : &ae);
    CHECK(as == bs && ap == bp && af == bf); same_error(&ae, &be);
    same_state(a, b); same_multi(a->multi_insert, b->multi_insert);
    memset(sql, '#', offset + length); same_multi(a->multi_insert, b->multi_insert);
    *span_safe = af; branch_checks++;
}

static void branch_matrix(void)
{
    sqlparser_oracle_state_t *a, *b;
    char sql[2048];
    size_t i, offset;
    int complete, null_error, seed, safe;
    unsigned byte;
    for (complete = 0; complete <= 1; complete++) for (null_error = 0; null_error <= 1; null_error++)
    for (seed = 0; seed <= 1; seed++) {
        a = new_state(complete); b = new_state(complete);
        for (i = 0; i < COUNT(constructorgates_cases); i++) {
            offset = i % 2U ? 17U : 0U; safe = seed;
            CHECK(snprintf(sql, sizeof(sql), "into %s(a,b) values (:same,N'x')", constructorgates_cases[i]) > 0);
            compare_into(a, b, sql, offset, &safe, null_error, 0);
            safe = seed;
            CHECK(snprintf(sql, sizeof(sql), "into %s values (1)", constructorgates_cases[i]) > 0);
            compare_into(a, b, sql, offset, &safe, null_error, 0);
        }
        for (byte = 0U; byte <= 255U; byte++) {
            safe = seed;
            CHECK(snprintf(sql, sizeof(sql), "into a%cZ values (1)", (int)byte) > 0);
            compare_into(a, b, sql, 17U, &safe, null_error, 0);
        }
        /* A prior quoted relation clears the shared flag; later ordinary
         * relation bytes still execute all original name/VALUES processing. */
        safe = 1;
        compare_into(a, b, "into \"previous\" values (1)", 17U, &safe, null_error, 0); CHECK(!safe);
        compare_into(a, b, "into alpha.beta.gamma values (2)", 17U, &safe, null_error, 0); CHECK(!safe);
        compare_into(a, b, "into a.b.c values (3)", 0U, &safe, null_error, 1);
        compare_into(a, b, "into a.b.c.d values (4)", 17U, &safe, null_error, 0);
        compare_into(a, b, "into final_target values ('after')", 17U, &safe, null_error, 0);
        sqlparser_oracle_state_destroy(a); sqlparser_oracle_state_destroy(b);
    }
}

static void seed_bounds(sqlparser_oracle_list_bounds_t *b)
{
    size_t i;
    memset(b, 0, sizeof(*b));
    for (i = 0; i < SQLPARSER_ORACLE_LIST_BOUND_CAPACITY; i++) b->ends[i] = SIZE_MAX - 17U - i;
    b->count = 11U; b->start = 13U; b->close = 17U; b->complete = 1; b->kind = 99;
}

static void gate_bytes(void)
{
    static const unsigned char allowed[] = {'$', 'q', 'Q', 'n', 'N', '\'', '"', '-', '/'};
    char input[32];
    unsigned byte;
    size_t i;
    for (byte = 0U; byte <= 255U; byte++) {
        int expected = 0;
        for (i = 0; i < COUNT(allowed); i++) if (byte == allowed[i]) expected = 1;
        CHECK(sqlparser_oracle_quote_or_comment_may_start((unsigned char)byte) == expected);
        input[0] = (char)byte; strcpy(input + 1U, "'$tag$/*-quoted"); fixture = input;
        if (!expected) CHECK(listbounds_frozen_skip_quoted_or_comment_span(input, 0U) == 0U);
    }
    fixture = "constructor gate byte matrix";
}

static void compare_match(const char *sql, size_t open, size_t end, int kind, unsigned optional,
                          sqlparser_oracle_list_bounds_t *a, sqlparser_oracle_list_bounds_t *b)
{
    size_t ac = SIZE_MAX - 37U, bc = ac, i;
    int ar, br, aerrno, berrno;
    fixture = sql ? sql : "NULL scanner input";
    errno = EDOM;
    br = constructorgates_frozen_find_matching_paren_with_bounds(sql, open, end,
        optional & 1U ? NULL : &bc, kind, optional & 2U ? NULL : b);
    berrno = errno; errno = EDOM;
    ar = sqlparser_oracle_find_matching_paren_with_bounds(sql, open, end,
        optional & 1U ? NULL : &ac, kind, optional & 2U ? NULL : a);
    aerrno = errno;
    CHECK(ar == br && ac == bc && aerrno == berrno && aerrno == EDOM);
    CHECK(a->count == b->count && a->start == b->start && a->close == b->close &&
          a->complete == b->complete && a->kind == b->kind);
    /* Both objects begin with defined size_t sentinels. Compare every slot,
     * including boundaries written before an overflow/late fallback reset. */
    for (i = 0; i < SQLPARSER_ORACLE_LIST_BOUND_CAPACITY; i++) CHECK(a->ends[i] == b->ends[i]);
    if (ar) CHECK(ac < end && sql[ac] == ')');
    match_checks++;
}

static void match_text(const char *text)
{
    static const int kinds[] = {SQLPARSER_ORACLE_LIST_COLUMNS, SQLPARSER_ORACLE_LIST_VALUES, 0, 99};
    sqlparser_oracle_list_bounds_t a, b;
    size_t length = strlen(text), open, end, k;
    unsigned optional;
    seed_bounds(&a); b = a;
    for (open = 0; open <= length; open++) {
        if (open != 0U && open != length && text[open] != '(') continue;
        /* end is only a logical scan limit. The backing object always remains
         * valid through its NUL, as required by the original unbounded helper. */
        for (end = 0; end <= length; end++) for (k = 0; k < COUNT(kinds); k++)
            for (optional = 0; optional < 4U; optional++) compare_match(text, open, end, kinds[k], optional, &a, &b);
    }
}

static void matcher_matrix(void)
{
    static const char *const texts[] = {
        "", "x", "()", "(a)", "(a,b)", "(a,)", "(,a)", "(a,,b)", "(,)",
        "( a_1 , B2 , _ )", "(a.b,c)", "(a b,c)", "(a\t,\r\nb\f,\vc)",
        "((a,b),c)", "(f(a,b),c)", "(a", "((a)", "(a)) suffix", "prefix (a,b) suffix",
        "('a,b',c)tail", "('a''b',c)tail", "('','''',a)", "('\xce\xa9,\xe4\xb8\xad',b)",
        "('a\\b',c)", "(N'a,b',c)", "(\"a,b\",c)", "(\"a\"\"b\",c)",
        "(q'[a,b]',c)", "(Q'{a,b}',c)", "(nq'(a,b)',c)", "(NQ'<a,b>',c)",
        "(nQ'!a,b!',c)", "(Nq'[a,b]',c)", "(aq'[a,b]',c)",
        "($$a,b$$,c)", "($tag$a,b$tag$,c)", "(a$tag$a,b$tag$,c)", "(#$tag$a,b$tag$,c)",
        "(\335$tag$a,b$tag$,c)", "(a/* , ) */ ,b)", "(a-- , )\n,b)",
        "(/* outer /* inner */ tail */ a,b)", "(a[1,2],b)", "(a{1,2},b)",
        "(`a,b`,c)", "(a\\b,c)", "(a$b,c)", "(a#b,c)", "(a@b,c)",
        "(q'", "(nq'", "($tag$", "(/*", "(--", "(''", "(\"\"",
        "(a,'unterminated)", "(a/*unterminated)", "(a)unrelated(unterminated"
    };
    static const size_t counts[] = {1U, 31U, 32U, 33U, 34U, 65U, 32U, 1U};
    sqlparser_oracle_list_bounds_t a, b;
    char text[512];
    size_t i, j, n, end;
    unsigned byte;
    int kind;
    for (i = 0; i < COUNT(texts); i++) match_text(texts[i]);
    seed_bounds(&a); b = a;
    for (i = 0; i < COUNT(counts); i++) {
        n = 0; text[n++] = '(';
        for (j = 0; j < counts[i]; j++) { if (j) text[n++] = ','; text[n++] = 'a'; }
        text[n++] = ')'; text[n] = '\0';
        /* The same table retains prior end-slot writes between success,
         * overflow, missing-close and later success in alternating kinds. */
        for (kind = 1; kind <= 2; kind++) compare_match(text, 0, n, kind, 0U, &a, &b);
        text[n - 1U] = '\0';
        for (kind = 1; kind <= 2; kind++) compare_match(text, 0, n - 1U, kind, 0U, &a, &b);
        text[n - 1U] = ','; text[n++] = ')'; text[n] = '\0';
        for (kind = 1; kind <= 2; kind++) compare_match(text, 0, n, kind, 0U, &a, &b);
        compare_match("(a)", 0, 3, SQLPARSER_ORACLE_LIST_COLUMNS, 0U, &a, &b);
    }
    for (byte = 0U; byte <= 255U; byte++) {
        text[0] = '('; text[1] = (char)byte; text[2] = ','; text[3] = 'a'; text[4] = ')'; text[5] = '\0';
        match_text(text);
    }
    for (end = 0; end <= 3U; end++) for (kind = 0; kind <= 2; kind++) {
        compare_match(NULL, 0, end, kind, 0U, &a, &b);
        compare_match("(a)", 4U, end, kind, 0U, &a, &b);
    }
    /* Deterministic short byte strings exercise failed candidates and nested
     * depth without creating a parser/fault-testing dependency. */
    for (i = 0; i < 256U; i++) {
        uint32_t random = (uint32_t)i + 0x7421U;
        text[0] = '('; n = 1U + i % 23U;
        for (j = 0; j < n; j++) { random = random * 1664525U + 1013904223U; text[j + 1U] = (char)(1U + (random >> 16) % 255U); }
        text[n + 1U] = ')'; text[n + 2U] = '\0'; match_text(text);
    }
}

int main(void)
{
    size_t i;
    (void)listbounds_frozen_find_matching_paren;
    for (i = 0; i < COUNT(constructorgates_locales); i++) {
        if (!setlocale(LC_CTYPE, constructorgates_locales[i])) { CHECK(i != 0U); continue; }
        active_locale = constructorgates_locales[i]; locale_checks++; gate_bytes(); matcher_matrix(); branch_matrix();
    }
    CHECK(setlocale(LC_CTYPE, "C"));
    printf("Oracle constructor gates: %zu exact matcher/table comparisons, %zu branch comparisons; %zu locales passed\n",
        match_checks, branch_checks, locale_checks);
    return 0;
}
