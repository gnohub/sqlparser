#include "sqlparser_case_runner.h"

int main(void)
{
	return sqlparser_case_runner_run(
		"./tests/cases/vastbase_sqlserver_dialect_input.json",
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
		0);
}
