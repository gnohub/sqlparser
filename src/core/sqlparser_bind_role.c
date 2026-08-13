#include <string.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_bind_occurrence_internal.h"

static int sqlparser_bind_role_same_level(
	const sqlparser_sqlserver_token_t *token,
	size_t paren_depth,
	size_t block_depth)
{
	return token->paren_depth == paren_depth &&
		token->block_depth == block_depth;
}

static int sqlparser_bind_role_word(
	const sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_sqlserver_token_t *token,
	const char *word)
{
	return sqlparser_sqlserver_token_word_equal(cursor->sql, token, word);
}

static int sqlparser_bind_role_next_token(
	sqlparser_bind_role_cursor_t *cursor,
	sqlparser_sqlserver_token_t *out_token)
{
	if (cursor->has_current) {
		*out_token = cursor->current;
		cursor->has_current = 0;
		return out_token->kind != SQLPARSER_SQLSERVER_TOKEN_EOF;
	}
	if (sqlparser_sqlserver_scanner_next(
		    &cursor->scanner, out_token, NULL) != SQLPARSER_STATUS_OK) {
		memset(out_token, 0, sizeof(*out_token));
		out_token->kind = SQLPARSER_SQLSERVER_TOKEN_EOF;
		return 0;
	}
	return out_token->kind != SQLPARSER_SQLSERVER_TOKEN_EOF;
}

static int sqlparser_bind_role_peek_token(
	sqlparser_bind_role_cursor_t *cursor,
	sqlparser_sqlserver_token_t *out_token)
{
	if (!cursor->has_current) {
		if (sqlparser_sqlserver_scanner_next(
			    &cursor->scanner,
			    &cursor->current,
			    NULL) != SQLPARSER_STATUS_OK) {
			memset(&cursor->current, 0, sizeof(cursor->current));
			cursor->current.kind = SQLPARSER_SQLSERVER_TOKEN_EOF;
		}
		cursor->has_current = 1;
	}
	*out_token = cursor->current;
	return out_token->kind != SQLPARSER_SQLSERVER_TOKEN_EOF;
}

static void sqlparser_bind_role_reset_statement(
	sqlparser_bind_role_cursor_t *cursor)
{
	cursor->exec_arguments = 0;
	cursor->exec_procedure_pending = 0;
	cursor->exec_argument_start = 0;
	cursor->output_clause = 0;
	cursor->output_sink_next = 0;
	cursor->select_prefix = 0;
	cursor->has_previous = 0;
}

static int sqlparser_bind_role_has_line_break(
	const sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_sqlserver_token_t *token)
{
	size_t pos;

	if (cursor == NULL || token == NULL || !cursor->has_previous) {
		return 0;
	}
	for (pos = cursor->previous.end; pos < token->start; pos++) {
		if (cursor->sql[pos] == '\n' || cursor->sql[pos] == '\r') {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_bind_role_statement_start_word(
	const sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_sqlserver_token_t *token)
{
	static const char *const words[] = {
		"delete", "exec", "execute", "insert", "merge", "revert",
		"select", "set", "update"
	};
	size_t index;

	if (token == NULL || token->kind != SQLPARSER_SQLSERVER_TOKEN_WORD ||
	    token->paren_depth != 0U || token->case_depth != 0U) {
		return 0;
	}
	for (index = 0U; index < sizeof(words) / sizeof(words[0]); index++) {
		if (sqlparser_bind_role_word(cursor, token, words[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_bind_role_lookahead_assignment(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_sqlserver_token_t *token,
	int allow_compound)
{
	sqlparser_sqlserver_token_t next;
	sqlparser_sqlserver_scanner_t scanner;

	if (!sqlparser_bind_role_peek_token(cursor, &next)) {
		return 0;
	}
	if (next.kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL ||
	    next.paren_depth != token->paren_depth ||
	    next.block_depth != token->block_depth ||
	    next.case_depth != token->case_depth) {
		return 0;
	}
	if (next.symbol == '=') {
		return 1;
	}
	if (!allow_compound || strchr("+-*/%&^|", next.symbol) == NULL) {
		return 0;
	}
	scanner = cursor->scanner;
	if (sqlparser_sqlserver_scanner_next(
		    &scanner, &next, NULL) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	return next.kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		next.symbol == '=' &&
		next.paren_depth == token->paren_depth &&
		next.block_depth == token->block_depth &&
		next.case_depth == token->case_depth;
}

static int sqlparser_bind_role_process_candidate(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_sqlserver_token_t *surface,
	const sqlparser_bind_token_t *candidate)
{
	int accept;
	int assignment;
	int exec_level;
	int lookahead_equal;
	int role_target;
	int select_level;

	accept = 1;
	exec_level = cursor->exec_arguments &&
		sqlparser_bind_role_same_level(
			surface,
			cursor->exec_paren_depth,
			cursor->exec_block_depth);
	lookahead_equal = sqlparser_bind_role_lookahead_assignment(
		cursor, surface, 0);
	assignment = sqlparser_bind_role_lookahead_assignment(
		cursor, surface, 1);
	role_target = cursor->sql[candidate->start] != '?';
	select_level = cursor->select_prefix &&
		sqlparser_bind_role_same_level(
			surface,
			cursor->select_paren_depth,
			cursor->select_block_depth);
	if (role_target && cursor->output_sink_next &&
	    sqlparser_bind_role_same_level(
		    surface,
		    cursor->output_paren_depth,
		    cursor->output_block_depth)) {
		accept = 0;
		cursor->output_sink_next = 0;
		cursor->output_clause = 0;
	}
	if (role_target && exec_level && cursor->exec_procedure_pending) {
		if (lookahead_equal) {
			/* EXEC @return_status = procedure ... */
			accept = 0;
		} else {
			/* Dynamic EXEC uses the variable itself as a value. */
			cursor->exec_procedure_pending = 0;
		}
	} else if (role_target && exec_level && cursor->exec_argument_start) {
		if (lookahead_equal) {
			accept = 0;
		}
		cursor->exec_argument_start = 0;
	}
	if (role_target && cursor->has_previous &&
	    ((cursor->previous.kind == SQLPARSER_SQLSERVER_TOKEN_WORD &&
	      sqlparser_bind_role_word(cursor, &cursor->previous, "select")) ||
	     (cursor->previous.kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
	      cursor->previous.symbol == ',' &&
	      cursor->previous.paren_depth == surface->paren_depth &&
	      cursor->previous.block_depth == surface->block_depth) ||
	     (cursor->previous.kind == SQLPARSER_SQLSERVER_TOKEN_WORD &&
	      sqlparser_bind_role_word(cursor, &cursor->previous, "set"))) &&
	    assignment) {
		accept = 0;
	}
	if (role_target && select_level) {
		if (assignment) {
			accept = 0;
		}
		if (!cursor->has_previous ||
		    cursor->previous.kind != SQLPARSER_SQLSERVER_TOKEN_WORD ||
		    !sqlparser_bind_role_word(cursor, &cursor->previous, "top")) {
			cursor->select_prefix = 0;
		}
	}
	cursor->previous = *surface;
	cursor->has_previous = 1;
	(void)candidate;
	return accept;
}

static void sqlparser_bind_role_process_token(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_sqlserver_token_t *token)
{
	int exec_word;

	if (sqlparser_bind_role_has_line_break(cursor, token) &&
	    sqlparser_bind_role_statement_start_word(cursor, token)) {
		sqlparser_bind_role_reset_statement(cursor);
	}

	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
	    token->symbol == ';' && token->paren_depth == 0U &&
	    token->case_depth == 0U) {
		sqlparser_bind_role_reset_statement(cursor);
		return;
	}
	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_WORD &&
	    sqlparser_sqlserver_line_is_go(cursor->sql, token->start, NULL)) {
		sqlparser_bind_role_reset_statement(cursor);
		return;
	}
	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_WORD &&
	    token->case_depth == 0U &&
	    (sqlparser_bind_role_word(cursor, token, "if") ||
	     sqlparser_bind_role_word(cursor, token, "else") ||
	     sqlparser_bind_role_word(cursor, token, "begin") ||
	     sqlparser_bind_role_word(cursor, token, "end"))) {
		sqlparser_bind_role_reset_statement(cursor);
	}
	if (cursor->output_sink_next &&
	    sqlparser_bind_role_same_level(
		    token,
		    cursor->output_paren_depth,
		    cursor->output_block_depth)) {
		/* The first token after INTO starts a non-bind sink relation. */
		cursor->output_sink_next = 0;
		cursor->output_clause = 0;
	}
	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_STRING ||
	    token->kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER) {
		if (cursor->select_prefix &&
		    sqlparser_bind_role_same_level(
			    token,
			    cursor->select_paren_depth,
			    cursor->select_block_depth)) {
			cursor->select_prefix = 0;
		}
		if (cursor->exec_arguments && cursor->exec_procedure_pending) {
			cursor->exec_procedure_pending = 0;
			cursor->exec_argument_start =
				token->kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER;
		} else if (cursor->exec_arguments &&
			   cursor->exec_argument_start) {
			cursor->exec_argument_start = 0;
		}
		cursor->previous = *token;
		cursor->has_previous = 1;
		return;
	}
	if (token->kind == SQLPARSER_SQLSERVER_TOKEN_WORD) {
		int select_word;
		int select_modifier;

		exec_word = sqlparser_bind_role_word(cursor, token, "exec") ||
			sqlparser_bind_role_word(cursor, token, "execute");
		select_word = sqlparser_bind_role_word(cursor, token, "select");
		select_modifier =
			sqlparser_bind_role_word(cursor, token, "all") ||
			sqlparser_bind_role_word(cursor, token, "distinct") ||
			sqlparser_bind_role_word(cursor, token, "top") ||
			sqlparser_bind_role_word(cursor, token, "percent") ||
			sqlparser_bind_role_word(cursor, token, "with") ||
			sqlparser_bind_role_word(cursor, token, "ties");
		if (select_word) {
			cursor->select_prefix = 1;
			cursor->select_paren_depth = token->paren_depth;
			cursor->select_block_depth = token->block_depth;
		} else if (cursor->select_prefix && !select_modifier &&
			   sqlparser_bind_role_same_level(
				   token,
				   cursor->select_paren_depth,
				   cursor->select_block_depth)) {
			cursor->select_prefix = 0;
		}
		if (exec_word) {
			cursor->exec_arguments = 1;
			cursor->exec_procedure_pending = 1;
			cursor->exec_argument_start = 0;
			cursor->exec_paren_depth = token->paren_depth;
			cursor->exec_block_depth = token->block_depth;
			cursor->output_clause = 0;
			cursor->output_sink_next = 0;
		} else if (cursor->exec_arguments &&
			   cursor->exec_procedure_pending &&
			   sqlparser_bind_role_word(cursor, token, "as")) {
			cursor->exec_arguments = 0;
			cursor->exec_procedure_pending = 0;
			cursor->exec_argument_start = 0;
		} else if (!cursor->exec_arguments &&
			   sqlparser_bind_role_word(cursor, token, "output")) {
			cursor->output_clause = 1;
			cursor->output_sink_next = 0;
			cursor->output_paren_depth = token->paren_depth;
			cursor->output_block_depth = token->block_depth;
		} else if (cursor->output_clause &&
			   sqlparser_bind_role_word(cursor, token, "into") &&
			   sqlparser_bind_role_same_level(
				   token,
				   cursor->output_paren_depth,
				   cursor->output_block_depth)) {
			cursor->output_sink_next = 1;
		}
		if (cursor->exec_arguments && cursor->exec_procedure_pending &&
		    !exec_word) {
			cursor->exec_procedure_pending = 0;
			cursor->exec_argument_start = 1;
		} else if (cursor->exec_arguments &&
			   cursor->exec_argument_start && !exec_word) {
			cursor->exec_argument_start = 0;
		}
		cursor->previous = *token;
		cursor->has_previous = 1;
		return;
	}
	if (token->kind != SQLPARSER_SQLSERVER_TOKEN_SYMBOL) {
		return;
	}
	if (cursor->exec_arguments && token->symbol == ',' &&
	    sqlparser_bind_role_same_level(
		    token,
		    cursor->exec_paren_depth,
		    cursor->exec_block_depth)) {
		cursor->exec_argument_start = 1;
	}
	if (cursor->exec_arguments && token->symbol == '.' &&
	    cursor->exec_argument_start &&
	    sqlparser_bind_role_same_level(
		    token,
		    cursor->exec_paren_depth,
		    cursor->exec_block_depth)) {
		cursor->exec_procedure_pending = 1;
		cursor->exec_argument_start = 0;
	}
	if (cursor->exec_arguments && token->symbol == '(' &&
	    cursor->exec_procedure_pending &&
	    sqlparser_bind_role_same_level(
		    token,
		    cursor->exec_paren_depth,
		    cursor->exec_block_depth)) {
		cursor->exec_procedure_pending = 0;
	}
	cursor->previous = *token;
	cursor->has_previous = 1;
}

void sqlparser_bind_role_cursor_init(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_handle_t *handle,
	const char *sql)
{
	size_t length;

	if (cursor == NULL) {
		return;
	}
	memset(cursor, 0, sizeof(*cursor));
	cursor->sql = sql;
	length = sql != NULL ? strlen(sql) : 0U;
	cursor->sqlserver = handle != NULL &&
		sqlparser_dialect_is_sqlserver_compatible(handle->dialect);
	sqlparser_bind_role_reset_statement(cursor);
	if (cursor->sqlserver && sql != NULL) {
		(void)sqlparser_sqlserver_scanner_init(
			&cursor->scanner, sql, 0U, length, NULL);
	}
}

int sqlparser_bind_role_cursor_accept(
	sqlparser_bind_role_cursor_t *cursor,
	const sqlparser_bind_token_t *candidate)
{
	sqlparser_sqlserver_token_t token;

	if (cursor == NULL || candidate == NULL || !cursor->sqlserver) {
		return 1;
	}
	while (sqlparser_bind_role_next_token(cursor, &token)) {
		if (token.end <= candidate->start) {
			sqlparser_bind_role_process_token(cursor, &token);
			continue;
		}
		if (token.start == candidate->start &&
		    token.end == candidate->end) {
			return sqlparser_bind_role_process_candidate(
				cursor, &token, candidate);
		}
		cursor->current = token;
		cursor->has_current = 1;
		return 1;
	}
	return 1;
}
