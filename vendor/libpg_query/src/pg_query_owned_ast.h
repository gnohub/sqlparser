#ifndef PG_QUERY_OWNED_AST_H
#define PG_QUERY_OWNED_AST_H

#include "pg_query.h"
#include "protobuf/pg_query.pb-c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every populated node, repeated-field array and nondefault string is an
 * independent malloc allocation. The AST owns no PostgreSQL-context storage
 * and can be mutated, transferred between threads and released with the
 * ordinary protobuf-c free_unpacked routines after parsing has returned.
 * Generated static protobuf string defaults retain their normal semantics. */
typedef struct PgQueryOwnedParseResult
{
    PgQuery__ParseResult *parse_tree;
    char *stderr_buffer;
    PgQueryError *error;
} PgQueryOwnedParseResult;

/* Full ordinary PostgreSQL grammar, with the same options and identifier
 * spelling behavior as the corresponding protobuf entry point. No packed
 * representation is built. SQL errors take precedence over allocation errors;
 * parse_tree == NULL and error == NULL denotes allocation failure. */
PgQueryOwnedParseResult
pg_query_parse_owned_opts_preserving_identifier_spelling(
    const char *input, int parser_options);

void pg_query_free_owned_parse_result(PgQueryOwnedParseResult result);

/* Private native-tree conversion, called while its PostgreSQL context lives.
 * NULL reports allocation failure; every partially constructed node is freed. */
PgQuery__ParseResult *pg_query_nodes_to_owned_ast(const void *obj);

#ifdef __cplusplus
}
#endif

#endif
