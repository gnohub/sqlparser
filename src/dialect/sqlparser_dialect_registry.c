#include <string.h>

#include "sqlparser_dialect_internal.h"

const sqlparser_dialect_ops_t *sqlparser_dialect_get_ops(sqlparser_dialect_t dialect)
{
	switch (dialect) {
		case SQLPARSER_DIALECT_POSTGRESQL:
			return sqlparser_dialect_postgresql_ops();
		case SQLPARSER_DIALECT_MYSQL:
			return sqlparser_dialect_mysql_ops();
		case SQLPARSER_DIALECT_ORACLE:
			return sqlparser_dialect_oracle_ops();
		case SQLPARSER_DIALECT_SQLSERVER:
			return sqlparser_dialect_sqlserver_ops();
		case SQLPARSER_DIALECT_DAMENG:
			return sqlparser_dialect_dameng_ops();
		case SQLPARSER_DIALECT_VASTBASE_ORACLE:
			return sqlparser_dialect_vastbase_oracle_ops();
		case SQLPARSER_DIALECT_VASTBASE_MYSQL:
			return sqlparser_dialect_vastbase_mysql_ops();
		case SQLPARSER_DIALECT_VASTBASE_POSTGRESQL:
			return sqlparser_dialect_vastbase_postgresql_ops();
		case SQLPARSER_DIALECT_VASTBASE_SQLSERVER:
			return sqlparser_dialect_vastbase_sqlserver_ops();
		case SQLPARSER_DIALECT_KINGBASE_ORACLE:
			return sqlparser_dialect_kingbase_oracle_ops();
		case SQLPARSER_DIALECT_KINGBASE_MYSQL:
			return sqlparser_dialect_mysql_ops();
		case SQLPARSER_DIALECT_KINGBASE_POSTGRESQL:
			return sqlparser_dialect_postgresql_ops();
		case SQLPARSER_DIALECT_KINGBASE_SQLSERVER:
			return sqlparser_dialect_sqlserver_ops();
		default:
			return NULL;
	}
}

int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_get_ops(dialect) != NULL;
}

sqlparser_validation_preprocess_fn sqlparser_dialect_validation_preprocessor(
	sqlparser_dialect_t dialect, const sqlparser_dialect_ops_t *ops)
{
	/* Do not admit copied ops, another owner's state, or another dialect
	 * that happens to have compatible-looking callbacks. Kingbase shares
	 * the exact base SQLServer owner; Vastbase keeps that owner's state. */
	if (ops == NULL || ops != sqlparser_dialect_get_ops(dialect)) return NULL;
	if ((dialect == SQLPARSER_DIALECT_SQLSERVER ||
	     dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER) &&
	    ops == sqlparser_dialect_sqlserver_ops()) {
		return sqlparser_sqlserver_preprocess_validation_proof;
	}
	if (dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER &&
	    ops == sqlparser_dialect_vastbase_sqlserver_ops()) {
		return sqlparser_vastbase_sqlserver_preprocess_validation_proof;
	}
	return NULL;
}

int sqlparser_dialect_supports_plain_scalar_insert(const sqlparser_handle_t *handle)
{
	return handle != NULL && handle->dialect_ops != NULL &&
		handle->dialect_ops == sqlparser_dialect_get_ops(handle->dialect) &&
		/* A strict source/wire certificate is independent of the initial
		 * native-validation route, notably for control-capable SQLServer. */
		handle->dialect_ops->state_is_plain_insert_strings != NULL;
}

int sqlparser_dialect_state_is_plain_insert_strings(
	const sqlparser_handle_t *handle, size_t string_count)
{
	/* In particular, Vastbase owns the base dialect's state directly. Its
	 * stateless text rewrites must not be mistaken for identity provenance. */
	return sqlparser_dialect_supports_plain_scalar_insert(handle) &&
		handle->sql != NULL && handle->parser_sql != NULL &&
		handle->sql_len == handle->parser_sql_len &&
		(handle->sql == handle->parser_sql ||
		 memcmp(handle->sql, handle->parser_sql, handle->sql_len) == 0) &&
		handle->dialect_ops->state_is_plain_insert_strings(
			handle->dialect_state, string_count);
}

int sqlparser_dialect_state_is_plain_insert_batch_strings(
	const sqlparser_handle_t *handle, size_t statement_count, size_t string_count)
{
	const sqlparser_dialect_ops_t *ops;
	if (handle == NULL || statement_count < 2U ||
	    (ops = handle->dialect_ops) == NULL ||
	    ops != sqlparser_dialect_get_ops(handle->dialect) ||
	    handle->sql == NULL || handle->parser_sql == NULL ||
	    handle->sql_len != handle->parser_sql_len ||
	    (handle->sql != handle->parser_sql &&
	     memcmp(handle->sql, handle->parser_sql, handle->sql_len) != 0)) return 0;
	if ((handle->dialect == SQLPARSER_DIALECT_SQLSERVER ||
	     handle->dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER) &&
	    ops == sqlparser_dialect_sqlserver_ops())
		return sqlparser_sqlserver_state_is_plain_insert_batch_strings(
			handle->dialect_state, statement_count, string_count);
	if (handle->dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER &&
	    ops == sqlparser_dialect_vastbase_sqlserver_ops())
		return sqlparser_vastbase_sqlserver_state_is_plain_insert_batch_strings(
			handle->dialect_state, statement_count, string_count);
	return 0;
}

int sqlparser_dialect_uses_postgresql_placeholders(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL ||
		dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL;
}

int sqlparser_dialect_supports_postgresql_dml_results(
	sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
		dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE;
}

int sqlparser_dialect_uses_oracle_placeholders(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_ORACLE ||
		dialect == SQLPARSER_DIALECT_DAMENG ||
		dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE ||
		dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE;
}

int sqlparser_dialect_uses_sqlserver_placeholders(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER;
}

int sqlparser_dialect_is_oracle_compatible(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_ORACLE ||
		dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE ||
		dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE;
}

int sqlparser_dialect_is_oracle_or_dameng_compatible(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_is_oracle_compatible(dialect) ||
		dialect == SQLPARSER_DIALECT_DAMENG;
}

int sqlparser_dialect_is_mysql_compatible(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_MYSQL ||
		dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL ||
		dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL;
}

int sqlparser_dialect_is_sqlserver_compatible(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER;
}

const char *sqlparser_dialect_relation_object_name(
	const sqlparser_dialect_ops_t *ops,
	const void *state,
	const char *parser_object_name,
	const char **out_spelling)
{
	if (out_spelling != NULL) {
		*out_spelling = NULL;
	}
	if (ops == NULL || ops->relation_object_name == NULL) {
		return NULL;
	}
	return ops->relation_object_name(
		state,
		parser_object_name,
		out_spelling);
}

const char *sqlparser_dialect_relation_link_name(
	const sqlparser_dialect_ops_t *ops,
	const void *state,
	const char *parser_object_name)
{
	if (ops == NULL || ops->relation_link_name == NULL) {
		return NULL;
	}
	return ops->relation_link_name(state, parser_object_name);
}

const char *sqlparser_dialect_relation_link_sql(
	const sqlparser_dialect_ops_t *ops,
	const void *state,
	const char *parser_object_name)
{
	if (ops == NULL || ops->relation_link_sql == NULL) {
		return NULL;
	}
	return ops->relation_link_sql(state, parser_object_name);
}
