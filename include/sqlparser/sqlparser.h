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
	SQLPARSER_BIND_KIND_NONE = 0,
	SQLPARSER_BIND_KIND_POSITIONAL = 1,
	SQLPARSER_BIND_KIND_NAMED = 2
} sqlparser_bind_kind_t;

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
	SQLPARSER_SELECTOR_KIND_VALUE = 9,
	SQLPARSER_SELECTOR_KIND_SELECT_TARGETS = 10,
	SQLPARSER_SELECTOR_KIND_SELECT_TARGET = 11,
	SQLPARSER_SELECTOR_KIND_WHERE = 12,
	SQLPARSER_SELECTOR_KIND_CLAUSE = 13
} sqlparser_selector_kind_t;

typedef enum {
	SQLPARSER_CLAUSE_KIND_UNKNOWN = 0,
	SQLPARSER_CLAUSE_KIND_SELECT_LIST = 1,
	SQLPARSER_CLAUSE_KIND_WHERE = 2,
	SQLPARSER_CLAUSE_KIND_ORDER_BY = 3,
	SQLPARSER_CLAUSE_KIND_SET_LIST = 4,
	SQLPARSER_CLAUSE_KIND_ON = 5,
	SQLPARSER_CLAUSE_KIND_GROUP_BY = 6,
	SQLPARSER_CLAUSE_KIND_HAVING = 7
} sqlparser_clause_kind_t;

typedef enum {
	SQLPARSER_GRAPH_BLOCK_SELECT = 1,
	SQLPARSER_GRAPH_BLOCK_SCALAR_SUBQUERY = 2,
	SQLPARSER_GRAPH_BLOCK_CTE = 3,
	SQLPARSER_GRAPH_BLOCK_SET = 4
} sqlparser_graph_block_kind_t;

typedef enum {
	SQLPARSER_GRAPH_REL_BASE = 1,
	SQLPARSER_GRAPH_REL_DERIVED = 2,
	SQLPARSER_GRAPH_REL_CTE = 3,
	SQLPARSER_GRAPH_REL_DUAL = 4
} sqlparser_graph_relation_kind_t;

typedef enum {
	SQLPARSER_GRAPH_TARGET_FIELD = 1,
	SQLPARSER_GRAPH_TARGET_STAR = 2,
	SQLPARSER_GRAPH_TARGET_QUALIFIED_STAR = 3,
	SQLPARSER_GRAPH_TARGET_LITERAL = 4,
	SQLPARSER_GRAPH_TARGET_PSEUDO = 5,
	SQLPARSER_GRAPH_TARGET_SUBQUERY = 6,
	SQLPARSER_GRAPH_TARGET_EXPRESSION = 7
} sqlparser_graph_target_kind_t;

typedef enum {
	SQLPARSER_GRAPH_VALUE_LITERAL = 1,
	SQLPARSER_GRAPH_VALUE_BIND = 2,
	SQLPARSER_GRAPH_VALUE_DEFAULT = 3,
	SQLPARSER_GRAPH_VALUE_EXPRESSION = 4
} sqlparser_graph_value_kind_t;

typedef enum {
	SQLPARSER_GRAPH_SET_UNION = 1,
	SQLPARSER_GRAPH_SET_UNION_ALL = 2,
	SQLPARSER_GRAPH_SET_INTERSECT = 3,
	SQLPARSER_GRAPH_SET_EXCEPT = 4
} sqlparser_graph_set_kind_t;

typedef enum {
	SQLPARSER_GRAPH_DML_INSERT = 1,
	SQLPARSER_GRAPH_DML_UPDATE = 2,
	SQLPARSER_GRAPH_DML_DELETE = 3,
	SQLPARSER_GRAPH_DML_MERGE = 4
} sqlparser_graph_dml_kind_t;

typedef enum {
	SQLPARSER_DIALECT_POSTGRESQL = 0,
	SQLPARSER_DIALECT_MYSQL = 1,
	SQLPARSER_DIALECT_ORACLE = 2,
	SQLPARSER_DIALECT_SQLSERVER = 3,
	SQLPARSER_DIALECT_DAMENG = 4
} sqlparser_dialect_t;

typedef enum {
	SQLPARSER_BOOL_OPERATOR_AND = 1,
	SQLPARSER_BOOL_OPERATOR_OR = 2
} sqlparser_bool_operator_t;

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
	size_t offset;
	size_t count;
} sqlparser_index_span_t;

enum {
	SQLPARSER_BIND_TEXT_CAPACITY = 128,
	SQLPARSER_BIND_SQL_CAPACITY = 128,
	SQLPARSER_TARGET_PATH_CAPACITY = 16,
	SQLPARSER_TARGET_PATH_NAME_CAPACITY = 128
};

typedef struct {
	const char *kind;
	char name[SQLPARSER_TARGET_PATH_NAME_CAPACITY];
	int has_name;
	int name_truncated;
	size_t arg_index;
} sqlparser_target_path_entry_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_index;
	unsigned long generation;
	size_t root_block_index;
	int has_root_block;
	size_t block_count;
	size_t relation_count;
	size_t target_count;
	size_t field_count;
	size_t value_count;
	size_t set_count;
	int has_dml;
} sqlparser_query_graph_view_t;

typedef struct {
	size_t index;
	size_t statement_index;
	sqlparser_graph_block_kind_t kind;
	sqlparser_index_span_t relations;
	sqlparser_index_span_t targets;
} sqlparser_graph_block_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t block_index;
	sqlparser_graph_relation_kind_t kind;
	const char *database_name;
	const char *schema_name;
	const char *object_name;
	const char *alias_name;
	size_t source_block_index;
	int has_source_block;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_graph_relation_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t block_index;
	size_t ordinal;
	sqlparser_graph_target_kind_t kind;
	const char *output_name;
	size_t field_index;
	int has_field;
	sqlparser_index_span_t star_relations;
	size_t source_block_index;
	int has_source_block;
	sqlparser_selector_t selector;
	int has_selector;
	sqlparser_selector_t target_list_selector;
	int has_target_list_selector;
} sqlparser_graph_target_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t block_index;
	sqlparser_clause_kind_t clause;
	size_t relation_index;
	int has_relation;
	sqlparser_index_span_t candidate_relations;
	const char *column_name;
	size_t target_index;
	int has_target;
	sqlparser_target_path_entry_t target_path[SQLPARSER_TARGET_PATH_CAPACITY];
	size_t target_path_count;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_graph_field_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t block_index;
	sqlparser_clause_kind_t clause;
	const char *operator_name;
	size_t field_index;
	int has_field;
	sqlparser_graph_value_kind_t kind;
	sqlparser_literal_view_t literal;
	char bind[SQLPARSER_BIND_TEXT_CAPACITY];
	int has_bind;
	sqlparser_bind_kind_t bind_kind;
	char bind_sql[SQLPARSER_BIND_SQL_CAPACITY];
	int has_bind_sql;
	size_t bind_position;
	int has_bind_position;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_graph_value_t;

typedef struct {
	size_t index;
	size_t statement_index;
	sqlparser_graph_set_kind_t kind;
	size_t result_block_index;
	sqlparser_index_span_t branch_blocks;
} sqlparser_graph_set_t;

typedef struct {
	size_t index;
	size_t statement_index;
	sqlparser_graph_dml_kind_t kind;
	size_t target_relation_index;
	int has_target_relation;
	sqlparser_index_span_t target_columns;
	sqlparser_index_span_t rows;
	sqlparser_index_span_t assignments;
	sqlparser_index_span_t delete_targets;
	size_t source_block_index;
	int has_source_block;
} sqlparser_graph_dml_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t dml_index;
	size_t ordinal;
	const char *column_name;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_graph_dml_column_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t dml_index;
	size_t row_index;
	size_t column_ordinal;
	sqlparser_graph_value_kind_t kind;
	sqlparser_literal_view_t literal;
	char bind[SQLPARSER_BIND_TEXT_CAPACITY];
	int has_bind;
	sqlparser_bind_kind_t bind_kind;
	char bind_sql[SQLPARSER_BIND_SQL_CAPACITY];
	int has_bind_sql;
	size_t bind_position;
	int has_bind_position;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_graph_dml_cell_t;

typedef struct {
	size_t index;
	size_t statement_index;
	size_t dml_index;
	size_t target_field_index;
	sqlparser_graph_value_kind_t value_kind;
	sqlparser_literal_view_t literal;
	char bind[SQLPARSER_BIND_TEXT_CAPACITY];
	int has_bind;
	sqlparser_bind_kind_t bind_kind;
	char bind_sql[SQLPARSER_BIND_SQL_CAPACITY];
	int has_bind_sql;
	size_t bind_position;
	int has_bind_position;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_graph_dml_assignment_t;

typedef struct {
	const sqlparser_handle_t *handle;
	size_t statement_index;
	size_t clause_index;
	sqlparser_clause_kind_t kind;
	sqlparser_selector_t selector;
	int has_selector;
} sqlparser_clause_view_t;

typedef enum {
	SQLPARSER_PATCH_REPLACE = 1,
	SQLPARSER_PATCH_INSERT_COLUMN = 2,
	SQLPARSER_PATCH_DELETE_COLUMN = 3,
	SQLPARSER_PATCH_DELETE_ROW = 4,
	SQLPARSER_PATCH_APPEND_CONDITION = 5,
	SQLPARSER_PATCH_INSERT_ASSIGNMENT = 6,
	SQLPARSER_PATCH_DELETE_ASSIGNMENT = 7,
	SQLPARSER_PATCH_REPLACE_ASSIGNMENT = 8
} sqlparser_patch_op_t;

typedef struct {
	sqlparser_patch_op_t op;
	const char *selector;
	size_t index;
	const char *name;
	const char *sql;
	const char *default_sql;
	sqlparser_bool_operator_t bool_operator;
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
const char *sqlparser_bind_kind_name(sqlparser_bind_kind_t kind);
const char *sqlparser_literal_kind_name(sqlparser_literal_kind_t kind);
const char *sqlparser_selector_kind_name(sqlparser_selector_kind_t kind);
const char *sqlparser_dialect_name(sqlparser_dialect_t dialect);
const char *sqlparser_bool_operator_name(sqlparser_bool_operator_t bool_operator);
const char *sqlparser_clause_kind_name(sqlparser_clause_kind_t kind);
const char *sqlparser_graph_block_kind_name(sqlparser_graph_block_kind_t kind);
const char *sqlparser_graph_relation_kind_name(sqlparser_graph_relation_kind_t kind);
const char *sqlparser_graph_target_kind_name(sqlparser_graph_target_kind_t kind);
const char *sqlparser_graph_value_kind_name(sqlparser_graph_value_kind_t kind);
const char *sqlparser_graph_set_kind_name(sqlparser_graph_set_kind_t kind);
const char *sqlparser_graph_dml_kind_name(sqlparser_graph_dml_kind_t kind);

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

sqlparser_status_t sqlparser_select_target_list_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_select_target_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_select_target_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_select_set_target_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_select_set_targets_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_select_insert_target_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_select_delete_target(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
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

sqlparser_status_t sqlparser_update_insert_assignment_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *assignment_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_delete_assignment(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_update_set_assignment_full_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *assignment_sql,
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

sqlparser_status_t sqlparser_statement_where_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_clause_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_clause(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_clause_view_t *out_clause,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_clause_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_set_clause_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_append_clause_condition(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_where_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_set_where_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_append_where_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
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

sqlparser_status_t sqlparser_selector_where_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_where_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_append_where_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_clause(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_clause_view_t *out_clause,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_clause_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_clause_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_append_clause_condition(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
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

sqlparser_status_t sqlparser_selector_insert_update_assignment_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_delete_update_assignment(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_update_assignment_full_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
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

sqlparser_status_t sqlparser_selector_select_target_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_select_target_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_selector_set_select_targets_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_statement_query_graph(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_query_graph_view_t *out_graph,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_span_index_at(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_index_span_t span,
	size_t item_index,
	size_t *out_index,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_block_at(
	const sqlparser_query_graph_view_t *graph,
	size_t block_index,
	sqlparser_graph_block_t *out_block,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_relation_at(
	const sqlparser_query_graph_view_t *graph,
	size_t relation_index,
	sqlparser_graph_relation_t *out_relation,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_target_at(
	const sqlparser_query_graph_view_t *graph,
	size_t target_index,
	sqlparser_graph_target_t *out_target,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_field_at(
	const sqlparser_query_graph_view_t *graph,
	size_t field_index,
	sqlparser_graph_field_t *out_field,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_value_at(
	const sqlparser_query_graph_view_t *graph,
	size_t value_index,
	sqlparser_graph_value_t *out_value,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_set_at(
	const sqlparser_query_graph_view_t *graph,
	size_t set_index,
	sqlparser_graph_set_t *out_set,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_dml(
	const sqlparser_query_graph_view_t *graph,
	sqlparser_graph_dml_t *out_dml,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_dml_column_at(
	const sqlparser_query_graph_view_t *graph,
	size_t column_index,
	sqlparser_graph_dml_column_t *out_column,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_dml_cell_at(
	const sqlparser_query_graph_view_t *graph,
	size_t cell_index,
	sqlparser_graph_dml_cell_t *out_cell,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_query_graph_dml_assignment_at(
	const sqlparser_query_graph_view_t *graph,
	size_t assignment_index,
	sqlparser_graph_dml_assignment_t *out_assignment,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_clause_sql(
	const sqlparser_clause_view_t *clause,
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
