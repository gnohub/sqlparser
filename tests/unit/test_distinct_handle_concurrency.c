/*
 * One owning thread per handle, as required by the public API thread model.
 * No handle, graph view, borrowed string, error object, or output buffer is
 * shared between workers. The only shared mutable object is the start gate.
 *
 * The normal GNU Makefile discovers this test and already links pthreads:
 *   make bin/test_distinct_handle_concurrency
 *   timeout 120s ./bin/test_distinct_handle_concurrency
 * For a longer run, the optional argument is the number of thread waves:
 *   timeout 300s ./bin/test_distinct_handle_concurrency 20
 * On systems without `timeout`, use an external process watchdog, for example:
 *   python3 -c 'import subprocess; subprocess.run(
 *       ["./bin/test_distinct_handle_concurrency"], timeout=120, check=True)'
 * Use the same bounded runner for sanitizer/Valgrind builds, increasing the
 * deadline for instrumentation. A timeout is a failure, never a passing skip.
 * All owned allocations are freed, including on assertion failure, so this
 * test can also exercise leak checking and thread-local teardown.
 *
 * Windows builds, or ports defining SQLPARSER_TEST_NO_PTHREAD, report SKIP.
 * They must not claim concurrent-handle coverage from this executable.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(SQLPARSER_TEST_NO_PTHREAD)

int main(void)
{
	puts("SKIP: distinct-handle concurrency requires POSIX pthreads");
	return 0;
}

#else

#include <errno.h>
#include <pthread.h>

#include "sqlparser/sqlparser.h"

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define WORKER_COUNT 4U
#define DEFAULT_WAVES 2U
#define MAX_WAVES 1000U

static const sqlparser_dialect_t dialects[] = {
	SQLPARSER_DIALECT_POSTGRESQL,
	SQLPARSER_DIALECT_MYSQL,
	SQLPARSER_DIALECT_ORACLE,
	SQLPARSER_DIALECT_SQLSERVER,
	SQLPARSER_DIALECT_DAMENG,
	SQLPARSER_DIALECT_VASTBASE_ORACLE,
	SQLPARSER_DIALECT_VASTBASE_MYSQL,
	SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
	SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
	SQLPARSER_DIALECT_KINGBASE_ORACLE,
	SQLPARSER_DIALECT_KINGBASE_MYSQL,
	SQLPARSER_DIALECT_KINGBASE_POSTGRESQL,
	SQLPARSER_DIALECT_KINGBASE_SQLSERVER
};

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	size_t ready;
	int start;
} start_gate_t;

typedef struct {
	start_gate_t *gate;
	size_t wave;
	size_t worker;
	size_t completed;
	size_t bulk_completed;
	int failed;
} worker_t;

/* A failed pthread operation is a test infrastructure failure. Exiting also
 * avoids stranding workers if thread creation or the start gate fails. */
static void require_pthread(int result, const char *operation)
{
	if (result != 0) {
		fprintf(stderr, "FAIL: %s: %s\n", operation, strerror(result));
		exit(EXIT_FAILURE);
	}
}

static int unchanged(sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph, const char *sql,
	const char *view, sqlparser_error_t *error)
{
	sqlparser_graph_dml_t dml;
	char *actual_sql = NULL;
	char *actual_view = NULL;
	int ok;

	ok = sqlparser_query_graph_dml(graph, &dml, error) == SQLPARSER_STATUS_OK &&
		sqlparser_deparse(handle, &actual_sql, error) == SQLPARSER_STATUS_OK &&
		actual_sql != NULL && strcmp(actual_sql, sql) == 0 &&
		sqlparser_export_view_json(handle, 0, &actual_view, error) == SQLPARSER_STATUS_OK &&
		actual_view != NULL && strcmp(actual_view, view) == 0;
	if (!ok) {
		/* Keep the original failure and strict assertion, but obtain the SQL
		 * even when a stale graph caused the check to short-circuit. */
		sqlparser_error_t diagnostic_error;

		memset(&diagnostic_error, 0, sizeof(diagnostic_error));
		if (actual_sql == NULL)
			(void)sqlparser_deparse(handle, &actual_sql, &diagnostic_error);
		fprintf(stderr, "Unchanged-state diagnostic: expected SQL=[%s] actual SQL=[%s]\n",
			sql, actual_sql != NULL ? actual_sql : "<deparse failed>");
	}
	sqlparser_string_free(actual_sql);
	sqlparser_string_free(actual_view);
	return ok;
}

static int identity_batch_result(sqlparser_handle_t *handle,
	sqlparser_query_graph_view_t *graph, const char *sql, const char *view,
	sqlparser_error_t *error)
{
	sqlparser_graph_dml_t dml;
	sqlparser_query_graph_view_t current_graph;
	sqlparser_status_t borrowed_status;

	/* Observe the old view before rebuilding anything on the handle. */
	borrowed_status = sqlparser_query_graph_dml(graph, &dml, error);
	if (sqlparser_statement_query_graph(handle, 0U, &current_graph, error) !=
	    SQLPARSER_STATUS_OK)
		return 0;
	if (current_graph.generation != graph->generation + 1UL ||
	    borrowed_status != SQLPARSER_STATUS_INVALID_ARGUMENT)
		return 0;
	if (!unchanged(handle, &current_graph, sql, view, error))
		return 0;
	*graph = current_graph;
	return 1;
}

static int poisoned(sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph, sqlparser_error_t *error)
{
	sqlparser_graph_dml_t dml;
	sqlparser_query_graph_view_t current_graph;
	sqlparser_patch_list_t empty = {NULL, 0U};
	char *sql = NULL;
	char *view = NULL;
	int ok;

	ok = sqlparser_statement_count(handle) == 0U &&
		sqlparser_query_graph_dml(graph, &dml, error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
		sqlparser_statement_query_graph(handle, 0U, &current_graph, error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
		sqlparser_deparse(handle, &sql, error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
		sql == NULL &&
		sqlparser_export_view_json(handle, 0, &view, error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
		view == NULL &&
		sqlparser_apply_patch(handle, &empty, error) == SQLPARSER_STATUS_INVALID_ARGUMENT;
	sqlparser_string_free(sql);
	sqlparser_string_free(view);
	return ok;
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "FAIL: %s:%d wave=%zu worker=%zu iteration=%zu " \
			"dialect=%d: %s; error=%s\n", __FILE__, __LINE__, \
			worker->wave, worker->worker, iteration, (int)options.dialect, \
			#expression, error.message); \
		goto done; \
	} \
} while (0)

static int run_lifecycle(const worker_t *worker, size_t iteration)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle = NULL;
	sqlparser_handle_t *reference = NULL;
	sqlparser_handle_t *malformed = NULL;
	sqlparser_query_graph_view_t before_graph;
	sqlparser_query_graph_view_t after_graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_selector_t selectors[3];
	sqlparser_selector_t parsed_selector;
	sqlparser_literal_view_t literal;
	sqlparser_patch_t patches[3];
	sqlparser_patch_list_t list;
	char *selector_text[3] = {NULL, NULL, NULL};
	char *before_view = NULL;
	char *after_view = NULL;
	char *reference_view = NULL;
	char *output = NULL;
	char sql[512];
	char expected[512];
	char old_value[80];
	char new_value[80];
	char old_sql[88];
	char new_sql[88];
	const char *original_values[3];
	const char *changed_values[3];
	size_t ordinal;
	size_t cell_index;
	int failed = 1;

	memset(&error, 0, sizeof(error));
	memset(patches, 0, sizeof(patches));
	sqlparser_parse_options_default(&options);
	options.dialect = dialects[(iteration + worker->worker) % ARRAY_LENGTH(dialects)];
	(void)snprintf(old_value, sizeof(old_value), "old_%zu_%zu_%zu",
		worker->wave, worker->worker, iteration);
	(void)snprintf(new_value, sizeof(new_value), "new_%zu_%zu_%zu_longer",
		worker->wave, worker->worker, iteration);
	(void)snprintf(old_sql, sizeof(old_sql), "'%s'", old_value);
	(void)snprintf(new_sql, sizeof(new_sql), "'%s'", new_value);
	(void)snprintf(sql, sizeof(sql),
		"/*head*/ INSERT INTO t(id,a,b,c,keep_value) "
		"VALUES (7,%s,'before','stable','untouched'); /*tail*/", old_sql);
	(void)snprintf(expected, sizeof(expected),
		"/*head*/ INSERT INTO t(id,a,b,c,keep_value) "
		"VALUES (7,%s,'can''t','','untouched'); /*tail*/", new_sql);
	original_values[0] = old_value;
	original_values[1] = "before";
	original_values[2] = "stable";
	changed_values[0] = new_value;
	changed_values[1] = "can't";
	changed_values[2] = "";

	/* Invalid parse paths and successful parse paths run simultaneously in
	 * different workers, including the process's first parser initialization. */
	CHECK(sqlparser_parse_with_options("INSERT INTO", &options,
		&malformed, &error) != SQLPARSER_STATUS_OK);
	CHECK(malformed == NULL);
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(handle != NULL && sqlparser_statement_count(handle) == 1U);
	CHECK(sqlparser_handle_dialect(handle) == options.dialect);
	CHECK(sqlparser_statement_query_graph(handle, 0, &before_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&before_graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.kind == SQLPARSER_GRAPH_DML_INSERT &&
		dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_VALUES &&
		dml.target_columns.count == 5U && dml.rows.count == 5U);

	/* Derive all patch targets from the public graph, rather than bypassing
	 * graph construction by hard-coding the selectors used for mutation. */
	for (ordinal = 0U; ordinal < 3U; ordinal++) {
		char expected_selector[64];

		CHECK(sqlparser_query_graph_span_index_at(&before_graph, dml.rows,
			ordinal + 1U, &cell_index, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_query_graph_dml_cell_at(&before_graph, cell_index,
			&cell, &error) == SQLPARSER_STATUS_OK);
		CHECK(cell.row_index == 0U && cell.column_ordinal == ordinal + 1U &&
			cell.has_selector && cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL);
		selectors[ordinal] = cell.selector;
		CHECK(sqlparser_selector_format(&selectors[ordinal],
			&selector_text[ordinal], &error) == SQLPARSER_STATUS_OK);
		(void)snprintf(expected_selector, sizeof(expected_selector),
			"stmt[0].insert_cell[0][%zu]", ordinal + 1U);
		CHECK(selector_text[ordinal] != NULL &&
			strcmp(selector_text[ordinal], expected_selector) == 0);
		CHECK(sqlparser_selector_parse(selector_text[ordinal],
			&parsed_selector, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_selector_insert_cell_literal(handle, &parsed_selector,
			&literal, &error) == SQLPARSER_STATUS_OK);
		CHECK(literal.kind == SQLPARSER_LITERAL_KIND_STRING && literal.string_value != NULL &&
			strcmp(literal.string_value, original_values[ordinal]) == 0);
		patches[ordinal].op = SQLPARSER_PATCH_REPLACE;
		patches[ordinal].selector = selector_text[ordinal];
	}
	CHECK(sqlparser_export_view_json(handle, 0, &before_view, &error) == SQLPARSER_STATUS_OK);
	CHECK(before_view != NULL);

	/* Empty batches preserve views; every nonempty batch invalidates them. */
	list.items = NULL;
	list.count = 0U;
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(unchanged(handle, &before_graph, sql, before_view, &error));
	patches[0].sql = old_sql;
	patches[1].sql = "'before'";
	patches[2].sql = "'stable'";
	list.items = patches;
	list.count = ARRAY_LENGTH(patches);
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(identity_batch_result(handle, &before_graph, sql, before_view, &error));

	/* A late failure invalidates all views and makes the handle destroy-only. */
	patches[0].sql = new_sql;
	patches[1].sql = "'can''t'";
	patches[2].sql = "'unterminated";
	CHECK(sqlparser_apply_patch(handle, &list, &error) != SQLPARSER_STATUS_OK);
	CHECK(poisoned(handle, &before_graph, &error));
	sqlparser_handle_destroy(handle);
	handle = NULL;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_statement_query_graph(handle, 0, &before_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(unchanged(handle, &before_graph, sql, before_view, &error));
	patches[2].sql = "''";
	patches[2].selector = "stmt[0].insert_cell[999][3]";
	CHECK(sqlparser_apply_patch(handle, &list, &error) != SQLPARSER_STATUS_OK);
	CHECK(poisoned(handle, &before_graph, &error));
	sqlparser_handle_destroy(handle);
	handle = NULL;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_statement_query_graph(handle, 0, &before_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(unchanged(handle, &before_graph, sql, before_view, &error));
	patches[2].selector = selector_text[2];

	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&before_graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
	CHECK(output != NULL && strcmp(output, expected) == 0);
	CHECK(sqlparser_statement_query_graph(handle, 0, &after_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&after_graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.rows.count == 5U);
	for (ordinal = 0U; ordinal < 3U; ordinal++) {
		CHECK(sqlparser_query_graph_span_index_at(&after_graph, dml.rows,
			ordinal + 1U, &cell_index, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_query_graph_dml_cell_at(&after_graph, cell_index,
			&cell, &error) == SQLPARSER_STATUS_OK);
		CHECK(cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL &&
			cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING && cell.literal.string_value != NULL &&
			strcmp(cell.literal.string_value, changed_values[ordinal]) == 0);
		CHECK(sqlparser_selector_insert_cell_literal(handle, &selectors[ordinal],
			&literal, &error) == SQLPARSER_STATUS_OK);
		CHECK(literal.kind == SQLPARSER_LITERAL_KIND_STRING && literal.string_value != NULL &&
			strcmp(literal.string_value, changed_values[ordinal]) == 0);
	}
	CHECK(sqlparser_export_view_json(handle, 0, &after_view, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_parse_with_options(expected, &options, &reference, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_export_view_json(reference, 0, &reference_view, &error) == SQLPARSER_STATUS_OK);
	CHECK(after_view != NULL && reference_view != NULL && strcmp(after_view, reference_view) == 0);

	/* Repeat success/no-op/error paths on the mutated handle as well. */
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(identity_batch_result(handle, &after_graph, expected, after_view, &error));
	patches[0].sql = old_sql;
	patches[2].sql = "'unterminated";
	CHECK(sqlparser_apply_patch(handle, &list, &error) != SQLPARSER_STATUS_OK);
	CHECK(poisoned(handle, &after_graph, &error));

	/* Caller-owned strings must survive destruction and another handle's
	 * teardown; never dereference borrowed graph/selector results here. */
	sqlparser_handle_destroy(reference);
	reference = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;
	CHECK(strcmp(output, expected) == 0 && strcmp(after_view, reference_view) == 0);
	failed = 0;
done:
	for (ordinal = 0U; ordinal < ARRAY_LENGTH(selector_text); ordinal++)
		sqlparser_string_free(selector_text[ordinal]);
	sqlparser_string_free(before_view);
	sqlparser_string_free(after_view);
	sqlparser_string_free(reference_view);
	sqlparser_string_free(output);
	sqlparser_handle_destroy(malformed);
	sqlparser_handle_destroy(reference);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int append_sql(char *buffer, size_t capacity, size_t *used,
	const char *format, ...)
{
	va_list arguments;
	int count;

	if (*used >= capacity)
		return 0;
	va_start(arguments, format);
	count = vsnprintf(buffer + *used, capacity - *used, format, arguments);
	va_end(arguments);
	if (count < 0 || (size_t)count >= capacity - *used)
		return 0;
	*used += (size_t)count;
	return 1;
}

/* The small dialect fixtures do not reach the serialized-tree arena's size
 * threshold. This bounded bulk case exercises large trees concurrently too. */
static int run_bulk_lifecycle(const worker_t *worker)
{
	enum { ROW_COUNT = 256, COLUMN_COUNT = 3, SQL_CAPACITY = 65536 };
	const size_t iteration = ARRAY_LENGTH(dialects);
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle = NULL;
	sqlparser_query_graph_view_t before_graph;
	sqlparser_query_graph_view_t after_graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t list;
	char *sql = NULL;
	char *expected = NULL;
	char *output = NULL;
	char *selector_text[2] = {NULL, NULL};
	char first_value[80];
	char last_value[80];
	char first_sql[88];
	char last_sql[88];
	size_t sql_used = 0U;
	size_t expected_used = 0U;
	size_t row;
	size_t patch_index;
	size_t cell_index;
	int failed = 1;

	memset(&error, 0, sizeof(error));
	memset(patches, 0, sizeof(patches));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	sql = (char *)calloc(SQL_CAPACITY, 1U);
	expected = (char *)calloc(SQL_CAPACITY, 1U);
	CHECK(sql != NULL && expected != NULL);
	(void)snprintf(first_value, sizeof(first_value), "first_%zu_%zu_changed",
		worker->wave, worker->worker);
	(void)snprintf(last_value, sizeof(last_value), "last_%zu_%zu_changed",
		worker->wave, worker->worker);
	(void)snprintf(first_sql, sizeof(first_sql), "'%s'", first_value);
	(void)snprintf(last_sql, sizeof(last_sql), "'%s'", last_value);
	CHECK(append_sql(sql, SQL_CAPACITY, &sql_used,
		"/*bulk-head*/ INSERT INTO t(id,a,b) VALUES\n"));
	CHECK(append_sql(expected, SQL_CAPACITY, &expected_used,
		"/*bulk-head*/ INSERT INTO t(id,a,b) VALUES\n"));
	for (row = 0U; row < ROW_COUNT; row++) {
		char original_a[80];
		char original_b[80];
		const char *separator = row + 1U == ROW_COUNT ? "; /*bulk-tail*/" : ",\n";

		(void)snprintf(original_a, sizeof(original_a), "secret_%zu_%zu_%zu",
			worker->wave, worker->worker, row);
		(void)snprintf(original_b, sizeof(original_b), "keep_%zu_%zu_%zu",
			worker->wave, worker->worker, row);
		CHECK(append_sql(sql, SQL_CAPACITY, &sql_used, "(%zu,'%s','%s')%s",
			row, original_a, original_b, separator));
		CHECK(append_sql(expected, SQL_CAPACITY, &expected_used, "(%zu,'%s','%s')%s",
			row, row == 0U ? first_value : original_a,
			row + 1U == ROW_COUNT ? last_value : original_b, separator));
	}
	CHECK(sql_used > 4096U && expected_used > 4096U);
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_statement_query_graph(handle, 0, &before_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&before_graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.kind == SQLPARSER_GRAPH_DML_INSERT &&
		dml.target_columns.count == COLUMN_COUNT && dml.rows.count == ROW_COUNT * COLUMN_COUNT);
	for (patch_index = 0U; patch_index < ARRAY_LENGTH(patches); patch_index++) {
		const size_t ordinal = patch_index == 0U ? 1U : ROW_COUNT * COLUMN_COUNT - 1U;

		CHECK(sqlparser_query_graph_span_index_at(&before_graph, dml.rows,
			ordinal, &cell_index, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_query_graph_dml_cell_at(&before_graph, cell_index,
			&cell, &error) == SQLPARSER_STATUS_OK);
		CHECK(cell.has_selector && cell.row_index == (patch_index == 0U ? 0U : ROW_COUNT - 1U) &&
			cell.column_ordinal == (patch_index == 0U ? 1U : 2U));
		CHECK(sqlparser_selector_format(&cell.selector, &selector_text[patch_index],
			&error) == SQLPARSER_STATUS_OK);
		patches[patch_index].op = SQLPARSER_PATCH_REPLACE;
		patches[patch_index].selector = selector_text[patch_index];
	}
	list.items = patches;
	list.count = ARRAY_LENGTH(patches);
	patches[0].sql = first_sql;
	patches[1].sql = "'unterminated";
	CHECK(sqlparser_apply_patch(handle, &list, &error) != SQLPARSER_STATUS_OK);
	CHECK(poisoned(handle, &before_graph, &error));
	sqlparser_handle_destroy(handle);
	handle = NULL;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_statement_query_graph(handle, 0, &before_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&before_graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
	CHECK(output != NULL && strcmp(output, sql) == 0);
	sqlparser_string_free(output);
	output = NULL;
	patches[1].sql = last_sql;
	CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&before_graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
	CHECK(output != NULL && strcmp(output, expected) == 0);
	CHECK(sqlparser_statement_query_graph(handle, 0, &after_graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&after_graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.rows.count == ROW_COUNT * COLUMN_COUNT);
	for (patch_index = 0U; patch_index < ARRAY_LENGTH(patches); patch_index++) {
		const size_t ordinal = patch_index == 0U ? 1U : ROW_COUNT * COLUMN_COUNT - 1U;

		CHECK(sqlparser_query_graph_span_index_at(&after_graph, dml.rows,
			ordinal, &cell_index, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_query_graph_dml_cell_at(&after_graph, cell_index,
			&cell, &error) == SQLPARSER_STATUS_OK);
		CHECK(cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL &&
			cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING && cell.literal.string_value != NULL &&
			strcmp(cell.literal.string_value, patch_index == 0U ? first_value : last_value) == 0);
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;
	CHECK(strcmp(output, expected) == 0);
	failed = 0;
done:
	for (patch_index = 0U; patch_index < ARRAY_LENGTH(selector_text); patch_index++)
		sqlparser_string_free(selector_text[patch_index]);
	sqlparser_string_free(output);
	sqlparser_handle_destroy(handle);
	free(expected);
	free(sql);
	return failed;
}

static void *run_worker(void *argument)
{
	worker_t *worker = (worker_t *)argument;
	start_gate_t *gate = worker->gate;
	size_t iteration;

	require_pthread(pthread_mutex_lock(&gate->mutex), "pthread_mutex_lock");
	gate->ready++;
	require_pthread(pthread_cond_broadcast(&gate->condition), "pthread_cond_broadcast");
	while (!gate->start)
		require_pthread(pthread_cond_wait(&gate->condition, &gate->mutex), "pthread_cond_wait");
	require_pthread(pthread_mutex_unlock(&gate->mutex), "pthread_mutex_unlock");

	for (iteration = 0U; iteration < ARRAY_LENGTH(dialects); iteration++) {
		if (run_lifecycle(worker, iteration) != 0) {
			worker->failed = 1;
			break;
		}
		worker->completed++;
	}
	if (!worker->failed) {
		if (run_bulk_lifecycle(worker) != 0)
			worker->failed = 1;
		else
			worker->bulk_completed++;
	}
	return NULL;
}

int main(int argc, char **argv)
{
	size_t waves = DEFAULT_WAVES;
	size_t wave;
	size_t index;
	size_t completed = 0U;
	size_t bulk_completed = 0U;
	int failed = 0;

	if (argc == 2) {
		char *end = NULL;
		unsigned long value;

		errno = 0;
		value = strtoul(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != '\0' || value == 0UL || value > MAX_WAVES)
			failed = 1;
		else
			waves = (size_t)value;
	} else if (argc != 1) {
		failed = 1;
	}
	if (failed) {
		fprintf(stderr, "Usage: %s [waves: 1..%u]\n", argv[0], MAX_WAVES);
		return EXIT_FAILURE;
	}

	for (wave = 0U; wave < waves; wave++) {
		start_gate_t gate;
		worker_t workers[WORKER_COUNT];
		pthread_t threads[WORKER_COUNT];

		memset(&gate, 0, sizeof(gate));
		memset(workers, 0, sizeof(workers));
		require_pthread(pthread_mutex_init(&gate.mutex, NULL), "pthread_mutex_init");
		require_pthread(pthread_cond_init(&gate.condition, NULL), "pthread_cond_init");
		for (index = 0U; index < WORKER_COUNT; index++) {
			workers[index].gate = &gate;
			workers[index].wave = wave;
			workers[index].worker = index;
			require_pthread(pthread_create(&threads[index], NULL, run_worker, &workers[index]), "pthread_create");
		}
		require_pthread(pthread_mutex_lock(&gate.mutex), "pthread_mutex_lock");
		while (gate.ready != WORKER_COUNT)
			require_pthread(pthread_cond_wait(&gate.condition, &gate.mutex), "pthread_cond_wait");
		gate.start = 1;
		require_pthread(pthread_cond_broadcast(&gate.condition), "pthread_cond_broadcast");
		require_pthread(pthread_mutex_unlock(&gate.mutex), "pthread_mutex_unlock");
		for (index = 0U; index < WORKER_COUNT; index++) {
			require_pthread(pthread_join(threads[index], NULL), "pthread_join");
			failed |= workers[index].failed;
			if (workers[index].completed != ARRAY_LENGTH(dialects) || workers[index].bulk_completed != 1U)
				failed = 1;
			completed += workers[index].completed;
			bulk_completed += workers[index].bulk_completed;
		}
		require_pthread(pthread_cond_destroy(&gate.condition), "pthread_cond_destroy");
		require_pthread(pthread_mutex_destroy(&gate.mutex), "pthread_mutex_destroy");
		if (failed)
			return EXIT_FAILURE;
	}
	printf("Distinct-handle concurrency passed: %zu small lifecycles, %zu bulk MySQL lifecycles, %zu waves, "
		"%u workers/wave, %zu dialects/worker\n",
		completed, bulk_completed, waves, WORKER_COUNT, ARRAY_LENGTH(dialects));
	return EXIT_SUCCESS;
}

#endif
