#ifndef SQLPARSER_INTERNAL_H
#define SQLPARSER_INTERNAL_H

#include <stddef.h>

#include <jansson.h>

#include "pg_query.h"
#include "protobuf/pg_query.pb-c.h"
#include "sqlparser/sqlparser.h"

typedef struct sqlparser_dialect_ops sqlparser_dialect_ops_t;

#define SQLPARSER_INTERNAL_CURRENT_DATABASE "sqlparser_current_database"
#define SQLPARSER_INTERNAL_CURRENT_SCHEMA "sqlparser_current_schema"

struct sqlparser_handle {
	char *sql;
	char *parser_sql;
	char *current_sql;
	char *current_parser_sql;
	size_t sql_len;
	size_t parser_sql_len;
	size_t statement_count;
	PgQueryProtobuf parse_tree;
	PgQuery__ParseResult *ast;
	sqlparser_limits_t limits;
	unsigned long generation;
	sqlparser_dialect_t dialect;
	const sqlparser_dialect_ops_t *dialect_ops;
	void *dialect_state;
};

void sqlparser_error_clear(sqlparser_error_t *out_error);
void sqlparser_error_set_message(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *message);
void sqlparser_error_from_pg(
	sqlparser_error_t *out_error,
	sqlparser_status_t code,
	const char *sql,
	const PgQueryError *error);

char *sqlparser_strdup(const char *text);
char *sqlparser_strndup(const char *text, size_t len);
char *sqlparser_strndup_lower_ascii(const char *text, size_t len);
void sqlparser_pg_query_prepare(void);
void sqlparser_limits_normalize(
	const sqlparser_limits_t *limits,
	sqlparser_limits_t *out_limits);
sqlparser_status_t sqlparser_validate_text_limit(
	const char *text,
	size_t max_bytes,
	const char *field_name,
	size_t *out_len,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_validate_handle_sql_input(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_validate_handle_output_text(
	const sqlparser_handle_t *handle,
	const char *text,
	const char *field_name,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_handle_ensure_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
void sqlparser_handle_clear_ast(sqlparser_handle_t *handle);
void sqlparser_handle_invalidate_derived(sqlparser_handle_t *handle);
sqlparser_status_t sqlparser_handle_commit_ast(
	sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_handle_clone(
	const sqlparser_handle_t *source,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error);
void sqlparser_handle_replace_contents(
	sqlparser_handle_t *target,
	sqlparser_handle_t *source);
sqlparser_status_t sqlparser_ensure_current_sql_text(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error);
const char *sqlparser_effective_sql(const sqlparser_handle_t *handle);
const char *sqlparser_effective_parser_sql(const sqlparser_handle_t *handle);
sqlparser_status_t sqlparser_postprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	const char *core_sql,
	const char *field_name,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_preprocess_handle_sql_fragment(
	const sqlparser_handle_t *handle,
	const char *public_sql,
	const char *field_name,
	char **out_parser_sql,
	void **out_dialect_state,
	sqlparser_error_t *out_error);
void sqlparser_handle_discard_dialect_state(
	const sqlparser_handle_t *handle,
	void *state);
void sqlparser_handle_adopt_dialect_state(
	sqlparser_handle_t *handle,
	void *state);

int sqlparser_json_array_contains_string(json_t *array, const char *value);
void sqlparser_json_object_set_nonempty_string(
	json_t *object,
	const char *key,
	const char *value);
sqlparser_status_t sqlparser_json_object_set_string(
	json_t *object,
	const char *key,
	const char *value,
	sqlparser_error_t *out_error);
void sqlparser_json_array_append_table(
	json_t *array,
	const char *schema_name,
	const char *table_name,
	const char *context);

#endif
