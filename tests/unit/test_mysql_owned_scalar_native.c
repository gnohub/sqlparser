/* Run the complete scalar wire/error/native-allocation suite through the new
 * single-use owned plan entry as well as its explicit mismatch fallbacks. */
#include <stdlib.h>
#include <string.h>
#include "src/pg_query_observer.h"

static PgQueryProtobufParseResult owned_scalar_parse(
    const char *sql, int options, PgQueryProtobufObserver observer, void *context,
    size_t *count, int *certified)
{
    PgQueryMysqlOwnedScalarInsertPlan plan = {{0}}, zero = {{0}};
    PgQueryNativeScalarInsertProof proof = {0}, empty = {0};
    PgQueryProtobufParseResult result;
    size_t strings = 0U;
    (void)pg_query_prove_mysql_owned_scalar_insert(sql, strlen(sql), &plan, &strings);
    result = pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(
        sql, strlen(sql), options, observer, context, count, certified, &proof, &plan);
    if (memcmp(&plan, &zero, sizeof(plan)) != 0) abort();
    if ((result.error != NULL || result.parse_tree.data == NULL || !*certified) &&
        memcmp(&proof, &empty, sizeof(proof)) != 0) abort();
    return result;
}
#define pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified owned_scalar_parse
#include "test_scalar_insert_native.c"
