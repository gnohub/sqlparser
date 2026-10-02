/* Compile tools/sqlparser_bench.c with -Dsqlparser_parse=pipeline_mysql_parse
 * and link this adapter to exercise the existing API benchmark in MySQL mode. */
#include "sqlparser/sqlparser.h"
sqlparser_status_t pipeline_mysql_parse(const char *sql,
    sqlparser_handle_t **handle, sqlparser_error_t *error)
{
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_MYSQL;
    return sqlparser_parse_with_options(sql, &options, handle, error);
}
