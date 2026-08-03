#include "sqlparser_case_runner.h"

int main(void)
{
	return sqlparser_case_runner_run(
		"./tests/cases/mysql_dialect_input.json",
		SQLPARSER_DIALECT_MYSQL,
		0);
}
