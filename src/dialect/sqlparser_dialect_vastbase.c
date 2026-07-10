#include "sqlparser_dialect_internal.h"

static sqlparser_status_t sqlparser_vastbase_preprocess_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->preprocess == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase dialect mode is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->preprocess(input_sql, limits, out_parser_sql, out_state, out_error);
}

static sqlparser_status_t sqlparser_vastbase_preprocess_fragment_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *input_sql,
	void *state,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->preprocess_fragment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase SQL fragment is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->preprocess_fragment(input_sql, state, out_parser_sql, out_error);
}

static sqlparser_status_t sqlparser_vastbase_postprocess_deparse_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->postprocess_deparse == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase deparse is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->postprocess_deparse(core_sql, state, out_sql, out_error);
}

static sqlparser_status_t sqlparser_vastbase_postprocess_fragment_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase SQL fragment is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (base_ops->postprocess_fragment != NULL) {
		return base_ops->postprocess_fragment(core_sql, state, out_sql, out_error);
	}
	if (base_ops->postprocess_deparse != NULL) {
		return base_ops->postprocess_deparse(core_sql, state, out_sql, out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase SQL fragment is not supported");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_vastbase_clone_state_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->clone_state == NULL) {
		if (out_state == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_state = NULL;
		return SQLPARSER_STATUS_OK;
	}
	return base_ops->clone_state(state, out_state, out_error);
}

static void sqlparser_vastbase_destroy_state_delegate(const sqlparser_dialect_ops_t *base_ops, void *state)
{
	if (base_ops != NULL && base_ops->destroy_state != NULL) {
		base_ops->destroy_state(state);
	}
}

static sqlparser_status_t sqlparser_vastbase_postprocess_literal_fragment_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *core_sql,
	const void *state,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->postprocess_literal_fragment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase literal fragment is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->postprocess_literal_fragment(core_sql, state, literal_index, out_sql, out_error);
}

static const char *sqlparser_vastbase_statement_keyword_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement)
{
	if (base_ops == NULL || base_ops->statement_keyword == NULL) {
		return NULL;
	}
	return base_ops->statement_keyword(state, statement_index, statement);
}

static sqlparser_graph_insert_mode_t sqlparser_vastbase_insert_mode_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	size_t statement_index,
	sqlparser_graph_insert_mode_t core_mode)
{
	if (base_ops == NULL || base_ops->insert_mode == NULL) {
		return core_mode;
	}
	return base_ops->insert_mode(state, statement_index, core_mode);
}

static const char *sqlparser_vastbase_relation_object_name_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	const char *parser_object_name)
{
	if (base_ops == NULL || base_ops->relation_object_name == NULL) {
		return NULL;
	}
	return base_ops->relation_object_name(state, parser_object_name);
}

static const char *sqlparser_vastbase_relation_link_name_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	const char *parser_object_name)
{
	if (base_ops == NULL || base_ops->relation_link_name == NULL) {
		return NULL;
	}
	return base_ops->relation_link_name(state, parser_object_name);
}

#define SQLPARSER_DEFINE_VASTBASE_PRE_POST(TAG, BASE_OPS_FN) \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_preprocess( \
		const char *input_sql, \
		const sqlparser_limits_t *limits, \
		char **out_parser_sql, \
		void **out_state, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_preprocess_delegate( \
			BASE_OPS_FN(), \
			input_sql, \
			limits, \
			out_parser_sql, \
			out_state, \
			out_error); \
	} \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_postprocess_deparse( \
		const char *core_sql, \
		const void *state, \
		char **out_sql, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_postprocess_deparse_delegate( \
			BASE_OPS_FN(), \
			core_sql, \
			state, \
			out_sql, \
			out_error); \
	} \
	static const char *sqlparser_vastbase_##TAG##_statement_keyword( \
		const void *state, \
		size_t statement_index, \
		const PgQuery__Node *statement) \
	{ \
		return sqlparser_vastbase_statement_keyword_delegate( \
			BASE_OPS_FN(), \
			state, \
			statement_index, \
			statement); \
	} \
	static sqlparser_graph_insert_mode_t sqlparser_vastbase_##TAG##_insert_mode( \
		const void *state, \
		size_t statement_index, \
		sqlparser_graph_insert_mode_t core_mode) \
	{ \
		return sqlparser_vastbase_insert_mode_delegate( \
			BASE_OPS_FN(), \
			state, \
			statement_index, \
			core_mode); \
	} \
	static const char *sqlparser_vastbase_##TAG##_relation_object_name( \
		const void *state, \
		const char *parser_object_name) \
	{ \
		return sqlparser_vastbase_relation_object_name_delegate( \
			BASE_OPS_FN(), \
			state, \
			parser_object_name); \
	} \
	static const char *sqlparser_vastbase_##TAG##_relation_link_name( \
		const void *state, \
		const char *parser_object_name) \
	{ \
		return sqlparser_vastbase_relation_link_name_delegate( \
			BASE_OPS_FN(), \
			state, \
			parser_object_name); \
	}

#define SQLPARSER_DEFINE_VASTBASE_STATEFUL(TAG, BASE_OPS_FN) \
	SQLPARSER_DEFINE_VASTBASE_PRE_POST(TAG, BASE_OPS_FN) \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_preprocess_fragment( \
		const char *input_sql, \
		void *state, \
		char **out_parser_sql, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_preprocess_fragment_delegate( \
			BASE_OPS_FN(), \
			input_sql, \
			state, \
			out_parser_sql, \
			out_error); \
	} \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_clone_state( \
		const void *state, \
		void **out_state, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_clone_state_delegate(BASE_OPS_FN(), state, out_state, out_error); \
	} \
	static void sqlparser_vastbase_##TAG##_destroy_state(void *state) \
	{ \
		sqlparser_vastbase_destroy_state_delegate(BASE_OPS_FN(), state); \
	}

SQLPARSER_DEFINE_VASTBASE_STATEFUL(oracle, sqlparser_dialect_oracle_ops)
SQLPARSER_DEFINE_VASTBASE_STATEFUL(mysql, sqlparser_dialect_mysql_ops)
SQLPARSER_DEFINE_VASTBASE_STATEFUL(postgresql, sqlparser_dialect_postgresql_ops)
SQLPARSER_DEFINE_VASTBASE_STATEFUL(sqlserver, sqlparser_dialect_sqlserver_ops)

static sqlparser_status_t sqlparser_vastbase_oracle_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_oracle_ops(),
		core_sql,
		state,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_mysql_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_mysql_ops(),
		core_sql,
		state,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_postgresql_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_postgresql_ops(),
		core_sql,
		state,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_sqlserver_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_sqlserver_ops(),
		core_sql,
		state,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_sqlserver_postprocess_fragment(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_fragment_delegate(
		sqlparser_dialect_sqlserver_ops(),
		core_sql,
		state,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_mysql_postprocess_fragment(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_fragment_delegate(
		sqlparser_dialect_mysql_ops(),
		core_sql,
		state,
		out_sql,
		out_error);
}

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_ORACLE_OPS = {
	SQLPARSER_DIALECT_VASTBASE_ORACLE,
	"vastbase-oracle",
	sqlparser_vastbase_oracle_preprocess,
	sqlparser_vastbase_oracle_preprocess_fragment,
	sqlparser_vastbase_oracle_postprocess_deparse,
	sqlparser_vastbase_oracle_clone_state,
	sqlparser_vastbase_oracle_destroy_state,
	sqlparser_vastbase_oracle_postprocess_literal_fragment,
	sqlparser_vastbase_oracle_statement_keyword,
	sqlparser_vastbase_oracle_insert_mode,
	sqlparser_vastbase_oracle_relation_object_name,
	sqlparser_vastbase_oracle_relation_link_name,
	NULL
};

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_MYSQL_OPS = {
	SQLPARSER_DIALECT_VASTBASE_MYSQL,
	"vastbase-mysql",
	sqlparser_vastbase_mysql_preprocess,
	sqlparser_vastbase_mysql_preprocess_fragment,
	sqlparser_vastbase_mysql_postprocess_deparse,
	sqlparser_vastbase_mysql_clone_state,
	sqlparser_vastbase_mysql_destroy_state,
	sqlparser_vastbase_mysql_postprocess_literal_fragment,
	sqlparser_vastbase_mysql_statement_keyword,
	sqlparser_vastbase_mysql_insert_mode,
	sqlparser_vastbase_mysql_relation_object_name,
	sqlparser_vastbase_mysql_relation_link_name,
	sqlparser_vastbase_mysql_postprocess_fragment
};

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_POSTGRESQL_OPS = {
	SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
	"vastbase-postgresql",
	sqlparser_vastbase_postgresql_preprocess,
	sqlparser_vastbase_postgresql_preprocess_fragment,
	sqlparser_vastbase_postgresql_postprocess_deparse,
	sqlparser_vastbase_postgresql_clone_state,
	sqlparser_vastbase_postgresql_destroy_state,
	sqlparser_vastbase_postgresql_postprocess_literal_fragment,
	sqlparser_vastbase_postgresql_statement_keyword,
	sqlparser_vastbase_postgresql_insert_mode,
	sqlparser_vastbase_postgresql_relation_object_name,
	sqlparser_vastbase_postgresql_relation_link_name,
	NULL
};

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_SQLSERVER_OPS = {
	SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
	"vastbase-sqlserver",
	sqlparser_vastbase_sqlserver_preprocess,
	sqlparser_vastbase_sqlserver_preprocess_fragment,
	sqlparser_vastbase_sqlserver_postprocess_deparse,
	sqlparser_vastbase_sqlserver_clone_state,
	sqlparser_vastbase_sqlserver_destroy_state,
	sqlparser_vastbase_sqlserver_postprocess_literal_fragment,
	sqlparser_vastbase_sqlserver_statement_keyword,
	sqlparser_vastbase_sqlserver_insert_mode,
	sqlparser_vastbase_sqlserver_relation_object_name,
	sqlparser_vastbase_sqlserver_relation_link_name,
	sqlparser_vastbase_sqlserver_postprocess_fragment
};

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_oracle_ops(void)
{
	return &SQLPARSER_VASTBASE_ORACLE_OPS;
}

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_mysql_ops(void)
{
	return &SQLPARSER_VASTBASE_MYSQL_OPS;
}

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_postgresql_ops(void)
{
	return &SQLPARSER_VASTBASE_POSTGRESQL_OPS;
}

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_sqlserver_ops(void)
{
	return &SQLPARSER_VASTBASE_SQLSERVER_OPS;
}
