#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_ast_internal.h"
#include "sqlparser_control_internal.h"

typedef struct {
	uint8_t *data;
	size_t capacity;
} sqlparser_control_scratch_t;

typedef struct {
	char *data;
	size_t length;
	size_t capacity;
	size_t limit;
} sqlparser_control_buffer_t;

typedef struct {
	const sqlparser_control_state_t *state;
	unsigned char *seen_nodes;
	unsigned char *seen_branches;
	unsigned char *seen_items;
	unsigned char *seen_units;
} sqlparser_control_validation_t;

static int sqlparser_control_size_add(size_t left, size_t right, size_t *out)
{
	if (out == NULL || left > SIZE_MAX - right) {
		return -1;
	}
	*out = left + right;
	return 0;
}

static int sqlparser_control_size_mul(size_t left, size_t right, size_t *out)
{
	if (out == NULL || (left != 0U && right > SIZE_MAX / left)) {
		return -1;
	}
	*out = left * right;
	return 0;
}

static int sqlparser_control_align_size(size_t value, size_t alignment, size_t *out)
{
	size_t remainder;

	if (out == NULL || alignment == 0U) {
		return -1;
	}
	remainder = value % alignment;
	if (remainder == 0U) {
		*out = value;
		return 0;
	}
	return sqlparser_control_size_add(value, alignment - remainder, out);
}

static int sqlparser_control_layout_append(
	size_t count,
	size_t item_size,
	size_t item_alignment,
	size_t *io_total,
	size_t *out_offset)
{
	size_t aligned;
	size_t bytes;

	if (io_total == NULL || out_offset == NULL ||
	    sqlparser_control_align_size(*io_total, item_alignment, &aligned) != 0 ||
	    sqlparser_control_size_mul(count, item_size, &bytes) != 0 ||
	    sqlparser_control_size_add(aligned, bytes, io_total) != 0) {
		return -1;
	}
	*out_offset = aligned;
	return 0;
}

sqlparser_status_t sqlparser_control_state_allocate(
	const sqlparser_control_counts_t *counts,
	const sqlparser_limits_t *limits,
	sqlparser_control_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_control_state_t *state;
	size_t branch_limit;
	size_t index_limit;
	size_t total;
	size_t nodes_offset;
	size_t branches_offset;
	size_t items_offset;
	size_t indices_offset;
	size_t units_offset;
	uint8_t *base;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	if (counts == NULL || limits == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control counts and limits must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (counts->root_count > counts->item_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "control root count exceeds item count");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (limits->max_statement_count > 0U) {
		if (counts->unit_count > limits->max_statement_count ||
		    counts->node_count > limits->max_statement_count ||
		    counts->item_count > limits->max_statement_count) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "control flow exceeds configured statement limit");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		if (sqlparser_control_size_add(
			    limits->max_statement_count, limits->max_statement_count, &branch_limit) != 0) {
			branch_limit = SIZE_MAX;
		}
		if (counts->branch_count > branch_limit) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "control branch count exceeds configured statement limit");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		if (branch_limit == SIZE_MAX ||
		    sqlparser_control_size_add(branch_limit, limits->max_statement_count, &index_limit) != 0) {
			index_limit = SIZE_MAX;
		}
		if (counts->index_count > index_limit) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "control index count exceeds configured statement limit");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
	}

	total = sizeof(*state);
	if (sqlparser_control_layout_append(
		    counts->node_count,
		    sizeof(*state->nodes),
		    _Alignof(sqlparser_control_node_t),
		    &total,
		    &nodes_offset) != 0 ||
	    sqlparser_control_layout_append(
		    counts->branch_count,
		    sizeof(*state->branches),
		    _Alignof(sqlparser_control_branch_t),
		    &total,
		    &branches_offset) != 0 ||
	    sqlparser_control_layout_append(
		    counts->item_count,
		    sizeof(*state->items),
		    _Alignof(sqlparser_control_item_t),
		    &total,
		    &items_offset) != 0 ||
	    sqlparser_control_layout_append(
		    counts->index_count,
		    sizeof(*state->index_pool),
		    _Alignof(size_t),
		    &total,
		    &indices_offset) != 0 ||
	    sqlparser_control_layout_append(
		    counts->unit_count,
		    sizeof(*state->units),
		    _Alignof(sqlparser_control_unit_t),
		    &total,
		    &units_offset) != 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "control flow allocation is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	state = (sqlparser_control_state_t *)calloc(1U, total);
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	base = (uint8_t *)state;
	state->roots.offset = 0U;
	state->roots.count = counts->root_count;
	state->node_count = counts->node_count;
	state->branch_count = counts->branch_count;
	state->item_count = counts->item_count;
	state->index_count = counts->index_count;
	state->unit_count = counts->unit_count;
	state->nodes = counts->node_count > 0U ? (sqlparser_control_node_t *)(base + nodes_offset) : NULL;
	state->branches = counts->branch_count > 0U ? (sqlparser_control_branch_t *)(base + branches_offset) : NULL;
	state->items = counts->item_count > 0U ? (sqlparser_control_item_t *)(base + items_offset) : NULL;
	state->index_pool = counts->index_count > 0U ? (size_t *)(base + indices_offset) : NULL;
	state->units = counts->unit_count > 0U ? (sqlparser_control_unit_t *)(base + units_offset) : NULL;
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_control_state_release(sqlparser_control_state_t *state)
{
	free(state);
}

sqlparser_status_t sqlparser_control_state_clone(
	const sqlparser_control_state_t *source,
	const sqlparser_limits_t *limits,
	sqlparser_control_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_control_counts_t counts;
	sqlparser_control_state_t *clone;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	if (source == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (limits == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "limits must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&counts, 0, sizeof(counts));
	counts.root_count = source->roots.count;
	counts.node_count = source->node_count;
	counts.branch_count = source->branch_count;
	counts.item_count = source->item_count;
	counts.index_count = source->index_count;
	counts.unit_count = source->unit_count;
	clone = NULL;
	status = sqlparser_control_state_allocate(&counts, limits, &clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->roots = source->roots;
	if (source->node_count > 0U) {
		memcpy(clone->nodes, source->nodes, source->node_count * sizeof(*source->nodes));
	}
	if (source->branch_count > 0U) {
		memcpy(clone->branches, source->branches, source->branch_count * sizeof(*source->branches));
	}
	if (source->item_count > 0U) {
		memcpy(clone->items, source->items, source->item_count * sizeof(*source->items));
	}
	if (source->index_count > 0U) {
		memcpy(clone->index_pool, source->index_pool, source->index_count * sizeof(*source->index_pool));
	}
	if (source->unit_count > 0U) {
		memcpy(clone->units, source->units, source->unit_count * sizeof(*source->units));
	}
	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_control_span_is_valid(
	const sqlparser_control_state_t *state,
	sqlparser_index_span_t span)
{
	return state != NULL && span.offset <= state->index_count &&
		span.count <= state->index_count - span.offset;
}

static sqlparser_status_t sqlparser_control_invalid_state(
	sqlparser_error_t *out_error,
	const char *message)
{
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, message);
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static sqlparser_status_t sqlparser_control_validate_item(
	sqlparser_control_validation_t *validation,
	size_t item_index,
	size_t depth,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_control_validate_node(
	sqlparser_control_validation_t *validation,
	size_t node_index,
	size_t depth,
	sqlparser_error_t *out_error)
{
	const sqlparser_control_state_t *state;
	const sqlparser_control_node_t *node;
	size_t branch_ordinal;

	state = validation->state;
	if (node_index >= state->node_count || validation->seen_nodes[node_index] != 0U) {
		return sqlparser_control_invalid_state(out_error, "control node topology is invalid");
	}
	if (depth > SQLPARSER_CONTROL_MAX_DEPTH) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "control flow nesting is too deep");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	validation->seen_nodes[node_index] = 1U;
	node = &state->nodes[node_index];
	if (node->kind != SQLPARSER_CONTROL_NODE_IF ||
	    (node->branches.count != 1U && node->branches.count != 2U) ||
	    !sqlparser_control_span_is_valid(state, node->branches)) {
		return sqlparser_control_invalid_state(out_error, "control node is invalid");
	}

	for (branch_ordinal = 0U; branch_ordinal < node->branches.count; branch_ordinal++) {
		const sqlparser_control_branch_t *branch;
		size_t branch_index;
		size_t item_ordinal;

		branch_index = state->index_pool[node->branches.offset + branch_ordinal];
		if (branch_index >= state->branch_count || validation->seen_branches[branch_index] != 0U) {
			return sqlparser_control_invalid_state(out_error, "control branch topology is invalid");
		}
		validation->seen_branches[branch_index] = 1U;
		branch = &state->branches[branch_index];
		if ((branch_ordinal == 0U && !branch->has_condition) ||
		    (branch_ordinal > 0U && branch->has_condition) ||
		    branch->items.count == 0U ||
		    !sqlparser_control_span_is_valid(state, branch->items)) {
			return sqlparser_control_invalid_state(out_error, "control branch is invalid");
		}
		if (branch->has_condition) {
			if (branch->condition_statement_index >= state->unit_count ||
			    state->units[branch->condition_statement_index].kind != SQLPARSER_CONTROL_UNIT_CONDITION ||
			    validation->seen_units[branch->condition_statement_index] != 0U) {
				return sqlparser_control_invalid_state(out_error, "control condition unit is invalid");
			}
			validation->seen_units[branch->condition_statement_index] = 1U;
		}
		for (item_ordinal = 0U; item_ordinal < branch->items.count; item_ordinal++) {
			sqlparser_status_t status;
			size_t item_index;

			item_index = state->index_pool[branch->items.offset + item_ordinal];
			status = sqlparser_control_validate_item(validation, item_index, depth, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_control_validate_item(
	sqlparser_control_validation_t *validation,
	size_t item_index,
	size_t depth,
	sqlparser_error_t *out_error)
{
	const sqlparser_control_state_t *state;
	const sqlparser_control_item_t *item;

	state = validation->state;
	if (item_index >= state->item_count || validation->seen_items[item_index] != 0U) {
		return sqlparser_control_invalid_state(out_error, "control item topology is invalid");
	}
	validation->seen_items[item_index] = 1U;
	item = &state->items[item_index];
	if (item->kind == SQLPARSER_CONTROL_ITEM_STATEMENT) {
		if (item->index >= state->unit_count ||
		    state->units[item->index].kind != SQLPARSER_CONTROL_UNIT_STATEMENT ||
		    validation->seen_units[item->index] != 0U) {
			return sqlparser_control_invalid_state(out_error, "control statement unit is invalid");
		}
		validation->seen_units[item->index] = 1U;
		return SQLPARSER_STATUS_OK;
	}
	if (item->kind == SQLPARSER_CONTROL_ITEM_NODE) {
		return sqlparser_control_validate_node(validation, item->index, depth + 1U, out_error);
	}
	return sqlparser_control_invalid_state(out_error, "control item kind is invalid");
}

static sqlparser_status_t sqlparser_control_validate_state(
	sqlparser_handle_t *handle,
	const sqlparser_control_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_control_validation_t validation;
	unsigned char *seen;
	size_t scratch_count;
	size_t doubled_unit_count;
	size_t expected_indices;
	size_t branch_refs;
	size_t item_refs;
	size_t previous_end;
	size_t index;
	sqlparser_status_t status;

	if (handle == NULL || state == NULL || state->roots.count == 0U ||
	    state->node_count == 0U || state->branch_count == 0U ||
	    state->item_count == 0U || state->unit_count == 0U ||
	    state->nodes == NULL || state->branches == NULL || state->items == NULL ||
	    state->units == NULL || state->index_pool == NULL ||
	    !sqlparser_control_span_is_valid(state, state->roots)) {
		return sqlparser_control_invalid_state(out_error, "control state is incomplete");
	}
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (handle->ast->n_stmts != state->unit_count || handle->ast->stmts == NULL) {
		return sqlparser_control_invalid_state(out_error, "control units do not match parser statements");
	}

	branch_refs = 0U;
	for (index = 0U; index < state->node_count; index++) {
		if (!sqlparser_control_span_is_valid(state, state->nodes[index].branches) ||
		    sqlparser_control_size_add(branch_refs, state->nodes[index].branches.count, &branch_refs) != 0) {
			return sqlparser_control_invalid_state(out_error, "control branch span is invalid");
		}
	}
	item_refs = state->roots.count;
	for (index = 0U; index < state->branch_count; index++) {
		if (!sqlparser_control_span_is_valid(state, state->branches[index].items) ||
		    sqlparser_control_size_add(item_refs, state->branches[index].items.count, &item_refs) != 0) {
			return sqlparser_control_invalid_state(out_error, "control item span is invalid");
		}
	}
	if (branch_refs != state->branch_count || item_refs != state->item_count ||
	    sqlparser_control_size_add(branch_refs, item_refs, &expected_indices) != 0 ||
	    expected_indices != state->index_count) {
		return sqlparser_control_invalid_state(out_error, "control index pool is inconsistent");
	}

	if (sqlparser_control_size_add(state->unit_count, state->unit_count, &doubled_unit_count) != 0 ||
	    sqlparser_control_size_add(state->node_count, state->branch_count, &scratch_count) != 0 ||
	    sqlparser_control_size_add(scratch_count, state->item_count, &scratch_count) != 0 ||
	    sqlparser_control_size_add(scratch_count, doubled_unit_count, &scratch_count) != 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "control validation state is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	seen = (unsigned char *)calloc(scratch_count, 1U);
	if (seen == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memset(&validation, 0, sizeof(validation));
	validation.state = state;
	validation.seen_nodes = seen;
	validation.seen_branches = validation.seen_nodes + state->node_count;
	validation.seen_items = validation.seen_branches + state->branch_count;
	validation.seen_units = validation.seen_items + state->item_count;

	previous_end = 0U;
	for (index = 0U; index < state->unit_count; index++) {
		const sqlparser_control_unit_t *unit;
		unsigned char *seen_ast;
		PgQuery__RawStmt *raw_stmt;

		unit = &state->units[index];
		seen_ast = validation.seen_units + state->unit_count;
		if ((unit->kind != SQLPARSER_CONTROL_UNIT_STATEMENT && unit->kind != SQLPARSER_CONTROL_UNIT_CONDITION) ||
		    unit->ast_statement_index >= handle->ast->n_stmts || seen_ast[unit->ast_statement_index] != 0U ||
		    unit->source_length == 0U || unit->source_offset > handle->sql_len ||
		    unit->source_length > handle->sql_len - unit->source_offset ||
		    (index > 0U && unit->source_offset < previous_end)) {
			free(seen);
			return sqlparser_control_invalid_state(out_error, "control unit mapping is invalid");
		}
		seen_ast[unit->ast_statement_index] = 1U;
		previous_end = unit->source_offset + unit->source_length;
		raw_stmt = handle->ast->stmts[unit->ast_statement_index];
		if (raw_stmt == NULL || raw_stmt->stmt == NULL ||
		    (unit->kind == SQLPARSER_CONTROL_UNIT_CONDITION &&
		     (raw_stmt->stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
		      raw_stmt->stmt->select_stmt == NULL || raw_stmt->stmt->select_stmt->where_clause == NULL))) {
			free(seen);
			return sqlparser_control_invalid_state(out_error, "control unit AST is invalid");
		}
	}

	status = SQLPARSER_STATUS_OK;
	for (index = 0U; index < state->roots.count; index++) {
		size_t item_index;

		item_index = state->index_pool[state->roots.offset + index];
		status = sqlparser_control_validate_item(&validation, item_index, 0U, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < state->node_count; index++) {
			if (validation.seen_nodes[index] == 0U) {
				status = sqlparser_control_invalid_state(out_error, "control node is unreachable");
				break;
			}
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < state->branch_count; index++) {
			if (validation.seen_branches[index] == 0U) {
				status = sqlparser_control_invalid_state(out_error, "control branch is unreachable");
				break;
			}
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < state->item_count; index++) {
			if (validation.seen_items[index] == 0U) {
				status = sqlparser_control_invalid_state(out_error, "control item is unreachable");
				break;
			}
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < state->unit_count; index++) {
			if (validation.seen_units[index] == 0U) {
				status = sqlparser_control_invalid_state(out_error, "control unit is unreachable");
				break;
			}
		}
	}
	free(seen);
	return status;
}

sqlparser_status_t sqlparser_control_state_attach(
	sqlparser_handle_t *handle,
	sqlparser_control_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (handle == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and control state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (handle->control != NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle already has control state");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_control_validate_state(handle, state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	handle->control = state;
	handle->statement_count = state->unit_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_statement_ast_index(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_ast_statement_index,
	sqlparser_error_t *out_error)
{
	if (out_ast_statement_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_ast_statement_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_ast_statement_index = 0U;
	if (handle == NULL || statement_index >= handle->statement_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_ast_statement_index = handle->control != NULL ?
		handle->control->units[statement_index].ast_statement_index : statement_index;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_condition_expression(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_expression,
	sqlparser_error_t *out_error)
{
	if (out_expression == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_expression must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_expression = NULL;
	if (!sqlparser_control_unit_is_condition(handle, statement_index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement is not a control condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_get_statement_node(handle, statement_index, out_expression, out_error);
}

static sqlparser_status_t sqlparser_control_postprocess_unit(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	int is_condition,
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (handle != NULL && handle->dialect_ops != NULL &&
	    handle->dialect_ops->postprocess_control_unit != NULL) {
		*out_sql = NULL;
		status = handle->dialect_ops->postprocess_control_unit(
			core_sql,
			handle->dialect_state,
			statement_index,
			is_condition,
			out_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(*out_sql);
			*out_sql = NULL;
			return status;
		}
		if (*out_sql == NULL) {
			return sqlparser_control_invalid_state(out_error, "dialect control fragment output is missing");
		}
		status = sqlparser_validate_handle_output_text(handle, *out_sql, "control SQL fragment", out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(*out_sql);
			*out_sql = NULL;
		}
		return status;
	}
	return sqlparser_postprocess_handle_sql_fragment(
		handle,
		statement_index,
		core_sql,
		"control SQL fragment",
		out_sql,
		out_error);
}

sqlparser_status_t sqlparser_control_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *expression;
	char *core_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	expression = NULL;
	core_sql = NULL;
	status = sqlparser_control_condition_expression(
		(sqlparser_handle_t *)handle, statement_index, &expression, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_render_where_node_sql(expression, &core_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_control_postprocess_unit(
			handle, statement_index, 1, core_sql, out_sql, out_error);
	}
	free(core_sql);
	return status;
}

sqlparser_status_t sqlparser_control_set_condition_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *expression;
	PgQuery__Node *replacement;
	char *parser_sql;
	void *candidate_state;
	sqlparser_status_t status;

	if (handle == NULL || sql_text == NULL ||
	    !sqlparser_control_unit_is_condition(handle, statement_index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control condition SQL requires a condition statement");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	expression = NULL;
	replacement = NULL;
	parser_sql = NULL;
	candidate_state = NULL;
	status = sqlparser_preprocess_handle_sql_fragment(
		handle, statement_index, sql_text, "control condition SQL", &parser_sql, &candidate_state, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_where_node_sql(parser_sql, &replacement, out_error);
	}
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	status = sqlparser_control_condition_expression(handle, statement_index, &expression, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(replacement);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	{
		size_t ast_index;
		PgQuery__SelectStmt *wrapper;

		status = sqlparser_control_statement_ast_index(handle, statement_index, &ast_index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_free_proto_node(replacement);
			sqlparser_handle_discard_dialect_state(handle, candidate_state);
			return status;
		}
		wrapper = handle->ast->stmts[ast_index]->stmt->select_stmt;
		wrapper->where_clause = replacement;
		replacement = NULL;
		sqlparser_free_proto_node(expression);
	}
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, candidate_state);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_control_scratch_reserve(
	sqlparser_control_scratch_t *scratch,
	size_t required,
	sqlparser_error_t *out_error)
{
	uint8_t *next;
	size_t capacity;

	if (required <= scratch->capacity) {
		return SQLPARSER_STATUS_OK;
	}
	capacity = scratch->capacity > 0U ? scratch->capacity : 256U;
	while (capacity < required) {
		if (capacity > SIZE_MAX / 2U) {
			capacity = required;
			break;
		}
		capacity *= 2U;
	}
	next = (uint8_t *)realloc(scratch->data, capacity);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	scratch->data = next;
	scratch->capacity = capacity;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_control_render_statement_core(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_control_scratch_t *scratch,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__ParseResult single = PG_QUERY__PARSE_RESULT__INIT;
	PgQuery__RawStmt *raw_statement;
	PgQuery__RawStmt *statements[1];
	PgQueryProtobuf protobuf;
	PgQueryDeparseResult result;
	size_t ast_index;
	size_t packed_size;
	size_t packed_len;
	size_t source_end;
	size_t source_start;
	sqlparser_status_t status;

	*out_sql = NULL;
	status = sqlparser_control_statement_ast_index(handle, statement_index, &ast_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	raw_statement = handle->ast->stmts[ast_index];
	if (raw_statement == NULL ||
	    raw_statement->stmt_location < 0 ||
	    (size_t)raw_statement->stmt_location >
		    handle->parser_sql_len) {
		return sqlparser_control_invalid_state(
			out_error,
			"control statement source range is invalid");
	}
	source_start = (size_t)raw_statement->stmt_location;
	if (raw_statement->stmt_len > 0) {
		if ((size_t)raw_statement->stmt_len >
		    handle->parser_sql_len - source_start) {
			return sqlparser_control_invalid_state(
				out_error,
				"control statement source range is invalid");
		}
		source_end =
			source_start + (size_t)raw_statement->stmt_len;
	} else if (ast_index + 1U < handle->ast->n_stmts &&
		   handle->ast->stmts[ast_index + 1U] != NULL &&
		   handle->ast->stmts[ast_index + 1U]->stmt_location >= 0 &&
		   (size_t)handle->ast->stmts[ast_index + 1U]
				    ->stmt_location > source_start &&
		   (size_t)handle->ast->stmts[ast_index + 1U]
				    ->stmt_location <=
			   handle->parser_sql_len) {
		source_end =
			(size_t)handle->ast->stmts[ast_index + 1U]
				->stmt_location;
	} else {
		source_end = handle->parser_sql_len;
	}
	statements[0] = raw_statement;
	single.version = handle->ast->version;
	single.n_stmts = 1U;
	single.stmts = statements;
	packed_size = pg_query__parse_result__get_packed_size(&single);
	if (packed_size == 0U) {
		return sqlparser_control_invalid_state(out_error, "failed to pack control statement");
	}
	status = sqlparser_control_scratch_reserve(scratch, packed_size, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	packed_len = pg_query__parse_result__pack(&single, scratch->data);
	if (packed_len != packed_size) {
		return sqlparser_control_invalid_state(out_error, "failed to serialize control statement");
	}
	protobuf.data = (char *)scratch->data;
	protobuf.len = packed_len;
	sqlparser_pg_query_prepare();
	result = sqlparser_deparse_protobuf_for_handle(
		handle,
		protobuf,
		ast_index,
		source_start,
		source_end);
	if (result.error != NULL || result.query == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			result.error != NULL && result.error->message != NULL ? result.error->message : "failed to deparse control statement");
		pg_query_free_deparse_result(result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_sql = result.query;
	result.query = NULL;
	pg_query_free_deparse_result(result);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_control_buffer_reserve(
	sqlparser_control_buffer_t *buffer,
	size_t additional,
	sqlparser_error_t *out_error)
{
	size_t required;
	size_t capacity;
	size_t max_capacity;
	char *next;

	if (buffer->length > buffer->limit || additional > buffer->limit - buffer->length ||
	    sqlparser_control_size_add(buffer->length, additional, &required) != 0 ||
	    required == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "deparse output exceeds configured byte limit");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	required++;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}
	max_capacity = buffer->limit == SIZE_MAX ? SIZE_MAX : buffer->limit + 1U;
	capacity = buffer->capacity > 0U ? buffer->capacity : (max_capacity < 256U ? max_capacity : 256U);
	while (capacity < required) {
		if (capacity > max_capacity / 2U) {
			capacity = max_capacity;
			break;
		}
		capacity *= 2U;
	}
	next = (char *)realloc(buffer->data, capacity);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	buffer->data = next;
	buffer->capacity = capacity;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_control_buffer_append(
	sqlparser_control_buffer_t *buffer,
	const char *text,
	size_t length,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_control_buffer_reserve(buffer, length, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (length > 0U) {
		memcpy(buffer->data + buffer->length, text, length);
	}
	buffer->length += length;
	buffer->data[buffer->length] = '\0';
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_build_public_sql(
	const sqlparser_handle_t *handle,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_control_buffer_t buffer;
	sqlparser_control_scratch_t scratch;
	sqlparser_handle_t *mutable_handle;
	size_t source_offset;
	size_t unit_index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || handle->control == NULL || handle->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle does not contain control flow");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_handle_ensure_ast(mutable_handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&buffer, 0, sizeof(buffer));
	memset(&scratch, 0, sizeof(scratch));
	buffer.limit = handle->limits.max_output_bytes;
	source_offset = 0U;
	status = SQLPARSER_STATUS_OK;
	for (unit_index = 0U; unit_index < handle->control->unit_count; unit_index++) {
		sqlparser_control_unit_t *unit;
		char *core_sql;
		char *public_sql;
		size_t current_offset;
		size_t current_length;

		unit = &handle->control->units[unit_index];
		status = sqlparser_control_buffer_append(
			&buffer,
			handle->sql + source_offset,
			unit->source_offset - source_offset,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		core_sql = NULL;
		public_sql = NULL;
		if (unit->kind == SQLPARSER_CONTROL_UNIT_CONDITION) {
			PgQuery__Node *expression;

			expression = NULL;
			status = sqlparser_control_condition_expression(mutable_handle, unit_index, &expression, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_render_where_node_sql(expression, &core_sql, out_error);
			}
		} else {
			status = sqlparser_control_render_statement_core(handle, unit_index, &scratch, &core_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_control_postprocess_unit(
				handle,
				unit_index,
				unit->kind == SQLPARSER_CONTROL_UNIT_CONDITION,
				core_sql,
				&public_sql,
				out_error);
		}
		free(core_sql);
		current_offset = buffer.length;
		current_length = public_sql != NULL ? strlen(public_sql) : 0U;
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_control_buffer_append(
				&buffer,
				public_sql,
				current_length,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			unit->current_offset = current_offset;
			unit->current_length = current_length;
		}
		free(public_sql);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		source_offset = unit->source_offset + unit->source_length;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_control_buffer_append(
			&buffer,
			handle->sql + source_offset,
			handle->sql_len - source_offset,
			out_error);
	}
	free(scratch.data);
	if (status != SQLPARSER_STATUS_OK) {
		free(buffer.data);
		return status;
	}
	*out_sql = buffer.data;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_control_state_t *sqlparser_control_state_from_view(
	const sqlparser_control_flow_view_t *flow)
{
	const sqlparser_control_state_t *state;

	if (flow == NULL || flow->handle == NULL || flow->generation != flow->handle->generation) {
		return NULL;
	}
	state = flow->handle->control;
	if (state == NULL || flow->roots.offset != state->roots.offset || flow->roots.count != state->roots.count ||
	    flow->node_count != state->node_count || flow->branch_count != state->branch_count ||
	    flow->item_count != state->item_count) {
		return NULL;
	}
	return state;
}

sqlparser_status_t sqlparser_handle_control_flow(
	const sqlparser_handle_t *handle,
	sqlparser_control_flow_view_t *out_flow,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_flow == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_flow must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_flow, 0, sizeof(*out_flow));
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	out_flow->handle = handle;
	out_flow->generation = handle->generation;
	if (handle->control != NULL) {
		out_flow->roots = handle->control->roots;
		out_flow->node_count = handle->control->node_count;
		out_flow->branch_count = handle->control->branch_count;
		out_flow->item_count = handle->control->item_count;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_span_index_at(
	const sqlparser_control_flow_view_t *flow,
	sqlparser_index_span_t span,
	size_t item_index,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	const sqlparser_control_state_t *state;

	sqlparser_error_clear(out_error);
	if (out_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = 0U;
	state = sqlparser_control_state_from_view(flow);
	if (state == NULL || !sqlparser_control_span_is_valid(state, span) || item_index >= span.count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control span index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = state->index_pool[span.offset + item_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_node_at(
	const sqlparser_control_flow_view_t *flow,
	size_t node_index,
	sqlparser_control_node_t *out_node,
	sqlparser_error_t *out_error)
{
	const sqlparser_control_state_t *state;

	sqlparser_error_clear(out_error);
	if (out_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_node, 0, sizeof(*out_node));
	state = sqlparser_control_state_from_view(flow);
	if (state == NULL || node_index >= state->node_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control node index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = state->nodes[node_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_branch_at(
	const sqlparser_control_flow_view_t *flow,
	size_t branch_index,
	sqlparser_control_branch_t *out_branch,
	sqlparser_error_t *out_error)
{
	const sqlparser_control_state_t *state;

	sqlparser_error_clear(out_error);
	if (out_branch == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_branch must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_branch, 0, sizeof(*out_branch));
	state = sqlparser_control_state_from_view(flow);
	if (state == NULL || branch_index >= state->branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_branch = state->branches[branch_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_control_item_at(
	const sqlparser_control_flow_view_t *flow,
	size_t item_index,
	sqlparser_control_item_t *out_item,
	sqlparser_error_t *out_error)
{
	const sqlparser_control_state_t *state;

	sqlparser_error_clear(out_error);
	if (out_item == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_item must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_item, 0, sizeof(*out_item));
	state = sqlparser_control_state_from_view(flow);
	if (state == NULL || item_index >= state->item_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "control item index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_item = state->items[item_index];
	return SQLPARSER_STATUS_OK;
}
