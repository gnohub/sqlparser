#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_ast_surface_internal.h"
#include "sqlparser_dialect_minus_internal.h"

typedef struct {
	sqlparser_dialect_minuses_t *minuses;
	size_t record_end;
	size_t ordinal_base;
	size_t seen;
	size_t next_record;
} sqlparser_dialect_minus_bind_t;

typedef struct {
	sqlparser_dialect_minuses_t *minuses;
	size_t seen;
} sqlparser_dialect_minus_reconcile_t;

typedef struct {
	sqlparser_dialect_minuses_t *minuses;
	size_t source_record_count;
} sqlparser_dialect_minus_clone_t;

static int sqlparser_dialect_minus_owner_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dialect_minus_t *left_minus;
	const sqlparser_dialect_minus_t *right_minus;
	uintptr_t left_owner;
	uintptr_t right_owner;

	left_minus = (const sqlparser_dialect_minus_t *)left;
	right_minus = (const sqlparser_dialect_minus_t *)right;
	left_owner = (uintptr_t)left_minus->owner;
	right_owner = (uintptr_t)right_minus->owner;
	return left_owner < right_owner ? -1 : left_owner > right_owner;
}

static int sqlparser_dialect_minus_ordinal_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dialect_minus_t *left_minus;
	const sqlparser_dialect_minus_t *right_minus;

	left_minus = (const sqlparser_dialect_minus_t *)left;
	right_minus = (const sqlparser_dialect_minus_t *)right;
	return left_minus->ordinal < right_minus->ordinal ?
		-1 : left_minus->ordinal > right_minus->ordinal;
}

static sqlparser_dialect_minus_t *sqlparser_dialect_minus_find_owner(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__SelectStmt *owner,
	size_t count)
{
	sqlparser_dialect_minus_t key;

	if (count == 0U) {
		return NULL;
	}
	memset(&key, 0, sizeof(key));
	key.owner = (PgQuery__SelectStmt *)owner;
	return (sqlparser_dialect_minus_t *)bsearch(
		&key,
		minuses->items,
		count,
		sizeof(*minuses->items),
		sqlparser_dialect_minus_owner_compare);
}

static void sqlparser_dialect_minus_bind_visit(
	PgQuery__SelectStmt *select,
	size_t statement_index,
	void *context)
{
	sqlparser_dialect_minus_bind_t *bind;
	size_t ordinal;

	(void)statement_index;
	if (select->op != PG_QUERY__SET_OPERATION__SETOP_EXCEPT) {
		return;
	}
	bind = (sqlparser_dialect_minus_bind_t *)context;
	ordinal = bind->ordinal_base + bind->seen;
	while (bind->next_record < bind->record_end &&
	       bind->minuses->items[bind->next_record].ordinal < ordinal) {
		bind->next_record++;
	}
	if (bind->next_record < bind->record_end &&
	    bind->minuses->items[bind->next_record].ordinal == ordinal) {
		bind->minuses->items[bind->next_record].owner = select;
		bind->next_record++;
	}
	bind->seen++;
}

static sqlparser_status_t sqlparser_dialect_minus_bind_roots(
	sqlparser_dialect_minuses_t *minuses,
	size_t record_start,
	size_t record_end,
	size_t ordinal_base,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_dialect_minus_bind_t bind;

	if (record_start >= record_end) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&bind, 0, sizeof(bind));
	bind.minuses = minuses;
	bind.record_end = record_end;
	bind.ordinal_base = ordinal_base;
	bind.next_record = record_start;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &bind;
	visitor.set_operation = sqlparser_dialect_minus_bind_visit;
	sqlparser_dialect_ast_surface_visit_roots(
		roots, root_count, 0U, &visitor);
	if (bind.next_record != record_end) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"MINUS AST owner is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_dialect_minus_reconcile_visit(
	PgQuery__SelectStmt *select,
	size_t statement_index,
	void *context)
{
	sqlparser_dialect_minus_reconcile_t *reconcile;
	sqlparser_dialect_minus_t *record;

	(void)statement_index;
	if (select->op != PG_QUERY__SET_OPERATION__SETOP_EXCEPT) {
		return;
	}
	reconcile = (sqlparser_dialect_minus_reconcile_t *)context;
	record = sqlparser_dialect_minus_find_owner(
		reconcile->minuses,
		select,
		reconcile->minuses->count);
	if (record != NULL) {
		record->ordinal = reconcile->seen;
	}
	reconcile->seen++;
}

void sqlparser_dialect_minuses_clear(sqlparser_dialect_minuses_t *minuses)
{
	if (minuses == NULL) {
		return;
	}
	free(minuses->items);
	memset(minuses, 0, sizeof(*minuses));
}

sqlparser_status_t sqlparser_dialect_minuses_append(
	sqlparser_dialect_minuses_t *minuses,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_minus_t *items;
	size_t capacity;

	if (minuses == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MINUS state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (minuses->count == minuses->capacity) {
		capacity = minuses->capacity == 0U ? 4U : minuses->capacity * 2U;
		if (capacity < minuses->capacity ||
		    capacity > SIZE_MAX / sizeof(*items)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		items = (sqlparser_dialect_minus_t *)realloc(
			minuses->items, capacity * sizeof(*items));
		if (items == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		minuses->items = items;
		minuses->capacity = capacity;
	}
	minuses->items[minuses->count].ordinal = ordinal;
	minuses->items[minuses->count].owner = NULL;
	minuses->count++;
	return SQLPARSER_STATUS_OK;
}

int sqlparser_dialect_minuses_match(
	const sqlparser_dialect_minuses_t *minuses,
	size_t ordinal)
{
	size_t left;
	size_t right;

	if (minuses == NULL) {
		return 0;
	}
	left = 0U;
	right = minuses->count;
	while (left < right) {
		size_t middle;

		middle = left + (right - left) / 2U;
		if (minuses->items[middle].ordinal < ordinal) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	return left < minuses->count &&
		minuses->items[left].ordinal == ordinal;
}

sqlparser_status_t sqlparser_dialect_minuses_clone(
	const sqlparser_dialect_minuses_t *source,
	sqlparser_dialect_minuses_t *target,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	if (source == NULL || target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MINUS state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(target, 0, sizeof(*target));
	target->except_count = source->except_count;
	for (index = 0U; index < source->count; index++) {
		status = sqlparser_dialect_minuses_append(
			target, source->items[index].ordinal, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dialect_minuses_clear(target);
			return status;
		}
	}
	target->fragment_start = target->count;
	target->fragment_except_base = target->except_count;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_dialect_minuses_begin_fragment(
	sqlparser_dialect_minuses_t *minuses)
{
	if (minuses != NULL) {
		minuses->fragment_start = minuses->count;
		minuses->fragment_except_base = minuses->except_count;
	}
}

sqlparser_status_t sqlparser_dialect_minuses_bind_ast(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *roots[1];
	size_t index;

	if (minuses == NULL || minuses->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	for (index = 0U; index < minuses->count; index++) {
		minuses->items[index].owner = NULL;
	}
	if (ast == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	roots[0] = (ProtobufCMessage *)ast;
	return sqlparser_dialect_minus_bind_roots(
		minuses, 0U, minuses->count, 0U, roots, 1U, out_error);
}

sqlparser_status_t sqlparser_dialect_minuses_bind_fragment(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__ParseResult *base_ast,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *base_roots[1];
	sqlparser_status_t status;

	if (minuses == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (minuses->fragment_start > minuses->count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"MINUS fragment checkpoint is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	base_roots[0] = (ProtobufCMessage *)base_ast;
	status = sqlparser_dialect_minus_bind_roots(
		minuses,
		0U,
		minuses->fragment_start,
		0U,
		base_roots,
		base_ast != NULL ? 1U : 0U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_dialect_minus_bind_roots(
		minuses,
		minuses->fragment_start,
		minuses->count,
		minuses->fragment_except_base,
		roots,
		root_count,
		out_error);
}

void sqlparser_dialect_minuses_reconcile(
	sqlparser_dialect_minuses_t *minuses,
	const PgQuery__ParseResult *ast)
{
	sqlparser_dialect_ast_surface_visitor_t visitor;
	sqlparser_dialect_minus_reconcile_t reconcile;
	size_t read_index;
	size_t write_index;

	if (minuses == NULL || ast == NULL) {
		return;
	}
	if (minuses->count > 1U) {
		qsort(
			minuses->items,
			minuses->count,
			sizeof(*minuses->items),
			sqlparser_dialect_minus_owner_compare);
	}
	for (read_index = 0U; read_index < minuses->count; read_index++) {
		minuses->items[read_index].ordinal = SIZE_MAX;
	}
	memset(&reconcile, 0, sizeof(reconcile));
	reconcile.minuses = minuses;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &reconcile;
	visitor.set_operation = sqlparser_dialect_minus_reconcile_visit;
	sqlparser_dialect_ast_surface_visit(ast, &visitor);
	write_index = 0U;
	for (read_index = 0U; read_index < minuses->count; read_index++) {
		if (minuses->items[read_index].ordinal == SIZE_MAX) {
			continue;
		}
		if (write_index != read_index) {
			minuses->items[write_index] = minuses->items[read_index];
		}
		write_index++;
	}
	minuses->count = write_index;
	if (minuses->count > 1U) {
		qsort(
			minuses->items,
			minuses->count,
			sizeof(*minuses->items),
			sqlparser_dialect_minus_ordinal_compare);
	}
	minuses->except_count = reconcile.seen;
	minuses->fragment_start = minuses->count;
	minuses->fragment_except_base = minuses->except_count;
}

static sqlparser_status_t sqlparser_dialect_minus_clone_visit(
	const PgQuery__SelectStmt *source,
	PgQuery__SelectStmt *clone,
	void *context,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_minus_clone_t *clone_context;
	sqlparser_dialect_minus_t *record;
	sqlparser_status_t status;

	if (source->op != PG_QUERY__SET_OPERATION__SETOP_EXCEPT) {
		return SQLPARSER_STATUS_OK;
	}
	clone_context = (sqlparser_dialect_minus_clone_t *)context;
	record = sqlparser_dialect_minus_find_owner(
		clone_context->minuses,
		source,
		clone_context->source_record_count);
	if (record == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dialect_minuses_append(
		clone_context->minuses, SIZE_MAX, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		clone_context->minuses->items[
			clone_context->minuses->count - 1U].owner = clone;
	}
	return status;
}

sqlparser_status_t sqlparser_dialect_minuses_clone_owners(
	sqlparser_dialect_minuses_t *minuses,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_clone_visitor_t visitor;
	sqlparser_dialect_minus_clone_t clone_context;
	size_t original_count;
	sqlparser_status_t status;

	if (minuses == NULL || source_root == NULL || clone_root == NULL ||
	    minuses->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	original_count = minuses->count;
	if (original_count > 1U) {
		qsort(
			minuses->items,
			original_count,
			sizeof(*minuses->items),
			sqlparser_dialect_minus_owner_compare);
	}
	memset(&clone_context, 0, sizeof(clone_context));
	clone_context.minuses = minuses;
	clone_context.source_record_count = original_count;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &clone_context;
	visitor.set_operation = sqlparser_dialect_minus_clone_visit;
	status = sqlparser_dialect_ast_surface_clone(
		source_root,
		(ProtobufCMessage *)clone_root,
		&visitor,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		minuses->count = original_count;
		if (status == SQLPARSER_STATUS_INTERNAL_ERROR) {
			sqlparser_error_set_message(
				out_error,
				status,
				"cloned MINUS AST shape is inconsistent");
		}
	}
	return status;
}
