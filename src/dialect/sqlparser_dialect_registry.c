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
		default:
			return NULL;
	}
}

int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_get_ops(dialect) != NULL;
}
