/* Compare --dump output against the same test linked to the reference library.
 * Allocation injection starts after AST unpack, isolating graph construction. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, #x, error.message); \
	exit(1); \
} } while (0)

static sqlparser_error_t error;
static int dump;
static size_t calls, fail_at;
static uint64_t allocation_hash;

#ifdef SQLPARSER_GRAPH_BULK_ALLOC_WRAPPERS
static int armed;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);

static int allocation(unsigned kind, size_t count, size_t size)
{
	if (!armed) return 0;
	allocation_hash = (allocation_hash ^ kind) * UINT64_C(1099511628211);
	allocation_hash = (allocation_hash ^ count) * UINT64_C(1099511628211);
	allocation_hash = (allocation_hash ^ size) * UINT64_C(1099511628211);
	return ++calls == fail_at;
}

void *__wrap_malloc(size_t size)
{
	return allocation(1U, 1U, size) ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
	return allocation(2U, count, size) ? NULL : __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
	return allocation(3U, 1U, size) ? NULL : __real_realloc(pointer, size);
}
#endif

static char *make_sql(size_t rows, size_t columns, int variant)
{
	size_t capacity = rows * (columns + 1U) * 32U + 512U;
	char *sql = malloc(capacity);
	size_t used, row, column;

	CHECK(sql != NULL);
	used = (size_t)snprintf(sql, capacity, "%sINSERT INTO t(",
		variant == 5 ? "SELECT 9; " :
		variant == 6 ? "WITH q AS (SELECT 1) " : "");
	for (column = 0U; column < columns; column++)
		used += (size_t)snprintf(sql + used, capacity - used, "%sc%zu", column ? "," : "", column);
	used += (size_t)snprintf(sql + used, capacity - used, ") VALUES ");
	for (row = 0U; row < rows; row++) {
		size_t width = columns + (variant == 1 && row + 1U == rows);
		used += (size_t)snprintf(sql + used, capacity - used, "%s(", row ? "," : "");
		for (column = 0U; column < width; column++) {
			const char *value = column % 4U == 0U ? "17" :
				column % 4U == 1U ? "'a''b'" :
				column % 4U == 2U ? "NULL" : "1.25";
			if (row + 1U == rows && column + 1U == width) {
				if (variant == 2) value = "abs(-19)";
				if (variant == 3) value = "$1";
				if (variant == 4) value = "DEFAULT";
				if (variant == 9) value = "B'101'";
			}
			used += (size_t)snprintf(sql + used, capacity - used,
				"%s%s", column ? "," : "", value);
		}
		used += (size_t)snprintf(sql + used, capacity - used, ")");
	}
	if (variant == 7) used += (size_t)snprintf(sql + used, capacity - used, " RETURNING *");
	if (variant == 8) used += (size_t)snprintf(sql + used, capacity - used, " ON CONFLICT DO NOTHING");
	CHECK(used < capacity);
	return sql;
}

static void check_graph(sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph, size_t rows, size_t columns, int variant)
{
	sqlparser_graph_dml_t dml;
	size_t row, column, ordinal = 0U;
	char *json = NULL;

	CHECK(sqlparser_query_graph_dml(graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.rows.count == rows * columns + (variant == 1));
	for (row = 0U; row < rows; row++) {
		size_t width = columns + (variant == 1 && row + 1U == rows);
		for (column = 0U; column < width; column++, ordinal++) {
			sqlparser_graph_dml_cell_t cell;
			size_t index;
			CHECK(sqlparser_query_graph_span_index_at(graph, dml.rows, ordinal,
				&index, &error) == SQLPARSER_STATUS_OK);
			CHECK(index == ordinal);
			CHECK(sqlparser_query_graph_dml_cell_at(graph, index, &cell, &error) == SQLPARSER_STATUS_OK);
			CHECK(cell.index == ordinal && cell.row_index == row && cell.column_ordinal == column);
			CHECK(cell.has_selector && cell.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
			CHECK(cell.selector.statement_index == (size_t)(variant == 5));
			CHECK(cell.selector.row_index == row && cell.selector.column_index == column);
			CHECK(cell.selector.item_index == 0U && cell.dml_index == 0U);
			CHECK(!cell.has_source_field && !cell.has_source_target);
			if (variant == 9 && row + 1U == rows && column + 1U == width)
				CHECK(cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL && cell.literal.kind == SQLPARSER_LITERAL_KIND_UNKNOWN);
		}
	}
	CHECK(sqlparser_export_view_json(handle, 0, &json, &error) == SQLPARSER_STATUS_OK);
	if (dump) printf("%s\n", json);
	sqlparser_string_free(json);
}

static void run_case(sqlparser_dialect_t dialect, size_t rows, size_t columns,
	int variant, int fault_sweep)
{
	char *sql = make_sql(rows, columns, variant);
	size_t failure, count = 0U;

	for (failure = 0U; failure <= count; failure++) {
		sqlparser_parse_options_t options;
		sqlparser_handle_t *handle = NULL;
		sqlparser_query_graph_view_t graph;
		sqlparser_status_t status;

		sqlparser_parse_options_default(&options);
		options.dialect = dialect;
		CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
		calls = 0U;
		fail_at = failure;
		allocation_hash = UINT64_C(1469598103934665603);
#ifdef SQLPARSER_GRAPH_BULK_ALLOC_WRAPPERS
		armed = 1;
#endif
		status = sqlparser_statement_query_graph(handle, variant == 5 ? 1U : 0U, &graph, &error);
#ifdef SQLPARSER_GRAPH_BULK_ALLOC_WRAPPERS
		armed = 0;
#endif
		if (dump) printf("dialect=%d rows=%zu columns=%zu variant=%d fail=%zu calls=%zu hash=%llu status=%d error=%s\n",
			(int)dialect, rows, columns, variant, failure, calls,
			(unsigned long long)allocation_hash, (int)status, error.message);
		if (failure == 0U) {
			CHECK(status == SQLPARSER_STATUS_OK);
			if (fault_sweep) count = calls;
		} else {
			CHECK(calls >= failure);
			CHECK(status == SQLPARSER_STATUS_OK || status == SQLPARSER_STATUS_NO_MEMORY);
			CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
		}
		check_graph(handle, &graph, rows, columns, variant);
		sqlparser_handle_destroy(handle);
	}
	free(sql);
}

int main(int argc, char **argv)
{
	static const size_t row_counts[] = {1U, 31U, 32U, 33U, 129U};
	size_t i, columns;
	int dialect, variant;

	if (argc == 2 && strcmp(argv[1], "--dump") == 0) dump = 1;
	else CHECK(argc == 1);
	for (i = 0U; i < sizeof(row_counts) / sizeof(row_counts[0]); i++)
		for (columns = 1U; columns <= 5U; columns++)
			run_case(SQLPARSER_DIALECT_MYSQL, row_counts[i], columns, 0, row_counts[i] == 32U);
	for (dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++)
		run_case((sqlparser_dialect_t)dialect, 33U, 4U, 0, 0);
	for (variant = 1; variant <= 9; variant++)
		run_case(SQLPARSER_DIALECT_POSTGRESQL, 33U, 3U, variant, 0);
	run_case(SQLPARSER_DIALECT_MYSQL, 5000U, 2U, 0, 0);
	puts("INSERT bulk rows: thresholds, row-major spans, all dialects, fallbacks, unsupported literals and graph OOM retries passed");
	return 0;
}
