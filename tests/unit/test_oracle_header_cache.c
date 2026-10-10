/* Ordinary header-template/root differential; immutable input owners stay
 * alive for every cache use. No allocation hooks, faults, or clone calls. */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#include "../oracle_headercache/frozen_header_path.inc"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *fixture = "initialization", *active_locale = "C";
static size_t branch_checks, root_checks, matches, locale_checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "header cache line %d locale=%s case=%s: %s\n", __LINE__, active_locale, fixture, #x); abort(); } } while (0)
#include "../oracle_headercache/compare_helpers.inc"

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
