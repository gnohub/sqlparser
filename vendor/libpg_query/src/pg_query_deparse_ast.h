#ifndef PG_QUERY_DEPARSE_AST_H
#define PG_QUERY_DEPARSE_AST_H

#include "pg_query.h"
#include "postgres_deparse.h"
#include "protobuf/pg_query.pb-c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private borrowed-AST adapters. The caller retains every AST allocation.
 * Native PostgreSQL nodes and copied strings live only for the deparse call.
 * source_slot is the address of a protobuf-C string field, not its value;
 * native_string identifies the corresponding independent PostgreSQL copy. */
typedef void (*PgQueryASTStringReader)(
	void *context, const void *source_slot, const char *native_string);

PgQueryDeparseResult pg_query_deparse_ast(const PgQuery__ParseResult *parse_tree);
PgQueryDeparseResult pg_query_deparse_ast_opts(
	const PgQuery__ParseResult *parse_tree, PostgresDeparseOpts opts);
PgQueryDeparseResult pg_query_deparse_ast_opts_with_reader(
	const PgQuery__ParseResult *parse_tree,
	PostgresDeparseOpts opts,
	PgQueryASTStringReader string_reader,
	void *string_reader_context);

#ifdef __cplusplus
}
#endif

#endif
