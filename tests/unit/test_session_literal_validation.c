/* Regression coverage for session literal validation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"

static sqlparser_error_t error;
static const char *input_sql;
static const char *replacement_sql;
static size_t literal_index;
static int typed;
static const char *phase;
static size_t valid_cases, rejected_cases;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "grammar literal: %s\ninput=%s literal=%zu typed=%d replacement=%s phase=%s error=%s\n", \
			#condition, input_sql, literal_index, typed, replacement_sql, phase, error.message); \
		exit(1); \
	} \
} while (0)

static sqlparser_handle_t *parse(const char *sql)
{
	sqlparser_handle_t *handle = NULL;
	CHECK(sqlparser_parse(sql, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(handle != NULL);
	return handle;
}

static void verify_reads(sqlparser_handle_t *handle, const char *required)
{
	sqlparser_bind_occurrence_view_t binds = {0};
	sqlparser_handle_t *fresh;
	char *sql = NULL, *view = NULL, *fresh_view = NULL;
	CHECK(sqlparser_handle_bind_occurrences(handle, &binds, &error) == SQLPARSER_STATUS_OK);
	CHECK(binds.count == 0U);
	CHECK(sqlparser_export_view_json(handle, 0, &view, &error) == SQLPARSER_STATUS_OK);
	CHECK(view != NULL);
	CHECK(sqlparser_deparse(handle, &sql, &error) == SQLPARSER_STATUS_OK);
	CHECK(sql != NULL && (required == NULL || strstr(sql, required) != NULL));
	fresh = parse(sql);
	CHECK(sqlparser_export_view_json(fresh, 0, &fresh_view, &error) == SQLPARSER_STATUS_OK);
	CHECK(fresh_view != NULL);
	/* Existing generic SET arguments can retain identifier/literal origin
	 * differences after replacement. Transaction modes and JSON paths have
	 * stable semantic views and must round-trip exactly. */
	if (strstr(input_sql, "ISOLATION LEVEL") != NULL ||
	    strncmp(input_sql, "SELECT", 6U) == 0) {
		CHECK(strcmp(view, fresh_view) == 0);
	}
	sqlparser_string_free(fresh_view);
	sqlparser_string_free(view);
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(fresh);
}

static void replace_literal(const char *sql, size_t index, const char *raw,
	const sqlparser_literal_value_t *value, const char *required, int valid)
{
	sqlparser_patch_t patch = {0};
	sqlparser_patch_list_t patches = {&patch, 1U};
	sqlparser_bind_occurrence_view_t binds = {0};
	sqlparser_handle_t *handle;
	sqlparser_status_t status;
	char selector[64];
	input_sql = sql;
	replacement_sql = raw;
	literal_index = index;
	handle = parse(sql);
	/* Also invalidate already materialized readers on a terminal failure. */
	phase = "initial read";
	verify_reads(handle, NULL);
	(void)snprintf(selector, sizeof(selector), "stmt[0].literal[%zu]", index);
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector;
	if (typed) patch.literal = value;
	else patch.sql = raw;
	phase = "first apply";
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (valid) {
		CHECK(status == SQLPARSER_STATUS_OK);
		phase = "after apply read";
		verify_reads(handle, required);
		/* Repeat on the same handle after all public readers have run. */
		CHECK(sqlparser_apply_patch(handle, &patches, &error) == SQLPARSER_STATUS_OK);
		phase = "after apply read";
		verify_reads(handle, required);
		valid_cases++;
	} else {
		CHECK(status != SQLPARSER_STATUS_OK);
		if (typed) CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT ||
			status == SQLPARSER_STATUS_UNSUPPORTED);
		CHECK(sqlparser_handle_bind_occurrences(handle, &binds, &error) ==
			SQLPARSER_STATUS_INVALID_ARGUMENT);
		CHECK(sqlparser_test_failed_handle(handle));
		rejected_cases++;
	}
	sqlparser_handle_destroy(handle);
}

static void transaction_modes(int valid_only)
{
	static const char *const inputs[] = {
		"SET TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE;",
		"SET LOCAL TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE;",
		"SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE;",
		"BEGIN ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE;",
		"START TRANSACTION ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE;"
	};
	static const char *const levels[] = {
		"read uncommitted", "read committed", "repeatable read", "serializable"
	};
	static const char *const keywords[] = {
		"READ UNCOMMITTED", "READ COMMITTED", "REPEATABLE READ", "SERIALIZABLE"
	};
	static const struct {
		const char *raw;
		sqlparser_literal_kind_t kind;
		const char *string;
		long long integer;
	} invalid[] = {
		{"'long 日本 quote '' done'", SQLPARSER_LITERAL_KIND_STRING, "long 日本 quote ' done", 0},
		{"'a'", SQLPARSER_LITERAL_KIND_STRING, "a", 0},
		{"'z'", SQLPARSER_LITERAL_KIND_STRING, "z", 0},
		{"''", SQLPARSER_LITERAL_KIND_STRING, "", 0},
		{"NULL", SQLPARSER_LITERAL_KIND_NULL, NULL, 0},
		{"2", SQLPARSER_LITERAL_KIND_INTEGER, NULL, 2},
		{"-1", SQLPARSER_LITERAL_KIND_INTEGER, NULL, -1},
		{"2147483648", SQLPARSER_LITERAL_KIND_INTEGER, NULL, 2147483648LL},
		{"1.5", SQLPARSER_LITERAL_KIND_FLOAT, NULL, 0},
		{"TRUE", SQLPARSER_LITERAL_KIND_BOOLEAN, NULL, 0}
	};
	size_t si, i, index;
	sqlparser_literal_value_t value = {0};
	char raw[64];
	for (si = 0U; si < sizeof(inputs) / sizeof(inputs[0]); si++) {
		for (typed = 0; typed < 2; typed++) {
			for (i = 0U; i < sizeof(levels) / sizeof(levels[0]); i++) {
				memset(&value, 0, sizeof(value));
				value.kind = SQLPARSER_LITERAL_KIND_STRING;
				value.string_value = levels[i];
				(void)snprintf(raw, sizeof(raw), "'%s'", levels[i]);
				replace_literal(inputs[si], 0U, raw, &value, keywords[i], 1);
			}
			for (index = 1U; index <= 2U; index++) {
				for (i = 0U; i <= 1U; i++) {
					memset(&value, 0, sizeof(value));
					value.kind = SQLPARSER_LITERAL_KIND_INTEGER;
					value.integer_value = (long long)i;
					replace_literal(inputs[si], index, i ? "1" : "0", &value,
						index == 1U ? (i ? "READ ONLY" : "READ WRITE") :
						(i ? "DEFERRABLE" : "NOT DEFERRABLE"), 1);
				}
			}
			if (valid_only) continue;
			for (index = 0U; index <= 2U; index++) {
				for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
					memset(&value, 0, sizeof(value));
					value.kind = invalid[i].kind;
					value.string_value = invalid[i].string;
					value.integer_value = invalid[i].integer;
					value.float_value = "1.5";
					value.boolean_value = 1;
					replace_literal(inputs[si], index, invalid[i].raw, &value, NULL, 0);
				}
			}
			/* Even 0/1 are invalid in the isolation string slot. */
			value.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value.integer_value = 0;
			replace_literal(inputs[si], 0U, "0", &value, NULL, 0);
			value.integer_value = 1;
			replace_literal(inputs[si], 0U, "1", &value, NULL, 0);
		}
	}
}

static void required_strings(int valid_only)
{
	static const struct { const char *sql; size_t index; } inputs[] = {
		{"SET TRANSACTION SNAPSHOT 'old';", 0U},
		{"SELECT * FROM JSON_TABLE('{}', '$' COLUMNS (v int PATH '$.v')) AS jt;", 1U},
		{"SELECT * FROM JSON_TABLE('{}', '$' COLUMNS (v int PATH '$.v')) AS jt;", 2U},
		{"SELECT * FROM JSON_TABLE('{}', '$' COLUMNS (NESTED PATH '$.rows' COLUMNS (v int PATH '$.v'))) AS jt;", 2U},
		{"SELECT * FROM JSON_TABLE('{}', '$' COLUMNS (NESTED PATH '$.rows' COLUMNS (v int PATH '$.v'))) AS jt;", 3U}
	};
	size_t i;
	sqlparser_literal_value_t value = {0};
	for (i = 0U; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
		for (typed = 0; typed < 2; typed++) {
			value.kind = SQLPARSER_LITERAL_KIND_STRING;
			value.string_value = "$.updated";
			replace_literal(inputs[i].sql, inputs[i].index, "'$.updated'", &value, "$.updated", 1);
			value.string_value = "";
			replace_literal(inputs[i].sql, inputs[i].index, "''", &value, NULL, 1);
			if (valid_only) continue;
			value.kind = SQLPARSER_LITERAL_KIND_NULL;
			replace_literal(inputs[i].sql, inputs[i].index, "NULL", &value, NULL, 0);
			value.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value.integer_value = 1;
			replace_literal(inputs[i].sql, inputs[i].index, "1", &value, NULL, 0);
			value.kind = SQLPARSER_LITERAL_KIND_BOOLEAN;
			value.boolean_value = 1;
			/* In SET syntax the unquoted word TRUE is a string argument. */
			replace_literal(inputs[i].sql, inputs[i].index, "TRUE", &value, NULL,
				i == 0U && !typed);
		}
	}
}

static void unconstrained_literals(void)
{
	sqlparser_literal_value_t value = {0};
	for (typed = 0; typed < 2; typed++) {
		value.kind = SQLPARSER_LITERAL_KIND_STRING;
		value.string_value = "arbitrary_setting";
		if (typed) {
			/* Ordinary SET values have no transaction-mode enum constraint. */
			replace_literal("SET application_name TO 'old';", 0U,
				"'arbitrary_setting'", &value, "arbitrary_setting", 1);
			replace_literal("SET transaction_isolation TO 'serializable';", 0U,
				"'arbitrary_setting'", &value, "arbitrary_setting", 1);
		}
		value.kind = SQLPARSER_LITERAL_KIND_NULL;
		replace_literal("SELECT 'old';", 0U, "NULL", &value, "NULL", 1);
		/* The JSON input expression is unrestricted; only pathspecs need strings. */
		replace_literal("SELECT * FROM JSON_TABLE('{}', '$' COLUMNS (v int PATH '$.v')) AS jt;",
			0U, "NULL", &value, "NULL", 1);
	}
}

int main(int argc, char **argv)
{
	int valid_only = argc == 2 && strcmp(argv[1], "--valid-only") == 0;
	transaction_modes(valid_only);
	required_strings(valid_only);
	unconstrained_literals();
	printf("grammar literal validation: %zu valid, %zu rejected\n", valid_cases, rejected_cases);
	return 0;
}
