#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

void sqlparser_relation_view_clear(sqlparser_relation_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
}

void sqlparser_literal_view_clear(sqlparser_literal_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
	view->kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
}

void sqlparser_assignment_view_clear(sqlparser_assignment_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
	view->value_kind = SQLPARSER_VALUE_KIND_UNKNOWN;
	view->literal.kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
}

void sqlparser_where_literal_view_clear(sqlparser_where_literal_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
	view->literal.kind = SQLPARSER_LITERAL_KIND_UNKNOWN;
}

void sqlparser_name_view_clear(sqlparser_name_view_t *view)
{
	if (view == NULL) {
		return;
	}

	memset(view, 0, sizeof(*view));
}

static int sqlparser_quoted_identifier_token_matches(
	const char *parser_sql,
	int32_t location,
	const char *value)
{
	size_t pos;
	size_t len;
	size_t value_pos;

	if (parser_sql == NULL || location < 0 || value == NULL) {
		return 0;
	}

	pos = (size_t)location;
	len = strlen(parser_sql);
	if (pos >= len) {
		return 0;
	}
	if (parser_sql[pos] != '"') {
		return 0;
	}

	pos++;
	value_pos = 0U;
	while (pos < len) {
		if (parser_sql[pos] == '"') {
			if (pos + 1U < len && parser_sql[pos + 1U] == '"') {
				if (value[value_pos] != '"') {
					return 0;
				}
				value_pos++;
				pos += 2U;
				continue;
			}
			return value[value_pos] == '\0';
		}
		if (value[value_pos] == '\0' || parser_sql[pos] != value[value_pos]) {
			return 0;
		}
		value_pos++;
		pos++;
	}

	return 0;
}

const char *sqlparser_statement_node_name_from_case(PgQuery__Node__NodeCase node_case)
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
		case PG_QUERY__NODE__NODE_INDEX_STMT:
			return "IndexStmt";
		case PG_QUERY__NODE__NODE_CREATE_SCHEMA_STMT:
			return "CreateSchemaStmt";
		case PG_QUERY__NODE__NODE_CREATE_SEQ_STMT:
			return "CreateSeqStmt";
		case PG_QUERY__NODE__NODE_ALTER_SEQ_STMT:
			return "AlterSeqStmt";
		case PG_QUERY__NODE__NODE_GRANT_STMT:
			return "GrantStmt";
		case PG_QUERY__NODE__NODE_LOCK_STMT:
			return "LockStmt";
		case PG_QUERY__NODE__NODE_RENAME_STMT:
			return "RenameStmt";
		case PG_QUERY__NODE__NODE_VARIABLE_SET_STMT:
			return "VariableSetStmt";
		case PG_QUERY__NODE__NODE_PREPARE_STMT:
			return "PrepareStmt";
		case PG_QUERY__NODE__NODE_EXECUTE_STMT:
			return "ExecuteStmt";
		case PG_QUERY__NODE__NODE_DEALLOCATE_STMT:
			return "DeallocateStmt";
		default:
			return "OtherStmt";
	}
}

sqlparser_statement_kind_t sqlparser_statement_kind_from_case(PgQuery__Node__NodeCase node_case)
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
		case PG_QUERY__NODE__NODE_CREATE_SCHEMA_STMT:
		case PG_QUERY__NODE__NODE_CREATE_SEQ_STMT:
		case PG_QUERY__NODE__NODE_ALTER_SEQ_STMT:
		case PG_QUERY__NODE__NODE_ALTER_TABLE_STMT:
		case PG_QUERY__NODE__NODE_TRUNCATE_STMT:
		case PG_QUERY__NODE__NODE_INDEX_STMT:
		case PG_QUERY__NODE__NODE_GRANT_STMT:
		case PG_QUERY__NODE__NODE_COMMENT_STMT:
		case PG_QUERY__NODE__NODE_RENAME_STMT:
			return SQLPARSER_STATEMENT_KIND_DDL;
		case PG_QUERY__NODE__NODE_PREPARE_STMT:
		case PG_QUERY__NODE__NODE_EXECUTE_STMT:
		case PG_QUERY__NODE__NODE_DEALLOCATE_STMT:
			return SQLPARSER_STATEMENT_KIND_OTHER;
		case PG_QUERY__NODE__NODE__NOT_SET:
			return SQLPARSER_STATEMENT_KIND_UNKNOWN;
		default:
			return SQLPARSER_STATEMENT_KIND_OTHER;
	}
}

sqlparser_status_t sqlparser_get_statement_node(
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

sqlparser_status_t sqlparser_get_insert_stmt(
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

sqlparser_status_t sqlparser_get_update_stmt(
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

sqlparser_status_t sqlparser_get_statement_where_clause(
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

sqlparser_status_t sqlparser_search_statement_messages(
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

sqlparser_insert_source_kind_t sqlparser_insert_source_from_stmt(PgQuery__InsertStmt *stmt)
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

sqlparser_status_t sqlparser_get_insert_values_stmt(
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

void sqlparser_fill_relation_view(
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
	out_relation->database_name =
		relation->catalogname != NULL && relation->catalogname[0] != '\0'
			? relation->catalogname
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

sqlparser_status_t sqlparser_fill_literal_view_from_a_const(
	const PgQuery__AConst *a_const,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	return sqlparser_fill_literal_view_from_a_const_with_sql(a_const, NULL, out_literal, out_error);
}

sqlparser_status_t sqlparser_fill_literal_view_from_a_const_with_sql(
	const PgQuery__AConst *a_const,
	const char *parser_sql,
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
			out_literal->quoted_identifier =
				sqlparser_quoted_identifier_token_matches(
					parser_sql,
					a_const->location,
					out_literal->string_value);
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

	if (search->match_slot != NULL) {
		if (slot == search->match_slot) {
			if (search->name_view != NULL) {
				search->name_view->owner_type = owner_type;
				search->name_view->field_name = field_name;
				search->name_view->value = *slot;
			}
			search->target_slot = slot;
			search->target_index = search->seen;
		}
		search->seen++;
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

sqlparser_status_t sqlparser_walk_message_tree(
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
sqlparser_status_t sqlparser_walk_message_names(
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

sqlparser_status_t sqlparser_find_statement_name_index_by_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	char **slot,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;

	if (out_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = 0U;
	if (slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.match_slot = slot;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name slot is not part of the statement");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_index = search.target_index;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_descriptor_is_node(const ProtobufCMessageDescriptor *descriptor)
{
	return descriptor != NULL &&
		descriptor->short_name != NULL &&
		strcmp(descriptor->short_name, "Node") == 0;
}

static sqlparser_status_t sqlparser_record_node_slot(
	PgQuery__Node **slot,
	sqlparser_node_slot_search_t *search)
{
	if (slot == NULL || search == NULL || *slot == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->match_node != NULL) {
		if (*slot == search->match_node) {
			search->target_slot = slot;
			search->target_index = search->seen;
		}
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}
	if (!search->want_target) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}
	if (search->seen == search->target_index) {
		search->target_slot = slot;
	}
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_message_node_slots(
	ProtobufCMessage *message,
	sqlparser_node_slot_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if ((search->want_target || search->match_node != NULL) && search->target_slot != NULL) {
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
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage **items;
			size_t item_index;

			item_count = *(const size_t *)(base + field->quantifier_offset);
			items = *(ProtobufCMessage ***)(base + field->offset);
			for (item_index = 0U; item_index < item_count; item_index++) {
				ProtobufCMessage *child;
				sqlparser_status_t status;

				child = items != NULL ? items[item_index] : NULL;
				if (child == NULL) {
					continue;
				}
				if (sqlparser_descriptor_is_node(child->descriptor)) {
					status = sqlparser_record_node_slot((PgQuery__Node **)&items[item_index], search);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					if ((search->want_target || search->match_node != NULL) && search->target_slot != NULL) {
						return SQLPARSER_STATUS_OK;
					}
				}
				status = sqlparser_walk_message_node_slots(child, search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if ((search->want_target || search->match_node != NULL) && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			continue;
		}

		{
			ProtobufCMessage **child_slot;
			ProtobufCMessage *child;
			sqlparser_status_t status;

			child_slot = (ProtobufCMessage **)(base + field->offset);
			child = child_slot != NULL ? *child_slot : NULL;
			if (child == NULL) {
				continue;
			}
			if (sqlparser_descriptor_is_node(child->descriptor)) {
				status = sqlparser_record_node_slot((PgQuery__Node **)child_slot, search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if ((search->want_target || search->match_node != NULL) && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			status = sqlparser_walk_message_node_slots(child, search);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if ((search->want_target || search->match_node != NULL) && search->target_slot != NULL) {
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_find_statement_node_index_by_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *node,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_node_slot_search_t search;
	sqlparser_status_t status;

	if (out_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = 0U;
	if (node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&search, 0, sizeof(search));
	search.match_node = node;
	status = sqlparser_walk_message_node_slots((ProtobufCMessage *)statement, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "node is not part of the statement");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = search.target_index;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_get_statement_node_slot_by_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t node_index,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_node_slot_search_t search;
	sqlparser_status_t status;

	if (out_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_slot = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = node_index;
	status = sqlparser_walk_message_node_slots((ProtobufCMessage *)statement, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "node_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_slot = search.target_slot;
	return SQLPARSER_STATUS_OK;
}

int sqlparser_node_string_value(const PgQuery__Node *node, const char **out_text)
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

int sqlparser_try_extract_column_ref(
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

static int sqlparser_a_expr_name_is(const PgQuery__AExpr *a_expr, const char *expected)
{
	const char *text;
	size_t index;

	if (a_expr == NULL || expected == NULL) {
		return 0;
	}
	for (index = 0U; index < a_expr->n_name; index++) {
		if (sqlparser_node_string_value(a_expr->name[index], &text) &&
		    strcmp(text, expected) == 0) {
			return 1;
		}
	}
	return 0;
}

int sqlparser_a_expr_is_not_in(const PgQuery__AExpr *a_expr)
{
	if (a_expr == NULL || a_expr->kind != PG_QUERY__A__EXPR__KIND__AEXPR_IN) {
		return 0;
	}
	return sqlparser_a_expr_name_is(a_expr, "<>") || sqlparser_a_expr_name_is(a_expr, "!=");
}

int sqlparser_a_expr_is_not_like(const PgQuery__AExpr *a_expr)
{
	if (a_expr == NULL || a_expr->kind != PG_QUERY__A__EXPR__KIND__AEXPR_LIKE) {
		return 0;
	}
	return sqlparser_a_expr_name_is(a_expr, "!~~");
}

int sqlparser_a_expr_is_not_ilike(const PgQuery__AExpr *a_expr)
{
	if (a_expr == NULL || a_expr->kind != PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE) {
		return 0;
	}
	return sqlparser_a_expr_name_is(a_expr, "!~~*");
}

int sqlparser_a_expr_is_not_similar(const PgQuery__AExpr *a_expr)
{
	if (a_expr == NULL || a_expr->kind != PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR) {
		return 0;
	}
	return sqlparser_a_expr_name_is(a_expr, "!~");
}

const char *sqlparser_a_expr_operator_name(const PgQuery__AExpr *a_expr)
{
	const char *text;
	size_t index;

	if (a_expr == NULL) {
		return NULL;
	}

	switch (a_expr->kind) {
		case PG_QUERY__A__EXPR__KIND__AEXPR_IN:
			return sqlparser_a_expr_is_not_in(a_expr) ? "NOT IN" : "IN";
		case PG_QUERY__A__EXPR__KIND__AEXPR_LIKE:
			return sqlparser_a_expr_is_not_like(a_expr) ? "NOT LIKE" : "LIKE";
		case PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE:
			return sqlparser_a_expr_is_not_ilike(a_expr) ? "NOT ILIKE" : "ILIKE";
		case PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR:
			return sqlparser_a_expr_is_not_similar(a_expr) ? "NOT SIMILAR TO" : "SIMILAR TO";
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

sqlparser_status_t sqlparser_replace_proto_string(
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

sqlparser_status_t sqlparser_a_const_set_literal(
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

void sqlparser_free_proto_node(PgQuery__Node *node)
{
	if (node != NULL) {
		protobuf_c_message_free_unpacked((ProtobufCMessage *)node, NULL);
	}
}

sqlparser_status_t sqlparser_clone_proto_node(
	const PgQuery__Node *source,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	size_t packed_size;
	size_t packed_len;
	uint8_t *buffer;
	ProtobufCMessage *cloned_message;

	if (source == NULL || out_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"node clone requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_node = NULL;
	packed_size = protobuf_c_message_get_packed_size((const ProtobufCMessage *)source);
	if (packed_size == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to pack node");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	buffer = (uint8_t *)malloc(packed_size);
	if (buffer == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	packed_len = protobuf_c_message_pack((const ProtobufCMessage *)source, buffer);
	if (packed_len != packed_size) {
		free(buffer);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to serialize node");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	cloned_message = protobuf_c_message_unpack(
		((const ProtobufCMessage *)source)->descriptor,
		NULL,
		packed_size,
		buffer);
	free(buffer);
	if (cloned_message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to deserialize node");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_node = (PgQuery__Node *)cloned_message;
	return SQLPARSER_STATUS_OK;
}
