#ifndef SQLPARSER_DIALECT_INTERNAL_H
#define SQLPARSER_DIALECT_INTERNAL_H

#include "sqlparser_internal.h"

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
		size_t literal_index,
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
		const char *parser_object_name);
	const char *(*relation_link_name)(
		const void *state,
		const char *parser_object_name);
	sqlparser_status_t (*postprocess_fragment)(
		const char *core_sql,
		const void *state,
		char **out_sql,
		sqlparser_error_t *out_error);
};

const sqlparser_dialect_ops_t *sqlparser_dialect_get_ops(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect);

const sqlparser_dialect_ops_t *sqlparser_dialect_postgresql_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_mysql_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_oracle_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_sqlserver_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_dameng_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_oracle_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_mysql_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_postgresql_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_sqlserver_ops(void);

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
