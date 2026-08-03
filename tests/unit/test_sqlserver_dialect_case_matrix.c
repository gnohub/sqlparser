#include "sqlparser_case_runner.h"

int main(void)
{
	return sqlparser_case_runner_run(
		"./tests/cases/sqlserver_dialect_input.json",
		SQLPARSER_DIALECT_SQLSERVER,
		0);
}
