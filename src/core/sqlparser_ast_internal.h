#ifndef SQLPARSER_AST_INTERNAL_H
#define SQLPARSER_AST_INTERNAL_H

#include <stddef.h>

#include "sqlparser_internal.h"
#include "sqlparser_control_internal.h"

typedef struct {
	const char *table_name;
	const char *column_name;
	const char *operator_name;
} sqlparser_predicate_context_t;

typedef struct {
	const char *public_sql;
	size_t public_sql_length;
	size_t parser_sql_length;
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_dialect_t dialect;
	sqlparser_handle_t *spelling_handle;
	void *candidate_dialect_state;
	size_t statement_index;
} sqlparser_generated_source_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__AConst *literal_node;
	const char *parser_sql;
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
	ProtobufCMessage *field_owner;
	ProtobufCMessage *location_owner;
	int identifier_forbidden;
} sqlparser_name_context_t;

typedef struct {
	size_t resume;
	size_t search_position;
	size_t last_location;
	int valid;
} sqlparser_view_expression_source_cache_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	char **match_slot;
	char **target_slot;
	ProtobufCMessage *target_owner;
	ProtobufCMessage *target_field_owner;
	sqlparser_name_view_t *name_view;
} sqlparser_name_search_t;

int sqlparser_name_atom_is_identifier(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field);
sqlparser_name_context_t sqlparser_next_name_context(
	ProtobufCMessage *message,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	const sqlparser_name_context_t *context);

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	char **match_slot;
	char *match_value;
	char **target_slot;
} sqlparser_string_search_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__Node *match_node;
	PgQuery__AConst *match_a_const;
	PgQuery__Node **target_slot;
	ProtobufCMessage *target_parent;
	PgQuery__SelectStmt *dml_tail_select;
} sqlparser_node_slot_search_t;

int sqlparser_node_is_mysql_dml_tail_wrapper(
	const PgQuery__Node *node,
	const PgQuery__SelectStmt *tail_select);

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__Node **target_slot;
	PgQuery__SelectStmt *dml_tail_select;
} sqlparser_where_clause_search_t;

static inline int sqlparser_a_indirection_is_grouping(
	const PgQuery__AIndirection *indirection)
{
	return indirection != NULL && indirection->n_indirection == 0U;
}

static inline int sqlparser_node_is_grouping_wrapper(const PgQuery__Node *node)
{
	return node != NULL &&
	       node->node_case == PG_QUERY__NODE__NODE_A_INDIRECTION &&
	       sqlparser_a_indirection_is_grouping(node->a_indirection);
}

static inline const PgQuery__Node *sqlparser_unwrap_grouping_node_const(
	const PgQuery__Node *node)
{
	while (sqlparser_node_is_grouping_wrapper(node)) {
		node = node->a_indirection->arg;
	}
	return node;
}

static inline PgQuery__Node *sqlparser_unwrap_grouping_node(PgQuery__Node *node)
{
	return (PgQuery__Node *)sqlparser_unwrap_grouping_node_const(node);
}

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
	PgQuery__SelectStmt *dml_tail_select;
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
sqlparser_status_t sqlparser_get_mysql_dml_tail_select(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *statement,
	PgQuery__SelectStmt **out_select,
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
void sqlparser_fill_relation_view_for_handle(
	const sqlparser_handle_t *handle,
	const PgQuery__RangeVar *relation,
	sqlparser_relation_view_t *out_relation);
sqlparser_status_t sqlparser_fill_literal_view_from_a_const(
	const PgQuery__AConst *a_const,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_fill_literal_view_from_a_const_with_sql(
	const PgQuery__AConst *a_const,
	const char *parser_sql,
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
sqlparser_status_t sqlparser_walk_message_strings(
	ProtobufCMessage *message,
	sqlparser_string_search_t *search);
sqlparser_status_t sqlparser_find_raw_statement_string_index_by_slot(
	sqlparser_handle_t *handle,
	size_t raw_statement_index,
	char **slot,
	size_t *out_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_raw_statement_string_slot_by_index(
	PgQuery__ParseResult *ast,
	size_t raw_statement_index,
	size_t string_index,
	char ***out_slot);
sqlparser_status_t sqlparser_handle_prepare_identifier_mutation(
	sqlparser_handle_t *handle,
	size_t statement_index,
	char **slot,
	ProtobufCMessage *location_owner,
	size_t *out_mutation_index,
	int *out_created,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_set_name_spelling(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	const char *spelling,
	sqlparser_error_t *out_error);
typedef struct {
	PgQuery__ColumnRef **column_refs;
	size_t column_ref_count;
	PgQuery__ResTarget **assignment_targets;
	size_t assignment_target_count;
} sqlparser_relation_bindings_t;
void sqlparser_relation_bindings_clear(
	sqlparser_relation_bindings_t *bindings);
sqlparser_status_t sqlparser_replace_relation_and_bound_qualifiers(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	PgQuery__RangeVar *duplicate_relation,
	const sqlparser_relation_bindings_t *bindings,
	const char *const *values,
	const char *const *spellings,
	void *dialect_state,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_set_relation_name_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	size_t source_encoding,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_relation_patch_source_encoding(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	size_t *out_source_encoding,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_collect_relation_bindings(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	sqlparser_relation_bindings_t *out_bindings,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_find_duplicate_delete_relation(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *target,
	PgQuery__RangeVar **out_primary,
	PgQuery__RangeVar **out_duplicate,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_find_statement_node_index_by_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *node,
	size_t *out_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_find_statement_a_const_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__AConst *a_const,
	PgQuery__Node **out_node,
	size_t *out_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_statement_node_slot_by_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t node_index,
	PgQuery__Node ***out_slot,
	ProtobufCMessage **out_parent,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_find_statement_literal_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	int where_only,
	PgQuery__Node **out_node,
	size_t *out_node_index,
	sqlparser_error_t *out_error);

int sqlparser_node_string_value(const PgQuery__Node *node, const char **out_text);
PgQuery__Node *sqlparser_alloc_string_node(
	const char *text,
	sqlparser_error_t *out_error);
int sqlparser_try_extract_column_ref(
	const PgQuery__Node *node,
	const char **table_name_out,
	const char **column_name_out);
int sqlparser_a_expr_is_not_in(const PgQuery__AExpr *a_expr);
int sqlparser_a_expr_is_not_like(const PgQuery__AExpr *a_expr);
int sqlparser_a_expr_is_not_ilike(const PgQuery__AExpr *a_expr);
int sqlparser_a_expr_is_not_similar(const PgQuery__AExpr *a_expr);
const char *sqlparser_a_expr_operator_name(const PgQuery__AExpr *a_expr);

sqlparser_status_t sqlparser_replace_proto_string(
	char **slot,
	const char *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_a_const_set_literal(
	PgQuery__AConst *a_const,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_bind_value_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_bind_value_t *bind,
	char **out_sql,
	sqlparser_error_t *out_error);
void sqlparser_free_proto_node(PgQuery__Node *node);
int32_t *sqlparser_proto_location_slot(ProtobufCMessage *message);
int sqlparser_identifier_is_sysdate(const char *text);
int sqlparser_proto_location_is_generated(int32_t location);
sqlparser_proto_identifier_style_t sqlparser_proto_identifier_style(
	int32_t location,
	size_t component_index);
void sqlparser_mark_proto_generated(ProtobufCMessage *message);
sqlparser_status_t sqlparser_mark_proto_generated_with_fragment_source(
	ProtobufCMessage *message,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_mark_proto_nodes_generated_with_fragment_source(
	PgQuery__Node *const *nodes,
	size_t count,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_mark_proto_generated_from_handle(
	const sqlparser_handle_t *handle,
	ProtobufCMessage *message,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_identifier_origins_for_handle(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_resolve_identifier_component_spelling(
	const sqlparser_handle_t *handle,
	int32_t location,
	size_t component_index,
	const char *identifier,
	char **out_spelling,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_default_identifier_spelling(
	const char *identifier,
	char **out_spelling,
	sqlparser_error_t *out_error);
int sqlparser_source_alias_has_explicit_as(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t length,
	size_t alias_offset);
sqlparser_status_t sqlparser_res_target_alias_style(
	const sqlparser_handle_t *handle,
	const PgQuery__ResTarget *target,
	int *out_known,
	int *out_explicit_as,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_clone_proto_node(
	const PgQuery__Node *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_identifier_path_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_path_view_t *path,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_selector_apply_single_patch(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_build_update_assignment_identifier_node(
	const sqlparser_identifier_path_view_t *target,
	PgQuery__Node *value_node,
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
	const sqlparser_handle_t *handle,
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
	const sqlparser_generated_source_t *source,
	PgQuery__Node **out_node,
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
sqlparser_status_t sqlparser_assignment_by_selector(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_set_literal_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_value_node_index_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t *out_node_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_set_literal_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	int where_only,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_sql_by_selector(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_set_sql_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_insert_sql_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_insert_from_assignment_value_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *insert_selector,
	const sqlparser_identifier_path_view_t *target,
	const sqlparser_selector_t *source_selector,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_delete_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_assignment_set_full_sql_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_select_target_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_select_set_targets_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_select_set_target_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_select_insert_target_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_select_delete_target_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_insert_set_cell_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_insert_set_cell_literal_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_where_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_set_where_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_append_where_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_set_clause_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_statement_append_clause_condition_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_update_assignment_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_parse_variable_set_arg_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_insert_cell_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
int sqlparser_view_insert_cell_source_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error);
int sqlparser_view_insert_cell_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_surface_source_edits_t *surface_edits,
	sqlparser_view_expression_source_cache_t *cache,
	int allow_comments,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error);
int sqlparser_merge_condition_source_span(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	sqlparser_selector_kind_t role,
	const char **out_source_sql,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_merge_branch_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_merge_delete_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t when_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_merge_condition_set_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_select_target_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_where_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_update_assignment_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_render_variable_set_arg_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dml_result_target_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_dml_result_message(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_graph_dml_kind_t *out_kind,
	ProtobufCMessage **out_message,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_get_merge_stmt_by_dml_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	PgQuery__MergeStmt **out_stmt,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_set_target_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_insert_target_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_delete_target(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t target_index,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_set_sink_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_set_sink_column_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_insert_sink_column_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dml_result_delete_sink_column(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t column_index,
	sqlparser_error_t *out_error);

sqlparser_value_kind_t sqlparser_node_value_kind(const PgQuery__Node *node);

#endif
