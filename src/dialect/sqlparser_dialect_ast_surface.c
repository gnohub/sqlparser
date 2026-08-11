#include <stdint.h>

#include "sqlparser_dialect_ast_surface_internal.h"

static void sqlparser_dialect_ast_surface_visit_message(
	ProtobufCMessage *message,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor);
static void sqlparser_dialect_ast_surface_visit_generic(
	ProtobufCMessage *message,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor);

static void sqlparser_dialect_ast_surface_visit_node_array(
	PgQuery__Node *const *items,
	size_t count,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	size_t index;

	for (index = 0U; index < count; index++) {
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)items[index],
			statement_index,
			visitor);
	}
}

static void sqlparser_dialect_ast_surface_visit_select(
	PgQuery__SelectStmt *select,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	if (select == NULL) {
		return;
	}
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)select->with_clause,
		statement_index,
		visitor);
	if (select->op != PG_QUERY__SET_OPERATION__SETOP_NONE) {
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->larg,
			statement_index,
			visitor);
		if (visitor->set_operation != NULL) {
			visitor->set_operation(
				select, statement_index, visitor->context);
		}
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->rarg,
			statement_index,
			visitor);
	} else {
		sqlparser_dialect_ast_surface_visit_node_array(
			select->distinct_clause,
			select->n_distinct_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			select->target_list,
			select->n_target_list,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->into_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			select->from_clause,
			select->n_from_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->where_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->start_with_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->connect_by_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			select->group_clause,
			select->n_group_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)select->having_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			select->window_clause,
			select->n_window_clause,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			select->values_lists,
			select->n_values_lists,
			statement_index,
			visitor);
	}
	sqlparser_dialect_ast_surface_visit_node_array(
		select->sort_clause,
		select->n_sort_clause,
		statement_index,
		visitor);
	if (select->limit_count != NULL &&
	    select->limit_clause_style ==
		    PG_QUERY__LIMIT_CLAUSE_STYLE__LIMIT_CLAUSE_STYLE_LIMIT &&
	    visitor->select_limit != NULL) {
		visitor->select_limit(select, statement_index, visitor->context);
	}
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)select->limit_count,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)select->limit_offset,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		select->locking_clause,
		select->n_locking_clause,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_insert(
	PgQuery__InsertStmt *insert,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)insert->with_clause,
		statement_index,
		visitor);
	if (insert->relation != NULL) {
		if (visitor->insert_relation != NULL) {
			visitor->insert_relation(
				insert->relation,
				statement_index,
				visitor->context);
		}
		sqlparser_dialect_ast_surface_visit_generic(
			(ProtobufCMessage *)insert->relation,
			statement_index,
			visitor);
	}
	sqlparser_dialect_ast_surface_visit_node_array(
		insert->cols, insert->n_cols, statement_index, visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)insert->select_stmt,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)insert->on_conflict_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		insert->returning_list,
		insert->n_returning_list,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_update(
	PgQuery__UpdateStmt *update,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)update->with_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)update->relation,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		update->target_list,
		update->n_target_list,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		update->from_clause,
		update->n_from_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)update->where_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		update->returning_list,
		update->n_returning_list,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_delete(
	PgQuery__DeleteStmt *delete_stmt,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)delete_stmt->with_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)delete_stmt->relation,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		delete_stmt->using_clause,
		delete_stmt->n_using_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)delete_stmt->where_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		delete_stmt->returning_list,
		delete_stmt->n_returning_list,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_merge(
	PgQuery__MergeStmt *merge,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)merge->with_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)merge->relation,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)merge->source_relation,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)merge->join_condition,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		merge->merge_when_clauses,
		merge->n_merge_when_clauses,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		merge->returning_list,
		merge->n_returning_list,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_join(
	PgQuery__JoinExpr *join,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)join->larg,
		statement_index,
		visitor);
	if (visitor->join != NULL) {
		visitor->join(join, statement_index, visitor->context);
	}
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)join->rarg,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		join->using_clause,
		join->n_using_clause,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)join->join_using_alias,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)join->quals,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)join->alias,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_res_target(
	PgQuery__ResTarget *target,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	sqlparser_dialect_ast_surface_visit_message(
		(ProtobufCMessage *)target->val,
		statement_index,
		visitor);
	sqlparser_dialect_ast_surface_visit_node_array(
		target->indirection,
		target->n_indirection,
		statement_index,
		visitor);
}

static void sqlparser_dialect_ast_surface_visit_generic(
	ProtobufCMessage *message,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	const ProtobufCMessageDescriptor *descriptor;
	const uint8_t *base;
	unsigned int field_index;

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return;
	}
	base = (const uint8_t *)message;
	for (field_index = 0U;
	     field_index < descriptor->n_fields;
	     field_index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[field_index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}
		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			ProtobufCMessage *const *items;
			size_t count;
			size_t item_index;

			count = *(const size_t *)(base + field->quantifier_offset);
			items = *(ProtobufCMessage *const * const *)(base + field->offset);
			for (item_index = 0U; item_index < count; item_index++) {
				sqlparser_dialect_ast_surface_visit_message(
					items[item_index],
					statement_index,
					visitor);
			}
			continue;
		}
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) !=
			    (int)field->id) {
			continue;
		}
		sqlparser_dialect_ast_surface_visit_message(
			*(ProtobufCMessage **)(base + field->offset),
			statement_index,
			visitor);
	}
}

static void sqlparser_dialect_ast_surface_visit_message(
	ProtobufCMessage *message,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	if (message == NULL || visitor == NULL) {
		return;
	}
	if (message->descriptor == &pg_query__a__const__descriptor) {
		PgQuery__AConst *literal;

		literal = (PgQuery__AConst *)message;
		if (((literal->val_case == PG_QUERY__A__CONST__VAL_SVAL &&
		      literal->sval != NULL) ||
		     (literal->val_case == PG_QUERY__A__CONST__VAL_BSVAL &&
		      literal->bsval != NULL)) &&
		    visitor->string_literal != NULL) {
			visitor->string_literal(
				literal, statement_index, visitor->context);
		}
		return;
	}
	if (message->descriptor == &pg_query__param_ref__descriptor) {
		if (visitor->param_ref != NULL) {
			visitor->param_ref(
				(PgQuery__ParamRef *)message,
				statement_index,
				visitor->context);
		}
		return;
	}
	if (message->descriptor == &pg_query__range_var__descriptor) {
		if (visitor->relation != NULL) {
			visitor->relation(
				(PgQuery__RangeVar *)message,
				statement_index,
				visitor->context);
		}
		sqlparser_dialect_ast_surface_visit_generic(
			message, statement_index, visitor);
		return;
	}
	if (message->descriptor == &pg_query__range_subselect__descriptor) {
		if (visitor->derived_relation != NULL) {
			visitor->derived_relation(
				(PgQuery__RangeSubselect *)message,
				statement_index,
				visitor->context);
		}
		sqlparser_dialect_ast_surface_visit_generic(
			message, statement_index, visitor);
		return;
	}
	if (message->descriptor == &pg_query__select_stmt__descriptor) {
		sqlparser_dialect_ast_surface_visit_select(
			(PgQuery__SelectStmt *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__insert_stmt__descriptor) {
		sqlparser_dialect_ast_surface_visit_insert(
			(PgQuery__InsertStmt *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__update_stmt__descriptor) {
		sqlparser_dialect_ast_surface_visit_update(
			(PgQuery__UpdateStmt *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__delete_stmt__descriptor) {
		sqlparser_dialect_ast_surface_visit_delete(
			(PgQuery__DeleteStmt *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__merge_stmt__descriptor) {
		sqlparser_dialect_ast_surface_visit_merge(
			(PgQuery__MergeStmt *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__join_expr__descriptor) {
		sqlparser_dialect_ast_surface_visit_join(
			(PgQuery__JoinExpr *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__res_target__descriptor) {
		sqlparser_dialect_ast_surface_visit_res_target(
			(PgQuery__ResTarget *)message,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__view_stmt__descriptor) {
		PgQuery__ViewStmt *view;

		view = (PgQuery__ViewStmt *)message;
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)view->view,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			view->aliases,
			view->n_aliases,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)view->query,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_node_array(
			view->options,
			view->n_options,
			statement_index,
			visitor);
		return;
	}
	if (message->descriptor == &pg_query__create_table_as_stmt__descriptor) {
		PgQuery__CreateTableAsStmt *create;

		create = (PgQuery__CreateTableAsStmt *)message;
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)create->into,
			statement_index,
			visitor);
		sqlparser_dialect_ast_surface_visit_message(
			(ProtobufCMessage *)create->query,
			statement_index,
			visitor);
		return;
	}
	sqlparser_dialect_ast_surface_visit_generic(
		message, statement_index, visitor);
}

void sqlparser_dialect_ast_surface_visit(
	const PgQuery__ParseResult *ast,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	size_t statement_index;

	for (statement_index = 0U;
	     ast != NULL && statement_index < ast->n_stmts;
	     statement_index++) {
		PgQuery__RawStmt *raw_stmt;

		raw_stmt = ast->stmts[statement_index];
		sqlparser_dialect_ast_surface_visit_message(
			raw_stmt != NULL ?
				(ProtobufCMessage *)raw_stmt->stmt : NULL,
			statement_index,
			visitor);
	}
}

void sqlparser_dialect_ast_surface_visit_roots(
	ProtobufCMessage *const *roots,
	size_t root_count,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor)
{
	size_t root_index;

	for (root_index = 0U; root_index < root_count; root_index++) {
		sqlparser_dialect_ast_surface_visit_message(
			roots[root_index], statement_index, visitor);
	}
}

sqlparser_status_t sqlparser_dialect_ast_surface_clone(
	const ProtobufCMessage *source,
	ProtobufCMessage *clone,
	const sqlparser_dialect_ast_surface_clone_visitor_t *visitor,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	const uint8_t *source_base;
	uint8_t *clone_base;
	unsigned int field_index;
	sqlparser_status_t status;

	if (source == NULL || clone == NULL) {
		return source == NULL && clone == NULL ?
			SQLPARSER_STATUS_OK : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (visitor == NULL || source->descriptor != clone->descriptor) {
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	status = SQLPARSER_STATUS_OK;
	if (source->descriptor == &pg_query__a__const__descriptor &&
	    visitor->string_literal != NULL) {
		status = visitor->string_literal(
			(const PgQuery__AConst *)source,
			(PgQuery__AConst *)clone,
			visitor->context,
			out_error);
	} else if (source->descriptor == &pg_query__range_var__descriptor &&
		   visitor->relation != NULL) {
		status = visitor->relation(
			(const PgQuery__RangeVar *)source,
			(PgQuery__RangeVar *)clone,
			visitor->context,
			out_error);
	} else if (source->descriptor == &pg_query__join_expr__descriptor &&
		   visitor->join != NULL) {
		status = visitor->join(
			(const PgQuery__JoinExpr *)source,
			(PgQuery__JoinExpr *)clone,
			visitor->context,
			out_error);
	} else if (source->descriptor == &pg_query__select_stmt__descriptor) {
		const PgQuery__SelectStmt *source_select;
		PgQuery__SelectStmt *clone_select;

		source_select = (const PgQuery__SelectStmt *)source;
		clone_select = (PgQuery__SelectStmt *)clone;
		if (source_select->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
		    visitor->set_operation != NULL) {
			status = visitor->set_operation(
				source_select,
				clone_select,
				visitor->context,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK &&
		    source_select->limit_count != NULL &&
		    source_select->limit_clause_style ==
			    PG_QUERY__LIMIT_CLAUSE_STYLE__LIMIT_CLAUSE_STYLE_LIMIT &&
		    visitor->select_limit != NULL) {
			status = visitor->select_limit(
				source_select,
				clone_select,
				visitor->context,
				out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	descriptor = source->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	source_base = (const uint8_t *)source;
	clone_base = (uint8_t *)clone;
	for (field_index = 0U;
	     field_index < descriptor->n_fields;
	     field_index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[field_index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(source_base + field->quantifier_offset) !=
			    *(const int *)(clone_base + field->quantifier_offset)) {
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			ProtobufCMessage *const *source_items;
			ProtobufCMessage *const *clone_items;
			size_t source_count;
			size_t clone_count;
			size_t item_index;

			source_count = *(const size_t *)(
				source_base + field->quantifier_offset);
			clone_count = *(const size_t *)(
				clone_base + field->quantifier_offset);
			if (source_count != clone_count) {
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			source_items = *(ProtobufCMessage *const * const *)(
				source_base + field->offset);
			clone_items = *(ProtobufCMessage *const * const *)(
				clone_base + field->offset);
			if (source_count > 0U &&
			    (source_items == NULL || clone_items == NULL)) {
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			for (item_index = 0U;
			     item_index < source_count;
			     item_index++) {
				status = sqlparser_dialect_ast_surface_clone(
					source_items[item_index],
					clone_items[item_index],
					visitor,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
			continue;
		}
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(source_base + field->quantifier_offset) !=
			    (int)field->id) {
			continue;
		}
		status = sqlparser_dialect_ast_surface_clone(
			*(ProtobufCMessage *const *)(source_base + field->offset),
			*(ProtobufCMessage **)(clone_base + field->offset),
			visitor,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}
