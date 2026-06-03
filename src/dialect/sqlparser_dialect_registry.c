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
		default:
			return NULL;
	}
}

int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_get_ops(dialect) != NULL;
}

int sqlparser_dialect_uses_postgresql_placeholders(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL;
}

int sqlparser_dialect_uses_oracle_placeholders(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_ORACLE ||
		dialect == SQLPARSER_DIALECT_DAMENG ||
		dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE;
}

int sqlparser_dialect_uses_sqlserver_placeholders(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
}

int sqlparser_dialect_is_oracle_compatible(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_ORACLE ||
		dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE;
}

int sqlparser_dialect_is_oracle_or_dameng_compatible(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_is_oracle_compatible(dialect) ||
		dialect == SQLPARSER_DIALECT_DAMENG;
}

int sqlparser_dialect_is_mysql_compatible(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_MYSQL ||
		dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL;
}

int sqlparser_dialect_is_sqlserver_compatible(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_SQLSERVER ||
		dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
}
