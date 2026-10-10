#ifndef SQLPARSER_DIALECT_ORACLE_INTERNAL_H
#define SQLPARSER_DIALECT_ORACLE_INTERNAL_H

#include "sqlparser_identifier_origin_internal.h"
#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_dialect_multi_insert_types.h"

/* The caller validates index < cell_count. These reads neither allocate nor
 * expand compact storage; tag zero also covers ordinary Dameng branches. */
static inline int sqlparser_oracle_branch_has_cells(
	const sqlparser_dialect_multi_insert_branch_t *branch)
{
	return branch->cell_storage == SQLPARSER_ORACLE_CELL_STORAGE_COMPACT ?
		branch->oracle_compact_cells != NULL : branch->cells != NULL;
}

static inline const char *sqlparser_oracle_cell_public_sql(
	const sqlparser_dialect_multi_insert_branch_t *branch, size_t index)
{
	return branch->cell_storage == SQLPARSER_ORACLE_CELL_STORAGE_COMPACT ?
		branch->oracle_compact_cells[index].public_sql : branch->cells[index].public_sql;
}

static inline const char *sqlparser_oracle_cell_parser_sql(
	const sqlparser_dialect_multi_insert_branch_t *branch, size_t index)
{
	return branch->cell_storage == SQLPARSER_ORACLE_CELL_STORAGE_COMPACT ?
		branch->oracle_compact_cells[index].parser_sql : branch->cells[index].parser_sql;
}

static inline int sqlparser_oracle_cell_has_literal(
	const sqlparser_dialect_multi_insert_branch_t *branch, size_t index)
{
	return branch->cell_storage == SQLPARSER_ORACLE_CELL_STORAGE_COMPACT ?
		branch->oracle_compact_cells[index].has_literal : branch->cells[index].has_literal;
}

static inline const sqlparser_literal_view_t *sqlparser_oracle_cell_literal(
	const sqlparser_dialect_multi_insert_branch_t *branch, size_t index)
{
	return branch->cell_storage == SQLPARSER_ORACLE_CELL_STORAGE_COMPACT ?
		&branch->oracle_compact_cells[index].literal : &branch->cells[index].literal;
}

static inline const sqlparser_dialect_multi_insert_value_t *sqlparser_oracle_cell_legacy(
	const sqlparser_dialect_multi_insert_branch_t *branch, size_t index)
{
	return branch->cell_storage == SQLPARSER_ORACLE_CELL_STORAGE_COMPACT ?
		NULL : &branch->cells[index];
}

/* Only exact registered native/compatible owners at a fresh core entry grant
 * these constructor capabilities. Ordinary ops and cold reparses stay legacy. */
sqlparser_status_t sqlparser_oracle_preprocess_compact_initial(
	const char *input_sql, const sqlparser_limits_t *limits,
	char **out_parser_sql, void **out_state, sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_kingbase_oracle_preprocess_compact_initial(
	const char *input_sql, const sqlparser_limits_t *limits,
	char **out_parser_sql, void **out_state, sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_oracle_parse_legacy_replacement(
	const char *sql, const sqlparser_parse_options_t *options,
	sqlparser_handle_t **out_handle, sqlparser_error_t *out_error);

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

/* Request-local only: caller inputs and retained owners remain immutable until
 * all source edits and the complete replacement SQL independently own bytes.
 * IDs are optional scratch; the normal commit keeps both certification passes. */
typedef struct {
	const sqlparser_handle_t *handle;
	const sqlparser_dialect_ops_t *owner;
	void *state;
	sqlparser_dialect_multi_insert_t *multi;
	const char *sql;
	const char *parser_sql;
	const char *wire;
	size_t sql_length;
	size_t parser_sql_length;
	size_t wire_length;
	unsigned long generation;
	const sqlparser_query_graph_cache_t *graph;
	uint32_t *ids;
	size_t count;
	size_t capacity;
	int disabled;
	int admitted;
} sqlparser_oracle_readonly_batch_t;

int sqlparser_oracle_readonly_batch_begin(
	const sqlparser_handle_t *handle,
	sqlparser_oracle_readonly_batch_t *batch);
int sqlparser_oracle_readonly_batch_target(
	const sqlparser_oracle_readonly_batch_t *batch,
	size_t statement_index, size_t branch_index, size_t column_index);
void sqlparser_oracle_readonly_batch_note(
	sqlparser_oracle_readonly_batch_t *batch,
	size_t statement_index, size_t branch_index, size_t column_index,
	size_t source_start, size_t source_end);
sqlparser_status_t sqlparser_oracle_readonly_batch_commit(
	sqlparser_handle_t *handle,
	sqlparser_oracle_readonly_batch_t *batch,
	const sqlparser_surface_source_edits_t *edits,
	char **owned_sql, int *out_handled,
	sqlparser_error_t *out_error);
void sqlparser_oracle_readonly_batch_release(
	sqlparser_oracle_readonly_batch_t *batch);

/* Issue only after fresh parse, certified commit, or an equivalent clone. */
void sqlparser_oracle_multi_insert_certify_source(sqlparser_handle_t *handle);
void sqlparser_oracle_multi_insert_invalidate_source(sqlparser_handle_t *handle);
/* Replacing a handle via reparse is a mutation, even when its new constructor
 * could issue a fresh proof. Preserve ordinary provenance and drop only this. */
void sqlparser_oracle_multi_insert_discard_initial_span_proof(sqlparser_handle_t *handle);
void sqlparser_oracle_multi_insert_discard_graph_header_proof(sqlparser_handle_t *handle);
int sqlparser_oracle_multi_insert_source_is_current(const sqlparser_handle_t *handle);
/* Once per graph build: only exact-owner, current fresh-parse headers can
 * replace the ordinary relation and column identifier classification. */
int sqlparser_oracle_multi_insert_graph_headers_simple(
	const sqlparser_handle_t *handle, const sqlparser_dialect_multi_insert_t *multi);
/* A separate complete lexical proof can replace the first generic scan.
 * A miss changes no outputs; callers retain the final delimiter/comment checks. */
int sqlparser_oracle_multi_insert_initial_cell_span(
	const sqlparser_handle_t *handle, size_t branch_index, size_t column_index,
	size_t *statement_start, size_t *statement_end, size_t *values_position,
	size_t *out_start, size_t *out_end);
/* Use after the generic source scanner validated all retained branches for
 * this planning pass, or inside the complete constructor-proof accessor above.
 * A miss leaves its normal scanner path unchanged. */
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
