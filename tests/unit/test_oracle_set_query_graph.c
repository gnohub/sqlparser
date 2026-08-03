#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_SET_SQL_LEFT_DEEP = 0,
	SQLPARSER_SET_SQL_BALANCED = 1
} sqlparser_set_sql_shape_t;

typedef struct {
	const sqlparser_query_graph_view_t *graph;
	sqlparser_graph_set_t *sets;
	size_t *set_by_block;
	unsigned char *visited;
	sqlparser_graph_set_kind_t kind;
	sqlparser_set_sql_shape_t shape;
	int mixed;
	const char *label;
} sqlparser_set_graph_check_t;

typedef struct {
	const char *database_name;
	const char *schema_name;
	const char *link_name;
	const char *cte_name;
} sqlparser_cte_scope_expectation_t;

typedef int (*sqlparser_set_graph_validator_t)(
	const sqlparser_query_graph_view_t *,
	const char *,
	sqlparser_error_t *,
	const void *);

static size_t sqlparser_set_leaf_length(size_t ordinal)
{
	return (size_t)snprintf(NULL, 0, "SELECT %lu AS C FROM DUAL", (unsigned long)ordinal);
}

static size_t sqlparser_set_sql_length(
	size_t first,
	size_t last,
	const char *operator_sql,
	sqlparser_set_sql_shape_t shape)
{
	size_t index;
	size_t length;
	size_t middle;

	if (first == last) {
		return sqlparser_set_leaf_length(first);
	}
	if (shape == SQLPARSER_SET_SQL_LEFT_DEEP) {
		length = 0U;
		for (index = first; index <= last; index++) {
			length += sqlparser_set_leaf_length(index);
		}
		return length + (last - first) * (strlen(operator_sql) + 2U);
	}
	middle = first + (last - first) / 2U;
	return sqlparser_set_sql_length(first, middle, operator_sql, shape) +
		sqlparser_set_sql_length(middle + 1U, last, operator_sql, shape) +
		strlen(operator_sql) + 6U;
}

static size_t sqlparser_set_write_leaf(char *output, size_t ordinal)
{
	return (size_t)sprintf(output, "SELECT %lu AS C FROM DUAL", (unsigned long)ordinal);
}

static size_t sqlparser_set_write_sql(
	char *output,
	size_t first,
	size_t last,
	const char *operator_sql,
	sqlparser_set_sql_shape_t shape)
{
	size_t index;
	size_t length;
	size_t middle;
	size_t operator_length;

	if (first == last) {
		return sqlparser_set_write_leaf(output, first);
	}
	operator_length = strlen(operator_sql);
	length = 0U;
	if (shape == SQLPARSER_SET_SQL_LEFT_DEEP) {
		for (index = first; index <= last; index++) {
			if (index != first) {
				output[length++] = ' ';
				memcpy(output + length, operator_sql, operator_length);
				length += operator_length;
				output[length++] = ' ';
			}
			length += sqlparser_set_write_leaf(output + length, index);
		}
		return length;
	}
	middle = first + (last - first) / 2U;
	output[length++] = '(';
	length += sqlparser_set_write_sql(
		output + length,
		first,
		middle,
		operator_sql,
		shape);
	output[length++] = ')';
	output[length++] = ' ';
	memcpy(output + length, operator_sql, operator_length);
	length += operator_length;
	output[length++] = ' ';
	output[length++] = '(';
	length += sqlparser_set_write_sql(
		output + length,
		middle + 1U,
		last,
		operator_sql,
		shape);
	output[length++] = ')';
	return length;
}

static char *sqlparser_set_make_sql(
	size_t branch_count,
	const char *operator_sql,
	sqlparser_set_sql_shape_t shape)
{
	char *sql;
	size_t length;
	size_t written;

	length = sqlparser_set_sql_length(1U, branch_count, operator_sql, shape);
	sql = (char *)malloc(length + 1U);
	if (sql == NULL) {
		return NULL;
	}
	written = sqlparser_set_write_sql(sql, 1U, branch_count, operator_sql, shape);
	if (written != length) {
		free(sql);
		return NULL;
	}
	sql[length] = '\0';
	return sql;
}

static int sqlparser_set_graph_fail(
	const sqlparser_set_graph_check_t *check,
	const char *detail)
{
	fprintf(stderr, "FAIL [%s]: %s\n", check->label, detail);
	return 1;
}

static int sqlparser_set_graph_walk(
	sqlparser_set_graph_check_t *check,
	size_t block_index,
	size_t first,
	size_t last,
	sqlparser_error_t *error)
{
	sqlparser_graph_block_t block;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_set_kind_t expected_kind;
	sqlparser_graph_set_t *set_item;
	sqlparser_graph_target_t target;
	sqlparser_graph_value_t value;
	size_t left_block;
	size_t middle;
	size_t relation_index;
	size_t right_block;
	size_t set_index;
	size_t target_index;

	if (block_index >= check->graph->block_count || check->visited[block_index]) {
		return sqlparser_set_graph_fail(check, "block is out of range or visited twice");
	}
	check->visited[block_index] = 1U;
	if (sqlparser_query_graph_block_at(check->graph, block_index, &block, error) != SQLPARSER_STATUS_OK) {
		return sqlparser_set_graph_fail(check, error->message);
	}
	if (first == last) {
		if (block.kind != SQLPARSER_GRAPH_BLOCK_SELECT ||
		    block.relations.count != 1U || block.targets.count != 1U ||
		    block.predicates.count != 0U) {
			return sqlparser_set_graph_fail(check, "leaf block shape is invalid");
		}
		if (sqlparser_query_graph_span_index_at(check->graph, block.relations, 0U, &relation_index, error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_relation_at(check->graph, relation_index, &relation, error) != SQLPARSER_STATUS_OK ||
		    relation.block_index != block_index || relation.kind != SQLPARSER_GRAPH_REL_DUAL ||
		    relation.object_name == NULL || strcmp(relation.object_name, "DUAL") != 0 ||
		    !relation.has_selector) {
			return sqlparser_set_graph_fail(check, "leaf DUAL relation is invalid");
		}
		if (sqlparser_query_graph_span_index_at(check->graph, block.targets, 0U, &target_index, error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_target_at(check->graph, target_index, &target, error) != SQLPARSER_STATUS_OK ||
		    target.block_index != block_index || target.kind != SQLPARSER_GRAPH_TARGET_LITERAL ||
		    target.output_name == NULL || strcmp(target.output_name, "C") != 0 ||
		    !target.has_value || !target.has_selector || !target.has_target_list_selector ||
		    sqlparser_query_graph_value_at(check->graph, target.value_index, &value, error) != SQLPARSER_STATUS_OK ||
		    value.block_index != block_index || value.kind != SQLPARSER_GRAPH_VALUE_LITERAL ||
		    !value.has_selector ||
		    value.literal.kind != SQLPARSER_LITERAL_KIND_INTEGER ||
		    value.literal.integer_value != (long long)first) {
			return sqlparser_set_graph_fail(check, "leaf literal value is invalid");
		}
		return 0;
	}
	if (block.kind != SQLPARSER_GRAPH_BLOCK_SET) {
		return sqlparser_set_graph_fail(check, "non-leaf block is not a set");
	}
	set_index = check->set_by_block[block_index];
	if (set_index == SIZE_MAX) {
		return sqlparser_set_graph_fail(check, "set block has no set record");
	}
	set_item = &check->sets[set_index];
	expected_kind = check->mixed && first == 1U && last == 4U ?
		SQLPARSER_GRAPH_SET_UNION : check->kind;
	if (set_item->kind != expected_kind || set_item->branch_blocks.count != 2U) {
		return sqlparser_set_graph_fail(check, "set operator or branch count is invalid");
	}
	if (sqlparser_query_graph_span_index_at(check->graph, set_item->branch_blocks, 0U, &left_block, error) != SQLPARSER_STATUS_OK ||
	    sqlparser_query_graph_span_index_at(check->graph, set_item->branch_blocks, 1U, &right_block, error) != SQLPARSER_STATUS_OK) {
		return sqlparser_set_graph_fail(check, error->message);
	}
	if (check->mixed) {
		middle = first + (last - first) / 2U;
	} else if (check->shape == SQLPARSER_SET_SQL_LEFT_DEEP) {
		middle = last - 1U;
	} else {
		middle = first + (last - first) / 2U;
	}
	if (sqlparser_set_graph_walk(check, left_block, first, middle, error) != 0 ||
	    sqlparser_set_graph_walk(check, right_block, middle + 1U, last, error) != 0) {
		return 1;
	}
	return 0;
}

static int sqlparser_set_validate_graph(
	const sqlparser_query_graph_view_t *graph,
	size_t branch_count,
	sqlparser_graph_set_kind_t kind,
	sqlparser_set_sql_shape_t shape,
	int mixed,
	const char *label,
	sqlparser_error_t *error)
{
	sqlparser_set_graph_check_t check;
	sqlparser_graph_set_t set_item;
	size_t block_index;
	size_t index;
	int failed;

	memset(&check, 0, sizeof(check));
	check.graph = graph;
	check.kind = kind;
	check.shape = shape;
	check.mixed = mixed;
	check.label = label;
	failed = 0;
	if (!graph->has_root_block || graph->block_count != branch_count * 2U - 1U ||
	    graph->set_count != branch_count - 1U || graph->relation_count != branch_count ||
	    graph->target_count != branch_count || graph->value_count != branch_count ||
	    graph->field_count != 0U || graph->predicate_count != 0U || graph->has_dml) {
		return sqlparser_set_graph_fail(&check, "query graph counts are invalid");
	}
	check.sets = (sqlparser_graph_set_t *)calloc(graph->set_count, sizeof(*check.sets));
	check.set_by_block = (size_t *)malloc(graph->block_count * sizeof(*check.set_by_block));
	check.visited = (unsigned char *)calloc(graph->block_count, sizeof(*check.visited));
	if (check.sets == NULL || check.set_by_block == NULL || check.visited == NULL) {
		failed = sqlparser_set_graph_fail(&check, "out of memory");
		goto done;
	}
	for (index = 0U; index < graph->block_count; index++) {
		check.set_by_block[index] = SIZE_MAX;
	}
	for (index = 0U; index < graph->set_count; index++) {
		if (sqlparser_query_graph_set_at(graph, index, &set_item, error) != SQLPARSER_STATUS_OK ||
		    set_item.result_block_index >= graph->block_count ||
		    check.set_by_block[set_item.result_block_index] != SIZE_MAX) {
			failed = sqlparser_set_graph_fail(&check, "set record is invalid or duplicated");
			goto done;
		}
		check.sets[index] = set_item;
		check.set_by_block[set_item.result_block_index] = index;
	}
	failed = sqlparser_set_graph_walk(
		&check,
		graph->root_block_index,
		1U,
		branch_count,
		error);
	if (failed != 0) {
		goto done;
	}
	for (block_index = 0U; block_index < graph->block_count; block_index++) {
		if (!check.visited[block_index]) {
			failed = sqlparser_set_graph_fail(&check, "query graph contains an unreachable block");
			goto done;
		}
	}

done:
	free(check.sets);
	free(check.set_by_block);
	free(check.visited);
	return failed;
}

static int sqlparser_set_run_case(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t branch_count,
	sqlparser_graph_set_kind_t kind,
	sqlparser_set_sql_shape_t shape,
	int mixed,
	const char *label)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	char *deparsed_sql;
	char *view_json;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	deparsed_sql = NULL;
	view_json = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL || sqlparser_statement_count(handle) != 1U) {
		fprintf(stderr, "FAIL [%s]: parse failed: %s\n", label, error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparsed_sql == NULL || strcmp(deparsed_sql, sql) != 0) {
		fprintf(stderr, "FAIL [%s]: exact deparse failed: %s\n", label,
			deparsed_sql != NULL ? deparsed_sql : error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK || view_json == NULL) {
		fprintf(stderr, "FAIL [%s]: View export failed: %s\n", label, error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [%s]: query graph failed: %s\n", label, error.message);
		failed = 1;
		goto done;
	}
	failed = sqlparser_set_validate_graph(
		&graph,
		branch_count,
		kind,
		shape,
		mixed,
		label,
		&error);

done:
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int sqlparser_set_expect_relation(
	const sqlparser_query_graph_view_t *graph,
	size_t relation_index,
	size_t block_index,
	sqlparser_graph_relation_kind_t kind,
	const char *object_name,
	const char *alias_name,
	size_t source_block_index,
	int has_source_block,
	const char *label,
	sqlparser_error_t *error)
{
	sqlparser_graph_relation_t relation;

	if (sqlparser_query_graph_relation_at(graph, relation_index, &relation, error) != SQLPARSER_STATUS_OK ||
	    relation.block_index != block_index || relation.kind != kind ||
	    relation.has_source_block != has_source_block ||
	    (has_source_block && relation.source_block_index != source_block_index) ||
	    ((object_name == NULL) != (relation.object_name == NULL)) ||
	    (object_name != NULL && strcmp(relation.object_name, object_name) != 0) ||
	    ((alias_name == NULL) != (relation.alias_name == NULL)) ||
	    (alias_name != NULL && strcmp(relation.alias_name, alias_name) != 0)) {
		fprintf(stderr, "FAIL [%s]: relation %lu is invalid\n", label, (unsigned long)relation_index);
		return 1;
	}
	return 0;
}

static int sqlparser_set_expect_field(
	const sqlparser_query_graph_view_t *graph,
	size_t field_index,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	size_t relation_index,
	int has_relation,
	const char *label,
	sqlparser_error_t *error)
{
	sqlparser_graph_field_t field;

	if (sqlparser_query_graph_field_at(graph, field_index, &field, error) != SQLPARSER_STATUS_OK ||
	    field.block_index != block_index || field.clause != clause ||
	    field.has_relation != has_relation ||
	    (has_relation && field.relation_index != relation_index)) {
		fprintf(stderr, "FAIL [%s]: field %lu is invalid\n", label, (unsigned long)field_index);
		return 1;
	}
	return 0;
}

static int sqlparser_set_expect_single_set(
	const sqlparser_query_graph_view_t *graph,
	size_t result_block,
	size_t left_block,
	size_t right_block,
	const char *label,
	sqlparser_error_t *error)
{
	sqlparser_graph_set_t set_item;
	size_t actual_left;
	size_t actual_right;

	if (graph->set_count != 1U ||
	    sqlparser_query_graph_set_at(graph, 0U, &set_item, error) != SQLPARSER_STATUS_OK ||
	    set_item.kind != SQLPARSER_GRAPH_SET_UNION_ALL ||
	    set_item.result_block_index != result_block || set_item.branch_blocks.count != 2U ||
	    sqlparser_query_graph_span_index_at(graph, set_item.branch_blocks, 0U, &actual_left, error) != SQLPARSER_STATUS_OK ||
	    sqlparser_query_graph_span_index_at(graph, set_item.branch_blocks, 1U, &actual_right, error) != SQLPARSER_STATUS_OK ||
	    actual_left != left_block || actual_right != right_block) {
		fprintf(stderr, "FAIL [%s]: set topology is invalid\n", label);
		return 1;
	}
	return 0;
}

static int sqlparser_set_validate_order_scope(
	const sqlparser_query_graph_view_t *graph,
	const char *label,
	sqlparser_error_t *error,
	const void *context)
{
	(void)context;
	if (!graph->has_root_block || graph->root_block_index != 0U ||
	    graph->block_count != 4U || graph->relation_count != 3U ||
	    graph->field_count != 4U ||
	    sqlparser_set_expect_single_set(graph, 0U, 1U, 3U, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 0U, 1U, SQLPARSER_GRAPH_REL_CTE, "src", NULL, 2U, 1, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 1U, 2U, SQLPARSER_GRAPH_REL_BASE, "orders", NULL, 0U, 0, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 2U, 3U, SQLPARSER_GRAPH_REL_CTE, "src", NULL, 2U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 0U, 2U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 1U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 1U, 1U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 0U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 2U, 3U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 2U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 3U, 0U, SQLPARSER_CLAUSE_KIND_ORDER_BY, 0U, 0, label, error) != 0) {
		return 1;
	}
	return 0;
}

static int sqlparser_set_validate_branch_fetch_scope(
	const sqlparser_query_graph_view_t *graph,
	const char *label,
	sqlparser_error_t *error,
	const void *context)
{
	(void)context;
	if (!graph->has_root_block || graph->root_block_index != 0U ||
	    graph->block_count != 5U || graph->relation_count != 4U ||
	    graph->field_count != 6U ||
	    sqlparser_set_expect_single_set(graph, 0U, 1U, 3U, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 0U, 1U, SQLPARSER_GRAPH_REL_DERIVED, NULL, "v", 2U, 1, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 1U, 2U, SQLPARSER_GRAPH_REL_BASE, "orders", NULL, 0U, 0, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 2U, 3U, SQLPARSER_GRAPH_REL_DERIVED, NULL, "v", 4U, 1, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 3U, 4U, SQLPARSER_GRAPH_REL_BASE, "archived_orders", NULL, 0U, 0, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 0U, 2U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 1U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 1U, 2U, SQLPARSER_CLAUSE_KIND_ORDER_BY, 1U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 2U, 1U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 0U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 3U, 4U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 3U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 4U, 4U, SQLPARSER_CLAUSE_KIND_ORDER_BY, 3U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 5U, 3U, SQLPARSER_CLAUSE_KIND_SELECT_LIST, 2U, 1, label, error) != 0) {
		return 1;
	}
	return 0;
}

static int sqlparser_set_optional_string_equal(
	const char *actual,
	const char *expected)
{
	return (actual == NULL && expected == NULL) ||
		(actual != NULL && expected != NULL && strcmp(actual, expected) == 0);
}

static int sqlparser_set_validate_namespace_scope(
	const sqlparser_query_graph_view_t *graph,
	const char *label,
	sqlparser_error_t *error,
	const void *context)
{
	const sqlparser_cte_scope_expectation_t *expected;
	sqlparser_graph_relation_t relation;

	expected = (const sqlparser_cte_scope_expectation_t *)context;
	if (expected == NULL || expected->cte_name == NULL ||
	    !graph->has_root_block || graph->root_block_index != 0U ||
	    graph->block_count != 4U || graph->relation_count != 3U ||
	    graph->target_count != 3U || graph->field_count != 3U ||
	    graph->value_count != 0U ||
	    sqlparser_set_expect_single_set(graph, 0U, 1U, 3U, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 0U, 1U, SQLPARSER_GRAPH_REL_CTE,
		    expected->cte_name, NULL, 2U, 1, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 1U, 2U, SQLPARSER_GRAPH_REL_BASE,
		    "seed", NULL, 0U, 0, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 2U, 3U, SQLPARSER_GRAPH_REL_BASE,
		    expected->cte_name, "s", 0U, 0, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 0U, 2U, SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    1U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 1U, 1U, SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    0U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 2U, 3U, SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    2U, 1, label, error) != 0 ||
	    sqlparser_query_graph_relation_at(graph, 2U, &relation, error) != SQLPARSER_STATUS_OK ||
	    !sqlparser_set_optional_string_equal(relation.database_name, expected->database_name) ||
	    !sqlparser_set_optional_string_equal(relation.schema_name, expected->schema_name) ||
	    !sqlparser_set_optional_string_equal(relation.link_name, expected->link_name)) {
		fprintf(stderr, "FAIL [%s]: qualified relation scope is invalid\n", label);
		return 1;
	}
	return 0;
}

static int sqlparser_set_validate_quoted_cte_scope(
	const sqlparser_query_graph_view_t *graph,
	const char *label,
	sqlparser_error_t *error,
	const void *context)
{
	const sqlparser_cte_scope_expectation_t *expected;

	expected = (const sqlparser_cte_scope_expectation_t *)context;
	if (expected == NULL || expected->cte_name == NULL ||
	    !graph->has_root_block || graph->root_block_index != 0U ||
	    graph->block_count != 2U || graph->relation_count != 2U ||
	    graph->target_count != 2U || graph->field_count != 2U ||
	    graph->value_count != 0U || graph->set_count != 0U ||
	    sqlparser_set_expect_relation(graph, 0U, 0U, SQLPARSER_GRAPH_REL_CTE,
		    expected->cte_name, "q", 1U, 1, label, error) != 0 ||
	    sqlparser_set_expect_relation(graph, 1U, 1U, SQLPARSER_GRAPH_REL_BASE,
		    "seed", NULL, 0U, 0, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 0U, 1U, SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    1U, 1, label, error) != 0 ||
	    sqlparser_set_expect_field(graph, 1U, 0U, SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		    0U, 1, label, error) != 0) {
		fprintf(stderr, "FAIL [%s]: quoted CTE scope is invalid\n", label);
		return 1;
	}
	return 0;
}

static int sqlparser_set_run_semantic_case(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *name,
	sqlparser_set_graph_validator_t validate,
	const void *context)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	char label[128];
	char *deparsed_sql;
	char *view_json;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	deparsed_sql = NULL;
	view_json = NULL;
	failed = 0;
	(void)snprintf(label, sizeof(label), "%s %s", sqlparser_dialect_name(dialect), name);
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL || sqlparser_statement_count(handle) != 1U) {
		fprintf(stderr, "FAIL [%s]: parse failed: %s\n", label, error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparsed_sql == NULL || strcmp(deparsed_sql, sql) != 0) {
		fprintf(stderr, "FAIL [%s]: exact deparse failed: %s\n", label,
			deparsed_sql != NULL ? deparsed_sql : error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK || view_json == NULL) {
		fprintf(stderr, "FAIL [%s]: View export failed: %s\n", label, error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [%s]: query graph failed: %s\n", label, error.message);
		failed = 1;
		goto done;
	}
	failed = validate(&graph, label, &error, context);

done:
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return failed;
}

int main(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_ORACLE,
		SQLPARSER_DIALECT_VASTBASE_ORACLE
	};
	static const size_t branch_counts[] = {31U, 32U, 33U, 65U, 129U};
	static const struct {
		const char *sql;
		sqlparser_graph_set_kind_t kind;
	} operators[] = {
		{"UNION", SQLPARSER_GRAPH_SET_UNION},
		{"UNION ALL", SQLPARSER_GRAPH_SET_UNION_ALL}
	};
	static const char mixed_sql[] =
		"(SELECT 1 AS C FROM DUAL UNION ALL SELECT 2 AS C FROM DUAL) UNION "
		"(SELECT 3 AS C FROM DUAL UNION ALL SELECT 4 AS C FROM DUAL)";
	static const char order_scope_sql[] =
		"WITH src AS (SELECT id FROM orders) SELECT id FROM src UNION ALL "
		"SELECT id FROM src ORDER BY id";
	static const char branch_fetch_scope_sql[] =
		"SELECT v.id FROM (SELECT id FROM orders ORDER BY id FETCH FIRST 1 ROW ONLY) v "
		"UNION ALL SELECT v.id FROM (SELECT id FROM archived_orders ORDER BY id DESC "
		"FETCH FIRST 2 ROWS ONLY) v";
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		sqlparser_cte_scope_expectation_t expected;
	} namespace_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM public.src s",
		 {NULL, "public", NULL, "src"}},
		{SQLPARSER_DIALECT_MYSQL,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM app.src s",
		 {NULL, "app", NULL, "src"}},
		{SQLPARSER_DIALECT_ORACLE,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM app.src s",
		 {NULL, "app", NULL, "src"}},
		{SQLPARSER_DIALECT_SQLSERVER,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM dbo.src s",
		 {NULL, "dbo", NULL, "src"}},
		{SQLPARSER_DIALECT_SQLSERVER,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM Db.dbo.src s",
		 {"Db", "dbo", NULL, "src"}},
		{SQLPARSER_DIALECT_DAMENG,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM app.src s",
		 {NULL, "app", NULL, "src"}},
		{SQLPARSER_DIALECT_VASTBASE_ORACLE,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM app.src s",
		 {NULL, "app", NULL, "src"}},
		{SQLPARSER_DIALECT_VASTBASE_MYSQL,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM app.src s",
		 {NULL, "app", NULL, "src"}},
		{SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM public.src s",
		 {NULL, "public", NULL, "src"}},
		{SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM dbo.src s",
		 {NULL, "dbo", NULL, "src"}},
		{SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM Db.dbo.src s",
		 {"Db", "dbo", NULL, "src"}},
		{SQLPARSER_DIALECT_ORACLE,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM src@remote_db s",
		 {NULL, NULL, "remote_db", "src"}},
		{SQLPARSER_DIALECT_DAMENG,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM src@remote_db s",
		 {NULL, NULL, "remote_db", "src"}},
		{SQLPARSER_DIALECT_VASTBASE_ORACLE,
		 "WITH src AS (SELECT id FROM seed) SELECT src.id FROM src UNION ALL "
		 "SELECT s.id FROM src@remote_db s",
		 {NULL, NULL, "remote_db", "src"}}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		sqlparser_cte_scope_expectation_t expected;
	} quoted_cte_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "WITH \"app.src\" AS (SELECT id FROM seed) SELECT q.id FROM \"app.src\" q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_MYSQL,
		 "WITH `app.src` AS (SELECT id FROM seed) SELECT q.id FROM `app.src` q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_ORACLE,
		 "WITH \"app.src\" AS (SELECT id FROM seed) SELECT q.id FROM \"app.src\" q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_SQLSERVER,
		 "WITH [dbo.src] AS (SELECT id FROM seed) SELECT q.id FROM [dbo.src] q",
		 {NULL, NULL, NULL, "dbo.src"}},
		{SQLPARSER_DIALECT_DAMENG,
		 "WITH \"app.src\" AS (SELECT id FROM seed) SELECT q.id FROM \"app.src\" q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_VASTBASE_ORACLE,
		 "WITH \"app.src\" AS (SELECT id FROM seed) SELECT q.id FROM \"app.src\" q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_VASTBASE_MYSQL,
		 "WITH `app.src` AS (SELECT id FROM seed) SELECT q.id FROM `app.src` q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		 "WITH \"app.src\" AS (SELECT id FROM seed) SELECT q.id FROM \"app.src\" q",
		 {NULL, NULL, NULL, "app.src"}},
		{SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
		 "WITH [dbo.src] AS (SELECT id FROM seed) SELECT q.id FROM [dbo.src] q",
		 {NULL, NULL, NULL, "dbo.src"}}
	};
	char label[128];
	char *sql;
	size_t count_index;
	size_t dialect_index;
	size_t namespace_index;
	size_t operator_index;
	size_t quoted_index;
	size_t shape_index;
	int failed;

	failed = 0;
	for (dialect_index = 0U; dialect_index < sizeof(dialects) / sizeof(dialects[0]); dialect_index++) {
		for (operator_index = 0U; operator_index < sizeof(operators) / sizeof(operators[0]); operator_index++) {
			for (shape_index = 0U; shape_index < 2U; shape_index++) {
				for (count_index = 0U; count_index < sizeof(branch_counts) / sizeof(branch_counts[0]); count_index++) {
					(void)snprintf(
						label,
						sizeof(label),
						"%s %s %s %lu",
						sqlparser_dialect_name(dialects[dialect_index]),
						operators[operator_index].sql,
						shape_index == SQLPARSER_SET_SQL_LEFT_DEEP ? "left" : "balanced",
						(unsigned long)branch_counts[count_index]);
					sql = sqlparser_set_make_sql(
						branch_counts[count_index],
						operators[operator_index].sql,
						(sqlparser_set_sql_shape_t)shape_index);
					if (sql == NULL) {
						fprintf(stderr, "FAIL [%s]: SQL generation failed\n", label);
						failed = 1;
						continue;
					}
					failed |= sqlparser_set_run_case(
						dialects[dialect_index],
						sql,
						branch_counts[count_index],
						operators[operator_index].kind,
						(sqlparser_set_sql_shape_t)shape_index,
						0,
						label);
					free(sql);
				}
			}
		}
		(void)snprintf(
			label,
			sizeof(label),
			"%s mixed explicit grouping",
			sqlparser_dialect_name(dialects[dialect_index]));
		failed |= sqlparser_set_run_case(
			dialects[dialect_index],
			mixed_sql,
			4U,
			SQLPARSER_GRAPH_SET_UNION_ALL,
			SQLPARSER_SET_SQL_BALANCED,
			1,
			label);
		failed |= sqlparser_set_run_semantic_case(
			dialects[dialect_index],
			order_scope_sql,
			"set ORDER BY scope",
			sqlparser_set_validate_order_scope,
			NULL);
		failed |= sqlparser_set_run_semantic_case(
			dialects[dialect_index],
			branch_fetch_scope_sql,
			"branch ORDER FETCH scope",
			sqlparser_set_validate_branch_fetch_scope,
			NULL);
	}
	for (namespace_index = 0U;
	     namespace_index < sizeof(namespace_cases) / sizeof(namespace_cases[0]);
	     namespace_index++) {
		failed |= sqlparser_set_run_semantic_case(
			namespace_cases[namespace_index].dialect,
			namespace_cases[namespace_index].sql,
			namespace_cases[namespace_index].expected.link_name != NULL ?
				"database-link name bypasses CTE" :
				(namespace_cases[namespace_index].expected.database_name != NULL ?
					"catalog-qualified name bypasses CTE" :
					"schema-qualified name bypasses CTE"),
			sqlparser_set_validate_namespace_scope,
			&namespace_cases[namespace_index].expected);
	}
	for (quoted_index = 0U;
	     quoted_index < sizeof(quoted_cte_cases) / sizeof(quoted_cte_cases[0]);
	     quoted_index++) {
		failed |= sqlparser_set_run_semantic_case(
			quoted_cte_cases[quoted_index].dialect,
			quoted_cte_cases[quoted_index].sql,
			"quoted dotted CTE name",
			sqlparser_set_validate_quoted_cte_scope,
			&quoted_cte_cases[quoted_index].expected);
	}
	if (failed != 0) {
		return 1;
	}
	printf("oracle set query graph tests passed\n");
	return 0;
}
