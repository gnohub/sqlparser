#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"

static void sqlparser_relation_view_clear(sqlparser_relation_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
}

static void sqlparser_literal_view_clear(sqlparser_literal_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
	view->kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
}

static void sqlparser_assignment_view_clear(sqlparser_assignment_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
	view->value_kind = SQLPARSER_VALUE_KIND_UNKNOWN;
	view->literal.kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
}

static void sqlparser_where_literal_view_clear(sqlparser_where_literal_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
	view->literal.kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
}

static void sqlparser_name_view_clear(sqlparser_name_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
}

typedef struct {
	const char *table_name;
	const char *column_name;
	const char *operator_name;
} sqlparser_predicate_context_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	PgQuery__AConst *literal_node;
	sqlparser_where_literal_view_t *literal_view;
} sqlparser_where_literal_search_t;

typedef struct {
	const ProtobufCMessageDescriptor *descriptor;
	size_t seen;
	size_t target_index;
	int want_target;
	ProtobufCMessage *target_message;
	int (*accept)(ProtobufCMessage *message);
} sqlparser_message_search_t;

typedef struct {
	const char *owner_type;
	const char *field_name;
} sqlparser_name_context_t;

typedef struct {
	size_t seen;
	size_t target_index;
	int want_target;
	char **target_slot;
	sqlparser_name_view_t *name_view;
} sqlparser_name_search_t;

static sqlparser_status_t sqlparser_walk_message_tree(
	ProtobufCMessage *message,
	sqlparser_message_search_t *search,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_walk_message_names(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_name_search_t *search);

static const char *sqlparser_statement_node_name_from_case(PgQuery__Node__NodeCase node_case)
{
	switch (node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return "SelectStmt";
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return "InsertStmt";
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return "UpdateStmt";
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return "DeleteStmt";
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return "MergeStmt";
		case PG_QUERY__NODE__NODE_TRANSACTION_STMT:
			return "TransactionStmt";
		case PG_QUERY__NODE__NODE_DROP_STMT:
			return "DropStmt";
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return "ViewStmt";
		case PG_QUERY__NODE__NODE_CREATE_STMT:
			return "CreateStmt";
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return "CreateTableAsStmt";
		case PG_QUERY__NODE__NODE_ALTER_TABLE_STMT:
			return "AlterTableStmt";
		case PG_QUERY__NODE__NODE_TRUNCATE_STMT:
			return "TruncateStmt";
		case PG_QUERY__NODE__NODE_EXPLAIN_STMT:
			return "ExplainStmt";
		case PG_QUERY__NODE__NODE_COPY_STMT:
			return "CopyStmt";
		case PG_QUERY__NODE__NODE_CALL_STMT:
			return "CallStmt";
		case PG_QUERY__NODE__NODE_DO_STMT:
			return "DoStmt";
		case PG_QUERY__NODE__NODE_COMMENT_STMT:
			return "CommentStmt";
		case PG_QUERY__NODE__NODE_VACUUM_STMT:
			return "VacuumStmt";
		case PG_QUERY__NODE__NODE_LOCK_STMT:
			return "LockStmt";
		case PG_QUERY__NODE__NODE_RENAME_STMT:
			return "RenameStmt";
		default:
			return "OtherStmt";
	}
}

static sqlparser_statement_kind_t sqlparser_statement_kind_from_case(PgQuery__Node__NodeCase node_case)
{
	switch (node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return SQLPARSER_STATEMENT_KIND_SELECT;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return SQLPARSER_STATEMENT_KIND_INSERT;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return SQLPARSER_STATEMENT_KIND_UPDATE;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return SQLPARSER_STATEMENT_KIND_DELETE;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return SQLPARSER_STATEMENT_KIND_MERGE;
		case PG_QUERY__NODE__NODE_TRANSACTION_STMT:
			return SQLPARSER_STATEMENT_KIND_TRANSACTION;
		case PG_QUERY__NODE__NODE_CALL_STMT:
			return SQLPARSER_STATEMENT_KIND_CALL;
		case PG_QUERY__NODE__NODE_DROP_STMT:
		case PG_QUERY__NODE__NODE_VIEW_STMT:
		case PG_QUERY__NODE__NODE_CREATE_STMT:
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
		case PG_QUERY__NODE__NODE_ALTER_TABLE_STMT:
		case PG_QUERY__NODE__NODE_TRUNCATE_STMT:
		case PG_QUERY__NODE__NODE_COMMENT_STMT:
		case PG_QUERY__NODE__NODE_RENAME_STMT:
			return SQLPARSER_STATEMENT_KIND_DDL;
		case PG_QUERY__NODE__NODE__NOT_SET:
			return SQLPARSER_STATEMENT_KIND_UNKNOWN;
		default:
			return SQLPARSER_STATEMENT_KIND_OTHER;
	}
}

static sqlparser_status_t sqlparser_get_statement_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_statement,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	sqlparser_status_t status;

	if (out_statement == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_statement = NULL;
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (statement_index >= handle->ast->n_stmts) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	raw_stmt = handle->ast->stmts[statement_index];
	if (raw_stmt == NULL || raw_stmt->stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"statement node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_statement = raw_stmt->stmt;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__InsertStmt **out_stmt,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;

	if (out_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_stmt must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_stmt = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (statement->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
	    statement->insert_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"statement is not an INSERT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_stmt = statement->insert_stmt;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_update_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__UpdateStmt **out_stmt,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;

	if (out_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_stmt must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_stmt = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (statement->node_case != PG_QUERY__NODE__NODE_UPDATE_STMT ||
	    statement->update_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"statement is not an UPDATE");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_stmt = statement->update_stmt;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_statement_where_clause(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node **out_where_clause,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;

	if (out_where_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_where_clause must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_where_clause = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			if (statement->select_stmt != NULL) {
				*out_where_clause = statement->select_stmt->where_clause;
			}
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			if (statement->update_stmt != NULL) {
				*out_where_clause = statement->update_stmt->where_clause;
			}
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (statement->delete_stmt != NULL) {
				*out_where_clause = statement->delete_stmt->where_clause;
			}
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (statement->insert_stmt != NULL &&
			    statement->insert_stmt->select_stmt != NULL &&
			    statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
			    statement->insert_stmt->select_stmt->select_stmt != NULL) {
				*out_where_clause = statement->insert_stmt->select_stmt->select_stmt->where_clause;
			}
			return SQLPARSER_STATUS_OK;
		default:
			return SQLPARSER_STATUS_OK;
	}
}

static sqlparser_status_t sqlparser_search_statement_messages(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const ProtobufCMessageDescriptor *descriptor,
	int (*accept)(ProtobufCMessage *message),
	int want_target,
	size_t target_index,
	size_t *out_count,
	ProtobufCMessage **out_message,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_message_search_t search;
	sqlparser_status_t status;

	if (descriptor == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"descriptor must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (out_count != NULL) {
		*out_count = 0U;
	}
	if (out_message != NULL) {
		*out_message = NULL;
	}

	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.descriptor = descriptor;
	search.accept = accept;
	search.want_target = want_target;
	search.target_index = target_index;
	status = sqlparser_walk_message_tree((ProtobufCMessage *)statement, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (out_count != NULL) {
		*out_count = search.seen;
	}
	if (out_message != NULL) {
		*out_message = search.target_message;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_insert_source_kind_t sqlparser_insert_source_from_stmt(PgQuery__InsertStmt *stmt)
{
	PgQuery__SelectStmt *select_stmt;

	if (stmt == NULL || stmt->select_stmt == NULL) {
		return SQLPARSER_INSERT_SOURCE_UNKNOWN;
	}

	if (stmt->select_stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    stmt->select_stmt->select_stmt == NULL) {
		return SQLPARSER_INSERT_SOURCE_QUERY;
	}

	select_stmt = stmt->select_stmt->select_stmt;
	if (select_stmt->n_values_lists > 0U) {
		return SQLPARSER_INSERT_SOURCE_VALUES;
	}

	return SQLPARSER_INSERT_SOURCE_QUERY;
}

static sqlparser_status_t sqlparser_get_insert_values_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__InsertStmt **out_insert_stmt,
	PgQuery__SelectStmt **out_values_stmt,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;

	if (out_insert_stmt == NULL || out_values_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"output pointer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_insert_stmt = NULL;
	*out_values_stmt = NULL;
	status = sqlparser_get_insert_stmt(handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_insert_source_from_stmt(insert_stmt) != SQLPARSER_INSERT_SOURCE_VALUES) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"INSERT source is not VALUES");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_insert_stmt = insert_stmt;
	*out_values_stmt = insert_stmt->select_stmt->select_stmt;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_fill_relation_view(
	const PgQuery__RangeVar *relation,
	sqlparser_relation_view_t *out_relation)
{
	sqlparser_relation_view_clear(out_relation);
	if (relation == NULL || out_relation == NULL) {
		return;
	}

	out_relation->schema_name =
		relation->schemaname != NULL && relation->schemaname[0] != '\0'
			? relation->schemaname
			: NULL;
	out_relation->table_name =
		relation->relname != NULL && relation->relname[0] != '\0' ? relation->relname : NULL;
	out_relation->alias_name =
		relation->alias != NULL &&
				relation->alias->aliasname != NULL &&
				relation->alias->aliasname[0] != '\0'
			? relation->alias->aliasname
			: NULL;
}

static sqlparser_status_t sqlparser_fill_literal_view_from_a_const(
	const PgQuery__AConst *a_const,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_literal_view_clear(out_literal);
	if (a_const == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"literal node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	if (a_const->isnull) {
		out_literal->kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}

	switch (a_const->val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
			out_literal->kind = SQLPARSER_LITERAL_KIND_STRING;
			out_literal->string_value =
				a_const->sval != NULL ? a_const->sval->sval : NULL;
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__A__CONST__VAL_IVAL:
			out_literal->kind = SQLPARSER_LITERAL_KIND_INTEGER;
			out_literal->integer_value =
				a_const->ival != NULL ? (long long)a_const->ival->ival : 0LL;
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__A__CONST__VAL_FVAL:
			out_literal->kind = SQLPARSER_LITERAL_KIND_FLOAT;
			out_literal->float_value =
				a_const->fval != NULL ? a_const->fval->fval : NULL;
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__A__CONST__VAL_BOOLVAL:
			out_literal->kind = SQLPARSER_LITERAL_KIND_BOOLEAN;
			out_literal->boolean_value =
				a_const->boolval != NULL && a_const->boolval->boolval ? 1 : 0;
			return SQLPARSER_STATUS_OK;
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"literal kind is not supported");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

static int sqlparser_message_search_accept(
	sqlparser_message_search_t *search,
	ProtobufCMessage *message)
{
	if (search == NULL || message == NULL) {
		return 0;
	}
	if (message->descriptor != search->descriptor) {
		return 0;
	}
	if (search->accept != NULL && !search->accept(message)) {
		return 0;
	}

	return 1;
}

static int sqlparser_message_is_name_container(const ProtobufCMessageDescriptor *descriptor)
{
	const char *short_name;

	if (descriptor == NULL || descriptor->short_name == NULL) {
		return 0;
	}

	short_name = descriptor->short_name;
	return strcmp(short_name, "Node") == 0 || strcmp(short_name, "List") == 0 ||
	       strcmp(short_name, "String") == 0 || strcmp(short_name, "Float") == 0 ||
	       strcmp(short_name, "BitString") == 0;
}

static int sqlparser_name_context_is_literal(const sqlparser_name_context_t *context)
{
	return context != NULL && context->owner_type != NULL &&
	       strcmp(context->owner_type, "AConst") == 0;
}

static sqlparser_name_context_t sqlparser_next_name_context(
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	const sqlparser_name_context_t *context)
{
	sqlparser_name_context_t next_context;

	memset(&next_context, 0, sizeof(next_context));
	if (sqlparser_message_is_name_container(descriptor)) {
		if (context != NULL) {
			next_context = *context;
		}
		return next_context;
	}

	if (descriptor != NULL) {
		next_context.owner_type = descriptor->short_name;
	}
	if (field != NULL) {
		next_context.field_name = field->name;
	}
	return next_context;
}

static sqlparser_status_t sqlparser_record_name_atom(
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	char **slot,
	sqlparser_name_search_t *search)
{
	const char *owner_type;
	const char *field_name;

	if (slot == NULL || search == NULL || *slot == NULL || (*slot)[0] == '\0') {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_name_context_is_literal(context)) {
		return SQLPARSER_STATUS_OK;
	}

	owner_type = NULL;
	field_name = NULL;
	if (sqlparser_message_is_name_container(descriptor) && context != NULL) {
		owner_type = context->owner_type;
		field_name = context->field_name;
	}
	if (owner_type == NULL && descriptor != NULL) {
		owner_type = descriptor->short_name;
	}
	if (field_name == NULL && field != NULL) {
		field_name = field->name;
	}

	if (owner_type == NULL || field_name == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	if (!search->want_target) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->seen != search->target_index) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->name_view != NULL) {
		search->name_view->owner_type = owner_type;
		search->name_view->field_name = field_name;
		search->name_view->value = *slot;
	}
	search->target_slot = slot;
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_message_tree(
	ProtobufCMessage *message,
	sqlparser_message_search_t *search,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	const uint8_t *base;
	unsigned index;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->target_message != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	if (sqlparser_message_search_accept(search, message)) {
		if (!search->want_target) {
			search->seen++;
		} else if (search->seen == search->target_index) {
			search->target_message = message;
			search->seen++;
			return SQLPARSER_STATUS_OK;
		} else {
			search->seen++;
		}
	}

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	base = (const uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}

		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage *const *items;
			size_t item_index;

			item_count = *(const size_t *)(base + field->quantifier_offset);
			items = *(ProtobufCMessage *const * const *)(base + field->offset);
			for (item_index = 0U; item_index < item_count; item_index++) {
				sqlparser_status_t status;

				status = sqlparser_walk_message_tree(items[item_index], search, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_message != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			continue;
		}

		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		{
			ProtobufCMessage *child;
			sqlparser_status_t status;

			child = *(ProtobufCMessage **)(base + field->offset);
			if (child == NULL) {
				continue;
			}

			status = sqlparser_walk_message_tree(child, search, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (search->want_target && search->target_message != NULL) {
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

/*
 * 通用名称层只关心“非 literal 的字符串原子”。
 * 通过 protobuf-c 反射递归遍历，既覆盖 DDL 对象名，也覆盖列引用、别名等节点。
 */
static sqlparser_status_t sqlparser_walk_message_names(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_name_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->target_slot != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	base = (uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		if (field->type == PROTOBUF_C_TYPE_STRING) {
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				char **items;
				size_t item_index;
				sqlparser_status_t status;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(char ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					status = sqlparser_record_name_atom(
						context,
						descriptor,
						field,
						&items[item_index],
						search);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					if (search->want_target && search->target_slot != NULL) {
						return SQLPARSER_STATUS_OK;
					}
				}
			} else {
				sqlparser_status_t status;

				status = sqlparser_record_name_atom(
					context,
					descriptor,
					field,
					(char **)(base + field->offset),
					search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			continue;
		}

		if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			sqlparser_name_context_t next_context;

			next_context = sqlparser_next_name_context(descriptor, field, context);
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				ProtobufCMessage **items;
				size_t item_index;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(ProtobufCMessage ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					sqlparser_status_t status;

					status = sqlparser_walk_message_names(
						items[item_index],
						&next_context,
						search);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					if (search->want_target && search->target_slot != NULL) {
						return SQLPARSER_STATUS_OK;
					}
				}
			} else {
				ProtobufCMessage *child;
				sqlparser_status_t status;

				child = *(ProtobufCMessage **)(base + field->offset);
				if (child == NULL) {
					continue;
				}

				status = sqlparser_walk_message_names(child, &next_context, search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

static int sqlparser_node_string_value(const PgQuery__Node *node, const char **out_text)
{
	if (out_text == NULL) {
		return 0;
	}

	*out_text = NULL;
	if (node == NULL) {
		return 0;
	}

	if (node->node_case == PG_QUERY__NODE__NODE_STRING &&
	    node->string != NULL &&
	    node->string->sval != NULL &&
	    node->string->sval[0] != '\0') {
		*out_text = node->string->sval;
		return 1;
	}

	return 0;
}

static int sqlparser_extract_column_ref_parts(
	const PgQuery__ColumnRef *column_ref,
	const char **table_name_out,
	const char **column_name_out)
{
	size_t part_count;
	size_t index;
	const char *parts[4];

	if (table_name_out != NULL) {
		*table_name_out = NULL;
	}
	if (column_name_out != NULL) {
		*column_name_out = NULL;
	}
	if (column_ref == NULL || column_ref->n_fields == 0U) {
		return 0;
	}

	part_count = 0U;
	for (index = 0U; index < column_ref->n_fields && part_count < 4U; index++) {
		const char *text;

		text = NULL;
		if (!sqlparser_node_string_value(column_ref->fields[index], &text)) {
			continue;
		}
		parts[part_count++] = text;
	}

	if (part_count == 0U) {
		return 0;
	}

	if (column_name_out != NULL) {
		*column_name_out = parts[part_count - 1U];
	}
	if (table_name_out != NULL && part_count >= 2U) {
		*table_name_out = parts[part_count - 2U];
	}

	return 1;
}

static int sqlparser_try_extract_column_ref(
	const PgQuery__Node *node,
	const char **table_name_out,
	const char **column_name_out)
{
	if (table_name_out != NULL) {
		*table_name_out = NULL;
	}
	if (column_name_out != NULL) {
		*column_name_out = NULL;
	}
	if (node == NULL) {
		return 0;
	}

	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_COLUMN_REF:
			return sqlparser_extract_column_ref_parts(
				node->column_ref,
				table_name_out,
				column_name_out);
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			if (node->type_cast == NULL) {
				return 0;
			}
			return sqlparser_try_extract_column_ref(
				node->type_cast->arg,
				table_name_out,
				column_name_out);
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			if (node->collate_clause == NULL) {
				return 0;
			}
			return sqlparser_try_extract_column_ref(
				node->collate_clause->arg,
				table_name_out,
				column_name_out);
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			if (node->a_indirection == NULL) {
				return 0;
			}
			return sqlparser_try_extract_column_ref(
				node->a_indirection->arg,
				table_name_out,
				column_name_out);
		default:
			return 0;
	}
}

static const char *sqlparser_a_expr_operator_name(const PgQuery__AExpr *a_expr)
{
	const char *text;
	size_t index;

	if (a_expr == NULL) {
		return NULL;
	}

	switch (a_expr->kind) {
		case PG_QUERY__A__EXPR__KIND__AEXPR_IN:
			return "IN";
		case PG_QUERY__A__EXPR__KIND__AEXPR_LIKE:
			return "LIKE";
		case PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE:
			return "ILIKE";
		case PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR:
			return "SIMILAR";
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN:
			return "BETWEEN";
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN:
			return "NOT BETWEEN";
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN_SYM:
			return "BETWEEN SYMMETRIC";
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN_SYM:
			return "NOT BETWEEN SYMMETRIC";
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP_ANY:
			return "ANY";
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP_ALL:
			return "ALL";
		case PG_QUERY__A__EXPR__KIND__AEXPR_DISTINCT:
			return "IS DISTINCT FROM";
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_DISTINCT:
			return "IS NOT DISTINCT FROM";
		case PG_QUERY__A__EXPR__KIND__AEXPR_NULLIF:
			return "NULLIF";
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP:
		case PG_QUERY__A__EXPR__KIND__A_EXPR_KIND_UNDEFINED:
		default:
			break;
	}

	text = NULL;
	for (index = a_expr->n_name; index > 0U; index--) {
		if (sqlparser_node_string_value(a_expr->name[index - 1U], &text)) {
			return text;
		}
	}

	return NULL;
}

static void sqlparser_free_proto_string(char *text)
{
	if (text != NULL && text != (char *)protobuf_c_empty_string) {
		free(text);
	}
}

static sqlparser_status_t sqlparser_replace_proto_string(
	char **slot,
	const char *value,
	sqlparser_error_t *out_error)
{
	char *copy;

	if (slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"string slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	copy = sqlparser_strdup(value != NULL ? value : "");
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	sqlparser_free_proto_string(*slot);
	*slot = copy;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_free_a_const_value(PgQuery__AConst *a_const)
{
	if (a_const == NULL) {
		return;
	}

	switch (a_const->val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
			if (a_const->sval != NULL) {
				sqlparser_free_proto_string(a_const->sval->sval);
				free(a_const->sval);
			}
			break;
		case PG_QUERY__A__CONST__VAL_IVAL:
			free(a_const->ival);
			break;
		case PG_QUERY__A__CONST__VAL_FVAL:
			if (a_const->fval != NULL) {
				sqlparser_free_proto_string(a_const->fval->fval);
				free(a_const->fval);
			}
			break;
		case PG_QUERY__A__CONST__VAL_BOOLVAL:
			free(a_const->boolval);
			break;
		case PG_QUERY__A__CONST__VAL_BSVAL:
			if (a_const->bsval != NULL) {
				sqlparser_free_proto_string(a_const->bsval->bsval);
				free(a_const->bsval);
			}
			break;
		case PG_QUERY__A__CONST__VAL__NOT_SET:
		default:
			break;
	}

	a_const->val_case = PG_QUERY__A__CONST__VAL__NOT_SET;
	a_const->ival = NULL;
}

typedef struct {
	PgQuery__AConst__ValCase val_case;
	PgQuery__Integer *ival;
	PgQuery__Float *fval;
	PgQuery__Boolean *boolval;
	PgQuery__String *sval;
} sqlparser_literal_payload_t;

static void sqlparser_literal_payload_clear(sqlparser_literal_payload_t *payload)
{
	if (payload == NULL) {
		return;
	}

	memset(payload, 0, sizeof(*payload));
	payload->val_case = PG_QUERY__A__CONST__VAL__NOT_SET;
}

static void sqlparser_literal_payload_free(sqlparser_literal_payload_t *payload)
{
	if (payload == NULL) {
		return;
	}

	if (payload->sval != NULL) {
		sqlparser_free_proto_string(payload->sval->sval);
		free(payload->sval);
	}
	if (payload->fval != NULL) {
		sqlparser_free_proto_string(payload->fval->fval);
		free(payload->fval);
	}
	free(payload->ival);
	free(payload->boolval);
	sqlparser_literal_payload_clear(payload);
}

static sqlparser_status_t sqlparser_literal_payload_build(
	const sqlparser_literal_value_t *value,
	sqlparser_literal_payload_t *payload,
	sqlparser_error_t *out_error)
{
	if (value == NULL || payload == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_literal_payload_clear(payload);
	switch (value->kind) {
		case SQLPARSER_LITERAL_KIND_NULL:
			return SQLPARSER_STATUS_OK;
		case SQLPARSER_LITERAL_KIND_STRING:
			if (value->string_value == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INVALID_ARGUMENT,
					"string literal requires string_value");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			payload->sval = (PgQuery__String *)malloc(sizeof(*payload->sval));
			if (payload->sval == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			pg_query__string__init(payload->sval);
			payload->sval->sval = sqlparser_strdup(value->string_value);
			if (payload->sval->sval == NULL) {
				sqlparser_literal_payload_free(payload);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			payload->val_case = PG_QUERY__A__CONST__VAL_SVAL;
			return SQLPARSER_STATUS_OK;
		case SQLPARSER_LITERAL_KIND_INTEGER:
			if (value->integer_value > INT_MAX || value->integer_value < INT_MIN) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"integer literal is out of supported range");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			payload->ival = (PgQuery__Integer *)malloc(sizeof(*payload->ival));
			if (payload->ival == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			pg_query__integer__init(payload->ival);
			payload->ival->ival = (int32_t)value->integer_value;
			payload->val_case = PG_QUERY__A__CONST__VAL_IVAL;
			return SQLPARSER_STATUS_OK;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			if (value->float_value == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INVALID_ARGUMENT,
					"float literal requires float_value");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			payload->fval = (PgQuery__Float *)malloc(sizeof(*payload->fval));
			if (payload->fval == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			pg_query__float__init(payload->fval);
			payload->fval->fval = sqlparser_strdup(value->float_value);
			if (payload->fval->fval == NULL) {
				sqlparser_literal_payload_free(payload);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			payload->val_case = PG_QUERY__A__CONST__VAL_FVAL;
			return SQLPARSER_STATUS_OK;
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			payload->boolval = (PgQuery__Boolean *)malloc(sizeof(*payload->boolval));
			if (payload->boolval == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			pg_query__boolean__init(payload->boolval);
			payload->boolval->boolval = value->boolean_value ? 1 : 0;
			payload->val_case = PG_QUERY__A__CONST__VAL_BOOLVAL;
			return SQLPARSER_STATUS_OK;
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"literal kind is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
}

static sqlparser_status_t sqlparser_a_const_set_literal(
	PgQuery__AConst *a_const,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_literal_payload_t payload;
	sqlparser_status_t status;

	if (a_const == NULL || value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_literal_payload_clear(&payload);
	status = sqlparser_literal_payload_build(value, &payload, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	sqlparser_free_a_const_value(a_const);
	a_const->isnull = value->kind == SQLPARSER_LITERAL_KIND_NULL ? 1 : 0;
	a_const->val_case = payload.val_case;
	switch (payload.val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
			a_const->sval = payload.sval;
			payload.sval = NULL;
			break;
		case PG_QUERY__A__CONST__VAL_IVAL:
			a_const->ival = payload.ival;
			payload.ival = NULL;
			break;
		case PG_QUERY__A__CONST__VAL_FVAL:
			a_const->fval = payload.fval;
			payload.fval = NULL;
			break;
		case PG_QUERY__A__CONST__VAL_BOOLVAL:
			a_const->boolval = payload.boolval;
			payload.boolval = NULL;
			break;
		case PG_QUERY__A__CONST__VAL__NOT_SET:
		default:
			break;
	}

	sqlparser_literal_payload_free(&payload);
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_a_const_is_supported(const PgQuery__AConst *a_const)
{
	if (a_const == NULL) {
		return 0;
	}

	if (a_const->isnull) {
		return 1;
	}

	switch (a_const->val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
		case PG_QUERY__A__CONST__VAL_IVAL:
		case PG_QUERY__A__CONST__VAL_FVAL:
		case PG_QUERY__A__CONST__VAL_BOOLVAL:
			return 1;
		default:
			return 0;
	}
}

static int sqlparser_message_accept_supported_a_const(ProtobufCMessage *message)
{
	PgQuery__AConst *a_const;

	if (message == NULL || message->descriptor != &pg_query__a__const__descriptor) {
		return 0;
	}

	a_const = (PgQuery__AConst *)message;
	return sqlparser_a_const_is_supported(a_const);
}

static sqlparser_status_t sqlparser_record_where_literal(
	PgQuery__AConst *a_const,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (!sqlparser_a_const_is_supported(a_const)) {
		return SQLPARSER_STATUS_OK;
	}

	if (!search->want_target) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->seen != search->target_index) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->literal_view != NULL) {
		search->literal_view->table_name = context != NULL ? context->table_name : NULL;
		search->literal_view->column_name = context != NULL ? context->column_name : NULL;
		search->literal_view->operator_name = context != NULL ? context->operator_name : NULL;
		status = sqlparser_fill_literal_view_from_a_const(
			a_const,
			&search->literal_view->literal,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	search->literal_node = a_const;
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_expression_literals(
	PgQuery__Node *node,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_walk_select_stmt_where_literals(
	PgQuery__SelectStmt *select_stmt,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_walk_node_array_literals(
	size_t item_count,
	PgQuery__Node **items,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	for (index = 0U; index < item_count; index++) {
		status = sqlparser_walk_expression_literals(items[index], context, search, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (search->want_target && search->literal_node != NULL) {
			return SQLPARSER_STATUS_OK;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_select_stmt_where_literals(
	PgQuery__SelectStmt *select_stmt,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (select_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	if (select_stmt->where_clause != NULL) {
		status = sqlparser_walk_expression_literals(
			select_stmt->where_clause,
			context,
			search,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (select_stmt->larg != NULL) {
		status = sqlparser_walk_select_stmt_where_literals(
			select_stmt->larg,
			context,
			search,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (select_stmt->rarg != NULL) {
		status = sqlparser_walk_select_stmt_where_literals(
			select_stmt->rarg,
			context,
			search,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_expression_literals(
	PgQuery__Node *node,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (node == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->literal_node != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_A_CONST:
			return sqlparser_record_where_literal(node->a_const, context, search, out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
			if (node->a_expr != NULL) {
				sqlparser_predicate_context_t left_context;
				sqlparser_predicate_context_t right_context;
				const char *left_table;
				const char *left_column;
				const char *right_table;
				const char *right_column;
				const char *operator_name;

				left_context = context != NULL ? *context : (sqlparser_predicate_context_t){0};
				right_context = left_context;
				left_table = NULL;
				left_column = NULL;
				right_table = NULL;
				right_column = NULL;
				operator_name = sqlparser_a_expr_operator_name(node->a_expr);
				left_context.operator_name = operator_name;
				right_context.operator_name = operator_name;

				if (sqlparser_try_extract_column_ref(
					    node->a_expr->lexpr,
					    &left_table,
					    &left_column)) {
					right_context.table_name = left_table;
					right_context.column_name = left_column;
				}
				if (sqlparser_try_extract_column_ref(
					    node->a_expr->rexpr,
					    &right_table,
					    &right_column)) {
					left_context.table_name = right_table;
					left_context.column_name = right_column;
				}

				status = sqlparser_walk_expression_literals(
					node->a_expr->lexpr,
					&left_context,
					search,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				return sqlparser_walk_expression_literals(
					node->a_expr->rexpr,
					&right_context,
					search,
					out_error);
			}
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			if (node->bool_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->bool_expr->n_args,
				node->bool_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			if (node->type_cast == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->type_cast->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			if (node->collate_clause == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->collate_clause->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_RELABEL_TYPE:
			if (node->relabel_type == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->relabel_type->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_COERCE_VIA_IO:
			if (node->coerce_via_io == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->coerce_via_io->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			if (node->a_indirection == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_walk_expression_literals(
				node->a_indirection->arg,
				context,
				search,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			return sqlparser_walk_node_array_literals(
				node->a_indirection->n_indirection,
				node->a_indirection->indirection,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			if (node->a_array_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->a_array_expr->n_elements,
				node->a_array_expr->elements,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			if (node->row_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->row_expr->n_args,
				node->row_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_LIST:
			if (node->list == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->list->n_items,
				node->list->items,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->func_call->n_args,
				node->func_call->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_NULL_TEST:
			if (node->null_test == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->null_test->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			if (node->boolean_test == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->boolean_test->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			if (node->coalesce_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->coalesce_expr->n_args,
				node->coalesce_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			if (node->min_max_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->min_max_expr->n_args,
				node->min_max_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_walk_expression_literals(
				node->sub_link->testexpr,
				context,
				search,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			return sqlparser_walk_expression_literals(
				node->sub_link->subselect,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_walk_select_stmt_where_literals(
				node->select_stmt,
				context,
				search,
				out_error);
		default:
			return SQLPARSER_STATUS_OK;
	}
}

static sqlparser_status_t sqlparser_get_insert_column_res_target(
	PgQuery__InsertStmt *stmt,
	size_t column_index,
	PgQuery__ResTarget **out_target,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *column_node;

	if (out_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_target = NULL;
	if (stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (column_index >= stmt->n_cols) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	column_node = stmt->cols[column_index];
	if (column_node == NULL ||
	    column_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    column_node->res_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert column node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_target = column_node->res_target;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_a_const(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__AConst **out_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *row_node;
	PgQuery__List *row_list;
	PgQuery__Node *value_node;
	sqlparser_status_t status;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_literal = NULL;
	status = sqlparser_get_insert_values_stmt(
		handle,
		statement_index,
		&insert_stmt,
		&values_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (row_index >= values_stmt->n_values_lists) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"row_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	row_node = values_stmt->values_lists[row_index];
	if (row_node == NULL ||
	    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
	    row_node->list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert row node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	row_list = row_node->list;
	if (column_index >= row_list->n_items) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	value_node = row_list->items[column_index];
	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    value_node->a_const == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"insert cell is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_literal = value_node->a_const;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_update_assignment_res_target(
	PgQuery__UpdateStmt *stmt,
	size_t assignment_index,
	PgQuery__ResTarget **out_target,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *target_node;

	if (out_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_target = NULL;
	if (stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (assignment_index >= stmt->n_target_list) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	target_node = stmt->target_list[assignment_index];
	if (target_node == NULL ||
	    target_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    target_node->res_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"update assignment node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_target = target_node->res_target;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_statement_kind_t *out_kind,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_kind == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_kind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_kind = SQLPARSER_STATEMENT_KIND_UNKNOWN;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_kind = sqlparser_statement_kind_from_case(statement->node_case);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_node_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char **out_name,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_name = NULL;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_name = sqlparser_statement_node_name_from_case(statement->node_case);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_target_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	PgQuery__RangeVar *relation;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_relation_view_clear(out_relation);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	relation = NULL;
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (statement->insert_stmt != NULL) {
				relation = statement->insert_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			if (statement->update_stmt != NULL) {
				relation = statement->update_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (statement->delete_stmt != NULL) {
				relation = statement->delete_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			if (statement->merge_stmt != NULL) {
				relation = statement->merge_stmt->relation;
			}
			break;
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"statement does not expose a single target relation");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"target relation is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	sqlparser_fill_relation_view(relation, out_relation);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_relation_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	return sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		0,
		0U,
		out_count,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_statement_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (out_relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_relation_view_clear(out_relation);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	message = NULL;
	status = sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		relation_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_fill_relation_view((PgQuery__RangeVar *)message, out_relation);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_relation_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error)
{
	PgQuery__RangeVar *relation;
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (table_name == NULL || table_name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"table_name must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_error_clear(out_error);
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		relation_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	relation = (PgQuery__RangeVar *)message;
	status = sqlparser_replace_proto_string(&relation->relname, table_name, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_replace_proto_string(
		&relation->schemaname,
		schema_name != NULL ? schema_name : "",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_statement_name_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	sqlparser_name_view_t *out_name,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_name_view_clear(out_name);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = name_index;
	search.name_view = out_name;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;

	if (value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_error_clear(out_error);
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = name_index;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_replace_proto_string(search.target_slot, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_insert_source_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_insert_source_kind_t *out_kind,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_kind == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_kind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_kind = SQLPARSER_INSERT_SOURCE_UNKNOWN;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_kind = sqlparser_insert_source_from_stmt(insert_stmt);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_column_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = insert_stmt->n_cols;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_column_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t column_index,
	const char **out_column_name,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_column_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_column_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_column_name = NULL;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_insert_column_res_target(insert_stmt, column_index, &target, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_column_name = target->name != NULL ? target->name : NULL;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_row_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_insert_source_from_stmt(insert_stmt) == SQLPARSER_INSERT_SOURCE_VALUES &&
	    insert_stmt->select_stmt != NULL &&
	    insert_stmt->select_stmt->select_stmt != NULL) {
		*out_count = insert_stmt->select_stmt->select_stmt->n_values_lists;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_cell_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *literal;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_literal_view_clear(out_literal);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_cell_a_const(
		mutable_handle,
		statement_index,
		row_index,
		column_index,
		&literal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_fill_literal_view_from_a_const(literal, out_literal, out_error);
}

sqlparser_status_t sqlparser_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *literal;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	status = sqlparser_get_insert_cell_a_const(
		handle,
		statement_index,
		row_index,
		column_index,
		&literal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_a_const_set_literal(literal, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_update_assignment_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_update_stmt(mutable_handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = update_stmt->n_target_list;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_assignment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_assignment == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_assignment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_assignment_view_clear(out_assignment);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_update_stmt(mutable_handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	out_assignment->column_name = target->name != NULL ? target->name : NULL;
	if (target->val == NULL) {
		out_assignment->value_kind = SQLPARSER_VALUE_KIND_UNKNOWN;
		return SQLPARSER_STATUS_OK;
	}

	if (target->val->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
		out_assignment->value_kind = SQLPARSER_VALUE_KIND_DEFAULT;
		return SQLPARSER_STATUS_OK;
	}

	if (target->val->node_case == PG_QUERY__NODE__NODE_A_CONST &&
	    target->val->a_const != NULL) {
		out_assignment->value_kind = SQLPARSER_VALUE_KIND_LITERAL;
		return sqlparser_fill_literal_view_from_a_const(
			target->val->a_const,
			&out_assignment->literal,
			out_error);
	}

	out_assignment->value_kind = SQLPARSER_VALUE_KIND_EXPRESSION;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_set_assignment_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (target->val == NULL ||
	    target->val->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    target->val->a_const == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"update assignment is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = sqlparser_a_const_set_literal(target->val->a_const, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_statement_where_literal_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_where_literal_search_t search;
	PgQuery__Node *where_clause;
	sqlparser_status_t status;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_where_clause(
		mutable_handle,
		statement_index,
		&where_clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (where_clause == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&search, 0, sizeof(search));
	status = sqlparser_walk_expression_literals(where_clause, NULL, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_where_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	sqlparser_where_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_where_literal_search_t search;
	PgQuery__Node *where_clause;
	sqlparser_status_t status;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_where_literal_view_clear(out_literal);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_where_clause(
		mutable_handle,
		statement_index,
		&where_clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (where_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = literal_index;
	search.literal_view = out_literal;
	status = sqlparser_walk_expression_literals(where_clause, NULL, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (search.literal_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_where_set_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_where_literal_search_t search;
	PgQuery__Node *where_clause;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	status = sqlparser_get_statement_where_clause(
		handle,
		statement_index,
		&where_clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (where_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = literal_index;
	status = sqlparser_walk_expression_literals(where_clause, NULL, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (search.literal_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_a_const_set_literal(search.literal_node, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_statement_literal_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	return sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__a__const__descriptor,
		sqlparser_message_accept_supported_a_const,
		0,
		0U,
		out_count,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_statement_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_literal_view_clear(out_literal);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	message = NULL;
	status = sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__a__const__descriptor,
		sqlparser_message_accept_supported_a_const,
		1,
		literal_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_fill_literal_view_from_a_const((PgQuery__AConst *)message, out_literal, out_error);
}

sqlparser_status_t sqlparser_statement_set_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__a__const__descriptor,
		sqlparser_message_accept_supported_a_const,
		1,
		literal_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_a_const_set_literal((PgQuery__AConst *)message, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}
