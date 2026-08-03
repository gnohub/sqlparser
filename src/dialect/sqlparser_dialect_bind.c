#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_ast_surface_internal.h"
#include "sqlparser_dialect_bind_internal.h"

typedef struct {
	size_t count;
} sqlparser_dialect_bind_count_t;

typedef struct {
	PgQuery__ParamRef *owner;
	size_t number;
} sqlparser_dialect_bind_update_t;

typedef struct {
	char *const *source_names;
	size_t source_count;
	size_t *source_to_prepared;
	sqlparser_dialect_prepared_binds_t *prepared;
	sqlparser_dialect_bind_update_t *updates;
	size_t update_count;
	sqlparser_status_t status;
} sqlparser_dialect_bind_prepare_t;

static void sqlparser_dialect_bind_count_visit(
	PgQuery__ParamRef *param_ref,
	size_t statement_index,
	void *context)
{
	sqlparser_dialect_bind_count_t *count;

	(void)param_ref;
	(void)statement_index;
	count = (sqlparser_dialect_bind_count_t *)context;
	if (count->count != SIZE_MAX) {
		count->count++;
	}
}

static void sqlparser_dialect_bind_prepare_visit(
	PgQuery__ParamRef *param_ref,
	size_t statement_index,
	void *context)
{
	sqlparser_dialect_bind_prepare_t *prepare;
	const char *name;
	size_t source_number;
	size_t prepared_number;
	char *copy;

	(void)statement_index;
	prepare = (sqlparser_dialect_bind_prepare_t *)context;
	if (prepare->status != SQLPARSER_STATUS_OK) {
		return;
	}
	if (param_ref == NULL || param_ref->number <= 0 ||
	    (size_t)param_ref->number > prepare->source_count) {
		prepare->status = SQLPARSER_STATUS_INTERNAL_ERROR;
		return;
	}
	source_number = (size_t)param_ref->number;
	name = prepare->source_names[source_number - 1U];
	if (name == NULL) {
		prepare->status = SQLPARSER_STATUS_INTERNAL_ERROR;
		return;
	}
	prepared_number = strcmp(name, "?") != 0 ?
		prepare->source_to_prepared[source_number] : 0U;
	if (prepared_number == 0U) {
		copy = sqlparser_strdup(name);
		if (copy == NULL) {
			prepare->status = SQLPARSER_STATUS_NO_MEMORY;
			return;
		}
		prepare->prepared->names[prepare->prepared->count] = copy;
		prepare->prepared->count++;
		prepared_number = prepare->prepared->count;
		if (strcmp(name, "?") != 0) {
			prepare->source_to_prepared[source_number] = prepared_number;
		}
	}
	prepare->updates[prepare->update_count].owner = param_ref;
	prepare->updates[prepare->update_count].number = prepared_number;
	prepare->update_count++;
}

void sqlparser_dialect_prepared_binds_clear(
	sqlparser_dialect_prepared_binds_t *prepared)
{
	size_t index;

	if (prepared == NULL) {
		return;
	}
	for (index = 0U; index < prepared->count; index++) {
		free(prepared->names[index]);
	}
	free(prepared->names);
	memset(prepared, 0, sizeof(*prepared));
}

sqlparser_status_t sqlparser_dialect_prepare_binds(
	char *const *names,
	size_t name_count,
	PgQuery__ParseResult *ast,
	sqlparser_dialect_prepared_binds_t *prepared,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_dialect_bind_count_t count;
	sqlparser_dialect_bind_prepare_t prepare;
	sqlparser_dialect_bind_update_t *updates;
	size_t *source_to_prepared;
	size_t index;

	if (prepared == NULL || ast == NULL ||
	    (name_count > 0U && names == NULL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind prepare arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (prepared->valid) {
		return SQLPARSER_STATUS_OK;
	}
	if (name_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&count, 0, sizeof(count));
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &count;
	visitor.param_ref = sqlparser_dialect_bind_count_visit;
	sqlparser_dialect_ast_surface_visit(ast, &visitor);
	if (count.count == SIZE_MAX || count.count > (size_t)INT_MAX ||
	    count.count > SIZE_MAX / sizeof(*prepared->names) ||
	    count.count > SIZE_MAX / sizeof(*updates) ||
	    name_count == SIZE_MAX ||
	    name_count + 1U > SIZE_MAX / sizeof(*source_to_prepared)) {
		sqlparser_error_set_message(
			out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	prepared->names = count.count > 0U ?
		(char **)calloc(count.count, sizeof(*prepared->names)) : NULL;
	updates = count.count > 0U ?
		(sqlparser_dialect_bind_update_t *)calloc(
			count.count, sizeof(*updates)) : NULL;
	source_to_prepared = (size_t *)calloc(
		name_count + 1U, sizeof(*source_to_prepared));
	if ((count.count > 0U &&
	     (prepared->names == NULL || updates == NULL)) ||
	    source_to_prepared == NULL) {
		free(updates);
		free(source_to_prepared);
		sqlparser_dialect_prepared_binds_clear(prepared);
		sqlparser_error_set_message(
			out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	prepared->capacity = count.count;
	prepared->occurrence_count = count.count;
	memset(&prepare, 0, sizeof(prepare));
	prepare.source_names = names;
	prepare.source_count = name_count;
	prepare.source_to_prepared = source_to_prepared;
	prepare.prepared = prepared;
	prepare.updates = updates;
	prepare.status = SQLPARSER_STATUS_OK;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &prepare;
	visitor.param_ref = sqlparser_dialect_bind_prepare_visit;
	sqlparser_dialect_ast_surface_visit(ast, &visitor);
	free(source_to_prepared);
	if (prepare.status != SQLPARSER_STATUS_OK ||
	    prepare.update_count != count.count) {
		free(updates);
		sqlparser_dialect_prepared_binds_clear(prepared);
		sqlparser_error_set_message(
			out_error,
			prepare.status != SQLPARSER_STATUS_OK ?
				prepare.status : SQLPARSER_STATUS_INTERNAL_ERROR,
			prepare.status == SQLPARSER_STATUS_NO_MEMORY ?
				"out of memory" : "bind AST state is inconsistent");
		return prepare.status != SQLPARSER_STATUS_OK ?
			prepare.status : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	for (index = 0U; index < count.count; index++) {
		updates[index].owner->number = (int)updates[index].number;
	}
	free(updates);
	prepared->valid = 1;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_dialect_reconcile_binds(
	sqlparser_dialect_prepared_binds_t *prepared,
	char ***names,
	size_t *name_count,
	size_t *name_capacity,
	size_t *occurrence_count)
{
	size_t index;

	if (prepared == NULL || !prepared->valid || names == NULL ||
	    name_count == NULL || name_capacity == NULL ||
	    occurrence_count == NULL) {
		return;
	}
	for (index = 0U; index < *name_count; index++) {
		free((*names)[index]);
	}
	free(*names);
	*names = prepared->names;
	*name_count = prepared->count;
	*name_capacity = prepared->capacity;
	*occurrence_count = prepared->occurrence_count;
	memset(prepared, 0, sizeof(*prepared));
}
