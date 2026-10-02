/* Dialect-state staging is retained for isolated work, but destructive
 * mutations reuse live metadata. Inserted AST nodes still own independent data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "sqlparser_test_failure.h"

static sqlparser_error_t error;
static sqlparser_dialect_t dialect;
static const char *phase;
static int destructive;
static size_t clone_calls;
static size_t fault_index;
static sqlparser_status_t (*original_clone)(const void *, void **, sqlparser_error_t *);

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "mutation state: %s dialect=%s destructive=%d fault=%zu: %s: %s\n", \
			phase, sqlparser_dialect_name(dialect), destructive, fault_index, #condition, error.message); \
		exit(1); \
	} \
} while (0)

#ifdef SQLPARSER_MUTATION_ALLOC_WRAPPERS
static int allocation_armed;
static size_t allocation_calls, allocation_fail_at, allocation_failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);

static int reject_allocation(void)
{
	if (!allocation_armed) return 0;
	allocation_calls++;
	if (allocation_calls != allocation_fail_at) return 0;
	allocation_failures++;
	return 1;
}
void *__wrap_malloc(size_t n) { return reject_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t size) { return reject_allocation() ? NULL : __real_calloc(n, size); }
void *__wrap_realloc(void *p, size_t n) { return reject_allocation() ? NULL : __real_realloc(p, n); }

static void arm_allocations(size_t failure)
{
	if (getenv("SQLPARSER_MUTATION_FAULT_TRACE") != NULL) {
		fprintf(stderr, "mutation fault probe dialect=%s phase=%s fail_at=%zu\n",
			sqlparser_dialect_name(dialect), phase, failure);
	}
	allocation_calls = allocation_failures = 0U;
	allocation_fail_at = failure;
	allocation_armed = 1;
}
static void disarm_allocations(void) { allocation_armed = 0; }
#endif

static sqlparser_status_t count_clone(const void *state, void **out_state,
	sqlparser_error_t *out_error)
{
	clone_calls++;
	return original_clone(state, out_state, out_error);
}

static sqlparser_handle_t *parse(const char *sql)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(handle != NULL);
	return handle;
}

static const sqlparser_dialect_ops_t *observe_clones(sqlparser_handle_t *handle,
	sqlparser_dialect_ops_t *ops)
{
	const sqlparser_dialect_ops_t *original = handle->dialect_ops;
	CHECK(original != NULL && original->clone_state != NULL);
	*ops = *original;
	original_clone = original->clone_state;
	ops->clone_state = count_clone;
	handle->dialect_ops = ops;
	clone_calls = 0U;
	handle->patch_batch_flags = destructive ? SQLPARSER_PATCH_BATCH_IN_PLACE : 0U;
	return original;
}

static void verify_staging(sqlparser_handle_t *handle, void *before, size_t count)
{
	CHECK(clone_calls == (destructive ? 0U : count));
	CHECK((handle->dialect_state == before) == destructive);
}

static void verify_roundtrip(sqlparser_handle_t *handle, const char *required)
{
	sqlparser_handle_t *reference;
	char *sql = NULL;
	char *view = NULL;
	char *reference_view = NULL;
	CHECK(sqlparser_deparse(handle, &sql, &error) == SQLPARSER_STATUS_OK);
	CHECK(sql != NULL && (required == NULL || strstr(sql, required) != NULL));
	reference = parse(sql);
	CHECK(sqlparser_export_view_json(handle, 0, &view, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_export_view_json(reference, 0, &reference_view, &error) == SQLPARSER_STATUS_OK);
	CHECK(strcmp(view, reference_view) == 0);
	sqlparser_string_free(reference_view);
	sqlparser_string_free(view);
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(reference);
}

/* An ordinary function call has an empty ODBC restoration range. */
static void empty_odbc_range(void)
{
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch = {0};
	sqlparser_patch_list_t patches = {&patch, 1U};
	phase = "ordinary call with empty ODBC range";
	handle = parse("UPDATE t SET a = 1");
	patch.op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patch.selector = "stmt[0].assignment[0]";
	patch.sql = "a = CONCAT(N'new', N'key')";
	CHECK(sqlparser_apply_patch(handle, &patches, &error) == SQLPARSER_STATUS_OK);
	verify_roundtrip(handle, "N'new'");
	CHECK(sqlparser_apply_patch(handle, &patches, &error) == SQLPARSER_STATUS_OK);
	verify_roundtrip(handle, "N'key'");
	sqlparser_handle_destroy(handle);
}

static void assignment_copy(void)
{
	sqlparser_handle_t *handle;
	sqlparser_dialect_ops_t observed_ops;
	const sqlparser_dialect_ops_t *original_ops;
	sqlparser_selector_t source = {0}, insert = {0};
	const char *parts[] = {"copied"};
	sqlparser_identifier_path_view_t target = {0};
	PgQuery__UpdateStmt *update;
	PgQuery__Node *source_value, *copied_value;
	void *before;
	sqlparser_patch_t patch = {0};
	sqlparser_patch_list_t patches = {&patch, 1U};

	phase = "assignment copy ownership";
	handle = parse("UPDATE t SET a = N'clone', b = 0");
	CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
	original_ops = observe_clones(handle, &observed_ops);
	before = handle->dialect_state;
	source.kind = insert.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	insert.item_index = 1U;
	target.parts = parts;
	target.part_count = 1U;
	CHECK(sqlparser_assignment_insert_from_assignment_value_by_selector(
		handle, &insert, &target, &source, &error) == SQLPARSER_STATUS_OK);
	verify_staging(handle, before, 1U);
	handle->dialect_ops = original_ops;
	handle->patch_batch_flags = 0U;
	CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
	update = handle->ast->stmts[0]->stmt->update_stmt;
	CHECK(update != NULL && update->n_target_list == 3U);
	source_value = update->target_list[0]->res_target->val;
	copied_value = update->target_list[1]->res_target->val;
	CHECK(source_value != NULL && copied_value != NULL && source_value != copied_value);
	CHECK(source_value->a_const != NULL && copied_value->a_const != NULL);
	CHECK(source_value->a_const != copied_value->a_const);
	CHECK(source_value->a_const->sval != copied_value->a_const->sval);
	CHECK(source_value->a_const->sval->sval != copied_value->a_const->sval->sval);
	verify_roundtrip(handle, "N'clone'");
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patch.selector = "stmt[0].assignment[0]";
	CHECK(sqlparser_apply_patch(handle, &patches, &error) == SQLPARSER_STATUS_OK);
	verify_roundtrip(handle, "copied = N'clone'");
	sqlparser_handle_destroy(handle);
}

static void isolated_fragment(void)
{
	sqlparser_handle_t *handle;
	sqlparser_dialect_ops_t observed_ops;
	const sqlparser_dialect_ops_t *original_ops;
	void *candidate = NULL, *before;
	char *parser_sql = NULL, *before_sql = NULL, *after_sql = NULL;
	sqlparser_identifier_origin_map_t *origins = NULL;

	phase = "successful scratch isolation";
	handle = parse("UPDATE t SET a = N'keep', b = 0");
	CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_deparse(handle, &before_sql, &error) == SQLPARSER_STATUS_OK);
	original_ops = observe_clones(handle, &observed_ops);
	before = handle->dialect_state;
	CHECK(sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle, 0U, "N'scratch'", "isolated test", &parser_sql,
		&candidate, &origins, &error) == SQLPARSER_STATUS_OK);
	CHECK(clone_calls == 1U);
	CHECK(candidate != NULL && candidate != before && handle->dialect_state == before);
	sqlparser_handle_discard_dialect_state(handle, candidate);
	/* A failed mutation's cleanup must never destroy the aliased live state. */
	sqlparser_handle_discard_dialect_state(handle, before);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	handle->dialect_ops = original_ops;
	handle->patch_batch_flags = 0U;
	CHECK(sqlparser_deparse(handle, &after_sql, &error) == SQLPARSER_STATUS_OK);
	CHECK(strcmp(before_sql, after_sql) == 0);
	sqlparser_string_free(before_sql);
	sqlparser_string_free(after_sql);
	sqlparser_handle_destroy(handle);
}

static void result_target_delete(void)
{
	sqlparser_handle_t *handle;
	sqlparser_dialect_ops_t observed_ops;
	const sqlparser_dialect_ops_t *original_ops;
	sqlparser_selector_t selector = {0};
	void *before;
	int sqlserver = sqlparser_dialect_is_sqlserver_compatible(dialect);

	phase = "result target deletion";
	handle = parse(sqlserver ?
		"UPDATE t SET a = 1 OUTPUT INSERTED.a, INSERTED.b" :
		"UPDATE t SET a = 1 RETURNING a, b");
	CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
	original_ops = observe_clones(handle, &observed_ops);
	before = handle->dialect_state;
	selector.kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS;
	CHECK(sqlparser_dml_result_delete_target(handle, &selector, 1U, &error) == SQLPARSER_STATUS_OK);
	/* Deletion keeps the original isolated state path in both modes. */
	CHECK(clone_calls == 1U && handle->dialect_state != before);
	handle->dialect_ops = original_ops;
	handle->patch_batch_flags = 0U;
	verify_roundtrip(handle, sqlserver ? "OUTPUT INSERTED.a" : "RETURNING a");
	sqlparser_handle_destroy(handle);
}

static void result_sink_mutations(void)
{
	sqlparser_handle_t *handle;
	sqlparser_dialect_ops_t observed_ops;
	const sqlparser_dialect_ops_t *original_ops;
	sqlparser_selector_t relation = {0}, column = {0}, columns = {0};
	void *before;

	phase = "result sink mutation";
	handle = parse("UPDATE t SET a = 1 OUTPUT INSERTED.a, INSERTED.b INTO audit (a, b)");
	CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
	original_ops = observe_clones(handle, &observed_ops);
	relation.kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK;
	column.kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN;
	columns.kind = SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS;
	before = handle->dialect_state;
	CHECK(sqlparser_dml_result_set_sink_sql(handle, &relation, "audit_next", &error) == SQLPARSER_STATUS_OK);
	verify_staging(handle, before, 1U);
	before = handle->dialect_state;
	CHECK(sqlparser_dml_result_set_sink_column_sql(handle, &column, "new_a", &error) == SQLPARSER_STATUS_OK);
	verify_staging(handle, before, 2U);
	before = handle->dialect_state;
	CHECK(sqlparser_dml_result_insert_sink_column_sql(handle, &columns, 1U, "transient", &error) == SQLPARSER_STATUS_OK);
	verify_staging(handle, before, 3U);
	before = handle->dialect_state;
	CHECK(sqlparser_dml_result_delete_sink_column(handle, &columns, 1U, &error) == SQLPARSER_STATUS_OK);
	verify_staging(handle, before, 4U);
	handle->dialect_ops = original_ops;
	handle->patch_batch_flags = 0U;
	verify_roundtrip(handle, "INTO audit_next (new_a, b)");
	sqlparser_handle_destroy(handle);
}

static void failure_is_terminal(void)
{
	sqlparser_handle_t *handle;
	sqlparser_patch_t items[3] = {{0}};
	sqlparser_patch_list_t patches = {items, 3U};

	phase = "public copy then terminal failure";
	handle = parse("UPDATE t SET a = N'clone', b = 0");
	items[0].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	items[0].selector = "stmt[0].assignment[1]";
	items[0].name = "copied";
	items[0].source_selector = "stmt[0].assignment[0]";
	items[1].op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	items[1].selector = "stmt[0].assignment[999]";
	items[2].op = SQLPARSER_PATCH_REPLACE;
	items[2].selector = "stmt[0].assignment[0]";
	items[2].sql = "(";
	CHECK(sqlparser_apply_patch(handle, &patches, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(sqlparser_test_failed_handle(handle));
	sqlparser_handle_destroy(handle);
}

#ifdef SQLPARSER_MUTATION_ALLOC_WRAPPERS
enum {
	MUTATION_SWEEP_REPLACE = 0,
	MUTATION_SWEEP_COPY,
	MUTATION_SWEEP_SINK,
	MUTATION_SWEEP_TERMINAL,
	MUTATION_SWEEP_COUNT
};

static const char *sweep_bind(void)
{
	if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) return "$1";
	if (sqlparser_dialect_is_mysql_compatible(dialect)) return "?";
	return sqlparser_dialect_is_sqlserver_compatible(dialect) ? "@original" : ":original";
}

static size_t prepare_sweep(int operation, char *input, size_t capacity,
	sqlparser_patch_t items[4])
{
	int written;
	memset(items, 0, 4U * sizeof(*items));
	if (operation == MUTATION_SWEEP_SINK) {
		written = snprintf(input, capacity,
			"UPDATE t SET a = N'keep' OUTPUT INSERTED.a, INSERTED.b INTO audit (a, b)");
		CHECK(written > 0 && (size_t)written < capacity);
		items[0].op = SQLPARSER_PATCH_REPLACE;
		items[0].selector = "stmt[0].dml_result_sink[0][0]";
		items[0].sql = "audit_next";
		items[1].op = SQLPARSER_PATCH_REPLACE;
		items[1].selector = "stmt[0].dml_result_sink_column[0][0][0]";
		items[1].sql = "new_a";
		items[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
		items[2].selector = "stmt[0].dml_result_sink_columns[0][0]";
		items[2].index = 1U;
		items[2].name = "transient";
		items[3].op = SQLPARSER_PATCH_DELETE_COLUMN;
		items[3].selector = "stmt[0].dml_result_sink_columns[0][0]";
		items[3].index = 1U;
		return 4U;
	}
	written = snprintf(input, capacity,
		"UPDATE t SET a = CONCAT(N'clone', %s), b = N'old' WHERE id = 1", sweep_bind());
	CHECK(written > 0 && (size_t)written < capacity);
	if (operation == MUTATION_SWEEP_REPLACE) {
		items[0].op = items[1].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
		items[0].selector = "stmt[0].assignment[1]";
		items[0].sql = "b = CONCAT(N'changed', N'suffix')";
		items[1].selector = "stmt[0].assignment[0]";
		items[1].sql = "a = N'next'";
		return 2U;
	}
	items[0].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	items[0].selector = "stmt[0].assignment[2]";
	items[0].name = "copied";
	items[0].source_selector = "stmt[0].assignment[0]";
	items[1].op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	items[1].selector = operation == MUTATION_SWEEP_TERMINAL ?
		"stmt[0].assignment[999]" : "stmt[0].assignment[0]";
	if (operation == MUTATION_SWEEP_TERMINAL) {
		/* A valid, allocating operation that must never execute after index 1. */
		items[2].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
		items[2].selector = "stmt[0].assignment[0]";
		items[2].sql = "a = CONCAT(N'unreachable', N'sentinel')";
		return 3U;
	}
	return 2U;
}

static void public_allocation_sweeps(void)
{
	static const char *names[] = {
		"live assignment replacement allocation sweep",
		"source-copy allocation sweep",
		"live sink allocation sweep",
		"terminal public allocation sweep"
	};
	int operation;
	for (operation = 0; operation < MUTATION_SWEEP_COUNT; operation++) {
		size_t failure, total = 0U;
		char *expected = NULL;
		if (operation == MUTATION_SWEEP_SINK &&
		    !sqlparser_dialect_is_sqlserver_compatible(dialect)) continue;
		phase = names[operation];
		destructive = 1;
		for (failure = 0U; failure <= total; failure++) {
			char input[512], snapshot[512];
			sqlparser_patch_t items[4];
			sqlparser_patch_list_t patches;
			sqlparser_handle_t *handle;
			sqlparser_status_t status;
			char *output = NULL;
			fault_index = failure;
			patches.items = items;
			patches.count = prepare_sweep(operation, input, sizeof(input), items);
			strcpy(snapshot, input);
			handle = parse(input);
			arm_allocations(failure);
			status = sqlparser_apply_patch(handle, &patches, &error);
			disarm_allocations();
			if (failure == 0U) {
				CHECK(status == (operation == MUTATION_SWEEP_TERMINAL ?
					SQLPARSER_STATUS_INVALID_ARGUMENT : SQLPARSER_STATUS_OK));
				total = allocation_calls;
				CHECK(total > 0U && allocation_failures == 0U);
			} else {
				CHECK(allocation_failures == 1U);
			}
			if (operation == MUTATION_SWEEP_TERMINAL) CHECK(status != SQLPARSER_STATUS_OK);
			if (status != SQLPARSER_STATUS_OK) {
				/* Historical unpack/fragment sites can classify OOM differently.
				 * Any public failure must be terminal, with no borrowed reads. */
				CHECK(sqlparser_test_failed_handle(handle));
			} else {
				CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
				CHECK(output != NULL);
				if (failure == 0U) {
					expected = malloc(strlen(output) + 1U);
					CHECK(expected != NULL);
					strcpy(expected, output);
				} else {
					/* A handled allocation failure may succeed only equivalently. */
					CHECK(expected != NULL && strcmp(output, expected) == 0);
				}
				verify_roundtrip(handle, operation == MUTATION_SWEEP_COPY ? "copied" : NULL);
			}
			CHECK(strcmp(input, snapshot) == 0);
			sqlparser_string_free(output);
			sqlparser_handle_destroy(handle);
		}
		free(expected);
		printf("mutation allocation sweep dialect=%s operation=%d allocations=%zu\n",
			sqlparser_dialect_name(dialect), operation, total);
	}
	fault_index = 0U;
}

/* Exercise the same isolated preprocessing + expression parser combination
 * used by a successful read-only patch validator. Nothing is adopted. */
static sqlparser_status_t isolated_validation_probe(sqlparser_handle_t *handle,
	const char *fragment)
{
	sqlparser_generated_source_t source = {0};
	sqlparser_identifier_origin_map_t *origins = NULL;
	PgQuery__Node *node = NULL;
	void *candidate = NULL;
	char *parser_sql = NULL;
	sqlparser_status_t status;
	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle, 0U, fragment, "expression patch SQL", &parser_sql, &candidate, &origins, &error);
	if (status == SQLPARSER_STATUS_OK) {
		CHECK(candidate != NULL && candidate != handle->dialect_state);
		source.public_sql = fragment;
		source.origins = origins;
		source.dialect = dialect;
		source.candidate_dialect_state = candidate;
		/* Like the real scratch validator, omit spelling_handle. */
		status = sqlparser_parse_insert_cell_node_sql(parser_sql, &source, &node, &error);
	}
	sqlparser_free_proto_node(node);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	sqlparser_handle_discard_dialect_state(handle, candidate);
	return status;
}

static void isolated_validation_allocation_sweep(void)
{
	size_t failure, total = 0U;
	phase = "successful validator isolation allocation sweep";
	destructive = 1;
	for (failure = 0U; failure <= total; failure++) {
		sqlparser_handle_t *handle;
		sqlparser_dialect_ops_t observed_ops;
		const sqlparser_dialect_ops_t *original_ops;
		void *before_state;
		unsigned long generation;
		size_t spellings;
		sqlparser_status_t status;
		char *before_sql = NULL, *after_sql = NULL;
		fault_index = failure;
		handle = parse("UPDATE t SET a = N'keep', b = 0");
		CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_deparse(handle, &before_sql, &error) == SQLPARSER_STATUS_OK);
		original_ops = observe_clones(handle, &observed_ops);
		before_state = handle->dialect_state;
		generation = handle->generation;
		spellings = handle->identifier_spelling_count;
		arm_allocations(failure);
		status = isolated_validation_probe(handle, "CONCAT(N'scratch', N'probe')");
		disarm_allocations();
		if (failure == 0U) {
			CHECK(status == SQLPARSER_STATUS_OK);
			total = allocation_calls;
			CHECK(total > 0U && allocation_failures == 0U);
		} else {
			CHECK(allocation_failures == 1U);
		}
		handle->dialect_ops = original_ops;
		if (status == SQLPARSER_STATUS_OK) {
			CHECK(clone_calls == 1U && handle->dialect_state == before_state);
			CHECK(handle->generation == generation && handle->identifier_spelling_count == spellings);
			handle->patch_batch_flags = 0U;
			CHECK(sqlparser_deparse(handle, &after_sql, &error) == SQLPARSER_STATUS_OK);
			CHECK(strcmp(before_sql, after_sql) == 0);
			verify_roundtrip(handle, "N'keep'");
		}
		/* A failed internal scratch operation is unwound only by destruction;
		 * it is not a public entry and does not itself poison the handle. */
		sqlparser_string_free(before_sql);
		sqlparser_string_free(after_sql);
		sqlparser_handle_destroy(handle);
	}
	printf("mutation scratch allocation sweep dialect=%s allocations=%zu\n",
		sqlparser_dialect_name(dialect), total);
	fault_index = 0U;
}

static void first_error_stops_mutation_batch(void)
{
	size_t pass, prefix_allocations = 0U;
	sqlparser_error_t prefix_error = {0};
	phase = "first error stops metadata mutation batch";
	destructive = 1;
	fault_index = 0U;
	for (pass = 0U; pass < 2U; pass++) {
		char input[512];
		sqlparser_patch_t items[4];
		sqlparser_patch_list_t patches;
		sqlparser_handle_t *handle;
		sqlparser_status_t status;
		sqlparser_error_t first_error;
		(void)prepare_sweep(MUTATION_SWEEP_TERMINAL, input, sizeof(input), items);
		patches.items = items;
		patches.count = pass == 0U ? 2U : 3U;
		handle = parse(input);
		arm_allocations(0U);
		status = sqlparser_apply_patch(handle, &patches, &error);
		disarm_allocations();
		first_error = error;
		CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT && allocation_failures == 0U);
		CHECK(sqlparser_test_failed_handle(handle));
		sqlparser_handle_destroy(handle);
		if (pass == 0U) {
			prefix_error = first_error;
			prefix_allocations = allocation_calls;
			CHECK(prefix_allocations > 0U);
		} else {
			CHECK(allocation_calls == prefix_allocations);
			CHECK(first_error.code == prefix_error.code && first_error.cursor == prefix_error.cursor &&
				first_error.line == prefix_error.line && first_error.column == prefix_error.column &&
				strcmp(first_error.message, prefix_error.message) == 0);
		}
	}
}
#endif

int main(void)
{
	int value;
	int sweeps_only = 0;
#ifdef SQLPARSER_MUTATION_ALLOC_WRAPPERS
	sweeps_only = getenv("SQLPARSER_MUTATION_SWEEPS_ONLY") != NULL;
#endif
	for (value = SQLPARSER_DIALECT_POSTGRESQL;
	     value <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; value++) {
		dialect = (sqlparser_dialect_t)value;
		if (!sweeps_only) {
			for (destructive = 0; destructive <= 1; destructive++) {
				assignment_copy();
				isolated_fragment();
				if (sqlparser_dialect_supports_postgresql_dml_results(dialect) ||
				    sqlparser_dialect_is_sqlserver_compatible(dialect)) {
					result_target_delete();
				}
				if (sqlparser_dialect_is_sqlserver_compatible(dialect)) result_sink_mutations();
			}
			failure_is_terminal();
			empty_odbc_range();
		}
#ifdef SQLPARSER_MUTATION_ALLOC_WRAPPERS
		public_allocation_sweeps();
		isolated_validation_allocation_sweep();
		first_error_stops_mutation_batch();
#endif
	}
	puts("mutation dialect state: all 13 dialects passed");
	return 0;
}
