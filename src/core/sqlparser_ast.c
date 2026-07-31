#include <ctype.h>
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

int sqlparser_identifier_is_sysdate(const char *text)
{
	static const char expected[] = "sysdate";
	size_t index;

	if (text == NULL) {
		return 0;
	}
	for (index = 0U; expected[index] != '\0'; index++) {
		unsigned char byte;

		byte = (unsigned char)text[index];
		if (byte >= 'A' && byte <= 'Z') {
			byte = (unsigned char)(byte + ('a' - 'A'));
		}
		if (byte != (unsigned char)expected[index]) {
			return 0;
		}
	}
	return text[index] == '\0';
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
	size_t ast_statement_index;

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
	status = sqlparser_control_statement_ast_index(
		handle, statement_index, &ast_statement_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (ast_statement_index >= handle->ast->n_stmts) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"statement AST mapping is out of range");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	raw_stmt = handle->ast->stmts[ast_statement_index];
	if (raw_stmt == NULL || raw_stmt->stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"statement node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (sqlparser_control_unit_is_condition(handle, statement_index)) {
		if (raw_stmt->stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
		    raw_stmt->stmt->select_stmt == NULL ||
		    raw_stmt->stmt->select_stmt->where_clause == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"control condition node is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		*out_statement = raw_stmt->stmt->select_stmt->where_clause;
		return SQLPARSER_STATUS_OK;
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

void sqlparser_fill_relation_view_for_handle(
	const sqlparser_handle_t *handle,
	const PgQuery__RangeVar *relation,
	sqlparser_relation_view_t *out_relation)
{
	const char *object_name;
	const char *link_name;

	sqlparser_fill_relation_view(relation, out_relation);
	if (handle == NULL || relation == NULL || out_relation == NULL ||
	    handle->dialect_ops == NULL || relation->relname == NULL) {
		return;
	}

	object_name = sqlparser_dialect_relation_object_name(
		handle->dialect_ops,
		handle->dialect_state,
		relation->relname);
	if (object_name != NULL && object_name[0] != '\0') {
		out_relation->table_name = object_name;
	}
	link_name = sqlparser_dialect_relation_link_name(
		handle->dialect_ops,
		handle->dialect_state,
		relation->relname);
	if (link_name != NULL && link_name[0] != '\0') {
		out_relation->link_name = link_name;
	}
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
	       strcmp(short_name, "String") == 0;
}

int sqlparser_name_atom_is_identifier(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field)
{
	static const char *const identifier_fields[][2] = {
		{"AIndirection", "indirection"},
		{"AccessPriv", "cols"},
		{"Alias", "aliasname"},
		{"Alias", "colnames"},
		{"AlterCollationStmt", "collname"},
		{"AlterDatabaseRefreshCollStmt", "dbname"},
		{"AlterDatabaseSetStmt", "dbname"},
		{"AlterDatabaseStmt", "dbname"},
		{"AlterDomainStmt", "name"},
		{"AlterDomainStmt", "type_name"},
		{"AlterEnumStmt", "type_name"},
		{"AlterEventTrigStmt", "trigname"},
		{"AlterExtensionContentsStmt", "extname"},
		{"AlterExtensionContentsStmt", "object"},
		{"AlterExtensionStmt", "extname"},
		{"AlterFdwStmt", "fdwname"},
		{"AlterForeignServerStmt", "servername"},
		{"AlterObjectDependsStmt", "extname"},
		{"AlterObjectDependsStmt", "object"},
		{"AlterObjectSchemaStmt", "newschema"},
		{"AlterObjectSchemaStmt", "object"},
		{"AlterOpFamilyStmt", "amname"},
		{"AlterOwnerStmt", "object"},
		{"AlterPolicyStmt", "policy_name"},
		{"AlterPublicationStmt", "pubname"},
		{"AlterRoleSetStmt", "database"},
		{"AlterStatsStmt", "defnames"},
		{"AlterSubscriptionStmt", "subname"},
		{"AlterTSConfigurationStmt", "cfgname"},
		{"AlterTSConfigurationStmt", "dicts"},
		{"AlterTSConfigurationStmt", "tokentype"},
		{"AlterTSDictionaryStmt", "dictname"},
		{"AlterTableCmd", "name"},
		{"AlterTableMoveAllStmt", "new_tablespacename"},
		{"AlterTableMoveAllStmt", "orig_tablespacename"},
		{"AlterTableSpaceOptionsStmt", "tablespacename"},
		{"AlterTypeStmt", "type_name"},
		{"AlterUserMappingStmt", "servername"},
		{"CTECycleClause", "cycle_col_list"},
		{"CTECycleClause", "cycle_mark_column"},
		{"CTECycleClause", "cycle_path_column"},
		{"CTESearchClause", "search_col_list"},
		{"CTESearchClause", "search_seq_column"},
		{"ClosePortalStmt", "portalname"},
		{"ClusterStmt", "indexname"},
		{"CollateClause", "collname"},
		{"ColumnDef", "colname"},
		{"ColumnDef", "compression"},
		{"ColumnDef", "storage_name"},
		{"ColumnRef", "fields"},
		{"CommentStmt", "object"},
		{"CommonTableExpr", "aliascolnames"},
		{"CommonTableExpr", "ctecolnames"},
		{"CommonTableExpr", "ctename"},
		{"Constraint", "access_method"},
		{"Constraint", "conname"},
		{"Constraint", "fk_attrs"},
		{"Constraint", "fk_del_set_cols"},
		{"Constraint", "including"},
		{"Constraint", "indexname"},
		{"Constraint", "indexspace"},
		{"Constraint", "keys"},
		{"Constraint", "pk_attrs"},
		{"ConstraintsSetStmt", "constraints"},
		{"CreateAmStmt", "amname"},
		{"CreateAmStmt", "handler_name"},
		{"CreateConversionStmt", "conversion_name"},
		{"CreateConversionStmt", "func_name"},
		{"CreateDomainStmt", "domainname"},
		{"CreateEnumStmt", "type_name"},
		{"CreateEventTrigStmt", "eventname"},
		{"CreateEventTrigStmt", "funcname"},
		{"CreateEventTrigStmt", "trigname"},
		{"CreateExtensionStmt", "extname"},
		{"CreateFdwStmt", "fdwname"},
		{"CreateForeignServerStmt", "fdwname"},
		{"CreateForeignServerStmt", "servername"},
		{"CreateForeignTableStmt", "servername"},
		{"CreateFunctionStmt", "funcname"},
		{"CreateOpClassItem", "order_family"},
		{"CreateOpClassStmt", "amname"},
		{"CreateOpClassStmt", "opclassname"},
		{"CreateOpClassStmt", "opfamilyname"},
		{"CreateOpFamilyStmt", "amname"},
		{"CreateOpFamilyStmt", "opfamilyname"},
		{"CreatePLangStmt", "plhandler"},
		{"CreatePLangStmt", "plinline"},
		{"CreatePLangStmt", "plname"},
		{"CreatePLangStmt", "plvalidator"},
		{"CreatePolicyStmt", "policy_name"},
		{"CreatePublicationStmt", "pubname"},
		{"CreateRangeStmt", "type_name"},
		{"CreateRoleStmt", "role"},
		{"CreateSchemaStmt", "schemaname"},
		{"CreateStatsStmt", "defnames"},
		{"CreateStmt", "access_method"},
		{"CreateStmt", "tablespacename"},
		{"CreateSubscriptionStmt", "publication"},
		{"CreateSubscriptionStmt", "subname"},
		{"CreateTableSpaceStmt", "tablespacename"},
		{"CreateTransformStmt", "lang"},
		{"CreateTrigStmt", "columns"},
		{"CreateTrigStmt", "funcname"},
		{"CreateTrigStmt", "trigname"},
		{"CreateUserMappingStmt", "servername"},
		{"CreatedbStmt", "dbname"},
		{"CurrentOfExpr", "cursor_name"},
		{"DeallocateStmt", "name"},
		{"DeclareCursorStmt", "portalname"},
		{"DropOwnedStmt", "roles"},
		{"DropRoleStmt", "roles"},
		{"DropStmt", "objects"},
		{"DropSubscriptionStmt", "subname"},
		{"DropTableSpaceStmt", "tablespacename"},
		{"DropUserMappingStmt", "servername"},
		{"DropdbStmt", "dbname"},
		{"ExecuteStmt", "name"},
		{"FetchStmt", "portalname"},
		{"FuncCall", "funcname"},
		{"FunctionParameter", "name"},
		{"GrantStmt", "objects"},
		{"ImportForeignSchemaStmt", "local_schema"},
		{"ImportForeignSchemaStmt", "remote_schema"},
		{"ImportForeignSchemaStmt", "server_name"},
		{"ImportForeignSchemaStmt", "table_list"},
		{"IndexElem", "collation"},
		{"IndexElem", "indexcolname"},
		{"IndexElem", "name"},
		{"IndexElem", "opclass"},
		{"IndexStmt", "access_method"},
		{"IndexStmt", "idxname"},
		{"IndexStmt", "table_space"},
		{"InferClause", "conname"},
		{"IntoClause", "access_method"},
		{"IntoClause", "col_names"},
		{"IntoClause", "table_space_name"},
		{"JoinExpr", "using_clause"},
		{"JsonArgument", "name"},
		{"JsonExpr", "column_name"},
		{"JsonFuncExpr", "column_name"},
		{"JsonTableColumn", "name"},
		{"JsonTablePath", "name"},
		{"JsonTablePathSpec", "name"},
		{"ListenStmt", "conditionname"},
		{"NamedArgExpr", "name"},
		{"NotifyStmt", "conditionname"},
		{"PLAssignStmt", "indirection"},
		{"PLAssignStmt", "name"},
		{"PartitionElem", "collation"},
		{"PartitionElem", "name"},
		{"PartitionElem", "opclass"},
		{"PrepareStmt", "name"},
		{"PublicationObjSpec", "name"},
		{"PublicationTable", "columns"},
		{"RangeTableFuncCol", "colname"},
		{"RangeTableSample", "method"},
		{"RangeTblEntry", "ctename"},
		{"RangeTblEntry", "enrname"},
		{"RangeVar", "catalogname"},
		{"RangeVar", "relname"},
		{"RangeVar", "schemaname"},
		{"ReassignOwnedStmt", "roles"},
		{"ReindexStmt", "name"},
		{"RenameStmt", "newname"},
		{"RenameStmt", "object"},
		{"RenameStmt", "subname"},
		{"ReplicaIdentityStmt", "name"},
		{"ResTarget", "indirection"},
		{"ResTarget", "name"},
		{"RoleSpec", "rolename"},
		{"RowExpr", "colnames"},
		{"RuleStmt", "rulename"},
		{"SecLabelStmt", "object"},
		{"SecLabelStmt", "provider"},
		{"StatsElem", "name"},
		{"TargetEntry", "resname"},
		{"TransactionStmt", "savepoint_name"},
		{"TriggerTransition", "name"},
		{"TypeName", "names"},
		{"UnlistenStmt", "conditionname"},
		{"VacuumRelation", "va_cols"},
		{"VariableShowStmt", "name"},
		{"ViewStmt", "aliases"},
		{"WindowClause", "name"},
		{"WindowClause", "refname"},
		{"WindowDef", "name"},
		{"WindowDef", "refname"},
		{"WithCheckOption", "polname"},
		{"WithCheckOption", "relname"},
		{"XmlExpr", "name"}
	};
	const char *field_name;
	const char *owner_type;
	size_t high;
	size_t low;

	owner_type = NULL;
	field_name = NULL;
	if (sqlparser_message_is_name_container(descriptor) &&
	    context != NULL) {
		owner_type = context->owner_type;
		field_name = context->field_name;
	}
	if (owner_type == NULL && descriptor != NULL) {
		owner_type = descriptor->short_name;
	}
	if (field_name == NULL && field != NULL) {
		field_name = field->name;
	}
	if (message == NULL || owner_type == NULL || field_name == NULL ||
	    (context != NULL && context->identifier_forbidden)) {
		return 0;
	}
	if (strcmp(owner_type, "VariableSetStmt") == 0 &&
	    strcmp(field_name, "name") == 0) {
		if (message->descriptor !=
		    &pg_query__variable_set_stmt__descriptor) {
			return 0;
		}
		return ((PgQuery__VariableSetStmt *)message)->kind !=
			PG_QUERY__VARIABLE_SET_KIND__VAR_SET_MULTI;
	}
	if (strcmp(owner_type, "FuncCall") == 0 &&
	    strcmp(field_name, "funcname") == 0) {
		return context != NULL &&
			context->field_owner != NULL &&
			context->field_owner->descriptor ==
				&pg_query__func_call__descriptor &&
			((PgQuery__FuncCall *)context->field_owner)->funcformat ==
				PG_QUERY__COERCION_FORM__COERCE_EXPLICIT_CALL;
	}
	if (strcmp(owner_type, "Constraint") == 0 &&
	    strcmp(field_name, "keys") == 0 &&
	    context != NULL &&
	    context->field_owner != NULL &&
	    context->field_owner->descriptor ==
		    &pg_query__constraint__descriptor &&
	    ((PgQuery__Constraint *)context->field_owner)->contype ==
		    PG_QUERY__CONSTR_TYPE__CONSTR_NOTNULL) {
		return 0;
	}
	if (strcmp(owner_type, "DefElem") == 0) {
		PgQuery__AlterTableCmd *alter_table_cmd;
		PgQuery__Constraint *constraint;

		if ((strcmp(field_name, "defname") != 0 &&
		     strcmp(field_name, "defnamespace") != 0) ||
		    context == NULL || context->owner_type == NULL ||
		    context->field_name == NULL) {
			return 0;
		}
		if (strcmp(context->owner_type, "Constraint") == 0 &&
		    strcmp(context->field_name, "options") == 0) {
			if (context->field_owner == NULL ||
			    context->field_owner->descriptor !=
				    &pg_query__constraint__descriptor) {
				return 0;
			}
			constraint = (PgQuery__Constraint *)context->field_owner;
			return constraint->contype ==
					PG_QUERY__CONSTR_TYPE__CONSTR_PRIMARY ||
				constraint->contype ==
					PG_QUERY__CONSTR_TYPE__CONSTR_UNIQUE ||
				constraint->contype ==
					PG_QUERY__CONSTR_TYPE__CONSTR_EXCLUSION;
		}
		if (strcmp(context->owner_type, "AlterTableCmd") == 0 &&
		    strcmp(context->field_name, "def") == 0) {
			if (context->field_owner == NULL ||
			    context->field_owner->descriptor !=
				    &pg_query__alter_table_cmd__descriptor) {
				return 0;
			}
			alter_table_cmd =
				(PgQuery__AlterTableCmd *)context->field_owner;
			if (alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_SetOptions ||
			    alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_ResetOptions ||
			    alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_SetRelOptions ||
			    alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_ResetRelOptions) {
				return 1;
			}
			return strcmp(field_name, "defname") == 0 &&
				(alter_table_cmd->subtype ==
					PG_QUERY__ALTER_TABLE_TYPE__AT_AlterColumnGenericOptions ||
				 alter_table_cmd->subtype ==
					 PG_QUERY__ALTER_TABLE_TYPE__AT_GenericOptions);
		}
		if ((strcmp(context->owner_type, "AlterFdwStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "AlterForeignServerStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "AlterUserMappingStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "ColumnDef") == 0 &&
		     strcmp(context->field_name, "fdwoptions") == 0) ||
		    (strcmp(context->owner_type, "CreateFdwStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "CreateForeignServerStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "CreateForeignTableStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "CreateUserMappingStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0) ||
		    (strcmp(context->owner_type, "ImportForeignSchemaStmt") == 0 &&
		     strcmp(context->field_name, "options") == 0)) {
			return strcmp(field_name, "defname") == 0;
		}
		return
			(strcmp(context->owner_type, "AlterTableSpaceOptionsStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "CreateStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "CreateTableSpaceStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "IndexElem") == 0 &&
			 strcmp(context->field_name, "opclassopts") == 0) ||
			(strcmp(context->owner_type, "IndexStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "IntoClause") == 0 &&
			 strcmp(context->field_name, "options") == 0) ||
			(strcmp(context->owner_type, "ViewStmt") == 0 &&
			 strcmp(context->field_name, "options") == 0);
	}

	low = 0U;
	high = sizeof(identifier_fields) / sizeof(identifier_fields[0]);
	while (low < high) {
		const size_t middle = low + (high - low) / 2U;
		int comparison;

		comparison =
			strcmp(owner_type, identifier_fields[middle][0]);
		if (comparison == 0) {
			comparison =
				strcmp(field_name, identifier_fields[middle][1]);
		}
		if (comparison < 0) {
			high = middle;
		} else if (comparison > 0) {
			low = middle + 1U;
		} else {
			return 1;
		}
	}
	return 0;
}

int32_t *sqlparser_proto_location_slot(ProtobufCMessage *message)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (message == NULL || message->descriptor == NULL) {
		return NULL;
	}

	descriptor = message->descriptor;
	base = (uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if (field->type == PROTOBUF_C_TYPE_INT32 &&
		    field->label != PROTOBUF_C_LABEL_REPEATED &&
		    strcmp(field->name, "location") == 0) {
			return (int32_t *)(base + field->offset);
		}
	}

	return NULL;
}

static int sqlparser_func_call_is_implicit_pattern_wrapper(
	const PgQuery__FuncCall *func_call,
	const PgQuery__AExpr *a_expr)
{
	const char *function_name;
	const char *schema_name;

	if (func_call == NULL || a_expr == NULL ||
	    func_call->n_funcname != 2U || func_call->funcname == NULL ||
	    !sqlparser_node_string_value(
		    func_call->funcname[0],
		    &schema_name) ||
	    !sqlparser_node_string_value(
		    func_call->funcname[1],
		    &function_name) ||
	    strcmp(schema_name, "pg_catalog") != 0) {
		return 0;
	}
	if (a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR) {
		return strcmp(function_name, "similar_to_escape") == 0 &&
			(func_call->n_args == 1U || func_call->n_args == 2U);
	}
	if ((a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_LIKE ||
	     a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE) &&
	    strcmp(function_name, "like_escape") == 0) {
		return func_call->n_args == 2U &&
			func_call->location == a_expr->location;
	}
	return 0;
}

sqlparser_name_context_t sqlparser_next_name_context(
	ProtobufCMessage *message,
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
	next_context.field_owner = message;
	if (descriptor == &pg_query__func_call__descriptor &&
	    field != NULL && strcmp(field->name, "funcname") == 0 &&
	    context != NULL &&
	    context->owner_type != NULL &&
	    strcmp(context->owner_type, "AExpr") == 0 &&
	    context->field_name != NULL &&
	    strcmp(context->field_name, "rexpr") == 0 &&
	    context->field_owner != NULL &&
	    context->field_owner->descriptor ==
		    &pg_query__a__expr__descriptor &&
	    sqlparser_func_call_is_implicit_pattern_wrapper(
		    (PgQuery__FuncCall *)message,
		    (PgQuery__AExpr *)context->field_owner)) {
		next_context.identifier_forbidden = 1;
	}
	return next_context;
}

static sqlparser_status_t sqlparser_record_name_atom(
	ProtobufCMessage *message,
	ProtobufCMessage *location_owner,
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
	if (!sqlparser_name_atom_is_identifier(
		    message,
		    context,
		    descriptor,
		    field)) {
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
			search->target_owner = location_owner;
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
	search->target_owner = location_owner;
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
 * 通用名称层只枚举可安全改写的 identifier 字符串原子。
 * protobuf 中的 literal、operator 和结构判别字符串默认不公开。
 */
sqlparser_status_t sqlparser_walk_message_names(
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_name_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	ProtobufCMessage *location_owner;
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
	location_owner =
		sqlparser_proto_location_slot(message) != NULL ?
			message :
			(context != NULL ? context->location_owner : NULL);
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
						message,
						location_owner,
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
					message,
					location_owner,
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

			next_context =
				sqlparser_next_name_context(
					message,
					descriptor,
					field,
					context);
			next_context.location_owner = location_owner;
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

static void sqlparser_record_string_slot(
	char **slot,
	sqlparser_string_search_t *search)
{
	if (slot == NULL || search == NULL || search->target_slot != NULL) {
		return;
	}
	if (search->match_slot != NULL) {
		if (slot == search->match_slot &&
		    (search->match_value == NULL ||
		     *slot == search->match_value)) {
			search->target_slot = slot;
			search->target_index = search->seen;
		}
		search->seen++;
		return;
	}
	if (search->match_value != NULL) {
		if (*slot == search->match_value) {
			search->target_slot = slot;
			search->target_index = search->seen;
		}
		search->seen++;
		return;
	}
	if (search->want_target && search->seen == search->target_index) {
		search->target_slot = slot;
	}
	search->seen++;
}

sqlparser_status_t sqlparser_walk_message_strings(
	ProtobufCMessage *message,
	sqlparser_string_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (message == NULL || search == NULL || search->target_slot != NULL) {
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
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) !=
			    (int)field->id) {
			continue;
		}
		if (field->type == PROTOBUF_C_TYPE_STRING) {
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t count;
				char **items;
				size_t item_index;

				count = *(const size_t *)(base + field->quantifier_offset);
				items = *(char ***)(base + field->offset);
				for (item_index = 0U;
				     items != NULL && item_index < count;
				     item_index++) {
					sqlparser_record_string_slot(
						&items[item_index],
						search);
					if (search->target_slot != NULL) {
						return SQLPARSER_STATUS_OK;
					}
				}
			} else {
				sqlparser_record_string_slot(
					(char **)(base + field->offset),
					search);
				if (search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
			continue;
		}
		if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t count;
				ProtobufCMessage **items;
				size_t item_index;

				count = *(const size_t *)(base + field->quantifier_offset);
				items = *(ProtobufCMessage ***)(base + field->offset);
				for (item_index = 0U;
				     items != NULL && item_index < count;
				     item_index++) {
					sqlparser_status_t status;

					status = sqlparser_walk_message_strings(
						items[item_index],
						search);
					if (status != SQLPARSER_STATUS_OK ||
					    search->target_slot != NULL) {
						return status;
					}
				}
			} else {
				ProtobufCMessage *child;
				sqlparser_status_t status;

				child =
					*(ProtobufCMessage **)(base + field->offset);
				if (child == NULL) {
					continue;
				}
				status = sqlparser_walk_message_strings(
					child,
					search);
				if (status != SQLPARSER_STATUS_OK ||
				    search->target_slot != NULL) {
					return status;
				}
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_get_raw_statement_string_slot_by_index(
	PgQuery__ParseResult *ast,
	size_t raw_statement_index,
	size_t string_index,
	char ***out_slot)
{
	sqlparser_string_search_t search;
	sqlparser_status_t status;

	if (out_slot == NULL) {
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_slot = NULL;
	if (ast == NULL || raw_statement_index >= ast->n_stmts ||
	    ast->stmts == NULL || ast->stmts[raw_statement_index] == NULL ||
	    ast->stmts[raw_statement_index]->stmt == NULL) {
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = string_index;
	status = sqlparser_walk_message_strings(
		(ProtobufCMessage *)ast->stmts[raw_statement_index]->stmt,
		&search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*out_slot = search.target_slot;
	return search.target_slot != NULL ?
		SQLPARSER_STATUS_OK :
		SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_find_raw_statement_string_index_by_slot(
	sqlparser_handle_t *handle,
	size_t raw_statement_index,
	char **slot,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_string_search_t search;
	sqlparser_status_t status;

	if (handle == NULL || slot == NULL || out_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (raw_statement_index >= handle->ast->n_stmts ||
	    handle->ast->stmts == NULL ||
	    handle->ast->stmts[raw_statement_index] == NULL ||
	    handle->ast->stmts[raw_statement_index]->stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&search, 0, sizeof(search));
	search.match_slot = slot;
	status = sqlparser_walk_message_strings(
		(ProtobufCMessage *)
			handle->ast->stmts[raw_statement_index]->stmt,
		&search);
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
			payload->sval->location = -1;
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

int sqlparser_proto_location_is_generated(int32_t location)
{
	return location == SQLPARSER_PROTO_LOCATION_GENERATED ||
		location < SQLPARSER_PROTO_LOCATION_GENERATED_STYLE_BASE;
}

sqlparser_proto_identifier_style_t sqlparser_proto_identifier_style(
	int32_t location,
	size_t component_index)
{
	uint32_t payload;

	if (location >= SQLPARSER_PROTO_LOCATION_GENERATED_STYLE_BASE ||
	    sqlparser_proto_location_is_identifier_spelling(location) ||
	    component_index >= SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS) {
		return SQLPARSER_PROTO_IDENTIFIER_STYLE_NONE;
	}
	payload = (uint32_t)(
		(int64_t)SQLPARSER_PROTO_LOCATION_GENERATED_STYLE_BASE -
		(int64_t)location);
	return (sqlparser_proto_identifier_style_t)(
		(payload >>
		 (component_index * SQLPARSER_PROTO_IDENTIFIER_STYLE_BITS)) &
		SQLPARSER_PROTO_IDENTIFIER_STYLE_MASK);
}

static size_t sqlparser_generated_skip_trivia(
	const char *sql,
	size_t length,
	size_t position)
{
	for (;;) {
		while (position < length &&
		       isspace((unsigned char)sql[position])) {
			position++;
		}
		if (position + 1U < length &&
		    sql[position] == '-' &&
		    sql[position + 1U] == '-') {
			position += 2U;
			while (position < length &&
			       sql[position] != '\n' &&
			       sql[position] != '\r') {
				position++;
			}
			continue;
		}
		if (position + 1U < length &&
		    sql[position] == '/' &&
		    sql[position + 1U] == '*') {
			size_t depth;

			position += 2U;
			depth = 1U;
			while (position < length && depth > 0U) {
				if (position + 1U < length &&
				    sql[position] == '/' &&
				    sql[position + 1U] == '*') {
					depth++;
					position += 2U;
				} else if (position + 1U < length &&
					   sql[position] == '*' &&
					   sql[position + 1U] == '/') {
					depth--;
					position += 2U;
				} else {
					position++;
				}
			}
			continue;
		}
		return position;
	}
}

static size_t sqlparser_generated_parser_sql_length(
	const char *parser_sql,
	const sqlparser_generated_source_t *source)
{
	return source != NULL ?
		source->parser_sql_length :
		(parser_sql != NULL ? strlen(parser_sql) : 0U);
}

static unsigned char sqlparser_generated_ascii_lower(unsigned char byte)
{
	return byte >= 'A' && byte <= 'Z' ?
		(unsigned char)(byte + ('a' - 'A')) :
		byte;
}

static int sqlparser_generated_token_equal_ci(
	const char *sql,
	size_t start,
	size_t end,
	const char *expected)
{
	size_t index;

	if (sql == NULL || expected == NULL ||
	    strlen(expected) != end - start) {
		return 0;
	}
	for (index = 0U; start + index < end; index++) {
		if (sqlparser_generated_ascii_lower(
			    (unsigned char)sql[start + index]) !=
		    sqlparser_generated_ascii_lower(
			    (unsigned char)expected[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_generated_hex_value(unsigned char byte)
{
	if (byte >= '0' && byte <= '9') {
		return (int)(byte - '0');
	}
	if (byte >= 'a' && byte <= 'f') {
		return (int)(byte - 'a') + 10;
	}
	if (byte >= 'A' && byte <= 'F') {
		return (int)(byte - 'A') + 10;
	}
	return -1;
}

static int sqlparser_generated_identifier_match_byte(
	const char *value,
	size_t *value_index,
	unsigned char byte)
{
	if (value[*value_index] == '\0' ||
	    (unsigned char)value[*value_index] != byte) {
		return 0;
	}
	(*value_index)++;
	return 1;
}

static int sqlparser_generated_identifier_match_codepoint(
	const char *value,
	size_t *value_index,
	unsigned long codepoint)
{
	unsigned char encoded[4];
	size_t count;
	size_t index;

	if (codepoint <= 0x7FUL) {
		encoded[0] = (unsigned char)codepoint;
		count = 1U;
	} else if (codepoint <= 0x7FFUL) {
		encoded[0] = (unsigned char)(0xC0U | (codepoint >> 6));
		encoded[1] = (unsigned char)(0x80U | (codepoint & 0x3FU));
		count = 2U;
	} else if (codepoint <= 0xFFFFUL) {
		encoded[0] = (unsigned char)(0xE0U | (codepoint >> 12));
		encoded[1] =
			(unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
		encoded[2] = (unsigned char)(0x80U | (codepoint & 0x3FU));
		count = 3U;
	} else if (codepoint <= 0x10FFFFUL) {
		encoded[0] = (unsigned char)(0xF0U | (codepoint >> 18));
		encoded[1] =
			(unsigned char)(0x80U | ((codepoint >> 12) & 0x3FU));
		encoded[2] =
			(unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
		encoded[3] = (unsigned char)(0x80U | (codepoint & 0x3FU));
		count = 4U;
	} else {
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (!sqlparser_generated_identifier_match_byte(
			    value,
			    value_index,
			    encoded[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_generated_unquoted_word_end(
	const char *sql,
	size_t length,
	size_t position)
{
	while (position < length) {
		unsigned char byte;

		byte = (unsigned char)sql[position];
		if (!(isalnum(byte) || byte == '_' || byte == '$' ||
		      byte >= 0x80U)) {
			break;
		}
		position++;
	}
	return position;
}

static size_t sqlparser_generated_uamp_span_end(
	const char *sql,
	size_t length,
	size_t quote_end,
	unsigned char *out_escape)
{
	size_t keyword_end;
	size_t position;

	*out_escape = '\\';
	position = sqlparser_generated_skip_trivia(sql, length, quote_end);
	keyword_end =
		sqlparser_generated_unquoted_word_end(sql, length, position);
	if (!sqlparser_generated_token_equal_ci(
		    sql,
		    position,
		    keyword_end,
		    "uescape")) {
		return quote_end;
	}
	position = sqlparser_generated_skip_trivia(
		sql,
		length,
		keyword_end);
	if (position + 2U >= length ||
	    sql[position] != '\'' ||
	    sql[position + 2U] != '\'') {
		return quote_end;
	}
	*out_escape = (unsigned char)sql[position + 1U];
	return position + 3U;
}

static int sqlparser_generated_identifier_token(
	const char *sql,
	size_t length,
	size_t position,
	size_t *out_end,
	sqlparser_proto_identifier_style_t *out_style)
{
	size_t cursor;

	if (sql == NULL || position >= length ||
	    out_end == NULL || out_style == NULL) {
		return 0;
	}
	cursor = position;
	if ((sql[cursor] == 'U' || sql[cursor] == 'u') &&
	    cursor + 2U < length &&
	    sql[cursor + 1U] == '&' &&
	    sql[cursor + 2U] == '"') {
		unsigned char escape;

		cursor += 3U;
		*out_style = SQLPARSER_PROTO_IDENTIFIER_STYLE_DOUBLE_QUOTED;
		while (cursor < length) {
			if (sql[cursor] == '"') {
				if (cursor + 1U < length &&
				    sql[cursor + 1U] == '"') {
					cursor += 2U;
					continue;
				}
				*out_end = sqlparser_generated_uamp_span_end(
					sql,
					length,
					cursor + 1U,
					&escape);
				return 1;
			}
			cursor++;
		}
		return 0;
	} else if (sql[cursor] == '"') {
		cursor++;
		*out_style = SQLPARSER_PROTO_IDENTIFIER_STYLE_DOUBLE_QUOTED;
	} else {
		unsigned char byte;

		byte = (unsigned char)sql[cursor];
		if (!(isalpha(byte) || byte == '_' || byte >= 0x80U)) {
			return 0;
		}
		cursor++;
		while (cursor < length) {
			byte = (unsigned char)sql[cursor];
			if (!(isalnum(byte) || byte == '_' || byte == '$' ||
			      byte >= 0x80U)) {
				break;
			}
			cursor++;
		}
		*out_end = cursor;
		*out_style = SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED;
		return 1;
	}
	while (cursor < length) {
		if (sql[cursor] == '"') {
			if (cursor + 1U < length && sql[cursor + 1U] == '"') {
				cursor += 2U;
				continue;
			}
			*out_end = cursor + 1U;
			return 1;
		}
		cursor++;
	}
	return 0;
}

static int sqlparser_generated_identifier_matches(
	const char *sql,
	size_t start,
	size_t end,
	const char *value)
{
	size_t cursor;
	size_t quote_end;
	size_t value_index;

	if (sql == NULL || value == NULL || end <= start) {
		return 0;
	}
	cursor = start;
	if ((sql[cursor] == 'U' || sql[cursor] == 'u') &&
	    cursor + 2U < end &&
	    sql[cursor + 1U] == '&' &&
	    sql[cursor + 2U] == '"') {
		unsigned char escape;

		cursor += 3U;
		quote_end = cursor;
		while (quote_end < end) {
			if (sql[quote_end] == '"') {
				if (quote_end + 1U < end &&
				    sql[quote_end + 1U] == '"') {
					quote_end += 2U;
					continue;
				}
				break;
			}
			quote_end++;
		}
		if (quote_end >= end) {
			return 0;
		}
		(void)sqlparser_generated_uamp_span_end(
			sql,
			end,
			quote_end + 1U,
			&escape);
		value_index = 0U;
		while (cursor < quote_end) {
			unsigned char byte;

			byte = (unsigned char)sql[cursor++];
			if (byte == '"') {
				if (cursor >= quote_end || sql[cursor] != '"') {
					return 0;
				}
				cursor++;
				if (!sqlparser_generated_identifier_match_byte(
					    value,
					    &value_index,
					    '"')) {
					return 0;
				}
				continue;
			}
			if (byte != escape) {
				if (!sqlparser_generated_identifier_match_byte(
					    value,
					    &value_index,
					    byte)) {
					return 0;
				}
				continue;
			}
			if (cursor >= quote_end) {
				return 0;
			}
			if ((unsigned char)sql[cursor] == escape) {
				cursor++;
				if (!sqlparser_generated_identifier_match_byte(
					    value,
					    &value_index,
					    escape)) {
					return 0;
				}
				continue;
			}
			{
				unsigned long codepoint;
				size_t digit_count;
				size_t digit_index;

				digit_count = 4U;
				if (sql[cursor] == '+') {
					cursor++;
					digit_count = 6U;
				}
				if (digit_count > quote_end - cursor) {
					return 0;
				}
				codepoint = 0UL;
				for (digit_index = 0U;
				     digit_index < digit_count;
				     digit_index++) {
					int digit;

					digit = sqlparser_generated_hex_value(
						(unsigned char)
							sql[cursor + digit_index]);
					if (digit < 0) {
						return 0;
					}
					codepoint =
						(codepoint << 4) |
						(unsigned long)digit;
				}
				cursor += digit_count;
				if (codepoint >= 0xD800UL &&
				    codepoint <= 0xDBFFUL) {
					unsigned long low;

					if (cursor + 5U > quote_end ||
					    (unsigned char)sql[cursor++] != escape) {
						return 0;
					}
					low = 0UL;
					for (digit_index = 0U;
					     digit_index < 4U;
					     digit_index++) {
						int digit;

						digit =
							sqlparser_generated_hex_value(
								(unsigned char)
									sql[cursor +
									    digit_index]);
						if (digit < 0) {
							return 0;
						}
						low =
							(low << 4) |
							(unsigned long)digit;
					}
					cursor += 4U;
					if (low < 0xDC00UL || low > 0xDFFFUL) {
						return 0;
					}
					codepoint =
						0x10000UL +
						((codepoint - 0xD800UL) << 10) +
						(low - 0xDC00UL);
				}
				if (!sqlparser_generated_identifier_match_codepoint(
					    value,
					    &value_index,
					    codepoint)) {
					return 0;
				}
			}
		}
		return value[value_index] == '\0';
	}
	if (sql[cursor] != '"') {
		return strlen(value) == end - start &&
			memcmp(sql + start, value, end - start) == 0;
	}
	cursor++;
	value_index = 0U;
	while (cursor + 1U <= end) {
		if (sql[cursor] == '"') {
			if (cursor + 1U < end && sql[cursor + 1U] == '"') {
				if (value[value_index] != '"') {
					return 0;
				}
				value_index++;
				cursor += 2U;
				continue;
			}
			return cursor + 1U == end &&
				value[value_index] == '\0';
		}
		if (value[value_index] == '\0' ||
		    value[value_index] != sql[cursor]) {
			return 0;
		}
		value_index++;
		cursor++;
	}
	return 0;
}

static int sqlparser_generated_read_keyword(
	const char *sql,
	size_t length,
	size_t *position,
	const char *keyword)
{
	sqlparser_proto_identifier_style_t style;
	size_t start;
	size_t end;

	if (sql == NULL || position == NULL || keyword == NULL) {
		return 0;
	}
	start = sqlparser_generated_skip_trivia(sql, length, *position);
	if (!sqlparser_generated_identifier_token(
		    sql,
		    length,
		    start,
		    &end,
		    &style) ||
	    style != SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED ||
	    !sqlparser_generated_token_equal_ci(
		    sql,
		    start,
		    end,
		    keyword)) {
		return 0;
	}
	*position = end;
	return 1;
}

static int sqlparser_generated_find_keyword(
	const char *sql,
	size_t length,
	size_t *position,
	const char *keyword)
{
	size_t cursor;

	if (sql == NULL || position == NULL || keyword == NULL) {
		return 0;
	}
	cursor = *position;
	while (cursor < length) {
		sqlparser_proto_identifier_style_t style;
		size_t end;

		cursor = sqlparser_generated_skip_trivia(
			sql,
			length,
			cursor);
		if (cursor >= length) {
			break;
		}
		if (sql[cursor] == '\'') {
			cursor++;
			while (cursor < length) {
				if (sql[cursor] != '\'') {
					cursor++;
					continue;
				}
				if (cursor + 1U < length &&
				    sql[cursor + 1U] == '\'') {
					cursor += 2U;
					continue;
				}
				cursor++;
				break;
			}
			continue;
		}
		if (sqlparser_generated_identifier_token(
			    sql,
			    length,
			    cursor,
			    &end,
			    &style)) {
			if (style ==
				    SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED &&
			    sqlparser_generated_token_equal_ci(
				    sql,
				    cursor,
				    end,
				    keyword)) {
				*position = end;
				return 1;
			}
			cursor = end;
			continue;
		}
		cursor++;
	}
	return 0;
}

static int sqlparser_generated_read_identifier(
	const char *sql,
	size_t length,
	size_t *position,
	const char *value,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	sqlparser_proto_identifier_style_t *out_style)
{
	size_t public_sql_length;
	size_t start;
	size_t end;

	if (position == NULL) {
		return 0;
	}
	start = sqlparser_generated_skip_trivia(
		sql,
		length,
		*position);
	if (!sqlparser_generated_identifier_token(
		    sql,
		    length,
		    start,
		    &end,
		    out_style) ||
	    !sqlparser_generated_identifier_matches(
		    sql,
		    start,
		    end,
		    value)) {
		return 0;
	}
	if (source != NULL &&
	    source->origins != NULL &&
	    source->public_sql != NULL &&
	    start >= parser_fragment_offset) {
		sqlparser_identifier_origin_t origin;
		sqlparser_identifier_origin_kind_t kind;

		public_sql_length = source->public_sql_length;
		kind = sqlparser_identifier_origin_map_lookup(
			source->origins,
			start - parser_fragment_offset,
			end - start,
			&origin);
		if (kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE &&
		    origin.source_offset <= public_sql_length &&
		    origin.source_length <=
			    public_sql_length -
				    origin.source_offset) {
			const char *public_token;

			public_token =
				source->public_sql + origin.source_offset;
			if (origin.source_length > 0U &&
			    public_token[0] == '`') {
				*out_style =
					SQLPARSER_PROTO_IDENTIFIER_STYLE_BACKTICK_QUOTED;
			} else if (origin.source_length > 0U &&
				   public_token[0] == '[') {
				*out_style =
					SQLPARSER_PROTO_IDENTIFIER_STYLE_BRACKET_QUOTED;
			} else if (origin.source_length > 0U &&
				   public_token[0] == '"') {
				*out_style =
					SQLPARSER_PROTO_IDENTIFIER_STYLE_DOUBLE_QUOTED;
			} else if (origin.source_length > 2U &&
				   (public_token[0] == 'U' ||
				    public_token[0] == 'u') &&
				   public_token[1] == '&' &&
				   public_token[2] == '"') {
				*out_style =
					SQLPARSER_PROTO_IDENTIFIER_STYLE_DOUBLE_QUOTED;
			} else {
				*out_style =
					SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED;
			}
			if (source->spelling_handle != NULL) {
				(void)sqlparser_handle_append_identifier_spelling(
					source->spelling_handle,
					public_token,
					origin.source_length);
			}
		}
	} else if (source != NULL &&
		   (source->dialect == SQLPARSER_DIALECT_MYSQL ||
		    source->dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL) &&
		   *out_style ==
			   SQLPARSER_PROTO_IDENTIFIER_STYLE_DOUBLE_QUOTED) {
		*out_style =
			SQLPARSER_PROTO_IDENTIFIER_STYLE_BACKTICK_QUOTED;
	}
	*position = end;
	return 1;
}

static int32_t sqlparser_generated_style_location(
	const sqlparser_proto_identifier_style_t *styles,
	size_t count,
	const sqlparser_generated_source_t *source)
{
	uint32_t payload;
	size_t index;

	if (source != NULL && source->spelling_handle != NULL &&
	    source->spelling_handle->identifier_spelling_build_group !=
		    SIZE_MAX) {
		return (int32_t)(
			(int64_t)SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_BASE +
			(int64_t)(
				source->spelling_handle
					->identifier_spelling_build_group
				<<
				SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENT_BITS));
	}
	if (styles == NULL || count == 0U ||
	    count > SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	payload = 0U;
	for (index = 0U; index < count; index++) {
		if (styles[index] == SQLPARSER_PROTO_IDENTIFIER_STYLE_NONE) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		payload |=
			(uint32_t)styles[index] <<
			(index * SQLPARSER_PROTO_IDENTIFIER_STYLE_BITS);
	}
	return (int32_t)(
		SQLPARSER_PROTO_LOCATION_GENERATED_STYLE_BASE -
		(int32_t)payload);
}

static void sqlparser_generated_set_string_style(
	PgQuery__Node *part,
	sqlparser_proto_identifier_style_t style,
	const sqlparser_generated_source_t *source)
{
	if (part == NULL ||
	    part->node_case != PG_QUERY__NODE__NODE_STRING ||
	    part->string == NULL) {
		return;
	}
	part->string->location =
		source != NULL && source->spelling_handle != NULL &&
				sqlparser_proto_location_is_identifier_spelling(
					source->spelling_handle
						->identifier_spelling_last_location) ?
			source->spelling_handle->identifier_spelling_last_location :
			sqlparser_generated_style_location(&style, 1U, NULL);
}

static int sqlparser_generated_read_identifier_list(
	PgQuery__Node *const *parts,
	size_t part_count,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	size_t *position)
{
	size_t index;
	size_t length;

	if (parts == NULL || part_count == 0U ||
	    parser_sql == NULL || position == NULL) {
		return 0;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	for (index = 0U; index < part_count; index++) {
		PgQuery__Node *part;
		sqlparser_proto_identifier_style_t style;

		part = parts[index];
		if (part == NULL ||
		    part->node_case != PG_QUERY__NODE__NODE_STRING ||
		    part->string == NULL ||
		    !sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    position,
			    part->string->sval,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return 0;
		}
		sqlparser_generated_set_string_style(part, style, source);
		if (index + 1U < part_count) {
			*position = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				*position);
			if (*position >= length ||
			    parser_sql[*position] != ',') {
				return 0;
			}
			(*position)++;
		}
	}
	return 1;
}

static int32_t sqlparser_generated_node_name_list_location(
	PgQuery__Node *const *parts,
	size_t part_count,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t
		styles[SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS];
	size_t index;
	size_t length;
	size_t position;

	if (parser_sql == NULL || location < 0 || parts == NULL ||
	    part_count == 0U) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	for (index = 0U; index < part_count; index++) {
		PgQuery__Node *part;
		sqlparser_proto_identifier_style_t style;

		part = parts[index];
		if (part == NULL ||
		    part->node_case != PG_QUERY__NODE__NODE_STRING ||
		    part->string == NULL ||
		    !sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    part->string->sval,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		sqlparser_generated_set_string_style(part, style, source);
		if (index < SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS) {
			styles[index] = style;
		}
		if (index + 1U < part_count) {
			position = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				position);
			if (position >= length || parser_sql[position] != '.') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		}
	}
	return part_count <= SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS ?
		sqlparser_generated_style_location(styles, part_count, source) :
		SQLPARSER_PROTO_LOCATION_GENERATED;
}

static int32_t sqlparser_generated_type_name_location(
	PgQuery__TypeName *type_name,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t styles[2];
	int32_t marked_location;

	if (type_name == NULL) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	marked_location = sqlparser_generated_node_name_list_location(
		type_name->names,
		type_name->n_names,
		parser_sql,
		parser_fragment_offset,
		source,
		location);
	if (marked_location != SQLPARSER_PROTO_LOCATION_GENERATED) {
		return marked_location;
	}
	if (type_name->n_names != 2U ||
	    type_name->names == NULL ||
	    type_name->names[0] == NULL ||
	    type_name->names[1] == NULL ||
	    type_name->names[0]->node_case !=
		    PG_QUERY__NODE__NODE_STRING ||
	    type_name->names[1]->node_case !=
		    PG_QUERY__NODE__NODE_STRING ||
	    type_name->names[0]->string == NULL ||
	    type_name->names[1]->string == NULL ||
	    type_name->names[0]->string->sval == NULL ||
	    strcmp(type_name->names[0]->string->sval, "pg_catalog") != 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	styles[0] = SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED;
	styles[1] = SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED;
	sqlparser_generated_set_string_style(
		type_name->names[0],
		styles[0],
		source);
	sqlparser_generated_set_string_style(
		type_name->names[1],
		styles[1],
		source);
	return sqlparser_generated_style_location(styles, 2U, source);
}

static int32_t sqlparser_generated_column_ref_location(
	ProtobufCMessage *message,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	PgQuery__ColumnRef *column_ref;
	sqlparser_proto_identifier_style_t
		styles[SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS];
	size_t component_index;
	size_t field_index;
	size_t length;
	size_t position;

	if (parser_sql == NULL ||
	    location < 0 ||
	    message == NULL ||
	    message->descriptor != &pg_query__column_ref__descriptor) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	column_ref = (PgQuery__ColumnRef *)message;
	if (column_ref->n_fields == 0U || column_ref->fields == NULL) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	component_index = 0U;
	for (field_index = 0U;
	     field_index < column_ref->n_fields;
	     field_index++) {
		PgQuery__Node *part;

		part = column_ref->fields[field_index];
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (part != NULL &&
		    part->node_case == PG_QUERY__NODE__NODE_STRING &&
		    part->string != NULL) {
			if (component_index >=
				    SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS ||
			    !sqlparser_generated_read_identifier(
				    parser_sql,
				    length,
				    &position,
				    part->string->sval,
				    parser_fragment_offset,
				    source,
				    &styles[component_index])) {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			sqlparser_generated_set_string_style(
				part,
				styles[component_index],
				source);
			component_index++;
		} else if (part != NULL &&
			   part->node_case == PG_QUERY__NODE__NODE_A_STAR) {
			position = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				position);
			if (position >= length || parser_sql[position] != '*') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		} else {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		if (field_index + 1U < column_ref->n_fields) {
			position = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				position);
			if (position >= length || parser_sql[position] != '.') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		}
	}
	return sqlparser_generated_style_location(
		styles,
		component_index,
		source);
}

static int32_t sqlparser_generated_range_var_location(
	PgQuery__RangeVar *range_var,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t
		styles[SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS];
	const char *relation_parts[3];
	size_t component_index;
	size_t length;
	size_t part_count;
	size_t part_index;
	size_t position;

	if (range_var == NULL || parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	relation_parts[0] = range_var->catalogname;
	relation_parts[1] = range_var->schemaname;
	relation_parts[2] = range_var->relname;
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	component_index = 0U;
	part_count = 0U;
	for (part_index = 0U; part_index < 3U; part_index++) {
		if (relation_parts[part_index] != NULL &&
		    relation_parts[part_index][0] != '\0') {
			part_count++;
		}
	}
	for (part_index = 0U; part_index < 3U; part_index++) {
		if (relation_parts[part_index] == NULL ||
		    relation_parts[part_index][0] == '\0') {
			continue;
		}
		if (component_index >=
			    SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS ||
		    !sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    relation_parts[part_index],
			    parser_fragment_offset,
			    source,
			    &styles[component_index])) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		component_index++;
		if (component_index < part_count) {
			position = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				position);
			if (position >= length || parser_sql[position] != '.') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		}
	}
	return sqlparser_generated_style_location(
		styles,
		component_index,
		source);
}

static int32_t sqlparser_generated_common_table_expr_location(
	PgQuery__CommonTableExpr *cte,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t cte_style;
	size_t index;
	size_t length;
	size_t position;

	if (cte == NULL || cte->ctename == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (!sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    cte->ctename,
		    parser_fragment_offset,
		    source,
		    &cte_style)) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (cte->n_aliascolnames > 0U) {
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (position >= length || parser_sql[position] != '(') {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		position++;
	}
	for (index = 0U; index < cte->n_aliascolnames; index++) {
		PgQuery__Node *part;
		sqlparser_proto_identifier_style_t style;

		part = cte->aliascolnames[index];
		if (part == NULL ||
		    part->node_case != PG_QUERY__NODE__NODE_STRING ||
		    part->string == NULL ||
		    !sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    part->string->sval,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		sqlparser_generated_set_string_style(part, style, source);
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (index + 1U < cte->n_aliascolnames) {
			if (position >= length || parser_sql[position] != ',') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		}
	}
	return sqlparser_generated_style_location(&cte_style, 1U, source);
}

static int32_t sqlparser_generated_collate_clause_location(
	PgQuery__CollateClause *clause,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t
		styles[SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS];
	size_t index;
	size_t length;
	size_t position;
	size_t token_end;
	sqlparser_proto_identifier_style_t token_style;

	if (clause == NULL || clause->collname == NULL ||
	    clause->n_collname == 0U ||
	    clause->n_collname >
		    SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = sqlparser_generated_skip_trivia(
		parser_sql,
		length,
		(size_t)location);
	if (sqlparser_generated_identifier_token(
		    parser_sql,
		    length,
		    position,
		    &token_end,
		    &token_style) &&
	    token_style == SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED &&
	    sqlparser_generated_token_equal_ci(
		    parser_sql,
		    position,
		    token_end,
		    "collate")) {
		position = token_end;
	}
	for (index = 0U; index < clause->n_collname; index++) {
		PgQuery__Node *part;

		part = clause->collname[index];
		if (part == NULL ||
		    part->node_case != PG_QUERY__NODE__NODE_STRING ||
		    part->string == NULL ||
		    !sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    part->string->sval,
			    parser_fragment_offset,
			    source,
			    &styles[index])) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		sqlparser_generated_set_string_style(
			part,
			styles[index],
			source);
		if (index + 1U < clause->n_collname) {
			position = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				position);
			if (position >= length || parser_sql[position] != '.') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		}
	}
	return sqlparser_generated_style_location(
		styles,
		clause->n_collname,
		source);
}

static int32_t sqlparser_generated_alias_location(
	PgQuery__Alias *alias,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t alias_style;
	size_t index;
	size_t length;
	size_t position;

	if (alias == NULL || alias->aliasname == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (!sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    alias->aliasname,
		    parser_fragment_offset,
		    source,
		    &alias_style)) {
		sqlparser_proto_identifier_style_t token_style;
		size_t token_end;

		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (!sqlparser_generated_identifier_token(
			    parser_sql,
			    length,
			    position,
			    &token_end,
			    &token_style) ||
		    token_style !=
			    SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED ||
		    token_end - position != 2U ||
		    (parser_sql[position] != 'A' &&
		     parser_sql[position] != 'a') ||
		    (parser_sql[position + 1U] != 'S' &&
		     parser_sql[position + 1U] != 's')) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		position = token_end;
		if (!sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    alias->aliasname,
			    parser_fragment_offset,
			    source,
			    &alias_style)) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
	}
	if (alias->n_colnames > 0U) {
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (position >= length || parser_sql[position] != '(') {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		position++;
	}
	for (index = 0U; index < alias->n_colnames; index++) {
		PgQuery__Node *part;
		sqlparser_proto_identifier_style_t style;

		part = alias->colnames[index];
		if (part == NULL ||
		    part->node_case != PG_QUERY__NODE__NODE_STRING ||
		    part->string == NULL ||
		    !sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    part->string->sval,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		sqlparser_generated_set_string_style(part, style, source);
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (index + 1U < alias->n_colnames) {
			if (position >= length || parser_sql[position] != ',') {
				return SQLPARSER_PROTO_LOCATION_GENERATED;
			}
			position++;
		}
	}
	if (alias->n_colnames > 0U) {
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (position >= length || parser_sql[position] != ')') {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
	}
	return sqlparser_generated_style_location(&alias_style, 1U, source);
}

static int32_t sqlparser_generated_window_def_location(
	PgQuery__WindowDef *window,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t styles[2];
	size_t component_count;
	size_t length;
	size_t position;

	if (window == NULL || parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if ((window->name == NULL || window->name[0] == '\0') &&
	    (window->refname == NULL || window->refname[0] == '\0')) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (window->name != NULL && window->name[0] != '\0') {
		if (!sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    window->name,
		    parser_fragment_offset,
		    source,
		    &styles[0])) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
	} else {
		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		if (position >= length || parser_sql[position] != '(') {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		position++;
		if (!sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    window->refname,
			    parser_fragment_offset,
			    source,
			    &styles[0])) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
	}
	component_count = 1U;
	if (window->name != NULL && window->name[0] != '\0' &&
	    window->refname != NULL && window->refname[0] != '\0') {
		while (position < length && parser_sql[position] != '(') {
			position++;
		}
		if (position >= length) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		position++;
		if (!sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    window->refname,
			    parser_fragment_offset,
			    source,
			    &styles[1])) {
			return SQLPARSER_PROTO_LOCATION_GENERATED;
		}
		component_count = 2U;
	}
	return sqlparser_generated_style_location(
		styles,
		component_count,
		source);
}

static int32_t sqlparser_generated_res_target_location(
	PgQuery__ResTarget *target,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t
		styles[SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS];
	sqlparser_proto_identifier_style_t style;
	size_t component_count;
	size_t index;
	size_t length;
	size_t position;

	if (target == NULL || target->name == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    target->name,
		    parser_fragment_offset,
		    source,
		    &style)) {
		size_t next;

		styles[0] = style;
		component_count = 1U;
		next = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		for (index = 0U;
		     next < length &&
		     parser_sql[next] == '.' &&
		     target->indirection != NULL &&
		     index < target->n_indirection;
		     index++) {
			PgQuery__Node *part;

			part = target->indirection[index];
			if (part == NULL ||
			    part->node_case != PG_QUERY__NODE__NODE_STRING ||
			    part->string == NULL ||
			    component_count >=
				    SQLPARSER_PROTO_IDENTIFIER_STYLE_COMPONENTS) {
				break;
			}
			position = next + 1U;
			if (!sqlparser_generated_read_identifier(
				    parser_sql,
				    length,
				    &position,
				    part->string->sval,
				    parser_fragment_offset,
				    source,
				    &styles[component_count])) {
				break;
			}
			sqlparser_generated_set_string_style(
				part,
				styles[component_count],
				source);
			component_count++;
			next = sqlparser_generated_skip_trivia(
				parser_sql,
				length,
				position);
		}
		if (next < length &&
		    (parser_sql[next] == '=' ||
		     parser_sql[next] == '.' ||
		     parser_sql[next] == '[')) {
			return sqlparser_generated_style_location(
				styles,
				component_count,
				source);
		}
	}
	position = (size_t)location;
	while (position < length) {
		sqlparser_proto_identifier_style_t token_style;
		size_t start;
		size_t token_end;

		position = sqlparser_generated_skip_trivia(
			parser_sql,
			length,
			position);
		start = position;
		if (sqlparser_generated_identifier_token(
			    parser_sql,
			    length,
			    start,
			    &token_end,
			    &token_style)) {
			if (token_style ==
				    SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED &&
			    token_end - start == 2U &&
			    (parser_sql[start] == 'A' ||
			     parser_sql[start] == 'a') &&
			    (parser_sql[start + 1U] == 'S' ||
			     parser_sql[start + 1U] == 's')) {
				position = token_end;
				if (sqlparser_generated_read_identifier(
					    parser_sql,
					    length,
					    &position,
					    target->name,
					    parser_fragment_offset,
					    source,
					    &style)) {
					return sqlparser_generated_style_location(
						&style,
						1U,
						source);
				}
			}
			position = token_end;
		} else if (parser_sql[position] == '\'') {
			position++;
			while (position < length) {
				if (parser_sql[position] == '\'' &&
				    position + 1U < length &&
				    parser_sql[position + 1U] == '\'') {
					position += 2U;
				} else if (parser_sql[position] == '\'') {
					position++;
					break;
				} else {
					position++;
				}
			}
		} else {
			position++;
		}
	}
	return SQLPARSER_PROTO_LOCATION_GENERATED;
}

static int32_t sqlparser_generated_cte_search_location(
	PgQuery__CTESearchClause *clause,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t style;
	size_t length;
	size_t position;

	if (clause == NULL || clause->search_seq_column == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (!sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "search") ||
	    !sqlparser_generated_read_keyword(
		    parser_sql,
		    length,
		    &position,
		    clause->search_breadth_first ? "breadth" : "depth") ||
	    !sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "first") ||
	    !sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "by") ||
	    !sqlparser_generated_read_identifier_list(
		    clause->search_col_list,
		    clause->n_search_col_list,
		    parser_sql,
		    parser_fragment_offset,
		    source,
		    &position) ||
	    !sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "set") ||
	    !sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    clause->search_seq_column,
		    parser_fragment_offset,
		    source,
		    &style)) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (source != NULL && source->spelling_handle != NULL &&
	    sqlparser_proto_location_is_identifier_spelling(
		    source->spelling_handle
			    ->identifier_spelling_last_location)) {
		return source->spelling_handle
			->identifier_spelling_last_location;
	}
	return sqlparser_generated_style_location(&style, 1U, source);
}

static int32_t sqlparser_generated_cte_cycle_location(
	PgQuery__CTECycleClause *clause,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t styles[2];
	int32_t mark_location;
	size_t length;
	size_t position;

	if (clause == NULL || clause->cycle_mark_column == NULL ||
	    clause->cycle_path_column == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	mark_location = SQLPARSER_PROTO_LOCATION_GENERATED;
	if (!sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "cycle") ||
	    !sqlparser_generated_read_identifier_list(
		    clause->cycle_col_list,
		    clause->n_cycle_col_list,
		    parser_sql,
		    parser_fragment_offset,
		    source,
		    &position) ||
	    !sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "set") ||
	    !sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    clause->cycle_mark_column,
		    parser_fragment_offset,
		    source,
		    &styles[0])) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (source != NULL && source->spelling_handle != NULL) {
		mark_location = source->spelling_handle
			->identifier_spelling_last_location;
	}
	if (!sqlparser_generated_find_keyword(
		    parser_sql, length, &position, "using") ||
	    !sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    clause->cycle_path_column,
		    parser_fragment_offset,
		    source,
		    &styles[1])) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (sqlparser_proto_location_is_identifier_spelling(
		    mark_location)) {
		return mark_location;
	}
	return sqlparser_generated_style_location(styles, 2U, source);
}

static int32_t sqlparser_generated_column_def_location(
	PgQuery__ColumnDef *column,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t style;
	size_t length;
	size_t position;

	if (column == NULL || column->colname == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (!sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    column->colname,
		    parser_fragment_offset,
		    source,
		    &style)) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	return sqlparser_generated_style_location(&style, 1U, source);
}

static int32_t sqlparser_generated_xml_expr_location(
	PgQuery__XmlExpr *expression,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	sqlparser_proto_identifier_style_t style;
	const char *function_name;
	size_t length;
	size_t position;

	if (expression == NULL || expression->name == NULL ||
	    parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (expression->op == PG_QUERY__XML_EXPR_OP__IS_XMLELEMENT) {
		function_name = "xmlelement";
	} else if (expression->op == PG_QUERY__XML_EXPR_OP__IS_XMLPI) {
		function_name = "xmlpi";
	} else {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	position = (size_t)location;
	if (!sqlparser_generated_read_keyword(
		    parser_sql, length, &position, function_name)) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	position = sqlparser_generated_skip_trivia(
		parser_sql, length, position);
	if (position >= length || parser_sql[position] != '(') {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	position++;
	if (!sqlparser_generated_read_keyword(
		    parser_sql, length, &position, "name") ||
	    !sqlparser_generated_read_identifier(
		    parser_sql,
		    length,
		    &position,
		    expression->name,
		    parser_fragment_offset,
		    source,
		    &style)) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	return sqlparser_generated_style_location(&style, 1U, source);
}

static const char *sqlparser_sql_value_function_keyword(
	PgQuery__SQLValueFunctionOp op)
{
	switch (op) {
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_DATE:
			return "current_date";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIME:
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIME_N:
			return "current_time";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIMESTAMP:
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIMESTAMP_N:
			return "current_timestamp";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIME:
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIME_N:
			return "localtime";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIMESTAMP:
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIMESTAMP_N:
			return "localtimestamp";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_ROLE:
			return "current_role";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_USER:
			return "current_user";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_USER:
			return "user";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_SESSION_USER:
			return "session_user";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_CATALOG:
			return "current_catalog";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_SCHEMA:
			return "current_schema";
		case PG_QUERY__SQLVALUE_FUNCTION_OP__SQLVALUE_FUNCTION_OP_UNDEFINED:
		default:
			return NULL;
	}
}

static int32_t sqlparser_generated_sql_value_function_location(
	const PgQuery__SQLValueFunction *expression,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	const char *keyword;
	const char *token;
	size_t end;
	size_t index;
	size_t length;
	size_t start;
	sqlparser_proto_identifier_style_t style;
	uint32_t uppercase_mask;

	if (expression == NULL || parser_sql == NULL || location < 0) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	keyword = sqlparser_sql_value_function_keyword(expression->op);
	if (keyword == NULL) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	length = sqlparser_generated_parser_sql_length(
		parser_sql,
		source);
	start = sqlparser_generated_skip_trivia(
		parser_sql,
		length,
		(size_t)location);
	if (!sqlparser_generated_identifier_token(
		    parser_sql,
		    length,
		    start,
		    &end,
		    &style) ||
	    style != SQLPARSER_PROTO_IDENTIFIER_STYLE_UNQUOTED ||
	    !sqlparser_generated_token_equal_ci(
		    parser_sql,
		    start,
		    end,
		    keyword)) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	token = parser_sql + start;
	if (source != NULL &&
	    source->origins != NULL &&
	    source->public_sql != NULL &&
	    start >= parser_fragment_offset) {
		sqlparser_identifier_origin_t origin;
		size_t public_sql_length;

		public_sql_length = source->public_sql_length;
		if (sqlparser_identifier_origin_map_lookup(
			    source->origins,
			    start - parser_fragment_offset,
			    end - start,
			    &origin) == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE &&
		    origin.source_offset <= public_sql_length &&
		    origin.source_length <=
			    public_sql_length -
				    origin.source_offset &&
		    sqlparser_generated_token_equal_ci(
			    source->public_sql,
			    origin.source_offset,
			    origin.source_offset + origin.source_length,
			    keyword)) {
			token = source->public_sql + origin.source_offset;
			start = origin.source_offset;
			end = start + origin.source_length;
		}
	}
	uppercase_mask = 0U;
	for (index = 0U; index < end - start; index++) {
		if (token[index] >= 'A' && token[index] <= 'Z') {
			uppercase_mask |= UINT32_C(1) << index;
		}
	}
	return (int32_t)(
		(int64_t)SQLPARSER_PROTO_LOCATION_SQL_VALUE_CASE_BASE -
		(int64_t)uppercase_mask);
}

static int32_t sqlparser_generated_message_location(
	ProtobufCMessage *message,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	int32_t location)
{
	if (message == NULL || message->descriptor == NULL) {
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (source != NULL && source->spelling_handle != NULL) {
		sqlparser_handle_begin_identifier_spelling(
			source->spelling_handle);
	}
	if (message->descriptor == &pg_query__string__descriptor) {
		PgQuery__String *string;
		sqlparser_proto_identifier_style_t style;
		size_t length;
		size_t position;

		string = (PgQuery__String *)message;
		length = sqlparser_generated_parser_sql_length(
			parser_sql,
			source);
		position = location >= 0 ? (size_t)location : 0U;
		if (string->sval != NULL &&
		    sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    string->sval,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return sqlparser_generated_style_location(
				&style,
				1U,
				source);
		}
		return SQLPARSER_PROTO_LOCATION_GENERATED;
	}
	if (message->descriptor == &pg_query__column_ref__descriptor) {
		return sqlparser_generated_column_ref_location(
			message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__range_var__descriptor) {
		return sqlparser_generated_range_var_location(
			(PgQuery__RangeVar *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__alias__descriptor) {
		return sqlparser_generated_alias_location(
			(PgQuery__Alias *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor ==
	    &pg_query__common_table_expr__descriptor) {
		return sqlparser_generated_common_table_expr_location(
			(PgQuery__CommonTableExpr *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor ==
	    &pg_query__ctesearch_clause__descriptor) {
		return sqlparser_generated_cte_search_location(
			(PgQuery__CTESearchClause *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor ==
	    &pg_query__ctecycle_clause__descriptor) {
		return sqlparser_generated_cte_cycle_location(
			(PgQuery__CTECycleClause *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor ==
	    &pg_query__collate_clause__descriptor) {
		return sqlparser_generated_collate_clause_location(
			(PgQuery__CollateClause *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__column_def__descriptor) {
		return sqlparser_generated_column_def_location(
			(PgQuery__ColumnDef *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor ==
	    &pg_query__sqlvalue_function__descriptor) {
		return sqlparser_generated_sql_value_function_location(
			(PgQuery__SQLValueFunction *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__xml_expr__descriptor) {
		return sqlparser_generated_xml_expr_location(
			(PgQuery__XmlExpr *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__window_def__descriptor) {
		return sqlparser_generated_window_def_location(
			(PgQuery__WindowDef *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__func_call__descriptor) {
		PgQuery__FuncCall *call;

		call = (PgQuery__FuncCall *)message;
		return sqlparser_generated_node_name_list_location(
			call->funcname,
			call->n_funcname,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__type_name__descriptor) {
		return sqlparser_generated_type_name_location(
			(PgQuery__TypeName *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__res_target__descriptor) {
		return sqlparser_generated_res_target_location(
			(PgQuery__ResTarget *)message,
			parser_sql,
			parser_fragment_offset,
			source,
			location);
	}
	if (message->descriptor == &pg_query__named_arg_expr__descriptor) {
		PgQuery__NamedArgExpr *argument;
		sqlparser_proto_identifier_style_t style;
		size_t length;
		size_t position;

		argument = (PgQuery__NamedArgExpr *)message;
		length = sqlparser_generated_parser_sql_length(
			parser_sql,
			source);
		position = location >= 0 ? (size_t)location : 0U;
		if (argument->name != NULL &&
		    sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    argument->name,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return sqlparser_generated_style_location(
				&style,
				1U,
				source);
		}
	}
	if (message->descriptor == &pg_query__a__const__descriptor) {
		PgQuery__AConst *constant;
		sqlparser_proto_identifier_style_t style;
		size_t length;
		size_t position;

		constant = (PgQuery__AConst *)message;
		length = sqlparser_generated_parser_sql_length(
			parser_sql,
			source);
		position = location >= 0 ? (size_t)location : 0U;
		if (constant->val_case == PG_QUERY__A__CONST__VAL_SVAL &&
		    constant->sval != NULL &&
		    constant->sval->sval != NULL &&
		    sqlparser_generated_read_identifier(
			    parser_sql,
			    length,
			    &position,
			    constant->sval->sval,
			    parser_fragment_offset,
			    source,
			    &style)) {
			return sqlparser_generated_style_location(
				&style,
				1U,
				source);
		}
	}
	return SQLPARSER_PROTO_LOCATION_GENERATED;
}

static void sqlparser_mark_proto_generated_internal(
	ProtobufCMessage *message,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (message == NULL || message->descriptor == NULL) {
		return;
	}

	descriptor = message->descriptor;
	base = (uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) != (int)field->id) {
			continue;
		}
		if (field->type == PROTOBUF_C_TYPE_INT32 &&
		    field->label != PROTOBUF_C_LABEL_REPEATED &&
		    strcmp(field->name, "location") == 0) {
			int32_t *location;

			location = (int32_t *)(base + field->offset);
			if (!sqlparser_proto_location_is_generated(*location)) {
				*location = sqlparser_generated_message_location(
					message,
					parser_sql,
					parser_fragment_offset,
					source,
					*location);
			}
			continue;
		}
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}
		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage **items;
			size_t item_index;

			item_count = *(const size_t *)(base + field->quantifier_offset);
			items = *(ProtobufCMessage ***)(base + field->offset);
			for (item_index = 0U; items != NULL && item_index < item_count; item_index++) {
				sqlparser_mark_proto_generated_internal(
					items[item_index],
					parser_sql,
					parser_fragment_offset,
					source);
			}
		} else {
			sqlparser_mark_proto_generated_internal(
				*(ProtobufCMessage **)(base + field->offset),
				parser_sql,
				parser_fragment_offset,
				source);
		}
	}
}

void sqlparser_mark_proto_generated(ProtobufCMessage *message)
{
	sqlparser_mark_proto_generated_internal(message, NULL, 0U, NULL);
}

static const sqlparser_generated_source_t *
sqlparser_measure_generated_source(
	const sqlparser_generated_source_t *source,
	const char *parser_sql,
	sqlparser_generated_source_t *measured)
{
	if (source == NULL) {
		return NULL;
	}
	*measured = *source;
	measured->public_sql_length =
		source->public_sql != NULL ? strlen(source->public_sql) : 0U;
	measured->parser_sql_length =
		parser_sql != NULL ? strlen(parser_sql) : 0U;
	return measured;
}

static sqlparser_status_t sqlparser_finish_generated_source(
	const sqlparser_generated_source_t *source,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_handle_finish_identifier_spelling(
		source != NULL ? source->spelling_handle : NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK && source != NULL) {
		sqlparser_handle_sweep_identifier_spellings(
			source->spelling_handle);
	}
	return status;
}

sqlparser_status_t sqlparser_mark_proto_generated_with_fragment_source(
	ProtobufCMessage *message,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	sqlparser_error_t *out_error)
{
	sqlparser_generated_source_t measured_source;

	source = sqlparser_measure_generated_source(
		source,
		parser_sql,
		&measured_source);
	sqlparser_mark_proto_generated_internal(
		message,
		parser_sql,
		parser_fragment_offset,
		source);
	return sqlparser_finish_generated_source(source, out_error);
}

sqlparser_status_t sqlparser_mark_proto_nodes_generated_with_fragment_source(
	PgQuery__Node *const *nodes,
	size_t count,
	const char *parser_sql,
	size_t parser_fragment_offset,
	const sqlparser_generated_source_t *source,
	sqlparser_error_t *out_error)
{
	sqlparser_generated_source_t measured_source;
	size_t index;

	source = sqlparser_measure_generated_source(
		source,
		parser_sql,
		&measured_source);
	for (index = 0U; nodes != NULL && index < count; index++) {
		sqlparser_mark_proto_generated_internal(
			(ProtobufCMessage *)nodes[index],
			parser_sql,
			parser_fragment_offset,
			source);
	}
	return sqlparser_finish_generated_source(source, out_error);
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
