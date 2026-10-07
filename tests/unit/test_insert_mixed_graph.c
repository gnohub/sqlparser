#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jansson.h"
#include "sqlparser/sqlparser.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, \
			#condition, error.message); \
		goto fail; \
	} \
} while (0)

static int dump_views;

static const char *cell_sql(sqlparser_dialect_t dialect, size_t row,
	size_t column, size_t rows, size_t columns, int late_expression,
	int field_fallback, int patched)
{
	static const char *const fragments[] = {
		"'alpha''beta'", "123", "CURRENT_TIMESTAMP", "abs(-7)",
		"(2 + 3)", "(SELECT 9 FROM source_t)", "DEFAULT", NULL,
		"NULL", "COALESCE(NULL, 4)", "(CURRENT_DATE)",
		"CAST(42 AS INTEGER)", "CASE WHEN 1 = 1 THEN 7 ELSE 8 END"
	};

	if (patched && row == 0U && column == 0U) return "'replacement'";
	if (rows == 5000U) return column == 2U ? "CURRENT_TIMESTAMP" : "'literal'";
	if (field_fallback && row + 1U == rows && column + 1U == columns)
		return "t.c0";
	if (late_expression)
		return row + 1U == rows && column + 1U == columns ?
			"CURRENT_TIMESTAMP" : "'literal'";
	if (column == 7U)
		return dialect == SQLPARSER_DIALECT_MYSQL ? "?" : "$1";
	return fragments[column];
}

static sqlparser_graph_value_kind_t cell_kind(size_t row, size_t column,
	size_t rows, size_t columns, int late_expression, int field_fallback)
{
	if (rows == 5000U) return column == 2U ?
		SQLPARSER_GRAPH_VALUE_EXPRESSION : SQLPARSER_GRAPH_VALUE_LITERAL;
	if (field_fallback && row + 1U == rows && column + 1U == columns)
		return SQLPARSER_GRAPH_VALUE_FIELD;
	if (late_expression)
		return row + 1U == rows && column + 1U == columns ?
			SQLPARSER_GRAPH_VALUE_EXPRESSION : SQLPARSER_GRAPH_VALUE_LITERAL;
	if (column == 0U || column == 1U || column == 8U)
		return SQLPARSER_GRAPH_VALUE_LITERAL;
	if (column == 6U) return SQLPARSER_GRAPH_VALUE_DEFAULT;
	if (column == 7U) return SQLPARSER_GRAPH_VALUE_BIND;
	return SQLPARSER_GRAPH_VALUE_EXPRESSION;
}

static int check_graph(sqlparser_handle_t *handle, sqlparser_dialect_t dialect,
	size_t rows, size_t columns, int late_expression, int field_fallback,
	int patched)
{
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_relation_t relation;
	sqlparser_relation_view_t selected_relation;
	json_t *root = NULL;
	json_t *json_rows = NULL;
	int check_json = rows * columns <= 1000U;
	char *json = NULL;
	char *selector = NULL;
	size_t ordinal;

	memset(&error, 0, sizeof(error));
	CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.rows.count == rows * columns && dml.target_columns.count == columns);
	CHECK(sqlparser_query_graph_relation_at(&graph, dml.target_relation_index,
		&relation, &error) == SQLPARSER_STATUS_OK);
	CHECK(relation.has_selector && relation.selector.item_index == 0U);
	CHECK(sqlparser_selector_relation(handle, &relation.selector,
		&selected_relation, &error) == SQLPARSER_STATUS_OK);
	CHECK(strcmp(selected_relation.table_name, "t") == 0);
	if (check_json) {
		CHECK(sqlparser_export_view_json(handle, 0, &json, &error) == SQLPARSER_STATUS_OK);
		root = json_loads(json, 0U, NULL);
		CHECK(root != NULL);
		json_rows = json_object_get(json_object_get(json_object_get(
			json_array_get(json_object_get(root, "statements"), 0U),
			"query_graph"), "dml"), "rows");
		CHECK(json_array_size(json_rows) == rows * columns);
	}
	for (ordinal = 0U; ordinal < rows * columns; ordinal++) {
		sqlparser_graph_dml_cell_t cell;
		sqlparser_literal_view_t literal;
		sqlparser_selector_t parsed;
		size_t cell_index;
		size_t row = ordinal / columns;
		size_t column = ordinal % columns;
		sqlparser_graph_value_kind_t kind = cell_kind(row, column,
			rows, columns, late_expression, field_fallback);
		json_t *json_cell = json_array_get(json_rows, ordinal);
		const char *expression;

		CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, ordinal,
			&cell_index, &error) == SQLPARSER_STATUS_OK);
		CHECK(cell_index == ordinal);
		CHECK(sqlparser_query_graph_dml_cell_at(&graph, cell_index,
			&cell, &error) == SQLPARSER_STATUS_OK);
		CHECK(cell.index == ordinal && cell.dml_index == 0U && cell.statement_index == 0U);
		CHECK(cell.row_index == row && cell.column_ordinal == column && cell.kind == kind);
		CHECK(cell.has_selector && cell.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
		CHECK(cell.selector.statement_index == 0U && cell.selector.item_index == 0U);
		CHECK(cell.selector.row_index == row && cell.selector.column_index == column);
		CHECK(sqlparser_selector_format(&cell.selector, &selector, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_selector_parse(selector, &parsed, &error) == SQLPARSER_STATUS_OK);
		CHECK(parsed.kind == cell.selector.kind && parsed.row_index == row &&
			parsed.column_index == column);
		free(selector); selector = NULL;
		CHECK(cell.has_source_field == (kind == SQLPARSER_GRAPH_VALUE_FIELD));
		expression = json_string_value(json_object_get(json_cell, "expression_sql"));
		if (check_json && kind == SQLPARSER_GRAPH_VALUE_EXPRESSION) {
			CHECK(expression != NULL);
			CHECK(strcmp(expression, cell_sql(dialect, row, column, rows,
				columns, late_expression, field_fallback, patched)) == 0);
		} else {
			CHECK(expression == NULL);
		}
		if (!check_json && kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
			CHECK(cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
			CHECK(strcmp(cell.literal.string_value,
				patched && row == 0U && column == 0U ? "replacement" : "literal") == 0);
		}
		if (check_json && kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
			CHECK(sqlparser_insert_cell_literal(handle, 0U, row, column,
				&literal, &error) == SQLPARSER_STATUS_OK);
			CHECK(literal.kind == cell.literal.kind);
			if (literal.kind == SQLPARSER_LITERAL_KIND_STRING) {
				CHECK(strcmp(literal.string_value, cell.literal.string_value) == 0);
				if (patched && row == 0U && column == 0U)
					CHECK(strcmp(cell.literal.string_value, "replacement") == 0);
			}
		}
		if (kind == SQLPARSER_GRAPH_VALUE_BIND)
			CHECK(cell.has_bind && cell.has_bind_sql);
	}
	if (dump_views && json != NULL) {
		printf("dialect=%d rows=%zu columns=%zu late=%d field=%d patched=%d\n%s\n",
			(int)dialect, rows, columns, late_expression, field_fallback, patched, json);
	}
	json_decref(root);
	free(json);
	return 0;
fail:
	json_decref(root);
	free(selector);
	free(json);
	return 1;
}

static int check_case(sqlparser_dialect_t dialect, size_t rows, size_t columns,
	int late_expression, int field_fallback)
{
	sqlparser_error_t error;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle = NULL;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t list;
	char *sql = NULL;
	size_t capacity = 128U + rows * columns * 64U;
	size_t used;
	size_t row;
	size_t column;

	memset(&error, 0, sizeof(error));
	sql = malloc(capacity);
	CHECK(sql != NULL);
	used = (size_t)snprintf(sql, capacity, "INSERT INTO t(");
	for (column = 0U; column < columns; column++)
		used += (size_t)snprintf(sql + used, capacity - used, "%sc%zu",
			column == 0U ? "" : ",", column);
	used += (size_t)snprintf(sql + used, capacity - used, ") VALUES ");
	for (row = 0U; row < rows; row++) {
		used += (size_t)snprintf(sql + used, capacity - used, "%s(", row == 0U ? "" : ",");
		for (column = 0U; column < columns; column++)
			used += (size_t)snprintf(sql + used, capacity - used, "%s%s",
				column == 0U ? "" : ",", cell_sql(dialect, row, column,
				rows, columns, late_expression, field_fallback, 0));
		used += (size_t)snprintf(sql + used, capacity - used, ")");
	}
	CHECK(used < capacity);
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(check_graph(handle, dialect, rows, columns, late_expression, field_fallback, 0) == 0);
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].insert_cell[0][0]";
	patch.sql = "'replacement'";
	list.items = &patch;
	list.count = 1U;
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(check_graph(handle, dialect, rows, columns, late_expression, field_fallback, 1) == 0);
	free(sql);
	sqlparser_handle_destroy(handle);
	return 0;
fail:
	fprintf(stderr, "dialect=%d rows=%zu columns=%zu late=%d field=%d\n",
		(int)dialect, rows, columns, late_expression, field_fallback);
	free(sql);
	sqlparser_handle_destroy(handle);
	return 1;
}

int main(int argc, char **argv)
{
	static const size_t row_counts[] = { 31U, 32U, 33U, 65U };
	static const size_t column_counts[] = { 3U, 5U, 7U, 8U, 9U, 10U, 11U, 13U };
	sqlparser_dialect_t dialect;
	size_t row_index;
	size_t column_index;
	int failures = 0;

	if (argc == 2 && strcmp(argv[1], "--dump") == 0) dump_views = 1;
	else if (argc != 1) return 2;
	for (dialect = SQLPARSER_DIALECT_POSTGRESQL;
	     dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++)
		failures += check_case(dialect, 33U, 3U, 1, 0);
	for (row_index = 0U; row_index < sizeof(row_counts) / sizeof(row_counts[0]); row_index++) {
		for (column_index = 0U; column_index < sizeof(column_counts) / sizeof(column_counts[0]); column_index++) {
			failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
				row_counts[row_index], column_counts[column_index], 0, 0);
			failures += check_case(SQLPARSER_DIALECT_MYSQL,
				row_counts[row_index], column_counts[column_index], 0, 0);
		}
	}
	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL, 33U, 9U, 0, 1);
	/* Functional full-graph coverage; this test has no performance timer. */
	failures += check_case(SQLPARSER_DIALECT_MYSQL, 5000U, 9U, 0, 0);
	if (failures != 0) return 1;
	puts("Mixed VALUES graph cells, selectors and expression SQL passed");
	return 0;
}
