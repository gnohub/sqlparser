#include "sqlparser_case_runner.h"

int main(void)
{
	return sqlparser_case_runner_run(
		"./tests/cases/sql_batch_input.json",
		SQLPARSER_DIALECT_POSTGRESQL,
		1);
}
