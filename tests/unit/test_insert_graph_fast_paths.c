#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, \
			#condition, error.message); \
		goto fail; \
	} \
} while (0)

static int dump_views;

static int text_equal(const char *a, const char *b)
{
	return a == NULL || b == NULL ? a == b : strcmp(a, b) == 0;
}

static int literal_equal(const sqlparser_literal_view_t *a,
	const sqlparser_literal_view_t *b)
{
	return a->kind == b->kind && a->integer_value == b->integer_value &&
		a->boolean_value == b->boolean_value &&
		a->quoted_identifier == b->quoted_identifier &&
		text_equal(a->string_value, b->string_value) &&
		text_equal(a->float_value, b->float_value);
}

static int check_cells(sqlparser_handle_t *handle, size_t statement,
	size_t rows, size_t columns, const sqlparser_graph_value_kind_t *kinds)
{
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_relation_t relation;
	sqlparser_relation_view_t selected_relation;
	sqlparser_literal_view_t literal;
	sqlparser_status_t status;
	size_t index;
	size_t cell_index;
	char *selector_text = NULL;
	char expected[128];

	memset(&error, 0, sizeof(error));
	CHECK(sqlparser_statement_query_graph(handle, statement, &graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.rows.count == rows * columns);
	CHECK(dml.has_target_relation);
	CHECK(sqlparser_query_graph_relation_at(&graph, dml.target_relation_index,
		&relation, &error) == SQLPARSER_STATUS_OK);
	CHECK(relation.has_selector && relation.selector.kind == SQLPARSER_SELECTOR_KIND_RELATION);
	CHECK(relation.selector.item_index == 0U && relation.selector.statement_index == statement);
	CHECK(sqlparser_selector_relation(handle, &relation.selector, &selected_relation,
		&error) == SQLPARSER_STATUS_OK);
	CHECK(text_equal(relation.object_name, selected_relation.table_name));
	for (index = 0U; index < rows * columns; index++) {
		CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, index,
			&cell_index, &error) == SQLPARSER_STATUS_OK);
		memset(&cell, 0xA5, sizeof(cell));
		CHECK(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell,
			&error) == SQLPARSER_STATUS_OK);
		CHECK(cell.index == cell_index && cell.statement_index == statement && cell.dml_index == 0U);
		CHECK(cell.row_index == index / columns && cell.column_ordinal == index % columns);
		CHECK(cell.kind == (kinds != NULL ? kinds[index] : SQLPARSER_GRAPH_VALUE_LITERAL));
		CHECK(cell.has_selector && cell.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
		CHECK(cell.selector.statement_index == statement && cell.selector.item_index == 0U);
		CHECK(cell.selector.row_index == cell.row_index && cell.selector.column_index == cell.column_ordinal);
		CHECK(!cell.has_source_field && !cell.has_source_target);
		CHECK(sqlparser_selector_format(&cell.selector, &selector_text, &error) == SQLPARSER_STATUS_OK);
		(void)snprintf(expected, sizeof(expected), "stmt[%zu].insert_cell[%zu][%zu]",
			statement, index / columns, index % columns);
		CHECK(strcmp(selector_text, expected) == 0);
		free(selector_text);
		selector_text = NULL;
		if (cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
			status = sqlparser_insert_cell_literal(handle, statement, index / columns,
				index % columns, &literal, &error);
			CHECK(status == (cell.literal.kind == SQLPARSER_LITERAL_KIND_UNKNOWN ?
				SQLPARSER_STATUS_UNSUPPORTED : SQLPARSER_STATUS_OK));
			CHECK(literal_equal(&cell.literal, &literal));
			CHECK(!cell.has_bind && !cell.has_bind_sql && !cell.has_bind_position);
			CHECK(cell.bind[0] == '\0' && cell.bind_sql[0] == '\0');
		}
	}
	return 0;
fail:
	free(selector_text);
	return 1;
}

static int check_case(sqlparser_dialect_t dialect, const char *sql,
	size_t statement, size_t rows, size_t columns,
	const sqlparser_graph_value_kind_t *kinds)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle = NULL;
	sqlparser_error_t error;
	char *before = NULL;
	char *after = NULL;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t list;
	char target[128];
	sqlparser_selector_t relation_selector;
	sqlparser_literal_view_t literal;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(check_cells(handle, statement, rows, columns, kinds) == 0);
	CHECK(sqlparser_export_view_json(handle, 0, &before, &error) == SQLPARSER_STATUS_OK);
	if (dump_views) printf("dialect=%d SQL=%s\n%s\n", (int)dialect, sql, before);
	/* A failed batch poisons the handle, even after a valid first edit. */
	memset(patches, 0, sizeof(patches));
	(void)snprintf(target, sizeof(target), "stmt[%zu].insert_cell[0][0]", statement);
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = target;
	patches[0].sql = "987";
	patches[1].op = SQLPARSER_PATCH_REPLACE;
	patches[1].selector = "stmt[999].insert_cell[0][0]";
	patches[1].sql = "654";
	list.items = patches;
	list.count = 2U;
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(sqlparser_test_failed_handle(handle));
	sqlparser_handle_destroy(handle); handle = NULL;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(check_cells(handle, statement, rows, columns, kinds) == 0);
	/* Successful changes rebuild the cache and use relation-binding fallback. */
	list.count = 1U;
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_insert_cell_literal(handle, statement, 0U, 0U,
		&literal, &error) == SQLPARSER_STATUS_OK);
	CHECK(literal.kind == SQLPARSER_LITERAL_KIND_INTEGER && literal.integer_value == 987LL);
	CHECK(check_cells(handle, statement, rows, columns, kinds) == 0);
	memset(&relation_selector, 0, sizeof(relation_selector));
	relation_selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
	relation_selector.statement_index = statement;
	CHECK(sqlparser_selector_set_relation_name(handle, &relation_selector, NULL,
		"renamed_t", &error) == SQLPARSER_STATUS_OK);
	CHECK(check_cells(handle, statement, rows, columns, kinds) == 0);
	free(before);
	free(after);
	sqlparser_handle_destroy(handle);
	return 0;
fail:
	fprintf(stderr, "dialect=%d SQL=%s\n", (int)dialect, sql);
	free(before);
	free(after);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int check_selector_values(void)
{
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	sqlparser_selector_t parsed;
	size_t values[3U * sizeof(size_t) * CHAR_BIT + 8U];
	size_t count = 0U;
	size_t power;
	size_t i;
	size_t j;
	char expected[128];
	char *text = NULL;
	int length;

	memset(&error, 0, sizeof(error));
	values[count++] = 0U;
	values[count++] = SIZE_MAX;
	values[count++] = SIZE_MAX - 1U;
	for (power = 1U;; power *= 10U) {
		values[count++] = power - 1U;
		values[count++] = power;
		values[count++] = power + 1U;
		if (power > SIZE_MAX / 10U) break;
	}
	for (i = 0U; i < count; i++) {
		for (j = 0U; j < 3U; j++) {
			memset(&selector, 0, sizeof(selector));
			selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
			selector.statement_index = j == 0U ? values[i] : SIZE_MAX;
			selector.row_index = j == 1U ? values[i] : SIZE_MAX;
			selector.column_index = j == 2U ? values[i] : SIZE_MAX;
			selector.item_index = SIZE_MAX; /* Unused, as in the original formatter. */
			length = snprintf(expected, sizeof(expected), "stmt[%zu].insert_cell[%zu][%zu]",
				selector.statement_index, selector.row_index, selector.column_index);
			if (length < 0 || (size_t)length >= sizeof(expected)) {
				CHECK(sqlparser_selector_format(&selector, &text, &error) == SQLPARSER_STATUS_INTERNAL_ERROR);
				CHECK(text == NULL);
				continue;
			}
			CHECK(sqlparser_selector_format(&selector, &text, &error) == SQLPARSER_STATUS_OK);
			CHECK(strcmp(text, expected) == 0);
			CHECK(sqlparser_selector_parse(text, &parsed, &error) == SQLPARSER_STATUS_OK);
			CHECK(parsed.kind == selector.kind && parsed.statement_index == selector.statement_index &&
				parsed.row_index == selector.row_index && parsed.column_index == selector.column_index &&
				parsed.item_index == 0U);
			free(text);
			text = NULL;
		}
	}
	CHECK(sqlparser_selector_format(NULL, &text, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(text == NULL && strcmp(error.message, "selector must not be NULL") == 0);
	CHECK(sqlparser_selector_format(&selector, NULL, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(strcmp(error.message, "out_text must not be NULL") == 0);
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	CHECK(sqlparser_selector_format(&selector, &text, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(text == NULL && strcmp(error.message, "selector kind is invalid") == 0);
	return 0;
fail:
	free(text);
	return 1;
}

int main(int argc, char **argv)
{
	static const char *const locales[] = { "C", "", "C.UTF-8", "en_US.UTF-8", "de_DE.UTF-8", "ar_EG.UTF-8" };
	static const sqlparser_graph_value_kind_t mixed[] = {
		SQLPARSER_GRAPH_VALUE_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL,
		SQLPARSER_GRAPH_VALUE_LITERAL, SQLPARSER_GRAPH_VALUE_EXPRESSION
	};
	static const sqlparser_graph_value_kind_t bind_default[] = {
		SQLPARSER_GRAPH_VALUE_LITERAL, SQLPARSER_GRAPH_VALUE_BIND,
		SQLPARSER_GRAPH_VALUE_DEFAULT
	};
	sqlparser_dialect_t dialect;
	size_t i;
	size_t offset;
	char large_sql[8192];
	int failures = 0;

	if (argc == 2 && strcmp(argv[1], "--dump") == 0) dump_views = 1;
	else if (argc != 1) return 2;
	offset = (size_t)snprintf(large_sql, sizeof(large_sql), "INSERT INTO t(a,b) VALUES ");
	for (i = 0U; i < 129U; i++) {
		offset += (size_t)snprintf(large_sql + offset, sizeof(large_sql) - offset,
			"%s(%zu,'row')", i == 0U ? "" : ",", i);
	}

	for (i = 0U; i < sizeof(locales) / sizeof(locales[0]); i++) {
		if (setlocale(LC_ALL, locales[i]) != NULL) failures += check_selector_values();
	}
	(void)setlocale(LC_ALL, "C");
	for (dialect = SQLPARSER_DIALECT_POSTGRESQL;
	     dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
		failures += check_case(dialect,
			"INSERT INTO t(a,b,c,d,e,f) VALUES (0,-2147483648,2147483648,1.25e-3,'a''b',NULL)",
			0U, 1U, 6U, NULL);
		/* Later-row expressions must prevent the literal-only inventory shortcut. */
		failures += check_case(dialect,
			"INSERT INTO t(a,b) VALUES (1,2),(3,abs(-4))", 0U, 2U, 2U, mixed);
		failures += check_case(dialect,
			"INSERT INTO t(a,b) VALUES ((1),'grouped')", 0U, 1U, 2U, NULL);
		failures += check_case(dialect, large_sql, 0U, 129U, 2U, NULL);
	}
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"INSERT INTO t(a) VALUES (B'101')", 0U, 1U, 1U, NULL);
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"INSERT INTO t(a,b,c) VALUES (1,$1,DEFAULT)", 0U, 1U, 3U, bind_default);
	failures += check_case(SQLPARSER_DIALECT_MYSQL,
		"INSERT INTO t(a,b,c) VALUES (1,?,DEFAULT)", 0U, 1U, 3U, bind_default);
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"SELECT 9; INSERT INTO t(a,b) VALUES (true,false),(NULL,E'a\\\\b')",
		1U, 2U, 2U, NULL);
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"INSERT INTO t(a) VALUES (1) RETURNING a + 1", 0U, 1U, 1U, NULL);
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"INSERT INTO t(a) VALUES (1) ON CONFLICT(a) DO UPDATE SET a = EXCLUDED.a + 1",
		0U, 1U, 1U, NULL);
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"WITH q AS (SELECT 42 AS a) INSERT INTO t(a) VALUES (1)", 0U, 1U, 1U, NULL);
	failures += check_case(SQLPARSER_DIALECT_MYSQL,
		"INSERT INTO t(a,b) VALUES (\"quoted string\",'a\\\\b')", 0U, 1U, 2U, NULL);
	if (failures != 0) return 1;
	puts("INSERT graph fast/fallback paths and selector boundaries passed");
	return 0;
}
