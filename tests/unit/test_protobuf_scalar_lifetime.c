/* Raw-tree scalar text is borrowed only until synchronous protobuf packing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

#ifdef SQLPARSER_SCALAR_COPY_COUNTS
static int converting;
static size_t conversion_calls, scalar_copies;
char *__real_pstrdup(const char *);
char *__wrap_pstrdup(const char *text)
{
    if (converting) scalar_copies++;
    return __real_pstrdup(text);
}
PgQueryProtobuf __real_pg_query_nodes_to_protobuf_observed(
    const void *, PgQueryProtobufObserver, void *);
PgQueryProtobuf __wrap_pg_query_nodes_to_protobuf_observed(
    const void *tree, PgQueryProtobufObserver observer, void *context)
{
    PgQueryProtobuf result;
    CHECK(!converting);
    conversion_calls++;
    converting = 1;
    result = __real_pg_query_nodes_to_protobuf_observed(tree, observer, context);
    converting = 0;
    return result;
}
#endif

typedef struct {
    PgQuery__AConst__ValCase kind;
    const char *text;
    int integer;
} scalar_t;

typedef struct {
    const scalar_t *expected;
    size_t count, calls, size;
    uint8_t *bytes;
} observation_t;

static void check_scalars(const PgQuery__ParseResult *tree,
                          const observation_t *expected)
{
    const PgQuery__SelectStmt *select;
    size_t i;
    CHECK(tree != NULL && tree->n_stmts == 1U && tree->stmts != NULL);
    CHECK(tree->stmts[0] != NULL && tree->stmts[0]->stmt != NULL);
    CHECK(tree->stmts[0]->stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT);
    select = tree->stmts[0]->stmt->select_stmt;
    CHECK(select != NULL && select->n_target_list == expected->count);
    for (i = 0U; i < expected->count; i++) {
        const PgQuery__Node *target = select->target_list[i];
        const PgQuery__AConst *value;
        CHECK(target != NULL && target->node_case == PG_QUERY__NODE__NODE_RES_TARGET);
        CHECK(target->res_target != NULL && target->res_target->val != NULL);
        CHECK(target->res_target->val->node_case == PG_QUERY__NODE__NODE_A_CONST);
        value = target->res_target->val->a_const;
        CHECK(value != NULL && value->val_case == expected->expected[i].kind);
        switch (value->val_case) {
            case PG_QUERY__A__CONST__VAL_SVAL:
                CHECK(value->sval != NULL);
                CHECK(strcmp(value->sval->sval, expected->expected[i].text) == 0);
                break;
            case PG_QUERY__A__CONST__VAL_FVAL:
                CHECK(value->fval != NULL);
                CHECK(strcmp(value->fval->fval, expected->expected[i].text) == 0);
                break;
            case PG_QUERY__A__CONST__VAL_BSVAL:
                CHECK(value->bsval != NULL);
                CHECK(strcmp(value->bsval->bsval, expected->expected[i].text) == 0);
                break;
            case PG_QUERY__A__CONST__VAL_IVAL:
                CHECK(value->ival != NULL && value->ival->ival == expected->expected[i].integer);
                break;
            case PG_QUERY__A__CONST__VAL_BOOLVAL:
                CHECK(value->boolval != NULL && value->boolval->boolval == expected->expected[i].integer);
                break;
            case PG_QUERY__A__CONST__VAL__NOT_SET:
                CHECK(value->isnull);
                break;
            default:
                CHECK(0);
        }
    }
}

static void observe(const PgQuery__ParseResult *tree, void *context)
{
    observation_t *observation = context;
    observation->calls++;
    check_scalars(tree, observation);
    observation->size = pg_query__parse_result__get_packed_size(tree);
    observation->bytes = malloc(observation->size);
    CHECK(observation->bytes != NULL);
    CHECK(pg_query__parse_result__pack(tree, observation->bytes) == observation->size);
    /* Retain only independently packed bytes, never borrowed tree pointers. */
}

static void exercise(const char *sql, const scalar_t *expected, size_t count)
{
    observation_t observation = {expected, count, 0U, 0U, NULL};
    PgQueryProtobufParseResult normal, observed, churn;
    PgQuery__ParseResult *unpacked;
    char *input = strdup(sql);
    size_t i;
    CHECK(input != NULL);
    sqlparser_pg_query_prepare();
    /* The original can be read-only storage; both entry points must leave it intact. */
    normal = pg_query_parse_protobuf_opts(sql, PG_QUERY_PARSE_DEFAULT);
    observed = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        input, PG_QUERY_PARSE_DEFAULT, observe, &observation);
    CHECK(normal.error == NULL && observed.error == NULL);
    CHECK(normal.parse_tree.data != NULL && observed.parse_tree.data != NULL);
    CHECK(strcmp(input, sql) == 0);
    CHECK(normal.parse_tree.len == observed.parse_tree.len);
    CHECK(memcmp(normal.parse_tree.data, observed.parse_tree.data, normal.parse_tree.len) == 0);
    CHECK(observation.calls <= 1U); /* The protobuf C++ backend has no live observer. */
    if (observation.calls != 0U) {
        CHECK(observation.size == observed.parse_tree.len);
        CHECK(memcmp(observation.bytes, observed.parse_tree.data, observation.size) == 0);
    }
    memset(input, 'z', strlen(input));
    free(input);
    /* Parsing has already destroyed the raw-tree context; aggressively reuse it. */
    for (i = 0U; i < 32U; i++) {
        churn = pg_query_parse_protobuf("SELECT 'overwrite old context', 98.765, B'11110000'");
        CHECK(churn.error == NULL && churn.parse_tree.data != NULL);
        pg_query_free_protobuf_parse_result(churn);
    }
    CHECK(memcmp(normal.parse_tree.data, observed.parse_tree.data, normal.parse_tree.len) == 0);
    if (observation.calls != 0U)
        CHECK(memcmp(observation.bytes, observed.parse_tree.data, observation.size) == 0);
    unpacked = pg_query__parse_result__unpack(NULL, observed.parse_tree.len,
        (const uint8_t *)observed.parse_tree.data);
    CHECK(unpacked != NULL);
    pg_query_free_protobuf_parse_result(normal);
    pg_query_free_protobuf_parse_result(observed);
    free(observation.bytes);
    /* The public unpacked AST owns its strings independently of either buffer. */
    check_scalars(unpacked, &observation);
    pg_query__parse_result__free_unpacked(unpacked, NULL);
}

int main(void)
{
    static const scalar_t expected[] = {
        {PG_QUERY__A__CONST__VAL_SVAL, "ordinary", 0},
        {PG_QUERY__A__CONST__VAL_SVAL, "", 0},
        {PG_QUERY__A__CONST__VAL_SVAL, "a'b", 0},
        {PG_QUERY__A__CONST__VAL_SVAL, "line\nnext", 0},
        {PG_QUERY__A__CONST__VAL_SVAL, "UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd", 0},
        {PG_QUERY__A__CONST__VAL_FVAL, "123.45", 0},
        {PG_QUERY__A__CONST__VAL_FVAL, "1e20", 0},
        {PG_QUERY__A__CONST__VAL_FVAL, "-1.25", 0},
        {PG_QUERY__A__CONST__VAL_FVAL, "999999999999999999999", 0},
        {PG_QUERY__A__CONST__VAL_BSVAL, "b0101", 0},
        {PG_QUERY__A__CONST__VAL_BSVAL, "xdeadbeef", 0},
        {PG_QUERY__A__CONST__VAL_IVAL, NULL, 17},
        {PG_QUERY__A__CONST__VAL_BOOLVAL, NULL, 1},
        {PG_QUERY__A__CONST__VAL_BOOLVAL, NULL, 0},
        {PG_QUERY__A__CONST__VAL__NOT_SET, NULL, 0}
    };
    scalar_t large_expected = {PG_QUERY__A__CONST__VAL_SVAL, NULL, 0};
    char *large_text = malloc(70001U), *large_sql = malloc(70010U);
    CHECK(large_text != NULL && large_sql != NULL);
    exercise("SELECT 'ordinary', '', 'a''b', E'line\\nnext', "
        "'UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd', 123.45, 1e20, -1.25, "
        "999999999999999999999, B'0101', X'deadbeef', 17, TRUE, FALSE, NULL",
        expected, sizeof(expected) / sizeof(expected[0]));
    memset(large_text, 'x', 70000U);
    large_text[70000U] = '\0';
    snprintf(large_sql, 70010U, "SELECT '%s'", large_text);
    large_expected.text = large_text;
    exercise(large_sql, &large_expected, 1U);
    free(large_text);
    free(large_sql);
#ifdef SQLPARSER_SCALAR_COPY_COUNTS
    CHECK(conversion_calls > 0U && scalar_copies == 0U);
#endif
    puts("protobuf scalar observer, input immutability and packed/unpacked lifetime checks passed");
    return 0;
}
