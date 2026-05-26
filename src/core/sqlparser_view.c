#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "../dialect/sqlparser_dialect_internal.h"
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
	sqlparser_handle_t *handle;
	size_t statement_index;
	size_t seen;
	size_t target_index;
	int want_target;
	sqlparser_clause_kind_t target_kind;
	PgQuery__Node *target_expr;
} sqlparser_view_readonly_clause_search_t;

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

static int sqlparser_json_set_size_or_null(json_t *object, const char *key, int has_value, size_t value)
{
	if (object == NULL || key == NULL) {
		return -1;
	}
	return has_value ?
		json_object_set_new(object, key, json_integer((json_int_t)value)) :
		json_object_set_new(object, key, json_null());
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

static int sqlparser_json_set_owned_string_or_null(json_t *object, const char *key, char *value)
{
	json_t *item;

	if (object == NULL || key == NULL) {
		free(value);
		return -1;
	}
	item = value != NULL ? json_string(value) : json_null();
	free(value);
	if (item == NULL) {
		return -1;
	}
	return json_object_set_new(object, key, item);
}

static int sqlparser_view_select_has_base_slot(const PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL || stmt->n_values_lists > 0U) {
		return 0;
	}
	if (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	    stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
	    stmt->n_target_list == 0U &&
	    stmt->n_from_clause == 0U &&
	    stmt->where_clause == NULL &&
	    stmt->n_sort_clause == 0U) {
		return stmt->larg != NULL || stmt->rarg != NULL;
	}
	return stmt->n_target_list > 0U ||
		stmt->n_from_clause > 0U ||
		stmt->where_clause != NULL ||
		stmt->having_clause != NULL ||
		stmt->with_clause != NULL ||
		stmt->n_group_clause > 0U ||
		stmt->n_sort_clause > 0U ||
		stmt->limit_offset != NULL ||
		stmt->limit_count != NULL;
}

static int sqlparser_view_select_allows_where_slot(const PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL || stmt->n_values_lists > 0U) {
		return 0;
	}
	if (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	    stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
	    stmt->n_target_list == 0U &&
	    stmt->n_from_clause == 0U &&
	    stmt->where_clause == NULL) {
		return 0;
	}
	return stmt->n_target_list > 0U ||
		stmt->n_from_clause > 0U ||
		stmt->where_clause != NULL ||
		stmt->having_clause != NULL ||
		stmt->with_clause != NULL ||
		stmt->n_group_clause > 0U ||
		stmt->n_sort_clause > 0U ||
		stmt->limit_offset != NULL ||
		stmt->limit_count != NULL;
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
				(build->handle->dialect == SQLPARSER_DIALECT_ORACLE ||
				 build->handle->dialect == SQLPARSER_DIALECT_DAMENG) ?
				"minus" :
				"except";
		default:
			return NULL;
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

static sqlparser_status_t sqlparser_view_full_clause_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_view_readonly_clause_search_t readonly;
	size_t base_count;
	sqlparser_status_t status;

	if (out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	base_count = 0U;
	status = sqlparser_statement_clause_count(handle, statement_index, &base_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_view_readonly_clause_search(
		(sqlparser_handle_t *)handle,
		statement_index,
		0,
		0U,
		&readonly,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*out_count = base_count + readonly.seen;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_view_full_clause_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_clause_view_t *out_clause,
	sqlparser_error_t *out_error)
{
	sqlparser_view_readonly_clause_search_t readonly;
	size_t base_count;
	sqlparser_status_t status;

	if (out_clause == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_clause must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_clause, 0, sizeof(*out_clause));
	base_count = 0U;
	status = sqlparser_statement_clause_count(handle, statement_index, &base_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause_index < base_count) {
		return sqlparser_statement_clause(handle, statement_index, clause_index, out_clause, out_error);
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
	if (readonly.target_expr == NULL || readonly.target_kind == SQLPARSER_CLAUSE_KIND_UNKNOWN) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	out_clause->handle = handle;
	out_clause->statement_index = statement_index;
	out_clause->clause_index = clause_index;
	out_clause->kind = readonly.target_kind;
	out_clause->selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	out_clause->has_selector = 0;
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
		"SQL view clause",
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

static int sqlparser_variable_set_is_session_context(const PgQuery__VariableSetStmt *stmt)
{
	return sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) ||
		sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX) ||
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_PREPARE) ||
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
		sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE) ||
		sqlparser_variable_set_name_is(stmt, "search_path");
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

static const char *sqlparser_variable_set_prepared_arg_name(
	const PgQuery__VariableSetStmt *stmt,
	size_t arg_index)
{
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE)) {
		return arg_index == 0U ? "sql" : "params";
	}
	if (arg_index == 0U) {
		if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL)) {
			return "sql";
		}
		if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE) ||
		    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE) ||
		    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC) ||
		    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE)) {
			return "handle";
		}
		return "name";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_MYSQL_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE)) {
		return arg_index == 1U ? "sql" : "params";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE) ||
	    sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC)) {
		return arg_index == 1U ? "params" : (arg_index == 2U ? "sql" : "params");
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL)) {
		return "params";
	}
	return "params";
}

static const char *sqlparser_variable_set_public_name(
	const sqlparser_handle_t *handle,
	const PgQuery__VariableSetStmt *stmt)
{
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_DATABASE)) {
		return handle != NULL && handle->dialect == SQLPARSER_DIALECT_ORACLE ? "CONTAINER" : "DATABASE";
	}
	if (sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA)) {
		return handle != NULL &&
			       (handle->dialect == SQLPARSER_DIALECT_ORACLE ||
			        handle->dialect == SQLPARSER_DIALECT_DAMENG) ?
			"CURRENT_SCHEMA" :
			"SCHEMA";
	}
	if (sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX)) {
		return stmt->name + strlen(SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX);
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
	if (sqlparser_variable_set_is_prepared_statement(stmt)) {
		return NULL;
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
	if (sqlparser_variable_set_is_prepared_statement(stmt)) {
		return sqlparser_variable_set_prepared_arg_name(stmt, arg_index);
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
	if (handle->dialect == SQLPARSER_DIALECT_ORACLE &&
	    sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX)) {
		return 1;
	}
	return (handle->dialect == SQLPARSER_DIALECT_ORACLE ||
	        handle->dialect == SQLPARSER_DIALECT_DAMENG) &&
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

static sqlparser_status_t sqlparser_decode_variable_set_internal_arg_sql(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *decoded;
	char quote;
	size_t len;
	size_t pos;
	size_t end;
	size_t out_len;

	if (core_sql == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"internal SET argument decoder requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	len = strlen(core_sql);
	pos = 0U;
	while (pos < len && isspace((unsigned char)core_sql[pos])) {
		pos++;
	}
	if (pos >= len) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal SET argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	if (core_sql[pos] != '\'' && core_sql[pos] != '"') {
		end = len;
		while (end > pos && isspace((unsigned char)core_sql[end - 1U])) {
			end--;
		}
		if (pos >= end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal SET argument");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		*out_sql = sqlparser_strndup(core_sql + pos, end - pos);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	quote = core_sql[pos];
	pos++;
	decoded = (char *)malloc(len + 1U);
	if (decoded == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	out_len = 0U;
	while (pos < len) {
		if (core_sql[pos] == quote) {
			if (pos + 1U < len && core_sql[pos + 1U] == quote) {
				decoded[out_len++] = quote;
				pos += 2U;
				continue;
			}
			pos++;
			while (pos < len && isspace((unsigned char)core_sql[pos])) {
				pos++;
			}
			if (pos != len) {
				free(decoded);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid internal SET argument");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			decoded[out_len] = '\0';
			*out_sql = decoded;
			return SQLPARSER_STATUS_OK;
		}
		decoded[out_len++] = core_sql[pos++];
	}

	free(decoded);
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal SET argument");
	return SQLPARSER_STATUS_PARSE_ERROR;
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
	const char *public_name;
	char *dynamic_prefix;
	char *statement_sql;
	char *public_statement_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	dynamic_prefix = NULL;
	if (sqlparser_variable_set_is_prepared_statement(stmt)) {
		return sqlparser_decode_variable_set_internal_arg_sql(core_sql, out_sql, out_error);
	}
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
		if (sqlparser_variable_set_name_has_prefix(stmt, SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX)) {
			size_t prefix_len;
			size_t name_len;
			static const char oracle_prefix[] = "ALTER SESSION SET ";
			static const char oracle_suffix[] = " = ";

			public_name = sqlparser_variable_set_public_name(handle, stmt);
			if (public_name == NULL) {
				free(public_statement_sql);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"Oracle session parameter name is missing");
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			name_len = strlen(public_name);
			prefix_len = (sizeof(oracle_prefix) - 1U) + name_len + (sizeof(oracle_suffix) - 1U);
			dynamic_prefix = (char *)malloc(prefix_len + 1U);
			if (dynamic_prefix == NULL) {
				free(public_statement_sql);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			memcpy(dynamic_prefix, oracle_prefix, sizeof(oracle_prefix) - 1U);
			memcpy(dynamic_prefix + sizeof(oracle_prefix) - 1U, public_name, name_len);
			memcpy(
				dynamic_prefix + sizeof(oracle_prefix) - 1U + name_len,
				oracle_suffix,
				sizeof(oracle_suffix));
			prefix = dynamic_prefix;
		} else {
			prefix = sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA) ?
				"ALTER SESSION SET CURRENT_SCHEMA = " :
				"ALTER SESSION SET CONTAINER = ";
		}
	} else if (handle->dialect == SQLPARSER_DIALECT_DAMENG &&
	           sqlparser_variable_set_name_is(stmt, SQLPARSER_INTERNAL_CURRENT_SCHEMA)) {
		prefix = "ALTER SESSION SET CURRENT_SCHEMA = ";
	} else {
		prefix = "USE ";
	}
	status = sqlparser_extract_after_prefix(public_statement_sql, prefix, out_sql, out_error);
	free(dynamic_prefix);
	free(public_statement_sql);
	return status;
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

static int sqlparser_view_func_call_name_is(const PgQuery__FuncCall *func_call, const char *expected_upper_name)
{
	char *actual;
	int matches;

	if (expected_upper_name == NULL) {
		return 0;
	}
	actual = sqlparser_view_func_call_name_dup(func_call);
	if (actual == NULL) {
		return 0;
	}
	matches = strcmp(actual, expected_upper_name) == 0;
	free(actual);
	return matches;
}

static PgQuery__Node *sqlparser_view_similar_pattern_value(PgQuery__Node *value_node)
{
	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_FUNC_CALL ||
	    value_node->func_call == NULL ||
	    value_node->func_call->n_args == 0U ||
	    value_node->func_call->args == NULL ||
	    !sqlparser_view_func_call_name_is(value_node->func_call, "SIMILAR_TO_ESCAPE")) {
		return value_node;
	}
	return value_node->func_call->args[0];
}

static PgQuery__Node *sqlparser_view_public_a_expr_value(
	const PgQuery__AExpr *a_expr,
	PgQuery__Node *value_node)
{
	if (a_expr == NULL) {
		return value_node;
	}
	if (a_expr->kind == PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR) {
		return sqlparser_view_similar_pattern_value(value_node);
	}
	return value_node;
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
			"SQL view value",
			out_public_sql,
			out_error);
	}
	free(core_sql);
	return status;
}

static int sqlparser_view_fill_column_bind_view(
	sqlparser_column_view_t *column,
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__Node *value_node,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	sqlparser_view_bind_info_t bind_info;
	sqlparser_status_t status;
	char *public_sql;
	int bind_status;

	if (column == NULL || value_node == NULL) {
		return 0;
	}
	public_sql = NULL;
	status = sqlparser_view_render_value_node_public_sql(
		handle,
		statement_index,
		value_node,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (status == SQLPARSER_STATUS_NO_MEMORY || status == SQLPARSER_STATUS_RESOURCE_LIMIT) {
			return -1;
		}
		sqlparser_error_clear(out_error);
		return 0;
	}

	bind_status = sqlparser_view_bind_info_from_value(
		handle,
		public_sql,
		value_node,
		&bind_info,
		out_error);
	if (bind_status <= 0) {
		free(public_sql);
		return bind_status;
	}

	sqlparser_view_copy_public_text(
		column->bind_key,
		sizeof(column->bind_key),
		bind_info.name,
		&column->bind_key_truncated);
	sqlparser_view_copy_public_text(
		column->bind_sql,
		sizeof(column->bind_sql),
		public_sql,
		&column->bind_sql_truncated);
	column->has_bind_key = column->bind_key[0] != '\0';
	column->bind_kind = bind_info.kind;
	column->bind_position = bind_info.position;
	column->has_bind_sql = column->bind_sql[0] != '\0';
	if (selector != NULL && selector->kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		column->bind_selector = *selector;
		column->has_bind_selector = 1;
	}

	sqlparser_view_bind_info_release(&bind_info);
	free(public_sql);
	return 1;
}

static int sqlparser_view_fill_cell_bind_view(
	sqlparser_cell_view_t *cell,
	sqlparser_handle_t *handle,
	PgQuery__Node *value_node,
	const char *public_sql,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	sqlparser_view_bind_info_t bind_info;
	int bind_status;

	if (cell == NULL || public_sql == NULL || value_node == NULL) {
		return 0;
	}
	bind_status = sqlparser_view_bind_info_from_value(
		handle,
		public_sql,
		value_node,
		&bind_info,
		out_error);
	if (bind_status <= 0) {
		return bind_status;
	}
	sqlparser_view_copy_public_text(
		cell->bind_key,
		sizeof(cell->bind_key),
		bind_info.name,
		&cell->bind_key_truncated);
	sqlparser_view_copy_public_text(
		cell->bind_sql,
		sizeof(cell->bind_sql),
		public_sql,
		&cell->bind_sql_truncated);
	cell->has_bind_key = cell->bind_key[0] != '\0';
	cell->bind_kind = bind_info.kind;
	cell->bind_position = bind_info.position;
	cell->has_bind_sql = cell->bind_sql[0] != '\0';
	if (selector != NULL && selector->kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		cell->bind_selector = *selector;
		cell->has_bind_selector = 1;
	}
	sqlparser_view_bind_info_release(&bind_info);
	return 1;
}

static int sqlparser_view_relation_from_qualified_name_node(
	PgQuery__Node *node,
	sqlparser_relation_view_t *out_relation)
{
	const char *parts[3] = {NULL, NULL, NULL};
	const char *text;
	size_t part_count;
	size_t index;

	if (out_relation == NULL) {
		return 0;
	}
	sqlparser_relation_view_clear(out_relation);
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
		out_relation->table_name = parts[0];
		return 1;
	}
	if (part_count == 2U) {
		out_relation->schema_name = parts[0];
		out_relation->table_name = parts[1];
		return 1;
	}
	out_relation->database_name = parts[0];
	out_relation->schema_name = parts[1];
	out_relation->table_name = parts[2];
	return 1;
}

static size_t sqlparser_view_extra_object_count_from_statement(PgQuery__Node *statement)
{
	size_t index;
	size_t count;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_DROP_STMT ||
	    statement->drop_stmt == NULL) {
		return 0U;
	}
	count = 0U;
	for (index = 0U; index < statement->drop_stmt->n_objects; index++) {
		sqlparser_relation_view_t relation;

		if (sqlparser_view_relation_from_qualified_name_node(statement->drop_stmt->objects[index], &relation)) {
			count++;
		}
	}
	return count;
}

static int sqlparser_view_extra_object_at_from_statement(
	PgQuery__Node *statement,
	size_t object_index,
	sqlparser_relation_view_t *out_relation)
{
	size_t index;
	size_t seen;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_DROP_STMT ||
	    statement->drop_stmt == NULL) {
		return 0;
	}
	seen = 0U;
	for (index = 0U; index < statement->drop_stmt->n_objects; index++) {
		sqlparser_relation_view_t relation;

		if (!sqlparser_view_relation_from_qualified_name_node(statement->drop_stmt->objects[index], &relation)) {
			continue;
		}
		if (seen == object_index) {
			if (out_relation != NULL) {
				*out_relation = relation;
			}
			return 1;
		}
		seen++;
	}
	return 0;
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
	const PgQuery__SelectStmt *stmt);
static void sqlparser_view_keyword_view_from_expr(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__Node *node);

static void sqlparser_view_keyword_view_from_expr_array(
	sqlparser_view_keyword_state_t *keywords,
	PgQuery__Node *const *items,
	size_t item_count)
{
	size_t index;

	for (index = 0U; index < item_count; index++) {
		sqlparser_view_keyword_view_from_expr(keywords, items[index]);
	}
}

static void sqlparser_view_keyword_view_from_bool_expr(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__BoolExpr *expr)
{
	if (expr == NULL) {
		return;
	}
	switch (expr->boolop) {
		case PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR:
			sqlparser_view_keyword_view_add(keywords, "and");
			break;
		case PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR:
			sqlparser_view_keyword_view_add(keywords, "or");
			break;
		case PG_QUERY__BOOL_EXPR_TYPE__NOT_EXPR:
			sqlparser_view_keyword_view_add(keywords, "not");
			break;
		default:
			break;
	}
	sqlparser_view_keyword_view_from_expr_array(keywords, expr->args, expr->n_args);
}

static void sqlparser_view_keyword_view_from_a_expr(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__AExpr *expr)
{
	if (expr == NULL) {
		return;
	}
	switch (expr->kind) {
		case PG_QUERY__A__EXPR__KIND__AEXPR_IN:
			if (sqlparser_a_expr_is_not_in(expr)) {
				sqlparser_view_keyword_view_add(keywords, "not");
			}
			sqlparser_view_keyword_view_add(keywords, "in");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_LIKE:
			if (sqlparser_a_expr_is_not_like(expr)) {
				sqlparser_view_keyword_view_add(keywords, "not");
			}
			sqlparser_view_keyword_view_add(keywords, "like");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_ILIKE:
			if (sqlparser_a_expr_is_not_ilike(expr)) {
				sqlparser_view_keyword_view_add(keywords, "not");
			}
			sqlparser_view_keyword_view_add(keywords, "ilike");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_SIMILAR:
			if (sqlparser_a_expr_is_not_similar(expr)) {
				sqlparser_view_keyword_view_add(keywords, "not");
			}
			sqlparser_view_keyword_view_add(keywords, "similar");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_BETWEEN_SYM:
			sqlparser_view_keyword_view_add(keywords, "between");
			break;
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN:
		case PG_QUERY__A__EXPR__KIND__AEXPR_NOT_BETWEEN_SYM:
			sqlparser_view_keyword_view_add(keywords, "not");
			sqlparser_view_keyword_view_add(keywords, "between");
			break;
		default:
			break;
	}
	sqlparser_view_keyword_view_from_expr(keywords, expr->lexpr);
	sqlparser_view_keyword_view_from_expr(keywords, expr->rexpr);
}

static void sqlparser_view_keyword_view_from_expr(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__Node *node)
{
	if (node == NULL) {
		return;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_RES_TARGET:
			if (node->res_target != NULL) {
				if (node->res_target->name != NULL && node->res_target->name[0] != '\0') {
					sqlparser_view_keyword_view_add(keywords, "as");
				}
				sqlparser_view_keyword_view_from_expr(keywords, node->res_target->val);
			}
			break;
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			sqlparser_view_keyword_view_from_bool_expr(keywords, node->bool_expr);
			break;
		case PG_QUERY__NODE__NODE_A_EXPR:
			sqlparser_view_keyword_view_from_a_expr(keywords, node->a_expr);
			break;
		case PG_QUERY__NODE__NODE_LIST:
			if (node->list != NULL) {
				sqlparser_view_keyword_view_from_expr_array(keywords, node->list->items, node->list->n_items);
			}
			break;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call != NULL) {
				if (node->func_call->agg_distinct) {
					sqlparser_view_keyword_view_add(keywords, "distinct");
				}
				sqlparser_view_keyword_view_from_expr_array(keywords, node->func_call->args, node->func_call->n_args);
				if (node->func_call->over != NULL) {
					sqlparser_view_keyword_view_add(keywords, "over");
					if (node->func_call->over->n_partition_clause > 0U) {
						sqlparser_view_keyword_view_add(keywords, "partition");
						sqlparser_view_keyword_view_add(keywords, "by");
					}
					if (node->func_call->over->n_order_clause > 0U) {
						sqlparser_view_keyword_view_add(keywords, "order");
						sqlparser_view_keyword_view_add(keywords, "by");
					}
					sqlparser_view_keyword_view_from_expr_array(
						keywords,
						node->func_call->over->partition_clause,
						node->func_call->over->n_partition_clause);
					sqlparser_view_keyword_view_from_expr_array(
						keywords,
						node->func_call->over->order_clause,
						node->func_call->over->n_order_clause);
					sqlparser_view_keyword_view_from_expr(keywords, node->func_call->over->start_offset);
					sqlparser_view_keyword_view_from_expr(keywords, node->func_call->over->end_offset);
				}
			}
			break;
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr != NULL) {
				sqlparser_view_keyword_view_add(keywords, "case");
				sqlparser_view_keyword_view_from_expr(keywords, node->case_expr->arg);
				sqlparser_view_keyword_view_from_expr_array(keywords, node->case_expr->args, node->case_expr->n_args);
				if (node->case_expr->defresult != NULL) {
					sqlparser_view_keyword_view_add(keywords, "else");
					sqlparser_view_keyword_view_from_expr(keywords, node->case_expr->defresult);
				}
				sqlparser_view_keyword_view_add(keywords, "end");
			}
			break;
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when != NULL) {
				sqlparser_view_keyword_view_add(keywords, "when");
				sqlparser_view_keyword_view_from_expr(keywords, node->case_when->expr);
				sqlparser_view_keyword_view_add(keywords, "then");
				sqlparser_view_keyword_view_from_expr(keywords, node->case_when->result);
			}
			break;
		case PG_QUERY__NODE__NODE_NULL_TEST:
			sqlparser_view_keyword_view_add(keywords, "is");
			sqlparser_view_keyword_view_add(keywords, "null");
			if (node->null_test != NULL) {
				sqlparser_view_keyword_view_from_expr(keywords, node->null_test->arg);
			}
			break;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			sqlparser_view_keyword_view_add(keywords, "is");
			if (node->boolean_test != NULL) {
				sqlparser_view_keyword_view_from_expr(keywords, node->boolean_test->arg);
			}
			break;
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link != NULL) {
				if (node->sub_link->sub_link_type == PG_QUERY__SUB_LINK_TYPE__EXISTS_SUBLINK) {
					sqlparser_view_keyword_view_add(keywords, "exists");
				}
				sqlparser_view_keyword_view_from_expr(keywords, node->sub_link->testexpr);
				if (node->sub_link->subselect != NULL &&
				    node->sub_link->subselect->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
					sqlparser_view_keyword_view_from_select(keywords, node->sub_link->subselect->select_stmt);
				}
			}
			break;
		default:
			break;
	}
}

static void sqlparser_view_keyword_view_from_from_item(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__Node *node)
{
	if (node == NULL ||
	    node->node_case != PG_QUERY__NODE__NODE_JOIN_EXPR ||
	    node->join_expr == NULL) {
		return;
	}
	sqlparser_view_keyword_view_add(keywords, "join");
	if (node->join_expr->quals != NULL) {
		sqlparser_view_keyword_view_add(keywords, "on");
		sqlparser_view_keyword_view_from_expr(keywords, node->join_expr->quals);
	}
	if (node->join_expr->n_using_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "using");
	}
	sqlparser_view_keyword_view_from_from_item(keywords, node->join_expr->larg);
	sqlparser_view_keyword_view_from_from_item(keywords, node->join_expr->rarg);
}

static void sqlparser_view_keyword_view_from_from_clause(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__SelectStmt *stmt)
{
	size_t index;

	if (stmt == NULL) {
		return;
	}
	for (index = 0U; index < stmt->n_from_clause; index++) {
		sqlparser_view_keyword_view_from_from_item(keywords, stmt->from_clause[index]);
	}
}

static void sqlparser_view_keyword_view_from_on_conflict(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__OnConflictClause *clause)
{
	if (clause == NULL) {
		return;
	}
	sqlparser_view_keyword_view_add(keywords, "on");
	sqlparser_view_keyword_view_add(keywords, "conflict");
	sqlparser_view_keyword_view_add(keywords, "do");
	switch (clause->action) {
		case PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE:
			sqlparser_view_keyword_view_add(keywords, "update");
			sqlparser_view_keyword_view_add(keywords, "set");
			break;
		case PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_NOTHING:
			sqlparser_view_keyword_view_add(keywords, "nothing");
			break;
		default:
			break;
	}
}

static void sqlparser_view_keyword_view_from_merge_when_clause(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__MergeWhenClause *clause)
{
	if (clause == NULL) {
		return;
	}
	sqlparser_view_keyword_view_add(keywords, "when");
	if (clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE ||
	    clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_TARGET) {
		sqlparser_view_keyword_view_add(keywords, "not");
	}
	sqlparser_view_keyword_view_add(keywords, "matched");
	if (clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE ||
	    clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_TARGET) {
		sqlparser_view_keyword_view_add(keywords, "by");
		sqlparser_view_keyword_view_add(
			keywords,
			clause->match_kind == PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_SOURCE ?
				"source" :
				"target");
	}
	sqlparser_view_keyword_view_add(keywords, "then");
	switch (clause->command_type) {
		case PG_QUERY__CMD_TYPE__CMD_UPDATE:
			sqlparser_view_keyword_view_add(keywords, "update");
			sqlparser_view_keyword_view_add(keywords, "set");
			break;
		case PG_QUERY__CMD_TYPE__CMD_INSERT:
			sqlparser_view_keyword_view_add(keywords, "insert");
			sqlparser_view_keyword_view_add(keywords, "values");
			break;
		case PG_QUERY__CMD_TYPE__CMD_DELETE:
			sqlparser_view_keyword_view_add(keywords, "delete");
			break;
		case PG_QUERY__CMD_TYPE__CMD_NOTHING:
			sqlparser_view_keyword_view_add(keywords, "do");
			sqlparser_view_keyword_view_add(keywords, "nothing");
			break;
		default:
			break;
	}
	sqlparser_view_keyword_view_from_expr(keywords, clause->condition);
}

static void sqlparser_view_keyword_view_from_merge(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__MergeStmt *stmt)
{
	size_t index;

	if (stmt == NULL) {
		return;
	}
	sqlparser_view_keyword_view_add(keywords, "into");
	if (stmt->source_relation != NULL) {
		sqlparser_view_keyword_view_add(keywords, "using");
	}
	if (stmt->join_condition != NULL) {
		sqlparser_view_keyword_view_add(keywords, "on");
		sqlparser_view_keyword_view_from_expr(keywords, stmt->join_condition);
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *node;

		node = stmt->merge_when_clauses[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE) {
			sqlparser_view_keyword_view_from_merge_when_clause(keywords, node->merge_when_clause);
		}
	}
	if (stmt->n_returning_list > 0U) {
		sqlparser_view_keyword_view_add(keywords, "returning");
	}
}

static void sqlparser_view_keyword_view_from_grant(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__GrantStmt *stmt)
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
			sqlparser_view_keyword_view_add(keywords, node->access_priv->priv_name);
		}
	}
	sqlparser_view_keyword_view_add(keywords, "on");
	object_keyword = sqlparser_grant_object_keyword(stmt->objtype);
	if (object_keyword != NULL) {
		sqlparser_view_keyword_view_add(keywords, object_keyword);
	}
	sqlparser_view_keyword_view_add(keywords, stmt->is_grant ? "to" : "from");
}

static void sqlparser_view_keyword_view_from_alter_table_cmd(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__AlterTableCmd *cmd)
{
	if (cmd == NULL) {
		return;
	}
	switch (cmd->subtype) {
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AddColumn:
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AddColumnToView:
			sqlparser_view_keyword_view_add(keywords, "add");
			sqlparser_view_keyword_view_add(keywords, "column");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_DropColumn:
			sqlparser_view_keyword_view_add(keywords, "drop");
			sqlparser_view_keyword_view_add(keywords, "column");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AddConstraint:
		case PG_QUERY__ALTER_TABLE_TYPE__AT_ReAddConstraint:
			sqlparser_view_keyword_view_add(keywords, "add");
			sqlparser_view_keyword_view_add(keywords, "constraint");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_DropConstraint:
			sqlparser_view_keyword_view_add(keywords, "drop");
			sqlparser_view_keyword_view_add(keywords, "constraint");
			break;
		case PG_QUERY__ALTER_TABLE_TYPE__AT_AlterColumnType:
			sqlparser_view_keyword_view_add(keywords, "alter");
			sqlparser_view_keyword_view_add(keywords, "column");
			sqlparser_view_keyword_view_add(keywords, "type");
			break;
		default:
			break;
	}
}

static void sqlparser_view_keyword_view_from_alter_table(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__AlterTableStmt *stmt)
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
			sqlparser_view_keyword_view_from_alter_table_cmd(keywords, node->alter_table_cmd);
		}
	}
}

static void sqlparser_view_keyword_view_from_vacuum(
	sqlparser_view_keyword_state_t *keywords,
	const PgQuery__VacuumStmt *stmt)
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
			sqlparser_view_keyword_view_add(keywords, node->def_elem->defname);
		}
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
	if (stmt->with_clause != NULL) {
		size_t index;

		sqlparser_view_keyword_view_add(keywords, "with");
		for (index = 0U; index < stmt->with_clause->n_ctes; index++) {
			PgQuery__Node *cte_node;

			cte_node = stmt->with_clause->ctes[index];
			if (cte_node != NULL &&
			    cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR &&
			    cte_node->common_table_expr != NULL &&
			    cte_node->common_table_expr->ctequery != NULL &&
			    cte_node->common_table_expr->ctequery->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
				sqlparser_view_keyword_view_from_select(keywords, cte_node->common_table_expr->ctequery->select_stmt);
			}
		}
	}
	sqlparser_view_keyword_view_add(keywords, "select");
	if (stmt->n_distinct_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "distinct");
	}
	sqlparser_view_keyword_view_from_expr_array(keywords, stmt->target_list, stmt->n_target_list);
	if (stmt->n_from_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "from");
		sqlparser_view_keyword_view_from_from_clause(keywords, stmt);
	}
	if (stmt->where_clause != NULL) {
		sqlparser_view_keyword_view_add(keywords, "where");
		sqlparser_view_keyword_view_from_expr(keywords, stmt->where_clause);
	}
	if (stmt->n_group_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "group");
		sqlparser_view_keyword_view_add(keywords, "by");
		sqlparser_view_keyword_view_from_expr_array(keywords, stmt->group_clause, stmt->n_group_clause);
	}
	if (stmt->having_clause != NULL) {
		sqlparser_view_keyword_view_add(keywords, "having");
		sqlparser_view_keyword_view_from_expr(keywords, stmt->having_clause);
	}
	if (stmt->n_sort_clause > 0U) {
		sqlparser_view_keyword_view_add(keywords, "order");
		sqlparser_view_keyword_view_add(keywords, "by");
		sqlparser_view_keyword_view_from_expr_array(keywords, stmt->sort_clause, stmt->n_sort_clause);
	}
	if (stmt->limit_offset != NULL || stmt->limit_count != NULL) {
		if (keywords->build != NULL &&
		    keywords->build->handle != NULL &&
		    keywords->build->handle->dialect == SQLPARSER_DIALECT_SQLSERVER) {
			if (stmt->limit_offset != NULL) {
				sqlparser_view_keyword_view_add(keywords, "offset");
			}
			if (stmt->limit_count != NULL) {
				sqlparser_view_keyword_view_add(keywords, stmt->limit_offset != NULL ? "fetch" : "top");
			}
		} else {
			sqlparser_view_keyword_view_add(keywords, "limit");
		}
	}
	set_keyword = sqlparser_select_set_operator_keyword(keywords->build, stmt);
	if (set_keyword != NULL) {
		sqlparser_view_keyword_view_add(keywords, set_keyword);
		if (stmt->all) {
			sqlparser_view_keyword_view_add(keywords, "all");
		}
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
		if (statement != NULL &&
		    statement->view_stmt != NULL &&
		    statement->view_stmt->query != NULL &&
		    statement->view_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
			sqlparser_view_keyword_view_add(keywords, "as");
			sqlparser_view_keyword_view_from_select(keywords, statement->view_stmt->query->select_stmt);
		}
		return;
	}
	if (strcmp(main_keyword, "create_table") == 0) {
		sqlparser_view_keyword_view_add(keywords, "create");
		sqlparser_view_keyword_view_add(keywords, "table");
		return;
	}
	if (strcmp(main_keyword, "create_table_as") == 0 ||
	    strcmp(main_keyword, "create_materialized_view") == 0) {
		sqlparser_view_keyword_view_add(keywords, "create");
		if (strcmp(main_keyword, "create_materialized_view") == 0) {
			sqlparser_view_keyword_view_add(keywords, "materialized");
			sqlparser_view_keyword_view_add(keywords, "view");
		} else {
			sqlparser_view_keyword_view_add(keywords, "table");
		}
		sqlparser_view_keyword_view_add(keywords, "as");
		if (statement != NULL &&
		    statement->create_table_as_stmt != NULL &&
		    statement->create_table_as_stmt->query != NULL &&
		    statement->create_table_as_stmt->query->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
			sqlparser_view_keyword_view_from_select(keywords, statement->create_table_as_stmt->query->select_stmt);
		}
		return;
	}
	if (strcmp(main_keyword, "create_schema") == 0) {
		sqlparser_view_keyword_view_add(keywords, "create");
		sqlparser_view_keyword_view_add(keywords, "schema");
		return;
	}
	if (strcmp(main_keyword, "create_index") == 0) {
		sqlparser_view_keyword_view_add(keywords, "create");
		sqlparser_view_keyword_view_add(keywords, "index");
		if (statement != NULL && statement->index_stmt != NULL && statement->index_stmt->relation != NULL) {
			sqlparser_view_keyword_view_add(keywords, "on");
		}
		return;
	}
	if (strcmp(main_keyword, "alter_table") == 0) {
		sqlparser_view_keyword_view_add(keywords, "alter");
		sqlparser_view_keyword_view_add(keywords, "table");
		if (statement != NULL) {
			sqlparser_view_keyword_view_from_alter_table(keywords, statement->alter_table_stmt);
		}
		return;
	}
	if (strncmp(main_keyword, "drop_", 5) == 0) {
		sqlparser_view_keyword_view_add(keywords, "drop");
		sqlparser_view_keyword_view_add(keywords, main_keyword + 5);
		return;
	}
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_RENAME_STMT) {
		sqlparser_view_keyword_view_add(keywords, "rename");
		if (keywords->build != NULL &&
		    keywords->build->handle != NULL &&
		    keywords->build->handle->dialect == SQLPARSER_DIALECT_SQLSERVER) {
			sqlparser_view_keyword_view_add(keywords, "object");
		}
		sqlparser_view_keyword_view_add(keywords, "to");
		return;
	}
	sqlparser_view_keyword_view_add(keywords, main_keyword);
	if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_INSERT_STMT) {
		if (statement->insert_stmt != NULL && statement->insert_stmt->with_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "with");
		}
		sqlparser_view_keyword_view_add(keywords, "into");
		if (statement->insert_stmt != NULL &&
		    sqlparser_insert_source_from_stmt(statement->insert_stmt) == SQLPARSER_INSERT_SOURCE_VALUES) {
			sqlparser_view_keyword_view_add(keywords, "values");
		} else if (statement->insert_stmt != NULL &&
		    statement->insert_stmt->select_stmt != NULL &&
		    statement->insert_stmt->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT) {
			sqlparser_view_keyword_view_from_select(keywords, statement->insert_stmt->select_stmt->select_stmt);
		} else {
			sqlparser_view_keyword_view_add(keywords, "select");
		}
		if (statement->insert_stmt != NULL) {
			sqlparser_view_keyword_view_from_on_conflict(keywords, statement->insert_stmt->on_conflict_clause);
			if (statement->insert_stmt->n_returning_list > 0U) {
				sqlparser_view_keyword_view_add(keywords, "returning");
			}
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT) {
		if (statement->update_stmt != NULL && statement->update_stmt->with_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "with");
		}
		sqlparser_view_keyword_view_add(keywords, "set");
		if (statement->update_stmt != NULL && statement->update_stmt->n_from_clause > 0U) {
			sqlparser_view_keyword_view_add(keywords, "from");
		}
		if (statement->update_stmt != NULL && statement->update_stmt->where_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "where");
			sqlparser_view_keyword_view_from_expr(keywords, statement->update_stmt->where_clause);
		}
		if (statement->update_stmt != NULL && statement->update_stmt->n_returning_list > 0U) {
			sqlparser_view_keyword_view_add(keywords, "returning");
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_DELETE_STMT) {
		if (statement->delete_stmt != NULL && statement->delete_stmt->with_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "with");
		}
		sqlparser_view_keyword_view_add(keywords, "from");
		if (statement->delete_stmt != NULL && statement->delete_stmt->n_using_clause > 0U) {
			sqlparser_view_keyword_view_add(keywords, "using");
		}
		if (statement->delete_stmt != NULL && statement->delete_stmt->where_clause != NULL) {
			sqlparser_view_keyword_view_add(keywords, "where");
			sqlparser_view_keyword_view_from_expr(keywords, statement->delete_stmt->where_clause);
		}
		if (statement->delete_stmt != NULL && statement->delete_stmt->n_returning_list > 0U) {
			sqlparser_view_keyword_view_add(keywords, "returning");
		}
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_MERGE_STMT) {
		sqlparser_view_keyword_view_from_merge(keywords, statement->merge_stmt);
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_GRANT_STMT) {
		sqlparser_view_keyword_view_from_grant(keywords, statement->grant_stmt);
	} else if (statement != NULL && statement->node_case == PG_QUERY__NODE__NODE_VACUUM_STMT) {
		sqlparser_view_keyword_view_from_vacuum(keywords, statement->vacuum_stmt);
	}
}

typedef struct {
	sqlparser_handle_t *handle;
	size_t statement_index;
	size_t object_index;
	sqlparser_relation_view_t relation;
	sqlparser_clause_kind_t *clause_kinds;
	size_t clause_count;
	size_t next_readonly_clause_id;
	size_t seen;
	size_t target_index;
	int want_target;
	int found;
	sqlparser_column_view_t *out_column;
	size_t select_list_seen;
	size_t where_seen;
	size_t order_by_seen;
	size_t set_list_seen;
	size_t current_clause_id;
	int has_current_clause_id;
	int has_select_target_context;
	sqlparser_selector_t select_target_list_selector;
	sqlparser_selector_t select_target_selector;
	size_t target_path_count;
	int target_path_truncated;
	sqlparser_target_path_entry_t target_path[SQLPARSER_TARGET_PATH_CAPACITY];
} sqlparser_view_column_search_t;

typedef struct {
	size_t current_clause_id;
	int has_current_clause_id;
} sqlparser_view_search_clause_context_t;

typedef struct {
	int has_select_target_context;
	sqlparser_selector_t select_target_list_selector;
	sqlparser_selector_t select_target_selector;
	size_t target_path_count;
	int target_path_truncated;
} sqlparser_view_search_target_context_t;

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

static void sqlparser_view_search_save_clause_context(
	const sqlparser_view_column_search_t *search,
	sqlparser_view_search_clause_context_t *saved)
{
	if (search == NULL || saved == NULL) {
		return;
	}
	saved->current_clause_id = search->current_clause_id;
	saved->has_current_clause_id = search->has_current_clause_id;
}

static void sqlparser_view_search_restore_clause_context(
	sqlparser_view_column_search_t *search,
	const sqlparser_view_search_clause_context_t *saved)
{
	if (search == NULL || saved == NULL) {
		return;
	}
	search->current_clause_id = saved->current_clause_id;
	search->has_current_clause_id = saved->has_current_clause_id;
}

static void sqlparser_view_search_enter_existing_clause(
	sqlparser_view_column_search_t *search,
	sqlparser_clause_kind_t kind,
	size_t *seen,
	sqlparser_view_search_clause_context_t *saved)
{
	size_t index;
	size_t occurrence;
	size_t target_occurrence;

	sqlparser_view_search_save_clause_context(search, saved);
	if (search == NULL || seen == NULL || kind == SQLPARSER_CLAUSE_KIND_UNKNOWN) {
		if (search != NULL) {
			search->current_clause_id = 0U;
			search->has_current_clause_id = 0;
		}
		return;
	}
	target_occurrence = *seen;
	(*seen)++;
	occurrence = 0U;
	for (index = 0U; index < search->clause_count; index++) {
		if (search->clause_kinds[index] != kind) {
			continue;
		}
		if (occurrence == target_occurrence) {
			search->current_clause_id = index + 1U;
			search->has_current_clause_id = 1;
			return;
		}
		occurrence++;
	}
	search->current_clause_id = 0U;
	search->has_current_clause_id = 0;
}

static void sqlparser_view_search_enter_readonly_clause(
	sqlparser_view_column_search_t *search,
	PgQuery__Node *expr,
	sqlparser_view_search_clause_context_t *saved)
{
	sqlparser_view_search_save_clause_context(search, saved);
	if (search == NULL || expr == NULL) {
		if (search != NULL) {
			search->current_clause_id = 0U;
			search->has_current_clause_id = 0;
		}
		return;
	}
	search->current_clause_id = search->next_readonly_clause_id;
	search->has_current_clause_id = 1;
	search->next_readonly_clause_id++;
}

static void sqlparser_view_search_save_target_context(
	const sqlparser_view_column_search_t *search,
	sqlparser_view_search_target_context_t *saved)
{
	if (search == NULL || saved == NULL) {
		return;
	}
	saved->has_select_target_context = search->has_select_target_context;
	saved->select_target_list_selector = search->select_target_list_selector;
	saved->select_target_selector = search->select_target_selector;
	saved->target_path_count = search->target_path_count;
	saved->target_path_truncated = search->target_path_truncated;
}

static void sqlparser_view_search_restore_target_context(
	sqlparser_view_column_search_t *search,
	const sqlparser_view_search_target_context_t *saved)
{
	if (search == NULL || saved == NULL) {
		return;
	}
	search->has_select_target_context = saved->has_select_target_context;
	search->select_target_list_selector = saved->select_target_list_selector;
	search->select_target_selector = saved->select_target_selector;
	search->target_path_count = saved->target_path_count;
	search->target_path_truncated = saved->target_path_truncated;
}

static void sqlparser_view_search_clear_target_shape(sqlparser_view_column_search_t *search)
{
	if (search == NULL) {
		return;
	}
	search->target_path_count = 0U;
	search->target_path_truncated = 0;
}

static int sqlparser_view_search_target_path_last_is(
	const sqlparser_view_column_search_t *search,
	const char *kind,
	const char *name)
{
	const sqlparser_target_path_entry_t *entry;

	if (search == NULL || search->target_path_count == 0U || kind == NULL || name == NULL) {
		return 0;
	}
	entry = &search->target_path[search->target_path_count - 1U];
	return entry->kind != NULL &&
		entry->has_name &&
		strcmp(entry->kind, kind) == 0 &&
		strcmp(entry->name, name) == 0;
}

static int sqlparser_view_search_push_target_path(
	sqlparser_view_column_search_t *search,
	const char *kind,
	const char *name,
	size_t arg_index)
{
	sqlparser_target_path_entry_t *entry;

	if (search == NULL || kind == NULL || kind[0] == '\0') {
		return 0;
	}
	if (name == NULL) {
		name = "";
	}
	if (strcmp(kind, "expression") == 0 &&
	    strcmp(name, "case_when") == 0 &&
	    sqlparser_view_search_target_path_last_is(search, kind, name)) {
		return 0;
	}
	if (search->target_path_count >= SQLPARSER_TARGET_PATH_CAPACITY) {
		search->target_path_truncated = 1;
		return 0;
	}
	entry = &search->target_path[search->target_path_count];
	memset(entry, 0, sizeof(*entry));
	entry->kind = kind;
	sqlparser_view_copy_public_text(
		entry->name,
		sizeof(entry->name),
		name,
		&entry->name_truncated);
	entry->has_name = entry->name[0] != '\0';
	entry->arg_index = arg_index;
	search->target_path_count++;
	return 0;
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
	int is_select_output;

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
	is_select_output = keyword != NULL &&
		strcmp(keyword, "select") == 0 &&
		search->has_select_target_context;
	column->operator_name = is_select_output ? NULL : operator_name;
	if (selector.kind != SQLPARSER_SELECTOR_KIND_UNKNOWN) {
		column->selector = selector;
		column->has_selector = 1;
	}
	if (search->has_select_target_context) {
		column->target_list_selector = search->select_target_list_selector;
		column->has_target_list_selector = 1;
		column->target_selector = search->select_target_selector;
		column->has_target_selector = 1;
	}
	if (search->has_current_clause_id) {
		column->clause_id = search->current_clause_id;
		column->has_clause_id = 1;
	}
	if (keyword != NULL &&
	    strcmp(keyword, "select") == 0 &&
	    search->has_select_target_context) {
		size_t path_index;

		column->target_path_count = search->target_path_count;
		if (column->target_path_count > SQLPARSER_TARGET_PATH_CAPACITY) {
			column->target_path_count = SQLPARSER_TARGET_PATH_CAPACITY;
			column->target_path_truncated = 1;
		}
		column->target_path_truncated =
			column->target_path_truncated || search->target_path_truncated;
		for (path_index = 0U; path_index < column->target_path_count; path_index++) {
			column->target_path[path_index] = search->target_path[path_index];
		}
	}
	if (value_node != NULL) {
		sqlparser_value_view_t value_view;
		int bind_status;

		memset(&value_view, 0, sizeof(value_view));
		sqlparser_view_fill_value_view(search, value_node, forced_value_selector, &value_view);
		bind_status = sqlparser_view_fill_column_bind_view(
			column,
			search->handle,
			search->statement_index,
			value_node,
			value_view.has_selector ? &value_view.selector : NULL,
			out_error);
			if (bind_status < 0) {
				return -1;
			}
			if (bind_status == 0 && !is_select_output) {
				column->value_count = 1U;
				column->value = value_view;
			}
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

static int sqlparser_view_search_append_column_ref_values(
	sqlparser_view_column_search_t *search,
	PgQuery__ColumnRef *column_ref,
	const char *keyword,
	const char *operator_name,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (value_node != NULL &&
	    value_node->node_case == PG_QUERY__NODE__NODE_LIST &&
	    value_node->list != NULL &&
	    value_node->list->n_items > 0U) {
		for (index = 0U; index < value_node->list->n_items; index++) {
			if (sqlparser_view_search_append_column_ref(
				    search,
				    column_ref,
				    keyword,
				    operator_name,
				    value_node->list->items[index],
				    out_error) != 0) {
				return -1;
			}
			if (search->want_target && search->found) {
				return 0;
			}
		}
		return 0;
	}

	return sqlparser_view_search_append_column_ref(
		search,
		column_ref,
		keyword,
		operator_name,
		value_node,
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

static int sqlparser_view_search_walk_node_array_with_target_path(
	sqlparser_view_column_search_t *search,
	PgQuery__Node **items,
	size_t count,
	const char *keyword,
	const char *path_kind,
	const char *path_name,
	int use_arg_index,
	sqlparser_error_t *out_error)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		sqlparser_view_search_target_context_t saved_target;

		sqlparser_view_search_save_target_context(search, &saved_target);
		(void)sqlparser_view_search_push_target_path(
			search,
			path_kind,
			path_name,
			use_arg_index ? index : 0U);
		if (sqlparser_view_search_walk_expr(search, items[index], keyword, out_error) != 0) {
			sqlparser_view_search_restore_target_context(search, &saved_target);
			return -1;
		}
		if (search->want_target && search->found) {
			sqlparser_view_search_restore_target_context(search, &saved_target);
			return 0;
		}
		sqlparser_view_search_restore_target_context(search, &saved_target);
	}
	return 0;
}

static int sqlparser_view_search_walk_select_targets(
	sqlparser_view_column_search_t *search,
	PgQuery__SelectStmt *stmt,
	sqlparser_error_t *out_error)
{
	sqlparser_view_search_clause_context_t saved_clause;
	sqlparser_view_search_target_context_t saved_target;
	size_t target_list_index;
	size_t index;
	sqlparser_status_t status;

	if (stmt == NULL || stmt->n_target_list == 0U || stmt->target_list == NULL) {
		return 0;
	}

	target_list_index = 0U;
	status = sqlparser_find_select_target_list_index_by_stmt(
		search->handle,
		search->statement_index,
		stmt,
		&target_list_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}

	sqlparser_view_search_save_target_context(search, &saved_target);
	sqlparser_view_search_enter_existing_clause(
		search,
		SQLPARSER_CLAUSE_KIND_SELECT_LIST,
		&search->select_list_seen,
		&saved_clause);

	memset(&search->select_target_list_selector, 0, sizeof(search->select_target_list_selector));
	search->select_target_list_selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGETS;
	search->select_target_list_selector.statement_index = search->statement_index;
	search->select_target_list_selector.item_index = target_list_index;

	for (index = 0U; index < stmt->n_target_list; index++) {
		memset(&search->select_target_selector, 0, sizeof(search->select_target_selector));
		search->select_target_selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
		search->select_target_selector.statement_index = search->statement_index;
		search->select_target_selector.item_index = target_list_index;
		search->select_target_selector.column_index = index;
		search->has_select_target_context = 1;
		sqlparser_view_search_clear_target_shape(search);
		if (sqlparser_view_search_walk_expr(search, stmt->target_list[index], "select", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			sqlparser_view_search_restore_target_context(search, &saved_target);
			return -1;
		}
		if (search->want_target && search->found) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			sqlparser_view_search_restore_target_context(search, &saved_target);
			return 0;
		}
	}

	sqlparser_view_search_restore_clause_context(search, &saved_clause);
	sqlparser_view_search_restore_target_context(search, &saved_target);
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
	sqlparser_view_search_target_context_t saved_target;
	sqlparser_view_search_target_context_t side_target;

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

	sqlparser_view_search_save_target_context(search, &saved_target);
	if (sqlparser_try_extract_column_ref(a_expr->lexpr, &left_table, &left_column) &&
	    left_column != NULL) {
		if (a_expr->lexpr != NULL && a_expr->lexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    a_expr->lexpr->column_ref != NULL) {
			sqlparser_view_search_save_target_context(search, &side_target);
			(void)sqlparser_view_search_push_target_path(search, "expression", operator_name, 0U);
			if (sqlparser_view_search_append_column_ref_values(
				    search,
				    a_expr->lexpr->column_ref,
				    keyword,
				    operator_name,
				    sqlparser_view_public_a_expr_value(a_expr, a_expr->rexpr),
				    out_error) != 0) {
				sqlparser_view_search_restore_target_context(search, &side_target);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return -1;
			}
			if (search->want_target && search->found) {
				sqlparser_view_search_restore_target_context(search, &side_target);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return 0;
			}
			sqlparser_view_search_restore_target_context(search, &side_target);
		}
		sqlparser_view_search_save_target_context(search, &side_target);
		(void)sqlparser_view_search_push_target_path(search, "expression", operator_name, 1U);
		if (sqlparser_view_search_walk_expr(search, a_expr->rexpr, keyword, out_error) != 0) {
			sqlparser_view_search_restore_target_context(search, &side_target);
			sqlparser_view_search_restore_target_context(search, &saved_target);
			return -1;
		}
		sqlparser_view_search_restore_target_context(search, &side_target);
		sqlparser_view_search_restore_target_context(search, &saved_target);
		return 0;
	}
	if (sqlparser_try_extract_column_ref(a_expr->rexpr, &right_table, &right_column) &&
	    right_column != NULL) {
		if (a_expr->rexpr != NULL && a_expr->rexpr->node_case == PG_QUERY__NODE__NODE_COLUMN_REF &&
		    a_expr->rexpr->column_ref != NULL) {
			sqlparser_view_search_save_target_context(search, &side_target);
			(void)sqlparser_view_search_push_target_path(search, "expression", operator_name, 1U);
			if (sqlparser_view_search_append_column_ref_values(
				    search,
				    a_expr->rexpr->column_ref,
				    keyword,
				    operator_name,
				    sqlparser_view_public_a_expr_value(a_expr, a_expr->lexpr),
				    out_error) != 0) {
				sqlparser_view_search_restore_target_context(search, &side_target);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return -1;
			}
			if (search->want_target && search->found) {
				sqlparser_view_search_restore_target_context(search, &side_target);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return 0;
			}
			sqlparser_view_search_restore_target_context(search, &side_target);
		}
		sqlparser_view_search_save_target_context(search, &side_target);
		(void)sqlparser_view_search_push_target_path(search, "expression", operator_name, 0U);
		if (sqlparser_view_search_walk_expr(search, a_expr->lexpr, keyword, out_error) != 0) {
			sqlparser_view_search_restore_target_context(search, &side_target);
			sqlparser_view_search_restore_target_context(search, &saved_target);
			return -1;
		}
		sqlparser_view_search_restore_target_context(search, &side_target);
		sqlparser_view_search_restore_target_context(search, &saved_target);
		return 0;
	}

	sqlparser_view_search_save_target_context(search, &side_target);
	(void)sqlparser_view_search_push_target_path(search, "expression", operator_name, 0U);
	if (sqlparser_view_search_walk_expr(search, a_expr->lexpr, keyword, out_error) != 0) {
		sqlparser_view_search_restore_target_context(search, &side_target);
		sqlparser_view_search_restore_target_context(search, &saved_target);
		return -1;
	}
	sqlparser_view_search_restore_target_context(search, &side_target);
	sqlparser_view_search_save_target_context(search, &side_target);
	(void)sqlparser_view_search_push_target_path(search, "expression", operator_name, 1U);
	if (sqlparser_view_search_walk_expr(search, a_expr->rexpr, keyword, out_error) != 0) {
		sqlparser_view_search_restore_target_context(search, &side_target);
		sqlparser_view_search_restore_target_context(search, &saved_target);
		return -1;
	}
	sqlparser_view_search_restore_target_context(search, &side_target);
	sqlparser_view_search_restore_target_context(search, &saved_target);
	return 0;
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
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->bool_expr->args,
					node->bool_expr->n_args,
					keyword,
					"expression",
					sqlparser_view_bool_expr_name(node->bool_expr),
					1,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call == NULL) {
				return 0;
			}
			{
				char *func_name;
				int rc;

				func_name = sqlparser_view_func_call_name_dup(node->func_call);
				rc = sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->func_call->args,
					node->func_call->n_args,
					keyword,
					"function",
					func_name,
					1,
					out_error);
				if (rc != 0) {
					free(func_name);
					return -1;
				}
				if (search->want_target && search->found) {
					free(func_name);
					return 0;
				}
				if (node->func_call->over != NULL &&
				    (sqlparser_view_search_walk_node_array_with_target_path(
					     search,
					     node->func_call->over->partition_clause,
					     node->func_call->over->n_partition_clause,
					     keyword,
					     "expression",
					     "window_partition",
					     1,
					     out_error) != 0 ||
				     sqlparser_view_search_walk_node_array_with_target_path(
					     search,
					     node->func_call->over->order_clause,
					     node->func_call->over->n_order_clause,
					     keyword,
					     "expression",
					     "window_order",
					     1,
					     out_error) != 0 ||
				     sqlparser_view_search_walk_node_array_with_target_path(
					     search,
					     &node->func_call->over->start_offset,
					     node->func_call->over->start_offset != NULL ? 1U : 0U,
					     keyword,
					     "expression",
					     "window_frame_start",
					     0,
					     out_error) != 0 ||
				     sqlparser_view_search_walk_node_array_with_target_path(
					     search,
					     &node->func_call->over->end_offset,
					     node->func_call->over->end_offset != NULL ? 1U : 0U,
					     keyword,
					     "expression",
					     "window_frame_end",
					     0,
					     out_error) != 0)) {
					free(func_name);
					return -1;
				}
				free(func_name);
				return 0;
			}
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			return node->type_cast != NULL ?
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					&node->type_cast->arg,
					node->type_cast->arg != NULL ? 1U : 0U,
					keyword,
					"function",
					"CAST",
					1,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			if (node->collate_clause != NULL) {
				sqlparser_view_search_target_context_t saved_target;
				int rc;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "collate", 0U);
				rc = sqlparser_view_search_walk_expr(search, node->collate_clause->arg, keyword, out_error);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return rc;
			}
			return 0;
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			if (node->a_indirection != NULL) {
				sqlparser_view_search_target_context_t saved_target;
				int rc;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "indirection", 0U);
				rc = sqlparser_view_search_walk_expr(search, node->a_indirection->arg, keyword, out_error);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return rc;
			}
			return 0;
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			return node->a_array_expr != NULL ?
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->a_array_expr->elements,
					node->a_array_expr->n_elements,
					keyword,
					"expression",
					"array",
					1,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
			return node->array_expr != NULL ?
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->array_expr->elements,
					node->array_expr->n_elements,
					keyword,
					"expression",
					"array",
					1,
					out_error) :
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
			if (node->null_test != NULL) {
				sqlparser_view_search_target_context_t saved_target;
				int rc;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "is_null", 0U);
				rc = sqlparser_view_search_walk_expr(search, node->null_test->arg, keyword, out_error);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return rc;
			}
			return 0;
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			if (node->boolean_test != NULL) {
				sqlparser_view_search_target_context_t saved_target;
				int rc;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "boolean_test", 0U);
				rc = sqlparser_view_search_walk_expr(search, node->boolean_test->arg, keyword, out_error);
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return rc;
			}
			return 0;
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link == NULL) {
				return 0;
			}
			{
				sqlparser_view_search_target_context_t saved_target;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "subquery", 0U);
				if (sqlparser_view_search_walk_expr(search, node->sub_link->testexpr, keyword, out_error) != 0 ||
				    sqlparser_view_search_walk_expr(search, node->sub_link->subselect, keyword, out_error) != 0) {
					sqlparser_view_search_restore_target_context(search, &saved_target);
					return -1;
				}
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return 0;
			}
		case PG_QUERY__NODE__NODE_CASE_EXPR:
			if (node->case_expr == NULL) {
				return 0;
			}
			{
				sqlparser_view_search_target_context_t saved_target;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "case_when", 0U);
				if (sqlparser_view_search_walk_expr(search, node->case_expr->arg, keyword, out_error) != 0 ||
				    sqlparser_view_search_walk_node_array(search, node->case_expr->args, node->case_expr->n_args, keyword, out_error) != 0 ||
				    sqlparser_view_search_walk_expr(search, node->case_expr->defresult, keyword, out_error) != 0) {
					sqlparser_view_search_restore_target_context(search, &saved_target);
					return -1;
				}
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return 0;
			}
		case PG_QUERY__NODE__NODE_CASE_WHEN:
			if (node->case_when == NULL) {
				return 0;
			}
			{
				sqlparser_view_search_target_context_t saved_target;

				sqlparser_view_search_save_target_context(search, &saved_target);
				(void)sqlparser_view_search_push_target_path(search, "expression", "case_when", 0U);
				if (sqlparser_view_search_walk_expr(search, node->case_when->expr, keyword, out_error) != 0 ||
				    sqlparser_view_search_walk_expr(search, node->case_when->result, keyword, out_error) != 0) {
					sqlparser_view_search_restore_target_context(search, &saved_target);
					return -1;
				}
				sqlparser_view_search_restore_target_context(search, &saved_target);
				return 0;
			}
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			return node->row_expr != NULL ?
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->row_expr->args,
					node->row_expr->n_args,
					keyword,
					"expression",
					"row",
					1,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_ROW_COMPARE_EXPR:
			if (node->row_compare_expr == NULL) {
				return 0;
			}
			return sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->row_compare_expr->largs,
					node->row_compare_expr->n_largs,
					keyword,
					"expression",
					"row_compare",
					1,
					out_error) != 0 ||
					sqlparser_view_search_walk_node_array_with_target_path(
						search,
						node->row_compare_expr->rargs,
						node->row_compare_expr->n_rargs,
						keyword,
						"expression",
						"row_compare",
						1,
						out_error) != 0 ?
				-1 :
				0;
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			return node->coalesce_expr != NULL ?
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->coalesce_expr->args,
					node->coalesce_expr->n_args,
					keyword,
					"function",
					"COALESCE",
					1,
					out_error) :
				0;
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			return node->min_max_expr != NULL ?
				sqlparser_view_search_walk_node_array_with_target_path(
					search,
					node->min_max_expr->args,
					node->min_max_expr->n_args,
					keyword,
					"function",
					sqlparser_view_min_max_name(node->min_max_expr),
					1,
					out_error) :
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
			if (sqlparser_view_search_process_from_item(search, node->join_expr->larg, out_error) != 0 ||
			    sqlparser_view_search_process_from_item(search, node->join_expr->rarg, out_error) != 0) {
				return -1;
			}
			if (node->join_expr->quals != NULL) {
				sqlparser_view_search_clause_context_t saved_clause;

				sqlparser_view_search_enter_readonly_clause(search, node->join_expr->quals, &saved_clause);
				if (sqlparser_view_search_walk_expr(search, node->join_expr->quals, "on", out_error) != 0) {
					sqlparser_view_search_restore_clause_context(search, &saved_clause);
					return -1;
				}
				sqlparser_view_search_restore_clause_context(search, &saved_clause);
			}
			return 0;
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
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	if (stmt->with_clause != NULL) {
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
		if (sqlparser_view_search_process_select_stmt(search, stmt->larg, out_error) != 0 ||
		    sqlparser_view_search_process_select_stmt(search, stmt->rarg, out_error) != 0) {
			return -1;
		}
		if (sqlparser_view_select_has_base_slot(stmt)) {
			sqlparser_view_search_clause_context_t saved_clause;

			sqlparser_view_search_enter_existing_clause(
				search,
				SQLPARSER_CLAUSE_KIND_ORDER_BY,
				&search->order_by_seen,
				&saved_clause);
			if (sqlparser_view_search_walk_node_array(search, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0) {
				sqlparser_view_search_restore_clause_context(search, &saved_clause);
				return -1;
			}
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
		} else if (sqlparser_view_search_walk_node_array(search, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0) {
			return -1;
		}
		return 0;
	}
	if (sqlparser_view_search_walk_select_targets(search, stmt, out_error) != 0 ||
	    sqlparser_view_search_process_from_clause(search, stmt->from_clause, stmt->n_from_clause, out_error) != 0) {
		return -1;
	}
	if (sqlparser_view_select_allows_where_slot(stmt)) {
		sqlparser_view_search_clause_context_t saved_clause;

		sqlparser_view_search_enter_existing_clause(
			search,
			SQLPARSER_CLAUSE_KIND_WHERE,
			&search->where_seen,
			&saved_clause);
		if (sqlparser_view_search_walk_expr(search, stmt->where_clause, "where", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			return -1;
		}
		sqlparser_view_search_restore_clause_context(search, &saved_clause);
	} else if (sqlparser_view_search_walk_expr(search, stmt->where_clause, "where", out_error) != 0) {
		return -1;
	}
	for (index = 0U; index < stmt->n_group_clause; index++) {
		sqlparser_view_search_clause_context_t saved_clause;

		sqlparser_view_search_enter_readonly_clause(search, stmt->group_clause[index], &saved_clause);
		if (sqlparser_view_search_walk_expr(search, stmt->group_clause[index], "group", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			return -1;
		}
		sqlparser_view_search_restore_clause_context(search, &saved_clause);
	}
	if (stmt->having_clause != NULL) {
		sqlparser_view_search_clause_context_t saved_clause;

		sqlparser_view_search_enter_readonly_clause(search, stmt->having_clause, &saved_clause);
		if (sqlparser_view_search_walk_expr(search, stmt->having_clause, "having", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			return -1;
		}
		sqlparser_view_search_restore_clause_context(search, &saved_clause);
	}
	if (sqlparser_view_select_has_base_slot(stmt)) {
		sqlparser_view_search_clause_context_t saved_clause;

		sqlparser_view_search_enter_existing_clause(
			search,
			SQLPARSER_CLAUSE_KIND_ORDER_BY,
			&search->order_by_seen,
			&saved_clause);
		if (sqlparser_view_search_walk_node_array(search, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			return -1;
		}
		sqlparser_view_search_restore_clause_context(search, &saved_clause);
	} else if (sqlparser_view_search_walk_node_array(search, stmt->sort_clause, stmt->n_sort_clause, "order", out_error) != 0) {
		return -1;
	}
	return 0;
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
		sqlparser_view_search_clause_context_t saved_set_clause;

		sqlparser_view_search_enter_existing_clause(
			search,
			SQLPARSER_CLAUSE_KIND_SET_LIST,
			&search->set_list_seen,
			&saved_set_clause);
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
				sqlparser_view_search_restore_clause_context(search, &saved_set_clause);
				return -1;
			}
			if (search->want_target && search->found) {
				sqlparser_view_search_restore_clause_context(search, &saved_set_clause);
				return 0;
			}
		}
		sqlparser_view_search_restore_clause_context(search, &saved_set_clause);
	}
	if (sqlparser_view_search_process_from_clause(search, stmt->from_clause, stmt->n_from_clause, out_error) != 0) {
		return -1;
	}
	{
		sqlparser_view_search_clause_context_t saved_clause;

		sqlparser_view_search_enter_existing_clause(
			search,
			SQLPARSER_CLAUSE_KIND_WHERE,
			&search->where_seen,
			&saved_clause);
		if (sqlparser_view_search_walk_expr(search, stmt->where_clause, "where", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			return -1;
		}
		sqlparser_view_search_restore_clause_context(search, &saved_clause);
	}
	return 0;
}

static int sqlparser_view_search_append_merge_target_column(
	sqlparser_view_column_search_t *search,
	PgQuery__ResTarget *target,
	const char *keyword,
	PgQuery__Node *value_node,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t name_selector;
	sqlparser_selector_t value_selector;
	sqlparser_view_build_t build;
	size_t name_index;

	if (search == NULL || search->object_index != 0U ||
	    target == NULL || target->name == NULL || target->name[0] == '\0') {
		return 0;
	}
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
	value_selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
	if (value_node != NULL && strcmp(keyword, "set") == 0) {
		size_t value_index;

		value_index = sqlparser_view_find_value_index(&build, value_node);
		if (value_index != (size_t)-1) {
			value_selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
			value_selector.statement_index = search->statement_index;
			value_selector.item_index = value_index;
		}
	}
	return sqlparser_view_emit_column_view(
		search,
		target->name,
		keyword,
		value_node != NULL ? "=" : NULL,
		NULL,
		value_node,
		&name_selector,
		value_selector.kind != SQLPARSER_SELECTOR_KIND_UNKNOWN ? &value_selector : NULL,
		out_error);
}

static int sqlparser_view_search_process_merge_when_clause(
	sqlparser_view_column_search_t *search,
	PgQuery__MergeWhenClause *clause,
	sqlparser_error_t *out_error)
{
	const char *keyword;
	size_t index;

	if (clause == NULL) {
		return 0;
	}
	if (sqlparser_view_search_walk_expr(search, clause->condition, "when", out_error) != 0) {
		return -1;
	}
	if (search->want_target && search->found) {
		return 0;
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
		if (sqlparser_view_search_append_merge_target_column(
			    search,
			    node->res_target,
			    keyword,
			    clause->command_type == PG_QUERY__CMD_TYPE__CMD_UPDATE ? node->res_target->val : NULL,
			    out_error) != 0 ||
		    sqlparser_view_search_walk_expr(search, node->res_target->val, keyword, out_error) != 0) {
			return -1;
		}
		if (search->want_target && search->found) {
			return 0;
		}
	}
	if (sqlparser_view_search_walk_node_array(search, clause->values, clause->n_values, "insert", out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_search_process_merge_stmt(
	sqlparser_view_column_search_t *search,
	PgQuery__MergeStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL) {
		return 0;
	}
	if (stmt->join_condition != NULL) {
		sqlparser_view_search_clause_context_t saved_clause;

		sqlparser_view_search_enter_readonly_clause(search, stmt->join_condition, &saved_clause);
		if (sqlparser_view_search_walk_expr(search, stmt->join_condition, "on", out_error) != 0) {
			sqlparser_view_search_restore_clause_context(search, &saved_clause);
			return -1;
		}
		sqlparser_view_search_restore_clause_context(search, &saved_clause);
		if (search->want_target && search->found) {
			return 0;
		}
	}
	if (sqlparser_view_search_process_from_item(search, stmt->source_relation, out_error) != 0) {
		return -1;
	}
	if (search->want_target && search->found) {
		return 0;
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *node;

		node = stmt->merge_when_clauses[index];
		if (node != NULL &&
		    node->node_case == PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE &&
		    sqlparser_view_search_process_merge_when_clause(search, node->merge_when_clause, out_error) != 0) {
			return -1;
		}
		if (search->want_target && search->found) {
			return 0;
		}
	}
	return 0;
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
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return sqlparser_view_search_process_merge_stmt(search, statement->merge_stmt, out_error);
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (statement->delete_stmt != NULL) {
				sqlparser_view_search_clause_context_t saved_clause;

				if (sqlparser_view_search_process_from_clause(
					    search,
					    statement->delete_stmt->using_clause,
					    statement->delete_stmt->n_using_clause,
					    out_error) != 0) {
					return -1;
				}
				sqlparser_view_search_enter_existing_clause(
					search,
					SQLPARSER_CLAUSE_KIND_WHERE,
					&search->where_seen,
					&saved_clause);
				if (sqlparser_view_search_walk_expr(search, statement->delete_stmt->where_clause, "where", out_error) != 0) {
					sqlparser_view_search_restore_clause_context(search, &saved_clause);
					return -1;
				}
				sqlparser_view_search_restore_clause_context(search, &saved_clause);
			}
			return 0;
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
	size_t clause_index;
	size_t clause_count;

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
	clause_count = 0U;
	status = sqlparser_statement_clause_count(
		object->handle,
		object->statement_index,
		&clause_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause_count > 0U) {
		search.clause_kinds = (sqlparser_clause_kind_t *)calloc(clause_count, sizeof(*search.clause_kinds));
		if (search.clause_kinds == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		search.clause_count = clause_count;
		for (clause_index = 0U; clause_index < clause_count; clause_index++) {
			sqlparser_clause_view_t clause;

			memset(&clause, 0, sizeof(clause));
			status = sqlparser_statement_clause(
				object->handle,
				object->statement_index,
				clause_index,
				&clause,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(search.clause_kinds);
				return status;
			}
			search.clause_kinds[clause_index] = clause.kind;
		}
	}
	search.next_readonly_clause_id = clause_count + 1U;
	if (sqlparser_view_search_process_statement(&search, statement, out_error) != 0) {
		status = out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_INTERNAL_ERROR;
		free(search.clause_kinds);
		return status;
	}
	if (!want_target) {
		if (out_count != NULL) {
			*out_count = search.seen;
		}
		free(search.clause_kinds);
		return SQLPARSER_STATUS_OK;
	}
	if (!search.found) {
		free(search.clause_kinds);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	free(search.clause_kinds);
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_view_json_set_bind_from_column(json_t *object, const sqlparser_column_view_t *column, sqlparser_error_t *out_error)
{
	if (object == NULL || column == NULL) {
		return -1;
	}
	if (sqlparser_json_set_string_or_null(object, "bind_key", column->has_bind_key ? column->bind_key : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(column->bind_kind)) != 0 ||
	    json_object_set_new(object, "bind_position", json_integer((json_int_t)column->bind_position)) != 0 ||
	    sqlparser_json_set_string_or_null(object, "bind_sql", column->has_bind_sql ? column->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_selector_or_null(
		    object,
		    "bind_selector",
		    column->has_bind_selector ? &column->bind_selector : NULL,
		    out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_json_set_bind_from_cell(json_t *object, const sqlparser_cell_view_t *cell, sqlparser_error_t *out_error)
{
	if (object == NULL || cell == NULL) {
		return -1;
	}
	if (sqlparser_json_set_string_or_null(object, "bind_key", cell->has_bind_key ? cell->bind_key : NULL) != 0 ||
	    json_object_set_new(object, "bind_kind", json_integer(cell->bind_kind)) != 0 ||
	    json_object_set_new(object, "bind_position", json_integer((json_int_t)cell->bind_position)) != 0 ||
	    sqlparser_json_set_string_or_null(object, "bind_sql", cell->has_bind_sql ? cell->bind_sql : NULL) != 0 ||
	    sqlparser_json_set_selector_or_null(
		    object,
		    "bind_selector",
		    cell->has_bind_selector ? &cell->bind_selector : NULL,
		    out_error) != 0) {
		return -1;
	}
	return 0;
}

static int sqlparser_view_json_set_target_path_from_column(json_t *object, const sqlparser_column_view_t *column)
{
	json_t *path;
	size_t index;

	path = json_array();
	if (path == NULL) {
		return -1;
	}
	if (column != NULL) {
		for (index = 0U; index < column->target_path_count; index++) {
			json_t *entry;

			entry = json_object();
			if (entry == NULL ||
			    json_object_set_new(entry, "kind", json_string(column->target_path[index].kind != NULL ? column->target_path[index].kind : "")) != 0 ||
			    sqlparser_json_set_string_or_null(entry, "name", column->target_path[index].has_name ? column->target_path[index].name : NULL) != 0 ||
			    sqlparser_json_set_size(entry, "arg_index", column->target_path[index].arg_index) != 0 ||
			    json_array_append_new(path, entry) != 0) {
				json_decref(entry);
				json_decref(path);
				return -1;
			}
		}
	}
	if (json_object_set_new(object, "target_path", path) != 0) {
		json_decref(path);
		return -1;
	}
	return 0;
}

static int sqlparser_view_json_set_column_value_from_view(
	json_t *column_json,
	const sqlparser_column_view_t *column,
	sqlparser_error_t *out_error)
{
	sqlparser_value_view_t value;
	char *value_sql;
	json_t *value_json;
	sqlparser_status_t status;

	if (json_object_set_new(column_json, "value", json_null()) != 0) {
		return -1;
	}
	if (column == NULL || column->handle == NULL || column->value_count == 0U) {
		return 0;
	}
	memset(&value, 0, sizeof(value));
	status = sqlparser_column_value_at(column, 0U, &value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	value_sql = NULL;
	status = sqlparser_value_sql(column->handle, &value, &value_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	value_json = json_object();
	if (value_json == NULL ||
	    json_object_set_new(value_json, "sql", json_string(value_sql != NULL ? value_sql : "")) != 0 ||
	    sqlparser_json_set_selector_or_null(
		    value_json,
		    "selector",
		    value.has_selector ? &value.selector : NULL,
		    out_error) != 0 ||
	    json_object_set_new(column_json, "value", value_json) != 0) {
		json_decref(value_json);
		sqlparser_string_free(value_sql);
		return -1;
	}
	sqlparser_string_free(value_sql);
	return 0;
}

static json_t *sqlparser_view_json_column_from_view(
	const sqlparser_column_view_t *column,
	sqlparser_error_t *out_error)
{
	json_t *column_json;

	column_json = json_object();
	if (column_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	if (json_object_set_new(column_json, "name", json_string(column->name != NULL ? column->name : "")) != 0 ||
	    json_object_set_new(column_json, "keyword", json_string(column->keyword != NULL ? column->keyword : "")) != 0 ||
	    sqlparser_json_set_string_or_null(column_json, "operator", column->operator_name) != 0 ||
	    sqlparser_view_json_set_bind_from_column(column_json, column, out_error) != 0 ||
	    sqlparser_json_set_selector_or_null(column_json, "selector", column->has_selector ? &column->selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_selector_or_null(column_json, "target_list_selector", column->has_target_list_selector ? &column->target_list_selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_selector_or_null(column_json, "target_selector", column->has_target_selector ? &column->target_selector : NULL, out_error) != 0 ||
	    sqlparser_json_set_size_or_null(column_json, "clause_id", column->has_clause_id, column->clause_id) != 0 ||
	    sqlparser_view_json_set_target_path_from_column(column_json, column) != 0 ||
	    sqlparser_view_json_set_column_value_from_view(column_json, column, out_error) != 0) {
		json_decref(column_json);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	return column_json;
}

static json_t *sqlparser_view_json_cell_from_view(
	const sqlparser_cell_view_t *cell,
	sqlparser_error_t *out_error)
{
	json_t *cell_json;
	char *cell_sql;
	sqlparser_status_t status;

	cell_json = json_object();
	cell_sql = NULL;
	if (cell_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	status = sqlparser_cell_sql(cell, &cell_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(cell_json);
		return NULL;
	}
	if (sqlparser_json_set_string_or_null(cell_json, "column", cell->column_name) != 0 ||
	    sqlparser_json_set_size(cell_json, "column_index", cell->column_index) != 0 ||
	    json_object_set_new(cell_json, "sql", json_string(cell_sql != NULL ? cell_sql : "")) != 0 ||
	    sqlparser_view_json_set_bind_from_cell(cell_json, cell, out_error) != 0 ||
	    sqlparser_json_set_selector_or_null(cell_json, "selector", cell->has_selector ? &cell->selector : NULL, out_error) != 0) {
		sqlparser_string_free(cell_sql);
		json_decref(cell_json);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	sqlparser_string_free(cell_sql);
	return cell_json;
}

static json_t *sqlparser_view_json_object_from_view(
	const sqlparser_object_view_t *object,
	sqlparser_error_t *out_error)
{
	json_t *object_json;
	json_t *columns;
	json_t *rows;
	size_t index;

	object_json = json_object();
	columns = json_array();
	rows = json_array();
	if (object_json == NULL || columns == NULL || rows == NULL ||
	    sqlparser_json_set_string_or_null(object_json, "database", object != NULL ? object->database_name : NULL) != 0 ||
	    sqlparser_json_set_string_or_null(object_json, "schema", object != NULL ? object->schema_name : NULL) != 0 ||
	    sqlparser_json_set_string_or_null(object_json, "table", object != NULL ? object->table_name : NULL) != 0 ||
	    sqlparser_json_set_string_or_null(object_json, "alias", object != NULL ? object->alias_name : NULL) != 0 ||
	    sqlparser_json_set_selector_or_null(object_json, "selector", object != NULL && object->has_selector ? &object->selector : NULL, out_error) != 0) {
		json_decref(object_json);
		json_decref(columns);
		json_decref(rows);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	if (json_object_set_new(object_json, "columns", columns) != 0) {
		json_decref(object_json);
		json_decref(columns);
		json_decref(rows);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	columns = NULL;
	if (json_object_set_new(object_json, "rows", rows) != 0) {
		json_decref(object_json);
		json_decref(rows);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	rows = NULL;

	for (index = 0U; object != NULL && index < object->column_count; index++) {
		sqlparser_column_view_t column;
		json_t *column_json;
		sqlparser_status_t status;

		memset(&column, 0, sizeof(column));
		status = sqlparser_object_column_at(object, index, &column, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(object_json);
			return NULL;
		}
		column_json = sqlparser_view_json_column_from_view(&column, out_error);
		if (column_json == NULL ||
		    json_array_append_new(json_object_get(object_json, "columns"), column_json) != 0) {
			json_decref(column_json);
			json_decref(object_json);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	for (index = 0U; object != NULL && index < object->row_count; index++) {
		sqlparser_row_view_t row;
		json_t *row_json;
		json_t *cells;
		size_t cell_index;
		sqlparser_status_t status;

		memset(&row, 0, sizeof(row));
		status = sqlparser_object_row_at(object, index, &row, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(object_json);
			return NULL;
		}
		row_json = json_object();
		cells = json_array();
		if (row_json == NULL || cells == NULL ||
		    sqlparser_json_set_size(row_json, "index", index) != 0) {
			json_decref(row_json);
			json_decref(cells);
			json_decref(object_json);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
		if (json_object_set_new(row_json, "cells", cells) != 0) {
			json_decref(row_json);
			json_decref(cells);
			json_decref(object_json);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
		cells = NULL;
		for (cell_index = 0U; cell_index < row.cell_count; cell_index++) {
			sqlparser_cell_view_t cell;
			json_t *cell_json;

			memset(&cell, 0, sizeof(cell));
			status = sqlparser_row_cell_at(&row, cell_index, &cell, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				json_decref(row_json);
				json_decref(object_json);
				return NULL;
			}
			cell_json = sqlparser_view_json_cell_from_view(&cell, out_error);
			if (cell_json == NULL ||
			    json_array_append_new(json_object_get(row_json, "cells"), cell_json) != 0) {
				json_decref(cell_json);
				json_decref(row_json);
				json_decref(object_json);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return NULL;
			}
		}
		if (json_array_append_new(json_object_get(object_json, "rows"), row_json) != 0) {
			json_decref(row_json);
			json_decref(object_json);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return NULL;
		}
	}
	return object_json;
}

static json_t *sqlparser_view_json_clause_from_view(
	const sqlparser_clause_view_t *clause,
	sqlparser_error_t *out_error)
{
	json_t *clause_json;
	char *clause_sql;
	sqlparser_status_t status;

	clause_json = json_object();
	clause_sql = NULL;
	if (clause_json == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	status = sqlparser_clause_sql(clause, &clause_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(clause_json);
		return NULL;
	}
	if (sqlparser_json_set_size(clause_json, "id", clause->clause_index + 1U) != 0 ||
	    json_object_set_new(clause_json, "kind", json_string(sqlparser_clause_kind_name(clause->kind))) != 0 ||
	    sqlparser_json_set_selector_or_null(clause_json, "selector", clause->has_selector ? &clause->selector : NULL, out_error) != 0) {
		sqlparser_string_free(clause_sql);
		json_decref(clause_json);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	if (sqlparser_json_set_owned_string_or_null(clause_json, "sql", clause_sql) != 0) {
		json_decref(clause_json);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	return clause_json;
}

sqlparser_status_t sqlparser_export_view_json(
	const sqlparser_handle_t *handle,
	int pretty,
	char **out_json,
	sqlparser_error_t *out_error)
{
	json_t *root;
	json_t *statements;
	sqlparser_view_t view;
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

	status = sqlparser_get_view(handle, &view, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		json_decref(root);
		if (!ast_was_loaded) {
			sqlparser_handle_clear_ast(mutable_handle);
		}
		return status;
	}

	for (statement_index = 0U; statement_index < view.statement_count; statement_index++) {
		sqlparser_statement_view_t statement;
		json_t *statement_json;
		json_t *keywords;
		json_t *clauses;
		json_t *objects;
		size_t index;

		memset(&statement, 0, sizeof(statement));
		status = sqlparser_view_statement_at(&view, statement_index, &statement, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			return status;
		}

		statement_json = json_object();
		keywords = json_array();
		clauses = json_array();
		objects = json_array();
		if (statement_json == NULL || keywords == NULL || clauses == NULL || objects == NULL) {
			json_decref(statement_json);
			json_decref(keywords);
			json_decref(clauses);
			json_decref(objects);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}

		for (index = 0U; index < statement.keyword_count; index++) {
			const char *keyword;

			keyword = NULL;
			status = sqlparser_statement_keyword_at(&statement, index, &keyword, out_error);
			if (status != SQLPARSER_STATUS_OK ||
			    json_array_append_new(keywords, json_string(keyword != NULL ? keyword : "")) != 0) {
				json_decref(statement_json);
				json_decref(keywords);
				json_decref(clauses);
				json_decref(objects);
				json_decref(root);
				if (!ast_was_loaded) {
					sqlparser_handle_clear_ast(mutable_handle);
				}
				if (status == SQLPARSER_STATUS_OK) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					status = SQLPARSER_STATUS_NO_MEMORY;
				}
				return status;
			}
		}
		if (sqlparser_json_set_size(statement_json, "index", statement_index) != 0 ||
		    json_object_set_new(statement_json, "keyword", json_string(statement.keyword != NULL ? statement.keyword : "")) != 0) {
			json_decref(keywords);
			json_decref(clauses);
			json_decref(objects);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (json_object_set_new(statement_json, "keywords", keywords) != 0) {
			json_decref(keywords);
			json_decref(clauses);
			json_decref(objects);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		keywords = NULL;
		if (json_object_set_new(statement_json, "clauses", clauses) != 0) {
			json_decref(clauses);
			json_decref(objects);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clauses = NULL;
		if (json_object_set_new(statement_json, "objects", objects) != 0) {
			json_decref(objects);
			json_decref(statement_json);
			json_decref(root);
			if (!ast_was_loaded) {
				sqlparser_handle_clear_ast(mutable_handle);
			}
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		objects = NULL;

		for (index = 0U; index < statement.clause_count; index++) {
			sqlparser_clause_view_t clause;
			json_t *clause_json;

			memset(&clause, 0, sizeof(clause));
			status = sqlparser_statement_clause_at(&statement, index, &clause, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				json_decref(statement_json);
				json_decref(root);
				if (!ast_was_loaded) {
					sqlparser_handle_clear_ast(mutable_handle);
				}
				return status;
			}
			clause_json = sqlparser_view_json_clause_from_view(&clause, out_error);
			if (clause_json == NULL ||
			    json_array_append_new(json_object_get(statement_json, "clauses"), clause_json) != 0) {
				json_decref(clause_json);
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
		}

		for (index = 0U; index < statement.object_count; index++) {
			sqlparser_object_view_t object;
			json_t *object_json;

			memset(&object, 0, sizeof(object));
			status = sqlparser_statement_object_at(&statement, index, &object, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				json_decref(statement_json);
				json_decref(root);
				if (!ast_was_loaded) {
					sqlparser_handle_clear_ast(mutable_handle);
				}
				return status;
			}
			object_json = sqlparser_view_json_object_from_view(&object, out_error);
			if (object_json == NULL ||
			    json_array_append_new(json_object_get(statement_json, "objects"), object_json) != 0) {
				json_decref(object_json);
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
		}

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
	size_t clause_count;
	size_t where_count;
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
		object_count += sqlparser_view_extra_object_count_from_statement(statement);
	}
	where_count = 0U;
	status = sqlparser_statement_where_count(handle, statement_index, &where_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clause_count = 0U;
	status = sqlparser_view_full_clause_count(handle, statement_index, &clause_count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
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
	out_statement->clause_count = clause_count;
	out_statement->where_count = where_count;
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
	size_t relation_count;

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
	relation_count = 0U;
	status = sqlparser_statement_relation_count(
		statement->handle,
		statement->index,
		&relation_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (object_index >= relation_count) {
		if (!sqlparser_view_extra_object_at_from_statement(
			    statement_node,
			    object_index - relation_count,
			    &relation)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "object_index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		out_object->handle = statement->handle;
		out_object->statement_index = statement->index;
		out_object->object_index = object_index;
		out_object->database_name = relation.database_name;
		out_object->schema_name = relation.schema_name;
		out_object->table_name = relation.table_name;
		out_object->alias_name = relation.alias_name;
		memset(&out_object->selector, 0, sizeof(out_object->selector));
		out_object->selector.kind = SQLPARSER_SELECTOR_KIND_UNKNOWN;
		out_object->has_selector = 0;
		out_object->column_count = 0U;
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

sqlparser_status_t sqlparser_statement_clause_at(
	const sqlparser_statement_view_t *statement,
	size_t clause_index,
	sqlparser_clause_view_t *out_clause,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (out_clause == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_clause must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_clause, 0, sizeof(*out_clause));
	if (statement == NULL || statement->handle == NULL || clause_index >= statement->clause_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_view_full_clause_at(
		statement->handle,
		statement->index,
		clause_index,
		out_clause,
		out_error);
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
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *cell_node;
	char *cell_sql;
	sqlparser_status_t status;

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
	cell_node = NULL;
	cell_sql = NULL;
	out_cell->handle = row->handle;
	out_cell->statement_index = row->statement_index;
	out_cell->object_index = row->object_index;
	out_cell->row_index = row->row_index;
	out_cell->cell_index = cell_index;
	out_cell->column_name = column_name;
	out_cell->column_index = cell_index;
	out_cell->selector = selector;
	out_cell->has_selector = 1;
	insert_stmt = NULL;
	values_stmt = NULL;
	status = sqlparser_get_insert_values_stmt(
		(sqlparser_handle_t *)row->handle,
		row->statement_index,
		&insert_stmt,
		&values_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (values_stmt != NULL &&
	    row->row_index < values_stmt->n_values_lists &&
	    values_stmt->values_lists[row->row_index] != NULL &&
	    values_stmt->values_lists[row->row_index]->node_case == PG_QUERY__NODE__NODE_LIST &&
	    values_stmt->values_lists[row->row_index]->list != NULL &&
	    cell_index < values_stmt->values_lists[row->row_index]->list->n_items) {
		cell_node = values_stmt->values_lists[row->row_index]->list->items[cell_index];
		status = sqlparser_cell_sql(out_cell, &cell_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (sqlparser_view_fill_cell_bind_view(
			    out_cell,
			    (sqlparser_handle_t *)row->handle,
			    cell_node,
			    cell_sql,
			    &selector,
			    out_error) < 0) {
			sqlparser_string_free(cell_sql);
			return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
				out_error->code :
				SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		sqlparser_string_free(cell_sql);
	}
	(void)insert_stmt;
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
