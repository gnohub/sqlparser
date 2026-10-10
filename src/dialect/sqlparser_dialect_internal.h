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
	const char *(*relation_link_sql)(
		const void *state,
		const char *parser_object_name);
	/* Static grammar/validation capabilities are independent of retained
	 * state. A successful state predicate is still required before reusing
	 * source/wire provenance or committing literal-only changes. A non-NULL
	 * predicate enables strict wire admission without opting into native
	 * validation or changing control-aware initial parsing. */
	int plain_scalar_native_validation;
	int plain_ascii_string_fragments;
	int (*state_is_plain_insert_strings)(const void *state, size_t string_count);
};

const sqlparser_dialect_ops_t *sqlparser_dialect_get_ops(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect);

/* Private scalar pipeline admission. The dynamic predicate dispatches only
 * through the exact registered owner and requires unchanged parser SQL. */
int sqlparser_dialect_supports_plain_scalar_insert(const sqlparser_handle_t *handle);
int sqlparser_dialect_state_is_plain_insert_strings(
	const sqlparser_handle_t *handle, size_t string_count);
/* Exact-owner plain batch state predicate; not native/wire provenance. */
int sqlparser_dialect_state_is_plain_insert_batch_strings(
	const sqlparser_handle_t *handle, size_t statement_count, size_t string_count);
int sqlparser_sqlserver_state_is_plain_insert_batch_strings(
	const void *state, size_t statement_count, size_t string_count);
int sqlparser_vastbase_sqlserver_state_is_plain_insert_batch_strings(
	const void *state, size_t statement_count, size_t string_count);

/* Separate, initial-parse-only capability. The callback returns scalar source
 * metadata from its actual identity-preprocessing pass, with the parser text
 * and state it just created. A zero record declines certification. No caller
 * pointer, proof, or additional allocation is retained in the state/handle.
 * The proof is NOT native-tree or wire provenance: the core still requires
 * exact source/parser bytes, the registered owner's state predicate, an
 * independently complete native constructor (or ordinary grammar fallback),
 * and successful canonical writer certification. Batch metadata is separate
 * from singleton metadata and can never authorize singleton provenance. */
/* Fresh exact base-MySQL initial parse only; never retained in state. */
sqlparser_status_t sqlparser_mysql_preprocess_with_native_plan(
    const char *input_sql, size_t input_length, const sqlparser_limits_t *limits,
    char **out_parser_sql, void **out_state,
    PgQueryMysqlOwnedScalarInsertPlan *plan, sqlparser_error_t *out_error);

typedef struct sqlparser_identity_insert_batch_proof {
    size_t source_length;
    size_t statement_count;
    size_t string_count;
} sqlparser_identity_insert_batch_proof_t;
typedef sqlparser_status_t (*sqlparser_validation_preprocess_fn)(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	PgQueryIdentityScalarInsertProof *out_proof,
	sqlparser_identity_insert_batch_proof_t *out_batch_proof,
	sqlparser_error_t *out_error);
sqlparser_validation_preprocess_fn sqlparser_dialect_validation_preprocessor(
	sqlparser_dialect_t dialect, const sqlparser_dialect_ops_t *ops);
sqlparser_status_t sqlparser_dameng_preprocess_validation_proof(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	PgQueryIdentityScalarInsertProof *out_proof,
	sqlparser_identity_insert_batch_proof_t *out_batch_proof,
	sqlparser_error_t *out_error);
/* Internal replacement parses must not regain fresh Dameng proof admission. */
sqlparser_status_t sqlparser_dameng_parse_legacy_replacement(
	const char *sql,
	const sqlparser_parse_options_t *options,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_sqlserver_preprocess_validation_proof(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	PgQueryIdentityScalarInsertProof *out_proof,
	sqlparser_identity_insert_batch_proof_t *out_batch_proof,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_vastbase_sqlserver_preprocess_validation_proof(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	PgQueryIdentityScalarInsertProof *out_proof,
	sqlparser_identity_insert_batch_proof_t *out_batch_proof,
	sqlparser_error_t *out_error);

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
int sqlparser_mysql_state_is_plain_insert_strings(
	const void *state, size_t string_count);
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
const sqlparser_dialect_ops_t *sqlparser_dialect_kingbase_oracle_ops(void);
sqlparser_status_t sqlparser_kingbase_oracle_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
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
/* Exact wrapper admission and its actual Oracle-mode outer rewrite contract. */
sqlparser_status_t sqlparser_vastbase_oracle_multi_insert_identity_input(
	const sqlparser_handle_t *handle,
	const char *sql,
	int *out_identity,
	sqlparser_error_t *out_error);
/* Returns an owned origin map only for a fresh, exactly owned Oracle
 * multi-insert. Ineligible handles return OK with a NULL map. */
sqlparser_status_t sqlparser_vastbase_oracle_try_replay_identifier_origins(
	const sqlparser_handle_t *handle,
	sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error);
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
int sqlparser_dialect_supports_postgresql_dml_results(sqlparser_dialect_t dialect);
int sqlparser_dialect_uses_oracle_placeholders(sqlparser_dialect_t dialect);
int sqlparser_dialect_uses_sqlserver_placeholders(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_oracle_compatible(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_oracle_or_dameng_compatible(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_mysql_compatible(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_sqlserver_compatible(sqlparser_dialect_t dialect);

const char *sqlparser_dialect_relation_link_sql(
	const sqlparser_dialect_ops_t *ops,
	const void *state,
	const char *parser_object_name);

sqlparser_status_t sqlparser_dialect_rewrite_like_escape(
	char **io_sql,
	sqlparser_error_t *out_error);

#endif
