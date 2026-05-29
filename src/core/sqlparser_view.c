#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_oracle_internal.h"

typedef struct {
	char *name;
	sqlparser_bind_kind_t kind;
	size_t position;
} sqlparser_view_bind_info_t;

typedef struct {
	PgQuery__ParamRef *param_ref;
	size_t traversal_index;
	int location;
} sqlparser_view_bind_position_entry_t;

typedef struct {
	sqlparser_handle_t *handle;
	size_t statement_index;
} sqlparser_view_build_t;

typedef struct {
	sqlparser_handle_t *handle;
	size_t statement_index;
	size_t seen;
	size_t target_index;
	int want_target;
	sqlparser_clause_kind_t target_kind;
	PgQuery__Node *target_expr;
} sqlparser_view_readonly_clause_search_t;

static int sqlparser_text_equal_ci(const char *left, const char *right)
{
	if (left == NULL || right == NULL) {
		return left == right;
	}
	while (*left != '\0' && *right != '\0') {
		if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
			return 0;
		}
		left++;
		right++;
	}
	return *left == '\0' && *right == '\0';
}

static void sqlparser_view_copy_public_text(
	char *dst,
	size_t dst_size,
	const char *src,
	int *out_truncated)
{
	size_t len;
	size_t copy_len;

	if (out_truncated != NULL) {
		*out_truncated = 0;
	}
	if (dst == NULL || dst_size == 0U) {
		if (out_truncated != NULL && src != NULL && src[0] != '\0') {
			*out_truncated = 1;
		}
		return;
	}
	dst[0] = '\0';
	if (src == NULL || src[0] == '\0') {
		return;
	}
	len = strlen(src);
	copy_len = len < dst_size ? len : dst_size - 1U;
	if (copy_len > 0U) {
		memcpy(dst, src, copy_len);
	}
	dst[copy_len] = '\0';
	if (out_truncated != NULL && copy_len < len) {
		*out_truncated = 1;
	}
}

static int sqlparser_json_set_optional_string(json_t *object, const char *key, const char *value)
{
	json_t *item;

	if (object == NULL || key == NULL) {
		return -1;
	}
	if (value == NULL || value[0] == '\0') {
		return 0;
	}
	item = json_string(value);
	if (item == NULL) {
		return -1;
	}
	return json_object_set_new(object, key, item);
}

static int sqlparser_json_set_size(json_t *object, const char *key, size_t value)
{
	return json_object_set_new(object, key, json_integer((json_int_t)value));
}

static int sqlparser_json_set_optional_size(json_t *object, const char *key, int has_value, size_t value)
{
	if (object == NULL || key == NULL) {
		return -1;
	}
	return has_value ? json_object_set_new(object, key, json_integer((json_int_t)value)) : 0;
}

static int sqlparser_json_set_optional_selector(
	json_t *object,
	const char *key,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	char *selector_text;
	sqlparser_status_t status;
	int rc;

	if (selector == NULL || selector->kind == SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		return 0;
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

static int sqlparser_json_set_nonempty_array(json_t *object, const char *key, json_t **array)
{
	if (object == NULL || key == NULL || array == NULL || *array == NULL) {
		return -1;
	}
	if (json_array_size(*array) == 0U) {
		json_decref(*array);
		*array = NULL;
		return 0;
	}
	if (json_object_set_new(object, key, *array) != 0) {
		*array = NULL;
		return -1;
	}
	*array = NULL;
	return 0;
}

static int sqlparser_json_array_append_owned(json_t *array, json_t **item)
{
	if (array == NULL || item == NULL || *item == NULL) {
		return -1;
	}
	if (json_array_append_new(array, *item) != 0) {
		*item = NULL;
		return -1;
	}
	*item = NULL;
	return 0;
}

static int sqlparser_json_object_set_owned(json_t *object, const char *key, json_t **item)
{
	if (object == NULL || key == NULL || item == NULL || *item == NULL) {
		return -1;
	}
	if (json_object_set_new(object, key, *item) != 0) {
		*item = NULL;
		return -1;
	}
	*item = NULL;
	return 0;
}

static char *sqlparser_view_ascii_upper_dup(const char *text)
{
	char *copy;
	size_t index;
	size_t len;

	if (text == NULL || text[0] == '\0') {
		return NULL;
	}
	len = strlen(text);
	copy = (char *)malloc(len + 1U);
	if (copy == NULL) {
		return NULL;
	}
	for (index = 0U; index < len; index++) {
		copy[index] = (char)toupper((unsigned char)text[index]);
	}
	copy[len] = '\0';
	return copy;
}

static int sqlparser_view_readonly_clause_record(
	sqlparser_view_readonly_clause_search_t *search,
	sqlparser_clause_kind_t kind,
	PgQuery__Node *expr)
{
	if (search == NULL || expr == NULL) {
		return 0;
	}
	if (search->want_target && search->seen == search->target_index) {
		search->target_kind = kind;
		search->target_expr = expr;
		return 1;
	}
	search->seen++;
	return 0;
}

static int sqlparser_view_readonly_clause_walk_select(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__SelectStmt *stmt);

static int sqlparser_view_readonly_clause_walk_from_item(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__Node *node)
{
	if (node == NULL) {
		return 0;
	}
	if (search->want_target && search->target_expr != NULL) {
		return 1;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_JOIN_EXPR:
			if (node->join_expr == NULL) {
				return 0;
			}
			if (sqlparser_view_readonly_clause_walk_from_item(search, node->join_expr->larg) ||
			    sqlparser_view_readonly_clause_walk_from_item(search, node->join_expr->rarg)) {
				return 1;
			}
			return sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_ON, node->join_expr->quals);
		case PG_QUERY__NODE__NODE_RANGE_SUBSELECT:
			return node->range_subselect != NULL &&
				node->range_subselect->subquery != NULL &&
				node->range_subselect->subquery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, node->range_subselect->subquery->select_stmt) :
				0;
		default:
			return 0;
	}
}

static int sqlparser_view_readonly_clause_walk_from_clause(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__Node **items,
	size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_view_readonly_clause_walk_from_item(search, items[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_view_readonly_clause_walk_select(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__SelectStmt *stmt)
{
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	if (search->want_target && search->target_expr != NULL) {
		return 1;
	}
	if (stmt->with_clause != NULL) {
		for (index = 0U; index < stmt->with_clause->n_ctes; index++) {
			PgQuery__Node *cte_node;

			cte_node = stmt->with_clause->ctes[index];
			if (cte_node != NULL &&
			    cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
			    cte_node->common_table_expr != NULL &&
			    cte_node->common_table_expr->ctequery != NULL &&
			    cte_node->common_table_expr->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
			    sqlparser_view_readonly_clause_walk_select(search, cte_node->common_table_expr->ctequery->select_stmt)) {
				return 1;
			}
		}
	}
	if (stmt->larg != NULL || stmt->rarg != NULL) {
		return sqlparser_view_readonly_clause_walk_select(search, stmt->larg) ||
			sqlparser_view_readonly_clause_walk_select(search, stmt->rarg);
	}
	if (sqlparser_view_readonly_clause_walk_from_clause(search, stmt->from_clause, stmt->n_from_clause)) {
		return 1;
	}
	for (index = 0U; index < stmt->n_group_clause; index++) {
		if (sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_GROUP_BY, stmt->group_clause[index])) {
			return 1;
		}
	}
	return sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_HAVING, stmt->having_clause);
}

static int sqlparser_view_readonly_clause_walk_merge(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__MergeStmt *stmt)
{
	if (stmt == NULL) {
		return 0;
	}
	if (sqlparser_view_readonly_clause_record(search, SQLPARSER_CLAUSE_KIND_ON, stmt->join_condition)) {
		return 1;
	}
	return sqlparser_view_readonly_clause_walk_from_item(search, stmt->source_relation);
}

static int sqlparser_view_readonly_clause_walk_statement(
	sqlparser_view_readonly_clause_search_t *search,
	PgQuery__Node *statement)
{
	if (statement == NULL) {
		return 0;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_view_readonly_clause_walk_select(search, statement->select_stmt);
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return statement->insert_stmt != NULL &&
					statement->insert_stmt->select_stmt != NULL &&
					statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, statement->insert_stmt->select_stmt->select_stmt) :
				0;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return statement->update_stmt != NULL ?
				sqlparser_view_readonly_clause_walk_from_clause(search, statement->update_stmt->from_clause, statement->update_stmt->n_from_clause) :
				0;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return statement->delete_stmt != NULL ?
				sqlparser_view_readonly_clause_walk_from_clause(search, statement->delete_stmt->using_clause, statement->delete_stmt->n_using_clause) :
				0;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return sqlparser_view_readonly_clause_walk_merge(search, statement->merge_stmt);
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return statement->view_stmt != NULL &&
					statement->view_stmt->query != NULL &&
					statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, statement->view_stmt->query->select_stmt) :
				0;
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return statement->create_table_as_stmt != NULL &&
					statement->create_table_as_stmt->query != NULL &&
					statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_view_readonly_clause_walk_select(search, statement->create_table_as_stmt->query->select_stmt) :
				0;
		default:
			return 0;
	}
}

static sqlparser_status_t sqlparser_view_readonly_clause_search(
	sqlparser_handle_t *handle,
	size_t statement_index,
	int want_target,
	size_t target_index,
	sqlparser_view_readonly_clause_search_t *out_search,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;

	if (handle == NULL || out_search == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "readonly clause search requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	statement = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(out_search, 0, sizeof(*out_search));
	out_search->handle = handle;
	out_search->statement_index = statement_index;
	out_search->want_target = want_target;
	out_search->target_index = target_index;
	out_search->target_kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	(void)sqlparser_view_readonly_clause_walk_statement(out_search, statement);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_full_clause_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_view_readonly_clause_search_t readonly;
	size_t base_count;
	char *core_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	base_count = 0U;
	status = sqlparser_statement_clause_count(handle, statement_index, &base_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause_index < base_count) {
		return sqlparser_statement_clause_sql(handle, statement_index, clause_index, out_sql, out_error);
	}
	status = sqlparser_view_readonly_clause_search(
		(sqlparser_handle_t *)handle,
		statement_index,
		1,
		clause_index - base_count,
		&readonly,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (readonly.target_expr == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	core_sql = NULL;
	status = sqlparser_render_update_assignment_node_sql(readonly.target_expr, &core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		core_sql,
		"view clause",
		out_sql,
		out_error);
	free(core_sql);
	return status;
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
		case PG_QUERY__NODE__NODE_CREATE_SEQ_STMT:
			return "create_sequence";
		case PG_QUERY__NODE__NODE_ALTER_SEQ_STMT:
			return "alter_sequence";
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
		case PG_QUERY__NODE__NODE_PREPARE_STMT:
			return "prepare";
		case PG_QUERY__NODE__NODE_EXECUTE_STMT:
			return "execute";
		case PG_QUERY__NODE__NODE_DEALLOCATE_STMT:
			return "deallocate";
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

static int sqlparser_variable_set_name_has_prefix(const PgQuery__VariableSetStmt *stmt, const char *prefix)
{
	size_t prefix_len;

	if (stmt == NULL || stmt->name == NULL || prefix == NULL) {
		return 0;
	}
	prefix_len = strlen(prefix);
	return strncmp(stmt->name, prefix, prefix_len) == 0;
}

static int sqlparser_variable_set_is_prepared_statement(const PgQuery__VariableSetStmt *stmt)
{
	return sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_EXECUTE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE);
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
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE)) {
		return "prepare";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_EXECUTE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE)) {
		return "execute";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE)) {
		return "deallocate";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE)) {
		return "sp_prepare";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE)) {
		return "sp_execute";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC)) {
		return "sp_prepexec";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE)) {
		return "sp_unprepare";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL)) {
		return "sp_executesql";
	}
	return "set";
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
	    (handle->dialect == SQLPARSER_DIALECT_ORACLE ||
	     handle->dialect == SQLPARSER_DIALECT_DAMENG)) {
		return "alter_session";
	}
	if (handle != NULL &&
	    handle->dialect == SQLPARSER_DIALECT_ORACLE &&
	    sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX)) {
		return "alter_session";
	}
	if (sqlparser_variable_set_is_prepared_statement(stmt)) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	return "set";
}

static char *sqlparser_view_func_call_name_dup(const PgQuery__FuncCall *func_call)
{
	const char *name;
	size_t index;

	if (func_call == NULL || func_call->n_funcname == 0U) {
		return NULL;
	}
	name = NULL;
	for (index = func_call->n_funcname; index > 0U; index--) {
		if (sqlparser_node_string_value(func_call->funcname[index - 1U], &name)) {
			break;
		}
	}
	return sqlparser_view_ascii_upper_dup(name);
}

static int sqlparser_view_func_call_is_name(
	const PgQuery__FuncCall *func_call,
	const char *expected_name)
{
	const char *name;
	size_t index;

	if (func_call == NULL || expected_name == NULL || func_call->n_funcname == 0U) {
		return 0;
	}
	name = NULL;
	for (index = func_call->n_funcname; index > 0U; index--) {
		if (sqlparser_node_string_value(func_call->funcname[index - 1U], &name)) {
			break;
		}
	}
	return name != NULL && sqlparser_text_equal_ci(name, expected_name);
}

static const char *sqlparser_view_bool_expr_name(const PgQuery__BoolExpr *expr)
{
	if (expr == NULL) {
		return NULL;
	}
	switch (expr->boolop) {
		case PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR:
			return "AND";
		case PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR:
			return "OR";
		case PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR:
			return "NOT";
		default:
			return NULL;
	}
}

static const char *sqlparser_view_min_max_name(const PgQuery__MinMaxExpr *expr)
{
	if (expr == NULL) {
		return NULL;
	}
	switch (expr->op) {
		case PG_QUERY__MIN_MAX_OP__IS_GREATEST:
			return "GREATEST";
		case PG_QUERY__MIN_MAX_OP__IS_LEAST:
			return "LEAST";
		default:
			return NULL;
	}
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

static int sqlparser_view_text_is_digits(const char *text)
{
	size_t index;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	for (index = 0U; text[index] != '\0'; index++) {
		if (!isdigit((unsigned char)text[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_view_parse_positive_size(const char *text, size_t *out_value)
{
	size_t index;
	size_t value;

	if (text == NULL || out_value == NULL || text[0] == '\0') {
		return 0;
	}
	value = 0U;
	for (index = 0U; text[index] != '\0'; index++) {
		unsigned int digit;

		if (!isdigit((unsigned char)text[index])) {
			return 0;
		}
		digit = (unsigned int)(text[index] - '0');
		if (value > (((size_t)-1) - digit) / 10U) {
			return 0;
		}
		value = value * 10U + (size_t)digit;
	}
	if (value == 0U) {
		return 0;
	}
	*out_value = value;
	return 1;
}

static sqlparser_bind_kind_t sqlparser_view_bind_kind_from_public_sql(
	const char *public_sql,
	const char *normalized_name)
{
	if (public_sql == NULL || public_sql[0] == '\0') {
		return SQLPARSER_BIND_KIND_NONE;
	}
	if (strcmp(public_sql, "?") == 0) {
		return SQLPARSER_BIND_KIND_POSITIONAL;
	}
	if ((public_sql[0] == ':' || public_sql[0] == '$') &&
	    sqlparser_view_text_is_digits(normalized_name)) {
		return SQLPARSER_BIND_KIND_POSITIONAL;
	}
	if (public_sql[0] == ':' || public_sql[0] == '@' || public_sql[0] == '$') {
		return SQLPARSER_BIND_KIND_NAMED;
	}
	return SQLPARSER_BIND_KIND_NONE;
}

static void sqlparser_view_bind_info_release(sqlparser_view_bind_info_t *info)
{
	if (info == NULL) {
		return;
	}
	free(info->name);
	info->name = NULL;
	info->kind = SQLPARSER_BIND_KIND_NONE;
	info->position = 0U;
}

static int sqlparser_view_bind_position_entry_compare(const void *left, const void *right)
{
	const sqlparser_view_bind_position_entry_t *a;
	const sqlparser_view_bind_position_entry_t *b;

	a = (const sqlparser_view_bind_position_entry_t *)left;
	b = (const sqlparser_view_bind_position_entry_t *)right;
	if (a->location < b->location) {
		return -1;
	}
	if (a->location > b->location) {
		return 1;
	}
	if (a->traversal_index < b->traversal_index) {
		return -1;
	}
	if (a->traversal_index > b->traversal_index) {
		return 1;
	}
	return 0;
}

static int sqlparser_view_bind_public_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch == '$' || ch == '#';
}

static int sqlparser_view_bind_dollar_tag_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_';
}

static size_t sqlparser_view_bind_public_skip_dollar_quote(const char *sql, size_t index)
{
	size_t tag_end;
	size_t body;
	size_t delimiter_len;

	if (sql == NULL || sql[index] != '$') {
		return index;
	}
	tag_end = index + 1U;
	while (sqlparser_view_bind_dollar_tag_char_is_ident((unsigned char)sql[tag_end])) {
		tag_end++;
	}
	if (sql[tag_end] != '$') {
		return index;
	}
	delimiter_len = tag_end - index + 1U;
	body = tag_end + 1U;
	while (sql[body] != '\0') {
		if (strncmp(sql + body, sql + index, delimiter_len) == 0) {
			return body + delimiter_len;
		}
		body++;
	}
	return index;
}

static size_t sqlparser_view_bind_public_skip_quoted_or_comment(const char *sql, size_t index)
{
	char quote;
	size_t pos;

	if (sql == NULL || sql[index] == '\0') {
		return index;
	}
	pos = sqlparser_view_bind_public_skip_dollar_quote(sql, index);
	if (pos != index) {
		return pos;
	}
	if (sql[index] == '-' && sql[index + 1U] == '-') {
		pos = index + 2U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sql[index] == '/' && sql[index + 1U] == '*') {
		pos = index + 2U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				return pos + 2U;
			}
			pos++;
		}
		return pos;
	}
	if (sql[index] == '[') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == ']') {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}
	if (sql[index] != '\'' && sql[index] != '"' && sql[index] != '`') {
		return index;
	}

	quote = sql[index];
	pos = index + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == quote) {
			if (quote == '\'' && sql[pos + 1U] == '\'') {
				pos += 2U;
				continue;
			}
			return pos + 1U;
		}
		pos++;
	}
	return pos;
}

static int sqlparser_view_bind_public_token_matches(const char *sql, size_t pos, const char *public_sql)
{
	size_t len;

	if (sql == NULL || public_sql == NULL || public_sql[0] == '\0') {
		return 0;
	}
	len = strlen(public_sql);
	if (strncmp(sql + pos, public_sql, len) != 0) {
		return 0;
	}
	if (public_sql[0] == ':' && pos > 0U && sql[pos - 1U] == ':') {
		return 0;
	}
	if ((public_sql[0] == ':' || public_sql[0] == '@' || public_sql[0] == '$') &&
	    sqlparser_view_bind_public_char_is_ident((unsigned char)sql[pos + len])) {
		return 0;
	}
	return 1;
}

static int sqlparser_view_bind_info_set_public_position(
	sqlparser_handle_t *handle,
	const char *public_sql,
	size_t same_bind_ordinal,
	sqlparser_view_bind_info_t *out_info)
{
	const char *sql;
	size_t index;
	size_t position;
	size_t same_seen;
	size_t question_seen;
	size_t question_target;
	sqlparser_error_t error;

	if (handle == NULL || public_sql == NULL || public_sql[0] == '\0' || out_info == NULL || same_bind_ordinal == 0U) {
		return 0;
	}
	memset(&error, 0, sizeof(error));
	if (sqlparser_ensure_current_sql_text(handle, &error) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	sql = sqlparser_effective_sql(handle);
	if (sql == NULL) {
		return 0;
	}

	position = 0U;
	same_seen = 0U;
	question_seen = 0U;
	question_target = 0U;
	if (strcmp(public_sql, "?") == 0 &&
	    !sqlparser_view_parse_positive_size(out_info->name, &question_target)) {
		return 0;
	}
	for (index = 0U; sql[index] != '\0';) {
		size_t skipped;
		int is_bind_token;

		skipped = sqlparser_view_bind_public_skip_quoted_or_comment(sql, index);
		if (skipped != index) {
			index = skipped;
			continue;
		}
		is_bind_token =
			sql[index] == '?' ||
			(sql[index] == '$' && isdigit((unsigned char)sql[index + 1U])) ||
			(sql[index] == ':' && sql[index + 1U] != ':' &&
			 (isalnum((unsigned char)sql[index + 1U]) || sql[index + 1U] == '_')) ||
			(sql[index] == '@' &&
			 (isalnum((unsigned char)sql[index + 1U]) || sql[index + 1U] == '_'));
		if (is_bind_token) {
			position++;
			if (public_sql[0] == '?' && sql[index] == '?') {
				question_seen++;
				if (question_seen == question_target) {
					out_info->position = position;
					return 1;
				}
			}
		}
		if (public_sql[0] != '?' && is_bind_token) {
			if (sqlparser_view_bind_public_token_matches(sql, index, public_sql)) {
				same_seen++;
				if (same_seen == same_bind_ordinal) {
					out_info->position = position;
					return 1;
				}
			}
		}
		index++;
	}
	return 0;
}

static int sqlparser_view_bind_info_set_position(
	sqlparser_handle_t *handle,
	const char *public_sql,
	const PgQuery__Node *value_node,
	sqlparser_view_bind_info_t *out_info,
	sqlparser_error_t *out_error)
{
	sqlparser_view_bind_position_entry_t *entries;
	ProtobufCMessage *message;
	size_t statement_index;
	size_t statement_bind_count;
	size_t count;
	size_t index;
	size_t local_index;
	size_t entry_index;
	size_t ast_position;
	size_t same_bind_ordinal;
	int use_location_order;
	int found;
	sqlparser_status_t status;

	if (handle == NULL || value_node == NULL || out_info == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind position arguments are missing");
		return -1;
	}
	if (value_node->node_case != PG_QUERY__NODE__NODE_PARAM_REF || value_node->param_ref == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind position requires a ParamRef node");
		return -1;
	}

	count = 0U;
	for (statement_index = 0U; statement_index < handle->statement_count; statement_index++) {
		statement_bind_count = 0U;
		status = sqlparser_search_statement_messages(
			handle,
			statement_index,
			&pg_query__param_ref__descriptor,
			NULL,
			0,
			0U,
			&statement_bind_count,
			NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		count += statement_bind_count;
	}
	if (count == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "bind position could not be resolved");
		return -1;
	}
	entries = (sqlparser_view_bind_position_entry_t *)calloc(count, sizeof(*entries));
	if (entries == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}

	use_location_order = value_node->param_ref->location >= 0;
	found = 0;
	entry_index = 0U;
	for (statement_index = 0U; statement_index < handle->statement_count; statement_index++) {
		statement_bind_count = 0U;
		status = sqlparser_search_statement_messages(
			handle,
			statement_index,
			&pg_query__param_ref__descriptor,
			NULL,
			0,
			0U,
			&statement_bind_count,
			NULL,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(entries);
			return -1;
		}
		for (local_index = 0U; local_index < statement_bind_count; local_index++) {
			message = NULL;
			status = sqlparser_search_statement_messages(
				handle,
				statement_index,
				&pg_query__param_ref__descriptor,
				NULL,
				1,
				local_index,
				NULL,
				&message,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(entries);
				return -1;
			}
			if (message == NULL || entry_index >= count) {
				free(entries);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "bind position traversal failed");
				return -1;
			}
			entries[entry_index].param_ref = (PgQuery__ParamRef *)message;
			entries[entry_index].traversal_index = entry_index;
			entries[entry_index].location = entries[entry_index].param_ref->location;
			if (entries[entry_index].param_ref->location < 0) {
				use_location_order = 0;
			}
			if (entries[entry_index].param_ref == value_node->param_ref) {
				found = 1;
			}
			entry_index++;
		}
	}
	if (!found) {
		free(entries);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "bind position could not be resolved");
		return -1;
	}
	if (use_location_order) {
		qsort(entries, count, sizeof(*entries), sqlparser_view_bind_position_entry_compare);
	}
	ast_position = 0U;
	same_bind_ordinal = 0U;
	for (index = 0U; index < count; index++) {
		if (entries[index].param_ref->number == value_node->param_ref->number) {
			same_bind_ordinal++;
		}
		if (entries[index].param_ref == value_node->param_ref) {
			ast_position = index + 1U;
			free(entries);
			out_info->position = ast_position;
			(void)sqlparser_view_bind_info_set_public_position(handle, public_sql, same_bind_ordinal, out_info);
			return 1;
		}
	}

	free(entries);
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "bind position could not be resolved");
	return -1;
}

static int sqlparser_view_bind_info_from_value(
	sqlparser_handle_t *handle,
	const char *public_sql,
	const PgQuery__Node *value_node,
	sqlparser_view_bind_info_t *out_info,
	sqlparser_error_t *out_error)
{
	char buffer[32];
	const char *start;
	size_t index;

	if (out_info == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_info must not be NULL");
		return -1;
	}
	memset(out_info, 0, sizeof(*out_info));
	out_info->kind = SQLPARSER_BIND_KIND_NONE;
	if (public_sql == NULL ||
	    handle == NULL ||
	    value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_PARAM_REF ||
	    value_node->param_ref == NULL) {
		return 0;
	}

	if (strcmp(public_sql, "?") == 0) {
		if (value_node->param_ref->number <= 0) {
			return 0;
		}
		(void)snprintf(buffer, sizeof(buffer), "%ld", (long)value_node->param_ref->number);
		out_info->name = sqlparser_strdup(buffer);
		if (out_info->name == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
		out_info->kind = SQLPARSER_BIND_KIND_POSITIONAL;
		return sqlparser_view_bind_info_set_position(handle, public_sql, value_node, out_info, out_error);
	}

	if (public_sql[0] != ':' && public_sql[0] != '@' && public_sql[0] != '$') {
		return 0;
	}
	start = public_sql + 1U;
	while (*start == public_sql[0]) {
		start++;
	}
	if (*start == '\0') {
		return 0;
	}
	for (index = 0U; start[index] != '\0'; index++) {
		if (isspace((unsigned char)start[index])) {
			return 0;
		}
	}
	out_info->name = sqlparser_strdup(start);
	if (out_info->name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	out_info->kind = sqlparser_view_bind_kind_from_public_sql(public_sql, out_info->name);
	if (out_info->kind == SQLPARSER_BIND_KIND_NONE) {
		sqlparser_view_bind_info_release(out_info);
		return 0;
	}
	return sqlparser_view_bind_info_set_position(handle, public_sql, value_node, out_info, out_error);
}

static sqlparser_status_t sqlparser_view_render_param_ref_public_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__ParamRef *param_ref,
	char **out_public_sql,
	sqlparser_error_t *out_error)
{
	char core_sql[32];

	if (out_public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_public_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_sql = NULL;
	if (param_ref == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "ParamRef node is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (param_ref->number <= 0) {
		*out_public_sql = sqlparser_strdup("?");
		if (*out_public_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	(void)snprintf(core_sql, sizeof(core_sql), "$%ld", (long)param_ref->number);
	return sqlparser_postprocess_handle_sql_fragment(
		handle,
		core_sql,
		"query graph value",
		out_public_sql,
		out_error);
}

static sqlparser_status_t sqlparser_view_render_value_node_public_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *value_node,
	char **out_public_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_view_build_t build;
	sqlparser_error_t render_error;
	sqlparser_status_t status;
	char *core_sql;
	size_t literal_index;

	if (out_public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_public_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_sql = NULL;
	if (handle == NULL || value_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value node is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (value_node->node_case == PG_QUERY__NODE__NODE_PARAM_REF && value_node->param_ref != NULL) {
		return sqlparser_view_render_param_ref_public_sql(handle, value_node->param_ref, out_public_sql, out_error);
	}

	core_sql = NULL;
	literal_index = (size_t)-1;
	memset(&build, 0, sizeof(build));
	build.handle = handle;
	build.statement_index = statement_index;
	if (value_node->node_case == PG_QUERY__NODE__NODE_A_CONST && value_node->a_const != NULL) {
		literal_index = sqlparser_view_find_literal_index(&build, value_node->a_const);
	}

	memset(&render_error, 0, sizeof(render_error));
	status = sqlparser_render_update_assignment_node_sql(value_node, &core_sql, &render_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (status == SQLPARSER_STATUS_NO_MEMORY || status == SQLPARSER_STATUS_RESOURCE_LIMIT) {
			sqlparser_error_set_message(out_error, status, render_error.message);
		}
		return status;
	}
	if (literal_index != (size_t)-1 &&
	    handle->dialect_ops != NULL &&
	    handle->dialect_ops->postprocess_literal_fragment != NULL) {
		status = handle->dialect_ops->postprocess_literal_fragment(
			core_sql,
			handle->dialect_state,
			literal_index,
			out_public_sql,
			out_error);
	} else {
		status = sqlparser_postprocess_handle_sql_fragment(
			handle,
			core_sql,
			"query graph value",
			out_public_sql,
			out_error);
	}
	free(core_sql);
	return status;
}

static PgQuery__CommonTableExpr *sqlparser_view_select_find_cte(
	PgQuery__SelectStmt *scope,
	const char *name)
{
	size_t index;

	if (scope == NULL || scope->with_clause == NULL || name == NULL || name[0] == '\0') {
		return NULL;
	}
	for (index = 0U; index < scope->with_clause->n_ctes; index++) {
		PgQuery__Node *node;

		node = scope->with_clause->ctes[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
		    node->common_table_expr != NULL &&
		    node->common_table_expr->ctename != NULL &&
		    strcmp(node->common_table_expr->ctename, name) == 0) {
			return node->common_table_expr;
		}
	}
	return NULL;
}

typedef struct {
	size_t root_block_index;
	int has_root_block;
	size_t block_offset;
	size_t block_count;
	size_t relation_offset;
	size_t relation_count;
	size_t target_offset;
	size_t target_count;
	size_t field_offset;
	size_t field_count;
	size_t value_offset;
	size_t value_count;
	size_t set_offset;
	size_t set_count;
	size_t dml_offset;
	size_t dml_count;
	size_t dml_branch_offset;
	size_t dml_branch_count;
	size_t dml_column_offset;
	size_t dml_column_count;
	size_t dml_cell_offset;
	size_t dml_cell_count;
	size_t dml_assignment_offset;
	size_t dml_assignment_count;
} sqlparser_statement_graph_t;

struct sqlparser_query_graph_cache {
	unsigned long generation;
	size_t statement_count;
	sqlparser_statement_graph_t *statements;
	size_t block_count;
	size_t block_capacity;
	sqlparser_graph_block_t *blocks;
	size_t relation_count;
	size_t relation_capacity;
	sqlparser_graph_relation_t *relations;
	size_t target_count;
	size_t target_capacity;
	sqlparser_graph_target_t *targets;
	size_t field_count;
	size_t field_capacity;
	sqlparser_graph_field_t *fields;
	size_t value_count;
	size_t value_capacity;
	sqlparser_graph_value_t *values;
	size_t set_count;
	size_t set_capacity;
	sqlparser_graph_set_t *sets;
	size_t dml_count;
	size_t dml_capacity;
	sqlparser_graph_dml_t *dml;
	size_t dml_branch_count;
	size_t dml_branch_capacity;
	sqlparser_graph_dml_branch_t *dml_branches;
	size_t dml_column_count;
	size_t dml_column_capacity;
	sqlparser_graph_dml_column_t *dml_columns;
	size_t dml_cell_count;
	size_t dml_cell_capacity;
	sqlparser_graph_dml_cell_t *dml_cells;
	size_t dml_assignment_count;
	size_t dml_assignment_capacity;
	sqlparser_graph_dml_assignment_t *dml_assignments;
	size_t index_pool_count;
	size_t index_pool_capacity;
	size_t *index_pool;
};

typedef struct {
	size_t block_index;
	PgQuery__SelectStmt *select_stmt;
} sqlparser_graph_scope_t;

typedef struct {
	sqlparser_handle_t *handle;
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	size_t statement_index;
	sqlparser_graph_scope_t scopes[32];
	size_t scope_count;
	sqlparser_target_path_entry_t target_path[SQLPARSER_TARGET_PATH_CAPACITY];
	size_t target_path_count;
} sqlparser_graph_build_t;

static int sqlparser_graph_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *value_node,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error);

static int sqlparser_query_graph_reserve_array(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	sqlparser_error_t *out_error)
{
	void *new_items;
	size_t new_capacity;

	if (items == NULL || capacity == NULL || item_size == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid graph array");
		return -1;
	}
	if (required <= *capacity) {
		return 0;
	}
	new_capacity = *capacity != 0U ? *capacity : 16U;
	while (new_capacity < required) {
		if (new_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph is too large");
			return -1;
		}
		new_capacity *= 2U;
	}
	if (new_capacity > ((size_t)-1) / item_size) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph is too large");
		return -1;
	}
	new_items = realloc(*items, new_capacity * item_size);
	if (new_items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	*items = new_items;
	*capacity = new_capacity;
	return 0;
}

void sqlparser_query_graph_cache_release(sqlparser_query_graph_cache_t *cache)
{
	if (cache == NULL) {
		return;
	}
	free(cache->statements);
	free(cache->blocks);
	free(cache->relations);
	free(cache->targets);
	free(cache->fields);
	free(cache->values);
	free(cache->sets);
	free(cache->dml);
	free(cache->dml_branches);
	free(cache->dml_columns);
	free(cache->dml_cells);
	free(cache->dml_assignments);
	free(cache->index_pool);
	free(cache);
}

static int sqlparser_graph_append_index(
	sqlparser_graph_build_t *build,
	size_t value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;

	if (build == NULL || build->cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder is missing");
		return -1;
	}
	cache = build->cache;
	if (sqlparser_query_graph_reserve_array(
		    (void **)&cache->index_pool,
		    &cache->index_pool_capacity,
		    cache->index_pool_count + 1U,
		    sizeof(*cache->index_pool),
		    out_error) != 0) {
		return -1;
	}
	cache->index_pool[cache->index_pool_count++] = value;
	return 0;
}

static int sqlparser_graph_span_append_index(
	sqlparser_graph_build_t *build,
	sqlparser_index_span_t *span,
	size_t value,
	sqlparser_error_t *out_error)
{
	if (span == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph span is missing");
		return -1;
	}
	if (span->count == 0U) {
		span->offset = build->cache->index_pool_count;
	}
	if (sqlparser_graph_append_index(build, value, out_error) != 0) {
		return -1;
	}
	span->count++;
	return 0;
}

static size_t sqlparser_graph_local_block_count(const sqlparser_graph_build_t *build)
{
	return build->cache->block_count - build->statement->block_offset;
}

static size_t sqlparser_graph_local_relation_count(const sqlparser_graph_build_t *build)
{
	return build->cache->relation_count - build->statement->relation_offset;
}

static size_t sqlparser_graph_local_target_count(const sqlparser_graph_build_t *build)
{
	return build->cache->target_count - build->statement->target_offset;
}

static size_t sqlparser_graph_local_field_count(const sqlparser_graph_build_t *build)
{
	return build->cache->field_count - build->statement->field_offset;
}

static size_t sqlparser_graph_local_value_count(const sqlparser_graph_build_t *build)
{
	return build->cache->value_count - build->statement->value_offset;
}

static size_t sqlparser_graph_local_set_count(const sqlparser_graph_build_t *build)
{
	return build->cache->set_count - build->statement->set_offset;
}

static int sqlparser_graph_add_block(
	sqlparser_graph_build_t *build,
	sqlparser_graph_block_kind_t kind,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_block_t *block;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (build == NULL || build->cache == NULL || build->statement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "query graph builder is missing");
		return -1;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->blocks,
		    &build->cache->block_capacity,
		    build->cache->block_count + 1U,
		    sizeof(*build->cache->blocks),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->block_count++;
	local_index = sqlparser_graph_local_block_count(build) - 1U;
	block = &build->cache->blocks[global_index];
	memset(block, 0, sizeof(*block));
	block->index = local_index;
	block->statement_index = build->statement_index;
	block->kind = kind;
	if (!build->statement->has_root_block) {
		build->statement->root_block_index = local_index;
		build->statement->has_root_block = 1;
	}
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static sqlparser_graph_block_t *sqlparser_graph_block_by_local(
	sqlparser_graph_build_t *build,
	size_t block_index)
{
	if (build == NULL || build->statement == NULL ||
	    block_index >= sqlparser_graph_local_block_count(build)) {
		return NULL;
	}
	return &build->cache->blocks[build->statement->block_offset + block_index];
}

static sqlparser_graph_relation_t *sqlparser_graph_relation_by_local(
	sqlparser_graph_build_t *build,
	size_t relation_index)
{
	if (build == NULL || build->statement == NULL ||
	    relation_index >= sqlparser_graph_local_relation_count(build)) {
		return NULL;
	}
	return &build->cache->relations[build->statement->relation_offset + relation_index];
}

static sqlparser_graph_target_t *sqlparser_graph_target_by_local(
	sqlparser_graph_build_t *build,
	size_t target_index)
{
	if (build == NULL || build->statement == NULL ||
	    target_index >= sqlparser_graph_local_target_count(build)) {
		return NULL;
	}
	return &build->cache->targets[build->statement->target_offset + target_index];
}

static int sqlparser_graph_add_relation(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_graph_relation_kind_t kind,
	const sqlparser_relation_view_t *relation_view,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_relation_t *relation;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->relations,
		    &build->cache->relation_capacity,
		    build->cache->relation_count + 1U,
		    sizeof(*build->cache->relations),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->relation_count++;
	local_index = sqlparser_graph_local_relation_count(build) - 1U;
	relation = &build->cache->relations[global_index];
	memset(relation, 0, sizeof(*relation));
	relation->index = local_index;
	relation->statement_index = build->statement_index;
	relation->block_index = block_index;
	relation->kind = kind;
	if (relation_view != NULL) {
		relation->database_name = relation_view->database_name;
		relation->schema_name = relation_view->schema_name;
		relation->object_name = relation_view->table_name;
		relation->alias_name = relation_view->alias_name;
	}
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_target(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_target_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_target_t *target;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->targets,
		    &build->cache->target_capacity,
		    build->cache->target_count + 1U,
		    sizeof(*build->cache->targets),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->target_count++;
	local_index = sqlparser_graph_local_target_count(build) - 1U;
	target = &build->cache->targets[global_index];
	memset(target, 0, sizeof(*target));
	if (source != NULL) {
		*target = *source;
	}
	target->index = local_index;
	target->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_field(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_field_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t *field;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->fields,
		    &build->cache->field_capacity,
		    build->cache->field_count + 1U,
		    sizeof(*build->cache->fields),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->field_count++;
	local_index = sqlparser_graph_local_field_count(build) - 1U;
	field = &build->cache->fields[global_index];
	memset(field, 0, sizeof(*field));
	if (source != NULL) {
		*field = *source;
	}
	field->index = local_index;
	field->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_selector_equal(
	const sqlparser_selector_t *left,
	const sqlparser_selector_t *right)
{
	return left != NULL &&
		right != NULL &&
		left->kind == right->kind &&
		left->statement_index == right->statement_index &&
		left->item_index == right->item_index &&
		left->row_index == right->row_index &&
		left->column_index == right->column_index;
}

static int sqlparser_graph_add_value(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_value_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t *value;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->values,
		    &build->cache->value_capacity,
		    build->cache->value_count + 1U,
		    sizeof(*build->cache->values),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->value_count++;
	local_index = sqlparser_graph_local_value_count(build) - 1U;
	value = &build->cache->values[global_index];
	memset(value, 0, sizeof(*value));
	if (source != NULL) {
		*value = *source;
	}
	value->index = local_index;
	value->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_target_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__Node *value_node,
	size_t *out_value_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	int value_status;

	if (out_value_index != NULL) {
		*out_value_index = 0U;
	}
	memset(&value, 0, sizeof(value));
	value_status = sqlparser_graph_value_from_node(
		build,
		block_index,
		SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		NULL,
		0U,
		0,
		SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
		value_node,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status == 0) {
		return 0;
	}
	return sqlparser_graph_add_value(build, &value, out_value_index, out_error);
}

static int sqlparser_graph_add_set(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_set_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_set_t *set_item;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->sets,
		    &build->cache->set_capacity,
		    build->cache->set_count + 1U,
		    sizeof(*build->cache->sets),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->set_count++;
	local_index = sqlparser_graph_local_set_count(build) - 1U;
	set_item = &build->cache->sets[global_index];
	memset(set_item, 0, sizeof(*set_item));
	if (source != NULL) {
		*set_item = *source;
	}
	set_item->index = local_index;
	set_item->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static char **sqlparser_graph_column_ref_name_slot(PgQuery__ColumnRef *column_ref)
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

static const char *sqlparser_graph_column_ref_part(PgQuery__ColumnRef *column_ref, size_t reverse_index)
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

static int sqlparser_graph_column_ref_is_pseudo(PgQuery__ColumnRef *column_ref)
{
	const char *name;
	const char *qualifier;

	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	return name != NULL &&
		(qualifier == NULL || qualifier[0] == '\0') &&
		sqlparser_text_equal_ci(name, "rownum");
}

static int sqlparser_graph_relation_matches(
	const sqlparser_graph_relation_t *relation,
	const char *qualifier)
{
	if (relation == NULL || qualifier == NULL || qualifier[0] == '\0') {
		return 0;
	}
	if (relation->alias_name != NULL && strcmp(relation->alias_name, qualifier) == 0) {
		return 1;
	}
	return relation->object_name != NULL && strcmp(relation->object_name, qualifier) == 0;
}

static int sqlparser_graph_resolve_relation(
	sqlparser_graph_build_t *build,
	size_t block_index,
	const char *qualifier,
	size_t *out_relation_index,
	int *out_has_relation,
	sqlparser_index_span_t *out_candidates,
	sqlparser_error_t *out_error)
{
	size_t scope_pos;

	if (out_relation_index != NULL) {
		*out_relation_index = 0U;
	}
	if (out_has_relation != NULL) {
		*out_has_relation = 0;
	}
	if (out_candidates != NULL) {
		memset(out_candidates, 0, sizeof(*out_candidates));
	}
	if (qualifier != NULL && qualifier[0] != '\0') {
		for (scope_pos = build->scope_count; scope_pos > 0U; scope_pos--) {
			size_t scope_block;
			size_t index;

			scope_block = build->scopes[scope_pos - 1U].block_index;
			for (index = 0U; index < sqlparser_graph_local_relation_count(build); index++) {
				sqlparser_graph_relation_t *relation;

				relation = sqlparser_graph_relation_by_local(build, index);
				if (relation != NULL &&
				    relation->block_index == scope_block &&
				    sqlparser_graph_relation_matches(relation, qualifier)) {
					if (out_relation_index != NULL) {
						*out_relation_index = index;
					}
					if (out_has_relation != NULL) {
						*out_has_relation = 1;
					}
					return 0;
				}
			}
		}
		return 0;
	}
	{
		size_t count;
		size_t only_index;
		size_t index;

		count = 0U;
		only_index = 0U;
		for (index = 0U; index < sqlparser_graph_local_relation_count(build); index++) {
			sqlparser_graph_relation_t *relation;

			relation = sqlparser_graph_relation_by_local(build, index);
			if (relation == NULL || relation->block_index != block_index) {
				continue;
			}
			only_index = index;
			count++;
			if (out_candidates != NULL &&
			    sqlparser_graph_span_append_index(build, out_candidates, index, out_error) != 0) {
				return -1;
			}
		}
		if (count == 1U) {
			if (out_relation_index != NULL) {
				*out_relation_index = only_index;
			}
			if (out_has_relation != NULL) {
				*out_has_relation = 1;
			}
			if (out_candidates != NULL) {
				memset(out_candidates, 0, sizeof(*out_candidates));
			}
		}
	}
	return 0;
}

static int sqlparser_graph_push_scope(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__SelectStmt *select_stmt)
{
	if (build == NULL || build->scope_count >= sizeof(build->scopes) / sizeof(build->scopes[0])) {
		return -1;
	}
	build->scopes[build->scope_count].block_index = block_index;
	build->scopes[build->scope_count].select_stmt = select_stmt;
	build->scope_count++;
	return 0;
}

static void sqlparser_graph_pop_scope(sqlparser_graph_build_t *build)
{
	if (build != NULL && build->scope_count > 0U) {
		build->scope_count--;
	}
}

static PgQuery__CommonTableExpr *sqlparser_graph_find_cte(
	sqlparser_graph_build_t *build,
	const char *name)
{
	size_t scope_pos;

	if (build == NULL || name == NULL || name[0] == '\0') {
		return NULL;
	}
	for (scope_pos = build->scope_count; scope_pos > 0U; scope_pos--) {
		PgQuery__CommonTableExpr *cte;

		cte = sqlparser_view_select_find_cte(build->scopes[scope_pos - 1U].select_stmt, name);
		if (cte != NULL) {
			return cte;
		}
	}
	return NULL;
}

static size_t sqlparser_graph_find_relation_selector_index(
	sqlparser_graph_build_t *build,
	PgQuery__RangeVar *range_var)
{
	size_t count;
	size_t index;
	ProtobufCMessage *message;
	sqlparser_error_t error;

	if (build == NULL || build->handle == NULL || range_var == NULL) {
		return (size_t)-1;
	}
	memset(&error, 0, sizeof(error));
	if (sqlparser_search_statement_messages(
		    build->handle,
		    build->statement_index,
		    &pg_query__range_var__descriptor,
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
			    &pg_query__range_var__descriptor,
			    NULL,
			    1,
			    index,
			    NULL,
			    &message,
			    &error) != SQLPARSER_STATUS_OK) {
			return (size_t)-1;
		}
		if ((PgQuery__RangeVar *)message == range_var) {
			return index;
		}
	}
	return (size_t)-1;
}

static size_t sqlparser_graph_target_path_save(const sqlparser_graph_build_t *build)
{
	return build != NULL ? build->target_path_count : 0U;
}

static void sqlparser_graph_target_path_restore(sqlparser_graph_build_t *build, size_t saved_count)
{
	if (build != NULL && saved_count <= build->target_path_count) {
		build->target_path_count = saved_count;
	}
}

static int sqlparser_graph_target_path_push(
	sqlparser_graph_build_t *build,
	const char *kind,
	const char *name,
	size_t arg_index)
{
	sqlparser_target_path_entry_t *entry;

	if (build == NULL || kind == NULL) {
		return 0;
	}
	if (build->target_path_count >= SQLPARSER_TARGET_PATH_CAPACITY) {
		return 0;
	}
	entry = &build->target_path[build->target_path_count++];
	memset(entry, 0, sizeof(*entry));
	entry->kind = kind;
	if (name != NULL && name[0] != '\0') {
		sqlparser_view_copy_public_text(entry->name, sizeof(entry->name), name, &entry->name_truncated);
		entry->has_name = entry->name[0] != '\0';
	}
	entry->arg_index = arg_index;
	return 0;
}

static int sqlparser_graph_walk_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error);

static int sqlparser_graph_walk_expr_with_target_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	const char *kind,
	const char *name,
	size_t arg_index,
	sqlparser_error_t *out_error)
{
	size_t saved_count;
	int rc;

	saved_count = sqlparser_graph_target_path_save(build);
	if (has_target) {
		(void)sqlparser_graph_target_path_push(build, kind, name, arg_index);
	}
	rc = sqlparser_graph_walk_expr(build, block_index, clause, node, target_index, has_target, out_error);
	sqlparser_graph_target_path_restore(build, saved_count);
	return rc;
}

static int sqlparser_graph_add_column_ref_field(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__ColumnRef *column_ref,
	size_t target_index,
	int has_target,
	size_t *out_field_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t field;
	sqlparser_view_build_t view_build;
	const char *column_name;
	const char *qualifier;
	size_t name_index;

	if (out_field_index != NULL) {
		*out_field_index = 0U;
	}
	if (sqlparser_graph_column_ref_is_pseudo(column_ref)) {
		return 0;
	}
	column_name = sqlparser_graph_column_ref_part(column_ref, 0U);
	if (column_name == NULL || column_name[0] == '\0' || strcmp(column_name, "*") == 0) {
		return 0;
	}
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	memset(&field, 0, sizeof(field));
	field.block_index = block_index;
	field.clause = clause;
	field.column_name = column_name;
	field.target_index = target_index;
	field.has_target = has_target;
	if (has_target && build != NULL && build->target_path_count > 0U) {
		field.target_path_count = build->target_path_count;
		if (field.target_path_count > SQLPARSER_TARGET_PATH_CAPACITY) {
			field.target_path_count = SQLPARSER_TARGET_PATH_CAPACITY;
		}
		memcpy(field.target_path, build->target_path, field.target_path_count * sizeof(field.target_path[0]));
	}
	if (sqlparser_graph_resolve_relation(
		    build,
		    block_index,
		    qualifier,
		    &field.relation_index,
		    &field.has_relation,
		    &field.candidate_relations,
		    out_error) != 0) {
		return -1;
	}
	memset(&view_build, 0, sizeof(view_build));
	view_build.handle = build->handle;
	view_build.statement_index = build->statement_index;
	name_index = sqlparser_view_find_name_selector_index(&view_build, sqlparser_graph_column_ref_name_slot(column_ref));
	if (name_index != (size_t)-1) {
		field.selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
		field.selector.statement_index = build->statement_index;
		field.selector.item_index = name_index;
		field.has_selector = 1;
	}
	if (field.has_selector) {
		size_t index;

		for (index = 0U; index < build->cache->field_count; index++) {
			sqlparser_graph_field_t *existing;

			existing = &build->cache->fields[index];
			if (existing->statement_index == build->statement_index &&
			    existing->has_selector &&
			    sqlparser_graph_selector_equal(&existing->selector, &field.selector)) {
				if (out_field_index != NULL) {
					*out_field_index = existing->index;
				}
				return 0;
			}
		}
	}
	return sqlparser_graph_add_field(build, &field, out_field_index, out_error);
}

static int sqlparser_graph_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *value_node,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_view_build_t view_build;
	char *public_sql;
	sqlparser_view_bind_info_t bind_info;
	sqlparser_status_t status;
	size_t value_index;
	int bind_status;

	if (out_value == NULL) {
		return -1;
	}
	memset(out_value, 0, sizeof(*out_value));
	if (value_node == NULL) {
		return 0;
	}
	out_value->block_index = block_index;
	out_value->clause = clause;
	out_value->operator_name = operator_name;
	out_value->field_index = field_index;
	out_value->has_field = has_field;
	out_value->field_match_kind = has_field ? field_match_kind : SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN;
	if (value_node->node_case == PG_QUERY__NODE__NODE_A_CONST && value_node->a_const != NULL) {
		out_value->kind = SQLPARSER_GRAPH_VALUE_LITERAL;
		(void)sqlparser_fill_literal_view_from_a_const_with_sql(
			value_node->a_const,
			build != NULL && build->handle != NULL ? sqlparser_effective_parser_sql(build->handle) : NULL,
			&out_value->literal,
			NULL);
	} else if (value_node->node_case == PG_QUERY__NODE__NODE_PARAM_REF && value_node->param_ref != NULL) {
		out_value->kind = SQLPARSER_GRAPH_VALUE_BIND;
		public_sql = NULL;
		status = sqlparser_view_render_value_node_public_sql(
			build->handle,
			build->statement_index,
			value_node,
			&public_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, status, "failed to render bind SQL");
			}
			return -1;
		}
		memset(&bind_info, 0, sizeof(bind_info));
		bind_status = sqlparser_view_bind_info_from_value(
			    build->handle,
			    public_sql,
			    value_node,
			    &bind_info,
			    out_error);
		if (bind_status < 0) {
			free(public_sql);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "failed to resolve bind info");
			}
			return -1;
		}
		sqlparser_view_copy_public_text(out_value->bind, sizeof(out_value->bind), bind_status > 0 ? bind_info.name : NULL, NULL);
		sqlparser_view_copy_public_text(out_value->bind_sql, sizeof(out_value->bind_sql), public_sql, NULL);
		out_value->has_bind = out_value->bind[0] != '\0';
		out_value->bind_kind = bind_status > 0 ? bind_info.kind : SQLPARSER_BIND_KIND_NONE;
		out_value->bind_position = bind_status > 0 ? bind_info.position : 0U;
		out_value->has_bind_position = out_value->bind_position != 0U;
		out_value->has_bind_sql = out_value->bind_sql[0] != '\0';
		sqlparser_view_bind_info_release(&bind_info);
		free(public_sql);
	} else if (value_node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
		out_value->kind = SQLPARSER_GRAPH_VALUE_DEFAULT;
	} else {
		return 0;
	}
	memset(&view_build, 0, sizeof(view_build));
	view_build.handle = build->handle;
	view_build.statement_index = build->statement_index;
	value_index = sqlparser_view_find_value_index(&view_build, value_node);
	if (value_index != (size_t)-1) {
		out_value->selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
		out_value->selector.statement_index = build->statement_index;
		out_value->selector.item_index = value_index;
		out_value->has_selector = 1;
	}
	return 1;
}

static int sqlparser_graph_walk_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error);

static int sqlparser_graph_node_is_recordable_value(PgQuery__Node *node)
{
	return node != NULL &&
		(node->node_case == PG_QUERY__NODE__NODE_A_CONST ||
		 node->node_case == PG_QUERY__NODE__NODE_PARAM_REF ||
		 node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT);
}

static int sqlparser_graph_node_has_recordable_value(PgQuery__Node *node);

static int sqlparser_graph_node_records_as_expression_value(PgQuery__Node *node)
{
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_A_EXPR:
		case PG_QUERY__NODE__NODE_TYPE_CAST:
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
		case PG_QUERY__NODE__NODE_FUNC_CALL:
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
		case PG_QUERY__NODE__NODE_NULL_TEST:
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
		case PG_QUERY__NODE__NODE_CASE_EXPR:
		case PG_QUERY__NODE__NODE_CASE_WHEN:
		case PG_QUERY__NODE__NODE_ROW_EXPR:
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return 1;
		default:
			return 0;
	}
}

static int sqlparser_graph_node_array_has_recordable_value(PgQuery__Node **items, size_t count)
{
	size_t index;

	if (items == NULL) {
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_graph_node_has_recordable_value(items[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_graph_node_has_recordable_value(PgQuery__Node *node)
{
	if (sqlparser_graph_node_is_recordable_value(node)) {
		return 1;
	}
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_graph_node_array_has_recordable_value(node->list->items, node->list->n_items) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->a_array_expr->elements,
					node->a_array_expr->n_elements) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->array_expr->elements,
					node->array_expr->n_elements) :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->row_expr->args,
					node->row_expr->n_args) :
				0;
		case PG_QUERY__NODE__NODE_A_EXPR:
			return node->a_expr != NULL &&
				(sqlparser_graph_node_has_recordable_value(node->a_expr->lexpr) ||
				 sqlparser_graph_node_has_recordable_value(node->a_expr->rexpr));
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_node_has_recordable_value(node->type_cast->arg) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_node_has_recordable_value(node->collate_clause->arg) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			return node->func_call != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->func_call->args,
					node->func_call->n_args) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->coalesce_expr->args,
					node->coalesce_expr->n_args) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_node_array_has_recordable_value(
					node->min_max_expr->args,
					node->min_max_expr->n_args) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_node_has_recordable_value(node->null_test->arg) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_node_has_recordable_value(node->boolean_test->arg) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			return node->case_expr != NULL &&
				(sqlparser_graph_node_has_recordable_value(node->case_expr->arg) ||
				 sqlparser_graph_node_array_has_recordable_value(node->case_expr->args, node->case_expr->n_args) ||
				 sqlparser_graph_node_has_recordable_value(node->case_expr->defresult));
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			return node->case_when != NULL &&
				(sqlparser_graph_node_has_recordable_value(node->case_when->expr) ||
				 sqlparser_graph_node_has_recordable_value(node->case_when->result));
		default:
			return 0;
	}
}

static sqlparser_graph_field_match_kind_t sqlparser_graph_field_match_kind_from_expr(PgQuery__Node *node)
{
	if (node == NULL) {
		return SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN;
	}
	return node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF ?
		SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD :
		SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD;
}

static int sqlparser_graph_a_expr_is_select_predicate(const PgQuery__AExpr *a_expr)
{
	const char *operator_name;

	if (a_expr == NULL) {
		return 0;
	}

	switch (a_expr->kind) {
		case PG_QUERY__A__EXPR__KIND__AEXPR_IN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_LIKE:
		case PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE:
		case PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR:
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN_SYM:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN_SYM:
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP_ANY:
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP_ALL:
		case PG_QUERY__A__EXPR__KIND__AEXPR_DISTINCT:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_DISTINCT:
			return 1;
		case PG_QUERY__A__EXPR__KIND__AEXPR_OP:
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_NULLIF:
		case PG_QUERY__A__EXPR__KIND__A_EXPR_KIND_UNDEFINED:
		default:
			return 0;
	}

	operator_name = sqlparser_a_expr_operator_name(a_expr);
	return operator_name != NULL &&
		(strcmp(operator_name, "=") == 0 ||
		 strcmp(operator_name, "<>") == 0 ||
		 strcmp(operator_name, "!=") == 0 ||
		 strcmp(operator_name, "<") == 0 ||
		 strcmp(operator_name, "<=") == 0 ||
		 strcmp(operator_name, ">") == 0 ||
		 strcmp(operator_name, ">=") == 0);
}

static int sqlparser_graph_clause_records_field_values(
	sqlparser_clause_kind_t clause,
	const PgQuery__AExpr *a_expr)
{
	return clause == SQLPARSER_CLAUSE_KIND_SELECT_LIST ? sqlparser_graph_a_expr_is_select_predicate(a_expr) :
		clause == SQLPARSER_CLAUSE_KIND_WHERE ||
		clause == SQLPARSER_CLAUSE_KIND_ON ||
		clause == SQLPARSER_CLAUSE_KIND_HAVING;
}

static int sqlparser_graph_record_value_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	int value_status;

	value_status = sqlparser_graph_value_from_node(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		has_field,
		field_match_kind,
		node,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status > 0 &&
	    sqlparser_graph_add_value(build, &value, NULL, out_error) != 0) {
		return -1;
	}
	return value_status > 0 ? 1 : 0;
}

static int sqlparser_graph_record_expression_value_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;

	if (!has_field) {
		return 0;
	}
	memset(&value, 0, sizeof(value));
	value.block_index = block_index;
	value.clause = clause;
	value.operator_name = operator_name;
	value.field_index = field_index;
	value.has_field = 1;
	value.field_match_kind = field_match_kind;
	value.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	return sqlparser_graph_add_value(build, &value, NULL, out_error) == 0 ? 1 : -1;
}

static int sqlparser_graph_record_value_nodes(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *node,
	sqlparser_error_t *out_error);

static int sqlparser_graph_record_value_node_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node **items,
	size_t count,
	sqlparser_error_t *out_error)
{
	size_t index;
	int value_count;

	if (items == NULL) {
		return 0;
	}
	value_count = 0;
	for (index = 0U; index < count; index++) {
		int item_status;

		item_status = sqlparser_graph_record_value_nodes(
			build,
			block_index,
			clause,
			operator_name,
			field_index,
			has_field,
			field_match_kind,
			items[index],
			out_error);
		if (item_status < 0) {
			return -1;
		}
		value_count += item_status;
	}
	return value_count > 0 ? 1 : 0;
}

static int sqlparser_graph_record_value_nodes(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	if (node == NULL) {
		return 0;
	}
	if (has_field && sqlparser_graph_node_records_as_expression_value(node)) {
		return sqlparser_graph_record_expression_value_node(
			build,
			block_index,
			clause,
			operator_name,
			field_index,
			has_field,
			field_match_kind,
			out_error);
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->list->items,
					node->list->n_items,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->array_expr->elements,
					node->array_expr->n_elements,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->row_expr->args,
					node->row_expr->n_args,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_EXPR:
		{
			int left_status;
			int right_status;

			if (node->a_expr == NULL) {
				return 0;
			}
			left_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->a_expr->lexpr,
				out_error);
			if (left_status < 0) {
				return -1;
			}
			right_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->a_expr->rexpr,
				out_error);
			if (right_status < 0) {
				return -1;
			}
			return left_status > 0 || right_status > 0 ? 1 : 0;
		}
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->type_cast->arg,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->collate_clause->arg,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			return node->func_call != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->func_call->args,
					node->func_call->n_args,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_record_value_node_array(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->null_test->arg,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_record_value_nodes(
					build,
					block_index,
					clause,
					operator_name,
					field_index,
					has_field,
					field_match_kind,
					node->boolean_test->arg,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
		{
			int arg_status;
			int args_status;
			int def_status;

			if (node->case_expr == NULL) {
				return 0;
			}
			arg_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_expr->arg,
				out_error);
			if (arg_status < 0) {
				return -1;
			}
			args_status = sqlparser_graph_record_value_node_array(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_expr->args,
				node->case_expr->n_args,
				out_error);
			if (args_status < 0) {
				return -1;
			}
			def_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_expr->defresult,
				out_error);
			if (def_status < 0) {
				return -1;
			}
			return arg_status > 0 || args_status > 0 || def_status > 0 ? 1 : 0;
		}
		case PG_QUERY__NODE__NODE_CASE_WHEN:
		{
			int expr_status;
			int result_status;

			if (node->case_when == NULL) {
				return 0;
			}
			expr_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_when->expr,
				out_error);
			if (expr_status < 0) {
				return -1;
			}
			result_status = sqlparser_graph_record_value_nodes(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node->case_when->result,
				out_error);
			if (result_status < 0) {
				return -1;
			}
			return expr_status > 0 || result_status > 0 ? 1 : 0;
		}
		default:
			return sqlparser_graph_record_value_node(
				build,
				block_index,
				clause,
				operator_name,
				field_index,
				has_field,
				field_match_kind,
				node,
				out_error);
	}
}

static int sqlparser_graph_record_column_ref_match(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__ColumnRef *column_ref,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	sqlparser_error_t *out_error)
{
	size_t field_index;
	size_t field_count_before;

	field_index = 0U;
	field_count_before = sqlparser_graph_local_field_count(build);
	if (sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    column_ref,
		    target_index,
		    has_target,
		    &field_index,
		    out_error) != 0) {
		return -1;
	}
	if (sqlparser_graph_local_field_count(build) == field_count_before) {
		return 0;
	}
	return sqlparser_graph_record_value_nodes(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		1,
		field_match_kind,
		value_node,
		out_error);
}

static int sqlparser_graph_record_column_ref_matches_in_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node **items,
	size_t count,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	sqlparser_error_t *out_error);

static int sqlparser_graph_record_column_ref_matches(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node *node,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	sqlparser_error_t *out_error)
{
	int left_status;
	int right_status;

	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_COLUMN_REF:
			return sqlparser_graph_record_column_ref_match(
				build,
				block_index,
				clause,
				operator_name,
				node->column_ref,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
			if (node->a_expr == NULL) {
				return 0;
			}
			left_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->a_expr->lexpr,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
			if (left_status < 0) {
				return -1;
			}
			right_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->a_expr->rexpr,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
			return right_status < 0 ? -1 : (left_status > 0 || right_status > 0 ? 1 : 0);
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return node->bool_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->bool_expr->args,
					node->bool_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			return node->func_call != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->func_call->args,
					node->func_call->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->type_cast->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->collate_clause->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->array_expr->elements,
					node->array_expr->n_elements,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->null_test->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->boolean_test->arg,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr == NULL) {
				return 0;
			}
			left_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->case_expr->arg,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
			if (left_status < 0) {
				return -1;
			}
			right_status = sqlparser_graph_record_column_ref_matches_in_array(
				build,
				block_index,
				clause,
				operator_name,
				node->case_expr->args,
				node->case_expr->n_args,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
			if (right_status < 0) {
				return -1;
			}
			left_status = left_status > 0 || right_status > 0 ? 1 : 0;
			right_status = sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->case_expr->defresult,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
			return right_status < 0 ? -1 : (left_status > 0 || right_status > 0 ? 1 : 0);
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when == NULL) {
				return 0;
			}
			return sqlparser_graph_record_column_ref_matches(
				build,
				block_index,
				clause,
				operator_name,
				node->case_when->result,
				value_node,
				target_index,
				has_target,
				field_match_kind,
				out_error);
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->row_expr->args,
					node->row_expr->n_args,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_SORT_BY:
			return node->sort_by != NULL ?
				sqlparser_graph_record_column_ref_matches(
					build,
					block_index,
					clause,
					operator_name,
					node->sort_by->node,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_LIST:
			return node->list != NULL ?
				sqlparser_graph_record_column_ref_matches_in_array(
					build,
					block_index,
					clause,
					operator_name,
					node->list->items,
					node->list->n_items,
					value_node,
					target_index,
					has_target,
					field_match_kind,
					out_error) :
				0;
		default:
			return 0;
	}
}

static int sqlparser_graph_record_column_ref_matches_in_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node **items,
	size_t count,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	sqlparser_error_t *out_error)
{
	size_t index;
	int matched;

	if (items == NULL) {
		return 0;
	}
	matched = 0;
	for (index = 0U; index < count; index++) {
		int item_status;

		item_status = sqlparser_graph_record_column_ref_matches(
			build,
			block_index,
			clause,
			operator_name,
			items[index],
			value_node,
			target_index,
			has_target,
			field_match_kind,
			out_error);
		if (item_status < 0) {
			return -1;
		}
		if (item_status > 0) {
			matched = 1;
		}
	}
	return matched;
}

static int sqlparser_graph_record_predicate_field_values(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node *field_node,
	PgQuery__Node *value_node,
	size_t target_index,
	int has_target,
	sqlparser_graph_field_match_kind_t field_match_kind,
	sqlparser_error_t *out_error)
{
	return sqlparser_graph_record_column_ref_matches(
		build,
		block_index,
		clause,
			operator_name,
			field_node,
			value_node,
			target_index,
			has_target,
			field_match_kind,
			out_error);
}

static int sqlparser_graph_record_predicate_value(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__Node *left,
	PgQuery__Node *right,
	size_t target_index,
	int has_target,
	const PgQuery__AExpr *a_expr,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_match_kind_t field_match_kind;
	int value_status;

	if (!sqlparser_graph_clause_records_field_values(clause, a_expr) ||
	    left == NULL ||
	    right == NULL ||
	    !sqlparser_graph_node_has_recordable_value(right)) {
		return 0;
	}
	field_match_kind = sqlparser_graph_field_match_kind_from_expr(left);
	value_status = sqlparser_graph_record_predicate_field_values(
		build,
		block_index,
		clause,
		operator_name,
		left,
		right,
		target_index,
		has_target,
		field_match_kind,
		out_error);
	return value_status;
}

static int sqlparser_graph_walk_node_array(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node **items,
	size_t count,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_graph_walk_expr(build, block_index, clause, items[index], 0U, 0, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_walk_node_array_with_target_path(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node **items,
	size_t count,
	size_t target_index,
	int has_target,
	const char *kind,
	const char *name,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		if (sqlparser_graph_walk_expr_with_target_path(
			    build,
			    block_index,
			    clause,
			    items[index],
			    target_index,
			    has_target,
			    kind,
			    name,
			    index,
			    out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_build_select(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_graph_block_kind_t kind,
	size_t *out_block_index,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_sublink(
	sqlparser_graph_build_t *build,
	PgQuery__SubLink *sub_link,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	if (out_block_index != NULL) {
		*out_block_index = 0U;
	}
	if (sub_link == NULL ||
	    sub_link->subselect == NULL ||
	    sub_link->subselect->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    sub_link->subselect->select_stmt == NULL) {
		return 0;
	}
	return sqlparser_graph_build_select(
		build,
		sub_link->subselect->select_stmt,
		SQLPARSER_GRAPH_BLOCK_SCALAR_SUBQUERY,
		out_block_index,
		out_error);
}

static int sqlparser_graph_walk_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t target_index,
	int has_target,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_RES_TARGET:
			return node->res_target != NULL ?
				sqlparser_graph_walk_expr(build, block_index, clause, node->res_target->val, target_index, has_target, out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLUMN_REF:
			return sqlparser_graph_add_column_ref_field(
				build,
				block_index,
				clause,
				node->column_ref,
				target_index,
				has_target,
				NULL,
				out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
		{
			const char *operator_name;
			int left_value_status;
			int right_value_status;
			size_t saved_count;

			if (node->a_expr == NULL) {
				return 0;
			}
			operator_name = sqlparser_a_expr_operator_name(node->a_expr);
			saved_count = sqlparser_graph_target_path_save(build);
			if (has_target) {
				(void)sqlparser_graph_target_path_push(build, "expression", operator_name, 0U);
			}
			left_value_status = sqlparser_graph_record_predicate_value(
				    build,
				    block_index,
				    clause,
				    operator_name,
				    node->a_expr->lexpr,
				    node->a_expr->rexpr,
				    target_index,
				    has_target,
				    node->a_expr,
				    out_error);
			sqlparser_graph_target_path_restore(build, saved_count);
			if (left_value_status < 0) {
				return -1;
			}
			saved_count = sqlparser_graph_target_path_save(build);
			if (has_target) {
				(void)sqlparser_graph_target_path_push(build, "expression", operator_name, 1U);
			}
				right_value_status = sqlparser_graph_record_predicate_value(
					    build,
					    block_index,
					    clause,
				    operator_name,
				    node->a_expr->rexpr,
				    node->a_expr->lexpr,
				    target_index,
				    has_target,
					    node->a_expr,
					    out_error);
				sqlparser_graph_target_path_restore(build, saved_count);
				if (right_value_status < 0) {
					return -1;
				}
				if (left_value_status > 0 || right_value_status > 0) {
					if (sqlparser_graph_walk_expr_with_target_path(
						    build,
						    block_index,
						    clause,
						    node->a_expr->lexpr,
						    target_index,
						    has_target,
						    "expression",
						    operator_name,
						    0U,
						    out_error) != 0) {
						return -1;
					}
					return sqlparser_graph_walk_expr_with_target_path(
						build,
						block_index,
						clause,
						node->a_expr->rexpr,
						target_index,
						has_target,
						"expression",
						operator_name,
						1U,
						out_error);
				}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->a_expr->lexpr,
				    target_index,
				    has_target,
				    "expression",
				    operator_name,
				    0U,
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr_with_target_path(
				build,
				block_index,
				clause,
				node->a_expr->rexpr,
				target_index,
				has_target,
				"expression",
				operator_name,
				1U,
				out_error);
		}
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return node->bool_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->bool_expr->args,
					node->bool_expr->n_args,
					target_index,
					has_target,
					"expression",
					sqlparser_view_bool_expr_name(node->bool_expr),
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call != NULL) {
				char *func_name;
				int rc;

				if (sqlparser_view_func_call_is_name(node->func_call, SQLPARSER_INTERNAL_MYSQL_JOIN_ON)) {
					return sqlparser_graph_walk_node_array_with_target_path(
						build,
						block_index,
						SQLPARSER_CLAUSE_KIND_ON,
						node->func_call->args,
						node->func_call->n_args,
						target_index,
						has_target,
						NULL,
						NULL,
						out_error);
				}
				func_name = sqlparser_view_func_call_name_dup(node->func_call);
				rc = sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->func_call->args,
					node->func_call->n_args,
					target_index,
					has_target,
					"function",
					func_name,
					out_error);
				if (rc == 0 && node->func_call->over != NULL) {
					rc = sqlparser_graph_walk_node_array_with_target_path(
						     build,
						     block_index,
						     clause,
						     node->func_call->over->partition_clause,
						     node->func_call->over->n_partition_clause,
						     target_index,
						     has_target,
						     "expression",
						     "window_partition",
						     out_error) != 0 ||
						     sqlparser_graph_walk_node_array_with_target_path(
							     build,
							     block_index,
							     clause,
							     node->func_call->over->order_clause,
							     node->func_call->over->n_order_clause,
							     target_index,
							     has_target,
							     "expression",
							     "window_order",
							     out_error) != 0 ?
						-1 :
						0;
				}
				free(func_name);
				return rc;
			}
			return 0;
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->type_cast->arg,
					target_index,
					has_target,
					"function",
					"CAST",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			return node->collate_clause != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->collate_clause->arg,
					target_index,
					has_target,
					"expression",
					"collate",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					target_index,
					has_target,
					"expression",
					"array",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->array_expr->elements,
					node->array_expr->n_elements,
					target_index,
					has_target,
					"expression",
					"array",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					target_index,
					has_target,
					"function",
					"COALESCE",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			return node->null_test != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->null_test->arg,
					target_index,
					has_target,
					"expression",
					"is_null",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			return node->boolean_test != NULL ?
				sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->boolean_test->arg,
					target_index,
					has_target,
					"expression",
					"boolean_test",
					0U,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr == NULL) {
				return 0;
			}
			if (sqlparser_graph_walk_expr_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->case_expr->arg,
				    target_index,
				    has_target,
				    "expression",
				    "case_when",
				    0U,
				    out_error) != 0 ||
			    sqlparser_graph_walk_node_array_with_target_path(
				    build,
				    block_index,
				    clause,
				    node->case_expr->args,
				    node->case_expr->n_args,
				    target_index,
				    has_target,
				    "expression",
				    "case_when",
				    out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr_with_target_path(
				build,
				block_index,
				clause,
				node->case_expr->defresult,
				target_index,
				has_target,
				"expression",
				"case_when",
				node->case_expr->n_args,
				out_error);
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when == NULL) {
				return 0;
			}
			return sqlparser_graph_walk_expr_with_target_path(
					build,
					block_index,
					clause,
					node->case_when->expr,
					target_index,
					has_target,
					"expression",
					"case_when",
					0U,
					out_error) != 0 ||
					sqlparser_graph_walk_expr_with_target_path(
						build,
						block_index,
						clause,
						node->case_when->result,
						target_index,
						has_target,
						"expression",
						"case_when",
						1U,
						out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->row_expr->args,
					node->row_expr->n_args,
					target_index,
					has_target,
					"expression",
					"row",
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_graph_walk_node_array_with_target_path(
					build,
					block_index,
					clause,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					target_index,
					has_target,
					"function",
					sqlparser_view_min_max_name(node->min_max_expr),
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_SUB_LINK:
			return node->sub_link != NULL ?
				sqlparser_graph_build_sublink(build, node->sub_link, NULL, out_error) :
				0;
		case PG_QUERY__NODE__NODE_SORT_BY:
			return node->sort_by != NULL ?
				sqlparser_graph_walk_expr(build, block_index, clause, node->sort_by->node, target_index, has_target, out_error) :
				0;
		case PG_QUERY__NODE__NODE_LIST:
			if (node->list == NULL) {
				return 0;
			}
			for (index = 0U; index < node->list->n_items; index++) {
				if (sqlparser_graph_walk_expr(build, block_index, clause, node->list->items[index], target_index, has_target, out_error) != 0) {
					return -1;
				}
			}
			return 0;
		default:
			return 0;
	}
}

static int sqlparser_graph_add_star_relations(
	sqlparser_graph_build_t *build,
	size_t block_index,
	const char *qualifier,
	sqlparser_index_span_t *out_span,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (out_span == NULL) {
		return -1;
	}
	memset(out_span, 0, sizeof(*out_span));
	for (index = 0U; index < sqlparser_graph_local_relation_count(build); index++) {
		sqlparser_graph_relation_t *relation;

		relation = sqlparser_graph_relation_by_local(build, index);
		if (relation == NULL || relation->block_index != block_index) {
			continue;
		}
		if (qualifier != NULL && qualifier[0] != '\0' && !sqlparser_graph_relation_matches(relation, qualifier)) {
			continue;
		}
		if (sqlparser_graph_span_append_index(build, out_span, index, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

static void sqlparser_graph_set_star_target_source_block(
	sqlparser_graph_build_t *build,
	sqlparser_graph_target_t *target)
{
	size_t relation_index;
	sqlparser_graph_relation_t *relation;

	if (build == NULL || build->cache == NULL || target == NULL ||
	    target->star_relations.count != 1U ||
	    target->star_relations.offset >= build->cache->index_pool_count) {
		return;
	}
	relation_index = build->cache->index_pool[target->star_relations.offset];
	relation = sqlparser_graph_relation_by_local(build, relation_index);
	if (relation == NULL || !relation->has_source_block) {
		return;
	}
	target->source_block_index = relation->source_block_index;
	target->has_source_block = 1;
}

static int sqlparser_graph_build_target(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	size_t block_index,
	size_t target_list_index,
	size_t ordinal,
	PgQuery__ResTarget *res_target,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_target_t target;
	PgQuery__Node *expr;
	const char *name;
	const char *qualifier;
	size_t target_index;

	(void)stmt;
	memset(&target, 0, sizeof(target));
	target.block_index = block_index;
	target.ordinal = ordinal;
	target.output_name = res_target != NULL && res_target->name != NULL && res_target->name[0] != '\0' ? res_target->name : NULL;
	target.target_list_selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGETS;
	target.target_list_selector.statement_index = build->statement_index;
	target.target_list_selector.item_index = target_list_index;
	target.has_target_list_selector = 1;
	target.selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
	target.selector.statement_index = build->statement_index;
	target.selector.item_index = target_list_index;
	target.selector.column_index = ordinal;
	target.has_selector = 1;
	expr = res_target != NULL ? res_target->val : NULL;
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF && expr->column_ref != NULL) {
		name = sqlparser_graph_column_ref_part(expr->column_ref, 0U);
		qualifier = sqlparser_graph_column_ref_part(expr->column_ref, 1U);
		if (name != NULL && strcmp(name, "*") == 0) {
			target.kind = qualifier != NULL && qualifier[0] != '\0' ?
				SQLPARSER_GRAPH_TARGET_QUALIFIED_STAR :
				SQLPARSER_GRAPH_TARGET_STAR;
			if (sqlparser_graph_add_star_relations(build, block_index, qualifier, &target.star_relations, out_error) != 0) {
				return -1;
			}
			sqlparser_graph_set_star_target_source_block(build, &target);
			return sqlparser_graph_add_target(build, &target, NULL, out_error);
		}
		if (sqlparser_graph_column_ref_is_pseudo(expr->column_ref)) {
			target.kind = SQLPARSER_GRAPH_TARGET_PSEUDO;
			if (target.output_name == NULL) {
				target.output_name = name;
			}
			return sqlparser_graph_add_target(build, &target, NULL, out_error);
		}
		target_index = 0U;
		target.kind = SQLPARSER_GRAPH_TARGET_FIELD;
		if (target.output_name == NULL) {
			target.output_name = name;
		}
		if (sqlparser_graph_add_target(build, &target, &target_index, out_error) != 0) {
			return -1;
		}
		if (sqlparser_graph_add_column_ref_field(
			    build,
			    block_index,
			    SQLPARSER_CLAUSE_KIND_SELECT_LIST,
			    expr->column_ref,
			    target_index,
			    1,
			    &target.field_index,
			    out_error) != 0) {
			return -1;
		}
		target = *sqlparser_graph_target_by_local(build, target_index);
		target.field_index = sqlparser_graph_local_field_count(build) - 1U;
		target.has_field = 1;
		*sqlparser_graph_target_by_local(build, target_index) = target;
		return 0;
	}
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_A_CONST) {
		target.kind = SQLPARSER_GRAPH_TARGET_LITERAL;
		if (sqlparser_graph_add_target_value_from_node(
			    build,
			    block_index,
			    expr,
			    &target.value_index,
			    out_error) != 0) {
			return -1;
		}
		target.has_value = 1;
		return sqlparser_graph_add_target(build, &target, NULL, out_error);
	}
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_PARAM_REF) {
		target.kind = SQLPARSER_GRAPH_TARGET_BIND;
		if (sqlparser_graph_add_target_value_from_node(
			    build,
			    block_index,
			    expr,
			    &target.value_index,
			    out_error) != 0) {
			return -1;
		}
		target.has_value = 1;
		return sqlparser_graph_add_target(build, &target, NULL, out_error);
	}
	if (expr != NULL && expr->node_case == PG_QUERY__NODE__NODE_SUB_LINK && expr->sub_link != NULL) {
		target.kind = SQLPARSER_GRAPH_TARGET_SUBQUERY;
		if (sqlparser_graph_build_sublink(build, expr->sub_link, &target.source_block_index, out_error) != 0) {
			return -1;
		}
		target.has_source_block = 1;
		return sqlparser_graph_add_target(build, &target, NULL, out_error);
	}
	target.kind = SQLPARSER_GRAPH_TARGET_EXPRESSION;
	if (sqlparser_graph_add_target(build, &target, &target_index, out_error) != 0) {
		return -1;
	}
	return sqlparser_graph_walk_expr(
		build,
		block_index,
		SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		expr,
		target_index,
		1,
		out_error);
}

static int sqlparser_graph_build_from_item(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	sqlparser_relation_view_t relation_view;
	size_t relation_index;
	size_t source_block_index;

	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_RANGE_VAR:
			{
				size_t selector_index;
				size_t added_relation;
				size_t relation_source_block;
				PgQuery__CommonTableExpr *cte;
				sqlparser_graph_relation_kind_t relation_kind;

				if (node->range_var == NULL) {
					return 0;
				}
				sqlparser_fill_relation_view(node->range_var, &relation_view);
				cte = sqlparser_graph_find_cte(build, relation_view.table_name);
				relation_kind = cte != NULL ?
					SQLPARSER_GRAPH_REL_CTE :
					(relation_view.table_name != NULL && strcmp(relation_view.table_name, "dual") == 0 ?
						SQLPARSER_GRAPH_REL_DUAL :
						SQLPARSER_GRAPH_REL_BASE);
				if (sqlparser_graph_add_relation(
					build,
					block_index,
					relation_kind,
					&relation_view,
					&added_relation,
					out_error) != 0) {
					return -1;
				}
			selector_index = sqlparser_graph_find_relation_selector_index(build, node->range_var);
			if (selector_index != (size_t)-1) {
				sqlparser_graph_relation_t *relation;

				relation = sqlparser_graph_relation_by_local(build, added_relation);
				if (relation != NULL) {
					relation->selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
					relation->selector.statement_index = build->statement_index;
					relation->selector.item_index = selector_index;
						relation->has_selector = 1;
					}
				}
				if (cte != NULL &&
				    cte->ctequery != NULL &&
				    cte->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
				    sqlparser_graph_build_select(
					    build,
					    cte->ctequery->select_stmt,
					    SQLPARSER_GRAPH_BLOCK_CTE,
					    &relation_source_block,
					    out_error) != 0) {
					return -1;
				}
				if (cte != NULL &&
				    cte->ctequery != NULL &&
				    cte->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
					sqlparser_graph_relation_t *relation;

					relation = sqlparser_graph_relation_by_local(build, added_relation);
					if (relation != NULL) {
						relation->source_block_index = relation_source_block;
						relation->has_source_block = 1;
					}
				}
				return 0;
			}
		case PG_QUERY__NODE__NODE_RANGE_SUBSELECT:
			if (node->range_subselect == NULL ||
			    node->range_subselect->subquery == NULL ||
			    node->range_subselect->subquery->node_case != PG_QUERY__NODE__NODE_SELECT_STMT) {
				return 0;
			}
			memset(&relation_view, 0, sizeof(relation_view));
			relation_view.alias_name =
				node->range_subselect->alias != NULL &&
						node->range_subselect->alias->aliasname != NULL &&
						node->range_subselect->alias->aliasname[0] != '\0'
					? node->range_subselect->alias->aliasname
					: NULL;
			if (sqlparser_graph_add_relation(
				    build,
				    block_index,
				    SQLPARSER_GRAPH_REL_DERIVED,
				    &relation_view,
				    &relation_index,
				    out_error) != 0 ||
			    sqlparser_graph_build_select(
				    build,
				    node->range_subselect->subquery->select_stmt,
				    SQLPARSER_GRAPH_BLOCK_SELECT,
				    &source_block_index,
				    out_error) != 0) {
				return -1;
			}
			sqlparser_graph_relation_by_local(build, relation_index)->source_block_index = source_block_index;
			sqlparser_graph_relation_by_local(build, relation_index)->has_source_block = 1;
			return 0;
		case PG_QUERY__NODE__NODE_JOIN_EXPR:
			if (node->join_expr == NULL) {
				return 0;
			}
			if (sqlparser_graph_build_from_item(build, block_index, node->join_expr->larg, out_error) != 0 ||
			    sqlparser_graph_build_from_item(build, block_index, node->join_expr->rarg, out_error) != 0) {
				return -1;
			}
			return sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_ON, node->join_expr->quals, 0U, 0, out_error);
		default:
			return 0;
	}
}

static sqlparser_graph_set_kind_t sqlparser_graph_set_kind_from_select(PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL) {
		return SQLPARSER_GRAPH_SET_UNION;
	}
	switch (stmt->op) {
		case PG_QUERY__SET_OPERATION__SETOP_INTERSECT:
			return SQLPARSER_GRAPH_SET_INTERSECT;
		case PG_QUERY__SET_OPERATION__SETOP_EXCEPT:
			return SQLPARSER_GRAPH_SET_EXCEPT;
		case PG_QUERY__SET_OPERATION__SETOP_UNION:
		default:
			return stmt->all ? SQLPARSER_GRAPH_SET_UNION_ALL : SQLPARSER_GRAPH_SET_UNION;
	}
}

static int sqlparser_graph_build_select(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_graph_block_kind_t kind,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	size_t block_index;
	size_t index;
	size_t target_list_index;

	if (out_block_index != NULL) {
		*out_block_index = 0U;
	}
	if (stmt == NULL) {
		return 0;
	}
	if (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	    stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
	    (stmt->larg != NULL || stmt->rarg != NULL)) {
		sqlparser_graph_set_t set_item;
		size_t left_block;
		size_t right_block;

		left_block = 0U;
		right_block = 0U;
		if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SET, &block_index, out_error) != 0) {
			return -1;
		}
		if (out_block_index != NULL) {
			*out_block_index = block_index;
		}
		memset(&set_item, 0, sizeof(set_item));
		set_item.kind = sqlparser_graph_set_kind_from_select(stmt);
		set_item.result_block_index = block_index;
		if (stmt->larg != NULL &&
		    sqlparser_graph_build_select(build, stmt->larg, SQLPARSER_GRAPH_BLOCK_SELECT, &left_block, out_error) != 0) {
			return -1;
		}
		if (stmt->rarg != NULL &&
		    sqlparser_graph_build_select(build, stmt->rarg, SQLPARSER_GRAPH_BLOCK_SELECT, &right_block, out_error) != 0) {
			return -1;
		}
		if (stmt->larg != NULL &&
		    sqlparser_graph_span_append_index(build, &set_item.branch_blocks, left_block, out_error) != 0) {
			return -1;
		}
		if (stmt->rarg != NULL &&
		    sqlparser_graph_span_append_index(build, &set_item.branch_blocks, right_block, out_error) != 0) {
			return -1;
		}
		if (sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_ORDER_BY, stmt->sort_clause, stmt->n_sort_clause, out_error) != 0) {
			return -1;
		}
		return sqlparser_graph_add_set(build, &set_item, NULL, out_error);
	}
	if (sqlparser_graph_add_block(build, kind, &block_index, out_error) != 0) {
		return -1;
	}
	if (out_block_index != NULL) {
		*out_block_index = block_index;
	}
		if (sqlparser_graph_push_scope(build, block_index, stmt) != 0) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph nesting is too deep");
			return -1;
		}
	for (index = 0U; index < stmt->n_from_clause; index++) {
		if (sqlparser_graph_build_from_item(build, block_index, stmt->from_clause[index], out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	target_list_index = 0U;
	(void)sqlparser_find_select_target_list_index_by_stmt(
		build->handle,
		build->statement_index,
		stmt,
		&target_list_index,
		NULL);
	for (index = 0U; index < stmt->n_target_list; index++) {
		PgQuery__Node *target_node;

		target_node = stmt->target_list[index];
		if (target_node != NULL &&
		    target_node->node_case == PG_QUERY__NODE__NODE_RES_TARGET &&
		    sqlparser_graph_build_target(
			    build,
			    stmt,
			    block_index,
			    target_list_index,
			    index,
			    target_node->res_target,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, 0U, 0, out_error) != 0 ||
	    sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_GROUP_BY, stmt->group_clause, stmt->n_group_clause, out_error) != 0 ||
	    sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_HAVING, stmt->having_clause, 0U, 0, out_error) != 0 ||
	    sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_ORDER_BY, stmt->sort_clause, stmt->n_sort_clause, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_finalize_statement_spans(sqlparser_graph_build_t *build, sqlparser_error_t *out_error)
{
	size_t block_index;

	if (build == NULL || build->statement == NULL) {
		return -1;
	}
	build->statement->block_count = build->cache->block_count - build->statement->block_offset;
	build->statement->relation_count = build->cache->relation_count - build->statement->relation_offset;
	build->statement->target_count = build->cache->target_count - build->statement->target_offset;
	build->statement->field_count = build->cache->field_count - build->statement->field_offset;
	build->statement->value_count = build->cache->value_count - build->statement->value_offset;
	build->statement->set_count = build->cache->set_count - build->statement->set_offset;
	build->statement->dml_count = build->cache->dml_count - build->statement->dml_offset;
	build->statement->dml_branch_count = build->cache->dml_branch_count - build->statement->dml_branch_offset;
	build->statement->dml_column_count = build->cache->dml_column_count - build->statement->dml_column_offset;
	build->statement->dml_cell_count = build->cache->dml_cell_count - build->statement->dml_cell_offset;
	build->statement->dml_assignment_count = build->cache->dml_assignment_count - build->statement->dml_assignment_offset;
	for (block_index = 0U; block_index < build->statement->block_count; block_index++) {
		sqlparser_graph_block_t *block;
		size_t index;

		block = sqlparser_graph_block_by_local(build, block_index);
		if (block == NULL) {
			continue;
		}
		memset(&block->relations, 0, sizeof(block->relations));
		for (index = 0U; index < build->statement->relation_count; index++) {
			sqlparser_graph_relation_t *relation;

			relation = sqlparser_graph_relation_by_local(build, index);
			if (relation != NULL &&
			    relation->block_index == block_index &&
			    sqlparser_graph_span_append_index(build, &block->relations, index, out_error) != 0) {
				return -1;
			}
		}
		memset(&block->targets, 0, sizeof(block->targets));
		for (index = 0U; index < build->statement->target_count; index++) {
			sqlparser_graph_target_t *target;

			target = sqlparser_graph_target_by_local(build, index);
			if (target != NULL &&
			    target->block_index == block_index &&
			    sqlparser_graph_span_append_index(build, &block->targets, index, out_error) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int sqlparser_graph_add_dml(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t *dml;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml,
		    &build->cache->dml_capacity,
		    build->cache->dml_count + 1U,
		    sizeof(*build->cache->dml),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_count++;
	local_index = build->cache->dml_count - build->statement->dml_offset - 1U;
	dml = &build->cache->dml[global_index];
	memset(dml, 0, sizeof(*dml));
	if (source != NULL) {
		*dml = *source;
	}
	dml->index = local_index;
	dml->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_column(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_column_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t *column;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_columns,
		    &build->cache->dml_column_capacity,
		    build->cache->dml_column_count + 1U,
		    sizeof(*build->cache->dml_columns),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_column_count++;
	local_index = build->cache->dml_column_count - build->statement->dml_column_offset - 1U;
	column = &build->cache->dml_columns[global_index];
	memset(column, 0, sizeof(*column));
	if (source != NULL) {
		*column = *source;
	}
	column->index = local_index;
	column->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_branch(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_branch_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_branch_t *branch;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_branches,
		    &build->cache->dml_branch_capacity,
		    build->cache->dml_branch_count + 1U,
		    sizeof(*build->cache->dml_branches),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_branch_count++;
	local_index = build->cache->dml_branch_count - build->statement->dml_branch_offset - 1U;
	branch = &build->cache->dml_branches[global_index];
	memset(branch, 0, sizeof(*branch));
	if (source != NULL) {
		*branch = *source;
	}
	branch->index = local_index;
	branch->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_cell(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_cell_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_cell_t *cell;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_cells,
		    &build->cache->dml_cell_capacity,
		    build->cache->dml_cell_count + 1U,
		    sizeof(*build->cache->dml_cells),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_cell_count++;
	local_index = build->cache->dml_cell_count - build->statement->dml_cell_offset - 1U;
	cell = &build->cache->dml_cells[global_index];
	memset(cell, 0, sizeof(*cell));
	if (source != NULL) {
		*cell = *source;
	}
	cell->index = local_index;
	cell->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_assignment(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_dml_assignment_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_assignment_t *assignment;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->dml_assignments,
		    &build->cache->dml_assignment_capacity,
		    build->cache->dml_assignment_count + 1U,
		    sizeof(*build->cache->dml_assignments),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->dml_assignment_count++;
	local_index = build->cache->dml_assignment_count - build->statement->dml_assignment_offset - 1U;
	assignment = &build->cache->dml_assignments[global_index];
	memset(assignment, 0, sizeof(*assignment));
	if (source != NULL) {
		*assignment = *source;
	}
	assignment->index = local_index;
	assignment->statement_index = build->statement_index;
	if (out_index != NULL) {
		*out_index = local_index;
	}
	return 0;
}

static int sqlparser_graph_fill_dml_value_fields(
	sqlparser_graph_build_t *build,
	PgQuery__Node *value_node,
	sqlparser_graph_value_kind_t *out_kind,
	sqlparser_literal_view_t *out_literal,
	char *bind,
	size_t bind_size,
	int *out_has_bind,
	sqlparser_bind_kind_t *out_bind_kind,
	char *bind_sql,
	size_t bind_sql_size,
	int *out_has_bind_sql,
	size_t *out_bind_position,
	int *out_has_bind_position,
	sqlparser_selector_t *out_selector,
	int *out_has_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	int value_status;

	if (out_kind != NULL) {
		*out_kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	}
	if (out_literal != NULL) {
		sqlparser_literal_view_clear(out_literal);
	}
	if (bind != NULL && bind_size > 0U) {
		bind[0] = '\0';
	}
	if (out_has_bind != NULL) {
		*out_has_bind = 0;
	}
	if (out_bind_kind != NULL) {
		*out_bind_kind = SQLPARSER_BIND_KIND_NONE;
	}
	if (bind_sql != NULL && bind_sql_size > 0U) {
		bind_sql[0] = '\0';
	}
	if (out_has_bind_sql != NULL) {
		*out_has_bind_sql = 0;
	}
	if (out_bind_position != NULL) {
		*out_bind_position = 0U;
	}
	if (out_has_bind_position != NULL) {
		*out_has_bind_position = 0;
	}
	if (out_selector != NULL) {
		memset(out_selector, 0, sizeof(*out_selector));
		out_selector->kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	}
	if (out_has_selector != NULL) {
		*out_has_selector = 0;
	}

	memset(&value, 0, sizeof(value));
	value_status = sqlparser_graph_value_from_node(
		build,
		0U,
		SQLPARSER_CLAUSE_KIND_UNKNOWN,
		NULL,
		0U,
		0,
		SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN,
		value_node,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status == 0) {
		if (out_kind != NULL) {
			*out_kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
		}
		return 0;
	}
	if (out_kind != NULL) {
		*out_kind = value.kind;
	}
	if (out_literal != NULL) {
		*out_literal = value.literal;
	}
	if (bind != NULL) {
		sqlparser_view_copy_public_text(bind, bind_size, value.has_bind ? value.bind : NULL, NULL);
		if (out_has_bind != NULL) {
			*out_has_bind = bind[0] != '\0';
		}
	}
	if (out_bind_kind != NULL) {
		*out_bind_kind = value.bind_kind;
	}
	if (bind_sql != NULL) {
		sqlparser_view_copy_public_text(bind_sql, bind_sql_size, value.has_bind_sql ? value.bind_sql : NULL, NULL);
		if (out_has_bind_sql != NULL) {
			*out_has_bind_sql = bind_sql[0] != '\0';
		}
	}
	if (out_bind_position != NULL) {
		*out_bind_position = value.bind_position;
	}
	if (out_has_bind_position != NULL) {
		*out_has_bind_position = value.has_bind_position;
	}
	if (out_selector != NULL) {
		*out_selector = value.selector;
	}
	if (out_has_selector != NULL) {
		*out_has_selector = value.has_selector;
	}
	return 0;
}

static int sqlparser_graph_add_dml_target_relation(
	sqlparser_graph_build_t *build,
	PgQuery__RangeVar *range_var,
	size_t block_index,
	size_t *out_relation_index,
	sqlparser_error_t *out_error)
{
	sqlparser_relation_view_t relation_view;
	size_t relation_index;
	size_t selector_index;
	int rc;

	if (out_relation_index != NULL) {
		*out_relation_index = 0U;
	}
	if (range_var == NULL) {
		return 0;
	}
	sqlparser_fill_relation_view(range_var, &relation_view);
	relation_index = 0U;
	rc = sqlparser_graph_add_relation(
		build,
		block_index,
		SQLPARSER_GRAPH_REL_BASE,
		&relation_view,
		&relation_index,
		out_error);
	if (rc != 0) {
		return rc;
	}
	selector_index = sqlparser_graph_find_relation_selector_index(build, range_var);
	if (selector_index != (size_t)-1) {
		sqlparser_graph_relation_t *relation;

		relation = sqlparser_graph_relation_by_local(build, relation_index);
		if (relation != NULL) {
			relation->selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
			relation->selector.statement_index = build->statement_index;
			relation->selector.item_index = selector_index;
			relation->has_selector = 1;
		}
	}
	if (out_relation_index != NULL) {
		*out_relation_index = relation_index;
	}
	return 0;
}

static int sqlparser_graph_add_dml_column_from_res_target(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t ordinal,
	PgQuery__Node *col_node,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t column;
	size_t column_index;

	if (col_node == NULL ||
	    col_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    col_node->res_target == NULL) {
		return 0;
	}
	memset(&column, 0, sizeof(column));
	column.dml_index = dml_index;
	column.ordinal = ordinal;
	column.column_name = col_node->res_target->name;
	if (sqlparser_graph_add_dml_column(build, &column, &column_index, out_error) != 0 ||
	    sqlparser_graph_span_append_index(build, &build->cache->dml[build->statement->dml_offset + dml_index].target_columns, column_index, out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_add_dml_cell_from_node(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t row_index,
	size_t column_ordinal,
	PgQuery__Node *value_node,
	int assign_insert_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_cell_t cell;
	size_t cell_index;

	memset(&cell, 0, sizeof(cell));
	cell.dml_index = dml_index;
	cell.row_index = row_index;
	cell.column_ordinal = column_ordinal;
	if (sqlparser_graph_fill_dml_value_fields(
		    build,
		    value_node,
		    &cell.kind,
		    &cell.literal,
		    cell.bind,
		    sizeof(cell.bind),
		    &cell.has_bind,
		    &cell.bind_kind,
		    cell.bind_sql,
		    sizeof(cell.bind_sql),
		    &cell.has_bind_sql,
		    &cell.bind_position,
		    &cell.has_bind_position,
		    &cell.selector,
		    &cell.has_selector,
		    out_error) != 0 ||
	    sqlparser_graph_add_dml_cell(build, &cell, &cell_index, out_error) != 0 ||
	    sqlparser_graph_span_append_index(build, &build->cache->dml[build->statement->dml_offset + dml_index].rows, cell_index, out_error) != 0) {
		return -1;
	}
	if (assign_insert_selector) {
		cell.selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
		cell.selector.statement_index = build->statement_index;
		cell.selector.row_index = row_index;
		cell.selector.column_index = column_ordinal;
		cell.has_selector = 1;
		build->cache->dml_cells[build->statement->dml_cell_offset + cell_index] = cell;
	}
	return 0;
}

static int sqlparser_graph_add_dml_assignment_from_res_target(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t block_index,
	size_t relation_index,
	int has_relation,
	PgQuery__Node *node,
	size_t selector_item_index,
	int assign_update_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t field;
	sqlparser_graph_dml_assignment_t assignment;
	size_t field_index;
	size_t assignment_index;

	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    node->res_target == NULL) {
		return 0;
	}
	memset(&field, 0, sizeof(field));
	field.block_index = block_index;
	field.clause = SQLPARSER_CLAUSE_KIND_SET_LIST;
	field.relation_index = relation_index;
	field.has_relation = has_relation;
	field.column_name = node->res_target->name;
	if (sqlparser_graph_add_field(build, &field, &field_index, out_error) != 0) {
		return -1;
	}
	memset(&assignment, 0, sizeof(assignment));
	assignment.dml_index = dml_index;
	assignment.target_field_index = field_index;
	if (sqlparser_graph_fill_dml_value_fields(
		    build,
		    node->res_target->val,
		    &assignment.value_kind,
		    &assignment.literal,
		    assignment.bind,
		    sizeof(assignment.bind),
		    &assignment.has_bind,
		    &assignment.bind_kind,
		    assignment.bind_sql,
		    sizeof(assignment.bind_sql),
		    &assignment.has_bind_sql,
		    &assignment.bind_position,
		    &assignment.has_bind_position,
		    &assignment.selector,
		    &assignment.has_selector,
		    out_error) != 0 ||
	    sqlparser_graph_add_dml_assignment(build, &assignment, &assignment_index, out_error) != 0 ||
	    sqlparser_graph_span_append_index(build, &build->cache->dml[build->statement->dml_offset + dml_index].assignments, assignment_index, out_error) != 0) {
		return -1;
	}
	if (assign_update_selector) {
		assignment.selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
		assignment.selector.statement_index = build->statement_index;
		assignment.selector.item_index = selector_item_index;
		assignment.has_selector = 1;
		build->cache->dml_assignments[build->statement->dml_assignment_offset + assignment_index] = assignment;
	}
	return 0;
}

static int sqlparser_graph_add_oracle_relation(
	sqlparser_graph_build_t *build,
	const sqlparser_oracle_relation_t *source,
	size_t block_index,
	size_t *out_relation_index,
	sqlparser_error_t *out_error)
{
	sqlparser_relation_view_t relation;

	if (source == NULL) {
		if (out_relation_index != NULL) {
			*out_relation_index = 0U;
		}
		return 0;
	}
	memset(&relation, 0, sizeof(relation));
	relation.database_name = source->database_name;
	relation.schema_name = source->schema_name;
	relation.table_name = source->table_name;
	if (sqlparser_graph_add_relation(
		    build,
		    block_index,
		    SQLPARSER_GRAPH_REL_BASE,
		    &relation,
		    out_relation_index,
		    out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_add_oracle_dml_column(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t ordinal,
	const sqlparser_oracle_column_t *source,
	sqlparser_graph_dml_branch_t *branch,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_column_t column;
	size_t column_index;

	memset(&column, 0, sizeof(column));
	column.dml_index = dml_index;
	column.ordinal = ordinal;
	column.column_name = source != NULL ? source->name : NULL;
	if (sqlparser_graph_add_dml_column(build, &column, &column_index, out_error) != 0) {
		return -1;
	}
	if (branch != NULL &&
	    sqlparser_graph_span_append_index(build, &branch->target_columns, column_index, out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_oracle_cell_resolve_source_target(
	sqlparser_graph_build_t *build,
	size_t source_block_index,
	PgQuery__ColumnRef *column_ref,
	size_t *out_source_target_index)
{
	const char *name;
	const char *qualifier;
	size_t index;
	size_t match_index;
	size_t match_count;

	if (out_source_target_index != NULL) {
		*out_source_target_index = 0U;
	}
	if (build == NULL || column_ref == NULL) {
		return 0;
	}
	if (sqlparser_graph_column_ref_is_pseudo(column_ref)) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	qualifier = sqlparser_graph_column_ref_part(column_ref, 1U);
	if (name == NULL || name[0] == '\0' || strcmp(name, "*") == 0 ||
	    (qualifier != NULL && qualifier[0] != '\0')) {
		return 0;
	}
	match_index = 0U;
	match_count = 0U;
	for (index = 0U; index < sqlparser_graph_local_target_count(build); index++) {
		sqlparser_graph_target_t *target;
		int matched;

		target = sqlparser_graph_target_by_local(build, index);
		if (target == NULL ||
		    target->block_index != source_block_index ||
		    target->kind != SQLPARSER_GRAPH_TARGET_FIELD) {
			continue;
		}
		matched = target->output_name != NULL &&
			sqlparser_text_equal_ci(target->output_name, name);
		if (!matched) {
			continue;
		}
		match_index = index;
		match_count++;
		if (match_count > 1U) {
			return 0;
		}
	}
	if (match_count == 1U && out_source_target_index != NULL) {
		*out_source_target_index = match_index;
	}
	return match_count == 1U;
}

static int sqlparser_graph_add_oracle_dml_cell(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t branch_ordinal,
	size_t column_ordinal,
	size_t source_block_index,
	int has_source_block,
	const sqlparser_oracle_value_t *source,
	sqlparser_graph_dml_branch_t *branch,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_cell_t cell;
	PgQuery__Node *node;
	size_t cell_index;

	if (source == NULL) {
		return 0;
	}
	memset(&cell, 0, sizeof(cell));
	cell.dml_index = dml_index;
	cell.row_index = branch_ordinal;
	cell.column_ordinal = column_ordinal;
	if (source->has_bind) {
		cell.kind = SQLPARSER_GRAPH_VALUE_BIND;
		cell.has_bind = 1;
		cell.bind_kind = source->bind_kind;
		sqlparser_view_copy_public_text(cell.bind, sizeof(cell.bind), source->bind, NULL);
		sqlparser_view_copy_public_text(cell.bind_sql, sizeof(cell.bind_sql), source->bind_sql, NULL);
		cell.has_bind_sql = cell.bind_sql[0] != '\0';
		cell.bind_position = source->bind_position;
		cell.has_bind_position = source->has_bind_position;
	} else if (source->has_literal) {
		cell.kind = SQLPARSER_GRAPH_VALUE_LITERAL;
		cell.literal = source->literal;
	} else if (source->parser_sql != NULL && strchr(source->parser_sql, '$') == NULL) {
		node = NULL;
		if (sqlparser_parse_insert_cell_node_sql(source->parser_sql, &node, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_A_CONST &&
		    node->a_const != NULL) {
			cell.kind = SQLPARSER_GRAPH_VALUE_LITERAL;
			if (sqlparser_fill_literal_view_from_a_const(node->a_const, &cell.literal, out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_free_proto_node(node);
				return -1;
			}
		} else if (node != NULL &&
		           node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
			cell.kind = SQLPARSER_GRAPH_VALUE_DEFAULT;
		} else if (has_source_block &&
		           node != NULL &&
		           node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		           node->column_ref != NULL &&
		           sqlparser_graph_oracle_cell_resolve_source_target(
			           build,
			           source_block_index,
			           node->column_ref,
			           &cell.source_target_index)) {
			cell.kind = SQLPARSER_GRAPH_VALUE_FIELD;
			cell.has_source_target = 1;
		} else {
			cell.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
		}
		sqlparser_free_proto_node(node);
	} else {
		cell.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	}
	cell.selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	cell.selector.statement_index = build->statement_index;
	cell.selector.row_index = branch_ordinal;
	cell.selector.column_index = column_ordinal;
	cell.has_selector = 1;
	if (sqlparser_graph_add_dml_cell(build, &cell, &cell_index, out_error) != 0) {
		return -1;
	}
	if (branch != NULL &&
	    sqlparser_graph_span_append_index(build, &branch->rows, cell_index, out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_graph_build_oracle_multi_insert_dml(
	sqlparser_graph_build_t *build,
	PgQuery__InsertStmt *stmt,
	const sqlparser_oracle_multi_insert_t *multi,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_t *dml_item;
	size_t root_block_index;
	size_t dml_index;
	size_t branch_index;
	size_t *local_branch_indices;

	if (build == NULL || stmt == NULL || multi == NULL) {
		return 0;
	}
	local_branch_indices = NULL;
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_INSERT;
	dml.insert_mode = multi->mode == SQLPARSER_ORACLE_MULTI_INSERT_FIRST ?
		SQLPARSER_GRAPH_INSERT_MODE_FIRST :
		SQLPARSER_GRAPH_INSERT_MODE_ALL;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &root_block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml(build, &dml, &dml_index, out_error) != 0) {
		return -1;
	}
	if (multi->branch_count > 0U) {
		local_branch_indices = (size_t *)calloc(multi->branch_count, sizeof(*local_branch_indices));
		if (local_branch_indices == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	dml_item = &build->cache->dml[build->statement->dml_offset + dml_index];
	if (stmt->select_stmt != NULL &&
	    stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		dml_item->has_source_block = 1;
		if (sqlparser_graph_build_select(
			    build,
			    stmt->select_stmt->select_stmt,
			    SQLPARSER_GRAPH_BLOCK_SELECT,
			    &dml_item->source_block_index,
			    out_error) != 0) {
			free(local_branch_indices);
			return -1;
		}
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		const sqlparser_oracle_multi_insert_branch_t *source_branch;
		sqlparser_graph_dml_branch_t branch;
		size_t local_branch_index;

		source_branch = &multi->branches[branch_index];
		memset(&branch, 0, sizeof(branch));
		branch.dml_index = dml_index;
		branch.ordinal = branch_index;
		if (sqlparser_graph_add_oracle_relation(
			    build,
			    &source_branch->relation,
			    root_block_index,
			    &branch.target_relation_index,
			    out_error) != 0) {
			free(local_branch_indices);
			return -1;
		}
		branch.has_target_relation = source_branch->relation.table_name != NULL;
		if (source_branch->has_condition) {
			branch.condition_selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_CONDITION;
			branch.condition_selector.statement_index = build->statement_index;
			branch.condition_selector.item_index = branch_index;
			branch.has_condition_selector = 1;
		}
		if (sqlparser_graph_add_dml_branch(build, &branch, &local_branch_index, out_error) != 0 ||
		    sqlparser_graph_span_append_index(build, &dml_item->branches, local_branch_index, out_error) != 0) {
			free(local_branch_indices);
			return -1;
		}
		local_branch_indices[branch_index] = local_branch_index;
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		const sqlparser_oracle_multi_insert_branch_t *source_branch;
		sqlparser_graph_dml_branch_t *branch_item;
		size_t index;

		source_branch = &multi->branches[branch_index];
		branch_item = &build->cache->dml_branches[build->statement->dml_branch_offset + local_branch_indices[branch_index]];
		for (index = 0U; index < source_branch->column_count; index++) {
			if (sqlparser_graph_add_oracle_dml_column(
				    build,
				    dml_index,
				    index,
				    &source_branch->columns[index],
				    branch_item,
				    out_error) != 0) {
				free(local_branch_indices);
				return -1;
			}
		}
		for (index = 0U; index < source_branch->cell_count; index++) {
			if (sqlparser_graph_add_oracle_dml_cell(
				    build,
				    dml_index,
				    branch_index,
				    index,
				    dml_item->source_block_index,
				    dml_item->has_source_block,
				    &source_branch->cells[index],
				    branch_item,
				    out_error) != 0) {
				free(local_branch_indices);
				return -1;
			}
		}
	}
	free(local_branch_indices);
	return 0;
}

static int sqlparser_graph_build_insert_dml(
	sqlparser_graph_build_t *build,
	PgQuery__InsertStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	size_t block_index;
	size_t dml_index;
	size_t index;
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__SelectStmt *values_stmt;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_INSERT;
	dml.insert_mode = SQLPARSER_GRAPH_INSERT_MODE_VALUES;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml_target_relation(build, stmt->relation, block_index, &dml.target_relation_index, out_error) != 0) {
		return -1;
	}
	dml.has_target_relation = stmt->relation != NULL;
	if (sqlparser_graph_add_dml(build, &dml, &dml_index, out_error) != 0) {
		return -1;
	}
	for (index = 0U; index < stmt->n_cols; index++) {
		if (sqlparser_graph_add_dml_column_from_res_target(
			    build,
			    dml_index,
			    index,
			    stmt->cols != NULL ? stmt->cols[index] : NULL,
			    out_error) != 0) {
			return -1;
		}
	}
	insert_stmt = NULL;
	values_stmt = NULL;
	if (sqlparser_get_insert_values_stmt(build->handle, build->statement_index, &insert_stmt, &values_stmt, NULL) == SQLPARSER_STATUS_OK &&
	    values_stmt != NULL &&
	    values_stmt->values_lists != NULL) {
		for (index = 0U; index < values_stmt->n_values_lists; index++) {
			PgQuery__Node *row_node;
			size_t column_index;

			row_node = values_stmt->values_lists[index];
			if (row_node == NULL ||
			    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
			    row_node->list == NULL) {
				continue;
			}
			for (column_index = 0U; column_index < row_node->list->n_items; column_index++) {
				if (sqlparser_graph_add_dml_cell_from_node(
					    build,
					    dml_index,
					    index,
					    column_index,
					    row_node->list->items[column_index],
					    1,
					    out_error) != 0) {
					return -1;
				}
			}
		}
	} else if (stmt->select_stmt != NULL &&
			   stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
		sqlparser_graph_dml_t *dml_item;

		dml_item = &build->cache->dml[build->statement->dml_offset + dml_index];
		dml_item->insert_mode = SQLPARSER_GRAPH_INSERT_MODE_SELECT;
		dml_item->has_source_block = 1;
		if (sqlparser_graph_build_select(
			    build,
			    stmt->select_stmt->select_stmt,
			    SQLPARSER_GRAPH_BLOCK_SELECT,
			    &dml_item->source_block_index,
			    out_error) != 0) {
			return -1;
		}
	}
	if (stmt->on_conflict_clause != NULL &&
	    stmt->on_conflict_clause->action == PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE) {
		for (index = 0U; index < stmt->on_conflict_clause->n_target_list; index++) {
			if (sqlparser_graph_add_dml_assignment_from_res_target(
				    build,
				    dml_index,
				    block_index,
				    dml.target_relation_index,
				    dml.has_target_relation,
				    stmt->on_conflict_clause->target_list != NULL ?
					    stmt->on_conflict_clause->target_list[index] :
					    NULL,
				    index,
				    0,
				    out_error) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

static int sqlparser_graph_build_update_dml(
	sqlparser_graph_build_t *build,
	PgQuery__UpdateStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	size_t dml_index;
	size_t block_index;
	size_t relation_index;
	size_t index;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_UPDATE;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml_target_relation(build, stmt->relation, block_index, &relation_index, out_error) != 0) {
		return -1;
	}
	dml.target_relation_index = relation_index;
	dml.has_target_relation = stmt->relation != NULL;
	if (sqlparser_graph_add_dml(build, &dml, &dml_index, out_error) != 0 ||
	    sqlparser_graph_push_scope(build, block_index, NULL) != 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph nesting is too deep");
		return -1;
	}
	for (index = 0U; index < stmt->n_from_clause; index++) {
		if (sqlparser_graph_build_from_item(build, block_index, stmt->from_clause[index], out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	for (index = 0U; index < stmt->n_target_list; index++) {
		if (sqlparser_graph_add_dml_assignment_from_res_target(
			    build,
			    dml_index,
			    block_index,
			    relation_index,
			    dml.has_target_relation,
			    stmt->target_list != NULL ? stmt->target_list[index] : NULL,
			    index,
			    1,
			    out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, 0U, 0, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_build_delete_dml(
	sqlparser_graph_build_t *build,
	PgQuery__DeleteStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	size_t dml_index;
	size_t block_index;
	size_t relation_index;
	size_t index;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_DELETE;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml_target_relation(build, stmt->relation, block_index, &relation_index, out_error) != 0) {
		return -1;
	}
	dml.target_relation_index = relation_index;
	dml.has_target_relation = stmt->relation != NULL;
	if (dml.has_target_relation &&
	    sqlparser_graph_span_append_index(build, &dml.delete_targets, relation_index, out_error) != 0) {
		return -1;
	}
	if (sqlparser_graph_add_dml(build, &dml, &dml_index, out_error) != 0 ||
	    sqlparser_graph_push_scope(build, block_index, NULL) != 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph nesting is too deep");
		return -1;
	}
	for (index = 0U; index < stmt->n_using_clause; index++) {
		if (sqlparser_graph_build_from_item(build, block_index, stmt->using_clause[index], out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
	}
	if (sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, 0U, 0, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_build_merge_dml(
	sqlparser_graph_build_t *build,
	PgQuery__MergeStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_dml_t dml;
	size_t block_index;
	size_t target_relation_index;
	size_t dml_index;
	size_t index;

	if (build == NULL || stmt == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	dml.kind = SQLPARSER_GRAPH_DML_MERGE;
	if (sqlparser_graph_add_block(build, SQLPARSER_GRAPH_BLOCK_SELECT, &block_index, out_error) != 0 ||
	    sqlparser_graph_add_dml_target_relation(build, stmt->relation, block_index, &target_relation_index, out_error) != 0) {
		return -1;
	}
	dml.target_relation_index = target_relation_index;
	dml.has_target_relation = stmt->relation != NULL;
	if (sqlparser_graph_add_dml(build, &dml, &dml_index, out_error) != 0 ||
	    sqlparser_graph_push_scope(build, block_index, NULL) != 0) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph nesting is too deep");
		return -1;
	}
	if (sqlparser_graph_build_from_item(build, block_index, stmt->source_relation, out_error) != 0 ||
	    sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_ON, stmt->join_condition, 0U, 0, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *when_node;
		PgQuery__MergeWhenClause *when_clause;
		size_t item_index;

		when_node = stmt->merge_when_clauses != NULL ? stmt->merge_when_clauses[index] : NULL;
		if (when_node == NULL ||
		    when_node->node_case != PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
		    when_node->merge_when_clause == NULL) {
			continue;
		}
		when_clause = when_node->merge_when_clause;
		if (sqlparser_graph_walk_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, when_clause->condition, 0U, 0, out_error) != 0) {
			sqlparser_graph_pop_scope(build);
			return -1;
		}
		if (when_clause->command_type == PG_QUERY__CMD_TYPE__CMD_UPDATE) {
			for (item_index = 0U; item_index < when_clause->n_target_list; item_index++) {
				if (sqlparser_graph_add_dml_assignment_from_res_target(
					    build,
					    dml_index,
					    block_index,
					    target_relation_index,
					    dml.has_target_relation,
					    when_clause->target_list != NULL ? when_clause->target_list[item_index] : NULL,
					    item_index,
					    0,
					    out_error) != 0) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
		} else if (when_clause->command_type == PG_QUERY__CMD_TYPE__CMD_INSERT) {
			for (item_index = 0U; item_index < when_clause->n_target_list; item_index++) {
				if (sqlparser_graph_add_dml_column_from_res_target(
					    build,
					    dml_index,
					    item_index,
					    when_clause->target_list != NULL ? when_clause->target_list[item_index] : NULL,
					    out_error) != 0) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
			for (item_index = 0U; item_index < when_clause->n_values; item_index++) {
				if (sqlparser_graph_add_dml_cell_from_node(
					    build,
					    dml_index,
					    0U,
					    item_index,
					    when_clause->values != NULL ? when_clause->values[item_index] : NULL,
					    0,
					    out_error) != 0) {
					sqlparser_graph_pop_scope(build);
					return -1;
				}
			}
		}
	}
	sqlparser_graph_pop_scope(build);
	return 0;
}

static int sqlparser_graph_build_statement(
	sqlparser_graph_build_t *build,
	PgQuery__Node *statement,
	sqlparser_error_t *out_error)
{
	if (statement == NULL) {
		return 0;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_graph_build_select(build, statement->select_stmt, SQLPARSER_GRAPH_BLOCK_SELECT, NULL, out_error);
		case PG_QUERY__NODE__NODE_VIEW_STMT:
			return statement->view_stmt != NULL &&
					statement->view_stmt->query != NULL &&
					statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_graph_build_select(build, statement->view_stmt->query->select_stmt, SQLPARSER_GRAPH_BLOCK_SELECT, NULL, out_error) :
				0;
		case PG_QUERY__NODE__NODE_CREATE_TABLE_AS_STMT:
			return statement->create_table_as_stmt != NULL &&
					statement->create_table_as_stmt->query != NULL &&
					statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT ?
				sqlparser_graph_build_select(build, statement->create_table_as_stmt->query->select_stmt, SQLPARSER_GRAPH_BLOCK_SELECT, NULL, out_error) :
				0;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (build != NULL &&
			    build->handle != NULL &&
			    build->handle->dialect == SQLPARSER_DIALECT_ORACLE &&
			    sqlparser_oracle_state_has_multi_insert(build->handle->dialect_state)) {
				return sqlparser_graph_build_oracle_multi_insert_dml(
					build,
					statement->insert_stmt,
					sqlparser_oracle_state_multi_insert(build->handle->dialect_state),
					out_error);
			}
			return sqlparser_graph_build_insert_dml(build, statement->insert_stmt, out_error);
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return sqlparser_graph_build_update_dml(build, statement->update_stmt, out_error);
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return sqlparser_graph_build_delete_dml(build, statement->delete_stmt, out_error);
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return sqlparser_graph_build_merge_dml(build, statement->merge_stmt, out_error);
		default:
			return 0;
		}
	}

static sqlparser_status_t sqlparser_query_graph_cache_build(
	sqlparser_handle_t *handle,
	sqlparser_query_graph_cache_t **out_cache,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	size_t statement_index;
	sqlparser_status_t status;

	if (out_cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_cache must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_cache = NULL;
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_handle_ensure_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	cache = (sqlparser_query_graph_cache_t *)calloc(1U, sizeof(*cache));
	if (cache == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	cache->generation = handle->generation;
	cache->statement_count = handle->statement_count;
	if (cache->statement_count > 0U) {
		cache->statements = (sqlparser_statement_graph_t *)calloc(cache->statement_count, sizeof(*cache->statements));
		if (cache->statements == NULL) {
			sqlparser_query_graph_cache_release(cache);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	for (statement_index = 0U; statement_index < handle->statement_count; statement_index++) {
		PgQuery__Node *statement_node;
		sqlparser_graph_build_t build;

		statement_node = NULL;
		status = sqlparser_get_statement_node(handle, statement_index, &statement_node, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_query_graph_cache_release(cache);
			return status;
		}
		memset(&build, 0, sizeof(build));
		build.handle = handle;
		build.cache = cache;
		build.statement = &cache->statements[statement_index];
		build.statement_index = statement_index;
		build.statement->block_offset = cache->block_count;
		build.statement->relation_offset = cache->relation_count;
		build.statement->target_offset = cache->target_count;
		build.statement->field_offset = cache->field_count;
		build.statement->value_offset = cache->value_count;
		build.statement->set_offset = cache->set_count;
		build.statement->dml_offset = cache->dml_count;
		build.statement->dml_branch_offset = cache->dml_branch_count;
		build.statement->dml_column_offset = cache->dml_column_count;
		build.statement->dml_cell_offset = cache->dml_cell_count;
		build.statement->dml_assignment_offset = cache->dml_assignment_count;
		if (sqlparser_graph_build_statement(&build, statement_node, out_error) != 0 ||
		    sqlparser_graph_finalize_statement_spans(&build, out_error) != 0) {
			sqlparser_query_graph_cache_release(cache);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "failed to build query graph");
			}
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	*out_cache = cache;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_query_graph_ensure(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_status_t status;

	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (handle->query_graph != NULL &&
	    handle->query_graph_generation == handle->generation &&
	    handle->query_graph->generation == handle->generation) {
		return SQLPARSER_STATUS_OK;
	}
	sqlparser_handle_clear_query_graph(handle);
	cache = NULL;
	status = sqlparser_query_graph_cache_build(handle, &cache, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	handle->query_graph = cache;
	handle->query_graph_generation = handle->generation;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_statement_graph_t *sqlparser_query_graph_statement(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_query_graph_cache_t **out_cache)
{
	sqlparser_handle_t *handle;
	sqlparser_query_graph_cache_t *cache;

	if (out_cache != NULL) {
		*out_cache = NULL;
	}
	if (graph == NULL || graph->handle == NULL) {
		return NULL;
	}
	handle = (sqlparser_handle_t *)graph->handle;
	cache = handle->query_graph;
	if (cache == NULL || graph->statement_index >= cache->statement_count) {
		return NULL;
	}
	if (out_cache != NULL) {
		*out_cache = cache;
	}
	return &cache->statements[graph->statement_index];
}

sqlparser_status_t sqlparser_statement_query_graph(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_query_graph_view_t *out_graph,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_statement_graph_t *statement;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (out_graph == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_graph must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_graph, 0, sizeof(*out_graph));
	if (handle == NULL || statement_index >= handle->statement_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_query_graph_ensure(mutable_handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	statement = &mutable_handle->query_graph->statements[statement_index];
	out_graph->handle = handle;
	out_graph->statement_index = statement_index;
	out_graph->generation = handle->generation;
	out_graph->root_block_index = statement->root_block_index;
	out_graph->has_root_block = statement->has_root_block;
	out_graph->block_count = statement->block_count;
	out_graph->relation_count = statement->relation_count;
	out_graph->target_count = statement->target_count;
	out_graph->field_count = statement->field_count;
	out_graph->value_count = statement->value_count;
	out_graph->set_count = statement->set_count;
	out_graph->has_dml = statement->dml_count > 0U;
	out_graph->dml_branch_count = statement->dml_branch_count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_span_index_at(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_index_span_t span,
	size_t item_index,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;

	sqlparser_error_clear(out_error);
	if (out_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = 0U;
	(void)sqlparser_query_graph_statement(graph, &cache);
	if (cache == NULL || item_index >= span.count || span.offset + item_index >= cache->index_pool_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "span index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = cache->index_pool[span.offset + item_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_block_at(
	const sqlparser_query_graph_view_t *graph,
	size_t block_index,
	sqlparser_graph_block_t *out_block,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_block == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_block must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_block, 0, sizeof(*out_block));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || block_index >= statement->block_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "block_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_block = cache->blocks[statement->block_offset + block_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_relation_at(
	const sqlparser_query_graph_view_t *graph,
	size_t relation_index,
	sqlparser_graph_relation_t *out_relation,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_relation, 0, sizeof(*out_relation));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || relation_index >= statement->relation_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_relation = cache->relations[statement->relation_offset + relation_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_target_at(
	const sqlparser_query_graph_view_t *graph,
	size_t target_index,
	sqlparser_graph_target_t *out_target,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_target, 0, sizeof(*out_target));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || target_index >= statement->target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "target_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_target = cache->targets[statement->target_offset + target_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_field_at(
	const sqlparser_query_graph_view_t *graph,
	size_t field_index,
	sqlparser_graph_field_t *out_field,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_field == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_field must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_field, 0, sizeof(*out_field));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || field_index >= statement->field_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "field_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_field = cache->fields[statement->field_offset + field_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_value_at(
	const sqlparser_query_graph_view_t *graph,
	size_t value_index,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || value_index >= statement->value_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = cache->values[statement->value_offset + value_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_set_at(
	const sqlparser_query_graph_view_t *graph,
	size_t set_index,
	sqlparser_graph_set_t *out_set,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_set == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_set must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_set, 0, sizeof(*out_set));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || set_index >= statement->set_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "set_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_set = cache->sets[statement->set_offset + set_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_graph_dml_t *out_dml,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_dml == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_dml must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || statement->dml_count == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement has no dml graph");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_dml = cache->dml[statement->dml_offset];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_branch_at(
	const sqlparser_query_graph_view_t *graph,
	size_t branch_index,
	sqlparser_graph_dml_branch_t *out_branch,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_branch == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_branch must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_branch, 0, sizeof(*out_branch));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || branch_index >= statement->dml_branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_branch = cache->dml_branches[statement->dml_branch_offset + branch_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_column_at(
	const sqlparser_query_graph_view_t *graph,
	size_t column_index,
	sqlparser_graph_dml_column_t *out_column,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_column == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_column must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_column, 0, sizeof(*out_column));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || column_index >= statement->dml_column_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml column index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_column = cache->dml_columns[statement->dml_column_offset + column_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_cell_at(
	const sqlparser_query_graph_view_t *graph,
	size_t cell_index,
	sqlparser_graph_dml_cell_t *out_cell,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_cell == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_cell must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_cell, 0, sizeof(*out_cell));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || cell_index >= statement->dml_cell_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_cell = cache->dml_cells[statement->dml_cell_offset + cell_index];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_query_graph_dml_assignment_at(
	const sqlparser_query_graph_view_t *graph,
	size_t assignment_index,
	sqlparser_graph_dml_assignment_t *out_assignment,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_assignment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_assignment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_assignment, 0, sizeof(*out_assignment));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || assignment_index >= statement->dml_assignment_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dml assignment index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_assignment = cache->dml_assignments[statement->dml_assignment_offset + assignment_index];
	return SQLPARSER_STATUS_OK;
}

static json_t *sqlparser_graph_span_json(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	json_t *array;
	size_t index;

	array = json_array();
	if (array == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	for (index = 0U; index < span.count; index++) {
		size_t value;

		value = 0U;
		if (sqlparser_query_graph_span_index_at(graph, span, index, &value, out_error) != SQLPARSER_STATUS_OK ||
		    json_array_append_new(array, json_integer((json_int_t)value)) != 0) {
			json_decref(array);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return NULL;
		}
	}
	return array;
}

static json_t *sqlparser_graph_block_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_block_t *block,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *relations;
	json_t *targets;

	object = json_object();
	relations = sqlparser_graph_span_json(graph, block->relations, out_error);
	targets = sqlparser_graph_span_json(graph, block->targets, out_error);
	if (object == NULL || relations == NULL || targets == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_block_kind_name(block->kind))) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "relations", &relations) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "targets", &targets) != 0) {
		json_decref(object);
		json_decref(relations);
		json_decref(targets);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_relation_json(
	const sqlparser_graph_relation_t *relation,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)relation->block_index)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_relation_kind_name(relation->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "database", relation->database_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "schema", relation->schema_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "table", relation->object_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "alias", relation->alias_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_block", relation->has_source_block, relation->source_block_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", relation->has_selector ? &relation->selector : NULL, out_error) != 0) {
		json_decref(object);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_target_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_target_t *target,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *star_relations;

	object = json_object();
	star_relations = sqlparser_graph_span_json(graph, target->star_relations, out_error);
	if (object == NULL || star_relations == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)target->block_index)) != 0 ||
	    json_object_set_new(object, "ordinal", json_integer((json_int_t)target->ordinal)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_target_kind_name(target->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "name", target->output_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "field", target->has_field, target->field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "value", target->has_value, target->value_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "star_relations", &star_relations) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_block", target->has_source_block, target->source_block_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", target->has_selector ? &target->selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_optional_selector(object, "target_list_selector", target->has_target_list_selector ? &target->target_list_selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(star_relations);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_target_path_json(
	const sqlparser_graph_field_t *field,
	sqlparser_error_t *out_error)
{
	json_t *array;
	size_t index;

	array = json_array();
	if (array == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	for (index = 0U; index < field->target_path_count; index++) {
		const sqlparser_target_path_entry_t *path;
		json_t *entry;

		path = &field->target_path[index];
		entry = json_object();
		if (entry == NULL ||
			    sqlparser_json_set_optional_string(entry, "kind", path->kind) != 0 ||
			    sqlparser_json_set_optional_string(entry, "name", path->has_name ? path->name : NULL) != 0 ||
		    json_object_set_new(entry, "arg_index", json_integer((json_int_t)path->arg_index)) != 0 ||
		    sqlparser_json_array_append_owned(array, &entry) != 0) {
		json_decref(entry);
		json_decref(array);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return NULL;
		}
	}
	return array;
}

static json_t *sqlparser_graph_field_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_field_t *field,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *candidates;
	json_t *target_path;

	object = json_object();
	candidates = sqlparser_graph_span_json(graph, field->candidate_relations, out_error);
	target_path = sqlparser_graph_target_path_json(field, out_error);
	if (object == NULL || candidates == NULL || target_path == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)field->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(field->clause))) != 0 ||
	    sqlparser_json_set_optional_size(object, "relation", field->has_relation, field->relation_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "candidate_relations", &candidates) != 0 ||
	    sqlparser_json_set_optional_string(object, "column", field->column_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "target", field->has_target, field->target_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", field->has_selector ? &field->selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "target_path", &target_path) != 0) {
		json_decref(object);
		json_decref(candidates);
		json_decref(target_path);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_literal_json(
	const sqlparser_literal_view_t *literal,
	sqlparser_graph_value_kind_t value_kind)
{
	json_t *object;

	if (value_kind != SQLPARSER_GRAPH_VALUE_LITERAL || literal == NULL) {
		return json_null();
	}
	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_literal_kind_name(literal->kind))) != 0) {
		json_decref(object);
		return NULL;
	}
	switch (literal->kind) {
		case SQLPARSER_LITERAL_KIND_STRING:
			if (json_object_set_new(object, "string_value", json_string(literal->string_value != NULL ? literal->string_value : "")) != 0) {
				json_decref(object);
				return NULL;
			}
			if (literal->quoted_identifier &&
			    json_object_set_new(object, "quoted_identifier", json_boolean(1)) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_INTEGER:
			if (json_object_set_new(object, "integer_value", json_integer((json_int_t)literal->integer_value)) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			if (sqlparser_json_set_optional_string(object, "float_value", literal->float_value) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			if (json_object_set_new(object, "boolean_value", json_boolean(literal->boolean_value != 0)) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_LITERAL_KIND_NULL:
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			break;
	}
	return object;
}

static json_t *sqlparser_graph_value_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_value_t *value,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *literal;
	const char *field_match_kind_name;

	(void)graph;
	field_match_kind_name = value->has_field &&
			value->field_match_kind != SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN ?
		sqlparser_graph_field_match_kind_name(value->field_match_kind) :
		NULL;
	object = json_object();
	literal = sqlparser_graph_literal_json(&value->literal, value->kind);
	if (object == NULL || literal == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)value->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(value->clause))) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator", value->operator_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "field", value->has_field, value->field_index) != 0 ||
	    sqlparser_json_set_optional_string(object, "field_match_kind", field_match_kind_name) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(value->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", value->has_bind ? value->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(value->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", value->has_bind_sql ? value->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", value->has_bind_position, value->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", value->has_selector ? &value->selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
		literal = NULL;
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_set_json(
	const sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_set_t *set_item,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *branches;
	const char *kind_name;

	object = json_object();
	branches = sqlparser_graph_span_json(graph, set_item->branch_blocks, out_error);
	kind_name = sqlparser_graph_set_kind_name(set_item->kind);
	if (set_item->kind == SQLPARSER_GRAPH_SET_EXCEPT &&
	    handle != NULL &&
	    (handle->dialect == SQLPARSER_DIALECT_ORACLE ||
	     handle->dialect == SQLPARSER_DIALECT_DAMENG)) {
		kind_name = "minus";
	}
	if (object == NULL || branches == NULL ||
	    json_object_set_new(object, "kind", json_string(kind_name)) != 0 ||
	    json_object_set_new(object, "result_block", json_integer((json_int_t)set_item->result_block_index)) != 0 ||
	    sqlparser_json_object_set_owned(object, "branches", &branches) != 0) {
	json_decref(object);
	json_decref(branches);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_dml_column_json(
	const sqlparser_graph_dml_column_t *column,
	sqlparser_error_t *out_error)
{
	json_t *object;

	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "ordinal", json_integer((json_int_t)column->ordinal)) != 0 ||
	    sqlparser_json_set_optional_string(object, "column", column->column_name) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", column->has_selector ? &column->selector : NULL, out_error) != 0) {
		json_decref(object);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_dml_cell_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_cell_t *cell,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *literal;

	(void)graph;
	object = json_object();
	literal = sqlparser_graph_literal_json(&cell->literal, cell->kind);
	if (object == NULL || literal == NULL ||
	    json_object_set_new(object, "row", json_integer((json_int_t)cell->row_index)) != 0 ||
	    json_object_set_new(object, "column", json_integer((json_int_t)cell->column_ordinal)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(cell->kind))) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_target", cell->has_source_target, cell->source_target_index) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", cell->has_bind ? cell->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(cell->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", cell->has_bind_sql ? cell->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", cell->has_bind_position, cell->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", cell->has_selector ? &cell->selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
		literal = NULL;
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static json_t *sqlparser_graph_dml_assignment_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_assignment_t *assignment,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *literal;

	(void)graph;
	object = json_object();
	literal = sqlparser_graph_literal_json(&assignment->literal, assignment->value_kind);
	if (object == NULL || literal == NULL ||
	    json_object_set_new(object, "target_field", json_integer((json_int_t)assignment->target_field_index)) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(assignment->value_kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", assignment->has_bind ? assignment->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(assignment->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", assignment->has_bind_sql ? assignment->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", assignment->has_bind_position, assignment->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", assignment->has_selector ? &assignment->selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	if (json_is_null(literal)) {
		json_decref(literal);
		literal = NULL;
	} else if (sqlparser_json_object_set_owned(object, "literal", &literal) != 0) {
		json_decref(object);
		json_decref(literal);
		if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
		return NULL;
	}
	return object;
}

static int sqlparser_graph_append_dml_column_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t column_index;
		sqlparser_graph_dml_column_t column;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &column_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_column_at(graph, column_index, &column, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_column_json(&column, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_append_dml_cell_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t cell_index;
		sqlparser_graph_dml_cell_t cell;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &cell_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_cell_at(graph, cell_index, &cell, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_cell_json(graph, &cell, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static int sqlparser_graph_append_dml_assignment_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t assignment_index;
		sqlparser_graph_dml_assignment_t assignment;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &assignment_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_assignment_at(graph, assignment_index, &assignment, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_assignment_json(graph, &assignment, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static json_t *sqlparser_graph_dml_branch_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_branch_t *branch,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *target_columns;
	json_t *rows;

	object = json_object();
	target_columns = json_array();
	rows = json_array();
	if (object == NULL || target_columns == NULL || rows == NULL) {
		goto fail;
	}
	if (sqlparser_graph_append_dml_column_objects(graph, target_columns, branch->target_columns, out_error) != 0 ||
	    sqlparser_graph_append_dml_cell_objects(graph, rows, branch->rows, out_error) != 0 ||
	    json_object_set_new(object, "ordinal", json_integer((json_int_t)branch->ordinal)) != 0 ||
	    sqlparser_json_set_optional_size(object, "target_relation", branch->has_target_relation, branch->target_relation_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "target_columns", &target_columns) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "rows", &rows) != 0 ||
	    sqlparser_json_set_optional_size(object, "condition_block", branch->has_condition_block, branch->condition_block_index) != 0 ||
	    sqlparser_json_set_optional_selector(object, "condition_selector", branch->has_condition_selector ? &branch->condition_selector : NULL, out_error) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(target_columns);
	json_decref(rows);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static int sqlparser_graph_append_dml_branch_objects(
	const sqlparser_query_graph_view_t *graph,
	json_t *array,
	sqlparser_index_span_t span,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < span.count; index++) {
		size_t branch_index;
		sqlparser_graph_dml_branch_t branch;
		json_t *entry;

		if (sqlparser_query_graph_span_index_at(graph, span, index, &branch_index, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_branch_at(graph, branch_index, &branch, out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		entry = sqlparser_graph_dml_branch_json(graph, &branch, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(array, &entry) != 0) {
			json_decref(entry);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
	}
	return 0;
}

static json_t *sqlparser_graph_dml_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_dml_t *dml,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *target_columns;
	json_t *rows;
	json_t *assignments;
	json_t *delete_targets;
	json_t *branches;

	object = json_object();
	target_columns = json_array();
	rows = json_array();
	assignments = json_array();
	delete_targets = sqlparser_graph_span_json(graph, dml->delete_targets, out_error);
	branches = json_array();
	if (object == NULL || target_columns == NULL || rows == NULL || assignments == NULL || delete_targets == NULL || branches == NULL) {
		goto fail;
	}
	if (sqlparser_graph_append_dml_column_objects(graph, target_columns, dml->target_columns, out_error) != 0 ||
	    sqlparser_graph_append_dml_cell_objects(graph, rows, dml->rows, out_error) != 0 ||
	    sqlparser_graph_append_dml_assignment_objects(graph, assignments, dml->assignments, out_error) != 0 ||
	    sqlparser_graph_append_dml_branch_objects(graph, branches, dml->branches, out_error) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_dml_kind_name(dml->kind))) != 0 ||
	    json_object_set_new(object, "insert_mode", json_string(sqlparser_graph_insert_mode_name(dml->insert_mode))) != 0 ||
	    sqlparser_json_set_optional_size(object, "target_relation", dml->has_target_relation, dml->target_relation_index) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "target_columns", &target_columns) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "rows", &rows) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "assignments", &assignments) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "delete_targets", &delete_targets) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_nonempty_array(object, "branches", &branches) != 0) {
		goto fail;
	}
	if (sqlparser_json_set_optional_size(object, "source_block", dml->has_source_block, dml->source_block_index) != 0) {
		goto fail;
	}
	return object;

fail:
	json_decref(object);
	json_decref(target_columns);
	json_decref(rows);
	json_decref(assignments);
	json_decref(delete_targets);
	json_decref(branches);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
}

static json_t *sqlparser_graph_json_from_view(
	const sqlparser_handle_t *handle,
	const sqlparser_query_graph_view_t *graph,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *blocks;
	json_t *relations;
	json_t *targets;
	json_t *fields;
	json_t *values;
	json_t *sets;
	size_t index;

	object = json_object();
	blocks = json_array();
	relations = json_array();
	targets = json_array();
	fields = json_array();
	values = json_array();
	sets = json_array();
	if (object == NULL || blocks == NULL || relations == NULL || targets == NULL ||
	    fields == NULL || values == NULL || sets == NULL ||
	    sqlparser_json_set_optional_size(object, "root", graph->has_root_block, graph->root_block_index) != 0) {
		json_decref(object);
		json_decref(blocks);
		json_decref(relations);
		json_decref(targets);
		json_decref(fields);
		json_decref(values);
		json_decref(sets);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	for (index = 0U; index < graph->block_count; index++) {
		sqlparser_graph_block_t block;
		json_t *entry;
		if (sqlparser_query_graph_block_at(graph, index, &block, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			return NULL;
		}
		entry = sqlparser_graph_block_json(graph, &block, out_error);
			if (entry == NULL || sqlparser_json_array_append_owned(blocks, &entry) != 0) {
				json_decref(entry);
				json_decref(object);
				json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	for (index = 0U; index < graph->relation_count; index++) {
		sqlparser_graph_relation_t relation;
		json_t *entry;
		if (sqlparser_query_graph_relation_at(graph, index, &relation, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			return NULL;
		}
		entry = sqlparser_graph_relation_json(&relation, out_error);
			if (entry == NULL || sqlparser_json_array_append_owned(relations, &entry) != 0) {
				json_decref(entry);
				json_decref(object);
				json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	for (index = 0U; index < graph->target_count; index++) {
		sqlparser_graph_target_t target;
		json_t *entry;
		if (sqlparser_query_graph_target_at(graph, index, &target, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			return NULL;
		}
		entry = sqlparser_graph_target_json(graph, &target, out_error);
			if (entry == NULL || sqlparser_json_array_append_owned(targets, &entry) != 0) {
				json_decref(entry);
				json_decref(object);
				json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	for (index = 0U; index < graph->field_count; index++) {
		sqlparser_graph_field_t field;
		json_t *entry;
		if (sqlparser_query_graph_field_at(graph, index, &field, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			return NULL;
		}
		entry = sqlparser_graph_field_json(graph, &field, out_error);
			if (entry == NULL || sqlparser_json_array_append_owned(fields, &entry) != 0) {
				json_decref(entry);
				json_decref(object);
				json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		json_t *entry;
		if (sqlparser_query_graph_value_at(graph, index, &value, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			return NULL;
		}
		entry = sqlparser_graph_value_json(graph, &value, out_error);
			if (entry == NULL || sqlparser_json_array_append_owned(values, &entry) != 0) {
				json_decref(entry);
				json_decref(object);
				json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	for (index = 0U; index < graph->set_count; index++) {
		sqlparser_graph_set_t set_item;
		json_t *entry;
		if (sqlparser_query_graph_set_at(graph, index, &set_item, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			return NULL;
		}
		entry = sqlparser_graph_set_json(handle, graph, &set_item, out_error);
			if (entry == NULL || sqlparser_json_array_append_owned(sets, &entry) != 0) {
				json_decref(entry);
				json_decref(object);
				json_decref(blocks);
			json_decref(relations);
			json_decref(targets);
			json_decref(fields);
			json_decref(values);
			json_decref(sets);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	if (sqlparser_json_set_nonempty_array(object, "blocks", &blocks) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "relations", &relations) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "targets", &targets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "fields", &fields) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "values", &values) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "sets", &sets) != 0) {
		json_decref(object);
		json_decref(blocks);
		json_decref(relations);
		json_decref(targets);
		json_decref(fields);
		json_decref(values);
		json_decref(sets);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	if (graph->has_dml) {
		sqlparser_graph_dml_t dml;
		json_t *entry;

		if (sqlparser_query_graph_dml(graph, &dml, out_error) != SQLPARSER_STATUS_OK) {
			json_decref(object);
			return NULL;
		}
		entry = sqlparser_graph_dml_json(graph, &dml, out_error);
		if (entry == NULL || sqlparser_json_object_set_owned(object, "dml", &entry) != 0) {
			json_decref(entry);
			json_decref(object);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	return object;
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
	if (root == NULL || statements == NULL ||
	    sqlparser_json_object_set_owned(root, "statements", &statements) != 0) {
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
		PgQuery__Node *statement_node;
		sqlparser_query_graph_view_t graph;
		json_t *statement_json;
		json_t *graph_json;
		const char *keyword;

		statement_node = NULL;
		status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement_node, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			return status;
		}
		keyword = sqlparser_statement_keyword_for_handle(handle, statement_node);
		statement_json = json_object();
		if (statement_json == NULL ||
		    sqlparser_json_set_size(statement_json, "index", statement_index) != 0 ||
		    json_object_set_new(statement_json, "keyword", json_string(keyword != NULL ? keyword : "")) != 0) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		memset(&graph, 0, sizeof(graph));
		status = sqlparser_statement_query_graph(handle, statement_index, &graph, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			return status;
		}
		graph_json = sqlparser_graph_json_from_view(handle, &graph, out_error);
		if (graph_json == NULL ||
		    sqlparser_json_object_set_owned(statement_json, "query_graph", &graph_json) != 0) {
			json_decref(graph_json);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
				sqlparser_handle_clear_ast(mutable_handle);
			}
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			}
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		if (sqlparser_json_array_append_owned(json_object_get(root, "statements"), &statement_json) != 0) {
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_query_graph(mutable_handle);
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
			sqlparser_handle_clear_query_graph(mutable_handle);
			sqlparser_handle_clear_ast(mutable_handle);
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_validate_handle_output_text(handle, json_text, "View JSON", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(json_text);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_query_graph(mutable_handle);
			sqlparser_handle_clear_ast(mutable_handle);
		}
		return status;
	}

	*out_json = json_text;
	if (!ast_was_loaded) {
		sqlparser_handle_clear_query_graph(mutable_handle);
		sqlparser_handle_clear_ast(mutable_handle);
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_clause_sql(
	const sqlparser_clause_view_t *clause,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (clause == NULL || clause->handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_view_full_clause_sql(
		clause->handle,
		clause->statement_index,
		clause->clause_index,
		out_sql,
		out_error);
}
