#ifndef SQLPARSER_CONTROL_INTERNAL_H
#define SQLPARSER_CONTROL_INTERNAL_H

#include "sqlparser_internal.h"

enum {
	SQLPARSER_CONTROL_MAX_DEPTH = 32
};

typedef enum {
	SQLPARSER_CONTROL_UNIT_STATEMENT = 1,
	SQLPARSER_CONTROL_UNIT_CONDITION = 2
} sqlparser_control_unit_kind_t;

typedef struct {
	size_t root_count;
	size_t node_count;
	size_t branch_count;
	size_t item_count;
	size_t index_count;
	size_t unit_count;
} sqlparser_control_counts_t;

typedef struct {
	sqlparser_control_unit_kind_t kind;
	size_t ast_statement_index;
	size_t source_offset;
	size_t source_length;
} sqlparser_control_unit_t;

struct sqlparser_control_state {
	sqlparser_index_span_t roots;
	size_t node_count;
	size_t branch_count;
	size_t item_count;
	size_t index_count;
	size_t unit_count;
	sqlparser_control_node_t *nodes;
	sqlparser_control_branch_t *branches;
	sqlparser_control_item_t *items;
	size_t *index_pool;
	sqlparser_control_unit_t *units;
};

sqlparser_status_t sqlparser_control_state_allocate(
	const sqlparser_control_counts_t *counts,
	const sqlparser_limits_t *limits,
	sqlparser_control_state_t **out_state,
	sqlparser_error_t *out_error);

void sqlparser_control_state_release(sqlparser_control_state_t *state);

sqlparser_status_t sqlparser_control_state_clone(
	const sqlparser_control_state_t *source,
	const sqlparser_limits_t *limits,
	sqlparser_control_state_t **out_state,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_control_state_attach(
	sqlparser_handle_t *handle,
	sqlparser_control_state_t *state,
	sqlparser_error_t *out_error);

static inline int sqlparser_control_unit_is_condition(
	const sqlparser_handle_t *handle,
	size_t statement_index)
{
	return handle != NULL && handle->control != NULL &&
		statement_index < handle->control->unit_count &&
		handle->control->units[statement_index].kind == SQLPARSER_CONTROL_UNIT_CONDITION;
}

sqlparser_status_t sqlparser_control_statement_ast_index(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_ast_statement_index,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_control_condition_expression(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_expression,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_control_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_control_set_condition_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_control_build_public_sql(
	const sqlparser_handle_t *handle,
	char **out_sql,
	sqlparser_error_t *out_error);

#endif
