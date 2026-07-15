#include <stdio.h>
#include <string.h>

#include "../../src/dialect/sqlparser_dialect_sqlserver_scan.h"

static int fail(const char *test_name, const char *message)
{
	fprintf(stderr, "FAIL [%s]: %s\n", test_name, message);
	return 1;
}

static int scan_to_eof(const char *test_name, const char *sql)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_error_t error;
	sqlparser_status_t status;

	memset(&error, 0, sizeof(error));
	status = sqlparser_sqlserver_scanner_init(
		&scanner,
		sql,
		0U,
		strlen(sql),
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		return fail(test_name, "scanner initialization failed");
	}
	do {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, &error);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "FAIL [%s]: %s\n", test_name, error.message);
			return 1;
		}
	} while (token.kind != SQLPARSER_SQLSERVER_TOKEN_EOF);
	return 0;
}

static int test_non_code_and_tokens(void)
{
	const char *test_name;
	const char *sql;
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_error_t error;
	sqlparser_status_t status;
	size_t word_count;
	int saw_unicode;
	int saw_temp;
	int saw_bind;
	int saw_action;
	int saw_fake_keyword;

	test_name = "non-code-and-tokens";
	sql =
		"SELECT N'IF;''x', [END]],x], \"BEGIN\"\"name\", #tmp, @p, $action "
		"-- ELSE;\r\n"
		"FROM t /* outer OUTPUT; /* inner IF; */ END; */;";
	memset(&error, 0, sizeof(error));
	if (sqlparser_sqlserver_scanner_init(
		    &scanner,
		    sql,
		    0U,
		    strlen(sql),
		    &error) != SQLPARSER_STATUS_OK) {
		return fail(test_name, "scanner initialization failed");
	}

	word_count = 0U;
	saw_unicode = 0;
	saw_temp = 0;
	saw_bind = 0;
	saw_action = 0;
	saw_fake_keyword = 0;
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, &error);
		if (status != SQLPARSER_STATUS_OK) {
			return fail(test_name, error.message);
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			break;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_WORD) {
			word_count++;
			if (sqlparser_sqlserver_token_word_equal(sql, &token, "#tmp")) {
				saw_temp = 1;
			} else if (sqlparser_sqlserver_token_word_equal(sql, &token, "@p")) {
				saw_bind = 1;
			} else if (sqlparser_sqlserver_token_word_equal(sql, &token, "$action")) {
				saw_action = 1;
			} else if (sqlparser_sqlserver_token_word_equal(sql, &token, "if") ||
			           sqlparser_sqlserver_token_word_equal(sql, &token, "else") ||
			           sqlparser_sqlserver_token_word_equal(sql, &token, "begin") ||
			           sqlparser_sqlserver_token_word_equal(sql, &token, "end") ||
			           sqlparser_sqlserver_token_word_equal(sql, &token, "output")) {
				saw_fake_keyword = 1;
			}
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_STRING &&
		    token.end - token.start == strlen("N'IF;''x'") &&
		    strncmp(sql + token.start, "N'IF;''x'", token.end - token.start) == 0) {
			saw_unicode = 1;
		}
	}

	if (!saw_unicode || !saw_temp || !saw_bind || !saw_action) {
		return fail(test_name, "expected SQL Server token was not emitted");
	}
	if (saw_fake_keyword) {
		return fail(test_name, "non-code text was emitted as a keyword");
	}
	if (word_count != 6U) {
		return fail(test_name, "unexpected word token count");
	}
	return 0;
}

static int test_candidate_mask(void)
{
	unsigned int mask;

	if (sqlparser_sqlserver_candidate_mask(NULL) != 0U ||
	    sqlparser_sqlserver_candidate_mask("SELECT id FROM dbo.t") != 0U) {
		return fail("candidate-mask", "ordinary SQL produced a candidate");
	}
	mask = sqlparser_sqlserver_candidate_mask(
		"INSERT dbo.t OUTPUT inserted.id VALUES (1)");
	if (mask != (SQLPARSER_SQLSERVER_CANDIDATE_INSERT |
	             SQLPARSER_SQLSERVER_CANDIDATE_OUTPUT)) {
		return fail("candidate-mask", "INSERT OUTPUT candidates are incorrect");
	}
	mask = sqlparser_sqlserver_candidate_mask(
		"MERGE dbo.t USING dbo.s ON t.id = s.id OUTPUT inserted.id;");
	if (mask != (SQLPARSER_SQLSERVER_CANDIDATE_MERGE |
	             SQLPARSER_SQLSERVER_CANDIDATE_OUTPUT)) {
		return fail("candidate-mask", "MERGE OUTPUT candidates are incorrect");
	}
	mask = sqlparser_sqlserver_candidate_mask(
		"IF EXISTS (SELECT 1) SELECT 1 ELSE SELECT 0");
	if (mask != SQLPARSER_SQLSERVER_CANDIDATE_CONTROL) {
		return fail("candidate-mask", "IF ELSE candidate is incorrect");
	}
	mask = sqlparser_sqlserver_candidate_mask(
		"SELECT 'IF OUTPUT', [ELSE], \"INSERT\" /* MERGE */ -- IF\nFROM dbo.t");
	if (mask != 0U) {
		return fail("candidate-mask", "non-code text produced a candidate");
	}
	mask = sqlparser_sqlserver_candidate_mask(
		"SELECT different, elsewhere, reinserted, merged, outputting FROM dbo.t");
	if (mask != 0U) {
		return fail("candidate-mask", "identifier substring produced a candidate");
	}
	return 0;
}

static int test_depth_and_statement_boundaries(void)
{
	const char *test_name;
	const char *sql;
	const char *last_end;
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_error_t error;
	sqlparser_status_t status;
	size_t semicolon_count;
	size_t top_level_semicolon_count;

	test_name = "depth-and-statement-boundaries";
	sql =
		"BEGIN "
		"SELECT CASE WHEN (@p = 1) THEN 2 ELSE 3 END; "
		"BEGIN SELECT 4; END; "
		"END; SELECT 5;";
	memset(&error, 0, sizeof(error));
	if (sqlparser_sqlserver_scanner_init(
		    &scanner,
		    sql,
		    0U,
		    strlen(sql),
		    &error) != SQLPARSER_STATUS_OK) {
		return fail(test_name, "scanner initialization failed");
	}

	semicolon_count = 0U;
	top_level_semicolon_count = 0U;
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, &error);
		if (status != SQLPARSER_STATUS_OK) {
			return fail(test_name, error.message);
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			break;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL && token.symbol == ';') {
			semicolon_count++;
			if (token.paren_depth == 0U && token.block_depth == 0U &&
			    token.case_depth == 0U) {
				top_level_semicolon_count++;
			}
		}
	}
	if (semicolon_count != 5U || top_level_semicolon_count != 2U) {
		return fail(test_name, "parenthesis, CASE, or block depth is incorrect");
	}

	last_end = strstr(sql, "END; SELECT 5");
	if (last_end == NULL ||
	    sqlparser_sqlserver_statement_end(sql, 0U, strlen(sql)) !=
		    (size_t)(last_end - sql) + strlen("END")) {
		return fail(test_name, "BEGIN block statement boundary is incorrect");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN TRANSACTION; SELECT 1;",
		    0U,
		    strlen("BEGIN TRANSACTION; SELECT 1;")) != strlen("BEGIN TRANSACTION")) {
		return fail(test_name, "BEGIN TRANSACTION was treated as a block");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN /* comment */ DISTRIBUTED TRANSACTION; SELECT 1;",
		    0U,
		    strlen("BEGIN /* comment */ DISTRIBUTED TRANSACTION; SELECT 1;")) !=
	    strlen("BEGIN /* comment */ DISTRIBUTED TRANSACTION")) {
		return fail(test_name, "BEGIN DISTRIBUTED TRANSACTION was treated as a block");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN DISTRIBUTED TRAN tx; SELECT 1;",
		    0U,
		    strlen("BEGIN DISTRIBUTED TRAN tx; SELECT 1;")) !=
	    strlen("BEGIN DISTRIBUTED TRAN tx")) {
		return fail(test_name, "BEGIN DISTRIBUTED TRAN was treated as a block");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN DIALOG CONVERSATION @h; SELECT 1;",
		    0U,
		    strlen("BEGIN DIALOG CONVERSATION @h; SELECT 1;")) !=
	    strlen("BEGIN DIALOG CONVERSATION @h")) {
		return fail(test_name, "BEGIN DIALOG CONVERSATION was treated as a block");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN DIALOG @h FROM SERVICE source TO SERVICE 'target'; SELECT 1;",
		    0U,
		    strlen("BEGIN DIALOG @h FROM SERVICE source TO SERVICE 'target'; SELECT 1;")) !=
	    strlen("BEGIN DIALOG @h FROM SERVICE source TO SERVICE 'target'")) {
		return fail(test_name, "BEGIN DIALOG was treated as a block");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN CONVERSATION TIMER (@h) TIMEOUT = 30; SELECT 1;",
		    0U,
		    strlen("BEGIN CONVERSATION TIMER (@h) TIMEOUT = 30; SELECT 1;")) !=
	    strlen("BEGIN CONVERSATION TIMER (@h) TIMEOUT = 30")) {
		return fail(test_name, "BEGIN CONVERSATION TIMER was treated as a block");
	}
	if (sqlparser_sqlserver_statement_end(
		    "END CONVERSATION @h; SELECT 1;",
		    0U,
		    strlen("END CONVERSATION @h; SELECT 1;")) != strlen("END CONVERSATION @h")) {
		return fail(test_name, "END CONVERSATION was treated as a block terminator");
	}
	if (sqlparser_sqlserver_statement_end(
		    "BEGIN ATOMIC WITH (TRANSACTION ISOLATION LEVEL = SNAPSHOT) SELECT 1; END; SELECT 2;",
		    0U,
		    strlen("BEGIN ATOMIC WITH (TRANSACTION ISOLATION LEVEL = SNAPSHOT) SELECT 1; END; SELECT 2;")) !=
	    strlen("BEGIN ATOMIC WITH (TRANSACTION ISOLATION LEVEL = SNAPSHOT) SELECT 1; END")) {
		return fail(test_name, "BEGIN ATOMIC block boundary is incorrect");
	}
	return 0;
}

static int test_span_and_top_level_helpers(void)
{
	const char *test_name;
	const char *sql;
	const char *expected;
	size_t close_pos;
	size_t next_pos;
	size_t found_pos;
	size_t span_end;
	sqlparser_error_t error;

	test_name = "span-and-top-level-helpers";
	sql = "fn((1 + 2), 'not )', [also)] /* outer ) /* inner */ */) tail";
	close_pos = 0U;
	next_pos = 0U;
	if (!sqlparser_sqlserver_find_matching_paren(sql, 2U, &close_pos, &next_pos) ||
	    sql[close_pos] != ')' || next_pos != close_pos + 1U ||
	    strcmp(sql + next_pos, " tail") != 0) {
		return fail(test_name, "matching parenthesis ignored an invalid span boundary");
	}

	sql = "(a, b), N'x,y', CASE WHEN c = 1 THEN d ELSE e END, z";
	found_pos = 0U;
	if (!sqlparser_sqlserver_find_top_level_char(sql, 0U, strlen(sql), ',', &found_pos) ||
	    found_pos != strlen("(a, b)")) {
		return fail(test_name, "top-level comma was not found");
	}

	sql = "(x AS int) AS varchar(20) /* AS ignored */";
	expected = strstr(sql, ") AS");
	if (expected == NULL ||
	    !sqlparser_sqlserver_find_top_level_word(
		    sql,
		    0U,
		    strlen(sql),
		    "as",
		    &found_pos) ||
	    found_pos != (size_t)(expected - sql) + 2U) {
		return fail(test_name, "top-level word was not found");
	}

	sql = "/* outer /* inner */ tail */";
	span_end = 0U;
	memset(&error, 0, sizeof(error));
	if (sqlparser_sqlserver_quoted_or_comment_span(
		    sql,
		    0U,
		    &span_end,
		    &error) != SQLPARSER_STATUS_OK ||
	    span_end != strlen(sql)) {
		return fail(test_name, "nested block comment span is incorrect");
	}
	if (sqlparser_sqlserver_skip_quoted_or_comment_span("-- comment\r\nSELECT", 0U) !=
	    strlen("-- comment\r\n")) {
		return fail(test_name, "line comment terminator was not consumed");
	}
	return 0;
}

static int expect_scan_error(
	const char *test_name,
	const char *sql,
	const char *message_fragment)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_error_t error;
	sqlparser_status_t status;

	memset(&error, 0, sizeof(error));
	if (sqlparser_sqlserver_scanner_init(
		    &scanner,
		    sql,
		    0U,
		    strlen(sql),
		    &error) != SQLPARSER_STATUS_OK) {
		return fail(test_name, "scanner initialization failed");
	}
	do {
		status = sqlparser_sqlserver_scanner_next(&scanner, &token, &error);
	} while (status == SQLPARSER_STATUS_OK &&
	         token.kind != SQLPARSER_SQLSERVER_TOKEN_EOF);
	if (status != SQLPARSER_STATUS_PARSE_ERROR) {
		return fail(test_name, "malformed SQL did not return a parse error");
	}
	if (strstr(error.message, message_fragment) == NULL ||
	    error.cursor <= 0 || error.line <= 0 || error.column <= 0) {
		return fail(test_name, "parse error details are incomplete");
	}
	return 0;
}

static int test_errors(void)
{
	static const struct {
		const char *sql;
		const char *message;
	} cases[] = {
		{"SELECT 'x", "string literal"},
		{"SELECT \"x", "quoted identifier"},
		{"SELECT [x", "bracket-delimited identifier"},
		{"SELECT /* outer /* inner */", "block comment"},
		{"SELECT (1", "parenthesized expression"},
		{"SELECT CASE WHEN 1 = 1 THEN 1", "CASE expression"},
		{"BEGIN SELECT 1;", "BEGIN block"},
		{"SELECT 1)", "closing parenthesis"},
		{"END;", "unmatched SQL Server END"}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_scan_error("errors", cases[index].sql, cases[index].message) != 0) {
			return 1;
		}
	}
	if (scan_to_eof("line-comment-at-eof", "SELECT 1 -- comment") != 0) {
		return 1;
	}
	return 0;
}

static int test_invalid_arguments(void)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_error_t error;
	size_t next;

	memset(&error, 0, sizeof(error));
	if (sqlparser_sqlserver_scanner_init(NULL, "", 0U, 0U, &error) !=
	    SQLPARSER_STATUS_INVALID_ARGUMENT) {
		return fail("invalid-arguments", "NULL scanner was accepted");
	}
	if (sqlparser_sqlserver_scanner_init(&scanner, "x", 1U, 0U, &error) !=
	    SQLPARSER_STATUS_INVALID_ARGUMENT) {
		return fail("invalid-arguments", "invalid scanner range was accepted");
	}
	if (sqlparser_sqlserver_scanner_init(&scanner, "x", 0U, 1U, &error) !=
	    SQLPARSER_STATUS_OK) {
		return fail("invalid-arguments", "valid scanner range was rejected");
	}
	if (sqlparser_sqlserver_scanner_next(&scanner, NULL, &error) !=
	    SQLPARSER_STATUS_INVALID_ARGUMENT) {
		return fail("invalid-arguments", "NULL token output was accepted");
	}
	if (sqlparser_sqlserver_quoted_or_comment_span(
		    "abc",
		    0U,
		    &next,
		    &error) != SQLPARSER_STATUS_INVALID_ARGUMENT) {
		return fail("invalid-arguments", "ordinary text was accepted as a quoted span");
	}
	memset(&token, 0, sizeof(token));
	if (sqlparser_sqlserver_token_word_equal("", &token, "select")) {
		return fail("invalid-arguments", "EOF token matched a word");
	}
	return 0;
}

int main(void)
{
	int failures;

	failures = 0;
	failures += test_non_code_and_tokens();
	failures += test_candidate_mask();
	failures += test_depth_and_statement_boundaries();
	failures += test_span_and_top_level_helpers();
	failures += test_errors();
	failures += test_invalid_arguments();
	if (failures != 0) {
		return 1;
	}

	printf("sqlserver scanner tests passed\n");
	return 0;
}
