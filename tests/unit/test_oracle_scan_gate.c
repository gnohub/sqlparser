/* The current implementation is the real Oracle translation unit. Frozen varint3 scanner
 * body differs only by name; both retain the valid NUL-buffer/index contract.
 * NULL or out-of-object indexes were never admitted by this private scanner. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <locale.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#include "../oracle_scangates/frozen_oracle_scanner.inc"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Oracle scanner gate line %d: %s\n",__LINE__,#x); abort(); } } while(0)
static size_t cases;
static void check_text(const char *sql)
{
    size_t i, n = strlen(sql);
    for (i = 0; i <= n; ++i) {
        size_t expected = scangates_frozen_oracle_scanner(sql, i);
        size_t actual = sqlparser_oracle_skip_quoted_or_comment_span(sql, i);
        CHECK(actual == expected); CHECK(actual >= i && actual <= n); ++cases;
    }
}
static uint32_t random_state = UINT32_C(0x902174b3);
static uint32_t next_random(void)
{ random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static void run(void)
{
    static const char *const strings[] = {
        "", "x", "'", "\"", "''", "\"\"", "'a''b'", "\"a\"\"b\"", "'a\\b'", "'unterminated", "\"unterminated",
        "$", "$$", "$$abc$$", "$$a;--/*q'[]'$$", "$tag$abc$tag$", "$a_9$abc$a_9$", "$9$abc$9$", "$a$abc$b$",
        "$tag", "$tag$unterminated", "x$$abc$$", "_$$abc$$", "#$x$body$x$", "9$tag$body$tag$", " $tag$$$tag$",
        "q", "q'", "nq'", "q'[]'", "Q'{a''b}'", "q'(a;b)'", "q'<a>'", "q'!a!'", "q'$a$'", "q' a '",
        "nq'[Ω中]'", "NQ'{x}'", "Nq'(x)'", "nQ'<x>'", "q'[unclosed", "q'!unclosed", "q'", "xq'[a]'",
        "--", "--x\nGO", "--x\r\nGO", "/*", "/**/", "/*x*/GO", "/*outer /* inner */ tail*/",
        "/* '$' q'[]' $$ */", "-- $tag$\n'next'", "a\t\r\n;b", "Ω中é", "\001\037\177\200\377"
    };
    static const unsigned char alphabet[] = "a9_$#qQnN'\"[]{}()<>!/*-; \n\r\t";
    size_t i, j, n; char buffer[132];
    for (i = 0; i < sizeof(strings)/sizeof(strings[0]); ++i) check_text(strings[i]);
    /* Exhaustively cover every one- and two-byte prefix/terminator pair. */
    for (i = 1; i <= 255; ++i) {
        buffer[0] = (char)i; buffer[1] = '\0'; check_text(buffer);
        for (j = 1; j <= 255; ++j) {
            buffer[1] = (char)j; buffer[2] = '\0'; check_text(buffer);
        }
    }
    for (i = 0; i < 6000; ++i) {
        n = next_random() % 129U;
        for (j = 0; j < n; ++j) buffer[j] = (char)alphabet[next_random() % (sizeof(alphabet)-1U)];
        buffer[n] = '\0'; check_text(buffer);
    }
}
int main(void)
{
    CHECK(setlocale(LC_CTYPE,"C") != NULL); run();
    if (setlocale(LC_CTYPE,"C.UTF-8") != NULL) run();
    printf("Oracle dollar-call gate: %zu frozen-scanner offset comparisons passed\n",cases);
    return 0;
}
