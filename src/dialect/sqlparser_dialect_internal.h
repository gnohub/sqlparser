#ifndef SQLPARSER_DIALECT_INTERNAL_H
#define SQLPARSER_DIALECT_INTERNAL_H

#include "sqlparser_internal.h"

struct sqlparser_dialect_ops {
	sqlparser_dialect_t dialect;
	const char *name;
	sqlparser_status_t (*preprocess)(
		const char *input_sql,
		const sqlparser_limits_t *limits,
		char **out_parser_sql,
		void **out_state,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*postprocess_deparse)(
		const char *core_sql,
		const void *state,
		char **out_sql,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*clone_state)(
		const void *state,
		void **out_state,
		sqlparser_error_t *out_error);
	void (*destroy_state)(void *state);
};

const sqlparser_dialect_ops_t *sqlparser_dialect_get_ops(sqlparser_dialect_t dialect);
int sqlparser_dialect_is_supported(sqlparser_dialect_t dialect);

const sqlparser_dialect_ops_t *sqlparser_dialect_postgresql_ops(void);
const sqlparser_dialect_ops_t *sqlparser_dialect_mysql_ops(void);

#endif
