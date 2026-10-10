/* Ordinary-input differential tests only: no fault hooks or clone calls. */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_oracle.c"
#include "../oracle_valuefacts/frozen_value_path.inc"
#include "../oracle_valuefacts/cases.h"

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
