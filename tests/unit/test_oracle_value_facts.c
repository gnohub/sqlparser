/* Ordinary-input differential tests only: no fault hooks or clone calls. */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
/* Frozen listbounds baseline value path. Only the listed private names are
 * renamed. Ordinary allocation, decoder, classification and error behavior
 * are retained verbatim. Shared dependencies outside this closure are unchanged.
 */

static sqlparser_status_t valuefacts_frozen_buffer_reserve_input(
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

static int valuefacts_frozen_text_is_integer_literal(const char *text)
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

static int valuefacts_frozen_text_is_float_literal(const char *text)
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

static int valuefacts_frozen_ascii_text_equal(const char *left, const char *right)
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

static sqlparser_status_t valuefacts_frozen_unquote_string_literal(
	const char *text,
	char **out_value,
	uint32_t *out_lexical_flags,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t len;
	size_t pos;
	int ordinary = 1;

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
		char value = text[pos];
		int doubled = value == '\'' && pos + 1U < len - 1U && text[pos + 1U] == '\'';
		/* Record exactly the legacy decoder's fact: a doubled quote bypasses
		 * the ordinary-character check, but every other quote/control does not. */
		if (!doubled && (value == '\'' || value == '\\' ||
		    (unsigned char)value < 32U || (unsigned char)value == 127U))
			ordinary = 0;
		/* This local buffer starts empty and only grows by decoded bytes.
		 * Reserve at exactly the old append_char capacity crossings, including
		 * its initial 256-byte allocation and subsequent doubling. Preserve the
		 * same byte/NUL stores and failure cleanup; empty strings still finish
		 * below and allocate there. No ownership or allocation schedule changes. */
		if (out.capacity == 0U || out.len >= out.capacity - 1U) {
			if (sqlparser_oracle_buffer_reserve(&out, 1U, out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
		}
		out.data[out.len++] = value;
		out.data[out.len] = '\0';
		pos += doubled ? 2U : 1U;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_value = sqlparser_oracle_buffer_take(&out);
	if (*out_value != NULL && ordinary && out_lexical_flags != NULL)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_ORDINARY_STRING;
	return *out_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t valuefacts_frozen_value_fill_literal(
	sqlparser_dialect_multi_insert_value_t *value,
	uint32_t *out_lexical_flags,
	sqlparser_error_t *out_error)
{
	long long integer_value;
	char *endptr;

	if (value == NULL || value->public_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (valuefacts_frozen_ascii_text_equal(value->public_sql, "null")) {
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}
	if (value->public_sql[0] == '\'') {
		if (valuefacts_frozen_unquote_string_literal(value->public_sql, &value->literal_string_value, out_lexical_flags, out_error) != SQLPARSER_STATUS_OK) {
			return SQLPARSER_STATUS_OK;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value->literal.string_value = value->literal_string_value;
		return SQLPARSER_STATUS_OK;
	}
	if (valuefacts_frozen_text_is_integer_literal(value->public_sql)) {
		endptr = NULL;
		integer_value = strtoll(value->public_sql, &endptr, 10);
		if (endptr != NULL && *endptr == '\0') {
			value->has_literal = 1;
			value->literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value->literal.integer_value = integer_value;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (valuefacts_frozen_text_is_float_literal(value->public_sql)) {
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

/* A complete-token proof for preprocessing identity, not a replacement for
 * literal classification. Unknown syntax stays with preprocess_text, including
 * prefixed/alternative strings, binds, comments and compound expressions. */
static int valuefacts_frozen_value_identity_kind(const char *text, size_t *out_length)
{
	static const char *const sqlvalue_keywords[] = {
		"current_date", "current_time", "current_timestamp",
		"localtime", "localtimestamp", "current_role", "current_user",
		"session_user", "user", "current_catalog", "current_schema"
	};
	size_t pos, index;
	int saw_digit, saw_dot;

	if (text == NULL || text[0] == '\0') return 0;
	if (text[0] == '\'') {
		for (pos = 1U; text[pos] != '\0'; pos++) {
			unsigned char c = (unsigned char)text[pos];
			if (c < 32U || c == 127U || c == '\\') return 0;
			if (c != '\'') continue;
			if (text[pos + 1U] == '\'') {
				pos++;
				continue;
			}
			if (text[pos + 1U] != '\0') return 0;
			*out_length = pos + 1U;
			return 1; /* Exactly one ordinary String token. */
		}
		return 0;
	}
	if ((text[0] >= '0' && text[0] <= '9') ||
	    text[0] == '+' || text[0] == '-' || text[0] == '.') {
		pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
		saw_digit = saw_dot = 0;
		for (; text[pos] != '\0'; pos++) {
			if (text[pos] >= '0' && text[pos] <= '9') saw_digit = 1;
			else if (text[pos] == '.' && !saw_dot) saw_dot = 1;
			else return 0;
		}
		if (!saw_digit) return 0;
		*out_length = pos;
		return 2;
	}
	if (valuefacts_frozen_ascii_text_equal(text, "null")) {
		*out_length = 4U;
		return 2;
	}
	/* These are the complete bare SQLValueFunction token alternatives in the
 * native grammar. Precision arguments and other function forms still use the
 * legacy path. None contains a rewrite keyword, bind, quote or DB-link. */
	for (index = 0U; index < sizeof(sqlvalue_keywords) / sizeof(sqlvalue_keywords[0]); index++) {
		if (valuefacts_frozen_ascii_text_equal(text, sqlvalue_keywords[index])) {
			*out_length = strlen(sqlvalue_keywords[index]);
			return 2;
		}
	}
	return 0;
}

static sqlparser_status_t valuefacts_frozen_preprocess_value_text(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_sql,
	int *out_span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_status_t status;
	size_t length = 0U;
	int identity_kind = valuefacts_frozen_value_identity_kind(input_sql, &length);

	if (out_span_safe != NULL) *out_span_safe = identity_kind != 0;
	if (identity_kind == 0)
		return sqlparser_oracle_preprocess_text(input_sql, state, out_sql, out_error);
	memset(&out, 0, sizeof(out));
	/* Preserve the legacy first allocation (realloc(NULL, strlen + 1)), its
 * error, and its position before any state update. The complete identity
 * proof establishes that the old append/finish calls cannot allocate again. */
	status = valuefacts_frozen_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) return status;
	memcpy(out.data, input_sql, length + 1U);
	out.len = length;
	if (identity_kind == 1) state->national_literals.literal_count++;
	*out_sql = sqlparser_oracle_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t valuefacts_frozen_parse_value_item_with_span_proof(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	uint32_t *out_lexical_flags,
	int *out_span_safe,
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
	if (out_lexical_flags != NULL) *out_lexical_flags = 0U;
	status = sqlparser_oracle_trimmed_slice_dup(sql, start, end, &out_value->public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	occurrence_before = state->bind_occurrence_count;
	status = valuefacts_frozen_preprocess_value_text(out_value->public_sql, state, &out_value->parser_sql, out_span_safe, out_error);
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
		status = valuefacts_frozen_value_fill_literal(out_value, out_lexical_flags, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_value_clear(out_value);
			return status;
		}
	}
	if (out_lexical_flags != NULL && strcmp(out_value->public_sql, out_value->parser_sql) == 0)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_IDENTITY;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t valuefacts_frozen_parse_value_item(
	const char *sql, size_t start, size_t end, sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	uint32_t *out_lexical_flags, sqlparser_error_t *out_error)
{
	return valuefacts_frozen_parse_value_item_with_span_proof(
		sql, start, end, state, out_value, out_lexical_flags, NULL, out_error);
}
#ifndef ORACLE_VALUEFACTS_CASES_H
#define ORACLE_VALUEFACTS_CASES_H

static const char *const valuefacts_cases[] = {
    "''", "'plain'", "'\xce\xa9\xe4\xb8\xad'", "''''", "'a''b'", "''''''",
    "'a\\b'", "'\\'", "'a\nb'", "'a\rb'", "'a\tb'", "'\001\037\177'",
    "0", "-0", "+0", "0001", "-42", "+42",
    "2147483647", "2147483648", "-2147483648", "-2147483649",
    "9223372036854775807", "-9223372036854775808",
    "9223372036854775808", "-9223372036854775809",
    "9999999999999999999999999999999999999999999",
    "0.0", "1.", ".5", "+.5", "-.5", "+1.0", "-1.25", "000.010",
    "NULL", "null", "NuLl", "nUlL",
    "CURRENT_DATE", "current_date", "CuRrEnT_DaTe",
    "CURRENT_TIME", "current_time", "CuRrEnT_TiMe",
    "CURRENT_TIMESTAMP", "current_timestamp", "CuRrEnT_TiMeStAmP",
    "LOCALTIME", "localtime", "LoCaLtImE",
    "LOCALTIMESTAMP", "localtimestamp", "LoCaLtImEsTaMp",
    "CURRENT_ROLE", "current_role", "CuRrEnT_RoLe",
    "CURRENT_USER", "current_user", "CuRrEnT_UsEr",
    "SESSION_USER", "session_user", "SeSsIoN_UsEr",
    "USER", "user", "UsEr",
    "CURRENT_CATALOG", "current_catalog", "CuRrEnT_CaTaLoG",
    "CURRENT_SCHEMA", "current_schema", "CuRrEnT_ScHeMa",
    ":name", ":name", ":other", ":1", ":12", "?", ":\"a b\"",
    "N'x'", "n'\xce\xa9'", "q'[x''y]'", "NQ'{x}'", "E'a\\nb'", "$$x$$",
    "(1)", "1+2", "coalesce(:name, N'x')", "current_timestamp(3)",
    "nullif(1,2)", "a@remote", "1e3", "1E-3", "0x1", "true", "false",
    "1/*tail*/", "/*head*/1", "1--tail\n", "'a' || 'b'", "'a' 'b'", "'a'b'", "'a' + :p + 'b'",
    "':name ? $1 @x /* */ --'",
    "", " ", "+", "-", ".", "+.", "-.", "1..2", "--1", "++1",
    "'", "'unterminated", "N'broken", "q'[broken", "/*broken", "(1", "1)",
    "nullx", "current_datex", " current_date ", "\t-42\r\n"
};

static const char *const valuefacts_locales[] = {
    "C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "tr_TR", "en_US",
    "English_United States.1252", "Turkish_Turkey.1254"
};

#endif
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *fixture, *active_locale;
static size_t item_checks, literal_checks, locale_checks, highbyte_aliases;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "value facts %s:%d locale=%s case=%s: %s\n", __FILE__, __LINE__, active_locale, fixture, #x); abort(); } } while (0)

static sqlparser_error_t seeded_error(void)
{
    sqlparser_error_t e;
    memset(&e, 0, sizeof(e)); e.code = SQLPARSER_STATUS_UNSUPPORTED;
    e.cursor = 17; e.line = 19; e.column = 23;
    strcpy(e.message, "retained value-facts sentinel");
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

static void compare_item(sqlparser_oracle_state_t *a, sqlparser_oracle_state_t *b,
                         const char *sql, size_t start, size_t end, unsigned mode, int seed_errno)
{
    sqlparser_dialect_multi_insert_value_t av, bv;
    sqlparser_error_t ae = seeded_error(), be = ae;
    sqlparser_status_t as, bs;
    uint32_t af = 0xa5a5U, bf = af;
    int ap = 37, bp = ap, aerrno, berrno;
    size_t al = 12345U, bl = al;
    fixture = sql;
    CHECK(sqlparser_oracle_value_identity_kind(sql, &al) == valuefacts_frozen_value_identity_kind(sql, &bl));
    CHECK(al == bl);
    errno = seed_errno;
    bs = valuefacts_frozen_parse_value_item_with_span_proof(sql, start, end, b, &bv,
        mode & 1U ? NULL : &bf, mode & 2U ? NULL : &bp, mode & 4U ? NULL : &be);
    berrno = errno;
    errno = seed_errno;
    as = sqlparser_oracle_parse_value_item_with_span_proof(sql, start, end, a, &av,
        mode & 1U ? NULL : &af, mode & 2U ? NULL : &ap, mode & 4U ? NULL : &ae);
    aerrno = errno;
    CHECK(as == bs && af == bf && ap == bp && aerrno == berrno);
    same_error(&ae, &be); same_value(&av, &bv); same_state(a, b);
    if (as == SQLPARSER_STATUS_OK) {
        CHECK(av.public_sql && av.parser_sql && av.public_sql != av.parser_sql);
        CHECK(bv.public_sql && bv.parser_sql && bv.public_sql != bv.parser_sql);
        /* Verify ownership independence even when both strings have equal bytes. */
        if (av.public_sql[0]) {
            char saved = av.public_sql[0], parser_saved = av.parser_sql[0];
            av.public_sql[0] ^= 1; CHECK(av.parser_sql[0] == parser_saved); av.public_sql[0] = saved;
        }
    }
    sqlparser_oracle_value_clear(&av); sqlparser_oracle_value_clear(&bv);
    item_checks++;
}

static void compare_literal(const char *sql, int seed_errno)
{
    sqlparser_dialect_multi_insert_value_t a, b;
    sqlparser_error_t ae = seeded_error(), be = ae;
    sqlparser_status_t as, bs;
    uint32_t af = 0x8000U, bf = af;
    int aerrno, berrno;
    fixture = sql; memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.public_sql = sqlparser_strdup(sql); b.public_sql = sqlparser_strdup(sql);
    CHECK(a.public_sql && b.public_sql);
    errno = seed_errno; bs = valuefacts_frozen_value_fill_literal(&b, &bf, &be); berrno = errno;
    errno = seed_errno; as = sqlparser_oracle_value_fill_literal(&a, &af, &ae); aerrno = errno;
    CHECK(as == bs && af == bf && aerrno == berrno);
    same_error(&ae, &be); same_value(&a, &b);
    sqlparser_oracle_value_clear(&a); sqlparser_oracle_value_clear(&b); literal_checks++;
}

static void matrix(void)
{
    static const int errno_seeds[] = {0, EDOM, ERANGE};
    static const size_t lengths[] = {253, 254, 255, 256, 257, 511, 512, 513};
    sqlparser_oracle_state_t *a, *b;
    sqlparser_error_t e = seeded_error();
    char sql[4096];
    unsigned mode, byte;
    size_t i, j, k, n;
    int doubled;
    for (mode = 0; mode < 8U; mode++) {
        CHECK(sqlparser_oracle_state_new(&a, &e) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_oracle_state_new(&b, &e) == SQLPARSER_STATUS_OK);
        for (j = 0; j < COUNT(errno_seeds); j++) for (i = 0; i < COUNT(valuefacts_cases); i++) {
            compare_item(a, b, valuefacts_cases[i], 0U, strlen(valuefacts_cases[i]), mode, errno_seeds[j]);
            if (mode == 0U) compare_literal(valuefacts_cases[i], errno_seeds[j]);
        }
        for (i = 0; i < COUNT(valuefacts_cases); i++) {
            n = strlen(valuefacts_cases[i]); CHECK(n + 16U < sizeof(sql));
            memcpy(sql, "prefix\t ", 8U); memcpy(sql + 8U, valuefacts_cases[i], n);
            memcpy(sql + 8U + n, "\r\nsuffix", 9U);
            compare_item(a, b, sql, 6U, 10U + n, mode, EDOM);
        }
        for (i = 0; i < COUNT(lengths); i++) for (doubled = 0; doubled <= 1; doubled++) {
            n = 0U; sql[n++] = '\'';
            for (k = 0; k < lengths[i]; k++) { sql[n++] = doubled ? '\'' : 'a'; if (doubled) sql[n++] = '\''; }
            sql[n++] = '\''; sql[n] = '\0'; compare_item(a, b, sql, 0U, n, mode, ERANGE);
        }
        /* State remains live across ordinary success and syntax-error inputs. */
        compare_item(a, b, "'after :p ? $1 @x /* */ --'", 0U, strlen("'after :p ? $1 @x /* */ --'"), mode, 0);
        sqlparser_oracle_state_destroy(a); sqlparser_oracle_state_destroy(b);
    }
    CHECK(sqlparser_oracle_state_new(&a, &e) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_oracle_state_new(&b, &e) == SQLPARSER_STATUS_OK);
    for (byte = 128U; byte <= 255U; byte++) {
        sql[0] = '\''; sql[1] = (char)byte; sql[2] = '\''; sql[3] = '\0';
        compare_item(a, b, sql, 0U, 3U, 0U, EDOM);
        sql[0] = (char)byte; sql[1] = '1'; sql[2] = '\0';
        compare_item(a, b, sql, 0U, 2U, 0U, EDOM);
        sql[0] = '1'; sql[1] = (char)byte; sql[2] = '\0';
        compare_item(a, b, sql, 0U, 2U, 0U, EDOM);
        /* Real locale folds, never stubbed ctype tables. Include every high
         * byte that the legacy case comparison aliases to an ASCII keyword. */
        for (i = 0; i < COUNT(valuefacts_cases); i++) {
            const char *s = valuefacts_cases[i];
            if (strcmp(s, "null") && strncmp(s, "current_", 8U) &&
                strncmp(s, "localtime", 9U) && strcmp(s, "session_user") && strcmp(s, "user")) continue;
            for (k = 0; s[k]; k++) if (tolower((unsigned char)s[k]) == tolower((unsigned char)byte)) {
                strcpy(sql, s); sql[k] = (char)byte;
                compare_item(a, b, sql, 0U, strlen(sql), 0U, EDOM); highbyte_aliases++;
            }
        }
    }
    sqlparser_oracle_state_destroy(a); sqlparser_oracle_state_destroy(b);
}

int main(void)
{
    size_t i;
    fixture = "initialization"; active_locale = "C";
    for (i = 0; i < COUNT(valuefacts_locales); i++) {
        if (!setlocale(LC_CTYPE, valuefacts_locales[i])) { CHECK(i != 0U); continue; }
        active_locale = valuefacts_locales[i]; locale_checks++; matrix();
    }
    CHECK(setlocale(LC_CTYPE, "C"));
    printf("Oracle value facts: %zu item, %zu literal comparisons; %zu real high-byte aliases; %zu locales passed\n",
        item_checks, literal_checks, highbyte_aliases, locale_checks);
    return 0;
}
