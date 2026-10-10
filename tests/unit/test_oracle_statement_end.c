/* Ordinary scanner differential tests. Exact-size heap strings and legal
 * starts only; no allocation hooks, protected pages, or sanitizer execution. */
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#define LISTBOUNDS_FROZEN_HELPERS_ONLY
#include "../oracle_listbounds/frozen_scanner_reference.inc"
#undef LISTBOUNDS_FROZEN_HELPERS_ONLY
#include "../oracle_statementend/frozen_statement_end.inc"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static size_t comparisons, strings, locales;
static const char *active_locale = "C";
static const unsigned char *case_bytes;
static size_t case_length, case_start;
static void fail(const char *condition, int line)
{
    size_t i;
    fprintf(stderr, "statement end line %d locale=%s length=%zu start=%zu: %s bytes=",
        line, active_locale, case_length, case_start, condition);
    for (i = 0; i < case_length; i++) fprintf(stderr, "%02x", (unsigned)case_bytes[i]);
    fputc('\n', stderr); abort();
}
#define CHECK(x) do { if (!(x)) fail(#x, __LINE__); } while (0)

static void compare_start(const char *sql, const char *snapshot, size_t length, size_t start)
{
    static const int seeds[] = {0, EDOM, ERANGE};
    size_t actual, reference;
    int aerrno, rerrno, seed = seeds[comparisons % COUNT(seeds)];
    case_bytes = (const unsigned char *)sql; case_length = length; case_start = start;
    CHECK(start <= length);
    errno = seed; reference = statementend_frozen_statement_end(sql, start); rerrno = errno;
    errno = seed; actual = sqlparser_oracle_statement_end(sql, start); aerrno = errno;
    CHECK(actual == reference && actual >= start && actual <= length);
    CHECK(aerrno == rerrno && aerrno == seed);
    CHECK(sql[actual] == ';' || sql[actual] == '\0');
    CHECK(memcmp(sql, snapshot, length + 1U) == 0);
    comparisons++;
}

static void compare_bytes(const unsigned char *bytes, size_t length)
{
    char *sql = malloc(length + 1U), *snapshot = malloc(length + 1U);
    size_t start;
    case_bytes = bytes; case_length = length; case_start = 0U; CHECK(sql && snapshot);
    memcpy(sql, bytes, length); sql[length] = '\0'; memcpy(snapshot, sql, length + 1U);
    CHECK(strlen(sql) == length);
    /* The terminating NUL is the last allocated byte, including one-byte
     * allocations for an empty input. Every start is an absolute index. */
    for (start = 0; start <= length; start++) compare_start(sql, snapshot, length, start);
    free(snapshot); free(sql); strings++;
}

static void compare_text(const char *text)
{
    compare_bytes((const unsigned char *)text, strlen(text));
}

static void byte_pairs(void)
{
    unsigned first, second;
    unsigned char pair[2];
    for (first = 0; first <= 255U; first++) for (second = 0; second <= 255U; second++) {
        pair[0] = (unsigned char)first; pair[1] = (unsigned char)second;
        compare_bytes(pair, first == 0U ? 0U : (second == 0U ? 1U : 2U));
    }
}

static void focused(void)
{
    static const char *const cases[] = {
        "", "abc", ";", ";;", "abc;def;", "abc;", "plain text without delimiter",
        "'a;b';tail", "'a'';b';tail", "'''';tail", "'''", "'", "'unterminated;",
        "\"a;b\";tail", "\"a\"\";b\";tail", "\"\"\"\";tail", "\"\"\"", "\"",
        "N'a;b';tail", "n'a'';b';tail", "aQ'[b;c]';tail", "anq'{b;c}';tail",
        "q'", "nq'", "Q'[", "NQ'{broken;", "nQ'<broken;", "nq'!broken;",
        "$$a;b$$;tail", "$tag$a;b$tag$;tail", "$_a9$a;b$_a9$;tail", "$9$a;b$9$;tail",
        "a$tag$a;b$tag$;tail", "#$tag$a;b$tag$;tail", "_$tag$a;b$tag$;tail",
        "$tag$a;b$wrong$;tail", "$tag", "$", "$$", "$tag$", "$$$", "$$$$;tail",
        "--line;\n;tail", "--line;\r;still\n;tail", "--line;", "-", "--", "---;",
        "/*block;*/;tail", "/**/;tail", "/* outer /* inner ; */ tail; */",
        "/*unterminated;", "/", "/*", "*/;tail", "/-/;tail", "-/-;tail",
        "qQnNqQnN$$/--\n/*x*/'a''b'\"c\"\"d\";tail",
        "\xce\xa9$tag$a;b$tag$;tail", "\335$tag$a;b$tag$;tail",
        "abc\nxyz\r\t\001\177;tail", "select 1;select 'a;b';select 3"
    };
    static const char *const prefixes[] = {"q", "Q", "nq", "NQ", "nQ", "Nq"};
    static const char opens[] = "[{(<!";
    static const char closes[] = "]})>!";
    char text[4096];
    size_t i, j, k, length;
    unsigned byte;
    for (i = 0; i < COUNT(cases); i++) {
        compare_text(cases[i]);
        /* Every truncated prefix has its own legal, tight NUL boundary. */
        for (length = 0; length < strlen(cases[i]); length++) compare_bytes((const unsigned char *)cases[i], length);
    }
    for (i = 0; i < COUNT(prefixes); i++) for (j = 0; j < sizeof(opens) - 1U; j++) {
        CHECK(snprintf(text, sizeof(text), "a%s'%cx;y'\"%c';tail", prefixes[i], opens[j], closes[j]) > 0);
        compare_text(text);
        for (length = 0; length < strlen(text); length++) compare_bytes((const unsigned char *)text, length);
    }
    for (byte = 1U; byte <= 255U; byte++) {
        text[0] = (char)byte; strcpy(text + 1U, "$tag$a;b$tag$;tail"); compare_text(text);
        text[0] = (char)byte; strcpy(text + 1U, "$$a;b$$;tail"); compare_text(text);
        strcpy(text, "$a"); text[2] = (char)byte; strcpy(text + 3U, "$x;y$a");
        text[9] = (char)byte; strcpy(text + 10U, "$;tail"); compare_text(text);
    }
    /* Long ordinary runs and isolated candidates cross common libc scan
     * widths; these are normal allocated inputs, not protected-page probes. */
    for (i = 0; i <= 64U; i++) {
        memset(text, 'a', 1024U + i); strcpy(text + 1024U + i, ";tail"); compare_text(text);
        memset(text, 'a', 1024U + i); text[1024U + i] = '\0'; compare_text(text);
    }
    for (i = 0; i < 512U; i++) {
        uint32_t state = (uint32_t)i + 0x9147U;
        length = 1U + i % 63U;
        for (k = 0; k < length; k++) {
            state = state * 1664525U + 1013904223U;
            text[k] = (char)(1U + (state >> 16) % 255U);
        }
        text[length] = '\0'; compare_text(text);
    }
}

int main(void)
{
    static const char *const names[] = {"C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", "tr_TR", "en_US",
        "English_United States.1252", "Turkish_Turkey.1254"};
    size_t i;
    /* Keep the complete existing six-helper reference closure linked without
     * invoking unrelated delimiter tests. The statement scanner uses four. */
    (void)listbounds_frozen_span_name_byte; (void)listbounds_frozen_find_matching_paren;
    for (i = 0; i < COUNT(names); i++) {
        if (!setlocale(LC_CTYPE, names[i])) { CHECK(i != 0U); continue; }
        active_locale = names[i]; locales++; byte_pairs(); focused();
    }
    CHECK(setlocale(LC_CTYPE, "C"));
    printf("Oracle statement end: %zu comparisons, %zu tight-heap strings, full byte-pair matrix; %zu locales passed\n",
        comparisons, strings, locales);
    return 0;
}
