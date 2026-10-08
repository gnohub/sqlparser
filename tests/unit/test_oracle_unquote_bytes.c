/* Independent pre-optimization decoder versus production, including every
 * realloc failure, exact sizes/old-buffer presence, errors, flags and decoded
 * bytes. Include Oracle TU and link its static library without whole-archive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL decoder line %d: %s\n", __LINE__, #x); abort(); } } while (0)
typedef struct { size_t size; int old, success; } event;
static event events[32];
static size_t calls, failure;
static void *live;
static void *test_realloc(void *p, size_t n)
{
    void *q;
    CHECK(calls < 32U && (!p || p == live));
    events[calls].size = n;
    events[calls].old = p != NULL;
    events[calls].success = 0;
    ++calls;
    if (calls == failure) return NULL;
    q = realloc(p, n);
    CHECK(q != NULL);
    events[calls - 1U].success = 1;
    live = q;
    return q;
}
static void test_free(void *p)
{
    if (p) { CHECK(p == live); live = NULL; }
    free(p);
}
#define realloc test_realloc
#define free test_free
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#undef realloc
#undef free
static sqlparser_status_t frozen_unquote(
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
		if (text[pos] == '\'' && pos + 1U < len - 1U && text[pos + 1U] == '\'') {
			if (sqlparser_oracle_buffer_append_char(&out, '\'', out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			pos += 2U;
			continue;
		}
		/* Record a stricter fact during the existing decode, without changing
		 * the legacy permissive literal classification or its errors. */
		if (text[pos] == '\'' || text[pos] == '\\' ||
		    (unsigned char)text[pos] < 32U || (unsigned char)text[pos] == 127U)
			ordinary = 0;
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
	if (*out_value != NULL && ordinary && out_lexical_flags != NULL)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_ORDINARY_STRING;
	return *out_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

typedef struct {
    sqlparser_status_t status;
    sqlparser_error_t error;
    uint32_t flags;
    size_t count;
    event allocations[32];
    int has_value;
    char decoded[4096];
} result;
typedef sqlparser_status_t (*decoder)(const char *, char **, uint32_t *, sqlparser_error_t *);
static void run(decoder fn, const char *input, size_t fail, int null_error, result *r)
{
    char *value = NULL;
    memset(r, 0, sizeof(*r));
    r->flags = 0x80U;
    r->error.code = SQLPARSER_STATUS_INVALID_ARGUMENT;
    strcpy(r->error.message, "seeded error");
    CHECK(live == NULL);
    calls = 0U; failure = fail;
    memset(events, 0, sizeof(events));
    r->status = fn(input, &value, &r->flags, null_error ? NULL : &r->error);
    r->count = calls;
    memcpy(r->allocations, events, sizeof(events));
    r->has_value = value != NULL;
    if (value) {
        CHECK(value != input && strlen(value) < sizeof(r->decoded));
        strcpy(r->decoded, value);
    }
    test_free(value);
    CHECK(live == NULL);
}
static void verify(const char *text)
{
    result a, b;
    size_t fail, total;
    int null_error;
    run(frozen_unquote, text, 0U, 0, &a);
    total = a.count;
    for (null_error = 0; null_error < 2; ++null_error) {
        for (fail = 0U; fail <= total + 1U; ++fail) {
            run(frozen_unquote, text, fail, null_error, &a);
            run(sqlparser_oracle_unquote_string_literal, text, fail, null_error, &b);
            CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        }
    }
}
int main(void)
{
    static const size_t lengths[] = {0,1,127,128,129,254,255,256,257,510,511,512,513,1024};
    static const char *const tokens[] = {NULL,"","x","'","''","'a'b'","'a''b'","'\\x'","'a\tb'","'\177'","'张三李四'","'unterminated","unquoted'"};
    size_t i, j, style;
    char token[4096];
    for (i = 0U; i < sizeof(tokens)/sizeof(tokens[0]); ++i) verify(tokens[i]);
    for (i = 0U; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
        for (style = 0U; style < 3U; ++style) {
            size_t pos = 0U;
            token[pos++] = '\'';
            for (j = 0U; j < lengths[i]; ++j) {
                if (style == 1U && j % 7U == 0U) { token[pos++] = '\''; token[pos++] = '\''; }
                else token[pos++] = style == 2U && j == lengths[i]/2U ? '\t' : 'a';
            }
            token[pos++] = '\''; token[pos] = '\0';
            verify(token);
        }
    }
    puts("Oracle decoder bytes, lexical flags and allocation-failure parity passed");
    return 0;
}
