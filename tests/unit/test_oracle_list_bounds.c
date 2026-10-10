/* Pure byte scanner differential test. The production TU is included solely
 * to reach private static helpers. No public parser, constructor, clone, OOM,
 * allocation wrapper, fault injection, or sanitizer path is invoked. */
#include <assert.h>
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#include "../oracle_listbounds/frozen_scanner_reference.inc"

static size_t comparisons;
static size_t complete_columns;
static size_t complete_values;
static size_t locale_runs;
static const char *case_text;
static size_t case_open;
static size_t case_end;
static int case_kind;

static void check_failed(const char *condition, int line)
{
	size_t i;
	fprintf(stderr, "Oracle list bounds line %d: %s; kind=%d open=%zu end=%zu bytes=",
		line, condition, case_kind, case_open, case_end);
	if (case_text == NULL) {
		fputs("NULL", stderr);
	} else {
		for (i = 0U; case_text[i] != '\0'; ++i)
			fprintf(stderr, "%02x", (unsigned char)case_text[i]);
	}
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

#define CHECK(condition) do { if (!(condition)) check_failed(#condition, __LINE__); } while (0)

/* expect_complete: -1 checks soundness only; 0/1 also locks the intended
 * conservative admission behavior of the focused cases. */
static void check_case(const char *sql, size_t open, size_t end, int kind,
	int expect_complete, sqlparser_oracle_list_bounds_t *bounds)
{
	const size_t sentinel = SIZE_MAX - 37U;
	size_t expected_close = sentinel;
	size_t actual_close = sentinel;
	size_t wrapper_close = sentinel;
	size_t null_close = sentinel;
	int expected;
	int actual;
	int wrapper;
	int null_result;

	case_text = sql;
	case_open = open;
	case_end = end;
	case_kind = kind;
	expected = listbounds_frozen_find_matching_paren(sql, open, end, &expected_close);
	actual = sqlparser_oracle_find_matching_paren_with_bounds(
		sql, open, end, &actual_close, kind, bounds);
	wrapper = sqlparser_oracle_find_matching_paren(sql, open, end, &wrapper_close);
	null_result = sqlparser_oracle_find_matching_paren_with_bounds(
		sql, open, end, &null_close, kind, NULL);
	CHECK(actual == expected);
	CHECK(wrapper == expected);
	CHECK(null_result == expected);
	/* Failed matches must preserve the original out_close write behavior. */
	CHECK(actual_close == expected_close);
	CHECK(wrapper_close == expected_close);
	CHECK(null_close == expected_close);
	CHECK(!bounds->complete || actual);
	if (expect_complete >= 0) CHECK(!!bounds->complete == expect_complete);
	if (bounds->complete) {
		listbounds_frozen_delimiters_t reference;
		size_t i;
		CHECK(bounds->start == open + 1U);
		CHECK(bounds->close == actual_close);
		CHECK((int)bounds->kind == kind);
		CHECK(bounds->count >= 1U && bounds->count <= 32U);
		if (kind == SQLPARSER_ORACLE_LIST_COLUMNS) {
			listbounds_frozen_column_delimiters(sql, open + 1U, actual_close, &reference);
			CHECK(reference.span_safe);
			++complete_columns;
		} else {
			CHECK(kind == SQLPARSER_ORACLE_LIST_VALUES);
			listbounds_frozen_value_delimiters(sql, open + 1U, actual_close, &reference);
			++complete_values;
		}
		CHECK(bounds->count == reference.count);
		for (i = 0U; i < reference.count; ++i) CHECK(bounds->ends[i] == reference.ends[i]);
		CHECK(bounds->ends[bounds->count - 1U] == actual_close);
	}
	++comparisons;
}

static void check_fresh(const char *sql, size_t open, size_t end, int kind,
	int expect_complete)
{
	sqlparser_oracle_list_bounds_t bounds;
	/* A stale complete flag and table must never survive a failed/fallback
 * scan. Unsigned bytes avoid evaluating any uninitialized C object. */
	memset(&bounds, 0xa5, sizeof(bounds));
	check_case(sql, open, end, kind, expect_complete, &bounds);
}

static void check_text(const char *sql)
{
	size_t open;
	size_t end;
	size_t length = strlen(sql);
	for (open = 0U; open <= length; ++open) {
		/* Every '(' offset, plus invalid leading/open==end calls. Every bound
 * stays inside the NUL-terminated allocation, including helper lookahead
 * beyond end. end is a scan limit, never a claim of a shorter allocation. */
		if (open != 0U && open != length && sql[open] != '(') continue;
		for (end = open; end <= length; ++end) {
			check_fresh(sql, open, end, SQLPARSER_ORACLE_LIST_COLUMNS, -1);
			check_fresh(sql, open, end, SQLPARSER_ORACLE_LIST_VALUES, -1);
		}
	}
}

static void check_focused(void)
{
	static const struct {
		const char *sql;
		int columns;
		int values;
	} focused[] = {
		{"(a)", 1, 1}, {"( a_1 , B2 , _ )", 1, 1},
		{"(a\t,\r\nb\f,\vc)", 1, 1}, {"()", 0, 1},
		{"(  )", 0, 1}, {"(a,)", 0, 1}, {"(,a)", 0, 1},
		{"(,,)", 0, 1}, {"(a,,b)", 0, 1},
		{"((a,b),c)", 0, 1}, {"(f(1,2),((3)),4)", 0, 1},
		{"(1,2.3,-4,+5,NULL,a+b,:p)", 0, 1},
		{"('a,b','c''d',e)", 0, 1}, {"('','''',a)", 0, 1},
		{"('x)(',a)", 0, 1}, {"('\xce\xa9,\xe4\xb8\xad',b)", 0, 1},
		{"('a\\b',c)", 0, 1}, {"(N'a,b',c)", 0, 1},
		{"(a.b,c)", 0, 1}, {"(a b,c)", 0, 1},
		{"(\"a,b\",c)", 0, 0}, {"(q'[a,b]',c)", 0, 0},
		{"(NQ'{a,b}',c)", 0, 0}, {"($$a,b$$,c)", 0, 0},
		{"($tag$a,b$tag$,c)", 0, 0}, {"(a/*,*/ ,b)", 0, 0},
		{"(a--,\nb,c)", 0, 0}, {"(a[1,2],b)", 0, 0},
		{"(a{1,2},b)", 0, 0}, {"(`a,b`,c)", 0, 0},
		{"(a\\b,c)", 0, 0}, {"(a$b,c)", 0, 0},
		{"(\xce\xa9,b)", 0, 0}, {"(a,\200)", 0, 0},
		{"(a", 0, 0}, {"((a)", 0, 0}, {"('a)", 0, 0},
		{"(a/*)", 0, 0}, {"(a--)", 0, 0}
	};
	size_t i;
	for (i = 0U; i < sizeof(focused) / sizeof(focused[0]); ++i) {
		size_t length = strlen(focused[i].sql);
		check_fresh(focused[i].sql, 0U, length, SQLPARSER_ORACLE_LIST_COLUMNS, focused[i].columns);
		check_fresh(focused[i].sql, 0U, length, SQLPARSER_ORACLE_LIST_VALUES, focused[i].values);
		check_text(focused[i].sql);
	}
}

static void check_capacity(void)
{
	static const size_t counts[] = {1U, 2U, 30U, 31U, 32U, 33U, 34U, 65U};
	char text[512];
	size_t test;
	for (test = 0U; test < sizeof(counts) / sizeof(counts[0]); ++test) {
		size_t i;
		size_t n = counts[test];
		size_t pos = 0U;
		text[pos++] = '(';
		for (i = 0U; i < n; ++i) {
			if (i != 0U) text[pos++] = ',';
			text[pos++] = 'a';
		}
		text[pos++] = ')';
		text[pos] = '\0';
		check_fresh(text, 0U, pos, SQLPARSER_ORACLE_LIST_COLUMNS, n <= 32U);
		check_fresh(text, 0U, pos, SQLPARSER_ORACLE_LIST_VALUES, n <= 32U);
		/* A trailing comma creates one additional empty value item. */
		text[pos - 1U] = ',';
		text[pos++] = ')';
		text[pos] = '\0';
		check_fresh(text, 0U, pos, SQLPARSER_ORACLE_LIST_COLUMNS, 0);
		check_fresh(text, 0U, pos, SQLPARSER_ORACLE_LIST_VALUES, n + 1U <= 32U);
		/* Overflow must not terminate the structural scan: no final ')' is
 * still a failed match, while a later true close still wins. */
		text[pos - 1U] = '\0';
		check_fresh(text, 0U, pos - 1U, SQLPARSER_ORACLE_LIST_COLUMNS, 0);
		check_fresh(text, 0U, pos - 1U, SQLPARSER_ORACLE_LIST_VALUES, 0);
	}
}

static void check_reuse_and_arguments(void)
{
	sqlparser_oracle_list_bounds_t bounds;
	size_t close;
	const char *sql = "prefix (a,b,c) values (1,2)";
	memset(&bounds, 0xa5, sizeof(bounds));
	check_case(sql, 7U, strlen(sql), SQLPARSER_ORACLE_LIST_COLUMNS, 1, &bounds);
	check_case(sql, 22U, strlen(sql), SQLPARSER_ORACLE_LIST_VALUES, 1, &bounds);
	CHECK(bounds.count == 2U);
	check_case("(q'[x,y]',z)", 0U, 12U, SQLPARSER_ORACLE_LIST_VALUES, 0, &bounds);
	check_case("(a)", 0U, 3U, SQLPARSER_ORACLE_LIST_COLUMNS, 1, &bounds);
	check_case("()", 0U, 2U, SQLPARSER_ORACLE_LIST_VALUES, 1, &bounds);
	CHECK(bounds.count == 1U);
	check_case("(x", 0U, 2U, SQLPARSER_ORACLE_LIST_VALUES, 0, &bounds);
	check_case("(a)", 0U, 3U, SQLPARSER_ORACLE_LIST_COLUMNS, 1, &bounds);
	check_case(NULL, 0U, 0U, SQLPARSER_ORACLE_LIST_VALUES, 0, &bounds);
	check_case("x", 0U, 1U, SQLPARSER_ORACLE_LIST_COLUMNS, 0, &bounds);
	check_case("(a)", 3U, 3U, SQLPARSER_ORACLE_LIST_COLUMNS, 0, &bounds);
	check_case("(a,b)", 0U, 5U, 0, 0, &bounds);
	check_case("(a,b)", 0U, 5U, 99, 0, &bounds);
	close = SIZE_MAX;
	CHECK(!listbounds_frozen_find_matching_paren("(a)", 0U, 3U, NULL));
	CHECK(!sqlparser_oracle_find_matching_paren("(a)", 0U, 3U, NULL));
	CHECK(!sqlparser_oracle_find_matching_paren_with_bounds(
		"(a)", 0U, 3U, NULL, SQLPARSER_ORACLE_LIST_VALUES, &bounds));
	CHECK(!bounds.complete);
	CHECK(!sqlparser_oracle_find_matching_paren_with_bounds(
		NULL, 0U, 0U, &close, SQLPARSER_ORACLE_LIST_VALUES, NULL));
	CHECK(close == SIZE_MAX);
}

static void check_reference_semantics(void)
{
	listbounds_frozen_delimiters_t columns;
	listbounds_frozen_delimiters_t values;
	case_text = "frozen-reference self-check";
	case_open = case_end = 0U;
	case_kind = 0;
	/* Assert the odd baseline semantics explicitly; do not silently repair
 * columns' depth-insensitive splitting or normalize empty values. */
	listbounds_frozen_column_delimiters("(f(a,b),c)", 1U, 9U, &columns);
	listbounds_frozen_value_delimiters("(f(a,b),c)", 1U, 9U, &values);
	CHECK(columns.count == 3U && columns.ends[0] == 4U && !columns.span_safe);
	CHECK(values.count == 2U && values.ends[0] == 7U);
	listbounds_frozen_value_delimiters("()", 1U, 1U, &values);
	CHECK(values.count == 1U && values.ends[0] == 1U);
	listbounds_frozen_value_delimiters("(a,)", 1U, 3U, &values);
	CHECK(values.count == 2U && values.ends[0] == 2U && values.ends[1] == 3U);
	/* The original skip helper sees the complete NUL string, not end.
 * Crossing a boundary suppresses even the normal terminal delimiter. */
	listbounds_frozen_column_delimiters("('abc',z)", 1U, 3U, &columns);
	listbounds_frozen_value_delimiters("('abc',z)", 1U, 3U, &values);
	CHECK(columns.count == 0U && !columns.span_safe);
	CHECK(values.count == 0U);
}

static uint32_t random_state = UINT32_C(0xa158df27);
static uint32_t next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void check_generated(void)
{
	static const unsigned char alphabet[] = "ab09_$#qQnN'\"[]{}()<>!/*-+,.:;`\\ \n\r\t\200\377";
	static const char *const boundaries[] = {
		"('a,b',c)tail", "('a''b',c)tail", "(\"a,b\",c)tail",
		"(q'[a,b]',c)tail", "(nq'{a,b}',c)tail", "($$a,b$$,c)tail",
		"($tag$a,b$tag$,c)tail", "(/*a,b*/c,d)tail", "(--a,b\nc,d)tail",
		"prefix ((a,b),c) suffix", "('x') trailing ('z',b)",
		"(q'", "(nq'", "($tag$", "(/*", "(--", "(''", "(\"\"",
		"(a)unrelated(unterminated", "(a))", "((a),)"
	};
	char text[100];
	size_t i;
	size_t j;
	for (i = 0U; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) check_text(boundaries[i]);
	/* Every non-NUL byte and ordered byte pair, always within a real
 * terminated buffer. Both quotes and every high-byte combination occur. */
	for (i = 1U; i <= 255U; ++i) {
		text[0] = '(';
		text[1] = (char)i;
		text[2] = ')';
		text[3] = '\0';
		check_text(text);
		for (j = 1U; j <= 255U; ++j) {
			text[2] = (char)j;
			text[3] = ')';
			text[4] = '\0';
			check_fresh(text, 0U, 4U, SQLPARSER_ORACLE_LIST_COLUMNS, -1);
			check_fresh(text, 0U, 4U, SQLPARSER_ORACLE_LIST_VALUES, -1);
		}
	}
	for (i = 0U; i < 4000U; ++i) {
		size_t n = next_random() % 80U;
		text[0] = '(';
		for (j = 0U; j < n; ++j) text[j + 1U] = (char)alphabet[next_random() % (sizeof(alphabet) - 1U)];
		text[n + 1U] = ')';
		text[n + 2U] = '\0';
		check_text(text);
	}
}

static void run_locale(void)
{
	check_reference_semantics();
	check_focused();
	check_capacity();
	check_reuse_and_arguments();
	check_generated();
	++locale_runs;
}

int main(void)
{
	static const char *const optional_locales[] = {"C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "en_US.ISO-8859-1"};
	size_t i;
	CHECK(setlocale(LC_CTYPE, "C") != NULL);
	run_locale();
	for (i = 0U; i < sizeof(optional_locales) / sizeof(optional_locales[0]); ++i) {
		if (setlocale(LC_CTYPE, optional_locales[i]) != NULL) run_locale();
	}
	printf("Oracle list bounds: %zu frozen matcher comparisons; %zu complete column tables; "
		"%zu complete value tables; %zu locales passed\n",
		comparisons, complete_columns, complete_values, locale_runs);
	return 0;
}
