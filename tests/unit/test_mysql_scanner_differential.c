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


/* Reference classifier: only the optimized opening-byte dispatch differs. */
static sqlparser_mysql_extension_features_t sqlparser_mysql_classify_extensions_reference(const char *sql)
{
	sqlparser_mysql_extension_features_t features;
	size_t index;

	memset(&features, 0, sizeof(features));
	if (sql == NULL) {
		return features;
	}
	index = 0U;
	while (sql[index] != '\0') {
		size_t skipped;
		size_t word_end;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (!sqlparser_mysql_is_ident_start((unsigned char)sql[index])) {
			index++;
			continue;
		}
		word_end = index + 1U;
		while (sqlparser_mysql_is_ident_char((unsigned char)sql[word_end])) {
			word_end++;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "limit")) {
			features.limit = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "straight_join")) {
			features.straight_join = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "partition")) {
			features.table_partition = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "lock")) {
			features.locking_read = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "use") ||
			   sqlparser_mysql_ascii_word_equal(sql, index, "force") ||
			   sqlparser_mysql_ascii_word_equal(sql, index, "ignore")) {
			size_t next_word;

			next_word = sqlparser_mysql_skip_space(sql, word_end);
			if (sqlparser_mysql_ascii_word_equal(sql, next_word, "index") ||
			    sqlparser_mysql_ascii_word_equal(sql, next_word, "key")) {
				features.index_hint = 1U;
			}
		}
		if (features.straight_join && features.index_hint &&
		    features.table_partition && features.locking_read) {
			break;
		}
		index = word_end;
	}
	return features;
}

static unsigned classifier_bits(sqlparser_mysql_extension_features_t features)
{
    return (features.straight_join ? 1U : 0U) |
        (features.index_hint ? 2U : 0U) |
        (features.table_partition ? 4U : 0U) |
        (features.locking_read ? 8U : 0U) |
        (features.limit ? 16U : 0U);
}

static void compare_classifier(const char *sql)
{
    unsigned actual = classifier_bits(sqlparser_mysql_classify_extensions(sql));
    unsigned expected = classifier_bits(sqlparser_mysql_classify_extensions_reference(sql));
    if (actual != expected) {
        fprintf(stderr, "classifier difference: actual=%u expected=%u SQL=[%s]\n",
            actual, expected, sql != NULL ? sql : "NULL");
        CHECK(0);
    }
}

static void check_classifier_bytes(void)
{
    static const char suffix[] = " limit straight_join partition lock use index";
    static const char *const cases[] = {
        "", "0", "12345(1,2);", "LIMIT", "STRAIGHT_JOIN", "PARTITION", "LOCK", "USE INDEX",
        "FORCE KEY", "IGNORE INDEX", "limitx xlimit 1limit limit1 _limit limit_",
        "LIMIT STRAIGHT_JOIN PARTITION LOCK USE INDEX", "LI", "USE", "USE INDE", "USE KEYx",
        "'limit' lock", "'limit''lock' partition", "'' LIMIT", "''' LIMIT", "'''' LIMIT",
        "\"limit\" lock", "\"limit\"\"lock\" partition", "`limit` lock", "`limit``lock` partition",
        "'unterminated LIMIT", "\"unterminated LOCK", "`unterminated PARTITION",
        "'\\' LIMIT", "'\\'' LIMIT", "-- LIMIT\nLOCK", "--\rLIMIT\nLOCK", "--\tLIMIT\nLOCK",
        "--x LIMIT", "- LIMIT", "# LIMIT\nLOCK", "# LIMIT", "/ LIMIT", "/* LIMIT */ LOCK",
        "/*/ LIMIT", "/**/ LIMIT", "/*/*/ LIMIT", "/* nested /* LIMIT */ LOCK */",
        "/* unterminated LIMIT", "/*! LIMIT */ LOCK", "/*+ LIMIT */ LOCK",
        "USE/*x*/INDEX", "USE\nINDEX", "FORCE `INDEX`", "IGNORE \"KEY\"",
        "\xff LIMIT", "LIMIT\xff", "\xff" "LIMIT", "LI\xff" "MIT", "USE \xff INDEX",
        "--\xff LIMIT", "INSERT INTO t(id,text_col) VALUES(12345,'small-secret-5000')"
    };
    char pair[3];
    char adjacent[sizeof(suffix) + 2U];
    size_t a, b, i;

    compare_classifier(NULL);
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) compare_classifier(cases[i]);
    for (a = 0U; a < 256U; a++) {
        pair[0] = (char)a;
        pair[1] = '\0';
        compare_classifier(pair);
        for (b = 0U; b < 256U; b++) {
            pair[0] = (char)a;
            pair[1] = (char)b;
            pair[2] = '\0';
            compare_classifier(pair);
            /* Every pair at a scan position, followed by every feature. Embedded
             * NUL bytes intentionally retain ordinary C-string termination. */
            adjacent[0] = (char)a;
            adjacent[1] = (char)b;
            memcpy(adjacent + 2U, suffix, sizeof(suffix));
            compare_classifier(adjacent);
        }
    }
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

    compare_classifier(sql);
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
    if (!sqlparser_mysql_raw_word_may_appear(sql, 0U, len, "duplicate") ||
        !sqlparser_mysql_unquoted_word_may_appear(sql, 0U, len, "duplicate")) {
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

/* Borrowed input must never become owned output or dialect-state storage.
 * Exercise both optional pre-quote rewrites, individually and together. */
static void check_preprocess_ownership(const char *sql, int valid)
{
    char *input = sqlparser_strdup(sql);
    char *owned = NULL, *reference = NULL;
    char *restored = NULL, *reference_restored = NULL;
    void *state = NULL, *reference_state = NULL;
    sqlparser_error_t error, reference_error;
    sqlparser_status_t status, reference_status;
    size_t length = strlen(sql);
    CHECK(input != NULL);
    memset(&error, 0, sizeof(error));
    memset(&reference_error, 0, sizeof(reference_error));
    reference_status = sqlparser_mysql_preprocess_internal(sql, NULL,
        &reference, &reference_state, NULL, &reference_error);
    status = sqlparser_mysql_preprocess_internal(input, NULL,
        &owned, &state, NULL, &error);
    CHECK(status == reference_status);
    CHECK((status == SQLPARSER_STATUS_OK) == valid);
    CHECK(memcmp(&error, &reference_error, sizeof(error)) == 0);
    CHECK(strcmp(input, sql) == 0);
    CHECK(owned == NULL || owned != input);
    memset(input, 0xa5, length);
    free(input);
    if (status == SQLPARSER_STATUS_OK) {
        CHECK(owned != NULL && reference != NULL);
        CHECK(strcmp(owned, reference) == 0);
        CHECK(sqlparser_mysql_postprocess_deparse(owned, state, &restored,
            &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_mysql_postprocess_deparse(reference, reference_state,
            &reference_restored, &reference_error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(restored, reference_restored) == 0);
    } else {
        CHECK(owned == NULL && reference == NULL);
        CHECK(state == NULL && reference_state == NULL);
    }
    free(owned);
    free(reference);
    free(restored);
    free(reference_restored);
    sqlparser_mysql_state_destroy(state);
    sqlparser_mysql_state_destroy(reference_state);
}

static unsigned next_random(unsigned *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void check_quoted_keyword_gate(void)
{
    static const struct { const char *sql; int expected; } inputs[] = {
        {"INSERT INTO t(a,b) VALUES('张三李四',100.50)", 0},
        {"INSERT INTO t(a,b) VALUES('张''三','DUPLICATE')", 0},
        {"INSERT INTO t(a,b) VALUES(\"张\"\"三\",'x')", 0},
        {"INSERT INTO `张``三`(a) VALUES('x')", 0},
        {"INSERT INTO t(a) VALUES('张三') ON duplicate KEY UPDATE a='李四'", 1},
        {"INSERT INTO t(a) VALUES('张''三') ON DuPliCaTe KEY UPDATE a=1", 1},
        {"INSERT INTO t(a) VALUES('张\\'三') ON duplicate KEY UPDATE a=1", 1},
        {"INSERT INTO t(a) VALUES('slash\\n')", 1},
        {"INSERT INTO t(a) VALUES('unterminated", 1},
        {"INSERT INTO t(a) VALUES('张三", 1},
        {"INSERT INTO 张三(a) VALUES('x')", 1},
        {"/*!40101 INSERT INTO t(a) VALUES('张') ON duplicate KEY UPDATE a=1 */", 1},
        {"INSERT INTO t(a) VALUES('x') /*! ON duplicate KEY UPDATE a=1 */", 1},
        {"INSERT INTO t(a) VALUES('x') /* no extension */", 0},
        {"INSERT INTO t(a) VALUES('x') -- duplicate\n", 1},
        {"INSERT INTO t(a) VALUES('x') # duplicate\n", 1},
        {"INSERT INTO t(a) VALUES('x') /* ' */ ON duplicate KEY UPDATE a=1", 1}
    };
    size_t i;
    for (i = 0U; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        const char *sql = inputs[i].sql;
        int actual = sqlparser_mysql_unquoted_word_may_appear(sql, 0U, strlen(sql), "duplicate");
        if (actual != inputs[i].expected) fprintf(stderr, "quoted gate case=%zu actual=%d expected=%d SQL=%s locale=%s\n", i, actual, inputs[i].expected, sql, setlocale(LC_CTYPE, NULL));
        CHECK(actual == inputs[i].expected);
        compare_scanners(sql, 1);
        check_insert_gate(sql);
    }
    {
        const char *sql = "'张''三' duplicate";
        const char *close = strchr(sql + 5U, ' ');
        size_t end;
        CHECK(close != NULL);
        for (end = 1U; end < (size_t)(close - sql); end++) {
            /* A bounded span ending on an interior doubled quote may itself
             * be a complete quote. Every incomplete quote stays conservative. */
            if (sql[end - 1U] != '\'')
                CHECK(sqlparser_mysql_unquoted_word_may_appear(sql, 0U, end, "duplicate"));
        }
        CHECK(!sqlparser_mysql_unquoted_word_may_appear(sql, 0U, (size_t)(close - sql), "duplicate"));
        CHECK(sqlparser_mysql_unquoted_word_may_appear(sql, 0U, strlen(sql), "duplicate"));
    }
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
    static const char *const ownership_cases[] = {
        "SELECT 1", "INSERT INTO `t` (`id`, `v`) VALUES (1, 'one')",
        "SELECT N'national', \"double\", 'slash\\n', ? AS `Alias`",
        "/*!40101 SELECT \"surface\" AS `Alias` */",
        "CREATE TABLE `t` (`id` INT UNSIGNED) ENGINE=InnoDB",
        "/*!40101 CREATE TABLE `t` (`id` INT UNSIGNED) ENGINE=InnoDB */",
        "SELECT 1; CREATE TABLE `t` (`id` INT UNSIGNED) ENGINE=InnoDB; SELECT 2"
    };
    unsigned random = 0x3159U;
    size_t locale_index, i, j;
    char buffer[513];
    size_t locale_count = 0U;
    for (locale_index = 0U; locale_index < sizeof(locales) / sizeof(locales[0]); locale_index++) {
        if (setlocale(LC_CTYPE, locales[locale_index]) == NULL) continue;
        locale_count++;
        check_classifier_bytes();
        check_quoted_keyword_gate();
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
                /* Original raw gate, independent of the optimized filter. */
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
    for (i = 0U; i < sizeof(ownership_cases) / sizeof(ownership_cases[0]); i++)
        check_preprocess_ownership(ownership_cases[i], 1);
    check_preprocess_ownership("SELECT 'unterminated", 0);
    check_preprocess_ownership("/*!40101 SELECT 'unterminated */", 0);
    check_preprocess_ownership("CREATE TABLE `t` (`id` INT) ENGINE=", 0);
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    printf("MySQL scanner differential and no-op gate checks passed (%zu locales)\n", locale_count);
    return 0;
}
