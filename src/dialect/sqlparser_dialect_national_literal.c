#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_ast_surface_internal.h"
#include "sqlparser_dialect_national_literal_internal.h"

typedef struct {
	sqlparser_dialect_national_literals_t *literals;
	size_t record_end;
	size_t ordinal_base;
	size_t seen;
	size_t next_record;
} sqlparser_dialect_national_bind_t;

typedef struct {
	sqlparser_dialect_national_literals_t *literals;
	size_t seen;
} sqlparser_dialect_national_reconcile_t;

static void sqlparser_dialect_national_bind_visit(
	PgQuery__AConst *literal,
	size_t statement_index,
	void *context)
{
	sqlparser_dialect_national_bind_t *bind;
	size_t ordinal;

	(void)statement_index;
	bind = (sqlparser_dialect_national_bind_t *)context;
	ordinal = bind->ordinal_base + bind->seen;
	while (bind->next_record < bind->record_end &&
	       bind->literals->items[bind->next_record].ordinal < ordinal) {
		bind->next_record++;
	}
	if (bind->next_record < bind->record_end &&
	    bind->literals->items[bind->next_record].ordinal == ordinal) {
		bind->literals->items[bind->next_record].owner = literal;
		bind->next_record++;
	}
	bind->seen++;
}

static sqlparser_status_t sqlparser_dialect_national_bind_messages(
	sqlparser_dialect_national_literals_t *literals,
	size_t record_start,
	size_t record_end,
	size_t ordinal_base,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_national_bind_t bind;
	sqlparser_dialect_ast_surface_visitor_t visitor;

	if (record_start >= record_end) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&bind, 0, sizeof(bind));
	bind.literals = literals;
	bind.record_end = record_end;
	bind.ordinal_base = ordinal_base;
	bind.next_record = record_start;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &bind;
	visitor.string_literal = sqlparser_dialect_national_bind_visit;
	sqlparser_dialect_ast_surface_visit_roots(
		roots, root_count, 0U, &visitor);
	if (bind.next_record != record_end) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"national literal AST owner is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dialect_national_owner_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dialect_national_literal_t *left_literal;
	const sqlparser_dialect_national_literal_t *right_literal;
	uintptr_t left_owner;
	uintptr_t right_owner;

	left_literal = (const sqlparser_dialect_national_literal_t *)left;
	right_literal = (const sqlparser_dialect_national_literal_t *)right;
	left_owner = (uintptr_t)left_literal->owner;
	right_owner = (uintptr_t)right_literal->owner;
	return left_owner < right_owner ? -1 : left_owner > right_owner;
}

static int sqlparser_dialect_national_ordinal_compare(
	const void *left,
	const void *right)
{
	const sqlparser_dialect_national_literal_t *left_literal;
	const sqlparser_dialect_national_literal_t *right_literal;

	left_literal = (const sqlparser_dialect_national_literal_t *)left;
	right_literal = (const sqlparser_dialect_national_literal_t *)right;
	return left_literal->ordinal < right_literal->ordinal ?
		-1 : left_literal->ordinal > right_literal->ordinal;
}

static sqlparser_dialect_national_literal_t *
sqlparser_dialect_national_find_owner_sorted(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__AConst *owner,
	size_t count)
{
	sqlparser_dialect_national_literal_t key;

	if (count == 0U) {
		return NULL;
	}
	memset(&key, 0, sizeof(key));
	key.owner = (PgQuery__AConst *)owner;
	return (sqlparser_dialect_national_literal_t *)bsearch(
		&key,
		literals->items,
		count,
		sizeof(*literals->items),
		sqlparser_dialect_national_owner_compare);
}

static void sqlparser_dialect_national_reconcile_visit(
	PgQuery__AConst *literal,
	size_t statement_index,
	void *context)
{
	sqlparser_dialect_national_reconcile_t *reconcile;
	sqlparser_dialect_national_literal_t *record;

	(void)statement_index;
	reconcile = (sqlparser_dialect_national_reconcile_t *)context;
	record = sqlparser_dialect_national_find_owner_sorted(
		reconcile->literals,
		literal,
		reconcile->literals->count);
	if (record != NULL) {
		record->ordinal = reconcile->seen;
	}
	reconcile->seen++;
}

void sqlparser_dialect_national_literals_clear(
	sqlparser_dialect_national_literals_t *literals)
{
	size_t index;

	if (literals == NULL) {
		return;
	}
	for (index = 0U; index < literals->count; index++) {
		free(literals->items[index].sql);
		free(literals->items[index].surface_sql);
	}
	free(literals->items);
	memset(literals, 0, sizeof(*literals));
}

sqlparser_status_t sqlparser_dialect_national_literals_append(
	sqlparser_dialect_national_literals_t *literals,
	const char *literal_sql,
	size_t literal_sql_length,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_national_literal_t *items;
	char *copy;
	size_t capacity;

	if (literals == NULL || literal_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"national literal arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (literals->count == literals->capacity) {
		capacity = literals->capacity == 0U ? 4U : literals->capacity * 2U;
		if (capacity < literals->capacity ||
		    capacity > SIZE_MAX / sizeof(*items)) {
			sqlparser_error_set_message(
				out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		items = (sqlparser_dialect_national_literal_t *)realloc(
			literals->items, capacity * sizeof(*items));
		if (items == NULL) {
			sqlparser_error_set_message(
				out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		literals->items = items;
		literals->capacity = capacity;
	}
	copy = sqlparser_strndup(literal_sql, literal_sql_length);
	if (copy == NULL) {
		sqlparser_error_set_message(
			out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	literals->items[literals->count].sql = copy;
	literals->items[literals->count].surface_sql = NULL;
	literals->items[literals->count].ordinal = ordinal;
	literals->items[literals->count].owner = NULL;
	literals->count++;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_national_literals_append_surface(
	sqlparser_dialect_national_literals_t *literals,
	const char *literal_sql,
	size_t literal_sql_length,
	const char *surface_sql,
	size_t surface_sql_length,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	char *surface_copy;
	sqlparser_status_t status;

	if (surface_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal surface must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_dialect_national_literals_append(
		literals,
		literal_sql,
		literal_sql_length,
		ordinal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	surface_copy = sqlparser_strndup(surface_sql, surface_sql_length);
	if (surface_copy == NULL) {
		literals->count--;
		free(literals->items[literals->count].sql);
		sqlparser_error_set_message(
			out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	literals->items[literals->count - 1U].surface_sql = surface_copy;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dialect_national_literal_t *
sqlparser_dialect_national_literals_find(
	const sqlparser_dialect_national_literals_t *literals,
	size_t ordinal,
	const char *sql,
	size_t start,
	size_t end)
{
	size_t left;
	size_t right;

	if (literals == NULL || sql == NULL || end <= start) {
		return NULL;
	}
	left = 0U;
	right = literals->count;
	while (left < right) {
		size_t middle;
		const sqlparser_dialect_national_literal_t *literal;

		middle = left + (right - left) / 2U;
		literal = &literals->items[middle];
		if (literal->ordinal < ordinal) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	if (left >= literals->count ||
	    literals->items[left].ordinal != ordinal ||
	    strlen(literals->items[left].sql) != end - start ||
	    memcmp(literals->items[left].sql, sql + start, end - start) != 0) {
		return NULL;
	}
	return &literals->items[left];
}

int sqlparser_dialect_national_literals_match(
	const sqlparser_dialect_national_literals_t *literals,
	size_t ordinal,
	const char *sql,
	size_t start,
	size_t end)
{
	return sqlparser_dialect_national_literals_find(
		literals, ordinal, sql, start, end) != NULL;
}

const sqlparser_dialect_national_literal_t *
sqlparser_dialect_national_literals_find_owner(
	const sqlparser_dialect_national_literals_t *literals,
	const PgQuery__AConst *owner)
{
	size_t index;

	if (literals == NULL || owner == NULL) {
		return NULL;
	}
	for (index = 0U; index < literals->count; index++) {
		if (literals->items[index].owner == owner) {
			return &literals->items[index];
		}
	}
	return NULL;
}

sqlparser_status_t sqlparser_dialect_national_literals_clone(
	const sqlparser_dialect_national_literals_t *source,
	sqlparser_dialect_national_literals_t *target,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	if (source == NULL || target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"national literal state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(target, 0, sizeof(*target));
	target->literal_count = source->literal_count;
	for (index = 0U; index < source->count; index++) {
		if (source->items[index].surface_sql != NULL) {
			status = sqlparser_dialect_national_literals_append_surface(
				target,
				source->items[index].sql,
				strlen(source->items[index].sql),
				source->items[index].surface_sql,
				strlen(source->items[index].surface_sql),
				source->items[index].ordinal,
				out_error);
		} else {
			status = sqlparser_dialect_national_literals_append(
				target,
				source->items[index].sql,
				strlen(source->items[index].sql),
				source->items[index].ordinal,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dialect_national_literals_clear(target);
			return status;
		}
	}
	target->fragment_start = target->count;
	target->fragment_literal_base = target->literal_count;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_dialect_national_literals_begin_fragment(
	sqlparser_dialect_national_literals_t *literals)
{
	if (literals == NULL) {
		return;
	}
	literals->fragment_start = literals->count;
	literals->fragment_literal_base = literals->literal_count;
}

sqlparser_status_t sqlparser_dialect_national_literals_bind_ast(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__ParseResult *ast,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *roots[1];
	size_t index;

	if (literals == NULL || literals->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	for (index = 0U; index < literals->count; index++) {
		literals->items[index].owner = NULL;
	}
	if (ast == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	roots[0] = (ProtobufCMessage *)ast;
	return sqlparser_dialect_national_bind_messages(
		literals,
		0U,
		literals->count,
		0U,
		roots,
		1U,
		out_error);
}

sqlparser_status_t sqlparser_dialect_national_literals_bind_fragment(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__ParseResult *base_ast,
	ProtobufCMessage *const *roots,
	size_t root_count,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *base_roots[1];
	sqlparser_status_t status;

	if (literals == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (literals->fragment_start > literals->count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"national literal fragment checkpoint is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	base_roots[0] = (ProtobufCMessage *)base_ast;
	status = sqlparser_dialect_national_bind_messages(
		literals,
		0U,
		literals->fragment_start,
		0U,
		base_roots,
		base_ast != NULL ? 1U : 0U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_dialect_national_bind_messages(
		literals,
		literals->fragment_start,
		literals->count,
		literals->fragment_literal_base,
		roots,
		root_count,
		out_error);
}

void sqlparser_dialect_national_literals_reconcile(
	sqlparser_dialect_national_literals_t *literals,
	const PgQuery__ParseResult *ast)
{
	sqlparser_dialect_national_reconcile_t reconcile;
	sqlparser_dialect_ast_surface_visitor_t visitor;
	size_t read_index;
	size_t write_index;

	if (literals == NULL || ast == NULL) {
		return;
	}
	if (literals->count > 1U) {
		qsort(
			literals->items,
			literals->count,
			sizeof(*literals->items),
			sqlparser_dialect_national_owner_compare);
	}
	for (read_index = 0U; read_index < literals->count; read_index++) {
		literals->items[read_index].ordinal = SIZE_MAX;
	}
	memset(&reconcile, 0, sizeof(reconcile));
	reconcile.literals = literals;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &reconcile;
	visitor.string_literal = sqlparser_dialect_national_reconcile_visit;
	sqlparser_dialect_ast_surface_visit(ast, &visitor);
	write_index = 0U;
	for (read_index = 0U; read_index < literals->count; read_index++) {
		if (literals->items[read_index].ordinal == SIZE_MAX) {
			free(literals->items[read_index].sql);
			free(literals->items[read_index].surface_sql);
			continue;
		}
		if (write_index != read_index) {
			literals->items[write_index] = literals->items[read_index];
		}
		write_index++;
	}
	literals->count = write_index;
	if (literals->count > 1U) {
		qsort(
			literals->items,
			literals->count,
			sizeof(*literals->items),
			sqlparser_dialect_national_ordinal_compare);
	}
	literals->literal_count = reconcile.seen;
	literals->fragment_start = literals->count;
	literals->fragment_literal_base = literals->literal_count;
}

typedef struct {
	sqlparser_dialect_national_literals_t *literals;
	size_t source_record_count;
} sqlparser_dialect_national_clone_t;

static sqlparser_status_t sqlparser_dialect_national_clone_visit(
	const PgQuery__AConst *source,
	PgQuery__AConst *clone,
	void *context,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_national_clone_t *clone_context;
	sqlparser_dialect_national_literal_t *record;
	sqlparser_status_t status;

	if ((source->val_case != PG_QUERY__A__CONST__VAL_SVAL ||
	     source->sval == NULL) &&
	    (source->val_case != PG_QUERY__A__CONST__VAL_BSVAL ||
	     source->bsval == NULL)) {
		return SQLPARSER_STATUS_OK;
	}
	clone_context = (sqlparser_dialect_national_clone_t *)context;
	record = sqlparser_dialect_national_find_owner_sorted(
		clone_context->literals,
		source,
		clone_context->source_record_count);
	if (record == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (record->surface_sql != NULL) {
		status = sqlparser_dialect_national_literals_append_surface(
			clone_context->literals,
			record->sql,
			strlen(record->sql),
			record->surface_sql,
			strlen(record->surface_sql),
			SIZE_MAX,
			out_error);
	} else {
		status = sqlparser_dialect_national_literals_append(
			clone_context->literals,
			record->sql,
			strlen(record->sql),
			SIZE_MAX,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		clone_context->literals->items[
			clone_context->literals->count - 1U].owner = clone;
	}
	return status;
}

sqlparser_status_t sqlparser_dialect_national_literals_clone_owners(
	sqlparser_dialect_national_literals_t *literals,
	const ProtobufCMessage *source_root,
	const ProtobufCMessage *clone_root,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_ast_surface_clone_visitor_t visitor;
	sqlparser_dialect_national_clone_t clone_context;
	size_t original_count;
	sqlparser_status_t status;

	if (literals == NULL || source_root == NULL || clone_root == NULL ||
	    literals->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	original_count = literals->count;
	if (original_count > 1U) {
		qsort(
			literals->items,
			original_count,
			sizeof(*literals->items),
			sqlparser_dialect_national_owner_compare);
	}
	memset(&clone_context, 0, sizeof(clone_context));
	clone_context.literals = literals;
	clone_context.source_record_count = original_count;
	memset(&visitor, 0, sizeof(visitor));
	visitor.context = &clone_context;
	visitor.string_literal = sqlparser_dialect_national_clone_visit;
	status = sqlparser_dialect_ast_surface_clone(
		source_root,
		(ProtobufCMessage *)clone_root,
		&visitor,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		while (literals->count > original_count) {
			literals->count--;
			free(literals->items[literals->count].sql);
			free(literals->items[literals->count].surface_sql);
		}
		if (status == SQLPARSER_STATUS_INTERNAL_ERROR) {
			sqlparser_error_set_message(
				out_error,
				status,
				"cloned national literal AST shape is inconsistent");
		}
	}
	return status;
}
