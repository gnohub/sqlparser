#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

typedef struct {
	sqlparser_dialect_t dialect;
	const char *dialect_name;
	const char *sql;
	const char *deparse_contains;
} vastbase_case_t;

static int run_case(const vastbase_case_t *item)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	char *deparsed_sql;
	int status;

	handle = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));
	sqlparser_parse_options_default(&options);
	options.dialect = item->dialect;

	/* Vastbase 需要显式指定兼容模式，库不会根据 SQL 文本自动猜测。 */
	status = sqlparser_parse_with_options(item->sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "%s parse failed: %s\n", item->dialect_name, err.message);
		return 1;
	}

	printf("dialect: %s\n", sqlparser_dialect_name(sqlparser_handle_dialect(handle)));

	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "%s deparse failed: %s\n", item->dialect_name, err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("sql: %s\n", deparsed_sql);

	if (strstr(deparsed_sql, item->deparse_contains) == NULL) {
		fprintf(stderr, "%s deparse output is unexpected\n", item->dialect_name);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

int main(void)
{
	static const vastbase_case_t cases[] = {
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"vastbase-oracle",
			"ALTER SESSION SET CURRENT_SCHEMA=APP",
			"CURRENT_SCHEMA"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"vastbase-mysql",
			"SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?",
			"LIMIT ?, ?"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"vastbase-postgresql",
			"SELECT id FROM public.users WHERE id = $1",
			"$1"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"vastbase-sqlserver",
			"SELECT TOP (5) [id] FROM [dbo].[users] WHERE [id] = @id",
			"TOP (5)"
		}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		if (run_case(&cases[index]) != 0) {
			return 1;
		}
	}
	return 0;
}
