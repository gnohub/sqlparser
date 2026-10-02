/* Differential references retain the pre-optimization scanner algorithms. */
#include <locale.h>
#include "../../src/dialect/sqlparser_dialect_mysql.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static sqlparser_status_t sqlparser_mysql_preprocess_quotes_reference(
	const char *input_sql,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		input_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_mysql_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (input_sql[index] != '\0') {
		char c;

		c = input_sql[index];
		if (sqlparser_mysql_is_dash_comment_start(input_sql, index)) {
			while (input_sql[index] != '\0') {
				status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK || input_sql[index] == '\n') {
					break;
				}
				index++;
			}
			if (status == SQLPARSER_STATUS_OK && input_sql[index] == '\n') {
				index++;
			}
		} else if (c == '-' && input_sql[index + 1U] == '-') {
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				input_sql + index,
				1U,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					input_sql + index + 1U,
					1U,
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				index += 2U;
			}
		} else if (c == '/' && input_sql[index + 1U] == '*') {
			status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
			while (status == SQLPARSER_STATUS_OK && input_sql[index] != '\0') {
				status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
				if (input_sql[index] == '*' && input_sql[index + 1U] == '/') {
					index++;
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
					}
					index++;
					break;
				}
				index++;
			}
		} else if (state != NULL && sqlparser_mysql_is_n_string_literal(input_sql + index)) {
			size_t input_start;
			size_t literal_start;

			input_start = index;
			literal_start = out.len;
			status = sqlparser_mysql_copy_n_string_literal(input_sql, &index, &out, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_finish_string_literal(
					state,
					out.data + literal_start,
					out.len - literal_start,
					input_sql + input_start,
					index - input_start,
					out_error);
			}
		} else if (c == '`') {
			status = sqlparser_mysql_copy_backtick_identifier(input_sql, &index, &out, out_error);
		} else if (c == '\'' || c == '"') {
			size_t input_start;
			size_t literal_start;

			input_start = index;
			literal_start = out.len;
			status = sqlparser_mysql_copy_string_literal(input_sql, &index, c, &out, out_error);
			if (status == SQLPARSER_STATUS_OK && state != NULL) {
				status = sqlparser_mysql_finish_string_literal(
					state,
					out.data + literal_start,
					out.len - literal_start,
					input_sql + input_start,
					index - input_start,
					out_error);
			}
		} else if (c == '#') {
			status = sqlparser_mysql_buffer_append_cstr(&out, "-- ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
				while (input_sql[index] != '\0' && input_sql[index] != '\n') {
					status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
					if (status != SQLPARSER_STATUS_OK) {
						break;
					}
					index++;
				}
			}
		} else if (c == '?' && state != NULL) {
			size_t output_start;

			if (state->positional_param_count == (size_t)-1) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				sqlparser_mysql_buffer_release(&out);
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			state->positional_param_count++;
			output_start = out.len;
			status = sqlparser_mysql_append_pg_param(&out, state->positional_param_count, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_mark_source_identifier(
					&out,
					output_start,
					input_sql + index,
					1U,
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		} else {
			size_t end = index + 1U + strcspn(input_sql + index + 1U, "'\"`#?/\\-nN");
			status = sqlparser_mysql_buffer_append_mem(
				&out, input_sql + index, end - index, out_error);
			if (status == SQLPARSER_STATUS_OK) index = end;
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_mask_non_code_reference(
	const char *sql,
	char **out_masked,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t index;

	len = strlen(sql);
	masked = sqlparser_strndup(sql, len);
	if (masked == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (index = 0U; index < len; index++) {
		if (masked[index] == '\'') {
			index++;
			while (index < len) {
				if (masked[index] == '\'' && masked[index + 1U] == '\'') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '\'') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '"') {
			index++;
			while (index < len) {
				if (masked[index] == '"' && masked[index + 1U] == '"') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '"') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '`') {
			index++;
			while (index < len) {
				if (masked[index] == '`' && masked[index + 1U] == '`') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '`') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (sqlparser_mysql_is_dash_comment_start(masked, index) ||
			   masked[index] == '#') {
			while (index < len && masked[index] != '\n') {
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '/' && masked[index + 1U] == '*') {
			masked[index] = ' ';
			masked[index + 1U] = ' ';
			index += 2U;
			while (index < len) {
				if (masked[index] == '*' && masked[index + 1U] == '/') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index++;
					break;
				}
				masked[index] = ' ';
				index++;
			}
		}
	}

	for (index = 0U; index < len; index++) {
		masked[index] = (char)tolower((unsigned char)masked[index]);
	}

	*out_masked = masked;
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_statement_end_reference(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			break;
		}
		index++;
	}
	return index;
}

static size_t sqlparser_mysql_find_top_level_word_between_reference(
	const char *masked,
	const char *word,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	if (masked == NULL || word == NULL || word[0] == '\0') {
		return (size_t)-1;
	}
	depth = 0;
	for (pos = start; pos < end && masked[pos] != '\0'; pos++) {
		if (masked[pos] == '(') {
			depth++;
			continue;
		}
		if (masked[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
		if (depth == 0 && sqlparser_mysql_word_at(masked, pos, word)) {
			return pos;
		}
	}
	return (size_t)-1;
}

static void compare_scanners(const char *sql, int compare_quotes)
{
    char *masked = NULL, *reference = NULL, *quoted = NULL, *quoted_ref = NULL;
    sqlparser_error_t error, reference_error;
    sqlparser_mysql_state_t *state = NULL, *reference_state = NULL;
    sqlparser_mysql_origin_trace_t trace = {0}, reference_trace = {0};
    sqlparser_status_t status, reference_status;
    size_t len = strlen(sql), i, statement_start = 0U;
    static const char *const words[] = { "set", "values", "value", "select", "duplicate" };

    CHECK(sqlparser_mysql_mask_non_code(sql, &masked, NULL) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_mysql_mask_non_code_reference(sql, &reference, NULL) == SQLPARSER_STATUS_OK);
    if (strcmp(masked, reference) != 0) {
        fprintf(stderr, "mask difference for [%s]\nnew=[%s]\nold=[%s]\n", sql, masked, reference);
        CHECK(0);
    }
    do {
        size_t end = sqlparser_mysql_statement_end(sql, statement_start);
        CHECK(end == sqlparser_mysql_statement_end_reference(sql, statement_start));
        CHECK(end >= statement_start && end <= len);
        if (end == len) break;
        statement_start = end + 1U;
    } while (statement_start <= len);
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        size_t bound;
        for (bound = 0U; bound <= len; bound += len / 7U + 1U) {
            CHECK(sqlparser_mysql_find_top_level_word_between(masked, words[i], 0U, bound) ==
                sqlparser_mysql_find_top_level_word_between_reference(reference, words[i], 0U, bound));
        }
    }
    if (!sqlparser_mysql_raw_word_may_appear(sql, 0U, len, "duplicate")) {
        size_t ignored;
        CHECK(!sqlparser_mysql_find_on_duplicate_key_update(reference, 0U, len, &ignored));
    }
    free(masked);
    free(reference);
    if (!compare_quotes) return;
    CHECK(sqlparser_mysql_state_new(&state, NULL) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_mysql_state_new(&reference_state, NULL) == SQLPARSER_STATUS_OK);
    sqlparser_error_clear(&error);
    sqlparser_error_clear(&reference_error);
    status = sqlparser_mysql_preprocess_quotes(sql, state, &quoted, 0U, &trace, &error);
    reference_status = sqlparser_mysql_preprocess_quotes_reference(sql, reference_state,
        &quoted_ref, 0U, &reference_trace, &reference_error);
    CHECK(status == reference_status);
    CHECK(memcmp(&error, &reference_error, sizeof(error)) == 0);
    if (status == SQLPARSER_STATUS_OK) {
        CHECK(strcmp(quoted, quoted_ref) == 0);
        CHECK(trace.run_count == reference_trace.run_count);
        for (i = 0U; i < trace.run_count; i++)
            CHECK(memcmp(&trace.runs[i], &reference_trace.runs[i], sizeof(trace.runs[i])) == 0);
        CHECK(state->positional_param_count == reference_state->positional_param_count);
        CHECK(state->national_literals.count == reference_state->national_literals.count);
        CHECK(state->national_literals.literal_count == reference_state->national_literals.literal_count);
        for (i = 0U; i < state->national_literals.count; i++) {
            CHECK(state->national_literals.items[i].ordinal == reference_state->national_literals.items[i].ordinal);
            CHECK(strcmp(state->national_literals.items[i].sql, reference_state->national_literals.items[i].sql) == 0);
            CHECK(strcmp(state->national_literals.items[i].surface_sql, reference_state->national_literals.items[i].surface_sql) == 0);
        }
    }
    free(quoted);
    free(quoted_ref);
    sqlparser_mysql_origin_trace_release(&trace);
    sqlparser_mysql_origin_trace_release(&reference_trace);
    sqlparser_mysql_state_destroy(state);
    sqlparser_mysql_state_destroy(reference_state);
}

static void check_insert_gate(const char *sql)
{
    sqlparser_mysql_state_t *state = NULL;
    char *rewritten = NULL;
    sqlparser_error_t error;
    if (sqlparser_mysql_dml_pass_may_apply(sql, 0U, strlen(sql),
        SQLPARSER_MYSQL_DML_ORIGIN_MODIFIER)) return;
    CHECK(sqlparser_mysql_state_new(&state, NULL) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_mysql_rewrite_dml_modifier_statement(sql, 0U, state, &rewritten,
        0U, NULL, &error) == SQLPARSER_STATUS_OK);
    CHECK(rewritten == NULL && state->dml_modifier_count == 0U);
    sqlparser_mysql_state_destroy(state);
}

static unsigned next_random(unsigned *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

int main(void)
{
    static const char *const cases[] = {
        "", "SELECT 1", "SELECT 'a''b', \"c\"\"d\", `e``f`", "SELECT N'n',n'N',?",
        "SELECT '\\\\', '\\n', '\\0', '\\\'', \"\\\"\"", "SELECT '-- ; # /*', `a;b`",
        "-- comment ; ' \" `\nSELECT 1; # comment\nSELECT 2", "--x; SELECT 2", "--\t;\nSELECT 3",
        "/* comment */ SELECT 1; /* unterminated", "/*/ SELECT `quoted`, \"string\", ?",
        "/* nested /* x */ SELECT 'z' */", "SELECT 'unterminated", "SELECT `unterminated",
        "INSERT INTO t VALUES(1),(2)", "INSERT INTO t (a, `set`) VALUES(1,2)",
        "INSERT t SELECT x FROM q", "INSERT t VALUE(1)", "INSERT INTO t SET v=1",
        "INSERT IGNORE INTO t VALUES(1)", "INSERT /*x*/ HIGH_PRIORITY INTO t VALUES(1)",
        "INSERT low_priority t SET x=1", "INSERT delayed INTO t VALUE(1)",
        "INSERT INTO `values` SET x=1", "INSERT INTO t(v) (SELECT x FROM q)",
        "INSERT INTO t VALUES(1) ON DUPLICATE KEY UPDATE v=1",
        "INSERT INTO t VALUES('duplicate')", "SELECT x ON /*hello*/ DuPlIcAtE KEY UPDATE v=1",
        "SELECT duplicate_suffix, preduplicate, 'on duplicate key update'", "SELECT )",
        "SELECT '\xff' /*\xfe*/", "INSERT\xff t VALUES(1)", "INSERT INTO t VALUES(1); SET x=1"
    };
    static const char *const locales[] = { "C", "C.UTF-8", "en_US.UTF-8", "tr_TR", "tr_TR.UTF-8", "de_DE" };
    static const char *const parts[] = {
        " t ", " INTO ", "'set'", "\"values\"", "`select`", " set ", " select ", " values ", " value ",
        " /* set */ ", " -- values\n", " # select\n", "(", ")", " IGNORE ", " LOW_PRIORITY ", "x", ";"
    };
    unsigned random = 0x3159U;
    size_t locale_index, i, j;
    char buffer[513];
    size_t locale_count = 0U;
    for (locale_index = 0U; locale_index < sizeof(locales) / sizeof(locales[0]); locale_index++) {
        if (setlocale(LC_CTYPE, locales[locale_index]) == NULL) continue;
        locale_count++;
        for (i = 0U; i < 256U; i++) CHECK(sqlparser_mysql_fold_char((unsigned char)i) == tolower((unsigned char)i));
        {
            static const char *const tokens[] = {
                "duplicate", "DUPLICATE", "DuPlIcAtE", "xduplicate", "duplicate_x",
                "duplicate9", "9duplicate", "(duplicate)", "'duplicate'", "/*duplicate*/",
                "-- duplicate", "# DUPLICATE", "on duplicate key update", "dupli", "",
                "\xff", "none\xff", "\xff" "duplicate", "duplicate\xff"
            };
            size_t token_index;
            for (token_index = 0U; token_index < sizeof(tokens) / sizeof(tokens[0]); token_index++) {
                const char *token = tokens[token_index];
                size_t pos, len = strlen(token);
                int expected = 0;
                /* Original raw gate, independent of the candidate filter. */
                for (pos = 0U; pos < len; pos++) {
                    if ((unsigned char)token[pos] >= 0x80U) { expected = 1; break; }
                    if (tolower((unsigned char)token[pos]) == 'd' &&
                        (pos == 0U || !sqlparser_mysql_is_ident_char((unsigned char)token[pos - 1U])) &&
                        9U <= len - pos && sqlparser_mysql_ascii_word_equal(token, pos, "duplicate")) {
                        expected = 1;
                        break;
                    }
                }
                CHECK(sqlparser_mysql_raw_word_may_appear(token, 0U, len, "duplicate") == expected);
            }
        }
        for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
            compare_scanners(cases[i], 1);
            check_insert_gate(cases[i]);
        }
        for (i = 0U; i < 2000U; i++) {
            size_t len = next_random(&random) % (sizeof(buffer) - 1U);
            for (j = 0U; j < len; j++) buffer[j] = (char)(1U + next_random(&random) % 255U);
            buffer[len] = '\0';
            compare_scanners(buffer, i < 100U);
        }
        for (i = 0U; i < 1000U; i++) {
            strcpy(buffer, "INSERT ");
            for (j = 0U; j < 12U; j++) strcat(buffer, parts[next_random(&random) % (sizeof(parts) / sizeof(parts[0]))]);
            check_insert_gate(buffer);
        }
    }
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    printf("MySQL scanner differential and no-op gate checks passed (%zu locales)\n", locale_count);
    return 0;
}
