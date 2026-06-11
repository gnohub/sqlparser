#ifndef SQLPARSER_DIALECT_MULTI_INSERT_TYPES_H
#define SQLPARSER_DIALECT_MULTI_INSERT_TYPES_H

#include <stddef.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_DIALECT_MULTI_INSERT_NONE = 0,
	SQLPARSER_DIALECT_MULTI_INSERT_ALL = 1,
	SQLPARSER_DIALECT_MULTI_INSERT_FIRST = 2
} sqlparser_dialect_multi_insert_mode_t;

typedef struct {
	char *database_name;
	char *schema_name;
	char *table_name;
	char *sql;
} sqlparser_dialect_multi_insert_relation_t;

typedef struct {
	char *name;
	char *sql;
} sqlparser_dialect_multi_insert_column_t;

typedef struct {
	char *public_sql;
	char *parser_sql;
	int has_bind;
	sqlparser_bind_kind_t bind_kind;
	char bind[SQLPARSER_BIND_TEXT_CAPACITY];
	char bind_sql[SQLPARSER_BIND_SQL_CAPACITY];
	size_t bind_position;
	int has_bind_position;
	int has_literal;
	sqlparser_literal_view_t literal;
	char *literal_string_value;
	char *literal_float_value;
} sqlparser_dialect_multi_insert_value_t;

typedef struct {
	size_t ordinal;
	sqlparser_dialect_multi_insert_relation_t relation;
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t column_count;
	sqlparser_dialect_multi_insert_value_t *cells;
	size_t cell_count;
	char *condition_public_sql;
	char *condition_parser_sql;
	int has_condition;
	int is_else;
	size_t condition_group_id;
} sqlparser_dialect_multi_insert_branch_t;

typedef struct {
	sqlparser_dialect_multi_insert_mode_t mode;
	sqlparser_dialect_multi_insert_branch_t *branches;
	size_t branch_count;
	char *source_public_sql;
	char *source_parser_sql;
} sqlparser_dialect_multi_insert_t;

#endif
