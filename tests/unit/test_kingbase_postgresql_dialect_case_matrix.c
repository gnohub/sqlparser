#include "sqlparser_case_runner.h"

int main(void)
{
	return sqlparser_case_runner_run(
		"./tests/cases/kingbase_postgresql_dialect_input.json",
		SQLPARSER_DIALECT_KINGBASE_POSTGRESQL,
		0);
}
