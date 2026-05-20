#ifndef SQLPARSER_AST_INTERNAL_H
#define SQLPARSER_AST_INTERNAL_H

#include <stddef.h>

#include "sqlparser_internal.h"

typedef struct {
	const char *table_name;
	const char *column_name;
	const char *operator_name;
} sqlparser_predicate_context_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__AConst *literal_node;
	sqlparser_where_literal_view_t *literal_view;
} sqlparser_where_literal_search_t;

typedef struct {
	const ProtobufCMessageDescriptor *descriptor;
	size_t seen;
	size_t target_index;
	int want_target;
	ProtobufCMessage *target_message;
	int (*accept)(ProtobufCMessage *message);
} sqlparser_message_search_t;

typedef struct {
	const char *owner_type;
	const char *field_name;
} sqlparser_name_context_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	char **match_slot;
	char **target_slot;
	sqlparser_name_view_t *name_view;
} sqlparser_name_search_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__Node *match_node;
	PgQuery__Node **target_slot;
} sqlparser_node_slot_search_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__Node **target_slot;
} sqlparser_where_clause_search_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	sqlparser_clause_kind_t target_kind;
	size_t target_internal_index;
	PgQuery__SelectStmt *target_select_stmt;
	size_t select_list_seen;
	size_t where_seen;
	size_t order_by_seen;
	size_t set_list_seen;
	size_t set_operand_depth;
} sqlparser_clause_search_t;

void sqlparser_relation_view_clear(sqlparser_relation_view_t *view);
void sqlparser_literal_view_clear(sqlparser_literal_view_t *view);
void sqlparser_assignment_view_clear(sqlparser_assignment_view_t *view);
void sqlparser_where_literal_view_clear(sqlparser_where_literal_view_t *view);
void sqlparser_name_view_clear(sqlparser_name_view_t *view);

const char *sqlparser_statement_node_name_from_case(PgQuery__Node__NodeCase node_case);
sqlparser_statement_kind_t sqlparser_statement_kind_from_case(PgQuery__Node__NodeCase node_case);

sqlparser_status_t sqlparser_get_statement_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_statement,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_insert_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__InsertStmt **out_stmt,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_update_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__UpdateStmt **out_stmt,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_select_stmt_by_target_list_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	PgQuery__SelectStmt **out_stmt,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_find_select_target_list_index_by_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__SelectStmt *stmt,
	size_t *out_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_statement_where_clause(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_where_clause,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_statement_where_clause_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_count_statement_where_clauses(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_search_statement_messages(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const ProtobufCMessageDescriptor *descriptor,
	int (*accept)(ProtobufCMessage *message),
	int want_target,
	size_t target_index,
	size_t *out_count,
	ProtobufCMessage **out_message,
	sqlparser_error_t *out_error);

sqlparser_insert_source_kind_t sqlparser_insert_source_from_stmt(PgQuery__InsertStmt *stmt);
sqlparser_status_t sqlparser_get_insert_values_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__InsertStmt **out_insert_stmt,
	PgQuery__SelectStmt **out_values_stmt,
	sqlparser_error_t *out_error);

void sqlparser_fill_relation_view(
	const PgQuery__RangeVar *relation,
	sqlparser_relation_view_t *out_relation);
sqlparser_status_t sqlparser_fill_literal_view_from_a_const(
	const PgQuery__AConst *a_const,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_walk_message_tree(
	ProtobufCMessage *message,
	sqlparser_message_search_t *search,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_walk_message_names(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_name_search_t *search);
sqlparser_status_t sqlparser_find_statement_name_index_by_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	char **slot,
	size_t *out_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_find_statement_node_index_by_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *node,
	size_t *out_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_statement_node_slot_by_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t node_index,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error);

int sqlparser_node_string_value(const PgQuery__Node *node, const char **out_text);
int sqlparser_try_extract_column_ref(
	const PgQuery__Node *node,
	const char **table_name_out,
	const char **column_name_out);
const char *sqlparser_a_expr_operator_name(const PgQuery__AExpr *a_expr);

sqlparser_status_t sqlparser_replace_proto_string(
	char **slot,
	const char *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_a_const_set_literal(
	PgQuery__AConst *a_const,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
void sqlparser_free_proto_node(PgQuery__Node *node);
sqlparser_status_t sqlparser_clone_proto_node(
	const PgQuery__Node *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_build_wrapped_sql(
	const char *prefix,
	const char *sql_text,
	const char *suffix,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_wrapper_ast(
	const char *wrapped_sql,
	PgQuery__ParseResult **out_ast,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_deparse_wrapper_ast(
	const PgQuery__ParseResult *ast,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_extract_wrapped_value_sql(
	const char *wrapped_sql,
	const char *prefix,
	const char *suffix,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_parse_insert_cell_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_select_target_nodes_sql(
	const char *sql_text,
	PgQuery__Node ***out_nodes,
	size_t *out_count,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_select_targets_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_update_assignments_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_update_set_assignments_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_select_target_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_where_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_update_assignment_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_variable_set_arg_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_insert_cell_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_select_target_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_where_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_update_assignment_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_variable_set_arg_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_value_kind_t sqlparser_node_value_kind(const PgQuery__Node *node);

#endif
