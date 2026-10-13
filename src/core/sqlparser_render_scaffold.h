#ifndef SQLPARSER_RENDER_SCAFFOLD_H
#define SQLPARSER_RENDER_SCAFFOLD_H

#include "sqlparser/sqlparser.h"
#include "protobuf/pg_query.pb-c.h"

typedef enum {
	SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET,
	SQLPARSER_RENDER_SCAFFOLD_WHERE,
	SQLPARSER_RENDER_SCAFFOLD_ORDER_BY,
	SQLPARSER_RENDER_SCAFFOLD_INSERT_CELL,
	SQLPARSER_RENDER_SCAFFOLD_UPDATE_ASSIGNMENT,
	SQLPARSER_RENDER_SCAFFOLD_VARIABLE_SET_ARG
} sqlparser_render_scaffold_kind_t;

/* A local, allocation-free AST for the six fixed internal rendering wrappers.
 * Only ast is the caller-facing member; the remaining fields are its storage.
 *
 * This object is self-referential: do not move or copy it after initialization.
 * The AST and its placeholder nodes/arrays/strings must NEVER be passed to a
 * recursive protobuf free routine. There is no scaffold destructor. Callers
 * may replace a slot or list with their independently owned, generated-marked
 * nodes for synchronous deparse only. Such replacements are borrowed, never
 * owned by this object, and must be freed separately by their original owner.
 * Reinitialization does not free or otherwise inspect borrowed replacements.
 */
typedef struct {
	PgQuery__ParseResult ast;
	PgQuery__RawStmt raw_stmt;
	PgQuery__RawStmt *statements[1];
	PgQuery__Node statement_node;
	PgQuery__SelectStmt select_stmt;
	union {
		PgQuery__InsertStmt insert_stmt;
		PgQuery__UpdateStmt update_stmt;
		PgQuery__VariableSetStmt variable_set_stmt;
	} command;
	PgQuery__RangeVar relation;
	PgQuery__Node relation_node;
	PgQuery__ResTarget target;
	PgQuery__Node target_node;
	PgQuery__AConst value;
	PgQuery__Node value_node;
	union {
		PgQuery__Integer integer;
		PgQuery__String string;
	} literal;
	PgQuery__Node auxiliary_node;
	PgQuery__Node item_node;
	PgQuery__AConst auxiliary_value;
	union {
		PgQuery__Boolean boolean;
		PgQuery__Integer integer;
		PgQuery__List list;
	} auxiliary;
	PgQuery__SortBy sort_by;
	PgQuery__Node *targets[1];
	PgQuery__Node *relations[1];
	PgQuery__Node *children[1];
	PgQuery__Node *items[1];
	char relation_name[sizeof("__sqlparser_source__")];
	char field_name[sizeof("__sqlparser_session_parameter__")];
	char string_value[sizeof("__sqlparser_placeholder__")];
	char relation_persistence[2];
} sqlparser_render_scaffold_t;

sqlparser_status_t sqlparser_render_scaffold_init(
	sqlparser_render_scaffold_t *scaffold,
	sqlparser_render_scaffold_kind_t kind,
	sqlparser_error_t *out_error);

#endif
