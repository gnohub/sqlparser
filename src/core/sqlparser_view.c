#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "../dialect/sqlparser_dialect_multi_insert_internal.h"
#include "sqlparser_ast_internal.h"

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
	const void *pointer;
	size_t index;
} sqlparser_graph_pointer_index_t;

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
	    (sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
	     sqlparser_dialect_is_sqlserver_compatible(handle->dialect))) {
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
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE)) {
		return "create_application_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE)) {
		return "alter_application_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE)) {
		return "drop_application_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM)) {
		return "create_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM)) {
		return "drop_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE)) {
		return "create_type";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE)) {
		return "alter_database";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX)) {
		return "drop_index";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS)) {
		return "update_statistics";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT)) {
		return "set";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER)) {
		return "create_user";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER)) {
		return "alter_user";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE)) {
		return "create_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE)) {
		return "alter_role";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA)) {
		return "alter_schema";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION)) {
		return "alter_authorization";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM)) {
		return "create_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM)) {
		return "drop_synonym";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN)) {
		return "explain_plan";
	}
	return "set";
}

static const char *sqlparser_statement_keyword_for_handle(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__Node *statement)
{
	PgQuery__VariableSetStmt *stmt;
	const char *dialect_keyword;

	if (handle != NULL &&
	    handle->dialect_ops != NULL &&
	    handle->dialect_ops->statement_keyword != NULL) {
		dialect_keyword = handle->dialect_ops->statement_keyword(
			handle->dialect_state,
			statement_index,
			statement);
		if (dialect_keyword != NULL) {
			return dialect_keyword;
		}
	}

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL) {
		return sqlparser_statement_keyword_from_node(statement);
	}

	stmt = statement->variable_set_stmt;
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE)) {
		if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
			return "alter_session";
		}
		if (handle != NULL &&
		    (sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
		     sqlparser_dialect_is_sqlserver_compatible(handle->dialect))) {
			return "use";
		}
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) &&
	    handle != NULL &&
	    sqlparser_dialect_is_oracle_or_dameng_compatible(handle->dialect)) {
		return "alter_session";
	}
	if (handle != NULL &&
	    sqlparser_dialect_is_oracle_compatible(handle->dialect) &&
	    sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX)) {
		return "alter_session";
	}
	if (handle != NULL &&
	    sqlparser_dialect_is_oracle_compatible(handle->dialect) &&
	    (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_CREATE_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_DROP_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXPLAIN_PLAN))) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	if (handle != NULL &&
	    handle->dialect == SQLPARSER_DIALECT_DAMENG &&
	    sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_DAMENG_SESSION_PARAM_PREFIX)) {
		return "alter_session";
	}
	if (sqlparser_variable_set_is_prepared_statement(stmt)) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	if (handle != NULL && sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	    (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA) ||
	     sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION))) {
		return sqlparser_variable_set_column_keyword(handle, stmt);
	}
	return "set";
}

static const char *sqlparser_view_func_call_name(const PgQuery__FuncCall *func_call)
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
	return name;
}

static char *sqlparser_view_func_call_name_dup(const PgQuery__FuncCall *func_call)
{
	return sqlparser_view_ascii_upper_dup(sqlparser_view_func_call_name(func_call));
}

static int sqlparser_view_func_call_is_name(
	const PgQuery__FuncCall *func_call,
	const char *expected_name)
{
	const char *name;

	if (expected_name == NULL) {
		return 0;
	}
	name = sqlparser_view_func_call_name(func_call);
	return name != NULL && sqlparser_text_equal_ci(name, expected_name);
}

static int sqlparser_view_func_call_is_mysql_join_on(const PgQuery__FuncCall *func_call)
{
	return sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_JOIN_ON) ||
		sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_LEFT_JOIN_ON) ||
		sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_RIGHT_JOIN_ON) ||
		sqlparser_view_func_call_is_name(func_call, SQLPARSER_INTERNAL_MYSQL_CROSS_JOIN_ON);
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

static int sqlparser_view_dialect_uses_at_binds(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_SQLSERVER ||
	       dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
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
			(sqlparser_view_dialect_uses_at_binds(handle->dialect) && sql[index] == '@' &&
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
	size_t predicate_offset;
	size_t predicate_count;
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
	size_t predicate_count;
	size_t predicate_capacity;
	sqlparser_graph_predicate_t *predicates;
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
	PgQuery__CommonTableExpr *cte;
	size_t block_index;
	int has_block;
	int building;
} sqlparser_graph_cte_entry_t;

typedef struct {
	sqlparser_handle_t *handle;
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;
	size_t statement_index;
	PgQuery__Node *statement_node;
	sqlparser_graph_scope_t scopes[32];
	size_t scope_count;
	sqlparser_target_path_entry_t target_path[SQLPARSER_TARGET_PATH_CAPACITY];
	size_t target_path_count;
	int selector_cache_built;
	int selector_cache_failed;
	sqlparser_graph_pointer_index_t *node_indices;
	size_t node_index_count;
	size_t node_index_capacity;
	sqlparser_graph_pointer_index_t *relation_indices;
	size_t relation_index_count;
	size_t relation_index_capacity;
	sqlparser_graph_pointer_index_t *select_target_list_indices;
	size_t select_target_list_index_count;
	size_t select_target_list_index_capacity;
	sqlparser_graph_pointer_index_t *name_indices;
	size_t name_index_count;
	size_t name_index_capacity;
	sqlparser_graph_cte_entry_t *cte_entries;
	size_t cte_entry_count;
	size_t cte_entry_capacity;
	sqlparser_graph_cte_entry_t *registering_cte;
	PgQuery__SelectStmt *registering_cte_stmt;
} sqlparser_graph_build_t;

static int sqlparser_graph_selector_cache_append(
	sqlparser_graph_pointer_index_t **items,
	size_t *count,
	size_t *capacity,
	const void *pointer,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_pointer_index_t *new_items;
	size_t new_capacity;

	if (items == NULL || count == NULL || capacity == NULL || pointer == NULL) {
		return 0;
	}
	if (*count >= *capacity) {
		new_capacity = *capacity != 0U ? *capacity : 64U;
		while (new_capacity <= *count) {
			if (new_capacity > ((size_t)-1) / 2U) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph selector cache is too large");
				return -1;
			}
			new_capacity *= 2U;
		}
		if (new_capacity > ((size_t)-1) / sizeof(*new_items)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph selector cache is too large");
			return -1;
		}
		new_items = (sqlparser_graph_pointer_index_t *)realloc(*items, new_capacity * sizeof(*new_items));
		if (new_items == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
		*items = new_items;
		*capacity = new_capacity;
	}
	(*items)[*count].pointer = pointer;
	(*items)[*count].index = *count;
	(*count)++;
	return 0;
}

static size_t sqlparser_graph_selector_cache_find(
	const sqlparser_graph_pointer_index_t *items,
	size_t count,
	const void *pointer)
{
	size_t index;

	if (items == NULL || pointer == NULL) {
		return (size_t)-1;
	}
	for (index = 0U; index < count; index++) {
		if (items[index].pointer == pointer) {
			return items[index].index;
		}
	}
	return (size_t)-1;
}

static void sqlparser_graph_selector_cache_clear(sqlparser_graph_build_t *build)
{
	if (build == NULL) {
		return;
	}
	free(build->node_indices);
	free(build->relation_indices);
	free(build->select_target_list_indices);
	free(build->name_indices);
	build->node_indices = NULL;
	build->relation_indices = NULL;
	build->select_target_list_indices = NULL;
	build->name_indices = NULL;
	build->node_index_count = 0U;
	build->relation_index_count = 0U;
	build->select_target_list_index_count = 0U;
	build->name_index_count = 0U;
	build->node_index_capacity = 0U;
	build->relation_index_capacity = 0U;
	build->select_target_list_index_capacity = 0U;
	build->name_index_capacity = 0U;
	build->selector_cache_built = 0;
	build->selector_cache_failed = 0;
}

static void sqlparser_graph_build_clear(sqlparser_graph_build_t *build)
{
	if (build == NULL) {
		return;
	}
	sqlparser_graph_selector_cache_clear(build);
	free(build->cte_entries);
	build->cte_entries = NULL;
	build->cte_entry_count = 0U;
	build->cte_entry_capacity = 0U;
	build->registering_cte = NULL;
	build->registering_cte_stmt = NULL;
}

static int sqlparser_graph_select_has_target_list(const PgQuery__SelectStmt *stmt)
{
	return stmt != NULL && stmt->n_target_list > 0U && stmt->target_list != NULL;
}

static int sqlparser_graph_message_is_name_container(const ProtobufCMessageDescriptor *descriptor)
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

static int sqlparser_graph_name_context_is_literal(const sqlparser_name_context_t *context)
{
	return context != NULL && context->owner_type != NULL &&
	       strcmp(context->owner_type, "AConst") == 0;
}

static sqlparser_name_context_t sqlparser_graph_next_name_context(
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	const sqlparser_name_context_t *context)
{
	sqlparser_name_context_t next_context;

	memset(&next_context, 0, sizeof(next_context));
	if (sqlparser_graph_message_is_name_container(descriptor)) {
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

static int sqlparser_graph_selector_cache_record_name(
	sqlparser_graph_build_t *build,
	const sqlparser_name_context_t *context,
	const ProtobufCMessageDescriptor *descriptor,
	const ProtobufCFieldDescriptor *field,
	char **slot,
	sqlparser_error_t *out_error)
{
	const char *owner_type;
	const char *field_name;

	if (slot == NULL || *slot == NULL || (*slot)[0] == '\0') {
		return 0;
	}
	if (sqlparser_graph_name_context_is_literal(context)) {
		return 0;
	}
	owner_type = NULL;
	field_name = NULL;
	if (sqlparser_graph_message_is_name_container(descriptor) && context != NULL) {
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
		return 0;
	}
	return sqlparser_graph_selector_cache_append(
		&build->name_indices,
		&build->name_index_count,
		&build->name_index_capacity,
		slot,
		out_error);
}

static int sqlparser_graph_selector_cache_collect(
	sqlparser_graph_build_t *build,
	ProtobufCMessage *message,
	const sqlparser_name_context_t *context,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (build == NULL || message == NULL) {
		return 0;
	}
	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return 0;
	}
	if (descriptor == &pg_query__range_var__descriptor &&
	    sqlparser_graph_selector_cache_append(
		    &build->relation_indices,
		    &build->relation_index_count,
		    &build->relation_index_capacity,
		    message,
		    out_error) != 0) {
		return -1;
	}
	if (descriptor == &pg_query__select_stmt__descriptor &&
	    sqlparser_graph_select_has_target_list((PgQuery__SelectStmt *)message) &&
	    sqlparser_graph_selector_cache_append(
		    &build->select_target_list_indices,
		    &build->select_target_list_index_count,
		    &build->select_target_list_index_capacity,
		    message,
		    out_error) != 0) {
		return -1;
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

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(char ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					if (sqlparser_graph_selector_cache_record_name(
						    build,
						    context,
						    descriptor,
						    field,
						    items != NULL ? &items[item_index] : NULL,
						    out_error) != 0) {
						return -1;
					}
				}
			} else if (sqlparser_graph_selector_cache_record_name(
					   build,
					   context,
					   descriptor,
					   field,
					   (char **)(base + field->offset),
					   out_error) != 0) {
				return -1;
			}
			continue;
		}

		if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			sqlparser_name_context_t next_context;

			next_context = sqlparser_graph_next_name_context(descriptor, field, context);
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				size_t item_count;
				ProtobufCMessage **items;
				size_t item_index;

				item_count = *(const size_t *)(base + field->quantifier_offset);
				items = *(ProtobufCMessage ***)(base + field->offset);
				for (item_index = 0U; item_index < item_count; item_index++) {
					ProtobufCMessage *child;

					child = items != NULL ? items[item_index] : NULL;
					if (child == NULL) {
						continue;
					}
					if (child->descriptor == &pg_query__node__descriptor &&
					    sqlparser_graph_selector_cache_append(
						    &build->node_indices,
						    &build->node_index_count,
						    &build->node_index_capacity,
						    child,
						    out_error) != 0) {
						return -1;
					}
					if (sqlparser_graph_selector_cache_collect(build, child, &next_context, out_error) != 0) {
						return -1;
					}
				}
			} else {
				ProtobufCMessage *child;

				child = *(ProtobufCMessage **)(base + field->offset);
				if (child == NULL) {
					continue;
				}
				if (child->descriptor == &pg_query__node__descriptor &&
				    sqlparser_graph_selector_cache_append(
					    &build->node_indices,
					    &build->node_index_count,
					    &build->node_index_capacity,
					    child,
					    out_error) != 0) {
					return -1;
				}
				if (sqlparser_graph_selector_cache_collect(build, child, &next_context, out_error) != 0) {
					return -1;
				}
			}
		}
	}
	return 0;
}

static int sqlparser_graph_ensure_selector_cache(
	sqlparser_graph_build_t *build,
	sqlparser_error_t *out_error)
{
	if (build == NULL || build->statement_node == NULL) {
		return -1;
	}
	if (build->selector_cache_built) {
		return build->selector_cache_failed ? -1 : 0;
	}
	build->selector_cache_built = 1;
	if (sqlparser_graph_selector_cache_collect(build, (ProtobufCMessage *)build->statement_node, NULL, out_error) != 0) {
		sqlparser_graph_selector_cache_clear(build);
		build->selector_cache_built = 1;
		build->selector_cache_failed = 1;
		return -1;
	}
	return 0;
}

static size_t sqlparser_graph_find_cached_value_index(
	sqlparser_graph_build_t *build,
	PgQuery__Node *node)
{
	sqlparser_view_build_t fallback_build;

	if (node == NULL) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		return sqlparser_graph_selector_cache_find(
			build->node_indices,
			build->node_index_count,
			node);
	}
	memset(&fallback_build, 0, sizeof(fallback_build));
	fallback_build.handle = build != NULL ? build->handle : NULL;
	fallback_build.statement_index = build != NULL ? build->statement_index : 0U;
	return sqlparser_view_find_value_index(&fallback_build, node);
}

static size_t sqlparser_graph_find_cached_name_index(
	sqlparser_graph_build_t *build,
	char **slot)
{
	sqlparser_view_build_t fallback_build;

	if (slot == NULL) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		return sqlparser_graph_selector_cache_find(
			build->name_indices,
			build->name_index_count,
			slot);
	}
	memset(&fallback_build, 0, sizeof(fallback_build));
	fallback_build.handle = build != NULL ? build->handle : NULL;
	fallback_build.statement_index = build != NULL ? build->statement_index : 0U;
	return sqlparser_view_find_name_selector_index(&fallback_build, slot);
}

static size_t sqlparser_graph_find_cached_select_target_list_index(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt)
{
	size_t index;

	if (stmt == NULL || !sqlparser_graph_select_has_target_list(stmt)) {
		return (size_t)-1;
	}
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		return sqlparser_graph_selector_cache_find(
			build->select_target_list_indices,
			build->select_target_list_index_count,
			stmt);
	}
	if (build == NULL ||
	    sqlparser_find_select_target_list_index_by_stmt(
		    build->handle,
		    build->statement_index,
		    stmt,
		    &index,
		    NULL) != SQLPARSER_STATUS_OK) {
		return (size_t)-1;
	}
	return index;
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
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error);

static int sqlparser_query_graph_reserve_array_with_initial(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	size_t initial_capacity,
	sqlparser_error_t *out_error)
{
	void *new_items;
	size_t new_capacity;

	if (items == NULL || capacity == NULL || item_size == 0U || initial_capacity == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "invalid graph array");
		return -1;
	}
	if (required <= *capacity) {
		return 0;
	}
	new_capacity = *capacity != 0U ? *capacity : initial_capacity;
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

static int sqlparser_query_graph_reserve_array(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	sqlparser_error_t *out_error)
{
	return sqlparser_query_graph_reserve_array_with_initial(
		items,
		capacity,
		required,
		item_size,
		16U,
		out_error);
}

static int sqlparser_query_graph_reserve_sparse_array(
	void **items,
	size_t *capacity,
	size_t required,
	size_t item_size,
	sqlparser_error_t *out_error)
{
	return sqlparser_query_graph_reserve_array_with_initial(
		items,
		capacity,
		required,
		item_size,
		4U,
		out_error);
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
	free(cache->predicates);
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

static size_t sqlparser_graph_local_predicate_count(const sqlparser_graph_build_t *build)
{
	return build->cache->predicate_count - build->statement->predicate_offset;
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

static sqlparser_graph_field_t *sqlparser_graph_field_by_local(
	sqlparser_graph_build_t *build,
	size_t field_index)
{
	if (build == NULL || build->statement == NULL ||
	    field_index >= sqlparser_graph_local_field_count(build)) {
		return NULL;
	}
	return &build->cache->fields[build->statement->field_offset + field_index];
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
		relation->link_name = relation_view->link_name;
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
	if (source != NULL && source->has_selector) {
		size_t index;

		for (index = 0U; index < build->cache->value_count; index++) {
			sqlparser_graph_value_t *existing;

			existing = &build->cache->values[index];
			if (existing->statement_index == build->statement_index &&
			    existing->has_selector &&
			    sqlparser_graph_selector_equal(&existing->selector, &source->selector) &&
			    existing->kind == source->kind &&
			    existing->has_field == source->has_field &&
			    (!source->has_field || existing->field_index == source->field_index) &&
			    existing->has_source_field == source->has_source_field &&
			    (!source->has_source_field || existing->source_field_index == source->source_field_index) &&
			    existing->field_match_kind == source->field_match_kind) {
				if (out_index != NULL) {
					*out_index = existing->index;
				}
				return 0;
			}
		}
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

static int sqlparser_graph_add_predicate(
	sqlparser_graph_build_t *build,
	const sqlparser_graph_predicate_t *source,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t *predicate;
	size_t global_index;
	size_t local_index;

	if (out_index != NULL) {
		*out_index = 0U;
	}
	if (sqlparser_query_graph_reserve_array(
		    (void **)&build->cache->predicates,
		    &build->cache->predicate_capacity,
		    build->cache->predicate_count + 1U,
		    sizeof(*build->cache->predicates),
		    out_error) != 0) {
		return -1;
	}
	global_index = build->cache->predicate_count++;
	local_index = sqlparser_graph_local_predicate_count(build) - 1U;
	predicate = &build->cache->predicates[global_index];
	memset(predicate, 0, sizeof(*predicate));
	if (source != NULL) {
		*predicate = *source;
	}
	predicate->index = local_index;
	predicate->statement_index = build->statement_index;
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
		NULL,
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
	if (name == NULL) {
		return 0;
	}
	if (sqlparser_text_equal_ci(name, "rowid")) {
		return 1;
	}
	return (qualifier == NULL || qualifier[0] == '\0') &&
		sqlparser_text_equal_ci(name, "rownum");
}

static int sqlparser_graph_column_ref_is_recordable_field(PgQuery__ColumnRef *column_ref)
{
	const char *name;

	if (column_ref == NULL || sqlparser_graph_column_ref_is_pseudo(column_ref)) {
		return 0;
	}
	name = sqlparser_graph_column_ref_part(column_ref, 0U);
	return name != NULL && name[0] != '\0' && strcmp(name, "*") != 0;
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
	if (sqlparser_graph_ensure_selector_cache(build, NULL) == 0) {
		index = sqlparser_graph_selector_cache_find(
			build->relation_indices,
			build->relation_index_count,
			range_var);
		return index;
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
	const char *column_name;
	const char *qualifier;
	size_t name_index;

	if (out_field_index != NULL) {
		*out_field_index = 0U;
	}
	column_name = sqlparser_graph_column_ref_part(column_ref, 0U);
	if (!sqlparser_graph_column_ref_is_recordable_field(column_ref)) {
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
	name_index = sqlparser_graph_find_cached_name_index(build, sqlparser_graph_column_ref_name_slot(column_ref));
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

static const char *sqlparser_graph_like_escape_kind_name(sqlparser_graph_like_escape_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL:
			return "literal";
		case SQLPARSER_GRAPH_LIKE_ESCAPE_BIND:
			return "bind";
		case SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION:
			return "expression";
		case SQLPARSER_GRAPH_LIKE_ESCAPE_NONE:
		default:
			return "none";
	}
}

static int sqlparser_graph_fill_bind_fields(
	sqlparser_graph_build_t *build,
	PgQuery__Node *value_node,
	char *bind,
	size_t bind_size,
	int *has_bind,
	sqlparser_bind_kind_t *bind_kind,
	char *bind_sql,
	size_t bind_sql_size,
	int *has_bind_sql,
	size_t *bind_position,
	int *has_bind_position,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_view_bind_info_t bind_info;
	sqlparser_status_t status;
	int bind_status;

	if (bind != NULL && bind_size > 0U) {
		bind[0] = '\0';
	}
	if (bind_sql != NULL && bind_sql_size > 0U) {
		bind_sql[0] = '\0';
	}
	if (has_bind != NULL) {
		*has_bind = 0;
	}
	if (bind_kind != NULL) {
		*bind_kind = SQLPARSER_BIND_KIND_NONE;
	}
	if (has_bind_sql != NULL) {
		*has_bind_sql = 0;
	}
	if (bind_position != NULL) {
		*bind_position = 0U;
	}
	if (has_bind_position != NULL) {
		*has_bind_position = 0;
	}
	if (build == NULL || value_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "bind node is missing");
		return -1;
	}

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
	sqlparser_view_copy_public_text(bind, bind_size, bind_status > 0 ? bind_info.name : NULL, NULL);
	sqlparser_view_copy_public_text(bind_sql, bind_sql_size, public_sql, NULL);
	if (has_bind != NULL) {
		*has_bind = bind != NULL && bind[0] != '\0';
	}
	if (bind_kind != NULL) {
		*bind_kind = bind_status > 0 ? bind_info.kind : SQLPARSER_BIND_KIND_NONE;
	}
	if (bind_position != NULL) {
		*bind_position = bind_status > 0 ? bind_info.position : 0U;
	}
	if (has_bind_position != NULL && bind_position != NULL) {
		*has_bind_position = *bind_position != 0U;
	}
	if (has_bind_sql != NULL) {
		*has_bind_sql = bind_sql != NULL && bind_sql[0] != '\0';
	}
	sqlparser_view_bind_info_release(&bind_info);
	free(public_sql);
	return 0;
}

static sqlparser_graph_operator_kind_t sqlparser_graph_operator_kind_from_name(const char *operator_name)
{
	if (operator_name == NULL) {
		return SQLPARSER_GRAPH_OPERATOR_UNKNOWN;
	}
	if (strcmp(operator_name, "LIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_LIKE;
	}
	if (strcmp(operator_name, "NOT LIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_NOT_LIKE;
	}
	if (strcmp(operator_name, "ILIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_ILIKE;
	}
	if (strcmp(operator_name, "NOT ILIKE") == 0) {
		return SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE;
	}
	return SQLPARSER_GRAPH_OPERATOR_UNKNOWN;
}

static int sqlparser_graph_operator_is_like(const char *operator_name)
{
	return sqlparser_graph_operator_is_like_pattern(sqlparser_graph_operator_kind_from_name(operator_name));
}

static int sqlparser_graph_func_call_is_pg_like_escape(const PgQuery__FuncCall *func_call)
{
	const char *schema_name;
	const char *function_name;

	if (func_call == NULL ||
	    func_call->n_funcname < 2U ||
	    func_call->funcname == NULL ||
	    !sqlparser_node_string_value(func_call->funcname[func_call->n_funcname - 2U], &schema_name) ||
	    !sqlparser_node_string_value(func_call->funcname[func_call->n_funcname - 1U], &function_name)) {
		return 0;
	}
	return sqlparser_text_equal_ci(schema_name, "pg_catalog") &&
		sqlparser_text_equal_ci(function_name, "like_escape");
}

static int sqlparser_graph_split_like_escape(
	const PgQuery__AExpr *a_expr,
	PgQuery__Node *node,
	PgQuery__Node **out_pattern,
	PgQuery__Node **out_escape)
{
	PgQuery__FuncCall *func_call;

	if (out_pattern != NULL) {
		*out_pattern = node;
	}
	if (out_escape != NULL) {
		*out_escape = NULL;
	}
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_FUNC_CALL ||
	    node->func_call == NULL) {
		return 0;
	}
	func_call = node->func_call;
	if (!sqlparser_graph_func_call_is_pg_like_escape(func_call) ||
	    a_expr == NULL ||
	    a_expr->location < 0 ||
	    func_call->location != a_expr->location ||
	    func_call->n_args < 2U ||
	    func_call->args == NULL) {
		return 0;
	}
	if (out_pattern != NULL) {
		*out_pattern = func_call->args[0];
	}
	if (out_escape != NULL) {
		*out_escape = func_call->args[1];
	}
	return 1;
}

static int sqlparser_graph_like_escape_from_node(
	sqlparser_graph_build_t *build,
	PgQuery__Node *escape_node,
	sqlparser_graph_like_escape_t *out_escape,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (out_escape == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "LIKE escape output is missing");
		return -1;
	}
	memset(out_escape, 0, sizeof(*out_escape));
	if (escape_node == NULL) {
		return 0;
	}
	if (escape_node->node_case == PG_QUERY__NODE__NODE_A_CONST && escape_node->a_const != NULL) {
		out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL;
		status = sqlparser_fill_literal_view_from_a_const_with_sql(
			escape_node->a_const,
			build != NULL && build->handle != NULL ? sqlparser_effective_parser_sql(build->handle) : NULL,
			&out_escape->literal,
			out_error);
		return status == SQLPARSER_STATUS_OK ? 0 : -1;
	}
	if (escape_node->node_case == PG_QUERY__NODE__NODE_PARAM_REF && escape_node->param_ref != NULL) {
		out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_BIND;
		return sqlparser_graph_fill_bind_fields(
			build,
			escape_node,
			out_escape->bind,
			sizeof(out_escape->bind),
			&out_escape->has_bind,
			&out_escape->bind_kind,
			out_escape->bind_sql,
			sizeof(out_escape->bind_sql),
			&out_escape->has_bind_sql,
			&out_escape->bind_position,
			&out_escape->has_bind_position,
			out_error);
	}
	out_escape->kind = SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION;
	return 0;
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
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error)
{
	size_t value_index;

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
	out_value->operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
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
		if (sqlparser_graph_fill_bind_fields(
			    build,
			    value_node,
			    out_value->bind,
			    sizeof(out_value->bind),
			    &out_value->has_bind,
			    &out_value->bind_kind,
			    out_value->bind_sql,
			    sizeof(out_value->bind_sql),
			    &out_value->has_bind_sql,
			    &out_value->bind_position,
			    &out_value->has_bind_position,
			    out_error) != 0) {
			return -1;
		}
	} else if (value_node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
		out_value->kind = SQLPARSER_GRAPH_VALUE_DEFAULT;
	} else {
		return 0;
	}
	if (like_escape != NULL && like_escape->kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		out_value->like_escape = *like_escape;
	}
	value_index = sqlparser_graph_find_cached_value_index(build, value_node);
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
			 strcmp(operator_name, ">=") == 0 ||
			 strcmp(operator_name, "!<") == 0 ||
			 strcmp(operator_name, "!>") == 0);
}

static int sqlparser_graph_clause_records_field_values(
	sqlparser_clause_kind_t clause,
	const PgQuery__AExpr *a_expr)
{
	if (clause != SQLPARSER_CLAUSE_KIND_SELECT_LIST &&
	    clause != SQLPARSER_CLAUSE_KIND_WHERE &&
	    clause != SQLPARSER_CLAUSE_KIND_ON &&
	    clause != SQLPARSER_CLAUSE_KIND_HAVING) {
		return 0;
	}
	return sqlparser_graph_a_expr_is_select_predicate(a_expr);
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
	const sqlparser_graph_like_escape_t *like_escape,
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
		like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
	value.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	value.field_index = field_index;
	value.has_field = 1;
	value.field_match_kind = field_match_kind;
	value.kind = SQLPARSER_GRAPH_VALUE_EXPRESSION;
	if (like_escape != NULL && like_escape->kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		value.like_escape = *like_escape;
	}
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
	const sqlparser_graph_like_escape_t *like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
			like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
			like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
				like_escape,
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
				like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_error_t *out_error)
{
	size_t field_index;

	if (!sqlparser_graph_column_ref_is_recordable_field(column_ref)) {
		return 0;
	}
	field_index = 0U;
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
	return sqlparser_graph_record_value_nodes(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		1,
		field_match_kind,
		value_node,
		like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
				like_escape,
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
					like_escape,
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
					like_escape,
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
					like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
			like_escape,
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
	const sqlparser_graph_like_escape_t *like_escape,
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
		like_escape,
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
	sqlparser_graph_like_escape_t like_escape;
	PgQuery__Node *pattern_node;
	PgQuery__Node *escape_node;
	const sqlparser_graph_like_escape_t *like_escape_ptr;
	int value_status;

	pattern_node = right;
	escape_node = NULL;
	like_escape_ptr = NULL;
	if (sqlparser_graph_operator_is_like(operator_name) &&
	    sqlparser_graph_split_like_escape(a_expr, right, &pattern_node, &escape_node)) {
		if (sqlparser_graph_like_escape_from_node(build, escape_node, &like_escape, out_error) != 0) {
			return -1;
		}
		like_escape_ptr = like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE ? &like_escape : NULL;
	}

	if (!sqlparser_graph_clause_records_field_values(clause, a_expr) ||
	    left == NULL ||
	    pattern_node == NULL ||
	    !sqlparser_graph_node_has_recordable_value(pattern_node)) {
		return 0;
	}
	field_match_kind = sqlparser_graph_field_match_kind_from_expr(left);
	value_status = sqlparser_graph_record_predicate_field_values(
		build,
		block_index,
		clause,
		operator_name,
		left,
		pattern_node,
		target_index,
		has_target,
		field_match_kind,
		like_escape_ptr,
		out_error);
	return value_status;
}

static sqlparser_graph_predicate_bool_t sqlparser_graph_predicate_bool_from_pg(PgQuery__BoolExprType boolop)
{
	switch (boolop) {
		case PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_AND;
		case PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_OR;
		case PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_NOT;
		case PG_QUERY__BOOL_EXPR_TYPE__BOOL_EXPR_TYPE_UNDEFINED:
		default:
			return SQLPARSER_GRAPH_PREDICATE_BOOL_NONE;
	}
}

static int sqlparser_graph_add_predicate_value_from_node(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	size_t field_index,
	int has_field,
	sqlparser_graph_field_match_kind_t field_match_kind,
	PgQuery__Node *value_node,
	const sqlparser_graph_like_escape_t *like_escape,
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
		clause,
		operator_name,
		field_index,
		has_field,
		field_match_kind,
		value_node,
		like_escape,
		&value,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status == 0) {
		return 0;
	}
	return sqlparser_graph_add_value(build, &value, out_value_index, out_error) == 0 ? 1 : -1;
}

static int sqlparser_graph_add_predicate_field_value(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__ColumnRef *field_ref,
	PgQuery__Node *value_node,
	const sqlparser_graph_like_escape_t *like_escape,
	sqlparser_graph_predicate_t *predicate,
	sqlparser_error_t *out_error)
{
	size_t field_index;
	size_t value_index;
	int value_status;

	if (!sqlparser_graph_column_ref_is_recordable_field(field_ref)) {
		return 0;
	}
	field_index = 0U;
	if (sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    field_ref,
		    0U,
		    0,
		    &field_index,
		    out_error) != 0) {
		return -1;
	}
	predicate->left_field_index = field_index;
	predicate->has_left_field = 1;
	value_index = 0U;
	value_status = sqlparser_graph_add_predicate_value_from_node(
		build,
		block_index,
		clause,
		operator_name,
		field_index,
		1,
		SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD,
		value_node,
		like_escape,
		&value_index,
		out_error);
	if (value_status < 0) {
		return -1;
	}
	if (value_status > 0) {
		predicate->value_index = value_index;
		predicate->has_value = 1;
	}
	return 0;
}

static int sqlparser_graph_add_predicate_field_to_field_value(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	const char *operator_name,
	PgQuery__ColumnRef *left_ref,
	PgQuery__ColumnRef *right_ref,
	sqlparser_graph_predicate_t *predicate,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_value_t value;
	size_t left_field_index;
	size_t right_field_index;
	size_t value_index;

	if (!sqlparser_graph_column_ref_is_recordable_field(left_ref) ||
	    !sqlparser_graph_column_ref_is_recordable_field(right_ref)) {
		return 0;
	}
	left_field_index = 0U;
	right_field_index = 0U;
	if (sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    left_ref,
		    0U,
		    0,
		    &left_field_index,
		    out_error) != 0 ||
	    sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    right_ref,
		    0U,
		    0,
		    &right_field_index,
		    out_error) != 0) {
		return -1;
	}
	memset(&value, 0, sizeof(value));
	value.block_index = block_index;
	value.clause = clause;
	value.operator_name = operator_name;
	value.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	value.field_index = left_field_index;
	value.has_field = 1;
	value.source_field_index = right_field_index;
	value.has_source_field = 1;
	value.field_match_kind = SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD;
	value.kind = SQLPARSER_GRAPH_VALUE_FIELD;
	if (sqlparser_graph_add_value(build, &value, &value_index, out_error) != 0) {
		return -1;
	}
	predicate->left_field_index = left_field_index;
	predicate->has_left_field = 1;
	predicate->right_field_index = right_field_index;
	predicate->has_right_field = 1;
	predicate->value_index = value_index;
	predicate->has_value = 1;
	return 0;
}

static int sqlparser_graph_build_predicate_tree(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error);

static int sqlparser_graph_build_bool_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__BoolExpr *bool_expr,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	size_t predicate_index;
	size_t index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (bool_expr == NULL) {
		return 0;
	}
	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_BOOL;
	predicate.bool_operator = sqlparser_graph_predicate_bool_from_pg(bool_expr->boolop);
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	for (index = 0U; index < bool_expr->n_args; index++) {
		size_t child_index;
		int child_status;

		child_index = 0U;
		child_status = sqlparser_graph_build_predicate_tree(
			build,
			block_index,
			clause,
			bool_expr->args != NULL ? bool_expr->args[index] : NULL,
			&child_index,
			out_error);
		if (child_status < 0) {
			return -1;
		}
		if (child_status > 0 &&
		    sqlparser_graph_span_append_index(
			    build,
			    &build->cache->predicates[build->statement->predicate_offset + predicate_index].children,
			    child_index,
			    out_error) != 0) {
			return -1;
		}
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_clause_records_function_predicate(sqlparser_clause_kind_t clause)
{
	return clause == SQLPARSER_CLAUSE_KIND_WHERE ||
		clause == SQLPARSER_CLAUSE_KIND_ON ||
		clause == SQLPARSER_CLAUSE_KIND_HAVING;
}

static int sqlparser_graph_build_a_expr_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__AExpr *a_expr,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	sqlparser_graph_like_escape_t like_escape;
	PgQuery__Node *left;
	PgQuery__Node *right;
	PgQuery__Node *pattern_node;
	PgQuery__Node *escape_node;
	const sqlparser_graph_like_escape_t *like_escape_ptr;
	const char *operator_name;
	size_t predicate_index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (a_expr == NULL || !sqlparser_graph_clause_records_field_values(clause, a_expr)) {
		return 0;
	}
	operator_name = sqlparser_a_expr_operator_name(a_expr);
	left = a_expr->lexpr;
	right = a_expr->rexpr;
	pattern_node = right;
	escape_node = NULL;
	like_escape_ptr = NULL;
	if (sqlparser_graph_operator_is_like(operator_name) &&
	    sqlparser_graph_split_like_escape(a_expr, right, &pattern_node, &escape_node)) {
		if (sqlparser_graph_like_escape_from_node(build, escape_node, &like_escape, out_error) != 0) {
			return -1;
		}
		like_escape_ptr = like_escape.kind != SQLPARSER_GRAPH_LIKE_ESCAPE_NONE ? &like_escape : NULL;
	}

	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_COMPARISON;
	predicate.operator_name = operator_name;
	predicate.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	if (left != NULL && left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	    pattern_node != NULL && pattern_node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF) {
		if (sqlparser_graph_add_predicate_field_to_field_value(
			    build,
			    block_index,
			    clause,
			    operator_name,
			    left->column_ref,
			    pattern_node->column_ref,
			    &predicate,
			    out_error) != 0) {
			return -1;
		}
	} else if (left != NULL && left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	           pattern_node != NULL && sqlparser_graph_node_has_recordable_value(pattern_node)) {
		if (sqlparser_graph_add_predicate_field_value(
			    build,
			    block_index,
			    clause,
			    operator_name,
			    left->column_ref,
			    pattern_node,
			    like_escape_ptr,
			    &predicate,
			    out_error) != 0) {
			return -1;
		}
	} else if (pattern_node != NULL && pattern_node->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
	           left != NULL && sqlparser_graph_node_has_recordable_value(left)) {
		if (sqlparser_graph_add_predicate_field_value(
			    build,
			    block_index,
			    clause,
			    operator_name,
			    pattern_node->column_ref,
			    left,
			    like_escape_ptr,
			    &predicate,
			    out_error) != 0) {
			return -1;
		}
	} else {
		predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXPRESSION;
		if (left != NULL &&
		    left->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_graph_column_ref_is_recordable_field(left->column_ref)) {
			if (sqlparser_graph_add_column_ref_field(
				    build,
				    block_index,
				    clause,
				    left->column_ref,
				    0U,
				    0,
				    &predicate.left_field_index,
				    out_error) != 0) {
				return -1;
			}
			predicate.has_left_field = 1;
		}
	}
	if (!predicate.has_left_field && !predicate.has_right_field && !predicate.has_value) {
		return 0;
	}
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_build_func_call_predicate(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__FuncCall *func_call,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	const char *operator_name;
	PgQuery__ColumnRef *field_ref;
	PgQuery__Node *value_node;
	size_t predicate_index;
	size_t index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (func_call == NULL ||
	    func_call->n_args == 0U ||
	    func_call->args == NULL ||
	    !sqlparser_graph_clause_records_function_predicate(clause)) {
		return 0;
	}
	operator_name = sqlparser_view_func_call_name(func_call);
	if (operator_name == NULL || operator_name[0] == '\0') {
		return 0;
	}
	field_ref = NULL;
	value_node = NULL;
	for (index = 0U; index < func_call->n_args; index++) {
		PgQuery__Node *arg;

		arg = func_call->args[index];
		if (arg == NULL) {
			continue;
		}
		if (field_ref == NULL &&
		    arg->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    sqlparser_graph_column_ref_is_recordable_field(arg->column_ref)) {
			field_ref = arg->column_ref;
			continue;
		}
		if (value_node == NULL && sqlparser_graph_node_is_recordable_value(arg)) {
			value_node = arg;
		}
	}
	if (field_ref == NULL) {
		return 0;
	}
	memset(&predicate, 0, sizeof(predicate));
	predicate.block_index = block_index;
	predicate.clause = clause;
	predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXPRESSION;
	predicate.operator_name = operator_name;
	predicate.operator_kind = sqlparser_graph_operator_kind_from_name(operator_name);
	if (sqlparser_graph_add_predicate_field_value(
		    build,
		    block_index,
		    clause,
		    operator_name,
		    field_ref,
		    value_node,
		    NULL,
		    &predicate,
		    out_error) != 0) {
		return -1;
	}
	if (!predicate.has_left_field && !predicate.has_value) {
		return 0;
	}
	if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
		return -1;
	}
	if (out_predicate_index != NULL) {
		*out_predicate_index = predicate_index;
	}
	return 1;
}

static int sqlparser_graph_build_predicate_tree(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	size_t *out_predicate_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_predicate_t predicate;
	size_t predicate_index;

	if (out_predicate_index != NULL) {
		*out_predicate_index = 0U;
	}
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			return sqlparser_graph_build_bool_predicate(
				build,
				block_index,
				clause,
				node->bool_expr,
				out_predicate_index,
				out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
			return sqlparser_graph_build_a_expr_predicate(
				build,
				block_index,
				clause,
				node->a_expr,
				out_predicate_index,
				out_error);
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link == NULL ||
			    node->sub_link->sub_link_type != PG_QUERY__SUB_LINK_TYPE__EXISTS_SUBLINK) {
				return 0;
			}
			memset(&predicate, 0, sizeof(predicate));
			predicate.block_index = block_index;
			predicate.clause = clause;
			predicate.kind = SQLPARSER_GRAPH_PREDICATE_EXISTS;
			if (sqlparser_graph_add_predicate(build, &predicate, &predicate_index, out_error) != 0) {
				return -1;
			}
			if (out_predicate_index != NULL) {
				*out_predicate_index = predicate_index;
			}
			return 1;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call == NULL) {
				return 0;
			}
			if (sqlparser_view_func_call_is_mysql_join_on(node->func_call)) {
				if (node->func_call->n_args == 0U || node->func_call->args == NULL) {
					return 0;
				}
				return sqlparser_graph_build_predicate_tree(
					build,
					block_index,
					SQLPARSER_CLAUSE_KIND_ON,
					node->func_call->args[0],
					out_predicate_index,
					out_error);
			}
			return sqlparser_graph_build_func_call_predicate(
				build,
				block_index,
				clause,
				node->func_call,
				out_predicate_index,
				out_error);
		default:
			return 0;
	}
}

static int sqlparser_graph_walk_predicate_expr(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *node,
	sqlparser_error_t *out_error)
{
	int predicate_status;

	predicate_status = sqlparser_graph_build_predicate_tree(build, block_index, clause, node, NULL, out_error);
	if (predicate_status < 0) {
		return -1;
	}
	return sqlparser_graph_walk_expr(build, block_index, clause, node, 0U, 0, out_error);
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

static sqlparser_graph_cte_entry_t *sqlparser_graph_find_cte_entry(
	sqlparser_graph_build_t *build,
	PgQuery__CommonTableExpr *cte)
{
	size_t index;

	if (build == NULL || cte == NULL) {
		return NULL;
	}
	for (index = 0U; index < build->cte_entry_count; index++) {
		if (build->cte_entries[index].cte == cte) {
			return &build->cte_entries[index];
		}
	}
	return NULL;
}

static int sqlparser_graph_ensure_cte_block(
	sqlparser_graph_build_t *build,
	PgQuery__CommonTableExpr *cte,
	size_t *out_block_index,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_cte_entry_t *entry;
	PgQuery__SelectStmt *cte_stmt;
	int rc;

	if (out_block_index != NULL) {
		*out_block_index = 0U;
	}
	if (cte == NULL || cte->ctequery == NULL ||
	    cte->ctequery->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    cte->ctequery->select_stmt == NULL) {
		return 0;
	}
	entry = sqlparser_graph_find_cte_entry(build, cte);
	if (entry != NULL && entry->has_block) {
		if (out_block_index != NULL) {
			*out_block_index = entry->block_index;
		}
		return 0;
	}
	if (entry != NULL && entry->building) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "recursive CTE block is not registered");
		return -1;
	}
	if (entry == NULL) {
		if (sqlparser_query_graph_reserve_sparse_array(
			    (void **)&build->cte_entries,
			    &build->cte_entry_capacity,
			    build->cte_entry_count + 1U,
			    sizeof(*build->cte_entries),
			    out_error) != 0) {
			return -1;
		}
		entry = &build->cte_entries[build->cte_entry_count++];
		memset(entry, 0, sizeof(*entry));
		entry->cte = cte;
	}
	entry->building = 1;
	cte_stmt = cte->ctequery->select_stmt;
	build->registering_cte = entry;
	build->registering_cte_stmt = cte_stmt;
	rc = sqlparser_graph_build_select(
		build,
		cte_stmt,
		SQLPARSER_GRAPH_BLOCK_CTE,
		NULL,
		out_error);
	if (build->registering_cte != NULL && build->registering_cte->cte == cte) {
		build->registering_cte = NULL;
		build->registering_cte_stmt = NULL;
	}
	entry = sqlparser_graph_find_cte_entry(build, cte);
	if (rc != 0 || entry == NULL || !entry->has_block) {
		if (rc == 0) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "CTE block was not registered");
		}
		return -1;
	}
	entry->building = 0;
	if (out_block_index != NULL) {
		*out_block_index = entry->block_index;
	}
	return 0;
}

static void sqlparser_graph_register_cte_block(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	size_t block_index)
{
	if (build == NULL || build->registering_cte == NULL ||
	    build->registering_cte_stmt != stmt) {
		return;
	}
	build->registering_cte->block_index = block_index;
	build->registering_cte->has_block = 1;
	build->registering_cte = NULL;
	build->registering_cte_stmt = NULL;
}

static int sqlparser_graph_ensure_select_ctes(
	sqlparser_graph_build_t *build,
	PgQuery__SelectStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL || stmt->with_clause == NULL) {
		return 0;
	}
	for (index = 0U; index < stmt->with_clause->n_ctes; index++) {
		PgQuery__Node *node;

		node = stmt->with_clause->ctes[index];
		if (node != NULL && node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
		    node->common_table_expr != NULL &&
		    sqlparser_graph_ensure_cte_block(build, node->common_table_expr, NULL, out_error) != 0) {
			return -1;
		}
	}
	return 0;
}

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

				if (sqlparser_view_func_call_is_mysql_join_on(node->func_call)) {
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

static int sqlparser_graph_add_using_field(
	sqlparser_graph_build_t *build,
	size_t block_index,
	PgQuery__Node *using_node,
	size_t relation_begin,
	size_t relation_end,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_field_t field;
	size_t relation_index;
	size_t name_index;

	if (using_node == NULL || using_node->node_case != PG_QUERY__NODE__NODE_STRING ||
	    using_node->string == NULL || using_node->string->sval == NULL) {
		return 0;
	}
	memset(&field, 0, sizeof(field));
	field.block_index = block_index;
	field.clause = SQLPARSER_CLAUSE_KIND_ON;
	field.column_name = using_node->string->sval;
	for (relation_index = relation_begin; relation_index < relation_end; relation_index++) {
		sqlparser_graph_relation_t *relation;

		relation = sqlparser_graph_relation_by_local(build, relation_index);
		if (relation != NULL && relation->block_index == block_index &&
		    sqlparser_graph_span_append_index(
			    build,
			    &field.candidate_relations,
			    relation_index,
			    out_error) != 0) {
			return -1;
		}
	}
	name_index = sqlparser_graph_find_cached_name_index(build, &using_node->string->sval);
	if (name_index != (size_t)-1) {
		field.selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
		field.selector.statement_index = build->statement_index;
		field.selector.item_index = name_index;
		field.has_selector = 1;
	}
	return sqlparser_graph_add_field(build, &field, NULL, out_error);
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
				sqlparser_fill_relation_view_for_handle(build->handle, node->range_var, &relation_view);
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
				if (cte != NULL && cte->ctequery != NULL &&
				    cte->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT &&
				    sqlparser_graph_ensure_cte_block(
					    build,
					    cte,
					    &relation_source_block,
					    out_error) != 0) {
					return -1;
				}
				if (cte != NULL && cte->ctequery != NULL &&
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
			{
				size_t relation_begin;
				size_t relation_end;
				size_t using_index;

				relation_begin = sqlparser_graph_local_relation_count(build);
				if (sqlparser_graph_build_from_item(build, block_index, node->join_expr->larg, out_error) != 0 ||
				    sqlparser_graph_build_from_item(build, block_index, node->join_expr->rarg, out_error) != 0) {
					return -1;
				}
				relation_end = sqlparser_graph_local_relation_count(build);
				for (using_index = 0U; using_index < node->join_expr->n_using_clause; using_index++) {
					if (sqlparser_graph_add_using_field(
						    build,
						    block_index,
						    node->join_expr->using_clause[using_index],
						    relation_begin,
						    relation_end,
						    out_error) != 0) {
						return -1;
					}
				}
				return sqlparser_graph_walk_predicate_expr(
					build,
					block_index,
					SQLPARSER_CLAUSE_KIND_ON,
					node->join_expr->quals,
					out_error);
			}
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
		sqlparser_graph_register_cte_block(build, stmt, block_index);
		if (out_block_index != NULL) {
			*out_block_index = block_index;
		}
		if (sqlparser_graph_push_scope(build, block_index, stmt) != 0) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "query graph nesting is too deep");
			return -1;
		}
		memset(&set_item, 0, sizeof(set_item));
		set_item.kind = sqlparser_graph_set_kind_from_select(stmt);
		set_item.result_block_index = block_index;
		if (stmt->larg != NULL &&
		    sqlparser_graph_build_select(build, stmt->larg, SQLPARSER_GRAPH_BLOCK_SELECT, &left_block, out_error) != 0) {
			goto set_fail;
		}
		if (stmt->rarg != NULL &&
		    sqlparser_graph_build_select(build, stmt->rarg, SQLPARSER_GRAPH_BLOCK_SELECT, &right_block, out_error) != 0) {
			goto set_fail;
		}
		if (stmt->larg != NULL &&
		    sqlparser_graph_span_append_index(build, &set_item.branch_blocks, left_block, out_error) != 0) {
			goto set_fail;
		}
		if (stmt->rarg != NULL &&
		    sqlparser_graph_span_append_index(build, &set_item.branch_blocks, right_block, out_error) != 0) {
			goto set_fail;
		}
		if (sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_ORDER_BY, stmt->sort_clause, stmt->n_sort_clause, out_error) != 0 ||
		    sqlparser_graph_ensure_select_ctes(build, stmt, out_error) != 0 ||
		    sqlparser_graph_add_set(build, &set_item, NULL, out_error) != 0) {
			goto set_fail;
		}
		sqlparser_graph_pop_scope(build);
		return 0;

	set_fail:
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_add_block(build, kind, &block_index, out_error) != 0) {
		return -1;
	}
	sqlparser_graph_register_cte_block(build, stmt, block_index);
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
	index = sqlparser_graph_find_cached_select_target_list_index(build, stmt);
	if (index != (size_t)-1) {
		target_list_index = index;
	}
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
	if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, out_error) != 0 ||
	    sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_GROUP_BY, stmt->group_clause, stmt->n_group_clause, out_error) != 0 ||
	    sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_HAVING, stmt->having_clause, out_error) != 0 ||
	    sqlparser_graph_walk_node_array(build, block_index, SQLPARSER_CLAUSE_KIND_ORDER_BY, stmt->sort_clause, stmt->n_sort_clause, out_error) != 0) {
		sqlparser_graph_pop_scope(build);
		return -1;
	}
	if (sqlparser_graph_ensure_select_ctes(build, stmt, out_error) != 0) {
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
	build->statement->predicate_count = build->cache->predicate_count - build->statement->predicate_offset;
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
		memset(&block->predicates, 0, sizeof(block->predicates));
		for (index = 0U; index < build->statement->predicate_count; index++) {
			sqlparser_graph_predicate_t *predicate;

			predicate = &build->cache->predicates[build->statement->predicate_offset + index];
			if (predicate != NULL &&
			    predicate->block_index == block_index &&
			    sqlparser_graph_span_append_index(build, &block->predicates, index, out_error) != 0) {
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
		NULL,
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

static int sqlparser_graph_resolve_source_target_from_field(
	sqlparser_graph_build_t *build,
	size_t field_index,
	size_t *out_source_target_index)
{
	sqlparser_graph_field_t *field;
	sqlparser_graph_relation_t *relation;
	size_t index;
	size_t match_index;
	size_t match_count;

	if (out_source_target_index != NULL) {
		*out_source_target_index = 0U;
	}
	field = sqlparser_graph_field_by_local(build, field_index);
	if (build == NULL || field == NULL || !field->has_relation || field->column_name == NULL) {
		return 0;
	}
	relation = sqlparser_graph_relation_by_local(build, field->relation_index);
	if (relation == NULL || !relation->has_source_block) {
		return 0;
	}
	match_index = 0U;
	match_count = 0U;
	for (index = 0U; index < sqlparser_graph_local_target_count(build); index++) {
		sqlparser_graph_target_t *target;

		target = sqlparser_graph_target_by_local(build, index);
		if (target == NULL ||
		    target->block_index != relation->source_block_index ||
		    target->output_name == NULL ||
		    !sqlparser_text_equal_ci(target->output_name, field->column_name)) {
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

static int sqlparser_graph_fill_dml_source_field(
	sqlparser_graph_build_t *build,
	size_t block_index,
	sqlparser_clause_kind_t clause,
	PgQuery__Node *value_node,
	sqlparser_graph_value_kind_t *kind,
	size_t *source_field_index,
	int *has_source_field,
	size_t *source_target_index,
	int *has_source_target,
	sqlparser_error_t *out_error)
{
	size_t field_index;

	if (source_field_index != NULL) {
		*source_field_index = 0U;
	}
	if (has_source_field != NULL) {
		*has_source_field = 0;
	}
	if (source_target_index != NULL) {
		*source_target_index = 0U;
	}
	if (has_source_target != NULL) {
		*has_source_target = 0;
	}
	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_COLUMN_REF ||
	    value_node->column_ref == NULL ||
	    sqlparser_graph_column_ref_is_pseudo(value_node->column_ref)) {
		return 0;
	}
	field_index = 0U;
	if (sqlparser_graph_add_column_ref_field(
		    build,
		    block_index,
		    clause,
		    value_node->column_ref,
		    0U,
		    0,
		    &field_index,
		    out_error) != 0) {
		return -1;
	}
	if (kind != NULL) {
		*kind = SQLPARSER_GRAPH_VALUE_FIELD;
	}
	if (source_field_index != NULL) {
		*source_field_index = field_index;
	}
	if (has_source_field != NULL) {
		*has_source_field = 1;
	}
	if (source_target_index != NULL &&
	    sqlparser_graph_resolve_source_target_from_field(build, field_index, source_target_index)) {
		if (has_source_target != NULL) {
			*has_source_target = 1;
		}
	}
	return 1;
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
	sqlparser_fill_relation_view_for_handle(build->handle, range_var, &relation_view);
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
	    sqlparser_graph_fill_dml_source_field(
		    build,
		    0U,
		    SQLPARSER_CLAUSE_KIND_UNKNOWN,
		    value_node,
		    &cell.kind,
		    &cell.source_field_index,
		    &cell.has_source_field,
		    &cell.source_target_index,
		    &cell.has_source_target,
		    out_error) < 0 ||
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

static const char *sqlparser_graph_res_target_assignment_column(const PgQuery__ResTarget *target)
{
	const char *name;

	if (target == NULL) {
		return NULL;
	}
	if (target->n_indirection > 0U && target->indirection != NULL &&
	    sqlparser_node_string_value(target->indirection[target->n_indirection - 1U], &name) &&
	    name != NULL && name[0] != '\0') {
		return name;
	}
	return target->name;
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
	field.column_name = sqlparser_graph_res_target_assignment_column(node->res_target);
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
	    sqlparser_graph_fill_dml_source_field(
		    build,
		    block_index,
		    SQLPARSER_CLAUSE_KIND_SET_LIST,
		    node->res_target->val,
		    &assignment.value_kind,
		    &assignment.source_field_index,
		    &assignment.has_source_field,
		    &assignment.source_target_index,
		    &assignment.has_source_target,
		    out_error) < 0 ||
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

static int sqlparser_graph_add_multi_insert_relation(
	sqlparser_graph_build_t *build,
	const sqlparser_dialect_multi_insert_relation_t *source,
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
	relation.link_name = NULL;
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

static int sqlparser_graph_add_multi_insert_dml_column(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t ordinal,
	const sqlparser_dialect_multi_insert_column_t *source,
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

static int sqlparser_graph_multi_insert_cell_resolve_source_target(
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

static int sqlparser_graph_add_multi_insert_dml_cell(
	sqlparser_graph_build_t *build,
	size_t dml_index,
	size_t branch_ordinal,
	size_t column_ordinal,
	size_t source_block_index,
	int has_source_block,
	const sqlparser_dialect_multi_insert_value_t *source,
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
		           sqlparser_graph_multi_insert_cell_resolve_source_target(
			           build,
			           source_block_index,
			           node->column_ref,
			           &cell.source_target_index)) {
			sqlparser_graph_target_t *target;

			cell.kind = SQLPARSER_GRAPH_VALUE_FIELD;
			cell.has_source_target = 1;
			target = sqlparser_graph_target_by_local(build, cell.source_target_index);
			if (target != NULL && target->has_field) {
				cell.source_field_index = target->field_index;
				cell.has_source_field = 1;
			}
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

static int sqlparser_graph_build_multi_insert_dml(
	sqlparser_graph_build_t *build,
	PgQuery__InsertStmt *stmt,
	const sqlparser_dialect_multi_insert_t *multi,
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
	dml.insert_mode = multi->mode == SQLPARSER_DIALECT_MULTI_INSERT_FIRST ?
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
		const sqlparser_dialect_multi_insert_branch_t *source_branch;
		sqlparser_graph_dml_branch_t branch;
		size_t local_branch_index;

		source_branch = &multi->branches[branch_index];
		memset(&branch, 0, sizeof(branch));
		branch.dml_index = dml_index;
		branch.ordinal = branch_index;
		if (source_branch->has_condition) {
			branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_WHEN;
		} else if (source_branch->is_else) {
			branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_ELSE;
		} else {
			branch.branch_kind = SQLPARSER_GRAPH_DML_BRANCH_UNCONDITIONAL;
		}
		if (sqlparser_graph_add_multi_insert_relation(
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
		const sqlparser_dialect_multi_insert_branch_t *source_branch;
		sqlparser_graph_dml_branch_t *branch_item;
		size_t index;

		source_branch = &multi->branches[branch_index];
		branch_item = &build->cache->dml_branches[build->statement->dml_branch_offset + local_branch_indices[branch_index]];
		for (index = 0U; index < source_branch->column_count; index++) {
			if (sqlparser_graph_add_multi_insert_dml_column(
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
			if (sqlparser_graph_add_multi_insert_dml_cell(
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
	if (build->handle != NULL &&
	    build->handle->dialect_ops != NULL &&
	    build->handle->dialect_ops->insert_mode != NULL) {
		sqlparser_graph_dml_t *dml_item;

		dml_item = &build->cache->dml[build->statement->dml_offset + dml_index];
		dml_item->insert_mode = build->handle->dialect_ops->insert_mode(
			build->handle->dialect_state,
			build->statement_index,
			dml_item->insert_mode);
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
	if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, out_error) != 0) {
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
	if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, stmt->where_clause, out_error) != 0) {
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
	    sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_ON, stmt->join_condition, out_error) != 0) {
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
		if (sqlparser_graph_walk_predicate_expr(build, block_index, SQLPARSER_CLAUSE_KIND_WHERE, when_clause->condition, out_error) != 0) {
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
			    sqlparser_dialect_state_has_multi_insert(build->handle->dialect, build->handle->dialect_state)) {
				return sqlparser_graph_build_multi_insert_dml(
					build,
					statement->insert_stmt,
					sqlparser_dialect_state_multi_insert(build->handle->dialect, build->handle->dialect_state),
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
		build.statement_node = statement_node;
		build.statement->block_offset = cache->block_count;
		build.statement->relation_offset = cache->relation_count;
		build.statement->target_offset = cache->target_count;
		build.statement->field_offset = cache->field_count;
		build.statement->value_offset = cache->value_count;
		build.statement->set_offset = cache->set_count;
		build.statement->predicate_offset = cache->predicate_count;
		build.statement->dml_offset = cache->dml_count;
		build.statement->dml_branch_offset = cache->dml_branch_count;
		build.statement->dml_column_offset = cache->dml_column_count;
		build.statement->dml_cell_offset = cache->dml_cell_count;
		build.statement->dml_assignment_offset = cache->dml_assignment_count;
		if (sqlparser_graph_build_statement(&build, statement_node, out_error) != 0 ||
		    sqlparser_graph_finalize_statement_spans(&build, out_error) != 0) {
			sqlparser_graph_build_clear(&build);
			sqlparser_query_graph_cache_release(cache);
			if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "failed to build query graph");
			}
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		sqlparser_graph_build_clear(&build);
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
	out_graph->predicate_count = statement->predicate_count;
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

sqlparser_status_t sqlparser_query_graph_predicate_at(
	const sqlparser_query_graph_view_t *graph,
	size_t predicate_index,
	sqlparser_graph_predicate_t *out_predicate,
	sqlparser_error_t *out_error)
{
	sqlparser_query_graph_cache_t *cache;
	sqlparser_statement_graph_t *statement;

	sqlparser_error_clear(out_error);
	if (out_predicate == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_predicate must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_predicate, 0, sizeof(*out_predicate));
	statement = sqlparser_query_graph_statement(graph, &cache);
	if (statement == NULL || predicate_index >= statement->predicate_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "predicate_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_predicate = cache->predicates[statement->predicate_offset + predicate_index];
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
	json_t *predicates;

	object = json_object();
	relations = sqlparser_graph_span_json(graph, block->relations, out_error);
	targets = sqlparser_graph_span_json(graph, block->targets, out_error);
	predicates = sqlparser_graph_span_json(graph, block->predicates, out_error);
	if (object == NULL || relations == NULL || targets == NULL || predicates == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_block_kind_name(block->kind))) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "relations", &relations) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "targets", &targets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "predicates", &predicates) != 0) {
		json_decref(object);
		json_decref(relations);
		json_decref(targets);
		json_decref(predicates);
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
	    sqlparser_json_set_optional_string(object, "link", relation->link_name) != 0 ||
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

static int sqlparser_graph_like_escape_literal_json(json_t *object, const sqlparser_literal_view_t *literal)
{
	if (object == NULL || literal == NULL) {
		return -1;
	}
	if (json_object_set_new(object, "literal_kind", json_string(sqlparser_literal_kind_name(literal->kind))) != 0) {
		return -1;
	}
	switch (literal->kind) {
		case SQLPARSER_LITERAL_KIND_STRING:
			return json_object_set_new(
				object,
				"literal_value",
				json_string(literal->string_value != NULL ? literal->string_value : ""));
		case SQLPARSER_LITERAL_KIND_INTEGER:
			return json_object_set_new(object, "integer_value", json_integer((json_int_t)literal->integer_value));
		case SQLPARSER_LITERAL_KIND_FLOAT:
			return sqlparser_json_set_optional_string(object, "literal_value", literal->float_value);
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			return json_object_set_new(object, "boolean_value", json_boolean(literal->boolean_value != 0));
		case SQLPARSER_LITERAL_KIND_NULL:
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			return 0;
	}
}

static json_t *sqlparser_graph_like_escape_json(const sqlparser_graph_like_escape_t *escape)
{
	json_t *object;

	if (escape == NULL || escape->kind == SQLPARSER_GRAPH_LIKE_ESCAPE_NONE) {
		return json_null();
	}
	object = json_object();
	if (object == NULL ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_like_escape_kind_name(escape->kind))) != 0) {
		json_decref(object);
		return NULL;
	}
	switch (escape->kind) {
		case SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL:
			if (sqlparser_graph_like_escape_literal_json(object, &escape->literal) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_GRAPH_LIKE_ESCAPE_BIND:
			if (sqlparser_json_set_optional_string(object, "bind_key", escape->has_bind ? escape->bind : NULL) != 0 ||
			    json_object_set_new(object, "bind_kind", json_integer(escape->bind_kind)) != 0 ||
			    sqlparser_json_set_optional_string(object, "bind_sql", escape->has_bind_sql ? escape->bind_sql : NULL) != 0 ||
			    sqlparser_json_set_optional_size(object, "bind_position", escape->has_bind_position, escape->bind_position) != 0) {
				json_decref(object);
				return NULL;
			}
			break;
		case SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION:
		case SQLPARSER_GRAPH_LIKE_ESCAPE_NONE:
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
	json_t *like_escape;
	const char *field_match_kind_name;
	const char *operator_kind_name;

	(void)graph;
	field_match_kind_name = value->has_field &&
			value->field_match_kind != SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN ?
		sqlparser_graph_field_match_kind_name(value->field_match_kind) :
		NULL;
	operator_kind_name = value->operator_name != NULL ?
		sqlparser_graph_operator_kind_name(value->operator_kind) :
		NULL;
	object = json_object();
	literal = sqlparser_graph_literal_json(&value->literal, value->kind);
	like_escape = sqlparser_graph_like_escape_json(&value->like_escape);
	if (object == NULL || literal == NULL || like_escape == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)value->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(value->clause))) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator", value->operator_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator_kind", operator_kind_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "field", value->has_field, value->field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_field", value->has_source_field, value->source_field_index) != 0 ||
	    sqlparser_json_set_optional_string(object, "field_match_kind", field_match_kind_name) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_value_kind_name(value->kind))) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_key", value->has_bind ? value->bind : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(value->bind_kind)) != 0 ||
	    sqlparser_json_set_optional_string(object, "bind_sql", value->has_bind_sql ? value->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_optional_size(object, "bind_position", value->has_bind_position, value->bind_position) != 0 ||
	    sqlparser_json_set_optional_selector(object, "selector", value->has_selector ? &value->selector : NULL, out_error) != 0) {
		json_decref(object);
		json_decref(literal);
		json_decref(like_escape);
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
	if (json_is_null(like_escape)) {
		json_decref(like_escape);
		like_escape = NULL;
	} else if (sqlparser_json_object_set_owned(object, "like_escape", &like_escape) != 0) {
		json_decref(object);
		json_decref(like_escape);
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
	    sqlparser_dialect_is_oracle_or_dameng_compatible(handle->dialect)) {
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

static json_t *sqlparser_graph_predicate_json(
	const sqlparser_query_graph_view_t *graph,
	const sqlparser_graph_predicate_t *predicate,
	sqlparser_error_t *out_error)
{
	json_t *object;
	json_t *children;
	const char *operator_kind_name;

	object = json_object();
	children = sqlparser_graph_span_json(graph, predicate->children, out_error);
	operator_kind_name = predicate->operator_name != NULL ?
		sqlparser_graph_operator_kind_name(predicate->operator_kind) :
		NULL;
	if (object == NULL || children == NULL ||
	    json_object_set_new(object, "block", json_integer((json_int_t)predicate->block_index)) != 0 ||
	    json_object_set_new(object, "clause", json_string(sqlparser_clause_kind_name(predicate->clause))) != 0 ||
	    json_object_set_new(object, "kind", json_string(sqlparser_graph_predicate_kind_name(predicate->kind))) != 0 ||
	    json_object_set_new(object, "bool_operator", json_string(sqlparser_graph_predicate_bool_name(predicate->bool_operator))) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator", predicate->operator_name) != 0 ||
	    sqlparser_json_set_optional_string(object, "operator_kind", operator_kind_name) != 0 ||
	    sqlparser_json_set_optional_size(object, "left_field", predicate->has_left_field, predicate->left_field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "right_field", predicate->has_right_field, predicate->right_field_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "value", predicate->has_value, predicate->value_index) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "children", &children) != 0) {
		json_decref(object);
		json_decref(children);
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
	    sqlparser_json_set_optional_size(object, "source_field", cell->has_source_field, cell->source_field_index) != 0 ||
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
	    sqlparser_json_set_optional_size(object, "source_target", assignment->has_source_target, assignment->source_target_index) != 0 ||
	    sqlparser_json_set_optional_size(object, "source_field", assignment->has_source_field, assignment->source_field_index) != 0 ||
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
	    json_object_set_new(object, "branch_kind", json_string(sqlparser_graph_dml_branch_kind_name(branch->branch_kind))) != 0 ||
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
	json_t *predicates;
	size_t index;

	object = json_object();
	blocks = json_array();
	relations = json_array();
	targets = json_array();
	fields = json_array();
	values = json_array();
	sets = json_array();
	predicates = json_array();
	if (object == NULL || blocks == NULL || relations == NULL || targets == NULL ||
	    fields == NULL || values == NULL || sets == NULL || predicates == NULL ||
	    sqlparser_json_set_optional_size(object, "root", graph->has_root_block, graph->root_block_index) != 0) {
		goto fail;
	}
	for (index = 0U; index < graph->block_count; index++) {
		sqlparser_graph_block_t block;
		json_t *entry;
		if (sqlparser_query_graph_block_at(graph, index, &block, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_block_json(graph, &block, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(blocks, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->relation_count; index++) {
		sqlparser_graph_relation_t relation;
		json_t *entry;
		if (sqlparser_query_graph_relation_at(graph, index, &relation, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_relation_json(&relation, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(relations, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->target_count; index++) {
		sqlparser_graph_target_t target;
		json_t *entry;
		if (sqlparser_query_graph_target_at(graph, index, &target, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_target_json(graph, &target, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(targets, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->field_count; index++) {
		sqlparser_graph_field_t field;
		json_t *entry;
		if (sqlparser_query_graph_field_at(graph, index, &field, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_field_json(graph, &field, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(fields, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->value_count; index++) {
		sqlparser_graph_value_t value;
		json_t *entry;
		if (sqlparser_query_graph_value_at(graph, index, &value, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_value_json(graph, &value, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(values, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->set_count; index++) {
		sqlparser_graph_set_t set_item;
		json_t *entry;
		if (sqlparser_query_graph_set_at(graph, index, &set_item, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_set_json(handle, graph, &set_item, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(sets, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	for (index = 0U; index < graph->predicate_count; index++) {
		sqlparser_graph_predicate_t predicate;
		json_t *entry;
		if (sqlparser_query_graph_predicate_at(graph, index, &predicate, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_predicate_json(graph, &predicate, out_error);
		if (entry == NULL || sqlparser_json_array_append_owned(predicates, &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	if (sqlparser_json_set_nonempty_array(object, "blocks", &blocks) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "relations", &relations) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "targets", &targets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "fields", &fields) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "values", &values) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "sets", &sets) != 0 ||
	    sqlparser_json_set_nonempty_array(object, "predicates", &predicates) != 0) {
		goto fail;
	}
	if (graph->has_dml) {
		sqlparser_graph_dml_t dml;
		json_t *entry;

		if (sqlparser_query_graph_dml(graph, &dml, out_error) != SQLPARSER_STATUS_OK) {
			goto fail;
		}
		entry = sqlparser_graph_dml_json(graph, &dml, out_error);
		if (entry == NULL || sqlparser_json_object_set_owned(object, "dml", &entry) != 0) {
			json_decref(entry);
			goto fail;
		}
	}
	return object;

fail:
	json_decref(object);
	json_decref(blocks);
	json_decref(relations);
	json_decref(targets);
	json_decref(fields);
	json_decref(values);
	json_decref(sets);
	json_decref(predicates);
	if (out_error != NULL && out_error->code == SQLPARSER_STATUS_OK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return NULL;
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
		keyword = sqlparser_statement_keyword_for_handle(handle, statement_index, statement_node);
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
