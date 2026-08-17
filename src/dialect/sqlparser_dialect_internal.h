#ifndef SQLPARSER_DIALECT_INTERNAL_H
#define SQLPARSER_DIALECT_INTERNAL_H

#include "sqlparser_internal.h"
#include "sqlparser_identifier_origin_internal.h"

typedef struct {
	const char *name;
	size_t name_length;
	sqlparser_graph_session_value_kind_t kind;
	const char *text;
	size_t text_length;
	sqlparser_literal_view_t literal;
	const char *bind_key;
	size_t bind_key_length;
	sqlparser_bind_kind_t bind_kind;
	const char *bind_sql;
	size_t bind_sql_length;
	size_t bind_position;
	int has_bind_position;
	const char *source_sql;
	size_t source_offset;
} sqlparser_dialect_session_value_t;

typedef struct {
	void *context;
	sqlparser_status_t (*set_action)(
		void *context,
		sqlparser_graph_session_action_t action,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*add_item)(
		void *context,
		sqlparser_graph_session_scope_t scope,
		sqlparser_graph_session_target_kind_t target_kind,
		const char *name,
		size_t name_length,
		size_t *out_item_index,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*add_value)(
		void *context,
		size_t item_index,
		const sqlparser_dialect_session_value_t *value,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*add_ast_value)(
		void *context,
		size_t item_index,
		const char *name,
		const PgQuery__Node *node,
		sqlparser_error_t *out_error);
} sqlparser_dialect_session_emitter_t;

struct sqlparser_dialect_ops {
	sqlparser_dialect_t dialect;
	const char *name;
	sqlparser_status_t (*preprocess)(
		const char *input_sql,
		const sqlparser_limits_t *limits,
		char **out_parser_sql,
		void **out_state,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*preprocess_fragment)(
		const char *input_sql,
		void *state,
		size_t statement_index,
		char **out_parser_sql,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*postprocess_deparse)(
		const char *core_sql,
		const void *state,
		char **out_sql,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*clone_state)(
		const void *state,
		void **out_state,
		sqlparser_error_t *out_error);
	void (*destroy_state)(void *state);
	sqlparser_status_t (*postprocess_literal_fragment)(
		const char *core_sql,
		const void *state,
		size_t statement_index,
		const PgQuery__AConst *literal_owner,
		char **out_sql,
		sqlparser_error_t *out_error);
	const char *(*statement_keyword)(
		const void *state,
		size_t statement_index,
		const PgQuery__Node *statement);
	sqlparser_graph_insert_mode_t (*insert_mode)(
		const void *state,
		size_t statement_index,
		sqlparser_graph_insert_mode_t core_mode);
	const char *(*relation_object_name)(
		const void *state,
		const char *parser_object_name,
		const char **out_spelling);
	const char *(*relation_link_name)(
		const void *state,
		const char *parser_object_name);
	sqlparser_status_t (*postprocess_fragment)(
		const char *core_sql,
		const void *state,
		size_t statement_index,
		sqlparser_fragment_context_t fragment_context,
		ProtobufCMessage *const *roots,
		size_t root_count,
		char **out_sql,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*postprocess_control_unit)(
		const char *core_sql,
		const void *state,
		size_t statement_index,
		int is_condition,
		ProtobufCMessage *const *roots,
		size_t root_count,
		char **out_sql,
		sqlparser_error_t *out_error);
	sqlparser_control_state_t *(*take_control_state)(void *state);
	sqlparser_status_t (*project_session)(
		const sqlparser_handle_t *handle,
		const void *state,
		size_t statement_index,
		const PgQuery__Node *statement,
		const sqlparser_dialect_session_emitter_t *emitter,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*bind_ast_state)(
		void *state,
		const PgQuery__ParseResult *ast,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*bind_fragment_ast_state)(
		void *state,
		const PgQuery__ParseResult *base_ast,
		size_t statement_index,
		size_t parser_fragment_offset,
		ProtobufCMessage *const *roots,
		size_t root_count,
		sqlparser_error_t *out_error);
	void (*reconcile_ast_state)(
		void *state,
		const PgQuery__ParseResult *ast);
	sqlparser_status_t (*clone_ast_state)(
		void *state,
		size_t statement_index,
		const ProtobufCMessage *source_root,
		const ProtobufCMessage *clone_root,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*prepare_ast_state)(
		void *state,
		PgQuery__ParseResult *ast,
		sqlparser_error_t *out_error);
};

const sqlparser_dialect_ops_t *sqlparser_dialect_get_ops(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect);

const sqlparser_dialect_ops_t *sqlparser_dialect_postgresql_ops(void);
sqlparser_status_t sqlparser_postgresql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
const sqlparser_dialect_ops_t *sqlparser_dialect_mysql_ops(void);
sqlparser_status_t sqlparser_mysql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_mysql_preprocess_fragment_identifier_origins(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_mysql_dml_tail_select(
	const void *state,
	size_t statement_index,
	PgQuery__Node *const *returning_list,
	size_t returning_count,
	PgQuery__SelectStmt **out_select,
	sqlparser_error_t *out_error);
int sqlparser_mysql_statement_has_dml_join(
	const void *state,
	size_t statement_index);
int sqlparser_mysql_statement_update_join_reversed(
	const void *state,
	size_t statement_index);
int sqlparser_mysql_statement_update_join_multi_target(
	const void *state,
	size_t statement_index);
int sqlparser_mysql_statement_update_join_assignment_fallback(
	const void *state,
	size_t statement_index);
int sqlparser_mysql_reorient_replaced_update_join(
	void *state,
	size_t statement_index,
	PgQuery__UpdateStmt *stmt,
	const PgQuery__ResTarget *replacement);
int sqlparser_mysql_on_duplicate_name_surface(
	const void *state,
	size_t statement_index,
	const char *internal_qualifier,
	const char *current_name,
	const char *source_sql,
	size_t source_length,
	size_t source_start,
	const char *replacement_sql,
	const char **out_alias_prefix);
int sqlparser_mysql_public_sql_is_session_statement(
	const char *sql,
	size_t length);
const sqlparser_dialect_ops_t *sqlparser_dialect_oracle_ops(void);
sqlparser_status_t sqlparser_oracle_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_preprocess_fragment_identifier_origins(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
const sqlparser_dialect_ops_t *sqlparser_dialect_sqlserver_ops(void);
sqlparser_status_t sqlparser_sqlserver_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_sqlserver_preprocess_fragment_identifier_origins(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
sqlparser_status_t
sqlparser_vastbase_sqlserver_preprocess_fragment_identifier_origins(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
int sqlparser_sqlserver_generated_identifier_spelling(
	const char *identifier,
	const char **out_spelling,
	size_t *out_spelling_length);
const sqlparser_dialect_ops_t *sqlparser_dialect_dameng_ops(void);
int sqlparser_dameng_statement_multi_update_target_index(
	const void *state,
	size_t statement_index,
	size_t *out_index);
int sqlparser_dameng_statement_multi_update_join_condition_owner(
	const void *state,
	size_t statement_index,
	const PgQuery__FuncCall *owner);
int sqlparser_dameng_multi_update_target_name_slot(
	const void *state,
	size_t statement_index,
	char **slot);
sqlparser_status_t sqlparser_dameng_multi_update_relation_replaced(
	void *state,
	size_t statement_index,
	const PgQuery__RangeVar *relation,
	const char *const *values,
	const char *const *spellings,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_multi_update_public_where_slot(
	const void *state,
	size_t statement_index,
	PgQuery__Node **where_slot,
	int *out_is_join_carrier,
	PgQuery__Node ***out_public_slot,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_multi_update_insert_public_where(
	const void *state,
	size_t statement_index,
	PgQuery__Node **where_slot,
	PgQuery__Node *public_where,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_preprocess_multi_update_assignment_fragment(
	const char *input_sql,
	void *state,
	size_t statement_index,
	int target_only,
	char **out_parser_sql,
	sqlparser_error_t *out_error);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_oracle_ops(void);
sqlparser_status_t sqlparser_vastbase_oracle_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_mysql_ops(void);
sqlparser_status_t sqlparser_vastbase_mysql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_postgresql_ops(void);
sqlparser_status_t sqlparser_vastbase_postgresql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_sqlserver_ops(void);
sqlparser_status_t sqlparser_vastbase_sqlserver_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

int sqlparser_dialect_uses_postgresql_placeholders(sqlparser_dialect_t dialect);
int sqlparser_dialect_uses_oracle_placeholders(sqlparser_dialect_t dialect);
int sqlparser_dialect_uses_sqlserver_placeholders(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_oracle_compatible(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_oracle_or_dameng_compatible(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_mysql_compatible(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_sqlserver_compatible(sqlparser_dialect_t dialect);

sqlparser_status_t sqlparser_dialect_rewrite_like_escape(
	char **io_sql,
	sqlparser_error_t *out_error);

#endif
