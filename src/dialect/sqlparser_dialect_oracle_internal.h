#ifndef SQLPARSER_DIALECT_ORACLE_INTERNAL_H
#define SQLPARSER_DIALECT_ORACLE_INTERNAL_H

#include "sqlparser_identifier_origin_internal.h"
#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_dialect_multi_insert_types.h"

/* Matching original source, parser input and unmodified dialect state only. */
sqlparser_status_t sqlparser_oracle_replay_identifier_origins(
	const char *input_sql,
	const char *parser_sql,
	const void *dialect_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

/* A current exact-owner constructor certificate can prove that both outer
 * statement rewrites were identity. A miss leaves the output NULL and the
 * caller on its existing replay path; successful output is independently owned. */
sqlparser_status_t sqlparser_oracle_try_replay_certified_multi_insert_origins(
	const sqlparser_handle_t *handle,
	sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error);

/* Already validated, independently owned surface edits. A miss changes no
 * handle state and leaves owned_sql with the caller for the normal reparse. */
sqlparser_status_t sqlparser_oracle_try_commit_multi_insert_strings(
	sqlparser_handle_t *handle,
	const sqlparser_surface_source_edits_t *edits,
	char **owned_sql,
	int *out_handled,
	sqlparser_error_t *out_error);

/* Issue only after fresh parse, certified commit, or an equivalent clone. */
void sqlparser_oracle_multi_insert_certify_source(sqlparser_handle_t *handle);
void sqlparser_oracle_multi_insert_invalidate_source(sqlparser_handle_t *handle);
int sqlparser_oracle_multi_insert_source_is_current(const sqlparser_handle_t *handle);
/* Use only after the generic source scanner validated all retained branches
 * for this planning pass. A miss leaves its normal scanner path unchanged. */
int sqlparser_oracle_multi_insert_certified_cell_span(
	const sqlparser_handle_t *handle, size_t branch_index, size_t column_index,
	size_t *out_start, size_t *out_end);
void sqlparser_oracle_note_multi_insert_edit(
	sqlparser_handle_t *handle, size_t statement_index,
	size_t branch_index, size_t column_index, size_t start, size_t end);
void sqlparser_oracle_multi_insert_record_outer_identity(
	const sqlparser_dialect_ops_t *outer_owner,
	const sqlparser_dialect_ops_t *base_owner, void *state, int identity);

int sqlparser_oracle_state_has_multi_insert(const void *state);
const sqlparser_dialect_multi_insert_t *sqlparser_oracle_state_multi_insert(const void *state);
const sqlparser_dialect_returning_into_state_t *
sqlparser_oracle_state_returning_into(const void *state);
sqlparser_status_t sqlparser_oracle_render_bind_value(
	const sqlparser_bind_value_t *bind,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_render_literal_value(
	const sqlparser_literal_value_t *value,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_sql_in_place(
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
sqlparser_status_t sqlparser_oracle_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error);

#endif
