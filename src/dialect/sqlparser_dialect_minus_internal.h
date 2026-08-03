#ifndef SQLPARSER_DIALECT_MINUS_INTERNAL_H
#define SQLPARSER_DIALECT_MINUS_INTERNAL_H

#include "sqlparser_dialect_internal.h"

typedef struct {
	size_t ordinal;
	PgQuery__SelectStmt *owner;
} sqlparser_dialect_minus_t;

typedef struct {
	sqlparser_dialect_minus_t *items;
	size_t count;
	size_t capacity;
	size_t except_count;
	size_t fragment_start;
	size_t fragment_except_base;
} sqlparser_dialect_minuses_t;

void sqlparser_dialect_minuses_clear(sqlparser_dialect_minuses_t *minuses);
sqlparser_status_t sqlparser_dialect_minuses_append(
	sqlparser_dialect_minuses_t *minuses,
	size_t ordinal,
	sqlparser_error_t *out_error);
int sqlparser_dialect_minuses_match(
	const sqlparser_dialect_minuses_t *minuses,
	size_t ordinal);
sqlparser_status_t sqlparser_dialect_minuses_clone(
	const sqlparser_dialect_minuses_t *source,
	sqlparser_dialect_minuses_t *target,
	sqlparser_error_t *out_error);
void sqlparser_dialect_minuses_begin_fragment(
	sqlparser_dialect_minuses_t *minuses);
sqlparser_status_t sqlparser_dialect_minuses_bind_ast(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_minuses_bind_fragment(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__ParseResult *base_ast,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error);
void sqlparser_dialect_minuses_reconcile(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__ParseResult *ast);
sqlparser_status_t sqlparser_dialect_minuses_clone_owners(
	sqlparser_dialect_minuses_t *minuses,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error);

#endif
