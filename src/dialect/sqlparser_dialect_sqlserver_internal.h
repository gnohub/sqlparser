#ifndef SQLPARSER_DIALECT_SQLSERVER_INTERNAL_H
#define SQLPARSER_DIALECT_SQLSERVER_INTERNAL_H

#include "sqlparser_dialect_sqlserver_output.h"

const sqlparser_sqlserver_output_state_t *sqlparser_sqlserver_state_output(
	const void *state,
	size_t statement_index,
	size_t *out_local_statement_index);
sqlparser_sqlserver_output_state_t *sqlparser_sqlserver_state_output_mutable(
	void *state,
	size_t statement_index,
	size_t *out_local_statement_index);
int sqlparser_sqlserver_state_has_odbc_function(
	const void *state,
	size_t statement_index);

/* Owner identity only; does not certify the source, state or native graph. */
int sqlparser_sqlserver_is_registered_native_batch_owner(
	sqlparser_dialect_t dialect, const sqlparser_dialect_ops_t *ops);

#endif
