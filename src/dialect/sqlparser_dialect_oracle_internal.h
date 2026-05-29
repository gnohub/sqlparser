#ifndef SQLPARSER_DIALECT_ORACLE_INTERNAL_H
#define SQLPARSER_DIALECT_ORACLE_INTERNAL_H

#include <stddef.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_ORACLE_MULTI_INSERT_NONE = 0,
	SQLPARSER_ORACLE_MULTI_INSERT_ALL = 1,
	SQLPARSER_ORACLE_MULTI_INSERT_FIRST = 2
} sqlparser_oracle_multi_insert_mode_t;

typedef struct {
	char *database_name;
	char *schema_name;
	char *table_name;
	char *sql;
} sqlparser_oracle_relation_t;

typedef struct {
	char *name;
	char *sql;
} sqlparser_oracle_column_t;

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
} sqlparser_oracle_value_t;

typedef struct {
	size_t ordinal;
	sqlparser_oracle_relation_t relation;
	sqlparser_oracle_column_t *columns;
	size_t column_count;
	sqlparser_oracle_value_t *cells;
	size_t cell_count;
	char *condition_public_sql;
	char *condition_parser_sql;
	int has_condition;
} sqlparser_oracle_multi_insert_branch_t;

typedef struct {
	sqlparser_oracle_multi_insert_mode_t mode;
	sqlparser_oracle_multi_insert_branch_t *branches;
	size_t branch_count;
	char *source_public_sql;
	char *source_parser_sql;
} sqlparser_oracle_multi_insert_t;

int sqlparser_oracle_state_has_multi_insert(const void *state);
const sqlparser_oracle_multi_insert_t *sqlparser_oracle_state_multi_insert(const void *state);
sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_bind(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_bind_value_t *bind,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error);

#endif
