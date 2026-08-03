#ifndef SQLPARSER_DIALECT_BIND_INTERNAL_H
#define SQLPARSER_DIALECT_BIND_INTERNAL_H

#include "sqlparser_dialect_internal.h"

typedef struct {
	char **names;
	size_t count;
	size_t capacity;
	size_t occurrence_count;
	int valid;
} sqlparser_dialect_prepared_binds_t;

void sqlparser_dialect_prepared_binds_clear(
	sqlparser_dialect_prepared_binds_t *prepared);
sqlparser_status_t sqlparser_dialect_prepare_binds(
	char *const *names,
	size_t name_count,
	PgQuery__ParseResult *ast,
	sqlparser_dialect_prepared_binds_t *prepared,
	sqlparser_error_t *out_error);
void sqlparser_dialect_reconcile_binds(
	sqlparser_dialect_prepared_binds_t *prepared,
	char ***names,
	size_t *name_count,
	size_t *name_capacity,
	size_t *occurrence_count);

#endif
