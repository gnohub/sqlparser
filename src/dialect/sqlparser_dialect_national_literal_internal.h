#ifndef SQLPARSER_DIALECT_NATIONAL_LITERAL_INTERNAL_H
#define SQLPARSER_DIALECT_NATIONAL_LITERAL_INTERNAL_H

#include "sqlparser_dialect_internal.h"

typedef struct {
	char *sql;
	char *surface_sql;
	size_t ordinal;
	PgQuery__AConst *owner;
} sqlparser_dialect_national_literal_t;

typedef struct {
	sqlparser_dialect_national_literal_t *items;
	size_t count;
	size_t capacity;
	size_t literal_count;
	size_t fragment_start;
	size_t fragment_literal_base;
} sqlparser_dialect_national_literals_t;

void sqlparser_dialect_national_literals_clear(
	sqlparser_dialect_national_literals_t *literals);
sqlparser_status_t sqlparser_dialect_national_literals_append(
	sqlparser_dialect_national_literals_t *literals,
	const char *literal_sql,
	size_t literal_sql_length,
	size_t ordinal,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_national_literals_append_surface(
	sqlparser_dialect_national_literals_t *literals,
	const char *literal_sql,
	size_t literal_sql_length,
	const char *surface_sql,
	size_t surface_sql_length,
	size_t ordinal,
	sqlparser_error_t *out_error);
int sqlparser_dialect_national_literals_match(
	const sqlparser_dialect_national_literals_t *literals,
	size_t ordinal,
	const char *sql,
	size_t start,
	size_t end);
const sqlparser_dialect_national_literal_t *
sqlparser_dialect_national_literals_find_owner(
	const sqlparser_dialect_national_literals_t *literals,
	const PgQuery__AConst *owner);
sqlparser_status_t sqlparser_dialect_national_literals_clone(
	const sqlparser_dialect_national_literals_t *source,
	sqlparser_dialect_national_literals_t *target,
	sqlparser_error_t *out_error);
void sqlparser_dialect_national_literals_begin_fragment(
	sqlparser_dialect_national_literals_t *literals);
sqlparser_status_t sqlparser_dialect_national_literals_bind_ast(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_national_literals_bind_fragment(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__ParseResult *base_ast,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error);
void sqlparser_dialect_national_literals_reconcile(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__ParseResult *ast);
sqlparser_status_t sqlparser_dialect_national_literals_clone_owners(
	sqlparser_dialect_national_literals_t *literals,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error);

#endif
