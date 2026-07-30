#ifndef SQLPARSER_INTERNAL_H
#define SQLPARSER_INTERNAL_H

#include <stddef.h>

#include "pg_query.h"
#include "protobuf/pg_query.pb-c.h"
#include "sqlparser/sqlparser.h"

typedef struct sqlparser_dialect_ops sqlparser_dialect_ops_t;
typedef struct sqlparser_query_graph_cache sqlparser_query_graph_cache_t;
typedef struct sqlparser_control_state sqlparser_control_state_t;

typedef struct {
	size_t raw_statement_index;
	size_t string_index;
	size_t relation_group;
	size_t source_component_index;
	char **slot;
	char *value;
	char *original;
	int has_source_component;
	int source_present;
} sqlparser_identifier_mutation_t;

#define SQLPARSER_INTERNAL_CURRENT_DATABASE "sqlparser_current_database"
#define SQLPARSER_INTERNAL_CURRENT_SCHEMA "sqlparser_current_schema"
#define SQLPARSER_INTERNAL_MYSQL_PREPARE "sqlparser_mysql_prepare"
#define SQLPARSER_INTERNAL_MYSQL_EXECUTE "sqlparser_mysql_execute"
#define SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE "sqlparser_mysql_deallocate_prepare"
#define SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE "sqlparser_mysql_drop_prepare"
#define SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT "sqlparser_mysql_session_statement"
#define SQLPARSER_INTERNAL_MYSQL_JOIN_ON "sqlparser_mysql_join_on"
#define SQLPARSER_INTERNAL_MYSQL_LEFT_JOIN_ON "sqlparser_mysql_left_join_on"
#define SQLPARSER_INTERNAL_MYSQL_RIGHT_JOIN_ON "sqlparser_mysql_right_join_on"
#define SQLPARSER_INTERNAL_MYSQL_CROSS_JOIN_ON "sqlparser_mysql_cross_join_on"
#define SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE "sqlparser_sqlserver_sp_prepare"
#define SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE "sqlparser_sqlserver_sp_execute"
#define SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC "sqlparser_sqlserver_sp_prepexec"
#define SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE "sqlparser_sqlserver_sp_unprepare"
#define SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL "sqlparser_sqlserver_sp_executesql"
#define SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE "sqlparser_sqlserver_create_application_role"
#define SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE "sqlparser_sqlserver_alter_application_role"
#define SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE "sqlparser_sqlserver_drop_application_role"
#define SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM "sqlparser_sqlserver_create_synonym"
#define SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM "sqlparser_sqlserver_drop_synonym"
#define SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE "sqlparser_sqlserver_create_type"
#define SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE "sqlparser_sqlserver_alter_database"
#define SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX "sqlparser_sqlserver_drop_index"
#define SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS "sqlparser_sqlserver_update_statistics"
#define SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT "sqlparser_sqlserver_set_statement"
#define SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT "sqlparser_sqlserver_execute_statement"
#define SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT "sqlparser_sqlserver_revert_statement"
#define SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT "sqlparser_sqlserver_setuser_statement"
#define SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER "sqlparser_sqlserver_create_user"
#define SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER "sqlparser_sqlserver_alter_user"
#define SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE "sqlparser_sqlserver_create_role"
#define SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE "sqlparser_sqlserver_alter_role"
#define SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA "sqlparser_sqlserver_alter_schema"
#define SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION "sqlparser_sqlserver_alter_authorization"
#define SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE "sqlparser_oracle_execute_immediate"
#define SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM "sqlparser_oracle_create_synonym"
#define SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM "sqlparser_oracle_drop_synonym"
#define SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN "sqlparser_oracle_explain_plan"
#define SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX "sqlparser_oracle_session_param_"
#define SQLPARSER_INTERNAL_ORACLE_SESSION_STATEMENT "sqlparser_oracle_session_statement"
#define SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX "sqlparser_dameng_session_param_"
#define SQLPARSER_INTERNAL_DAMENG_SESSION_STATEMENT "sqlparser_dameng_session_statement"
#define SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE "sqlparser_dameng_exec_sql_prepare"
#define SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE "sqlparser_dameng_exec_sql_execute"
#define SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE "sqlparser_dameng_exec_sql_deallocate_prepare"
#define SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT "sqlparser_vastbase_session_statement"
#define SQLPARSER_PROTO_LOCATION_GENERATED (-2)

struct sqlparser_handle {
	char *sql;
	char *parser_sql;
	char *current_sql;
	char *current_parser_sql;
	size_t sql_len;
	size_t parser_sql_len;
	size_t statement_count;
	PgQueryProtobuf parse_tree;
	PgQuery__ParseResult *ast;
	sqlparser_limits_t limits;
	unsigned long generation;
	sqlparser_dialect_t dialect;
	const sqlparser_dialect_ops_t *dialect_ops;
	void *dialect_state;
	sqlparser_query_graph_cache_t *query_graph;
	unsigned long query_graph_generation;
	sqlparser_control_state_t *control;
	sqlparser_identifier_mutation_t *identifier_mutations;
	size_t identifier_mutation_count;
	size_t identifier_mutation_capacity;
};

void sqlparser_error_clear(sqlparser_error_t *out_error);
void sqlparser_error_set_message(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *message);
void sqlparser_error_from_pg(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *sql,
	const PgQueryError *error);

char *sqlparser_strdup(const char *text);
char *sqlparser_strndup(const char *text, size_t len);
void sqlparser_pg_query_prepare(void);
void sqlparser_limits_normalize(
	const sqlparser_limits_t *limits,
	sqlparser_limits_t *out_limits);
sqlparser_status_t sqlparser_validate_text_limit(
	const char *text,
	size_t max_bytes,
	const char *field_name,
	size_t *out_len,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_validate_handle_sql_input(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_validate_handle_output_text(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_handle_ensure_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
void sqlparser_handle_clear_ast(sqlparser_handle_t *handle);
sqlparser_status_t sqlparser_handle_rebind_identifier_mutations(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
void sqlparser_handle_remove_identifier_mutation(
	sqlparser_handle_t *handle,
	size_t mutation_index);
void sqlparser_handle_invalidate_derived(sqlparser_handle_t *handle);
void sqlparser_query_graph_cache_release(sqlparser_query_graph_cache_t *cache);
void sqlparser_handle_clear_query_graph(sqlparser_handle_t *handle);
sqlparser_status_t sqlparser_handle_commit_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_handle_clone(
	const sqlparser_handle_t *source,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error);
void sqlparser_handle_replace_contents(
	sqlparser_handle_t *target,
	sqlparser_handle_t *source);
sqlparser_status_t sqlparser_ensure_current_sql_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
PgQueryDeparseResult sqlparser_deparse_protobuf_for_handle(
	const sqlparser_handle_t *handle,
	PgQueryProtobuf parse_tree,
	size_t raw_statement_offset,
	size_t source_start,
	size_t source_end);
PgQueryProtobufParseResult
sqlparser_parse_protobuf_preserving_identifier_spelling(
	const char *parser_sql);
sqlparser_status_t sqlparser_validate_ast_identifier_spelling(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
const char *sqlparser_effective_sql(const sqlparser_handle_t *handle);
const char *sqlparser_effective_parser_sql(const sqlparser_handle_t *handle);
sqlparser_status_t sqlparser_postprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *core_sql,
	const char *field_name,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_preprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *public_sql,
	const char *field_name,
	char **out_parser_sql,
	void **out_dialect_state,
	sqlparser_error_t *out_error);
void sqlparser_handle_discard_dialect_state(
	const sqlparser_handle_t *handle,
	void *state);
void sqlparser_handle_adopt_dialect_state(
	sqlparser_handle_t *handle,
	void *state);
const char *sqlparser_dialect_relation_object_name(
	const sqlparser_dialect_ops_t *ops,
	const void *state,
	const char *parser_object_name);
const char *sqlparser_dialect_relation_link_name(
	const sqlparser_dialect_ops_t *ops,
	const void *state,
	const char *parser_object_name);

#endif
