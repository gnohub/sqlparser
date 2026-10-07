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

/* Private allocation-free source proof for identity preprocessing only.
 * Success proves the complete default-options scalar INSERT source and clean
 * strings. The required name predicate must also exclude the caller dialect's
 * identifier rejection/rewrite triggers. It receives a borrowed, non-terminated
 * ASCII name span, is called only after complete source certification, and must
 * not retain/mutate the input, allocate, or change parser/error state.
 * All proof fields are scalar metadata; no input pointer is retained. A miss
 * zeros the output and must use ordinary preprocessing and its diagnostics.
 * This is not native-tree, graph, or wire provenance. */
typedef struct PgQueryIdentityScalarInsertProof
{
    size_t source_length;
    size_t row_count;
    size_t column_count;
    size_t string_count;
    int statement_length;
} PgQueryIdentityScalarInsertProof;
typedef int (*PgQueryIdentityScalarInsertNamePredicate)(
    const char *name, size_t length);
int pg_query_prove_identity_scalar_insert(
    const char *input, PgQueryIdentityScalarInsertNamePredicate name_predicate,
    PgQueryIdentityScalarInsertProof *proof);
/* Keep the existing MySQL-only policy at its caller boundary. */
int pg_query_prove_mysql_identity_scalar_insert(
    const char *input, PgQueryIdentityScalarInsertProof *proof);

/* Private native-constructor attestation for the narrower initial scalar
 * graph subset. Published only after the exact constructed tree has been
 * successfully serialized by the canonical certified writer. No parse-context
 * pointer survives. An all-zero record means no attestation. */
typedef struct PgQueryNativeScalarInsertProof
{
    size_t source_length;
    size_t row_count;
    size_t column_count;
    size_t string_count;
    size_t text_bytes;
    int statement_length;
} PgQueryNativeScalarInsertProof;

/* Shared output-allocation boundary for both serialization implementations. */
void *pg_query_protobuf_alloc_output(size_t size);

/* Internal certified route: a proved-safe raw-node subset can encode
 * directly; other trees use the unchanged full-tree observer callback. */
PgQueryProtobuf pg_query_nodes_to_protobuf_certified(
    const void *obj, PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified);
PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
    const char *input, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified);
PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
    const char *input, int parser_options,
    PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified,
    PgQueryNativeScalarInsertProof *native_proof);

/* Validation-only route: always use the ordinary grammar. Certification is
 * produced by the existing canonical writer over that exact native tree.
 * No specialized constructor, source recognizer or native proof is enabled.
 * A writer/backend miss retains ordinary observed(NULL) serialization. */
PgQueryProtobufParseResult
pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_ordinary(
    const char *input, int parser_options,
    size_t *statement_count, int *certified);

#ifdef __cplusplus
}
#endif

#endif
