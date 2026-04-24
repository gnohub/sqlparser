#ifndef SQLPARSER_INTERNAL_H
#define SQLPARSER_INTERNAL_H

#include <stddef.h>

#include "pg_query.h"
#include "protobuf/pg_query.pb-c.h"
#include "sqlparser/sqlparser.h"

struct sqlparser_handle {
	char *sql;
	char *current_sql;
	size_t sql_len;
	size_t statement_count;
	PgQueryProtobuf parse_tree;
	char *parse_tree_json;
	char *model_json;
	PgQueryProtobuf scan;
	PgQueryProtobuf summary;
	PgQuery__ParseResult *ast;
	sqlparser_limits_t limits;
	unsigned long generation;
};

void sqlparser_error_clear(sqlparser_error_t *out_error);
void sqlparser_error_set_message(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *message);
void sqlparser_error_from_pg(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *sql,
	const PgQueryError *error);

char *sqlparser_strdup(const char *text);
char *sqlparser_strndup(const char *text, size_t len);
void sqlparser_pg_query_prepare(void);
sqlparser_status_t sqlparser_validate_handle_sql_input(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_validate_handle_output_text(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_handle_ensure_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
void sqlparser_handle_clear_ast(sqlparser_handle_t *handle);
void sqlparser_handle_invalidate_derived(sqlparser_handle_t *handle);
sqlparser_status_t sqlparser_handle_commit_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);

#endif
