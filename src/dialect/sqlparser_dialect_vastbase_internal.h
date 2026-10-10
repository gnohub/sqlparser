#ifndef SQLPARSER_DIALECT_VASTBASE_INTERNAL_H
#define SQLPARSER_DIALECT_VASTBASE_INTERNAL_H

#include "sqlparser/sqlparser.h"

/* Only the exact registered outer owner at a fresh core entry may call this.
 * The real Vastbase delegate additionally checks the base owner and outer
 * rewrite identity before granting one-shot Oracle compact construction. */
sqlparser_status_t sqlparser_vastbase_oracle_preprocess_compact_initial(
	const char *input_sql, const sqlparser_limits_t *limits,
	char **out_parser_sql, void **out_state, sqlparser_error_t *out_error);

#endif
