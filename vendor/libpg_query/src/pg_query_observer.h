#ifndef PG_QUERY_OBSERVER_H
#define PG_QUERY_OBSERVER_H

#include "pg_query.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PgQuery__ParseResult;

/* Internal, per-call observer. The tree and every pointer reachable from it
 * are borrowed only for the duration of this call. The observer must neither
 * mutate/retain them nor enter libpg_query recursively. Backends without a
 * live protobuf-c tree (notably protobuf C++) do not invoke the observer. */
typedef void (*PgQueryProtobufObserver)(
	const struct PgQuery__ParseResult *tree, void *context);

PgQueryProtobuf pg_query_nodes_to_protobuf_observed(
	const void *obj, PgQueryProtobufObserver observer, void *context);

PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
	const char *input, int parser_options,
	PgQueryProtobufObserver observer, void *context);

/* Shared output-allocation boundary for both serialization implementations. */
void *pg_query_protobuf_alloc_output(size_t size);

/* Internal experimental route: a proved-safe raw-node subset can encode
 * directly; other trees use the unchanged full-tree observer callback. */
PgQueryProtobuf pg_query_nodes_to_protobuf_certified(
    const void *obj, PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified);
PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
    const char *input, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified);

#ifdef __cplusplus
}
#endif

#endif
