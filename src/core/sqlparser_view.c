#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_ast_internal.h"

typedef struct {
	json_t *objects;
	sqlparser_handle_t *handle;
	size_t statement_index;
} sqlparser_view_build_t;

static int sqlparser_text_equal(const char *left, const char *right)
{
	if (left == NULL || left[0] == '\0') {
		left = NULL;
	}
	if (right == NULL || right[0] == '\0') {
		right = NULL;
	}
	if (left == NULL || right == NULL) {
		return left == right;
	}
	return strcmp(left, right) == 0;
}

static const char *sqlparser_nonempty(const char *value)
{
	return value != NULL && value[0] != '\0' ? value : NULL;
}

static int sqlparser_json_set_string_or_null(json_t *object, const char *key, const char *value)
{
	json_t *item;

	if (object == NULL || key == NULL) {
		return -1;
	}
	item = value != NULL && value[0] != '\0' ? json_string(value) : json_null();
	if (item == NULL) {
		return -1;
	}
	return json_object_set_new(object, key, item);
}

static int sqlparser_json_set_size(json_t *object, const char *key, size_t value)
{
	return json_object_set_new(object, key, json_integer((json_int_t)value));
}

static int sqlparser_json_set_selector_or_null(
	json_t *object,
	const char *key,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	char *selector_text;
	sqlparser_status_t status;
	int rc;

	if (selector == NULL || selector->kind == SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		return json_object_set_new(object, key, json_null());
	}

	selector_text = NULL;
	status = sqlparser_selector_format(selector, &selector_text, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	rc = json_object_set_new(object, key, json_string(selector_text));
	sqlparser_string_free(selector_text);
	return rc;
}

static const char *sqlparser_transaction_keyword(const PgQuery__TransactionStmt *stmt)
{
	if (stmt == NULL) {
		return "transaction";
	}
	switch (stmt->kind) {
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_BEGIN:
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_START:
			return "begin";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_COMMIT:
			return "commit";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK:
			return "rollback";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_SAVEPOINT:
			return "savepoint";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_RELEASE:
			return "release";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK_TO:
			return "rollback_to";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_PREPARE:
			return "prepare_transaction";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_COMMIT_PREPARED:
			return "commit_prepared";
		case PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK_PREPARED:
			return "rollback_prepared";
		default:
			return "transaction";
	}
}

static const char *sqlparser_drop_keyword(const PgQuery__DropStmt *stmt)
{
	if (stmt == NULL) {
		return "drop";
	}
	switch (stmt->remove_type) {
		case PG_QUERY__OBJECT_TYPE__OBJECT_VIEW:
			return "drop_view";
		case PG_QUERY__OBJECT_TYPE__OBJECT_TABLE:
			return "drop_table";
		case PG_QUERY__OBJECT_TYPE__OBJECT_DATABASE:
			return "drop_database";
		case PG_QUERY__OBJECT_TYPE__OBJECT_SCHEMA:
			return "drop_schema";
		case PG_QUERY__OBJECT_TYPE__OBJECT_INDEX:
			return "drop_index";
		case PG_QUERY__OBJECT_TYPE__OBJECT_SEQUENCE:
			return "drop_sequence";
		default:
			return "drop";
	}
}

static const char *sqlparser_vacuum_keyword(const PgQuery__VacuumStmt *stmt)
{
	return stmt != NULL && stmt->is_vacuumcmd ? "vacuum" : "analyze";
}

static const char *sqlparser_statement_keyword_from_node(const PgQuery__Node *statement)
{
	if (statement == NULL) {
		return "unknown";
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return "select";
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return "insert";
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return "update";
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return "delete";
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return "merge";
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return "create_view";
		case PG_QUERY__NODE__NODE_CREATE_STMT:
			return "create_table";
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return statement->create_table_as_stmt != NULL &&
				statement->create_table_as_stmt->objtype == PG_QUERY__OBJECT_TYPE__OBJECT_MATVIEW ?
				"create_materialized_view" :
				"create_table_as";
		case PG_QUERY__NODE__NODE_CREATE_SCHEMA_STMT:
			return "create_schema";
		case PG_QUERY__NODE__NODE_INDEX_STMT:
			return "create_index";
		case PG_QUERY__NODE__NODE_DROP_STMT:
			return sqlparser_drop_keyword(statement->drop_stmt);
		case PG_QUERY__NODE__NODE_ALTER_TABLE_STMT:
			return "alter_table";
		case PG_QUERY__NODE__NODE_RENAME_STMT:
			return "rename";
		case PG_QUERY__NODE__NODE_GRANT_STMT:
			return statement->grant_stmt != NULL && !statement->grant_stmt->is_grant ? "revoke" : "grant";
		case PG_QUERY__NODE__NODE_VACUUM_STMT:
			return sqlparser_vacuum_keyword(statement->vacuum_stmt);
		case PG_QUERY__NODE__NODE_TRANSACTION_STMT:
			return sqlparser_transaction_keyword(statement->transaction_stmt);
		case PG_QUERY__NODE__NODE_VARIABLE_SET_STMT:
			return "set";
		default:
			return sqlparser_statement_kind_name(sqlparser_statement_kind_from_case(statement->node_case));
	}
}

static int sqlparser_variable_set_name_is(const PgQuery__VariableSetStmt *stmt, const char *name)
{
	return stmt != NULL &&
		stmt->name != NULL &&
		name != NULL &&
		strcmp(stmt->name, name) == 0;
}

static int sqlparser_variable_set_is_session_context(const PgQuery__VariableSetStmt *stmt)
{
	return sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) ||
		sqlparser_variable_set_name_is(stmt, "search_path");
}

static const char *sqlparser_variable_set_public_name(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt)
{
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE)) {
		return handle != NULL && handle->dialect == SQLPARSER_DIALECT_ORACLE ? "CONTAINER" : "DATABASE";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA)) {
		return handle != NULL && handle->dialect == SQLPARSER_DIALECT_ORACLE ? "CURRENT_SCHEMA" : "SCHEMA";
	}
	if (sqlparser_variable_set_name_is(stmt, "search_path")) {
		return "search_path";
	}
	return stmt != NULL ? stmt->name : NULL;
}

static const char *sqlparser_variable_set_operator(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt)
{
	if (stmt == NULL) {
		return NULL;
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE) &&
	    handle != NULL &&
	    (handle->dialect == SQLPARSER_DIALECT_MYSQL || handle->dialect == SQLPARSER_DIALECT_SQLSERVER)) {
		return NULL;
	}
	if (sqlparser_variable_set_name_is(stmt, "search_path")) {
		return "to";
	}
	return "=";
}

static const char *sqlparser_variable_set_column_keyword(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt)
{
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE) &&
	    handle != NULL &&
	    (handle->dialect == SQLPARSER_DIALECT_MYSQL || handle->dialect == SQLPARSER_DIALECT_SQLSERVER)) {
		return "use";
	}
	return "set";
}

static int sqlparser_variable_set_arg_slot_matches(
	const PgQuery__Node *statement,
	PgQuery__Node **slot)
{
	PgQuery__VariableSetStmt *stmt;
	size_t index;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    slot == NULL) {
		return 0;
	}

	stmt = statement->variable_set_stmt;
	for (index = 0U; index < stmt->n_args; index++) {
		if (&stmt->args[index] == slot) {
			return 1;
		}
	}
	return 0;
}

static size_t sqlparser_variable_set_arg_index(
	const PgQuery__VariableSetStmt *stmt,
	const PgQuery__Node *value_node)
{
	size_t index;

	if (stmt == NULL || value_node == NULL) {
		return (size_t)-1;
	}
	for (index = 0U; index < stmt->n_args; index++) {
		if (stmt->args[index] == value_node) {
			return index;
		}
	}
	return (size_t)-1;
}

static const char *sqlparser_statement_keyword_for_handle(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *statement)
{
	PgQuery__VariableSetStmt *stmt;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL) {
		return sqlparser_statement_keyword_from_node(statement);
	}

	stmt = statement->variable_set_stmt;
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE)) {
		if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_ORACLE) {
			return "alter_session";
		}
		if (handle != NULL &&
		    (handle->dialect == SQLPARSER_DIALECT_MYSQL || handle->dialect == SQLPARSER_DIALECT_SQLSERVER)) {
			return "use";
		}
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) &&
	    handle != NULL &&
	    handle->dialect == SQLPARSER_DIALECT_ORACLE) {
		return "alter_session";
	}
	return "set";
}

static const char *sqlparser_variable_set_public_name_at(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt,
	size_t arg_index)
{
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE) &&
	    handle != NULL &&
	    handle->dialect == SQLPARSER_DIALECT_ORACLE &&
	    arg_index == 1U) {
		return "SERVICE";
	}
	return sqlparser_variable_set_public_name(handle, stmt);
}

static int sqlparser_variable_set_arg_needs_statement_postprocess(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt,
	size_t arg_index)
{
	if (handle == NULL || arg_index != 0U) {
		return 0;
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE)) {
		return handle->dialect == SQLPARSER_DIALECT_MYSQL ||
			handle->dialect == SQLPARSER_DIALECT_SQLSERVER ||
			handle->dialect == SQLPARSER_DIALECT_ORACLE;
	}
	return handle->dialect == SQLPARSER_DIALECT_ORACLE &&
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA);
}

static sqlparser_status_t sqlparser_build_variable_set_statement_sql(
	const PgQuery__VariableSetStmt *stmt,
	const char *value_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	static const char prefix[] = "SET ";
	static const char separator[] = " TO ";
	size_t name_len;
	size_t value_len;
	size_t overhead_len;
	size_t total_len;
	char *sql;

	if (stmt == NULL || stmt->name == NULL || value_sql == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"variable SET statement builder requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	name_len = strlen(stmt->name);
	value_len = strlen(value_sql);
	overhead_len = (sizeof(prefix) - 1U) + (sizeof(separator) - 1U);
	if (name_len > ((size_t)-1) - overhead_len) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	total_len = overhead_len + name_len;
	if (total_len >= (size_t)-1 || value_len > ((size_t)-1) - total_len - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	total_len += value_len;
	sql = (char *)malloc(total_len + 1U);
	if (sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	memcpy(sql, prefix, sizeof(prefix) - 1U);
	memcpy(sql + sizeof(prefix) - 1U, stmt->name, name_len);
	memcpy(sql + sizeof(prefix) - 1U + name_len, separator, sizeof(separator) - 1U);
	memcpy(sql + sizeof(prefix) - 1U + name_len + sizeof(separator) - 1U, value_sql, value_len);
	sql[total_len] = '\0';
	*out_sql = sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_extract_after_prefix(
	const char *sql,
	const char *prefix,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t sql_len;
	size_t prefix_len;

	if (sql == NULL || prefix == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL prefix extractor requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	sql_len = strlen(sql);
	prefix_len = strlen(prefix);
	if (sql_len < prefix_len || strncmp(sql, prefix, prefix_len) != 0) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"variable SET public SQL format is not recognized");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_sql = sqlparser_strndup(sql + prefix_len, sql_len - prefix_len);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postprocess_variable_set_arg_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt,
	size_t arg_index,
	const char *core_sql,
	const char *field_name,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	char *statement_sql;
	char *public_statement_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (!sqlparser_variable_set_arg_needs_statement_postprocess(handle, stmt, arg_index)) {
		return sqlparser_postprocess_handle_sql_fragment(handle, core_sql, field_name, out_sql, out_error);
	}

	statement_sql = NULL;
	public_statement_sql = NULL;
	status = sqlparser_build_variable_set_statement_sql(stmt, core_sql, &statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_postprocess_handle_sql_fragment(
			handle,
			statement_sql,
			field_name,
			&public_statement_sql,
			out_error);
	}
	free(statement_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (handle->dialect == SQLPARSER_DIALECT_ORACLE) {
		prefix = sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) ?
			"ALTER SESSION SET CURRENT_SCHEMA = " :
			"ALTER SESSION SET CONTAINER = ";
	} else {
		prefix = "USE ";
	}
	status = sqlparser_extract_after_prefix(public_statement_sql, prefix, out_sql, out_error);
	free(public_statement_sql);
	return status;
}

static int sqlparser_keywords_add(json_t *keywords, const char *keyword)
{
	if (!json_is_array(keywords) || keyword == NULL || keyword[0] == '\0') {
		return 0;
	}
	if (sqlparser_json_array_contains_string(keywords, keyword)) {
		return 0;
	}
	return json_array_append_new(keywords, json_string(keyword));
}

static void sqlparser_keywords_from_select(
	const sqlparser_view_build_t *build,
	const PgQuery__SelectStmt *stmt,
	json_t *keywords);
static void sqlparser_keywords_from_expr(const PgQuery__Node *node, json_t *keywords);

static const char *sqlparser_select_set_operator_keyword(
	const sqlparser_view_build_t *build,
	const PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL) {
		return NULL;
	}
	switch (stmt->op) {
		case PG_QUERY__SET_OPERATION__SETOP_UNION:
			return "union";
		case PG_QUERY__SET_OPERATION__SETOP_INTERSECT:
			return "intersect";
		case PG_QUERY__SET_OPERATION__SETOP_EXCEPT:
			return build != NULL &&
				build->handle != NULL &&
				build->handle->dialect == SQLPARSER_DIALECT_ORACLE ?
				"minus" :
				"except";
		default:
			return NULL;
	}
}

static void sqlparser_keywords_from_expr_array(PgQuery__Node *const *items, size_t item_count, json_t *keywords)
{
	size_t index;

	for (index = 0U; index < item_count; index++) {
		sqlparser_keywords_from_expr(items[index], keywords);
	}
}

static void sqlparser_keywords_from_bool_expr(const PgQuery__BoolExpr *expr, json_t *keywords)
{
	if (expr == NULL) {
		return;
	}
	switch (expr->boolop) {
		case PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR:
			(void)sqlparser_keywords_add(keywords, "and");
			break;
		case PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR:
			(void)sqlparser_keywords_add(keywords, "or");
			break;
		case PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR:
			(void)sqlparser_keywords_add(keywords, "not");
			break;
		default:
			break;
	}
	sqlparser_keywords_from_expr_array(expr->args, expr->n_args, keywords);
}

static void sqlparser_keywords_from_a_expr(const PgQuery__AExpr *expr, json_t *keywords)
{
	if (expr == NULL) {
		return;
	}
	switch (expr->kind) {
		case PG_QUERY__A__EXPR__KIND__AEXPR_IN:
			(void)sqlparser_keywords_add(keywords, "in");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_LIKE:
			(void)sqlparser_keywords_add(keywords, "like");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE:
			(void)sqlparser_keywords_add(keywords, "ilike");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR:
			(void)sqlparser_keywords_add(keywords, "similar");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN_SYM:
			(void)sqlparser_keywords_add(keywords, "between");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN_SYM:
			(void)sqlparser_keywords_add(keywords, "not");
			(void)sqlparser_keywords_add(keywords, "between");
			break;
		default:
			break;
	}
	sqlparser_keywords_from_expr(expr->lexpr, keywords);
	sqlparser_keywords_from_expr(expr->rexpr, keywords);
}

static void sqlparser_keywords_from_expr(const PgQuery__Node *node, json_t *keywords)
{
	if (node == NULL) {
		return;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_RES_TARGET:
			if (node->res_target != NULL) {
				if (node->res_target->name != NULL && node->res_target->name[0] != '\0') {
					(void)sqlparser_keywords_add(keywords, "as");
				}
				sqlparser_keywords_from_expr(node->res_target->val, keywords);
			}
			break;
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			sqlparser_keywords_from_bool_expr(node->bool_expr, keywords);
			break;
		case PG_QUERY__NODE__NODE_A_EXPR:
			sqlparser_keywords_from_a_expr(node->a_expr, keywords);
			break;
		case PG_QUERY__NODE__NODE_LIST:
			if (node->list != NULL) {
				sqlparser_keywords_from_expr_array(node->list->items, node->list->n_items, keywords);
			}
			break;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call != NULL) {
				sqlparser_keywords_from_expr_array(node->func_call->args, node->func_call->n_args, keywords);
				if (node->func_call->over != NULL) {
					(void)sqlparser_keywords_add(keywords, "over");
					if (node->func_call->over->n_partition_clause > 0U) {
						(void)sqlparser_keywords_add(keywords, "partition");
						(void)sqlparser_keywords_add(keywords, "by");
					}
					if (node->func_call->over->n_order_clause > 0U) {
						(void)sqlparser_keywords_add(keywords, "order");
						(void)sqlparser_keywords_add(keywords, "by");
					}
					sqlparser_keywords_from_expr_array(
						node->func_call->over->partition_clause,
						node->func_call->over->n_partition_clause,
						keywords);
					sqlparser_keywords_from_expr_array(
						node->func_call->over->order_clause,
						node->func_call->over->n_order_clause,
						keywords);
					sqlparser_keywords_from_expr(node->func_call->over->start_offset, keywords);
					sqlparser_keywords_from_expr(node->func_call->over->end_offset, keywords);
				}
			}
			break;
		case PG_QUERY__NODE__NODE_WINDOW_DEF:
			if (node->window_def != NULL) {
				if (node->window_def->n_partition_clause > 0U) {
					(void)sqlparser_keywords_add(keywords, "partition");
					(void)sqlparser_keywords_add(keywords, "by");
				}
				if (node->window_def->n_order_clause > 0U) {
					(void)sqlparser_keywords_add(keywords, "order");
					(void)sqlparser_keywords_add(keywords, "by");
				}
				sqlparser_keywords_from_expr_array(
					node->window_def->partition_clause,
					node->window_def->n_partition_clause,
					keywords);
				sqlparser_keywords_from_expr_array(
					node->window_def->order_clause,
					node->window_def->n_order_clause,
					keywords);
				sqlparser_keywords_from_expr(node->window_def->start_offset, keywords);
				sqlparser_keywords_from_expr(node->window_def->end_offset, keywords);
			}
			break;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr != NULL) {
				(void)sqlparser_keywords_add(keywords, "case");
				sqlparser_keywords_from_expr(node->case_expr->arg, keywords);
				sqlparser_keywords_from_expr_array(node->case_expr->args, node->case_expr->n_args, keywords);
				if (node->case_expr->defresult != NULL) {
					(void)sqlparser_keywords_add(keywords, "else");
					sqlparser_keywords_from_expr(node->case_expr->defresult, keywords);
				}
				(void)sqlparser_keywords_add(keywords, "end");
			}
			break;
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when != NULL) {
				(void)sqlparser_keywords_add(keywords, "when");
				sqlparser_keywords_from_expr(node->case_when->expr, keywords);
				(void)sqlparser_keywords_add(keywords, "then");
				sqlparser_keywords_from_expr(node->case_when->result, keywords);
			}
			break;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			(void)sqlparser_keywords_add(keywords, "is");
			(void)sqlparser_keywords_add(keywords, "null");
			if (node->null_test != NULL) {
				sqlparser_keywords_from_expr(node->null_test->arg, keywords);
			}
			break;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			(void)sqlparser_keywords_add(keywords, "is");
			if (node->boolean_test != NULL) {
				sqlparser_keywords_from_expr(node->boolean_test->arg, keywords);
			}
			break;
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link != NULL) {
				if (node->sub_link->sub_link_type == PG_QUERY__SUB_LINK_TYPE__EXISTS_SUBLINK) {
					(void)sqlparser_keywords_add(keywords, "exists");
				}
				sqlparser_keywords_from_expr(node->sub_link->testexpr, keywords);
				if (node->sub_link->subselect != NULL &&
				    node->sub_link->subselect->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
					sqlparser_keywords_from_select(NULL, node->sub_link->subselect->select_stmt, keywords);
				}
			}
			break;
		default:
			break;
	}
}

static void sqlparser_keywords_from_from_item(const PgQuery__Node *node, json_t *keywords)
{
	if (node == NULL) {
		return;
	}
	if (node->node_case != PG_QUERY__NODE__NODE_JOIN_EXPR || node->join_expr == NULL) {
		return;
	}
	(void)sqlparser_keywords_add(keywords, "join");
	if (node->join_expr->quals != NULL) {
		(void)sqlparser_keywords_add(keywords, "on");
		sqlparser_keywords_from_expr(node->join_expr->quals, keywords);
	}
	if (node->join_expr->n_using_clause > 0U) {
		(void)sqlparser_keywords_add(keywords, "using");
	}
	sqlparser_keywords_from_from_item(node->join_expr->larg, keywords);
	sqlparser_keywords_from_from_item(node->join_expr->rarg, keywords);
}

static void sqlparser_keywords_from_from_clause(const PgQuery__SelectStmt *stmt, json_t *keywords)
{
	size_t index;

	if (stmt == NULL) {
		return;
	}
	for (index = 0U; index < stmt->n_from_clause; index++) {
		sqlparser_keywords_from_from_item(stmt->from_clause[index], keywords);
	}
}

static void sqlparser_keywords_from_select(
	const sqlparser_view_build_t *build,
	const PgQuery__SelectStmt *stmt,
	json_t *keywords)
{
	const char *set_keyword;

	if (stmt == NULL) {
		return;
	}
	if (stmt->with_clause != NULL) {
		(void)sqlparser_keywords_add(keywords, "with");
	}
	(void)sqlparser_keywords_add(keywords, "select");
	sqlparser_keywords_from_expr_array(stmt->target_list, stmt->n_target_list, keywords);
	if (stmt->n_from_clause > 0U) {
		(void)sqlparser_keywords_add(keywords, "from");
		sqlparser_keywords_from_from_clause(stmt, keywords);
	}
	if (stmt->where_clause != NULL) {
		(void)sqlparser_keywords_add(keywords, "where");
		sqlparser_keywords_from_expr(stmt->where_clause, keywords);
	}
	if (stmt->n_group_clause > 0U) {
		(void)sqlparser_keywords_add(keywords, "group");
		(void)sqlparser_keywords_add(keywords, "by");
	}
	if (stmt->having_clause != NULL) {
		(void)sqlparser_keywords_add(keywords, "having");
		sqlparser_keywords_from_expr(stmt->having_clause, keywords);
	}
	if (stmt->n_sort_clause > 0U) {
		(void)sqlparser_keywords_add(keywords, "order");
		(void)sqlparser_keywords_add(keywords, "by");
	}
	if (stmt->limit_offset != NULL || stmt->limit_count != NULL) {
		if (build != NULL &&
		    build->handle != NULL &&
		    build->handle->dialect == SQLPARSER_DIALECT_SQLSERVER) {
			if (stmt->limit_offset != NULL) {
				(void)sqlparser_keywords_add(keywords, "offset");
			}
			if (stmt->limit_count != NULL) {
				(void)sqlparser_keywords_add(keywords, stmt->limit_offset != NULL ? "fetch" : "top");
			}
		} else {
			(void)sqlparser_keywords_add(keywords, "limit");
		}
	}
	set_keyword = sqlparser_select_set_operator_keyword(build, stmt);
	if (set_keyword != NULL) {
		(void)sqlparser_keywords_add(keywords, set_keyword);
		if (stmt->all) {
			(void)sqlparser_keywords_add(keywords, "all");
		}
	}
	if (stmt->larg != NULL) {
		sqlparser_keywords_from_select(build, stmt->larg, keywords);
	}
	if (stmt->rarg != NULL) {
		sqlparser_keywords_from_select(build, stmt->rarg, keywords);
	}
}

static void sqlparser_keywords_from_on_conflict(const PgQuery__OnConflictClause *clause, json_t *keywords)
{
	if (clause == NULL) {
		return;
	}
	(void)sqlparser_keywords_add(keywords, "on");
	(void)sqlparser_keywords_add(keywords, "conflict");
	(void)sqlparser_keywords_add(keywords, "do");
	switch (clause->action) {
		case PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE:
			(void)sqlparser_keywords_add(keywords, "update");
			(void)sqlparser_keywords_add(keywords, "set");
			break;
		case PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_NOTHING:
			(void)sqlparser_keywords_add(keywords, "nothing");
			break;
		default:
			break;
	}
}

static void sqlparser_keywords_from_merge_when_clause(const PgQuery__MergeWhenClause *clause, json_t *keywords)
{
	if (clause == NULL) {
		return;
	}
	(void)sqlparser_keywords_add(keywords, "when");
	if (clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE ||
	    clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_TARGET) {
		(void)sqlparser_keywords_add(keywords, "not");
	}
	(void)sqlparser_keywords_add(keywords, "matched");
	if (clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE ||
	    clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_TARGET) {
		(void)sqlparser_keywords_add(keywords, "by");
		(void)sqlparser_keywords_add(
			keywords,
			clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE ?
				"source" :
				"target");
	}
	(void)sqlparser_keywords_add(keywords, "then");
	switch (clause->command_type) {
		case PG_QUERY__CMD_TYPE__CMD_UPDATE:
			(void)sqlparser_keywords_add(keywords, "update");
			(void)sqlparser_keywords_add(keywords, "set");
			break;
		case PG_QUERY__CMD_TYPE__CMD_INSERT:
			(void)sqlparser_keywords_add(keywords, "insert");
			(void)sqlparser_keywords_add(keywords, "values");
			break;
		case PG_QUERY__CMD_TYPE__CMD_DELETE:
			(void)sqlparser_keywords_add(keywords, "delete");
			break;
		case PG_QUERY__CMD_TYPE__CMD_NOTHING:
			(void)sqlparser_keywords_add(keywords, "do");
			(void)sqlparser_keywords_add(keywords, "nothing");
			break;
		default:
			break;
	}
	sqlparser_keywords_from_expr(clause->condition, keywords);
}

static void sqlparser_keywords_from_merge(const PgQuery__MergeStmt *stmt, json_t *keywords)
{
	size_t index;

	if (stmt == NULL) {
		return;
	}
	(void)sqlparser_keywords_add(keywords, "into");
	if (stmt->source_relation != NULL) {
		(void)sqlparser_keywords_add(keywords, "using");
	}
	if (stmt->join_condition != NULL) {
		(void)sqlparser_keywords_add(keywords, "on");
		sqlparser_keywords_from_expr(stmt->join_condition, keywords);
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *node;

		node = stmt->merge_when_clauses[index];
		if (node != NULL && node->node_case == PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE) {
			sqlparser_keywords_from_merge_when_clause(node->merge_when_clause, keywords);
		}
	}
	if (stmt->n_returning_list > 0U) {
		(void)sqlparser_keywords_add(keywords, "returning");
	}
}

static const char *sqlparser_grant_object_keyword(PgQuery__ObjectType object_type)
{
	switch (object_type) {
		case PG_QUERY__OBJECT_TYPE__OBJECT_TABLE:
			return "table";
		case PG_QUERY__OBJECT_TYPE__OBJECT_VIEW:
			return "view";
		case PG_QUERY__OBJECT_TYPE__OBJECT_SCHEMA:
			return "schema";
		case PG_QUERY__OBJECT_TYPE__OBJECT_DATABASE:
			return "database";
		case PG_QUERY__OBJECT_TYPE__OBJECT_SEQUENCE:
			return "sequence";
		case PG_QUERY__OBJECT_TYPE__OBJECT_FUNCTION:
			return "function";
		case PG_QUERY__OBJECT_TYPE__OBJECT_PROCEDURE:
			return "procedure";
		case PG_QUERY__OBJECT_TYPE__OBJECT_ROLE:
			return "role";
		case PG_QUERY__OBJECT_TYPE__OBJECT_INDEX:
			return "index";
		case PG_QUERY__OBJECT_TYPE__OBJECT_TYPE:
			return "type";
		default:
			return NULL;
	}
}

static void sqlparser_keywords_from_grant(const PgQuery__GrantStmt *stmt, json_t *keywords)
{
	size_t index;
	const char *object_keyword;

	if (stmt == NULL) {
		return;
	}
	for (index = 0U; index < stmt->n_privileges; index++) {
		PgQuery__Node *node;

		node = stmt->privileges[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_ACCESS_PRIV &&
		    node->access_priv != NULL) {
			(void)sqlparser_keywords_add(keywords, node->access_priv->priv_name);
		}
	}
	(void)sqlparser_keywords_add(keywords, "on");
	object_keyword = sqlparser_grant_object_keyword(stmt->objtype);
	if (object_keyword != NULL) {
		(void)sqlparser_keywords_add(keywords, object_keyword);
	}
	(void)sqlparser_keywords_add(keywords, stmt->is_grant ? "to" : "from");
}

static void sqlparser_keywords_from_alter_table_cmd(const PgQuery__AlterTableCmd *cmd, json_t *keywords)
{
	if (cmd == NULL) {
		return;
	}
	switch (cmd->subtype) {
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AddColumn:
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AddColumnToView:
			(void)sqlparser_keywords_add(keywords, "add");
			(void)sqlparser_keywords_add(keywords, "column");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_DropColumn:
			(void)sqlparser_keywords_add(keywords, "drop");
			(void)sqlparser_keywords_add(keywords, "column");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AddConstraint:
		case PG_QUERY__ALTER_TABLE_TYPE__AT_ReAddConstraint:
			(void)sqlparser_keywords_add(keywords, "add");
			(void)sqlparser_keywords_add(keywords, "constraint");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_DropConstraint:
			(void)sqlparser_keywords_add(keywords, "drop");
			(void)sqlparser_keywords_add(keywords, "constraint");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AlterColumnType:
			(void)sqlparser_keywords_add(keywords, "alter");
			(void)sqlparser_keywords_add(keywords, "column");
			(void)sqlparser_keywords_add(keywords, "type");
			break;
		default:
			break;
	}
}

static void sqlparser_keywords_from_alter_table(const PgQuery__AlterTableStmt *stmt, json_t *keywords)
{
	size_t index;

	if (stmt == NULL) {
		return;
	}
	for (index = 0U; index < stmt->n_cmds; index++) {
		PgQuery__Node *node;

		node = stmt->cmds[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_ALTER_TABLE_CMD &&
		    node->alter_table_cmd != NULL) {
			sqlparser_keywords_from_alter_table_cmd(node->alter_table_cmd, keywords);
		}
	}
}

static void sqlparser_keywords_from_vacuum(const PgQuery__VacuumStmt *stmt, json_t *keywords)
{
	size_t index;

	if (stmt == NULL) {
		return;
	}
	for (index = 0U; index < stmt->n_options; index++) {
		PgQuery__Node *node;

		node = stmt->options[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_DEF_ELEM &&
		    node->def_elem != NULL) {
			(void)sqlparser_keywords_add(keywords, node->def_elem->defname);
		}
	}
}

static void sqlparser_keywords_from_node(
	const sqlparser_view_build_t *build,
	const PgQuery__Node *statement,
	json_t *keywords)
{
	const char *main_keyword;

	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		sqlparser_keywords_from_select(build, statement->select_stmt, keywords);
		return;
	}
	main_keyword = sqlparser_statement_keyword_for_handle(build != NULL ? build->handle : NULL, statement);
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT) {
		if (strcmp(main_keyword, "alter_session") == 0) {
			(void)sqlparser_keywords_add(keywords, "alter");
			(void)sqlparser_keywords_add(keywords, "session");
			(void)sqlparser_keywords_add(keywords, "set");
		} else {
			(void)sqlparser_keywords_add(keywords, main_keyword);
		}
		return;
	}
	if (strcmp(main_keyword, "create_view") == 0) {
		(void)sqlparser_keywords_add(keywords, "create");
		(void)sqlparser_keywords_add(keywords, "view");
		if (statement != NULL &&
		    statement->view_stmt != NULL &&
		    statement->view_stmt->query != NULL &&
		    statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
			(void)sqlparser_keywords_add(keywords, "as");
			sqlparser_keywords_from_select(build, statement->view_stmt->query->select_stmt, keywords);
		}
		return;
	}
	if (strcmp(main_keyword, "create_table") == 0) {
		(void)sqlparser_keywords_add(keywords, "create");
		(void)sqlparser_keywords_add(keywords, "table");
		return;
	}
	if (strcmp(main_keyword, "create_table_as") == 0 ||
	    strcmp(main_keyword, "create_materialized_view") == 0) {
		(void)sqlparser_keywords_add(keywords, "create");
		if (strcmp(main_keyword, "create_materialized_view") == 0) {
			(void)sqlparser_keywords_add(keywords, "materialized");
			(void)sqlparser_keywords_add(keywords, "view");
		} else {
			(void)sqlparser_keywords_add(keywords, "table");
		}
		(void)sqlparser_keywords_add(keywords, "as");
		if (statement != NULL &&
		    statement->create_table_as_stmt != NULL &&
		    statement->create_table_as_stmt->query != NULL &&
		    statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
			sqlparser_keywords_from_select(build, statement->create_table_as_stmt->query->select_stmt, keywords);
		}
		return;
	}
	if (strcmp(main_keyword, "create_schema") == 0) {
		(void)sqlparser_keywords_add(keywords, "create");
		(void)sqlparser_keywords_add(keywords, "schema");
		return;
	}
	if (strcmp(main_keyword, "create_index") == 0) {
		(void)sqlparser_keywords_add(keywords, "create");
		(void)sqlparser_keywords_add(keywords, "index");
		if (statement != NULL && statement->index_stmt != NULL && statement->index_stmt->relation != NULL) {
			(void)sqlparser_keywords_add(keywords, "on");
		}
		return;
	}
	if (strcmp(main_keyword, "alter_table") == 0) {
		(void)sqlparser_keywords_add(keywords, "alter");
		(void)sqlparser_keywords_add(keywords, "table");
		if (statement != NULL) {
			sqlparser_keywords_from_alter_table(statement->alter_table_stmt, keywords);
		}
		return;
	}
	if (strncmp(main_keyword, "drop_", 5) == 0) {
		(void)sqlparser_keywords_add(keywords, "drop");
		(void)sqlparser_keywords_add(keywords, main_keyword + 5);
		return;
	}
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_RENAME_STMT) {
		(void)sqlparser_keywords_add(keywords, "rename");
		if (build != NULL &&
		    build->handle != NULL &&
		    build->handle->dialect == SQLPARSER_DIALECT_SQLSERVER) {
			(void)sqlparser_keywords_add(keywords, "object");
		}
		(void)sqlparser_keywords_add(keywords, "to");
		return;
	}
	(void)sqlparser_keywords_add(keywords, main_keyword);
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_INSERT_STMT) {
		if (statement->insert_stmt != NULL && statement->insert_stmt->with_clause != NULL) {
			(void)sqlparser_keywords_add(keywords, "with");
		}
		(void)sqlparser_keywords_add(keywords, "into");
		if (statement->insert_stmt != NULL &&
		    sqlparser_insert_source_from_stmt(statement->insert_stmt) == SQLPARSER_INSERT_SOURCE_VALUES) {
			(void)sqlparser_keywords_add(keywords, "values");
		} else {
			if (statement->insert_stmt != NULL &&
			    statement->insert_stmt->select_stmt != NULL &&
			    statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
				sqlparser_keywords_from_select(build, statement->insert_stmt->select_stmt->select_stmt, keywords);
			} else {
				(void)sqlparser_keywords_add(keywords, "select");
			}
		}
		if (statement->insert_stmt != NULL) {
			sqlparser_keywords_from_on_conflict(statement->insert_stmt->on_conflict_clause, keywords);
			if (statement->insert_stmt->n_returning_list > 0U) {
				(void)sqlparser_keywords_add(keywords, "returning");
			}
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT) {
		if (statement->update_stmt != NULL && statement->update_stmt->with_clause != NULL) {
			(void)sqlparser_keywords_add(keywords, "with");
		}
		(void)sqlparser_keywords_add(keywords, "set");
		if (statement->update_stmt != NULL && statement->update_stmt->n_from_clause > 0U) {
			(void)sqlparser_keywords_add(keywords, "from");
		}
		if (statement->update_stmt != NULL && statement->update_stmt->where_clause != NULL) {
			(void)sqlparser_keywords_add(keywords, "where");
			sqlparser_keywords_from_expr(statement->update_stmt->where_clause, keywords);
		}
		if (statement->update_stmt != NULL && statement->update_stmt->n_returning_list > 0U) {
			(void)sqlparser_keywords_add(keywords, "returning");
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_DELETE_STMT) {
		if (statement->delete_stmt != NULL && statement->delete_stmt->with_clause != NULL) {
			(void)sqlparser_keywords_add(keywords, "with");
		}
		(void)sqlparser_keywords_add(keywords, "from");
		if (statement->delete_stmt != NULL && statement->delete_stmt->n_using_clause > 0U) {
			(void)sqlparser_keywords_add(keywords, "using");
		}
		if (statement->delete_stmt != NULL && statement->delete_stmt->where_clause != NULL) {
			(void)sqlparser_keywords_add(keywords, "where");
			sqlparser_keywords_from_expr(statement->delete_stmt->where_clause, keywords);
		}
		if (statement->delete_stmt != NULL && statement->delete_stmt->n_returning_list > 0U) {
			(void)sqlparser_keywords_add(keywords, "returning");
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_MERGE_STMT) {
		sqlparser_keywords_from_merge(statement->merge_stmt, keywords);
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_GRANT_STMT) {
		sqlparser_keywords_from_grant(statement->grant_stmt, keywords);
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_VACUUM_STMT) {
		sqlparser_keywords_from_vacuum(statement->vacuum_stmt, keywords);
	}
}

static json_t *sqlparser_view_object_new(
	const sqlparser_relation_view_t *relation,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = json_object();
	if (object == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	if (sqlparser_json_set_string_or_null(object, "database", relation != NULL ? relation->database_name : NULL) != 0 ||
	    sqlparser_json_set_string_or_null(object, "schema", relation != NULL ? relation->schema_name : NULL) != 0 ||
	    sqlparser_json_set_string_or_null(object, "table", relation != NULL ? relation->table_name : NULL) != 0 ||
	    sqlparser_json_set_string_or_null(object, "alias", relation != NULL ? relation->alias_name : NULL) != 0 ||
	    sqlparser_json_set_selector_or_null(object, "selector", selector, out_error) != 0 ||
	    json_object_set_new(object, "columns", json_array()) != 0 ||
	    json_object_set_new(object, "rows", json_array()) != 0) {
		json_decref(object);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	return object;
}

static int sqlparser_view_object_matches(
	json_t *object,
	const char *database_name,
	const char *schema_name,
	const char *table_name,
	const char *alias_name)
{
	const char *object_database;
	const char *object_schema;
	const char *object_table;
	const char *object_alias;

	if (!json_is_object(object)) {
		return 0;
	}
	object_database = json_string_value(json_object_get(object, "database"));
	object_schema = json_string_value(json_object_get(object, "schema"));
	object_table = json_string_value(json_object_get(object, "table"));
	object_alias = json_string_value(json_object_get(object, "alias"));

	if (alias_name != NULL && alias_name[0] != '\0') {
		return sqlparser_text_equal(object_alias, alias_name);
	}
	return sqlparser_text_equal(object_database, database_name) &&
		sqlparser_text_equal(object_schema, schema_name) &&
		sqlparser_text_equal(object_table, table_name);
}

static json_t *sqlparser_view_find_object(
	json_t *objects,
	const char *database_name,
	const char *schema_name,
	const char *table_name,
	const char *alias_name)
{
	size_t index;
	json_t *object;

	if (!json_is_array(objects)) {
		return NULL;
	}
	json_array_foreach(objects, index, object)
	{
		if (sqlparser_view_object_matches(object, database_name, schema_name, table_name, alias_name)) {
			return object;
		}
	}
	return NULL;
}

static json_t *sqlparser_view_add_relation_object(
	sqlparser_view_build_t *build,
	const sqlparser_relation_view_t *relation,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = sqlparser_view_find_object(
		build->objects,
		relation != NULL ? relation->database_name : NULL,
		relation != NULL ? relation->schema_name : NULL,
		relation != NULL ? relation->table_name : NULL,
		relation != NULL ? relation->alias_name : NULL);
	if (object != NULL) {
		return object;
	}

	object = sqlparser_view_object_new(relation, selector, out_error);
	if (object == NULL) {
		return NULL;
	}
	if (json_array_append_new(build->objects, object) != 0) {
		json_decref(object);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	return object;
}

static json_t *sqlparser_view_object_columns(json_t *object)
{
	return json_object_get(object, "columns");
}

static json_t *sqlparser_view_object_rows(json_t *object)
{
	return json_object_get(object, "rows");
}

static char **sqlparser_column_ref_name_slot(PgQuery__ColumnRef *column_ref)
{
	PgQuery__Node *field;

	if (column_ref == NULL || column_ref->n_fields == 0U) {
		return NULL;
	}
	field = column_ref->fields[column_ref->n_fields - 1U];
	if (field == NULL || field->node_case != PG_QUERY__NODE__NODE_STRING || field->string == NULL) {
		return NULL;
	}
	return &field->string->sval;
}

static const char *sqlparser_column_ref_part(PgQuery__ColumnRef *column_ref, size_t reverse_index)
{
	size_t text_seen;
	size_t index;

	if (column_ref == NULL) {
		return NULL;
	}
	text_seen = 0U;
	for (index = column_ref->n_fields; index > 0U; index--) {
		const char *text;

		text = NULL;
		if (sqlparser_node_string_value(column_ref->fields[index - 1U], &text)) {
			if (text_seen == reverse_index) {
				return text;
			}
			text_seen++;
		} else if (column_ref->fields[index - 1U] != NULL &&
			   column_ref->fields[index - 1U]->node_case == PG_QUERY__NODE__NODE_A_STAR) {
			if (text_seen == reverse_index) {
				return "*";
			}
			text_seen++;
		}
	}
	return NULL;
}

static size_t sqlparser_view_find_name_selector_index(
	sqlparser_view_build_t *build,
	char **slot)
{
	size_t index;
	sqlparser_error_t error;

	memset(&error, 0, sizeof(error));
	if (slot == NULL ||
	    sqlparser_find_statement_name_index_by_slot(
		    build->handle,
		    build->statement_index,
		    slot,
		    &index,
		    &error) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	return index;
}

static size_t sqlparser_view_find_literal_index(
	sqlparser_view_build_t *build,
	PgQuery__AConst *literal)
{
	size_t count;
	size_t index;
	ProtobufCMessage *message;
	sqlparser_error_t error;

	if (literal == NULL) {
		return (size_t)-1;
	}
	memset(&error, 0, sizeof(error));
	if (sqlparser_search_statement_messages(
		    build->handle,
		    build->statement_index,
		    &pg_query__a__const__descriptor,
		    NULL,
		    0,
		    0U,
		    &count,
		    NULL,
		    &error) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	for (index = 0U; index < count; index++) {
		message = NULL;
		if (sqlparser_search_statement_messages(
			    build->handle,
			    build->statement_index,
			    &pg_query__a__const__descriptor,
			    NULL,
			    1,
			    index,
			    NULL,
			    &message,
			    &error) != SQLPARSER_STATUS_OK) {
			return (size_t)-1;
		}
		if ((PgQuery__AConst *)message == literal) {
			return index;
		}
	}
	return (size_t)-1;
}

static size_t sqlparser_view_find_value_index(
	sqlparser_view_build_t *build,
	PgQuery__Node *value_node)
{
	size_t index;
	sqlparser_error_t error;

	memset(&error, 0, sizeof(error));
	if (value_node == NULL ||
	    sqlparser_find_statement_node_index_by_node(
		    build->handle,
		    build->statement_index,
		    value_node,
		    &index,
		    &error) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	return index;
}

static int sqlparser_view_set_column_value(
	json_t *column,
	sqlparser_view_build_t *build,
	PgQuery__Node *value_node,
	const sqlparser_selector_t *forced_selector,
	sqlparser_error_t *out_error)
{
	json_t *value;
	sqlparser_selector_t selector;
	char *core_sql;
	char *public_sql;
	sqlparser_status_t status;
	size_t literal_index;
	size_t value_index;
	sqlparser_error_t render_error;

	if (!json_is_object(column) || value_node == NULL) {
		return 0;
	}

	core_sql = NULL;
	public_sql = NULL;
	literal_index = (size_t)-1;
	value_index = sqlparser_view_find_value_index(build, value_node);
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	if (value_node->node_case == PG_QUERY__NODE__NODE_A_CONST && value_node->a_const != NULL) {
		literal_index = sqlparser_view_find_literal_index(build, value_node->a_const);
	}
	if (forced_selector != NULL && forced_selector->kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		selector = *forced_selector;
	} else if (value_index != (size_t)-1) {
		selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
		selector.statement_index = build->statement_index;
		selector.item_index = value_index;
	} else if (literal_index != (size_t)-1) {
		selector.kind = SQLPARSER_SELECTOR_KIND_LITERAL;
		selector.statement_index = build->statement_index;
		selector.item_index = literal_index;
	}

	memset(&render_error, 0, sizeof(render_error));
	status = sqlparser_render_update_assignment_node_sql(value_node, &core_sql, &render_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (status == SQLPARSER_STATUS_NO_MEMORY || status == SQLPARSER_STATUS_RESOURCE_LIMIT) {
			sqlparser_error_set_message(out_error, status, render_error.message);
			return -1;
		}
		return 0;
	}
	if (literal_index != (size_t)-1 &&
	    build->handle->dialect_ops != NULL &&
	    build->handle->dialect_ops->postprocess_literal_fragment != NULL) {
		status = build->handle->dialect_ops->postprocess_literal_fragment(
			core_sql,
			build->handle->dialect_state,
			literal_index,
			&public_sql,
			out_error);
	} else {
		status = sqlparser_postprocess_handle_sql_fragment(
			build->handle,
			core_sql,
			"SQL view value",
			&public_sql,
			out_error);
	}
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}

	value = json_object();
	if (value == NULL ||
	    json_object_set_new(value, "sql", json_string(public_sql)) != 0) {
		free(public_sql);
		json_decref(value);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	free(public_sql);
	if (sqlparser_json_set_selector_or_null(value, "selector", &selector, out_error) != 0) {
		json_decref(value);
		return -1;
	}

	if (json_object_set_new(column, "value", value) != 0) {
		json_decref(value);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	return 0;
}

static int sqlparser_view_append_column_to_object(
	sqlparser_view_build_t *build,
	json_t *object,
	const char *name,
	const char *keyword,
	const char *operator_name,
	PgQuery__ColumnRef *column_ref,
	PgQuery__Node *value_node,
	const sqlparser_selector_t *forced_selector,
	const sqlparser_selector_t *forced_value_selector,
	sqlparser_error_t *out_error)
{
	json_t *columns;
	json_t *column;
	sqlparser_selector_t selector;
	size_t name_index;

	if (!json_is_object(object) || name == NULL || name[0] == '\0') {
		return 0;
	}

	columns = sqlparser_view_object_columns(object);
	column = json_object();
	if (column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	if (json_object_set_new(column, "name", json_string(name)) != 0 ||
	    json_object_set_new(column, "keyword", json_string(keyword != NULL ? keyword : "")) != 0 ||
	    sqlparser_json_set_string_or_null(column, "operator", operator_name) != 0 ||
	    json_object_set_new(column, "value", json_null()) != 0) {
		json_decref(column);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	if (forced_selector != NULL && forced_selector->kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		selector = *forced_selector;
	} else if (column_ref != NULL) {
		name_index = sqlparser_view_find_name_selector_index(build, sqlparser_column_ref_name_slot(column_ref));
		if (name_index != (size_t)-1) {
			selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
			selector.statement_index = build->statement_index;
			selector.item_index = name_index;
		}
	}
	if (sqlparser_json_set_selector_or_null(column, "selector", &selector, out_error) != 0) {
		json_decref(column);
		return -1;
	}

	if (value_node != NULL &&
	    sqlparser_view_set_column_value(column, build, value_node, forced_value_selector, out_error) != 0) {
		json_decref(column);
		return -1;
	}

	if (json_array_append_new(columns, column) != 0) {
		json_decref(column);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	return 0;
}

static int sqlparser_view_set_variable_arg_value(
	json_t *column,
	sqlparser_view_build_t *build,
	PgQuery__VariableSetStmt *stmt,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	json_t *value;
	sqlparser_selector_t selector;
	char *core_sql;
	char *public_sql;
	sqlparser_status_t status;
	size_t value_index;
	size_t arg_index;

	if (!json_is_object(column) || build == NULL || stmt == NULL || value_node == NULL) {
		return 0;
	}

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	value_index = sqlparser_view_find_value_index(build, value_node);
	arg_index = sqlparser_variable_set_arg_index(stmt, value_node);
	if (value_index != (size_t)-1) {
		selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
		selector.statement_index = build->statement_index;
		selector.item_index = value_index;
	}

	core_sql = NULL;
	public_sql = NULL;
	status = sqlparser_render_variable_set_arg_node_sql(value_node, &core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	status = sqlparser_postprocess_variable_set_arg_sql(
		build->handle,
		stmt,
		arg_index,
		core_sql,
		"SQL view value",
		&public_sql,
		out_error);
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}

	value = json_object();
	if (value == NULL ||
	    json_object_set_new(value, "sql", json_string(public_sql)) != 0) {
		free(public_sql);
		json_decref(value);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	free(public_sql);
	if (sqlparser_json_set_selector_or_null(value, "selector", &selector, out_error) != 0) {
		json_decref(value);
		return -1;
	}
	if (json_object_set_new(column, "value", value) != 0) {
		json_decref(value);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	return 0;
}

static int sqlparser_view_append_variable_set_column(
	sqlparser_view_build_t *build,
	json_t *object,
	PgQuery__VariableSetStmt *stmt,
	const char *name,
	const char *keyword,
	const char *operator_name,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	json_t *columns;
	json_t *column;
	sqlparser_selector_t selector;

	if (!json_is_object(object) || name == NULL || name[0] == '\0') {
		return 0;
	}

	columns = sqlparser_view_object_columns(object);
	column = json_object();
	if (column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	if (json_object_set_new(column, "name", json_string(name)) != 0 ||
	    json_object_set_new(column, "keyword", json_string(keyword != NULL ? keyword : "")) != 0 ||
	    sqlparser_json_set_string_or_null(column, "operator", operator_name) != 0 ||
	    json_object_set_new(column, "value", json_null()) != 0) {
		json_decref(column);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	if (sqlparser_json_set_selector_or_null(column, "selector", &selector, out_error) != 0 ||
	    sqlparser_view_set_variable_arg_value(column, build, stmt, value_node, out_error) != 0) {
		json_decref(column);
		return -1;
	}

	if (json_array_append_new(columns, column) != 0) {
		json_decref(column);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	return 0;
}

static int sqlparser_view_append_variable_set_object(
	sqlparser_view_build_t *build,
	PgQuery__VariableSetStmt *stmt,
	sqlparser_error_t *out_error)
{
	json_t *object;
	sqlparser_relation_view_t relation;
	sqlparser_selector_t selector;
	const char *name;
	const char *keyword;
	const char *operator_name;
	size_t index;

	if (build == NULL || stmt == NULL || !sqlparser_variable_set_is_session_context(stmt)) {
		return 0;
	}

	memset(&relation, 0, sizeof(relation));
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	object = sqlparser_view_object_new(&relation, &selector, out_error);
	if (object == NULL) {
		return -1;
	}

	keyword = sqlparser_variable_set_column_keyword(build->handle, stmt);
	operator_name = sqlparser_variable_set_operator(build->handle, stmt);
	for (index = 0U; index < stmt->n_args; index++) {
		name = sqlparser_variable_set_public_name_at(build->handle, stmt, index);
		if (sqlparser_view_append_variable_set_column(
			    build,
			    object,
			    stmt,
			    name,
			    keyword,
			    operator_name,
			    stmt->args[index],
			    out_error) != 0) {
			json_decref(object);
			return -1;
		}
	}

	if (json_array_append_new(build->objects, object) != 0) {
		json_decref(object);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	return 0;
}

static int sqlparser_view_column_matches_object(
	json_t *object,
	const char *database_name,
	const char *schema_name,
	const char *table_name)
{
	const char *object_alias;
	const char *object_table;
	const char *object_schema;
	const char *object_database;

	if (!json_is_object(object)) {
		return 0;
	}
	object_alias = json_string_value(json_object_get(object, "alias"));
	object_table = json_string_value(json_object_get(object, "table"));
	object_schema = json_string_value(json_object_get(object, "schema"));
	object_database = json_string_value(json_object_get(object, "database"));
	if (table_name == NULL || table_name[0] == '\0') {
		return 1;
	}
	if (database_name != NULL && database_name[0] != '\0') {
		return sqlparser_text_equal(object_database, database_name) &&
			sqlparser_text_equal(object_schema, schema_name) &&
			sqlparser_text_equal(object_table, table_name);
	}
	if (schema_name != NULL && schema_name[0] != '\0') {
		return sqlparser_text_equal(object_schema, schema_name) &&
			sqlparser_text_equal(object_table, table_name);
	}
	return sqlparser_text_equal(object_alias, table_name) || sqlparser_text_equal(object_table, table_name);
}

static int sqlparser_view_append_column_ref(
	sqlparser_view_build_t *build,
	PgQuery__ColumnRef *column_ref,
	const char *keyword,
	const char *operator_name,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	const char *column_name;
	const char *table_name;
	const char *schema_name;
	const char *database_name;
	size_t index;
	json_t *object;
	int appended;

	if (column_ref == NULL) {
		return 0;
	}
	column_name = sqlparser_column_ref_part(column_ref, 0U);
	if (column_name == NULL) {
		return 0;
	}
	table_name = sqlparser_column_ref_part(column_ref, 1U);
	schema_name = sqlparser_column_ref_part(column_ref, 2U);
	database_name = sqlparser_column_ref_part(column_ref, 3U);
	appended = 0;

	json_array_foreach(build->objects, index, object)
	{
		if (!sqlparser_view_column_matches_object(object, database_name, schema_name, table_name)) {
			continue;
		}
		if (sqlparser_view_append_column_to_object(
			    build,
			    object,
			    column_name,
			    keyword,
			    operator_name,
			    column_ref,
			    value_node,
			    NULL,
			    NULL,
			    out_error) != 0) {
			return -1;
		}
		appended = 1;
	}

	if (!appended && json_array_size(build->objects) == 0U) {
		sqlparser_relation_view_t relation;
		sqlparser_selector_t selector;

		memset(&relation, 0, sizeof(relation));
		memset(&selector, 0, sizeof(selector));
		object = sqlparser_view_add_relation_object(build, &relation, &selector, out_error);
		if (object == NULL) {
			return -1;
		}
		return sqlparser_view_append_column_to_object(
			build,
			object,
			column_name,
			keyword,
			operator_name,
			column_ref,
			value_node,
			NULL,
			NULL,
			out_error);
	}

	return 0;
}

static int sqlparser_view_walk_expr(
	sqlparser_view_build_t *build,
	PgQuery__Node *node,
	const char *keyword,
	sqlparser_error_t *out_error);
static int sqlparser_view_process_select_stmt(
	sqlparser_view_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_error_t *out_error);

static int sqlparser_view_walk_node_array(
	sqlparser_view_build_t *build,
	PgQuery__Node **items,
	size_t count,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_view_walk_expr(build, items[index], keyword, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_view_walk_a_expr(
	sqlparser_view_build_t *build,
	PgQuery__AExpr *a_expr,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	const char *operator_name;
	const char *left_table;
	const char *left_column;
	const char *right_table;
	const char *right_column;

	if (a_expr == NULL) {
		return 0;
	}
	operator_name = sqlparser_a_expr_operator_name(a_expr);
	left_table = NULL;
	left_column = NULL;
	right_table = NULL;
	right_column = NULL;
	(void)left_table;
	(void)right_table;

	if (sqlparser_try_extract_column_ref(a_expr->lexpr, &left_table, &left_column) &&
	    left_column != NULL) {
		if (a_expr->lexpr != NULL && a_expr->lexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF) {
			if (sqlparser_view_append_column_ref(
				    build,
				    a_expr->lexpr->column_ref,
				    keyword,
				    operator_name,
				    a_expr->rexpr,
				    out_error) != 0) {
				return -1;
			}
		}
		if (sqlparser_view_walk_expr(build, a_expr->rexpr, keyword, out_error) != 0) {
			return -1;
		}
		return 0;
	}
	if (sqlparser_try_extract_column_ref(a_expr->rexpr, &right_table, &right_column) &&
	    right_column != NULL) {
		if (a_expr->rexpr != NULL && a_expr->rexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF) {
			if (sqlparser_view_append_column_ref(
				    build,
				    a_expr->rexpr->column_ref,
				    keyword,
				    operator_name,
				    a_expr->lexpr,
				    out_error) != 0) {
				return -1;
			}
		}
		if (sqlparser_view_walk_expr(build, a_expr->lexpr, keyword, out_error) != 0) {
			return -1;
		}
		return 0;
	}

	if (sqlparser_view_walk_expr(build, a_expr->lexpr, keyword, out_error) != 0 ||
	    sqlparser_view_walk_expr(build, a_expr->rexpr, keyword, out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_walk_expr(
	sqlparser_view_build_t *build,
	PgQuery__Node *node,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_COLUMN_REF:
			return sqlparser_view_append_column_ref(
				build,
				node->column_ref,
				keyword,
				NULL,
				NULL,
				out_error);
		case PG_QUERY__NODE__NODE_RES_TARGET:
			if (node->res_target != NULL) {
				return sqlparser_view_walk_expr(build, node->res_target->val, keyword, out_error);
			}
			return 0;
		case PG_QUERY__NODE__NODE_A_EXPR:
			return sqlparser_view_walk_a_expr(build, node->a_expr, keyword, out_error);
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			if (node->bool_expr != NULL) {
				return sqlparser_view_walk_node_array(
					build,
					node->bool_expr->args,
					node->bool_expr->n_args,
					keyword,
					out_error);
			}
			return 0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call != NULL) {
				if (sqlparser_view_walk_node_array(
					    build,
					    node->func_call->args,
					    node->func_call->n_args,
					    keyword,
					    out_error) != 0) {
					return -1;
				}
				if (node->func_call->over != NULL &&
				    (sqlparser_view_walk_node_array(
					     build,
					     node->func_call->over->partition_clause,
					     node->func_call->over->n_partition_clause,
					     keyword,
					     out_error) != 0 ||
				     sqlparser_view_walk_node_array(
					     build,
					     node->func_call->over->order_clause,
					     node->func_call->over->n_order_clause,
					     keyword,
					     out_error) != 0 ||
				     sqlparser_view_walk_expr(build, node->func_call->over->start_offset, keyword, out_error) != 0 ||
				     sqlparser_view_walk_expr(build, node->func_call->over->end_offset, keyword, out_error) != 0)) {
					return -1;
				}
				return 0;
			}
			return 0;
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_view_walk_expr(build, node->type_cast->arg, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_view_walk_expr(build, node->collate_clause->arg, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			return node->a_indirection != NULL ?
				sqlparser_view_walk_expr(build, node->a_indirection->arg, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_view_walk_node_array(
					build,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					keyword,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_view_walk_node_array(
					build,
					node->array_expr->elements,
					node->array_expr->n_elements,
					keyword,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_SORT_BY:
			return node->sort_by != NULL ?
				sqlparser_view_walk_expr(build, node->sort_by->node, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_WINDOW_DEF:
			if (node->window_def != NULL) {
				if (sqlparser_view_walk_node_array(
					    build,
					    node->window_def->partition_clause,
					    node->window_def->n_partition_clause,
					    keyword,
					    out_error) != 0 ||
				    sqlparser_view_walk_node_array(
					    build,
					    node->window_def->order_clause,
					    node->window_def->n_order_clause,
					    keyword,
					    out_error) != 0 ||
				    sqlparser_view_walk_expr(build, node->window_def->start_offset, keyword, out_error) != 0 ||
				    sqlparser_view_walk_expr(build, node->window_def->end_offset, keyword, out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_view_walk_expr(build, node->null_test->arg, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_view_walk_expr(build, node->boolean_test->arg, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link != NULL) {
				if (sqlparser_view_walk_expr(build, node->sub_link->testexpr, keyword, out_error) != 0 ||
				    sqlparser_view_walk_expr(build, node->sub_link->subselect, keyword, out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr != NULL) {
				if (sqlparser_view_walk_expr(build, node->case_expr->arg, keyword, out_error) != 0 ||
				    sqlparser_view_walk_node_array(
					    build,
					    node->case_expr->args,
					    node->case_expr->n_args,
					    keyword,
					    out_error) != 0 ||
				    sqlparser_view_walk_expr(build, node->case_expr->defresult, keyword, out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when != NULL) {
				if (sqlparser_view_walk_expr(build, node->case_when->expr, keyword, out_error) != 0 ||
				    sqlparser_view_walk_expr(build, node->case_when->result, keyword, out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_view_walk_node_array(
					build,
					node->row_expr->args,
					node->row_expr->n_args,
					keyword,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ROW_COMPARE_EXPR:
			if (node->row_compare_expr != NULL) {
				if (sqlparser_view_walk_node_array(
					    build,
					    node->row_compare_expr->largs,
					    node->row_compare_expr->n_largs,
					    keyword,
					    out_error) != 0 ||
				    sqlparser_view_walk_node_array(
					    build,
					    node->row_compare_expr->rargs,
					    node->row_compare_expr->n_rargs,
					    keyword,
					    out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_view_walk_node_array(
					build,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					keyword,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_view_walk_node_array(
					build,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					keyword,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_view_walk_node_array(
					build,
					node->list->items,
					node->list->n_items,
					keyword,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_view_process_select_stmt(build, node->select_stmt, out_error);
		default:
			return 0;
	}
}

static int sqlparser_view_add_all_statement_relations(
	sqlparser_view_build_t *build,
	sqlparser_error_t *out_error)
{
	size_t count;
	size_t index;
	sqlparser_status_t status;

	count = 0U;
	status = sqlparser_statement_relation_count(
		build->handle,
		build->statement_index,
		&count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	for (index = 0U; index < count; index++) {
		sqlparser_relation_view_t relation;
		sqlparser_selector_t selector;

		memset(&relation, 0, sizeof(relation));
		status = sqlparser_statement_relation(
			build->handle,
			build->statement_index,
			index,
			&relation,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
		selector.statement_index = build->statement_index;
		selector.item_index = index;
		if (sqlparser_view_add_relation_object(build, &relation, &selector, out_error) == NULL) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_view_add_qualified_relation_object(
	sqlparser_view_build_t *build,
	const char *database_name,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error)
{
	sqlparser_relation_view_t relation;

	if (table_name == NULL || table_name[0] == '\0') {
		return 0;
	}
	memset(&relation, 0, sizeof(relation));
	relation.database_name = database_name;
	relation.schema_name = schema_name;
	relation.table_name = table_name;
	return sqlparser_view_add_relation_object(build, &relation, NULL, out_error) == NULL ? -1 : 0;
}

static int sqlparser_view_process_qualified_name_node(
	sqlparser_view_build_t *build,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	const char *parts[3];
	const char *text;
	size_t part_count;
	size_t index;

	memset(parts, 0, sizeof(parts));
	part_count = 0U;
	if (sqlparser_node_string_value(node, &text)) {
		parts[0] = text;
		part_count = 1U;
	} else if (node != NULL && node->node_case == PG_QUERY__NODE__NODE_LIST && node->list != NULL) {
		for (index = 0U; index < node->list->n_items; index++) {
			if (!sqlparser_node_string_value(node->list->items[index], &text)) {
				continue;
			}
			if (part_count < 3U) {
				parts[part_count] = text;
				part_count++;
			} else {
				parts[0] = parts[1];
				parts[1] = parts[2];
				parts[2] = text;
			}
		}
	}
	if (part_count == 0U) {
		return 0;
	}
	if (part_count == 1U) {
		return sqlparser_view_add_qualified_relation_object(build, NULL, NULL, parts[0], out_error);
	}
	if (part_count == 2U) {
		return sqlparser_view_add_qualified_relation_object(build, NULL, parts[0], parts[1], out_error);
	}
	return sqlparser_view_add_qualified_relation_object(build, parts[0], parts[1], parts[2], out_error);
}

static int sqlparser_view_process_drop_stmt(
	sqlparser_view_build_t *build,
	PgQuery__DropStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	for (index = 0U; index < stmt->n_objects; index++) {
		if (sqlparser_view_process_qualified_name_node(build, stmt->objects[index], out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_view_process_from_item(
	sqlparser_view_build_t *build,
	PgQuery__Node *node,
	sqlparser_error_t *out_error);

static int sqlparser_view_append_merge_target_column(
	sqlparser_view_build_t *build,
	PgQuery__ResTarget *target,
	const char *keyword,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	if (target == NULL || target->name == NULL || target->name[0] == '\0' ||
	    json_array_size(build->objects) == 0U) {
		return 0;
	}
	return sqlparser_view_append_column_to_object(
		build,
		json_array_get(build->objects, 0U),
		target->name,
		keyword,
		value_node != NULL ? "=" : NULL,
		NULL,
		value_node,
		NULL,
		NULL,
		out_error);
}

static int sqlparser_view_process_merge_when_clause(
	sqlparser_view_build_t *build,
	PgQuery__MergeWhenClause *clause,
	sqlparser_error_t *out_error)
{
	size_t index;
	const char *keyword;

	if (clause == NULL) {
		return 0;
	}
	if (sqlparser_view_walk_expr(build, clause->condition, "when", out_error) != 0) {
		return -1;
	}
	keyword = clause->command_type == PG_QUERY__CMD_TYPE__CMD_UPDATE ? "set" : "insert";
	for (index = 0U; index < clause->n_target_list; index++) {
		PgQuery__Node *node;

		node = clause->target_list[index];
		if (node == NULL ||
		    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    node->res_target == NULL) {
			continue;
		}
		if (sqlparser_view_append_merge_target_column(
			    build,
			    node->res_target,
			    keyword,
			    clause->command_type == PG_QUERY__CMD_TYPE__CMD_UPDATE ? node->res_target->val : NULL,
			    out_error) != 0 ||
		    sqlparser_view_walk_expr(build, node->res_target->val, keyword, out_error) != 0) {
			return -1;
		}
	}
	if (sqlparser_view_walk_node_array(build, clause->values, clause->n_values, "insert", out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_process_merge_stmt(
	sqlparser_view_build_t *build,
	PgQuery__MergeStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	if (sqlparser_view_walk_expr(build, stmt->join_condition, "on", out_error) != 0 ||
	    sqlparser_view_process_from_item(build, stmt->source_relation, out_error) != 0) {
		return -1;
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *node;

		node = stmt->merge_when_clauses[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE &&
		    sqlparser_view_process_merge_when_clause(build, node->merge_when_clause, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_view_append_insert_columns(
	sqlparser_view_build_t *build,
	PgQuery__InsertStmt *stmt,
	sqlparser_error_t *out_error)
{
	json_t *object;
	size_t index;

	if (stmt == NULL || stmt->relation == NULL || json_array_size(build->objects) == 0U) {
		return 0;
	}
	object = json_array_get(build->objects, 0U);
	for (index = 0U; index < stmt->n_cols; index++) {
		PgQuery__Node *node;
		PgQuery__ResTarget *target;
		sqlparser_selector_t selector;
		size_t name_index;

		node = stmt->cols[index];
		if (node == NULL || node->node_case != PG_QUERY__NODE__NODE_RES_TARGET || node->res_target == NULL) {
			continue;
		}
		target = node->res_target;
		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
		name_index = sqlparser_view_find_name_selector_index(build, &target->name);
		if (name_index != (size_t)-1) {
			selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
			selector.statement_index = build->statement_index;
			selector.item_index = name_index;
		}
		if (sqlparser_view_append_column_to_object(
			    build,
			    object,
			    target->name,
			    "insert",
			    NULL,
			    NULL,
			    NULL,
			    &selector,
			    NULL,
			    out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_view_append_insert_rows(
	sqlparser_view_build_t *build,
	PgQuery__InsertStmt *stmt,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *rows;
	PgQuery__SelectStmt *values_stmt;
	size_t row_index;

	if (stmt == NULL || sqlparser_insert_source_from_stmt(stmt) != SQLPARSER_INSERT_SOURCE_VALUES ||
	    stmt->select_stmt == NULL || stmt->select_stmt->select_stmt == NULL ||
	    json_array_size(build->objects) == 0U) {
		return 0;
	}

	object = json_array_get(build->objects, 0U);
	rows = sqlparser_view_object_rows(object);
	values_stmt = stmt->select_stmt->select_stmt;
	for (row_index = 0U; row_index < values_stmt->n_values_lists; row_index++) {
		PgQuery__Node *row_node;
		PgQuery__List *row_list;
		json_t *row;
		json_t *cells;
		size_t cell_index;

		row_node = values_stmt->values_lists[row_index];
		if (row_node == NULL || row_node->node_case != PG_QUERY__NODE__NODE_LIST || row_node->list == NULL) {
			continue;
		}
		row_list = row_node->list;
		row = json_object();
		cells = json_array();
		if (row == NULL || cells == NULL ||
		    sqlparser_json_set_size(row, "index", row_index) != 0 ||
		    json_object_set_new(row, "cells", cells) != 0) {
			json_decref(row);
			json_decref(cells);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
		cells = NULL;

		for (cell_index = 0U; cell_index < row_list->n_items; cell_index++) {
			json_t *cell;
			sqlparser_selector_t selector;
			char *cell_sql;
			const char *column_name;
			sqlparser_status_t status;

			cell_sql = NULL;
			status = sqlparser_insert_cell_sql(
				build->handle,
				build->statement_index,
				row_index,
				cell_index,
				&cell_sql,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				json_decref(row);
				return -1;
			}

			column_name = NULL;
			if (cell_index < stmt->n_cols &&
			    stmt->cols[cell_index] != NULL &&
			    stmt->cols[cell_index]->node_case == PG_QUERY__NODE__NODE_RES_TARGET &&
			    stmt->cols[cell_index]->res_target != NULL) {
				column_name = sqlparser_nonempty(stmt->cols[cell_index]->res_target->name);
			}

			cell = json_object();
			memset(&selector, 0, sizeof(selector));
			selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
			selector.statement_index = build->statement_index;
			selector.row_index = row_index;
			selector.column_index = cell_index;
			if (cell == NULL ||
			    sqlparser_json_set_string_or_null(cell, "column", column_name) != 0 ||
			    sqlparser_json_set_size(cell, "column_index", cell_index) != 0 ||
			    json_object_set_new(cell, "sql", json_string(cell_sql)) != 0 ||
			    sqlparser_json_set_selector_or_null(cell, "selector", &selector, out_error) != 0 ||
			    json_array_append_new(json_object_get(row, "cells"), cell) != 0) {
				json_decref(cell);
				sqlparser_string_free(cell_sql);
				json_decref(row);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return -1;
			}
			sqlparser_string_free(cell_sql);
		}

		if (json_array_append_new(rows, row) != 0) {
			json_decref(row);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}

	return 0;
}

static int sqlparser_view_process_from_clause(
	sqlparser_view_build_t *build,
	PgQuery__Node **items,
	size_t count,
	sqlparser_error_t *out_error);

static int sqlparser_view_process_from_item(
	sqlparser_view_build_t *build,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_JOIN_EXPR:
			if (node->join_expr != NULL) {
				if (sqlparser_view_process_from_item(build, node->join_expr->larg, out_error) != 0 ||
				    sqlparser_view_process_from_item(build, node->join_expr->rarg, out_error) != 0 ||
				    sqlparser_view_walk_expr(build, node->join_expr->quals, "on", out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_RANGE_SUBSELECT:
			if (node->range_subselect != NULL) {
				return sqlparser_view_walk_expr(build, node->range_subselect->subquery, "select", out_error);
			}
			return 0;
		default:
			return 0;
	}
}

static int sqlparser_view_process_from_clause(
	sqlparser_view_build_t *build,
	PgQuery__Node **items,
	size_t count,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_view_process_from_item(build, items[index], out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_view_process_select_stmt(
	sqlparser_view_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_error_t *out_error)
{
	if (stmt == NULL) {
		return 0;
	}
	if (stmt->with_clause != NULL) {
		size_t index;
		for (index = 0U; index < stmt->with_clause->n_ctes; index++) {
			PgQuery__Node *cte_node;

			cte_node = stmt->with_clause->ctes[index];
			if (cte_node != NULL &&
			    cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
			    cte_node->common_table_expr != NULL) {
				if (sqlparser_view_walk_expr(
					    build,
					    cte_node->common_table_expr->ctequery,
					    "select",
					    out_error) != 0) {
					return -1;
				}
			}
		}
	}
	if (stmt->larg != NULL || stmt->rarg != NULL) {
		if (sqlparser_view_process_select_stmt(build, stmt->larg, out_error) != 0 ||
		    sqlparser_view_process_select_stmt(build, stmt->rarg, out_error) != 0 ||
		    sqlparser_view_walk_node_array(build, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0) {
			return -1;
		}
		return 0;
	}
	if (sqlparser_view_walk_node_array(build, stmt->target_list, stmt->n_target_list, "select", out_error) != 0 ||
	    sqlparser_view_process_from_clause(build, stmt->from_clause, stmt->n_from_clause, out_error) != 0 ||
	    sqlparser_view_walk_expr(build, stmt->where_clause, "where", out_error) != 0 ||
	    sqlparser_view_walk_node_array(build, stmt->group_clause, stmt->n_group_clause, "group", out_error) != 0 ||
	    sqlparser_view_walk_expr(build, stmt->having_clause, "having", out_error) != 0 ||
	    sqlparser_view_walk_node_array(build, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_process_update_stmt(
	sqlparser_view_build_t *build,
	PgQuery__UpdateStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL || json_array_size(build->objects) == 0U) {
		return 0;
	}
	for (index = 0U; index < stmt->n_target_list; index++) {
		PgQuery__Node *node;
		PgQuery__ResTarget *target;
		sqlparser_selector_t selector;
		sqlparser_selector_t value_selector;
		size_t name_index;

		node = stmt->target_list[index];
		if (node == NULL || node->node_case != PG_QUERY__NODE__NODE_RES_TARGET || node->res_target == NULL) {
			continue;
		}
		target = node->res_target;
		memset(&selector, 0, sizeof(selector));
		selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
		name_index = sqlparser_view_find_name_selector_index(build, &target->name);
		if (name_index != (size_t)-1) {
			selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
			selector.statement_index = build->statement_index;
			selector.item_index = name_index;
		}
		memset(&value_selector, 0, sizeof(value_selector));
		value_selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		value_selector.statement_index = build->statement_index;
		value_selector.item_index = index;
		if (sqlparser_view_append_column_to_object(
			    build,
			    json_array_get(build->objects, 0U),
			    target->name,
			    "set",
			    "=",
			    NULL,
			    target->val,
			    &selector,
			    &value_selector,
			    out_error) != 0 ||
		    sqlparser_view_walk_expr(build, target->val, "set", out_error) != 0) {
			return -1;
		}
	}
	if (sqlparser_view_process_from_clause(build, stmt->from_clause, stmt->n_from_clause, out_error) != 0 ||
	    sqlparser_view_walk_expr(build, stmt->where_clause, "where", out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_process_statement(
	sqlparser_view_build_t *build,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	if (statement == NULL) {
		return 0;
	}
	if (sqlparser_view_add_all_statement_relations(build, out_error) != 0) {
		return -1;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_view_process_select_stmt(build, statement->select_stmt, out_error);
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (sqlparser_view_append_insert_columns(build, statement->insert_stmt, out_error) != 0 ||
			    sqlparser_view_append_insert_rows(build, statement->insert_stmt, out_error) != 0) {
				return -1;
			}
			if (statement->insert_stmt != NULL &&
			    statement->insert_stmt->select_stmt != NULL &&
			    statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
				return sqlparser_view_process_select_stmt(
					build,
					statement->insert_stmt->select_stmt->select_stmt,
					out_error);
			}
			return 0;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return sqlparser_view_process_update_stmt(build, statement->update_stmt, out_error);
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (statement->delete_stmt != NULL) {
				if (sqlparser_view_process_from_clause(
					    build,
					    statement->delete_stmt->using_clause,
					    statement->delete_stmt->n_using_clause,
					    out_error) != 0 ||
				    sqlparser_view_walk_expr(build, statement->delete_stmt->where_clause, "where", out_error) != 0) {
					return -1;
				}
			}
			return 0;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return sqlparser_view_process_merge_stmt(build, statement->merge_stmt, out_error);
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			if (statement->view_stmt != NULL &&
			    statement->view_stmt->query != NULL &&
			    statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
				return sqlparser_view_process_select_stmt(
					build,
					statement->view_stmt->query->select_stmt,
					out_error);
			}
			return 0;
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			if (statement->create_table_as_stmt != NULL &&
			    statement->create_table_as_stmt->query != NULL &&
			    statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
				return sqlparser_view_process_select_stmt(
					build,
					statement->create_table_as_stmt->query->select_stmt,
					out_error);
			}
			return 0;
		case PG_QUERY__NODE__NODE_DROP_STMT:
			return sqlparser_view_process_drop_stmt(build, statement->drop_stmt, out_error);
		case PG_QUERY__NODE__NODE_VARIABLE_SET_STMT:
			return sqlparser_view_append_variable_set_object(build, statement->variable_set_stmt, out_error);
		default:
			return 0;
	}
}

typedef struct {
	const sqlparser_view_build_t *build;
	const char *items[SQLPARSER_STATEMENT_KEYWORD_CAPACITY];
	size_t count;
} sqlparser_view_keyword_state_t;

static void sqlparser_view_keyword_view_add(
	sqlparser_view_keyword_state_t *keywords,
	const char *keyword)
{
	size_t index;

	if (keywords == NULL || keyword == NULL || keyword[0] == '\0') {
		return;
	}
	for (index = 0U; index < keywords->count; index++) {
		if (strcmp(keywords->items[index], keyword) == 0) {
			return;
		}
	}
	if (keywords->count < SQLPARSER_STATEMENT_KEYWORD_CAPACITY) {
		keywords->items[keywords->count++] = keyword;
	}
}

static void sqlparser_view_keyword_view_from_select(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__SelectStmt *stmt)
{
	const char *set_keyword;

	if (stmt == NULL) {
		return;
	}
	sqlparser_view_keyword_view_add(keywords, "select");
	if (stmt->n_from_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "from");
	}
	if (stmt->where_clause != NULL) {
		sqlparser_view_keyword_view_add(keywords, "where");
	}
	if (stmt->n_group_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "group");
		sqlparser_view_keyword_view_add(keywords, "by");
	}
	if (stmt->having_clause != NULL) {
		sqlparser_view_keyword_view_add(keywords, "having");
	}
	if (stmt->n_sort_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "order");
		sqlparser_view_keyword_view_add(keywords, "by");
	}
	if (stmt->limit_offset != NULL || stmt->limit_count != NULL) {
		sqlparser_view_keyword_view_add(keywords, "limit");
	}
	set_keyword = sqlparser_select_set_operator_keyword(keywords->build, stmt);
	if (set_keyword != NULL) {
		sqlparser_view_keyword_view_add(keywords, set_keyword);
	}
	if (stmt->larg != NULL) {
		sqlparser_view_keyword_view_from_select(keywords, stmt->larg);
	}
	if (stmt->rarg != NULL) {
		sqlparser_view_keyword_view_from_select(keywords, stmt->rarg);
	}
}

static void sqlparser_view_keyword_view_from_node(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__Node *statement)
{
	const char *main_keyword;

	if (keywords == NULL) {
		return;
	}
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		sqlparser_view_keyword_view_from_select(keywords, statement->select_stmt);
		return;
	}
	main_keyword = sqlparser_statement_keyword_for_handle(
		keywords->build != NULL ? keywords->build->handle : NULL,
		statement);
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT) {
		if (strcmp(main_keyword, "alter_session") == 0) {
			sqlparser_view_keyword_view_add(keywords, "alter");
			sqlparser_view_keyword_view_add(keywords, "session");
			sqlparser_view_keyword_view_add(keywords, "set");
		} else {
			sqlparser_view_keyword_view_add(keywords, main_keyword);
		}
		return;
	}
	if (strcmp(main_keyword, "create_view") == 0) {
		sqlparser_view_keyword_view_add(keywords, "create");
		sqlparser_view_keyword_view_add(keywords, "view");
		return;
	}
	if (strcmp(main_keyword, "create_table") == 0) {
		sqlparser_view_keyword_view_add(keywords, "create");
		sqlparser_view_keyword_view_add(keywords, "table");
		return;
	}
	if (strcmp(main_keyword, "alter_table") == 0) {
		sqlparser_view_keyword_view_add(keywords, "alter");
		sqlparser_view_keyword_view_add(keywords, "table");
		return;
	}
	if (strncmp(main_keyword, "drop_", 5) == 0) {
		sqlparser_view_keyword_view_add(keywords, "drop");
		sqlparser_view_keyword_view_add(keywords, main_keyword + 5);
		return;
	}
	sqlparser_view_keyword_view_add(keywords, main_keyword);
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_INSERT_STMT) {
		sqlparser_view_keyword_view_add(keywords, "into");
		if (statement->insert_stmt != NULL &&
		    sqlparser_insert_source_from_stmt(statement->insert_stmt) == SQLPARSER_INSERT_SOURCE_VALUES) {
			sqlparser_view_keyword_view_add(keywords, "values");
		} else {
			sqlparser_view_keyword_view_add(keywords, "select");
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT) {
		sqlparser_view_keyword_view_add(keywords, "set");
		if (statement->update_stmt != NULL && statement->update_stmt->where_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "where");
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_DELETE_STMT) {
		sqlparser_view_keyword_view_add(keywords, "from");
		if (statement->delete_stmt != NULL && statement->delete_stmt->where_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "where");
		}
	}
}

typedef struct {
	sqlparser_handle_t *handle;
	size_t statement_index;
	size_t object_index;
	sqlparser_relation_view_t relation;
	size_t seen;
	size_t target_index;
	int want_target;
	int found;
	sqlparser_column_view_t *out_column;
} sqlparser_view_column_search_t;

static int sqlparser_view_relation_matches_column(
	const sqlparser_relation_view_t *relation,
	const char *database_name,
	const char *schema_name,
	const char *table_name)
{
	if (table_name == NULL || table_name[0] == '\0') {
		return 1;
	}
	if (database_name != NULL && database_name[0] != '\0') {
		return sqlparser_text_equal(relation != NULL ? relation->database_name : NULL, database_name) &&
			sqlparser_text_equal(relation != NULL ? relation->schema_name : NULL, schema_name) &&
			sqlparser_text_equal(relation != NULL ? relation->table_name : NULL, table_name);
	}
	if (schema_name != NULL && schema_name[0] != '\0') {
		return sqlparser_text_equal(relation != NULL ? relation->schema_name : NULL, schema_name) &&
			sqlparser_text_equal(relation != NULL ? relation->table_name : NULL, table_name);
	}
	return sqlparser_text_equal(relation != NULL ? relation->alias_name : NULL, table_name) ||
		sqlparser_text_equal(relation != NULL ? relation->table_name : NULL, table_name);
}

static void sqlparser_view_fill_value_view(
	sqlparser_view_column_search_t *search,
	PgQuery__Node *value_node,
	const sqlparser_selector_t *forced_selector,
	sqlparser_value_view_t *out_value)
{
	sqlparser_view_build_t build;
	size_t literal_index;
	size_t value_index;

	if (out_value == NULL) {
		return;
	}
	memset(out_value, 0, sizeof(*out_value));
	if (value_node == NULL) {
		return;
	}
	out_value->statement_index = search->statement_index;
	literal_index = (size_t)-1;
	memset(&build, 0, sizeof(build));
	build.handle = search->handle;
	build.statement_index = search->statement_index;
	if (value_node->node_case == PG_QUERY__NODE__NODE_A_CONST && value_node->a_const != NULL) {
		literal_index = sqlparser_view_find_literal_index(&build, value_node->a_const);
	}
	if (forced_selector != NULL && forced_selector->kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		out_value->selector = *forced_selector;
		out_value->has_selector = 1;
		return;
	}
	value_index = sqlparser_view_find_value_index(&build, value_node);
	if (value_index != (size_t)-1) {
		out_value->selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
		out_value->selector.statement_index = search->statement_index;
		out_value->selector.item_index = value_index;
		out_value->has_selector = 1;
		return;
	}
	if (literal_index == (size_t)-1) {
		return;
	}
	out_value->selector.kind = SQLPARSER_SELECTOR_KIND_LITERAL;
	out_value->selector.statement_index = search->statement_index;
	out_value->selector.item_index = literal_index;
	out_value->has_selector = 1;
}

static int sqlparser_view_emit_column_view(
	sqlparser_view_column_search_t *search,
	const char *name,
	const char *keyword,
	const char *operator_name,
	PgQuery__ColumnRef *column_ref,
	PgQuery__Node *value_node,
	const sqlparser_selector_t *forced_selector,
	const sqlparser_selector_t *forced_value_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_column_view_t *column;
	sqlparser_selector_t selector;
	size_t name_index;

	if (search == NULL || name == NULL || name[0] == '\0') {
		return 0;
	}
	if (!search->want_target) {
		search->seen++;
		return 0;
	}
	if (search->seen != search->target_index) {
		search->seen++;
		return 0;
	}

	column = search->out_column;
	if (column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_column must not be NULL");
		return -1;
	}
	memset(column, 0, sizeof(*column));
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	if (forced_selector != NULL && forced_selector->kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		selector = *forced_selector;
	} else if (column_ref != NULL) {
		sqlparser_view_build_t build;

		memset(&build, 0, sizeof(build));
		build.handle = search->handle;
		build.statement_index = search->statement_index;
		name_index = sqlparser_view_find_name_selector_index(&build, sqlparser_column_ref_name_slot(column_ref));
		if (name_index != (size_t)-1) {
			selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
			selector.statement_index = search->statement_index;
			selector.item_index = name_index;
		}
	}

	column->handle = search->handle;
	column->statement_index = search->statement_index;
	column->object_index = search->object_index;
	column->column_index = search->seen;
	column->name = name;
	column->keyword = keyword != NULL ? keyword : "";
	column->operator_name = operator_name;
	if (selector.kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		column->selector = selector;
		column->has_selector = 1;
	}
	if (value_node != NULL) {
		column->value_count = 1U;
		sqlparser_view_fill_value_view(search, value_node, forced_value_selector, &column->value);
	}
	search->seen++;
	search->found = 1;
	return 0;
}

static int sqlparser_view_search_append_column_ref(
	sqlparser_view_column_search_t *search,
	PgQuery__ColumnRef *column_ref,
	const char *keyword,
	const char *operator_name,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	const char *column_name;
	const char *table_name;
	const char *schema_name;
	const char *database_name;

	if (column_ref == NULL) {
		return 0;
	}
	column_name = sqlparser_column_ref_part(column_ref, 0U);
	if (column_name == NULL) {
		return 0;
	}
	table_name = sqlparser_column_ref_part(column_ref, 1U);
	schema_name = sqlparser_column_ref_part(column_ref, 2U);
	database_name = sqlparser_column_ref_part(column_ref, 3U);
	if (!sqlparser_view_relation_matches_column(&search->relation, database_name, schema_name, table_name)) {
		return 0;
	}
	return sqlparser_view_emit_column_view(
		search,
		column_name,
		keyword,
		operator_name,
		column_ref,
		value_node,
		NULL,
		NULL,
		out_error);
}

static int sqlparser_view_search_walk_expr(
	sqlparser_view_column_search_t *search,
	PgQuery__Node *node,
	const char *keyword,
	sqlparser_error_t *out_error);

static int sqlparser_view_search_process_select_stmt(
	sqlparser_view_column_search_t *search,
	PgQuery__SelectStmt *stmt,
	sqlparser_error_t *out_error);

static int sqlparser_view_search_walk_node_array(
	sqlparser_view_column_search_t *search,
	PgQuery__Node **items,
	size_t count,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_view_search_walk_expr(search, items[index], keyword, out_error) != 0) {
			return -1;
		}
		if (search->want_target && search->found) {
			return 0;
		}
	}
	return 0;
}

static int sqlparser_view_search_walk_a_expr(
	sqlparser_view_column_search_t *search,
	PgQuery__AExpr *a_expr,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	const char *operator_name;
	const char *left_table;
	const char *left_column;
	const char *right_table;
	const char *right_column;

	if (a_expr == NULL) {
		return 0;
	}
	operator_name = sqlparser_a_expr_operator_name(a_expr);
	left_table = NULL;
	left_column = NULL;
	right_table = NULL;
	right_column = NULL;
	(void)left_table;
	(void)right_table;
	if (sqlparser_try_extract_column_ref(a_expr->lexpr, &left_table, &left_column) &&
	    left_column != NULL) {
		if (a_expr->lexpr != NULL && a_expr->lexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_view_search_append_column_ref(
			    search,
			    a_expr->lexpr->column_ref,
			    keyword,
			    operator_name,
			    a_expr->rexpr,
			    out_error) != 0) {
			return -1;
		}
		return sqlparser_view_search_walk_expr(search, a_expr->rexpr, keyword, out_error);
	}
	if (sqlparser_try_extract_column_ref(a_expr->rexpr, &right_table, &right_column) &&
	    right_column != NULL) {
		if (a_expr->rexpr != NULL && a_expr->rexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_view_search_append_column_ref(
			    search,
			    a_expr->rexpr->column_ref,
			    keyword,
			    operator_name,
			    a_expr->lexpr,
			    out_error) != 0) {
			return -1;
		}
		return sqlparser_view_search_walk_expr(search, a_expr->lexpr, keyword, out_error);
	}
	if (sqlparser_view_search_walk_expr(search, a_expr->lexpr, keyword, out_error) != 0) {
		return -1;
	}
	return sqlparser_view_search_walk_expr(search, a_expr->rexpr, keyword, out_error);
}

static int sqlparser_view_search_walk_expr(
	sqlparser_view_column_search_t *search,
	PgQuery__Node *node,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	if (node == NULL || (search->want_target && search->found)) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_COLUMN_REF:
			return sqlparser_view_search_append_column_ref(
				search,
				node->column_ref,
				keyword,
				NULL,
				NULL,
				out_error);
		case PG_QUERY__NODE__NODE_RES_TARGET:
			return node->res_target != NULL ?
				sqlparser_view_search_walk_expr(search, node->res_target->val, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_EXPR:
			return sqlparser_view_search_walk_a_expr(search, node->a_expr, keyword, out_error);
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return node->bool_expr != NULL ?
				sqlparser_view_search_walk_node_array(search, node->bool_expr->args, node->bool_expr->n_args, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call == NULL) {
				return 0;
			}
			if (sqlparser_view_search_walk_node_array(search, node->func_call->args, node->func_call->n_args, keyword, out_error) != 0) {
				return -1;
			}
			if (node->func_call->over != NULL &&
			    (sqlparser_view_search_walk_node_array(search, node->func_call->over->partition_clause, node->func_call->over->n_partition_clause, keyword, out_error) != 0 ||
			     sqlparser_view_search_walk_node_array(search, node->func_call->over->order_clause, node->func_call->over->n_order_clause, keyword, out_error) != 0 ||
			     sqlparser_view_search_walk_expr(search, node->func_call->over->start_offset, keyword, out_error) != 0 ||
			     sqlparser_view_search_walk_expr(search, node->func_call->over->end_offset, keyword, out_error) != 0)) {
				return -1;
			}
			return 0;
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ? sqlparser_view_search_walk_expr(search, node->type_cast->arg, keyword, out_error) : 0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ? sqlparser_view_search_walk_expr(search, node->collate_clause->arg, keyword, out_error) : 0;
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			return node->a_indirection != NULL ? sqlparser_view_search_walk_expr(search, node->a_indirection->arg, keyword, out_error) : 0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_view_search_walk_node_array(search, node->a_array_expr->elements, node->a_array_expr->n_elements, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_view_search_walk_node_array(search, node->array_expr->elements, node->array_expr->n_elements, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_SORT_BY:
			return node->sort_by != NULL ? sqlparser_view_search_walk_expr(search, node->sort_by->node, keyword, out_error) : 0;
		case PG_QUERY__NODE__NODE_WINDOW_DEF:
			if (node->window_def == NULL) {
				return 0;
			}
			return sqlparser_view_search_walk_node_array(search, node->window_def->partition_clause, node->window_def->n_partition_clause, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_node_array(search, node->window_def->order_clause, node->window_def->n_order_clause, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_expr(search, node->window_def->start_offset, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_expr(search, node->window_def->end_offset, keyword, out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ? sqlparser_view_search_walk_expr(search, node->null_test->arg, keyword, out_error) : 0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ? sqlparser_view_search_walk_expr(search, node->boolean_test->arg, keyword, out_error) : 0;
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link == NULL) {
				return 0;
			}
			return sqlparser_view_search_walk_expr(search, node->sub_link->testexpr, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_expr(search, node->sub_link->subselect, keyword, out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr == NULL) {
				return 0;
			}
			return sqlparser_view_search_walk_expr(search, node->case_expr->arg, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_node_array(search, node->case_expr->args, node->case_expr->n_args, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_expr(search, node->case_expr->defresult, keyword, out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when == NULL) {
				return 0;
			}
			return sqlparser_view_search_walk_expr(search, node->case_when->expr, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_expr(search, node->case_when->result, keyword, out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_view_search_walk_node_array(search, node->row_expr->args, node->row_expr->n_args, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_ROW_COMPARE_EXPR:
			if (node->row_compare_expr == NULL) {
				return 0;
			}
			return sqlparser_view_search_walk_node_array(search, node->row_compare_expr->largs, node->row_compare_expr->n_largs, keyword, out_error) != 0 ||
					sqlparser_view_search_walk_node_array(search, node->row_compare_expr->rargs, node->row_compare_expr->n_rargs, keyword, out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_view_search_walk_node_array(search, node->coalesce_expr->args, node->coalesce_expr->n_args, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_view_search_walk_node_array(search, node->min_max_expr->args, node->min_max_expr->n_args, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_view_search_walk_node_array(search, node->list->items, node->list->n_items, keyword, out_error) :
				0;
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_view_search_process_select_stmt(search, node->select_stmt, out_error);
		default:
			return 0;
	}
}

static int sqlparser_view_search_append_insert_columns(
	sqlparser_view_column_search_t *search,
	PgQuery__InsertStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (search->object_index != 0U || stmt == NULL) {
		return 0;
	}
	for (index = 0U; index < stmt->n_cols; index++) {
		PgQuery__Node *node;
		PgQuery__ResTarget *target;
		sqlparser_selector_t selector;
		sqlparser_view_build_t build;
		size_t name_index;

		node = stmt->cols[index];
		if (node == NULL || node->node_case != PG_QUERY__NODE__NODE_RES_TARGET || node->res_target == NULL) {
			continue;
		}
		target = node->res_target;
		memset(&selector, 0, sizeof(selector));
		memset(&build, 0, sizeof(build));
		build.handle = search->handle;
		build.statement_index = search->statement_index;
		name_index = sqlparser_view_find_name_selector_index(&build, &target->name);
		if (name_index != (size_t)-1) {
			selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
			selector.statement_index = search->statement_index;
			selector.item_index = name_index;
		}
		if (sqlparser_view_emit_column_view(
			    search,
			    target->name,
			    "insert",
			    NULL,
			    NULL,
			    NULL,
			    &selector,
			    NULL,
			    out_error) != 0) {
			return -1;
		}
		if (search->want_target && search->found) {
			return 0;
		}
	}
	return 0;
}

static int sqlparser_view_search_process_from_item(
	sqlparser_view_column_search_t *search,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_JOIN_EXPR:
			if (node->join_expr == NULL) {
				return 0;
			}
			return sqlparser_view_search_process_from_item(search, node->join_expr->larg, out_error) != 0 ||
					sqlparser_view_search_process_from_item(search, node->join_expr->rarg, out_error) != 0 ||
					sqlparser_view_search_walk_expr(search, node->join_expr->quals, "on", out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_RANGE_SUBSELECT:
			return node->range_subselect != NULL ?
				sqlparser_view_search_walk_expr(search, node->range_subselect->subquery, "select", out_error) :
				0;
		default:
			return 0;
	}
}

static int sqlparser_view_search_process_from_clause(
	sqlparser_view_column_search_t *search,
	PgQuery__Node **items,
	size_t count,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_view_search_process_from_item(search, items[index], out_error) != 0) {
			return -1;
		}
		if (search->want_target && search->found) {
			return 0;
		}
	}
	return 0;
}

static int sqlparser_view_search_process_select_stmt(
	sqlparser_view_column_search_t *search,
	PgQuery__SelectStmt *stmt,
	sqlparser_error_t *out_error)
{
	if (stmt == NULL) {
		return 0;
	}
	if (stmt->with_clause != NULL) {
		size_t index;

		for (index = 0U; index < stmt->with_clause->n_ctes; index++) {
			PgQuery__Node *cte_node;

			cte_node = stmt->with_clause->ctes[index];
			if (cte_node != NULL &&
			    cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
			    cte_node->common_table_expr != NULL &&
			    sqlparser_view_search_walk_expr(search, cte_node->common_table_expr->ctequery, "select", out_error) != 0) {
				return -1;
			}
			if (search->want_target && search->found) {
				return 0;
			}
		}
	}
	if (stmt->larg != NULL || stmt->rarg != NULL) {
		return sqlparser_view_search_process_select_stmt(search, stmt->larg, out_error) != 0 ||
				sqlparser_view_search_process_select_stmt(search, stmt->rarg, out_error) != 0 ||
				sqlparser_view_search_walk_node_array(search, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0 ?
			-1 :
			0;
	}
	return sqlparser_view_search_walk_node_array(search, stmt->target_list, stmt->n_target_list, "select", out_error) != 0 ||
			sqlparser_view_search_process_from_clause(search, stmt->from_clause, stmt->n_from_clause, out_error) != 0 ||
			sqlparser_view_search_walk_expr(search, stmt->where_clause, "where", out_error) != 0 ||
			sqlparser_view_search_walk_node_array(search, stmt->group_clause, stmt->n_group_clause, "group", out_error) != 0 ||
			sqlparser_view_search_walk_expr(search, stmt->having_clause, "having", out_error) != 0 ||
			sqlparser_view_search_walk_node_array(search, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0 ?
		-1 :
		0;
}

static int sqlparser_view_search_process_update_stmt(
	sqlparser_view_column_search_t *search,
	PgQuery__UpdateStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	if (search->object_index == 0U) {
		for (index = 0U; index < stmt->n_target_list; index++) {
			PgQuery__Node *node;
			PgQuery__ResTarget *target;
			sqlparser_selector_t name_selector;
			sqlparser_selector_t value_selector;
			sqlparser_view_build_t build;
			size_t name_index;

			node = stmt->target_list[index];
			if (node == NULL || node->node_case != PG_QUERY__NODE__NODE_RES_TARGET || node->res_target == NULL) {
				continue;
			}
			target = node->res_target;
			memset(&name_selector, 0, sizeof(name_selector));
			memset(&build, 0, sizeof(build));
			build.handle = search->handle;
			build.statement_index = search->statement_index;
			name_index = sqlparser_view_find_name_selector_index(&build, &target->name);
			if (name_index != (size_t)-1) {
				name_selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
				name_selector.statement_index = search->statement_index;
				name_selector.item_index = name_index;
			}
			memset(&value_selector, 0, sizeof(value_selector));
			value_selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
			value_selector.statement_index = search->statement_index;
			value_selector.item_index = index;
			if (sqlparser_view_emit_column_view(
				    search,
				    target->name,
				    "set",
				    "=",
				    NULL,
				    target->val,
				    &name_selector,
				    &value_selector,
				    out_error) != 0 ||
			    sqlparser_view_search_walk_expr(search, target->val, "set", out_error) != 0) {
				return -1;
			}
			if (search->want_target && search->found) {
				return 0;
			}
		}
	}
	return sqlparser_view_search_process_from_clause(search, stmt->from_clause, stmt->n_from_clause, out_error) != 0 ||
			sqlparser_view_search_walk_expr(search, stmt->where_clause, "where", out_error) != 0 ?
		-1 :
		0;
}

static int sqlparser_view_search_process_variable_set_stmt(
	sqlparser_view_column_search_t *search,
	PgQuery__VariableSetStmt *stmt,
	sqlparser_error_t *out_error)
{
	const char *name;
	const char *keyword;
	const char *operator_name;
	size_t index;

	if (search == NULL || stmt == NULL || !sqlparser_variable_set_is_session_context(stmt)) {
		return 0;
	}
	if (search->object_index != 0U) {
		return 0;
	}

	keyword = sqlparser_variable_set_column_keyword(search->handle, stmt);
	operator_name = sqlparser_variable_set_operator(search->handle, stmt);
	for (index = 0U; index < stmt->n_args; index++) {
		name = sqlparser_variable_set_public_name_at(search->handle, stmt, index);
		if (sqlparser_view_emit_column_view(
			    search,
			    name,
			    keyword,
			    operator_name,
			    NULL,
			    stmt->args[index],
			    NULL,
			    NULL,
			    out_error) != 0) {
			return -1;
		}
		if (search->want_target && search->found) {
			return 0;
		}
	}
	return 0;
}

static int sqlparser_view_search_process_statement(
	sqlparser_view_column_search_t *search,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	if (statement == NULL) {
		return 0;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_view_search_process_select_stmt(search, statement->select_stmt, out_error);
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (sqlparser_view_search_append_insert_columns(search, statement->insert_stmt, out_error) != 0) {
				return -1;
			}
			if (statement->insert_stmt != NULL &&
			    statement->insert_stmt->select_stmt != NULL &&
			    statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
				return sqlparser_view_search_process_select_stmt(
					search,
					statement->insert_stmt->select_stmt->select_stmt,
					out_error);
			}
			return 0;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return sqlparser_view_search_process_update_stmt(search, statement->update_stmt, out_error);
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return statement->delete_stmt != NULL ?
				(sqlparser_view_search_process_from_clause(
					 search,
					 statement->delete_stmt->using_clause,
					 statement->delete_stmt->n_using_clause,
					 out_error) != 0 ||
						sqlparser_view_search_walk_expr(search, statement->delete_stmt->where_clause, "where", out_error) != 0 ?
					-1 :
					0) :
				0;
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return statement->view_stmt != NULL &&
					statement->view_stmt->query != NULL &&
					statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_search_process_select_stmt(search, statement->view_stmt->query->select_stmt, out_error) :
				0;
		case PG_QUERY__NODE__NODE_VARIABLE_SET_STMT:
			return sqlparser_view_search_process_variable_set_stmt(search, statement->variable_set_stmt, out_error);
		default:
			return 0;
	}
}

static sqlparser_status_t sqlparser_view_column_search(
	const sqlparser_object_view_t *object,
	int want_target,
	size_t column_index,
	sqlparser_column_view_t *out_column,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_view_column_search_t search;
	sqlparser_status_t status;

	if (object == NULL || object->handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "object must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (want_target && out_column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_column must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	statement = NULL;
	status = sqlparser_get_statement_node(
		(sqlparser_handle_t *)object->handle,
		object->statement_index,
		&statement,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.handle = (sqlparser_handle_t *)object->handle;
	search.statement_index = object->statement_index;
	search.object_index = object->object_index;
	search.relation.database_name = object->database_name;
	search.relation.schema_name = object->schema_name;
	search.relation.table_name = object->table_name;
	search.relation.alias_name = object->alias_name;
	search.want_target = want_target;
	search.target_index = column_index;
	search.out_column = out_column;
	if (sqlparser_view_search_process_statement(&search, statement, out_error) != 0) {
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (!want_target) {
		if (out_count != NULL) {
			*out_count = search.seen;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!search.found) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_export_view_json(
	const sqlparser_handle_t *handle,
	int pretty,
	char **out_json,
	sqlparser_error_t *out_error)
{
	json_t *root;
	json_t *statements;
	sqlparser_handle_t *mutable_handle;
	size_t statement_index;
	sqlparser_status_t status;
	char *json_text;
	int ast_was_loaded;

	if (out_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_json must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_json = NULL;
	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	mutable_handle = (sqlparser_handle_t *)handle;
	ast_was_loaded = mutable_handle->ast != NULL;
	status = sqlparser_handle_ensure_ast(mutable_handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	root = json_object();
	statements = json_array();
	if (root == NULL || statements == NULL || json_object_set_new(root, "statements", statements) != 0) {
		json_decref(root);
		json_decref(statements);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_ast(mutable_handle);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	statements = NULL;

	for (statement_index = 0U; statement_index < handle->statement_count; statement_index++) {
		PgQuery__Node *statement;
		json_t *statement_json;
		json_t *keywords;
		sqlparser_view_build_t build;

		statement = NULL;
		status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			return status;
		}

		statement_json = json_object();
		keywords = json_array();
		memset(&build, 0, sizeof(build));
		build.objects = json_array();
		build.handle = mutable_handle;
		build.statement_index = statement_index;
		if (statement_json == NULL || keywords == NULL || build.objects == NULL) {
			json_decref(statement_json);
			json_decref(keywords);
			json_decref(build.objects);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		sqlparser_keywords_from_node(&build, statement, keywords);
		if (sqlparser_json_set_size(statement_json, "index", statement_index) != 0 ||
		    json_object_set_new(
			    statement_json,
			    "keyword",
			    json_string(sqlparser_statement_keyword_for_handle(mutable_handle, statement))) != 0 ||
		    json_object_set_new(statement_json, "keywords", keywords) != 0) {
			json_decref(keywords);
			json_decref(build.objects);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		keywords = NULL;

		if (sqlparser_view_process_statement(&build, statement, out_error) != 0 ||
		    json_object_set_new(statement_json, "objects", build.objects) != 0) {
			json_decref(build.objects);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		build.objects = NULL;

		if (json_array_append_new(json_object_get(root, "statements"), statement_json) != 0) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	json_text = json_dumps(root, (pretty ? JSON_INDENT(2) : JSON_COMPACT) | JSON_ENSURE_ASCII);
	json_decref(root);
	if (json_text == NULL) {
		if (!ast_was_loaded) {
			sqlparser_handle_clear_ast(mutable_handle);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_validate_handle_output_text(handle, json_text, "SQL view JSON", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(json_text);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_ast(mutable_handle);
		}
		return status;
	}

	*out_json = json_text;
	if (!ast_was_loaded) {
		sqlparser_handle_clear_ast(mutable_handle);
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_get_view(
	const sqlparser_handle_t *handle,
	sqlparser_view_t *out_view,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_view == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_view must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_view, 0, sizeof(*out_view));
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	out_view->handle = handle;
	out_view->statement_count = handle->statement_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_view_statement_at(
	const sqlparser_view_t *view,
	size_t statement_index,
	sqlparser_statement_view_t *out_statement,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_handle_t *handle;
	sqlparser_status_t status;
	size_t object_count;
	sqlparser_view_build_t build;
	sqlparser_view_keyword_state_t keywords;
	size_t keyword_index;

	sqlparser_error_clear(out_error);
	if (out_statement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_statement, 0, sizeof(*out_statement));
	if (view == NULL || view->handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "view must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	handle = (sqlparser_handle_t *)view->handle;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	object_count = 0U;
	if (statement != NULL &&
	    statement->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT &&
	    sqlparser_variable_set_is_session_context(statement->variable_set_stmt)) {
		object_count = 1U;
	} else {
		(void)sqlparser_statement_relation_count(handle, statement_index, &object_count, NULL);
	}
	memset(&build, 0, sizeof(build));
	build.handle = handle;
	build.statement_index = statement_index;
	memset(&keywords, 0, sizeof(keywords));
	keywords.build = &build;
	sqlparser_view_keyword_view_from_node(&keywords, statement);
	out_statement->handle = view->handle;
	out_statement->index = statement_index;
	out_statement->keyword = sqlparser_statement_keyword_for_handle(handle, statement);
	out_statement->keyword_count = keywords.count;
	for (keyword_index = 0U; keyword_index < keywords.count; keyword_index++) {
		out_statement->keywords[keyword_index] = keywords.items[keyword_index];
	}
	out_statement->object_count = object_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_keyword_at(
	const sqlparser_statement_view_t *statement,
	size_t keyword_index,
	const char **out_keyword,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_keyword == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_keyword must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_keyword = NULL;
	if (statement == NULL || keyword_index >= statement->keyword_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "keyword_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_keyword = statement->keywords[keyword_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_object_at(
	const sqlparser_statement_view_t *statement,
	size_t object_index,
	sqlparser_object_view_t *out_object,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement_node;
	sqlparser_relation_view_t relation;
	sqlparser_status_t status;
	sqlparser_selector_t selector;

	sqlparser_error_clear(out_error);
	if (out_object == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_object must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_object, 0, sizeof(*out_object));
	if (statement == NULL || statement->handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	statement_node = NULL;
	status = sqlparser_get_statement_node(
		(sqlparser_handle_t *)statement->handle,
		statement->index,
		&statement_node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (statement_node != NULL &&
	    statement_node->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT &&
	    sqlparser_variable_set_is_session_context(statement_node->variable_set_stmt)) {
		if (object_index != 0U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "object_index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		out_object->handle = statement->handle;
		out_object->statement_index = statement->index;
		out_object->object_index = object_index;
		out_object->database_name = NULL;
		out_object->schema_name = NULL;
		out_object->table_name = NULL;
		out_object->alias_name = NULL;
		memset(&out_object->selector, 0, sizeof(out_object->selector));
		out_object->selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
		out_object->has_selector = 0;
		(void)sqlparser_view_column_search(out_object, 0, 0U, NULL, &out_object->column_count, NULL);
		out_object->row_count = 0U;
		return SQLPARSER_STATUS_OK;
	}
	memset(&relation, 0, sizeof(relation));
	status = sqlparser_statement_relation(
		statement->handle,
		statement->index,
		object_index,
		&relation,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
	selector.statement_index = statement->index;
	selector.item_index = object_index;
	out_object->handle = statement->handle;
	out_object->statement_index = statement->index;
	out_object->object_index = object_index;
	out_object->database_name = relation.database_name;
	out_object->schema_name = relation.schema_name;
	out_object->table_name = relation.table_name;
	out_object->alias_name = relation.alias_name;
	out_object->selector = selector;
	out_object->has_selector = 1;
	(void)sqlparser_view_column_search(out_object, 0, 0U, NULL, &out_object->column_count, NULL);
	if (object_index == 0U) {
		(void)sqlparser_insert_row_count(statement->handle, statement->index, &out_object->row_count, NULL);
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_object_column_at(
	const sqlparser_object_view_t *object,
	size_t column_index,
	sqlparser_column_view_t *out_column,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_column must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_column, 0, sizeof(*out_column));
	return sqlparser_view_column_search(object, 1, column_index, out_column, NULL, out_error);
}

sqlparser_status_t sqlparser_column_value_at(
	const sqlparser_column_view_t *column,
	size_t value_index,
	sqlparser_value_view_t *out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	if (column == NULL || value_index >= column->value_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = column->value;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_object_row_at(
	const sqlparser_object_view_t *object,
	size_t row_index,
	sqlparser_row_view_t *out_row,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_row == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_row must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_row, 0, sizeof(*out_row));
	if (object == NULL || object->handle == NULL || row_index >= object->row_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "row_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	out_row->handle = object->handle;
	out_row->statement_index = object->statement_index;
	out_row->object_index = object->object_index;
	out_row->row_index = row_index;
	(void)sqlparser_insert_column_count(object->handle, object->statement_index, &out_row->cell_count, NULL);
	if (out_row->cell_count == 0U) {
		PgQuery__InsertStmt *insert_stmt;
		PgQuery__SelectStmt *values_stmt;
		sqlparser_error_t tmp_error;
		memset(&tmp_error, 0, sizeof(tmp_error));
		if (sqlparser_get_insert_values_stmt(
			    (sqlparser_handle_t *)object->handle,
			    object->statement_index,
			    &insert_stmt,
			    &values_stmt,
			    &tmp_error) == SQLPARSER_STATUS_OK &&
		    values_stmt != NULL && row_index < values_stmt->n_values_lists &&
		    values_stmt->values_lists[row_index] != NULL &&
		    values_stmt->values_lists[row_index]->node_case == PG_QUERY__NODE__NODE_LIST &&
		    values_stmt->values_lists[row_index]->list != NULL) {
			out_row->cell_count = values_stmt->values_lists[row_index]->list->n_items;
		}
		(void)insert_stmt;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_row_cell_at(
	const sqlparser_row_view_t *row,
	size_t cell_index,
	sqlparser_cell_view_t *out_cell,
	sqlparser_error_t *out_error)
{
	const char *column_name;
	sqlparser_selector_t selector;

	sqlparser_error_clear(out_error);
	if (out_cell == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_cell must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_cell, 0, sizeof(*out_cell));
	if (row == NULL || row->handle == NULL || cell_index >= row->cell_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "cell_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	column_name = NULL;
	(void)sqlparser_insert_column_name(row->handle, row->statement_index, cell_index, &column_name, NULL);
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = row->statement_index;
	selector.row_index = row->row_index;
	selector.column_index = cell_index;
	out_cell->handle = row->handle;
	out_cell->statement_index = row->statement_index;
	out_cell->object_index = row->object_index;
	out_cell->row_index = row->row_index;
	out_cell->cell_index = cell_index;
	out_cell->column_name = column_name;
	out_cell->column_index = cell_index;
	out_cell->selector = selector;
	out_cell->has_selector = 1;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_value_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_value_view_t *value,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	PgQuery__Node **node_slot;
	PgQuery__Node literal_node;
	char *core_sql;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || value == NULL || !value->has_selector) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value selector is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (value->selector.kind == SQLPARSER_SELECTOR_KIND_VALUE) {
		PgQuery__Node *statement;
		PgQuery__VariableSetStmt *set_stmt;
		size_t arg_index;

		statement = NULL;
		set_stmt = NULL;
		arg_index = (size_t)-1;
		status = sqlparser_get_statement_node(
			(sqlparser_handle_t *)handle,
			value->selector.statement_index,
			&statement,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		node_slot = NULL;
		status = sqlparser_get_statement_node_slot_by_index(
			(sqlparser_handle_t *)handle,
			value->selector.statement_index,
			value->selector.item_index,
			&node_slot,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (node_slot == NULL || *node_slot == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value selector is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		core_sql = NULL;
		if (sqlparser_variable_set_arg_slot_matches(statement, node_slot)) {
			set_stmt = statement->variable_set_stmt;
			arg_index = sqlparser_variable_set_arg_index(set_stmt, *node_slot);
			status = sqlparser_render_variable_set_arg_node_sql(*node_slot, &core_sql, out_error);
		} else {
			status = sqlparser_render_update_assignment_node_sql(*node_slot, &core_sql, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (set_stmt != NULL) {
			status = sqlparser_postprocess_variable_set_arg_sql(
				handle,
				set_stmt,
				arg_index,
				core_sql,
				"value SQL",
				out_sql,
				out_error);
		} else if ((*node_slot)->node_case == PG_QUERY__NODE__NODE_A_CONST &&
		    (*node_slot)->a_const != NULL &&
		    handle->dialect_ops != NULL &&
		    handle->dialect_ops->postprocess_literal_fragment != NULL) {
			sqlparser_view_build_t build;
			size_t literal_index;

			memset(&build, 0, sizeof(build));
			build.handle = (sqlparser_handle_t *)handle;
			build.statement_index = value->selector.statement_index;
			literal_index = sqlparser_view_find_literal_index(&build, (*node_slot)->a_const);
			if (literal_index != (size_t)-1) {
				status = handle->dialect_ops->postprocess_literal_fragment(
					core_sql,
					handle->dialect_state,
					literal_index,
					out_sql,
					out_error);
			} else {
				status = sqlparser_postprocess_handle_sql_fragment(
					handle,
					core_sql,
					"value SQL",
					out_sql,
					out_error);
			}
		} else {
			status = sqlparser_postprocess_handle_sql_fragment(
				handle,
				core_sql,
				"value SQL",
				out_sql,
				out_error);
		}
		free(core_sql);
		return status;
	}
	if (value->selector.kind == SQLPARSER_SELECTOR_KIND_LITERAL) {
		message = NULL;
		status = sqlparser_search_statement_messages(
			(sqlparser_handle_t *)handle,
			value->selector.statement_index,
			&pg_query__a__const__descriptor,
			NULL,
			1,
			value->selector.item_index,
			NULL,
			&message,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (message == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal selector is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		pg_query__node__init(&literal_node);
		literal_node.node_case = PG_QUERY__NODE__NODE_A_CONST;
		literal_node.a_const = (PgQuery__AConst *)message;
		core_sql = NULL;
		status = sqlparser_render_update_assignment_node_sql(&literal_node, &core_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (handle->dialect_ops != NULL &&
		    handle->dialect_ops->postprocess_literal_fragment != NULL) {
			status = handle->dialect_ops->postprocess_literal_fragment(
				core_sql,
				handle->dialect_state,
				value->selector.item_index,
				out_sql,
				out_error);
		} else {
			status = sqlparser_postprocess_handle_sql_fragment(
				handle,
				core_sql,
				"value SQL",
				out_sql,
				out_error);
		}
		free(core_sql);
		return status;
	}
	if (value->selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		return sqlparser_selector_insert_cell_sql(handle, &value->selector, out_sql, out_error);
	}
	if (value->selector.kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT) {
		return sqlparser_selector_update_assignment_sql(handle, &value->selector, out_sql, out_error);
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "value does not expose SQL text");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

sqlparser_status_t sqlparser_cell_sql(
	const sqlparser_cell_view_t *cell,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (cell == NULL || cell->handle == NULL || !cell->has_selector) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "cell selector is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_selector_insert_cell_sql(cell->handle, &cell->selector, out_sql, out_error);
}
