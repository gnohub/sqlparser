#include "sqlparser_case_runner.h"

int main(void)
{
	return sqlparser_case_runner_run(
		"./tests/cases/oracle_dialect_input.json",
		SQLPARSER_DIALECT_ORACLE,
		0);
}
