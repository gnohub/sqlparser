#include <string.h>

#include "sqlparser_internal.h"
#include "sqlparser_render_scaffold.h"

static void sqlparser_render_scaffold_select_init(
	sqlparser_render_scaffold_t *scaffold)
{
	scaffold->select_stmt = (PgQuery__SelectStmt)PG_QUERY__SELECT_STMT__INIT;
	/* PostgreSQL grammar defaults differ from the protobuf enum zero values. */
	scaffold->select_stmt.limit_option = PG_QUERY__LIMIT_OPTION__LIMIT_OPTION_DEFAULT;
	scaffold->select_stmt.op = PG_QUERY__SET_OPERATION__SETOP_NONE;
}

static void sqlparser_render_scaffold_relation_init(
	sqlparser_render_scaffold_t *scaffold,
	const char *name,
	int32_t location)
{
	scaffold->relation = (PgQuery__RangeVar)PG_QUERY__RANGE_VAR__INIT;
	strcpy(scaffold->relation_name, name);
	scaffold->relation_persistence[0] = 'p';
	scaffold->relation_persistence[1] = '\0';
	scaffold->relation.relname = scaffold->relation_name;
	scaffold->relation.inh = 1;
	scaffold->relation.relpersistence = scaffold->relation_persistence;
	scaffold->relation.location = location;
}

static void sqlparser_render_scaffold_target_init(
	sqlparser_render_scaffold_t *scaffold,
	int32_t location)
{
	scaffold->target = (PgQuery__ResTarget)PG_QUERY__RES_TARGET__INIT;
	scaffold->target_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
	scaffold->target_node.node_case = PG_QUERY__NODE__NODE_RES_TARGET;
	scaffold->target_node.res_target = &scaffold->target;
	scaffold->target.val = &scaffold->value_node;
	scaffold->target.location = location;
	scaffold->targets[0] = &scaffold->target_node;
}

sqlparser_status_t sqlparser_render_scaffold_init(
	sqlparser_render_scaffold_t *scaffold,
	sqlparser_render_scaffold_kind_t kind,
	sqlparser_error_t *out_error)
{
	if (scaffold == NULL ||
	    kind < SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET ||
	    kind > SQLPARSER_RENDER_SCAFFOLD_VARIABLE_SET_ARG) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"render scaffold requires a non-NULL object and a valid kind");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	scaffold->ast = (PgQuery__ParseResult)PG_QUERY__PARSE_RESULT__INIT;
	scaffold->raw_stmt = (PgQuery__RawStmt)PG_QUERY__RAW_STMT__INIT;
	scaffold->statement_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
	scaffold->value = (PgQuery__AConst)PG_QUERY__A__CONST__INIT;
	scaffold->value_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
	scaffold->ast.version = PG_VERSION_NUM;
	scaffold->ast.n_stmts = 1U;
	scaffold->ast.stmts = scaffold->statements;
	scaffold->statements[0] = &scaffold->raw_stmt;
	scaffold->raw_stmt.stmt = &scaffold->statement_node;
	scaffold->value_node.node_case = PG_QUERY__NODE__NODE_A_CONST;
	scaffold->value_node.a_const = &scaffold->value;

	if (kind == SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET ||
	    kind == SQLPARSER_RENDER_SCAFFOLD_WHERE ||
	    kind == SQLPARSER_RENDER_SCAFFOLD_ORDER_BY) {
		sqlparser_render_scaffold_select_init(scaffold);
		sqlparser_render_scaffold_relation_init(
			scaffold, "__sqlparser_source__",
			kind == SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET ? 17 : 14);
		sqlparser_render_scaffold_target_init(scaffold, 7);
		scaffold->statement_node.node_case = PG_QUERY__NODE__NODE_SELECT_STMT;
		scaffold->statement_node.select_stmt = &scaffold->select_stmt;
		scaffold->select_stmt.n_target_list = 1U;
		scaffold->select_stmt.target_list = scaffold->targets;
		scaffold->relation_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
		scaffold->relation_node.node_case = PG_QUERY__NODE__NODE_RANGE_VAR;
		scaffold->relation_node.range_var = &scaffold->relation;
		scaffold->relations[0] = &scaffold->relation_node;
		scaffold->select_stmt.n_from_clause = 1U;
		scaffold->select_stmt.from_clause = scaffold->relations;
		scaffold->value.location = 7;
		if (kind == SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET) {
			scaffold->value.isnull = 1;
		} else {
			scaffold->literal.integer = (PgQuery__Integer)PG_QUERY__INTEGER__INIT;
			scaffold->literal.integer.ival = 1;
			scaffold->value.val_case = PG_QUERY__A__CONST__VAL_IVAL;
			scaffold->value.ival = &scaffold->literal.integer;
			scaffold->auxiliary_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
			scaffold->auxiliary_value = (PgQuery__AConst)PG_QUERY__A__CONST__INIT;
			if (kind == SQLPARSER_RENDER_SCAFFOLD_WHERE) {
				scaffold->auxiliary.boolean = (PgQuery__Boolean)PG_QUERY__BOOLEAN__INIT;
				scaffold->auxiliary.boolean.boolval = 1;
				scaffold->auxiliary_value.location = 41;
				scaffold->auxiliary_value.val_case = PG_QUERY__A__CONST__VAL_BOOLVAL;
				scaffold->auxiliary_value.boolval = &scaffold->auxiliary.boolean;
				scaffold->auxiliary_node.node_case = PG_QUERY__NODE__NODE_A_CONST;
				scaffold->auxiliary_node.a_const = &scaffold->auxiliary_value;
				scaffold->select_stmt.where_clause = &scaffold->auxiliary_node;
			} else {
				scaffold->sort_by = (PgQuery__SortBy)PG_QUERY__SORT_BY__INIT;
				scaffold->item_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
				scaffold->auxiliary.integer = (PgQuery__Integer)PG_QUERY__INTEGER__INIT;
				scaffold->auxiliary.integer.ival = 1;
				scaffold->auxiliary_value.location = 44;
				scaffold->auxiliary_value.val_case = PG_QUERY__A__CONST__VAL_IVAL;
				scaffold->auxiliary_value.ival = &scaffold->auxiliary.integer;
				scaffold->item_node.node_case = PG_QUERY__NODE__NODE_A_CONST;
				scaffold->item_node.a_const = &scaffold->auxiliary_value;
				scaffold->sort_by.node = &scaffold->item_node;
				scaffold->sort_by.sortby_dir = PG_QUERY__SORT_BY_DIR__SORTBY_DEFAULT;
				scaffold->sort_by.sortby_nulls = PG_QUERY__SORT_BY_NULLS__SORTBY_NULLS_DEFAULT;
				scaffold->sort_by.location = -1;
				scaffold->auxiliary_node.node_case = PG_QUERY__NODE__NODE_SORT_BY;
				scaffold->auxiliary_node.sort_by = &scaffold->sort_by;
				scaffold->children[0] = &scaffold->auxiliary_node;
				scaffold->select_stmt.n_sort_clause = 1U;
				scaffold->select_stmt.sort_clause = scaffold->children;
			}
		}
	} else if (kind == SQLPARSER_RENDER_SCAFFOLD_INSERT_CELL) {
		sqlparser_render_scaffold_select_init(scaffold);
		sqlparser_render_scaffold_relation_init(scaffold, "__sqlparser_target__", 12);
		scaffold->command.insert_stmt = (PgQuery__InsertStmt)PG_QUERY__INSERT_STMT__INIT;
		scaffold->statement_node.node_case = PG_QUERY__NODE__NODE_INSERT_STMT;
		scaffold->statement_node.insert_stmt = &scaffold->command.insert_stmt;
		scaffold->command.insert_stmt.relation = &scaffold->relation;
		scaffold->command.insert_stmt.override = PG_QUERY__OVERRIDING_KIND__OVERRIDING_NOT_SET;
		scaffold->auxiliary_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
		scaffold->auxiliary_node.node_case = PG_QUERY__NODE__NODE_SELECT_STMT;
		scaffold->auxiliary_node.select_stmt = &scaffold->select_stmt;
		scaffold->command.insert_stmt.select_stmt = &scaffold->auxiliary_node;
		scaffold->item_node = (PgQuery__Node)PG_QUERY__NODE__INIT;
		scaffold->auxiliary.list = (PgQuery__List)PG_QUERY__LIST__INIT;
		scaffold->item_node.node_case = PG_QUERY__NODE__NODE_LIST;
		scaffold->item_node.list = &scaffold->auxiliary.list;
		scaffold->children[0] = &scaffold->item_node;
		scaffold->select_stmt.n_values_lists = 1U;
		scaffold->select_stmt.values_lists = scaffold->children;
		scaffold->items[0] = &scaffold->value_node;
		scaffold->auxiliary.list.n_items = 1U;
		scaffold->auxiliary.list.items = scaffold->items;
		scaffold->value.isnull = 1;
		scaffold->value.location = 41;
	} else if (kind == SQLPARSER_RENDER_SCAFFOLD_UPDATE_ASSIGNMENT) {
		sqlparser_render_scaffold_relation_init(scaffold, "__sqlparser_target__", 7);
		sqlparser_render_scaffold_target_init(scaffold, 32);
		scaffold->command.update_stmt = (PgQuery__UpdateStmt)PG_QUERY__UPDATE_STMT__INIT;
		scaffold->statement_node.node_case = PG_QUERY__NODE__NODE_UPDATE_STMT;
		scaffold->statement_node.update_stmt = &scaffold->command.update_stmt;
		scaffold->command.update_stmt.relation = &scaffold->relation;
		scaffold->command.update_stmt.n_target_list = 1U;
		scaffold->command.update_stmt.target_list = scaffold->targets;
		strcpy(scaffold->field_name, "__sqlparser_column__");
		scaffold->target.name = scaffold->field_name;
		scaffold->value.isnull = 1;
		scaffold->value.location = 55;
	} else {
		scaffold->command.variable_set_stmt = (PgQuery__VariableSetStmt)PG_QUERY__VARIABLE_SET_STMT__INIT;
		scaffold->statement_node.node_case = PG_QUERY__NODE__NODE_VARIABLE_SET_STMT;
		scaffold->statement_node.variable_set_stmt = &scaffold->command.variable_set_stmt;
		scaffold->command.variable_set_stmt.kind = PG_QUERY__VARIABLE_SET_KIND__VAR_SET_VALUE;
		strcpy(scaffold->field_name, "__sqlparser_session_parameter__");
		scaffold->command.variable_set_stmt.name = scaffold->field_name;
		scaffold->items[0] = &scaffold->value_node;
		scaffold->command.variable_set_stmt.n_args = 1U;
		scaffold->command.variable_set_stmt.args = scaffold->items;
		scaffold->literal.string = (PgQuery__String)PG_QUERY__STRING__INIT;
		strcpy(scaffold->string_value, "__sqlparser_placeholder__");
		scaffold->literal.string.sval = scaffold->string_value;
		scaffold->value.val_case = PG_QUERY__A__CONST__VAL_SVAL;
		scaffold->value.sval = &scaffold->literal.string;
		scaffold->value.location = 38;
	}
	return SQLPARSER_STATUS_OK;
}
