#ifndef SQLPARSER_SQLPARSER_H
#define SQLPARSER_SQLPARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlparser_handle sqlparser_handle_t;

typedef enum {
	SQLPARSER_STATUS_OK = 0,
	SQLPARSER_STATUS_INVALID_ARGUMENT = 1,
	SQLPARSER_STATUS_NO_MEMORY = 2,
	SQLPARSER_STATUS_PARSE_ERROR = 3,
	SQLPARSER_STATUS_INTERNAL_ERROR = 4,
	SQLPARSER_STATUS_UNSUPPORTED = 5,
	SQLPARSER_STATUS_RESOURCE_LIMIT = 6
} sqlparser_status_t;

typedef enum {
	SQLPARSER_STATEMENT_KIND_UNKNOWN = 0,
	SQLPARSER_STATEMENT_KIND_SELECT = 1,
	SQLPARSER_STATEMENT_KIND_INSERT = 2,
	SQLPARSER_STATEMENT_KIND_UPDATE = 3,
	SQLPARSER_STATEMENT_KIND_DELETE = 4,
	SQLPARSER_STATEMENT_KIND_MERGE = 5,
	SQLPARSER_STATEMENT_KIND_TRANSACTION = 6,
	SQLPARSER_STATEMENT_KIND_DDL = 7,
	SQLPARSER_STATEMENT_KIND_CALL = 8,
	SQLPARSER_STATEMENT_KIND_OTHER = 9
} sqlparser_statement_kind_t;

typedef enum {
	SQLPARSER_INSERT_SOURCE_UNKNOWN = 0,
	SQLPARSER_INSERT_SOURCE_VALUES = 1,
	SQLPARSER_INSERT_SOURCE_QUERY = 2
} sqlparser_insert_source_kind_t;

typedef enum {
	SQLPARSER_VALUE_KIND_UNKNOWN = 0,
	SQLPARSER_VALUE_KIND_LITERAL = 1,
	SQLPARSER_VALUE_KIND_DEFAULT = 2,
	SQLPARSER_VALUE_KIND_EXPRESSION = 3
} sqlparser_value_kind_t;

typedef enum {
	SQLPARSER_LITERAL_KIND_UNKNOWN = 0,
	SQLPARSER_LITERAL_KIND_NULL = 1,
	SQLPARSER_LITERAL_KIND_STRING = 2,
	SQLPARSER_LITERAL_KIND_INTEGER = 3,
	SQLPARSER_LITERAL_KIND_FLOAT = 4,
	SQLPARSER_LITERAL_KIND_BOOLEAN = 5
} sqlparser_literal_kind_t;

typedef enum {
	SQLPARSER_SELECTOR_KIND_UNKNOWN = 0,
	SQLPARSER_SELECTOR_KIND_RELATION = 1,
	SQLPARSER_SELECTOR_KIND_NAME = 2,
	SQLPARSER_SELECTOR_KIND_LITERAL = 3,
	SQLPARSER_SELECTOR_KIND_WHERE_LITERAL = 4,
	SQLPARSER_SELECTOR_KIND_ASSIGNMENT = 5,
	SQLPARSER_SELECTOR_KIND_INSERT_CELL = 6,
	SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS = 7,
	SQLPARSER_SELECTOR_KIND_INSERT_ROW = 8,
	SQLPARSER_SELECTOR_KIND_VALUE = 9
} sqlparser_selector_kind_t;

typedef enum {
	SQLPARSER_DIALECT_POSTGRESQL = 0,
	SQLPARSER_DIALECT_MYSQL = 1,
	SQLPARSER_DIALECT_ORACLE = 2,
	SQLPARSER_DIALECT_SQLSERVER = 3
} sqlparser_dialect_t;

typedef struct {
	sqlparser_status_t code;
	int cursor;
	int line;
	int column;
	char message[256];
} sqlparser_error_t;

typedef struct {
	const char *database_name;
	const char *schema_name;
	const char *table_name;
	const char *alias_name;
} sqlparser_relation_view_t;

typedef struct {
	sqlparser_literal_kind_t kind;
	const char *string_value;
	const char *float_value;
	long long integer_value;
	int boolean_value;
} sqlparser_literal_view_t;

typedef struct {
	sqlparser_literal_kind_t kind;
	const char *string_value;
	const char *float_value;
	long long integer_value;
	int boolean_value;
} sqlparser_literal_value_t;

typedef struct {
	const char *column_name;
	sqlparser_value_kind_t value_kind;
	sqlparser_literal_view_t literal;
} sqlparser_assignment_view_t;

typedef struct {
	const char *table_name;
	const char *column_name;
	const char *operator_name;
	sqlparser_literal_view_t literal;
} sqlparser_where_literal_view_t;

typedef struct {
	const char *owner_type;
	const char *field_name;
	const char *value;
} sqlparser_name_view_t;

typedef struct {
	sqlparser_selector_kind_t kind;
	size_t statement_index;
	size_t item_index;
	size_t row_index;
	size_t column_index;
} sqlparser_selector_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_count;
} sqlparser_view_t;

enum {
	SQLPARSER_STATEMENT_KEYWORD_CAPACITY = 32
};

typedef struct {
	const sqlparser_handle_t *handle;
	size_t index;
	const char *keyword;
	size_t keyword_count;
	const char *keywords[SQLPARSER_STATEMENT_KEYWORD_CAPACITY];
	size_t object_count;
} sqlparser_statement_view_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_index;
	size_t object_index;
	const char *database_name;
	const char *schema_name;
	const char *table_name;
	const char *alias_name;
	sqlparser_selector_t selector;
	int has_selector;
	size_t column_count;
	size_t row_count;
} sqlparser_object_view_t;

typedef struct {
	sqlparser_selector_t selector;
	int has_selector;
	size_t statement_index;
} sqlparser_value_view_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_index;
	size_t object_index;
	size_t column_index;
	const char *name;
	const char *keyword;
	sqlparser_selector_t selector;
	int has_selector;
	const char *operator_name;
	sqlparser_value_view_t value;
	size_t value_count;
} sqlparser_column_view_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_index;
	size_t object_index;
	size_t row_index;
	size_t cell_count;
} sqlparser_row_view_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_index;
	size_t object_index;
	size_t row_index;
	size_t cell_index;
	const char *column_name;
	size_t column_index;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_cell_view_t;

typedef enum {
	SQLPARSER_PATCH_REPLACE = 1,
	SQLPARSER_PATCH_INSERT_COLUMN = 2,
	SQLPARSER_PATCH_DELETE_COLUMN = 3,
	SQLPARSER_PATCH_DELETE_ROW = 4
} sqlparser_patch_op_t;

typedef struct {
	sqlparser_patch_op_t op;
	const char *selector;
	size_t index;
	const char *name;
	const char *sql;
	const char *default_sql;
} sqlparser_patch_t;

typedef struct {
	const sqlparser_patch_t *items;
	size_t count;
} sqlparser_patch_list_t;

typedef struct {
	size_t struct_size;
	size_t max_sql_bytes;
	size_t max_output_bytes;
	size_t max_statement_count;
} sqlparser_limits_t;

typedef struct {
	size_t struct_size;
	sqlparser_dialect_t dialect;
	sqlparser_limits_t limits;
	unsigned int flags;
} sqlparser_parse_options_t;

const char *sqlparser_version_string(void);
const char *sqlparser_libpg_query_tag(void);
const char *sqlparser_statement_kind_name(sqlparser_statement_kind_t kind);
const char *sqlparser_insert_source_kind_name(sqlparser_insert_source_kind_t kind);
const char *sqlparser_value_kind_name(sqlparser_value_kind_t kind);
const char *sqlparser_literal_kind_name(sqlparser_literal_kind_t kind);
const char *sqlparser_selector_kind_name(sqlparser_selector_kind_t kind);
const char *sqlparser_dialect_name(sqlparser_dialect_t dialect);

void sqlparser_limits_default(sqlparser_limits_t *out_limits);
void sqlparser_parse_options_default(sqlparser_parse_options_t *out_options);

sqlparser_status_t sqlparser_parse(
	const char *sql,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_parse_with_limits(
	const char *sql,
	const sqlparser_limits_t *limits,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_parse_with_options(
	const char *sql,
	const sqlparser_parse_options_t *options,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error);

void sqlparser_handle_destroy(sqlparser_handle_t *handle);

const char *sqlparser_original_sql(const sqlparser_handle_t *handle);
sqlparser_dialect_t sqlparser_handle_dialect(const sqlparser_handle_t *handle);
size_t sqlparser_statement_count(const sqlparser_handle_t *handle);

sqlparser_status_t sqlparser_statement_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_statement_kind_t *out_kind,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_node_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char **out_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_target_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_relation_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_set_relation_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_name_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	sqlparser_name_view_t *out_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_set_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_source_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_insert_source_kind_t *out_kind,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_column_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_column_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t column_index,
	const char **out_column_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_row_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_cell_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_assignment_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_assignment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_set_assignment_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_assignment_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_set_assignment_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_where_literal_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_where_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	sqlparser_where_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_where_set_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_literal_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_set_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_parse(
	const char *text,
	sqlparser_selector_t *out_selector,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_format(
	const sqlparser_selector_t *selector,
	char **out_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_relation(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_relation_name(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_name(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_name_view_t *out_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_name(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_literal(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_where_literal(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_where_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_where_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_update_assignment(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_update_assignment_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_update_assignment_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_update_assignment_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_insert_cell_literal(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_insert_cell_literal(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_insert_cell_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_insert_cell_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_get_view(
	const sqlparser_handle_t *handle,
	sqlparser_view_t *out_view,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_view_statement_at(
	const sqlparser_view_t *view,
	size_t statement_index,
	sqlparser_statement_view_t *out_statement,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_keyword_at(
	const sqlparser_statement_view_t *statement,
	size_t keyword_index,
	const char **out_keyword,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_object_at(
	const sqlparser_statement_view_t *statement,
	size_t object_index,
	sqlparser_object_view_t *out_object,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_object_column_at(
	const sqlparser_object_view_t *object,
	size_t column_index,
	sqlparser_column_view_t *out_column,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_column_value_at(
	const sqlparser_column_view_t *column,
	size_t value_index,
	sqlparser_value_view_t *out_value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_object_row_at(
	const sqlparser_object_view_t *object,
	size_t row_index,
	sqlparser_row_view_t *out_row,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_row_cell_at(
	const sqlparser_row_view_t *row,
	size_t cell_index,
	sqlparser_cell_view_t *out_cell,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_value_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_value_view_t *value,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_cell_sql(
	const sqlparser_cell_view_t *cell,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_export_view_json(
	const sqlparser_handle_t *handle,
	int pretty,
	char **out_json,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_apply_patch(
	sqlparser_handle_t *handle,
	const sqlparser_patch_list_t *patches,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_deparse(
	const sqlparser_handle_t *handle,
	char **out_sql,
	sqlparser_error_t *out_error);

void sqlparser_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif
