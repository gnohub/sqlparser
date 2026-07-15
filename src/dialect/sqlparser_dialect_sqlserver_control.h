#ifndef SQLPARSER_DIALECT_SQLSERVER_CONTROL_H
#define SQLPARSER_DIALECT_SQLSERVER_CONTROL_H

#include "sqlparser_control_internal.h"

sqlparser_status_t sqlparser_sqlserver_control_parse(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	sqlparser_control_state_t **out_state,
	sqlparser_error_t *out_error);

#endif
