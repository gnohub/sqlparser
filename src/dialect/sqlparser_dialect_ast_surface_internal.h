#ifndef SQLPARSER_DIALECT_AST_SURFACE_INTERNAL_H
#define SQLPARSER_DIALECT_AST_SURFACE_INTERNAL_H

#include "sqlparser_dialect_internal.h"

typedef struct {
	void *context;
	void (*string_literal)(
		PgQuery__AConst *literal,
		size_t statement_index,
		void *context);
	void (*param_ref)(
		PgQuery__ParamRef *param_ref,
		size_t statement_index,
		void *context);
	void (*relation)(
		PgQuery__RangeVar *relation,
		size_t statement_index,
		void *context);
	void (*derived_relation)(
		PgQuery__RangeSubselect *relation,
		size_t statement_index,
		void *context);
	void (*insert_relation)(
		PgQuery__RangeVar *relation,
		size_t statement_index,
		void *context);
	void (*join)(
		PgQuery__JoinExpr *join,
		size_t statement_index,
		void *context);
	void (*select_limit)(
		PgQuery__SelectStmt *select,
		size_t statement_index,
		void *context);
	void (*set_operation)(
		PgQuery__SelectStmt *select,
		size_t statement_index,
		void *context);
} sqlparser_dialect_ast_surface_visitor_t;

typedef struct {
	void *context;
	sqlparser_status_t (*string_literal)(
		const PgQuery__AConst *source,
		PgQuery__AConst *clone,
		void *context,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*relation)(
		const PgQuery__RangeVar *source,
		PgQuery__RangeVar *clone,
		void *context,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*join)(
		const PgQuery__JoinExpr *source,
		PgQuery__JoinExpr *clone,
		void *context,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*select_limit)(
		const PgQuery__SelectStmt *source,
		PgQuery__SelectStmt *clone,
		void *context,
		sqlparser_error_t *out_error);
	sqlparser_status_t (*set_operation)(
		const PgQuery__SelectStmt *source,
		PgQuery__SelectStmt *clone,
		void *context,
		sqlparser_error_t *out_error);
} sqlparser_dialect_ast_surface_clone_visitor_t;

void sqlparser_dialect_ast_surface_visit(
	const PgQuery__ParseResult *ast,
	const sqlparser_dialect_ast_surface_visitor_t *visitor);
void sqlparser_dialect_ast_surface_visit_roots(
	ProtobufCMessage *const *roots,
	size_t root_count,
	size_t statement_index,
	const sqlparser_dialect_ast_surface_visitor_t *visitor);
sqlparser_status_t sqlparser_dialect_ast_surface_clone(
	const ProtobufCMessage *source,
	ProtobufCMessage *clone,
	const sqlparser_dialect_ast_surface_clone_visitor_t *visitor,
	sqlparser_error_t *out_error);

#endif
