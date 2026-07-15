#ifndef SQLPARSER_DIALECT_SQLSERVER_SCAN_H
#define SQLPARSER_DIALECT_SQLSERVER_SCAN_H

#include <stddef.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_SQLSERVER_TOKEN_EOF = 0,
	SQLPARSER_SQLSERVER_TOKEN_WORD,
	SQLPARSER_SQLSERVER_TOKEN_STRING,
	SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER,
	SQLPARSER_SQLSERVER_TOKEN_SYMBOL
} sqlparser_sqlserver_token_kind_t;

enum {
	SQLPARSER_SQLSERVER_CANDIDATE_INSERT = 1U,
	SQLPARSER_SQLSERVER_CANDIDATE_MERGE = 2U,
	SQLPARSER_SQLSERVER_CANDIDATE_OUTPUT = 4U,
	SQLPARSER_SQLSERVER_CANDIDATE_CONTROL = 8U
};

typedef struct {
	sqlparser_sqlserver_token_kind_t kind;
	size_t start;
	size_t end;
	size_t paren_depth;
	size_t block_depth;
	size_t case_depth;
	char symbol;
} sqlparser_sqlserver_token_t;

typedef struct {
	const char *sql;
	size_t end;
	size_t pos;
	size_t paren_depth;
	size_t block_depth;
	size_t case_depth;
	sqlparser_status_t status;
	int done;
} sqlparser_sqlserver_scanner_t;

sqlparser_status_t sqlparser_sqlserver_error_at(
	const char *sql,
	size_t pos,
	const char *message,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_scanner_init(
	sqlparser_sqlserver_scanner_t *scanner,
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_scanner_next(
	sqlparser_sqlserver_scanner_t *scanner,
	sqlparser_sqlserver_token_t *out_token,
	sqlparser_error_t *out_error);

int sqlparser_sqlserver_token_word_equal(
	const char *sql,
	const sqlparser_sqlserver_token_t *token,
	const char *word);

int sqlparser_sqlserver_is_ident_start(unsigned char c);
int sqlparser_sqlserver_is_ident_char(unsigned char c);
int sqlparser_sqlserver_ascii_word_equal(const char *text, size_t pos, const char *word);
unsigned int sqlparser_sqlserver_candidate_mask(const char *text);
int sqlparser_sqlserver_line_is_go(const char *text, size_t pos, size_t *out_next);
size_t sqlparser_sqlserver_skip_space(const char *text, size_t pos);
size_t sqlparser_sqlserver_trim_left(const char *text, size_t start, size_t end);
size_t sqlparser_sqlserver_trim_right(const char *text, size_t start, size_t end);

int sqlparser_sqlserver_can_copy_quoted_or_comment(const char *sql, size_t index);
size_t sqlparser_sqlserver_skip_quoted_or_comment_span(const char *sql, size_t index);
sqlparser_status_t sqlparser_sqlserver_quoted_or_comment_span(
	const char *sql,
	size_t index,
	size_t *out_next,
	sqlparser_error_t *out_error);

size_t sqlparser_sqlserver_statement_end(
	const char *sql,
	size_t start,
	size_t end);
int sqlparser_sqlserver_find_matching_paren(
	const char *input,
	size_t open_pos,
	size_t *out_close_pos,
	size_t *out_next_pos);
int sqlparser_sqlserver_find_top_level_char(
	const char *input,
	size_t start,
	size_t end,
	char target,
	size_t *out_pos);
int sqlparser_sqlserver_find_top_level_word(
	const char *input,
	size_t start,
	size_t end,
	const char *word,
	size_t *out_pos);

#endif
