/* Ordinary-input differential coverage for Oracle list materialization.
 * The current implementation is the real TU; old list loops and their lexical closure are
 * frozen independently. No allocation hooks, fault injection, or clone calls. */
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#define LISTBOUNDS_FROZEN_HELPERS_ONLY
#include "sqlparser_reference_oracle_list_scanners.inc"
#undef LISTBOUNDS_FROZEN_HELPERS_ONLY
/* Frozen list materialization bodies; only function names
 * and references to the separately frozen lexical helpers are renamed.
 * Unchanged item construction, trimming and cleanup helpers remain shared.
 */

static sqlparser_status_t listbounds_frozen_parse_column_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_column_t **out_columns,
	size_t *out_count,
	int *span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
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
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? listbounds_frozen_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			*span_safe = 0;
			pos = skipped;
			continue;
		}
		if (!at_end && sql[pos] != ',' &&
		    !listbounds_frozen_span_name_byte((unsigned char)sql[pos], 0, &name_state)) *span_safe = 0;
		if (at_end || sql[pos] == ',') {
			size_t item_end;
			sqlparser_dialect_multi_insert_column_t *next;

			if (name_state == 0) *span_safe = 0;
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

static sqlparser_status_t listbounds_frozen_parse_value_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_t *multi,
	sqlparser_dialect_multi_insert_value_t **out_values,
	size_t *out_count,
	int *span_safe,
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
		skipped = !at_end ? listbounds_frozen_skip_quoted_or_comment_span(sql, pos) : pos;
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
			uint32_t lexical_flags = 0U;
			int cell_span_safe = 0;

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
			if (sqlparser_oracle_parse_value_item_with_span_proof(sql, item_start, pos, state, &values[count],
				multi->oracle_spans_complete ? &lexical_flags : NULL, &cell_span_safe, out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			if (!cell_span_safe) *span_safe = 0;
			sqlparser_oracle_record_cell_span(multi, sql, item_start, pos, lexical_flags, span_safe);
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

static sqlparser_status_t listbounds_frozen_parse_multi_insert_into(
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
	status = sqlparser_oracle_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!listbounds_frozen_find_matching_paren(sql, columns_open, end, &columns_close)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = listbounds_frozen_parse_column_list(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			span_safe,
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
	if (!listbounds_frozen_find_matching_paren(sql, values_open, end, &values_close)) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (multi->oracle_spans_complete && multi->oracle_span_count < UINT32_MAX)
		branch.oracle_span_base = (uint32_t)multi->oracle_span_count;
	status = listbounds_frozen_parse_value_list(
		sql,
		values_open + 1U,
		values_close,
		state,
		multi,
		&branch.cells,
		&branch.cell_count,
		span_safe,
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
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *fixture = "initialization";
static const char *active_locale = "C";
static size_t branch_checks, public_checks, syntax_errors, locale_rejections, locale_checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Oracle list bounds line %d: %s\nSQL: %s\n", __LINE__, #x, fixture); abort(); } } while (0)

static void same_text(const char *a, const char *b)
{
    CHECK((a == NULL) == (b == NULL));
    if (a != NULL) CHECK(strcmp(a, b) == 0);
}

static sqlparser_error_t seeded_error(void)
{
    sqlparser_error_t error;
    memset(&error, 0, sizeof(error));
    error.code = SQLPARSER_STATUS_UNSUPPORTED;
    error.cursor = 17; error.line = 19; error.column = 23;
    strcpy(error.message, "retained ordinary-input sentinel");
    return error;
}

static void same_error(const sqlparser_error_t *a, const sqlparser_error_t *b)
{
    CHECK(a->code == b->code && a->cursor == b->cursor &&
          a->line == b->line && a->column == b->column);
    CHECK(memcmp(a->message, b->message, sizeof(a->message)) == 0);
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
    if (a->has_literal) {
        CHECK(a->literal.kind == b->literal.kind);
        if (a->literal.kind == SQLPARSER_LITERAL_KIND_STRING)
            same_text(a->literal.string_value, b->literal.string_value);
        if (a->literal.kind == SQLPARSER_LITERAL_KIND_FLOAT)
            same_text(a->literal.float_value, b->literal.float_value);
        if (a->literal.kind == SQLPARSER_LITERAL_KIND_INTEGER)
            CHECK(a->literal.integer_value == b->literal.integer_value);
    }
}

static void same_branch(const sqlparser_dialect_multi_insert_branch_t *a,
                        const sqlparser_dialect_multi_insert_branch_t *b)
{
    size_t i;
    CHECK(a->ordinal == b->ordinal && a->column_count == b->column_count &&
          a->cell_count == b->cell_count && a->has_condition == b->has_condition &&
          a->is_else == b->is_else && a->condition_group_id == b->condition_group_id &&
          a->oracle_span_base == b->oracle_span_base && a->oracle_values_position == b->oracle_values_position);
    same_text(a->relation.database_name, b->relation.database_name);
    same_text(a->relation.schema_name, b->relation.schema_name);
    same_text(a->relation.table_name, b->relation.table_name);
    same_text(a->relation.link_name, b->relation.link_name);
    same_text(a->relation.link_sql, b->relation.link_sql);
    same_text(a->relation.sql, b->relation.sql);
    same_text(a->condition_public_sql, b->condition_public_sql);
    same_text(a->condition_parser_sql, b->condition_parser_sql);
    for (i = 0U; i < a->column_count; i++) {
        same_text(a->columns[i].name, b->columns[i].name);
        same_text(a->columns[i].sql, b->columns[i].sql);
    }
    for (i = 0U; i < a->cell_count; i++) same_value(&a->cells[i], &b->cells[i]);
}

static void same_state(const sqlparser_oracle_state_t *a, const sqlparser_oracle_state_t *b)
{
    const sqlparser_dialect_multi_insert_t *am = a->multi_insert, *bm = b->multi_insert;
    const sqlparser_dialect_national_literals_t *an = &a->national_literals, *bn = &b->national_literals;
    size_t i;
    CHECK(a->bind_count == b->bind_count && a->bind_capacity == b->bind_capacity &&
          a->bind_occurrence_count == b->bind_occurrence_count);
    for (i = 0U; i < a->bind_count; i++) same_text(a->bind_names[i], b->bind_names[i]);
    CHECK(an->count == bn->count && an->capacity == bn->capacity &&
          an->literal_count == bn->literal_count && an->fragment_start == bn->fragment_start &&
          an->fragment_literal_base == bn->fragment_literal_base);
    for (i = 0U; i < an->count; i++) {
        same_text(an->items[i].sql, bn->items[i].sql);
        same_text(an->items[i].surface_sql, bn->items[i].surface_sql);
        CHECK(an->items[i].ordinal == bn->items[i].ordinal);
    }
    CHECK(am->branch_count == bm->branch_count && am->branch_capacity == bm->branch_capacity &&
          am->oracle_span_count == bm->oracle_span_count && am->oracle_span_capacity == bm->oracle_span_capacity &&
          am->oracle_spans_complete == bm->oracle_spans_complete &&
          am->oracle_spans_identity == bm->oracle_spans_identity &&
          am->oracle_generic_spans_equivalent == bm->oracle_generic_spans_equivalent);
    for (i = 0U; i < am->branch_count; i++) same_branch(&am->branches[i], &bm->branches[i]);
    for (i = 0U; i < am->oracle_span_count; i++) {
        CHECK(am->oracle_spans[i].source_start == bm->oracle_spans[i].source_start &&
              am->oracle_spans[i].source_length == bm->oracle_spans[i].source_length &&
              am->oracle_spans[i].lexical_flags == bm->oracle_spans[i].lexical_flags);
    }
}

static sqlparser_oracle_state_t *new_state(int spans)
{
    sqlparser_oracle_state_t *state = NULL;
    sqlparser_error_t error = seeded_error();
    CHECK(sqlparser_oracle_state_new(&state, &error) == SQLPARSER_STATUS_OK);
    state->multi_insert = (sqlparser_dialect_multi_insert_t *)calloc(1U, sizeof(*state->multi_insert));
    CHECK(state->multi_insert != NULL);
    state->multi_insert->oracle_spans_complete = spans;
    state->multi_insert->oracle_spans_identity = 1;
    state->multi_insert->oracle_generic_spans_equivalent = 1;
    return state;
}

/* Retain each state across consecutive INTO branches, including valid input
 * after a SQL syntax error. All calls use normal allocations and real output. */
static void compare_into(sqlparser_oracle_state_t *actual, sqlparser_oracle_state_t *reference,
                          const char *sql, int span_seed)
{
    size_t ap = 0U, rp = 0U;
    int as = span_seed, rs = span_seed;
    sqlparser_error_t ae = seeded_error(), re = ae;
    sqlparser_status_t av, rv;
    fixture = sql;
    rv = listbounds_frozen_parse_multi_insert_into(sql, &rp, strlen(sql), reference,
        reference->multi_insert, NULL, NULL, 0, 0U, &rs, &re);
    av = sqlparser_oracle_parse_multi_insert_into(sql, &ap, strlen(sql), actual,
        actual->multi_insert, NULL, NULL, 0, 0U, &as, &ae);
    CHECK(av == rv && ap == rp && as == rs);
    same_error(&ae, &re); same_state(actual, reference);
    branch_checks++;
}

static const char *const list_cases[] = {
    "(a,b_c,Z9)", "( a , _b9 \t)",
    "(a\r\n,\fb\v)", "()", "(,)",
    "(a,)", "(,a)", "(a,,b)",
    "( \t )", "(a b,c)", "(a.b,c)",
    "(1a,b)", "(a$1,b)", "(a#1,b)",
    "(f(a,b),c)", "(((1,2)),3)", "(f(),2)",
    "('a,b',2)", "('a''),b',NULL)",
    "('a\\b',-1.25)", "('é,中',2)",
    "(\"a,b\",c)", "(\"a\"\"b\",c)",
    "(q'[a,b]',2)", "(nq'{a,b}',2)",
    "($$a,b$$,2)", "($tag$a,b$tag$,2)",
    "(a/* , ) */ ,b)", "(a-- , )\n,b)",
    "(/* outer /* inner */ tail */ a,b)",
    "(é,1)", "(a\240,b)",
    "([a,b],2)", "({a,b},2)", "(`a,b`,2)",
    "(:name,:2,?,NULL,1,1.25,current_timestamp)",
    "(a", "('unterminated)", "(q'[unterminated)",
    "(a/*unterminated)", "(f(a,b)",
    "(a,b)) tail", "x(a,b)"
};

static void list_matrix(void)
{
    sqlparser_oracle_state_t *actual, *reference;
    char branch[2048];
    size_t i;
    int spans, seed;
    for (spans = 0; spans <= 1; spans++) for (seed = 0; seed <= 1; seed++) {
        actual = new_state(spans); reference = new_state(spans);
        for (i = 0U; i < COUNT(list_cases); i++) {
            CHECK(snprintf(branch, sizeof(branch), "INTO t%s VALUES (1)", list_cases[i]) > 0);
            compare_into(actual, reference, branch, seed);
            CHECK(snprintf(branch, sizeof(branch), "INTO t VALUES %s", list_cases[i]) > 0);
            compare_into(actual, reference, branch, seed);
        }
        compare_into(actual, reference, "INTO t(a,b) VALUES ('after',7)", seed);
        compare_into(actual, reference, "INTO t(a,b) VALUE ('wrong',7)", seed);
        compare_into(actual, reference, "INTO t(a,b) VALUES 'wrong'", seed);
        compare_into(actual, reference, "INTO t(a,b) VALUES ('again',8)", seed);
        sqlparser_oracle_state_destroy(actual); sqlparser_oracle_state_destroy(reference);
    }
}

static void make_list(char *out, size_t capacity, size_t count, int columns, int trailing)
{
    size_t i, used = 1U;
    int written;
    out[0] = '(';
    for (i = 0U; i < count; i++) {
        written = snprintf(out + used, capacity - used, "%s%s%u", i ? "," : "", columns ? "col_" : "", (unsigned)i);
        CHECK(written > 0 && (size_t)written < capacity - used);
        used += (size_t)written;
    }
    CHECK(used + (trailing ? 3U : 2U) <= capacity);
    if (trailing) out[used++] = ',';
    out[used++] = ')'; out[used] = '\0';
}

static void capacity_and_reuse(void)
{
    static const size_t sizes[] = {31U, 32U, 33U, 1U, 33U, 32U, 0U, 31U};
    sqlparser_oracle_state_t *actual = new_state(1), *reference = new_state(1);
    char columns[2048], values[2048], branch[4200];
    size_t i, round, count;
    int trailing;
    for (round = 0U; round < 3U; round++) for (i = 0U; i < COUNT(sizes); i++) {
        count = sizes[i];
        for (trailing = 0; trailing <= 1; trailing++) {
            make_list(columns, sizeof(columns), count, 1, trailing);
            make_list(values, sizeof(values), count, 0, trailing);
            CHECK(snprintf(branch, sizeof(branch), "INTO t%s VALUES %s", columns, values) > 0);
            compare_into(actual, reference, branch, 1);
            /* A trailing column error stops before VALUES. Exercise the
             * same trailing values independently, including item 32/33. */
            CHECK(snprintf(branch, sizeof(branch), "INTO t VALUES %s", values) > 0);
            compare_into(actual, reference, branch, 1);
        }
        compare_into(actual, reference, "INTO t(a,b VALUES (1,2)", 1);
        compare_into(actual, reference, "INTO t(a,b) VALUES (1,2)", 1);
        compare_into(actual, reference, "INTO t VALUES ('x,y',f(1,2),)", 1);
    }
    /* The constructor reuses one local table for columns and values; cross
     * the capacity boundary in opposite directions to expose stale facts. */
    for (i = 0U; i < 2U; i++) {
        make_list(columns, sizeof(columns), i ? 31U : 33U, 1, 0);
        make_list(values, sizeof(values), i ? 33U : 31U, 0, 0);
        CHECK(snprintf(branch, sizeof(branch), "INTO t%s VALUES %s", columns, values) > 0);
        compare_into(actual, reference, branch, 1);
    }
    sqlparser_oracle_state_destroy(actual); sqlparser_oracle_state_destroy(reference);
}

static void public_successes(void)
{
    static const sqlparser_dialect_t dialects[] = {
        SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE
    };
    static const char *const statements[] = {
        "INSERT ALL INTO t(a,b) VALUES ('a,b',1) INTO u(a,b) VALUES ('next',2) SELECT 1 FROM dual",
        "INSERT ALL INTO t(a,b) VALUES (coalesce(1,2),(3+4)) SELECT 1 FROM dual",
        "INSERT ALL INTO t(\"a,b\",c) VALUES (q'[a,b]',2) SELECT 1 FROM dual",
        "INSERT ALL INTO t(a,b) VALUES (1/*comma,*/,2) SELECT 1 FROM dual"
    };
    char columns[2048], values[2048], sql[4300];
    size_t d, i, count;
    for (d = 0U; d < COUNT(dialects); d++) for (i = 0U; i < COUNT(statements) + 3U; i++) {
        sqlparser_parse_options_t options;
        sqlparser_error_t error = seeded_error();
        sqlparser_status_t status;
        sqlparser_handle_t *handle = NULL;
        const sqlparser_dialect_multi_insert_t *multi;
        char *deparsed = NULL;
        if (i < COUNT(statements)) fixture = statements[i];
        else {
            count = 31U + i - COUNT(statements);
            make_list(columns, sizeof(columns), count, 1, 0);
            make_list(values, sizeof(values), count, 0, 0);
            CHECK(snprintf(sql, sizeof(sql), "INSERT ALL INTO t%s VALUES %s SELECT 1 FROM dual", columns, values) > 0);
            fixture = sql;
        }
        sqlparser_parse_options_default(&options); options.dialect = dialects[d];
        status = sqlparser_parse_with_options(fixture, &options, &handle, &error);
        /* The immutable public baseline rejects uppercase INSERT under a
         * locale whose byte tolower('I') is not 'i' (verified for tr_TR.UTF-8
         * by the separately linked public_locale_probe). Keep that input and
         * locale covered, asserting the exact old error rather than assuming
         * a successful parse in every locale. The first rejected token is ALL,
         * before any generated column/value list is reached. */
        if (tolower('I') != 'i') {
            CHECK(status == SQLPARSER_STATUS_PARSE_ERROR && handle == NULL);
            CHECK(error.code == SQLPARSER_STATUS_PARSE_ERROR && error.cursor == 8 &&
                  error.line == 1 && error.column == 8);
            CHECK(strcmp(error.message, "syntax error at or near \"ALL\"") == 0);
            locale_rejections++;
            continue;
        }
        if (status != SQLPARSER_STATUS_OK)
            fprintf(stderr, "public success case: locale=%s dialect=%d status=%d error=%d cursor=%d line=%d column=%d message=%s\n",
                active_locale, (int)dialects[d], (int)status, (int)error.code,
                error.cursor, error.line, error.column, error.message);
        CHECK(status == SQLPARSER_STATUS_OK);
        CHECK(handle != NULL);
        multi = sqlparser_oracle_state_multi_insert(handle->dialect_state);
        CHECK(multi != NULL && multi->branch_count == (i == 0U ? 2U : 1U));
        CHECK(multi->branches[0].cell_count == (i < COUNT(statements) ? 2U : 31U + i - COUNT(statements)));
        CHECK(multi->branches[0].column_count == multi->branches[0].cell_count);
        CHECK(sqlparser_deparse(handle, &deparsed, &error) == SQLPARSER_STATUS_OK);
        CHECK(deparsed != NULL && deparsed[0] != '\0');
        sqlparser_string_free(deparsed); sqlparser_handle_destroy(handle);
        public_checks++;
    }
}

static void public_syntax_errors(void)
{
    static const sqlparser_dialect_t dialects[] = {
        SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE
    };
    static const char *const statements[] = {
        "INSERT ALL INTO t() VALUES (1) SELECT 1 FROM dual",
        "INSERT ALL INTO t(a,) VALUES (1,2) SELECT 1 FROM dual",
        "INSERT ALL INTO t(,a) VALUES (1,2) SELECT 1 FROM dual",
        "INSERT ALL INTO t(a) VALUES 1 SELECT 1 FROM dual",
        "INSERT ALL INTO t(a) VALUE (1) SELECT 1 FROM dual",
        "INSERT ALL INTO t(a) VALUES (1 SELECT 1 FROM dual"
    };
    size_t d, i;
    for (d = 0U; d < COUNT(dialects); d++) for (i = 0U; i < COUNT(statements); i++) {
        sqlparser_parse_options_t options;
        sqlparser_error_t error = seeded_error();
        sqlparser_status_t status;
        sqlparser_handle_t *handle = NULL;
        fixture = statements[i];
        sqlparser_parse_options_default(&options); options.dialect = dialects[d];
        status = sqlparser_parse_with_options(fixture, &options, &handle, &error);
        if (status != SQLPARSER_STATUS_PARSE_ERROR)
            fprintf(stderr, "public syntax case: locale=%s dialect=%d status=%d error=%d cursor=%d line=%d column=%d message=%s\n",
                active_locale, (int)dialects[d], (int)status, (int)error.code,
                error.cursor, error.line, error.column, error.message);
        CHECK(status == SQLPARSER_STATUS_PARSE_ERROR);
        CHECK(handle == NULL && error.code == SQLPARSER_STATUS_PARSE_ERROR);
        CHECK(error.message[0] != '\0');
        syntax_errors++;
    }
}

int main(void)
{
    static const char *const locales[] = {"C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "English_United States.1252", "Turkish_Turkey.1254"};
    size_t i;
    for (i = 0U; i < COUNT(locales); i++) {
        if (setlocale(LC_CTYPE, locales[i]) == NULL) { CHECK(i != 0U); continue; }
        active_locale = locales[i];
        locale_checks++;
        list_matrix(); capacity_and_reuse(); public_successes(); public_syntax_errors();
    }
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    printf("Oracle list materialization: %zu branch comparisons, %zu public successes, %zu syntax errors, %zu exact baseline locale rejections, %zu locales passed\n",
        branch_checks, public_checks, syntax_errors, locale_rejections, locale_checks);
    return 0;
}
