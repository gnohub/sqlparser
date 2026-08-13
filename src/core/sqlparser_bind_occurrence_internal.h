#ifndef SQLPARSER_BIND_OCCURRENCE_INTERNAL_H
#define SQLPARSER_BIND_OCCURRENCE_INTERNAL_H

#include "sqlparser_internal.h"
#include "../dialect/sqlparser_dialect_sqlserver_scan.h"

typedef struct {
	const char *sql;
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t current;
	sqlparser_sqlserver_token_t previous;
	int has_current;
	int has_previous;
	int sqlserver;
	int exec_arguments;
	int exec_procedure_pending;
	int exec_argument_start;
	int output_clause;
	int output_sink_next;
	int select_prefix;
	size_t exec_paren_depth;
	size_t exec_block_depth;
	size_t output_paren_depth;
	size_t output_block_depth;
	size_t select_paren_depth;
	size_t select_block_depth;
} sqlparser_bind_role_cursor_t;

void sqlparser_bind_role_cursor_init(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_handle_t *handle,
	const char *sql);

int sqlparser_bind_role_cursor_accept(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_bind_token_t *token);

#endif
