#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_control_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"

typedef struct {
	size_t index;
	size_t statement_index;
	size_t dml_index;
	size_t ordinal;
	sqlparser_graph_dml_branch_kind_t branch_kind;
	size_t target_relation_index;
	int has_target_relation;
	sqlparser_index_span_t target_columns;
	sqlparser_index_span_t rows;
	size_t condition_block_index;
	int has_condition_block;
	sqlparser_selector_t condition_selector;
	int has_condition_selector;
} sqlparser_graph_dml_branch_abi_baseline_t;

_Static_assert(
	sizeof(sqlparser_graph_dml_branch_t) ==
		sizeof(sqlparser_graph_dml_branch_abi_baseline_t),
	"sqlparser_graph_dml_branch_t ABI size changed");
#define SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(field) \
	_Static_assert( \
		offsetof(sqlparser_graph_dml_branch_t, field) == \
			offsetof(sqlparser_graph_dml_branch_abi_baseline_t, field), \
		"sqlparser_graph_dml_branch_t ABI offset changed: " #field)
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(index);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(statement_index);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(dml_index);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(ordinal);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(branch_kind);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(target_relation_index);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(has_target_relation);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(target_columns);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(rows);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(condition_block_index);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(has_condition_block);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(condition_selector);
SQLPARSER_ASSERT_BRANCH_ABI_OFFSET(has_condition_selector);
#undef SQLPARSER_ASSERT_BRANCH_ABI_OFFSET

typedef struct {
	sqlparser_graph_merge_action_kind_t action_kind;
	sqlparser_graph_merge_match_kind_t match_kind;
	sqlparser_index_span_t assignments;
} sqlparser_test_merge_branch_detail_t;

static int json_string_is(
	json_t *object,
	const char *key,
	const char *expected);
static int json_integer_is(
	json_t *object,
	const char *key,
	json_int_t expected);

static int expect_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}

	return 0;
}

static int expect_status_ok(sqlparser_status_t status, const sqlparser_error_t *error, const char *message)
{
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s: %s\n", message, error != NULL ? error->message : "unknown");
		return 1;
	}

	return 0;
}

static int expect_merge_branch_detail(
	const sqlparser_query_graph_view_t *graph,
	size_t branch_index,
	sqlparser_test_merge_branch_detail_t *out_detail,
	sqlparser_error_t *error,
	const char *message)
{
	if (out_detail == NULL) {
		return expect_true(0, message);
	}
	memset(out_detail, 0, sizeof(*out_detail));
	return expect_status_ok(
		sqlparser_query_graph_merge_branch_detail(
			graph,
			branch_index,
			&out_detail->action_kind,
			&out_detail->match_kind,
			&out_detail->assignments,
			error),
		error,
		message);
}

static int expect_deparse_reparse_ok(const sqlparser_handle_t *handle, const char *message)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t error;
	char *sql;
	int rc;

	if (handle == NULL) {
		fprintf(stderr, "FAIL: %s: handle is NULL\n", message);
		return 1;
	}

	sql = NULL;
	reparsed = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		return 1;
	}

	sqlparser_parse_options_default(&options);
	options.dialect = sqlparser_handle_dialect(handle);
	rc = sqlparser_parse_with_options(sql, &options, &reparsed, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		sqlparser_string_free(sql);
		return 1;
	}

	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(sql);
	return 0;
}

static int expect_deparse_equals_and_reparse(
	const sqlparser_handle_t *handle,
	const char *expected_sql,
	const char *message)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t error;
	char *sql;
	int rc;

	if (handle == NULL || expected_sql == NULL) {
		fprintf(stderr, "FAIL: %s: invalid expected deparse input\n", message);
		return 1;
	}

	sql = NULL;
	reparsed = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		return 1;
	}
	if (sql == NULL || strcmp(sql, expected_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: %s: expected=%s actual=%s\n",
			message,
			expected_sql,
			sql != NULL ? sql : "(null)");
		sqlparser_string_free(sql);
		return 1;
	}

	sqlparser_parse_options_default(&options);
	options.dialect = sqlparser_handle_dialect(handle);
	rc = sqlparser_parse_with_options(sql, &options, &reparsed, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		sqlparser_string_free(sql);
		return 1;
	}

	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(sql);
	return 0;
}

static int expect_query_graph_clause_fields(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_clause_kind_t clause,
	const char *const *expected_names,
	size_t expected_count,
	const char *message)
{
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_field_t field;
	sqlparser_error_t error;
	unsigned int found;
	unsigned int expected;
	size_t field_index;
	size_t name_index;
	int rc;

	if (expected_count > sizeof(found) * 8U) {
		fprintf(stderr, "FAIL: %s: too many expected fields\n", message);
		return 1;
	}

	memset(&error, 0, sizeof(error));
	rc = sqlparser_statement_query_graph(handle, statement_index, &graph, &error);
	if (expect_status_ok(rc, &error, message) != 0) {
		return 1;
	}
	found = 0U;
	for (field_index = 0U; field_index < graph.field_count; field_index++) {
		rc = sqlparser_query_graph_field_at(&graph, field_index, &field, &error);
		if (expect_status_ok(rc, &error, message) != 0) {
			return 1;
		}
		if (field.clause != clause || field.column_name == NULL) {
			continue;
		}
		for (name_index = 0U; name_index < expected_count; name_index++) {
			if (strcmp(field.column_name, expected_names[name_index]) == 0) {
				found |= 1U << name_index;
			}
		}
	}
	expected = expected_count == 0U ? 0U : (1U << expected_count) - 1U;
	if (found != expected) {
		fprintf(
			stderr,
			"FAIL: %s: expected field mask=%u actual=%u\n",
			message,
			expected,
			found);
		return 1;
	}
	return 0;
}

static const char *test_control_sql(void)
{
	return "SELECT 1 FROM __sqlparser_source__ WHERE a = 1; "
		"SELECT id FROM t; UPDATE t SET x = 2 WHERE id = 1";
}

static void test_control_state_fill(
	sqlparser_control_state_t *state,
	const char *sql)
{
	const char *condition;
	const char *then_statement;
	const char *else_statement;

	condition = strstr(sql, "a = 1");
	then_statement = strstr(sql, "SELECT id FROM t");
	else_statement = strstr(sql, "UPDATE t SET x = 2 WHERE id = 1");
	state->roots.offset = 0U;
	state->roots.count = 1U;
	state->nodes[0].kind = SQLPARSER_CONTROL_NODE_IF;
	state->nodes[0].branches.offset = 1U;
	state->nodes[0].branches.count = 2U;
	state->branches[0].condition_statement_index = 0U;
	state->branches[0].has_condition = 1;
	state->branches[0].items.offset = 3U;
	state->branches[0].items.count = 1U;
	state->branches[1].items.offset = 4U;
	state->branches[1].items.count = 1U;
	state->items[0].kind = SQLPARSER_CONTROL_ITEM_NODE;
	state->items[0].index = 0U;
	state->items[1].kind = SQLPARSER_CONTROL_ITEM_STATEMENT;
	state->items[1].index = 1U;
	state->items[2].kind = SQLPARSER_CONTROL_ITEM_STATEMENT;
	state->items[2].index = 2U;
	state->index_pool[0] = 0U;
	state->index_pool[1] = 0U;
	state->index_pool[2] = 1U;
	state->index_pool[3] = 1U;
	state->index_pool[4] = 2U;
	state->units[0].kind = SQLPARSER_CONTROL_UNIT_CONDITION;
	state->units[0].ast_statement_index = 0U;
	state->units[0].source_offset = (size_t)(condition - sql);
	state->units[0].source_length = strlen("a = 1");
	state->units[1].kind = SQLPARSER_CONTROL_UNIT_STATEMENT;
	state->units[1].ast_statement_index = 1U;
	state->units[1].source_offset = (size_t)(then_statement - sql);
	state->units[1].source_length = strlen("SELECT id FROM t");
	state->units[2].kind = SQLPARSER_CONTROL_UNIT_STATEMENT;
	state->units[2].ast_statement_index = 2U;
	state->units[2].source_offset = (size_t)(else_statement - sql);
	state->units[2].source_length = strlen("UPDATE t SET x = 2 WHERE id = 1");
}

static int test_control_handle_new(sqlparser_handle_t **out_handle)
{
	sqlparser_control_counts_t counts;
	sqlparser_control_state_t *state;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;

	if (out_handle == NULL) {
		return 1;
	}
	*out_handle = NULL;
	handle = NULL;
	state = NULL;
	memset(&error, 0, sizeof(error));
	status = sqlparser_parse(test_control_sql(), &handle, &error);
	if (expect_status_ok(status, &error, "control test SQL should parse") != 0) {
		return 1;
	}
	memset(&counts, 0, sizeof(counts));
	counts.root_count = 1U;
	counts.node_count = 1U;
	counts.branch_count = 2U;
	counts.item_count = 3U;
	counts.index_count = 5U;
	counts.unit_count = 3U;
	status = sqlparser_control_state_allocate(&counts, &handle->limits, &state, &error);
	if (expect_status_ok(status, &error, "control state allocation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	test_control_state_fill(state, test_control_sql());
	status = sqlparser_control_state_attach(handle, state, &error);
	if (expect_status_ok(status, &error, "control state attach should succeed") != 0) {
		sqlparser_control_state_release(state);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	*out_handle = handle;
	return 0;
}

static int test_control_identifier_source_window(void)
{
	static const char sql[] =
		"SELECT 1 WHERE true; "
		"CREATE SERVER SameServer FOREIGN DATA WRAPPER \"WrapperName\"; "
		"CREATE SERVER SameServer FOREIGN DATA WRAPPER WrapperName";
	sqlparser_control_counts_t counts;
	sqlparser_control_state_t *state;
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	const char *condition;
	const char *first_statement;
	const char *second_statement;
	const char *second_output;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;
	int rc;

	handle = NULL;
	state = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "control identifier window SQL should parse") != 0) {
		return 1;
	}
	memset(&counts, 0, sizeof(counts));
	counts.root_count = 1U;
	counts.node_count = 1U;
	counts.branch_count = 2U;
	counts.item_count = 3U;
	counts.index_count = 5U;
	counts.unit_count = 3U;
	rc = sqlparser_control_state_allocate(
		&counts,
		&handle->limits,
		&state,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "control identifier window state should allocate") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	condition = strstr(sql, "true");
	first_statement = strstr(sql, "CREATE SERVER");
	second_statement =
		first_statement != NULL ?
			strstr(first_statement + 1, "CREATE SERVER") :
			NULL;
	if (condition == NULL || first_statement == NULL ||
	    second_statement == NULL) {
		fprintf(
			stderr,
			"FAIL: control identifier window source layout is invalid\n");
		goto fail;
	}
	state->roots.offset = 0U;
	state->roots.count = 1U;
	state->nodes[0].kind = SQLPARSER_CONTROL_NODE_IF;
	state->nodes[0].branches.offset = 1U;
	state->nodes[0].branches.count = 2U;
	state->branches[0].condition_statement_index = 0U;
	state->branches[0].has_condition = 1;
	state->branches[0].items.offset = 3U;
	state->branches[0].items.count = 1U;
	state->branches[1].items.offset = 4U;
	state->branches[1].items.count = 1U;
	state->items[0].kind = SQLPARSER_CONTROL_ITEM_NODE;
	state->items[0].index = 0U;
	state->items[1].kind = SQLPARSER_CONTROL_ITEM_STATEMENT;
	state->items[1].index = 1U;
	state->items[2].kind = SQLPARSER_CONTROL_ITEM_STATEMENT;
	state->items[2].index = 2U;
	state->index_pool[0] = 0U;
	state->index_pool[1] = 0U;
	state->index_pool[2] = 1U;
	state->index_pool[3] = 1U;
	state->index_pool[4] = 2U;
	state->units[0].kind = SQLPARSER_CONTROL_UNIT_CONDITION;
	state->units[0].ast_statement_index = 0U;
	state->units[0].source_offset = (size_t)(condition - sql);
	state->units[0].source_length = strlen("true");
	state->units[1].kind = SQLPARSER_CONTROL_UNIT_STATEMENT;
	state->units[1].ast_statement_index = 1U;
	state->units[1].source_offset =
		(size_t)(first_statement - sql);
	state->units[1].source_length =
		(size_t)(second_statement - first_statement - 2);
	state->units[2].kind = SQLPARSER_CONTROL_UNIT_STATEMENT;
	state->units[2].ast_statement_index = 2U;
	state->units[2].source_offset =
		(size_t)(second_statement - sql);
	state->units[2].source_length = strlen(second_statement);
	rc = sqlparser_control_state_attach(handle, state, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "control identifier window state should attach") != 0) {
		sqlparser_control_state_release(state);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	state = NULL;

	rc = sqlparser_statement_name_count(
		handle,
		2U,
		&count,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "control identifier window names should be available") != 0) {
		goto fail;
	}
	for (index = 0U; index < count; index++) {
		memset(&name, 0, sizeof(name));
		if (sqlparser_statement_name(
			    handle,
			    2U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(
			    name.owner_type,
			    "CreateForeignServerStmt") == 0 &&
		    strcmp(name.field_name, "servername") == 0 &&
		    strcmp(name.value, "SameServer") == 0) {
			target_index = index;
			break;
		}
	}
	rc =
		target_index != (size_t)-1 ?
			sqlparser_statement_set_name(
				handle,
				2U,
				target_index,
				"changed_server",
				&error) :
			SQLPARSER_STATUS_INTERNAL_ERROR;
	if (expect_status_ok(
		    rc,
		    &error,
		    "control identifier window mutation should succeed") != 0) {
		goto fail;
	}
	rc = sqlparser_deparse(handle, &deparsed, &error);
	second_output =
		deparsed != NULL ?
			strstr(deparsed, "CREATE SERVER changed_server") :
			NULL;
	if (expect_status_ok(
		    rc,
		    &error,
		    "control identifier window should deparse") != 0 ||
	    expect_true(
		    second_output != NULL &&
			    strstr(
				    second_output,
				    "FOREIGN DATA WRAPPER WrapperName") !=
				    NULL &&
			    strstr(second_output, "\"WrapperName\"") ==
				    NULL,
		    "a control unit must not borrow identifier spelling from an earlier unit") != 0) {
		goto fail;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 0;

fail:
	sqlparser_string_free(deparsed);
	sqlparser_control_state_release(state);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_nested_control_depth(size_t depth, sqlparser_status_t expected_status)
{
	static const char condition_statement[] =
		"SELECT 1 FROM __sqlparser_source__ WHERE a = 1; ";
	static const char condition_prefix[] =
		"SELECT 1 FROM __sqlparser_source__ WHERE ";
	static const char condition_sql[] = "a = 1";
	static const char leaf_sql[] = "SELECT 1";
	sqlparser_parse_options_t options;
	sqlparser_control_counts_t counts;
	sqlparser_control_state_t *state;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *sql;
	size_t sql_length;
	size_t index;
	int result;

	if (depth == 0U || depth > 64U) {
		return 1;
	}
	sql_length = depth * (sizeof(condition_statement) - 1U) +
		(sizeof(leaf_sql) - 1U);
	sql = (char *)malloc(sql_length + 1U);
	if (sql == NULL) {
		return 1;
	}
	for (index = 0U; index < depth; index++) {
		memcpy(
			sql + index * (sizeof(condition_statement) - 1U),
			condition_statement,
			sizeof(condition_statement) - 1U);
	}
	memcpy(
		sql + depth * (sizeof(condition_statement) - 1U),
		leaf_sql,
		sizeof(leaf_sql));

	handle = NULL;
	state = NULL;
	result = 1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(status, &error, "nested control SQL should parse") != 0) {
		goto done;
	}
	memset(&counts, 0, sizeof(counts));
	counts.root_count = 1U;
	counts.node_count = depth;
	counts.branch_count = depth;
	counts.item_count = depth + 1U;
	counts.index_count = depth * 2U + 1U;
	counts.unit_count = depth + 1U;
	status = sqlparser_control_state_allocate(&counts, &handle->limits, &state, &error);
	if (expect_status_ok(status, &error, "nested control state should allocate") != 0) {
		goto done;
	}
	state->roots.offset = 0U;
	state->roots.count = 1U;
	state->index_pool[0] = 0U;
	state->items[0].kind = SQLPARSER_CONTROL_ITEM_NODE;
	state->items[0].index = 0U;
	for (index = 0U; index < depth; index++) {
		state->nodes[index].kind = SQLPARSER_CONTROL_NODE_IF;
		state->nodes[index].branches.offset = 1U + index;
		state->nodes[index].branches.count = 1U;
		state->branches[index].condition_statement_index = index;
		state->branches[index].has_condition = 1;
		state->branches[index].items.offset = 1U + depth + index;
		state->branches[index].items.count = 1U;
		state->items[index + 1U].kind = index + 1U < depth ?
			SQLPARSER_CONTROL_ITEM_NODE : SQLPARSER_CONTROL_ITEM_STATEMENT;
		state->items[index + 1U].index = index + 1U < depth ? index + 1U : depth;
		state->index_pool[1U + index] = index;
		state->index_pool[1U + depth + index] = index + 1U;
		state->units[index].kind = SQLPARSER_CONTROL_UNIT_CONDITION;
		state->units[index].ast_statement_index = index;
		state->units[index].source_offset =
			index * (sizeof(condition_statement) - 1U) +
			(sizeof(condition_prefix) - 1U);
		state->units[index].source_length = sizeof(condition_sql) - 1U;
	}
	state->units[depth].kind = SQLPARSER_CONTROL_UNIT_STATEMENT;
	state->units[depth].ast_statement_index = depth;
	state->units[depth].source_offset = depth * (sizeof(condition_statement) - 1U);
	state->units[depth].source_length = sizeof(leaf_sql) - 1U;

	status = sqlparser_control_state_attach(handle, state, &error);
	if (status != expected_status) {
		fprintf(
			stderr,
			"FAIL: nested control depth %lu returned %d instead of %d: %s\n",
			(unsigned long)depth,
			(int)status,
			(int)expected_status,
			error.message);
		goto done;
	}
	if (status == SQLPARSER_STATUS_OK) {
		state = NULL;
	}
	result = 0;

done:
	sqlparser_control_state_release(state);
	sqlparser_handle_destroy(handle);
	free(sql);
	return result;
}

static int find_name_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *owner_type,
	const char *field_name,
	const char *value,
	size_t *out_index)
{
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	size_t count;
	size_t index;
	int rc;

	if (out_index == NULL) {
		fprintf(stderr, "FAIL: out_index must not be NULL\n");
		return 1;
	}

	*out_index = 0U;
	memset(&error, 0, sizeof(error));
	memset(&name, 0, sizeof(name));

	rc = sqlparser_statement_name_count(handle, statement_index, &count, &error);
	if (expect_status_ok(rc, &error, "statement name count should succeed") != 0) {
		return 1;
	}

	for (index = 0U; index < count; index++) {
		rc = sqlparser_statement_name(handle, statement_index, index, &name, &error);
		if (expect_status_ok(rc, &error, "statement name fetch should succeed") != 0) {
			return 1;
		}

		if ((owner_type == NULL || (name.owner_type != NULL && strcmp(name.owner_type, owner_type) == 0)) &&
		    (field_name == NULL || (name.field_name != NULL && strcmp(name.field_name, field_name) == 0)) &&
		    (value == NULL || (name.value != NULL && strcmp(name.value, value) == 0))) {
			*out_index = index;
			return 0;
		}
	}

	fprintf(stderr,
	        "FAIL: statement name not found: owner=%s field=%s value=%s\n",
	        owner_type != NULL ? owner_type : "(any)",
	        field_name != NULL ? field_name : "(any)",
	        value != NULL ? value : "(any)");
	return 1;
}

static int test_statement_kind_walk(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_statement_kind_t kind;
	const char *node_name;
	int rc;

	sql = "SELECT 1; INSERT INTO t (id) VALUES (1); UPDATE t SET id = 2; DELETE FROM t WHERE id = 2; DROP VIEW v_t; BEGIN";
	handle = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "multi statement parse should succeed") != 0) {
		return 1;
	}

	if (expect_true(sqlparser_statement_count(handle) == 6U, "statement count should be 6") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 0U, &kind, &error);
	if (expect_status_ok(rc, &error, "statement 0 kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_SELECT, "statement 0 should be SELECT") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_true(strcmp(sqlparser_bind_kind_name(SQLPARSER_BIND_KIND_POSITIONAL), "positional") == 0,
	                "bind kind name should be positional") != 0 ||
	    expect_true(strcmp(sqlparser_bind_kind_name(SQLPARSER_BIND_KIND_NAMED), "named") == 0,
	                "bind kind name should be named") != 0 ||
	    expect_true(strcmp(sqlparser_clause_kind_name(SQLPARSER_CLAUSE_KIND_GROUP_BY), "group_by") == 0,
	                "clause kind name should be group_by") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_true(strcmp(sqlparser_dialect_name(SQLPARSER_DIALECT_VASTBASE_ORACLE), "vastbase-oracle") == 0,
	                "vastbase oracle dialect name should match") != 0 ||
	    expect_true(strcmp(sqlparser_dialect_name(SQLPARSER_DIALECT_VASTBASE_MYSQL), "vastbase-mysql") == 0,
	                "vastbase mysql dialect name should match") != 0 ||
	    expect_true(strcmp(sqlparser_dialect_name(SQLPARSER_DIALECT_VASTBASE_POSTGRESQL), "vastbase-postgresql") == 0,
	                "vastbase postgresql dialect name should match") != 0 ||
	    expect_true(strcmp(sqlparser_dialect_name(SQLPARSER_DIALECT_VASTBASE_SQLSERVER), "vastbase-sqlserver") == 0,
	                "vastbase sqlserver dialect name should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_node_name(handle, 1U, &node_name, &error);
	if (expect_status_ok(rc, &error, "statement 1 node name should succeed") != 0 ||
	    expect_true(strcmp(node_name, "InsertStmt") == 0, "statement 1 should be InsertStmt") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 4U, &kind, &error);
	if (expect_status_ok(rc, &error, "statement 4 kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_DDL, "statement 4 should be DDL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 5U, &kind, &error);
	if (expect_status_ok(rc, &error, "statement 5 kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_TRANSACTION, "statement 5 should be transaction") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_values_literal_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_relation_view_t relation;
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	const char *column_name;
	size_t column_count;
	size_t row_count;
	int rc;

	sql = "INSERT INTO public.users (id, name, age) VALUES (1, 'bob', 18), (2, 'alice', 19)";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&relation, 0, sizeof(relation));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "insert parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_target_relation(handle, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "insert relation should succeed") != 0 ||
	    expect_true(strcmp(relation.schema_name, "public") == 0, "insert schema should be public") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "insert table should be users") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "insert source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_VALUES, "insert source should be VALUES") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_column_count(handle, 0U, &column_count, &error);
	if (expect_status_ok(rc, &error, "insert column count should succeed") != 0 ||
	    expect_true(column_count == 3U, "insert column count should be 3") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_column_name(handle, 0U, 1U, &column_name, &error);
	if (expect_status_ok(rc, &error, "insert column name should succeed") != 0 ||
	    expect_true(strcmp(column_name, "name") == 0, "column 1 should be name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "insert row count should succeed") != 0 ||
	    expect_true(row_count == 2U, "insert row count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_literal(handle, 0U, 0U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "insert cell literal should succeed") != 0 ||
	    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "insert cell kind should be string") != 0 ||
	    expect_true(strcmp(literal.string_value, "bob") == 0, "insert cell value should be bob") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "carol";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 1U, 1U, &replacement, &error);
	if (expect_status_ok(rc, &error, "insert cell mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "insert deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "carol") != NULL, "deparsed insert should contain carol") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_select_inspect(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_literal_view_t literal;
	sqlparser_where_literal_view_t where_literal;
	size_t row_count;
	size_t where_count;
	int rc;

	sql = "INSERT INTO archive_users (id, name) SELECT id, name FROM users WHERE active = true";
	handle = NULL;
	memset(&error, 0, sizeof(error));
	memset(&literal, 0, sizeof(literal));
	memset(&where_literal, 0, sizeof(where_literal));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "insert-select parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "insert-select source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_QUERY, "insert-select source should be query") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "insert-select row count should succeed") != 0 ||
	    expect_true(row_count == 0U, "insert-select row count should be 0") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "insert-select where literal count should succeed") != 0 ||
	    expect_true(where_count == 1U, "insert-select where literal count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "insert-select where literal should succeed") != 0 ||
	    expect_true(where_literal.literal.kind == SQLPARSER_LITERAL_KIND_BOOLEAN, "insert-select where literal should be boolean") != 0 ||
	    expect_true(where_literal.literal.boolean_value == 1, "insert-select where literal should be true") != 0 ||
	    expect_true(strcmp(where_literal.column_name, "active") == 0, "insert-select where column should be active") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_literal(handle, 0U, 0U, 0U, &literal, &error);
	if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED, "insert-select should reject values cell access") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_assignment_literal_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t replacement;
	sqlparser_literal_value_t where_replacement;
	sqlparser_relation_view_t relation;
	char *deparsed_sql;
	char *view_json;
	size_t assignment_count;
	size_t where_count;
	int rc;

	sql = "UPDATE public.users SET name = 'bob', age = 18 WHERE id = 1 AND status = 'active'";
	handle = NULL;
	deparsed_sql = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&replacement, 0, sizeof(replacement));
	memset(&where_replacement, 0, sizeof(where_replacement));
	memset(&relation, 0, sizeof(relation));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "update parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_target_relation(handle, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "update relation should succeed") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "update table should be users") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "update assignment count should succeed") != 0 ||
	    expect_true(assignment_count == 2U, "update assignment count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "update where literal count should succeed") != 0 ||
	    expect_true(where_count == 2U, "update where literal count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "update where literal should succeed") != 0 ||
	    expect_true(where_literal.literal.kind == SQLPARSER_LITERAL_KIND_INTEGER, "update where literal should be integer") != 0 ||
	    expect_true(where_literal.literal.integer_value == 1LL, "update where literal should be 1") != 0 ||
	    expect_true(strcmp(where_literal.column_name, "id") == 0, "update where column should be id") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "update assignment should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "name") == 0, "first assignment should be name") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_LITERAL, "first assignment should be literal") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "bob") == 0, "first assignment should be bob") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "carol";
	rc = sqlparser_update_set_assignment_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "update assignment mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	where_replacement.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	where_replacement.integer_value = 2LL;
	rc = sqlparser_statement_where_set_literal(handle, 0U, 0U, &where_replacement, &error);
	if (expect_status_ok(rc, &error, "update where mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = 'carol'") != NULL, "deparsed update should contain carol") != 0 ||
	    expect_true(strstr(deparsed_sql, "id = 2") != NULL, "deparsed update should contain id = 2") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "update view export should succeed") != 0 ||
	    expect_true(strstr(view_json, "carol") != NULL, "view JSON should contain carol") != 0 ||
	    expect_true(strstr(view_json, "\"operator\":\"=\"") != NULL, "view JSON should contain where operator") != 0 ||
	    expect_true(strstr(view_json, "\"integer_value\":2") != NULL, "view JSON should contain updated where value") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_assignment_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	sqlparser_selector_t selector;
	char *assignment_sql;
	char *selector_sql;
	char *deparsed_sql;
	int rc;

	sql = "UPDATE public.users SET name = upper(name), updated_at = CuRrEnT_TiMeStAmP WHERE id = 1";
	handle = NULL;
	assignment_sql = NULL;
	selector_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "expression update parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "expression assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "name") == 0, "expression assignment column should be name") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_EXPRESSION, "expression assignment should be expression") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment_sql(handle, 0U, 0U, &assignment_sql, &error);
	if (expect_status_ok(rc, &error, "expression assignment SQL fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment_sql, "upper(name)") == 0, "expression assignment SQL should be upper(name)") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 1U, &assignment, &error);
	if (expect_status_ok(rc, &error, "SQL value assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "updated_at") == 0, "SQL value assignment column should be updated_at") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_EXPRESSION, "SQL value assignment should be an expression") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = 0U;
	selector.item_index = 1U;
	rc = sqlparser_selector_update_assignment_sql(handle, &selector, &selector_sql, &error);
	if (expect_status_ok(rc, &error, "selector assignment SQL fetch should succeed") != 0 ||
	    expect_true(strcmp(selector_sql, "CuRrEnT_TiMeStAmP") == 0, "selector assignment SQL should retain source case") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_sql(handle, 0U, 0U, "lower(name)", &error);
	if (expect_status_ok(rc, &error, "expression assignment SQL mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(selector_sql);
	selector_sql = NULL;
	rc = sqlparser_selector_update_assignment_sql(handle, &selector, &selector_sql, &error);
	if (expect_status_ok(rc, &error, "unmodified SQL value assignment should remain readable") != 0 ||
	    expect_true(strcmp(selector_sql, "CuRrEnT_TiMeStAmP") == 0, "unmodified SQL value assignment should retain source case after another patch") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_selector_set_update_assignment_sql(
		handle,
		&selector,
		"cUrReNt_tImEsTaMp",
		&error);
	if (expect_status_ok(rc, &error, "selector assignment SQL mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(selector_sql);
	selector_sql = NULL;
	rc = sqlparser_selector_update_assignment_sql(handle, &selector, &selector_sql, &error);
	if (expect_status_ok(rc, &error, "patched SQL value assignment should be readable") != 0 ||
	    expect_true(strcmp(selector_sql, "cUrReNt_tImEsTaMp") == 0, "patched SQL value assignment should retain patch case") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "expression update deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "name = lower(name)") != NULL, "deparsed update should contain lower(name)") != 0 ||
	    expect_true(strstr(deparsed_sql, "updated_at = cUrReNt_tImEsTaMp") != NULL, "deparsed update should retain patched SQL value case") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_sql);
	sqlparser_string_free(assignment_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_assignment_list_patch_api(void)
{
	sqlparser_handle_t *handle;
	sqlparser_handle_t *guard_handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	char *deparsed_sql;
	size_t assignment_count;
	int rc;

	handle = NULL;
	guard_handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));

	rc = sqlparser_parse(
		"UPDATE public.users SET secret = 'qz$...', status = 'old' WHERE id = 1",
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "update assignment-list parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_update_insert_assignment_sql(handle, 0U, 1U, "secret_orig = 'abc'", &error);
	if (expect_status_ok(rc, &error, "update assignment insert should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "update assignment count after insert should succeed") != 0 ||
	    expect_true(assignment_count == 3U, "update assignment insert should add one item") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 1U, &assignment, &error);
	if (expect_status_ok(rc, &error, "inserted update assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "secret_orig") == 0, "inserted assignment column should match") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_LITERAL, "inserted assignment should be literal") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "abc") == 0, "inserted assignment value should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_full_sql(handle, 0U, 2U, "status_text = 'active'", &error);
	if (expect_status_ok(rc, &error, "update assignment full replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 2U, &assignment, &error);
	if (expect_status_ok(rc, &error, "replaced update assignment fetch should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "status_text") == 0, "replaced assignment column should match") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "active") == 0, "replaced assignment value should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_delete_assignment(handle, 0U, 2U, &error);
	if (expect_status_ok(rc, &error, "update assignment delete should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "update assignment count after delete should succeed") != 0 ||
	    expect_true(assignment_count == 2U, "update assignment delete should remove one item") != 0 ||
	    expect_deparse_reparse_ok(handle, "update assignment-list mutation should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update assignment-list deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "secret_orig = 'abc'") != NULL, "deparse should contain inserted assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "status_text") == NULL, "deparse should not contain deleted assignment") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("UPDATE t SET only_col = 1 WHERE id = 1", &guard_handle, &error);
	if (expect_status_ok(rc, &error, "single assignment update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_update_delete_assignment(guard_handle, 0U, 0U, &error);
	if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED, "delete last update assignment should be rejected") != 0) {
		sqlparser_handle_destroy(guard_handle);
		return 1;
	}
	sqlparser_handle_destroy(guard_handle);
	return 0;
}

static int test_update_assignment_list_apply_patch(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_patch_t patches[3];
	sqlparser_patch_list_t patch_list;
	sqlparser_assignment_view_t assignment;
	char *deparsed_sql;
	size_t assignment_count;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(patches, 0, sizeof(patches));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"UPDATE SERVERS SET IP = :ip, STATUS = :status WHERE ID = :id",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "oracle update assignment-list parse should succeed") != 0) {
		return 1;
	}

	patches[0].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patches[0].selector = "stmt[0].assignment[1]";
	patches[0].sql = "HOST = :host";
	patches[1].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patches[1].selector = "stmt[0].assignment[2]";
	patches[1].sql = "PORT = :port";
	patches[2].op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patches[2].selector = "stmt[0].assignment[0]";
	patch_list.items = patches;
	patch_list.count = 3U;

	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "update assignment-list patch should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "update assignment-list patch should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
	if (expect_status_ok(rc, &error, "patched update assignment count should succeed") != 0 ||
	    expect_true(assignment_count == 2U, "patched update should contain two assignments") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "patched first update assignment should be readable") != 0 ||
	    expect_true(assignment.column_name != NULL, "patched first assignment column should be present") != 0 ||
	    expect_true(strcmp(assignment.column_name, "HOST") == 0 || strcmp(assignment.column_name, "host") == 0,
	                "patched first assignment column should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "patched oracle update deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "HOST = :host") != NULL || strstr(deparsed_sql, "host = :host") != NULL,
	                "patched update should contain HOST bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "PORT = :port") != NULL || strstr(deparsed_sql, "port = :port") != NULL,
	                "patched update should contain PORT bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "IP = :ip") == NULL && strstr(deparsed_sql, "ip = :ip") == NULL,
	                "patched update should remove IP assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "patched update should not expose parser binds") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_structured_update_assignment_from_assignment_value(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
		const char *sql;
		const char *insert_selector_text;
		const char *source_selector_text;
		const char *target_part0;
		const char *target_part1;
		const char *expect_column;
		const char *expect_value_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-literal",
			"UPDATE public.users SET phone = '13800000000' WHERE id = 1",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"'13800000000'"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-subquery",
			"UPDATE public.users SET phone = (SELECT phone FROM backup WHERE id = 1) WHERE id = 2",
			"stmt[0].assignment[1]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"SELECT phone"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"mysql-question",
			"UPDATE users SET name = ?, phone = ?, email = ? WHERE id = ?",
			"stmt[0].assignment[1]",
			"stmt[0].assignment[1]",
			"phone_backup",
			NULL,
			"phone_backup",
			"?"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"mysql-update-join-question",
			"UPDATE users u JOIN profiles p ON u.id = p.user_id SET u.phone = ? WHERE p.id = ?",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"?"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"oracle-named-bind",
			"UPDATE users SET phone = :phone WHERE id = :id",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			":phone"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"sqlserver-named-bind",
			"UPDATE dbo.users SET phone = @phone WHERE id = @id",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"@phone"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"dameng-question",
			"UPDATE users SET phone = ? WHERE id = ?",
			"stmt[0].assignment[0]",
			"stmt[0].assignment[0]",
			"phone_backup",
			NULL,
			"phone_backup",
			"?"
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t insert_selector;
	sqlparser_selector_t source_selector;
	sqlparser_identifier_path_view_t target;
	sqlparser_assignment_view_t assignment;
	char *assignment_sql;
	char *deparsed_sql;
	size_t assignment_count;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const char *target_parts[2];
		size_t target_part_count;

		handle = NULL;
		assignment_sql = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		memset(&insert_selector, 0, sizeof(insert_selector));
		memset(&source_selector, 0, sizeof(source_selector));
		memset(&target, 0, sizeof(target));
		memset(&assignment, 0, sizeof(assignment));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "structured update parse should succeed") != 0) {
			return 1;
		}

		rc = sqlparser_selector_parse(cases[index].insert_selector_text, &insert_selector, &error);
		if (expect_status_ok(rc, &error, "structured update insert selector should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_selector_parse(cases[index].source_selector_text, &source_selector, &error);
		if (expect_status_ok(rc, &error, "structured update source selector should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		target_parts[0] = cases[index].target_part0;
		target_parts[1] = cases[index].target_part1;
		target_part_count = cases[index].target_part1 != NULL ? 2U : 1U;
		target.parts = target_parts;
		target.part_count = target_part_count;
		rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
			handle,
			&insert_selector,
			&target,
			&source_selector,
			&error);
		if (expect_status_ok(rc, &error, "structured update assignment insert should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "structured update assignment insert should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_update_assignment_count(handle, 0U, &assignment_count, &error);
		if (expect_status_ok(rc, &error, "structured update assignment count should succeed") != 0 ||
		    expect_true(assignment_count >= 2U, "structured update should insert an assignment") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment(handle, 0U, insert_selector.item_index, &assignment, &error);
		if (expect_status_ok(rc, &error, "structured inserted assignment should be readable") != 0 ||
		    expect_true(assignment.column_name != NULL &&
		                strcmp(assignment.column_name, cases[index].expect_column) == 0,
		                "structured inserted assignment column mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment_sql(handle, 0U, insert_selector.item_index, &assignment_sql, &error);
		if (expect_status_ok(rc, &error, "structured inserted assignment SQL should be readable") != 0 ||
		    expect_true(assignment_sql != NULL &&
		                strstr(assignment_sql, cases[index].expect_value_sql) != NULL,
		                "structured inserted assignment value should match source RHS") != 0) {
			sqlparser_string_free(assignment_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(assignment_sql);
		assignment_sql = NULL;

		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "structured update deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].expect_column) != NULL,
		                "structured update deparse should contain target column") != 0 ||
		    expect_true(strstr(deparsed_sql, "$") == NULL,
		                "structured update deparse should not expose internal bind markers") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_update_assignment_bind_rhs_literal_rewrite(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *backup_column;
		const char *encrypted_literal;
		const char *expect_backup_fragment;
		const char *expect_literal_fragment;
		const char *expect_where_fragment;
		size_t source_assignment_index;
		size_t protected_assignment_index_after_insert;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"UPDATE public.users SET secret = $1 WHERE id = $2",
			"secret_orig",
			"pg-encrypted",
			"secret_orig = $1",
			"secret = 'pg-encrypted'",
			"id = $2",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"UPDATE users SET secret = ? WHERE id = ?",
			"secret_orig",
			"mysql-encrypted",
			"secret_orig = ?",
			"secret = 'mysql-encrypted'",
			"id = ?",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = :1 WHERE ID = :2",
			"SECRET_ORIG",
			"oracle-pos-encrypted",
			"\"SECRET_ORIG\" = :1",
			"SECRET = 'oracle-pos-encrypted'",
			"ID = :2",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = ? WHERE ID = ?",
			"SECRET_ORIG",
			"oracle-question-encrypted",
			"\"SECRET_ORIG\" = ?",
			"SECRET = 'oracle-question-encrypted'",
			"ID = ?",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = :secret, NOTE = :note WHERE ID = :id",
			"SECRET_ORIG",
			"oracle-named-encrypted",
			"\"SECRET_ORIG\" = :secret",
			"SECRET = 'oracle-named-encrypted'",
			"ID = :id",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"UPDATE KDES.DBP_CRYPTO_TEST SET STATUS = 'ACTIVE', SECRET = :1 WHERE ID = :2",
			"SECRET_ORIG",
			"oracle-mixed-encrypted",
			"\"SECRET_ORIG\" = :1",
			"SECRET = 'oracle-mixed-encrypted'",
			"ID = :2",
			1U,
			2U
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"UPDATE \"KDES\".\"DBP_CRYPTO_TEST\" SET \"SECRET\" = :1 WHERE \"ID\" = :2",
			"SECRET_ORIG",
			"oracle-quoted-encrypted",
			"\"SECRET_ORIG\" = :1",
			"\"SECRET\" = 'oracle-quoted-encrypted'",
			"\"ID\" = :2",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"UPDATE dbo.users SET secret = @secret WHERE id = @id",
			"secret_orig",
			"sqlserver-encrypted",
			"secret_orig = @secret",
			"secret = 'sqlserver-encrypted'",
			"id = @id",
			0U,
			1U
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = :1 WHERE ID = :2",
			"SECRET_ORIG",
			"dameng-encrypted",
			"\"SECRET_ORIG\" = :1",
			"SECRET = 'dameng-encrypted'",
			"ID = :2",
			0U,
			1U
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const char *target_parts[1];
		sqlparser_handle_t *handle;
		sqlparser_selector_t insert_selector;
		sqlparser_selector_t source_selector;
		sqlparser_selector_t protected_selector;
		sqlparser_identifier_path_view_t target;
		sqlparser_literal_value_t encrypted;
		char *deparsed_sql;

		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		memset(&insert_selector, 0, sizeof(insert_selector));
		memset(&source_selector, 0, sizeof(source_selector));
		memset(&protected_selector, 0, sizeof(protected_selector));
		memset(&target, 0, sizeof(target));
		memset(&encrypted, 0, sizeof(encrypted));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;

		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "bind RHS rewrite parse should succeed") != 0) {
			return 1;
		}

		insert_selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		insert_selector.statement_index = 0U;
		insert_selector.item_index = cases[index].source_assignment_index;
		source_selector = insert_selector;
		target_parts[0] = cases[index].backup_column;
		target.parts = target_parts;
		target.part_count = 1U;
		rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
			handle,
			&insert_selector,
			&target,
			&source_selector,
			&error);
		if (expect_status_ok(rc, &error, "bind RHS backup assignment insert should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		protected_selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		protected_selector.statement_index = 0U;
		protected_selector.item_index = cases[index].protected_assignment_index_after_insert;
		encrypted.kind = SQLPARSER_LITERAL_KIND_STRING;
		encrypted.string_value = cases[index].encrypted_literal;
		rc = sqlparser_selector_set_update_assignment_literal(handle, &protected_selector, &encrypted, &error);
		if (expect_status_ok(rc, &error, "bind RHS literal replacement should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "bind RHS rewrite should produce parseable SQL") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "bind RHS deparse should succeed") != 0 ||
		    expect_true(deparsed_sql != NULL, "bind RHS deparse should return SQL") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].expect_backup_fragment) != NULL,
		                "bind RHS deparse should contain backup assignment") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].expect_literal_fragment) != NULL,
		                "bind RHS deparse should contain encrypted literal assignment") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].expect_where_fragment) != NULL,
		                "bind RHS deparse should preserve WHERE bind") != 0 ||
		    expect_true(strstr(deparsed_sql, "$") == NULL || cases[index].dialect == SQLPARSER_DIALECT_POSTGRESQL,
		                "bind RHS deparse should not expose internal bind markers") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);

	}

	return 0;
}

static int test_update_assignment_multiple_bind_rhs_literal_rewrite(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_selector_t insert_selector;
	sqlparser_selector_t source_selector;
	sqlparser_selector_t protected_selector;
	sqlparser_identifier_path_view_t target;
	sqlparser_literal_value_t encrypted;
	const char *phone_backup_parts[1];
	const char *secret_backup_parts[1];
	char *deparsed_sql;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&insert_selector, 0, sizeof(insert_selector));
	memset(&source_selector, 0, sizeof(source_selector));
	memset(&protected_selector, 0, sizeof(protected_selector));
	memset(&target, 0, sizeof(target));
	memset(&encrypted, 0, sizeof(encrypted));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"UPDATE KDES.DBP_CRYPTO_TEST SET PHONE = :1, SECRET = :2 WHERE ID = :3",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "multiple bind RHS parse should succeed") != 0) {
		return 1;
	}

	insert_selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	insert_selector.statement_index = 0U;
	insert_selector.item_index = 0U;
	source_selector = insert_selector;
	phone_backup_parts[0] = "PHONE_ORIG";
	target.parts = phone_backup_parts;
	target.part_count = 1U;
	rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
		handle,
		&insert_selector,
		&target,
		&source_selector,
		&error);
	if (expect_status_ok(rc, &error, "phone backup assignment insert should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	protected_selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	protected_selector.statement_index = 0U;
	protected_selector.item_index = 1U;
	encrypted.kind = SQLPARSER_LITERAL_KIND_STRING;
	encrypted.string_value = "encrypted-phone";
	rc = sqlparser_selector_set_update_assignment_literal(handle, &protected_selector, &encrypted, &error);
	if (expect_status_ok(rc, &error, "phone RHS literal replacement should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	insert_selector.item_index = 2U;
	source_selector = insert_selector;
	secret_backup_parts[0] = "SECRET_ORIG";
	target.parts = secret_backup_parts;
	target.part_count = 1U;
	rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
		handle,
		&insert_selector,
		&target,
		&source_selector,
		&error);
	if (expect_status_ok(rc, &error, "secret backup assignment insert should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	protected_selector.item_index = 3U;
	encrypted.string_value = "encrypted-secret";
	rc = sqlparser_selector_set_update_assignment_literal(handle, &protected_selector, &encrypted, &error);
	if (expect_status_ok(rc, &error, "secret RHS literal replacement should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "multiple bind RHS rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "multiple bind RHS deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "\"PHONE_ORIG\" = :1") != NULL,
	                "multiple bind RHS should contain phone backup bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "PHONE = 'encrypted-phone'") != NULL,
	                "multiple bind RHS should contain encrypted phone") != 0 ||
	    expect_true(strstr(deparsed_sql, "\"SECRET_ORIG\" = :2") != NULL,
	                "multiple bind RHS should contain secret backup bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "SECRET = 'encrypted-secret'") != NULL,
	                "multiple bind RHS should contain encrypted secret") != 0 ||
	    expect_true(strstr(deparsed_sql, "ID = :3") != NULL,
	                "multiple bind RHS should preserve WHERE bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_assignment_literal_rewrite_rejects_complex_rhs(void)
{
	static const char *sqls[] = {
		"UPDATE users SET secret = UPPER(?) WHERE id = ?",
		"UPDATE users SET secret = other_secret WHERE id = ?",
		"UPDATE users SET secret = DEFAULT WHERE id = ?"
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_literal_value_t encrypted;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(sqls) / sizeof(sqls[0]); index++) {
		sqlparser_handle_t *handle;
		sqlparser_selector_t selector;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		memset(&selector, 0, sizeof(selector));
		memset(&encrypted, 0, sizeof(encrypted));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_MYSQL;
		rc = sqlparser_parse_with_options(sqls[index], &options, &handle, &error);
		if (expect_status_ok(rc, &error, "complex RHS parse should succeed") != 0) {
			return 1;
		}

		selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		selector.statement_index = 0U;
		selector.item_index = 0U;
		encrypted.kind = SQLPARSER_LITERAL_KIND_STRING;
		encrypted.string_value = "encrypted";
		rc = sqlparser_selector_set_update_assignment_literal(handle, &selector, &encrypted, &error);
		if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED, "complex RHS replacement should be unsupported") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_insert_cell_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	char *cell_sql;
	char *selector_sql;
	char *deparsed_sql;
	int rc;

	sql =
		"INSERT INTO public.users (id, name, created_at, score) "
		"VALUES (1, upper('bob'), DEFAULT, 10), (2, 'carol', DEFAULT, 20)";
	handle = NULL;
	cell_sql = NULL;
	selector_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "expression insert parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_cell_sql(handle, 0U, 0U, 1U, &cell_sql, &error);
	if (expect_status_ok(rc, &error, "expression insert cell SQL fetch should succeed") != 0 ||
	    expect_true(strstr(cell_sql, "upper(") != NULL, "expression insert cell SQL should contain upper(") != 0 ||
	    expect_true(strstr(cell_sql, "'bob'") != NULL, "expression insert cell SQL should contain 'bob'") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = 0U;
	selector.row_index = 1U;
	selector.column_index = 2U;
	rc = sqlparser_selector_insert_cell_sql(handle, &selector, &selector_sql, &error);
	if (expect_status_ok(rc, &error, "selector insert cell SQL fetch should succeed") != 0 ||
	    expect_true(strcmp(selector_sql, "DEFAULT") == 0, "selector insert cell SQL should be DEFAULT") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_set_cell_sql(handle, 0U, 0U, 1U, "lower('BOB')", &error);
	if (expect_status_ok(rc, &error, "expression insert cell mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_selector_set_insert_cell_sql(handle, &selector, "now()", &error);
	if (expect_status_ok(rc, &error, "selector insert cell mutation should succeed") != 0) {
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "expression insert deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "lower(") != NULL, "deparsed insert should contain lower(") != 0 ||
	    expect_true(strstr(deparsed_sql, "'BOB'") != NULL, "deparsed insert should contain 'BOB'") != 0 ||
	    expect_true(strstr(deparsed_sql, "now()") != NULL, "deparsed insert should contain now()") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_sql);
	sqlparser_string_free(cell_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_cell_source_keyword_boundary(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"INSERT INTO évalue (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"INSERT INTO évalue (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"INSERT INTO évalues (x) VALUES (CoAlEsCe(1,  2))"
		}
	};
	const char *expected_sql;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_selector_t selector;
	char *cell_sql;
	char *selector_sql;
	char *view_json;
	size_t index;
	int rc;

	expected_sql = "CoAlEsCe(1,  2)";
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		cell_sql = NULL;
		selector_sql = NULL;
		view_json = NULL;
		memset(&error, 0, sizeof(error));
		memset(&selector, 0, sizeof(selector));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;

		rc = sqlparser_parse_with_options(
			cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "Unicode keyword-boundary INSERT parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_insert_cell_sql(
			handle, 0U, 0U, 0U, &cell_sql, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "Unicode keyword-boundary insert cell SQL should be available") != 0 ||
		    expect_true(
			    cell_sql != NULL && strcmp(cell_sql, expected_sql) == 0,
			    "insert cell SQL should preserve the exact source expression") != 0) {
			sqlparser_string_free(cell_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
		selector.statement_index = 0U;
		selector.row_index = 0U;
		selector.column_index = 0U;
		rc = sqlparser_selector_insert_cell_sql(
			handle, &selector, &selector_sql, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "Unicode keyword-boundary selector cell SQL should be available") != 0 ||
		    expect_true(
			    selector_sql != NULL &&
				    strcmp(selector_sql, expected_sql) == 0,
			    "selector cell SQL should preserve the exact source expression") != 0) {
			sqlparser_string_free(selector_sql);
			sqlparser_string_free(cell_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "Unicode keyword-boundary View JSON should export") != 0 ||
		    expect_true(
			    view_json != NULL &&
				    strstr(
					    view_json,
					    "\"expression_sql\":\"CoAlEsCe(1,  2)\"") != NULL,
			    "View JSON should preserve the exact source expression") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_string_free(selector_sql);
			sqlparser_string_free(cell_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_string_free(view_json);
		sqlparser_string_free(selector_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int test_sqlserver_expression_source_case(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t statement_index,
	size_t dml_index,
	sqlparser_graph_dml_kind_t dml_kind,
	const char *const *expected_expression_sql,
	size_t expected_cell_count,
	int expect_cell_selectors,
	const char *message)
{
	static const char *const merge_column_names[] = {
		"id",
		"calc",
		"created_at"
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	sqlparser_graph_dml_column_t dml_column;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_field_t field;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_target_t target;
	sqlparser_graph_value_t value;
	sqlparser_index_span_t cell_span;
	char *cell_sql;
	char *view_json;
	json_error_t json_error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	json_t *query_graph;
	json_t *dml_json;
	json_t *rows;
	json_t *merge_columns_json;
	size_t dml_count;
	size_t branch_index;
	size_t cell_index;
	size_t index;
	size_t merge_source_field_index;
	size_t merge_source_target_index;
	int rc;
	int result;

	handle = NULL;
	view_json = NULL;
	root = NULL;
	merge_columns_json = NULL;
	merge_source_field_index = (size_t)-1;
	merge_source_target_index = (size_t)-1;
	result = 1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, message) != 0 ||
	    expect_deparse_equals_and_reparse(handle, sql, message) != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    statement_index,
			    &graph,
			    &error),
		    &error,
		    "SQL Server expression source graph should succeed") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_count(
			    &graph,
			    &dml_count,
			    &error),
		    &error,
		    "SQL Server expression source DML count should succeed") != 0 ||
	    expect_true(
		    dml_count == dml_index + 1U,
		    "SQL Server expression source DML count mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_at(
			    &graph,
			    dml_index,
			    &dml,
			    &error),
		    &error,
		    "SQL Server expression source DML should be available") != 0 ||
	    expect_true(
		    dml.kind == dml_kind,
		    "SQL Server expression source DML shape mismatch") != 0) {
		goto done;
	}
	cell_span = dml.rows;
	if (dml_kind == SQLPARSER_GRAPH_DML_MERGE) {
		if (expect_true(
			    dml.target_columns.count == 0U &&
				    dml.rows.count == 0U &&
				    dml.branches.count == 1U,
			    "MERGE INSERT payload should be branch-scoped") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    0U,
				    &branch_index,
				    &error),
			    &error,
			    "MERGE branch index should be available") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    branch_index,
				    &branch,
				    &error),
			    &error,
			    "MERGE branch should be available") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    branch_index,
			    &branch_detail,
			    &error,
			    "MERGE branch detail should be available") != 0 ||
		    expect_true(
			    branch.statement_index == statement_index &&
				    branch.dml_index == dml_index &&
				    branch.ordinal == 0U &&
				    branch.branch_kind ==
					    SQLPARSER_GRAPH_DML_BRANCH_WHEN &&
				    branch_detail.action_kind ==
					    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
				    branch_detail.match_kind ==
					    SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET &&
				    branch.has_target_relation != 0 &&
				    branch.target_relation_index ==
					    dml.target_relation_index &&
				    branch.target_columns.count ==
					    expected_cell_count &&
				    branch.rows.count ==
					    expected_cell_count &&
				    branch_detail.assignments.count == 0U &&
				    branch.has_condition_block == 0 &&
				    branch.has_condition_selector == 0,
			    "MERGE branch payload shape mismatch") != 0) {
			goto done;
		}
		for (index = 0U; index < expected_cell_count; index++) {
			if (expect_status_ok(
				    sqlparser_query_graph_span_index_at(
					    &graph,
					    branch.target_columns,
					    index,
					    &cell_index,
					    &error),
				    &error,
				    "nested MERGE target column index should resolve") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_dml_column_at(
					    &graph,
					    cell_index,
					    &dml_column,
					    &error),
				    &error,
				    "nested MERGE target column should resolve") != 0 ||
			    expect_true(
				    dml_column.statement_index ==
						    statement_index &&
					    dml_column.dml_index == dml_index &&
					    dml_column.ordinal == index &&
					    dml_column.column_name != NULL &&
					    strcmp(
						    dml_column.column_name,
						    merge_column_names[index]) == 0,
				    "nested MERGE target column mismatch") != 0) {
				goto done;
			}
		}
		cell_span = branch.rows;
	} else if (expect_true(
			   dml.rows.count == expected_cell_count,
			   "INSERT row count mismatch") != 0) {
		goto done;
	}

	for (index = 0U; index < expected_cell_count; index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    cell_span,
				    index,
				    &cell_index,
				    &error),
			    &error,
			    "SQL Server expression source cell index should be available") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    cell_index,
				    &cell,
				    &error),
			    &error,
			    "SQL Server expression source cell should be available") != 0 ||
		    expect_true(
			    cell.dml_index == dml_index &&
				    cell.statement_index == statement_index &&
				    cell.row_index == 0U &&
				    cell.column_ordinal == index,
			    "SQL Server expression source cell coordinates mismatch") != 0 ||
		    expect_true(
			    cell.kind ==
				    (expected_expression_sql[index] != NULL ?
					     SQLPARSER_GRAPH_VALUE_EXPRESSION :
					     SQLPARSER_GRAPH_VALUE_FIELD),
			    "SQL Server expression source cell kind mismatch") != 0 ||
		    expect_true(
			    expect_cell_selectors ?
				    (cell.has_selector != 0 &&
				     cell.selector.kind ==
					     SQLPARSER_SELECTOR_KIND_INSERT_CELL &&
				     cell.selector.statement_index ==
					     statement_index &&
				     cell.selector.row_index == 0U &&
				     cell.selector.column_index == index) :
				    cell.has_selector == 0,
			    "SQL Server expression source cell selector mismatch") != 0) {
			goto done;
		}
		if (expected_expression_sql[index] != NULL) {
			if (expect_true(
				    cell.has_bind == 0 &&
					    cell.bind_kind ==
						    SQLPARSER_BIND_KIND_NONE &&
					    cell.has_bind_sql == 0 &&
					    cell.has_bind_position == 0 &&
					    cell.has_source_target == 0 &&
					    cell.has_source_field == 0,
				    "SQL Server expression cell metadata mismatch") != 0) {
				goto done;
			}
		} else if (dml_kind == SQLPARSER_GRAPH_DML_MERGE) {
			if (expect_true(
				    index == 0U &&
					    cell.has_bind == 0 &&
					    cell.bind_kind ==
						    SQLPARSER_BIND_KIND_NONE &&
					    cell.has_bind_sql == 0 &&
					    cell.has_bind_position == 0 &&
					    cell.has_source_target != 0 &&
					    cell.has_source_field != 0,
				    "nested MERGE source cell metadata mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_field_at(
					    &graph,
					    cell.source_field_index,
					    &field,
					    &error),
				    &error,
				    "nested MERGE source field should resolve") != 0 ||
			    expect_true(
				    field.column_name != NULL &&
					    strcmp(field.column_name, "id") == 0 &&
					    field.has_relation != 0,
				    "nested MERGE source field mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_relation_at(
					    &graph,
					    field.relation_index,
					    &relation,
					    &error),
				    &error,
				    "nested MERGE source relation should resolve") != 0 ||
			    expect_true(
				    relation.alias_name != NULL &&
					    strcmp(relation.alias_name, "s") == 0 &&
					    relation.has_source_block != 0 &&
					    field.block_index ==
						    relation.block_index,
				    "nested MERGE source relation mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_target_at(
					    &graph,
					    cell.source_target_index,
					    &target,
					    &error),
				    &error,
				    "nested MERGE source target should resolve") != 0 ||
			    expect_true(
				    target.kind ==
						    SQLPARSER_GRAPH_TARGET_BIND &&
					    target.output_name != NULL &&
					    strcmp(target.output_name, "id") == 0 &&
					    target.has_value != 0,
				    "nested MERGE source target mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_value_at(
					    &graph,
					    target.value_index,
					    &value,
					    &error),
				    &error,
				    "nested MERGE source target value should resolve") != 0 ||
			    expect_true(
				    value.kind == SQLPARSER_GRAPH_VALUE_BIND &&
					    value.has_bind != 0 &&
					    strcmp(value.bind, "id") == 0 &&
					    value.bind_kind ==
						    SQLPARSER_BIND_KIND_NAMED &&
					    value.has_bind_sql != 0 &&
					    strcmp(value.bind_sql, "@id") == 0 &&
					    value.has_bind_position != 0 &&
					    value.bind_position == 1U,
				    "nested MERGE source target bind mismatch") != 0) {
				goto done;
			}
			merge_source_field_index =
				cell.source_field_index;
			merge_source_target_index =
				cell.source_target_index;
		}
		if (!expect_cell_selectors) {
			continue;
		}
		cell_sql = NULL;
		rc = sqlparser_insert_cell_sql(
			handle,
			statement_index,
			0U,
			index,
			&cell_sql,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SQL Server control insert cell SQL should be available") != 0 ||
		    expect_true(
			    cell_sql != NULL &&
				    expected_expression_sql[index] != NULL &&
				    strcmp(
					    cell_sql,
					    expected_expression_sql[index]) == 0,
			    "SQL Server control insert cell SQL mismatch") != 0) {
			sqlparser_string_free(cell_sql);
			goto done;
		}
		sqlparser_string_free(cell_sql);
	}

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "SQL Server expression source View JSON should export") != 0) {
		goto done;
	}
	memset(&json_error, 0, sizeof(json_error));
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, statement_index) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml_json = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	if (dml_index > 0U && json_is_object(dml_json)) {
		json_t *children;

		children = json_object_get(dml_json, "children");
		dml_json = json_is_array(children) ?
			json_array_get(children, dml_index - 1U) :
			NULL;
	}
	rows = json_is_object(dml_json) ?
		json_object_get(dml_json, "rows") :
		NULL;
	if (dml_kind == SQLPARSER_GRAPH_DML_MERGE &&
	    json_is_object(dml_json)) {
		json_t *branch_json;
		json_t *branches;

		branches = json_object_get(dml_json, "branches");
		branch_json = json_is_array(branches) ?
			json_array_get(branches, 0U) :
			NULL;
		merge_columns_json = json_is_object(branch_json) ?
			json_object_get(branch_json, "target_columns") :
			NULL;
		if (expect_true(
			    json_object_get(dml_json, "target_columns") == NULL &&
				    rows == NULL &&
				    json_is_array(branches) &&
				    json_array_size(branches) == 1U &&
				    json_is_object(branch_json) &&
				    json_object_size(branch_json) == 7U &&
				    json_integer_is(
					    branch_json,
					    "ordinal",
					    0) &&
				    json_string_is(
					    branch_json,
					    "branch_kind",
					    "when") &&
				    json_string_is(
					    branch_json,
					    "merge_action_kind",
					    "insert") &&
				    json_string_is(
					    branch_json,
					    "merge_match_kind",
					    "not_matched_by_target") &&
				    json_integer_is(
					    branch_json,
					    "target_relation",
					    (json_int_t)
						    dml.target_relation_index) &&
				    json_object_get(
					    branch_json,
					    "condition_block") == NULL &&
				    json_object_get(
					    branch_json,
					    "condition_selector") == NULL &&
				    json_is_array(merge_columns_json) &&
				    json_array_size(merge_columns_json) ==
					    expected_cell_count,
			    "MERGE View payload should be branch-scoped") != 0) {
			goto done;
		}
		for (index = 0U; index < expected_cell_count; index++) {
			json_t *column_json;

			column_json =
				json_array_get(merge_columns_json, index);
			if (expect_true(
				    json_is_object(column_json) &&
					    json_object_size(column_json) == 2U &&
					    json_integer_is(
						    column_json,
						    "ordinal",
						    (json_int_t)index) &&
					    json_string_is(
						    column_json,
						    "column",
						    merge_column_names[index]),
				    "nested MERGE View target column mismatch") != 0) {
				goto done;
			}
		}
		rows = json_is_object(branch_json) ?
			json_object_get(branch_json, "rows") :
			NULL;
	}
	if (expect_true(
		    json_is_array(rows) &&
			    json_array_size(rows) == expected_cell_count,
		    "SQL Server expression source View rows mismatch") != 0) {
		goto done;
	}
	for (index = 0U; index < expected_cell_count; index++) {
		json_t *cell_json;
		json_t *row_json;
		json_t *column_json;
		json_t *kind_json;
		json_t *expression_json;
		json_t *selector_json;
		const char *expected_kind;

		cell_json = json_array_get(rows, index);
		row_json = json_is_object(cell_json) ?
			json_object_get(cell_json, "row") :
			NULL;
		column_json = json_is_object(cell_json) ?
			json_object_get(cell_json, "column") :
			NULL;
		kind_json = json_is_object(cell_json) ?
			json_object_get(cell_json, "kind") :
			NULL;
		expression_json = json_is_object(cell_json) ?
			json_object_get(cell_json, "expression_sql") :
			NULL;
		selector_json = json_is_object(cell_json) ?
			json_object_get(cell_json, "selector") :
			NULL;
		expected_kind = expected_expression_sql[index] != NULL ?
			"expression" :
			"field";
		if (expect_true(
			    json_is_integer(row_json) &&
				    json_integer_value(row_json) == 0 &&
				    json_is_integer(column_json) &&
				    json_integer_value(column_json) ==
					    (json_int_t)index &&
				    json_is_string(kind_json) &&
				    strcmp(
					    json_string_value(kind_json),
					    expected_kind) == 0,
			    "SQL Server expression source View cell mismatch") != 0 ||
		    expect_true(
			    expected_expression_sql[index] != NULL ?
				    (json_is_string(expression_json) &&
				     strcmp(
					     json_string_value(expression_json),
					     expected_expression_sql[index]) == 0) :
				    expression_json == NULL,
			    "SQL Server expression source View SQL mismatch") != 0) {
			goto done;
		}
		if (expected_expression_sql[index] != NULL) {
			if (expect_true(
				    json_object_size(cell_json) ==
						    (expect_cell_selectors ?
							     6U :
							     5U) &&
					    json_integer_is(
						    cell_json,
						    "bind_kind",
						    SQLPARSER_BIND_KIND_NONE) &&
					    json_object_get(
						    cell_json,
						    "source_target") == NULL &&
					    json_object_get(
						    cell_json,
						    "source_field") == NULL &&
					    json_object_get(
						    cell_json,
						    "bind_key") == NULL &&
					    json_object_get(
						    cell_json,
						    "bind_sql") == NULL &&
					    json_object_get(
						    cell_json,
						    "bind_position") == NULL,
				    "SQL Server expression source View metadata mismatch") != 0) {
				goto done;
			}
		} else if (expect_true(
				   dml_kind ==
						   SQLPARSER_GRAPH_DML_MERGE &&
					   json_object_size(cell_json) == 6U &&
					   json_integer_is(
						   cell_json,
						   "source_target",
						   (json_int_t)
							   merge_source_target_index) &&
					   json_integer_is(
						   cell_json,
						   "source_field",
						   (json_int_t)
							   merge_source_field_index) &&
					   json_integer_is(
						   cell_json,
						   "bind_kind",
						   SQLPARSER_BIND_KIND_NONE) &&
					   json_object_get(
						   cell_json,
						   "bind_key") == NULL &&
					   json_object_get(
						   cell_json,
						   "bind_sql") == NULL &&
					   json_object_get(
						   cell_json,
						   "bind_position") == NULL,
				   "nested MERGE View source lineage mismatch") != 0) {
			goto done;
		}
		if (expect_cell_selectors) {
			char expected_selector[64];

			snprintf(
				expected_selector,
				sizeof(expected_selector),
				"stmt[%zu].insert_cell[0][%zu]",
				statement_index,
				index);
			if (expect_true(
				    json_is_string(selector_json) &&
					    strcmp(
						    json_string_value(
							    selector_json),
						    expected_selector) == 0,
				    "SQL Server control cell View selector mismatch") != 0) {
				goto done;
			}
		} else if (expect_true(
			       selector_json == NULL,
			       "nested SQL Server DML cell must not expose a selector") != 0) {
			goto done;
		}
	}
	result = 0;

done:
	if (root != NULL) {
		json_decref(root);
	}
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return result;
}

static int test_sqlserver_control_nested_expression_source_sql(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	static const char *const control_expressions[] = {
		"/*lead-A*/ CoAlEsCe ( @x ,  0 ) /*tail-A*/",
		"CURRENT_TIMESTAMP",
		"[dbo].[Fn] ( @x )"
	};
	static const char *const nested_insert_expressions[] = {
		"/*lead-I*/ AbS ( @id ) /*tail-I*/",
		"CURRENT_TIMESTAMP",
		"[dbo].[Fn] ( @id )"
	};
	static const char *const nested_merge_expressions[] = {
		NULL,
		"/*lead-M*/ CoAlEsCe ( @v , 0 ) /*tail-M*/",
		"CURRENT_TIMESTAMP"
	};
	const char *control_sql;
	const char *nested_insert_sql;
	const char *nested_merge_sql;
	size_t dialect_index;

	control_sql =
		"IF @go = 1 INSERT dbo.ControlSink(a, b, c) VALUES (  "
		"/*lead-A*/ CoAlEsCe ( @x ,  0 ) /*tail-A*/  , "
		"CURRENT_TIMESTAMP, [dbo].[Fn] ( @x ) )";
	nested_insert_sql =
		"INSERT INTO dbo.InsertAudit(id, created_at, calc) "
		"SELECT d.id, d.created_at, d.calc "
		"FROM (INSERT INTO dbo.InsertSource(id, created_at, calc) "
		"OUTPUT INSERTED.id, INSERTED.created_at, INSERTED.calc "
		"VALUES (  /*lead-I*/ AbS ( @id ) /*tail-I*/  , "
		"CURRENT_TIMESTAMP, [dbo].[Fn] ( @id ) )) AS d";
	nested_merge_sql =
		"INSERT INTO dbo.MergeAudit(action_name, id, calc, created_at) "
		"SELECT d.action_name, d.id, d.calc, d.created_at "
		"FROM (MERGE dbo.MergeTarget AS t "
		"USING (SELECT @id AS id) AS s ON t.id = s.id "
		"WHEN NOT MATCHED THEN INSERT (id, calc, created_at) "
		"VALUES (s.id,  /*lead-M*/ CoAlEsCe ( @v , 0 ) /*tail-M*/ , "
		"CURRENT_TIMESTAMP) "
		"OUTPUT $action AS action_name, INSERTED.id, INSERTED.calc, "
		"INSERTED.created_at) AS d";

	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		if (test_sqlserver_expression_source_case(
			    dialects[dialect_index],
			    control_sql,
			    1U,
			    0U,
			    SQLPARSER_GRAPH_DML_INSERT,
			    control_expressions,
			    sizeof(control_expressions) /
				    sizeof(control_expressions[0]),
			    1,
			    "SQL Server control INSERT source SQL should remain exact") != 0 ||
		    test_sqlserver_expression_source_case(
			    dialects[dialect_index],
			    nested_insert_sql,
			    0U,
			    1U,
			    SQLPARSER_GRAPH_DML_INSERT,
			    nested_insert_expressions,
			    sizeof(nested_insert_expressions) /
				    sizeof(nested_insert_expressions[0]),
			    0,
			    "nested SQL Server INSERT source SQL should remain exact") != 0 ||
		    test_sqlserver_expression_source_case(
			    dialects[dialect_index],
			    nested_merge_sql,
			    0U,
			    1U,
			    SQLPARSER_GRAPH_DML_MERGE,
			    nested_merge_expressions,
			    sizeof(nested_merge_expressions) /
				    sizeof(nested_merge_expressions[0]),
			    0,
			    "nested SQL Server MERGE source SQL should remain exact") != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_merge_single_insert_branch_case(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *condition_sql,
	size_t when_index,
	const char *bind_key,
	sqlparser_bind_kind_t bind_kind,
	const char *bind_sql,
	const char *expression_sql)
{
	static const char *const column_names[] = {
		"id",
		"created_at"
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	sqlparser_graph_dml_column_t column;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_field_t assignment_field;
	sqlparser_graph_dml_branch_t update_branch;
	sqlparser_test_merge_branch_detail_t update_branch_detail;
	sqlparser_selector_t parsed_selector;
	char *assignment_sql;
	char *bind_selector_text;
	char *condition;
	char *selector_text;
	char *view_json;
	char expected_selector_text[64];
	json_error_t json_error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	json_t *query_graph;
	json_t *dml_json;
	json_t *branches;
	json_t *branch_json;
	json_t *columns_json;
	json_t *rows_json;
	size_t index;
	size_t item_index;
	size_t assignment_index;
	size_t update_assignment_index;
	int rc;

	handle = NULL;
	assignment_sql = NULL;
	bind_selector_text = NULL;
	condition = NULL;
	selector_text = NULL;
	view_json = NULL;
	root = NULL;
	snprintf(
		expected_selector_text,
		sizeof(expected_selector_text),
		"stmt[0].merge_branch_condition[%zu]",
		when_index);
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(
		sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "single-arm MERGE should parse") != 0 ||
	    expect_deparse_equals_and_reparse(
		    handle,
		    sql,
		    "single-arm MERGE should deparse exactly and reparse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "single-arm MERGE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "single-arm MERGE DML should be available") != 0 ||
	    expect_true(
		    dml.kind == SQLPARSER_GRAPH_DML_MERGE &&
			    dml.has_target_relation != 0 &&
			    dml.assignments.count ==
				    (when_index == 0U ? 0U : 1U) &&
			    dml.target_columns.count == 0U &&
			    dml.rows.count == 0U &&
			    dml.branches.count == when_index + 1U,
		    "single-arm MERGE payload should be branch-scoped") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.branches,
			    when_index,
			    &index,
			    &error),
		    &error,
		    "single-arm MERGE branch index should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_branch_at(
			    &graph,
			    index,
			    &branch,
			    &error),
		    &error,
		    "single-arm MERGE branch should be available") != 0 ||
	    expect_merge_branch_detail(
		    &graph,
		    index,
		    &branch_detail,
		    &error,
		    "single-arm MERGE branch detail should be available") != 0 ||
	    expect_true(
		    branch.ordinal == when_index &&
			    branch.statement_index == 0U &&
			    branch.dml_index == dml.index &&
			    branch.branch_kind ==
				    SQLPARSER_GRAPH_DML_BRANCH_WHEN &&
			    branch_detail.action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
			    branch_detail.match_kind ==
				    SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET &&
			    branch.has_target_relation != 0 &&
			    branch.target_relation_index ==
				    dml.target_relation_index &&
			    branch.target_columns.count == 2U &&
			    branch.rows.count == 2U &&
			    branch_detail.assignments.count == 0U &&
			    branch.has_condition_block == 0 &&
			    branch.has_condition_selector != 0 &&
			    branch.condition_selector.kind ==
				    SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
			    branch.condition_selector.statement_index == 0U &&
			    branch.condition_selector.row_index == 0U &&
			    branch.condition_selector.item_index == when_index,
		    "single-arm MERGE branch metadata mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_format(
			    &branch.condition_selector,
			    &selector_text,
			    &error),
		    &error,
		    "MERGE condition selector should format") != 0 ||
	    expect_true(
		    selector_text != NULL &&
			    strcmp(selector_text, expected_selector_text) == 0 &&
			    strcmp(
				    sqlparser_selector_kind_name(
					    branch.condition_selector.kind),
				    "merge_branch_condition") == 0,
		    "MERGE condition selector text mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_parse(
			    selector_text,
			    &parsed_selector,
			    &error),
		    &error,
		    "MERGE condition selector should parse") != 0 ||
	    expect_true(
			    parsed_selector.kind ==
				    branch.condition_selector.kind &&
			    parsed_selector.statement_index == 0U &&
			    parsed_selector.row_index == 0U &&
			    parsed_selector.item_index == when_index,
		    "MERGE condition selector round-trip mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_clause_sql(
			    handle,
			    &branch.condition_selector,
			    &condition,
			    &error),
		    &error,
		    "MERGE branch condition SQL should be available") != 0 ||
	    expect_true(
		    condition != NULL &&
			    strcmp(condition, condition_sql) == 0,
		    "MERGE branch condition SQL should preserve source text") != 0) {
		goto fail;
	}
	if (when_index != 0U) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    0U,
				    &index,
				    &error),
			    &error,
			    "preceding MERGE UPDATE branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    index,
				    &update_branch,
				    &error),
			    &error,
			    "preceding MERGE UPDATE branch should be available") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    index,
			    &update_branch_detail,
			    &error,
			    "preceding MERGE UPDATE branch detail should be available") != 0 ||
		    expect_true(
			    update_branch.ordinal == 0U &&
				    update_branch_detail.action_kind ==
					    SQLPARSER_GRAPH_MERGE_ACTION_UPDATE &&
				    update_branch_detail.match_kind ==
					    SQLPARSER_GRAPH_MERGE_MATCH_MATCHED &&
				    update_branch_detail.assignments.count == 1U &&
				    update_branch.target_columns.count == 0U &&
				    update_branch.rows.count == 0U &&
				    update_branch.has_condition_selector == 0,
			    "preceding MERGE UPDATE branch metadata mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    0U,
				    &assignment_index,
				    &error),
			    &error,
			    "preceding MERGE UPDATE assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    update_branch_detail.assignments,
				    0U,
				    &update_assignment_index,
				    &error),
			    &error,
			    "preceding MERGE UPDATE branch assignment should resolve") != 0 ||
		    expect_true(
			    update_assignment_index == assignment_index,
			    "MERGE branch and DML assignment spans should reference the same assignment") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "preceding MERGE UPDATE assignment should be available") != 0 ||
		    expect_true(
			    assignment.statement_index == 0U &&
				    assignment.dml_index == dml.index &&
				    assignment.value_kind ==
					    SQLPARSER_GRAPH_VALUE_EXPRESSION &&
				    assignment.has_source_target == 0 &&
				    assignment.has_source_field == 0 &&
				    assignment.has_bind == 0 &&
				    assignment.bind_kind ==
					    SQLPARSER_BIND_KIND_NONE &&
				    assignment.has_bind_sql == 0 &&
				    assignment.has_bind_position == 0 &&
				    assignment.has_selector != 0 &&
				    assignment.selector.kind ==
					    SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT &&
				    assignment.selector.row_index == 0U &&
				    assignment.selector.item_index == 0U &&
				    assignment.selector.column_index == 0U,
			    "preceding MERGE UPDATE assignment metadata mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_field_at(
				    &graph,
				    assignment.target_field_index,
				    &assignment_field,
				    &error),
			    &error,
			    "preceding MERGE UPDATE target field should resolve") != 0 ||
		    expect_true(
			    assignment_field.column_name != NULL &&
				    strcmp(
					    assignment_field.column_name,
					    "created_at") == 0,
			    "preceding MERGE UPDATE target field mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_selector_update_assignment_sql(
				    handle,
				    &assignment.selector,
				    &assignment_sql,
				    &error),
			    &error,
			    "preceding MERGE UPDATE SQL should be available") != 0 ||
		    expect_true(
			    assignment_sql != NULL &&
				    strcmp(
					    assignment_sql,
					    "CURRENT_TIMESTAMP") == 0,
			    "preceding MERGE UPDATE SQL mismatch") != 0) {
			goto fail;
		}
		sqlparser_string_free(assignment_sql);
		assignment_sql = NULL;
	}
	for (item_index = 0U; item_index < 2U; item_index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    branch.target_columns,
				    item_index,
				    &index,
				    &error),
			    &error,
			    "MERGE branch column index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_column_at(
				    &graph,
				    index,
				    &column,
				    &error),
			    &error,
			    "MERGE branch column should be available") != 0 ||
		    expect_true(
			    column.statement_index == 0U &&
				    column.dml_index == dml.index &&
				    column.ordinal == item_index &&
				    column.column_name != NULL &&
				    strcmp(
					    column.column_name,
					    column_names[item_index]) == 0,
			    "MERGE branch column metadata mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    branch.rows,
				    item_index,
				    &index,
				    &error),
			    &error,
			    "MERGE branch cell index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    index,
				    &cell,
				    &error),
			    &error,
			    "MERGE branch cell should be available") != 0 ||
		    expect_true(
			    cell.statement_index == 0U &&
				    cell.dml_index == dml.index &&
				    cell.row_index == when_index &&
				    cell.column_ordinal == item_index,
			    "MERGE branch cell coordinates mismatch") != 0) {
			goto fail;
		}
		if (item_index == 0U) {
			if (expect_true(
				    cell.kind == SQLPARSER_GRAPH_VALUE_BIND &&
					    cell.has_bind != 0 &&
					    strcmp(cell.bind, bind_key) == 0 &&
					    cell.bind_kind == bind_kind &&
					    cell.has_bind_sql != 0 &&
					    strcmp(cell.bind_sql, bind_sql) == 0 &&
					    cell.has_bind_position != 0 &&
					    cell.bind_position == 1U &&
					    cell.has_source_target == 0 &&
					    cell.has_source_field == 0 &&
					    cell.has_selector != 0 &&
					    cell.selector.kind ==
						    SQLPARSER_SELECTOR_KIND_VALUE &&
					    cell.selector.statement_index == 0U,
				    "MERGE branch bind metadata mismatch") != 0) {
				goto fail;
			}
			if (expect_status_ok(
				    sqlparser_selector_format(
					    &cell.selector,
					    &bind_selector_text,
					    &error),
				    &error,
				    "MERGE branch bind selector should format") != 0) {
				goto fail;
			}
		} else if (expect_true(
				   cell.kind ==
					   SQLPARSER_GRAPH_VALUE_EXPRESSION &&
					   cell.has_bind == 0 &&
					   cell.bind_kind ==
						   SQLPARSER_BIND_KIND_NONE &&
					   cell.has_bind_sql == 0 &&
					   cell.has_bind_position == 0 &&
					   cell.has_source_target == 0 &&
					   cell.has_source_field == 0 &&
					   cell.has_selector == 0,
				   "MERGE branch expression kind mismatch") != 0) {
			goto fail;
		}
	}
	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "single-arm MERGE View should export") != 0) {
		goto fail;
	}
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, 0U) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml_json = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	branches = json_is_object(dml_json) ?
		json_object_get(dml_json, "branches") :
		NULL;
	branch_json = json_is_array(branches) ?
		json_array_get(branches, when_index) :
		NULL;
	columns_json = json_is_object(branch_json) ?
		json_object_get(branch_json, "target_columns") :
		NULL;
	rows_json = json_is_object(branch_json) ?
		json_object_get(branch_json, "rows") :
		NULL;
	if (expect_true(
		    json_is_object(dml_json) &&
			    json_string_is(dml_json, "kind", "merge") &&
			    json_string_is(
				    dml_json,
				    "insert_mode",
				    "unknown") &&
			    json_integer_is(
				    dml_json,
				    "target_relation",
				    (json_int_t)dml.target_relation_index) &&
			    json_object_get(dml_json, "target_columns") == NULL &&
			    json_object_get(dml_json, "rows") == NULL &&
			    json_is_array(branches) &&
			    json_array_size(branches) == when_index + 1U &&
			    json_object_size(dml_json) ==
				    (when_index == 0U ? 4U : 5U),
		    "single-arm MERGE View parent payload should be empty") != 0 ||
	    expect_true(
		    json_is_object(branch_json) &&
			    json_integer_is(
				    branch_json,
				    "ordinal",
				    (json_int_t)when_index) &&
			    json_string_is(branch_json, "branch_kind", "when") &&
			    json_string_is(
				    branch_json,
				    "merge_action_kind",
				    "insert") &&
			    json_string_is(
				    branch_json,
				    "merge_match_kind",
				    "not_matched_by_target") &&
			    json_integer_is(
				    branch_json,
				    "target_relation",
				    (json_int_t)dml.target_relation_index) &&
			    json_string_is(
				    branch_json,
				    "condition_selector",
				    expected_selector_text) &&
			    json_object_get(
				    branch_json,
				    "condition_block") == NULL &&
			    json_object_size(branch_json) == 8U &&
			    json_is_array(columns_json) &&
			    json_array_size(columns_json) == 2U &&
			    json_is_array(rows_json) &&
			    json_array_size(rows_json) == 2U,
		    "single-arm MERGE View branch metadata mismatch") != 0) {
		goto fail;
	}
	if (when_index == 0U) {
		if (expect_true(
			    json_object_get(dml_json, "assignments") == NULL,
			    "single-arm MERGE View should omit assignments") != 0) {
			goto fail;
		}
	} else {
		json_t *assignment_json;
		json_t *assignments_json;
		json_t *branch_assignment_json;
		json_t *branch_assignments_json;
		json_t *update_branch_json;

		assignments_json =
			json_object_get(dml_json, "assignments");
		assignment_json = json_is_array(assignments_json) ?
			json_array_get(assignments_json, 0U) :
			NULL;
		update_branch_json = json_array_get(branches, 0U);
		branch_assignments_json =
			json_is_object(update_branch_json) ?
				json_object_get(
					update_branch_json,
					"assignments") :
				NULL;
		branch_assignment_json =
			json_is_array(branch_assignments_json) ?
				json_array_get(branch_assignments_json, 0U) :
				NULL;
		if (expect_true(
			    json_is_array(assignments_json) &&
				    json_array_size(assignments_json) == 1U &&
				    json_is_object(assignment_json) &&
				    json_object_size(assignment_json) == 4U &&
				    json_integer_is(
					    assignment_json,
					    "target_field",
					    (json_int_t)
						    assignment.target_field_index) &&
				    json_string_is(
					    assignment_json,
					    "kind",
					    "expression") &&
				    json_integer_is(
					    assignment_json,
					    "bind_kind",
					    SQLPARSER_BIND_KIND_NONE) &&
				    json_string_is(
					    assignment_json,
					    "selector",
					    "stmt[0].merge_assignment[0][0]"),
			    "preceding MERGE UPDATE View assignment mismatch") != 0 ||
		    expect_true(
			    json_is_object(update_branch_json) &&
				    json_integer_is(
					    update_branch_json,
					    "ordinal",
					    0) &&
				    json_string_is(
					    update_branch_json,
					    "merge_action_kind",
					    "update") &&
				    json_string_is(
					    update_branch_json,
					    "merge_match_kind",
					    "matched") &&
				    json_object_get(
					    update_branch_json,
					    "target_columns") == NULL &&
				    json_object_get(
					    update_branch_json,
					    "rows") == NULL &&
				    json_object_get(
					    update_branch_json,
					    "condition_selector") == NULL &&
				    json_is_array(branch_assignments_json) &&
				    json_array_size(
					    branch_assignments_json) == 1U &&
				    json_equal(
					    assignment_json,
					    branch_assignment_json),
			    "preceding MERGE UPDATE branch assignment mismatch") != 0) {
			goto fail;
		}
	}
	for (item_index = 0U; item_index < 2U; item_index++) {
		json_t *column_json;
		json_t *cell_json;

		column_json = json_array_get(columns_json, item_index);
		cell_json = json_array_get(rows_json, item_index);
		if (expect_true(
			    json_integer_is(
				    column_json,
				    "ordinal",
				    (json_int_t)item_index) &&
				    json_string_is(
					    column_json,
					    "column",
					    column_names[item_index]) &&
				    json_object_get(
					    column_json,
					    "selector") == NULL &&
				    json_object_size(column_json) == 2U,
			    "single-arm MERGE View column mismatch") != 0 ||
		    expect_true(
			    json_integer_is(
				    cell_json,
				    "row",
				    (json_int_t)when_index) &&
				    json_integer_is(
					    cell_json,
					    "column",
					    (json_int_t)item_index) &&
				    json_object_get(
					    cell_json,
					    "source_target") == NULL &&
				    json_object_get(
					    cell_json,
					    "source_field") == NULL &&
				    (item_index == 0U ||
				     json_object_get(
					     cell_json,
					     "selector") == NULL),
			    "single-arm MERGE View cell coordinates mismatch") != 0) {
			goto fail;
		}
		if (item_index == 0U) {
			if (expect_true(
				    json_object_size(cell_json) == 8U &&
					    json_string_is(
						    cell_json,
						    "kind",
						    "bind") &&
					    json_string_is(
						    cell_json,
						    "bind_key",
						    bind_key) &&
					    json_integer_is(
						    cell_json,
						    "bind_kind",
						    (json_int_t)bind_kind) &&
					    json_string_is(
						    cell_json,
						    "bind_sql",
						    bind_sql) &&
					    json_integer_is(
						    cell_json,
						    "bind_position",
						    1) &&
					    json_string_is(
						    cell_json,
						    "selector",
						    bind_selector_text) &&
					    json_object_get(
						    cell_json,
						    "expression_sql") == NULL,
				    "single-arm MERGE View bind mismatch") != 0) {
				goto fail;
			}
		} else if (expect_true(
				   json_object_size(cell_json) == 5U &&
					   json_string_is(
						   cell_json,
						   "kind",
						   "expression") &&
					   json_integer_is(
						   cell_json,
						   "bind_kind",
						   SQLPARSER_BIND_KIND_NONE) &&
					   json_object_get(
						   cell_json,
						   "bind_key") == NULL &&
					   json_object_get(
						   cell_json,
						   "bind_sql") == NULL &&
					   json_object_get(
						   cell_json,
						   "bind_position") == NULL &&
					   json_string_is(
						   cell_json,
						   "expression_sql",
						   expression_sql),
				   "single-arm MERGE View expression mismatch") != 0) {
			goto fail;
		}
	}

	json_decref(root);
	sqlparser_string_free(view_json);
	sqlparser_string_free(assignment_sql);
	sqlparser_string_free(bind_selector_text);
	sqlparser_string_free(condition);
	sqlparser_string_free(selector_text);
	sqlparser_handle_destroy(handle);
	return 0;

fail:
	if (root != NULL) {
		json_decref(root);
	}
	sqlparser_string_free(view_json);
	sqlparser_string_free(assignment_sql);
	sqlparser_string_free(bind_selector_text);
	sqlparser_string_free(condition);
	sqlparser_string_free(selector_text);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_merge_single_insert_branches(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		size_t when_index;
		const char *bind_key;
		sqlparser_bind_kind_t bind_kind;
		const char *bind_sql;
		const char *expression_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"MERGE INTO t USING s ON t.id=s.id "
			"WHEN NOT MATCHED AND  /*lead*/ s.flag  =  1 /*tail*/  THEN "
			"INSERT (id, created_at) VALUES ($1, CURRENT_TIMESTAMP)",
			0U,
			"1",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"$1",
			"CURRENT_TIMESTAMP"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"MERGE INTO dbo.t AS t USING dbo.s AS s ON t.id=s.id "
			"WHEN MATCHED THEN UPDATE SET t.created_at = CURRENT_TIMESTAMP "
			"WHEN NOT MATCHED AND  /*lead*/ s.flag  =  1 /*tail*/  THEN "
			"INSERT (id, created_at) VALUES (@id, CURRENT_TIMESTAMP);",
			1U,
			"id",
			SQLPARSER_BIND_KIND_NAMED,
			"@id",
			"CURRENT_TIMESTAMP"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"MERGE INTO t USING s ON (t.id=s.id) "
			"WHEN NOT MATCHED THEN INSERT (id, created_at) "
			"VALUES (:1, SySdAtE) WHERE  /*lead*/ s.flag  =  1 /*tail*/",
			0U,
			"1",
			SQLPARSER_BIND_KIND_POSITIONAL,
			":1",
			"SySdAtE"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"MERGE INTO t USING s ON t.id=s.id "
			"WHEN NOT MATCHED AND  /*lead*/ s.flag  =  1 /*tail*/  THEN "
			"INSERT (id, created_at) VALUES ($1, CURRENT_TIMESTAMP)",
			0U,
			"1",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"$1",
			"CURRENT_TIMESTAMP"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"MERGE INTO dbo.t AS t USING dbo.s AS s ON t.id=s.id "
			"WHEN NOT MATCHED AND  /*lead*/ s.flag  =  1 /*tail*/  THEN "
			"INSERT (id, created_at) VALUES (@id, CURRENT_TIMESTAMP);",
			0U,
			"id",
			SQLPARSER_BIND_KIND_NAMED,
			"@id",
			"CURRENT_TIMESTAMP"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"MERGE INTO t USING s ON (t.id=s.id) "
			"WHEN NOT MATCHED THEN INSERT (id, created_at) "
			"VALUES (:1, SySdAtE) WHERE  /*lead*/ s.flag  =  1 /*tail*/",
			0U,
			"1",
			SQLPARSER_BIND_KIND_POSITIONAL,
			":1",
			"SySdAtE"
		}
	};
	size_t index;

	for (index = 0U;
	     index < sizeof(cases) / sizeof(cases[0]);
	     index++) {
		if (test_merge_single_insert_branch_case(
			    cases[index].dialect,
			    cases[index].sql,
			    "/*lead*/ s.flag  =  1 /*tail*/",
			    cases[index].when_index,
			    cases[index].bind_key,
			    cases[index].bind_kind,
			    cases[index].bind_sql,
			    cases[index].expression_sql) != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_postgresql_merge_multiple_insert_branches(void)
{
	struct merge_cell_expectation {
		sqlparser_graph_value_kind_t kind;
		const char *bind_key;
		sqlparser_bind_kind_t bind_kind;
		const char *bind_sql;
		size_t bind_position;
		const char *expression_sql;
		const char *source_column;
	};
	static const char *const expected_conditions[] = {
		"/*c1*/ s.kind  =  1 /*t1*/",
		"/*c2*/ s.kind  =  2 /*t2*/"
	};
	static const char *const expected_columns[] = {
		"id",
		"a",
		"id",
		"b",
		"c"
	};
	static const size_t expected_column_counts[] = {
		2U,
		3U
	};
	static const struct merge_cell_expectation expected_cells[] = {
		{
			SQLPARSER_GRAPH_VALUE_FIELD,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			0U,
			NULL,
			"id"
		},
		{
			SQLPARSER_GRAPH_VALUE_EXPRESSION,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			0U,
			"CoAlEsCe($1,  1)",
			NULL
		},
		{
			SQLPARSER_GRAPH_VALUE_FIELD,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			0U,
			NULL,
			"id"
		},
		{
			SQLPARSER_GRAPH_VALUE_BIND,
			"2",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"$2",
			2U,
			NULL,
			NULL
		},
		{
			SQLPARSER_GRAPH_VALUE_EXPRESSION,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			0U,
			"CURRENT_TIMESTAMP",
			NULL
		}
	};
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	sqlparser_graph_dml_column_t column;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_field_t field;
	sqlparser_graph_target_t target;
	sqlparser_graph_relation_t relation;
	char *condition;
	char *view_json;
	json_error_t json_error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	json_t *query_graph;
	json_t *dml_json;
	json_t *branches_json;
	size_t source_field_indices[5];
	size_t source_target_indices[5];
	size_t branch_index;
	size_t column_base;
	size_t item_index;
	size_t local_index;
	int rc;

	sql =
		"MERGE INTO public.t AS t "
		"USING (SELECT id,kind FROM public.s) AS s ON t.id=s.id "
		"WHEN NOT MATCHED AND /*c1*/ s.kind  =  1 /*t1*/ THEN "
		"INSERT (id,a) VALUES (s.id,CoAlEsCe($1,  1)) "
		"WHEN NOT MATCHED AND /*c2*/ s.kind  =  2 /*t2*/ THEN "
		"INSERT (id,b,c) VALUES (s.id,$2,CURRENT_TIMESTAMP)";
	handle = NULL;
	condition = NULL;
	view_json = NULL;
	root = NULL;
	column_base = 0U;
	for (item_index = 0U;
	     item_index < sizeof(source_field_indices) /
		     sizeof(source_field_indices[0]);
	     item_index++) {
		source_field_indices[item_index] = (size_t)-1;
		source_target_indices[item_index] = (size_t)-1;
	}
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
	rc = sqlparser_parse_with_options(
		sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "PostgreSQL multi-arm MERGE should parse") != 0 ||
	    expect_deparse_equals_and_reparse(
		    handle,
		    sql,
		    "PostgreSQL multi-arm MERGE should deparse exactly and reparse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "PostgreSQL multi-arm MERGE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "PostgreSQL multi-arm MERGE DML should be available") != 0 ||
	    expect_true(
		    dml.kind == SQLPARSER_GRAPH_DML_MERGE &&
			    dml.has_target_relation != 0 &&
			    dml.target_columns.count == 0U &&
			    dml.rows.count == 0U &&
			    dml.branches.count == 2U,
		    "PostgreSQL multi-arm MERGE payload should be branch-scoped") != 0) {
		goto fail;
	}
	for (branch_index = 0U; branch_index < 2U; branch_index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    branch_index,
				    &local_index,
				    &error),
			    &error,
			    "PostgreSQL MERGE branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    local_index,
				    &branch,
				    &error),
			    &error,
			    "PostgreSQL MERGE branch should be available") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    local_index,
			    &branch_detail,
			    &error,
			    "PostgreSQL MERGE branch detail should be available") != 0 ||
		    expect_true(
			    branch.statement_index == 0U &&
				    branch.dml_index == dml.index &&
			    branch.ordinal == branch_index &&
				    branch.branch_kind ==
					    SQLPARSER_GRAPH_DML_BRANCH_WHEN &&
				    branch_detail.action_kind ==
					    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
				    branch_detail.match_kind ==
					    SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET &&
				    branch.has_target_relation != 0 &&
				    branch.target_relation_index ==
					    dml.target_relation_index &&
				    branch.target_columns.count ==
					    expected_column_counts[branch_index] &&
				    branch.rows.count ==
					    expected_column_counts[branch_index] &&
				    branch_detail.assignments.count == 0U &&
				    branch.has_condition_block == 0 &&
				    branch.has_condition_selector != 0 &&
				    branch.condition_selector.kind ==
					    SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
				    branch.condition_selector.statement_index ==
					    0U &&
				    branch.condition_selector.row_index == 0U &&
				    branch.condition_selector.item_index ==
					    branch_index,
			    "PostgreSQL MERGE branch metadata mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_selector_clause_sql(
				    handle,
				    &branch.condition_selector,
				    &condition,
				    &error),
			    &error,
			    "PostgreSQL MERGE condition should be available") != 0 ||
		    expect_true(
			    condition != NULL &&
				    strcmp(
					    condition,
					    expected_conditions[branch_index]) == 0,
			    "PostgreSQL MERGE condition source mismatch") != 0) {
			goto fail;
		}
		sqlparser_string_free(condition);
		condition = NULL;
		for (item_index = 0U;
		     item_index < expected_column_counts[branch_index];
		     item_index++) {
			const struct merge_cell_expectation *expected_cell;

			expected_cell =
				&expected_cells[column_base + item_index];
			if (expect_status_ok(
				    sqlparser_query_graph_span_index_at(
					    &graph,
					    branch.target_columns,
					    item_index,
					    &local_index,
					    &error),
				    &error,
				    "PostgreSQL MERGE column index should resolve") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_dml_column_at(
					    &graph,
					    local_index,
					    &column,
					    &error),
				    &error,
				    "PostgreSQL MERGE column should be available") != 0 ||
			    expect_true(
				    column.statement_index == 0U &&
					    column.dml_index == dml.index &&
					    column.ordinal == item_index &&
					    column.column_name != NULL &&
					    strcmp(
						    column.column_name,
						    expected_columns[
							    column_base +
							    item_index]) == 0,
				    "PostgreSQL MERGE column mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_span_index_at(
					    &graph,
					    branch.rows,
					    item_index,
					    &local_index,
					    &error),
				    &error,
				    "PostgreSQL MERGE cell index should resolve") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_dml_cell_at(
					    &graph,
					    local_index,
					    &cell,
					    &error),
				    &error,
				    "PostgreSQL MERGE cell should be available") != 0 ||
			    expect_true(
				    cell.statement_index == 0U &&
					    cell.dml_index == dml.index &&
					    cell.row_index == branch_index &&
					    cell.column_ordinal ==
						    item_index &&
					    cell.kind ==
						    expected_cell->kind &&
					    cell.has_selector ==
						    (expected_cell->kind ==
							     SQLPARSER_GRAPH_VALUE_BIND),
				    "PostgreSQL MERGE cell shape mismatch") != 0) {
				goto fail;
			}
			if (expected_cell->kind ==
			    SQLPARSER_GRAPH_VALUE_BIND) {
				if (expect_true(
					    cell.has_bind != 0 &&
						    strcmp(
							    cell.bind,
							    expected_cell->bind_key) ==
							    0 &&
						    cell.bind_kind ==
							    expected_cell->bind_kind &&
						    cell.has_bind_sql != 0 &&
						    strcmp(
							    cell.bind_sql,
							    expected_cell->bind_sql) ==
							    0 &&
						    cell.has_bind_position != 0 &&
						    cell.bind_position ==
							    expected_cell->bind_position &&
						    cell.selector.kind ==
							    SQLPARSER_SELECTOR_KIND_VALUE &&
						    cell.selector.statement_index ==
							    0U &&
						    cell.has_source_target == 0 &&
						    cell.has_source_field == 0,
					    "PostgreSQL MERGE direct bind metadata mismatch") != 0) {
					goto fail;
				}
			} else if (expected_cell->kind ==
				   SQLPARSER_GRAPH_VALUE_FIELD) {
				if (expect_true(
					    cell.has_bind == 0 &&
					    cell.bind_kind ==
						    SQLPARSER_BIND_KIND_NONE &&
						    cell.has_bind_sql == 0 &&
						    cell.has_bind_position == 0 &&
						    cell.has_source_target != 0 &&
						    cell.has_source_field != 0,
					    "PostgreSQL MERGE source field metadata mismatch") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_field_at(
						    &graph,
						    cell.source_field_index,
						    &field,
						    &error),
					    &error,
					    "PostgreSQL MERGE source field should resolve") != 0 ||
				    expect_true(
					    field.column_name != NULL &&
						    strcmp(
							    field.column_name,
							    expected_cell->source_column) ==
							    0 &&
						    field.has_relation != 0,
					    "PostgreSQL MERGE source field lineage mismatch") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_relation_at(
						    &graph,
						    field.relation_index,
						    &relation,
						    &error),
					    &error,
					    "PostgreSQL MERGE source relation should resolve") != 0 ||
				    expect_true(
					    relation.alias_name != NULL &&
						    strcmp(
							    relation.alias_name,
							    "s") == 0 &&
						    relation.has_source_block != 0,
					    "PostgreSQL MERGE source relation lineage mismatch") != 0) {
					goto fail;
				}
				if (expect_status_ok(
					    sqlparser_query_graph_target_at(
						    &graph,
						    cell.source_target_index,
						    &target,
						    &error),
					    &error,
					    "PostgreSQL MERGE source target should resolve") != 0 ||
				    expect_true(
					    target.kind ==
						    SQLPARSER_GRAPH_TARGET_FIELD &&
						    target.output_name != NULL &&
						    strcmp(
							    target.output_name,
							    expected_cell->source_column) ==
							    0 &&
						    target.has_field != 0,
					    "PostgreSQL MERGE source target lineage mismatch") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_field_at(
						    &graph,
						    target.field_index,
						    &field,
						    &error),
					    &error,
					    "PostgreSQL MERGE source target field should resolve") != 0 ||
				    expect_true(
					    field.column_name != NULL &&
						    strcmp(
							    field.column_name,
							    expected_cell->source_column) ==
							    0 &&
						    field.has_relation != 0,
					    "PostgreSQL MERGE source target field mismatch") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_relation_at(
						    &graph,
						    field.relation_index,
						    &relation,
						    &error),
					    &error,
					    "PostgreSQL MERGE source target relation should resolve") != 0 ||
				    expect_true(
					    relation.schema_name != NULL &&
						    strcmp(
							    relation.schema_name,
							    "public") == 0 &&
						    relation.object_name != NULL &&
						    strcmp(
							    relation.object_name,
							    "s") == 0,
					    "PostgreSQL MERGE source target relation mismatch") != 0) {
					goto fail;
				}
				source_field_indices[column_base + item_index] =
					cell.source_field_index;
				source_target_indices[column_base + item_index] =
					cell.source_target_index;
			} else if (expect_true(
					   cell.has_bind == 0 &&
						   cell.bind_kind ==
							   SQLPARSER_BIND_KIND_NONE &&
						   cell.has_bind_sql == 0 &&
						   cell.has_bind_position == 0 &&
						   cell.has_source_target == 0 &&
						   cell.has_source_field == 0,
					   "PostgreSQL MERGE expression metadata mismatch") != 0) {
				goto fail;
			}
		}
		column_base += expected_column_counts[branch_index];
	}
	if (expect_true(
		    source_target_indices[0] != (size_t)-1 &&
			    source_target_indices[0] ==
				    source_target_indices[2],
		    "PostgreSQL MERGE s.id cells should share one source target") != 0 ||
	    expect_true(
		    source_field_indices[0] != (size_t)-1 &&
			    source_field_indices[2] != (size_t)-1 &&
			    source_field_indices[0] !=
				    source_field_indices[2],
		    "PostgreSQL MERGE s.id occurrences should retain distinct source fields") != 0) {
		goto fail;
	}
	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "PostgreSQL multi-arm MERGE View should export") != 0) {
		goto fail;
	}
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, 0U) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml_json = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	branches_json = json_is_object(dml_json) ?
		json_object_get(dml_json, "branches") :
		NULL;
	if (expect_true(
		    json_is_object(dml_json) &&
			    json_string_is(dml_json, "kind", "merge") &&
			    json_string_is(
				    dml_json,
				    "insert_mode",
				    "unknown") &&
			    json_integer_is(
				    dml_json,
				    "target_relation",
				    (json_int_t)dml.target_relation_index) &&
			    json_object_get(dml_json, "target_columns") == NULL &&
			    json_object_get(dml_json, "rows") == NULL &&
			    json_is_array(branches_json) &&
			    json_array_size(branches_json) == 2U &&
			    json_object_size(dml_json) == 4U,
		    "PostgreSQL multi-arm MERGE View parent payload mismatch") != 0) {
		goto fail;
	}
	column_base = 0U;
	for (branch_index = 0U; branch_index < 2U; branch_index++) {
		json_t *branch_json;
		json_t *columns_json;
		json_t *rows_json;
		char expected_selector[64];

		branch_json = json_array_get(branches_json, branch_index);
		columns_json = json_is_object(branch_json) ?
			json_object_get(branch_json, "target_columns") :
			NULL;
		rows_json = json_is_object(branch_json) ?
			json_object_get(branch_json, "rows") :
			NULL;
		snprintf(
			expected_selector,
			sizeof(expected_selector),
			"stmt[0].merge_branch_condition[%zu]",
			branch_index);
		if (expect_true(
			    json_is_object(branch_json) &&
				    json_integer_is(
					    branch_json,
					    "ordinal",
					    (json_int_t)branch_index) &&
				    json_string_is(
					    branch_json,
					    "branch_kind",
					    "when") &&
				    json_string_is(
					    branch_json,
					    "merge_action_kind",
					    "insert") &&
				    json_string_is(
					    branch_json,
					    "merge_match_kind",
					    "not_matched_by_target") &&
				    json_integer_is(
					    branch_json,
					    "target_relation",
					    (json_int_t)dml.target_relation_index) &&
				    json_string_is(
					    branch_json,
					    "condition_selector",
					    expected_selector) &&
				    json_object_get(
					    branch_json,
					    "condition_block") == NULL &&
				    json_object_size(branch_json) == 8U &&
				    json_is_array(columns_json) &&
				    json_array_size(columns_json) ==
					    expected_column_counts[branch_index] &&
				    json_is_array(rows_json) &&
				    json_array_size(rows_json) ==
					    expected_column_counts[branch_index],
			    "PostgreSQL multi-arm MERGE View branch mismatch") != 0) {
			goto fail;
		}
		for (item_index = 0U;
		     item_index < expected_column_counts[branch_index];
		     item_index++) {
			const struct merge_cell_expectation *expected_cell;
			json_t *column_json;
			json_t *cell_json;

			expected_cell =
				&expected_cells[column_base + item_index];
			column_json =
				json_array_get(columns_json, item_index);
			cell_json = json_array_get(rows_json, item_index);
			if (expect_true(
				    json_integer_is(
					    column_json,
					    "ordinal",
					    (json_int_t)item_index) &&
					    json_string_is(
						    column_json,
						    "column",
						    expected_columns[
							    column_base +
							    item_index]) &&
					    json_object_get(
						    column_json,
						    "selector") == NULL &&
					    json_object_size(column_json) ==
						    2U,
				    "PostgreSQL multi-arm MERGE View column mismatch") != 0 ||
			    expect_true(
				    json_integer_is(
					    cell_json,
					    "row",
					    (json_int_t)branch_index) &&
					    json_integer_is(
						    cell_json,
						    "column",
						    (json_int_t)item_index) &&
					    json_string_is(
						    cell_json,
						    "kind",
						    sqlparser_graph_value_kind_name(
							    expected_cell->kind)) &&
					    json_integer_is(
						    cell_json,
						    "bind_kind",
						    (json_int_t)
							    expected_cell->bind_kind) &&
					    (expected_cell->kind ==
						     SQLPARSER_GRAPH_VALUE_BIND ?
						     json_string_is(
							     cell_json,
							     "selector",
							     "stmt[0].value[45]") :
						     json_object_get(
							     cell_json,
							     "selector") == NULL) &&
					    (expected_cell->kind ==
						     SQLPARSER_GRAPH_VALUE_FIELD ?
						     json_integer_is(
							     cell_json,
							     "source_target",
							     (json_int_t)
								     source_target_indices[
									     column_base +
									     item_index]) :
						     json_object_get(
							     cell_json,
							     "source_target") ==
							     NULL),
				    "PostgreSQL multi-arm MERGE View cell shape mismatch") != 0) {
				goto fail;
			}
			if (expected_cell->kind ==
			    SQLPARSER_GRAPH_VALUE_FIELD) {
				if (expect_true(
					    json_object_size(cell_json) == 6U &&
						    json_integer_is(
							    cell_json,
							    "source_field",
							    (json_int_t)
								    source_field_indices[
									    column_base +
									    item_index]) &&
						    json_object_get(
							    cell_json,
							    "bind_key") == NULL &&
						    json_object_get(
							    cell_json,
							    "bind_sql") == NULL &&
						    json_object_get(
							    cell_json,
							    "bind_position") == NULL &&
						    json_object_get(
							    cell_json,
							    "expression_sql") == NULL,
					    "PostgreSQL multi-arm MERGE View field lineage mismatch") != 0) {
					goto fail;
				}
			} else if (expected_cell->kind ==
				   SQLPARSER_GRAPH_VALUE_BIND) {
				if (expect_true(
					    json_object_size(cell_json) == 8U &&
						    json_object_get(
							    cell_json,
							    "source_field") == NULL &&
						    json_string_is(
							    cell_json,
							    "bind_key",
							    expected_cell->bind_key) &&
						    json_string_is(
							    cell_json,
							    "bind_sql",
							    expected_cell->bind_sql) &&
						    json_integer_is(
							    cell_json,
							    "bind_position",
							    (json_int_t)
								    expected_cell->bind_position) &&
						    json_object_get(
							    cell_json,
							    "expression_sql") == NULL,
					    "PostgreSQL multi-arm MERGE View bind mismatch") != 0) {
					goto fail;
				}
			} else if (expect_true(
					   json_object_size(cell_json) == 5U &&
						   json_object_get(
							   cell_json,
							   "source_field") == NULL &&
						   json_object_get(
							   cell_json,
							   "bind_key") == NULL &&
						   json_object_get(
							   cell_json,
							   "bind_sql") == NULL &&
						   json_object_get(
							   cell_json,
							   "bind_position") == NULL &&
						   json_string_is(
							   cell_json,
							   "expression_sql",
							   expected_cell->expression_sql),
					   "PostgreSQL multi-arm MERGE View expression mismatch") != 0) {
				goto fail;
			}
		}
		column_base += expected_column_counts[branch_index];
	}

	json_decref(root);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;

fail:
	if (root != NULL) {
		json_decref(root);
	}
	sqlparser_string_free(view_json);
	sqlparser_string_free(condition);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_postgresql_merge_complete_action_branches(void)
{
	static const sqlparser_graph_merge_action_kind_t expected_actions[] = {
		SQLPARSER_GRAPH_MERGE_ACTION_UPDATE,
		SQLPARSER_GRAPH_MERGE_ACTION_DELETE,
		SQLPARSER_GRAPH_MERGE_ACTION_INSERT,
		SQLPARSER_GRAPH_MERGE_ACTION_NOTHING
	};
	static const sqlparser_graph_merge_match_kind_t expected_matches[] = {
		SQLPARSER_GRAPH_MERGE_MATCH_MATCHED,
		SQLPARSER_GRAPH_MERGE_MATCH_MATCHED,
		SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET,
		SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET
	};
	static const char *const expected_action_names[] = {
		"update",
		"delete",
		"insert",
		"nothing"
	};
	static const char *const expected_match_names[] = {
		"matched",
		"matched",
		"not_matched_by_target",
		"not_matched_by_target"
	};
	static const char *const expected_conditions[] = {
		"t.balance > s.delta",
		NULL,
		"s.delta > 0",
		NULL
	};
	static const char sql[] =
		"MERGE INTO target AS t USING source AS s ON t.tid = s.sid "
		"WHEN MATCHED AND t.balance > s.delta THEN "
		"UPDATE SET balance = t.balance - s.delta "
		"WHEN MATCHED THEN DELETE "
		"WHEN NOT MATCHED AND s.delta > 0 THEN "
		"INSERT VALUES (s.sid, s.delta) "
		"WHEN NOT MATCHED THEN DO NOTHING;";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	char *condition;
	char *view_json;
	json_error_t json_error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	json_t *query_graph;
	json_t *dml_json;
	json_t *branches_json;
	json_t *parent_assignments_json;
	size_t assignment_index;
	size_t branch_assignment_index;
	size_t branch_index;
	size_t local_index;
	int rc;
	int result;

	handle = NULL;
	condition = NULL;
	view_json = NULL;
	root = NULL;
	result = 1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
	rc = sqlparser_parse_with_options(
		sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "official four-action PostgreSQL MERGE should parse") != 0 ||
	    expect_deparse_equals_and_reparse(
		    handle,
		    sql,
		    "official four-action PostgreSQL MERGE should deparse exactly") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "official four-action PostgreSQL MERGE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "official four-action PostgreSQL MERGE DML should resolve") != 0 ||
	    expect_true(
		    dml.kind == SQLPARSER_GRAPH_DML_MERGE &&
			    dml.branches.count == 4U &&
			    dml.assignments.count == 1U &&
			    dml.target_columns.count == 0U &&
			    dml.rows.count == 0U,
		    "official four-action MERGE parent payload mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.assignments,
			    0U,
			    &assignment_index,
			    &error),
		    &error,
		    "official four-action parent assignment should resolve") != 0) {
		goto done;
	}

	for (branch_index = 0U; branch_index < 4U; branch_index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    branch_index,
				    &local_index,
				    &error),
			    &error,
			    "official MERGE branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    local_index,
				    &branch,
				    &error),
			    &error,
			    "official MERGE branch should resolve") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    local_index,
			    &branch_detail,
			    &error,
			    "official MERGE branch detail should resolve") != 0 ||
		    expect_true(
			    branch.ordinal == branch_index &&
				    branch.branch_kind ==
					    SQLPARSER_GRAPH_DML_BRANCH_WHEN &&
				    branch_detail.action_kind ==
					    expected_actions[branch_index] &&
				    branch_detail.match_kind ==
					    expected_matches[branch_index] &&
				    strcmp(
					    sqlparser_graph_merge_action_kind_name(
						    branch_detail.action_kind),
					    expected_action_names[branch_index]) == 0 &&
				    strcmp(
					    sqlparser_graph_merge_match_kind_name(
						    branch_detail.match_kind),
					    expected_match_names[branch_index]) == 0 &&
				    branch.has_target_relation != 0 &&
				    branch.target_relation_index ==
					    dml.target_relation_index,
			    "official MERGE branch action or match mismatch") != 0) {
			goto done;
		}
		if (branch_index == 0U) {
			if (expect_true(
				    branch_detail.assignments.count == 1U &&
					    branch.target_columns.count == 0U &&
					    branch.rows.count == 0U,
				    "official MERGE UPDATE payload mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_span_index_at(
					    &graph,
					    branch_detail.assignments,
					    0U,
					    &branch_assignment_index,
					    &error),
				    &error,
				    "official MERGE UPDATE branch assignment should resolve") != 0 ||
			    expect_true(
				    branch_assignment_index == assignment_index,
				    "official MERGE UPDATE branch must reference the parent assignment") != 0) {
				goto done;
			}
		} else if (branch_index == 2U) {
			size_t cell_offset;

			if (expect_true(
				    branch_detail.assignments.count == 0U &&
					    branch.target_columns.count == 0U &&
					    branch.rows.count == 2U,
				    "official MERGE INSERT payload mismatch") != 0) {
				goto done;
			}
			for (cell_offset = 0U; cell_offset < 2U; cell_offset++) {
				sqlparser_graph_dml_cell_t cell;

				if (expect_status_ok(
					    sqlparser_query_graph_span_index_at(
						    &graph,
						    branch.rows,
						    cell_offset,
						    &local_index,
						    &error),
					    &error,
					    "official MERGE INSERT cell index should resolve") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_dml_cell_at(
						    &graph,
						    local_index,
						    &cell,
						    &error),
					    &error,
					    "official MERGE INSERT cell should resolve") != 0 ||
				    expect_true(
					    cell.row_index == 2U &&
						    cell.column_ordinal ==
							    cell_offset,
					    "official MERGE INSERT cell coordinates must use absolute WHEN ordinal") != 0) {
					goto done;
				}
			}
		} else if (expect_true(
				   branch_detail.assignments.count == 0U &&
					   branch.target_columns.count == 0U &&
					   branch.rows.count == 0U,
				   "MERGE DELETE/NOTHING branches must not expose payload") != 0) {
			goto done;
		}
		if (expected_conditions[branch_index] != NULL) {
			if (expect_true(
				    branch.has_condition_selector != 0 &&
					    branch.condition_selector.row_index ==
						    0U &&
					    branch.condition_selector.item_index ==
						    branch_index,
				    "official MERGE conditioned branch selector mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_selector_clause_sql(
					    handle,
					    &branch.condition_selector,
					    &condition,
					    &error),
				    &error,
				    "official MERGE condition SQL should resolve") != 0 ||
			    expect_true(
				    condition != NULL &&
					    strcmp(
						    condition,
						    expected_conditions[
							    branch_index]) == 0,
				    "official MERGE condition SQL mismatch") != 0) {
				goto done;
			}
			sqlparser_string_free(condition);
			condition = NULL;
		} else if (expect_true(
				   branch.has_condition_selector == 0,
				   "unconditional MERGE branch must not expose a condition selector") != 0) {
			goto done;
		}
	}

	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "official four-action MERGE View JSON should export") != 0) {
		goto done;
	}
	memset(&json_error, 0, sizeof(json_error));
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, 0U) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml_json = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	branches_json = json_is_object(dml_json) ?
		json_object_get(dml_json, "branches") :
		NULL;
	parent_assignments_json = json_is_object(dml_json) ?
		json_object_get(dml_json, "assignments") :
		NULL;
	if (expect_true(
		    json_is_array(branches_json) &&
			    json_array_size(branches_json) == 4U &&
			    json_is_array(parent_assignments_json) &&
			    json_array_size(parent_assignments_json) == 1U,
		    "official four-action MERGE JSON parent payload mismatch") != 0) {
		goto done;
	}
	for (branch_index = 0U; branch_index < 4U; branch_index++) {
		json_t *branch_json;
		json_t *branch_assignments_json;
		json_t *rows_json;

		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    branch_index,
				    &local_index,
				    &error),
			    &error,
			    "official MERGE JSON branch index should resolve") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    local_index,
			    &branch_detail,
			    &error,
			    "official MERGE JSON branch detail should resolve") != 0) {
			goto done;
		}
		branch_json = json_array_get(branches_json, branch_index);
		branch_assignments_json =
			json_is_object(branch_json) ?
				json_object_get(branch_json, "assignments") :
				NULL;
		rows_json = json_is_object(branch_json) ?
			json_object_get(branch_json, "rows") :
			NULL;
		if (expect_true(
			    json_is_object(branch_json) &&
				    json_integer_is(
					    branch_json,
					    "ordinal",
					    (json_int_t)branch_index) &&
				    json_string_is(
					    branch_json,
					    "merge_action_kind",
					    sqlparser_graph_merge_action_kind_name(
						    branch_detail.action_kind)) &&
				    json_string_is(
					    branch_json,
					    "merge_match_kind",
					    sqlparser_graph_merge_match_kind_name(
						    branch_detail.match_kind)) &&
				    ((branch_detail.assignments.count == 0U &&
				      branch_assignments_json == NULL) ||
				     (json_is_array(branch_assignments_json) &&
				      json_array_size(
					      branch_assignments_json) ==
					      branch_detail.assignments.count)),
			    "official MERGE JSON action or match mismatch") != 0) {
			goto done;
		}
		if (branch_index == 0U) {
			if (expect_true(
				    json_is_array(branch_assignments_json) &&
					    json_array_size(
						    branch_assignments_json) == 1U &&
					    json_equal(
						    json_array_get(
							    parent_assignments_json,
							    0U),
						    json_array_get(
							    branch_assignments_json,
							    0U)) &&
					    rows_json == NULL,
				    "official MERGE UPDATE JSON payload mismatch") != 0) {
				goto done;
			}
		} else if (branch_index == 2U) {
			if (expect_true(
				    branch_assignments_json == NULL &&
					    json_is_array(rows_json) &&
					    json_array_size(rows_json) == 2U &&
					    json_integer_is(
						    json_array_get(rows_json, 0U),
						    "row",
						    2) &&
					    json_integer_is(
						    json_array_get(rows_json, 1U),
						    "row",
						    2),
				    "official MERGE INSERT JSON payload mismatch") != 0) {
				goto done;
			}
		} else if (expect_true(
				   branch_assignments_json == NULL &&
					   rows_json == NULL &&
					   json_object_get(
						   branch_json,
						   "target_columns") == NULL,
				   "MERGE DELETE/NOTHING JSON branches must not expose payload") != 0) {
			goto done;
		}
	}
	result = 0;

done:
	if (root != NULL) {
		json_decref(root);
	}
	sqlparser_string_free(view_json);
	sqlparser_string_free(condition);
	sqlparser_handle_destroy(handle);
	return result;
}

static int test_merge_branch_detail_api_contract(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_merge_action_kind_t action_kind;
	sqlparser_graph_merge_match_kind_t match_kind;
	sqlparser_index_span_t assignments;
	size_t branch_index;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
	rc = sqlparser_parse_with_options(
		"MERGE INTO t USING s ON t.id=s.id "
		"WHEN MATCHED THEN UPDATE SET v=s.v",
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "MERGE branch detail contract SQL should parse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "MERGE branch detail contract graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "MERGE branch detail contract DML should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.branches,
			    0U,
			    &branch_index,
			    &error),
		    &error,
		    "MERGE branch detail contract branch should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_merge_branch_detail(
			    &graph,
			    branch_index,
			    &action_kind,
			    &match_kind,
			    &assignments,
			    &error),
		    &error,
		    "MERGE branch detail getter should succeed") != 0 ||
	    expect_true(
		    action_kind == SQLPARSER_GRAPH_MERGE_ACTION_UPDATE &&
			    match_kind == SQLPARSER_GRAPH_MERGE_MATCH_MATCHED &&
			    assignments.count == 1U,
		    "MERGE branch detail getter payload mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	match_kind = SQLPARSER_GRAPH_MERGE_MATCH_MATCHED;
	assignments.offset = 1U;
	assignments.count = 1U;
	rc = sqlparser_query_graph_merge_branch_detail(
		&graph,
		branch_index,
		NULL,
		&match_kind,
		&assignments,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT &&
			    match_kind ==
				    SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN &&
			    assignments.offset == 0U &&
			    assignments.count == 0U,
		    "MERGE branch detail NULL action output contract mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	action_kind = SQLPARSER_GRAPH_MERGE_ACTION_UPDATE;
	assignments.offset = 1U;
	assignments.count = 1U;
	rc = sqlparser_query_graph_merge_branch_detail(
		&graph,
		branch_index,
		&action_kind,
		NULL,
		&assignments,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT &&
			    action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN &&
			    assignments.offset == 0U &&
			    assignments.count == 0U,
		    "MERGE branch detail NULL match output contract mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	action_kind = SQLPARSER_GRAPH_MERGE_ACTION_UPDATE;
	match_kind = SQLPARSER_GRAPH_MERGE_MATCH_MATCHED;
	rc = sqlparser_query_graph_merge_branch_detail(
		&graph,
		branch_index,
		&action_kind,
		&match_kind,
		NULL,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT &&
			    action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN &&
			    match_kind ==
				    SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN,
		    "MERGE branch detail NULL assignments output contract mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	action_kind = SQLPARSER_GRAPH_MERGE_ACTION_UPDATE;
	match_kind = SQLPARSER_GRAPH_MERGE_MATCH_MATCHED;
	assignments.offset = 1U;
	assignments.count = 1U;
	rc = sqlparser_query_graph_merge_branch_detail(
		&graph,
		graph.dml_branch_count,
		&action_kind,
		&match_kind,
		&assignments,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT &&
			    action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN &&
			    match_kind ==
				    SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN &&
			    assignments.offset == 0U &&
			    assignments.count == 0U,
		    "MERGE branch detail out-of-range contract mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	action_kind = SQLPARSER_GRAPH_MERGE_ACTION_UPDATE;
	match_kind = SQLPARSER_GRAPH_MERGE_MATCH_MATCHED;
	assignments.offset = 1U;
	assignments.count = 1U;
	rc = sqlparser_query_graph_merge_branch_detail(
		NULL,
		0U,
		&action_kind,
		&match_kind,
		&assignments,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT &&
			    action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN &&
			    match_kind ==
				    SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN &&
			    assignments.offset == 0U &&
			    assignments.count == 0U,
		    "MERGE branch detail NULL graph contract mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(
		"INSERT ALL "
		"INTO users (id) VALUES (1) "
		"INTO users (id) VALUES (2) "
		"SELECT 1 FROM dual",
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "non-MERGE branch detail contract SQL should parse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "non-MERGE branch detail contract graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "non-MERGE branch detail contract DML should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.branches,
			    0U,
			    &branch_index,
			    &error),
		    &error,
		    "non-MERGE branch detail contract branch should resolve") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	action_kind = SQLPARSER_GRAPH_MERGE_ACTION_UPDATE;
	match_kind = SQLPARSER_GRAPH_MERGE_MATCH_MATCHED;
	assignments.offset = 1U;
	assignments.count = 1U;
	rc = sqlparser_query_graph_merge_branch_detail(
		&graph,
		branch_index,
		&action_kind,
		&match_kind,
		&assignments,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_UNSUPPORTED &&
			    action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_UNKNOWN &&
			    match_kind ==
				    SQLPARSER_GRAPH_MERGE_MATCH_UNKNOWN &&
			    assignments.offset == 0U &&
			    assignments.count == 0U,
		    "non-MERGE branch detail contract mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_merge_condition_scanner_boundaries(void)
{
	static const char postgresql_sql[] =
		"WITH src AS (SELECT 1 AS merge, 1 AS id, 1 AS flag) "
		"MERGE INTO public.t AS t USING src AS s ON t.id=s.id "
		"WHEN MATCHED AND "
		"/* WHEN MATCHED THEN */ "
		"(CASE WHEN s.flag = 1 THEN true ELSE false END) THEN "
		"UPDATE SET id=s.id "
		"WHEN NOT MATCHED AND s.flag=2 THEN "
		"INSERT(id) VALUES(s.id)";
	static const char *const expected_postgresql_conditions[] = {
		"/* WHEN MATCHED THEN */ "
		"(CASE WHEN s.flag = 1 THEN true ELSE false END)",
		"s.flag=2"
	};
	static const char oracle_sql[] =
		"MERGE INTO t USING s ON (t.id=s.id) "
		"WHEN MATCHED THEN UPDATE SET v=s.v "
		"WHEN NOT MATCHED THEN INSERT(id) VALUES(s.id) "
		"WHERE s.flag=1";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_selector_t selector;
	char *condition;
	char *view_json;
	size_t branch_index;
	size_t local_index;
	int rc;

	handle = NULL;
	condition = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
	rc = sqlparser_parse_with_options(
		postgresql_sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "MERGE condition lexical-boundary SQL should parse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "MERGE condition lexical-boundary graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "MERGE condition lexical-boundary DML should resolve") != 0 ||
	    expect_true(
		    dml.branches.count == 2U,
		    "MERGE condition lexical-boundary branch count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (branch_index = 0U; branch_index < 2U; branch_index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    branch_index,
				    &local_index,
				    &error),
			    &error,
			    "MERGE lexical-boundary branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    local_index,
				    &branch,
				    &error),
			    &error,
			    "MERGE lexical-boundary branch should resolve") != 0 ||
		    expect_true(
			    branch.ordinal == branch_index &&
				    branch.has_condition_selector != 0 &&
				    branch.condition_selector.row_index == 0U &&
				    branch.condition_selector.item_index ==
					    branch_index,
			    "MERGE lexical-boundary selector mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_selector_clause_sql(
				    handle,
				    &branch.condition_selector,
				    &condition,
				    &error),
			    &error,
			    "MERGE lexical-boundary condition should resolve") != 0 ||
		    expect_true(
			    condition != NULL &&
				    strcmp(
					    condition,
					    expected_postgresql_conditions[
						    branch_index]) == 0,
			    "MERGE lexical-boundary condition source mismatch") != 0) {
			sqlparser_string_free(condition);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(condition);
		condition = NULL;
	}
	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "MERGE lexical-boundary View JSON should export") != 0 ||
	    expect_true(
		    view_json != NULL &&
			    strstr(
				    view_json,
				    "\"condition_selector\":\"stmt[0].merge_branch_condition[0]\"") != NULL &&
			    strstr(
				    view_json,
				    "\"condition_selector\":\"stmt[0].merge_branch_condition[1]\"") != NULL,
		    "MERGE lexical-boundary JSON selectors mismatch") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(
		oracle_sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle MERGE action-WHERE boundary SQL should parse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "Oracle MERGE action-WHERE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "Oracle MERGE action-WHERE DML should resolve") != 0 ||
	    expect_true(
		    dml.branches.count == 2U,
		    "Oracle MERGE action-WHERE branch count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&selector, 0, sizeof(selector));
	selector.kind =
		SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION;
	selector.statement_index = 0U;
	selector.item_index = 0U;
	condition = NULL;
	rc = sqlparser_selector_clause_sql(
		handle,
		&selector,
		&condition,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT &&
			    condition == NULL,
		    "unconditional W0 must not read W1 action-WHERE condition") != 0) {
		sqlparser_string_free(condition);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	selector.item_index = 1U;
	rc = sqlparser_selector_clause_sql(
		handle,
		&selector,
		&condition,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle W1 action-WHERE condition should resolve") != 0 ||
	    expect_true(
		    condition != NULL &&
			    strcmp(condition, "s.flag=1") == 0,
		    "Oracle W1 action-WHERE condition mismatch") != 0) {
		sqlparser_string_free(condition);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(condition);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_oracle_merge_action_where_after_patch(void)
{
	static const sqlparser_dialect_t rejected_dialects[] = {
		SQLPARSER_DIALECT_POSTGRESQL,
		SQLPARSER_DIALECT_MYSQL,
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		SQLPARSER_DIALECT_VASTBASE_MYSQL,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	static const char sql[] =
		"MERGE INTO t USING s ON (t.id=s.id) "
		"WHEN MATCHED AND (s.a=1 OR s.b=1) AND s.c=1 THEN "
		"UPDATE SET t.v=s.v WHERE s.flag=1 "
		"WHEN NOT MATCHED THEN "
		"INSERT (id,v) VALUES(s.id,s.v) WHERE s.flag=2";
	static const char expected[] =
		"MERGE INTO t USING s ON t.id = s.id "
		"WHEN MATCHED AND (s.a_changed = 1 OR s.b = 1) AND s.c = 1 THEN "
		"UPDATE SET t.v = s.v WHERE s.flag = 1 "
		"WHEN NOT MATCHED THEN "
		"INSERT (id, v) VALUES (s.id, s.v) WHERE s.flag = 2";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patches;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_selector_t selector;
	char *condition;
	char *deparsed;
	size_t dialect_index;
	size_t name_index;
	int rc;

	handle = NULL;
	reparsed = NULL;
	condition = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	memset(&selector, 0, sizeof(selector));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle MERGE action-WHERE patch SQL should parse") != 0) {
		return 1;
	}
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].merge_assignment[0][0]";
	patch.sql = "s.v";
	patches.items = &patch;
	patches.count = 1U;
	rc = sqlparser_apply_patch(handle, &patches, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle MERGE action-WHERE assignment patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (find_name_index(
		    handle,
		    0U,
		    "ColumnRef",
		    "fields",
		    "a",
		    &name_index) != 0 ||
	    expect_status_ok(
		    sqlparser_statement_set_name(
			    handle,
			    0U,
			    name_index,
			    "a_changed",
			    &error),
		    &error,
		    "Oracle MERGE condition name patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	selector.kind =
		SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION;
	selector.statement_index = 0U;
	selector.item_index = 0U;
	rc = sqlparser_selector_clause_sql(
		handle,
		&selector,
		&condition,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "same-handle patched Oracle MERGE condition should resolve") != 0 ||
	    expect_true(
		    condition != NULL &&
			    strcmp(
				    condition,
				    "(s.a_changed = 1 OR s.b = 1) AND s.c = 1") == 0,
		    "same-handle patched Oracle MERGE condition is stale") != 0) {
		sqlparser_string_free(condition);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(condition);
	condition = NULL;
	selector.item_index = 1U;
	rc = sqlparser_selector_clause_sql(
		handle,
		&selector,
		&condition,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "same-handle patched Oracle MERGE action condition should resolve") != 0 ||
	    expect_true(
		    condition != NULL &&
			    strcmp(condition, "s.flag = 2") == 0,
		    "same-handle patched Oracle MERGE action condition is stale") != 0) {
		sqlparser_string_free(condition);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(condition);
	condition = NULL;
	if (expect_status_ok(
		    sqlparser_deparse(handle, &deparsed, &error),
		    &error,
		    "patched Oracle MERGE action-WHERE should deparse") != 0 ||
	    expect_true(
		    deparsed != NULL && strcmp(deparsed, expected) == 0,
		    "patched Oracle MERGE action-WHERE deparse mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_parse_with_options(
			    deparsed,
			    &options,
			    &reparsed,
			    &error),
		    &error,
		    "patched Oracle MERGE action-WHERE should reparse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    reparsed,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "reparsed Oracle MERGE action-WHERE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "reparsed Oracle MERGE action-WHERE DML should resolve") != 0 ||
	    expect_true(
		    dml.assignments.count == 1U &&
			    dml.branches.count == 2U,
		    "reparsed Oracle MERGE action-WHERE graph mismatch") != 0) {
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(reparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	selector.kind =
		SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION;
	selector.statement_index = 0U;
	selector.item_index = 0U;
	rc = sqlparser_selector_clause_sql(
		reparsed,
		&selector,
		&condition,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "reparsed Oracle MERGE match condition should resolve") != 0 ||
	    expect_true(
		    condition != NULL &&
			    strcmp(
				    condition,
				    "(s.a_changed = 1 OR s.b = 1) AND s.c = 1") == 0,
		    "reparsed Oracle MERGE match condition mismatch") != 0) {
		sqlparser_string_free(condition);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(reparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(condition);
	condition = NULL;
	selector.item_index = 1U;
	rc = sqlparser_selector_clause_sql(
		reparsed,
		&selector,
		&condition,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "reparsed Oracle MERGE insert action condition should resolve") != 0 ||
	    expect_true(
		    condition != NULL &&
			    strcmp(condition, "s.flag = 2") == 0,
		    "reparsed Oracle MERGE insert action condition mismatch") != 0) {
		sqlparser_string_free(condition);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(reparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(condition);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(reparsed);
	sqlparser_handle_destroy(handle);

	for (dialect_index = 0U;
	     dialect_index <
		     sizeof(rejected_dialects) /
			     sizeof(rejected_dialects[0]);
	     dialect_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = rejected_dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			"MERGE INTO t USING s ON t.id=s.id "
			"WHEN MATCHED THEN UPDATE SET v=s.v WHERE s.flag=1",
			&options,
			&handle,
			&error);
		if (expect_true(
			    rc != SQLPARSER_STATUS_OK && handle == NULL,
			    "non-Oracle MERGE action WHERE must be rejected") ==
		    0) {
			continue;
		}
		sqlparser_handle_destroy(handle);
		return 1;
	}
	return 0;
}

static int test_postgresql_merge_by_source_match_branch(void)
{
	static const char sql[] =
		"MERGE INTO wines w USING new_wine_list s "
		"ON s.winename = w.winename "
		"WHEN NOT MATCHED BY TARGET THEN "
		"INSERT VALUES(s.winename, s.stock) "
		"WHEN MATCHED AND w.stock != s.stock THEN "
		"UPDATE SET stock = s.stock "
		"WHEN NOT MATCHED BY SOURCE THEN DELETE;";
	static const sqlparser_graph_merge_action_kind_t expected_actions[] = {
		SQLPARSER_GRAPH_MERGE_ACTION_INSERT,
		SQLPARSER_GRAPH_MERGE_ACTION_UPDATE,
		SQLPARSER_GRAPH_MERGE_ACTION_DELETE
	};
	static const sqlparser_graph_merge_match_kind_t expected_matches[] = {
		SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_TARGET,
		SQLPARSER_GRAPH_MERGE_MATCH_MATCHED,
		SQLPARSER_GRAPH_MERGE_MATCH_NOT_MATCHED_BY_SOURCE
	};
	static const char *const expected_action_names[] = {
		"insert",
		"update",
		"delete"
	};
	static const char *const expected_match_names[] = {
		"not_matched_by_target",
		"matched",
		"not_matched_by_source"
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	char *condition;
	char *view_json;
	json_error_t json_error;
	json_t *root;
	json_t *branches_json;
	json_t *dml_json;
	json_t *query_graph;
	json_t *statement;
	json_t *statements;
	size_t branch_index;
	size_t local_index;
	int rc;
	int result;

	handle = NULL;
	condition = NULL;
	view_json = NULL;
	root = NULL;
	result = 1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
	rc = sqlparser_parse_with_options(
		sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "official PostgreSQL BY SOURCE MERGE should parse") != 0 ||
	    expect_deparse_equals_and_reparse(
		    handle,
		    sql,
		    "official PostgreSQL BY SOURCE MERGE should deparse exactly") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "official PostgreSQL BY SOURCE MERGE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &graph,
			    &dml,
			    &error),
		    &error,
		    "official PostgreSQL BY SOURCE MERGE DML should resolve") != 0 ||
	    expect_true(
		    dml.branches.count == 3U &&
			    dml.assignments.count == 1U,
		    "official PostgreSQL BY SOURCE MERGE payload mismatch") != 0) {
		goto done;
	}
	for (branch_index = 0U; branch_index < 3U; branch_index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    branch_index,
				    &local_index,
				    &error),
			    &error,
			    "BY SOURCE MERGE branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    local_index,
				    &branch,
				    &error),
			    &error,
			    "BY SOURCE MERGE branch should resolve") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    local_index,
			    &branch_detail,
			    &error,
			    "BY SOURCE MERGE branch detail should resolve") != 0 ||
		    expect_true(
			    branch.ordinal == branch_index &&
				    branch_detail.action_kind ==
					    expected_actions[branch_index] &&
				    branch_detail.match_kind ==
					    expected_matches[branch_index],
			    "BY SOURCE MERGE branch action or match mismatch") != 0) {
			goto done;
		}
		if (branch_index == 0U) {
			if (expect_true(
				    branch.rows.count == 2U &&
					    branch_detail.assignments.count == 0U,
				    "BY SOURCE MERGE INSERT payload mismatch") != 0) {
				goto done;
			}
		} else if (branch_index == 1U) {
			if (expect_true(
				    branch.rows.count == 0U &&
					    branch_detail.assignments.count == 1U &&
					    branch.has_condition_selector != 0,
				    "BY SOURCE MERGE UPDATE payload mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_selector_clause_sql(
					    handle,
					    &branch.condition_selector,
					    &condition,
					    &error),
				    &error,
				    "BY SOURCE MERGE UPDATE condition should resolve") != 0 ||
			    expect_true(
				    strcmp(
					    condition,
					    "w.stock != s.stock") == 0,
				    "BY SOURCE MERGE UPDATE condition mismatch") != 0) {
				goto done;
			}
			sqlparser_string_free(condition);
			condition = NULL;
		} else if (expect_true(
				   branch.rows.count == 0U &&
					   branch_detail.assignments.count == 0U &&
					   branch.has_condition_selector == 0,
				   "BY SOURCE MERGE DELETE payload mismatch") != 0) {
			goto done;
		}
	}
	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "BY SOURCE MERGE View JSON should export") != 0) {
		goto done;
	}
	memset(&json_error, 0, sizeof(json_error));
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, 0U) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml_json = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	branches_json = json_is_object(dml_json) ?
		json_object_get(dml_json, "branches") :
		NULL;
	if (expect_true(
		    json_is_array(branches_json) &&
			    json_array_size(branches_json) == 3U,
		    "BY SOURCE MERGE JSON branch count mismatch") != 0) {
		goto done;
	}
	for (branch_index = 0U; branch_index < 3U; branch_index++) {
		json_t *branch_json;

		branch_json = json_array_get(
			branches_json,
			branch_index);
		if (expect_true(
			    json_is_object(branch_json) &&
				    json_string_is(
					    branch_json,
					    "merge_action_kind",
					    expected_action_names[
						    branch_index]) &&
				    json_string_is(
					    branch_json,
					    "merge_match_kind",
					    expected_match_names[
						    branch_index]),
			    "BY SOURCE MERGE JSON action or match mismatch") != 0) {
			goto done;
		}
	}
	result = 0;

done:
	if (root != NULL) {
		json_decref(root);
	}
	sqlparser_string_free(view_json);
	sqlparser_string_free(condition);
	sqlparser_handle_destroy(handle);
	return result;
}

static int test_sqlserver_multiple_nested_merge_selectors(void)
{
	static const char sql[] =
		"INSERT INTO dbo.MergeAudit(id1, id2) "
		"SELECT a.id, b.id "
		"FROM (MERGE dbo.T1 AS t1 "
		"USING (SELECT @id1 AS id, @v1 AS v, @f1 AS flag) AS s1 "
		"ON t1.id=s1.id "
		"WHEN MATCHED THEN UPDATE SET v=s1.v "
		"WHEN NOT MATCHED AND s1.flag=1 THEN "
		"INSERT(id,v) VALUES(s1.id,s1.v) "
		"OUTPUT INSERTED.id) AS a "
		"CROSS JOIN (MERGE dbo.T2 AS t2 "
		"USING (SELECT @id2 AS id, @v2 AS v, @f2 AS flag) AS s2 "
		"ON t2.id=s2.id "
		"WHEN MATCHED THEN UPDATE SET v=s2.v "
		"WHEN NOT MATCHED AND s2.flag=2 THEN "
		"INSERT(id,v) VALUES(s2.id,s2.v) "
		"OUTPUT INSERTED.id) AS b";
	static const char *const expected_assignment_selectors[] = {
		"stmt[0].merge_assignment[1][0][0]",
		"stmt[0].merge_assignment[2][0][0]"
	};
	static const char *const expected_condition_selectors[] = {
		"stmt[0].merge_branch_condition[1][1]",
		"stmt[0].merge_branch_condition[2][1]"
	};
	static const char *const expected_assignment_sql[] = {
		"s1.v",
		"s2.v"
	};
	static const char *const expected_condition_sql[] = {
		"s1.flag=1",
		"s2.flag=2"
	};
	static const char *const expected_reparsed_condition_sql[] = {
		"s1.flag = 1",
		"s2.flag = 2"
	};
	static const char *const expected_aliases[] = {
		"s1",
		"s2"
	};
	static const char *const expected_bind_sql[] = {
		"@id1",
		"@id2"
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_field_t field;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_target_t target;
	sqlparser_graph_value_t value;
	sqlparser_selector_t assignment_selectors[2];
	sqlparser_selector_t condition_selectors[2];
	char *assignment_sql;
	char *condition_sql;
	char *deparsed;
	char *selector_text;
	char *view_json;
	size_t assignment_index;
	size_t branch_index;
	size_t cell_index;
	size_t dml_count;
	size_t merge_index;
	int rc;
	int result;

	handle = NULL;
	reparsed = NULL;
	assignment_sql = NULL;
	condition_sql = NULL;
	deparsed = NULL;
	selector_text = NULL;
	view_json = NULL;
	result = 1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "multiple nested SQL Server MERGE SQL should parse") != 0 ||
	    expect_deparse_equals_and_reparse(
		    handle,
		    sql,
		    "multiple nested SQL Server MERGE SQL should deparse exactly") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "multiple nested SQL Server MERGE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_count(
			    &graph,
			    &dml_count,
			    &error),
		    &error,
		    "multiple nested SQL Server MERGE DML count should resolve") != 0 ||
	    expect_true(
		    dml_count == 3U,
		    "outer INSERT and two nested MERGE DML nodes should be present") != 0) {
		goto done;
	}

	for (merge_index = 0U; merge_index < 2U; merge_index++) {
		size_t dml_index;

		dml_index = merge_index + 1U;
		if (expect_status_ok(
			    sqlparser_query_graph_dml_at(
				    &graph,
				    dml_index,
				    &dml,
				    &error),
			    &error,
			    "nested MERGE DML should resolve") != 0 ||
		    expect_true(
			    dml.kind == SQLPARSER_GRAPH_DML_MERGE &&
				    dml.index == dml_index &&
				    dml.branches.count == 2U &&
				    dml.assignments.count == 1U,
			    "nested MERGE DML shape mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    0U,
				    &assignment_index,
				    &error),
			    &error,
			    "nested MERGE assignment index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "nested MERGE assignment should resolve") != 0 ||
		    expect_true(
			    assignment.has_selector != 0 &&
				    assignment.selector.kind ==
					    SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT &&
				    assignment.selector.row_index == dml_index &&
				    assignment.selector.item_index == 0U &&
				    assignment.selector.column_index == 0U,
			    "nested MERGE assignment selector coordinates mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_selector_format(
				    &assignment.selector,
				    &selector_text,
				    &error),
			    &error,
			    "nested MERGE assignment selector should format") != 0 ||
		    expect_true(
			    strcmp(
				    selector_text,
				    expected_assignment_selectors[
					    merge_index]) == 0,
			    "nested MERGE assignment selector must include DML index") != 0) {
			goto done;
		}
		sqlparser_string_free(selector_text);
		selector_text = NULL;
		assignment_selectors[merge_index] =
			assignment.selector;
		if (expect_status_ok(
			    sqlparser_selector_update_assignment_sql(
				    handle,
				    &assignment_selectors[merge_index],
				    &assignment_sql,
				    &error),
			    &error,
			    "nested MERGE assignment SQL should resolve") != 0 ||
		    expect_true(
			    strcmp(
				    assignment_sql,
				    expected_assignment_sql[merge_index]) == 0,
			    "nested MERGE assignment SQL mismatch") != 0) {
			goto done;
		}
		sqlparser_string_free(assignment_sql);
		assignment_sql = NULL;

		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    1U,
				    &branch_index,
				    &error),
			    &error,
			    "nested MERGE INSERT branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    branch_index,
				    &branch,
				    &error),
			    &error,
			    "nested MERGE INSERT branch should resolve") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    branch_index,
			    &branch_detail,
			    &error,
			    "nested MERGE INSERT branch detail should resolve") != 0 ||
		    expect_true(
			    branch.ordinal == 1U &&
				    branch_detail.action_kind ==
					    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
				    branch.condition_selector.row_index ==
					    dml_index &&
				    branch.condition_selector.item_index == 1U &&
				    branch.rows.count == 2U,
			    "nested MERGE INSERT branch selector or payload mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_selector_format(
				    &branch.condition_selector,
				    &selector_text,
				    &error),
			    &error,
			    "nested MERGE condition selector should format") != 0 ||
		    expect_true(
			    strcmp(
				    selector_text,
				    expected_condition_selectors[
					    merge_index]) == 0,
			    "nested MERGE condition selector must include DML index") != 0) {
			goto done;
		}
		sqlparser_string_free(selector_text);
		selector_text = NULL;
		condition_selectors[merge_index] =
			branch.condition_selector;
		if (expect_status_ok(
			    sqlparser_selector_clause_sql(
				    handle,
				    &condition_selectors[merge_index],
				    &condition_sql,
				    &error),
			    &error,
			    "nested MERGE condition SQL should resolve") != 0 ||
		    expect_true(
			    strcmp(
				    condition_sql,
				    expected_condition_sql[merge_index]) == 0,
			    "nested MERGE condition SQL must come from its own source SQL") != 0) {
			goto done;
		}
		sqlparser_string_free(condition_sql);
		condition_sql = NULL;

		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    branch.rows,
				    0U,
				    &cell_index,
				    &error),
			    &error,
			    "nested MERGE source cell index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    cell_index,
				    &cell,
				    &error),
			    &error,
			    "nested MERGE source cell should resolve") != 0 ||
		    expect_true(
			    cell.row_index == 1U &&
				    cell.has_source_field != 0 &&
				    cell.has_source_target != 0,
			    "nested MERGE qualified source cell lineage is missing") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_field_at(
				    &graph,
				    cell.source_field_index,
				    &field,
				    &error),
			    &error,
			    "nested MERGE source field should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_relation_at(
				    &graph,
				    field.relation_index,
				    &relation,
				    &error),
			    &error,
			    "nested MERGE source relation should resolve") != 0 ||
		    expect_true(
			    field.block_index == relation.block_index &&
				    relation.alias_name != NULL &&
				    strcmp(
					    relation.alias_name,
					    expected_aliases[merge_index]) == 0,
			    "nested MERGE source field must use the nested MERGE block") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_target_at(
				    &graph,
				    cell.source_target_index,
				    &target,
				    &error),
			    &error,
			    "nested MERGE source target should resolve") != 0 ||
		    expect_true(
			    target.has_value != 0,
			    "nested MERGE source target value is missing") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_value_at(
				    &graph,
				    target.value_index,
				    &value,
				    &error),
			    &error,
			    "nested MERGE source target value should resolve") != 0 ||
		    expect_true(
			    value.has_bind_sql != 0 &&
				    strcmp(
					    value.bind_sql,
					    expected_bind_sql[merge_index]) == 0,
			    "nested MERGE source target bind lineage mismatch") != 0) {
			goto done;
		}
	}

	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "multiple nested MERGE View JSON should export") != 0 ||
	    expect_true(
		    view_json != NULL &&
			    strstr(
				    view_json,
				    expected_assignment_selectors[0]) != NULL &&
			    strstr(
				    view_json,
				    expected_assignment_selectors[1]) != NULL &&
			    strstr(
				    view_json,
				    expected_condition_selectors[0]) != NULL &&
			    strstr(
				    view_json,
				    expected_condition_selectors[1]) != NULL,
		    "multiple nested MERGE JSON selectors must be unique") != 0) {
		goto done;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_selector_set_update_assignment_sql(
		handle,
		&assignment_selectors[0],
		"s1.v + 10",
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "first nested MERGE assignment mutation should succeed") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_update_assignment_sql(
			    handle,
			    &assignment_selectors[0],
			    &assignment_sql,
			    &error),
		    &error,
		    "first nested MERGE mutated assignment should resolve") != 0 ||
	    expect_true(
		    strcmp(assignment_sql, "s1.v + 10") == 0,
		    "first nested MERGE mutation mismatch") != 0) {
		goto done;
	}
	sqlparser_string_free(assignment_sql);
	assignment_sql = NULL;
	if (expect_status_ok(
		    sqlparser_selector_update_assignment_sql(
			    handle,
			    &assignment_selectors[1],
			    &assignment_sql,
			    &error),
		    &error,
		    "second nested MERGE assignment should remain readable") != 0 ||
	    expect_true(
		    strcmp(assignment_sql, "s2.v") == 0,
		    "first mutation must not alter the second nested MERGE") != 0) {
		goto done;
	}
	sqlparser_string_free(assignment_sql);
	assignment_sql = NULL;

	rc = sqlparser_selector_set_update_assignment_sql(
		handle,
		&assignment_selectors[1],
		"s2.v + 20",
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "second nested MERGE assignment mutation should succeed") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_update_assignment_sql(
			    handle,
			    &assignment_selectors[0],
			    &assignment_sql,
			    &error),
		    &error,
		    "first nested MERGE assignment should remain readable") != 0 ||
	    expect_true(
		    strcmp(assignment_sql, "s1.v + 10") == 0,
		    "second mutation must not alter the first nested MERGE") != 0) {
		goto done;
	}
	sqlparser_string_free(assignment_sql);
	assignment_sql = NULL;
	if (expect_status_ok(
		    sqlparser_selector_update_assignment_sql(
			    handle,
			    &assignment_selectors[1],
			    &assignment_sql,
			    &error),
		    &error,
		    "second nested MERGE mutated assignment should resolve") != 0 ||
	    expect_true(
		    strcmp(assignment_sql, "s2.v + 20") == 0,
		    "second nested MERGE mutation mismatch") != 0) {
		goto done;
	}
	sqlparser_string_free(assignment_sql);
	assignment_sql = NULL;

	for (merge_index = 0U; merge_index < 2U; merge_index++) {
		if (expect_status_ok(
			    sqlparser_selector_clause_sql(
				    handle,
				    &condition_selectors[merge_index],
				    &condition_sql,
				    &error),
			    &error,
			    "mutated same-handle nested MERGE condition should resolve") != 0 ||
		    expect_true(
			    strcmp(
				    condition_sql,
				    expected_reparsed_condition_sql[
					    merge_index]) == 0,
			    "mutated same-handle nested MERGE condition is stale") != 0) {
			goto done;
		}
		sqlparser_string_free(condition_sql);
		condition_sql = NULL;
	}

	rc = sqlparser_deparse(
		handle,
		&deparsed,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "mutated nested MERGE SQL should deparse") != 0 ||
	    expect_true(
		    strstr(deparsed, "v = s1.v + 10") != NULL &&
			    strstr(deparsed, "v = s2.v + 20") != NULL,
		    "mutated nested MERGE deparse must contain both independent changes") != 0) {
		goto done;
	}
	rc = sqlparser_parse_with_options(
		deparsed,
		&options,
		&reparsed,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "mutated nested MERGE SQL should reparse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    reparsed,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "reparsed nested MERGE View should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_count(
			    &graph,
			    &dml_count,
			    &error),
		    &error,
		    "reparsed nested MERGE DML count should resolve") != 0 ||
	    expect_true(
		    dml_count == 3U,
		    "reparsed nested MERGE DML count mismatch") != 0) {
		goto done;
	}
	for (merge_index = 0U; merge_index < 2U; merge_index++) {
		const char *expected_sql;

		expected_sql = merge_index == 0U ?
			"s1.v + 10" :
			"s2.v + 20";
		if (expect_status_ok(
			    sqlparser_selector_update_assignment_sql(
				    reparsed,
				    &assignment_selectors[merge_index],
				    &assignment_sql,
				    &error),
			    &error,
			    "reparsed nested MERGE assignment should resolve") != 0 ||
		    expect_true(
			    strcmp(assignment_sql, expected_sql) == 0,
			    "reparsed nested MERGE assignment mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_selector_clause_sql(
				    reparsed,
				    &condition_selectors[merge_index],
				    &condition_sql,
				    &error),
			    &error,
			    "reparsed nested MERGE condition should resolve") != 0 ||
		    expect_true(
			    strcmp(
				    condition_sql,
				    expected_reparsed_condition_sql[
					    merge_index]) == 0,
			    "reparsed nested MERGE condition mismatch") != 0) {
			goto done;
		}
		sqlparser_string_free(assignment_sql);
		assignment_sql = NULL;
		sqlparser_string_free(condition_sql);
		condition_sql = NULL;
	}
	rc = sqlparser_export_view_json(
		reparsed,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "reparsed nested MERGE JSON should export") != 0 ||
	    expect_true(
		    view_json != NULL &&
			    strstr(
				    view_json,
				    expected_assignment_selectors[0]) != NULL &&
			    strstr(
				    view_json,
				    expected_assignment_selectors[1]) != NULL &&
			    strstr(
				    view_json,
				    expected_condition_selectors[0]) != NULL &&
			    strstr(
				    view_json,
				    expected_condition_selectors[1]) != NULL,
		    "reparsed nested MERGE JSON selector mapping mismatch") != 0) {
		goto done;
	}
	result = 0;

done:
	sqlparser_string_free(view_json);
	sqlparser_string_free(selector_text);
	sqlparser_string_free(deparsed);
	sqlparser_string_free(condition_sql);
	sqlparser_string_free(assignment_sql);
	sqlparser_handle_destroy(reparsed);
	sqlparser_handle_destroy(handle);
	return result;
}

static int test_sqlserver_merge_scope_and_cte_lineage(void)
{
	static const char unqualified_sql[] =
		"INSERT INTO dbo.audit_log(id) SELECT d.id "
		"FROM (MERGE dbo.t AS t "
		"USING (SELECT @id AS id) AS s ON t.id=s.id "
		"WHEN NOT MATCHED THEN INSERT(id) VALUES(id) "
		"OUTPUT INSERTED.id) AS d";
	static const char cte_sql[] =
		"IF @run = 1 BEGIN; "
		"WITH src AS (SELECT id, v FROM dbo.s) "
		"MERGE INTO dbo.t AS t "
		"USING src AS s ON t.id = s.id "
		"WHEN MATCHED THEN UPDATE SET v = s.v "
		"WHEN NOT MATCHED THEN INSERT (id, v) VALUES (s.id, s.v); "
		"END ELSE SELECT 0";
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_field_t field;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_target_t target;
	char *view_json;
	json_error_t json_error;
	json_t *root;
	json_t *statements;
	json_t *statement;
	json_t *query_graph;
	json_t *dml_json;
	json_t *branches_json;
	json_t *rows_json;
	json_t *cell_json;
	size_t assignment_index;
	size_t branch_index;
	size_t cell_index;
	size_t dialect_index;
	size_t statement_count;
	size_t statement_index;
	int rc;

	handle = NULL;
	view_json = NULL;
	root = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		unqualified_sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "nested MERGE unqualified source SQL should parse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "nested MERGE unqualified source graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_at(
			    &graph,
			    1U,
			    &dml,
			    &error),
		    &error,
		    "nested MERGE unqualified DML should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.branches,
			    0U,
			    &branch_index,
			    &error),
		    &error,
		    "nested MERGE unqualified branch index should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_branch_at(
			    &graph,
			    branch_index,
			    &branch,
			    &error),
		    &error,
		    "nested MERGE unqualified branch should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    branch.rows,
			    0U,
			    &cell_index,
			    &error),
		    &error,
		    "nested MERGE unqualified cell index should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_cell_at(
			    &graph,
			    cell_index,
			    &cell,
			    &error),
		    &error,
		    "nested MERGE unqualified cell should resolve") != 0 ||
	    expect_true(
		    cell.has_source_field != 0 &&
			    cell.has_source_target == 0,
		    "ambiguous nested MERGE unqualified field must remain unresolved") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_field_at(
			    &graph,
			    cell.source_field_index,
			    &field,
			    &error),
		    &error,
		    "nested MERGE unqualified field should resolve") != 0 ||
	    expect_true(
		    field.block_index != 0U &&
			    field.has_relation == 0,
		    "nested MERGE unqualified field must stay in the nested block without binding the outer target") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_export_view_json(
		handle,
		0,
		&view_json,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "nested MERGE unqualified JSON should export") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&json_error, 0, sizeof(json_error));
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, 0U) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml_json = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	dml_json = json_is_object(dml_json) ?
		json_array_get(
			json_object_get(dml_json, "children"),
			0U) :
		NULL;
	branches_json = json_is_object(dml_json) ?
		json_object_get(dml_json, "branches") :
		NULL;
	rows_json = json_is_array(branches_json) ?
		json_object_get(
			json_array_get(branches_json, 0U),
			"rows") :
		NULL;
	cell_json = json_is_array(rows_json) ?
		json_array_get(rows_json, 0U) :
		NULL;
	if (expect_true(
		    json_is_object(cell_json) &&
			    json_integer_is(
				    cell_json,
				    "source_field",
				    (json_int_t)cell.source_field_index) &&
			    json_object_get(
				    cell_json,
				    "source_target") == NULL,
		    "nested MERGE unqualified JSON must remain unresolved") != 0) {
		json_decref(root);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	json_decref(root);
	root = NULL;
	sqlparser_string_free(view_json);
	view_json = NULL;
	sqlparser_handle_destroy(handle);

	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			cte_sql,
			&options,
			&handle,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SH402 CTE MERGE SQL should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		statement_count = sqlparser_statement_count(handle);
		statement_index = (size_t)-1;
		for (branch_index = 0U;
		     branch_index < statement_count;
		     branch_index++) {
			sqlparser_statement_kind_t kind;

			if (sqlparser_statement_kind(
				    handle,
				    branch_index,
				    &kind,
				    &error) == SQLPARSER_STATUS_OK &&
			    kind == SQLPARSER_STATEMENT_KIND_MERGE) {
				statement_index = branch_index;
				break;
			}
		}
		if (expect_true(
			    statement_index != (size_t)-1,
			    "SH402 MERGE control leaf should be present") != 0 ||
		    expect_status_ok(
			    sqlparser_statement_query_graph(
				    handle,
				    statement_index,
				    &graph,
				    &error),
			    &error,
			    "SH402 CTE MERGE graph should build") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml(
				    &graph,
				    &dml,
				    &error),
			    &error,
			    "SH402 CTE MERGE DML should resolve") != 0 ||
		    expect_true(
			    dml.branches.count == 2U &&
				    dml.assignments.count == 1U,
			    "SH402 CTE MERGE DML payload mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    0U,
				    &assignment_index,
				    &error),
			    &error,
			    "SH402 assignment index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "SH402 assignment should resolve") != 0 ||
		    expect_true(
			    assignment.has_source_field != 0 &&
				    assignment.has_source_target != 0,
			    "SH402 assignment CTE source lineage is missing") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_target_at(
				    &graph,
				    assignment.source_target_index,
				    &target,
				    &error),
			    &error,
			    "SH402 CTE source target should resolve") != 0 ||
		    expect_true(
			    target.has_field != 0 &&
				    target.output_name != NULL &&
				    strcmp(target.output_name, "v") == 0,
			    "SH402 CTE source target mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_field_at(
				    &graph,
				    target.field_index,
				    &field,
				    &error),
			    &error,
			    "SH402 underlying CTE target field should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_relation_at(
				    &graph,
				    field.relation_index,
				    &relation,
				    &error),
			    &error,
			    "SH402 underlying CTE target relation should resolve") != 0 ||
		    expect_true(
			    relation.schema_name != NULL &&
				    strcmp(relation.schema_name, "dbo") == 0 &&
				    relation.object_name != NULL &&
				    strcmp(relation.object_name, "s") == 0,
			    "SH402 CTE source target must trace to dbo.s") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    1U,
				    &branch_index,
				    &error),
			    &error,
			    "SH402 INSERT branch index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    branch_index,
				    &branch,
				    &error),
			    &error,
			    "SH402 INSERT branch should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    branch.rows,
				    0U,
				    &cell_index,
				    &error),
			    &error,
			    "SH402 INSERT cell index should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    cell_index,
				    &cell,
				    &error),
			    &error,
			    "SH402 INSERT cell should resolve") != 0 ||
		    expect_true(
			    cell.row_index == 1U &&
				    cell.has_source_target != 0,
			    "SH402 INSERT cell lineage is missing") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_export_view_json(
			handle,
			0,
			&view_json,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SH402 View JSON should export") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		memset(&json_error, 0, sizeof(json_error));
		root = json_loads(view_json, 0, &json_error);
		statements = root != NULL ?
			json_object_get(root, "statements") :
			NULL;
		statement = json_is_array(statements) ?
			json_array_get(statements, statement_index) :
			NULL;
		query_graph = json_is_object(statement) ?
			json_object_get(statement, "query_graph") :
			NULL;
		dml_json = json_is_object(query_graph) ?
			json_object_get(query_graph, "dml") :
			NULL;
		branches_json = json_is_object(dml_json) ?
			json_object_get(dml_json, "branches") :
			NULL;
		rows_json = json_is_array(branches_json) ?
			json_object_get(
				json_array_get(branches_json, 1U),
				"rows") :
			NULL;
		cell_json = json_is_array(rows_json) ?
			json_array_get(rows_json, 0U) :
			NULL;
		if (expect_true(
			    json_is_object(cell_json) &&
				    json_integer_is(
					    cell_json,
					    "source_target",
					    (json_int_t)
						    cell.source_target_index) &&
				    json_string_is(
					    json_array_get(
						    branches_json,
						    0U),
					    "merge_action_kind",
					    "update") &&
				    json_string_is(
					    json_array_get(
						    branches_json,
						    1U),
					    "merge_action_kind",
					    "insert"),
			    "SH402 JSON lineage or action payload mismatch") != 0) {
			json_decref(root);
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		json_decref(root);
		root = NULL;
		sqlparser_string_free(view_json);
		view_json = NULL;
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int test_non_postgresql_multiple_merge_insert_rejection(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_ORACLE,
		SQLPARSER_DIALECT_DAMENG,
		SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_ORACLE
	};
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	size_t index;
	int rc;

	sql =
		"MERGE INTO t USING s ON (t.id=s.id) "
		"WHEN NOT MATCHED AND s.flag=1 THEN "
		"INSERT (id,a) VALUES (s.id,1) "
		"WHEN NOT MATCHED AND s.flag=2 THEN "
		"INSERT (id,b) VALUES (s.id,2)";
	for (index = 0U;
	     index < sizeof(dialects) / sizeof(dialects[0]);
	     index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[index];
		rc = sqlparser_parse_with_options(
			sql,
			&options,
			&handle,
			&error);
		if (expect_true(
			    rc == SQLPARSER_STATUS_UNSUPPORTED &&
				    handle == NULL &&
				    strstr(
					    error.message,
					    "multiple MERGE INSERT actions") !=
					    NULL,
			    "non-PostgreSQL multiple MERGE INSERT actions should fail closed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		"INSERT INTO dbo.audit_log(id) SELECT d.id "
		"FROM (MERGE dbo.t AS t USING dbo.s AS s ON t.id=s.id "
		"WHEN NOT MATCHED AND s.flag=1 THEN "
		"INSERT (id,a) VALUES (s.id,1) "
		"WHEN NOT MATCHED AND s.flag=2 THEN "
		"INSERT (id,b) VALUES (s.id,2) "
		"OUTPUT INSERTED.id) AS d",
		&options,
		&handle,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_UNSUPPORTED &&
			    handle == NULL &&
			    strstr(
				    error.message,
				    "multiple MERGE INSERT actions") != NULL,
		    "nested SQL Server multiple MERGE INSERT actions should fail atomically during parse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_cell_bind_mutation(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_selector_t selector;
	sqlparser_bind_value_t bind;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	char *deparsed_sql;
	size_t cell_index;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (:1, :2)",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert bind mutation parse should succeed") != 0) {
		return 1;
	}

	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_new";
	rc = sqlparser_insert_set_cell_bind(handle, 0U, 0U, 1U, &bind, &error);
	if (expect_status_ok(rc, &error, "insert set cell bind should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = 0U;
	selector.row_index = 0U;
	selector.column_index = 0U;
	bind.kind = SQLPARSER_BIND_KIND_POSITIONAL;
	bind.key = "3";
	rc = sqlparser_selector_set_insert_cell_bind(handle, &selector, &bind, &error);
	if (expect_status_ok(rc, &error, "selector set insert cell bind should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "insert bind mutation deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, ":3") != NULL, "deparsed insert should contain positional bind") != 0 ||
	    expect_true(strstr(deparsed_sql, ":secret_new") != NULL, "deparsed insert should contain named bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "deparsed insert should not expose internal bind markers") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert bind mutation graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert bind mutation dml should be available") != 0 ||
	    expect_true(dml.rows.count == 2U, "insert bind mutation should expose two cells") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 0U, &cell_index, &error), &error, "first bind cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "first bind cell should be available") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && cell.has_bind != 0 && strcmp(cell.bind, "3") == 0, "first bind cell key mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 1U, &cell_index, &error), &error, "second bind cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "second bind cell should be available") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED && cell.has_bind != 0 && strcmp(cell.bind, "secret_new") == 0, "second bind cell key mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_delete_where_literal_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	size_t where_count;
	int rc;

	sql = "DELETE FROM public.users WHERE status IN ('inactive', 'blocked') AND age > 30";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "delete parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "delete where literal count should succeed") != 0 ||
	    expect_true(where_count == 3U, "delete where literal count should be 3") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "delete where literal should succeed") != 0 ||
	    expect_true(strcmp(where_literal.column_name, "status") == 0, "delete first where column should be status") != 0 ||
	    expect_true(strcmp(where_literal.operator_name, "IN") == 0, "delete first where operator should be IN") != 0 ||
	    expect_true(strcmp(where_literal.literal.string_value, "inactive") == 0, "delete first where literal should be inactive") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "archived";
	rc = sqlparser_statement_where_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "delete where mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "delete deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'archived'") != NULL, "deparsed delete should contain archived") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generic_relation_and_literal_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	sqlparser_relation_view_t relation;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	size_t relation_count;
	size_t literal_count;
	size_t name_count;
	size_t order_no_index;
	int rc;

	sql = "SELECT u.id, o.order_no FROM public.users u "
	      "JOIN public.orders o ON u.id = o.user_id "
	      "WHERE o.status = 'paid'";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&name, 0, sizeof(name));
	memset(&relation, 0, sizeof(relation));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));
	order_no_index = 0U;

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "generic select parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_relation_count(handle, 0U, &relation_count, &error);
	if (expect_status_ok(rc, &error, "generic relation count should succeed") != 0 ||
	    expect_true(relation_count == 2U, "generic relation count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_relation(handle, 0U, 1U, &relation, &error);
	if (expect_status_ok(rc, &error, "generic relation fetch should succeed") != 0 ||
	    expect_true(strcmp(relation.table_name, "orders") == 0, "second relation should be orders") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_literal_count(handle, 0U, &literal_count, &error);
	if (expect_status_ok(rc, &error, "generic literal count should succeed") != 0 ||
	    expect_true(literal_count == 1U, "generic literal count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_literal(handle, 0U, 0U, &literal, &error);
	if (expect_status_ok(rc, &error, "generic literal fetch should succeed") != 0 ||
	    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "generic literal should be string") != 0 ||
	    expect_true(strcmp(literal.string_value, "paid") == 0, "generic literal should be paid") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_name_count(handle, 0U, &name_count, &error);
	if (expect_status_ok(rc, &error, "generic name count should succeed") != 0 ||
	    expect_true(name_count > 0U, "generic name count should be greater than 0") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (find_name_index(handle, 0U, "ColumnRef", "fields", "order_no", &order_no_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_name(handle, 0U, order_no_index, &name, &error);
	if (expect_status_ok(rc, &error, "generic name fetch should succeed") != 0 ||
	    expect_true(strcmp(name.owner_type, "ColumnRef") == 0, "generic name owner should be ColumnRef") != 0 ||
	    expect_true(strcmp(name.field_name, "fields") == 0, "generic name field should be fields") != 0 ||
	    expect_true(strcmp(name.value, "order_no") == 0, "generic name value should be order_no") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_relation_name(handle, 0U, 1U, "public", "archived_orders", &error);
	if (expect_status_ok(rc, &error, "generic relation rename should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "closed";
	rc = sqlparser_statement_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "generic literal mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_name(handle, 0U, order_no_index, "order_code", &error);
	if (expect_status_ok(rc, &error, "generic name mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "generic deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "archived_orders") != NULL, "generic deparse should contain renamed relation") != 0 ||
	    expect_true(strstr(deparsed_sql, "order_code") != NULL, "generic deparse should contain renamed column") != 0 ||
	    expect_true(strstr(deparsed_sql, "'closed'") != NULL, "generic deparse should contain rewritten literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generic_name_mutation_preserves_later_spelling(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed_handle;
	sqlparser_error_t error;
	char *deparsed_sql;
	size_t alias_index;
	int rc;

	sql = "SELECT Foo AS Alias1, Bar AS Alias2";
	handle = NULL;
	reparsed_handle = NULL;
	deparsed_sql = NULL;
	alias_index = 0U;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "alias spelling mutation parse should succeed") != 0) {
		return 1;
	}
	if (find_name_index(
		    handle,
		    0U,
		    "ResTarget",
		    "name",
		    "Alias1",
		    &alias_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_name(handle, 0U, alias_index, "Alias2", &error);
	if (expect_status_ok(rc, &error, "alias spelling mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "alias spelling mutation deparse should succeed") != 0 ||
	    expect_true(
		    deparsed_sql != NULL &&
			    strstr(deparsed_sql, "Bar AS Alias2") != NULL &&
			    strstr(deparsed_sql, "Bar AS \"Alias2\"") == NULL,
		    "later original alias spelling should remain unquoted") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_parse(deparsed_sql, &reparsed_handle, &error);
	if (expect_status_ok(rc, &error, "alias spelling mutation output should reparse") != 0) {
		sqlparser_handle_destroy(reparsed_handle);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(reparsed_handle);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_literal_mutation_preserves_ddl_identifier_spelling(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed_handle;
	sqlparser_error_t error;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	int rc;

	sql = "CREATE TABLE Foo (id int DEFAULT 1) USING Heap TABLESPACE Bar";
	handle = NULL;
	reparsed_handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "DDL spelling mutation parse should succeed") != 0) {
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	replacement.integer_value = 2LL;
	rc = sqlparser_statement_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "DDL literal mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "DDL spelling mutation deparse should succeed") != 0 ||
	    expect_true(
		    deparsed_sql != NULL &&
			    strstr(deparsed_sql, "CREATE TABLE Foo") != NULL &&
			    strstr(deparsed_sql, "DEFAULT 2") != NULL &&
			    strstr(deparsed_sql, "USING Heap") != NULL &&
			    strstr(deparsed_sql, "TABLESPACE Bar") != NULL &&
			    strstr(deparsed_sql, "\"Foo\"") == NULL &&
			    strstr(deparsed_sql, "\"Heap\"") == NULL &&
			    strstr(deparsed_sql, "\"Bar\"") == NULL,
		    "DDL mutation should preserve unquoted identifier spelling") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_parse(deparsed_sql, &reparsed_handle, &error);
	if (expect_status_ok(rc, &error, "DDL spelling mutation output should reparse") != 0) {
		sqlparser_handle_destroy(reparsed_handle);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(reparsed_handle);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_literal_mutation_preserves_bracket_identifiers(void)
{
	const char *sql;
	const char *first_bracket;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed_handle;
	sqlparser_error_t error;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	int rc;

	sql = "INSERT dbo.t([select]) OUTPUT INSERTED.[select] VALUES (1)";
	handle = NULL;
	reparsed_handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&replacement, 0, sizeof(replacement));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "SQL Server bracket mutation parse should succeed") != 0) {
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	replacement.integer_value = 2LL;
	rc = sqlparser_statement_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "SQL Server bracket literal mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	first_bracket = deparsed_sql != NULL ? strstr(deparsed_sql, "[select]") : NULL;
	if (expect_status_ok(rc, &error, "SQL Server bracket mutation deparse should succeed") != 0 ||
	    expect_true(
		    first_bracket != NULL &&
			    strstr(first_bracket + strlen("[select]"), "[select]") != NULL &&
			    strstr(deparsed_sql, "\"select\"") == NULL &&
			    strstr(deparsed_sql, "VALUES (2)") != NULL,
		    "SQL Server bracket identifiers should retain their source quoting") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_parse_with_options(
		deparsed_sql,
		&options,
		&reparsed_handle,
		&error);
	if (expect_status_ok(rc, &error, "SQL Server bracket mutation output should reparse") != 0) {
		sqlparser_handle_destroy(reparsed_handle);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(reparsed_handle);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_copy_same_name_mutation_preserves_options(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed_handle;
	sqlparser_error_t error;
	char *deparsed_sql;
	size_t relation_name_index;
	int rc;

	sql = "COPY Foo TO STDOUT WITH (format CSV, header TRUE)";
	handle = NULL;
	reparsed_handle = NULL;
	deparsed_sql = NULL;
	relation_name_index = 0U;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "COPY option mutation parse should succeed") != 0) {
		return 1;
	}
	if (find_name_index(
		    handle,
		    0U,
		    "RangeVar",
		    "relname",
		    "Foo",
		    &relation_name_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_name(
		handle,
		0U,
		relation_name_index,
		"Foo",
		&error);
	if (expect_status_ok(rc, &error, "COPY same-name mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "COPY option mutation deparse should succeed") != 0 ||
	    expect_true(
		    deparsed_sql != NULL &&
			    strcmp(deparsed_sql, sql) == 0,
		    "same-name mutation should preserve original COPY SQL") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_parse(deparsed_sql, &reparsed_handle, &error);
	if (expect_status_ok(rc, &error, "COPY option mutation output should reparse") != 0) {
		sqlparser_handle_destroy(reparsed_handle);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(reparsed_handle);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generic_name_api_on_ddl(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	char *deparsed_sql;
	size_t name_count;
	size_t view_name_index;
	int rc;

	sql = "DROP VIEW IF EXISTS public.v_orders";
	handle = NULL;
	deparsed_sql = NULL;
	view_name_index = 0U;
	memset(&error, 0, sizeof(error));
	memset(&name, 0, sizeof(name));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "ddl name parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_name_count(handle, 0U, &name_count, &error);
	if (expect_status_ok(rc, &error, "ddl name count should succeed") != 0 ||
	    expect_true(name_count == 2U, "ddl name count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (find_name_index(handle, 0U, "DropStmt", "objects", "v_orders", &view_name_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_name(handle, 0U, view_name_index, &name, &error);
	if (expect_status_ok(rc, &error, "ddl name fetch should succeed") != 0 ||
	    expect_true(strcmp(name.owner_type, "DropStmt") == 0, "ddl name owner should be DropStmt") != 0 ||
	    expect_true(strcmp(name.field_name, "objects") == 0, "ddl name field should be objects") != 0 ||
	    expect_true(strcmp(name.value, "v_orders") == 0, "ddl name value should be v_orders") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_set_name(handle, 0U, view_name_index, "v_orders_archive", &error);
	if (expect_status_ok(rc, &error, "ddl name mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "ddl name deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "v_orders_archive") != NULL, "ddl deparse should contain renamed view") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_selector_parse_and_format(void)
{
	sqlparser_selector_t selector;
	sqlparser_error_t error;
	char *selector_text;
	int rc;

	memset(&selector, 0, sizeof(selector));
	memset(&error, 0, sizeof(error));
	selector_text = NULL;

	rc = sqlparser_selector_parse("stmt[0].where_literal[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for where literal should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_WHERE_LITERAL, "selector kind should be where_literal") != 0 ||
	    expect_true(selector.statement_index == 0U, "selector statement index should be 0") != 0 ||
	    expect_true(selector.item_index == 1U, "selector item index should be 1") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].where_literal[1]") == 0, "selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse(
		"stmt[2].merge_assignment[3][4]",
		&selector,
		&error);
	if (expect_status_ok(rc, &error, "selector parse for MERGE assignment should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT,
	                "selector kind should be merge_assignment") != 0 ||
	    expect_true(selector.statement_index == 2U,
	                "MERGE assignment statement index should be 2") != 0 ||
	    expect_true(selector.row_index == 0U,
	                "root MERGE assignment DML index should be 0") != 0 ||
	    expect_true(selector.item_index == 3U,
	                "MERGE assignment WHEN index should be 3") != 0 ||
	    expect_true(selector.column_index == 4U,
	                "MERGE assignment item index should be 4") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "MERGE assignment selector format should succeed") != 0 ||
	    expect_true(
		    strcmp(selector_text, "stmt[2].merge_assignment[3][4]") == 0,
		    "MERGE assignment selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse(
		"stmt[2].merge_assignment[5][3][4]",
		&selector,
		&error);
	if (expect_status_ok(rc, &error, "nested MERGE assignment selector should parse") != 0 ||
	    expect_true(
		    selector.kind == SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT &&
			    selector.statement_index == 2U &&
			    selector.row_index == 5U &&
			    selector.item_index == 3U &&
			    selector.column_index == 4U,
		    "nested MERGE assignment selector coordinates mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_format(
			    &selector,
			    &selector_text,
			    &error),
		    &error,
		    "nested MERGE assignment selector should format") != 0 ||
	    expect_true(
		    strcmp(
			    selector_text,
			    "stmt[2].merge_assignment[5][3][4]") == 0,
		    "nested MERGE assignment selector should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}
	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse(
		"stmt[2].merge_branch_condition[5][3]",
		&selector,
		&error);
	if (expect_status_ok(rc, &error, "nested MERGE condition selector should parse") != 0 ||
	    expect_true(
		    selector.kind ==
				    SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION &&
			    selector.statement_index == 2U &&
			    selector.row_index == 5U &&
			    selector.item_index == 3U,
		    "nested MERGE condition selector coordinates mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_format(
			    &selector,
			    &selector_text,
			    &error),
		    &error,
		    "nested MERGE condition selector should format") != 0 ||
	    expect_true(
		    strcmp(
			    selector_text,
			    "stmt[2].merge_branch_condition[5][3]") == 0,
		    "nested MERGE condition selector should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}
	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse(
		"stmt[2].merge_assignment[5][3][4][1]",
		&selector,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT,
		    "MERGE assignment selector with an extra coordinate must fail") != 0) {
		return 1;
	}
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse(
		"stmt[2].merge_branch_condition[5][3][1]",
		&selector,
		&error);
	if (expect_true(
		    rc == SQLPARSER_STATUS_INVALID_ARGUMENT,
		    "MERGE condition selector with an extra coordinate must fail") != 0) {
		return 1;
	}
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[3].insert_cell[2][4]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for insert cell should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL, "selector kind should be insert_cell") != 0 ||
	    expect_true(selector.statement_index == 3U, "selector statement index should be 3") != 0 ||
	    expect_true(selector.row_index == 2U, "selector row index should be 2") != 0 ||
	    expect_true(selector.column_index == 4U, "selector column index should be 4") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "insert cell selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[3].insert_cell[2][4]") == 0, "insert cell selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[1].value[7]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for value should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_VALUE, "selector kind should be value") != 0 ||
	    expect_true(selector.statement_index == 1U, "selector statement index should be 1") != 0 ||
	    expect_true(selector.item_index == 7U, "selector item index should be 7") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "value selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[1].value[7]") == 0, "value selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[2].insert_columns", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for insert columns should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS, "selector kind should be insert_columns") != 0 ||
	    expect_true(selector.statement_index == 2U, "selector statement index should be 2") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "insert columns selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[2].insert_columns") == 0, "insert columns selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[2].insert_row[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for insert row should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_ROW, "selector kind should be insert_row") != 0 ||
	    expect_true(selector.statement_index == 2U, "selector statement index should be 2") != 0 ||
	    expect_true(selector.row_index == 1U, "selector row index should be 1") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "insert row selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[2].insert_row[1]") == 0, "insert row selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[0].select_targets[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for select targets should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGETS, "selector kind should be select_targets") != 0 ||
	    expect_true(selector.statement_index == 0U, "selector statement index should be 0") != 0 ||
	    expect_true(selector.item_index == 1U, "selector target list index should be 1") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "select targets selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].select_targets[1]") == 0, "select targets selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	selector_text = NULL;
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_selector_parse("stmt[0].select_target[1][2]", &selector, &error);
	if (expect_status_ok(rc, &error, "selector parse for select target should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGET, "selector kind should be select_target") != 0 ||
	    expect_true(selector.statement_index == 0U, "selector statement index should be 0") != 0 ||
	    expect_true(selector.item_index == 1U, "selector target list index should be 1") != 0 ||
	    expect_true(selector.column_index == 2U, "selector select target index should be 2") != 0) {
		return 1;
	}

	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "select target selector format should succeed") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].select_target[1][2]") == 0, "select target selector text should round-trip") != 0) {
		sqlparser_string_free(selector_text);
		return 1;
	}

	sqlparser_string_free(selector_text);
	return 0;
}

static int test_where_clause_sql_rewrite_api(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	char *where_sql;
	char *clause_sql;
	char *deparsed_sql;
	char *view_json;
	size_t clause_count;
	size_t where_count;
	int rc;

	handle = NULL;
	where_sql = NULL;
	clause_sql = NULL;
	deparsed_sql = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	memset(patches, 0, sizeof(patches));

	rc = sqlparser_parse("SELECT id, name FROM public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "select without where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "select where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "select should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_where_sql(handle, 0U, 0U, &where_sql, &error);
	if (expect_status_ok(rc, &error, "empty select where SQL should succeed") != 0 ||
	    expect_true(where_sql == NULL, "empty select where SQL should be NULL") != 0) {
		sqlparser_string_free(where_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id = 1", &error);
	if (expect_status_ok(rc, &error, "select where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"status = 'active'",
		&error);
	if (expect_status_ok(rc, &error, "select where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "select where append should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_where_sql(handle, 0U, 0U, &where_sql, &error);
	if (expect_status_ok(rc, &error, "rewritten select where SQL should succeed") != 0 ||
	    expect_true(where_sql != NULL && strstr(where_sql, "id = 1") != NULL, "select where should contain id condition") != 0 ||
	    expect_true(strstr(where_sql, "status = 'active'") != NULL, "select where should contain appended condition") != 0) {
		sqlparser_string_free(where_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(where_sql);
	where_sql = NULL;
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "where view JSON should export") != 0 ||
	    expect_true(strstr(view_json, "\"query_graph\"") != NULL,
	                "view JSON should expose query graph") != 0 ||
	    expect_true(strstr(view_json, "\"clauses\"") == NULL,
	                "view JSON should not expose old clause array") != 0 ||
	    expect_true(strstr(view_json, "\"clause\":\"select_list\"") != NULL,
	                "query graph should expose select_list field clause") != 0 ||
	    expect_true(strstr(view_json, "\"clause\":\"where\"") != NULL,
	                "query graph should expose where field/value clause") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;
	rc = sqlparser_statement_clause_count(handle, 0U, &clause_count, &error);
	if (expect_status_ok(rc, &error, "select clause count should succeed") != 0 ||
	    expect_true(clause_count == 3U, "select should expose select_list, where, and order_by clauses") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, clause_count, &clause_sql, &error);
	if (expect_true(rc == SQLPARSER_STATUS_INVALID_ARGUMENT && clause_sql == NULL,
		    "out-of-range clause access should fail") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "select_list clause SQL should succeed") != 0 ||
	    expect_true(clause_sql != NULL && strstr(clause_sql, "id") != NULL && strstr(clause_sql, "name") != NULL,
	                "select_list clause should contain selected columns") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(clause_sql);
	clause_sql = NULL;
	rc = sqlparser_statement_clause_sql(handle, 0U, 2U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "empty order_by clause SQL should succeed") != 0 ||
	    expect_true(clause_sql == NULL, "empty order_by clause should be NULL") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_clause_sql(handle, 0U, 2U, "name DESC", &error);
	if (expect_status_ok(rc, &error, "order_by clause set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "order_by clause set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "order_by clause deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "ORDER BY") != NULL, "deparse should contain ORDER BY") != 0 ||
	    expect_true(strstr(deparsed_sql, "name DESC") != NULL, "deparse should contain order expression") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("UPDATE public.users SET name = 'bob'", &handle, &error);
	if (expect_status_ok(rc, &error, "update without where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_clause_count(handle, 0U, &clause_count, &error);
	if (expect_status_ok(rc, &error, "update clause count should succeed") != 0 ||
	    expect_true(clause_count == 2U, "update should expose set_list and where clauses") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "update set_list clause SQL should succeed") != 0 ||
	    expect_true(clause_sql != NULL && strstr(clause_sql, "name") != NULL && strstr(clause_sql, "'bob'") != NULL,
	                "update set_list clause should contain assignment") != 0) {
		sqlparser_string_free(clause_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(clause_sql);
	clause_sql = NULL;
	rc = sqlparser_statement_set_clause_sql(handle, 0U, 0U, "name = 'bob', age = 18", &error);
	if (expect_status_ok(rc, &error, "update set_list replacement should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "update set_list replacement should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_parse("stmt[0].clause[0]", &selector, &error);
	if (expect_status_ok(rc, &error, "set_list clause selector parse should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_CLAUSE, "set_list selector kind should be clause") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_parse("stmt[0].clause[1]", &selector, &error);
	if (expect_status_ok(rc, &error, "clause selector parse should succeed") != 0 ||
	    expect_true(selector.kind == SQLPARSER_SELECTOR_KIND_CLAUSE, "selector kind should be clause") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_append_clause_condition(
		handle,
		&selector,
		SQLPARSER_BOOL_OPERATOR_AND,
		"id = 10",
		&error);
	if (expect_status_ok(rc, &error, "update where append on empty slot should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_append_clause_condition(
		handle,
		&selector,
		SQLPARSER_BOOL_OPERATOR_OR,
		"external_id = 'u-10'",
		&error);
	if (expect_status_ok(rc, &error, "update where OR append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "update where append should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "age = 18") != NULL, "update deparse should contain added SET assignment") != 0 ||
	    expect_true(strstr(deparsed_sql, "WHERE") != NULL, "update deparse should contain WHERE") != 0 ||
	    expect_true(strstr(deparsed_sql, "external_id") != NULL, "update deparse should contain OR condition") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("DELETE FROM public.users WHERE status = 'inactive'", &handle, &error);
	if (expect_status_ok(rc, &error, "delete with where parse should succeed") != 0) {
		return 1;
	}
	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = "stmt[0].clause[0]";
	patches[0].sql = "deleted_at IS NULL";
	patches[1].op = SQLPARSER_PATCH_APPEND_CONDITION;
	patches[1].selector = "stmt[0].clause[0]";
	patches[1].bool_operator = SQLPARSER_BOOL_OPERATOR_AND;
	patches[1].sql = "status IN ('inactive', 'blocked')";
	patch_list.items = patches;
	patch_list.count = 2U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "delete where patch list should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "delete where patch should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "delete where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "deleted_at IS NULL") != NULL, "delete where should contain replacement") != 0 ||
	    expect_true(strstr(deparsed_sql, "blocked") != NULL, "delete where should contain appended condition") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("INSERT INTO public.archive_users SELECT id, name FROM public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "insert select without where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "insert select where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "insert select should expose one select where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active = TRUE", &error);
	if (expect_status_ok(rc, &error, "insert select where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "insert select where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("INSERT INTO public.users (id, name) VALUES (1, 'bob')", &handle, &error);
	if (expect_status_ok(rc, &error, "insert values parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "insert values where count should succeed") != 0 ||
	    expect_true(where_count == 0U, "insert values should not expose synthetic where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse(
		"INSERT INTO public.users (id, name) VALUES (1, 'bob') "
		"ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name",
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert on conflict parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "on conflict where count should succeed") != 0 ||
	    expect_true(where_count == 2U, "on conflict should expose infer and update where slots") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id > 0", &error);
	if (expect_status_ok(rc, &error, "on conflict infer where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 1U, "EXCLUDED.name IS NOT NULL", &error);
	if (expect_status_ok(rc, &error, "on conflict update where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "on conflict where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE VIEW public.v_users AS SELECT id FROM public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "create view parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "status = 'active'", &error);
	if (expect_status_ok(rc, &error, "create view select where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "create view where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE INDEX idx_users_name ON public.users (name)", &handle, &error);
	if (expect_status_ok(rc, &error, "create index parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active = TRUE", &error);
	if (expect_status_ok(rc, &error, "partial index where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "partial index where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("COPY public.users (id, name) FROM STDIN", &handle, &error);
	if (expect_status_ok(rc, &error, "copy from parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "copy from where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "copy from should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active IS TRUE", &error);
	if (expect_status_ok(rc, &error, "copy from where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "copy from where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("COPY public.users TO STDOUT", &handle, &error);
	if (expect_status_ok(rc, &error, "copy to parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "copy to where count should succeed") != 0 ||
	    expect_true(where_count == 0U, "copy to should not expose unsupported where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE RULE users_update_rule AS ON UPDATE TO public.users DO ALSO SELECT 1", &handle, &error);
	if (expect_status_ok(rc, &error, "create rule parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "create rule where count should succeed") != 0 ||
	    expect_true(where_count == 2U, "create rule should expose rule and action where slots") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "NEW.active IS TRUE", &error);
	if (expect_status_ok(rc, &error, "create rule condition where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 1U, "1 = 1", &error);
	if (expect_status_ok(rc, &error, "create rule action where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "create rule where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse("CREATE PUBLICATION pub_users FOR TABLE public.users", &handle, &error);
	if (expect_status_ok(rc, &error, "create publication parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "create publication where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "create publication should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "active IS TRUE", &error);
	if (expect_status_ok(rc, &error, "create publication where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "create publication where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	rc = sqlparser_parse(
		"CREATE TABLE public.room_booking ("
		"room_id integer, "
		"during tsrange, "
		"EXCLUDE USING gist (room_id WITH =, during WITH &&)"
		")",
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "create table exclusion constraint parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_where_count(handle, 0U, &where_count, &error);
	if (expect_status_ok(rc, &error, "exclusion constraint where count should succeed") != 0 ||
	    expect_true(where_count == 1U, "exclusion constraint should expose one writable where slot") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "room_id IS NOT NULL", &error);
	if (expect_status_ok(rc, &error, "exclusion constraint where set should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "exclusion constraint where set should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options("SELECT * FROM `users`", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "`id` = 1", &error);
	if (expect_status_ok(rc, &error, "mysql where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"`name` LIKE 'b%'",
		&error);
	if (expect_status_ok(rc, &error, "mysql where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "mysql where rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	handle = NULL;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options("SELECT * FROM users", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "oracle where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id = :id", &error);
	if (expect_status_ok(rc, &error, "oracle where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"name = q'[Bob's order]'",
		&error);
	if (expect_status_ok(rc, &error, "oracle where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "oracle where rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "oracle where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, ":id") != NULL, "oracle where should restore bind name") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "oracle where should not expose internal bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options("DELETE FROM [dbo].[users]", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "sqlserver where parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_where_sql(handle, 0U, 0U, "[id] = @id", &error);
	if (expect_status_ok(rc, &error, "sqlserver where set should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"[name] = N'bob'",
		&error);
	if (expect_status_ok(rc, &error, "sqlserver where append should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "sqlserver where rewrite should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sqlserver where deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "sqlserver where should not expose internal bind markers") != 0 ||
	    expect_true(strstr(deparsed_sql, "bob") != NULL, "sqlserver where should contain string literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}


static int test_generic_literal_api_on_ddl(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	size_t literal_count;
	int rc;

	sql = "CREATE VIEW public.v_users AS SELECT * FROM public.users WHERE status = 'active'";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "ddl parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_literal_count(handle, 0U, &literal_count, &error);
	if (expect_status_ok(rc, &error, "ddl literal count should succeed") != 0 ||
	    expect_true(literal_count == 1U, "ddl literal count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_literal(handle, 0U, 0U, &literal, &error);
	if (expect_status_ok(rc, &error, "ddl literal fetch should succeed") != 0 ||
	    expect_true(strcmp(literal.string_value, "active") == 0, "ddl literal should be active") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "archived";
	rc = sqlparser_statement_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "ddl literal mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "ddl deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'archived'") != NULL, "ddl deparse should contain archived") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_update_from_returning_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_assignment_view_t assignment;
	sqlparser_relation_view_t relation;
	char *assignment_sql;
	char *deparsed_sql;
	size_t relation_count;
	int rc;

	sql =
		"UPDATE public.users AS u "
		"SET name = src.name, updated_at = clock_timestamp() "
		"FROM public.user_stage AS src "
		"WHERE u.id = src.id "
		"RETURNING u.id, u.name";
	handle = NULL;
	assignment_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&relation, 0, sizeof(relation));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "update-from parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_target_relation(handle, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "update-from target relation should succeed") != 0 ||
	    expect_true(strcmp(relation.schema_name, "public") == 0, "update-from target schema should be public") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "update-from target table should be users") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_relation_count(handle, 0U, &relation_count, &error);
	if (expect_status_ok(rc, &error, "update-from relation count should succeed") != 0 ||
	    expect_true(relation_count >= 2U, "update-from should expose both target and source relations") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error);
	if (expect_status_ok(rc, &error, "update-from assignment should succeed") != 0 ||
	    expect_true(strcmp(assignment.column_name, "name") == 0, "update-from first assignment should be name") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_VALUE_KIND_EXPRESSION, "update-from assignment should be expression") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_assignment_sql(handle, 0U, 0U, &assignment_sql, &error);
	if (expect_status_ok(rc, &error, "update-from assignment SQL should succeed") != 0 ||
	    expect_true(strcmp(assignment_sql, "src.name") == 0, "update-from assignment SQL should be src.name") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_sql(handle, 0U, 0U, "src.display_name", &error);
	if (expect_status_ok(rc, &error, "update-from assignment mutation should succeed") != 0) {
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "update-from deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "src.display_name") != NULL, "update-from deparse should contain src.display_name") != 0 ||
	    expect_true(strstr(deparsed_sql, "RETURNING u.id, u.name") != NULL, "update-from deparse should preserve returning") != 0 ||
	    expect_true(strstr(deparsed_sql, "FROM public.user_stage src") != NULL, "update-from deparse should preserve source relation") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(assignment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(assignment_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_insert_on_conflict_returning_sql_mutation(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_insert_source_kind_t source_kind;
	char *cell_sql;
	char *deparsed_sql;
	size_t row_count;
	int rc;

	sql =
		"INSERT INTO public.users (id, name, updated_at) "
		"VALUES (1, 'alice', DEFAULT) "
		"ON CONFLICT (id) DO UPDATE SET name = excluded.name, updated_at = now() "
		"RETURNING id, name";
	handle = NULL;
	cell_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_VALUES, "insert-on-conflict should be VALUES source") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict row count should succeed") != 0 ||
	    expect_true(row_count == 1U, "insert-on-conflict should expose one VALUES row") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_sql(handle, 0U, 0U, 2U, &cell_sql, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict cell SQL should succeed") != 0 ||
	    expect_true(strcmp(cell_sql, "DEFAULT") == 0, "insert-on-conflict third value should be DEFAULT") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_set_cell_sql(handle, 0U, 0U, 2U, "clock_timestamp()", &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict cell mutation should succeed") != 0) {
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "insert-on-conflict deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "ON CONFLICT (id) DO UPDATE SET") != NULL, "insert-on-conflict deparse should preserve on conflict") != 0 ||
	    expect_true(strstr(deparsed_sql, "clock_timestamp()") != NULL, "insert-on-conflict deparse should contain clock_timestamp()") != 0 ||
	    expect_true(strstr(deparsed_sql, "RETURNING id, name") != NULL, "insert-on-conflict deparse should preserve returning") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(cell_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(cell_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_merge_statement_walk(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_statement_kind_t kind;
	const char *node_name;
	char *deparsed_sql;
	int rc;

	sql =
		"MERGE INTO public.target_table AS t "
		"USING public.source_table AS s "
		"ON t.id = s.id "
		"WHEN MATCHED THEN UPDATE SET name = s.name "
		"WHEN NOT MATCHED THEN INSERT (id, name) VALUES (s.id, s.name)";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "merge parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_statement_kind(handle, 0U, &kind, &error);
	if (expect_status_ok(rc, &error, "merge kind should succeed") != 0 ||
	    expect_true(kind == SQLPARSER_STATEMENT_KIND_MERGE, "statement should be MERGE") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_node_name(handle, 0U, &node_name, &error);
	if (expect_status_ok(rc, &error, "merge node name should succeed") != 0 ||
	    expect_true(strcmp(node_name, "MergeStmt") == 0, "statement node should be MergeStmt") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "merge deparse should succeed") != 0 ||
	    expect_true(strcmp(deparsed_sql, sql) == 0, "unmodified merge deparse should preserve original SQL") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_resource_limits(void)
{
	sqlparser_limits_t limits;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *json_text;
	int rc;

	handle = NULL;
	json_text = NULL;
	memset(&error, 0, sizeof(error));
	memset(&limits, 0, sizeof(limits));

	sqlparser_limits_default(&limits);
	if (expect_true(limits.struct_size == sizeof(limits), "default limits should expose struct size") != 0 ||
	    expect_true(limits.max_sql_bytes > 0U, "default SQL byte limit should be non-zero") != 0 ||
	    expect_true(limits.max_output_bytes > 0U, "default output byte limit should be non-zero") != 0 ||
	    expect_true(limits.max_statement_count > 0U, "default statement count limit should be non-zero") != 0) {
		return 1;
	}

	limits.max_sql_bytes = 8U;
	rc = sqlparser_parse_with_limits("SELECT 123456789", &limits, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL byte limit should reject large input") != 0 ||
	    expect_true(handle == NULL, "failed limited parse should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_limits_default(&limits);
	limits.max_statement_count = 1U;
	rc = sqlparser_parse_with_limits("SELECT 1; SELECT 2", &limits, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT, "statement count limit should reject multi statement") != 0 ||
	    expect_true(handle == NULL, "failed statement limit parse should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_limits_default(&limits);
	limits.max_output_bytes = 16U;
	rc = sqlparser_parse_with_limits("SELECT 1", &limits, &handle, &error);
	if (expect_status_ok(rc, &error, "limited output parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 0, &json_text, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT, "output byte limit should reject view JSON") != 0 ||
	    expect_true(json_text == NULL, "failed output export should not return JSON") != 0) {
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_mysql_dialect_select_rewrite(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_relation_view_t relation;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	int rc;

	sql = "SELECT `u`.`id`, \"hello\" AS `label` FROM `users` AS `u` "
	      "WHERE `u`.`id` = 1 LIMIT 5, 10";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&relation, 0, sizeof(relation));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&replacement, 0, sizeof(replacement));

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql select parse should succeed") != 0) {
		return 1;
	}

	if (expect_true(sqlparser_handle_dialect(handle) == SQLPARSER_DIALECT_MYSQL, "handle dialect should be mysql") != 0 ||
	    expect_true(strcmp(sqlparser_original_sql(handle), sql) == 0, "original SQL should be preserved") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_relation(handle, 0U, 0U, &relation, &error);
	if (expect_status_ok(rc, &error, "mysql select relation should succeed") != 0 ||
	    expect_true(strcmp(relation.table_name, "users") == 0, "mysql relation should be users") != 0 ||
	    expect_true(strcmp(relation.alias_name, "u") == 0, "mysql alias should be u") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_statement_where_literal(handle, 0U, 0U, &where_literal, &error);
	if (expect_status_ok(rc, &error, "mysql where literal should succeed") != 0 ||
	    expect_true(where_literal.literal.kind == SQLPARSER_LITERAL_KIND_INTEGER, "mysql where literal should be integer") != 0 ||
	    expect_true(where_literal.literal.integer_value == 1, "mysql where literal should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	replacement.integer_value = 2;
	rc = sqlparser_statement_where_set_literal(handle, 0U, 0U, &replacement, &error);
	if (expect_status_ok(rc, &error, "mysql where rewrite should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql select deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "LIMIT 5, 10") != NULL, "mysql deparse should use offset,count LIMIT") != 0 ||
	    expect_true(strstr(deparsed_sql, "OFFSET") == NULL, "mysql deparse should not expose OFFSET for comma LIMIT") != 0 ||
	    expect_true(strstr(deparsed_sql, "2") != NULL, "mysql deparse should contain rewritten literal") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_mysql_dialect_insert_rewrite(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_insert_source_kind_t source_kind;
	sqlparser_literal_view_t literal;
	sqlparser_literal_value_t replacement;
	char *deparsed_sql;
	const char *column_name;
	size_t row_count;
	int rc;

	sql = "INSERT INTO `users` (`id`, `name`) VALUES (1, \"bob\"), (2, 'alice')";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&literal, 0, sizeof(literal));
	memset(&replacement, 0, sizeof(replacement));

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql insert parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_insert_source_kind(handle, 0U, &source_kind, &error);
	if (expect_status_ok(rc, &error, "mysql insert source kind should succeed") != 0 ||
	    expect_true(source_kind == SQLPARSER_INSERT_SOURCE_VALUES, "mysql insert source should be VALUES") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_column_name(handle, 0U, 1U, &column_name, &error);
	if (expect_status_ok(rc, &error, "mysql insert column name should succeed") != 0 ||
	    expect_true(strcmp(column_name, "name") == 0, "mysql insert column should be name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_row_count(handle, 0U, &row_count, &error);
	if (expect_status_ok(rc, &error, "mysql insert row count should succeed") != 0 ||
	    expect_true(row_count == 2U, "mysql insert row count should be 2") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_insert_cell_literal(handle, 0U, 0U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "mysql insert cell literal should succeed") != 0 ||
	    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "mysql insert cell should be string") != 0 ||
	    expect_true(strcmp(literal.string_value, "bob") == 0, "mysql double string should normalize to string literal") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	replacement.kind = SQLPARSER_LITERAL_KIND_STRING;
	replacement.string_value = "carol";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 0U, 1U, &replacement, &error);
	if (expect_status_ok(rc, &error, "mysql insert rewrite should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql insert deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "carol") != NULL, "mysql insert deparse should contain rewritten value") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_dialect_insert_column_patch_with_question_param(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
	} cases[] = {
		{SQLPARSER_DIALECT_MYSQL, "mysql"},
		{SQLPARSER_DIALECT_ORACLE, "oracle"},
		{SQLPARSER_DIALECT_SQLSERVER, "sqlserver"},
		{SQLPARSER_DIALECT_DAMENG, "dameng"},
		{SQLPARSER_DIALECT_VASTBASE_ORACLE, "vastbase-oracle"},
		{SQLPARSER_DIALECT_VASTBASE_MYSQL, "vastbase-mysql"},
		{SQLPARSER_DIALECT_VASTBASE_SQLSERVER, "vastbase-sqlserver"}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_column_t column;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_index_span_t invalid_span;
	char *deparsed_sql;
	size_t cell_index;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(
			"INSERT INTO users (username, email, age) VALUES (?, ?, ?), (?, ?, ?)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "dialect question insert parse should succeed") != 0) {
			return 1;
		}

		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
		patch.selector = "stmt[0].insert_columns";
		patch.index = 3U;
		patch.name = "created_at";
		patch.default_sql = "?";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "insert column question patch should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		if (expect_deparse_reparse_ok(handle, "insert column question patch should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "insert column question deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, "created_at") != NULL, "patched insert should contain new column") != 0 ||
		    expect_true(strstr(deparsed_sql, "$") == NULL, "patched insert should not expose internal params") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		deparsed_sql = NULL;

		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "patched insert graph should succeed") != 0 ||
		    expect_true(graph.has_dml != 0, "patched insert should expose dml graph") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_dml(&graph, &dml, &error);
		if (expect_status_ok(rc, &error, "patched insert dml should be available") != 0 ||
		    expect_true(dml.target_columns.count == 4U, "patched insert should expose four target columns") != 0 ||
		    expect_true(dml.rows.count == 8U, "patched insert should expose eight value cells") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_dml_column_at(&graph, 3U, &column, &error);
		if (expect_status_ok(rc, &error, "patched insert column should be available") != 0 ||
		    expect_true(strcmp(column.column_name, "created_at") == 0, "patched insert column should be created_at") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_span_index_at(&graph, dml.rows, 7U, &cell_index, &error);
		if (expect_status_ok(rc, &error, "patched insert cell index should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error);
		if (expect_status_ok(rc, &error, "patched insert cell should be available") != 0 ||
		    expect_true(cell.row_index == 1U, "patched insert cell row mismatch") != 0 ||
		    expect_true(cell.column_ordinal == 3U, "patched insert cell column mismatch") != 0 ||
		    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "patched insert cell should be a bind") != 0 ||
		    expect_true(cell.has_bind_sql != 0 && strcmp(cell.bind_sql, "?") == 0, "patched insert cell should expose question bind SQL") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		invalid_span.offset = (size_t)-1;
		invalid_span.count = 1U;
		rc = sqlparser_query_graph_span_index_at(&graph, invalid_span, 0U, &cell_index, &error);
		if (expect_true(rc == SQLPARSER_STATUS_INVALID_ARGUMENT,
			    "overflowing query graph span should fail") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_handle_destroy(handle);
	}

		{
			sqlparser_parse_options_t options;
			sqlparser_handle_t *handle;
			sqlparser_error_t error;
			char *deparsed_sql;
		int rc;

		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_MYSQL;
		rc = sqlparser_parse_with_options(
			"INSERT LOW_PRIORITY IGNORE INTO `users` (`id`, `phone`) VALUES (?, ?)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "mysql insert modifiers should parse") != 0 ||
		    expect_true(handle != NULL, "mysql insert modifiers should return handle") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "mysql insert modifiers deparse should succeed") != 0 ||
		    expect_true(
			    strcmp(
				    deparsed_sql,
				    "INSERT LOW_PRIORITY IGNORE INTO `users` (`id`, `phone`) VALUES (?, ?)") == 0,
			    "unmodified mysql insert modifiers deparse should preserve original SQL") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
		}

		{
			sqlparser_parse_options_t options;
			sqlparser_handle_t *handle;
			sqlparser_error_t error;
			char *deparsed_sql;
			int rc;

			handle = NULL;
			deparsed_sql = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = SQLPARSER_DIALECT_MYSQL;
			rc = sqlparser_parse_with_options(
				"UPDATE LOW_PRIORITY IGNORE users u JOIN orders o ON u.id=o.user_id SET u.phone = ? WHERE o.shipping_phone = ?",
				&options,
				&handle,
				&error);
			if (expect_status_ok(rc, &error, "mysql update modifiers should parse") != 0 ||
			    expect_true(handle != NULL, "mysql update modifiers should return handle") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			rc = sqlparser_deparse(handle, &deparsed_sql, &error);
			if (expect_status_ok(rc, &error, "mysql update modifiers deparse should succeed") != 0 ||
			    expect_true(
				    strstr(deparsed_sql, "UPDATE LOW_PRIORITY IGNORE users u JOIN orders o ON") != NULL,
				    "mysql update modifiers should be preserved") != 0 ||
			    expect_true(strstr(deparsed_sql, "$") == NULL, "mysql update modifiers should not expose internal params") != 0) {
				sqlparser_string_free(deparsed_sql);
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
		}

		{
			sqlparser_parse_options_t options;
			sqlparser_handle_t *handle;
			sqlparser_error_t error;
			char *deparsed_sql;
			int rc;

			handle = NULL;
			deparsed_sql = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = SQLPARSER_DIALECT_MYSQL;
			rc = sqlparser_parse_with_options(
				"DELETE LOW_PRIORITY QUICK IGNORE u FROM users u JOIN orders o ON u.id=o.user_id WHERE u.phone = ?",
				&options,
				&handle,
				&error);
			if (expect_status_ok(rc, &error, "mysql delete modifiers should parse") != 0 ||
			    expect_true(handle != NULL, "mysql delete modifiers should return handle") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			rc = sqlparser_deparse(handle, &deparsed_sql, &error);
			if (expect_status_ok(rc, &error, "mysql delete modifiers deparse should succeed") != 0 ||
			    expect_true(
				    strstr(deparsed_sql, "DELETE LOW_PRIORITY QUICK IGNORE u FROM users u JOIN orders o ON") != NULL,
				    "mysql delete modifiers should be preserved") != 0 ||
			    expect_true(strstr(deparsed_sql, "$") == NULL, "mysql delete modifiers should not expose internal params") != 0) {
				sqlparser_string_free(deparsed_sql);
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
		}

		return 0;
	}

static int test_mysql_dialect_create_table_extensions(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed_sql;
	int rc;

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse_with_options(
		"CREATE TABLE `users` (`id` INT UNSIGNED AUTO_INCREMENT, `score` INT ZEROFILL, `name` VARCHAR(64)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "mysql create table extensions should parse") != 0 ||
	    expect_true(handle != NULL, "mysql create table extensions should return handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql create table extensions deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "UNSIGNED AUTO_INCREMENT") != NULL, "mysql unsigned auto_increment should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "ZEROFILL") != NULL, "mysql zerofill should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "ENGINE=InnoDB") != NULL, "mysql engine option should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "DEFAULT CHARSET=utf8mb4") != NULL, "mysql default charset option should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "COLLATE=utf8mb4_bin") != NULL, "mysql collate option should be preserved") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_dialect_option(void)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed_sql;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		"SELECT TOP (3) [id], [name] FROM [dbo].[users] WHERE [id] = @id",
		&options,
		&handle,
		&error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "sqlserver dialect should parse supported input") != 0 ||
	    expect_true(handle != NULL, "supported sqlserver parse should return handle") != 0 ||
	    expect_true(sqlparser_handle_dialect(handle) == SQLPARSER_DIALECT_SQLSERVER, "handle dialect should be sqlserver") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "sqlserver deparse should succeed") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "TOP (3)") != NULL, "sqlserver deparse should restore TOP") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "@id") != NULL, "sqlserver deparse should restore @ parameter") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "$1") == NULL, "sqlserver deparse should not expose internal parameter") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_control_flow_and_patch(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *clone;
	sqlparser_control_flow_view_t flow;
	sqlparser_control_branch_t branch;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_block_t block;
	sqlparser_graph_value_t value;
	sqlparser_patch_t patches[2];
	sqlparser_patch_t invalid_patch;
	sqlparser_patch_list_t patch_list;
	sqlparser_error_t error;
	char *view_json;
	char *deparsed_sql;
	char *clone_sql;
	char *before_invalid;
	char *after_invalid;
	size_t dialect_index;
	size_t index;
	size_t count;
	unsigned long generation;
	int found_new_name;
	int rc;

	sql =
		"IF @enabled = 1 AND EXISTS (SELECT 1 FROM dbo.config WHERE name = @name) "
		"BEGIN UPDATE dbo.t SET a = 2 "
		"OUTPUT DELETED.a AS old_a, INSERTED.a AS new_a WHERE id = @id; "
		"SELECT id FROM dbo.t; END "
		"ELSE INSERT dbo.t(a) OUTPUT INSERTED.id VALUES (1)";
	handle = NULL;
	clone = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	clone_sql = NULL;
	before_invalid = NULL;
	after_invalid = NULL;
	memset(&error, 0, sizeof(error));
	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		sqlparser_statement_kind_t statement_kind;

		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "SQL Server control flow should parse") != 0 ||
		    expect_true(sqlparser_statement_count(handle) == 4U,
			    "SQL Server control flow should expose four addressable units") != 0) {
			goto fail;
		}
		rc = sqlparser_statement_kind(handle, 0U, &statement_kind, &error);
		if (expect_status_ok(rc, &error, "SQL Server condition kind should be available") != 0 ||
		    expect_true(statement_kind == SQLPARSER_STATEMENT_KIND_CONDITION,
			    "SQL Server first control unit should be a condition") != 0) {
			goto fail;
		}
		memset(&flow, 0, sizeof(flow));
		rc = sqlparser_handle_control_flow(handle, &flow, &error);
		if (expect_status_ok(rc, &error, "SQL Server control topology should be available") != 0 ||
		    expect_true(flow.roots.count == 1U && flow.node_count == 1U &&
			    flow.branch_count == 2U && flow.item_count == 4U,
			    "SQL Server control topology counts should match the source") != 0) {
			goto fail;
		}
		memset(&branch, 0, sizeof(branch));
		rc = sqlparser_control_branch_at(&flow, 0U, &branch, &error);
		if (expect_status_ok(rc, &error, "SQL Server then branch should be available") != 0 ||
		    expect_true(branch.has_condition && branch.condition_statement_index == 0U &&
			    branch.items.count == 2U,
			    "SQL Server then branch should reference its condition and two statements") != 0) {
			goto fail;
		}
		rc = sqlparser_control_branch_at(&flow, 1U, &branch, &error);
		if (expect_status_ok(rc, &error, "SQL Server else branch should be available") != 0 ||
		    expect_true(!branch.has_condition && branch.items.count == 1U,
			    "SQL Server else branch should contain one unconditional statement") != 0) {
			goto fail;
		}

		memset(&graph, 0, sizeof(graph));
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "SQL Server condition graph should be available") != 0 ||
		    expect_true(graph.has_root_block && graph.block_count == 2U &&
			    graph.value_count == 4U && graph.predicate_count == 4U,
			    "SQL Server condition graph should include the condition and subquery") != 0) {
			goto fail;
		}
		memset(&block, 0, sizeof(block));
		rc = sqlparser_query_graph_block_at(&graph, graph.root_block_index, &block, &error);
		if (expect_status_ok(rc, &error, "SQL Server condition root block should be available") != 0 ||
		    expect_true(block.kind == SQLPARSER_GRAPH_BLOCK_CONDITION,
			    "SQL Server condition root should not expose its SELECT wrapper") != 0) {
			goto fail;
		}
		memset(&value, 0, sizeof(value));
		rc = sqlparser_query_graph_value_at(&graph, 0U, &value, &error);
		if (expect_status_ok(rc, &error, "SQL Server condition bind should be available") != 0 ||
		    expect_true(value.clause == SQLPARSER_CLAUSE_KIND_CONDITION &&
			    value.has_bind && strcmp(value.bind, "enabled") == 0 &&
			    value.has_bind_position && value.bind_position == 1U,
			    "SQL Server condition bind should retain clause and global position") != 0) {
			goto fail;
		}
		memset(&graph, 0, sizeof(graph));
		rc = sqlparser_statement_query_graph(handle, 1U, &graph, &error);
		if (expect_status_ok(rc, &error, "SQL Server branch DML graph should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_result_count(&graph, 0U, &count, &error),
			    &error, "SQL Server branch OUTPUT count should be available") != 0 ||
		    expect_true(count == 1U && graph.value_count == 1U,
			    "SQL Server branch DML should expose one OUTPUT channel and WHERE bind") != 0) {
			goto fail;
		}
		memset(&value, 0, sizeof(value));
		rc = sqlparser_query_graph_value_at(&graph, 0U, &value, &error);
		if (expect_status_ok(rc, &error, "SQL Server branch bind should be available") != 0 ||
		    expect_true(value.has_bind && strcmp(value.bind, "id") == 0 &&
			    value.has_bind_position && value.bind_position == 3U,
			    "SQL Server bind positions should remain global across control units") != 0) {
			goto fail;
		}

		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "SQL Server control View JSON should succeed") != 0 ||
		    expect_true(strstr(view_json, "\"control_flow\"") != NULL &&
			    strstr(view_json, "\"keyword\":\"condition\"") != NULL &&
			    strstr(view_json, "__sqlparser_") == NULL,
			    "SQL Server control View should mirror public structures only") != 0) {
			goto fail;
		}
		sqlparser_string_free(view_json);
		view_json = NULL;

		memset(patches, 0, sizeof(patches));
		patches[0].op = SQLPARSER_PATCH_REPLACE;
		patches[0].selector = "stmt[0].clause[0]";
		patches[0].sql =
			"@enabled = 2 AND EXISTS "
			"(SELECT 1 FROM dbo.config WHERE name = @new_name)";
		patches[1].op = SQLPARSER_PATCH_REPLACE;
		patches[1].selector = "stmt[1].dml_result_target[0][0][1]";
		patches[1].sql = "INSERTED.a AS current_a";
		patch_list.items = patches;
		patch_list.count = sizeof(patches) / sizeof(patches[0]);
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "SQL Server cross-unit patch should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "SQL Server patched control SQL should reparse") != 0) {
			goto fail;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "SQL Server patched control SQL should deparse") != 0 ||
		    expect_true(strstr(deparsed_sql, "@enabled = 2") != NULL &&
			    strstr(deparsed_sql, "@new_name") != NULL &&
			    strstr(deparsed_sql, "INSERTED.a AS current_a") != NULL &&
			    strstr(deparsed_sql, "$1") == NULL,
			    "SQL Server control deparse should restore public bind and OUTPUT syntax") != 0) {
			goto fail;
		}

		memset(&graph, 0, sizeof(graph));
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "patched SQL Server condition graph should rebuild") != 0) {
			goto fail;
		}
		found_new_name = 0;
		for (index = 0U; index < graph.value_count; index++) {
			memset(&value, 0, sizeof(value));
			rc = sqlparser_query_graph_value_at(&graph, index, &value, &error);
			if (expect_status_ok(rc, &error, "patched SQL Server condition value should resolve") != 0) {
				goto fail;
			}
			if (value.has_bind && strcmp(value.bind, "new_name") == 0) {
				found_new_name = 1;
			}
		}
		if (expect_true(found_new_name,
			    "patched SQL Server condition graph should expose the replacement bind") != 0) {
			goto fail;
		}

		rc = sqlparser_handle_clone(handle, &clone, &error);
		if (expect_status_ok(rc, &error, "SQL Server control handle clone should succeed") != 0 ||
		    expect_deparse_reparse_ok(clone, "cloned SQL Server control SQL should reparse") != 0) {
			goto fail;
		}
		rc = sqlparser_deparse(clone, &clone_sql, &error);
		if (expect_status_ok(rc, &error, "cloned SQL Server control SQL should deparse") != 0 ||
		    expect_true(strcmp(clone_sql, deparsed_sql) == 0,
			    "SQL Server control clone should preserve all patched units") != 0) {
			goto fail;
		}

		before_invalid = deparsed_sql;
		deparsed_sql = NULL;
		generation = handle->generation;
		memset(&invalid_patch, 0, sizeof(invalid_patch));
		invalid_patch.op = SQLPARSER_PATCH_REPLACE;
		invalid_patch.selector = "stmt[0].clause[0]";
		invalid_patch.sql = "@enabled =";
		patch_list.items = &invalid_patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_true(rc != SQLPARSER_STATUS_OK && handle->generation == generation,
			    "failed SQL Server condition patch should not mutate the handle") != 0) {
			goto fail;
		}
		rc = sqlparser_deparse(handle, &after_invalid, &error);
		if (expect_status_ok(rc, &error, "SQL Server control SQL should deparse after failed patch") != 0 ||
		    expect_true(strcmp(before_invalid, after_invalid) == 0,
			    "failed SQL Server condition patch should preserve every control unit") != 0) {
			goto fail;
		}

		sqlparser_string_free(before_invalid);
		before_invalid = NULL;
		sqlparser_string_free(after_invalid);
		after_invalid = NULL;
		sqlparser_string_free(clone_sql);
		clone_sql = NULL;
		sqlparser_handle_destroy(clone);
		clone = NULL;
		sqlparser_handle_destroy(handle);
		handle = NULL;
	}
	return 0;

fail:
	sqlparser_string_free(view_json);
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(clone_sql);
	sqlparser_string_free(before_invalid);
	sqlparser_string_free(after_invalid);
	sqlparser_handle_destroy(clone);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_sqlserver_control_depth_limit(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	static const char prefix[] = "IF @v = 1 ";
	static const char leaf[] = "SELECT 1";
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_control_flow_view_t flow;
	sqlparser_error_t error;
	char *sql;
	size_t depths[2];
	size_t depth_index;
	size_t dialect_index;
	size_t depth;
	size_t index;
	size_t sql_length;
	int rc;

	depths[0] = SQLPARSER_CONTROL_MAX_DEPTH;
	depths[1] = SQLPARSER_CONTROL_MAX_DEPTH + 1U;
	for (depth_index = 0U; depth_index < sizeof(depths) / sizeof(depths[0]); depth_index++) {
		depth = depths[depth_index];
		sql_length = depth * (sizeof(prefix) - 1U) + (sizeof(leaf) - 1U);
		sql = (char *)malloc(sql_length + 1U);
		if (sql == NULL) {
			return 1;
		}
		for (index = 0U; index < depth; index++) {
			memcpy(sql + index * (sizeof(prefix) - 1U), prefix, sizeof(prefix) - 1U);
		}
		memcpy(sql + depth * (sizeof(prefix) - 1U), leaf, sizeof(leaf));

		for (dialect_index = 0U;
		     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
		     dialect_index++) {
			char *deparsed;
			sqlparser_handle_t *reparsed;

			handle = NULL;
			reparsed = NULL;
			deparsed = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = dialects[dialect_index];
			rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
			if (depth <= SQLPARSER_CONTROL_MAX_DEPTH) {
				if (expect_status_ok(rc, &error, "SQL Server control depth limit should parse") != 0 ||
				    expect_true(sqlparser_statement_count(handle) == depth + 1U,
					    "SQL Server nested IF should expose every condition and leaf") != 0 ||
				    expect_status_ok(sqlparser_handle_control_flow(handle, &flow, &error),
					    &error, "SQL Server nested control topology should be available") != 0 ||
				    expect_true(flow.node_count == depth,
					    "SQL Server nested control topology should retain every IF") != 0) {
					sqlparser_handle_destroy(handle);
					free(sql);
					return 1;
				}
				rc = sqlparser_deparse(handle, &deparsed, &error);
				if (expect_status_ok(rc, &error,
					    "SQL Server control SQL at the depth limit should deparse") != 0) {
					sqlparser_handle_destroy(handle);
					free(sql);
					return 1;
				}
				rc = sqlparser_parse_with_options(deparsed, &options, &reparsed, &error);
				if (expect_status_ok(rc, &error,
					    "SQL Server control SQL at the depth limit should reparse") != 0) {
					sqlparser_string_free(deparsed);
					sqlparser_handle_destroy(handle);
					free(sql);
					return 1;
				}
			} else if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT && handle == NULL,
				   "SQL Server control SQL above the depth limit should fail cleanly") != 0) {
				sqlparser_handle_destroy(handle);
				free(sql);
				return 1;
			}
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(reparsed);
			sqlparser_handle_destroy(handle);
		}
		free(sql);
	}
	return 0;
}

static int test_sqlserver_output_query_graph_and_patch(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_result_t result;
	sqlparser_graph_dml_reference_t reference;
	sqlparser_graph_relation_t relation;
	sqlparser_patch_t patches[5];
	sqlparser_patch_list_t patch_list;
	char *deparsed_sql;
	size_t count;
	size_t reference_index;
	int rc;

	sql =
		"INSERT dbo.t(a, name) "
		"OUTPUT INSERTED.id INTO dbo.audit(id) "
		"OUTPUT INSERTED.id AS id "
		"VALUES (1, N'Alice')";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "SQL Server dual OUTPUT parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "SQL Server dual OUTPUT graph should succeed") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_count(&graph, &count, &error), &error,
	                     "SQL Server dual OUTPUT dml count should succeed") != 0 ||
	    expect_true(count == 1U, "SQL Server dual OUTPUT should expose one DML") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_at(&graph, 0U, &dml, &error), &error,
	                     "SQL Server dual OUTPUT dml should be available") != 0 ||
	    expect_true(dml.kind == SQLPARSER_GRAPH_DML_INSERT, "SQL Server dual OUTPUT DML should be insert") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_result_count(&graph, 0U, &count, &error), &error,
	                     "SQL Server dual OUTPUT result count should succeed") != 0 ||
	    expect_true(count == 2U, "SQL Server dual OUTPUT should expose two result channels") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_dml_result_at(&graph, 0U, 0U, &result, &error);
	if (expect_status_ok(rc, &error, "SQL Server OUTPUT sink channel should be available") != 0 ||
	    expect_true(result.kind == SQLPARSER_GRAPH_DML_RESULT_SINK, "first OUTPUT channel should be sink") != 0 ||
	    expect_true(result.has_sink_relation != 0, "sink OUTPUT should expose its relation") != 0 ||
	    expect_true(result.sink_columns.count == 1U, "sink OUTPUT should expose one sink column") != 0 ||
	    expect_true(result.references.count == 1U, "sink OUTPUT should expose one row-image reference") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, result.sink_relation_index, &relation, &error), &error,
	                     "SQL Server OUTPUT sink relation should be available") != 0 ||
	    expect_true(relation.object_name != NULL && strcmp(relation.object_name, "audit") == 0,
	                "SQL Server OUTPUT sink relation should be audit") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(
	                         &graph, result.references, 0U, &reference_index, &error),
	                     &error, "SQL Server OUTPUT reference index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_reference_at(
	                         &graph, reference_index, &reference, &error),
	                     &error, "SQL Server OUTPUT reference should be available") != 0 ||
	    expect_true(reference.kind == SQLPARSER_GRAPH_DML_REFERENCE_TARGET_AFTER,
	                "INSERTED reference should identify the target-after row image") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_dml_result_at(&graph, 0U, 1U, &result, &error);
	if (expect_status_ok(rc, &error, "SQL Server OUTPUT client channel should be available") != 0 ||
	    expect_true(result.kind == SQLPARSER_GRAPH_DML_RESULT_CLIENT, "second OUTPUT channel should be client") != 0 ||
	    expect_true(result.has_sink_relation == 0, "client OUTPUT must not expose a sink relation") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].dml_result_targets[0][0]";
	patches[0].index = 1U;
	patches[0].sql = "INSERTED.name AS audit_name";
	patches[1].op = SQLPARSER_PATCH_REPLACE;
	patches[1].selector = "stmt[0].dml_result_target[0][1][0]";
	patches[1].sql = "INSERTED.name AS output_name";
	patches[2].op = SQLPARSER_PATCH_REPLACE;
	patches[2].selector = "stmt[0].dml_result_sink[0][0]";
	patches[2].sql = "dbo.audit_log";
	patches[3].op = SQLPARSER_PATCH_REPLACE;
	patches[3].selector = "stmt[0].dml_result_sink_column[0][0][0]";
	patches[3].sql = "audit_id";
	patches[4].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[4].selector = "stmt[0].dml_result_sink_columns[0][0]";
	patches[4].index = 1U;
	patches[4].name = "audit_name";
	patch_list.items = patches;
	patch_list.count = sizeof(patches) / sizeof(patches[0]);
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "SQL Server OUTPUT patch list should succeed") != 0 ||
	    expect_true(
		    sqlparser_query_graph_dml_count(&graph, &count, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT,
		    "SQL Server OUTPUT patch should invalidate the previous query graph view") != 0 ||
	    expect_deparse_reparse_ok(handle, "SQL Server patched OUTPUT should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "SQL Server patched OUTPUT deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "INSERT INTO dbo.t") == NULL && strstr(deparsed_sql, "dbo.t") != NULL,
	                "omitted INSERT INTO should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "INSERT  ") == NULL,
	                "omitted INSERT INTO must not leave duplicate whitespace") != 0 ||
	    expect_true(strstr(deparsed_sql, ")  OUTPUT") == NULL,
	                "OUTPUT insertion must not create duplicate whitespace") != 0 ||
	    expect_true(strstr(deparsed_sql, "OUTPUT INSERTED.id, INSERTED.name AS audit_name INTO dbo.audit_log (audit_id, audit_name)") != NULL,
	                "patched sink OUTPUT should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "OUTPUT INSERTED.name AS output_name") != NULL,
	                "patched client OUTPUT should be preserved") != 0 ||
	    expect_true(strstr(deparsed_sql, "__sqlparser_") == NULL,
	                "patched OUTPUT must not expose internal identifiers") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_DELETE_COLUMN;
	patches[0].selector = "stmt[0].dml_result_targets[0][0]";
	patches[0].index = 0U;
	patches[1].op = SQLPARSER_PATCH_DELETE_COLUMN;
	patches[1].selector = "stmt[0].dml_result_sink_columns[0][0]";
	patches[1].index = 0U;
	patch_list.items = patches;
	patch_list.count = 2U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "SQL Server OUTPUT target and sink-column deletion should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "SQL Server OUTPUT after deletion should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_output_action_marker_and_patch(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_result_t result;
	sqlparser_graph_block_t block;
	sqlparser_graph_target_t target;
	sqlparser_graph_field_t field;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *view_json;
	char *deparsed_sql;
	size_t target_index;
	size_t field_index;
	size_t marker0_count;
	size_t marker1_count;
	size_t count;
	int rc;

	sql =
		"MERGE INTO dbo.t AS t USING dbo.s AS s ON t.id = s.id "
		"WHEN MATCHED THEN UPDATE SET a = s.a "
		"WHEN NOT MATCHED THEN INSERT (id, a) VALUES (s.id, s.a) "
		"OUTPUT $action, "
		"N'kind:' + $action + s.__sqlparser_dml_action_0__ + "
		"s.__sqlparser_dml_action_1__ AS label, "
		"s.__sqlparser_dml_action_0__ AS marker0";
	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "SQL Server MERGE $action parse should succeed") != 0 ||
	    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
	                     "SQL Server MERGE $action graph should succeed") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_result_count(&graph, 0U, &count, &error), &error,
	                     "SQL Server MERGE $action result count should succeed") != 0 ||
	    expect_true(count == 1U, "SQL Server MERGE should expose one OUTPUT result") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_result_at(&graph, 0U, 0U, &result, &error), &error,
	                     "SQL Server MERGE OUTPUT result should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_block_at(&graph, result.block_index, &block, &error), &error,
	                     "SQL Server MERGE OUTPUT block should be available") != 0 ||
	    expect_true(block.targets.count == 3U, "SQL Server MERGE OUTPUT should expose three targets") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, block.targets, 0U, &target_index, &error), &error,
	                     "SQL Server MERGE $action target index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, target_index, &target, &error), &error,
	                     "SQL Server MERGE $action target should be available") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_PSEUDO,
	                "SQL Server MERGE $action should be a pseudo target") != 0 ||
	    expect_true(target.output_name != NULL && strcmp(target.output_name, "$action") == 0,
	                "SQL Server MERGE $action public name should be preserved") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	marker0_count = 0U;
	marker1_count = 0U;
	for (field_index = 0U; field_index < graph.field_count; field_index++) {
		rc = sqlparser_query_graph_field_at(&graph, field_index, &field, &error);
		if (expect_status_ok(rc, &error, "SQL Server MERGE OUTPUT field should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.column_name != NULL && strcmp(field.column_name, "__sqlparser_dml_action_0__") == 0) {
			marker0_count++;
		}
		if (field.column_name != NULL && strcmp(field.column_name, "__sqlparser_dml_action_1__") == 0) {
			marker1_count++;
		}
		if (expect_true(field.column_name == NULL ||
		                strcmp(field.column_name, "__sqlparser_dml_action_2__") != 0,
		                "internal collision marker must not be exported as a field") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	if (expect_true(marker0_count >= 2U,
	                "legitimate marker-like source column should remain visible in both targets") != 0 ||
	    expect_true(marker1_count >= 1U,
	                "second legitimate marker-like source column should remain visible") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "SQL Server MERGE $action view export should succeed") != 0 ||
	    expect_true(strstr(view_json, "$action") != NULL,
	                "SQL Server MERGE OUTPUT view should expose $action") != 0 ||
	    expect_true(strstr(view_json, "__sqlparser_dml_action_0__") != NULL &&
	                strstr(view_json, "__sqlparser_dml_action_1__") != NULL,
	                "legitimate marker-like identifiers should remain in the view") != 0 ||
	    expect_true(strstr(view_json, "__sqlparser_dml_action_2__") == NULL,
	                "generated collision marker must not leak into the view") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].dml_result_target[0][0][0]";
	patch.sql = "s.__sqlparser_dml_action_0__ AS marker_action";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "SQL Server $action target replacement should succeed") != 0 ||
	    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
	                     "SQL Server graph after $action replacement should succeed") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_result_at(&graph, 0U, 0U, &result, &error), &error,
	                     "SQL Server result after $action replacement should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_block_at(&graph, result.block_index, &block, &error), &error,
	                     "SQL Server block after $action replacement should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, block.targets, 0U, &target_index, &error), &error,
	                     "SQL Server replaced target index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, target_index, &target, &error), &error,
	                     "SQL Server replaced target should be available") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_FIELD,
	                "replacing $action with a source field should update target semantics") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	patch.sql = "$action";
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "SQL Server source target replacement with $action should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "SQL Server patched $action should reparse") != 0 ||
	    expect_status_ok(sqlparser_deparse(handle, &deparsed_sql, &error), &error,
	                     "SQL Server patched $action deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "OUTPUT $action") != NULL,
	                "SQL Server patched OUTPUT should restore $action") != 0 ||
	    expect_true(strstr(deparsed_sql, "s.__sqlparser_dml_action_0__") != NULL &&
	                strstr(deparsed_sql, "s.__sqlparser_dml_action_1__") != NULL,
	                "SQL Server deparse should preserve legitimate marker-like identifiers") != 0 ||
	    expect_true(strstr(deparsed_sql, "__sqlparser_dml_action_2__") == NULL,
	                "SQL Server deparse must not expose generated collision marker") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_nested_output_query_graph_and_patch(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t outer_dml;
	sqlparser_graph_dml_t inner_dml;
	sqlparser_graph_dml_result_t inner_result;
	sqlparser_graph_relation_t relation;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	char *deparsed_sql;
	size_t dml_count;
	size_t result_count;
	size_t parent_index;
	size_t relation_index;
	int has_parent;
	int found_source;
	int rc;

	sql =
		"INSERT INTO dbo.Log (id) SELECT d.id "
		"FROM (DELETE FROM dbo.Items OUTPUT DELETED.id WHERE state = @state) AS d";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "nested SQL Server DML parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "nested SQL Server DML graph should succeed") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_count(&graph, &dml_count, &error), &error,
	                     "nested SQL Server DML count should succeed") != 0 ||
	    expect_true(dml_count == 2U, "nested SQL Server DML should expose outer and inner DML") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_at(&graph, 0U, &outer_dml, &error), &error,
	                     "outer nested DML should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_at(&graph, 1U, &inner_dml, &error), &error,
	                     "inner nested DML should be available") != 0 ||
	    expect_true(outer_dml.kind == SQLPARSER_GRAPH_DML_INSERT, "outer nested DML should be insert") != 0 ||
	    expect_true(inner_dml.kind == SQLPARSER_GRAPH_DML_DELETE, "inner nested DML should be delete") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_parent(
	                         &graph, 1U, &parent_index, &has_parent, &error),
	                     &error, "nested DML parent should be available") != 0 ||
	    expect_true(has_parent != 0 && parent_index == 0U, "inner DML should belong to the outer DML") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_result_count(
	                         &graph, 1U, &result_count, &error),
	                     &error, "inner DML result count should succeed") != 0 ||
	    expect_true(result_count == 1U, "inner DML should expose one client OUTPUT") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_result_at(
	                         &graph, 1U, 0U, &inner_result, &error),
	                     &error, "inner DML result should be available") != 0 ||
	    expect_true(inner_result.kind == SQLPARSER_GRAPH_DML_RESULT_CLIENT,
	                "nested DML OUTPUT should be a client result") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	found_source = 0;
	for (relation_index = 0U; relation_index < graph.relation_count; relation_index++) {
		rc = sqlparser_query_graph_relation_at(&graph, relation_index, &relation, &error);
		if (expect_status_ok(rc, &error, "nested DML relation should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (relation.alias_name != NULL && strcmp(relation.alias_name, "d") == 0) {
			found_source = relation.kind == SQLPARSER_GRAPH_REL_DERIVED &&
				relation.has_source_block != 0 &&
				relation.source_block_index == inner_result.block_index;
			break;
		}
	}
	if (expect_true(found_source, "outer DML source should point to the inner OUTPUT block") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = "stmt[0].dml_result_target[1][0][0]";
	patches[0].sql = "DELETED.id AS id";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].dml_result_targets[1][0]";
	patches[1].index = 1U;
	patches[1].sql = "DELETED.state AS old_state";
	patch_list.items = patches;
	patch_list.count = sizeof(patches) / sizeof(patches[0]);
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "nested SQL Server OUTPUT patch should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "nested SQL Server OUTPUT patch should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "nested SQL Server OUTPUT deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "FROM (DELETE FROM dbo.Items") != NULL,
	                "nested DML should retain its public table-source form") != 0 ||
	    expect_true(strstr(deparsed_sql, "OUTPUT DELETED.id AS id, DELETED.state AS old_state") != NULL,
	                "nested DML should contain patched OUTPUT targets") != 0 ||
	    expect_true(strstr(deparsed_sql, "__sqlparser_dml_source_") == NULL,
	                "nested DML must not expose its internal CTE name") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_DELETE_COLUMN;
	patches[0].selector = "stmt[0].dml_result_targets[1][0]";
	patches[0].index = 1U;
	patch_list.items = patches;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "nested SQL Server OUTPUT target deletion should succeed") != 0 ||
	    expect_deparse_reparse_ok(handle, "nested SQL Server OUTPUT after deletion should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_delete_output_source_graph_and_patch(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	size_t dialect_index;

	for (dialect_index = 0U; dialect_index < sizeof(dialects) / sizeof(dialects[0]); dialect_index++) {
		sqlparser_parse_options_t options;
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_result_t result;
		sqlparser_graph_dml_reference_t reference;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *deparsed_sql;
		size_t reference_index;
		int rc;

		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			"DELETE t OUTPUT DELETED.id, s.code AS source_code "
			"FROM dbo.t AS t JOIN dbo.s AS s ON t.id = s.id",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "SQL Server-compatible DELETE source OUTPUT should parse") != 0 ||
		    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
		                     "SQL Server-compatible DELETE source OUTPUT graph should succeed") != 0 ||
		    expect_true(graph.relation_count == 2U,
		                "DELETE source OUTPUT graph should not duplicate the target relation") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_at(&graph, 0U, &dml, &error), &error,
		                     "DELETE source OUTPUT dml should be available") != 0 ||
		    expect_true(dml.kind == SQLPARSER_GRAPH_DML_DELETE &&
		                dml.has_target_relation != 0 && dml.target_relation_index == 0U,
		                "DELETE source OUTPUT should identify the real delete target") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_result_at(&graph, 0U, 0U, &result, &error), &error,
		                     "DELETE source OUTPUT result should be available") != 0 ||
		    expect_true(result.references.count == 2U,
		                "DELETE source OUTPUT should expose target and source references") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (expect_status_ok(sqlparser_query_graph_span_index_at(
		                         &graph, result.references, 0U, &reference_index, &error),
		                     &error, "DELETE target-before reference index should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_reference_at(
		                         &graph, reference_index, &reference, &error),
		                     &error, "DELETE target-before reference should be available") != 0 ||
		    expect_true(reference.kind == SQLPARSER_GRAPH_DML_REFERENCE_TARGET_BEFORE &&
		                reference.relation_index == 0U,
		                "DELETED reference should belong to the delete target") != 0 ||
		    expect_status_ok(sqlparser_query_graph_span_index_at(
		                         &graph, result.references, 1U, &reference_index, &error),
		                     &error, "DELETE source reference index should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_reference_at(
		                         &graph, reference_index, &reference, &error),
		                     &error, "DELETE source reference should be available") != 0 ||
		    expect_true(reference.kind == SQLPARSER_GRAPH_DML_REFERENCE_SOURCE &&
		                reference.relation_index == 1U,
		                "source reference should belong to the joined source") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = "stmt[0].dml_result_target[0][0][1]";
		patch.sql = "s.name AS source_name";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "DELETE source OUTPUT target patch should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "patched DELETE source OUTPUT should reparse") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &deparsed_sql, &error), &error,
		                     "patched DELETE source OUTPUT should deparse") != 0 ||
		    expect_true(strstr(deparsed_sql, "DELETE t OUTPUT DELETED.id, s.name AS source_name FROM dbo.t t JOIN dbo.s s") != NULL,
		                "DELETE alias and source FROM form should be restored after patch") != 0 ||
		    expect_true(strstr(deparsed_sql, " USING ") == NULL,
		                "DELETE source deparse must not expose internal USING") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"DELETE [t]]x] OUTPUT DELETED.id "
			"FROM dbo.t AS \"t]x\" WHERE \"t]x\".id = 1",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "delimited DELETE alias OUTPUT should parse") != 0 ||
		    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
		                     "delimited DELETE alias OUTPUT graph should succeed") != 0 ||
		    expect_true(graph.relation_count == 1U,
		                "equivalent delimited DELETE aliases must identify one target relation") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_at(&graph, 0U, &dml, &error), &error,
		                     "delimited DELETE alias DML should be available") != 0 ||
		    expect_true(dml.has_target_relation != 0 && dml.target_relation_index == 0U,
		                "delimited DELETE alias should identify the source relation as its target") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_result_at(&graph, 0U, 0U, &result, &error), &error,
		                     "delimited DELETE alias OUTPUT result should be available") != 0 ||
		    expect_true(result.references.count == 1U,
		                "delimited DELETE alias OUTPUT should expose one target reference") != 0 ||
		    expect_deparse_reparse_ok(handle, "delimited DELETE alias OUTPUT should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse("DELETE FROM t USING t WHERE t.id = 1", &handle, &error);
		if (expect_status_ok(rc, &error, "PostgreSQL DELETE USING should parse") != 0 ||
		    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
		                     "PostgreSQL DELETE USING graph should succeed") != 0 ||
		    expect_true(graph.relation_count == 2U,
		                "native DELETE USING relations must not use SQL Server duplicate suppression") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int test_sqlserver_output_failure_is_non_destructive(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	size_t dialect_index;

	for (dialect_index = 0U; dialect_index < sizeof(dialects) / sizeof(dialects[0]); dialect_index++) {
		sqlparser_parse_options_t options;
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *before_sql;
		char *after_sql;
		int rc;

		handle = NULL;
		before_sql = NULL;
		after_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			"DELETE FROM dbo.t OUTPUT DELETED.id WHERE id = @id",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "SQL Server-compatible OUTPUT parse should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "SQL Server-compatible OUTPUT should reparse") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &before_sql, &error), &error,
		                     "SQL Server-compatible OUTPUT baseline deparse should succeed") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_DELETE_COLUMN;
		patch.selector = "stmt[0].dml_result_targets[0][0]";
		patch.index = 0U;
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_true(rc == SQLPARSER_STATUS_UNSUPPORTED,
		                "deleting the last OUTPUT target should be rejected") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &after_sql, &error), &error,
		                     "rejected OUTPUT patch should leave handle deparseable") != 0 ||
		    expect_true(strcmp(before_sql, after_sql) == 0,
		                "rejected OUTPUT patch must not modify SQL") != 0 ||
		    expect_deparse_reparse_ok(handle, "rejected OUTPUT patch should leave valid SQL") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_string_free(after_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(before_sql);
		sqlparser_string_free(after_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_sqlserver_output_sink_patch_validation(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	size_t dialect_index;

	for (dialect_index = 0U; dialect_index < sizeof(dialects) / sizeof(dialects[0]); dialect_index++) {
		sqlparser_parse_options_t options;
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_result_t result;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *before_sql;
		char *after_sql;
		int rc;

		handle = NULL;
		before_sql = NULL;
		after_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			"INSERT INTO dbo.t(a) OUTPUT INSERTED.id INTO dbo.audit(id) VALUES (1)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "SQL Server-compatible OUTPUT sink parse should succeed") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &before_sql, &error), &error,
		                     "SQL Server-compatible OUTPUT sink baseline deparse should succeed") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = "stmt[0].dml_result_sink[0][0]";
		patch.sql = "dbo.audit; DROP TABLE dbo.t";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_true(rc == SQLPARSER_STATUS_PARSE_ERROR,
		                "invalid OUTPUT sink relation should be rejected") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &after_sql, &error), &error,
		                     "rejected OUTPUT sink relation patch should leave handle deparseable") != 0 ||
		    expect_true(strcmp(before_sql, after_sql) == 0,
		                "rejected OUTPUT sink relation patch must not modify SQL") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_string_free(after_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(after_sql);
		after_sql = NULL;

		patch.selector = "stmt[0].dml_result_sink_column[0][0][0]";
		patch.sql = "audit_id, injected";
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_true(rc == SQLPARSER_STATUS_PARSE_ERROR,
		                "invalid OUTPUT sink column should be rejected") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &after_sql, &error), &error,
		                     "rejected OUTPUT sink column patch should leave handle deparseable") != 0 ||
		    expect_true(strcmp(before_sql, after_sql) == 0,
		                "rejected OUTPUT sink column patch must not modify SQL") != 0 ||
		    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
		                     "rejected OUTPUT sink patch should leave graph available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_result_at(&graph, 0U, 0U, &result, &error), &error,
		                     "rejected OUTPUT sink patch should leave result available") != 0 ||
		    expect_true(result.has_sink_relation != 0 && result.sink_columns.count == 1U,
		                "rejected OUTPUT sink patch should preserve sink metadata") != 0 ||
		    expect_deparse_reparse_ok(handle, "rejected OUTPUT sink patches should leave valid SQL") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_string_free(after_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_string_free(before_sql);
		sqlparser_string_free(after_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_sqlserver_nested_output_resource_limit(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	const char *sql;
	size_t dialect_index;

	sql =
		"INSERT INTO dbo.Log (id) SELECT d.id "
		"FROM (DELETE FROM dbo.Items OUTPUT DELETED.id WHERE state = @state) AS d";
	for (dialect_index = 0U; dialect_index < sizeof(dialects) / sizeof(dialects[0]); dialect_index++) {
		sqlparser_parse_options_t options;
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		size_t dml_count;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		options.limits.max_statement_count = 1U;
		rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
		if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT,
		                "nested DML should honor max_statement_count") != 0 ||
		    expect_true(handle == NULL,
		                "resource-limited nested DML parse must not return a handle") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		options.limits.max_statement_count = 2U;
		rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "nested DML at configured limit should parse") != 0 ||
		    expect_status_ok(sqlparser_statement_query_graph(handle, 0U, &graph, &error), &error,
		                     "nested DML at configured limit should expose graph") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_count(&graph, &dml_count, &error), &error,
		                     "nested DML at configured limit should expose count") != 0 ||
		    expect_true(dml_count == 2U,
		                "nested DML at configured limit should expose outer and inner DML") != 0 ||
		    expect_deparse_reparse_ok(handle, "nested DML at configured limit should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int json_string_is(json_t *object, const char *key, const char *expected);

static int test_query_graph_json_and_patch_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *view_json;
	char *deparsed_sql;
	json_error_t json_error;
	json_t *root;
	json_t *statement;
	json_t *graph_json;
	json_t *dml_json;
	json_t *rows_json;
	json_t *literal_json;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	int rc;

	sql = "INSERT INTO public.users (id, name) VALUES (1, 'bob'), (2, 'alice')";
	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	root = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 1, &view_json, &error);
	if (expect_status_ok(rc, &error, "query graph JSON export should succeed") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"query_graph\"") != NULL, "view JSON should contain query_graph") != 0 ||
	    expect_true(strstr(view_json, "\"objects\"") == NULL, "view JSON should not contain old objects") != 0 ||
	    expect_true(strstr(view_json, "\"clauses\"") == NULL, "view JSON should not contain old clauses") != 0 ||
	    expect_true(strstr(view_json, "\"sql\":") == NULL, "view JSON should not store per-node SQL text") != 0 ||
	    expect_true(strstr(view_json, "stmt[0].insert_cell[1][1]") != NULL, "view JSON should contain insert cell selector") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	root = json_loads(view_json, 0, &json_error);
	if (expect_true(root != NULL, "query graph JSON should decode") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	statement = json_array_get(json_object_get(root, "statements"), 0);
	graph_json = json_object_get(statement, "query_graph");
	dml_json = json_object_get(graph_json, "dml");
	rows_json = json_object_get(dml_json, "rows");
	literal_json = json_object_get(json_array_get(rows_json, 3), "literal");
	if (expect_true(json_is_object(graph_json), "query graph object should exist") != 0 ||
	    expect_true(json_array_size(json_object_get(dml_json, "target_columns")) == 2U, "insert graph should expose two target columns") != 0 ||
	    expect_true(json_array_size(rows_json) == 4U, "insert graph should expose four value cells") != 0 ||
	    expect_true(json_string_is(literal_json, "kind", "string"), "literal kind should be string") != 0 ||
	    expect_true(json_string_is(literal_json, "string_value", "alice"), "literal value should be alice") != 0) {
		json_decref(root);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	json_decref(root);
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph C view should be available") != 0 ||
	    expect_true(graph.has_dml != 0, "insert graph should expose dml") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 2U, "insert dml target column count mismatch") != 0 ||
	    expect_true(dml.rows.count == 4U, "insert dml row cell count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = "stmt[0].insert_cell[1][1]";
	patches[0].sql = "'carol'";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].insert_columns";
	patches[1].index = 2U;
	patches[1].name = "age";
	patches[1].default_sql = "18";
	patch_list.items = patches;
	patch_list.count = 2U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "query graph patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "query graph patched deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'carol'") != NULL, "patched SQL should contain carol") != 0 ||
	    expect_true(strstr(deparsed_sql, "age") != NULL, "patched SQL should contain added column") != 0 ||
	    expect_true(strstr(deparsed_sql, "18") != NULL, "patched SQL should contain default value") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_DELETE_ROW;
	patches[0].selector = "stmt[0].insert_row[0]";
	patch_list.items = patches;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "query graph delete row patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "query graph delete row deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'bob'") == NULL, "delete row should remove first row") != 0 ||
	    expect_true(strstr(deparsed_sql, "'carol'") != NULL, "delete row should keep remaining row") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_DELETE_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 2U;
	patch_list.items = patches;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "query graph delete column patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "query graph delete column deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "age") == NULL, "delete column should remove added column") != 0 ||
	    expect_true(strstr(deparsed_sql, "18") == NULL, "delete column should remove added value") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int json_string_is(json_t *object, const char *key, const char *expected)
{
	const char *value;

	if (!json_is_object(object) || key == NULL || expected == NULL) {
		return 0;
	}
	value = json_string_value(json_object_get(object, key));
	return value != NULL && strcmp(value, expected) == 0;
}

static int json_integer_is(json_t *object, const char *key, json_int_t expected)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return json_is_integer(value) && json_integer_value(value) == expected;
}

static int json_key_is_null(json_t *object, const char *key)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return value == NULL || json_is_null(value);
}

static int json_array_length_is(json_t *object, const char *key, size_t expected)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	if (value == NULL && expected == 0U) {
		return 1;
	}
	return json_is_array(value) && json_array_size(value) == expected;
}

static int json_optional_array_is_valid(json_t *object, const char *key)
{
	json_t *value;

	if (!json_is_object(object) || key == NULL) {
		return 0;
	}
	value = json_object_get(object, key);
	return value == NULL || json_is_array(value);
}

static const char *json_empty_array_key(json_t *node)
{
	const char *key;
	const char *nested_key;
	json_t *value;
	size_t index;

	if (json_is_object(node)) {
		json_object_foreach(node, key, value)
		{
			if (json_is_array(value) && json_array_size(value) == 0U) {
				return key;
			}
			nested_key = json_empty_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
		return NULL;
	}
	if (json_is_array(node)) {
		json_array_foreach(node, index, value)
		{
			nested_key = json_empty_array_key(value);
			if (nested_key != NULL) {
				return nested_key;
			}
		}
	}
	return NULL;
}

static int json_target_path_entry_is(
	json_t *column,
	size_t index,
	const char *kind,
	const char *name,
	json_int_t arg_index)
{
	json_t *path;
	json_t *entry;

	if (!json_is_object(column) || kind == NULL) {
		return 0;
	}
	path = json_object_get(column, "target_path");
	if (!json_is_array(path) || index >= json_array_size(path)) {
		return 0;
	}
	entry = json_array_get(path, index);
	if (!json_string_is(entry, "kind", kind)) {
		return 0;
	}
	if (name != NULL) {
		if (!json_string_is(entry, "name", name)) {
			return 0;
		}
	} else if (!json_key_is_null(entry, "name")) {
		return 0;
	}
	return json_integer_is(entry, "arg_index", arg_index);
}

static int expect_query_graph_shape(json_t *statement)
{
	json_t *graph;
	const char *empty_array_key;

	graph = json_object_get(statement, "query_graph");
	if (expect_true(json_is_object(graph), "view JSON should expose query_graph") != 0 ||
	    expect_true(json_object_get(statement, "objects") == NULL, "view JSON should not expose old objects") != 0 ||
	    expect_true(json_object_get(statement, "clauses") == NULL, "view JSON should not expose old clauses") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "blocks"), "query_graph blocks should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "relations"), "query_graph relations should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "targets"), "query_graph targets should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "fields"), "query_graph fields should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "values"), "query_graph values should be an array when present") != 0 ||
	    expect_true(json_optional_array_is_valid(graph, "sets"), "query_graph sets should be an array when present") != 0) {
		return 1;
	}
	empty_array_key = json_empty_array_key(graph);
	if (expect_true(empty_array_key == NULL, "query_graph should omit empty arrays") != 0) {
		return 1;
	}
	return 0;
}

static const char *graph_clause_name_from_keyword(const char *keyword)
{
	if (keyword == NULL) {
		return NULL;
	}
	if (strcmp(keyword, "select") == 0) {
		return "select_list";
	}
	if (strcmp(keyword, "order") == 0) {
		return "order_by";
	}
	if (strcmp(keyword, "group") == 0) {
		return "group_by";
	}
	if (strcmp(keyword, "set") == 0) {
		return "set_list";
	}
	return keyword;
}

static json_t *find_view_column_json(
	json_t *statement,
	const char *table_name,
	const char *column_name,
	const char *keyword,
	size_t skip)
{
	json_t *graph;
	json_t *fields;
	json_t *relations;
	json_t *field;
	const char *clause_name;
	size_t field_index;

	if (!json_is_object(statement) || column_name == NULL) {
		return NULL;
	}
	graph = json_object_get(statement, "query_graph");
	fields = json_object_get(graph, "fields");
	relations = json_object_get(graph, "relations");
	clause_name = graph_clause_name_from_keyword(keyword);
	json_array_foreach(fields, field_index, field)
	{
		json_t *relation_ref;
		json_t *relation;
		const char *object_table;

		if (!json_string_is(field, "column", column_name)) {
			continue;
		}
		if (clause_name != NULL && !json_string_is(field, "clause", clause_name)) {
			continue;
		}
		if (table_name != NULL) {
			relation_ref = json_object_get(field, "relation");
			if (!json_is_integer(relation_ref)) {
				continue;
			}
			relation = json_array_get(relations, (size_t)json_integer_value(relation_ref));
			object_table = json_string_value(json_object_get(relation, "table"));
			if (object_table == NULL || strcmp(object_table, table_name) != 0) {
				continue;
			}
		}
		if (skip > 0U) {
			skip--;
			continue;
		}
		return field;
	}
	return NULL;
}

static int view_json_parse_statement(
	sqlparser_dialect_t dialect,
	const char *sql,
	json_t **out_root,
	json_t **out_statement)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	json_error_t json_error;
	char *view_json;
	int rc;

	if (out_root == NULL || out_statement == NULL) {
		fprintf(stderr, "FAIL: output JSON arguments must not be NULL\n");
		return 1;
	}
	*out_root = NULL;
	*out_statement = NULL;
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "view semantics parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	sqlparser_handle_destroy(handle);
	if (expect_status_ok(rc, &error, "view semantics JSON export should succeed") != 0) {
		return 1;
	}
	*out_root = json_loads(view_json, 0, &json_error);
	sqlparser_string_free(view_json);
	if (expect_true(*out_root != NULL, "view semantics JSON should decode") != 0) {
		return 1;
	}
	*out_statement = json_array_get(json_object_get(*out_root, "statements"), 0U);
	if (expect_true(json_is_object(*out_statement), "view semantics statement should exist") != 0) {
		json_decref(*out_root);
		*out_root = NULL;
		*out_statement = NULL;
		return 1;
	}
	if (expect_query_graph_shape(*out_statement) != 0) {
		json_decref(*out_root);
		*out_root = NULL;
		*out_statement = NULL;
		return 1;
	}
	return 0;
}

static int expect_view_column_shape(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *table_name,
	const char *column_name,
	const char *keyword,
	const char *path_kind,
	const char *path_name,
	int has_path_arg_index,
	json_int_t path_arg_index)
{
	json_t *root;
	json_t *statement;
	json_t *column;
	int failed;

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(dialect, sql, &root, &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, table_name, column_name, keyword, 0U);
	failed = expect_true(column != NULL, "view column shape should exist") != 0;
	if (!failed && path_kind != NULL &&
	    strcmp(path_kind, "direct_column") != 0 &&
	    strcmp(path_kind, "qualified_star") != 0 &&
	    strcmp(path_kind, "star") != 0 &&
	    strcmp(path_kind, "not_output") != 0) {
		failed = expect_true(
			json_target_path_entry_is(column, 0U, path_kind, path_name, has_path_arg_index ? path_arg_index : 0),
			"view column target_path mismatch") != 0;
	} else if (!failed) {
		failed = expect_true(json_array_length_is(column, "target_path", 0U), "view column target_path should be empty") != 0;
	}
	json_decref(root);
	return failed ? 1 : 0;
}

static int test_query_graph_bind_fields(void)
{
	struct bind_case {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *set_bind_key;
		const char *where_bind_key;
		sqlparser_bind_kind_t set_bind_kind;
		sqlparser_bind_kind_t where_bind_kind;
		size_t set_bind_position;
		size_t where_bind_position;
		const char *set_bind_sql;
		const char *where_bind_sql;
	};
	static const struct bind_case cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "UPDATE servers SET ip = $1 WHERE id = $2", "1", "2", SQLPARSER_BIND_KIND_POSITIONAL, SQLPARSER_BIND_KIND_POSITIONAL, 1U, 2U, "$1", "$2"},
		{SQLPARSER_DIALECT_MYSQL, "UPDATE servers SET ip = ? WHERE id = ?", "1", "2", SQLPARSER_BIND_KIND_POSITIONAL, SQLPARSER_BIND_KIND_POSITIONAL, 1U, 2U, "?", "?"},
		{SQLPARSER_DIALECT_ORACLE, "UPDATE SERVERS SET IP = :aaa WHERE ID = :id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, ":aaa", ":id"},
		{SQLPARSER_DIALECT_SQLSERVER, "UPDATE dbo.servers SET ip = @aaa WHERE id = @id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, "@aaa", "@id"},
		{SQLPARSER_DIALECT_DAMENG, "UPDATE servers SET ip = :aaa WHERE id = :id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, ":aaa", ":id"},
		{SQLPARSER_DIALECT_VASTBASE_POSTGRESQL, "UPDATE servers SET ip = $1 WHERE id = $2", "1", "2", SQLPARSER_BIND_KIND_POSITIONAL, SQLPARSER_BIND_KIND_POSITIONAL, 1U, 2U, "$1", "$2"},
		{SQLPARSER_DIALECT_VASTBASE_MYSQL, "UPDATE servers SET ip = ? WHERE id = ?", "1", "2", SQLPARSER_BIND_KIND_POSITIONAL, SQLPARSER_BIND_KIND_POSITIONAL, 1U, 2U, "?", "?"},
		{SQLPARSER_DIALECT_VASTBASE_ORACLE, "UPDATE SERVERS SET IP = :aaa WHERE ID = :id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, ":aaa", ":id"},
		{SQLPARSER_DIALECT_VASTBASE_SQLSERVER, "UPDATE dbo.servers SET ip = @aaa WHERE id = @id", "aaa", "id", SQLPARSER_BIND_KIND_NAMED, SQLPARSER_BIND_KIND_NAMED, 1U, 2U, "@aaa", "@id"}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_handle_t *handle;
		char *deparsed_sql;
		const char *replacement_set_list;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_assignment_t assignment;
		sqlparser_graph_value_t where_value;
		int rc;

		handle = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "bind graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "bind graph should be available") != 0 ||
		    expect_true(graph.has_dml != 0, "UPDATE graph should expose dml") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "UPDATE dml should be available") != 0 ||
		    expect_true(dml.assignments.count == 1U, "UPDATE should expose one assignment") != 0 ||
		    expect_true(graph.value_count == 1U, "UPDATE should expose one WHERE value") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "assignment should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &where_value, &error), &error, "WHERE value should be available") != 0 ||
		    expect_true(assignment.has_bind != 0 && strcmp(assignment.bind, cases[index].set_bind_key) == 0, "SET bind key mismatch") != 0 ||
		    expect_true(where_value.has_bind != 0 && strcmp(where_value.bind, cases[index].where_bind_key) == 0, "WHERE bind key mismatch") != 0 ||
		    expect_true(assignment.bind_kind == cases[index].set_bind_kind, "SET bind kind mismatch") != 0 ||
		    expect_true(where_value.bind_kind == cases[index].where_bind_kind, "WHERE bind kind mismatch") != 0 ||
		    expect_true(assignment.has_bind_position != 0 && assignment.bind_position == cases[index].set_bind_position, "SET bind position mismatch") != 0 ||
		    expect_true(where_value.has_bind_position != 0 && where_value.bind_position == cases[index].where_bind_position, "WHERE bind position mismatch") != 0 ||
		    expect_true(assignment.has_bind_sql != 0 && strcmp(assignment.bind_sql, cases[index].set_bind_sql) == 0, "SET bind SQL mismatch") != 0 ||
		    expect_true(where_value.has_bind_sql != 0 && strcmp(where_value.bind_sql, cases[index].where_bind_sql) == 0, "WHERE bind SQL mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		replacement_set_list = "ip = :aaa, host = :host";
		if (cases[index].dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		    cases[index].dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL) {
			replacement_set_list = "ip = $1, host = $3";
		} else if (cases[index].dialect == SQLPARSER_DIALECT_MYSQL ||
		           cases[index].dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL) {
			replacement_set_list = "ip = ?, host = ?";
		} else if (cases[index].dialect == SQLPARSER_DIALECT_SQLSERVER ||
		           cases[index].dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER) {
			replacement_set_list = "ip = @aaa, host = @host";
		}
		rc = sqlparser_statement_set_clause_sql(handle, 0U, 0U, replacement_set_list, &error);
		if (expect_status_ok(rc, &error, "bind SET list replacement should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "bind SET list replacement should produce parseable SQL") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "bind SET list deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, "host") != NULL, "bind SET list deparse should contain added assignment") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t first;
		sqlparser_graph_value_t second;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_ORACLE;
		rc = sqlparser_parse_with_options("SELECT abc FROM table1 WHERE abc LIKE :1 AND def LIKE ?", &options, &handle, &error);
		if (expect_status_ok(rc, &error, "oracle mixed bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "oracle mixed bind graph should be available") != 0 ||
		    expect_true(graph.value_count == 2U, "oracle mixed bind should expose two values") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &first, &error), &error, "oracle first bind should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 1U, &second, &error), &error, "oracle second bind should be available") != 0 ||
		    expect_true(first.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && strcmp(first.bind, "1") == 0, "oracle :1 bind key mismatch") != 0 ||
		    expect_true(first.has_bind_position != 0 && first.bind_position == 1U, "oracle :1 bind position mismatch") != 0 ||
		    expect_true(first.has_bind_sql != 0 && strcmp(first.bind_sql, ":1") == 0, "oracle :1 bind SQL mismatch") != 0 ||
		    expect_true(second.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && strcmp(second.bind, "2") == 0, "oracle ? bind key mismatch") != 0 ||
		    expect_true(second.has_bind_position != 0 && second.bind_position == 2U, "oracle ? bind position mismatch") != 0 ||
		    expect_true(second.has_bind_sql != 0 && strcmp(second.bind_sql, "?") == 0, "oracle ? bind SQL mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		static const sqlparser_dialect_t dialects[] = {
			SQLPARSER_DIALECT_SQLSERVER,
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER
		};
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		size_t dialect_index;
		int rc;

		for (dialect_index = 0U;
		     dialect_index <
			     sizeof(dialects) / sizeof(dialects[0]);
		     dialect_index++) {
			handle = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = dialects[dialect_index];
			rc = sqlparser_parse_with_options(
				"IF @go = 1 SELECT TOP (?) id FROM dbo.t WHERE x = ? ELSE SELECT 0",
				&options,
				&handle,
				&error);
			if (expect_status_ok(
				    rc,
				    &error,
				    "SQL Server control TOP bind parse should succeed") != 0) {
				return 1;
			}
			rc = sqlparser_statement_query_graph(
				handle,
				1U,
				&graph,
				&error);
			if (expect_status_ok(
				    rc,
				    &error,
				    "SQL Server control TOP graph should be available") != 0 ||
			    expect_true(
				    graph.value_count == 1U,
				    "control TOP bind should not enter field values") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_value_at(
					    &graph,
					    0U,
					    &value,
					    &error),
				    &error,
				    "SQL Server control WHERE bind should be available") != 0 ||
			    expect_true(
				    value.has_bind_position != 0 &&
					    value.bind_position == 3U,
				    "control condition and TOP binds should precede WHERE bind") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_handle_destroy(handle);
		}
	}

	{
		static const sqlparser_dialect_t dialects[] = {
			SQLPARSER_DIALECT_SQLSERVER,
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER
		};
		static const char sql[] =
			"SELECT TOP (@a) [id] FROM ("
			"SELECT TOP (@b) [id] FROM ("
			"SELECT TOP (@a) [id] FROM [dbo].[users] "
			"WHERE [x] = @where) AS [v]) AS [u]";
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		size_t dialect_index;
		int rc;

		for (dialect_index = 0U;
		     dialect_index <
			     sizeof(dialects) / sizeof(dialects[0]);
		     dialect_index++) {
			handle = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = dialects[dialect_index];
			rc = sqlparser_parse_with_options(
				sql,
				&options,
				&handle,
				&error);
			if (expect_status_ok(
				    rc,
				    &error,
				    "SQL Server repeated TOP bind parse should succeed") != 0) {
				return 1;
			}
			rc = sqlparser_statement_query_graph(
				handle,
				0U,
				&graph,
				&error);
			if (expect_status_ok(
				    rc,
				    &error,
				    "SQL Server repeated TOP bind graph should be available") != 0 ||
			    expect_true(
				    graph.value_count == 1U,
				    "repeated TOP binds should not enter field values") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_value_at(
					    &graph,
					    0U,
					    &value,
					    &error),
				    &error,
				    "SQL Server repeated TOP WHERE bind should be available") != 0 ||
			    expect_true(
				    value.has_bind != 0 &&
					    strcmp(value.bind, "where") == 0 &&
					    value.has_bind_position != 0 &&
					    value.bind_position == 4U,
				    "interleaved repeated TOP binds should preserve public order") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_handle_destroy(handle);
		}
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_ORACLE;
		rc = sqlparser_parse_with_options(
			"SELECT IP, AREACODE, AREANAME, STATE, MSTSCPORT, NTUID, NTPWD,WORKER, "
			"WEBSITE,MSDEPLOYPORT, \"UID\", PWD, KEY_ENCRYPTION, MODIFYTIME "
			"FROM (SELECT a.*, ROWNUM RN FROM SERVERS a WHERE ROWNUM <= :endRow) "
			"WHERE RN > :startRow",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "oracle rownum pagination bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "oracle rownum pagination graph should be available") != 0 ||
		    expect_true(graph.relation_count >= 2U, "oracle rownum pagination should expose base and derived relations") != 0 ||
		    expect_true(graph.target_count >= 15U, "oracle rownum pagination should expose output targets") != 0 ||
		    expect_true(graph.value_count == 1U, "oracle rownum pagination should only expose field-bound values") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "oracle rownum field value should be available") != 0 ||
		    expect_true(value.bind_kind == SQLPARSER_BIND_KIND_NAMED && strcmp(value.bind, "startRow") == 0, "oracle startRow bind mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_SQLSERVER;
		rc = sqlparser_parse_with_options(
			"SELECT TOP (?) [id] FROM [dbo].[users] WHERE [name] LIKE ? ORDER BY [id]",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "sqlserver TOP bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "sqlserver TOP bind graph should be available") != 0 ||
		    expect_true(graph.value_count == 1U, "TOP bind should not enter field values") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "sqlserver WHERE value should be available") != 0 ||
		    expect_true(value.has_bind_position != 0 && value.bind_position == 2U, "WHERE bind should keep global position after TOP") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_assignment_t assignment;
		sqlparser_graph_value_t value;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_MYSQL;
		rc = sqlparser_parse_with_options("UPDATE users SET a = ? WHERE id = ?; UPDATE users SET b = ? WHERE id = ?", &options, &handle, &error);
		if (expect_status_ok(rc, &error, "multi-statement bind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 1U, &graph, &error);
		if (expect_status_ok(rc, &error, "second statement graph should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "second statement dml should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "second statement assignment should be available") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "second statement WHERE value should be available") != 0 ||
		    expect_true(assignment.has_bind_position != 0 && assignment.bind_position == 3U, "second SET bind position should be global") != 0 ||
		    expect_true(value.has_bind_position != 0 && value.bind_position == 4U, "second WHERE bind position should be global") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_query_graph_like_escape_semantics(void)
{
	struct like_escape_case {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *column_name;
		const char *operator_name;
		sqlparser_graph_value_kind_t value_kind;
		const char *value_literal;
		sqlparser_bind_kind_t value_bind_kind;
		const char *value_bind_key;
		const char *value_bind_sql;
		size_t value_bind_position;
		sqlparser_graph_like_escape_kind_t escape_kind;
		const char *escape_literal;
		sqlparser_bind_kind_t escape_bind_kind;
		const char *escape_bind_key;
		const char *escape_bind_sql;
		size_t escape_bind_position;
		int expect_json_escape;
	};
	static const struct like_escape_case cases[] = {
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone LIKE :pattern",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			":pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_NONE,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			0
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone NOT LIKE '123!_678' ESCAPE '!'",
			"phone",
			"NOT LIKE",
			SQLPARSER_GRAPH_VALUE_LITERAL,
			"123!_678",
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL,
			"!",
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone LIKE :pattern ESCAPE '!'",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			":pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL,
			"!",
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone LIKE :pattern ESCAPE :escape_char",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			":pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"escape_char",
			":escape_char",
			2U,
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT d.id FROM (SELECT id, phone FROM users) d WHERE d.phone LIKE :pattern ESCAPE '!'",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			":pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL,
			"!",
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone LIKE :pattern ESCAPE upper('!')",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			":pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			1
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM users WHERE phone LIKE $1 ESCAPE $2",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_POSITIONAL,
			"1",
			"$1",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_POSITIONAL,
			"2",
			"$2",
			2U,
			1
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM users WHERE phone LIKE like_escape($1, $2)",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_EXPRESSION,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_NONE,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			0
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM users WHERE phone LIKE pg_catalog.like_escape($1, $2)",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_EXPRESSION,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_NONE,
			NULL,
			SQLPARSER_BIND_KIND_NONE,
			NULL,
			NULL,
			0U,
			0
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE phone LIKE ? ESCAPE ?",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_POSITIONAL,
			"1",
			"?",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_POSITIONAL,
			"2",
			"?",
			2U,
			1
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE [phone] LIKE @pattern ESCAPE @escape_char",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			"@pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"escape_char",
			"@escape_char",
			2U,
			1
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM users WHERE phone LIKE :pattern ESCAPE :escape_char",
			"phone",
			"LIKE",
			SQLPARSER_GRAPH_VALUE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"pattern",
			":pattern",
			1U,
			SQLPARSER_GRAPH_LIKE_ESCAPE_BIND,
			NULL,
			SQLPARSER_BIND_KIND_NAMED,
			"escape_char",
			":escape_char",
			2U,
			1
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		sqlparser_graph_field_t field;
		char *view_json;
		char *deparsed_sql;
		int rc;

		handle = NULL;
		view_json = NULL;
		deparsed_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "LIKE ESCAPE parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "LIKE ESCAPE graph should be available") != 0 ||
		    expect_true(graph.value_count == 1U, "LIKE ESCAPE should expose one pattern value") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "LIKE ESCAPE value should be available") != 0 ||
		    expect_true(value.has_field != 0, "LIKE ESCAPE value should be attached to a field") != 0 ||
		    expect_status_ok(sqlparser_query_graph_field_at(&graph, value.field_index, &field, &error), &error, "LIKE ESCAPE field should be available") != 0 ||
		    expect_true(field.column_name != NULL && strcmp(field.column_name, cases[index].column_name) == 0, "LIKE ESCAPE field column mismatch") != 0 ||
		    expect_true(value.operator_name != NULL && strcmp(value.operator_name, cases[index].operator_name) == 0, "LIKE ESCAPE operator mismatch") != 0 ||
		    expect_true(value.kind == cases[index].value_kind, "LIKE ESCAPE value kind mismatch") != 0 ||
		    expect_true(value.like_escape.kind == cases[index].escape_kind, "LIKE ESCAPE kind mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].value_kind == SQLPARSER_GRAPH_VALUE_LITERAL &&
		    expect_true(value.literal.kind == SQLPARSER_LITERAL_KIND_STRING &&
		                value.literal.string_value != NULL &&
		                strcmp(value.literal.string_value, cases[index].value_literal) == 0,
		                "LIKE ESCAPE pattern literal mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].value_kind == SQLPARSER_GRAPH_VALUE_BIND &&
		    (expect_true(value.bind_kind == cases[index].value_bind_kind, "LIKE ESCAPE pattern bind kind mismatch") != 0 ||
		     expect_true(value.has_bind != 0 && strcmp(value.bind, cases[index].value_bind_key) == 0, "LIKE ESCAPE pattern bind key mismatch") != 0 ||
		     expect_true(value.has_bind_sql != 0 && strcmp(value.bind_sql, cases[index].value_bind_sql) == 0, "LIKE ESCAPE pattern bind SQL mismatch") != 0 ||
		     expect_true(value.has_bind_position != 0 && value.bind_position == cases[index].value_bind_position, "LIKE ESCAPE pattern bind position mismatch") != 0)) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].escape_kind == SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL &&
		    expect_true(value.like_escape.literal.kind == SQLPARSER_LITERAL_KIND_STRING &&
		                value.like_escape.literal.string_value != NULL &&
		                strcmp(value.like_escape.literal.string_value, cases[index].escape_literal) == 0,
		                "LIKE ESCAPE literal mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].escape_kind == SQLPARSER_GRAPH_LIKE_ESCAPE_BIND &&
		    (expect_true(value.like_escape.bind_kind == cases[index].escape_bind_kind, "LIKE ESCAPE bind kind mismatch") != 0 ||
		     expect_true(value.like_escape.has_bind != 0 && strcmp(value.like_escape.bind, cases[index].escape_bind_key) == 0, "LIKE ESCAPE bind key mismatch") != 0 ||
		     expect_true(value.like_escape.has_bind_sql != 0 && strcmp(value.like_escape.bind_sql, cases[index].escape_bind_sql) == 0, "LIKE ESCAPE bind SQL mismatch") != 0 ||
		     expect_true(value.like_escape.has_bind_position != 0 && value.like_escape.bind_position == cases[index].escape_bind_position, "LIKE ESCAPE bind position mismatch") != 0)) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "LIKE ESCAPE view JSON should export") != 0 ||
		    expect_true(view_json != NULL, "LIKE ESCAPE view JSON should not be NULL") != 0 ||
		    expect_true((strstr(view_json, "\"like_escape\"") != NULL) == cases[index].expect_json_escape,
		                "LIKE ESCAPE JSON presence mismatch") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].escape_kind == SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL &&
		    (expect_true(strstr(view_json, "\"kind\":\"literal\"") != NULL, "LIKE ESCAPE JSON literal kind missing") != 0 ||
		     expect_true(strstr(view_json, "\"literal_value\":\"!\"") != NULL, "LIKE ESCAPE JSON literal value missing") != 0)) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].escape_kind == SQLPARSER_GRAPH_LIKE_ESCAPE_BIND &&
		    (expect_true(strstr(view_json, "\"like_escape\":{\"kind\":\"bind\"") != NULL, "LIKE ESCAPE JSON bind object missing") != 0 ||
		     expect_true(strstr(view_json, cases[index].escape_bind_sql) != NULL, "LIKE ESCAPE JSON bind SQL missing") != 0)) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].escape_kind == SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION &&
		    expect_true(strstr(view_json, "\"like_escape\":{\"kind\":\"expression\"}") != NULL,
		                "LIKE ESCAPE JSON expression object missing") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "LIKE ESCAPE deparse should succeed") != 0 ||
		    expect_true(deparsed_sql != NULL, "LIKE ESCAPE deparse output should not be NULL") != 0 ||
		    expect_true(strcmp(deparsed_sql, cases[index].sql) == 0,
		                "LIKE ESCAPE deparse should preserve original SQL") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_query_graph_operator_kind_semantics(void)
{
	struct operator_kind_case {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *operator_name;
		sqlparser_graph_operator_kind_t operator_kind;
		int expect_like_pattern;
		int expect_like_escape;
	};
	static const struct operator_kind_case cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "SELECT id FROM users WHERE phone LIKE $1",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 0},
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "SELECT id FROM users WHERE phone NOT LIKE $1",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0},
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "SELECT id FROM users WHERE phone ILIKE $1",
		 "ILIKE", SQLPARSER_GRAPH_OPERATOR_ILIKE, 1, 0},
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "SELECT id FROM users WHERE phone NOT ILIKE $1",
		 "NOT ILIKE", SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE, 1, 0},
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "SELECT id FROM users WHERE phone ILIKE $1 ESCAPE $2",
		 "ILIKE", SQLPARSER_GRAPH_OPERATOR_ILIKE, 1, 1},
		{SQLPARSER_DIALECT_POSTGRESQL,
		 "SELECT id FROM users WHERE phone = $1",
		 "=", SQLPARSER_GRAPH_OPERATOR_UNKNOWN, 0, 0},
		{SQLPARSER_DIALECT_ORACLE,
		 "SELECT id FROM users WHERE phone LIKE :pattern",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 0},
		{SQLPARSER_DIALECT_ORACLE,
		 "SELECT id FROM users WHERE phone NOT LIKE :pattern",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0},
		{SQLPARSER_DIALECT_ORACLE,
		 "SELECT id FROM users WHERE phone LIKE :pattern ESCAPE :escape_char",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 1},
		{SQLPARSER_DIALECT_MYSQL,
		 "SELECT id FROM users WHERE phone LIKE ?",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 0},
		{SQLPARSER_DIALECT_MYSQL,
		 "SELECT id FROM users WHERE phone NOT LIKE ?",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0},
		{SQLPARSER_DIALECT_SQLSERVER,
		 "SELECT id FROM users WHERE phone LIKE @pattern",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 0},
		{SQLPARSER_DIALECT_SQLSERVER,
		 "SELECT id FROM users WHERE phone NOT LIKE @pattern",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0},
		{SQLPARSER_DIALECT_DAMENG,
		 "SELECT id FROM users WHERE phone LIKE :pattern",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 0},
		{SQLPARSER_DIALECT_DAMENG,
		 "SELECT id FROM users WHERE phone NOT LIKE :pattern",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0},
		{SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		 "SELECT id FROM users WHERE phone ILIKE $1 ESCAPE $2",
		 "ILIKE", SQLPARSER_GRAPH_OPERATOR_ILIKE, 1, 1},
		{SQLPARSER_DIALECT_VASTBASE_ORACLE,
		 "SELECT id FROM users WHERE phone NOT LIKE :pattern",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0},
		{SQLPARSER_DIALECT_VASTBASE_MYSQL,
		 "SELECT id FROM users WHERE phone LIKE ?",
		 "LIKE", SQLPARSER_GRAPH_OPERATOR_LIKE, 1, 0},
		{SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
		 "SELECT id FROM users WHERE phone NOT LIKE @pattern",
		 "NOT LIKE", SQLPARSER_GRAPH_OPERATOR_NOT_LIKE, 1, 0}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t index;

	if (expect_true(strcmp(sqlparser_graph_operator_kind_name(SQLPARSER_GRAPH_OPERATOR_LIKE), "like") == 0,
	                "operator kind name for LIKE should be stable") != 0 ||
	    expect_true(strcmp(sqlparser_graph_operator_kind_name(SQLPARSER_GRAPH_OPERATOR_NOT_LIKE), "not_like") == 0,
	                "operator kind name for NOT LIKE should be stable") != 0 ||
	    expect_true(strcmp(sqlparser_graph_operator_kind_name(SQLPARSER_GRAPH_OPERATOR_ILIKE), "ilike") == 0,
	                "operator kind name for ILIKE should be stable") != 0 ||
	    expect_true(strcmp(sqlparser_graph_operator_kind_name(SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE), "not_ilike") == 0,
	                "operator kind name for NOT ILIKE should be stable") != 0 ||
	    expect_true(strcmp(sqlparser_graph_operator_kind_name(SQLPARSER_GRAPH_OPERATOR_UNKNOWN), "unknown") == 0,
	                "operator kind name for unknown should be stable") != 0 ||
	    expect_true(sqlparser_graph_value_is_like_pattern(NULL) == 0,
	                "NULL graph value should not be a LIKE pattern") != 0) {
		return 1;
	}

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_value_t value;
		char *view_json;
		char expected_json[96];
		int rc;

		handle = NULL;
		view_json = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "operator kind parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "operator kind graph should be available") != 0 ||
		    expect_true(graph.value_count == 1U, "operator kind test should expose one value") != 0 ||
		    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "operator kind value should be available") != 0 ||
		    expect_true(value.operator_name != NULL && strcmp(value.operator_name, cases[index].operator_name) == 0,
		                "operator kind operator_name mismatch") != 0 ||
		    expect_true(value.operator_kind == cases[index].operator_kind,
		                "operator kind enum mismatch") != 0 ||
		    expect_true(sqlparser_graph_operator_is_like_pattern(value.operator_kind) == cases[index].expect_like_pattern,
		                "operator kind helper mismatch") != 0 ||
		    expect_true(sqlparser_graph_value_is_like_pattern(&value) == cases[index].expect_like_pattern,
		                "operator value helper mismatch") != 0 ||
		    expect_true((value.like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) == cases[index].expect_like_escape,
		                "operator kind LIKE ESCAPE presence mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "operator kind view JSON should export") != 0 ||
		    expect_true(view_json != NULL, "operator kind view JSON should not be NULL") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(expected_json,
		         sizeof(expected_json),
		         "\"operator_kind\":\"%s\"",
		         sqlparser_graph_operator_kind_name(cases[index].operator_kind));
		if (expect_true(strstr(view_json, expected_json) != NULL,
		                "operator kind JSON should contain expected kind") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int expect_query_graph_value_bind(
	const sqlparser_query_graph_view_t *graph,
	size_t value_index,
	const char *column_name,
	sqlparser_bind_kind_t bind_kind,
	const char *bind_key,
	size_t bind_position,
	const char *bind_sql)
{
	sqlparser_error_t error;
	sqlparser_graph_value_t value;
	sqlparser_graph_field_t field;
	int rc;

	memset(&error, 0, sizeof(error));
	rc = sqlparser_query_graph_value_at(graph, value_index, &value, &error);
	if (expect_status_ok(rc, &error, "query graph value should be available") != 0 ||
	    expect_true(value.has_field != 0, "query graph value should be attached to a field") != 0 ||
	    expect_true(value.kind == SQLPARSER_GRAPH_VALUE_BIND, "query graph value should be a bind") != 0) {
		return 1;
	}
	rc = sqlparser_query_graph_field_at(graph, value.field_index, &field, &error);
	if (expect_status_ok(rc, &error, "query graph value field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, column_name) == 0, "query graph value field column mismatch") != 0 ||
	    expect_true(value.bind_kind == bind_kind, "query graph value bind kind mismatch") != 0 ||
	    expect_true(value.has_bind != 0 && strcmp(value.bind, bind_key) == 0, "query graph value bind key mismatch") != 0 ||
	    expect_true(value.has_bind_position != 0 && value.bind_position == bind_position, "query graph value bind position mismatch") != 0 ||
	    expect_true(value.has_bind_sql != 0 && strcmp(value.bind_sql, bind_sql) == 0, "query graph value bind SQL mismatch") != 0) {
		fprintf(stderr,
		        "value[%zu] column=%s bind_key=%s bind_sql=%s expected_sql=%s position=%zu expected_position=%zu\n",
		        value_index,
		        field.column_name != NULL ? field.column_name : "(null)",
		        value.has_bind ? value.bind : "(none)",
		        value.has_bind_sql ? value.bind_sql : "(none)",
		        bind_sql,
		        value.has_bind_position ? value.bind_position : 0U,
		        bind_position);
		return 1;
	}
	return 0;
}

static int expect_query_graph_select_value_binds(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t expected_value_count,
	const char *column_name,
	sqlparser_bind_kind_t bind_kind,
	const char *first_key,
	const char *second_key,
	const char *first_sql,
	const char *second_sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph value-list parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph value-list graph should be available") != 0 ||
	    expect_true(graph.value_count == expected_value_count, "query graph value-list count mismatch") != 0 ||
	    expect_query_graph_value_bind(&graph, 0U, column_name, bind_kind, first_key, 1U, first_sql) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expected_value_count > 1U &&
	    expect_query_graph_value_bind(&graph, 1U, column_name, bind_kind, second_key, 2U, second_sql) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_no_field_values(
	sqlparser_dialect_t dialect,
	const char *sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph no-value parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph no-value graph should be available") != 0 ||
	    expect_true(graph.value_count == 0U, "query graph should not expose projection-only bind as field value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_update_set_and_value_list(
	sqlparser_dialect_t dialect,
	const char *sql,
	sqlparser_bind_kind_t bind_kind,
	const char *set_key,
	const char *first_where_key,
	const char *second_where_key,
	const char *set_sql,
	const char *first_where_sql,
	const char *second_where_sql)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_assignment_t assignment;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph update value-list parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph update value-list graph should be available") != 0 ||
	    expect_true(graph.value_count == 2U, "query graph update WHERE should expose two values") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "query graph update dml should be available") != 0 ||
	    expect_true(dml.assignments.count == 1U, "query graph update should expose one assignment") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "query graph update assignment should be available") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_BIND, "query graph update assignment should be bind") != 0 ||
	    expect_true(assignment.bind_kind == bind_kind, "query graph update assignment bind kind mismatch") != 0 ||
	    expect_true(assignment.has_bind != 0 && strcmp(assignment.bind, set_key) == 0, "query graph update assignment bind key mismatch") != 0 ||
	    expect_true(assignment.has_bind_position != 0 && assignment.bind_position == 1U, "query graph update assignment bind position mismatch") != 0 ||
	    expect_true(assignment.has_bind_sql != 0 && strcmp(assignment.bind_sql, set_sql) == 0, "query graph update assignment bind SQL mismatch") != 0 ||
	    expect_query_graph_value_bind(&graph, 0U, "phone", bind_kind, first_where_key, 2U, first_where_sql) != 0 ||
	    expect_query_graph_value_bind(&graph, 1U, "phone", bind_kind, second_where_key, 3U, second_where_sql) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_condition_value_lists(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *select_in_sql;
		const char *select_not_in_sql;
		const char *select_between_sql;
		const char *select_func_sql;
		const char *select_case_sql;
		const char *update_sql;
		const char *delete_sql;
		sqlparser_bind_kind_t bind_kind;
		const char *key1;
		const char *key2;
		const char *key3;
		const char *sql1;
		const char *sql2;
		const char *sql3;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM public.users WHERE phone IN ($1, $2)",
			"SELECT id FROM public.users WHERE phone NOT IN ($1, $2)",
			"SELECT id FROM public.users WHERE created_at BETWEEN $1 AND $2",
			"SELECT id FROM public.users WHERE UPPER(email) = $1",
			"SELECT CASE WHEN phone = $1 THEN name ELSE email END AS v FROM public.users",
			"UPDATE public.users SET note = $1 WHERE phone IN ($2, $3)",
			"DELETE FROM public.users WHERE email IN ($1, $2)",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"1",
			"2",
			"3",
			"$1",
			"$2",
			"$3"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE phone IN (?, ?)",
			"SELECT id FROM users WHERE phone NOT IN (?, ?)",
			"SELECT id FROM users WHERE created_at BETWEEN ? AND ?",
			"SELECT id FROM users WHERE UPPER(email) = ?",
			"SELECT CASE WHEN phone = ? THEN name ELSE email END AS v FROM users",
			"UPDATE users SET note = ? WHERE phone IN (?, ?)",
			"DELETE FROM users WHERE email IN (?, ?)",
			SQLPARSER_BIND_KIND_POSITIONAL,
			"1",
			"2",
			"3",
			"?",
			"?",
			"?"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT id FROM users WHERE phone IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE phone NOT IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE created_at BETWEEN :from_time AND :to_time",
			"SELECT id FROM users WHERE UPPER(email) = :email",
			"SELECT CASE WHEN phone = :phone1 THEN name ELSE email END AS v FROM users",
			"UPDATE users SET note = :note WHERE phone IN (:phone1, :phone2)",
			"DELETE FROM users WHERE email IN (:email1, :email2)",
			SQLPARSER_BIND_KIND_NAMED,
			"phone1",
			"phone2",
			"phone2",
			":phone1",
			":phone2",
			":phone2"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE [phone] IN (@phone1, @phone2)",
			"SELECT [id] FROM [dbo].[users] WHERE [phone] NOT IN (@phone1, @phone2)",
			"SELECT [id] FROM [dbo].[users] WHERE [created_at] BETWEEN @from_time AND @to_time",
			"SELECT [id] FROM [dbo].[users] WHERE UPPER([email]) = @email",
			"SELECT CASE WHEN [phone] = @phone1 THEN [name] ELSE [email] END AS [v] FROM [dbo].[users]",
			"UPDATE [dbo].[users] SET [note] = @note WHERE [phone] IN (@phone1, @phone2)",
			"DELETE FROM [dbo].[users] WHERE [email] IN (@email1, @email2)",
			SQLPARSER_BIND_KIND_NAMED,
			"phone1",
			"phone2",
			"phone2",
			"@phone1",
			"@phone2",
			"@phone2"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM users WHERE phone IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE phone NOT IN (:phone1, :phone2)",
			"SELECT id FROM users WHERE created_at BETWEEN :from_time AND :to_time",
			"SELECT id FROM users WHERE UPPER(email) = :email",
			"SELECT CASE WHEN phone = :phone1 THEN name ELSE email END AS v FROM users",
			"UPDATE users SET note = :note WHERE phone IN (:phone1, :phone2)",
			"DELETE FROM users WHERE email IN (:email1, :email2)",
			SQLPARSER_BIND_KIND_NAMED,
			"phone1",
			"phone2",
			"phone2",
			":phone1",
			":phone2",
			":phone2"
		}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
	} non_predicate_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "SELECT phone + $1 AS v FROM public.users"},
		{SQLPARSER_DIALECT_MYSQL, "SELECT phone + ? AS v FROM users"},
		{SQLPARSER_DIALECT_ORACLE, "SELECT phone + :delta AS v FROM users"},
		{SQLPARSER_DIALECT_SQLSERVER, "SELECT [phone] + @delta AS [v] FROM [dbo].[users]"},
		{SQLPARSER_DIALECT_DAMENG, "SELECT phone + :delta AS v FROM users"}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_in_sql,
			    2U,
			    "phone",
			    cases[index].bind_kind,
			    cases[index].key1,
			    cases[index].key2,
			    cases[index].sql1,
			    cases[index].sql2) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_not_in_sql,
			    2U,
			    "phone",
			    cases[index].bind_kind,
			    cases[index].key1,
			    cases[index].key2,
			    cases[index].sql1,
			    cases[index].sql2) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_between_sql,
			    2U,
			    "created_at",
			    cases[index].bind_kind,
			    strcmp(cases[index].key1, "phone1") == 0 ? "from_time" : cases[index].key1,
			    strcmp(cases[index].key2, "phone2") == 0 ? "to_time" : cases[index].key2,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":from_time" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@from_time" : cases[index].sql1),
			    strcmp(cases[index].sql2, ":phone2") == 0 ? ":to_time" : (strcmp(cases[index].sql2, "@phone2") == 0 ? "@to_time" : cases[index].sql2)) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_func_sql,
			    1U,
			    "email",
			    cases[index].bind_kind,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "email" : cases[index].key1,
			    cases[index].key2,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":email" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@email" : cases[index].sql1),
			    cases[index].sql2) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].select_case_sql,
			    1U,
			    "phone",
			    cases[index].bind_kind,
			    cases[index].key1,
			    cases[index].key2,
			    cases[index].sql1,
			    cases[index].sql2) != 0 ||
		    expect_query_graph_update_set_and_value_list(
			    cases[index].dialect,
			    cases[index].update_sql,
			    cases[index].bind_kind,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "note" : cases[index].key1,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "phone1" : cases[index].key2,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "phone2" : cases[index].key3,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":note" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@note" : cases[index].sql1),
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? cases[index].sql1 : cases[index].sql2,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? cases[index].sql2 : cases[index].sql3) != 0 ||
		    expect_query_graph_select_value_binds(
			    cases[index].dialect,
			    cases[index].delete_sql,
			    2U,
			    "email",
			    cases[index].bind_kind,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "email1" : cases[index].key1,
			    cases[index].bind_kind == SQLPARSER_BIND_KIND_NAMED ? "email2" : cases[index].key2,
			    strcmp(cases[index].sql1, ":phone1") == 0 ? ":email1" : (strcmp(cases[index].sql1, "@phone1") == 0 ? "@email1" : cases[index].sql1),
			    strcmp(cases[index].sql2, ":phone2") == 0 ? ":email2" : (strcmp(cases[index].sql2, "@phone2") == 0 ? "@email2" : cases[index].sql2)) != 0) {
			return 1;
		}
	}
	for (index = 0U; index < sizeof(non_predicate_cases) / sizeof(non_predicate_cases[0]); index++) {
		if (expect_query_graph_no_field_values(
			    non_predicate_cases[index].dialect,
			    non_predicate_cases[index].sql) != 0) {
			return 1;
		}
	}
	return 0;
}

static int expect_query_graph_single_value_match_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_field_match_kind_t expected_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_value_t value;
	sqlparser_graph_field_t field;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph field-match parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph field-match graph should be available") != 0 ||
	    expect_true(graph.value_count == 1U, "query graph field-match should expose one value") != 0 ||
	    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "query graph field-match value should be available") != 0 ||
	    expect_true(value.has_field != 0, "query graph field-match value should be attached to a field") != 0 ||
	    expect_true(value.field_match_kind == expected_kind, "query graph field-match kind mismatch") != 0 ||
	    expect_true(strcmp(sqlparser_graph_field_match_kind_name(value.field_match_kind),
	                       sqlparser_graph_field_match_kind_name(expected_kind)) == 0,
	                "query graph field-match name mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, value.field_index, &field, &error), &error, "query graph field-match field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, column_name) == 0, "query graph field-match column mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_field_match_kind_semantics(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *direct_sql;
		const char *function_sql;
		const char *cast_sql;
		const char *expression_sql;
		const char *case_sql;
		const char *column_name;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM public.users WHERE secret = $1",
			"SELECT id FROM public.users WHERE UPPER(secret) = $1",
			"SELECT id FROM public.users WHERE CAST(secret AS text) = $1",
			"SELECT id FROM public.users WHERE secret || 'x' = $1",
			"SELECT id FROM public.users WHERE CASE WHEN 1 = 1 THEN secret END = $1",
			"secret"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE secret = ?",
			"SELECT id FROM users WHERE UPPER(secret) = ?",
			"SELECT id FROM users WHERE CAST(secret AS CHAR) = ?",
			"SELECT id FROM users WHERE CONCAT(secret, 'x') = ?",
			"SELECT id FROM users WHERE CASE WHEN 1 = 1 THEN secret END = ?",
			"secret"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE UPPER(SECRET) = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CAST(SECRET AS VARCHAR(32)) = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET || 'x' = :secret",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN 1 = 1 THEN SECRET END = :secret",
			"SECRET"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE UPPER([secret]) = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE CAST([secret] AS VARCHAR(32)) = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] + 'x' = @secret",
			"SELECT [id] FROM [dbo].[users] WHERE CASE WHEN 1 = 1 THEN [secret] END = @secret",
			"secret"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE UPPER(secret) = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE CAST(secret AS VARCHAR(32)) = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret || 'x' = :secret",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN 1 = 1 THEN secret END = :secret",
			"secret"
		}
	};
	size_t index;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *view_json;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].direct_sql,
			    cases[index].column_name,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].function_sql,
			    cases[index].column_name,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].cast_sql,
			    cases[index].column_name,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].expression_sql,
			    cases[index].column_name,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_single_value_match_kind(
			    cases[index].dialect,
			    cases[index].case_sql,
			    cases[index].column_name,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0) {
			return 1;
		}
	}
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(
		"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :plain_secret AND UPPER(SECRET) = :upper_secret",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "query graph field-match JSON parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "query graph field-match JSON export should succeed") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"field_match_kind\":\"direct_field\"") != NULL,
	                "view JSON should expose direct field match") != 0 ||
	    expect_true(view_json != NULL && strstr(view_json, "\"field_match_kind\":\"expression_field\"") != NULL,
	                "view JSON should expose expression field match") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_column_value(
	const sqlparser_query_graph_view_t *graph,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind,
	int require_no_direct_bind)
{
	sqlparser_error_t error;
	size_t index;

	memset(&error, 0, sizeof(error));
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		sqlparser_graph_field_t field;
		int rc;

		rc = sqlparser_query_graph_value_at(graph, index, &value, &error);
		if (expect_status_ok(rc, &error, "query graph value scan should succeed") != 0) {
			return 1;
		}
		if (!value.has_field ||
		    value.kind != value_kind ||
		    value.field_match_kind != field_match_kind) {
			continue;
		}
		rc = sqlparser_query_graph_field_at(graph, value.field_index, &field, &error);
		if (expect_status_ok(rc, &error, "query graph value field scan should succeed") != 0) {
			return 1;
		}
		if (field.column_name == NULL || strcmp(field.column_name, column_name) != 0) {
			continue;
		}
		if (require_no_direct_bind &&
		    (value.has_bind || value.has_bind_sql || value.has_bind_position || value.has_selector)) {
			fprintf(stderr, "FAIL: expression value for %s must not expose direct bind or selector\n", column_name);
			return 1;
		}
		return 0;
	}
	fprintf(stderr,
	        "FAIL: query graph value not found: column=%s kind=%s field_match_kind=%s\n",
	        column_name,
	        sqlparser_graph_value_kind_name(value_kind),
	        sqlparser_graph_field_match_kind_name(field_match_kind));
	return 1;
}

static int expect_query_graph_column_value_absent(
	const sqlparser_query_graph_view_t *graph,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind)
{
	sqlparser_error_t error;
	size_t index;

	memset(&error, 0, sizeof(error));
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		sqlparser_graph_field_t field;
		int rc;

		rc = sqlparser_query_graph_value_at(graph, index, &value, &error);
		if (expect_status_ok(rc, &error, "query graph value scan should succeed") != 0) {
			return 1;
		}
		if (!value.has_field ||
		    value.kind != value_kind ||
		    value.field_match_kind != field_match_kind) {
			continue;
		}
		rc = sqlparser_query_graph_field_at(graph, value.field_index, &field, &error);
		if (expect_status_ok(rc, &error, "query graph value field scan should succeed") != 0) {
			return 1;
		}
		if (field.column_name != NULL && strcmp(field.column_name, column_name) == 0) {
			fprintf(stderr,
			        "FAIL: unexpected query graph value: column=%s kind=%s field_match_kind=%s\n",
			        column_name,
			        sqlparser_graph_value_kind_name(value_kind),
			        sqlparser_graph_field_match_kind_name(field_match_kind));
			return 1;
		}
	}
	return 0;
}

static int expect_query_graph_condition_value_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind,
	int require_no_direct_bind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph condition parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph condition graph should be available") != 0 ||
	    expect_query_graph_column_value(
		    &graph,
		    column_name,
		    value_kind,
		    field_match_kind,
		    require_no_direct_bind) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_condition_value_absent(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind,
	sqlparser_graph_field_match_kind_t field_match_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph condition parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph condition graph should be available") != 0 ||
	    expect_query_graph_column_value_absent(&graph, column_name, value_kind, field_match_kind) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_insert_cell_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	size_t fallback_column_ordinal,
	sqlparser_graph_value_kind_t value_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	size_t column_ordinal;
	size_t index;
	int found_column;
	int found_cell;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph insert parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph insert graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "query graph insert dml should be available") != 0 ||
	    expect_true(dml.kind == SQLPARSER_GRAPH_DML_INSERT, "query graph dml should be insert") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	column_ordinal = fallback_column_ordinal;
	found_column = column_name == NULL ? 1 : 0;
	for (index = 0U; !found_column && index < dml.target_columns.count; index++) {
		size_t column_index;
		sqlparser_graph_dml_column_t column;

		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.target_columns, index, &column_index, &error),
		                     &error,
		                     "query graph insert column span should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_column_at(&graph, column_index, &column, &error),
		                     &error,
		                     "query graph insert column should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (column.column_name != NULL && strcmp(column.column_name, column_name) == 0) {
			column_ordinal = column.ordinal;
			found_column = 1;
		}
	}
	if (!found_column) {
		fprintf(stderr, "FAIL: insert target column not found: %s\n", column_name);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	found_cell = 0;
	for (index = 0U; index < dml.rows.count; index++) {
		size_t cell_index;
		sqlparser_graph_dml_cell_t cell;

		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, index, &cell_index, &error),
		                     &error,
		                     "query graph insert row span should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error),
		                     &error,
		                     "query graph insert cell should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cell.column_ordinal == column_ordinal) {
			if (cell.kind != value_kind) {
				fprintf(stderr,
				        "FAIL: insert cell kind mismatch: expected=%s actual=%s\n",
				        sqlparser_graph_value_kind_name(value_kind),
				        sqlparser_graph_value_kind_name(cell.kind));
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (value_kind == SQLPARSER_GRAPH_VALUE_EXPRESSION &&
			    (cell.has_bind || cell.has_bind_sql || cell.has_bind_position)) {
				fprintf(stderr, "FAIL: expression insert cell must not expose direct bind\n");
				sqlparser_handle_destroy(handle);
				return 1;
			}
			found_cell = 1;
		}
	}
	if (!found_cell) {
		fprintf(stderr, "FAIL: insert cell ordinal not found: %zu\n", column_ordinal);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_query_graph_update_assignment_kind(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *column_name,
	sqlparser_graph_value_kind_t value_kind)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	size_t index;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph update graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "query graph update dml should be available") != 0 ||
	    expect_true(dml.kind == SQLPARSER_GRAPH_DML_UPDATE, "query graph dml should be update") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (index = 0U; index < dml.assignments.count; index++) {
		size_t assignment_index;
		sqlparser_graph_dml_assignment_t assignment;
		sqlparser_graph_field_t field;

		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.assignments, index, &assignment_index, &error),
		                     &error,
		                     "query graph update assignment span should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, assignment_index, &assignment, &error),
		                     &error,
		                     "query graph update assignment should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_field_at(&graph, assignment.target_field_index, &field, &error),
		                     &error,
		                     "query graph update target field should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.column_name == NULL || strcmp(field.column_name, column_name) != 0) {
			continue;
		}
		if (assignment.value_kind != value_kind) {
			fprintf(stderr,
			        "FAIL: update assignment kind mismatch: expected=%s actual=%s\n",
			        sqlparser_graph_value_kind_name(value_kind),
			        sqlparser_graph_value_kind_name(assignment.value_kind));
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (value_kind == SQLPARSER_GRAPH_VALUE_EXPRESSION &&
		    (assignment.has_bind || assignment.has_bind_sql || assignment.has_bind_position)) {
			fprintf(stderr, "FAIL: expression update assignment must not expose direct bind\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
		return 0;
	}
	fprintf(stderr, "FAIL: update target column not found: %s\n", column_name);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_query_graph_expression_field_value_semantics(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *field_case_sql;
		const char *field_multi_func_sql;
		const char *field_multi_operator_sql;
		const char *nested_field_sql;
		const char *value_func_sql;
		const char *value_operator_sql;
		const char *value_cast_sql;
		const char *value_case_sql;
		const char *insert_expr_sql;
		const char *insert_no_column_expr_sql;
		const char *update_expr_sql;
		const char *secret_column;
		const char *id_column;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT id FROM public.users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = $1",
			"SELECT id FROM public.users WHERE COALESCE(secret, id) = $1",
			"SELECT id FROM public.users WHERE secret || id = $1",
			"SELECT id FROM (SELECT id, secret FROM public.users) s WHERE UPPER(s.secret) = $1",
			"SELECT id FROM public.users WHERE secret = UPPER($1)",
			"SELECT id FROM public.users WHERE secret = $1 || 'x'",
			"SELECT id FROM public.users WHERE secret = CAST($1 AS text)",
			"SELECT id FROM public.users WHERE secret = CASE WHEN id = 1 THEN $1 END",
			"INSERT INTO public.users (id, secret) VALUES (1, UPPER($1))",
			"INSERT INTO public.users VALUES (1, UPPER($1))",
			"UPDATE public.users SET secret = UPPER($1) WHERE id = 1",
			"secret",
			"id"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT id FROM users WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = ?",
			"SELECT id FROM users WHERE CONCAT(secret, id) = ?",
			"SELECT id FROM users WHERE secret + id = ?",
			"SELECT id FROM (SELECT id, secret FROM users) s WHERE UPPER(s.secret) = ?",
			"SELECT id FROM users WHERE secret = UPPER(?)",
			"SELECT id FROM users WHERE secret = CONCAT(?, 'x')",
			"SELECT id FROM users WHERE secret = CAST(? AS CHAR)",
			"SELECT id FROM users WHERE secret = CASE WHEN id = 1 THEN ? END",
			"INSERT INTO users (id, secret) VALUES (1, UPPER(?))",
			"INSERT INTO users VALUES (1, UPPER(?))",
			"UPDATE users SET secret = UPPER(?) WHERE id = 1",
			"secret",
			"id"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN ID = 1 THEN SECRET ELSE BACKUP_SECRET END = :v",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE NVL(SECRET, ID) = :v",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET || ID = :v",
			"SELECT ID FROM (SELECT ID, SECRET FROM KDES.DBP_CRYPTO_TEST) s WHERE UPPER(s.SECRET) = :v",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = UPPER(:v)",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = :v || 'x'",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = CAST(:v AS VARCHAR(32))",
			"SELECT ID FROM KDES.DBP_CRYPTO_TEST WHERE SECRET = CASE WHEN ID = 1 THEN :v END",
			"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (1, UPPER(:v))",
			"INSERT INTO KDES.DBP_CRYPTO_TEST VALUES (1, UPPER(:v))",
			"UPDATE KDES.DBP_CRYPTO_TEST SET SECRET = UPPER(:v) WHERE ID = 1",
			"SECRET",
			"ID"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [id] FROM [dbo].[users] WHERE CASE WHEN [id] = 1 THEN [secret] ELSE [backup_secret] END = @v",
			"SELECT [id] FROM [dbo].[users] WHERE CONCAT([secret], [id]) = @v",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] + [id] = @v",
			"SELECT [id] FROM (SELECT [id], [secret] FROM [dbo].[users]) [s] WHERE UPPER([s].[secret]) = @v",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = UPPER(@v)",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = @v + 'x'",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = CAST(@v AS VARCHAR(32))",
			"SELECT [id] FROM [dbo].[users] WHERE [secret] = CASE WHEN [id] = 1 THEN @v END",
			"INSERT INTO [dbo].[users] ([id], [secret]) VALUES (1, UPPER(@v))",
			"INSERT INTO [dbo].[users] VALUES (1, UPPER(@v))",
			"UPDATE [dbo].[users] SET [secret] = UPPER(@v) WHERE [id] = 1",
			"secret",
			"id"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE CASE WHEN id = 1 THEN secret ELSE backup_secret END = :v",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE NVL(secret, id) = :v",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret || id = :v",
			"SELECT id FROM (SELECT id, secret FROM KDES.DBP_CRYPTO_TEST) s WHERE UPPER(s.secret) = :v",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = UPPER(:v)",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = :v || 'x'",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = CAST(:v AS VARCHAR(32))",
			"SELECT id FROM KDES.DBP_CRYPTO_TEST WHERE secret = CASE WHEN id = 1 THEN :v END",
			"INSERT INTO KDES.DBP_CRYPTO_TEST (id, secret) VALUES (1, UPPER(:v))",
			"INSERT INTO KDES.DBP_CRYPTO_TEST VALUES (1, UPPER(:v))",
			"UPDATE KDES.DBP_CRYPTO_TEST SET secret = UPPER(:v) WHERE id = 1",
			"secret",
			"id"
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_case_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_absent(
			    cases[index].dialect,
			    cases[index].field_case_sql,
			    cases[index].id_column,
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_multi_func_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_multi_func_sql,
			    cases[index].id_column,
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].field_multi_operator_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].nested_field_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_BIND,
			    SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD,
			    0) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_func_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_operator_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_cast_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_condition_value_kind(
			    cases[index].dialect,
			    cases[index].value_case_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION,
			    SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
			    1) != 0 ||
		    expect_query_graph_insert_cell_kind(
			    cases[index].dialect,
			    cases[index].insert_expr_sql,
			    cases[index].secret_column,
			    1U,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION) != 0 ||
		    expect_query_graph_insert_cell_kind(
			    cases[index].dialect,
			    cases[index].insert_no_column_expr_sql,
			    NULL,
			    1U,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION) != 0 ||
		    expect_query_graph_update_assignment_kind(
			    cases[index].dialect,
			    cases[index].update_expr_sql,
			    cases[index].secret_column,
			    SQLPARSER_GRAPH_VALUE_EXPRESSION) != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_query_graph_column_semantics_json(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *table_name;
	} dialect_cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "SELECT name, UPPER(name), first_name || last_name FROM users WHERE id = 1 ORDER BY created_at", "users"},
		{SQLPARSER_DIALECT_MYSQL, "SELECT name, UPPER(name), CONCAT(first_name, last_name) FROM users WHERE id = ? ORDER BY created_at", "users"},
		{SQLPARSER_DIALECT_ORACLE, "SELECT name, UPPER(name), first_name || last_name FROM KDES.USERS WHERE id = :id ORDER BY created_at", "USERS"},
		{SQLPARSER_DIALECT_SQLSERVER, "SELECT [name], UPPER([name]), [first_name] + [last_name] FROM [dbo].[users] WHERE [id] = @id ORDER BY [created_at]", "users"},
		{SQLPARSER_DIALECT_DAMENG, "SELECT name, UPPER(name), first_name || last_name FROM KDES.USERS WHERE id = :id ORDER BY created_at", "USERS"}
	};
	json_t *root;
	json_t *statement;
	json_t *column;
	json_t *graph;
	size_t index;

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT name, UPPER(name), first_name || last_name, CASE WHEN state = 1 THEN name END, * "
		    "FROM users WHERE id = 1 ORDER BY created_at",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	graph = json_object_get(statement, "query_graph");
	column = find_view_column_json(statement, "users", "name", "select", 0U);
	if (expect_true(column != NULL, "direct SELECT field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "direct SELECT field target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "name", "select", 1U);
	if (expect_true(column != NULL, "function SELECT field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 1U), "function target_path should contain one entry") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "UPPER", 0), "function target_path should be UPPER") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "first_name", "select", 0U);
	if (expect_true(column != NULL, "expression first field should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "||", 0), "expression first target_path should be || arg 0") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "last_name", "select", 0U);
	if (expect_true(column != NULL, "expression second field should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "||", 1), "expression second target_path should be || arg 1") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "state", "select", 0U);
	if (expect_true(column != NULL, "CASE expression condition field should exist") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "expression", "case_when", 0), "CASE target_path should start with case_when") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "where", 0U);
	if (expect_true(column != NULL, "WHERE field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 0U), "WHERE target_path should be empty") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "created_at", "order", 0U);
	if (expect_true(column != NULL, "ORDER BY field should exist") != 0) {
		json_decref(root);
		return 1;
	}
		{
			char *graph_text;
			int has_star;

			graph_text = json_dumps(graph, JSON_COMPACT);
			has_star = graph_text != NULL && strstr(graph_text, "\"kind\":\"star\"") != NULL;
			free(graph_text);
			if (expect_true(has_star, "query_graph should expose star target") != 0) {
				json_decref(root);
				return 1;
			}
		}
		json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT u.name FROM users u JOIN orders o ON u.id = o.user_id WHERE o.status = 'paid'",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "on", 0U);
	if (expect_true(column != NULL, "JOIN ON left field should exist") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "orders", "user_id", "on", 0U);
	if (expect_true(column != NULL, "JOIN ON right field should exist") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT dept, COUNT(id) FROM users GROUP BY dept HAVING COUNT(id) > 1",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "dept", "group", 0U);
	if (expect_true(column != NULL, "GROUP BY field should exist") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "id", "having", 0U);
	if (expect_true(column != NULL, "HAVING field should exist") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(
		    SQLPARSER_DIALECT_POSTGRESQL,
		    "SELECT CONCAT(UPPER(first_name), last_name) FROM users",
		    &root,
		    &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "users", "first_name", "select", 0U);
	if (expect_true(column != NULL, "nested function first arg should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 2U), "nested function should expose full target_path") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "CONCAT", 0), "nested function outer path should be CONCAT arg 0") != 0 ||
	    expect_true(json_target_path_entry_is(column, 1U, "function", "UPPER", 0), "nested function inner path should be UPPER arg 0") != 0) {
		json_decref(root);
		return 1;
	}
	column = find_view_column_json(statement, "users", "last_name", "select", 0U);
	if (expect_true(column != NULL, "function second arg should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 1U), "second arg should expose one target_path entry") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "CONCAT", 1), "second arg path should be CONCAT arg 1") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	root = NULL;
	statement = NULL;
	if (view_json_parse_statement(SQLPARSER_DIALECT_POSTGRESQL, "SELECT DISTINCT LOW(UPPER(name)) FROM table1", &root, &statement) != 0) {
		return 1;
	}
	column = find_view_column_json(statement, "table1", "name", "select", 0U);
	if (expect_true(column != NULL, "distinct nested function field should exist") != 0 ||
	    expect_true(json_array_length_is(column, "target_path", 2U), "distinct nested function should expose full target_path") != 0 ||
	    expect_true(json_target_path_entry_is(column, 0U, "function", "LOW", 0), "distinct nested function outer path should be LOW") != 0 ||
	    expect_true(json_target_path_entry_is(column, 1U, "function", "UPPER", 0), "distinct nested function inner path should be UPPER") != 0) {
		json_decref(root);
		return 1;
	}
	json_decref(root);

	if (expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT UPPER(name) || '_x' FROM users", "users", "name", "select", "expression", "||", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT COALESCE(name, fallback_name) FROM users", "users", "name", "select", "function", "COALESCE", 1, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT COALESCE(name, fallback_name) FROM users", "users", "fallback_name", "select", "function", "COALESCE", 1, 1) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT CAST(age AS text) FROM users", "users", "age", "select", "function", "CAST", 1, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT GREATEST(age, score) FROM users", "users", "score", "select", "function", "GREATEST", 1, 1) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT name COLLATE \"C\" FROM users", "users", "name", "select", "expression", "collate", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT ARRAY[id, age] FROM users", "users", "id", "select", "expression", "array", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT ROW(id, age) FROM users", "users", "age", "select", "expression", "row", 1, 1) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT name IS NULL FROM users", "users", "name", "select", "expression", "is_null", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT active IS TRUE FROM users", "users", "active", "select", "expression", "boolean_test", 0, 0) != 0 ||
	    expect_view_column_shape(SQLPARSER_DIALECT_POSTGRESQL, "SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users", "users", "amount", "select", "function", "SUM", 1, 0) != 0) {
		return 1;
	}

	for (index = 0U; index < sizeof(dialect_cases) / sizeof(dialect_cases[0]); index++) {
		root = NULL;
		statement = NULL;
		if (view_json_parse_statement(dialect_cases[index].dialect, dialect_cases[index].sql, &root, &statement) != 0) {
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "name", "select", 0U);
		if (expect_true(column != NULL, "dialect direct field should exist") != 0) {
			json_decref(root);
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "name", "select", 1U);
		if (expect_true(column != NULL, "dialect function field should exist") != 0 ||
		    expect_true(json_target_path_entry_is(column, 0U, "function", "UPPER", 0), "dialect function target_path should be UPPER") != 0) {
			json_decref(root);
			return 1;
		}
		column = find_view_column_json(statement, dialect_cases[index].table_name, "id", "where", 0U);
		if (expect_true(column != NULL, "dialect WHERE field should exist") != 0) {
			json_decref(root);
			return 1;
		}
		json_decref(root);
	}

	return 0;
}

static int test_query_graph_public_struct_semantics(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_field_t field;
	sqlparser_graph_value_t value;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	size_t cell_index;
	size_t index;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options("SELECT UPPER(name) FROM users WHERE id = :id", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph struct semantic parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph should be available") != 0 ||
	    expect_true(graph.relation_count == 1U, "query graph should expose one relation") != 0 ||
	    expect_true(graph.field_count >= 2U, "query graph should expose SELECT and WHERE fields") != 0 ||
	    expect_true(graph.value_count == 1U, "query graph should expose one WHERE value") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 0U, &relation, &error), &error, "relation should be available") != 0 ||
	    expect_true(strcmp(relation.object_name, "users") == 0, "relation table should be users") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, 0U, &field, &error), &error, "SELECT field should be available") != 0 ||
	    expect_true(strcmp(field.column_name, "name") == 0, "SELECT field should be name") != 0 ||
	    expect_true(field.has_target != 0, "SELECT field should point to target") != 0 ||
	    expect_true(field.target_path_count == 1U, "SELECT function field target_path count mismatch") != 0 ||
	    expect_true(strcmp(field.target_path[0].kind, "function") == 0, "SELECT function target_path kind mismatch") != 0 ||
	    expect_true(strcmp(field.target_path[0].name, "UPPER") == 0, "SELECT function target_path name mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "WHERE value should be available") != 0 ||
	    expect_true(value.has_bind != 0 && strcmp(value.bind, "id") == 0, "WHERE bind key mismatch") != 0 ||
	    expect_true(value.bind_kind == SQLPARSER_BIND_KIND_NAMED, "WHERE bind kind mismatch") != 0 ||
	    expect_true(value.has_bind_position != 0 && value.bind_position == 1U, "WHERE bind position mismatch") != 0 ||
	    expect_true(value.has_bind_sql != 0 && strcmp(value.bind_sql, ":id") == 0, "WHERE bind SQL mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse_with_options("SELECT * FROM users@remote_db WHERE id = :id", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph database link parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "database link graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 0U, &relation, &error), &error, "database link relation should be available") != 0 ||
	    expect_true(relation.object_name != NULL && strcmp(relation.object_name, "users") == 0, "database link relation table mismatch") != 0 ||
	    expect_true(relation.link_name != NULL && strcmp(relation.link_name, "remote_db") == 0, "database link relation link mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "database link WHERE value should be available") != 0 ||
	    expect_true(value.has_bind != 0 && strcmp(value.bind, "id") == 0, "database link bind key mismatch") != 0 ||
	    expect_true(value.has_bind_position != 0 && value.bind_position == 1U, "database link bind position mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse_with_options("INSERT INTO users (id, name) VALUES (:id, ?)", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph insert bind parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert dml should be available") != 0 ||
	    expect_true(dml.rows.count == 2U, "insert should expose two cells") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 0U, &cell_index, &error), &error, "first insert cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "first insert cell should be available") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "id") == 0, "insert named bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED, "insert named bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "insert named bind position mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.rows, 1U, &cell_index, &error), &error, "second insert cell index should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "second insert cell should be available") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "2") == 0, "insert positional bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL, "insert positional bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 2U, "insert positional bind position mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse_with_options(
		"INSERT INTO users (id, name, updated_at) "
		"VALUES (:id, source_name, :updated_at)",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "interleaved insert field parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "interleaved insert graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "interleaved insert dml should be available") != 0 ||
	    expect_true(dml.rows.count == 3U, "interleaved insert should expose three cells") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (index = 0U; index < dml.rows.count; index++) {
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.rows,
				    index,
				    &cell_index,
				    &error),
			    &error,
			    "interleaved insert cell index should be available") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    cell_index,
				    &cell,
				    &error),
			    &error,
			    "interleaved insert cell should be available") != 0 ||
		    expect_true(
			    cell.row_index == 0U && cell.column_ordinal == index,
			    "interleaved insert cell coordinates should remain ordered") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (index == 0U) {
			if (expect_true(
				    cell.kind == SQLPARSER_GRAPH_VALUE_BIND,
				    "interleaved first cell should be a bind") != 0 ||
			    expect_true(
				    cell.has_bind != 0 && strcmp(cell.bind, "id") == 0,
				    "interleaved first bind key mismatch") != 0 ||
			    expect_true(
				    cell.bind_kind == SQLPARSER_BIND_KIND_NAMED,
				    "interleaved first bind kind mismatch") != 0 ||
			    expect_true(
				    cell.has_bind_position != 0 &&
					    cell.bind_position == 1U,
				    "interleaved first bind position mismatch") != 0 ||
			    expect_true(
				    cell.has_bind_sql != 0 &&
					    strcmp(cell.bind_sql, ":id") == 0,
				    "interleaved first bind SQL mismatch") != 0 ||
			    expect_true(
				    cell.has_source_field == 0 &&
					    cell.has_source_target == 0,
				    "interleaved first bind should not expose a source field") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		} else if (index == 1U) {
			if (expect_true(
				    cell.kind == SQLPARSER_GRAPH_VALUE_FIELD,
				    "interleaved middle cell should be a field") != 0 ||
			    expect_true(
				    cell.has_bind == 0 &&
					    cell.has_bind_position == 0 &&
					    cell.has_bind_sql == 0,
				    "interleaved middle field should not expose bind metadata") != 0 ||
			    expect_true(
				    cell.has_source_field != 0 &&
					    cell.has_source_target == 0,
				    "interleaved middle field source metadata mismatch") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_field_at(
					    &graph,
					    cell.source_field_index,
					    &field,
					    &error),
				    &error,
				    "interleaved middle source field should be available") != 0 ||
			    expect_true(
				    field.column_name != NULL &&
					    strcmp(field.column_name, "source_name") == 0,
				    "interleaved middle source field name mismatch") != 0 ||
			    expect_true(
				    field.has_relation != 0 &&
					    field.relation_index == 0U,
				    "interleaved middle source field relation mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		} else {
			if (expect_true(
				    cell.kind == SQLPARSER_GRAPH_VALUE_BIND,
				    "interleaved last cell should be a bind") != 0 ||
			    expect_true(
				    cell.has_bind != 0 &&
					    strcmp(cell.bind, "updated_at") == 0,
				    "interleaved last bind key mismatch") != 0 ||
			    expect_true(
				    cell.bind_kind == SQLPARSER_BIND_KIND_NAMED,
				    "interleaved last bind kind mismatch") != 0 ||
			    expect_true(
				    cell.has_bind_position != 0 &&
					    cell.bind_position == 2U,
				    "interleaved last bind position mismatch") != 0 ||
			    expect_true(
				    cell.has_bind_sql != 0 &&
					    strcmp(cell.bind_sql, ":updated_at") == 0,
				    "interleaved last bind SQL mismatch") != 0 ||
			    expect_true(
				    cell.has_source_field == 0 &&
					    cell.has_source_target == 0,
				    "interleaved last bind should not expose a source field") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		}
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static sqlparser_status_t test_set_generated_expression_sql(
	sqlparser_handle_t *handle,
	int path,
	const char *sql,
	sqlparser_error_t *out_error)
{
	if (path == 0) {
		return sqlparser_insert_set_cell_sql(
			handle,
			0U,
			0U,
			0U,
			sql,
			out_error);
	}
	if (path == 1) {
		return sqlparser_update_set_assignment_sql(
			handle,
			0U,
			0U,
			sql,
			out_error);
	}
	return sqlparser_select_set_target_sql(
		handle,
		0U,
		0U,
		0U,
		sql,
		out_error);
}

static int test_query_graph_count_field(
	const sqlparser_query_graph_view_t *graph,
	const char *column_name,
	size_t *out_count)
{
	sqlparser_error_t error;
	sqlparser_graph_field_t field;
	size_t count;
	size_t index;
	int rc;

	count = 0U;
	for (index = 0U; index < graph->field_count; index++) {
		memset(&error, 0, sizeof(error));
		rc = sqlparser_query_graph_field_at(
			graph,
			index,
			&field,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "generated expression field should be available") != 0) {
			return 1;
		}
		if (field.column_name != NULL &&
		    strcmp(field.column_name, column_name) == 0) {
			count++;
		}
	}
	*out_count = count;
	return 0;
}

static int test_generated_sysdate_insert_view(
	sqlparser_handle_t *handle,
	const char *replacement_sql,
	int expression)
{
	sqlparser_error_t error;
	json_error_t json_error;
	json_t *cell;
	json_t *dml;
	json_t *query_graph;
	json_t *root;
	json_t *rows;
	json_t *statement;
	json_t *statements;
	char *view_json;
	int rc;
	int result;

	root = NULL;
	view_json = NULL;
	result = 1;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "generated SYSDATE View should export") != 0) {
		goto done;
	}
	memset(&json_error, 0, sizeof(json_error));
	root = json_loads(view_json, 0, &json_error);
	statements = root != NULL ?
		json_object_get(root, "statements") :
		NULL;
	statement = json_is_array(statements) ?
		json_array_get(statements, 0U) :
		NULL;
	query_graph = json_is_object(statement) ?
		json_object_get(statement, "query_graph") :
		NULL;
	dml = json_is_object(query_graph) ?
		json_object_get(query_graph, "dml") :
		NULL;
	rows = json_is_object(dml) ?
		json_object_get(dml, "rows") :
		NULL;
	cell = json_is_array(rows) && json_array_size(rows) == 1U ?
		json_array_get(rows, 0U) :
		NULL;
	if (expect_true(
		    json_is_object(cell) &&
			    json_string_is(
				    cell,
				    "kind",
				    expression ? "expression" : "field") &&
			    (expression ?
				     json_string_is(
					     cell,
					     "expression_sql",
					     replacement_sql) :
				     json_object_get(
					     cell,
					     "expression_sql") == NULL),
		    "generated SYSDATE View cell mismatch") != 0) {
		goto done;
	}
	result = 0;

done:
	if (root != NULL) {
		json_decref(root);
	}
	sqlparser_string_free(view_json);
	return result;
}

static int test_generated_sysdate_query_graph_semantics(void)
{
	static const sqlparser_dialect_t compatible_dialects[] = {
		SQLPARSER_DIALECT_ORACLE,
		SQLPARSER_DIALECT_DAMENG,
		SQLPARSER_DIALECT_VASTBASE_ORACLE
	};
	static const sqlparser_dialect_t other_dialects[] = {
		SQLPARSER_DIALECT_POSTGRESQL,
		SQLPARSER_DIALECT_MYSQL,
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_MYSQL,
		SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	static const struct {
		const char *sql;
		int path;
	} paths[] = {
		{ "INSERT INTO t(a) VALUES (1)", 0 },
		{ "UPDATE t SET a = 1", 1 },
		{ "SELECT 1 FROM t", 2 }
	};
	static const struct {
		const char *sql;
		const char *column_name;
		int expression;
	} replacements[] = {
		{ "SYSDATE", NULL, 1 },
		{ "\"SYSDATE\"", "SYSDATE", 0 },
		{ "\"sysdate\"", "sysdate", 0 }
	};
	static const struct {
		const char *sql;
		size_t sysdate_field_count;
	} nested_replacements[] = {
		{ "COALESCE(SYSDATE, source_col)", 0U },
		{ "COALESCE(\"sysdate\", source_col)", 1U }
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_target_t target;
	sqlparser_graph_field_t field;
	size_t compatible_index;
	size_t dialect_index;
	size_t field_count;
	size_t item_index;
	size_t nested_index;
	size_t path_index;
	size_t replacement_index;
	size_t sysdate_lower_count;
	size_t sysdate_upper_count;
	int rc;

	sqlparser_parse_options_default(&options);
	for (compatible_index = 0U;
	     compatible_index <
		     sizeof(compatible_dialects) / sizeof(compatible_dialects[0]);
	     compatible_index++) {
		options.dialect = compatible_dialects[compatible_index];
		for (path_index = 0U;
		     path_index < sizeof(paths) / sizeof(paths[0]);
		     path_index++) {
			for (replacement_index = 0U;
			     replacement_index <
				     sizeof(replacements) / sizeof(replacements[0]);
			     replacement_index++) {
			handle = NULL;
			memset(&error, 0, sizeof(error));
			rc = sqlparser_parse_with_options(
				paths[path_index].sql,
				&options,
				&handle,
				&error);
			if (expect_status_ok(rc, &error, "generated SYSDATE base parse should succeed") != 0) {
				return 1;
			}
			rc = test_set_generated_expression_sql(
				handle,
				paths[path_index].path,
				replacements[replacement_index].sql,
				&error);
			if (expect_status_ok(rc, &error, "generated SYSDATE mutation should succeed") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			rc = sqlparser_statement_query_graph(
				handle,
				0U,
				&graph,
				&error);
			if (expect_status_ok(rc, &error, "generated SYSDATE graph should be available") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}

			item_index = 0U;
			if (paths[path_index].path == 0) {
				if (expect_status_ok(
					    sqlparser_query_graph_dml(
						    &graph,
						    &dml,
						    &error),
					    &error,
					    "generated INSERT dml should be available") != 0 ||
				    expect_true(
					    dml.rows.count == 1U,
					    "generated INSERT should expose one cell") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_span_index_at(
						    &graph,
						    dml.rows,
						    0U,
						    &item_index,
						    &error),
					    &error,
					    "generated INSERT cell index should be available") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_dml_cell_at(
						    &graph,
						    item_index,
						    &cell,
						    &error),
					    &error,
					    "generated INSERT cell should be available") != 0 ||
				    expect_true(
					    cell.kind ==
						    (replacements[replacement_index].expression ?
							     SQLPARSER_GRAPH_VALUE_EXPRESSION :
							     SQLPARSER_GRAPH_VALUE_FIELD),
					    "generated INSERT cell kind mismatch") != 0 ||
				    expect_true(
					    cell.has_source_field ==
						    !replacements[replacement_index].expression,
					    "generated INSERT source field presence mismatch") != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
				if (!replacements[replacement_index].expression) {
					item_index = cell.source_field_index;
				}
				if (test_generated_sysdate_insert_view(
					    handle,
					    replacements[replacement_index].sql,
					    replacements[replacement_index].expression) != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
			} else if (paths[path_index].path == 1) {
				if (expect_status_ok(
					    sqlparser_query_graph_dml(
						    &graph,
						    &dml,
						    &error),
					    &error,
					    "generated UPDATE dml should be available") != 0 ||
				    expect_true(
					    dml.assignments.count == 1U,
					    "generated UPDATE should expose one assignment") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_span_index_at(
						    &graph,
						    dml.assignments,
						    0U,
						    &item_index,
						    &error),
					    &error,
					    "generated UPDATE assignment index should be available") != 0 ||
				    expect_status_ok(
					    sqlparser_query_graph_dml_assignment_at(
						    &graph,
						    item_index,
						    &assignment,
						    &error),
					    &error,
					    "generated UPDATE assignment should be available") != 0 ||
				    expect_true(
					    assignment.value_kind ==
						    (replacements[replacement_index].expression ?
							     SQLPARSER_GRAPH_VALUE_EXPRESSION :
							     SQLPARSER_GRAPH_VALUE_FIELD),
					    "generated UPDATE assignment kind mismatch") != 0 ||
				    expect_true(
					    assignment.has_source_field ==
						    !replacements[replacement_index].expression,
					    "generated UPDATE source field presence mismatch") != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
				if (!replacements[replacement_index].expression) {
					item_index = assignment.source_field_index;
				}
			} else {
				if (expect_status_ok(
					    sqlparser_query_graph_target_at(
						    &graph,
						    0U,
						    &target,
						    &error),
					    &error,
					    "generated SELECT target should be available") != 0 ||
				    expect_true(
					    target.kind ==
						    (replacements[replacement_index].expression ?
							     SQLPARSER_GRAPH_TARGET_EXPRESSION :
							     SQLPARSER_GRAPH_TARGET_FIELD),
					    "generated SELECT target kind mismatch") != 0 ||
				    expect_true(
					    target.has_field ==
						    !replacements[replacement_index].expression,
					    "generated SELECT target field presence mismatch") != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
				if (!replacements[replacement_index].expression) {
					item_index = target.field_index;
				}
			}

			if (!replacements[replacement_index].expression &&
			    (expect_status_ok(
				     sqlparser_query_graph_field_at(
					     &graph,
					     item_index,
					     &field,
					     &error),
				     &error,
				     "generated quoted SYSDATE field should be available") != 0 ||
			     expect_true(
				     field.column_name != NULL &&
					     strcmp(
						     field.column_name,
						     replacements[replacement_index].column_name) == 0,
				     "generated quoted SYSDATE field spelling mismatch") != 0)) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (test_query_graph_count_field(
				    &graph,
				    "sysdate",
				    &sysdate_lower_count) != 0 ||
			    test_query_graph_count_field(
				    &graph,
				    "SYSDATE",
				    &sysdate_upper_count) != 0 ||
			    expect_true(
				    replacements[replacement_index].expression ?
					    sysdate_lower_count == 0U &&
						    sysdate_upper_count == 0U :
					    (strcmp(
						     replacements[replacement_index].column_name,
						     "sysdate") == 0 ?
						     sysdate_lower_count == 1U &&
							     sysdate_upper_count == 0U :
						     sysdate_lower_count == 0U &&
							     sysdate_upper_count == 1U),
				    "generated SYSDATE field projection mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_handle_destroy(handle);
		}
	}
	}

	for (compatible_index = 0U;
	     compatible_index <
		     sizeof(compatible_dialects) / sizeof(compatible_dialects[0]);
	     compatible_index++) {
		options.dialect = compatible_dialects[compatible_index];
		for (nested_index = 0U;
		     nested_index <
			     sizeof(nested_replacements) /
				     sizeof(nested_replacements[0]);
		     nested_index++) {
			handle = NULL;
			memset(&error, 0, sizeof(error));
			rc = sqlparser_parse_with_options(
				"SELECT 1 FROM t",
				&options,
				&handle,
				&error);
			if (expect_status_ok(rc, &error, "nested SYSDATE base parse should succeed") != 0) {
				return 1;
			}
			rc = test_set_generated_expression_sql(
				handle,
				2,
				nested_replacements[nested_index].sql,
				&error);
			if (expect_status_ok(rc, &error, "nested SYSDATE mutation should succeed") != 0 ||
			    expect_status_ok(
				    sqlparser_statement_query_graph(
					    handle,
					    0U,
					    &graph,
					    &error),
				    &error,
				    "nested SYSDATE graph should be available") != 0 ||
			    expect_status_ok(
				    sqlparser_query_graph_target_at(
					    &graph,
					    0U,
					    &target,
					    &error),
				    &error,
				    "nested SYSDATE target should be available") != 0 ||
			    expect_true(
				    target.kind == SQLPARSER_GRAPH_TARGET_EXPRESSION &&
					    !target.has_field,
				    "nested SYSDATE target should remain an expression") != 0 ||
			    test_query_graph_count_field(
				    &graph,
				    "source_col",
				    &field_count) != 0 ||
			    expect_true(
				    field_count == 1U,
				    "nested SYSDATE source field count mismatch") != 0 ||
			    test_query_graph_count_field(
				    &graph,
				    "sysdate",
				    &field_count) != 0 ||
			    expect_true(
				    field_count ==
					    nested_replacements[nested_index]
						    .sysdate_field_count,
				    "nested SYSDATE field count mismatch") != 0 ||
			    expect_true(
				    graph.field_count ==
					    1U +
						    nested_replacements[nested_index]
							    .sysdate_field_count,
				    "nested SYSDATE total field count mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_handle_destroy(handle);
		}
	}

	for (dialect_index = 0U;
	     dialect_index < sizeof(other_dialects) / sizeof(other_dialects[0]);
	     dialect_index++) {
		options.dialect = other_dialects[dialect_index];
		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"SELECT 1 FROM t",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "non-Oracle SYSDATE base parse should succeed") != 0) {
			return 1;
		}
		rc = test_set_generated_expression_sql(
			handle,
			2,
			"SYSDATE",
			&error);
		if (expect_status_ok(rc, &error, "non-Oracle SYSDATE mutation should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_statement_query_graph(
				    handle,
				    0U,
				    &graph,
				    &error),
			    &error,
			    "non-Oracle SYSDATE graph should be available") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_target_at(
				    &graph,
				    0U,
				    &target,
				    &error),
			    &error,
			    "non-Oracle SYSDATE target should be available") != 0 ||
		    expect_true(
			    target.kind == SQLPARSER_GRAPH_TARGET_FIELD &&
				    target.has_field,
			    "non-Oracle SYSDATE should remain a field") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_field_at(
				    &graph,
				    target.field_index,
				    &field,
				    &error),
			    &error,
			    "non-Oracle SYSDATE field should be available") != 0 ||
		    expect_true(
			    field.column_name != NULL &&
				    strcmp(field.column_name, "SYSDATE") == 0,
			    "non-Oracle SYSDATE field spelling mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int expect_query_graph_target_value(
	const sqlparser_query_graph_view_t *graph,
	size_t target_index,
	sqlparser_graph_target_kind_t target_kind,
	sqlparser_graph_value_kind_t value_kind,
	const char *bind_key,
	sqlparser_bind_kind_t bind_kind,
	size_t bind_position,
	const char *bind_sql,
	sqlparser_literal_kind_t literal_kind,
	const char *literal_string,
	long long literal_integer)
{
	sqlparser_error_t error;
	sqlparser_graph_target_t target;
	sqlparser_graph_value_t value;
	int rc;

	memset(&error, 0, sizeof(error));
	rc = sqlparser_query_graph_target_at(graph, target_index, &target, &error);
	if (expect_status_ok(rc, &error, "target should be available") != 0 ||
	    expect_true(target.kind == target_kind, "target kind mismatch") != 0 ||
	    expect_true(target.has_value != 0, "target should reference a value") != 0) {
		return 1;
	}
	rc = sqlparser_query_graph_value_at(graph, target.value_index, &value, &error);
	if (expect_status_ok(rc, &error, "target value should be available") != 0 ||
	    expect_true(value.kind == value_kind, "target value kind mismatch") != 0 ||
	    expect_true(value.clause == SQLPARSER_CLAUSE_KIND_SELECT_LIST, "target value clause mismatch") != 0 ||
	    expect_true(value.has_field == 0, "target value should not be field-bound") != 0) {
		return 1;
	}
	if (value_kind == SQLPARSER_GRAPH_VALUE_BIND) {
		if (!value.has_bind || strcmp(value.bind, bind_key) != 0 ||
		    value.bind_kind != bind_kind ||
		    !value.has_bind_position || value.bind_position != bind_position ||
		    !value.has_bind_sql || strcmp(value.bind_sql, bind_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: target bind mismatch: expected=%s/%s/%lu actual=%s/%s/%lu\n",
				bind_key,
				bind_sql,
				(unsigned long)bind_position,
				value.bind,
				value.bind_sql,
				(unsigned long)value.bind_position);
			return 1;
		}
	} else if (value_kind == SQLPARSER_GRAPH_VALUE_LITERAL) {
		if (expect_true(value.literal.kind == literal_kind, "target literal kind mismatch") != 0) {
			return 1;
		}
		if (literal_kind == SQLPARSER_LITERAL_KIND_STRING &&
		    expect_true(value.literal.string_value != NULL && strcmp(value.literal.string_value, literal_string) == 0, "target literal string mismatch") != 0) {
			return 1;
		}
		if (literal_kind == SQLPARSER_LITERAL_KIND_INTEGER &&
		    expect_true(value.literal.integer_value == literal_integer, "target literal integer mismatch") != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_insert_select_target_values(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *verify_handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_set_t set_item;
	sqlparser_bind_value_t bind;
	sqlparser_literal_value_t literal;
	sqlparser_patch_t patches[3];
	sqlparser_patch_list_t patch_list;
	char *sql;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	verify_handle = NULL;
	sql = NULL;

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT 960001, 'a' FROM DUAL UNION ALL SELECT 960002, 'b' FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select literal parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select literal graph should be available") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select literal should expose four source targets") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select literal should expose four target values") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select literal dml should be available") != 0 ||
	    expect_true(dml.has_source_block != 0, "insert-select literal should expose source block") != 0 ||
	    expect_query_graph_target_value(&graph, 0U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 960001LL) != 0 ||
	    expect_query_graph_target_value(&graph, 1U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_STRING, "a", 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 2U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 960002LL) != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_STRING, "b", 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT 960001, 'a' FROM DUAL UNION SELECT 960002, 'b' FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select UNION parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select UNION graph should be available") != 0 ||
	    expect_true(graph.set_count == 1U, "insert-select UNION should expose one set") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "insert-select UNION set should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_UNION, "insert-select UNION set kind mismatch") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "insert-select UNION branch count mismatch") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select UNION source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select UNION source value count mismatch") != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_LITERAL, SQLPARSER_GRAPH_VALUE_LITERAL, NULL, SQLPARSER_BIND_KIND_NONE, 0U, NULL, SQLPARSER_LITERAL_KIND_STRING, "b", 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT :1, :2 FROM DUAL INTERSECT SELECT :3, :4 FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select INTERSECT parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select INTERSECT graph should be available") != 0 ||
	    expect_true(graph.set_count == 1U, "insert-select INTERSECT should expose one set") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "insert-select INTERSECT set should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_INTERSECT, "insert-select INTERSECT set kind mismatch") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "insert-select INTERSECT branch count mismatch") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select INTERSECT source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select INTERSECT source value count mismatch") != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "4", SQLPARSER_BIND_KIND_POSITIONAL, 4U, ":4", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT :id1, :secret1 FROM DUAL MINUS SELECT :id2, :secret2 FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select MINUS parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS graph should be available") != 0 ||
	    expect_true(graph.set_count == 1U, "insert-select MINUS should expose one set") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "insert-select MINUS set should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_EXCEPT, "insert-select MINUS set kind mismatch") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "insert-select MINUS branch count mismatch") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select MINUS source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select MINUS source value count mismatch") != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "secret2", SQLPARSER_BIND_KIND_NAMED, 4U, ":secret2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patches, 0, sizeof(patches));
	memset(&bind, 0, sizeof(bind));
	memset(&literal, 0, sizeof(literal));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "name_copy_right";
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "left-copy";
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 2U;
	patches[0].name = "NAME_COPY";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].select_targets[0]";
	patches[1].index = 2U;
	patches[1].literal = &literal;
	patches[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[2].selector = "stmt[0].select_targets[1]";
	patches[2].index = 2U;
	patches[2].bind = &bind;
	patch_list.items = patches;
	patch_list.count = 3U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS structured target patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS structured target deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "MINUS") != NULL, "insert-select MINUS should remain MINUS after patch") != 0 ||
	    expect_true(strstr(sql, "'left-copy'") != NULL, "insert-select MINUS patched literal missing") != 0 ||
	    expect_true(strstr(sql, ":name_copy_right") != NULL, "insert-select MINUS patched bind missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_parse_with_options(sql, &options, &verify_handle, &error);
	sqlparser_string_free(sql);
	sql = NULL;
	if (expect_status_ok(rc, &error, "insert-select MINUS patched SQL should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(verify_handle);
	verify_handle = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select MINUS patched graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select MINUS patched dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 3U, "insert-select MINUS patched target column count mismatch") != 0 ||
	    expect_true(graph.target_count == 6U, "insert-select MINUS patched source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 6U, "insert-select MINUS patched source value count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) "
		"SELECT :1, :2 FROM DUAL UNION ALL SELECT :id2, :secret2 FROM DUAL",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "insert-select bind parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select bind graph should be available") != 0 ||
	    expect_true(graph.target_count == 4U, "insert-select bind should expose four source targets") != 0 ||
	    expect_true(graph.value_count == 4U, "insert-select bind should expose four target values") != 0 ||
	    expect_query_graph_target_value(&graph, 0U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "1", SQLPARSER_BIND_KIND_POSITIONAL, 1U, ":1", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 1U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "2", SQLPARSER_BIND_KIND_POSITIONAL, 2U, ":2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 2U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "id2", SQLPARSER_BIND_KIND_NAMED, 3U, ":id2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0 ||
	    expect_query_graph_target_value(&graph, 3U, SQLPARSER_GRAPH_TARGET_BIND, SQLPARSER_GRAPH_VALUE_BIND, "secret2", SQLPARSER_BIND_KIND_NAMED, 4U, ":secret2", SQLPARSER_LITERAL_KIND_UNKNOWN, NULL, 0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patches, 0, sizeof(patches));
	memset(&bind, 0, sizeof(bind));
	memset(&literal, 0, sizeof(literal));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_copy1";
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "copy2";
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 2U;
	patches[0].name = "SECRET_COPY";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].select_targets[0]";
	patches[1].index = 2U;
	patches[1].bind = &bind;
	patches[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[2].selector = "stmt[0].select_targets[1]";
	patches[2].index = 2U;
	patches[2].literal = &literal;
	patch_list.items = patches;
	patch_list.count = 3U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "insert-select structured target patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "insert-select structured target deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "secret_copy") != NULL || strstr(sql, "SECRET_COPY") != NULL, "insert-select patched target column missing") != 0 ||
	    expect_true(strstr(sql, ":secret_copy1") != NULL, "insert-select patched bind missing") != 0 ||
	    expect_true(strstr(sql, "'copy2'") != NULL, "insert-select patched literal missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_parse_with_options(sql, &options, &verify_handle, &error);
	sqlparser_string_free(sql);
	sql = NULL;
	if (expect_status_ok(rc, &error, "insert-select patched SQL should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(verify_handle);
	verify_handle = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select patched graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select patched dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 3U, "insert-select patched target column count mismatch") != 0 ||
	    expect_true(graph.target_count == 6U, "insert-select patched source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 6U, "insert-select patched source value count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[0].selector = "stmt[0].insert_columns";
	patches[0].index = 3U;
	patches[0].name = "ID_COPY";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].select_targets[0]";
	patches[1].index = 3U;
	patches[1].source_selector = "stmt[0].select_target[0][0]";
	patches[2].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[2].selector = "stmt[0].select_targets[1]";
	patches[2].index = 3U;
	patches[2].source_selector = "stmt[0].select_target[1][0]";
	patch_list.items = patches;
	patch_list.count = 3U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "insert-select source target clone patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "insert-select cloned graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "insert-select cloned dml should be available") != 0 ||
	    expect_true(dml.target_columns.count == 4U, "insert-select cloned target column count mismatch") != 0 ||
	    expect_true(graph.target_count == 8U, "insert-select cloned source target count mismatch") != 0 ||
	    expect_true(graph.value_count == 8U, "insert-select cloned source value count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_oracle_multi_insert_query_graph_and_patch(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_target_t target;
	sqlparser_graph_field_t field;
	sqlparser_bind_value_t bind;
	sqlparser_literal_value_t literal;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *sql;
	size_t index;
	unsigned long generation;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT ALL "
		"INTO KDES.t1 (id, secret) VALUES (1, 'a') "
		"INTO KDES.t2 (id, phone) VALUES (2, :phone2) "
		"SELECT 1 FROM dual",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT ALL dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_ALL, "Oracle INSERT ALL mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Oracle INSERT ALL branch count mismatch") != 0 ||
	    expect_true(dml.has_source_block != 0, "Oracle INSERT ALL source block missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_span_index_at(&graph, dml.branches, 1U, &index, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL second branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT ALL second branch should be available") != 0 ||
	    expect_true(branch.ordinal == 1U, "Oracle INSERT ALL second branch ordinal mismatch") != 0 ||
	    expect_true(branch.target_columns.count == 2U, "Oracle INSERT ALL second branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 2U, "Oracle INSERT ALL second branch cell count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, branch.target_relation_index, &relation, &error), &error, "Oracle INSERT ALL second relation should be available") != 0 ||
	    expect_true(relation.schema_name != NULL && strcmp(relation.schema_name, "KDES") == 0, "Oracle INSERT ALL second relation schema mismatch") != 0 ||
	    expect_true(relation.object_name != NULL && strcmp(relation.object_name, "t2") == 0, "Oracle INSERT ALL second relation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL bind cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle INSERT ALL bind cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "Oracle INSERT ALL bind cell kind mismatch") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "phone2") == 0, "Oracle INSERT ALL named bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED, "Oracle INSERT ALL named bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "Oracle INSERT ALL bind position mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	generation = handle->generation;
	memset(&bind, 0, sizeof(bind));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_new";
	rc = sqlparser_insert_set_cell_bind(handle, 0U, 0U, 1U, &bind, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch bind replacement should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Oracle INSERT ALL bind replacement generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	generation = handle->generation;
	memset(&literal, 0, sizeof(literal));
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "phone-new";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 1U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch literal replacement should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Oracle INSERT ALL literal replacement generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL patched deparse should succeed") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (sql == NULL ||
	    strstr(sql, ":secret_new") == NULL ||
	    strstr(sql, "'phone-new'") == NULL) {
		fprintf(stderr, "FAIL: Oracle INSERT ALL patched SQL mismatch: %s\n",
		        sql != NULL ? sql : "(null)");
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	memset(&patch, 0, sizeof(patch));
	memset(&patch_list, 0, sizeof(patch_list));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_branch_columns[0]";
	patch.index = 2U;
	patch.name = "secret_copy";
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_copy";
	patch.bind = &bind;
	patch_list.items = &patch;
	patch_list.count = 1U;
	generation = handle->generation;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch column insertion should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Oracle INSERT ALL branch insertion generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch column deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "secret_copy") != NULL, "Oracle INSERT ALL inserted branch column missing") != 0 ||
	    expect_true(strstr(sql, ":secret_copy") != NULL, "Oracle INSERT ALL inserted branch bind missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL patched graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT ALL patched dml should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT ALL patched first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT ALL patched first branch should be available") != 0 ||
	    expect_true(branch.target_columns.count == 3U, "Oracle INSERT ALL patched branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 3U, "Oracle INSERT ALL patched branch cell count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_branch_columns[0]";
	patch.index = 3U;
	patch.name = "secret_clone";
	patch.source_selector = "stmt[0].insert_cell[0][1]";
	patch_list.items = &patch;
	patch_list.count = 1U;
	generation = handle->generation;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL branch cell clone should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Oracle INSERT ALL branch clone generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT ALL cloned graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT ALL cloned dml should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT ALL cloned first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT ALL cloned first branch should be available") != 0 ||
	    expect_true(branch.target_columns.count == 4U, "Oracle INSERT ALL cloned branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 4U, "Oracle INSERT ALL cloned branch cell count mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT ALL "
		"WHEN flag = 1 THEN INTO t1 (id, flag_copy) VALUES (:1, flag) "
		"WHEN flag = 2 THEN INTO t2 (id, flag_copy) VALUES (:2, flag) "
		"SELECT flag FROM src",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle conditional INSERT ALL parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle conditional INSERT ALL graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle conditional INSERT ALL dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_ALL, "Oracle conditional INSERT ALL mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Oracle conditional INSERT ALL branch count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle conditional INSERT ALL first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle conditional INSERT ALL first branch should be available") != 0 ||
	    expect_true(branch.branch_kind == SQLPARSER_GRAPH_DML_BRANCH_WHEN, "Oracle conditional INSERT ALL first branch kind mismatch") != 0 ||
	    expect_true(branch.has_condition_selector != 0, "Oracle conditional INSERT ALL condition selector missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 0U, &index, &error), &error, "Oracle conditional INSERT ALL bind cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle conditional INSERT ALL bind cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "Oracle conditional INSERT ALL bind cell kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "Oracle conditional INSERT ALL bind position mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error), &error, "Oracle conditional INSERT ALL source cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle conditional INSERT ALL source cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle conditional INSERT ALL source cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Oracle conditional INSERT ALL source target missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_selector_clause_sql(handle, &branch.condition_selector, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle conditional INSERT ALL condition selector should read SQL") != 0 ||
	    expect_true(sql != NULL && strcmp(sql, "flag = 1") == 0, "Oracle conditional INSERT ALL condition SQL mismatch") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN amount > 100 THEN INTO big_orders (id, amount) VALUES (order_id, amount) "
		"ELSE INTO small_orders (id, amount) VALUES (order_id, amount) "
		"SELECT id AS order_id, amount FROM orders",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST direct source fields should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST direct source graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT FIRST direct source dml should be available") != 0 ||
	    expect_true(dml.has_source_block != 0, "Oracle INSERT FIRST direct source block missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT FIRST direct source branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT FIRST direct source branch should be available") != 0 ||
	    expect_true(branch.branch_kind == SQLPARSER_GRAPH_DML_BRANCH_WHEN, "Oracle INSERT FIRST direct source first branch kind mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 0U, &index, &error), &error, "Oracle INSERT FIRST direct source first cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle INSERT FIRST direct source first cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle INSERT FIRST direct source first cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Oracle INSERT FIRST direct source first cell target missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, cell.source_target_index, &target, &error), &error, "Oracle INSERT FIRST direct source first target should be available") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_FIELD, "Oracle INSERT FIRST direct source first target kind mismatch") != 0 ||
	    expect_true(target.output_name != NULL && strcmp(target.output_name, "order_id") == 0, "Oracle INSERT FIRST direct source first target name mismatch") != 0 ||
	    expect_true(target.has_field != 0, "Oracle INSERT FIRST direct source first target field missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error, "Oracle INSERT FIRST direct source first field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "id") == 0, "Oracle INSERT FIRST direct source first field mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error), &error, "Oracle INSERT FIRST direct source second cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Oracle INSERT FIRST direct source second cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle INSERT FIRST direct source second cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Oracle INSERT FIRST direct source second cell target missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, cell.source_target_index, &target, &error), &error, "Oracle INSERT FIRST direct source second target should be available") != 0 ||
	    expect_true(target.has_field != 0, "Oracle INSERT FIRST direct source second target field missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error, "Oracle INSERT FIRST direct source second field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "amount") == 0, "Oracle INSERT FIRST direct source second field mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 1U, &index, &error), &error, "Oracle INSERT FIRST direct source else branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT FIRST direct source else branch should be available") != 0 ||
	    expect_true(branch.branch_kind == SQLPARSER_GRAPH_DML_BRANCH_ELSE, "Oracle INSERT FIRST direct source else branch kind mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN flag = 1 THEN INTO t1 (id, secret) VALUES (:1, :2) "
		"WHEN flag = 2 THEN INTO t2 (id, phone) VALUES (:3, :4) "
		"SELECT flag FROM src",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle INSERT FIRST dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_FIRST, "Oracle INSERT FIRST mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Oracle INSERT FIRST branch count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Oracle INSERT FIRST first branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Oracle INSERT FIRST first branch should be available") != 0 ||
	    expect_true(branch.has_condition_selector != 0, "Oracle INSERT FIRST condition selector missing") != 0 ||
	    expect_true(branch.condition_selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION, "Oracle INSERT FIRST condition selector kind mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_selector_clause_sql(handle, &branch.condition_selector, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST condition selector should read SQL") != 0 ||
	    expect_true(sql != NULL && strcmp(sql, "flag = 1") == 0, "Oracle INSERT FIRST condition SQL mismatch") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN flag = 1 THEN INTO t1 (id) VALUES (:1) INTO t2 (id) VALUES (:2) "
		"ELSE INTO t3 (id) VALUES (:3) INTO t4 (id) VALUES (:4) "
		"SELECT flag FROM src",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST grouped branches should parse") != 0) {
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Oracle INSERT FIRST grouped branches deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "WHEN flag = 1 THEN INTO t1") != NULL, "Oracle INSERT FIRST grouped WHEN should be preserved") != 0 ||
	    expect_true(strstr(sql, "WHEN flag = 1 THEN INTO t2") == NULL, "Oracle INSERT FIRST grouped WHEN should not repeat for second INTO") != 0 ||
	    expect_true(strstr(sql, "ELSE INTO t3") != NULL, "Oracle INSERT FIRST grouped ELSE should be preserved") != 0 ||
	    expect_true(strstr(sql, "ELSE INTO t4") == NULL, "Oracle INSERT FIRST grouped ELSE should not repeat for second INTO") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_dameng_multi_insert_query_graph_and_patch(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_relation_t relation;
	sqlparser_bind_value_t bind;
	sqlparser_literal_value_t literal;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *sql;
	size_t index;
	unsigned long generation;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_DAMENG;
	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT ALL "
		"INTO KDES.t1 (id, secret) VALUES (1, 'a') "
		"INTO KDES.t2 (id, phone) VALUES (2, :phone2) "
		"SELECT 1 FROM dual",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Dameng INSERT ALL dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_ALL, "Dameng INSERT ALL mode mismatch") != 0 ||
	    expect_true(dml.branches.count == 2U, "Dameng INSERT ALL branch count mismatch") != 0 ||
	    expect_true(dml.has_source_block != 0, "Dameng INSERT ALL source block missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_span_index_at(&graph, dml.branches, 1U, &index, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL second branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Dameng INSERT ALL second branch should be available") != 0 ||
	    expect_true(branch.target_columns.count == 2U, "Dameng INSERT ALL second branch column count mismatch") != 0 ||
	    expect_true(branch.rows.count == 2U, "Dameng INSERT ALL second branch cell count mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, branch.target_relation_index, &relation, &error), &error, "Dameng INSERT ALL second relation should be available") != 0 ||
	    expect_true(relation.schema_name != NULL && strcmp(relation.schema_name, "KDES") == 0, "Dameng INSERT ALL second relation schema mismatch") != 0 ||
	    expect_true(relation.object_name != NULL && strcmp(relation.object_name, "t2") == 0, "Dameng INSERT ALL second relation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_query_graph_span_index_at(&graph, branch.rows, 1U, &index, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL bind cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Dameng INSERT ALL bind cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_BIND, "Dameng INSERT ALL bind cell kind mismatch") != 0 ||
	    expect_true(cell.has_bind != 0 && strcmp(cell.bind, "phone2") == 0, "Dameng INSERT ALL named bind key mismatch") != 0 ||
	    expect_true(cell.bind_kind == SQLPARSER_BIND_KIND_NAMED, "Dameng INSERT ALL named bind kind mismatch") != 0 ||
	    expect_true(cell.has_bind_position != 0 && cell.bind_position == 1U, "Dameng INSERT ALL bind position mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	generation = handle->generation;
	memset(&bind, 0, sizeof(bind));
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_new";
	rc = sqlparser_insert_set_cell_bind(handle, 0U, 0U, 1U, &bind, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL branch bind replacement should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Dameng INSERT ALL bind replacement generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	generation = handle->generation;
	memset(&literal, 0, sizeof(literal));
	literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	literal.string_value = "phone-new";
	rc = sqlparser_insert_set_cell_literal(handle, 0U, 1U, 1U, &literal, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL branch literal replacement should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Dameng INSERT ALL literal replacement generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	memset(&patch_list, 0, sizeof(patch_list));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_branch_columns[0]";
	patch.index = 2U;
	patch.name = "secret_copy";
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_copy";
	patch.bind = &bind;
	patch_list.items = &patch;
	patch_list.count = 1U;
	generation = handle->generation;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL branch column insertion should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL, "Dameng INSERT ALL branch insertion generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT ALL patched deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "INSERT ALL") != NULL, "Dameng INSERT ALL keyword missing after patch") != 0 ||
	    expect_true(strstr(sql, ":secret_new") != NULL, "Dameng INSERT ALL patched bind missing") != 0 ||
	    expect_true(strstr(sql, "'phone-new'") != NULL, "Dameng INSERT ALL patched literal missing") != 0 ||
	    expect_true(strstr(sql, "secret_copy") != NULL, "Dameng INSERT ALL inserted branch column missing") != 0 ||
	    expect_true(strstr(sql, ":secret_copy") != NULL, "Dameng INSERT ALL inserted branch bind missing") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN amount > 100 THEN INTO big_orders (id, amount) VALUES (order_id, amount) "
		"ELSE INTO small_orders (id, amount) VALUES (order_id, amount) "
		"SELECT id AS order_id, amount FROM orders",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Dameng INSERT FIRST direct source fields should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT FIRST direct source graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Dameng INSERT FIRST direct source dml should be available") != 0 ||
	    expect_true(dml.insert_mode == SQLPARSER_GRAPH_INSERT_MODE_FIRST, "Dameng INSERT FIRST mode mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Dameng INSERT FIRST branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Dameng INSERT FIRST branch should be available") != 0 ||
	    expect_true(branch.branch_kind == SQLPARSER_GRAPH_DML_BRANCH_WHEN, "Dameng INSERT FIRST first branch kind mismatch") != 0 ||
	    expect_true(branch.has_condition_selector != 0, "Dameng INSERT FIRST condition selector missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, 0U, &index, &error), &error, "Dameng INSERT FIRST first cell span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error), &error, "Dameng INSERT FIRST first cell should be available") != 0 ||
	    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Dameng INSERT FIRST source cell kind mismatch") != 0 ||
	    expect_true(cell.has_source_target != 0, "Dameng INSERT FIRST source target missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 1U, &index, &error), &error, "Dameng INSERT FIRST else branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Dameng INSERT FIRST else branch should be available") != 0 ||
	    expect_true(branch.branch_kind == SQLPARSER_GRAPH_DML_BRANCH_ELSE, "Dameng INSERT FIRST else branch kind mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 0U, &index, &error), &error, "Dameng INSERT FIRST condition branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error), &error, "Dameng INSERT FIRST condition branch should be available") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sql = NULL;
	rc = sqlparser_selector_clause_sql(handle, &branch.condition_selector, &sql, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT FIRST condition selector should read SQL") != 0 ||
	    expect_true(sql != NULL && strcmp(sql, "amount > 100") == 0, "Dameng INSERT FIRST condition SQL mismatch") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"INSERT FIRST "
		"WHEN flag = 1 THEN INTO t1 (id) VALUES (:1) INTO t2 (id) VALUES (:2) "
		"ELSE INTO t3 (id) VALUES (:3) INTO t4 (id) VALUES (:4) "
		"SELECT flag FROM src",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Dameng INSERT FIRST grouped branches should parse") != 0) {
		return 1;
	}
	sql = NULL;
	rc = sqlparser_deparse(handle, &sql, &error);
	if (expect_status_ok(rc, &error, "Dameng INSERT FIRST grouped branches deparse should succeed") != 0 ||
	    expect_true(strstr(sql, "WHEN flag = 1 THEN INTO t1") != NULL, "Dameng INSERT FIRST grouped WHEN should be preserved") != 0 ||
	    expect_true(strstr(sql, "WHEN flag = 1 THEN INTO t2") == NULL, "Dameng INSERT FIRST grouped WHEN should not repeat for second INTO") != 0 ||
	    expect_true(strstr(sql, "ELSE INTO t3") != NULL, "Dameng INSERT FIRST grouped ELSE should be preserved") != 0 ||
	    expect_true(strstr(sql, "ELSE INTO t4") == NULL, "Dameng INSERT FIRST grouped ELSE should not repeat for second INTO") != 0) {
		sqlparser_string_free(sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_oracle_p3_update_assignment_graph(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_field_t field;
	sqlparser_graph_relation_t relation;
	sqlparser_assignment_view_t public_assignment;
	size_t assignment_index;
	size_t index;
	int found_field_value;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	handle = NULL;
	rc = sqlparser_parse_with_options(
		"UPDATE encrypt_test_data x SET x.email = :1 WHERE x.id = :2",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle P3 alias UPDATE should parse") != 0) {
		return 1;
	}
	rc = sqlparser_update_assignment(handle, 0U, 0U, &public_assignment, &error);
	if (expect_status_ok(rc, &error, "Oracle P3 public assignment should be readable") != 0 ||
	    expect_true(public_assignment.column_name != NULL && strcmp(public_assignment.column_name, "email") == 0,
	                "Oracle P3 public assignment column should be effective column") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle P3 alias UPDATE graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle P3 alias UPDATE dml should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.assignments, 0U, &assignment_index, &error),
	                     &error,
	                     "Oracle P3 alias UPDATE assignment span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, assignment_index, &assignment, &error),
	                     &error,
	                     "Oracle P3 alias UPDATE assignment should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, assignment.target_field_index, &field, &error),
	                     &error,
	                     "Oracle P3 alias UPDATE target field should be available") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "email") == 0,
	                "Oracle P3 alias UPDATE target field should be email") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_BIND, "Oracle P3 alias UPDATE assignment should keep bind value") != 0 ||
	    expect_true(assignment.has_bind != 0 && strcmp(assignment.bind, "1") == 0,
	                "Oracle P3 alias UPDATE assignment bind key mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	rc = sqlparser_parse_with_options(
		"UPDATE t SET name = s.name FROM src s WHERE t.id = s.id",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle P3 UPDATE FROM should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle P3 UPDATE FROM graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle P3 UPDATE FROM dml should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.assignments, 0U, &assignment_index, &error),
	                     &error,
	                     "Oracle P3 UPDATE FROM assignment span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, assignment_index, &assignment, &error),
	                     &error,
	                     "Oracle P3 UPDATE FROM assignment should be readable") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle P3 UPDATE FROM RHS should be a field") != 0 ||
	    expect_true(assignment.has_source_field != 0, "Oracle P3 UPDATE FROM RHS source field missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, assignment.source_field_index, &field, &error),
	                     &error,
	                     "Oracle P3 UPDATE FROM source field should be readable") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "name") == 0,
	                "Oracle P3 UPDATE FROM source field column mismatch") != 0 ||
	    expect_true(field.has_relation != 0, "Oracle P3 UPDATE FROM source field relation missing") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, field.relation_index, &relation, &error),
	                     &error,
	                     "Oracle P3 UPDATE FROM source relation should be readable") != 0 ||
	    expect_true(relation.object_name != NULL && strcmp(relation.object_name, "src") == 0,
	                "Oracle P3 UPDATE FROM source relation table mismatch") != 0 ||
	    expect_true(relation.alias_name != NULL && strcmp(relation.alias_name, "s") == 0,
	                "Oracle P3 UPDATE FROM source relation alias mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	found_field_value = 0;
	for (index = 0U; index < graph.value_count; index++) {
		sqlparser_graph_value_t value;

		if (expect_status_ok(sqlparser_query_graph_value_at(&graph, index, &value, &error),
		                     &error,
		                     "Oracle P3 UPDATE FROM value should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (value.kind == SQLPARSER_GRAPH_VALUE_FIELD && value.has_source_field) {
			found_field_value = 1;
			break;
		}
	}
	if (expect_true(found_field_value != 0, "Oracle P3 UPDATE FROM field-to-field predicate value missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (index = 0U; index < graph.predicate_count; index++) {
		sqlparser_graph_predicate_t predicate;

		if (expect_status_ok(sqlparser_query_graph_predicate_at(&graph, index, &predicate, &error),
		                     &error,
		                     "Oracle P3 UPDATE FROM predicate should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (predicate.kind == SQLPARSER_GRAPH_PREDICATE_COMPARISON &&
		    predicate.has_left_field && predicate.has_right_field) {
			sqlparser_handle_destroy(handle);
			return 0;
		}
	}
	fprintf(stderr, "FAIL: Oracle P3 UPDATE FROM field-to-field predicate missing\n");
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_oracle_p3_merge_source_target_graph(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_field_t field;
	sqlparser_graph_target_t target;
	size_t index;
	size_t branch_index;
	size_t item_index;
	int found_assignment;
	int found_cell;
	int rc;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	handle = NULL;
	rc = sqlparser_parse_with_options(
		"MERGE INTO t USING (SELECT :1 id, :2 email FROM dual) s "
		"ON (t.id=s.id) "
		"WHEN MATCHED THEN UPDATE SET t.email=s.email "
		"WHEN NOT MATCHED THEN INSERT(id,email) VALUES(s.id,s.email)",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "Oracle P3 MERGE should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle P3 MERGE graph should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "Oracle P3 MERGE dml should be readable") != 0 ||
	    expect_true(dml.rows.count == 0U && dml.target_columns.count == 0U && dml.branches.count == 2U,
	                "Oracle P3 MERGE insert payload should be branch-scoped") != 0 ||
	    expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.branches, 1U, &branch_index, &error),
	                     &error,
	                     "Oracle P3 MERGE branch span should resolve") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_branch_at(&graph, branch_index, &branch, &error),
	                     &error,
	                     "Oracle P3 MERGE branch should be readable") != 0 ||
	    expect_merge_branch_detail(
		    &graph,
		    branch_index,
		    &branch_detail,
		    &error,
		    "Oracle P3 MERGE branch detail should be readable") != 0 ||
	    expect_true(
		    branch.ordinal == 1U &&
			    branch_detail.action_kind ==
				    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
			    branch.rows.count == 2U,
		    "Oracle P3 MERGE INSERT branch mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	found_assignment = 0;
	for (index = 0U; index < dml.assignments.count; index++) {
		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, dml.assignments, index, &item_index, &error),
		                     &error,
		                     "Oracle P3 MERGE assignment span should resolve") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, item_index, &assignment, &error),
		                     &error,
		                     "Oracle P3 MERGE assignment should be readable") != 0 ||
		    expect_status_ok(sqlparser_query_graph_field_at(&graph, assignment.target_field_index, &field, &error),
		                     &error,
		                     "Oracle P3 MERGE target field should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.column_name != NULL && strcmp(field.column_name, "email") == 0) {
			if (expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_FIELD,
			                "Oracle P3 MERGE assignment RHS should be field") != 0 ||
			    expect_true(assignment.has_source_field != 0, "Oracle P3 MERGE assignment source field missing") != 0 ||
			    expect_true(assignment.has_source_target != 0, "Oracle P3 MERGE assignment source target missing") != 0 ||
			    expect_status_ok(sqlparser_query_graph_target_at(&graph, assignment.source_target_index, &target, &error),
			                     &error,
			                     "Oracle P3 MERGE assignment source target should be readable") != 0 ||
			    expect_true(target.output_name != NULL && strcmp(target.output_name, "email") == 0,
			                "Oracle P3 MERGE assignment source target name mismatch") != 0 ||
			    expect_true(target.has_value != 0, "Oracle P3 MERGE source target value missing") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			found_assignment = 1;
		}
	}
	if (expect_true(found_assignment != 0, "Oracle P3 MERGE email assignment missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	found_cell = 0;
	for (index = 0U; index < branch.rows.count; index++) {
		if (expect_status_ok(sqlparser_query_graph_span_index_at(&graph, branch.rows, index, &item_index, &error),
		                     &error,
		                     "Oracle P3 MERGE row span should resolve") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, item_index, &cell, &error),
		                     &error,
		                     "Oracle P3 MERGE cell should be readable") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cell.column_ordinal == 1U) {
			if (expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_FIELD, "Oracle P3 MERGE insert cell should be field") != 0 ||
			    expect_true(cell.has_source_field != 0, "Oracle P3 MERGE insert cell source field missing") != 0 ||
			    expect_true(cell.has_source_target != 0, "Oracle P3 MERGE insert cell source target missing") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			found_cell = 1;
		}
	}
	if (expect_true(found_cell != 0, "Oracle P3 MERGE email insert cell missing") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int expect_merge_assignment_lineage(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_t *dml,
	size_t assignment_ordinal,
	const char *target_column,
	const char *selector_text,
	size_t source_target_index,
	const char *source_target_name,
	const char *bind_key,
	size_t bind_position,
	const char *bind_sql,
	sqlparser_selector_t *out_selector)
{
	sqlparser_error_t error;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_field_t field;
	sqlparser_graph_target_t target;
	char *actual_selector_text;
	size_t assignment_index;
	int rc;

	memset(&error, 0, sizeof(error));
	memset(&assignment, 0, sizeof(assignment));
	memset(&field, 0, sizeof(field));
	memset(&target, 0, sizeof(target));
	actual_selector_text = NULL;
	if (out_selector != NULL) {
		memset(out_selector, 0, sizeof(*out_selector));
	}
	rc = sqlparser_query_graph_span_index_at(
		graph,
		dml->assignments,
		assignment_ordinal,
		&assignment_index,
		&error);
	if (expect_status_ok(rc, &error, "MERGE assignment span should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_assignment_at(
			    graph,
			    assignment_index,
			    &assignment,
			    &error),
		    &error,
		    "MERGE assignment should be readable") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_field_at(
			    graph,
			    assignment.target_field_index,
			    &field,
			    &error),
		    &error,
		    "MERGE assignment target field should be readable") != 0 ||
	    expect_true(
		    field.column_name != NULL &&
			    strcmp(field.column_name, target_column) == 0,
		    "MERGE assignment target column mismatch") != 0 ||
	    expect_true(
		    assignment.value_kind == SQLPARSER_GRAPH_VALUE_FIELD &&
			    assignment.has_source_field != 0 &&
			    assignment.has_source_target != 0 &&
			    assignment.source_target_index == source_target_index,
		    "MERGE assignment field lineage mismatch") != 0 ||
	    expect_true(
		    assignment.has_selector != 0 &&
			    assignment.selector.kind ==
				    SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT,
		    "MERGE assignment selector is missing") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_format(
			    &assignment.selector,
			    &actual_selector_text,
			    &error),
		    &error,
		    "MERGE assignment selector should format") != 0 ||
	    expect_true(
		    actual_selector_text != NULL &&
			    strcmp(actual_selector_text, selector_text) == 0,
		    "MERGE assignment selector text mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_target_at(
			    graph,
			    assignment.source_target_index,
			    &target,
			    &error),
		    &error,
		    "MERGE assignment source target should be readable") != 0 ||
	    expect_true(
		    target.output_name != NULL &&
			    strcmp(target.output_name, source_target_name) == 0,
		    "MERGE assignment source target name mismatch") != 0 ||
	    expect_query_graph_target_value(
		    graph,
		    assignment.source_target_index,
		    SQLPARSER_GRAPH_TARGET_BIND,
		    SQLPARSER_GRAPH_VALUE_BIND,
		    bind_key,
		    SQLPARSER_BIND_KIND_POSITIONAL,
		    bind_position,
		    bind_sql,
		    SQLPARSER_LITERAL_KIND_UNKNOWN,
		    NULL,
		    0LL) != 0) {
		sqlparser_string_free(actual_selector_text);
		return 1;
	}
	if (out_selector != NULL) {
		*out_selector = assignment.selector;
	}
	sqlparser_string_free(actual_selector_text);
	return 0;
}

static int test_oracle_compatible_merge_assignment_patch_closure_case(
	sqlparser_dialect_t dialect)
{
	static const char expected_inserted_sql[] =
		"MERGE INTO KDES.DBP_SQLM_USERS u "
		"USING (SELECT :1 AS ID, :2 AS PHONE FROM DUAL) s "
		"ON u.ID = s.ID "
		"WHEN MATCHED THEN UPDATE SET "
		"u.\"PHONE_ORIG\" = s.PHONE, "
		"u.PHONE = s.ID, "
		"u.phone_audit = s.PHONE";
	const char *sql;
	const char *target_parts[2];
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_selector_t source_selector;
	sqlparser_identifier_path_view_t target;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *deparsed_sql;
	unsigned long generation;
	int rc;

	sql =
		"MERGE INTO KDES.DBP_SQLM_USERS u "
		"USING (SELECT :1 ID, :2 PHONE FROM DUAL) s "
		"ON (u.ID = s.ID) "
		"WHEN MATCHED THEN UPDATE SET u.PHONE = s.PHONE";
	handle = NULL;
	reparsed = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&source_selector, 0, sizeof(source_selector));
	memset(&target, 0, sizeof(target));
	memset(&patch, 0, sizeof(patch));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "Oracle MERGE patch SQL should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "Oracle MERGE patch graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "Oracle MERGE patch DML should be readable") != 0 ||
	    expect_true(
		    dml.kind == SQLPARSER_GRAPH_DML_MERGE &&
			    dml.assignments.count == 1U,
		    "Oracle MERGE should expose one assignment") != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    0U,
		    "PHONE",
		    "stmt[0].merge_assignment[0][0]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    &source_selector) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	target_parts[0] = "u";
	target_parts[1] = "PHONE_ORIG";
	target.parts = target_parts;
	target.part_count = 2U;
	rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
		handle,
		&source_selector,
		&target,
		&source_selector,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle MERGE assignment RHS clone should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "cloned Oracle MERGE should deparse") != 0 ||
	    expect_true(
		    deparsed_sql != NULL &&
			    strstr(deparsed_sql, "u.\"PHONE_ORIG\" = s.PHONE") != NULL &&
			    strstr(deparsed_sql, "$") == NULL,
		    "cloned Oracle MERGE should preserve public spelling and bind syntax") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "cloned Oracle MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "cloned Oracle MERGE DML should be readable") != 0 ||
	    expect_true(
		    dml.assignments.count == 2U,
		    "cloned Oracle MERGE should expose two assignments") != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    0U,
		    "PHONE_ORIG",
		    "stmt[0].merge_assignment[0][0]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    1U,
		    "PHONE",
		    "stmt[0].merge_assignment[0][1]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].merge_assignment[0][1]";
	patch.sql = "s.ID";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle MERGE assignment RHS replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "replaced Oracle MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "replaced Oracle MERGE DML should be readable") != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    0U,
		    "PHONE_ORIG",
		    "stmt[0].merge_assignment[0][0]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    1U,
		    "PHONE",
		    "stmt[0].merge_assignment[0][1]",
		    0U,
		    "ID",
		    "1",
		    1U,
		    ":1",
		    NULL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patch.selector = "stmt[0].merge_assignment[0][2]";
	patch.sql = "u.phone_audit = s.PHONE";
	generation = handle->generation;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "generic Oracle-compatible MERGE assignment insert should succeed") != 0 ||
	    expect_true(
		    handle->generation == generation + 1UL,
		    "generic MERGE assignment insert generation mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "inserted Oracle-compatible MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "inserted Oracle-compatible MERGE DML should be readable") != 0 ||
	    expect_true(
		    graph.generation == handle->generation &&
			    dml.assignments.count == 3U,
		    "generic insert should rebuild the current MERGE generation") != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    0U,
		    "PHONE_ORIG",
		    "stmt[0].merge_assignment[0][0]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    1U,
		    "PHONE",
		    "stmt[0].merge_assignment[0][1]",
		    0U,
		    "ID",
		    "1",
		    1U,
		    ":1",
		    NULL) != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    2U,
		    "phone_audit",
		    "stmt[0].merge_assignment[0][2]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "inserted Oracle-compatible MERGE should deparse") != 0 ||
	    expect_true(
		    deparsed_sql != NULL &&
			    strcmp(deparsed_sql, expected_inserted_sql) == 0,
		    "inserted Oracle-compatible MERGE deparse mismatch") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_parse_with_options(
		deparsed_sql,
		&options,
		&reparsed,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "deparsed Oracle-compatible MERGE should reparse") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	rc = sqlparser_statement_query_graph(
		reparsed,
		0U,
		&graph,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "reparsed Oracle-compatible MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "reparsed Oracle-compatible MERGE DML should be readable") != 0 ||
	    expect_true(
		    graph.generation == 0UL &&
			    dml.assignments.count == 3U,
		    "reparsed Oracle-compatible MERGE graph shape mismatch") != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    0U,
		    "PHONE_ORIG",
		    "stmt[0].merge_assignment[0][0]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    1U,
		    "PHONE",
		    "stmt[0].merge_assignment[0][1]",
		    0U,
		    "ID",
		    "1",
		    1U,
		    ":1",
		    NULL) != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    2U,
		    "phone_audit",
		    "stmt[0].merge_assignment[0][2]",
		    1U,
		    "PHONE",
		    "2",
		    2U,
		    ":2",
		    NULL) != 0) {
		sqlparser_handle_destroy(reparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(reparsed);
	reparsed = NULL;

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patch.selector = "stmt[0].merge_assignment[0][2]";
	patch.sql = "u.phone_copy = s.ID";
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "generic Oracle MERGE full assignment replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "full-replaced Oracle MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "full-replaced Oracle MERGE DML should be readable") != 0 ||
	    expect_merge_assignment_lineage(
		    &graph,
		    &dml,
		    2U,
		    "phone_copy",
		    "stmt[0].merge_assignment[0][2]",
		    0U,
		    "ID",
		    "1",
		    1U,
		    ":1",
		    NULL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patch.selector = "stmt[0].merge_assignment[0][2]";
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "generic Oracle MERGE assignment delete should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "deleted Oracle MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "deleted Oracle MERGE DML should be readable") != 0 ||
	    expect_true(
		    dml.assignments.count == 2U,
		    "generic delete should remove one MERGE assignment") != 0 ||
	    expect_deparse_reparse_ok(
		    handle,
		    "patched Oracle MERGE should deparse and reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_oracle_merge_assignment_patch_closure(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_ORACLE,
		SQLPARSER_DIALECT_DAMENG,
		SQLPARSER_DIALECT_VASTBASE_ORACLE
	};
	size_t dialect_index;

	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		if (test_oracle_compatible_merge_assignment_patch_closure_case(
			    dialects[dialect_index]) != 0) {
			return 1;
		}
	}
	return 0;
}

static int test_sqlserver_merge_question_bind_positions(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	static const char sql[] =
		"MERGE INTO dbo.target t USING dbo.source s ON t.id = s.id "
		"WHEN MATCHED THEN UPDATE SET payload = 0xDEAD, phone = ? "
		"WHEN NOT MATCHED THEN INSERT (id) VALUES (?)";
	const char *target_part;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_test_merge_branch_detail_t branch_detail;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_dml_t dml;
	sqlparser_identifier_path_view_t target;
	sqlparser_query_graph_view_t graph;
	sqlparser_selector_t selector;
	sqlparser_handle_t *handle;
	size_t assignment_index;
	size_t branch_index;
	size_t cell_index;
	size_t dialect_index;
	int rc;

	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		handle = NULL;
		target_part = "phone_copy";
		memset(&error, 0, sizeof(error));
		memset(&assignment, 0, sizeof(assignment));
		memset(&cell, 0, sizeof(cell));
		memset(&dml, 0, sizeof(dml));
		memset(&target, 0, sizeof(target));
		memset(&graph, 0, sizeof(graph));
		memset(&selector, 0, sizeof(selector));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			sql,
			&options,
			&handle,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SQL Server MERGE question-bind SQL should parse") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SQL Server MERGE question-bind graph should build") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "SQL Server MERGE question-bind DML should be readable") != 0 ||
		    expect_true(
			    dml.assignments.count == 2U &&
				    dml.rows.count == 0U &&
				    dml.target_columns.count == 0U &&
				    dml.branches.count == 2U,
			    "SQL Server MERGE question-bind graph shape mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    1U,
				    &branch_index,
				    &error),
			    &error,
			    "SQL Server MERGE insert branch should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    branch_index,
				    &branch,
				    &error),
			    &error,
			    "SQL Server MERGE insert branch should be readable") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    branch_index,
			    &branch_detail,
			    &error,
			    "SQL Server MERGE insert branch detail should be readable") != 0 ||
		    expect_true(
			    branch.ordinal == 1U &&
				    branch_detail.action_kind ==
					    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
				    branch.rows.count == 1U &&
				    branch.target_columns.count == 1U,
			    "SQL Server MERGE insert branch shape mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    1U,
				    &assignment_index,
				    &error),
			    &error,
			    "SQL Server MERGE phone assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "SQL Server MERGE phone assignment should be readable") != 0 ||
		    expect_true(
			    assignment.has_bind != 0 &&
				    strcmp(assignment.bind, "1") == 0 &&
				    assignment.has_bind_position != 0 &&
				    assignment.bind_position == 1U &&
				    assignment.has_bind_sql != 0 &&
				    strcmp(assignment.bind_sql, "?") == 0,
			    "synthetic 0x parameter must not offset MERGE assignment bind") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    branch.rows,
				    0U,
				    &cell_index,
				    &error),
			    &error,
			    "SQL Server MERGE insert cell should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    cell_index,
				    &cell,
				    &error),
			    &error,
			    "SQL Server MERGE insert cell should be readable") != 0 ||
		    expect_true(
			    cell.row_index == 1U &&
				    cell.has_bind != 0 &&
				    strcmp(cell.bind, "2") == 0 &&
				    cell.has_bind_position != 0 &&
				    cell.bind_position == 2U,
			    "synthetic 0x parameter must not offset MERGE insert bind") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		selector = assignment.selector;
		target.parts = &target_part;
		target.part_count = 1U;
		rc = sqlparser_selector_insert_update_assignment_from_assignment_value(
			handle,
			&selector,
			&target,
			&selector,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SQL Server MERGE question assignment clone should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "cloned SQL Server MERGE question graph should build") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "cloned SQL Server MERGE question DML should be readable") != 0 ||
		    expect_true(
			    dml.assignments.count == 3U &&
				    dml.rows.count == 0U &&
				    dml.target_columns.count == 0U &&
				    dml.branches.count == 2U,
			    "cloned SQL Server MERGE question graph shape mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.branches,
				    1U,
				    &branch_index,
				    &error),
			    &error,
			    "cloned SQL Server MERGE insert branch should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_branch_at(
				    &graph,
				    branch_index,
				    &branch,
				    &error),
			    &error,
			    "cloned SQL Server MERGE insert branch should be readable") != 0 ||
		    expect_merge_branch_detail(
			    &graph,
			    branch_index,
			    &branch_detail,
			    &error,
			    "cloned SQL Server MERGE insert branch detail should be readable") != 0 ||
		    expect_true(
			    branch.ordinal == 1U &&
				    branch_detail.action_kind ==
					    SQLPARSER_GRAPH_MERGE_ACTION_INSERT &&
				    branch.rows.count == 1U &&
				    branch.target_columns.count == 1U,
			    "cloned SQL Server MERGE insert branch shape mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    1U,
				    &assignment_index,
				    &error),
			    &error,
			    "cloned SQL Server MERGE assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "cloned SQL Server MERGE assignment should be readable") != 0 ||
		    expect_true(
			    assignment.has_bind != 0 &&
				    strcmp(assignment.bind, "1") == 0 &&
				    assignment.has_bind_position != 0 &&
				    assignment.bind_position == 1U,
			    "cloned SQL Server MERGE bind should occupy position one") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    2U,
				    &assignment_index,
				    &error),
			    &error,
			    "shifted SQL Server MERGE assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "shifted SQL Server MERGE assignment should be readable") != 0 ||
		    expect_true(
			    assignment.has_bind != 0 &&
				    strcmp(assignment.bind, "2") == 0 &&
				    assignment.has_bind_position != 0 &&
				    assignment.bind_position == 2U,
			    "shifted SQL Server MERGE bind should occupy position two") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    branch.rows,
				    0U,
				    &cell_index,
				    &error),
			    &error,
			    "shifted SQL Server MERGE insert cell should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_cell_at(
				    &graph,
				    cell_index,
				    &cell,
				    &error),
			    &error,
			    "shifted SQL Server MERGE insert cell should be readable") != 0 ||
		    expect_true(
			    cell.row_index == 1U &&
				    cell.has_bind != 0 &&
				    strcmp(cell.bind, "3") == 0 &&
				    cell.has_bind_position != 0 &&
				    cell.bind_position == 3U,
			    "shifted SQL Server MERGE insert bind should occupy position three") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int test_sqlserver_merge_raw_question_bind_positions(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	static const char sql[] =
		"WITH src AS (SELECT ? AS id) "
		"MERGE INTO dbo.t AS t USING src AS s ON t.id = s.id "
		"WHEN MATCHED THEN UPDATE SET phone = ?";
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_dml_t dml;
	sqlparser_query_graph_view_t graph;
	sqlparser_selector_t selector;
	sqlparser_handle_t *handle;
	size_t assignment_index;
	size_t dialect_index;
	int rc;

	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		memset(&assignment, 0, sizeof(assignment));
		memset(&dml, 0, sizeof(dml));
		memset(&graph, 0, sizeof(graph));
		memset(&selector, 0, sizeof(selector));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(
			sql,
			&options,
			&handle,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "SQL Server CTE MERGE question SQL should parse") != 0 ||
		    expect_status_ok(
			    sqlparser_statement_query_graph(
				    handle,
				    0U,
				    &graph,
				    &error),
			    &error,
			    "SQL Server CTE MERGE graph should build") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml(
				    &graph,
				    &dml,
				    &error),
			    &error,
			    "SQL Server CTE MERGE DML should be readable") != 0 ||
		    expect_true(
			    dml.assignments.count == 1U,
			    "SQL Server CTE MERGE should have one assignment") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    0U,
				    &assignment_index,
				    &error),
			    &error,
			    "SQL Server CTE MERGE assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "SQL Server CTE MERGE assignment should be readable") != 0 ||
		    expect_true(
			    assignment.has_bind != 0 &&
				    strcmp(assignment.bind, "2") == 0 &&
				    assignment.has_bind_position != 0 &&
				    assignment.bind_position == 2U,
			    "CTE bind should precede the original MERGE assignment bind") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		selector = assignment.selector;
		rc = sqlparser_selector_insert_update_assignment_sql(
			handle,
			&selector,
			"phone_copy = ?",
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "raw SQL Server MERGE assignment insert should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_statement_query_graph(
				    handle,
				    0U,
				    &graph,
				    &error),
			    &error,
			    "raw SQL Server MERGE graph should rebuild") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml(
				    &graph,
				    &dml,
				    &error),
			    &error,
			    "raw SQL Server MERGE DML should be readable") != 0 ||
		    expect_true(
			    dml.assignments.count == 2U,
			    "raw SQL Server MERGE insert should add one assignment") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    0U,
				    &assignment_index,
				    &error),
			    &error,
			    "raw inserted SQL Server MERGE assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "raw inserted SQL Server MERGE assignment should be readable") != 0 ||
		    expect_true(
			    assignment.has_bind != 0 &&
				    strcmp(assignment.bind, "2") == 0 &&
				    assignment.has_bind_position != 0 &&
				    assignment.bind_position == 2U,
			    "raw inserted MERGE bind should follow the CTE bind") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    dml.assignments,
				    1U,
				    &assignment_index,
				    &error),
			    &error,
			    "shifted SQL Server MERGE assignment should resolve") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_assignment_at(
				    &graph,
				    assignment_index,
				    &assignment,
				    &error),
			    &error,
			    "shifted SQL Server MERGE assignment should be readable") != 0 ||
		    expect_true(
			    assignment.has_bind != 0 &&
				    strcmp(assignment.bind, "3") == 0 &&
				    assignment.has_bind_position != 0 &&
				    assignment.bind_position == 3U,
			    "shifted MERGE bind should follow the raw inserted bind") != 0 ||
		    expect_deparse_reparse_ok(
			    handle,
			    "raw SQL Server CTE MERGE should deparse and reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int expect_merge_patch_failure_is_atomic(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const char *message)
{
	sqlparser_error_t error;
	sqlparser_patch_list_t patch_list;
	sqlparser_query_graph_view_t before_graph;
	sqlparser_query_graph_view_t after_graph;
	sqlparser_graph_dml_t before_dml;
	sqlparser_graph_dml_t after_dml;
	char *before_sql;
	char *after_sql;
	int rc;

	memset(&error, 0, sizeof(error));
	memset(&before_graph, 0, sizeof(before_graph));
	memset(&after_graph, 0, sizeof(after_graph));
	memset(&before_dml, 0, sizeof(before_dml));
	memset(&after_dml, 0, sizeof(after_dml));
	before_sql = NULL;
	after_sql = NULL;
	patch_list.items = patch;
	patch_list.count = 1U;
	rc = sqlparser_deparse(handle, &before_sql, &error);
	if (expect_status_ok(rc, &error, message) != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &before_graph,
			    &error),
		    &error,
		    message) != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &before_graph,
			    &before_dml,
			    &error),
		    &error,
		    message) != 0) {
		sqlparser_string_free(before_sql);
		return 1;
	}
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_true(
		    rc != SQLPARSER_STATUS_OK,
		    "invalid MERGE assignment patch should fail") != 0) {
		sqlparser_string_free(before_sql);
		return 1;
	}
	rc = sqlparser_deparse(handle, &after_sql, &error);
	if (expect_status_ok(rc, &error, message) != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &after_graph,
			    &error),
		    &error,
		    message) != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(
			    &after_graph,
			    &after_dml,
			    &error),
		    &error,
		    message) != 0 ||
	    expect_true(
		    before_sql != NULL &&
			    after_sql != NULL &&
			    strcmp(before_sql, after_sql) == 0,
		    "failed MERGE assignment patch must preserve SQL") != 0 ||
	    expect_true(
		    before_graph.generation == after_graph.generation &&
			    before_dml.assignments.count ==
				    after_dml.assignments.count,
		    "failed MERGE assignment patch must preserve graph generation") != 0) {
		sqlparser_string_free(after_sql);
		sqlparser_string_free(before_sql);
		return 1;
	}
	sqlparser_string_free(after_sql);
	sqlparser_string_free(before_sql);
	return 0;
}

static int test_oracle_merge_assignment_patch_failures(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch;
	int rc;

	sql =
		"MERGE INTO KDES.DBP_SQLM_USERS u "
		"USING (SELECT :1 ID, :2 PHONE FROM DUAL) s "
		"ON (u.ID = s.ID) "
		"WHEN MATCHED THEN UPDATE SET u.PHONE = s.PHONE "
		"WHEN NOT MATCHED THEN INSERT (ID, PHONE) VALUES (s.ID, s.PHONE)";
	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Oracle MERGE failure SQL should parse") != 0) {
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].merge_assignment[1][0]";
	patch.sql = "s.ID";
	if (expect_merge_patch_failure_is_atomic(
		    handle,
		    &patch,
		    "MERGE INSERT branch assignment patch should be atomic") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	patch.selector = "stmt[0].merge_assignment[9][0]";
	if (expect_merge_patch_failure_is_atomic(
		    handle,
		    &patch,
		    "MERGE WHEN overflow patch should be atomic") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	patch.selector = "stmt[0].merge_assignment[0][9]";
	if (expect_merge_patch_failure_is_atomic(
		    handle,
		    &patch,
		    "MERGE assignment overflow patch should be atomic") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	patch.op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patch.selector = "stmt[0].merge_assignment[0][0]";
	patch.sql = "u.BROKEN";
	if (expect_merge_patch_failure_is_atomic(
		    handle,
		    &patch,
		    "bad MERGE assignment fragment should be atomic") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	patch.selector = "stmt[0].merge_assignment[0][0]";
	if (expect_merge_patch_failure_is_atomic(
		    handle,
		    &patch,
		    "deleting the last MERGE assignment should be atomic") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_vastbase_postgresql_multi_when_merge_assignment_patch(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_field_t source_field;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *selector_text;
	char *deparsed_sql;
	size_t assignment_index;
	int rc;

	sql =
		"MERGE INTO public.t AS t USING public.s AS s ON t.id = s.id "
		"WHEN MATCHED AND s.id < 0 THEN DELETE "
		"WHEN MATCHED THEN UPDATE SET phone = s.phone "
		"WHEN NOT MATCHED THEN INSERT (id, phone) VALUES (s.id, s.phone)";
	handle = NULL;
	selector_text = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_VASTBASE_POSTGRESQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "Vastbase PostgreSQL multi-WHEN MERGE should parse") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "multi-WHEN MERGE graph should build") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "multi-WHEN MERGE DML should be readable") != 0 ||
	    expect_true(
		    dml.assignments.count == 1U,
		    "multi-WHEN MERGE should expose one UPDATE assignment") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.assignments,
			    0U,
			    &assignment_index,
			    &error),
		    &error,
		    "multi-WHEN MERGE assignment span should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_assignment_at(
			    &graph,
			    assignment_index,
			    &assignment,
			    &error),
		    &error,
		    "multi-WHEN MERGE assignment should be readable") != 0 ||
	    expect_true(
		    assignment.has_selector != 0 &&
			    assignment.selector.kind ==
				    SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT &&
			    assignment.selector.item_index == 1U &&
			    assignment.selector.column_index == 0U,
		    "multi-WHEN MERGE selector should target the second WHEN") != 0 ||
	    expect_status_ok(
		    sqlparser_selector_format(
			    &assignment.selector,
			    &selector_text,
			    &error),
		    &error,
		    "multi-WHEN MERGE selector should format") != 0 ||
	    expect_true(
		    selector_text != NULL &&
			    strcmp(
				    selector_text,
				    "stmt[0].merge_assignment[1][0]") == 0,
		    "multi-WHEN MERGE selector text mismatch") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(selector_text);
	selector_text = NULL;

	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].merge_assignment[1][0]";
	patch.sql = "s.alt_phone";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "multi-WHEN MERGE directed RHS patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "patched multi-WHEN MERGE graph should rebuild") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml(&graph, &dml, &error),
		    &error,
		    "patched multi-WHEN MERGE DML should be readable") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    dml.assignments,
			    0U,
			    &assignment_index,
			    &error),
		    &error,
		    "patched multi-WHEN assignment span should resolve") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_dml_assignment_at(
			    &graph,
			    assignment_index,
			    &assignment,
			    &error),
		    &error,
		    "patched multi-WHEN assignment should be readable") != 0 ||
	    expect_true(
		    assignment.has_selector != 0 &&
			    assignment.selector.item_index == 1U &&
			    assignment.selector.column_index == 0U &&
			    assignment.has_source_field != 0,
		    "patched multi-WHEN assignment selector mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_field_at(
			    &graph,
			    assignment.source_field_index,
			    &source_field,
			    &error),
		    &error,
		    "patched multi-WHEN source field should be readable") != 0 ||
	    expect_true(
		    source_field.column_name != NULL &&
			    strcmp(source_field.column_name, "alt_phone") == 0,
		    "multi-WHEN patch should change only the selected RHS") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "patched multi-WHEN MERGE should deparse") != 0 ||
	    expect_true(
		    deparsed_sql != NULL &&
			    strstr(deparsed_sql, "alt_phone") != NULL &&
			    strstr(deparsed_sql, "THEN DELETE") != NULL &&
			    strstr(deparsed_sql, "THEN INSERT") != NULL,
		    "directed multi-WHEN patch must preserve other actions") != 0 ||
	    expect_deparse_reparse_ok(
		    handle,
		    "patched multi-WHEN MERGE should reparse") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_attribution_and_values(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_relation_t users_relation;
	sqlparser_graph_relation_t orders_relation;
	sqlparser_graph_field_t field;
	sqlparser_graph_value_t value;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_assignment_t assignment;
	char *selector_text;
	char *deparsed_sql;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	size_t index;
	int saw_users_id;
	int saw_order_no;
	int saw_order_status;
	int saw_paid_value;
	int rc;

	sql = "SELECT u.id, o.order_no FROM app.users u JOIN sales.orders o ON u.id = o.user_id WHERE o.status = 'paid'";
	handle = NULL;
	selector_text = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "query graph attribution parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph attribution should be available") != 0 ||
	    expect_true(graph.relation_count == 2U, "join should expose two relations") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 0U, &users_relation, &error), &error, "users relation should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_relation_at(&graph, 1U, &orders_relation, &error), &error, "orders relation should be available") != 0 ||
	    expect_true(strcmp(users_relation.schema_name, "app") == 0, "users schema should be app") != 0 ||
	    expect_true(strcmp(users_relation.object_name, "users") == 0, "users table should be users") != 0 ||
	    expect_true(strcmp(users_relation.alias_name, "u") == 0, "users alias should be u") != 0 ||
	    expect_true(strcmp(orders_relation.schema_name, "sales") == 0, "orders schema should be sales") != 0 ||
	    expect_true(strcmp(orders_relation.object_name, "orders") == 0, "orders table should be orders") != 0 ||
	    expect_true(strcmp(orders_relation.alias_name, "o") == 0, "orders alias should be o") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	saw_users_id = 0;
	saw_order_no = 0;
	saw_order_status = 0;
	for (index = 0U; index < graph.field_count; index++) {
		rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
		if (expect_status_ok(rc, &error, "field should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.has_relation && field.relation_index == 0U && strcmp(field.column_name, "id") == 0) {
			saw_users_id = 1;
		}
		if (field.has_relation && field.relation_index == 1U && strcmp(field.column_name, "order_no") == 0) {
			saw_order_no = 1;
		}
		if (field.has_relation && field.relation_index == 1U && strcmp(field.column_name, "status") == 0) {
			saw_order_status = 1;
		}
	}
	saw_paid_value = 0;
	for (index = 0U; index < graph.value_count; index++) {
		rc = sqlparser_query_graph_value_at(&graph, index, &value, &error);
		if (expect_status_ok(rc, &error, "WHERE literal value should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (value.kind == SQLPARSER_GRAPH_VALUE_LITERAL &&
		    value.literal.kind == SQLPARSER_LITERAL_KIND_STRING &&
		    value.literal.string_value != NULL &&
		    strcmp(value.literal.string_value, "paid") == 0 &&
		    value.has_selector != 0 &&
		    value.selector.kind == SQLPARSER_SELECTOR_KIND_VALUE) {
			saw_paid_value = 1;
			break;
		}
	}
	if (expect_true(saw_users_id != 0, "users relation should include id field") != 0 ||
	    expect_true(saw_order_no != 0, "orders relation should include order_no field") != 0 ||
	    expect_true(saw_order_status != 0, "orders relation should include status field") != 0 ||
	    expect_true(saw_paid_value != 0, "WHERE literal should expose paid graph value") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_format(&value.selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "WHERE value selector should format") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector_text;
	patch.sql = "'done'";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "WHERE value patch should succeed") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "WHERE value patched deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "'done'") != NULL, "WHERE value patch should appear in deparse") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(selector_text);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse("UPDATE users SET name = 'bob' WHERE id = 1", &handle, &error);
	if (expect_status_ok(rc, &error, "query graph update parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "query graph update should be available") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "update dml should be available") != 0 ||
	    expect_true(dml.assignments.count == 1U, "update should expose one assignment") != 0 ||
	    expect_status_ok(sqlparser_query_graph_dml_assignment_at(&graph, 0U, &assignment, &error), &error, "update assignment should be available") != 0 ||
	    expect_true(assignment.value_kind == SQLPARSER_GRAPH_VALUE_LITERAL, "update assignment should carry literal") != 0 ||
	    expect_true(assignment.literal.kind == SQLPARSER_LITERAL_KIND_STRING, "update assignment literal should be string") != 0 ||
	    expect_true(strcmp(assignment.literal.string_value, "bob") == 0, "update assignment literal mismatch") != 0 ||
	    expect_true(assignment.has_selector != 0 && assignment.selector.kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT, "update assignment selector should replace assignment") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_select_target_list_patch_api(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_field_t field;
	sqlparser_graph_target_t target;
	sqlparser_selector_t selector;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	sqlparser_parse_options_t options;
	char *target_sql;
	char *selector_text;
	char *view_json;
	char *deparsed_sql;
	size_t list_count;
	size_t target_count;
	int rc;

	sql = "SELECT * FROM public.users";
	handle = NULL;
	target_sql = NULL;
	selector_text = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "select target parse should succeed") != 0) {
		return 1;
	}

	rc = sqlparser_select_target_list_count(handle, 0U, &list_count, &error);
	if (expect_status_ok(rc, &error, "select target list count should succeed") != 0 ||
	    expect_true(list_count == 1U, "select should expose one target list") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
	if (expect_status_ok(rc, &error, "select target count should succeed") != 0 ||
	    expect_true(target_count == 1U, "select star should expose one target") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "select target SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "*") == 0, "select star target SQL should be *") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;

	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "select graph should be available") != 0 ||
	    expect_true(graph.target_count == 1U, "select star should expose one graph target") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, 0U, &target, &error), &error, "select star target should be available") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_STAR, "select star target kind mismatch") != 0 ||
	    expect_true(target.has_target_list_selector != 0, "select star should expose target list selector") != 0 ||
	    expect_true(target.has_selector != 0, "select star should expose target selector") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_selector_format(&target.target_list_selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "select target list selector should format") != 0 ||
	    expect_true(strcmp(selector_text, "stmt[0].select_targets[0]") == 0, "select target list selector mismatch") != 0) {
		sqlparser_string_free(selector_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(selector_text);
	selector_text = NULL;

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "select view JSON should export") != 0 ||
	    expect_true(strstr(view_json, "\"target_list_selector\":\"stmt[0].select_targets[0]\"") != NULL,
	                "select view JSON should expose target list selector") != 0 ||
	    expect_true(strstr(view_json, "\"selector\":\"stmt[0].select_target[0][0]\"") != NULL,
	                "select view JSON should expose target selector") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_select_set_targets_sql(handle, 0U, 0U, "id, name, created_at", &error);
	if (expect_status_ok(rc, &error, "select target list replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select target list replace should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_insert_target_sql(handle, 0U, 0U, 2U, "upper(name) AS upper_name", &error);
	if (expect_status_ok(rc, &error, "select target insert should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select target insert should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_delete_target(handle, 0U, 0U, 3U, &error);
	if (expect_status_ok(rc, &error, "select target delete should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select target delete should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_set_target_sql(handle, 0U, 0U, 0U, "users.id AS user_id", &error);
	if (expect_status_ok(rc, &error, "select single target replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "select single target replace should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
	if (expect_status_ok(rc, &error, "patched select target count should succeed") != 0 ||
	    expect_true(target_count == 3U, "patched select should expose three targets") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 1U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "patched select target SQL should succeed") != 0 ||
	    expect_true(strstr(target_sql, "name") != NULL, "patched select target should contain name") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "patched select deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "users.id AS user_id") != NULL, "patched select should contain user_id target") != 0 ||
	    expect_true(strstr(deparsed_sql, "upper(name) AS upper_name") != NULL, "patched select should contain inserted expression") != 0 ||
	    expect_true(strstr(deparsed_sql, "created_at") == NULL, "patched select should remove created_at") != 0 ||
	    expect_true(strstr(deparsed_sql, "SELECT *") == NULL, "patched select should remove star") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT u.* FROM public.users u";
	handle = NULL;
	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "qualified star parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "qualified star SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "u.*") == 0, "qualified star SQL should be u.*") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT * FROM abc";
	handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "mysql select target parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_select_set_targets_sql(handle, 0U, 0U, "`a`,`b,c`", &error);
	if (expect_status_ok(rc, &error, "mysql select target replace should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "mysql select target replace should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
	if (expect_status_ok(rc, &error, "mysql patched target count should succeed") != 0 ||
	    expect_true(target_count == 2U, "mysql patched target count should be two") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "mysql first patched target SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "`a`") == 0, "mysql first patched target should retain backticks") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 1U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "mysql second patched target SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "`b,c`") == 0, "mysql comma identifier should remain one backtick target") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "mysql patched target graph should succeed") != 0 ||
	    expect_true(graph.target_count == 2U, "mysql patched graph should expose two targets") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, 0U, &target, &error), &error,
	                     "mysql first patched graph target should succeed") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_FIELD && target.has_field,
	                "mysql first patched graph target should be a field") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error,
	                     "mysql first patched graph field should succeed") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "a") == 0,
	                "mysql first patched graph field should be a") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, 1U, &target, &error), &error,
	                     "mysql second patched graph target should succeed") != 0 ||
	    expect_true(target.kind == SQLPARSER_GRAPH_TARGET_FIELD && target.has_field,
	                "mysql second patched graph target should be a field") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error,
	                     "mysql second patched graph field should succeed") != 0 ||
	    expect_true(field.column_name != NULL && strcmp(field.column_name, "b,c") == 0,
	                "mysql comma identifier should remain one graph field") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "mysql select target deparse should succeed") != 0 ||
	    expect_true(strcmp(deparsed_sql, "SELECT `a`, `b,c` FROM abc") == 0,
	                "mysql deparse should retain patched backtick targets") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "mysql deparse should not expose internal state") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT * FROM users";
	handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "oracle select target parse should succeed") != 0) {
		return 1;
	}
	selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGETS;
	selector.statement_index = 0U;
	selector.item_index = 0U;
	rc = sqlparser_selector_format(&selector, &selector_text, &error);
	if (expect_status_ok(rc, &error, "oracle select targets selector should format") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = selector_text;
	patch.sql = ":id AS id, q'[Bob's order]' AS label";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	sqlparser_string_free(selector_text);
	selector_text = NULL;
	if (expect_status_ok(rc, &error, "oracle select target patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "oracle select target patch should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "oracle select target deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, ":id AS id") != NULL, "oracle deparse should restore named bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "oracle deparse should not expose internal bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	sqlparser_handle_destroy(handle);

	sql = "SELECT * FROM [dbo].[users]";
	handle = NULL;
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "sqlserver select target parse should succeed") != 0) {
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].select_targets[0]";
	patch.index = 1U;
	patch.sql = "[name] AS [display_name]";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "sqlserver select target insert patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_reparse_ok(handle, "sqlserver select target patch should produce parseable SQL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_select_target_sql(handle, 0U, 0U, 1U, &target_sql, &error);
	if (expect_status_ok(rc, &error, "sqlserver patched target SQL should succeed") != 0 ||
	    expect_true(strcmp(target_sql, "[name] AS [display_name]") == 0,
	                "sqlserver patched target should retain bracket delimiters") != 0) {
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "sqlserver select target deparse should succeed") != 0 ||
	    expect_true(strcmp(deparsed_sql, "SELECT *, [name] AS [display_name] FROM [dbo].[users]") == 0,
	                "sqlserver deparse should retain patched bracket delimiters") != 0 ||
	    expect_true(strstr(deparsed_sql, "$") == NULL, "sqlserver deparse should not expose internal bind markers") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_relation_patch_identifier_path_spelling(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *replacement;
		const char *expected_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"\"Db.Dot\".\"SchemaCase\".MixedTable",
			"SELECT 1 FROM \"Db.Dot\".\"SchemaCase\".MixedTable"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"`Db.Dot`.`SchemaCase`.MixedTable",
			"SELECT 1 FROM `Db.Dot`.`SchemaCase`.MixedTable"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"[Db.Dot].\"SchemaCase\".MixedTable",
			"SELECT 1 FROM [Db.Dot].\"SchemaCase\".MixedTable"
		}
	};
	sqlparser_parse_options_t options;
	size_t case_index;

	for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_relation_view_t relation;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_relation_t graph_relation;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *view_json;
		int rc;

		handle = NULL;
		view_json = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		rc = sqlparser_parse_with_options(
			"SELECT 1 FROM old_db.old_schema.old_table",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "relation identifier patch baseline should parse") != 0) {
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = "stmt[0].relation[0]";
		patch.sql = cases[case_index].replacement;
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "relation identifier path patch should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		memset(&relation, 0, sizeof(relation));
		rc = sqlparser_statement_relation(handle, 0U, 0U, &relation, &error);
		if (expect_status_ok(rc, &error, "patched relation view should succeed") != 0 ||
		    expect_true(
			    relation.database_name != NULL &&
				    strcmp(relation.database_name, "Db.Dot") == 0 &&
				    relation.schema_name != NULL &&
				    strcmp(relation.schema_name, "SchemaCase") == 0 &&
				    relation.table_name != NULL &&
				    strcmp(relation.table_name, "MixedTable") == 0,
			    "patched relation view should expose decoded identifier components") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "patched relation graph should succeed") != 0 ||
		    expect_true(graph.relation_count == 1U, "patched relation graph count mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_relation_at(&graph, 0U, &graph_relation, &error),
			    &error,
			    "patched graph relation should succeed") != 0 ||
		    expect_true(
			    graph_relation.database_name != NULL &&
				    strcmp(graph_relation.database_name, "Db.Dot") == 0 &&
				    graph_relation.schema_name != NULL &&
				    strcmp(graph_relation.schema_name, "SchemaCase") == 0 &&
				    graph_relation.object_name != NULL &&
				    strcmp(graph_relation.object_name, "MixedTable") == 0,
			    "patched graph relation should preserve semantic components") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "patched relation JSON view should succeed") != 0 ||
		    expect_true(
			    view_json != NULL &&
				    strstr(view_json, "Db.Dot") != NULL &&
				    strstr(view_json, "SchemaCase") != NULL &&
				    strstr(view_json, "MixedTable") != NULL,
			    "patched relation JSON view should contain decoded components") != 0 ||
		    expect_deparse_equals_and_reparse(
			    handle,
			    cases[case_index].expected_sql,
			    "patched relation path should deparse exactly and reparse") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *before_sql;
		char *after_sql;
		int rc;

		handle = NULL;
		before_sql = NULL;
		after_sql = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse("SELECT 1 FROM public.old_table", &handle, &error);
		if (expect_status_ok(rc, &error, "invalid relation patch baseline should parse") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &before_sql, &error), &error,
		                     "invalid relation patch baseline should deparse") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = "stmt[0].relation[0]";
		patch.sql = "\"unterminated";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_true(rc != SQLPARSER_STATUS_OK, "invalid relation identifier path should fail") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &after_sql, &error), &error,
		                     "failed relation patch should leave handle deparseable") != 0 ||
		    expect_true(
			    before_sql != NULL && after_sql != NULL && strcmp(before_sql, after_sql) == 0,
			    "failed relation identifier patch must be atomic") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_string_free(after_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(before_sql);
		sqlparser_string_free(after_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_insert_column_patch_identifier_spelling(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
		const char *decoded_name;
		const char *expected_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"\"Col.Dot\"",
			"Col.Dot",
			"INSERT INTO T (C, \"Col.Dot\") VALUES (1, 2)"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"AddedMixed",
			"AddedMixed",
			"INSERT INTO T (C, AddedMixed) VALUES (1, 2)"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"`Col.Dot`",
			"Col.Dot",
			"INSERT INTO T (C, `Col.Dot`) VALUES (1, 2)"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"[Col.Dot]",
			"Col.Dot",
			"INSERT INTO T (C, [Col.Dot]) VALUES (1, 2)"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"\"DoubleCol\"",
			"DoubleCol",
			"INSERT INTO T (C, \"DoubleCol\") VALUES (1, 2)"
		}
	};
	sqlparser_parse_options_t options;
	size_t case_index;

	for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_column_t column;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		const char *column_name;
		char *view_json;
		size_t column_index;
		int rc;

		handle = NULL;
		view_json = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		rc = sqlparser_parse_with_options(
			"INSERT INTO T (C) VALUES (1)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "insert-column identifier baseline should parse") != 0) {
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
		patch.selector = "stmt[0].insert_columns";
		patch.index = 1U;
		patch.name = cases[case_index].name;
		patch.default_sql = "2";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "insert-column identifier patch should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_insert_column_name(handle, 0U, 1U, &column_name, &error),
			    &error,
			    "insert-column decoded name should succeed") != 0 ||
		    expect_true(
			    column_name != NULL && strcmp(column_name, cases[case_index].decoded_name) == 0,
			    "insert-column view must not contain delimiters") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "insert-column graph should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error,
		                     "insert-column DML graph should succeed") != 0 ||
		    expect_true(dml.target_columns.count == 2U, "insert-column graph count mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph, dml.target_columns, 1U, &column_index, &error),
			    &error,
			    "insert-column graph index should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_dml_column_at(&graph, column_index, &column, &error),
			    &error,
			    "insert-column graph item should succeed") != 0 ||
		    expect_true(
			    column.column_name != NULL &&
				    strcmp(column.column_name, cases[case_index].decoded_name) == 0,
			    "insert-column graph must expose decoded identifier") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "insert-column JSON view should succeed") != 0 ||
		    expect_true(
			    view_json != NULL &&
				    strstr(view_json, cases[case_index].decoded_name) != NULL,
			    "insert-column JSON view should contain decoded identifier") != 0 ||
		    expect_deparse_equals_and_reparse(
			    handle,
			    cases[case_index].expected_sql,
			    "insert-column identifier should deparse exactly and reparse") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		char *before_sql;
		char *after_sql;
		int rc;

		handle = NULL;
		before_sql = NULL;
		after_sql = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse("INSERT INTO T (C) VALUES (1)", &handle, &error);
		if (expect_status_ok(rc, &error, "invalid insert-column baseline should parse") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &before_sql, &error), &error,
		                     "invalid insert-column baseline should deparse") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
		patch.selector = "stmt[0].insert_columns";
		patch.index = 1U;
		patch.name = "Bad.Name";
		patch.default_sql = "2";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_true(rc != SQLPARSER_STATUS_OK, "qualified insert-column name should fail") != 0 ||
		    expect_status_ok(sqlparser_deparse(handle, &after_sql, &error), &error,
		                     "failed insert-column patch should leave handle deparseable") != 0 ||
		    expect_true(
			    before_sql != NULL && after_sql != NULL && strcmp(before_sql, after_sql) == 0,
			    "failed insert-column identifier patch must be atomic") != 0) {
			sqlparser_string_free(before_sql);
			sqlparser_string_free(after_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(before_sql);
		sqlparser_string_free(after_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_fragment_mutation_identifier_spelling(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *expression;
		const char *condition;
		const char *order_by;
		const char *assignment;
		const char *assignment_rhs;
		const char *set_list;
		const char *generated_column;
		const char *field_names[3];
		size_t field_count;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"COALESCE(\"QuotedCase\", MixedCase)",
			"\"QuotedCase\" = MixedCase",
			"\"QuotedCase\", MixedCase",
			"\"TargetCase\" = \"SourceCase\"",
			"\"SourceCase\"",
			"\"TargetCase\" = \"SourceCase\", MixedTarget = MixedSource",
			"AddedColumn",
			{"QuotedCase", "MixedCase", NULL},
			2U
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"COALESCE(`BacktickCase`, MixedCase)",
			"`BacktickCase` = MixedCase",
			"`BacktickCase`, MixedCase",
			"`TargetCase` = `SourceCase`",
			"`SourceCase`",
			"`TargetCase` = `SourceCase`, MixedTarget = MixedSource",
			"AddedColumn",
			{"BacktickCase", "MixedCase", NULL},
			2U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"COALESCE([BracketCase], \"DoubleCase\", MixedCase)",
			"[BracketCase] = \"DoubleCase\" AND MixedCase = 1",
			"[BracketCase], \"DoubleCase\", MixedCase",
			"[TargetCase] = \"SourceCase\"",
			"\"SourceCase\"",
			"[TargetCase] = \"SourceCase\", MixedTarget = MixedSource",
			"AddedColumn",
			{"BracketCase", "DoubleCase", "MixedCase"},
			3U
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	size_t case_index;
	int rc;

	for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
		sqlparser_handle_t *handle;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_dml_t dml;
		sqlparser_graph_dml_cell_t cell;
		sqlparser_graph_dml_assignment_t graph_assignment;
		sqlparser_graph_dml_column_t column;
		sqlparser_graph_field_t field;
		sqlparser_assignment_view_t assignment;
		sqlparser_graph_value_t value;
		sqlparser_patch_t patch;
		sqlparser_patch_list_t patch_list;
		sqlparser_selector_t value_selector;
		const char *value_fields[4];
		char expected_sql[512];
		char expected_fragment[256];
		char *fragment;
		char *selector_text;
		size_t graph_index;
		size_t item_index;
		int found_value_selector;

		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"INSERT INTO T (C) VALUES (1)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling insert-cell parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_insert_set_cell_sql(
			handle,
			0U,
			0U,
			0U,
			cases[case_index].expression,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling insert-cell mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_insert_cell_sql(handle, 0U, 0U, 0U, &fragment, &error);
		if (expect_status_ok(rc, &error, "fragment spelling insert-cell getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].expression) == 0,
			    "fragment spelling insert-cell getter should preserve SQL") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		fragment = NULL;
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "fragment spelling insert-cell graph should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error,
		                     "fragment spelling insert-cell DML should succeed") != 0 ||
		    expect_true(dml.kind == SQLPARSER_GRAPH_DML_INSERT && dml.rows.count == 1U,
		                "fragment spelling insert-cell DML shape mismatch") != 0 ||
		    expect_status_ok(sqlparser_query_graph_span_index_at(
		                         &graph, dml.rows, 0U, &item_index, &error),
		                     &error,
		                     "fragment spelling insert-cell index should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, item_index, &cell, &error), &error,
		                     "fragment spelling insert-cell graph item should succeed") != 0 ||
		    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_EXPRESSION,
		                "fragment spelling insert-cell should remain an expression") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"INSERT INTO T (C) VALUES (%s)",
			cases[case_index].expression);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling insert-cell deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"UPDATE T SET C = 1",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling assignment RHS parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_update_set_assignment_sql(
			handle,
			0U,
			0U,
			cases[case_index].expression,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling assignment RHS mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment_sql(handle, 0U, 0U, &fragment, &error);
		memset(&assignment, 0, sizeof(assignment));
		if (expect_status_ok(rc, &error, "fragment spelling assignment RHS getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].expression) == 0,
			    "fragment spelling assignment RHS getter should preserve SQL") != 0 ||
		    expect_status_ok(sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error), &error,
		                     "fragment spelling assignment RHS view should succeed") != 0 ||
		    expect_true(
			    assignment.column_name != NULL && strcmp(assignment.column_name, "C") == 0 &&
				    assignment.value_kind == SQLPARSER_VALUE_KIND_EXPRESSION,
			    "fragment spelling assignment RHS view mismatch") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		fragment = NULL;
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "fragment spelling assignment RHS graph should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error,
		                     "fragment spelling assignment RHS DML should succeed") != 0 ||
		    expect_true(dml.assignments.count == 1U,
		                "fragment spelling assignment RHS graph count mismatch") != 0 ||
		    expect_status_ok(sqlparser_query_graph_span_index_at(
		                         &graph, dml.assignments, 0U, &item_index, &error),
		                     &error,
		                     "fragment spelling assignment RHS index should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_assignment_at(
		                         &graph, item_index, &graph_assignment, &error),
		                     &error,
		                     "fragment spelling assignment RHS graph item should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_field_at(
		                         &graph, graph_assignment.target_field_index, &field, &error),
		                     &error,
		                     "fragment spelling assignment RHS target should succeed") != 0 ||
		    expect_true(
			    graph_assignment.value_kind == SQLPARSER_GRAPH_VALUE_EXPRESSION &&
				    field.column_name != NULL && strcmp(field.column_name, "C") == 0,
			    "fragment spelling assignment RHS graph mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"UPDATE T SET C = %s",
			cases[case_index].expression);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling assignment RHS deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"UPDATE T SET C = 1",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling assignment insert parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_update_insert_assignment_sql(
			handle,
			0U,
			1U,
			cases[case_index].assignment,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling assignment insert should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment_sql(handle, 0U, 1U, &fragment, &error);
		memset(&assignment, 0, sizeof(assignment));
		if (expect_status_ok(rc, &error, "fragment spelling inserted assignment getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].assignment_rhs) == 0,
			    "fragment spelling inserted assignment RHS should preserve SQL") != 0 ||
		    expect_status_ok(sqlparser_update_assignment(handle, 0U, 1U, &assignment, &error), &error,
		                     "fragment spelling inserted assignment view should succeed") != 0 ||
		    expect_true(
			    assignment.column_name != NULL && strcmp(assignment.column_name, "TargetCase") == 0,
			    "fragment spelling inserted assignment target case mismatch") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"UPDATE T SET C = 1, %s",
			cases[case_index].assignment);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling assignment insert deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"UPDATE T SET C = 1",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling full assignment parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_update_set_assignment_full_sql(
			handle,
			0U,
			0U,
			cases[case_index].assignment,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling full assignment mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_update_assignment_sql(handle, 0U, 0U, &fragment, &error);
		memset(&assignment, 0, sizeof(assignment));
		if (expect_status_ok(rc, &error, "fragment spelling full assignment getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].assignment_rhs) == 0,
			    "fragment spelling full assignment RHS should preserve SQL") != 0 ||
		    expect_status_ok(sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error), &error,
		                     "fragment spelling full assignment view should succeed") != 0 ||
		    expect_true(
			    assignment.column_name != NULL && strcmp(assignment.column_name, "TargetCase") == 0,
			    "fragment spelling full assignment target case mismatch") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"UPDATE T SET %s",
			cases[case_index].assignment);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling full assignment deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"UPDATE T SET C = 1",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling SET-list parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_set_clause_sql(
			handle,
			0U,
			0U,
			cases[case_index].set_list,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling SET-list mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &fragment, &error);
		if (expect_status_ok(rc, &error, "fragment spelling SET-list getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].set_list) == 0,
			    "fragment spelling SET-list getter should preserve SQL") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		fragment = NULL;
		memset(&assignment, 0, sizeof(assignment));
		if (expect_status_ok(sqlparser_update_assignment(handle, 0U, 0U, &assignment, &error), &error,
		                     "fragment spelling SET-list first assignment should succeed") != 0 ||
		    expect_true(
			    assignment.column_name != NULL && strcmp(assignment.column_name, "TargetCase") == 0,
			    "fragment spelling SET-list quoted target mismatch") != 0 ||
		    expect_status_ok(sqlparser_update_assignment(handle, 0U, 1U, &assignment, &error), &error,
		                     "fragment spelling SET-list second assignment should succeed") != 0 ||
		    expect_true(
			    assignment.column_name != NULL && strcmp(assignment.column_name, "MixedTarget") == 0,
			    "fragment spelling SET-list mixed-case target mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"UPDATE T SET %s",
			cases[case_index].set_list);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling SET-list deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"SELECT 1 FROM T ORDER BY %s",
			cases[case_index].order_by);
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			expected_sql,
			&options,
			&handle,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "parsed fragment spelling ORDER BY should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_clause_sql(
			handle,
			0U,
			2U,
			&fragment,
			&error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "parsed fragment spelling ORDER BY getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL &&
				    strcmp(
					    fragment,
					    cases[case_index].order_by) == 0,
			    "parsed fragment spelling ORDER BY getter should preserve SQL") != 0 ||
		    expect_query_graph_clause_fields(
			    handle,
			    0U,
			    SQLPARSER_CLAUSE_KIND_ORDER_BY,
			    cases[case_index].field_names,
			    cases[case_index].field_count,
			    "parsed fragment spelling ORDER BY graph fields should preserve case") != 0 ||
		    expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "parsed fragment spelling ORDER BY deparse should be exact") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"SELECT 1 FROM T",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling WHERE parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_set_where_sql(
			handle,
			0U,
			0U,
			cases[case_index].condition,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling WHERE mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_where_sql(handle, 0U, 0U, &fragment, &error);
		if (expect_status_ok(rc, &error, "fragment spelling WHERE getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].condition) == 0,
			    "fragment spelling WHERE getter should preserve SQL") != 0 ||
		    expect_query_graph_clause_fields(
			    handle,
			    0U,
			    SQLPARSER_CLAUSE_KIND_WHERE,
			    cases[case_index].field_names,
			    cases[case_index].field_count,
			    "fragment spelling WHERE graph fields should preserve case") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"SELECT 1 FROM T WHERE %s",
			cases[case_index].condition);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling WHERE deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"SELECT 1 FROM T",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling ORDER BY parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_set_clause_sql(
			handle,
			0U,
			2U,
			cases[case_index].order_by,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling ORDER BY mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_clause_sql(handle, 0U, 2U, &fragment, &error);
		if (expect_status_ok(rc, &error, "fragment spelling ORDER BY getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].order_by) == 0,
			    "fragment spelling ORDER BY getter should preserve SQL") != 0 ||
		    expect_query_graph_clause_fields(
			    handle,
			    0U,
			    SQLPARSER_CLAUSE_KIND_ORDER_BY,
			    cases[case_index].field_names,
			    cases[case_index].field_count,
			    "fragment spelling ORDER BY graph fields should preserve case") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"SELECT 1 FROM T ORDER BY %s",
			cases[case_index].order_by);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling ORDER BY deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		selector_text = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"SELECT * FROM T WHERE C = 1",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling VALUE parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "fragment spelling VALUE graph should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		found_value_selector = 0;
		memset(&value_selector, 0, sizeof(value_selector));
		for (graph_index = 0U; graph_index < graph.value_count; graph_index++) {
			rc = sqlparser_query_graph_value_at(&graph, graph_index, &value, &error);
			if (expect_status_ok(rc, &error, "fragment spelling VALUE item should succeed") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (value.kind == SQLPARSER_GRAPH_VALUE_LITERAL &&
			    value.literal.kind == SQLPARSER_LITERAL_KIND_INTEGER &&
			    value.literal.integer_value == 1LL &&
			    value.has_selector) {
				value_selector = value.selector;
				found_value_selector = 1;
				break;
			}
		}
		if (expect_true(found_value_selector, "fragment spelling VALUE selector should exist") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_selector_format(&value_selector, &selector_text, &error);
		if (expect_status_ok(rc, &error, "fragment spelling VALUE selector should format") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = selector_text;
		patch.sql = cases[case_index].expression;
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		sqlparser_string_free(selector_text);
		selector_text = NULL;
		if (expect_status_ok(rc, &error, "fragment spelling VALUE mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(
			expected_fragment,
			sizeof(expected_fragment),
			"C = %s",
			cases[case_index].expression);
		rc = sqlparser_statement_where_sql(handle, 0U, 0U, &fragment, &error);
		if (expect_status_ok(rc, &error, "fragment spelling VALUE getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, expected_fragment) == 0,
			    "fragment spelling VALUE getter should preserve surrounding SQL") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		value_fields[0] = "C";
		for (graph_index = 0U; graph_index < cases[case_index].field_count; graph_index++) {
			value_fields[graph_index + 1U] = cases[case_index].field_names[graph_index];
		}
		if (expect_query_graph_clause_fields(
			    handle,
			    0U,
			    SQLPARSER_CLAUSE_KIND_WHERE,
			    value_fields,
			    cases[case_index].field_count + 1U,
			    "fragment spelling VALUE graph fields should preserve case") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"SELECT * FROM T WHERE C = %s",
			cases[case_index].expression);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling VALUE deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);

		handle = NULL;
		fragment = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse_with_options(
			"INSERT INTO T (C) VALUES (1)",
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling INSERT_COLUMN parse should succeed") != 0) {
			return 1;
		}
		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
		patch.selector = "stmt[0].insert_columns";
		patch.index = 1U;
		patch.name = "AddedColumn";
		patch.default_sql = cases[case_index].expression;
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "fragment spelling INSERT_COLUMN mutation should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_insert_cell_sql(handle, 0U, 0U, 1U, &fragment, &error);
		if (expect_status_ok(rc, &error, "fragment spelling INSERT_COLUMN default getter should succeed") != 0 ||
		    expect_true(
			    fragment != NULL && strcmp(fragment, cases[case_index].expression) == 0,
			    "fragment spelling INSERT_COLUMN default should preserve SQL") != 0) {
			sqlparser_string_free(fragment);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(fragment);
		fragment = NULL;
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "fragment spelling INSERT_COLUMN graph should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml(&graph, &dml, &error), &error,
		                     "fragment spelling INSERT_COLUMN DML should succeed") != 0 ||
		    expect_true(dml.target_columns.count == 2U && dml.rows.count == 2U,
		                "fragment spelling INSERT_COLUMN graph shape mismatch") != 0 ||
		    expect_status_ok(sqlparser_query_graph_span_index_at(
		                         &graph, dml.target_columns, 1U, &item_index, &error),
		                     &error,
		                     "fragment spelling INSERT_COLUMN target index should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_column_at(&graph, item_index, &column, &error), &error,
		                     "fragment spelling INSERT_COLUMN target should succeed") != 0 ||
		    expect_true(
			    column.column_name != NULL && strcmp(column.column_name, "AddedColumn") == 0,
			    "fragment spelling INSERT_COLUMN target case mismatch") != 0 ||
		    expect_status_ok(sqlparser_query_graph_span_index_at(
		                         &graph, dml.rows, 1U, &item_index, &error),
		                     &error,
		                     "fragment spelling INSERT_COLUMN cell index should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_dml_cell_at(&graph, item_index, &cell, &error), &error,
		                     "fragment spelling INSERT_COLUMN cell should succeed") != 0 ||
		    expect_true(cell.kind == SQLPARSER_GRAPH_VALUE_EXPRESSION,
		                "fragment spelling INSERT_COLUMN default should remain expression") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		snprintf(
			expected_sql,
			sizeof(expected_sql),
			"INSERT INTO T (C, %s) VALUES (1, %s)",
			cases[case_index].generated_column,
			cases[case_index].expression);
		if (expect_deparse_equals_and_reparse(
			    handle,
			    expected_sql,
			    "fragment spelling INSERT_COLUMN deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_special_fragment_mutation_identifier_spelling(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *replacement;
		const char *expected_sql;
		const char *expected_value;
	} session_cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET search_path TO app_schema",
			"MixedSchema",
			"SET search_path TO MixedSchema",
			"MixedSchema"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET search_path TO app_schema",
			"\"QuotedSchema\"",
			"SET search_path TO \"QuotedSchema\"",
			"QuotedSchema"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"USE analytics",
			"`BacktickSchema`",
			"USE `BacktickSchema`",
			"BacktickSchema"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [OldDatabase]",
			"[BracketSchema]",
			"USE [BracketSchema]",
			"BracketSchema"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [OldDatabase]",
			"\"DoubleSchema\"",
			"USE \"DoubleSchema\"",
			"DoubleSchema"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [OldDatabase]",
			"[A]]B]",
			"USE [A]]B]",
			"A]B"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [OldDatabase]",
			"\"A\"\"B\"",
			"USE \"A\"\"B\"",
			"A\"B"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [OldDatabase]",
			"\"A]B\"",
			"USE \"A]B\"",
			"A]B"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [OldDatabase]",
			"MixedSchema",
			"USE MixedSchema",
			"MixedSchema"
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_session_t session;
	sqlparser_graph_session_item_t session_item;
	sqlparser_graph_session_value_t session_value;
	sqlparser_literal_view_t literal;
	sqlparser_patch_t patches[2];
	sqlparser_patch_list_t patch_list;
	sqlparser_selector_t selector;
	sqlparser_graph_target_t target;
	sqlparser_graph_field_t field;
	const char *control_fields[] = {"BracketCase", "DoubleCase", "MixedCase"};
	char *fragment;
	size_t case_index;
	int rc;

	for (case_index = 0U;
	     case_index < sizeof(session_cases) / sizeof(session_cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = session_cases[case_index].dialect;
		rc = sqlparser_parse_with_options(
			session_cases[case_index].sql,
			&options,
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "fragment spelling session parse should succeed") != 0) {
			return 1;
		}
		memset(patches, 0, sizeof(patches));
		patches[0].op = SQLPARSER_PATCH_REPLACE;
		patches[0].selector = "stmt[0].value[0]";
		patches[0].sql = session_cases[case_index].replacement;
		patch_list.items = patches;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		memset(&literal, 0, sizeof(literal));
		if (expect_status_ok(rc, &error, "fragment spelling session VALUE mutation should succeed") != 0 ||
		    expect_status_ok(sqlparser_statement_literal(handle, 0U, 0U, &literal, &error), &error,
		                     "fragment spelling session literal getter should succeed") != 0 ||
		    expect_true(
			    literal.kind == SQLPARSER_LITERAL_KIND_STRING &&
				    literal.string_value != NULL &&
				    strcmp(literal.string_value, session_cases[case_index].expected_value) == 0,
			    "fragment spelling session literal getter mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "fragment spelling session graph should succeed") != 0 ||
		    expect_status_ok(sqlparser_query_graph_session(&graph, &session, &error), &error,
		                     "fragment spelling session view should succeed") != 0 ||
		    expect_true(session.item_count == 1U, "fragment spelling session item count mismatch") != 0 ||
		    expect_status_ok(sqlparser_query_graph_session_item_at(&graph, 0U, &session_item, &error), &error,
		                     "fragment spelling session item should succeed") != 0 ||
		    expect_true(session_item.value_count == 1U,
		                "fragment spelling session value count mismatch") != 0 ||
		    expect_status_ok(sqlparser_query_graph_session_value_at(
		                         &graph, session_item.value_offset, &session_value, &error),
		                     &error,
		                     "fragment spelling session value should succeed") != 0 ||
		    expect_true(
			    session_value.kind == SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER &&
				    session_value.text != NULL &&
				    strcmp(session_value.text, session_cases[case_index].expected_value) == 0,
			    "fragment spelling session graph value mismatch") != 0 ||
		    expect_deparse_equals_and_reparse(
			    handle,
			    session_cases[case_index].expected_sql,
			    "fragment spelling session deparse should be exact") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	handle = NULL;
	fragment = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		"IF [OldCase] = \"OldDouble\" BEGIN SELECT 1; END",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "fragment spelling control parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_set_clause_sql(
		handle,
		0U,
		0U,
		"[BracketCase] = \"DoubleCase\" AND MixedCase = 1",
		&error);
	if (expect_status_ok(rc, &error, "fragment spelling control condition mutation should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &fragment, &error);
	if (expect_status_ok(rc, &error, "fragment spelling control condition getter should succeed") != 0 ||
	    expect_true(
		    fragment != NULL &&
			    strcmp(fragment, "[BracketCase] = \"DoubleCase\" AND MixedCase = 1") == 0,
		    "fragment spelling control condition getter should preserve SQL") != 0 ||
	    expect_query_graph_clause_fields(
		    handle,
		    0U,
		    SQLPARSER_CLAUSE_KIND_CONDITION,
		    control_fields,
		    sizeof(control_fields) / sizeof(control_fields[0]),
		    "fragment spelling control graph fields should preserve case") != 0) {
		sqlparser_string_free(fragment);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment);
	fragment = NULL;
	if (expect_deparse_equals_and_reparse(
	    handle,
	    "IF [BracketCase] = \"DoubleCase\" AND MixedCase = 1 BEGIN SELECT 1; END",
	    "fragment spelling control deparse should be exact") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse_with_options(
		"INSERT dbo.t (a) OUTPUT INSERTED.id VALUES (1)",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT parse should succeed") != 0) {
		return 1;
	}
	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = "stmt[0].dml_result_target[0][0][0]";
	patches[0].sql = "INSERTED.[BracketCase] AS \"DoubleAlias\"";
	patches[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	patches[1].selector = "stmt[0].dml_result_targets[0][0]";
	patches[1].index = 1U;
	patches[1].sql = "INSERTED.MixedCase AS MixedAlias";
	patch_list.items = patches;
	patch_list.count = 2U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT target mutations should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	memset(&selector, 0, sizeof(selector));
	rc = sqlparser_selector_parse(
		"stmt[0].dml_result_target[0][0][0]",
		&selector,
		&error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT first selector should parse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_dml_result_target_sql(handle, &selector, &fragment, &error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT first target getter should succeed") != 0 ||
	    expect_true(
		    fragment != NULL &&
			    strcmp(fragment, "INSERTED.[BracketCase] AS \"DoubleAlias\"") == 0,
		    "fragment spelling OUTPUT first target should preserve SQL") != 0) {
		sqlparser_string_free(fragment);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment);
	fragment = NULL;
	memset(&selector, 0, sizeof(selector));
	rc = sqlparser_selector_parse(
		"stmt[0].dml_result_target[0][0][1]",
		&selector,
		&error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT second selector should parse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_dml_result_target_sql(handle, &selector, &fragment, &error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT second target getter should succeed") != 0 ||
	    expect_true(
		    fragment != NULL && strcmp(fragment, "INSERTED.MixedCase AS MixedAlias") == 0,
		    "fragment spelling OUTPUT second target should preserve SQL") != 0) {
		sqlparser_string_free(fragment);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment);
	fragment = NULL;
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "fragment spelling OUTPUT graph should succeed") != 0 ||
	    expect_true(graph.target_count == 2U && graph.field_count == 2U,
	                "fragment spelling OUTPUT graph shape mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, 0U, &target, &error), &error,
	                     "fragment spelling OUTPUT first graph target should succeed") != 0 ||
	    expect_true(
		    target.output_name != NULL && strcmp(target.output_name, "DoubleAlias") == 0 &&
			    target.has_field,
		    "fragment spelling OUTPUT first graph target mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error,
	                     "fragment spelling OUTPUT first graph field should succeed") != 0 ||
	    expect_true(
		    field.column_name != NULL && strcmp(field.column_name, "BracketCase") == 0,
		    "fragment spelling OUTPUT bracket field mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_target_at(&graph, 1U, &target, &error), &error,
	                     "fragment spelling OUTPUT second graph target should succeed") != 0 ||
	    expect_true(
		    target.output_name != NULL && strcmp(target.output_name, "MixedAlias") == 0 &&
			    target.has_field,
		    "fragment spelling OUTPUT second graph target mismatch") != 0 ||
	    expect_status_ok(sqlparser_query_graph_field_at(&graph, target.field_index, &field, &error), &error,
	                     "fragment spelling OUTPUT second graph field should succeed") != 0 ||
	    expect_true(
		    field.column_name != NULL && strcmp(field.column_name, "MixedCase") == 0,
		    "fragment spelling OUTPUT mixed-case field mismatch") != 0 ||
	    expect_deparse_equals_and_reparse(
		    handle,
		    "INSERT dbo.t (a) OUTPUT INSERTED.[BracketCase] AS \"DoubleAlias\", "
		    "INSERTED.MixedCase AS MixedAlias VALUES (1)",
		    "fragment spelling OUTPUT deparse should be exact") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	return 0;
}

static int test_structured_select_target_column_replacement(void)
{
	static const char *pg_id[] = {"id"};
	static const char *pg_name[] = {"name"};
	static const char *pg_phone[] = {"phone"};
	static const char *qualified_id[] = {"u", "id"};
	static const char *qualified_name[] = {"u", "name"};
	static const char *qualified_phone[] = {"u", "phone"};
	static const char *mysql_reserved[] = {"select"};
	static const char *mysql_cn[] = {"中文列"};
	static const char *mysql_space[] = {"has space"};
	static const char *mysql_case[] = {"CaseSensitive"};
	static const char *sqlserver_schema_id[] = {"dbo", "users", "id"};
	static const char *sqlserver_schema_phone[] = {"dbo", "users", "phone"};
	static const char *oracle_outer_id[] = {"d", "id"};
	static const char *oracle_outer_phone[] = {"d", "phone"};
	static const char *view_id[] = {"id"};
	static const char *view_phone[] = {"phone"};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *name;
		const char *sql;
		sqlparser_identifier_path_view_t columns[4];
		size_t column_count;
		size_t expected_target_count;
		const char *must_contain;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-star",
			"SELECT * FROM public.users",
			{{pg_id, 1U}, {pg_name, 1U}, {pg_phone, 1U}, {NULL, 0U}},
			3U,
			3U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-qualified-star",
			"SELECT u.* FROM public.users u",
			{{qualified_id, 2U}, {qualified_name, 2U}, {qualified_phone, 2U}, {NULL, 0U}},
			3U,
			3U,
			"u.phone"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-join-qualified-star",
			"SELECT u.*, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id",
			{{qualified_id, 2U}, {qualified_name, 2U}, {qualified_phone, 2U}, {NULL, 0U}},
			3U,
			4U,
			"o.order_no"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-derived-nested-star",
			"SELECT * FROM (SELECT * FROM (SELECT id, phone FROM users) x) y",
			{{pg_id, 1U}, {pg_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-union-wrapper-star",
			"SELECT * FROM (SELECT id, phone FROM users UNION ALL SELECT id, phone FROM archived_users) u",
			{{pg_id, 1U}, {pg_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"UNION ALL"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"postgresql-view-star",
			"SELECT * FROM user_view",
			{{view_id, 1U}, {view_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"mysql-identifier-boundaries",
			"SELECT * FROM `users`",
			{{mysql_reserved, 1U}, {mysql_cn, 1U}, {mysql_space, 1U}, {mysql_case, 1U}},
			4U,
			4U,
			"CaseSensitive"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"oracle-rownum-wrapper-star",
			"SELECT * FROM (SELECT ROWNUM rn, u.* FROM users u) d",
			{{oracle_outer_id, 2U}, {oracle_outer_phone, 2U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"d.phone"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"sqlserver-schema-qualified",
			"SELECT * FROM [dbo].[users] ORDER BY [id] OFFSET 0 ROWS",
			{{sqlserver_schema_id, 3U}, {sqlserver_schema_phone, 3U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"dameng-star",
			"SELECT * FROM users",
			{{pg_id, 1U}, {pg_phone, 1U}, {NULL, 0U}, {NULL, 0U}},
			2U,
			2U,
			"phone"
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	char *deparsed_sql;
	char *target_sql;
	size_t target_count;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		deparsed_sql = NULL;
		target_sql = NULL;
		memset(&error, 0, sizeof(error));
		memset(&selector, 0, sizeof(selector));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "structured select parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_selector_parse("stmt[0].select_target[0][0]", &selector, &error);
		if (expect_status_ok(rc, &error, "structured select target selector should parse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_selector_replace_select_target_with_columns(
			handle,
			&selector,
			cases[index].columns,
			cases[index].column_count,
			&error);
		if (expect_status_ok(rc, &error, "structured select replacement should succeed") != 0 ||
		    expect_deparse_reparse_ok(handle, "structured select replacement should reparse") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		rc = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &error);
		if (expect_status_ok(rc, &error, "structured select target count should succeed") != 0 ||
		    expect_true(target_count == cases[index].expected_target_count,
		                "structured select target count mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &error);
		if (expect_status_ok(rc, &error, "structured select first target SQL should succeed") != 0 ||
		    expect_true(target_sql != NULL && strcmp(target_sql, "*") != 0,
		                "structured select first target should not remain star") != 0) {
			sqlparser_string_free(target_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(target_sql);
		target_sql = NULL;

		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "structured select deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].must_contain) != NULL,
		                "structured select deparse should contain expected text") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_query_graph_join_using_reuses_fields(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_field_t field;
	char *deparsed;
	size_t index;
	int saw_using_field;
	int rc;

	sql = "SELECT * FROM table_a PARTITION(p0) USE INDEX (idx_a, idx_b) "
	      "JOIN table_b USING (id) FOR UPDATE NOWAIT";
	handle = NULL;
	deparsed = NULL;
	saw_using_field = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (expect_status_ok(rc, &error, "join graph parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "join graph should be available") != 0 ||
	    expect_true(graph.relation_count == 2U, "join should reuse the existing relation graph") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (index = 0U; index < graph.field_count; index++) {
		if (sqlparser_query_graph_field_at(&graph, index, &field, &error) == SQLPARSER_STATUS_OK &&
		    field.clause == SQLPARSER_CLAUSE_KIND_ON && field.column_name != NULL &&
		    strcmp(field.column_name, "id") == 0) {
			saw_using_field = field.candidate_relations.count == 2U;
		}
	}
	if (expect_true(saw_using_field, "JOIN USING should reuse fields with both relation candidates") != 0 ||
	    expect_status_ok(sqlparser_deparse(handle, &deparsed, &error), &error,
	                     "join extensions should deparse") != 0 ||
	    expect_true(strstr(deparsed, "PARTITION(p0)") != NULL, "partition clause should be preserved") != 0 ||
	    expect_true(strstr(deparsed, "USE INDEX (idx_a, idx_b)") != NULL, "index hint should be preserved") != 0 ||
	    expect_true(strstr(deparsed, "USING (id)") != NULL, "USING clause should be preserved") != 0 ||
	    expect_true(strstr(deparsed, "FOR UPDATE NOWAIT") != NULL, "locking clause should be preserved") != 0) {
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generated_keyword_insert_column(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *deparsed;
	int rc;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse(
		"INSERT INTO T (OldCol) VALUES (1)",
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "generated keyword insert column parse should succeed") !=
	    0) {
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].insert_columns";
	patch.index = 1U;
	patch.name = "INSERT";
	patch.default_sql = "2";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "generated keyword insert column patch should succeed") !=
		    0 ||
	    expect_deparse_reparse_ok(
		    handle,
		    "parsed keyword insert column should reparse") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed, &error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "parsed keyword insert column deparse should succeed") !=
		    0 ||
	    expect_true(
		    deparsed != NULL &&
			    strstr(deparsed, "(OldCol, INSERT)") != NULL,
		    "parsed keyword insert column should retain its unquoted spelling") != 0) {
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_set_operation_attribution(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_relation_t relation;
	sqlparser_graph_set_t set_item;
	sqlparser_graph_field_t field;
	char *view_json;
	size_t index;
	int saw_users;
	int saw_archived_users;
	int saw_order_field;
	int rc;

	sql = "SELECT u.id FROM users u UNION ALL SELECT a.id FROM archived_users a ORDER BY id";
	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_status_ok(rc, &error, "set operation parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "set operation graph should be available") != 0 ||
	    expect_true(graph.relation_count == 2U, "set operation should expose both table relations") != 0 ||
	    expect_true(graph.set_count == 1U, "set operation should expose one set node") != 0 ||
	    expect_status_ok(sqlparser_query_graph_set_at(&graph, 0U, &set_item, &error), &error, "set node should be available") != 0 ||
	    expect_true(set_item.kind == SQLPARSER_GRAPH_SET_UNION_ALL, "set kind should be UNION ALL") != 0 ||
	    expect_true(set_item.branch_blocks.count == 2U, "set operation should expose two branch blocks") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	saw_users = 0;
	saw_archived_users = 0;
	for (index = 0U; index < graph.relation_count; index++) {
		rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
		if (expect_status_ok(rc, &error, "set relation should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (relation.object_name != NULL && strcmp(relation.object_name, "users") == 0) {
			saw_users = 1;
		}
		if (relation.object_name != NULL && strcmp(relation.object_name, "archived_users") == 0) {
			saw_archived_users = 1;
		}
	}
	saw_order_field = 0;
	for (index = 0U; index < graph.field_count; index++) {
		rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
		if (expect_status_ok(rc, &error, "set field should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (field.clause == SQLPARSER_CLAUSE_KIND_ORDER_BY &&
		    field.column_name != NULL &&
		    strcmp(field.column_name, "id") == 0) {
			saw_order_field = 1;
		}
	}
	if (expect_true(saw_users != 0, "set operation graph should include users") != 0 ||
	    expect_true(saw_archived_users != 0, "set operation graph should include archived_users") != 0 ||
	    expect_true(saw_order_field != 0, "set operation graph should expose top-level order_by field") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "set operation view export should succeed") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"users\"") != NULL, "set operation view should include users") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"archived_users\"") != NULL, "set operation view should include archived_users") != 0 ||
	    expect_true(strstr(view_json, "\"sets\"") != NULL, "set operation view should expose sets") != 0 ||
	    expect_true(strstr(view_json, "\"clause\":\"order_by\"") != NULL, "set operation view should expose top-level order_by") != 0 ||
	    expect_true(strstr(view_json, "\"column\":\"id\"") != NULL, "set operation view should include id fields") != 0 ||
	    expect_true(strstr(view_json, "\"objects\"") == NULL, "set operation view should not expose old objects") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_strict_contract_edges(void)
{
	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_relation_t relation;
		sqlparser_graph_block_t block;
		size_t index;
		int saw_cte_relation;
		int saw_cte_block;
		int saw_base_users;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"WITH active_users AS (SELECT id FROM public.users WHERE status = $1) "
			"SELECT au.id FROM active_users au",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "CTE query graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "CTE query graph should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		saw_cte_relation = 0;
		saw_cte_block = 0;
		saw_base_users = 0;
		for (index = 0U; index < graph.relation_count; index++) {
			rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
			if (expect_status_ok(rc, &error, "CTE relation should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (relation.kind == SQLPARSER_GRAPH_REL_CTE &&
			    relation.has_source_block != 0 &&
			    relation.object_name != NULL &&
			    strcmp(relation.object_name, "active_users") == 0) {
				saw_cte_relation = 1;
				rc = sqlparser_query_graph_block_at(&graph, relation.source_block_index, &block, &error);
				if (expect_status_ok(rc, &error, "CTE source block should be readable") != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
				if (block.kind == SQLPARSER_GRAPH_BLOCK_CTE) {
					saw_cte_block = 1;
				}
			}
			if (relation.kind == SQLPARSER_GRAPH_REL_BASE &&
			    relation.object_name != NULL &&
			    strcmp(relation.object_name, "users") == 0) {
				saw_base_users = 1;
			}
		}
		if (expect_true(saw_cte_relation != 0, "query graph should expose CTE relation") != 0 ||
		    expect_true(saw_cte_block != 0, "query graph should expose CTE source block") != 0 ||
		    expect_true(saw_base_users != 0, "query graph should expose CTE inner base table") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_relation_t relation;
		size_t index;
		size_t used_source_block;
		size_t used_reference_count;
		size_t users_count;
		size_t audit_count;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"WITH used AS (SELECT id FROM users), unused AS (SELECT id FROM audit_log) "
			"SELECT x.id FROM used x JOIN used y ON x.id = y.id",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "shared CTE graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "shared CTE graph should be available") != 0 ||
		    expect_true(graph.block_count == 3U, "CTE definitions should each create one block") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		used_source_block = 0U;
		used_reference_count = 0U;
		users_count = 0U;
		audit_count = 0U;
		for (index = 0U; index < graph.relation_count; index++) {
			rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
			if (expect_status_ok(rc, &error, "shared CTE relation should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (relation.kind == SQLPARSER_GRAPH_REL_CTE && relation.object_name != NULL &&
			    strcmp(relation.object_name, "used") == 0 && relation.has_source_block) {
				if (used_reference_count == 0U) {
					used_source_block = relation.source_block_index;
				} else if (expect_true(relation.source_block_index == used_source_block,
				                       "repeated CTE references should share one source block") != 0) {
					sqlparser_handle_destroy(handle);
					return 1;
				}
				used_reference_count++;
			} else if (relation.kind == SQLPARSER_GRAPH_REL_BASE && relation.object_name != NULL &&
			           strcmp(relation.object_name, "users") == 0) {
				users_count++;
			} else if (relation.kind == SQLPARSER_GRAPH_REL_BASE && relation.object_name != NULL &&
			           strcmp(relation.object_name, "audit_log") == 0) {
				audit_count++;
			}
		}
		if (expect_true(used_reference_count == 2U, "both CTE references should be present") != 0 ||
		    expect_true(users_count == 1U, "shared CTE source should not be duplicated") != 0 ||
		    expect_true(audit_count == 1U, "unreferenced CTE definition should remain in the graph") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_field_t field;
		size_t index;
		int saw_ambiguous_id;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"SELECT id FROM users u JOIN orders o ON u.id = o.user_id WHERE id = 1",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "ambiguous field query graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "ambiguous field graph should be available") != 0 ||
		    expect_true(graph.relation_count == 2U, "ambiguous field graph should expose two relations") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		saw_ambiguous_id = 0;
		for (index = 0U; index < graph.field_count; index++) {
			rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
			if (expect_status_ok(rc, &error, "ambiguous field should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (field.column_name != NULL &&
			    strcmp(field.column_name, "id") == 0 &&
			    field.has_relation == 0 &&
			    field.candidate_relations.count == 2U) {
				saw_ambiguous_id = 1;
			}
		}
		if (expect_true(saw_ambiguous_id != 0, "unqualified ambiguous field should expose candidate relations") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		sqlparser_handle_t *handle;
		sqlparser_error_t error;
		sqlparser_query_graph_view_t graph;
		sqlparser_graph_relation_t relation;
		sqlparser_graph_field_t field;
		size_t users_relation_index;
		size_t users_block_index;
		size_t index;
		int saw_outer_reference;
		int rc;

		handle = NULL;
		memset(&error, 0, sizeof(error));
		rc = sqlparser_parse(
			"SELECT (SELECT dm.name FROM dict dm WHERE dm.id = u.dict_id) AS dict_name "
			"FROM users u WHERE u.id = $1",
			&handle,
			&error);
		if (expect_status_ok(rc, &error, "correlated subquery graph parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
		if (expect_status_ok(rc, &error, "correlated subquery graph should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		users_relation_index = (size_t)-1;
		users_block_index = 0U;
		for (index = 0U; index < graph.relation_count; index++) {
			rc = sqlparser_query_graph_relation_at(&graph, index, &relation, &error);
			if (expect_status_ok(rc, &error, "correlated relation should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (relation.object_name != NULL && strcmp(relation.object_name, "users") == 0) {
				users_relation_index = index;
				users_block_index = relation.block_index;
			}
		}
		if (expect_true(users_relation_index != (size_t)-1, "outer users relation should be present") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		saw_outer_reference = 0;
		for (index = 0U; index < graph.field_count; index++) {
			rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
			if (expect_status_ok(rc, &error, "correlated field should be readable") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			if (field.has_relation != 0 &&
			    field.relation_index == users_relation_index &&
			    field.block_index != users_block_index &&
			    field.column_name != NULL &&
			    strcmp(field.column_name, "dict_id") == 0) {
				saw_outer_reference = 1;
			}
		}
		if (expect_true(saw_outer_reference != 0, "correlated subquery should point to outer relation") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	{
		struct pagination_case {
			sqlparser_dialect_t dialect;
			const char *sql;
			size_t value_count;
			size_t bind_position;
		};
		static const struct pagination_case cases[] = {
			{
				SQLPARSER_DIALECT_POSTGRESQL,
				"SELECT id FROM users WHERE name LIKE $1 ORDER BY id LIMIT $2 OFFSET $3",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_MYSQL,
				"SELECT id FROM users WHERE name LIKE ? ORDER BY id LIMIT ? OFFSET ?",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_ORACLE,
				"SELECT id FROM users WHERE name LIKE :pattern ORDER BY id FETCH FIRST :limit ROWS ONLY",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_SQLSERVER,
				"SELECT [id] FROM [dbo].[users] WHERE [name] LIKE @pattern ORDER BY [id] OFFSET @offset ROWS FETCH NEXT @limit ROWS ONLY",
				1U,
				1U
			},
			{
				SQLPARSER_DIALECT_DAMENG,
				"SELECT id FROM users WHERE name LIKE ? ORDER BY id LIMIT ? OFFSET ?",
				1U,
				1U
			}
		};
		sqlparser_parse_options_t options;
		size_t case_index;

		for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
			sqlparser_handle_t *handle;
			sqlparser_error_t error;
			sqlparser_query_graph_view_t graph;
			sqlparser_graph_value_t value;
			int rc;

			handle = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = cases[case_index].dialect;
			rc = sqlparser_parse_with_options(cases[case_index].sql, &options, &handle, &error);
			if (expect_status_ok(rc, &error, "pagination bind parse should succeed") != 0) {
				return 1;
			}
			rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
			if (expect_status_ok(rc, &error, "pagination graph should be available") != 0 ||
			    expect_true(graph.value_count == cases[case_index].value_count, "pagination binds should not enter values") != 0 ||
			    expect_status_ok(sqlparser_query_graph_value_at(&graph, 0U, &value, &error), &error, "pagination WHERE value should be readable") != 0 ||
			    expect_true(value.has_bind_position != 0 && value.bind_position == cases[case_index].bind_position,
			                "pagination WHERE bind position mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
			sqlparser_handle_destroy(handle);
		}
	}

	return 0;
}

static int test_session_context_patch_api(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *selector;
		const char *replacement_sql;
		const char *deparse_contains;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET search_path TO app_schema",
			"stmt[0].value[0]",
			"next_schema",
			"SET search_path TO next_schema"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"USE analytics",
			"stmt[0].value[0]",
			"warehouse",
			"USE warehouse"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"USE [AdventureWorks2022]",
			"stmt[0].value[0]",
			"[ReportingDB]",
			"USE [ReportingDB]"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"stmt[0].value[0]",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = APP"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CONTAINER=PDB1",
			"stmt[0].value[0]",
			"PDB2",
			"ALTER SESSION SET CONTAINER = PDB2"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SET SCHEMA KDES",
			"stmt[0].value[0]",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = APP"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"stmt[0].value[0]",
			"APP",
			"ALTER SESSION SET CURRENT_SCHEMA = APP"
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
			sqlparser_parse_options_t options;
			sqlparser_error_t error;
			sqlparser_handle_t *handle;
			sqlparser_patch_t patch;
			sqlparser_patch_list_t patch_list;
			char *deparsed_sql;
			int rc;

			handle = NULL;
			deparsed_sql = NULL;
			memset(&error, 0, sizeof(error));
			sqlparser_parse_options_default(&options);
			options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "session context parse should succeed") != 0) {
				return 1;
			}

			memset(&patch, 0, sizeof(patch));
			patch.op = SQLPARSER_PATCH_REPLACE;
			patch.selector = cases[index].selector;
			patch.sql = cases[index].replacement_sql;
			patch_list.items = &patch;
			patch_list.count = 1U;
			rc = sqlparser_apply_patch(handle, &patch_list, &error);
			if (expect_status_ok(rc, &error, "session context patch should succeed") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed_sql, &error);
		if (expect_status_ok(rc, &error, "session context deparse should succeed") != 0 ||
		    expect_true(strstr(deparsed_sql, cases[index].deparse_contains) != NULL, "session context deparse mismatch") != 0) {
			sqlparser_string_free(deparsed_sql);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_session_context_quoted_identifier_literal_api(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *expected_value;
		int expected_quoted_identifier;
	} cases[] = {
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"KDES",
			0
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=\"KdesMixed\"",
			"KdesMixed",
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD'",
			"YYYY-MM-DD",
			0
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			"KDES",
			0
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET CURRENT_SCHEMA=\"KdesMixed\"",
			"KdesMixed",
			1
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_parse_options_t options;
		sqlparser_error_t error;
		sqlparser_handle_t *handle;
		sqlparser_literal_view_t literal;
		size_t literal_count;
		int rc;

		handle = NULL;
		literal_count = 0U;
		memset(&error, 0, sizeof(error));
		memset(&literal, 0, sizeof(literal));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "session context quoted identifier parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_statement_literal_count(handle, 0U, &literal_count, &error);
		if (expect_status_ok(rc, &error, "session context literal count should succeed") != 0 ||
		    expect_true(literal_count == 1U, "session context should expose one literal") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_statement_literal(handle, 0U, 0U, &literal, &error);
		if (expect_status_ok(rc, &error, "session context literal read should succeed") != 0 ||
		    expect_true(literal.kind == SQLPARSER_LITERAL_KIND_STRING, "session context literal should be string") != 0 ||
		    expect_true(literal.string_value != NULL, "session context string value should be present") != 0 ||
		    expect_true(literal.quoted_identifier == cases[index].expected_quoted_identifier,
		                "session context quoted identifier flag mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].expected_value != NULL &&
		    expect_true(strcmp(literal.string_value, cases[index].expected_value) == 0,
		                "session context literal value mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}

	return 0;
}

static int test_oracle_container_service_patch_api(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *deparsed_sql;
	int rc;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	rc = sqlparser_parse_with_options(
		"ALTER SESSION SET CONTAINER=PDB1 SERVICE=APP_SVC",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "oracle container service parse should succeed") != 0) {
		return 1;
	}

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].value[1]";
	patch.sql = "REPORT_SVC";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "oracle service patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status_ok(rc, &error, "oracle service deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "ALTER SESSION SET CONTAINER = PDB1 SERVICE = REPORT_SVC") != NULL,
	                "oracle service deparse mismatch") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_control_flow_core(void)
{
	sqlparser_handle_t *ordinary;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *clone;
	sqlparser_control_flow_view_t flow;
	sqlparser_control_node_t node;
	sqlparser_control_branch_t branch;
	sqlparser_control_item_t item;
	sqlparser_statement_kind_t statement_kind;
	sqlparser_clause_view_t clause;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_block_t block;
	sqlparser_error_t error;
	const char *node_name;
	char *clause_sql;
	char *view_json;
	char *clone_sql;
	json_error_t json_error;
	json_t *root;
	json_t *control_json;
	json_t *statements_json;
	json_t *statement_json;
	size_t index;
	size_t count;
	int rc;

	ordinary = NULL;
	handle = NULL;
	clone = NULL;
	clause_sql = NULL;
	view_json = NULL;
	clone_sql = NULL;
	root = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse("SELECT id FROM t", &ordinary, &error);
	if (expect_status_ok(rc, &error, "ordinary control-flow baseline should parse") != 0) {
		return 1;
	}
	memset(&flow, 0, sizeof(flow));
	rc = sqlparser_handle_control_flow(ordinary, &flow, &error);
	if (expect_status_ok(rc, &error, "ordinary handle control view should succeed") != 0 ||
	    expect_true(flow.node_count == 0U && flow.branch_count == 0U && flow.item_count == 0U && flow.roots.count == 0U,
		    "ordinary handle control view should be empty") != 0) {
		sqlparser_handle_destroy(ordinary);
		return 1;
	}
	memset(&node, 0, sizeof(node));
	rc = sqlparser_control_node_at(&flow, 0U, &node, &error);
	if (expect_true(rc == SQLPARSER_STATUS_INVALID_ARGUMENT, "empty control view should reject node access") != 0) {
		sqlparser_handle_destroy(ordinary);
		return 1;
	}
	sqlparser_handle_destroy(ordinary);

	if (test_control_handle_new(&handle) != 0) {
		return 1;
	}
	if (expect_true(sqlparser_statement_count(handle) == 3U, "control handle should expose three addressable units") != 0) {
		goto fail;
	}
	rc = sqlparser_statement_kind(handle, 0U, &statement_kind, &error);
	if (expect_status_ok(rc, &error, "condition statement kind should succeed") != 0 ||
	    expect_true(statement_kind == SQLPARSER_STATEMENT_KIND_CONDITION, "first control unit should be a condition") != 0) {
		goto fail;
	}
	rc = sqlparser_statement_node_name(handle, 0U, &node_name, &error);
	if (expect_status_ok(rc, &error, "condition node name should succeed") != 0 ||
	    expect_true(node_name != NULL && strcmp(node_name, "ConditionExpr") == 0,
		    "condition node name should hide its parser wrapper") != 0) {
		goto fail;
	}
	rc = sqlparser_statement_clause_count(handle, 0U, &count, &error);
	if (expect_status_ok(rc, &error, "condition clause count should succeed") != 0 ||
	    expect_true(count == 1U, "condition unit should expose one clause") != 0) {
		goto fail;
	}
	memset(&clause, 0, sizeof(clause));
	rc = sqlparser_statement_clause(handle, 0U, 0U, &clause, &error);
	if (expect_status_ok(rc, &error, "condition clause should succeed") != 0 ||
	    expect_true(clause.kind == SQLPARSER_CLAUSE_KIND_CONDITION, "condition clause kind should be explicit") != 0) {
		goto fail;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &clause_sql, &error);
	if (expect_status_ok(rc, &error, "condition clause SQL should succeed") != 0 ||
	    expect_true(clause_sql != NULL && strstr(clause_sql, "a = 1") != NULL,
		    "condition clause SQL should contain only the expression") != 0) {
		goto fail;
	}
	sqlparser_string_free(clause_sql);
	clause_sql = NULL;

	memset(&flow, 0, sizeof(flow));
	rc = sqlparser_handle_control_flow(handle, &flow, &error);
	if (expect_status_ok(rc, &error, "control view should succeed") != 0 ||
	    expect_true(flow.roots.count == 1U && flow.node_count == 1U &&
		    flow.branch_count == 2U && flow.item_count == 3U,
		    "control view counts should match the tree") != 0) {
		goto fail;
	}
	rc = sqlparser_control_span_index_at(&flow, flow.roots, 0U, &index, &error);
	if (expect_status_ok(rc, &error, "control root span should resolve") != 0 ||
	    expect_true(index == 0U, "control root should reference the first item") != 0) {
		goto fail;
	}
	memset(&item, 0, sizeof(item));
	rc = sqlparser_control_item_at(&flow, index, &item, &error);
	if (expect_status_ok(rc, &error, "control root item should resolve") != 0 ||
	    expect_true(item.kind == SQLPARSER_CONTROL_ITEM_NODE && item.index == 0U,
		    "control root item should reference the IF node") != 0) {
		goto fail;
	}
	memset(&node, 0, sizeof(node));
	rc = sqlparser_control_node_at(&flow, 0U, &node, &error);
	if (expect_status_ok(rc, &error, "control node should resolve") != 0 ||
	    expect_true(node.kind == SQLPARSER_CONTROL_NODE_IF && node.branches.count == 2U,
		    "control node should expose ordered IF branches") != 0) {
		goto fail;
	}
	memset(&branch, 0, sizeof(branch));
	rc = sqlparser_control_branch_at(&flow, 0U, &branch, &error);
	if (expect_status_ok(rc, &error, "then branch should resolve") != 0 ||
	    expect_true(branch.has_condition && branch.condition_statement_index == 0U && branch.items.count == 1U,
		    "then branch should reference the condition unit") != 0) {
		goto fail;
	}
	rc = sqlparser_control_branch_at(&flow, 1U, &branch, &error);
	if (expect_status_ok(rc, &error, "else branch should resolve") != 0 ||
	    expect_true(!branch.has_condition && branch.items.count == 1U,
		    "else branch should be unconditional") != 0) {
		goto fail;
	}

	memset(&graph, 0, sizeof(graph));
	rc = sqlparser_statement_query_graph(handle, 0U, &graph, &error);
	if (expect_status_ok(rc, &error, "condition query graph should succeed") != 0 ||
	    expect_true(graph.has_root_block && graph.block_count == 1U && graph.target_count == 0U,
		    "condition graph should contain one expression block and no fake target") != 0) {
		goto fail;
	}
	memset(&block, 0, sizeof(block));
	rc = sqlparser_query_graph_block_at(&graph, graph.root_block_index, &block, &error);
	if (expect_status_ok(rc, &error, "condition root block should resolve") != 0 ||
	    expect_true(block.kind == SQLPARSER_GRAPH_BLOCK_CONDITION, "condition graph block kind should be explicit") != 0) {
		goto fail;
	}
	for (index = 0U; index < graph.field_count; index++) {
		sqlparser_graph_field_t field;

		memset(&field, 0, sizeof(field));
		rc = sqlparser_query_graph_field_at(&graph, index, &field, &error);
		if (expect_status_ok(rc, &error, "condition graph field should resolve") != 0 ||
		    expect_true(field.clause == SQLPARSER_CLAUSE_KIND_CONDITION,
			    "condition graph field should retain condition clause ownership") != 0) {
			goto fail;
		}
	}
	for (index = 0U; index < graph.value_count; index++) {
		sqlparser_graph_value_t value;

		memset(&value, 0, sizeof(value));
		rc = sqlparser_query_graph_value_at(&graph, index, &value, &error);
		if (expect_status_ok(rc, &error, "condition graph value should resolve") != 0 ||
		    expect_true(value.clause == SQLPARSER_CLAUSE_KIND_CONDITION,
			    "condition graph value should retain condition clause ownership") != 0) {
			goto fail;
		}
	}

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_status_ok(rc, &error, "control View JSON should succeed") != 0) {
		goto fail;
	}
	memset(&json_error, 0, sizeof(json_error));
	root = json_loads(view_json, 0, &json_error);
	control_json = root != NULL ? json_object_get(root, "control_flow") : NULL;
	statements_json = root != NULL ? json_object_get(root, "statements") : NULL;
	statement_json = json_is_array(statements_json) ? json_array_get(statements_json, 0U) : NULL;
	if (expect_true(
		    json_is_object(control_json) &&
		    json_array_size(json_object_get(control_json, "roots")) == 1U &&
		    json_array_size(json_object_get(control_json, "nodes")) == 1U &&
		    json_array_size(json_object_get(control_json, "branches")) == 2U,
		    "View JSON should mirror the public control topology") != 0 ||
	    expect_true(
		    statement_json != NULL &&
		    json_is_string(json_object_get(statement_json, "keyword")) &&
		    strcmp(json_string_value(json_object_get(statement_json, "keyword")), "condition") == 0,
		    "View JSON should identify the condition unit") != 0) {
		goto fail;
	}
	json_decref(root);
	root = NULL;
	sqlparser_string_free(view_json);
	view_json = NULL;

	rc = sqlparser_handle_clone(handle, &clone, &error);
	if (expect_status_ok(rc, &error, "control handle clone should succeed") != 0) {
		goto fail;
	}
	rc = sqlparser_deparse(clone, &clone_sql, &error);
	if (expect_status_ok(rc, &error, "cloned control handle should deparse") != 0 ||
	    expect_true(clone_sql != NULL && strcmp(clone_sql, test_control_sql()) == 0,
		    "unmodified control deparse should preserve the source text") != 0) {
		goto fail;
	}

	sqlparser_string_free(clone_sql);
	sqlparser_handle_destroy(clone);
	sqlparser_handle_destroy(handle);
	return 0;

fail:
	json_decref(root);
	sqlparser_string_free(clause_sql);
	sqlparser_string_free(view_json);
	sqlparser_string_free(clone_sql);
	sqlparser_handle_destroy(clone);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_control_flow_patch_and_limits(void)
{
	sqlparser_handle_t *handle;
	sqlparser_handle_t *invalid_handle;
	sqlparser_control_flow_view_t stale_flow;
	sqlparser_control_node_t node;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	sqlparser_control_counts_t counts;
	sqlparser_control_state_t *state;
	sqlparser_limits_t limits;
	sqlparser_error_t error;
	char *condition_sql;
	char *deparsed;
	unsigned long generation;
	int rc;

	handle = NULL;
	invalid_handle = NULL;
	state = NULL;
	condition_sql = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (test_control_handle_new(&handle) != 0) {
		return 1;
	}
	memset(&stale_flow, 0, sizeof(stale_flow));
	rc = sqlparser_handle_control_flow(handle, &stale_flow, &error);
	if (expect_status_ok(rc, &error, "pre-patch control view should succeed") != 0) {
		goto fail;
	}
	generation = handle->generation;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[99].clause[0]";
	patch.sql = "a = 10";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_true(rc == SQLPARSER_STATUS_INVALID_ARGUMENT,
		    "failed control patch should report the invalid selector") != 0 ||
	    expect_true(handle->generation == generation,
		    "failed control patch should not change the handle") != 0) {
		goto fail;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &condition_sql, &error);
	if (expect_status_ok(rc, &error, "condition should remain readable after rollback") != 0 ||
	    expect_true(condition_sql != NULL && strstr(condition_sql, "a = 1") != NULL,
		    "failed control patch should preserve the original condition") != 0) {
		goto fail;
	}
	sqlparser_string_free(condition_sql);
	condition_sql = NULL;

	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].clause[0]";
	patch.sql = "a = 2 AND b = $1";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "control condition patch should succeed") != 0 ||
	    expect_true(handle->generation == generation + 1UL,
		    "successful control patch should advance generation once") != 0) {
		goto fail;
	}
	memset(&node, 0, sizeof(node));
	rc = sqlparser_control_node_at(&stale_flow, 0U, &node, &error);
	if (expect_true(rc == SQLPARSER_STATUS_INVALID_ARGUMENT,
		    "a control view should become stale after patch") != 0) {
		goto fail;
	}
	rc = sqlparser_statement_clause_sql(handle, 0U, 0U, &condition_sql, &error);
	if (expect_status_ok(rc, &error, "patched condition should be readable") != 0 ||
	    expect_true(condition_sql != NULL && strstr(condition_sql, "a = 2") != NULL &&
		    strstr(condition_sql, "b = $1") != NULL,
		    "patched condition should expose both predicates") != 0) {
		goto fail;
	}
	rc = sqlparser_deparse(handle, &deparsed, &error);
	if (expect_status_ok(rc, &error, "patched control handle should deparse") != 0 ||
	    expect_true(deparsed != NULL && strstr(deparsed, "a = 2") != NULL &&
		    strstr(deparsed, "SELECT id FROM t") != NULL &&
		    strstr(deparsed, "UPDATE t SET x = 2") != NULL,
		    "control deparse should preserve its skeleton and rewrite only the condition") != 0 ||
	    expect_deparse_reparse_ok(handle, "patched generic control SQL should reparse") != 0) {
		goto fail;
	}
	sqlparser_string_free(condition_sql);
	condition_sql = NULL;
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	sqlparser_handle_destroy(handle);
	handle = NULL;

	memset(&counts, 0, sizeof(counts));
	counts.root_count = 1U;
	counts.node_count = 1U;
	counts.branch_count = 2U;
	counts.item_count = 3U;
	counts.index_count = 5U;
	counts.unit_count = 3U;
	sqlparser_limits_default(&limits);
	limits.max_statement_count = 2U;
	rc = sqlparser_control_state_allocate(&counts, &limits, &state, &error);
	if (expect_true(rc == SQLPARSER_STATUS_RESOURCE_LIMIT && state == NULL,
		    "control units should honor the statement limit") != 0) {
		goto fail;
	}
	limits.max_statement_count = 3U;
	rc = sqlparser_control_state_allocate(&counts, &limits, &state, &error);
	if (expect_status_ok(rc, &error,
		    "control metadata should not reduce the configured statement limit") != 0) {
		goto fail;
	}
	sqlparser_control_state_release(state);
	state = NULL;

	rc = sqlparser_parse(test_control_sql(), &invalid_handle, &error);
	if (expect_status_ok(rc, &error, "invalid control topology baseline should parse") != 0) {
		goto fail;
	}
	rc = sqlparser_control_state_allocate(&counts, &invalid_handle->limits, &state, &error);
	if (expect_status_ok(rc, &error, "invalid control topology state should allocate") != 0) {
		goto fail;
	}
	test_control_state_fill(state, test_control_sql());
	state->index_pool[4] = 99U;
	rc = sqlparser_control_state_attach(invalid_handle, state, &error);
	if (expect_true(rc == SQLPARSER_STATUS_INTERNAL_ERROR && invalid_handle->control == NULL,
		    "invalid control topology should be rejected before ownership transfer") != 0) {
		goto fail;
	}
	sqlparser_control_state_release(state);
	state = NULL;
	sqlparser_handle_destroy(invalid_handle);
	return 0;

fail:
	sqlparser_string_free(condition_sql);
	sqlparser_string_free(deparsed);
	sqlparser_control_state_release(state);
	sqlparser_handle_destroy(invalid_handle);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int test_deparse_identifier_spelling(void)
{
	struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *expected[3];
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U.ID AS UserID, U.USER_NAME FROM Public.DBP_USERS U "
			"WHERE U.ID = $1 ORDER BY U.USER_NAME",
			{"U.ID AS UserID", "Public.DBP_USERS U", "U.USER_NAME"}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"INSERT INTO KDES.Users (UserID, UserName) VALUES (1, 'Alice')",
			{"KDES.Users", "(UserID, UserName)", NULL}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"UPDATE KDES.Users U SET UserName = 'Alice' WHERE U.UserID = 1",
			{"KDES.Users U", "UserName = 'Alice'", "U.UserID"}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"DELETE FROM KDES.Users U WHERE U.UserID = 1",
			{"KDES.Users U", "U.UserID", NULL}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE TABLE KDES.Users (UserID BIGINT, UserName TEXT, "
			"CONSTRAINT PK_Users PRIMARY KEY (UserID)); "
			"CREATE INDEX IDX_Users_UserName ON KDES.Users (UserName)",
			{"CONSTRAINT PK_Users PRIMARY KEY (UserID)", "INDEX IDX_Users_UserName", "(UserName)"}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT '\\' AS AliasName FROM Public.Users",
			{"AS AliasName", "Public.Users", NULL}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&'d\\0061t\\+000061' AS u FROM Public.Users",
			{"U&'d\\0061t\\+000061' AS u", "Public.Users", NULL}
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT /* AliasName */ 1 AS AliasName, "
			"$tag$AliasName$tag$ AS DollarAlias FROM Public.Users",
			{"1 AS AliasName", "$tag$AliasName$tag$ AS DollarAlias", "Public.Users"}
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT U.ID AS UserID FROM App.Users U WHERE U.UserName = ? ORDER BY U.ID LIMIT 1",
			{"U.ID AS UserID", "App.Users U", "U.UserName"}
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT a FROM T WHERE X = \"a\" AND Y = 'A\\'s' ORDER BY A",
			{"FROM T", "ORDER BY A", NULL}
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"MERGE INTO KDES.DBP_SQLM_USERS U "
			"USING (SELECT ? ID, ? PHONE FROM DUAL) S "
			"ON (U.ID = S.ID) WHEN MATCHED THEN UPDATE SET U.PHONE = S.PHONE",
			{"KDES.DBP_SQLM_USERS U", "? ID, ? PHONE", "U.PHONE = S.PHONE"}
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT a FROM T WHERE A = :a AND X = q'[A's]' ORDER BY A",
			{"FROM T", "A = :a", "ORDER BY A"}
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT U.ID AS UserID FROM App.Users U WHERE U.UserName = @UserName",
			{"U.ID AS UserID", "App.Users U", "U.UserName = @UserName"}
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT a FROM T WHERE A = @a ORDER BY A",
			{"FROM T", "A = @a", "ORDER BY A"}
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT U.ID AS UserID FROM KDES.Users U WHERE U.UserName = :UserName",
			{"U.ID AS UserID", "KDES.Users U", "U.UserName = :UserName"}
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"SELECT a FROM T WHERE A = :a AND X = q'[A's]' ORDER BY A",
			{"FROM T", "A = :a", "ORDER BY A"}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT U.ID AS UserID FROM App.Users U WHERE U.UserName = ?",
			{"U.ID AS UserID", "App.Users U", "U.UserName = ?"}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"SELECT a FROM T WHERE X = \"a\" AND Y = 'A\\'s' ORDER BY A",
			{"FROM T", "ORDER BY A", NULL}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"SELECT U.ID AS UserID FROM KDES.Users U WHERE U.UserName = :UserName",
			{"U.ID AS UserID", "KDES.Users U", "U.UserName = :UserName"}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"SELECT a FROM T WHERE A = :a AND X = q'[A's]' ORDER BY A",
			{"FROM T", "A = :a", "ORDER BY A"}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"SELECT U.ID AS UserID FROM Public.Users U WHERE U.UserName = $1",
			{"U.ID AS UserID", "Public.Users U", "U.UserName = $1"}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"SELECT U.ID AS UserID FROM App.Users U WHERE U.UserName = @UserName",
			{"U.ID AS UserID", "App.Users U", "U.UserName = @UserName"}
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"SELECT a FROM T WHERE A = @a ORDER BY A",
			{"FROM T", "A = @a", "ORDER BY A"}
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patch_list;
	char *deparsed;
	size_t case_index;
	size_t expected_index;
	int rc;

	for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
		handle = NULL;
		deparsed = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		rc = sqlparser_parse_with_options(cases[case_index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "identifier spelling parse should succeed") != 0) {
			return 1;
		}
		rc = sqlparser_deparse(handle, &deparsed, &error);
		if (expect_status_ok(rc, &error, "identifier spelling deparse should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (expect_true(
			    strcmp(deparsed, cases[case_index].sql) == 0,
			    "unmodified deparse should preserve original SQL") != 0) {
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		for (expected_index = 0U; expected_index < 3U; expected_index++) {
			if (cases[case_index].expected[expected_index] != NULL &&
			    expect_true(
				    strstr(deparsed, cases[case_index].expected[expected_index]) != NULL,
				    "deparse should preserve identifier spelling") != 0) {
				sqlparser_string_free(deparsed);
				sqlparser_handle_destroy(handle);
				return 1;
			}
		}
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(
		"UPDATE KDES.DBP_SQLM_USERS U SET U.PHONE = :Phone WHERE U.ID = :ID",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "identifier spelling patch parse should succeed") != 0) {
		return 1;
	}
	rc = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"U.STATUS = :Status",
		&error);
	if (expect_status_ok(rc, &error, "identifier spelling patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed, &error);
	if (expect_status_ok(rc, &error, "identifier spelling patch deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed, "KDES.DBP_SQLM_USERS U") != NULL,
	                "patched deparse should preserve relation spelling") != 0 ||
	    expect_true(strstr(deparsed, "U.PHONE") != NULL,
	                "patched deparse should preserve assignment column spelling") != 0 ||
	    expect_true(strstr(deparsed, "U.ID") != NULL,
	                "patched deparse should preserve existing predicate spelling") != 0) {
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	rc = sqlparser_parse_with_options(
		"SELECT U.ID FROM KDES.USERS U WHERE U.STATUS = :STATUS",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "identifier spelling collision parse should succeed") != 0) {
		return 1;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
	patch.selector = "stmt[0].select_targets[0]";
	patch.index = 1U;
	patch.sql = "u.status AS status";
	patch_list.items = &patch;
	patch_list.count = 1U;
	rc = sqlparser_apply_patch(handle, &patch_list, &error);
	if (expect_status_ok(rc, &error, "identifier spelling collision patch should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	rc = sqlparser_deparse(handle, &deparsed, &error);
	if (expect_status_ok(rc, &error, "identifier spelling collision deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed, "U.ID, u.status AS status") != NULL,
	                "generated target should keep patch spelling") != 0 ||
	    expect_true(strstr(deparsed, "KDES.USERS U") != NULL,
	                "collision deparse should preserve relation spelling") != 0 ||
	    expect_true(strstr(deparsed, "U.STATUS = :STATUS") != NULL,
	                "collision deparse should preserve existing predicate spelling") != 0) {
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_flat_and_span_growth(void)
{
	static const char prefix[] = "SELECT * FROM t WHERE ";
	const size_t condition_count = 8192U;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_block_t block;
	sqlparser_graph_predicate_t root_predicate;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *sql;
	size_t block_root_index;
	size_t index;
	size_t length;
	size_t sql_capacity;
	int rc;
	int result;

	sql_capacity =
		sizeof(prefix) + condition_count * 40U;
	sql = (char *)malloc(sql_capacity);
	if (sql == NULL) {
		fprintf(stderr, "FAIL: flat AND graph SQL allocation failed\n");
		return 1;
	}
	memcpy(sql, prefix, sizeof(prefix) - 1U);
	length = sizeof(prefix) - 1U;
	for (index = 0U; index < condition_count; index++) {
		int written;

		written = snprintf(
			sql + length,
			sql_capacity - length,
			"%sc%lu = %lu",
			index == 0U ? "" : " AND ",
			(unsigned long)(index + 1U),
			(unsigned long)(index + 1U));
		if (written < 0 ||
		    (size_t)written >= sql_capacity - length) {
			fprintf(stderr, "FAIL: flat AND graph SQL generation failed\n");
			free(sql);
			return 1;
		}
		length += (size_t)written;
	}

	handle = NULL;
	result = 1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
	rc = sqlparser_parse_with_options(
		sql,
		&options,
		&handle,
		&error);
	if (expect_status_ok(
		    rc,
		    &error,
		    "flat AND graph SQL should parse") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error),
		    &error,
		    "flat AND query graph should build") != 0 ||
	    expect_true(
		    graph.has_root_block != 0 &&
			    graph.field_count == condition_count &&
			    graph.value_count == condition_count &&
			    graph.predicate_count == condition_count + 1U,
		    "flat AND query graph counts mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_block_at(
			    &graph,
			    graph.root_block_index,
			    &block,
			    &error),
		    &error,
		    "flat AND root block should be available") != 0 ||
	    expect_true(
		    block.predicates.count == condition_count + 1U,
		    "flat AND root block predicate count mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_predicate_at(
			    &graph,
			    0U,
			    &root_predicate,
			    &error),
		    &error,
		    "flat AND root predicate should be available") != 0 ||
	    expect_true(
		    root_predicate.kind == SQLPARSER_GRAPH_PREDICATE_BOOL &&
			    root_predicate.bool_operator ==
				    SQLPARSER_GRAPH_PREDICATE_BOOL_AND &&
			    root_predicate.children.count == condition_count,
		    "flat AND root predicate shape mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_span_index_at(
			    &graph,
			    block.predicates,
			    0U,
			    &block_root_index,
			    &error),
		    &error,
		    "flat AND block root predicate should be available") != 0 ||
	    expect_true(
		    block_root_index == 0U,
		    "flat AND block root predicate order mismatch") != 0) {
		goto done;
	}

	for (index = 0U; index < condition_count; index++) {
		sqlparser_graph_field_t field;
		sqlparser_graph_predicate_t child;
		sqlparser_graph_value_t value;
		char expected_name[32];
		size_t block_predicate_index;
		size_t child_index;

		(void)snprintf(
			expected_name,
			sizeof(expected_name),
			"c%lu",
			(unsigned long)(index + 1U));
		if (expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    root_predicate.children,
				    index,
				    &child_index,
				    &error),
			    &error,
			    "flat AND child index should be available") != 0 ||
		    expect_true(
			    child_index == index + 1U,
			    "flat AND child order mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_span_index_at(
				    &graph,
				    block.predicates,
				    index + 1U,
				    &block_predicate_index,
				    &error),
			    &error,
			    "flat AND block predicate index should be available") != 0 ||
		    expect_true(
			    block_predicate_index == child_index,
			    "flat AND block predicate order mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_predicate_at(
				    &graph,
				    child_index,
				    &child,
				    &error),
			    &error,
			    "flat AND child predicate should be available") != 0 ||
		    expect_true(
			    child.kind ==
					    SQLPARSER_GRAPH_PREDICATE_COMPARISON &&
				    child.has_left_field != 0 &&
				    child.left_field_index == index &&
				    child.has_value != 0 &&
				    child.value_index == index &&
				    child.operator_name != NULL &&
				    strcmp(child.operator_name, "=") == 0,
			    "flat AND child predicate semantics mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_field_at(
				    &graph,
				    child.left_field_index,
				    &field,
				    &error),
			    &error,
			    "flat AND child field should be available") != 0 ||
		    expect_true(
			    field.has_relation != 0 &&
				    field.relation_index == 0U &&
				    field.column_name != NULL &&
				    strcmp(field.column_name, expected_name) == 0,
			    "flat AND child field semantics mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_value_at(
				    &graph,
				    child.value_index,
				    &value,
				    &error),
			    &error,
			    "flat AND child value should be available") != 0 ||
		    expect_true(
			    value.kind == SQLPARSER_GRAPH_VALUE_LITERAL &&
				    value.has_field != 0 &&
				    value.field_index ==
					    child.left_field_index &&
				    value.literal.kind ==
					    SQLPARSER_LITERAL_KIND_INTEGER &&
				    value.literal.integer_value ==
					    (long long)(index + 1U),
			    "flat AND child value semantics mismatch") != 0) {
			fprintf(
				stderr,
				"FAIL: flat AND semantic mismatch at condition %lu\n",
				(unsigned long)index);
			goto done;
		}
	}
	result = 0;

done:
	sqlparser_handle_destroy(handle);
	free(sql);
	return result;
}

static int test_query_graph_session_semantics(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		size_t statement_index;
		sqlparser_graph_session_action_t action;
		sqlparser_graph_session_scope_t scope;
		sqlparser_graph_session_target_kind_t target_kind;
		const char *item_name;
		sqlparser_graph_session_value_kind_t value_kind;
		const char *value_text;
		const char *bind_key;
		const char *bind_sql;
		size_t bind_position;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET search_path TO app",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			"search_path",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"app",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET sqlparser_user_parameter TO enabled",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"sqlparser_user_parameter",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"enabled",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET sqlparser_mysql_prepare TO enabled",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"sqlparser_mysql_prepare",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"enabled",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SET U&\"SQLPARSER_MYSQL_PREPARE\" TO enabled",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"SQLPARSER_MYSQL_PREPARE",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"enabled",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"USE analytics",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_DATABASE,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"analytics",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA=KDES",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"KDES",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"ALTER SESSION SET NLS_DATE_LANGUAGE = ENGLISH",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"NLS_DATE_LANGUAGE",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"ENGLISH",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT @input AS input_value, @@ROWCOUNT AS affected_rows; SET ROWCOUNT @row_count",
			1U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"ROWCOUNT",
			SQLPARSER_GRAPH_SESSION_VALUE_BIND,
			NULL,
			"row_count",
			"@row_count",
			2U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SET ROWCOUNT @@ROWCOUNT",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			"ROWCOUNT",
			SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION,
			"@@ROWCOUNT",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SET CONTEXT_INFO @@SPID",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SET,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SESSION_CONTEXT,
			"CONTEXT_INFO",
			SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION,
			"@@SPID",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"ALTER SESSION SET CURRENT_SCHEMA tpcds",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"tpcds",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"ALTER SESSION SET CURRENT_SCHEMA tpcds",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"tpcds",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"ALTER SESSION SET CURRENT_SCHEMA tpcds",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"tpcds",
			NULL,
			NULL,
			0U
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"ALTER SESSION SET CURRENT_SCHEMA tpcds",
			0U,
			SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			"tpcds",
			NULL,
			NULL,
			0U
		}
	};
	sqlparser_graph_session_item_t item;
	sqlparser_graph_session_t session;
	sqlparser_graph_session_value_t value;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	char *view_json;
	size_t index;
	int rc;

	handle = NULL;
	view_json = NULL;
	memset(&error, 0, sizeof(error));
	rc = sqlparser_parse("SELECT 1", &handle, &error);
	if (expect_status_ok(rc, &error, "non-session parse should succeed") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(handle, 0U, &graph, &error),
		    &error,
		    "non-session graph should be available") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_session(&graph, &session, &error),
		    &error,
		    "non-session view should be available") != 0 ||
		    expect_true(
			    session.action == SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN &&
				    session.item_count == 0U,
		    "non-session graph should not expose session state") != 0 ||
	    expect_status_ok(
		    sqlparser_export_view_json(handle, 0, &view_json, &error),
		    &error,
		    "non-session JSON should export") != 0 ||
	    expect_true(
		    view_json != NULL && strstr(view_json, "\"session\"") == NULL,
		    "non-session JSON should omit session") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		view_json = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[index].dialect;
		rc = sqlparser_parse_with_options(
			cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "session parse should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_statement_query_graph(
				    handle,
				    cases[index].statement_index,
				    &graph,
				    &error),
			    &error,
			    "session graph should be available") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session(&graph, &session, &error),
			    &error,
			    "session should be available") != 0 ||
		    expect_true(
			    session.action == cases[index].action &&
				    session.item_count == 1U,
			    "session action or item count mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_item_at(
				    &graph, 0U, &item, &error),
			    &error,
			    "session item should be available") != 0 ||
			    expect_true(
				    item.scope == cases[index].scope &&
					    item.target_kind == cases[index].target_kind &&
					    item.value_count == 1U &&
				    ((item.name == NULL && cases[index].item_name == NULL) ||
				     (item.name != NULL &&
				      cases[index].item_name != NULL &&
				      strcmp(item.name, cases[index].item_name) == 0)),
			    "session item mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_value_at(
				    &graph, item.value_offset, &value, &error),
			    &error,
			    "session value should be available") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (value.kind != cases[index].value_kind) {
			fprintf(
				stderr,
				"FAIL: session value kind mismatch for %s: expected=%d actual=%d\n",
				cases[index].sql,
				(int)cases[index].value_kind,
				(int)value.kind);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].value_kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND) {
			if (expect_true(
				    value.bind_kind == SQLPARSER_BIND_KIND_NAMED &&
					    value.bind_key != NULL &&
					    strcmp(value.bind_key, cases[index].bind_key) == 0 &&
					    value.bind_sql != NULL &&
					    strcmp(value.bind_sql, cases[index].bind_sql) == 0 &&
					    value.has_bind_position != 0 &&
					    value.bind_position == cases[index].bind_position,
				    "session bind mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		} else if (expect_true(
				   value.text != NULL &&
					   strcmp(value.text, cases[index].value_text) == 0,
				   "session text mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
		if (expect_status_ok(rc, &error, "session JSON should export") != 0 ||
		    expect_true(
			    view_json != NULL &&
				    strstr(view_json, "\"session\":{") != NULL,
			    "session JSON should match the public view") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].value_kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND &&
		    expect_true(
			    strstr(view_json, "\"bind_position\":2") != NULL,
			    "session JSON bind position mismatch") != 0) {
			sqlparser_string_free(view_json);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int test_mysql_session_scalar_semantics(void)
{
	static const struct {
		const char *sql;
		const char *item_name;
		sqlparser_graph_session_value_kind_t value_kind;
		sqlparser_literal_kind_t literal_kind;
		const char *text;
		const char *string_value;
		long long integer_value;
	} cases[] = {
		{
			"SET @s='a\\\\nb'",
			"s",
			SQLPARSER_GRAPH_SESSION_VALUE_LITERAL,
			SQLPARSER_LITERAL_KIND_STRING,
			NULL,
			"a\\nb",
			0LL
		},
		{
			"SET @enabled=ON",
			"enabled",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			SQLPARSER_LITERAL_KIND_UNKNOWN,
			"ON",
			NULL,
			0LL
		},
		{
			"SET @disabled=off",
			"disabled",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			SQLPARSER_LITERAL_KIND_UNKNOWN,
			"off",
			NULL,
			0LL
		},
		{
			"SET @'single''quote'=1",
			"single'quote",
			SQLPARSER_GRAPH_SESSION_VALUE_LITERAL,
			SQLPARSER_LITERAL_KIND_INTEGER,
			NULL,
			NULL,
			1LL
		},
		{
			"SET @`double\"quote``tick`=2",
			"double\"quote`tick",
			SQLPARSER_GRAPH_SESSION_VALUE_LITERAL,
			SQLPARSER_LITERAL_KIND_INTEGER,
			NULL,
			NULL,
			2LL
		}
	};
	sqlparser_graph_session_item_t item;
	sqlparser_graph_session_t session;
	sqlparser_graph_session_value_t value;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	size_t index;
	int rc;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_MYSQL;
		rc = sqlparser_parse_with_options(
			cases[index].sql, &options, &handle, &error);
		if (expect_status_ok(
			    rc,
			    &error,
			    "MySQL session scalar parse should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_statement_query_graph(
				    handle, 0U, &graph, &error),
			    &error,
			    "MySQL session scalar graph should be available") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session(&graph, &session, &error),
			    &error,
			    "MySQL session scalar should be available") != 0 ||
		    expect_true(
			    session.action == SQLPARSER_GRAPH_SESSION_ACTION_SET &&
				    session.item_count == 1U,
			    "MySQL session scalar action mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_item_at(
				    &graph, 0U, &item, &error),
			    &error,
			    "MySQL session scalar item should be available") != 0 ||
		    expect_true(
			    item.scope == SQLPARSER_GRAPH_SESSION_SCOPE_SESSION &&
				    item.target_kind ==
					    SQLPARSER_GRAPH_SESSION_TARGET_VARIABLE &&
				    item.name != NULL &&
				    strcmp(item.name, cases[index].item_name) == 0 &&
				    item.value_count == 1U,
			    "MySQL session scalar item mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_value_at(
				    &graph, item.value_offset, &value, &error),
			    &error,
			    "MySQL session scalar value should be available") != 0 ||
		    expect_true(
			    value.kind == cases[index].value_kind &&
				    value.literal.kind == cases[index].literal_kind,
			    "MySQL session scalar value kind mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		if (cases[index].value_kind ==
		    SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD) {
			if (expect_true(
				    value.text != NULL &&
					    strcmp(value.text, cases[index].text) == 0,
				    "MySQL session keyword mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		} else if (cases[index].literal_kind ==
			   SQLPARSER_LITERAL_KIND_STRING) {
			if (expect_true(
				    value.literal.string_value != NULL &&
					    strcmp(
						    value.literal.string_value,
						    cases[index].string_value) == 0,
				    "MySQL session string literal mismatch") != 0) {
				sqlparser_handle_destroy(handle);
				return 1;
			}
		} else if (expect_true(
				   value.literal.integer_value ==
					   cases[index].integer_value,
				   "MySQL session integer literal mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

static int test_query_graph_session_multi_bind_positions(void)
{
	static const char *const item_names[] = {
		"v1",
		"v2",
		"v3",
		"v4"
	};
	char expected_key[16];
	sqlparser_graph_session_item_t item;
	sqlparser_graph_session_t session;
	sqlparser_graph_session_value_t value;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	size_t index;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	rc = sqlparser_parse_with_options(
		"SELECT '?' AS ignored, ? /* ignored ? */; "
		"SET @v1=?, @v2=?, @v3=?, @v4=?",
		&options,
		&handle,
		&error);
	if (expect_status_ok(rc, &error, "MySQL multi-bind session parse should succeed") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(handle, 1U, &graph, &error),
		    &error,
		    "MySQL multi-bind session graph should be available") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_session(&graph, &session, &error),
		    &error,
		    "MySQL multi-bind session should be available") != 0 ||
	    expect_true(
		    session.action == SQLPARSER_GRAPH_SESSION_ACTION_SET &&
			    session.item_count == 4U,
		    "MySQL multi-bind session shape mismatch") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	for (index = 0U; index < 4U; index++) {
		(void)snprintf(
			expected_key,
			sizeof(expected_key),
			"%lu",
			(unsigned long)(index + 2U));
		if (expect_status_ok(
			    sqlparser_query_graph_session_item_at(
				    &graph,
				    index,
				    &item,
				    &error),
			    &error,
			    "MySQL multi-bind session item should be available") != 0 ||
		    expect_true(
			    item.scope == SQLPARSER_GRAPH_SESSION_SCOPE_SESSION &&
				    item.target_kind == SQLPARSER_GRAPH_SESSION_TARGET_VARIABLE &&
				    item.name != NULL &&
				    strcmp(item.name, item_names[index]) == 0 &&
				    item.value_count == 1U,
			    "MySQL multi-bind session item mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_value_at(
				    &graph,
				    item.value_offset,
				    &value,
				    &error),
			    &error,
			    "MySQL multi-bind session value should be available") != 0 ||
		    expect_true(
			    value.kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND &&
				    value.bind_kind == SQLPARSER_BIND_KIND_POSITIONAL &&
				    value.bind_key != NULL &&
				    strcmp(value.bind_key, expected_key) == 0 &&
				    value.bind_sql != NULL &&
				    strcmp(value.bind_sql, "?") == 0 &&
				    value.has_bind_position != 0 &&
				    value.bind_position == index + 2U,
			    "MySQL multi-bind session ordinal mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_sqlserver_system_variable_bind_ordinal(void)
{
	sqlparser_graph_target_t target;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	int rc;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	rc = sqlparser_parse_with_options(
		"SELECT @@ROWCOUNT, ? AS v", &options, &handle, &error);
	if (expect_status_ok(rc, &error, "SQL Server system variable bind parse should succeed") != 0 ||
	    expect_status_ok(
		    sqlparser_statement_query_graph(handle, 0U, &graph, &error),
		    &error,
		    "SQL Server system variable bind graph should be available") != 0 ||
	    expect_true(
		    graph.target_count == 2U,
		    "SQL Server system variable bind target count mismatch") != 0 ||
	    expect_status_ok(
		    sqlparser_query_graph_target_at(&graph, 0U, &target, &error),
		    &error,
		    "SQL Server system variable target should be available") != 0 ||
	    expect_true(
		    target.kind == SQLPARSER_GRAPH_TARGET_EXPRESSION &&
			    target.has_value == 0,
		    "SQL Server system variable should remain an expression") != 0 ||
	    expect_query_graph_target_value(
		    &graph,
		    1U,
		    SQLPARSER_GRAPH_TARGET_BIND,
		    SQLPARSER_GRAPH_VALUE_BIND,
		    "1",
		    SQLPARSER_BIND_KIND_POSITIONAL,
		    1U,
		    "?",
		    SQLPARSER_LITERAL_KIND_UNKNOWN,
		    NULL,
		    0LL) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_query_graph_session_control_patch_span(void)
{
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	const char *sql;
	sqlparser_graph_session_item_t item;
	sqlparser_graph_session_t session;
	sqlparser_graph_session_value_t value;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	size_t dialect_index;
	int rc;

	sql = "IF @enabled = 1 BEGIN SET CONTEXT_INFO @ctx; END";
	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = dialects[dialect_index];
		rc = sqlparser_parse_with_options(sql, &options, &handle, &error);
		if (expect_status_ok(rc, &error, "control session parse should succeed") != 0 ||
		    expect_true(
			    sqlparser_statement_count(handle) == 2U,
			    "control session should expose condition and SET units") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		memset(&patch, 0, sizeof(patch));
		patch.op = SQLPARSER_PATCH_REPLACE;
		patch.selector = "stmt[0].clause[0]";
		patch.sql = "@enabled = 2 AND @extra = 3";
		patch_list.items = &patch;
		patch_list.count = 1U;
		rc = sqlparser_apply_patch(handle, &patch_list, &error);
		if (expect_status_ok(rc, &error, "control condition patch should succeed") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}

		memset(&graph, 0, sizeof(graph));
		memset(&session, 0, sizeof(session));
		memset(&item, 0, sizeof(item));
		memset(&value, 0, sizeof(value));
		rc = sqlparser_statement_query_graph(handle, 1U, &graph, &error);
		if (expect_status_ok(rc, &error, "patched control session graph should succeed") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session(&graph, &session, &error),
			    &error,
			    "patched control session should be available") != 0 ||
		    expect_true(
			    session.action == SQLPARSER_GRAPH_SESSION_ACTION_SET &&
				    session.item_count == 1U,
			    "patched control session action mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_item_at(&graph, 0U, &item, &error),
			    &error,
			    "patched control session item should be available") != 0 ||
		    expect_true(
			    item.scope == SQLPARSER_GRAPH_SESSION_SCOPE_SESSION &&
				    item.target_kind == SQLPARSER_GRAPH_SESSION_TARGET_SESSION_CONTEXT &&
				    item.name != NULL &&
				    strcmp(item.name, "CONTEXT_INFO") == 0 &&
				    item.value_count == 1U,
			    "patched control session item mismatch") != 0 ||
		    expect_status_ok(
			    sqlparser_query_graph_session_value_at(
				    &graph,
				    item.value_offset,
				    &value,
				    &error),
			    &error,
			    "patched control session value should be available") != 0 ||
		    expect_true(
			    value.kind == SQLPARSER_GRAPH_SESSION_VALUE_BIND &&
				    value.bind_kind == SQLPARSER_BIND_KIND_NAMED &&
				    value.bind_key != NULL &&
				    strcmp(value.bind_key, "ctx") == 0 &&
				    value.bind_sql != NULL &&
				    strcmp(value.bind_sql, "@ctx") == 0 &&
				    value.has_bind_position != 0 &&
				    value.bind_position == 3U,
			    "patched control session bind mismatch") != 0) {
			sqlparser_handle_destroy(handle);
			return 1;
		}
		sqlparser_handle_destroy(handle);
	}
	return 0;
}

int main(void)
{
	if (test_relation_patch_identifier_path_spelling() != 0) {
		return 1;
	}
	if (test_insert_column_patch_identifier_spelling() != 0) {
		return 1;
	}
	if (test_deparse_identifier_spelling() != 0) {
		return 1;
	}
	if (test_control_flow_core() != 0) {
		return 1;
	}
	if (test_control_identifier_source_window() != 0) {
		return 1;
	}
	if (test_control_flow_patch_and_limits() != 0) {
		return 1;
	}
	if (test_nested_control_depth(SQLPARSER_CONTROL_MAX_DEPTH, SQLPARSER_STATUS_OK) != 0) {
		return 1;
	}
	if (test_nested_control_depth(SQLPARSER_CONTROL_MAX_DEPTH + 1U, SQLPARSER_STATUS_RESOURCE_LIMIT) != 0) {
		return 1;
	}
	if (test_statement_kind_walk() != 0) {
		return 1;
	}
	if (test_insert_values_literal_mutation() != 0) {
		return 1;
	}
	if (test_insert_select_inspect() != 0) {
		return 1;
	}
	if (test_update_assignment_literal_mutation() != 0) {
		return 1;
	}
	if (test_update_assignment_sql_mutation() != 0) {
		return 1;
	}
	if (test_update_assignment_list_patch_api() != 0) {
		return 1;
	}
	if (test_update_assignment_list_apply_patch() != 0) {
		return 1;
	}
	if (test_structured_update_assignment_from_assignment_value() != 0) {
		return 1;
	}
	if (test_update_assignment_bind_rhs_literal_rewrite() != 0) {
		return 1;
	}
	if (test_update_assignment_multiple_bind_rhs_literal_rewrite() != 0) {
		return 1;
	}
	if (test_update_assignment_literal_rewrite_rejects_complex_rhs() != 0) {
		return 1;
	}
	if (test_insert_cell_sql_mutation() != 0) {
		return 1;
	}
	if (test_insert_cell_source_keyword_boundary() != 0) {
		return 1;
	}
	if (test_sqlserver_control_nested_expression_source_sql() != 0) {
		return 1;
	}
	if (test_merge_single_insert_branches() != 0) {
		return 1;
	}
	if (test_postgresql_merge_multiple_insert_branches() != 0) {
		return 1;
	}
	if (test_postgresql_merge_complete_action_branches() != 0) {
		return 1;
	}
	if (test_merge_branch_detail_api_contract() != 0) {
		return 1;
	}
	if (test_merge_condition_scanner_boundaries() != 0) {
		return 1;
	}
	if (test_oracle_merge_action_where_after_patch() != 0) {
		return 1;
	}
	if (test_postgresql_merge_by_source_match_branch() != 0) {
		return 1;
	}
	if (test_sqlserver_multiple_nested_merge_selectors() != 0) {
		return 1;
	}
	if (test_sqlserver_merge_scope_and_cte_lineage() != 0) {
		return 1;
	}
	if (test_non_postgresql_multiple_merge_insert_rejection() != 0) {
		return 1;
	}
	if (test_insert_cell_bind_mutation() != 0) {
		return 1;
	}
	if (test_delete_where_literal_mutation() != 0) {
		return 1;
	}
	if (test_generic_relation_and_literal_api() != 0) {
		return 1;
	}
	if (test_generic_name_mutation_preserves_later_spelling() != 0) {
		return 1;
	}
	if (test_literal_mutation_preserves_ddl_identifier_spelling() != 0) {
		return 1;
	}
	if (test_sqlserver_literal_mutation_preserves_bracket_identifiers() != 0) {
		return 1;
	}
	if (test_copy_same_name_mutation_preserves_options() != 0) {
		return 1;
	}
	if (test_generic_name_api_on_ddl() != 0) {
		return 1;
	}
	if (test_selector_parse_and_format() != 0) {
		return 1;
	}
	if (test_where_clause_sql_rewrite_api() != 0) {
		return 1;
	}
	if (test_query_graph_json_and_patch_api() != 0) {
		return 1;
	}
	if (test_query_graph_session_semantics() != 0) {
		return 1;
	}
	if (test_mysql_session_scalar_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_session_multi_bind_positions() != 0) {
		return 1;
	}
	if (test_sqlserver_system_variable_bind_ordinal() != 0) {
		return 1;
	}
	if (test_query_graph_session_control_patch_span() != 0) {
		return 1;
	}
	if (test_query_graph_bind_fields() != 0) {
		return 1;
	}
	if (test_query_graph_like_escape_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_operator_kind_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_condition_value_lists() != 0) {
		return 1;
	}
	if (test_query_graph_field_match_kind_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_expression_field_value_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_column_semantics_json() != 0) {
		return 1;
	}
	if (test_query_graph_public_struct_semantics() != 0) {
		return 1;
	}
	if (test_generated_sysdate_query_graph_semantics() != 0) {
		return 1;
	}
	if (test_query_graph_join_using_reuses_fields() != 0) {
		return 1;
	}
	if (test_query_graph_flat_and_span_growth() != 0) {
		return 1;
	}
	if (test_generated_keyword_insert_column() != 0) {
		return 1;
	}
	if (test_insert_select_target_values() != 0) {
		return 1;
	}
	if (test_oracle_multi_insert_query_graph_and_patch() != 0) {
		return 1;
	}
	if (test_dameng_multi_insert_query_graph_and_patch() != 0) {
		return 1;
	}
	if (test_oracle_p3_update_assignment_graph() != 0) {
		return 1;
	}
	if (test_oracle_p3_merge_source_target_graph() != 0) {
		return 1;
	}
	if (test_oracle_merge_assignment_patch_closure() != 0) {
		return 1;
	}
	if (test_sqlserver_merge_question_bind_positions() != 0) {
		return 1;
	}
	if (test_sqlserver_merge_raw_question_bind_positions() != 0) {
		return 1;
	}
	if (test_oracle_merge_assignment_patch_failures() != 0) {
		return 1;
	}
	if (test_vastbase_postgresql_multi_when_merge_assignment_patch() != 0) {
		return 1;
	}
	if (test_query_graph_attribution_and_values() != 0) {
		return 1;
	}
	if (test_select_target_list_patch_api() != 0) {
		return 1;
	}
	if (test_fragment_mutation_identifier_spelling() != 0) {
		return 1;
	}
	if (test_special_fragment_mutation_identifier_spelling() != 0) {
		return 1;
	}
	if (test_structured_select_target_column_replacement() != 0) {
		return 1;
	}
	if (test_query_graph_set_operation_attribution() != 0) {
		return 1;
	}
	if (test_query_graph_strict_contract_edges() != 0) {
		return 1;
	}
	if (test_generic_literal_api_on_ddl() != 0) {
		return 1;
	}
	if (test_update_from_returning_sql_mutation() != 0) {
		return 1;
	}
	if (test_insert_on_conflict_returning_sql_mutation() != 0) {
		return 1;
	}
	if (test_merge_statement_walk() != 0) {
		return 1;
	}
	if (test_resource_limits() != 0) {
		return 1;
	}
	if (test_mysql_dialect_select_rewrite() != 0) {
		return 1;
	}
	if (test_mysql_dialect_insert_rewrite() != 0) {
		return 1;
	}
	if (test_dialect_insert_column_patch_with_question_param() != 0) {
		return 1;
	}
	if (test_mysql_dialect_create_table_extensions() != 0) {
		return 1;
	}
	if (test_sqlserver_dialect_option() != 0) {
		return 1;
	}
	if (test_sqlserver_control_flow_and_patch() != 0) {
		return 1;
	}
	if (test_sqlserver_control_depth_limit() != 0) {
		return 1;
	}
	if (test_sqlserver_output_query_graph_and_patch() != 0) {
		return 1;
	}
	if (test_sqlserver_output_action_marker_and_patch() != 0) {
		return 1;
	}
	if (test_sqlserver_nested_output_query_graph_and_patch() != 0) {
		return 1;
	}
	if (test_sqlserver_delete_output_source_graph_and_patch() != 0) {
		return 1;
	}
	if (test_sqlserver_output_failure_is_non_destructive() != 0) {
		return 1;
	}
	if (test_sqlserver_output_sink_patch_validation() != 0) {
		return 1;
	}
	if (test_sqlserver_nested_output_resource_limit() != 0) {
		return 1;
	}
	if (test_session_context_patch_api() != 0) {
		return 1;
	}
	if (test_session_context_quoted_identifier_literal_api() != 0) {
		return 1;
	}
	if (test_oracle_container_service_patch_api() != 0) {
		return 1;
	}

	return 0;
}
