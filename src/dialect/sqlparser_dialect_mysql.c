#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef enum {
	SQLPARSER_MYSQL_ORIGIN_INPUT = 1,
	SQLPARSER_MYSQL_ORIGIN_SOURCE_IDENTIFIER = 2,
	SQLPARSER_MYSQL_ORIGIN_GENERATED_IDENTIFIER = 3
} sqlparser_mysql_origin_kind_t;

typedef struct {
	sqlparser_mysql_origin_kind_t kind;
	size_t output_offset;
	size_t output_length;
	size_t input_offset;
	size_t input_length;
} sqlparser_mysql_origin_run_t;

typedef struct {
	sqlparser_mysql_origin_run_t *runs;
	size_t run_count;
	size_t run_capacity;
} sqlparser_mysql_origin_trace_t;

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
	const char *origin_input;
	size_t origin_input_length;
	size_t origin_input_base;
	sqlparser_mysql_origin_trace_t *origin_trace;
} sqlparser_mysql_buffer_t;

typedef enum {
	SQLPARSER_MYSQL_DML_MODIFIER_LOW_PRIORITY = 1U << 0,
	SQLPARSER_MYSQL_DML_MODIFIER_DELAYED = 1U << 1,
	SQLPARSER_MYSQL_DML_MODIFIER_HIGH_PRIORITY = 1U << 2,
	SQLPARSER_MYSQL_DML_MODIFIER_IGNORE = 1U << 3,
	SQLPARSER_MYSQL_DML_MODIFIER_QUICK = 1U << 4
} sqlparser_mysql_dml_modifier_flag_t;

typedef enum {
	SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT = 1,
	SQLPARSER_MYSQL_DML_MODIFIER_KIND_UPDATE = 2,
	SQLPARSER_MYSQL_DML_MODIFIER_KIND_DELETE = 3,
	SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE = 4
} sqlparser_mysql_dml_modifier_kind_t;

typedef struct {
	size_t statement_index;
	sqlparser_mysql_dml_modifier_kind_t kind;
	unsigned int flags;
	sqlparser_graph_insert_mode_t insert_mode_override;
} sqlparser_mysql_dml_modifier_t;

typedef struct {
	size_t statement_index;
	size_t column_ordinal;
	char *segment_sql;
} sqlparser_mysql_create_column_restore_t;

typedef struct {
	size_t statement_index;
	char *options_sql;
} sqlparser_mysql_create_table_restore_t;

typedef enum {
	SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_NONE = 0,
	SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_VALUES = 1,
	SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_ALIAS = 2
} sqlparser_mysql_on_duplicate_restore_kind_t;

typedef struct {
	size_t statement_index;
	sqlparser_mysql_on_duplicate_restore_kind_t kind;
	char *alias_name;
	char *alias_columns_sql;
} sqlparser_mysql_on_duplicate_restore_t;

typedef struct {
	size_t statement_index;
	size_t relation_index;
	int relation_location;
	char *fragment_sql;
} sqlparser_mysql_index_hint_t;

typedef struct {
	size_t statement_index;
	size_t relation_index;
	int relation_location;
	char *fragment_sql;
} sqlparser_mysql_partition_restore_t;

typedef struct {
	size_t statement_index;
	size_t join_index;
	char *keyword_sql;
} sqlparser_mysql_join_restore_t;

typedef struct {
	size_t statement_index;
	char *order_by_sql;
	char *limit_sql;
} sqlparser_mysql_dml_tail_t;

typedef struct {
	size_t positional_param_count;
	char **national_literals;
	size_t *national_literal_ordinals;
	size_t national_literal_count;
	size_t national_literal_capacity;
	size_t national_literal_ordinal_capacity;
	size_t literal_count;
	sqlparser_mysql_dml_modifier_t *dml_modifiers;
	size_t dml_modifier_count;
	size_t dml_modifier_capacity;
	sqlparser_mysql_create_column_restore_t *create_column_restores;
	size_t create_column_restore_count;
	size_t create_column_restore_capacity;
	sqlparser_mysql_create_table_restore_t *create_table_restores;
	size_t create_table_restore_count;
	size_t create_table_restore_capacity;
	sqlparser_mysql_on_duplicate_restore_t *on_duplicate_restores;
	size_t on_duplicate_restore_count;
	size_t on_duplicate_restore_capacity;
	sqlparser_mysql_index_hint_t *index_hints;
	size_t index_hint_count;
	size_t index_hint_capacity;
	sqlparser_mysql_partition_restore_t *partition_restores;
	size_t partition_restore_count;
	size_t partition_restore_capacity;
	sqlparser_mysql_join_restore_t *join_restores;
	size_t join_restore_count;
	size_t join_restore_capacity;
	sqlparser_mysql_dml_tail_t *dml_tails;
	size_t dml_tail_count;
	size_t dml_tail_capacity;
	size_t *lock_in_share_statements;
	size_t lock_in_share_count;
	size_t lock_in_share_capacity;
} sqlparser_mysql_state_t;

typedef enum {
	SQLPARSER_MYSQL_JOIN_INNER = 0,
	SQLPARSER_MYSQL_JOIN_LEFT,
	SQLPARSER_MYSQL_JOIN_RIGHT,
	SQLPARSER_MYSQL_JOIN_CROSS
} sqlparser_mysql_join_kind_t;

static sqlparser_status_t sqlparser_mysql_rewrite_pg_quotes_to_backticks(
	const char *sql,
	const sqlparser_mysql_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_mysql_state_new(
	sqlparser_mysql_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;

	state = (sqlparser_mysql_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_mysql_state_destroy(void *state)
{
	sqlparser_mysql_state_t *mysql_state;
	size_t index;

	mysql_state = (sqlparser_mysql_state_t *)state;
	if (mysql_state == NULL) {
		return;
	}

	for (index = 0U; index < mysql_state->national_literal_count; index++) {
		free(mysql_state->national_literals[index]);
	}
	for (index = 0U; index < mysql_state->create_column_restore_count; index++) {
		free(mysql_state->create_column_restores[index].segment_sql);
	}
	for (index = 0U; index < mysql_state->create_table_restore_count; index++) {
		free(mysql_state->create_table_restores[index].options_sql);
	}
	for (index = 0U; index < mysql_state->on_duplicate_restore_count; index++) {
		free(mysql_state->on_duplicate_restores[index].alias_name);
		free(mysql_state->on_duplicate_restores[index].alias_columns_sql);
	}
	for (index = 0U; index < mysql_state->index_hint_count; index++) {
		free(mysql_state->index_hints[index].fragment_sql);
	}
	for (index = 0U; index < mysql_state->partition_restore_count; index++) {
		free(mysql_state->partition_restores[index].fragment_sql);
	}
	for (index = 0U; index < mysql_state->join_restore_count; index++) {
		free(mysql_state->join_restores[index].keyword_sql);
	}
	for (index = 0U; index < mysql_state->dml_tail_count; index++) {
		free(mysql_state->dml_tails[index].order_by_sql);
		free(mysql_state->dml_tails[index].limit_sql);
	}
	free(mysql_state->national_literals);
	free(mysql_state->national_literal_ordinals);
	free(mysql_state->dml_modifiers);
	free(mysql_state->create_column_restores);
	free(mysql_state->create_table_restores);
	free(mysql_state->on_duplicate_restores);
	free(mysql_state->index_hints);
	free(mysql_state->partition_restores);
	free(mysql_state->join_restores);
	free(mysql_state->dml_tails);
	free(mysql_state->lock_in_share_statements);
	free(mysql_state);
}

static sqlparser_status_t sqlparser_mysql_size_array_reserve(
	size_t **items,
	size_t *capacity,
	size_t required,
	sqlparser_error_t *out_error)
{
	size_t *next;
	size_t next_capacity;

	if (items == NULL || capacity == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "array arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (required <= *capacity) {
		return SQLPARSER_STATUS_OK;
	}
	next_capacity = *capacity == 0U ? 4U : *capacity;
	while (next_capacity < required) {
		if (next_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}
	if (next_capacity > ((size_t)-1) / sizeof(*next)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	next = (size_t *)realloc(*items, next_capacity * sizeof(*next));
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*items = next;
	*capacity = next_capacity;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_store_national_literal(
	sqlparser_mysql_state_t *state,
	const char *literal,
	size_t len,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	char **next;
	char *copy;
	size_t next_capacity;
	sqlparser_status_t status;

	if (state == NULL || literal == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "national literal arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_mysql_size_array_reserve(
		&state->national_literal_ordinals,
		&state->national_literal_ordinal_capacity,
		state->national_literal_count + 1U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (state->national_literal_count == state->national_literal_capacity) {
		next_capacity = state->national_literal_capacity == 0U ? 4U : state->national_literal_capacity * 2U;
		if (next_capacity < state->national_literal_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*next)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(state->national_literals, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->national_literals = next;
		state->national_literal_capacity = next_capacity;
	}

	copy = sqlparser_strndup(literal, len);
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->national_literals[state->national_literal_count] = copy;
	state->national_literal_ordinals[state->national_literal_count] = ordinal;
	state->national_literal_count++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_national_literal_matches(
	const sqlparser_mysql_state_t *state,
	size_t ordinal,
	const char *sql,
	size_t start,
	size_t end)
{
	size_t index;
	size_t len;

	if (state == NULL || sql == NULL || end <= start) {
		return 0;
	}
	len = end - start;
	for (index = 0U; index < state->national_literal_count; index++) {
		if (state->national_literal_ordinals[index] == ordinal &&
		    strlen(state->national_literals[index]) == len &&
		    strncmp(state->national_literals[index], sql + start, len) == 0) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_mysql_state_add_dml_modifier(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	sqlparser_mysql_dml_modifier_kind_t kind,
	unsigned int flags,
	sqlparser_graph_insert_mode_t insert_mode_override,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_dml_modifier_t *next;
	size_t next_capacity;

	if (state == NULL ||
	    (flags == 0U && insert_mode_override == SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN)) {
		return SQLPARSER_STATUS_OK;
	}
	if (state->dml_modifier_count == state->dml_modifier_capacity) {
		next_capacity = state->dml_modifier_capacity == 0U ? 4U : state->dml_modifier_capacity * 2U;
		if (next_capacity < state->dml_modifier_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->dml_modifiers)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_dml_modifier_t *)realloc(
			state->dml_modifiers,
			next_capacity * sizeof(*state->dml_modifiers));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->dml_modifiers = next;
		state->dml_modifier_capacity = next_capacity;
	}

	state->dml_modifiers[state->dml_modifier_count].statement_index = statement_index;
	state->dml_modifiers[state->dml_modifier_count].kind = kind;
	state->dml_modifiers[state->dml_modifier_count].flags = flags;
	state->dml_modifiers[state->dml_modifier_count].insert_mode_override = insert_mode_override;
	state->dml_modifier_count++;
	return SQLPARSER_STATUS_OK;
}

static unsigned int sqlparser_mysql_state_dml_modifier_flags(
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	sqlparser_mysql_dml_modifier_kind_t kind)
{
	size_t index;

	if (state == NULL) {
		return 0U;
	}
	for (index = 0U; index < state->dml_modifier_count; index++) {
		if (state->dml_modifiers[index].statement_index == statement_index &&
		    state->dml_modifiers[index].kind == kind) {
			return state->dml_modifiers[index].flags;
		}
	}
	return 0U;
}

static sqlparser_status_t sqlparser_mysql_state_add_create_column_restore(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t column_ordinal,
	const char *segment_start,
	size_t segment_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_create_column_restore_t *next;
	char *segment_sql;
	size_t next_capacity;

	if (state == NULL || segment_start == NULL || segment_len == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	segment_sql = sqlparser_strndup(segment_start, segment_len);
	if (segment_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	if (state->create_column_restore_count == state->create_column_restore_capacity) {
		next_capacity = state->create_column_restore_capacity == 0U ?
			4U :
			state->create_column_restore_capacity * 2U;
		if (next_capacity < state->create_column_restore_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->create_column_restores)) {
			free(segment_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_create_column_restore_t *)realloc(
			state->create_column_restores,
			next_capacity * sizeof(*state->create_column_restores));
		if (next == NULL) {
			free(segment_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->create_column_restores = next;
		state->create_column_restore_capacity = next_capacity;
	}

	state->create_column_restores[state->create_column_restore_count].statement_index = statement_index;
	state->create_column_restores[state->create_column_restore_count].column_ordinal = column_ordinal;
	state->create_column_restores[state->create_column_restore_count].segment_sql = segment_sql;
	state->create_column_restore_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_state_add_create_table_restore(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	const char *options_start,
	size_t options_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_create_table_restore_t *next;
	char *options_sql;
	size_t next_capacity;

	if (state == NULL || options_start == NULL || options_len == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	options_sql = sqlparser_strndup(options_start, options_len);
	if (options_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	if (state->create_table_restore_count == state->create_table_restore_capacity) {
		next_capacity = state->create_table_restore_capacity == 0U ?
			4U :
			state->create_table_restore_capacity * 2U;
		if (next_capacity < state->create_table_restore_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->create_table_restores)) {
			free(options_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_create_table_restore_t *)realloc(
			state->create_table_restores,
			next_capacity * sizeof(*state->create_table_restores));
		if (next == NULL) {
			free(options_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->create_table_restores = next;
		state->create_table_restore_capacity = next_capacity;
	}

	state->create_table_restores[state->create_table_restore_count].statement_index = statement_index;
	state->create_table_restores[state->create_table_restore_count].options_sql = options_sql;
	state->create_table_restore_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_state_add_on_duplicate_restore(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	sqlparser_mysql_on_duplicate_restore_kind_t kind,
	const char *alias_start,
	size_t alias_len,
	const char *alias_columns_start,
	size_t alias_columns_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_on_duplicate_restore_t *next;
	char *alias_name;
	char *alias_columns_sql;
	size_t next_capacity;

	if (state == NULL || kind == SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_NONE) {
		return SQLPARSER_STATUS_OK;
	}
	alias_name = NULL;
	alias_columns_sql = NULL;
	if (alias_start != NULL && alias_len > 0U) {
		alias_name = sqlparser_strndup(alias_start, alias_len);
		if (alias_name == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (alias_columns_start != NULL && alias_columns_len > 0U) {
		alias_columns_sql = sqlparser_strndup(alias_columns_start, alias_columns_len);
		if (alias_columns_sql == NULL) {
			free(alias_name);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (state->on_duplicate_restore_count == state->on_duplicate_restore_capacity) {
		next_capacity = state->on_duplicate_restore_capacity == 0U ?
			4U :
			state->on_duplicate_restore_capacity * 2U;
		if (next_capacity < state->on_duplicate_restore_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->on_duplicate_restores)) {
			free(alias_name);
			free(alias_columns_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_on_duplicate_restore_t *)realloc(
			state->on_duplicate_restores,
			next_capacity * sizeof(*state->on_duplicate_restores));
		if (next == NULL) {
			free(alias_name);
			free(alias_columns_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->on_duplicate_restores = next;
		state->on_duplicate_restore_capacity = next_capacity;
	}
	state->on_duplicate_restores[state->on_duplicate_restore_count].statement_index = statement_index;
	state->on_duplicate_restores[state->on_duplicate_restore_count].kind = kind;
	state->on_duplicate_restores[state->on_duplicate_restore_count].alias_name = alias_name;
	state->on_duplicate_restores[state->on_duplicate_restore_count].alias_columns_sql = alias_columns_sql;
	state->on_duplicate_restore_count++;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_mysql_on_duplicate_restore_t *sqlparser_mysql_state_on_duplicate_restore(
	const sqlparser_mysql_state_t *state,
	size_t statement_index)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->on_duplicate_restore_count; index++) {
		if (state->on_duplicate_restores[index].statement_index == statement_index) {
			return &state->on_duplicate_restores[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_mysql_public_fragment_dup(
	const sqlparser_mysql_state_t *state,
	const char *start,
	size_t len,
	char **out_fragment,
	sqlparser_error_t *out_error)
{
	char *parser_fragment;
	sqlparser_status_t status;

	if (out_fragment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_fragment = NULL;
	if (start == NULL || len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	parser_fragment = sqlparser_strndup(start, len);
	if (parser_fragment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_mysql_rewrite_pg_quotes_to_backticks(
		parser_fragment,
		state,
		out_fragment,
		out_error);
	free(parser_fragment);
	return status;
}

static sqlparser_status_t sqlparser_mysql_state_add_index_hint(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t relation_index,
	int relation_location,
	const char *fragment_start,
	size_t fragment_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_index_hint_t *next;
	sqlparser_mysql_index_hint_t *hint;
	char *fragment_sql;
	size_t next_capacity;
	sqlparser_status_t status;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	fragment_sql = NULL;
	status = sqlparser_mysql_public_fragment_dup(
		state,
		fragment_start,
		fragment_len,
		&fragment_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(fragment_sql);
		return status;
	}
	if (state->index_hint_count == state->index_hint_capacity) {
		next_capacity = state->index_hint_capacity == 0U ? 4U : state->index_hint_capacity * 2U;
		if (next_capacity < state->index_hint_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->index_hints)) {
			free(fragment_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_index_hint_t *)realloc(
			state->index_hints,
			next_capacity * sizeof(*state->index_hints));
		if (next == NULL) {
			free(fragment_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->index_hints = next;
		state->index_hint_capacity = next_capacity;
	}
	hint = &state->index_hints[state->index_hint_count];
	hint->statement_index = statement_index;
	hint->relation_index = relation_index;
	hint->relation_location = relation_location;
	hint->fragment_sql = fragment_sql;
	state->index_hint_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_state_add_partition_restore(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t relation_index,
	int relation_location,
	const char *fragment_start,
	size_t fragment_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_partition_restore_t *next;
	sqlparser_mysql_partition_restore_t *restore;
	char *fragment_sql;
	size_t next_capacity;
	sqlparser_status_t status;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	fragment_sql = NULL;
	status = sqlparser_mysql_public_fragment_dup(
		state,
		fragment_start,
		fragment_len,
		&fragment_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (fragment_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "partition fragment is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (state->partition_restore_count == state->partition_restore_capacity) {
		next_capacity = state->partition_restore_capacity == 0U ?
			4U : state->partition_restore_capacity * 2U;
		if (next_capacity < state->partition_restore_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->partition_restores)) {
			free(fragment_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_partition_restore_t *)realloc(
			state->partition_restores,
			next_capacity * sizeof(*state->partition_restores));
		if (next == NULL) {
			free(fragment_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->partition_restores = next;
		state->partition_restore_capacity = next_capacity;
	}
	restore = &state->partition_restores[state->partition_restore_count++];
	restore->statement_index = statement_index;
	restore->relation_index = relation_index;
	restore->relation_location = relation_location;
	restore->fragment_sql = fragment_sql;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_mysql_adjust_hint_locations_after_removal(
	sqlparser_mysql_state_t *state,
	size_t removal_start,
	size_t removal_len)
{
	size_t index;

	if (state == NULL || removal_len == 0U) {
		return;
	}
	for (index = 0U; index < state->index_hint_count; index++) {
		int location;

		location = state->index_hints[index].relation_location;
		if (location >= 0 && (size_t)location > removal_start) {
			state->index_hints[index].relation_location =
				(size_t)location >= removal_len ? (int)((size_t)location - removal_len) : -1;
		}
	}
}

static sqlparser_status_t sqlparser_mysql_state_add_join_restore(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t join_index,
	const char *keyword_start,
	size_t keyword_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_join_restore_t *next;
	char *keyword_sql;
	size_t next_capacity;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	keyword_sql = keyword_start != NULL && keyword_len > 0U ?
		sqlparser_strndup(keyword_start, keyword_len) :
		NULL;
	if (keyword_start != NULL && keyword_len > 0U && keyword_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (state->join_restore_count == state->join_restore_capacity) {
		next_capacity = state->join_restore_capacity == 0U ? 4U : state->join_restore_capacity * 2U;
		if (next_capacity < state->join_restore_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->join_restores)) {
			free(keyword_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_join_restore_t *)realloc(
			state->join_restores,
			next_capacity * sizeof(*state->join_restores));
		if (next == NULL) {
			free(keyword_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->join_restores = next;
		state->join_restore_capacity = next_capacity;
	}
	state->join_restores[state->join_restore_count].statement_index = statement_index;
	state->join_restores[state->join_restore_count].join_index = join_index;
	state->join_restores[state->join_restore_count].keyword_sql = keyword_sql;
	state->join_restore_count++;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_mysql_join_restore_t *sqlparser_mysql_state_join_restore(
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t join_index)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->join_restore_count; index++) {
		if (state->join_restores[index].statement_index == statement_index &&
		    state->join_restores[index].join_index == join_index) {
			return &state->join_restores[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_mysql_state_add_dml_tail(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	const char *order_start,
	size_t order_len,
	const char *limit_start,
	size_t limit_len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_dml_tail_t *next;
	char *order_by_sql;
	char *limit_sql;
	char *raw_sql;
	size_t next_capacity;
	sqlparser_status_t status;

	if (state == NULL || (order_len == 0U && limit_len == 0U)) {
		return SQLPARSER_STATUS_OK;
	}
	order_by_sql = NULL;
	limit_sql = NULL;
	raw_sql = order_start != NULL && order_len > 0U ? sqlparser_strndup(order_start, order_len) : NULL;
	if (order_start != NULL && order_len > 0U && raw_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = raw_sql != NULL ?
		sqlparser_mysql_rewrite_pg_quotes_to_backticks(raw_sql, state, &order_by_sql, out_error) :
		SQLPARSER_STATUS_OK;
	free(raw_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	raw_sql = limit_start != NULL && limit_len > 0U ? sqlparser_strndup(limit_start, limit_len) : NULL;
	if (limit_start != NULL && limit_len > 0U && raw_sql == NULL) {
		free(order_by_sql);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = raw_sql != NULL ?
		sqlparser_mysql_rewrite_pg_quotes_to_backticks(raw_sql, state, &limit_sql, out_error) :
		SQLPARSER_STATUS_OK;
	free(raw_sql);
	if (status != SQLPARSER_STATUS_OK) {
		free(order_by_sql);
		free(limit_sql);
		return status;
	}
	if (state->dml_tail_count == state->dml_tail_capacity) {
		next_capacity = state->dml_tail_capacity == 0U ? 4U : state->dml_tail_capacity * 2U;
		if (next_capacity < state->dml_tail_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->dml_tails)) {
			free(order_by_sql);
			free(limit_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_mysql_dml_tail_t *)realloc(
			state->dml_tails,
			next_capacity * sizeof(*state->dml_tails));
		if (next == NULL) {
			free(order_by_sql);
			free(limit_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->dml_tails = next;
		state->dml_tail_capacity = next_capacity;
	}
	state->dml_tails[state->dml_tail_count].statement_index = statement_index;
	state->dml_tails[state->dml_tail_count].order_by_sql = order_by_sql;
	state->dml_tails[state->dml_tail_count].limit_sql = limit_sql;
	state->dml_tail_count++;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_mysql_dml_tail_t *sqlparser_mysql_state_dml_tail(
	const sqlparser_mysql_state_t *state,
	size_t statement_index)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->dml_tail_count; index++) {
		if (state->dml_tails[index].statement_index == statement_index) {
			return &state->dml_tails[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_mysql_state_add_lock_in_share(
	sqlparser_mysql_state_t *state,
	size_t statement_index,
	sqlparser_error_t *out_error)
{
	size_t *next;
	size_t next_capacity;
	size_t index;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	for (index = 0U; index < state->lock_in_share_count; index++) {
		if (state->lock_in_share_statements[index] == statement_index) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (state->lock_in_share_count == state->lock_in_share_capacity) {
		next_capacity = state->lock_in_share_capacity == 0U ? 4U : state->lock_in_share_capacity * 2U;
		if (next_capacity < state->lock_in_share_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*state->lock_in_share_statements)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (size_t *)realloc(
			state->lock_in_share_statements,
			next_capacity * sizeof(*state->lock_in_share_statements));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->lock_in_share_statements = next;
		state->lock_in_share_capacity = next_capacity;
	}
	state->lock_in_share_statements[state->lock_in_share_count++] = statement_index;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_state_has_lock_in_share(
	const sqlparser_mysql_state_t *state,
	size_t statement_index)
{
	size_t index;

	if (state == NULL) {
		return 0;
	}
	for (index = 0U; index < state->lock_in_share_count; index++) {
		if (state->lock_in_share_statements[index] == statement_index) {
			return 1;
		}
	}
	return 0;
}

static const sqlparser_mysql_dml_modifier_t *sqlparser_mysql_state_dml_modifier(
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	sqlparser_mysql_dml_modifier_kind_t kind)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->dml_modifier_count; index++) {
		if (state->dml_modifiers[index].statement_index == statement_index &&
		    state->dml_modifiers[index].kind == kind) {
			return &state->dml_modifiers[index];
		}
	}
	return NULL;
}

static sqlparser_graph_insert_mode_t sqlparser_mysql_state_insert_mode_override(
	const sqlparser_mysql_state_t *state,
	size_t statement_index)
{
	size_t index;

	if (state == NULL) {
		return SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN;
	}
	for (index = 0U; index < state->dml_modifier_count; index++) {
		if (state->dml_modifiers[index].statement_index == statement_index &&
		    state->dml_modifiers[index].insert_mode_override != SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN) {
			return state->dml_modifiers[index].insert_mode_override;
		}
	}
	return SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN;
}

static int sqlparser_mysql_origin_size_add(
	size_t left,
	size_t right,
	size_t *out_value)
{
	if (right > SIZE_MAX - left) {
		return 0;
	}
	*out_value = left + right;
	return 1;
}

static void sqlparser_mysql_origin_trace_release(
	sqlparser_mysql_origin_trace_t *trace)
{
	if (trace == NULL) {
		return;
	}
	free(trace->runs);
	memset(trace, 0, sizeof(*trace));
}

static sqlparser_status_t sqlparser_mysql_origin_trace_reserve(
	sqlparser_mysql_origin_trace_t *trace,
	size_t required,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_origin_run_t *next;
	size_t next_capacity;

	if (required <= trace->run_capacity) {
		return SQLPARSER_STATUS_OK;
	}
	next_capacity = trace->run_capacity == 0U ? 8U : trace->run_capacity;
	while (next_capacity < required) {
		if (next_capacity > SIZE_MAX / 2U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"MySQL identifier origin trace is too large");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}
	if (next_capacity > SIZE_MAX / sizeof(*next)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"MySQL identifier origin trace is too large");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	next = (sqlparser_mysql_origin_run_t *)realloc(
		trace->runs,
		next_capacity * sizeof(*next));
	if (next == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	trace->runs = next;
	trace->run_capacity = next_capacity;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_origin_trace_append(
	sqlparser_mysql_origin_trace_t *trace,
	const sqlparser_mysql_origin_run_t *run,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_origin_run_t *previous;
	sqlparser_status_t status;
	size_t previous_input_end;
	size_t previous_output_end;

	if (trace == NULL || run->output_length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (trace->run_count > 0U) {
		previous = &trace->runs[trace->run_count - 1U];
		if (!sqlparser_mysql_origin_size_add(
			    previous->output_offset,
			    previous->output_length,
			    &previous_output_end) ||
		    previous_output_end > run->output_offset) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"MySQL identifier origin trace overlaps");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (previous->kind == SQLPARSER_MYSQL_ORIGIN_INPUT &&
		    run->kind == SQLPARSER_MYSQL_ORIGIN_INPUT &&
		    previous_output_end == run->output_offset &&
		    sqlparser_mysql_origin_size_add(
			    previous->input_offset,
			    previous->input_length,
			    &previous_input_end) &&
		    previous_input_end == run->input_offset) {
			if (!sqlparser_mysql_origin_size_add(
				    previous->output_length,
				    run->output_length,
				    &previous->output_length) ||
			    !sqlparser_mysql_origin_size_add(
				    previous->input_length,
				    run->input_length,
				    &previous->input_length)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"MySQL identifier origin trace is too large");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			return SQLPARSER_STATUS_OK;
		}
	}
	status = sqlparser_mysql_origin_trace_reserve(
		trace,
		trace->run_count + 1U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	trace->runs[trace->run_count++] = *run;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_buffer_begin_origin(
	sqlparser_mysql_buffer_t *buffer,
	sqlparser_mysql_origin_trace_t *trace,
	const char *input,
	size_t input_base,
	sqlparser_error_t *out_error)
{
	if (trace == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (buffer == NULL || input == NULL || trace->runs != NULL ||
	    trace->run_count != 0U || trace->run_capacity != 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MySQL identifier origin buffer is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	buffer->origin_input = input;
	buffer->origin_input_length = strlen(input);
	buffer->origin_input_base = input_base;
	buffer->origin_trace = trace;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_mysql_buffer_release(sqlparser_mysql_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
	buffer->origin_input = NULL;
	buffer->origin_input_length = 0U;
	buffer->origin_input_base = 0U;
	buffer->origin_trace = NULL;
}

static sqlparser_status_t sqlparser_mysql_buffer_reserve(
	sqlparser_mysql_buffer_t *buffer,
	size_t extra,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t required;
	size_t next_capacity;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (extra > ((size_t)-1) - buffer->len - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	required = buffer->len + extra + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next_capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
	while (next_capacity < required) {
		if (next_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}

	next = (char *)realloc(buffer->data, next_capacity);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = next_capacity;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_buffer_append_raw_mem(
	sqlparser_mysql_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"append data must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_mysql_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_buffer_append_mem(
	sqlparser_mysql_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_origin_run_t run;
	uintptr_t data_address;
	uintptr_t input_address;
	size_t input_offset;
	sqlparser_status_t status;

	if (len > 0U && buffer != NULL && buffer->origin_trace != NULL &&
	    data != NULL) {
		data_address = (uintptr_t)(const void *)data;
		input_address = (uintptr_t)(const void *)buffer->origin_input;
		if (data_address >= input_address &&
		    data_address - input_address <= buffer->origin_input_length) {
			input_offset = (size_t)(data_address - input_address);
			if (len <= buffer->origin_input_length - input_offset &&
			    input_offset <= SIZE_MAX - buffer->origin_input_base) {
				memset(&run, 0, sizeof(run));
				run.kind = SQLPARSER_MYSQL_ORIGIN_INPUT;
				run.output_offset = buffer->len;
				run.output_length = len;
				run.input_offset =
					buffer->origin_input_base + input_offset;
				run.input_length = len;
				status = sqlparser_mysql_origin_trace_append(
					buffer->origin_trace,
					&run,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
		}
	}
	return sqlparser_mysql_buffer_append_raw_mem(
		buffer,
		data,
		len,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_append_char(
	sqlparser_mysql_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_append_generated_identifier(
	sqlparser_mysql_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_origin_run_t run;
	sqlparser_status_t status;
	size_t length;

	length = text != NULL ? strlen(text) : 0U;
	if (buffer != NULL && buffer->origin_trace != NULL && length > 0U) {
		memset(&run, 0, sizeof(run));
		run.kind = SQLPARSER_MYSQL_ORIGIN_GENERATED_IDENTIFIER;
		run.output_offset = buffer->len;
		run.output_length = length;
		status = sqlparser_mysql_origin_trace_append(
			buffer->origin_trace,
			&run,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_mysql_buffer_append_raw_mem(
		buffer,
		text,
		length,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_mark_source_identifier(
	sqlparser_mysql_buffer_t *buffer,
	size_t output_offset,
	const char *input_start,
	size_t input_length,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_origin_run_t run;
	uintptr_t input_address;
	uintptr_t start_address;
	size_t local_offset;

	if (buffer == NULL || buffer->origin_trace == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	input_address = (uintptr_t)(const void *)buffer->origin_input;
	start_address = (uintptr_t)(const void *)input_start;
	if (start_address < input_address ||
	    start_address - input_address > buffer->origin_input_length) {
		return SQLPARSER_STATUS_OK;
	}
	local_offset = (size_t)(start_address - input_address);
	if (input_length > buffer->origin_input_length - local_offset ||
	    local_offset > SIZE_MAX - buffer->origin_input_base ||
	    output_offset > buffer->len) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&run, 0, sizeof(run));
	run.kind = SQLPARSER_MYSQL_ORIGIN_SOURCE_IDENTIFIER;
	run.output_offset = output_offset;
	run.output_length = buffer->len - output_offset;
	run.input_offset = buffer->origin_input_base + local_offset;
	run.input_length = input_length;
	return sqlparser_mysql_origin_trace_append(
		buffer->origin_trace,
		&run,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_append_trace_mem(
	sqlparser_mysql_buffer_t *buffer,
	const char *data,
	size_t length,
	const sqlparser_mysql_origin_trace_t *trace,
	sqlparser_error_t *out_error)
{
	size_t index;
	size_t output_base;
	sqlparser_status_t status;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MySQL traced append buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	output_base = buffer->len;
	if (buffer->origin_trace != NULL && trace != NULL) {
		for (index = 0U; index < trace->run_count; index++) {
			sqlparser_mysql_origin_run_t run;

			run = trace->runs[index];
			if (!sqlparser_mysql_origin_size_add(
				    output_base,
				    run.output_offset,
				    &run.output_offset)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"MySQL identifier origin trace is too large");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			status = sqlparser_mysql_origin_trace_append(
				buffer->origin_trace,
				&run,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
	}
	return sqlparser_mysql_buffer_append_raw_mem(
		buffer,
		data,
		length,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_append_cstr(
	sqlparser_mysql_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_append_trace_cstr(
	sqlparser_mysql_buffer_t *buffer,
	const char *text,
	const sqlparser_mysql_origin_trace_t *trace,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_buffer_append_trace_mem(
		buffer,
		text,
		text != NULL ? strlen(text) : 0U,
		trace,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_finish(
	sqlparser_mysql_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_buffer_reserve(buffer, 0U, out_error);
}

static sqlparser_status_t sqlparser_mysql_append_pg_param(
	sqlparser_mysql_buffer_t *out,
	size_t param_index,
	sqlparser_error_t *out_error)
{
	char text[32];

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	return sqlparser_mysql_buffer_append_cstr(out, text, out_error);
}

static char *sqlparser_mysql_buffer_take(sqlparser_mysql_buffer_t *buffer)
{
	char *data;

	if (buffer == NULL) {
		return NULL;
	}

	data = buffer->data;
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
	buffer->origin_input = NULL;
	buffer->origin_input_length = 0U;
	buffer->origin_input_base = 0U;
	buffer->origin_trace = NULL;
	return data;
}

static sqlparser_status_t sqlparser_mysql_origin_trace_commit(
	sqlparser_identifier_origin_map_t *origins,
	const char *input_sql,
	const char *output_sql,
	const sqlparser_mysql_origin_trace_t *trace,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_writer_t writer;
	sqlparser_status_t status;
	size_t index;
	size_t output_length;
	size_t position;

	if (origins == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (input_sql == NULL || output_sql == NULL || trace == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MySQL identifier origin commit arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&writer, 0, sizeof(writer));
	status = sqlparser_identifier_origin_writer_begin(
		&writer,
		origins,
		strlen(input_sql),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	output_length = strlen(output_sql);
	position = 0U;
	for (index = 0U; index < trace->run_count; index++) {
		const sqlparser_mysql_origin_run_t *run;
		size_t run_end;

		run = &trace->runs[index];
		if (run->output_offset < position ||
		    !sqlparser_mysql_origin_size_add(
			    run->output_offset,
			    run->output_length,
			    &run_end) ||
		    run_end > output_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"MySQL identifier origin trace is invalid");
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			break;
		}
		status = sqlparser_identifier_origin_writer_append_unknown(
			&writer,
			run->output_offset - position,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		switch (run->kind) {
			case SQLPARSER_MYSQL_ORIGIN_INPUT:
				status = sqlparser_identifier_origin_writer_append_input(
					&writer,
					run->input_offset,
					run->input_length,
					out_error);
				break;
			case SQLPARSER_MYSQL_ORIGIN_SOURCE_IDENTIFIER:
				status =
					sqlparser_identifier_origin_writer_append_source_identifier(
						&writer,
						run->input_offset,
						run->input_length,
						run->output_length,
						out_error);
				break;
			case SQLPARSER_MYSQL_ORIGIN_GENERATED_IDENTIFIER:
				status =
					sqlparser_identifier_origin_writer_append_generated_identifier(
						&writer,
						run->output_length,
						out_error);
				break;
			default:
				status = SQLPARSER_STATUS_INTERNAL_ERROR;
				sqlparser_error_set_message(
					out_error,
					status,
					"MySQL identifier origin kind is invalid");
				break;
		}
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		position = run_end;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_append_unknown(
			&writer,
			output_length - position,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_commit(
			&writer,
			output_length,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_writer_release(&writer);
	}
	return status;
}

static sqlparser_status_t sqlparser_mysql_buffer_reserve_input(
	sqlparser_mysql_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t input_len;
	size_t required;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	input_len = input != NULL ? strlen(input) : 0U;
	if (input_len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	required = input_len + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next = (char *)realloc(buffer->data, required);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = required;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_';
}

static int sqlparser_mysql_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_';
}

static int sqlparser_mysql_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_mysql_is_ident_char(prev) && !sqlparser_mysql_is_ident_char(next);
}

static int sqlparser_mysql_ascii_word_equal(const char *text, size_t pos, const char *word)
{
	size_t index;
	size_t len;

	if (text == NULL || word == NULL) {
		return 0;
	}

	len = strlen(word);
	for (index = 0U; index < len; index++) {
		if (text[pos + index] == '\0') {
			return 0;
		}
		if (tolower((unsigned char)text[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}

	return sqlparser_mysql_is_word_boundary(text, pos, len);
}

static sqlparser_status_t sqlparser_mysql_append_pg_string_char(
	sqlparser_mysql_buffer_t *out,
	char value,
	sqlparser_error_t *out_error)
{
	if (value == '\'') {
		return sqlparser_mysql_buffer_append_cstr(out, "''", out_error);
	}

	return sqlparser_mysql_buffer_append_char(out, value, out_error);
}

static sqlparser_status_t sqlparser_mysql_copy_string_literal(
	const char *input,
	size_t *index,
	char quote,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t pos;

	status = sqlparser_mysql_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (input[pos] != '\0') {
		char c;

		c = input[pos];
		if (c == '\\' && input[pos + 1U] != '\0') {
			pos++;
			c = input[pos];
			switch (c) {
				case '0':
					c = '0';
					break;
				case 'b':
					c = '\b';
					break;
				case 'n':
					c = '\n';
					break;
				case 'r':
					c = '\r';
					break;
				case 't':
					c = '\t';
					break;
				case 'Z':
					c = 26;
					break;
				default:
					break;
			}
			status = sqlparser_mysql_append_pg_string_char(out, c, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			pos++;
			continue;
		}
		if (c == quote) {
			if (input[pos + 1U] == quote) {
				status = sqlparser_mysql_append_pg_string_char(out, quote, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += 2U;
				continue;
			}
			status = sqlparser_mysql_buffer_append_char(out, '\'', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 1U;
			return SQLPARSER_STATUS_OK;
		}

		status = sqlparser_mysql_append_pg_string_char(out, c, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated MySQL string literal");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static int sqlparser_mysql_is_n_string_literal(const char *text)
{
	return text != NULL &&
		(text[0] == 'n' || text[0] == 'N') &&
		text[1] == '\'';
}

static sqlparser_status_t sqlparser_mysql_copy_n_string_literal(
	const char *input,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t literal_index;
	sqlparser_status_t status;

	if (!sqlparser_mysql_is_n_string_literal(input + *index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid MySQL national string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	literal_index = *index + 1U;
	status = sqlparser_mysql_copy_string_literal(input, &literal_index, '\'', out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = literal_index;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_copy_backtick_identifier(
	const char *input,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t input_start;
	size_t output_start;
	size_t pos;

	input_start = *index;
	output_start = out->len;
	status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (input[pos] != '\0') {
		char c;

		c = input[pos];
		if (c == '`') {
			if (input[pos + 1U] == '`') {
				status = sqlparser_mysql_buffer_append_char(out, '`', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += 2U;
				continue;
			}
			status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 1U;
			return sqlparser_mysql_buffer_mark_source_identifier(
				out,
				output_start,
				input + input_start,
				*index - input_start,
				out_error);
		}
		if (c == '"') {
			status = sqlparser_mysql_buffer_append_cstr(out, "\"\"", out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(out, c, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated MySQL quoted identifier");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_mysql_preprocess_quotes(
	const char *input_sql,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		input_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_mysql_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (input_sql[index] != '\0') {
		char c;

		c = input_sql[index];
		if (c == '-' && input_sql[index + 1U] == '-') {
			while (input_sql[index] != '\0') {
				status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK || input_sql[index] == '\n') {
					break;
				}
				index++;
			}
			if (status == SQLPARSER_STATUS_OK && input_sql[index] == '\n') {
				index++;
			}
		} else if (c == '/' && input_sql[index + 1U] == '*') {
			status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
			while (status == SQLPARSER_STATUS_OK && input_sql[index] != '\0') {
				status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
				if (input_sql[index] == '*' && input_sql[index + 1U] == '/') {
					index++;
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
					}
					index++;
					break;
				}
				index++;
			}
		} else if (state != NULL && sqlparser_mysql_is_n_string_literal(input_sql + index)) {
			size_t literal_start;

			literal_start = out.len;
			status = sqlparser_mysql_copy_n_string_literal(input_sql, &index, &out, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_store_national_literal(
					state,
					out.data + literal_start,
					out.len - literal_start,
					state->literal_count,
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				state->literal_count++;
			}
		} else if (c == '`') {
			status = sqlparser_mysql_copy_backtick_identifier(input_sql, &index, &out, out_error);
		} else if (c == '\'' || c == '"') {
			status = sqlparser_mysql_copy_string_literal(input_sql, &index, c, &out, out_error);
			if (status == SQLPARSER_STATUS_OK && state != NULL) {
				state->literal_count++;
			}
		} else if (c == '#') {
			status = sqlparser_mysql_buffer_append_cstr(&out, "--", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
				while (input_sql[index] != '\0' && input_sql[index] != '\n') {
					status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
					if (status != SQLPARSER_STATUS_OK) {
						break;
					}
					index++;
				}
			}
		} else if (c == '?' && state != NULL) {
			if (state->positional_param_count == (size_t)-1) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				sqlparser_mysql_buffer_release(&out);
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			state->positional_param_count++;
			status = sqlparser_mysql_append_pg_param(&out, state->positional_param_count, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		} else {
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				input_sql + index,
				1U,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	size_t pos;

	if (sql == NULL) {
		return index;
	}

	quote = sql[index];
	if (quote == '\'' || quote == '"' || quote == '`') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}

	if (sql[index] == '-' && sql[index + 1U] == '-') {
		pos = index + 2U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '\n') {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}

	if (sql[index] == '#') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '\n') {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}

	if (sql[index] == '/' && sql[index + 1U] == '*') {
		pos = index + 2U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				return pos + 2U;
			}
			pos++;
		}
		return pos;
	}

	return index;
}

static size_t sqlparser_mysql_skip_leading_trivia(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t skipped;

	pos = start;
	for (;;) {
		while (pos < end && isspace((unsigned char)sql[pos])) {
			pos++;
		}
		if (pos >= end ||
		    !((sql[pos] == '-' && pos + 1U < end && sql[pos + 1U] == '-') ||
		      sql[pos] == '#' ||
		      (sql[pos] == '/' && pos + 1U < end && sql[pos + 1U] == '*'))) {
			return pos;
		}
		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
		if (skipped <= pos) {
			return pos;
		}
		pos = skipped < end ? skipped : end;
	}
}

static sqlparser_status_t sqlparser_mysql_mask_non_code(
	const char *sql,
	char **out_masked,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t index;

	len = strlen(sql);
	masked = sqlparser_strndup(sql, len);
	if (masked == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (index = 0U; index < len; index++) {
		if (masked[index] == '\'') {
			index++;
			while (index < len) {
				if (masked[index] == '\'' && masked[index + 1U] == '\'') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '\'') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '"') {
			index++;
			while (index < len) {
				if (masked[index] == '"' && masked[index + 1U] == '"') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '"') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '`') {
			index++;
			while (index < len) {
				if (masked[index] == '`' && masked[index + 1U] == '`') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '`') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '-' && masked[index + 1U] == '-') {
			while (index < len && masked[index] != '\n') {
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '/' && masked[index + 1U] == '*') {
			masked[index] = ' ';
			masked[index + 1U] = ' ';
			index += 2U;
			while (index < len) {
				if (masked[index] == '*' && masked[index + 1U] == '/') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index++;
					break;
				}
				masked[index] = ' ';
				index++;
			}
		}
	}

	for (index = 0U; index < len; index++) {
		masked[index] = (char)tolower((unsigned char)masked[index]);
	}

	*out_masked = masked;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_contains_phrase(const char *masked, const char *phrase)
{
	size_t phrase_len;
	size_t last_phrase_pos;
	int needs_left_boundary;
	int needs_right_boundary;
	size_t pos;

	if (masked == NULL || phrase == NULL || phrase[0] == '\0') {
		return 0;
	}

	phrase_len = strlen(phrase);
	last_phrase_pos = phrase_len;
	while (last_phrase_pos > 0U && isspace((unsigned char)phrase[last_phrase_pos - 1U])) {
		last_phrase_pos--;
	}
	if (last_phrase_pos == 0U) {
		return 0;
	}

	needs_left_boundary = sqlparser_mysql_is_ident_char((unsigned char)phrase[0]);
	needs_right_boundary = sqlparser_mysql_is_ident_char((unsigned char)phrase[last_phrase_pos - 1U]);

	for (pos = 0U; masked[pos] != '\0'; pos++) {
		size_t text_pos;
		size_t phrase_pos;
		int matched;

		if (needs_left_boundary && pos > 0U &&
		    sqlparser_mysql_is_ident_char((unsigned char)masked[pos - 1U])) {
			continue;
		}

		text_pos = pos;
		phrase_pos = 0U;
		matched = 1;
		while (phrase[phrase_pos] != '\0') {
			if (isspace((unsigned char)phrase[phrase_pos])) {
				int saw_space;

				saw_space = 0;
				while (isspace((unsigned char)phrase[phrase_pos])) {
					phrase_pos++;
				}
				while (isspace((unsigned char)masked[text_pos])) {
					saw_space = 1;
					text_pos++;
				}
				if (!saw_space) {
					matched = 0;
					break;
				}
				continue;
			}
			if (masked[text_pos] != phrase[phrase_pos]) {
				matched = 0;
				break;
			}
			text_pos++;
			phrase_pos++;
		}

		if (matched &&
		    (!needs_right_boundary || !sqlparser_mysql_is_ident_char((unsigned char)masked[text_pos]))) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_mysql_word_at(const char *masked, size_t pos, const char *word)
{
	size_t index;

	if (masked == NULL || word == NULL || word[0] == '\0') {
		return 0;
	}
	if (pos > 0U && sqlparser_mysql_is_ident_char((unsigned char)masked[pos - 1U])) {
		return 0;
	}
	for (index = 0U; word[index] != '\0'; index++) {
		if (masked[pos + index] != word[index]) {
			return 0;
		}
	}
	return !sqlparser_mysql_is_ident_char((unsigned char)masked[pos + index]);
}

static int sqlparser_mysql_first_top_level_word_is(const char *masked, const char *word)
{
	size_t pos;

	if (masked == NULL || word == NULL) {
		return 0;
	}
	for (pos = 0U; masked[pos] != '\0'; pos++) {
		if (!isspace((unsigned char)masked[pos])) {
			return sqlparser_mysql_word_at(masked, pos, word);
		}
	}
	return 0;
}

static int sqlparser_mysql_top_level_word_before(
	const char *masked,
	const char *word,
	const char *before_word)
{
	size_t pos;
	int depth;

	if (masked == NULL || word == NULL) {
		return 0;
	}

	depth = 0;
	for (pos = 0U; masked[pos] != '\0'; pos++) {
		if (masked[pos] == '(') {
			depth++;
			continue;
		}
		if (masked[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
		if (depth != 0) {
			continue;
		}
		if (before_word != NULL && sqlparser_mysql_word_at(masked, pos, before_word)) {
			return 0;
		}
		if (sqlparser_mysql_word_at(masked, pos, word)) {
			return 1;
		}
	}
	return 0;
}

static size_t sqlparser_mysql_find_top_level_word_between(
	const char *masked,
	const char *word,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	if (masked == NULL || word == NULL || word[0] == '\0') {
		return (size_t)-1;
	}
	depth = 0;
	for (pos = start; pos < end && masked[pos] != '\0'; pos++) {
		if (masked[pos] == '(') {
			depth++;
			continue;
		}
		if (masked[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
		if (depth == 0 && sqlparser_mysql_word_at(masked, pos, word)) {
			return pos;
		}
	}
	return (size_t)-1;
}

static size_t sqlparser_mysql_find_top_level_char_between(
	const char *sql,
	char needle,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	if (sql == NULL) {
		return (size_t)-1;
	}
	depth = 0;
	for (pos = start; pos < end && sql[pos] != '\0'; pos++) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped - 1U;
			continue;
		}
		if (depth == 0 && sql[pos] == needle) {
			return pos;
		}
		if (sql[pos] == '(') {
			depth++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
	}
	return (size_t)-1;
}

static int sqlparser_mysql_is_unsupported_update_join(const char *masked)
{
	return sqlparser_mysql_first_top_level_word_is(masked, "update") &&
		sqlparser_mysql_top_level_word_before(masked, "join", "set");
}

static int sqlparser_mysql_is_unsupported_delete_join(const char *masked)
{
	return sqlparser_mysql_first_top_level_word_is(masked, "delete") &&
		sqlparser_mysql_top_level_word_before(masked, "join", "where");
}

static int sqlparser_mysql_raw_contains_word_span(const char *sql, const char *word, size_t word_len)
{
	size_t pos;

	if (sql == NULL || word == NULL || word_len == 0U) {
		return 0;
	}

	for (pos = 0U; sql[pos] != '\0'; pos++) {
		size_t index;

		if (pos > 0U && sqlparser_mysql_is_ident_char((unsigned char)sql[pos - 1U])) {
			continue;
		}

		for (index = 0U; index < word_len; index++) {
			if (sql[pos + index] == '\0') {
				break;
			}
			if (tolower((unsigned char)sql[pos + index]) !=
			    tolower((unsigned char)word[index])) {
				break;
			}
		}
		if (index == word_len &&
		    !sqlparser_mysql_is_ident_char((unsigned char)sql[pos + word_len])) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_mysql_raw_may_contain_phrase(const char *sql, const char *phrase)
{
	size_t pos;
	int saw_token;

	if (sql == NULL || phrase == NULL) {
		return 0;
	}

	pos = 0U;
	saw_token = 0;
	while (phrase[pos] != '\0') {
		size_t start;
		size_t len;

		while (phrase[pos] != '\0' &&
		       !sqlparser_mysql_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		start = pos;
		while (phrase[pos] != '\0' &&
		       sqlparser_mysql_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		len = pos - start;
		if (len == 0U) {
			continue;
		}

		saw_token = 1;
		if (!sqlparser_mysql_raw_contains_word_span(sql, phrase + start, len)) {
			return 0;
		}
	}

	return saw_token;
}

static sqlparser_status_t sqlparser_mysql_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"on duplicate key update",
		"update join",
		"delete join",
		"auto_increment",
		"unsigned",
		"zerofill",
		"engine =",
		"engine=",
		"charset =",
		"charset=",
		"character set =",
		"character set=",
		"collate =",
		"collate="
	};
	char *masked;
	sqlparser_status_t status;
	size_t index;
	int needs_mask;

	needs_mask = 0;
	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_mysql_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
			break;
		}
	}
	if (!needs_mask) {
		return SQLPARSER_STATUS_OK;
	}

	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_mysql_is_unsupported_update_join(masked)) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported MySQL syntax: update join");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_mysql_is_unsupported_delete_join(masked)) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported MySQL syntax: delete join");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_mysql_contains_phrase(masked, unsupported_phrases[index])) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"unsupported MySQL syntax: %s",
				unsupported_phrases[index]);
			free(masked);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, message);
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	free(masked);
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_copy_single_quoted_or_comment(
	const char *sql,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	quote = sql[*index];
	if (quote == '\'') {
		pos = *index;
		if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos++;
					if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
						return -1;
					}
				} else {
					pos++;
					break;
				}
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	if (sql[*index] == '-' && sql[*index + 1U] == '-') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '\n') {
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	if (sql[*index] == '/' && sql[*index + 1U] == '*') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				pos++;
				if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
					return -1;
				}
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	return 0;
}

static const char *sqlparser_mysql_trim_left(const char *start, const char *end)
{
	while (start < end && isspace((unsigned char)*start)) {
		start++;
	}
	return start;
}

static const char *sqlparser_mysql_trim_right(const char *start, const char *end)
{
	while (end > start && isspace((unsigned char)*(end - 1))) {
		end--;
	}
	return end;
}

static size_t sqlparser_mysql_skip_space(const char *text, size_t pos)
{
	while (isspace((unsigned char)text[pos])) {
		pos++;
	}
	return pos;
}

static int sqlparser_mysql_span_has_space(const char *start, const char *end)
{
	const char *pos;

	for (pos = start; pos < end; pos++) {
		if (isspace((unsigned char)*pos)) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_mysql_append_pg_quoted_identifier(
	sqlparser_mysql_buffer_t *out,
	const char *start,
	const char *end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;
	size_t output_start;

	output_start = out != NULL ? out->len : 0U;
	status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (*pos == '"') {
			status = sqlparser_mysql_buffer_append_cstr(out, "\"\"", out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(out, *pos, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_mysql_buffer_mark_source_identifier(
		out,
		output_start,
		start,
		(size_t)(end - start),
		out_error);
}

static sqlparser_status_t sqlparser_mysql_append_internal_string_literal(
	sqlparser_mysql_buffer_t *out,
	const char *start,
	const char *end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (*pos == '\'') {
			status = sqlparser_mysql_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(out, *pos, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_mysql_buffer_append_char(out, '\'', out_error);
}

static sqlparser_status_t sqlparser_mysql_append_internal_set(
	sqlparser_mysql_buffer_t *out,
	const char *internal_name,
	const char *arg0_start,
	const char *arg0_end,
	const char *arg1_start,
	const char *arg1_end,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_generated_identifier(
			out,
			internal_name,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_internal_string_literal(out, arg0_start, arg0_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && arg1_start != NULL && arg1_end != NULL) {
		status = sqlparser_mysql_buffer_append_cstr(out, ", ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_append_internal_string_literal(out, arg1_start, arg1_end, out_error);
		}
	}
	return status;
}

static size_t sqlparser_mysql_statement_token_end(const char *sql, size_t pos, size_t end)
{
	if (pos < end && sql[pos] == '`') {
		pos++;
		while (pos < end) {
			if (sql[pos] == '`') {
				if (pos + 1U < end && sql[pos + 1U] == '`') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return end;
	}
	while (pos < end && !isspace((unsigned char)sql[pos]) && sql[pos] != ';' && sql[pos] != ',') {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_mysql_preprocess_use_statement(
	const char *input_sql,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t quoted_origin;
	const char *start;
	const char *end;
	const char *name_start;
	const char *name_end;
	char *quoted_name;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = input_sql;
	end = input_sql + strlen(input_sql);
	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (end > start && *(end - 1) == ';') {
		end--;
		end = sqlparser_mysql_trim_right(start, end);
	}
	if ((size_t)(end - start) < strlen("use") ||
	    !sqlparser_mysql_ascii_word_equal(start, 0U, "use")) {
		return SQLPARSER_STATUS_OK;
	}
	name_start = start + strlen("use");
	if (name_start >= end || !isspace((unsigned char)*name_start)) {
		return SQLPARSER_STATUS_OK;
	}
	name_start = sqlparser_mysql_trim_left(name_start, end);
	name_end = sqlparser_mysql_trim_right(name_start, end);
	if (name_start >= name_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE requires a database name");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		input_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_mysql_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_generated_identifier(
			&out,
			SQLPARSER_INTERNAL_CURRENT_DATABASE,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	quoted_name = NULL;
	if (*name_start == '`') {
		char *slice;
		size_t quoted_input_base;

		slice = sqlparser_strndup(name_start, (size_t)(name_end - name_start));
		if (slice == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		memset(&quoted_origin, 0, sizeof(quoted_origin));
		if (!sqlparser_mysql_origin_size_add(
			    origin_input_base,
			    (size_t)(name_start - input_sql),
			    &quoted_input_base)) {
			free(slice);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"MySQL identifier origin trace is too large");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		status = sqlparser_mysql_preprocess_quotes(
			slice,
			NULL,
			&quoted_name,
			quoted_input_base,
			origin_trace != NULL ? &quoted_origin : NULL,
			out_error);
		free(slice);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_trace_cstr(
				&out,
				quoted_name,
				&quoted_origin,
				out_error);
		}
		free(quoted_name);
		sqlparser_mysql_origin_trace_release(&quoted_origin);
	} else if (*name_start == '"') {
		const char *pos;

		pos = name_start + 1;
		while (pos < name_end) {
			if (*pos == '"' && pos + 1 < name_end && *(pos + 1) == '"') {
				pos += 2;
				continue;
			}
			if (*pos == '"') {
				pos++;
				break;
			}
			pos++;
		}
		if (pos != name_end) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name has trailing text");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_mysql_buffer_append_mem(&out, name_start, (size_t)(name_end - name_start), out_error);
	} else {
		if (sqlparser_mysql_span_has_space(name_start, name_end)) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name must be quoted when it contains whitespace");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_mysql_append_pg_quoted_identifier(&out, name_start, name_end, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_session_sql_is_complete(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	depth = 0;
	for (pos = start; pos < end; pos++) {
		char quote;

		quote = sql[pos];
		if (quote == '\'' || quote == '"' || quote == '`') {
			int closed;

			closed = 0;
			for (pos++; pos < end; pos++) {
				if (sql[pos] == '\\' && quote != '`' && pos + 1U < end) {
					pos++;
					continue;
				}
				if (sql[pos] != quote) {
					continue;
				}
				if (pos + 1U < end && sql[pos + 1U] == quote) {
					pos++;
					continue;
				}
				closed = 1;
				break;
			}
			if (!closed) {
				return 0;
			}
			continue;
		}
		if (sql[pos] == '/' && pos + 1U < end && sql[pos + 1U] == '*') {
			int closed;

			closed = 0;
			for (pos += 2U; pos + 1U < end; pos++) {
				if (sql[pos] == '*' && sql[pos + 1U] == '/') {
					pos++;
					closed = 1;
					break;
				}
			}
			if (!closed) {
				return 0;
			}
			continue;
		}
		if (sql[pos] == '-' && pos + 1U < end && sql[pos + 1U] == '-') {
			while (pos < end && sql[pos] != '\n') {
				pos++;
			}
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			if (depth == 0) {
				return 0;
			}
			depth--;
		}
	}
	return depth == 0;
}

static int sqlparser_mysql_session_consume_word(
	const char *sql,
	size_t *io_pos,
	size_t end,
	const char *word)
{
	size_t len;
	size_t pos;

	pos = sqlparser_mysql_skip_space(sql, *io_pos);
	len = strlen(word);
	if (pos >= end || len > end - pos ||
	    !sqlparser_mysql_ascii_word_equal(sql, pos, word)) {
		return 0;
	}
	*io_pos = pos + len;
	return 1;
}

static size_t sqlparser_mysql_session_token_end(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (start >= end) {
		return start;
	}
	if (sql[start] == '\'' || sql[start] == '"' || sql[start] == '`') {
		pos = sqlparser_mysql_skip_quoted_or_comment_span(sql, start);
		return pos <= end && pos > start + 1U && sql[pos - 1U] == sql[start] ?
			pos :
			start;
	}
	pos = start;
	while (pos < end && !isspace((unsigned char)sql[pos]) && sql[pos] != ',') {
		pos++;
	}
	return pos;
}

static int sqlparser_mysql_session_assignment_target_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t token_end;

	if (start >= end) {
		return 0;
	}
	if (sql[end - 1U] == ':') {
		end--;
		end = (size_t)(sqlparser_mysql_trim_right(sql + start, sql + end) - sql);
		if (start >= end) {
			return 0;
		}
	}
	pos = start;
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "session") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "local")) {
		pos += sqlparser_mysql_ascii_word_equal(sql, pos, "session") ?
			strlen("session") :
			strlen("local");
		if (pos >= end || !isspace((unsigned char)sql[pos])) {
			return 0;
		}
		pos = sqlparser_mysql_skip_space(sql, pos);
	}
	if (pos + 1U < end && sql[pos] == '@' && sql[pos + 1U] == '@') {
		pos += 2U;
		if (sqlparser_mysql_ascii_word_equal(sql, pos, "global")) {
			return 0;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, pos, "session") ||
		    sqlparser_mysql_ascii_word_equal(sql, pos, "local")) {
			pos += sqlparser_mysql_ascii_word_equal(sql, pos, "session") ?
				strlen("session") :
				strlen("local");
			if (pos >= end || sql[pos] != '.') {
				return 0;
			}
			pos++;
		}
		if (pos >= end || !sqlparser_mysql_is_ident_start((unsigned char)sql[pos])) {
			return 0;
		}
		while (pos < end && sqlparser_mysql_is_ident_char((unsigned char)sql[pos])) {
			pos++;
		}
		return pos == end;
	}
	if (sql[pos] == '@') {
		pos++;
		if (pos >= end) {
			return 0;
		}
		if (sql[pos] == '\'' || sql[pos] == '"' || sql[pos] == '`') {
			token_end = sqlparser_mysql_session_token_end(sql, pos, end);
			return token_end == end;
		}
		token_end = pos;
		while (token_end < end &&
		       (sqlparser_mysql_is_ident_char((unsigned char)sql[token_end]) ||
		        sql[token_end] == '.' || sql[token_end] == '$' ||
		        (unsigned char)sql[token_end] >= 0x80U)) {
			token_end++;
		}
		return token_end > pos && token_end == end;
	}
	if (!sqlparser_mysql_is_ident_start((unsigned char)sql[pos])) {
		return 0;
	}
	while (pos < end && sqlparser_mysql_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	return pos == end;
}

static int sqlparser_mysql_session_list_is_nonempty(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t comma;
	const char *item_start;
	const char *item_end;

	if (!sqlparser_mysql_session_sql_is_complete(sql, start, end)) {
		return 0;
	}
	while (start < end) {
		comma = sqlparser_mysql_find_top_level_char_between(sql, ',', start, end);
		item_start = sqlparser_mysql_trim_left(
			sql + start,
			sql + (comma == (size_t)-1 ? end : comma));
		item_end = sqlparser_mysql_trim_right(
			item_start,
			sql + (comma == (size_t)-1 ? end : comma));
		if (item_start == item_end) {
			return 0;
		}
		if (comma == (size_t)-1) {
			return 1;
		}
		start = comma + 1U;
	}
	return 0;
}

static int sqlparser_mysql_session_assignment_list_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t comma;
	size_t equals;
	const char *left_start;
	const char *left_end;
	const char *right_start;
	const char *right_end;

	if (!sqlparser_mysql_session_list_is_nonempty(sql, start, end)) {
		return 0;
	}
	while (start < end) {
		comma = sqlparser_mysql_find_top_level_char_between(sql, ',', start, end);
		if (comma == (size_t)-1) {
			comma = end;
		}
		equals = sqlparser_mysql_find_top_level_char_between(sql, '=', start, comma);
		if (equals == (size_t)-1) {
			return 0;
		}
		left_start = sqlparser_mysql_trim_left(sql + start, sql + equals);
		left_end = sqlparser_mysql_trim_right(left_start, sql + equals);
		right_start = sqlparser_mysql_trim_left(sql + equals + 1U, sql + comma);
		right_end = sqlparser_mysql_trim_right(right_start, sql + comma);
		if (left_start == left_end || right_start == right_end || *right_start == '=') {
			return 0;
		}
		if (left_end[-1] == ':' && left_end != sql + equals) {
			return 0;
		}
		if (!sqlparser_mysql_session_assignment_target_is_supported(
			    sql,
			    (size_t)(left_start - sql),
			    (size_t)(left_end - sql))) {
			return 0;
		}
		if (comma == end) {
			return 1;
		}
		start = comma + 1U;
	}
	return 0;
}

static int sqlparser_mysql_session_transaction_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	int option_count;

	option_count = 0;
	while (pos < end) {
		if (sqlparser_mysql_session_consume_word(sql, &pos, end, "isolation")) {
			if (!sqlparser_mysql_session_consume_word(sql, &pos, end, "level")) {
				return 0;
			}
			if (sqlparser_mysql_session_consume_word(sql, &pos, end, "serializable")) {
				/* Complete option. */
			} else if (sqlparser_mysql_session_consume_word(sql, &pos, end, "repeatable")) {
				if (!sqlparser_mysql_session_consume_word(sql, &pos, end, "read")) {
					return 0;
				}
			} else if (sqlparser_mysql_session_consume_word(sql, &pos, end, "read")) {
				if (!sqlparser_mysql_session_consume_word(sql, &pos, end, "committed") &&
				    !sqlparser_mysql_session_consume_word(sql, &pos, end, "uncommitted")) {
					return 0;
				}
			} else {
				return 0;
			}
		} else if (sqlparser_mysql_session_consume_word(sql, &pos, end, "read")) {
			if (!sqlparser_mysql_session_consume_word(sql, &pos, end, "only") &&
			    !sqlparser_mysql_session_consume_word(sql, &pos, end, "write")) {
				return 0;
			}
		} else {
			return 0;
		}
		option_count++;
		pos = sqlparser_mysql_skip_space(sql, pos);
		if (pos == end) {
			return option_count > 0;
		}
		if (pos > end || sql[pos] != ',') {
			return 0;
		}
		pos = sqlparser_mysql_skip_space(sql, pos + 1U);
		if (pos >= end) {
			return 0;
		}
	}
	return 0;
}

static int sqlparser_mysql_session_character_set_is_supported(
	const char *sql,
	size_t pos,
	size_t end,
	int allow_collation)
{
	size_t token_end;

	pos = sqlparser_mysql_skip_space(sql, pos);
	token_end = sqlparser_mysql_session_token_end(sql, pos, end);
	if (token_end <= pos) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, token_end);
	if (pos == end) {
		return 1;
	}
	if (!allow_collation ||
	    !sqlparser_mysql_session_consume_word(sql, &pos, end, "collate")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, pos);
	token_end = sqlparser_mysql_session_token_end(sql, pos, end);
	return token_end > pos && sqlparser_mysql_skip_space(sql, token_end) == end;
}

static int sqlparser_mysql_session_role_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	if (sqlparser_mysql_session_consume_word(sql, &pos, end, "default") ||
	    sqlparser_mysql_session_consume_word(sql, &pos, end, "none")) {
		return sqlparser_mysql_skip_space(sql, pos) == end;
	}
	if (sqlparser_mysql_session_consume_word(sql, &pos, end, "all")) {
		if (sqlparser_mysql_skip_space(sql, pos) == end) {
			return 1;
		}
		if (!sqlparser_mysql_session_consume_word(sql, &pos, end, "except")) {
			return 0;
		}
	}
	pos = sqlparser_mysql_skip_space(sql, pos);
	return pos < end && sqlparser_mysql_session_list_is_nonempty(sql, pos, end);
}

static int sqlparser_mysql_session_set_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (!sqlparser_mysql_ascii_word_equal(sql, start, "set")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, start + strlen("set"));
	if (pos >= end ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "global") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "persist") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "persist_only") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "default") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "password") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "resource")) {
		return 0;
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "character")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("character"));
		if (!sqlparser_mysql_ascii_word_equal(sql, pos, "set")) {
			return 0;
		}
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("set"));
		return sqlparser_mysql_session_character_set_is_supported(sql, pos, end, 0);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "charset")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("charset"));
		return sqlparser_mysql_session_character_set_is_supported(sql, pos, end, 0);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "names")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("names"));
		return sqlparser_mysql_session_character_set_is_supported(sql, pos, end, 1);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "role")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("role"));
		return sqlparser_mysql_session_role_is_supported(sql, pos, end);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "transaction")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("transaction"));
		return sqlparser_mysql_session_transaction_is_supported(sql, pos, end);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "session")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("session"));
		if (sqlparser_mysql_ascii_word_equal(sql, pos, "transaction")) {
			pos = sqlparser_mysql_skip_space(sql, pos + strlen("transaction"));
			return sqlparser_mysql_session_transaction_is_supported(sql, pos, end);
		}
	} else if (sqlparser_mysql_ascii_word_equal(sql, pos, "local")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("local"));
		if (sqlparser_mysql_ascii_word_equal(sql, pos, "transaction")) {
			pos = sqlparser_mysql_skip_space(sql, pos + strlen("transaction"));
			return sqlparser_mysql_session_transaction_is_supported(sql, pos, end);
		}
	}
	return sqlparser_mysql_session_assignment_list_is_supported(sql, pos, end);
}

static int sqlparser_mysql_session_set_is_candidate(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (!sqlparser_mysql_ascii_word_equal(sql, start, "set")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, start + strlen("set"));
	if (pos >= end) {
		return 0;
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "character") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "charset") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "names") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "role") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "transaction") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "global") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "persist") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "persist_only") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "default") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "password") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "resource")) {
		return 1;
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "session") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "local")) {
		pos = sqlparser_mysql_skip_space(
			sql,
			pos + (sqlparser_mysql_ascii_word_equal(sql, pos, "session") ?
				strlen("session") :
				strlen("local")));
		if (sqlparser_mysql_ascii_word_equal(sql, pos, "transaction")) {
			return 1;
		}
	}
	return sql[pos] == '@' ||
		sqlparser_mysql_find_top_level_char_between(sql, '=', pos, end) !=
			(size_t)-1;
}

static sqlparser_status_t sqlparser_mysql_preprocess_session_statement(
	const char *input_sql,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *start;
	const char *end;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_mysql_trim_left(input_sql, input_sql + strlen(input_sql));
	end = sqlparser_mysql_trim_right(start, input_sql + strlen(input_sql));
	if (end > start && end[-1] == ';') {
		end = sqlparser_mysql_trim_right(start, end - 1);
	}
	if (!sqlparser_mysql_session_set_is_supported(
		    input_sql,
		    (size_t)(start - input_sql),
		    (size_t)(end - input_sql))) {
		if (sqlparser_mysql_session_set_is_candidate(
			    input_sql,
			    (size_t)(start - input_sql),
			    (size_t)(end - input_sql))) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_PARSE_ERROR,
				"invalid MySQL SET statement");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		input_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_mysql_append_internal_set(
		&out,
		SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT,
		start,
		end,
		NULL,
		NULL,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

int sqlparser_mysql_public_sql_is_session_statement(
	const char *sql,
	size_t length)
{
	size_t start;
	size_t end;

	if (sql == NULL) {
		return 0;
	}
	start = sqlparser_mysql_skip_space(sql, 0U);
	end = length;
	while (end > start && isspace((unsigned char)sql[end - 1U])) {
		end--;
	}
	if (end > start && sql[end - 1U] == ';') {
		end--;
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
	}
	return sqlparser_mysql_session_set_is_supported(sql, start, end);
}

static sqlparser_status_t sqlparser_mysql_preprocess_prepared_statement(
	const char *input_sql,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *start;
	const char *end;
	const char *name_start;
	const char *name_end;
	const char *value_start;
	const char *value_end;
	size_t pos;
	const char *internal_name;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = input_sql;
	end = input_sql + strlen(input_sql);
	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (end > start && *(end - 1) == ';') {
		end--;
		end = sqlparser_mysql_trim_right(start, end);
	}
	if (start >= end) {
		return SQLPARSER_STATUS_OK;
	}

	internal_name = NULL;
	value_start = NULL;
	value_end = NULL;
	pos = 0U;
	if (sqlparser_mysql_ascii_word_equal(start, pos, "prepare")) {
		pos = sqlparser_mysql_skip_space(start, pos + strlen("prepare"));
		name_start = start + pos;
		name_end = start + sqlparser_mysql_statement_token_end(start, pos, (size_t)(end - start));
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_mysql_skip_space(start, (size_t)(name_end - start));
		if (!sqlparser_mysql_ascii_word_equal(start, pos, "from")) {
			return SQLPARSER_STATUS_OK;
		}
		value_start = start + sqlparser_mysql_skip_space(start, pos + strlen("from"));
		value_end = sqlparser_mysql_trim_right(value_start, end);
		if (value_start >= value_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "PREPARE FROM requires a SQL source");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		internal_name = SQLPARSER_INTERNAL_MYSQL_PREPARE;
	} else if (sqlparser_mysql_ascii_word_equal(start, pos, "execute")) {
		pos = sqlparser_mysql_skip_space(start, pos + strlen("execute"));
		name_start = start + pos;
		name_end = start + sqlparser_mysql_statement_token_end(start, pos, (size_t)(end - start));
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXECUTE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_mysql_skip_space(start, (size_t)(name_end - start));
		if (pos < (size_t)(end - start)) {
			if (!sqlparser_mysql_ascii_word_equal(start, pos, "using")) {
				return SQLPARSER_STATUS_OK;
			}
			value_start = start + sqlparser_mysql_skip_space(start, pos + strlen("using"));
			value_end = sqlparser_mysql_trim_right(value_start, end);
			if (value_start >= value_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXECUTE USING requires parameters");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
		}
		internal_name = SQLPARSER_INTERNAL_MYSQL_EXECUTE;
	} else if (sqlparser_mysql_ascii_word_equal(start, pos, "deallocate") ||
	           sqlparser_mysql_ascii_word_equal(start, pos, "drop")) {
		int is_drop;

		is_drop = sqlparser_mysql_ascii_word_equal(start, pos, "drop");
		pos += is_drop ? strlen("drop") : strlen("deallocate");
		pos = sqlparser_mysql_skip_space(start, pos);
		if (!sqlparser_mysql_ascii_word_equal(start, pos, "prepare")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_mysql_skip_space(start, pos + strlen("prepare"));
		name_start = start + pos;
		name_end = start + sqlparser_mysql_statement_token_end(start, pos, (size_t)(end - start));
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "DEALLOCATE PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_mysql_skip_space(start, (size_t)(name_end - start));
		if (pos < (size_t)(end - start)) {
			return SQLPARSER_STATUS_OK;
		}
		internal_name = is_drop ? SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE : SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE;
	} else {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		input_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_mysql_append_internal_set(
		&out,
		internal_name,
		name_start,
		name_end,
		value_start,
		value_end,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			break;
		}
		index++;
	}
	return index;
}

static size_t sqlparser_mysql_find_matching_paren(const char *sql, size_t open_pos, size_t end)
{
	size_t pos;
	int depth;

	if (sql == NULL || sql[open_pos] != '(') {
		return (size_t)-1;
	}

	depth = 0;
	for (pos = open_pos; pos < end && sql[pos] != '\0'; pos++) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth == 0) {
				return (size_t)-1;
			}
			depth--;
			if (depth == 0) {
				return pos;
			}
		}
	}
	return (size_t)-1;
}

static int sqlparser_mysql_find_create_table_body(
	const char *sql,
	const char *masked,
	size_t len,
	size_t *out_body_start,
	size_t *out_body_end)
{
	size_t pos;
	size_t open_pos;
	size_t close_pos;

	if (sql == NULL || masked == NULL || out_body_start == NULL || out_body_end == NULL) {
		return 0;
	}

	pos = sqlparser_mysql_skip_space(masked, 0U);
	if (!sqlparser_mysql_word_at(masked, pos, "create")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(masked, pos + strlen("create"));
	if (sqlparser_mysql_word_at(masked, pos, "temporary")) {
		pos = sqlparser_mysql_skip_space(masked, pos + strlen("temporary"));
	}
	if (!sqlparser_mysql_word_at(masked, pos, "table")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(masked, pos + strlen("table"));
	if (sqlparser_mysql_word_at(masked, pos, "if")) {
		pos = sqlparser_mysql_skip_space(masked, pos + strlen("if"));
		if (!sqlparser_mysql_word_at(masked, pos, "not")) {
			return 0;
		}
		pos = sqlparser_mysql_skip_space(masked, pos + strlen("not"));
		if (!sqlparser_mysql_word_at(masked, pos, "exists")) {
			return 0;
		}
		pos = sqlparser_mysql_skip_space(masked, pos + strlen("exists"));
	}

	open_pos = sqlparser_mysql_find_top_level_char_between(sql, '(', pos, len);
	if (open_pos == (size_t)-1) {
		return 0;
	}
	close_pos = sqlparser_mysql_find_matching_paren(sql, open_pos, len);
	if (close_pos == (size_t)-1) {
		return 0;
	}

	*out_body_start = open_pos + 1U;
	*out_body_end = close_pos;
	return 1;
}

static size_t sqlparser_mysql_identifier_token_end(const char *sql, size_t start, size_t end)
{
	size_t pos;
	size_t skipped;

	if (sql == NULL) {
		return start;
	}
	pos = start;
	if (pos >= end) {
		return pos;
	}
	skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
	if (skipped > pos) {
		return skipped <= end ? skipped : end;
	}
	while (pos < end && sql[pos] != '\0' &&
	       !isspace((unsigned char)sql[pos]) &&
	       sql[pos] != ',' &&
	       sql[pos] != '(' &&
	       sql[pos] != ')') {
		pos++;
	}
	return pos;
}

static int sqlparser_mysql_create_table_segment_is_column(
	const char *masked,
	size_t segment_start,
	size_t segment_end)
{
	size_t pos;

	if (masked == NULL) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(masked, segment_start);
	if (pos >= segment_end) {
		return 0;
	}
	if (masked[pos] == '"' || masked[pos] == '`') {
		return 1;
	}
	if (!sqlparser_mysql_is_ident_char((unsigned char)masked[pos])) {
		return 0;
	}
	if (sqlparser_mysql_word_at(masked, pos, "primary") ||
	    sqlparser_mysql_word_at(masked, pos, "unique") ||
	    sqlparser_mysql_word_at(masked, pos, "key") ||
	    sqlparser_mysql_word_at(masked, pos, "index") ||
	    sqlparser_mysql_word_at(masked, pos, "constraint") ||
	    sqlparser_mysql_word_at(masked, pos, "foreign") ||
	    sqlparser_mysql_word_at(masked, pos, "check") ||
	    sqlparser_mysql_word_at(masked, pos, "fulltext") ||
	    sqlparser_mysql_word_at(masked, pos, "spatial")) {
		return 0;
	}
	return 1;
}

static size_t sqlparser_mysql_parse_option_value_end(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t end)
{
	size_t skipped;
	size_t start;

	pos = sqlparser_mysql_skip_space(masked, pos);
	if (pos < end && masked[pos] == '=') {
		pos = sqlparser_mysql_skip_space(masked, pos + 1U);
	}
	if (pos >= end) {
		return (size_t)-1;
	}

	skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
	if (skipped > pos) {
		return skipped <= end ? skipped : (size_t)-1;
	}
	if (sql[pos] == '(') {
		skipped = sqlparser_mysql_find_matching_paren(sql, pos, end);
		return skipped == (size_t)-1 ? (size_t)-1 : skipped + 1U;
	}

	start = pos;
	while (pos < end &&
	       !isspace((unsigned char)masked[pos]) &&
	       masked[pos] != ',') {
		pos++;
	}
	return pos > start ? pos : (size_t)-1;
}

static size_t sqlparser_mysql_create_table_column_keyword_value_end(
	const char *sql,
	const char *masked,
	size_t keyword_end,
	size_t segment_end)
{
	return sqlparser_mysql_parse_option_value_end(sql, masked, keyword_end, segment_end);
}

static size_t sqlparser_mysql_create_table_attribute_end(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t segment_end)
{
	size_t next;

	if (sqlparser_mysql_word_at(masked, pos, "auto_increment")) {
		return pos + strlen("auto_increment");
	}
	if (sqlparser_mysql_word_at(masked, pos, "unsigned")) {
		return pos + strlen("unsigned");
	}
	if (sqlparser_mysql_word_at(masked, pos, "zerofill")) {
		return pos + strlen("zerofill");
	}
	if (sqlparser_mysql_word_at(masked, pos, "visible")) {
		return pos + strlen("visible");
	}
	if (sqlparser_mysql_word_at(masked, pos, "invisible")) {
		return pos + strlen("invisible");
	}
	if (sqlparser_mysql_word_at(masked, pos, "comment")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("comment"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "collate")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("collate"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "charset")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("charset"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "character")) {
		next = sqlparser_mysql_skip_space(masked, pos + strlen("character"));
		if (!sqlparser_mysql_word_at(masked, next, "set")) {
			return 0U;
		}
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			next + strlen("set"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "column_format")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("column_format"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "engine_attribute")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("engine_attribute"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "secondary_engine_attribute")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("secondary_engine_attribute"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "storage")) {
		return sqlparser_mysql_create_table_column_keyword_value_end(
			sql,
			masked,
			pos + strlen("storage"),
			segment_end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "unique")) {
		next = sqlparser_mysql_skip_space(masked, pos + strlen("unique"));
		if (sqlparser_mysql_word_at(masked, next, "key")) {
			return next + strlen("key");
		}
		return pos + strlen("unique");
	}
	if (sqlparser_mysql_word_at(masked, pos, "primary")) {
		next = sqlparser_mysql_skip_space(masked, pos + strlen("primary"));
		if (sqlparser_mysql_word_at(masked, next, "key")) {
			return next + strlen("key");
		}
		return 0U;
	}
	if (sqlparser_mysql_word_at(masked, pos, "key")) {
		return pos + strlen("key");
	}
	if (sqlparser_mysql_word_at(masked, pos, "on")) {
		next = sqlparser_mysql_skip_space(masked, pos + strlen("on"));
		if (sqlparser_mysql_word_at(masked, next, "update")) {
			return segment_end;
		}
		return 0U;
	}
	if (sqlparser_mysql_word_at(masked, pos, "generated") ||
	    sqlparser_mysql_word_at(masked, pos, "as") ||
	    sqlparser_mysql_word_at(masked, pos, "references") ||
	    sqlparser_mysql_word_at(masked, pos, "check") ||
	    sqlparser_mysql_word_at(masked, pos, "constraint")) {
		return segment_end;
	}

	return 0U;
}

static sqlparser_status_t sqlparser_mysql_rewrite_create_column_segment(
	const char *sql,
	const char *masked,
	size_t segment_start,
	size_t segment_end,
	size_t statement_index,
	size_t *column_ordinal,
	sqlparser_mysql_state_t *state,
	sqlparser_mysql_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t stripped;
	sqlparser_mysql_origin_trace_t stripped_origin;
	const char *trimmed_start;
	const char *trimmed_end;
	size_t token_start;
	size_t token_end;
	size_t copy_start;
	size_t pos;
	size_t ordinal;
	int depth;
	int removed;
	sqlparser_status_t status;

	if (!sqlparser_mysql_create_table_segment_is_column(masked, segment_start, segment_end)) {
		return sqlparser_mysql_buffer_append_mem(
			out,
			sql + segment_start,
			segment_end - segment_start,
			out_error);
	}

	ordinal = *column_ordinal;
	(*column_ordinal)++;
	token_start = sqlparser_mysql_skip_space(masked, segment_start);
	token_end = sqlparser_mysql_identifier_token_end(sql, token_start, segment_end);
	copy_start = segment_start;
	pos = segment_start;
	depth = 0;
	removed = 0;
	memset(&stripped, 0, sizeof(stripped));
	memset(&stripped_origin, 0, sizeof(stripped_origin));
	status = sqlparser_mysql_buffer_begin_origin(
		&stripped,
		out != NULL && out->origin_trace != NULL ? &stripped_origin : NULL,
		sql,
		out != NULL ? out->origin_input_base : 0U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	while (pos < segment_end && sql[pos] != '\0') {
		size_t skipped;
		size_t attr_end;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			pos++;
			continue;
		}
		attr_end = depth == 0 && pos >= token_end ?
			sqlparser_mysql_create_table_attribute_end(sql, masked, pos, segment_end) :
			0U;
		if (attr_end != (size_t)-1 && attr_end > pos) {
			status = sqlparser_mysql_buffer_append_mem(
				&stripped,
				sql + copy_start,
				pos - copy_start,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&stripped_origin);
				sqlparser_mysql_buffer_release(&stripped);
				return status;
			}
			copy_start = attr_end;
			pos = copy_start;
			removed = 1;
			continue;
		}
		pos++;
	}

	if (!removed) {
		sqlparser_mysql_origin_trace_release(&stripped_origin);
		sqlparser_mysql_buffer_release(&stripped);
		return sqlparser_mysql_buffer_append_mem(
			out,
			sql + segment_start,
			segment_end - segment_start,
			out_error);
	}

	status = sqlparser_mysql_buffer_append_mem(
		&stripped,
		sql + copy_start,
		segment_end - copy_start,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_origin_trace_release(&stripped_origin);
		sqlparser_mysql_buffer_release(&stripped);
		return status;
	}

	trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + segment_end);
	trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + segment_end);
	status = sqlparser_mysql_state_add_create_column_restore(
		state,
		statement_index,
		ordinal,
		trimmed_start,
		(size_t)(trimmed_end - trimmed_start),
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_trace_mem(
			out,
			stripped.data,
			stripped.len,
			&stripped_origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&stripped_origin);
	sqlparser_mysql_buffer_release(&stripped);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*out_rewritten = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_tail_is_balanced(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	if (sql == NULL) {
		return 0;
	}
	depth = 0;
	for (pos = start; pos < end && sql[pos] != '\0'; pos++) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			if (skipped > end) {
				return 0;
			}
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth == 0) {
				return 0;
			}
			depth--;
			continue;
		}
	}
	return depth == 0;
}

static size_t sqlparser_mysql_parse_table_option_value_end(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t end)
{
	return sqlparser_mysql_parse_option_value_end(sql, masked, pos, end);
}

static size_t sqlparser_mysql_parse_create_table_data_directory_option(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t end,
	const char *keyword)
{
	pos = sqlparser_mysql_skip_space(masked, pos + strlen(keyword));
	if (!sqlparser_mysql_word_at(masked, pos, "directory")) {
		return (size_t)-1;
	}
	return sqlparser_mysql_parse_table_option_value_end(
		sql,
		masked,
		pos + strlen("directory"),
		end);
}

static size_t sqlparser_mysql_parse_create_table_tablespace_option(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t end)
{
	pos = sqlparser_mysql_parse_table_option_value_end(
		sql,
		masked,
		pos + strlen("tablespace"),
		end);
	if (pos == (size_t)-1) {
		return (size_t)-1;
	}
	pos = sqlparser_mysql_skip_space(masked, pos);
	if (pos < end && sqlparser_mysql_word_at(masked, pos, "storage")) {
		pos = sqlparser_mysql_parse_table_option_value_end(
			sql,
			masked,
			pos + strlen("storage"),
			end);
	}
	return pos;
}

static size_t sqlparser_mysql_parse_create_table_storage_option(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t end)
{
	return sqlparser_mysql_parse_table_option_value_end(
		sql,
		masked,
		pos + strlen("storage"),
		end);
}

static size_t sqlparser_mysql_parse_create_table_start_option(
	const char *masked,
	size_t pos,
	size_t end)
{
	pos = sqlparser_mysql_skip_space(masked, pos + strlen("start"));
	if (!sqlparser_mysql_word_at(masked, pos, "transaction")) {
		return (size_t)-1;
	}
	pos += strlen("transaction");
	return pos <= end ? pos : (size_t)-1;
}

static size_t sqlparser_mysql_parse_create_table_table_option(
	const char *sql,
	const char *masked,
	size_t pos,
	size_t end)
{
	int has_default;

	has_default = 0;
	if (sqlparser_mysql_word_at(masked, pos, "default")) {
		has_default = 1;
		pos = sqlparser_mysql_skip_space(masked, pos + strlen("default"));
	}

	if (sqlparser_mysql_word_at(masked, pos, "character")) {
		pos = sqlparser_mysql_skip_space(masked, pos + strlen("character"));
		if (!sqlparser_mysql_word_at(masked, pos, "set")) {
			return (size_t)-1;
		}
		return sqlparser_mysql_parse_table_option_value_end(
			sql,
			masked,
			pos + strlen("set"),
			end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "charset")) {
		return sqlparser_mysql_parse_table_option_value_end(
			sql,
			masked,
			pos + strlen("charset"),
			end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "collate")) {
		return sqlparser_mysql_parse_table_option_value_end(
			sql,
			masked,
			pos + strlen("collate"),
			end);
	}
	if (has_default) {
		return (size_t)-1;
	}
	if (sqlparser_mysql_word_at(masked, pos, "data")) {
		return sqlparser_mysql_parse_create_table_data_directory_option(
			sql,
			masked,
			pos,
			end,
			"data");
	}
	if (sqlparser_mysql_word_at(masked, pos, "index")) {
		return sqlparser_mysql_parse_create_table_data_directory_option(
			sql,
			masked,
			pos,
			end,
			"index");
	}
	if (sqlparser_mysql_word_at(masked, pos, "tablespace")) {
		return sqlparser_mysql_parse_create_table_tablespace_option(sql, masked, pos, end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "storage")) {
		return sqlparser_mysql_parse_create_table_storage_option(sql, masked, pos, end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "start")) {
		return sqlparser_mysql_parse_create_table_start_option(masked, pos, end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "autoextend_size")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("autoextend_size"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "auto_increment")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("auto_increment"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "avg_row_length")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("avg_row_length"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "checksum")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("checksum"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "comment")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("comment"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "compression")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("compression"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "connection")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("connection"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "delay_key_write")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("delay_key_write"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "encryption")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("encryption"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "engine")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("engine"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "engine_attribute")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("engine_attribute"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "insert_method")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("insert_method"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "key_block_size")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("key_block_size"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "max_rows")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("max_rows"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "min_rows")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("min_rows"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "pack_keys")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("pack_keys"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "password")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("password"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "row_format")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("row_format"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "secondary_engine_attribute")) {
		return sqlparser_mysql_parse_table_option_value_end(
			sql,
			masked,
			pos + strlen("secondary_engine_attribute"),
			end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "stats_auto_recalc")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("stats_auto_recalc"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "stats_persistent")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("stats_persistent"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "stats_sample_pages")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("stats_sample_pages"), end);
	}
	if (sqlparser_mysql_word_at(masked, pos, "union")) {
		return sqlparser_mysql_parse_table_option_value_end(sql, masked, pos + strlen("union"), end);
	}

	return (size_t)-1;
}

static int sqlparser_mysql_tail_has_query_expression(
	const char *masked,
	size_t start,
	size_t end)
{
	size_t pos;

	pos = start;
	while (pos < end) {
		pos = sqlparser_mysql_skip_space(masked, pos);
		if (pos >= end) {
			return 0;
		}
		if (sqlparser_mysql_word_at(masked, pos, "select") ||
		    sqlparser_mysql_word_at(masked, pos, "as") ||
		    sqlparser_mysql_word_at(masked, pos, "ignore") ||
		    sqlparser_mysql_word_at(masked, pos, "replace")) {
			return 1;
		}
		pos++;
	}
	return 0;
}

static int sqlparser_mysql_parse_create_table_options(
	const char *sql,
	const char *masked,
	size_t start,
	size_t end)
{
	size_t pos;
	int saw_option;

	if (sql == NULL || masked == NULL) {
		return 0;
	}
	if (!sqlparser_mysql_tail_is_balanced(sql, start, end) ||
	    sqlparser_mysql_tail_has_query_expression(masked, start, end)) {
		return 0;
	}

	pos = start;
	saw_option = 0;
	while (pos < end) {
		pos = sqlparser_mysql_skip_space(masked, pos);
		if (pos < end && masked[pos] == ',') {
			pos++;
			continue;
		}
		if (pos >= end) {
			break;
		}

		if (sqlparser_mysql_word_at(masked, pos, "partition")) {
			return saw_option || sqlparser_mysql_word_at(masked, pos, "partition");
		}
		pos = sqlparser_mysql_parse_create_table_table_option(sql, masked, pos, end);
		if (pos == (size_t)-1) {
			return 0;
		}
		saw_option = 1;
	}

	return saw_option;
}

static sqlparser_status_t sqlparser_mysql_rewrite_create_table_statement_extensions(
	const char *statement_sql,
	size_t statement_index,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t columns;
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t columns_origin;
	char *masked;
	const char *tail_start;
	const char *tail_end;
	sqlparser_status_t status;
	size_t len;
	size_t body_start;
	size_t body_end;
	size_t segment_start;
	size_t column_ordinal;
	int columns_rewritten;
	int options_rewritten;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (statement_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	len = strlen(statement_sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!sqlparser_mysql_find_create_table_body(statement_sql, masked, len, &body_start, &body_end)) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}

	memset(&columns, 0, sizeof(columns));
	memset(&columns_origin, 0, sizeof(columns_origin));
	status = sqlparser_mysql_buffer_begin_origin(
		&columns,
		origin_trace != NULL ? &columns_origin : NULL,
		statement_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(masked);
		return status;
	}
	columns_rewritten = 0;
	column_ordinal = 0U;
	segment_start = body_start;
	while (segment_start <= body_end) {
		size_t comma_pos;
		size_t segment_end;

		comma_pos = sqlparser_mysql_find_top_level_char_between(
			statement_sql,
			',',
			segment_start,
			body_end);
		segment_end = comma_pos == (size_t)-1 ? body_end : comma_pos;
		status = sqlparser_mysql_rewrite_create_column_segment(
			statement_sql,
			masked,
			segment_start,
			segment_end,
			statement_index,
			&column_ordinal,
			state,
			&columns,
			&columns_rewritten,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(masked);
			sqlparser_mysql_origin_trace_release(&columns_origin);
			sqlparser_mysql_buffer_release(&columns);
			return status;
		}
		if (comma_pos == (size_t)-1) {
			break;
		}
		status = sqlparser_mysql_buffer_append_char(&columns, ',', out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(masked);
			sqlparser_mysql_origin_trace_release(&columns_origin);
			sqlparser_mysql_buffer_release(&columns);
			return status;
		}
		segment_start = comma_pos + 1U;
	}

	tail_start = sqlparser_mysql_trim_left(statement_sql + body_end + 1U, statement_sql + len);
	tail_end = sqlparser_mysql_trim_right(tail_start, statement_sql + len);
	options_rewritten = 0;
	if (tail_start < tail_end &&
	    sqlparser_mysql_parse_create_table_options(
		    statement_sql,
		    masked,
		    (size_t)(tail_start - statement_sql),
		    (size_t)(tail_end - statement_sql))) {
		status = sqlparser_mysql_state_add_create_table_restore(
			state,
			statement_index,
			tail_start,
			(size_t)(tail_end - tail_start),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(masked);
			sqlparser_mysql_origin_trace_release(&columns_origin);
			sqlparser_mysql_buffer_release(&columns);
			return status;
		}
		options_rewritten = 1;
	}

	if (!columns_rewritten && !options_rewritten) {
		free(masked);
		sqlparser_mysql_origin_trace_release(&columns_origin);
		sqlparser_mysql_buffer_release(&columns);
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(masked);
		sqlparser_mysql_origin_trace_release(&columns_origin);
		sqlparser_mysql_buffer_release(&columns);
		return status;
	}
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, body_start, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_trace_mem(
			&out,
			columns.data,
			columns.len,
			&columns_origin,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		if (options_rewritten) {
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				statement_sql + body_end,
				(size_t)(tail_start - (statement_sql + body_end)),
				out_error);
		} else {
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				statement_sql + body_end,
				len - body_end,
				out_error);
		}
	}
	free(masked);
	sqlparser_mysql_origin_trace_release(&columns_origin);
	sqlparser_mysql_buffer_release(&columns);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_create_table_extensions(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	memset(&origin, 0, sizeof(origin));
	while (segment_start < len) {
		sqlparser_mysql_origin_trace_t statement_origin;
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;
		char *statement_sql;
		char *rewritten_sql;
		size_t current_statement_index;

		memset(&statement_origin, 0, sizeof(statement_origin));
		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		current_statement_index = statement_index;
		if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_origin_trace_release(&origin);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_mysql_rewrite_create_table_statement_extensions(
			statement_sql,
			current_statement_index,
			state,
			&rewritten_sql,
			segment_start,
			origins != NULL ? &statement_origin : NULL,
			out_error);
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_mysql_origin_trace_release(&statement_origin);
			sqlparser_mysql_origin_trace_release(&origin);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origins != NULL ? &origin : NULL,
					sql,
					0U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					sqlparser_mysql_origin_trace_release(
						&statement_origin);
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					sqlparser_mysql_origin_trace_release(
						&statement_origin);
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				sql + copy_start,
				segment_start - copy_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_trace_cstr(
					&out,
					rewritten_sql,
					&statement_origin,
					out_error);
			}
			free(rewritten_sql);
			sqlparser_mysql_origin_trace_release(&statement_origin);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		} else {
			sqlparser_mysql_origin_trace_release(&statement_origin);
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_origin_trace_release(&origin);
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	status = sqlparser_mysql_origin_trace_commit(
		origins,
		sql,
		out.data,
		&origin,
		out_error);
	sqlparser_mysql_origin_trace_release(&origin);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_session_statements(
	char **io_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	char *statement_sql;
	char *rewritten_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	memset(&origin, 0, sizeof(origin));
	while (segment_start < len) {
		sqlparser_mysql_origin_trace_t statement_origin;

		memset(&statement_origin, 0, sizeof(statement_origin));
		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		leading_end = sqlparser_mysql_skip_leading_trivia(
			sql, segment_start, statement_end);
		statement_sql = sqlparser_strndup(
			sql + leading_end, statement_end - leading_end);
		if (statement_sql == NULL) {
			sqlparser_mysql_origin_trace_release(&origin);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_mysql_preprocess_use_statement(
			statement_sql,
			&rewritten_sql,
			leading_end,
			origins != NULL ? &statement_origin : NULL,
			out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_mysql_preprocess_session_statement(
				statement_sql,
				&rewritten_sql,
				leading_end,
				origins != NULL ? &statement_origin : NULL,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_mysql_preprocess_prepared_statement(
				statement_sql,
				&rewritten_sql,
				leading_end,
				origins != NULL ? &statement_origin : NULL,
				out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_mysql_origin_trace_release(&statement_origin);
			sqlparser_mysql_origin_trace_release(&origin);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origins != NULL ? &origin : NULL,
					sql,
					0U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					sqlparser_mysql_origin_trace_release(
						&statement_origin);
					sqlparser_mysql_origin_trace_release(&origin);
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					sqlparser_mysql_origin_trace_release(
						&statement_origin);
					sqlparser_mysql_origin_trace_release(&origin);
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_trace_cstr(
					&out,
					rewritten_sql,
					&statement_origin,
					out_error);
			}
			free(rewritten_sql);
			sqlparser_mysql_origin_trace_release(&statement_origin);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		} else {
			sqlparser_mysql_origin_trace_release(&statement_origin);
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_origin_trace_release(&origin);
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	status = sqlparser_mysql_origin_trace_commit(
		origins,
		sql,
		out.data,
		&origin,
		out_error);
	sqlparser_mysql_origin_trace_release(&origin);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_simple_limit_operand(const char *start, const char *end)
{
	const char *pos;

	while (start < end && isspace((unsigned char)*start)) {
		start++;
	}
	while (end > start && isspace((unsigned char)*(end - 1))) {
		end--;
	}
	if (start >= end) {
		return 0;
	}

	if (*start == '?') {
		return start + 1 == end;
	}

	if (*start == '$') {
		pos = start + 1;
		if (pos >= end || !isdigit((unsigned char)*pos)) {
			return 0;
		}
		while (pos < end && isdigit((unsigned char)*pos)) {
			pos++;
		}
		return pos == end;
	}

	pos = start;
	while (pos < end && isdigit((unsigned char)*pos)) {
		pos++;
	}

	return pos == end;
}

static sqlparser_status_t sqlparser_mysql_rewrite_limit_offset_count(
	char **io_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	sqlparser_status_t status;
	size_t index;
	size_t copy_start;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&origin, 0, sizeof(origin));
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}

		if (sqlparser_mysql_ascii_word_equal(sql, index, "limit")) {
			size_t first_start_pos;
			size_t comma_pos;
			size_t second_start_pos;
			size_t second_end_pos;
			const char *first_start;
			const char *first_end;
			const char *second_start;
			const char *second_end;

			first_start_pos = index + 5U;
			while (isspace((unsigned char)sql[first_start_pos])) {
				first_start_pos++;
			}
			comma_pos = first_start_pos;
			while (sql[comma_pos] != '\0' && sql[comma_pos] != ',' && sql[comma_pos] != ';') {
				comma_pos++;
			}
			if (sql[comma_pos] != ',') {
				index++;
				continue;
			}
			second_start_pos = comma_pos + 1U;
			second_end_pos = second_start_pos;
			while (sql[second_end_pos] != '\0' && sql[second_end_pos] != ';') {
				second_end_pos++;
			}

			first_start = sqlparser_mysql_trim_left(sql + first_start_pos, sql + comma_pos);
			first_end = sqlparser_mysql_trim_right(first_start, sql + comma_pos);
			second_start = sqlparser_mysql_trim_left(sql + second_start_pos, sql + second_end_pos);
			second_end = sqlparser_mysql_trim_right(second_start, sql + second_end_pos);
			if (!sqlparser_mysql_simple_limit_operand(first_start, first_end) ||
			    !sqlparser_mysql_simple_limit_operand(second_start, second_end)) {
				index++;
				continue;
			}

			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origins != NULL ? &origin : NULL,
					sql,
					0U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "LIMIT ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					second_start,
					(size_t)(second_end - second_start),
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, " OFFSET ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					first_start,
					(size_t)(first_end - first_start),
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index = second_end_pos;
			copy_start = index;
			continue;
		}

		index++;
	}

	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			sql,
			out.data,
			&origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&origin);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_find_on_duplicate_key_update(
	const char *masked,
	size_t start,
	size_t end,
	size_t *out_tail_start)
{
	size_t pos;
	size_t next;

	if (out_tail_start != NULL) {
		*out_tail_start = 0U;
	}
	pos = start;
	while (pos < end) {
		pos = sqlparser_mysql_find_top_level_word_between(masked, "on", pos, end);
		if (pos == (size_t)-1) {
			return 0;
		}
		next = sqlparser_mysql_skip_space(masked, pos + strlen("on"));
		if (next < end && sqlparser_mysql_word_at(masked, next, "duplicate")) {
			next = sqlparser_mysql_skip_space(masked, next + strlen("duplicate"));
			if (next < end && sqlparser_mysql_word_at(masked, next, "key")) {
				next = sqlparser_mysql_skip_space(masked, next + strlen("key"));
				if (next < end && sqlparser_mysql_word_at(masked, next, "update")) {
					if (out_tail_start != NULL) {
						*out_tail_start = next + strlen("update");
					}
					return 1;
				}
			}
		}
		pos += strlen("on");
	}
	return 0;
}

static int sqlparser_mysql_extract_alias_span(
	const char *start,
	const char *end,
	const char **out_alias_start,
	const char **out_alias_end)
{
	const char *alias_start;

	if (out_alias_start != NULL) {
		*out_alias_start = NULL;
	}
	if (out_alias_end != NULL) {
		*out_alias_end = NULL;
	}
	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (start >= end) {
		return 0;
	}
	alias_start = end;
	while (alias_start > start && !isspace((unsigned char)*(alias_start - 1))) {
		alias_start--;
	}
	if (alias_start <= start) {
		return 0;
	}
	if (out_alias_start != NULL) {
		*out_alias_start = alias_start;
	}
	if (out_alias_end != NULL) {
		*out_alias_end = end;
	}
	return 1;
}

static int sqlparser_mysql_span_equal(
	const char *left_start,
	const char *left_end,
	const char *right_start,
	const char *right_end)
{
	size_t left_len;
	size_t right_len;

	if (left_start == NULL || left_end == NULL || right_start == NULL || right_end == NULL) {
		return 0;
	}
	left_len = (size_t)(left_end - left_start);
	right_len = (size_t)(right_end - right_start);
	return left_len == right_len && strncmp(left_start, right_start, left_len) == 0;
}

static int sqlparser_mysql_extract_relation_name_span(
	const char *relation_start,
	const char *relation_end,
	const char *alias_start,
	const char **out_name_start,
	const char **out_name_end)
{
	const char *name_start;
	const char *name_end;
	size_t search_start;
	size_t relation_len;
	size_t last_dot;

	if (out_name_start != NULL) {
		*out_name_start = NULL;
	}
	if (out_name_end != NULL) {
		*out_name_end = NULL;
	}
	if (relation_start == NULL || relation_end == NULL || relation_start >= relation_end) {
		return 0;
	}
	name_start = sqlparser_mysql_trim_left(relation_start, relation_end);
	name_end = alias_start != NULL && alias_start > name_start ? alias_start : relation_end;
	name_end = sqlparser_mysql_trim_right(name_start, name_end);
	if (name_start >= name_end) {
		return 0;
	}
	relation_len = (size_t)(name_end - name_start);
	search_start = 0U;
	last_dot = (size_t)-1;
	while (search_start < relation_len) {
		size_t dot_offset;

		dot_offset = sqlparser_mysql_find_top_level_char_between(name_start, '.', search_start, relation_len);
		if (dot_offset == (size_t)-1) {
			break;
		}
		last_dot = dot_offset;
		search_start = dot_offset + 1U;
	}
	if (last_dot != (size_t)-1) {
		name_start = sqlparser_mysql_trim_left(name_start + last_dot + 1U, name_end);
		name_end = sqlparser_mysql_trim_right(name_start, name_end);
	}
	if (name_start >= name_end) {
		return 0;
	}
	if (out_name_start != NULL) {
		*out_name_start = name_start;
	}
	if (out_name_end != NULL) {
		*out_name_end = name_end;
	}
	return 1;
}

static int sqlparser_mysql_span_matches_relation_qualifier(
	const char *qualifier_start,
	const char *qualifier_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end)
{
	if (qualifier_start == NULL || qualifier_end == NULL || qualifier_start >= qualifier_end) {
		return 0;
	}
	if (alias_start != NULL &&
	    alias_end != NULL &&
	    sqlparser_mysql_span_equal(qualifier_start, qualifier_end, alias_start, alias_end)) {
		return 1;
	}
	return relation_name_start != NULL &&
		relation_name_end != NULL &&
		sqlparser_mysql_span_equal(qualifier_start, qualifier_end, relation_name_start, relation_name_end);
}

static int sqlparser_mysql_span_ci_equals_cstr(const char *start, const char *end, const char *word)
{
	size_t index;
	size_t len;

	if (start == NULL || end == NULL || word == NULL) {
		return 0;
	}
	len = strlen(word);
	if ((size_t)(end - start) != len) {
		return 0;
	}
	for (index = 0U; index < len; index++) {
		if (tolower((unsigned char)start[index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_mysql_previous_token_span(
	const char *start,
	const char *end,
	const char **out_token_start,
	const char **out_token_end)
{
	const char *token_end;
	const char *token_start;

	if (out_token_start != NULL) {
		*out_token_start = NULL;
	}
	if (out_token_end != NULL) {
		*out_token_end = NULL;
	}
	if (start == NULL || end == NULL || start >= end) {
		return 0;
	}
	token_end = sqlparser_mysql_trim_right(start, end);
	if (token_end <= start) {
		return 0;
	}
	token_start = token_end;
	while (token_start > start && sqlparser_mysql_is_ident_char((unsigned char)*(token_start - 1))) {
		token_start--;
	}
	if (token_start == token_end) {
		return 0;
	}
	if (out_token_start != NULL) {
		*out_token_start = token_start;
	}
	if (out_token_end != NULL) {
		*out_token_end = token_end;
	}
	return 1;
}

static sqlparser_mysql_join_kind_t sqlparser_mysql_join_kind_for_token(
	const char *token_start,
	const char *token_end)
{
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "left")) {
		return SQLPARSER_MYSQL_JOIN_LEFT;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "right")) {
		return SQLPARSER_MYSQL_JOIN_RIGHT;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "cross")) {
		return SQLPARSER_MYSQL_JOIN_CROSS;
	}
	return SQLPARSER_MYSQL_JOIN_INNER;
}

static sqlparser_mysql_join_kind_t sqlparser_mysql_invert_join_kind(sqlparser_mysql_join_kind_t kind)
{
	if (kind == SQLPARSER_MYSQL_JOIN_LEFT) {
		return SQLPARSER_MYSQL_JOIN_RIGHT;
	}
	if (kind == SQLPARSER_MYSQL_JOIN_RIGHT) {
		return SQLPARSER_MYSQL_JOIN_LEFT;
	}
	return kind;
}

static const char *sqlparser_mysql_join_on_func_name(sqlparser_mysql_join_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_MYSQL_JOIN_LEFT:
			return SQLPARSER_INTERNAL_MYSQL_LEFT_JOIN_ON;
		case SQLPARSER_MYSQL_JOIN_RIGHT:
			return SQLPARSER_INTERNAL_MYSQL_RIGHT_JOIN_ON;
		case SQLPARSER_MYSQL_JOIN_CROSS:
			return SQLPARSER_INTERNAL_MYSQL_CROSS_JOIN_ON;
		case SQLPARSER_MYSQL_JOIN_INNER:
		default:
			return SQLPARSER_INTERNAL_MYSQL_JOIN_ON;
	}
}

static const char *sqlparser_mysql_join_keyword(sqlparser_mysql_join_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_MYSQL_JOIN_LEFT:
			return " LEFT JOIN ";
		case SQLPARSER_MYSQL_JOIN_RIGHT:
			return " RIGHT JOIN ";
		case SQLPARSER_MYSQL_JOIN_CROSS:
			return " CROSS JOIN ";
		case SQLPARSER_MYSQL_JOIN_INNER:
		default:
			return " JOIN ";
	}
}

static void sqlparser_mysql_normalize_join_target_end(
	const char *start,
	const char **io_end,
	sqlparser_mysql_join_kind_t *out_kind)
{
	const char *token_start;
	const char *token_end;

	if (out_kind != NULL) {
		*out_kind = SQLPARSER_MYSQL_JOIN_INNER;
	}
	if (io_end == NULL || *io_end == NULL) {
		return;
	}
	if (!sqlparser_mysql_previous_token_span(start, *io_end, &token_start, &token_end)) {
		return;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "inner") ||
	    sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "cross")) {
		if (out_kind != NULL) {
			*out_kind = sqlparser_mysql_join_kind_for_token(token_start, token_end);
		}
		*io_end = sqlparser_mysql_trim_right(start, token_start);
		return;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "left") ||
	    sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "right")) {
		if (out_kind != NULL) {
			*out_kind = sqlparser_mysql_join_kind_for_token(token_start, token_end);
		}
		*io_end = sqlparser_mysql_trim_right(start, token_start);
		return;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "outer")) {
		const char *prev_start;
		const char *prev_end;

		if (sqlparser_mysql_previous_token_span(start, token_start, &prev_start, &prev_end) &&
		    (sqlparser_mysql_span_ci_equals_cstr(prev_start, prev_end, "left") ||
		     sqlparser_mysql_span_ci_equals_cstr(prev_start, prev_end, "right"))) {
			if (out_kind != NULL) {
				*out_kind = sqlparser_mysql_join_kind_for_token(prev_start, prev_end);
			}
			*io_end = sqlparser_mysql_trim_right(start, prev_start);
			return;
		}
	}
}

static const char *sqlparser_mysql_strip_assignment_qualifier(
	const char *left_start,
	const char *left_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end)
{
	size_t dot_offset;
	const char *qualifier_start;
	const char *qualifier_end;

	if (left_start == NULL || left_end == NULL || left_start >= left_end) {
		return left_start;
	}
	dot_offset = sqlparser_mysql_find_top_level_char_between(
		left_start,
		'.',
		0U,
		(size_t)(left_end - left_start));
	if (dot_offset == (size_t)-1) {
		return left_start;
	}
	qualifier_start = sqlparser_mysql_trim_left(left_start, left_start + dot_offset);
	qualifier_end = sqlparser_mysql_trim_right(qualifier_start, left_start + dot_offset);
	if (!sqlparser_mysql_span_matches_relation_qualifier(
		    qualifier_start,
		    qualifier_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return left_start;
	}
	return sqlparser_mysql_trim_left(left_start + dot_offset + 1U, left_end);
}

static int sqlparser_mysql_assignment_target_side(
	const char *left_start,
	const char *left_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	const char *source_alias_start,
	const char *source_alias_end,
	const char *source_relation_name_start,
	const char *source_relation_name_end)
{
	size_t dot_offset;
	const char *qualifier_start;
	const char *qualifier_end;

	if (left_start == NULL || left_end == NULL || left_start >= left_end) {
		return 0;
	}
	dot_offset = sqlparser_mysql_find_top_level_char_between(
		left_start,
		'.',
		0U,
		(size_t)(left_end - left_start));
	if (dot_offset == (size_t)-1) {
		return 0;
	}
	qualifier_start = sqlparser_mysql_trim_left(left_start, left_start + dot_offset);
	qualifier_end = sqlparser_mysql_trim_right(qualifier_start, left_start + dot_offset);
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    qualifier_start,
		    qualifier_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return 1;
	}
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    qualifier_start,
		    qualifier_end,
		    source_alias_start,
		    source_alias_end,
		    source_relation_name_start,
		    source_relation_name_end)) {
		return 2;
	}
	return -1;
}

static sqlparser_status_t sqlparser_mysql_select_update_join_target(
	const char *assign_start,
	const char *assign_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	const char *source_alias_start,
	const char *source_alias_end,
	const char *source_relation_name_start,
	const char *source_relation_name_end,
	int *out_use_source_target,
	sqlparser_error_t *out_error)
{
	const char *segment_start;
	int selected_side;

	if (out_use_source_target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "update target output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_use_source_target = 0;
	segment_start = assign_start;
	selected_side = 0;
	while (segment_start < assign_end) {
		size_t segment_offset;
		size_t eq_offset;
		const char *segment_end;
		const char *left_start;
		const char *left_end;
		int side;

		segment_offset = sqlparser_mysql_find_top_level_char_between(
			assign_start,
			',',
			(size_t)(segment_start - assign_start),
			(size_t)(assign_end - assign_start));
		segment_end = segment_offset == (size_t)-1 ? assign_end : assign_start + segment_offset;
		eq_offset = sqlparser_mysql_find_top_level_char_between(
			segment_start,
			'=',
			0U,
			(size_t)(segment_end - segment_start));
		left_start = sqlparser_mysql_trim_left(segment_start, eq_offset == (size_t)-1 ? segment_end : segment_start + eq_offset);
		left_end = sqlparser_mysql_trim_right(left_start, eq_offset == (size_t)-1 ? segment_end : segment_start + eq_offset);
		side = sqlparser_mysql_assignment_target_side(
			left_start,
			left_end,
			alias_start,
			alias_end,
			relation_name_start,
			relation_name_end,
			source_alias_start,
			source_alias_end,
			source_relation_name_start,
			source_relation_name_end);
		if (side < 0) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"unsupported MySQL syntax: update join assignment target");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		if (side != 0) {
			if (selected_side != 0 && selected_side != side) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"unsupported MySQL syntax: update join multiple assignment targets");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			selected_side = side;
		}
		segment_start = segment_offset == (size_t)-1 ? assign_end : segment_end + 1;
	}
	*out_use_source_target = selected_side == 2;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_validate_update_join_assignment_target(
	const char *left_start,
	const char *left_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	sqlparser_error_t *out_error)
{
	size_t dot_offset;
	const char *qualifier_start;
	const char *qualifier_end;

	if (left_start == NULL || left_end == NULL || left_start >= left_end) {
		return SQLPARSER_STATUS_OK;
	}
	dot_offset = sqlparser_mysql_find_top_level_char_between(
		left_start,
		'.',
		0U,
		(size_t)(left_end - left_start));
	if (dot_offset == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	qualifier_start = sqlparser_mysql_trim_left(left_start, left_start + dot_offset);
	qualifier_end = sqlparser_mysql_trim_right(qualifier_start, left_start + dot_offset);
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    qualifier_start,
		    qualifier_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return SQLPARSER_STATUS_OK;
	}
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_UNSUPPORTED,
		"unsupported MySQL syntax: update join assignment target");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_mysql_append_update_join_assignments(
	sqlparser_mysql_buffer_t *out,
	const char *assign_start,
	const char *assign_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	sqlparser_error_t *out_error)
{
	const char *segment_start;
	int first;

	segment_start = assign_start;
	first = 1;
	while (segment_start < assign_end) {
		size_t segment_offset;
		size_t eq_offset;
		const char *segment_end;
		const char *left_start;
		const char *left_end;
		const char *right_start;
		const char *right_end;
		sqlparser_status_t status;

		segment_offset = sqlparser_mysql_find_top_level_char_between(
			assign_start,
			',',
			(size_t)(segment_start - assign_start),
			(size_t)(assign_end - assign_start));
		segment_end = segment_offset == (size_t)-1 ? assign_end : assign_start + segment_offset;
		eq_offset = sqlparser_mysql_find_top_level_char_between(
			segment_start,
			'=',
			0U,
			(size_t)(segment_end - segment_start));
		if (eq_offset == (size_t)-1) {
			left_start = sqlparser_mysql_trim_left(segment_start, segment_end);
			left_end = sqlparser_mysql_trim_right(left_start, segment_end);
			right_start = NULL;
			right_end = NULL;
		} else {
			left_start = sqlparser_mysql_trim_left(segment_start, segment_start + eq_offset);
			left_end = sqlparser_mysql_trim_right(left_start, segment_start + eq_offset);
			right_start = sqlparser_mysql_trim_left(segment_start + eq_offset + 1U, segment_end);
			right_end = sqlparser_mysql_trim_right(right_start, segment_end);
			status = sqlparser_mysql_validate_update_join_assignment_target(
				left_start,
				left_end,
				alias_start,
				alias_end,
				relation_name_start,
				relation_name_end,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			left_start = sqlparser_mysql_strip_assignment_qualifier(
				left_start,
				left_end,
				alias_start,
				alias_end,
				relation_name_start,
				relation_name_end);
		}
		if (!first) {
			status = sqlparser_mysql_buffer_append_cstr(out, ", ", out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		first = 0;
		status = sqlparser_mysql_buffer_append_mem(out, left_start, (size_t)(left_end - left_start), out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (right_start != NULL) {
			status = sqlparser_mysql_buffer_append_cstr(out, " = ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(out, right_start, (size_t)(right_end - right_start), out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		segment_start = segment_offset == (size_t)-1 ? assign_end : segment_end + 1;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_validate_delete_join_target(
	const char *delete_target_start,
	const char *delete_target_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	const char *source_alias_start,
	const char *source_alias_end,
	const char *source_relation_name_start,
	const char *source_relation_name_end,
	int *out_use_source_target,
	sqlparser_error_t *out_error)
{
	const char *target_start;
	const char *target_end;

	if (out_use_source_target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete target output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_use_source_target = 0;
	if (delete_target_start == NULL || delete_target_end == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	target_start = sqlparser_mysql_trim_left(delete_target_start, delete_target_end);
	target_end = sqlparser_mysql_trim_right(target_start, delete_target_end);
	if (target_start >= target_end) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_mysql_find_top_level_char_between(
		    target_start,
		    ',',
		    0U,
		    (size_t)(target_end - target_start)) != (size_t)-1) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported MySQL syntax: delete join multiple targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    target_start,
		    target_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    target_start,
		    target_end,
		    source_alias_start,
		    source_alias_end,
		    source_relation_name_start,
		    source_relation_name_end)) {
		*out_use_source_target = 1;
		return SQLPARSER_STATUS_OK;
	}
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_UNSUPPORTED,
		"unsupported MySQL syntax: delete join target");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_mysql_append_join_on_call(
	sqlparser_mysql_buffer_t *out,
	const char *join_start,
	const char *join_end,
	sqlparser_mysql_join_kind_t join_kind,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_generated_identifier(
		out,
		sqlparser_mysql_join_on_func_name(join_kind),
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_char(out, '(', out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(out, join_start, (size_t)(join_end - join_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_char(out, ')', out_error);
	}
	return status;
}

static int sqlparser_mysql_unwrap_join_on_call(
	const char **io_start,
	const char **io_end,
	sqlparser_mysql_join_kind_t *out_kind)
{
	static const struct {
		const char *name;
		sqlparser_mysql_join_kind_t kind;
	} funcs[] = {
		{SQLPARSER_INTERNAL_MYSQL_LEFT_JOIN_ON, SQLPARSER_MYSQL_JOIN_LEFT},
		{SQLPARSER_INTERNAL_MYSQL_RIGHT_JOIN_ON, SQLPARSER_MYSQL_JOIN_RIGHT},
		{SQLPARSER_INTERNAL_MYSQL_CROSS_JOIN_ON, SQLPARSER_MYSQL_JOIN_CROSS},
		{SQLPARSER_INTERNAL_MYSQL_JOIN_ON, SQLPARSER_MYSQL_JOIN_INNER}
	};
	const char *start;
	const char *end;
	const char *cursor;
	const char *lparen;
	const char *inner_start;
	const char *inner_end;
	size_t prefix_len;
	size_t func_index;
	int depth;

	if (out_kind != NULL) {
		*out_kind = SQLPARSER_MYSQL_JOIN_INNER;
	}
	if (io_start == NULL || io_end == NULL || *io_start == NULL || *io_end == NULL) {
		return 0;
	}
	start = sqlparser_mysql_trim_left(*io_start, *io_end);
	end = sqlparser_mysql_trim_right(start, *io_end);
	prefix_len = 0U;
	for (func_index = 0U; func_index < sizeof(funcs) / sizeof(funcs[0]); func_index++) {
		size_t candidate_len;

		candidate_len = strlen(funcs[func_index].name);
		if ((size_t)(end - start) >= candidate_len + 2U &&
		    sqlparser_mysql_span_ci_equals_cstr(start, start + candidate_len, funcs[func_index].name)) {
			prefix_len = candidate_len;
			if (out_kind != NULL) {
				*out_kind = funcs[func_index].kind;
			}
			break;
		}
	}
	if (prefix_len == 0U) {
		return 0;
	}
	cursor = start + prefix_len;
	while (cursor < end && isspace((unsigned char)*cursor)) {
		cursor++;
	}
	if (cursor >= end || *cursor != '(') {
		return 0;
	}
	lparen = cursor;
	depth = 0;
	while (cursor < end) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(start, (size_t)(cursor - start));
		if (skipped > (size_t)(cursor - start)) {
			cursor = start + skipped;
			continue;
		}
		if (*cursor == '(') {
			depth++;
		} else if (*cursor == ')') {
			depth--;
			if (depth == 0) {
				if (cursor != end - 1) {
					return 0;
				}
				inner_start = sqlparser_mysql_trim_left(lparen + 1, cursor);
				inner_end = sqlparser_mysql_trim_right(inner_start, cursor);
				if (inner_start >= inner_end) {
					return 0;
				}
				*io_start = inner_start;
				*io_end = inner_end;
				return 1;
			}
			if (depth < 0) {
				return 0;
			}
		}
		cursor++;
	}
	return 0;
}

static sqlparser_status_t sqlparser_mysql_append_insert_modifier_sql(
	sqlparser_mysql_buffer_t *out,
	unsigned int flags,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = SQLPARSER_STATUS_OK;
	if ((flags & SQLPARSER_MYSQL_DML_MODIFIER_LOW_PRIORITY) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "LOW_PRIORITY ", out_error);
	} else if ((flags & SQLPARSER_MYSQL_DML_MODIFIER_DELAYED) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "DELAYED ", out_error);
	} else if ((flags & SQLPARSER_MYSQL_DML_MODIFIER_HIGH_PRIORITY) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "HIGH_PRIORITY ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    (flags & SQLPARSER_MYSQL_DML_MODIFIER_IGNORE) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "IGNORE ", out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_mysql_append_update_modifier_sql(
	sqlparser_mysql_buffer_t *out,
	unsigned int flags,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = SQLPARSER_STATUS_OK;
	if ((flags & SQLPARSER_MYSQL_DML_MODIFIER_LOW_PRIORITY) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "LOW_PRIORITY ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    (flags & SQLPARSER_MYSQL_DML_MODIFIER_IGNORE) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "IGNORE ", out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_mysql_append_delete_modifier_sql(
	sqlparser_mysql_buffer_t *out,
	unsigned int flags,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = SQLPARSER_STATUS_OK;
	if ((flags & SQLPARSER_MYSQL_DML_MODIFIER_LOW_PRIORITY) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "LOW_PRIORITY ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    (flags & SQLPARSER_MYSQL_DML_MODIFIER_QUICK) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "QUICK ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    (flags & SQLPARSER_MYSQL_DML_MODIFIER_IGNORE) != 0U) {
		status = sqlparser_mysql_buffer_append_cstr(out, "IGNORE ", out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_mysql_append_dml_modifier_sql(
	sqlparser_mysql_buffer_t *out,
	sqlparser_mysql_dml_modifier_kind_t kind,
	unsigned int flags,
	sqlparser_error_t *out_error)
{
	switch (kind) {
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT:
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE:
		return sqlparser_mysql_append_insert_modifier_sql(out, flags, out_error);
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_UPDATE:
		return sqlparser_mysql_append_update_modifier_sql(out, flags, out_error);
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_DELETE:
		return sqlparser_mysql_append_delete_modifier_sql(out, flags, out_error);
	default:
		break;
	}
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_mysql_dml_modifier_keyword(
	sqlparser_mysql_dml_modifier_kind_t kind)
{
	switch (kind) {
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT:
		return "INSERT";
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_UPDATE:
		return "UPDATE";
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_DELETE:
		return "DELETE";
	case SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE:
		return "REPLACE";
	default:
		break;
	}
	return "";
}

static sqlparser_graph_insert_mode_t sqlparser_mysql_replace_insert_mode_from_statement(
	const char *masked,
	size_t cursor,
	size_t len)
{
	size_t set_pos;
	size_t table_pos;
	size_t select_pos;
	size_t values_pos;
	size_t value_pos;

	set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", cursor, len);
	if (set_pos != (size_t)-1) {
		return SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SET;
	}
	table_pos = sqlparser_mysql_find_top_level_word_between(masked, "table", cursor, len);
	select_pos = sqlparser_mysql_find_top_level_word_between(masked, "select", cursor, len);
	values_pos = sqlparser_mysql_find_top_level_word_between(masked, "values", cursor, len);
	value_pos = sqlparser_mysql_find_top_level_word_between(masked, "value", cursor, len);
	if (table_pos != (size_t)-1 &&
	    (values_pos == (size_t)-1 || table_pos < values_pos) &&
	    (value_pos == (size_t)-1 || table_pos < value_pos) &&
	    (select_pos == (size_t)-1 || table_pos < select_pos)) {
		return SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SELECT;
	}
	if (select_pos != (size_t)-1 &&
	    (values_pos == (size_t)-1 || select_pos < values_pos) &&
	    (value_pos == (size_t)-1 || select_pos < value_pos)) {
		return SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SELECT;
	}
	return SQLPARSER_GRAPH_INSERT_MODE_REPLACE_VALUES;
}

static sqlparser_status_t sqlparser_mysql_rewrite_replace_set_statement(
	const char *statement_sql,
	const char *masked,
	size_t start_pos,
	size_t cursor,
	size_t len,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_buffer_t columns;
	sqlparser_mysql_buffer_t values;
	sqlparser_mysql_origin_trace_t columns_origin;
	sqlparser_mysql_origin_trace_t values_origin;
	const char *relation_start;
	const char *relation_end;
	const char *assignment_start;
	const char *assignment_end;
	size_t set_pos;
	size_t segment_start;
	size_t segment_end;
	size_t column_count;
	sqlparser_status_t status;

	*out_sql = NULL;
	set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", cursor, len);
	if (set_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}

	relation_start = sqlparser_mysql_trim_left(statement_sql + cursor, statement_sql + set_pos);
	relation_end = sqlparser_mysql_trim_right(relation_start, statement_sql + set_pos);
	assignment_start = sqlparser_mysql_trim_left(statement_sql + set_pos + strlen("set"), statement_sql + len);
	assignment_end = sqlparser_mysql_trim_right(assignment_start, statement_sql + len);
	if (relation_start >= relation_end || assignment_start >= assignment_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported MySQL REPLACE SET syntax");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	memset(&out, 0, sizeof(out));
	memset(&columns, 0, sizeof(columns));
	memset(&values, 0, sizeof(values));
	memset(&columns_origin, 0, sizeof(columns_origin));
	memset(&values_origin, 0, sizeof(values_origin));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_begin_origin(
			&columns,
			origin_trace != NULL ? &columns_origin : NULL,
			statement_sql,
			origin_input_base,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_begin_origin(
			&values,
			origin_trace != NULL ? &values_origin : NULL,
			statement_sql,
			origin_input_base,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_origin_trace_release(&columns_origin);
		sqlparser_mysql_origin_trace_release(&values_origin);
		return status;
	}
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&columns, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&values, statement_sql, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_origin_trace_release(&columns_origin);
		sqlparser_mysql_origin_trace_release(&values_origin);
		sqlparser_mysql_buffer_release(&out);
		sqlparser_mysql_buffer_release(&columns);
		sqlparser_mysql_buffer_release(&values);
		return status;
	}

	segment_start = (size_t)(assignment_start - statement_sql);
	segment_end = (size_t)(assignment_end - statement_sql);
	column_count = 0U;
	while (segment_start < segment_end) {
		size_t comma_pos;
		size_t current_end;
		size_t eq_pos;
		const char *left_start;
		const char *left_end;
		const char *right_start;
		const char *right_end;

		comma_pos = sqlparser_mysql_find_top_level_char_between(
			statement_sql,
			',',
			segment_start,
			segment_end);
		current_end = comma_pos == (size_t)-1 ? segment_end : comma_pos;
		eq_pos = sqlparser_mysql_find_top_level_char_between(
			statement_sql,
			'=',
			segment_start,
			current_end);
		if (eq_pos == (size_t)-1) {
			sqlparser_mysql_origin_trace_release(&columns_origin);
			sqlparser_mysql_origin_trace_release(&values_origin);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_mysql_buffer_release(&columns);
			sqlparser_mysql_buffer_release(&values);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported MySQL REPLACE SET assignment");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}

		left_start = sqlparser_mysql_trim_left(statement_sql + segment_start, statement_sql + eq_pos);
		left_end = sqlparser_mysql_trim_right(left_start, statement_sql + eq_pos);
		right_start = sqlparser_mysql_trim_left(statement_sql + eq_pos + 1U, statement_sql + current_end);
		right_end = sqlparser_mysql_trim_right(right_start, statement_sql + current_end);
		if (left_start >= left_end || right_start >= right_end) {
			sqlparser_mysql_origin_trace_release(&columns_origin);
			sqlparser_mysql_origin_trace_release(&values_origin);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_mysql_buffer_release(&columns);
			sqlparser_mysql_buffer_release(&values);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported MySQL REPLACE SET assignment");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		if (column_count > 0U) {
			status = sqlparser_mysql_buffer_append_cstr(&columns, ", ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&values, ", ", out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(
				&columns,
				left_start,
				(size_t)(left_end - left_start),
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(
				&values,
				right_start,
				(size_t)(right_end - right_start),
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_origin_trace_release(&columns_origin);
			sqlparser_mysql_origin_trace_release(&values_origin);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_mysql_buffer_release(&columns);
			sqlparser_mysql_buffer_release(&values);
			return status;
		}
		column_count++;
		if (comma_pos == (size_t)-1) {
			break;
		}
		segment_start = comma_pos + 1U;
	}

	status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "INSERT INTO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			relation_start,
			(size_t)(relation_end - relation_start),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " (", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_trace_cstr(
			&out,
			columns.data,
			&columns_origin,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, ") VALUES (", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_trace_cstr(
			&out,
			values.data,
			&values_origin,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_char(&out, ')', out_error);
	}
	sqlparser_mysql_origin_trace_release(&columns_origin);
	sqlparser_mysql_origin_trace_release(&values_origin);
	sqlparser_mysql_buffer_release(&columns);
	sqlparser_mysql_buffer_release(&values);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_replace_table_statement(
	const char *statement_sql,
	const char *masked,
	size_t start_pos,
	size_t cursor,
	size_t len,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *target_start;
	const char *target_end;
	const char *source_start;
	const char *source_end;
	size_t table_pos;
	sqlparser_status_t status;

	*out_sql = NULL;
	table_pos = sqlparser_mysql_find_top_level_word_between(masked, "table", cursor, len);
	if (table_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}

	target_start = sqlparser_mysql_trim_left(statement_sql + cursor, statement_sql + table_pos);
	target_end = sqlparser_mysql_trim_right(target_start, statement_sql + table_pos);
	source_start = sqlparser_mysql_trim_left(statement_sql + table_pos + strlen("table"), statement_sql + len);
	source_end = sqlparser_mysql_trim_right(source_start, statement_sql + len);
	if (target_start >= target_end || source_start >= source_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported MySQL REPLACE TABLE syntax");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "INSERT INTO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start, (size_t)(target_end - target_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " SELECT * FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start, (size_t)(source_end - source_start), out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_dml_modifier_statement(
	const char *statement_sql,
	size_t statement_index,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t cursor;
	unsigned int flags;
	sqlparser_mysql_dml_modifier_kind_t kind;
	sqlparser_graph_insert_mode_t insert_mode_override;
	const char *keyword;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (sqlparser_mysql_word_at(masked, start_pos, "insert")) {
		kind = SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT;
		keyword = "insert";
		insert_mode_override = SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN;
	} else if (sqlparser_mysql_word_at(masked, start_pos, "update")) {
		kind = SQLPARSER_MYSQL_DML_MODIFIER_KIND_UPDATE;
		keyword = "update";
		insert_mode_override = SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN;
	} else if (sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		kind = SQLPARSER_MYSQL_DML_MODIFIER_KIND_DELETE;
		keyword = "delete";
		insert_mode_override = SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN;
	} else if (sqlparser_mysql_word_at(masked, start_pos, "replace")) {
		kind = SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE;
		keyword = "replace";
		insert_mode_override = SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN;
	} else {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}

	cursor = sqlparser_mysql_skip_space(masked, start_pos + strlen(keyword));
	flags = 0U;
	if (sqlparser_mysql_word_at(masked, cursor, "low_priority")) {
		flags |= SQLPARSER_MYSQL_DML_MODIFIER_LOW_PRIORITY;
		cursor = sqlparser_mysql_skip_space(masked, cursor + strlen("low_priority"));
	} else if ((kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT ||
	            kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE) &&
	           sqlparser_mysql_word_at(masked, cursor, "delayed")) {
		flags |= SQLPARSER_MYSQL_DML_MODIFIER_DELAYED;
		cursor = sqlparser_mysql_skip_space(masked, cursor + strlen("delayed"));
	} else if (kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT &&
	           sqlparser_mysql_word_at(masked, cursor, "high_priority")) {
		flags |= SQLPARSER_MYSQL_DML_MODIFIER_HIGH_PRIORITY;
		cursor = sqlparser_mysql_skip_space(masked, cursor + strlen("high_priority"));
	}
	if (kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_DELETE &&
	    sqlparser_mysql_word_at(masked, cursor, "quick")) {
		flags |= SQLPARSER_MYSQL_DML_MODIFIER_QUICK;
		cursor = sqlparser_mysql_skip_space(masked, cursor + strlen("quick"));
	}
	if (kind != SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE &&
	    sqlparser_mysql_word_at(masked, cursor, "ignore")) {
		flags |= SQLPARSER_MYSQL_DML_MODIFIER_IGNORE;
		cursor = sqlparser_mysql_skip_space(masked, cursor + strlen("ignore"));
	}
	if (kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE) {
		size_t body_start;
		char *replace_sql;

		body_start = cursor;
		if (sqlparser_mysql_word_at(masked, body_start, "into")) {
			body_start = sqlparser_mysql_skip_space(masked, body_start + strlen("into"));
		}
		insert_mode_override = sqlparser_mysql_replace_insert_mode_from_statement(masked, body_start, len);
		replace_sql = NULL;
		if (insert_mode_override == SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SET) {
			status = sqlparser_mysql_rewrite_replace_set_statement(
				statement_sql,
				masked,
				start_pos,
				body_start,
				len,
				&replace_sql,
				origin_input_base,
				origin_trace,
				out_error);
		} else if (insert_mode_override == SQLPARSER_GRAPH_INSERT_MODE_REPLACE_SELECT) {
			size_t table_pos;
			size_t select_pos;
			size_t values_pos;
			size_t value_pos;
			int is_table_form;

			table_pos = sqlparser_mysql_find_top_level_word_between(masked, "table", body_start, len);
			select_pos = sqlparser_mysql_find_top_level_word_between(masked, "select", body_start, len);
			values_pos = sqlparser_mysql_find_top_level_word_between(masked, "values", body_start, len);
			value_pos = sqlparser_mysql_find_top_level_word_between(masked, "value", body_start, len);
			is_table_form = table_pos != (size_t)-1 &&
				(select_pos == (size_t)-1 || table_pos < select_pos) &&
				(values_pos == (size_t)-1 || table_pos < values_pos) &&
				(value_pos == (size_t)-1 || table_pos < value_pos);
			if (is_table_form) {
				status = sqlparser_mysql_rewrite_replace_table_statement(
					statement_sql,
					masked,
					start_pos,
					body_start,
					len,
					&replace_sql,
					origin_input_base,
					origin_trace,
					out_error);
			} else {
				memset(&out, 0, sizeof(out));
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origin_trace,
					statement_sql,
					origin_input_base,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, "INSERT INTO ", out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, statement_sql + body_start, out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					replace_sql = sqlparser_mysql_buffer_take(&out);
					if (replace_sql == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						status = SQLPARSER_STATUS_NO_MEMORY;
					}
				} else {
					sqlparser_mysql_buffer_release(&out);
				}
			}
		} else {
			memset(&out, 0, sizeof(out));
			status = sqlparser_mysql_buffer_begin_origin(
				&out,
				origin_trace,
				statement_sql,
				origin_input_base,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "INSERT INTO ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, statement_sql + body_start, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				replace_sql = sqlparser_mysql_buffer_take(&out);
				if (replace_sql == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					status = SQLPARSER_STATUS_NO_MEMORY;
				}
			} else {
				sqlparser_mysql_buffer_release(&out);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_state_add_dml_modifier(
				state,
				statement_index,
				kind,
				flags,
				insert_mode_override,
				out_error);
		}
		free(masked);
		if (status != SQLPARSER_STATUS_OK) {
			free(replace_sql);
			return status;
		}
		*out_sql = replace_sql;
		return SQLPARSER_STATUS_OK;
	}
	if (kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT) {
		size_t body_start;
		size_t set_pos;
		size_t values_pos;
		size_t value_pos;
		size_t select_pos;
		char *insert_set_sql;

		body_start = cursor;
		if (sqlparser_mysql_word_at(masked, body_start, "into")) {
			body_start = sqlparser_mysql_skip_space(masked, body_start + strlen("into"));
		}
		set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", body_start, len);
		values_pos = sqlparser_mysql_find_top_level_word_between(masked, "values", body_start, len);
		value_pos = sqlparser_mysql_find_top_level_word_between(masked, "value", body_start, len);
		select_pos = sqlparser_mysql_find_top_level_word_between(masked, "select", body_start, len);
		if (set_pos != (size_t)-1 &&
		    (values_pos == (size_t)-1 || set_pos < values_pos) &&
		    (value_pos == (size_t)-1 || set_pos < value_pos) &&
		    (select_pos == (size_t)-1 || set_pos < select_pos)) {
			insert_set_sql = NULL;
			status = sqlparser_mysql_rewrite_replace_set_statement(
				statement_sql,
				masked,
				start_pos,
				body_start,
				len,
				&insert_set_sql,
				origin_input_base,
				origin_trace,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_state_add_dml_modifier(
					state,
					statement_index,
					kind,
					flags,
					SQLPARSER_GRAPH_INSERT_MODE_SET,
					out_error);
			}
			free(masked);
			if (status != SQLPARSER_STATUS_OK) {
				free(insert_set_sql);
				return status;
			}
			*out_sql = insert_set_sql;
			return SQLPARSER_STATUS_OK;
		}
	}
	free(masked);
	if (flags == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(
			&out,
			sqlparser_mysql_dml_modifier_keyword(kind),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, statement_sql + cursor, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_state_add_dml_modifier(
			state,
			statement_index,
			kind,
			flags,
			SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_span_case_equal(
	const char *left,
	size_t left_len,
	const char *right,
	size_t right_len)
{
	size_t index;

	if (left == NULL || right == NULL || left_len != right_len) {
		return 0;
	}
	for (index = 0U; index < left_len; index++) {
		if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index])) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_mysql_append_on_duplicate_tail_internal(
	sqlparser_mysql_buffer_t *out,
	const char *tail,
	const char *tail_end,
	const char *alias_start,
	size_t alias_len,
	const char *alias_columns_start,
	size_t alias_columns_len,
	int *out_used_values,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;
	size_t expression_depth;
	int in_assignment_value;

	if (out_used_values != NULL) {
		*out_used_values = 0;
	}
	pos = tail;
	expression_depth = 0U;
	in_assignment_value = 0;
	while (pos < tail_end && *pos != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(tail, (size_t)(pos - tail));
		if (skipped > (size_t)(pos - tail)) {
			status = sqlparser_mysql_buffer_append_mem(out, pos, skipped - (size_t)(pos - tail), out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			pos = tail + skipped;
			continue;
		}
		if ((size_t)(tail_end - pos) >= 6U &&
		    sqlparser_mysql_span_case_equal(pos, 6U, "values", 6U) &&
		    !sqlparser_mysql_is_ident_char((unsigned char)(pos > tail ? pos[-1] : '\0')) &&
		    !sqlparser_mysql_is_ident_char((unsigned char)pos[6]) &&
		    sqlparser_mysql_skip_space(pos, 6U) < (size_t)(tail_end - pos) &&
		    pos[sqlparser_mysql_skip_space(pos, 6U)] == '(') {
			size_t open_pos;
			size_t close_pos;
			const char *arg_start;
			const char *arg_end;

			open_pos = sqlparser_mysql_skip_space(pos, 6U);
			close_pos = open_pos + 1U;
			while (pos + close_pos < tail_end && pos[close_pos] != ')') {
				close_pos++;
			}
			if (pos + close_pos < tail_end && pos[close_pos] == ')') {
				arg_start = sqlparser_mysql_trim_left(pos + open_pos + 1U, pos + close_pos);
				arg_end = sqlparser_mysql_trim_right(arg_start, pos + close_pos);
				status = sqlparser_mysql_buffer_append_generated_identifier(
					out,
					"EXCLUDED",
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_char(
						out,
						'.',
						out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_mem(out, arg_start, (size_t)(arg_end - arg_start), out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (out_used_values != NULL) {
					*out_used_values = 1;
				}
				pos += close_pos + 1U;
				continue;
			}
		}
		if (alias_start != NULL && alias_len > 0U &&
		    (size_t)(tail_end - pos) > alias_len &&
		    sqlparser_mysql_span_case_equal(pos, alias_len, alias_start, alias_len) &&
		    pos[alias_len] == '.' &&
		    !sqlparser_mysql_is_ident_char((unsigned char)(pos > tail ? pos[-1] : '\0'))) {
			status = sqlparser_mysql_buffer_append_generated_identifier(
				out,
				"EXCLUDED",
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_char(
					out,
					'.',
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			pos += alias_len + 1U;
			continue;
		}
		if (in_assignment_value && alias_columns_start != NULL && alias_columns_len > 0U &&
		    sqlparser_mysql_is_ident_start((unsigned char)*pos) &&
		    !sqlparser_mysql_is_ident_char((unsigned char)(pos > tail ? pos[-1] : '\0'))) {
			const char *token_end;
			const char *list_pos;
			int matched;

			token_end = pos + 1U;
			while (token_end < tail_end && sqlparser_mysql_is_ident_char((unsigned char)*token_end)) {
				token_end++;
			}
			matched = 0;
			list_pos = alias_columns_start;
			while (list_pos < alias_columns_start + alias_columns_len) {
				const char *item_start;
				const char *item_end;

				while (list_pos < alias_columns_start + alias_columns_len &&
				       (*list_pos == '(' || *list_pos == ')' || *list_pos == ',' ||
				        isspace((unsigned char)*list_pos))) {
					list_pos++;
				}
				item_start = list_pos;
				while (list_pos < alias_columns_start + alias_columns_len &&
				       sqlparser_mysql_is_ident_char((unsigned char)*list_pos)) {
					list_pos++;
				}
				item_end = list_pos;
				if (item_end > item_start &&
				    sqlparser_mysql_span_case_equal(
					    pos,
					    (size_t)(token_end - pos),
					    item_start,
					    (size_t)(item_end - item_start))) {
					matched = 1;
					break;
				}
				if (item_end == item_start) {
					list_pos++;
				}
			}
			if (matched) {
				status = sqlparser_mysql_buffer_append_generated_identifier(
					out,
					"sqlparser_mysql_alias_column",
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_char(
						out,
						'.',
						out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_mem(out, pos, (size_t)(token_end - pos), out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos = token_end;
				continue;
			}
		}
		if (*pos == '(') {
			expression_depth++;
		} else if (*pos == ')' && expression_depth > 0U) {
			expression_depth--;
		} else if (*pos == '=' && expression_depth == 0U) {
			in_assignment_value = 1;
		} else if (*pos == ',' && expression_depth == 0U) {
			in_assignment_value = 0;
		}
		status = sqlparser_mysql_buffer_append_mem(out, pos, 1U, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_on_duplicate_statement(
	const char *statement_sql,
	size_t statement_index,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t tail_start;
	size_t on_pos;
	size_t end_pos;
	const char *tail;
	const char *alias_start;
	const char *alias_end;
	const char *alias_columns_start;
	const char *alias_columns_end;
	size_t alias_as_pos;
	size_t insert_copy_end;
	int used_values;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	end_pos = strlen(statement_sql);
	if (!sqlparser_mysql_find_on_duplicate_key_update(masked, 0U, end_pos, &tail_start)) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	on_pos = sqlparser_mysql_find_top_level_word_between(masked, "on", 0U, end_pos);
	if (on_pos == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	alias_start = NULL;
	alias_end = NULL;
	alias_columns_start = NULL;
	alias_columns_end = NULL;
	alias_as_pos = sqlparser_mysql_find_top_level_word_between(masked, "as", 0U, on_pos);
	insert_copy_end = on_pos;
	if (alias_as_pos != (size_t)-1) {
		size_t alias_pos;
		size_t alias_stop;
		size_t after_alias;

		alias_pos = sqlparser_mysql_skip_space(masked, alias_as_pos + strlen("as"));
		alias_stop = alias_pos;
		while (alias_stop < on_pos && sqlparser_mysql_is_ident_char((unsigned char)masked[alias_stop])) {
			alias_stop++;
		}
		after_alias = sqlparser_mysql_skip_space(masked, alias_stop);
		if (alias_stop > alias_pos &&
		    (after_alias == on_pos || masked[after_alias] == '(')) {
			alias_start = statement_sql + alias_pos;
			alias_end = statement_sql + alias_stop;
			insert_copy_end = alias_as_pos;
			if (masked[after_alias] == '(') {
				size_t close_pos;
				size_t depth;

				close_pos = after_alias;
				depth = 0U;
				while (close_pos < on_pos) {
					if (masked[close_pos] == '(') {
						depth++;
					} else if (masked[close_pos] == ')' && depth > 0U && --depth == 0U) {
						alias_columns_start = statement_sql + after_alias;
						alias_columns_end = statement_sql + close_pos + 1U;
						break;
					}
					close_pos++;
				}
			}
		}
	}
	free(masked);
	tail = sqlparser_mysql_trim_left(statement_sql + tail_start, statement_sql + end_pos);
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, insert_copy_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(
			&out,
			"ON CONFLICT ON CONSTRAINT ",
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_generated_identifier(
			&out,
			"sqlparser_mysql_duplicate_key",
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(
			&out,
			" DO UPDATE SET ",
			out_error);
	}
	used_values = 0;
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_on_duplicate_tail_internal(
			&out,
			tail,
			statement_sql + end_pos,
			alias_start,
			alias_end != NULL ? (size_t)(alias_end - alias_start) : 0U,
			alias_columns_start,
			alias_columns_end != NULL ? (size_t)(alias_columns_end - alias_columns_start) : 0U,
			&used_values,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && (used_values || alias_start != NULL)) {
		status = sqlparser_mysql_state_add_on_duplicate_restore(
			state,
			statement_index,
			alias_start != NULL ?
				SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_ALIAS :
				SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_VALUES,
			alias_start,
			alias_end != NULL ? (size_t)(alias_end - alias_start) : 0U,
			alias_columns_start,
			alias_columns_end != NULL ? (size_t)(alias_columns_end - alias_columns_start) : 0U,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_update_join_statement(
	const char *statement_sql,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t target_start;
	size_t join_pos;
	size_t on_pos;
	size_t set_pos;
	size_t where_pos;
	const char *end;
	const char *target_start_ptr;
	const char *target_end_ptr;
	const char *source_start_ptr;
	const char *source_end_ptr;
	const char *join_start_ptr;
	const char *join_end_ptr;
	const char *assign_start_ptr;
	const char *assign_end_ptr;
	const char *where_start_ptr;
	const char *where_end_ptr;
	const char *alias_start;
	const char *alias_end;
	const char *relation_name_start;
	const char *relation_name_end;
	const char *source_alias_start;
	const char *source_alias_end;
	const char *source_relation_name_start;
	const char *source_relation_name_end;
	const char *update_target_start_ptr;
	const char *update_target_end_ptr;
	const char *update_source_start_ptr;
	const char *update_source_end_ptr;
	sqlparser_mysql_join_kind_t join_kind;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	int use_source_target;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "update")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	target_start = sqlparser_mysql_skip_space(masked, start_pos + strlen("update"));
	set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", target_start, len);
	join_pos = set_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "join", target_start, set_pos);
	on_pos = join_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "on", join_pos + strlen("join"), set_pos);
	where_pos = set_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", set_pos + strlen("set"), len);
	free(masked);
	if (set_pos == (size_t)-1 || join_pos == (size_t)-1 || on_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(statement_sql, statement_sql + len);
	target_start_ptr = sqlparser_mysql_trim_left(statement_sql + target_start, statement_sql + join_pos);
	target_end_ptr = sqlparser_mysql_trim_right(target_start_ptr, statement_sql + join_pos);
	sqlparser_mysql_normalize_join_target_end(target_start_ptr, &target_end_ptr, &join_kind);
	source_start_ptr = sqlparser_mysql_trim_left(statement_sql + join_pos + strlen("join"), statement_sql + on_pos);
	source_end_ptr = sqlparser_mysql_trim_right(source_start_ptr, statement_sql + on_pos);
	join_start_ptr = sqlparser_mysql_trim_left(statement_sql + on_pos + strlen("on"), statement_sql + set_pos);
	join_end_ptr = sqlparser_mysql_trim_right(join_start_ptr, statement_sql + set_pos);
	assign_start_ptr = sqlparser_mysql_trim_left(statement_sql + set_pos + strlen("set"), where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	assign_end_ptr = sqlparser_mysql_trim_right(assign_start_ptr, where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	where_start_ptr = where_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(statement_sql + where_pos + strlen("where"), end);
	where_end_ptr = where_start_ptr == NULL ? NULL : sqlparser_mysql_trim_right(where_start_ptr, end);
	if (target_start_ptr >= target_end_ptr || source_start_ptr >= source_end_ptr ||
	    join_start_ptr >= join_end_ptr || assign_start_ptr >= assign_end_ptr) {
		return SQLPARSER_STATUS_OK;
	}
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(target_start_ptr, target_end_ptr, &alias_start, &alias_end);
	relation_name_start = NULL;
	relation_name_end = NULL;
	(void)sqlparser_mysql_extract_relation_name_span(
		target_start_ptr,
		target_end_ptr,
		alias_start,
		&relation_name_start,
		&relation_name_end);
	source_alias_start = NULL;
	source_alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(source_start_ptr, source_end_ptr, &source_alias_start, &source_alias_end);
	source_relation_name_start = NULL;
	source_relation_name_end = NULL;
	(void)sqlparser_mysql_extract_relation_name_span(
		source_start_ptr,
		source_end_ptr,
		source_alias_start,
		&source_relation_name_start,
		&source_relation_name_end);
	use_source_target = 0;
	status = sqlparser_mysql_select_update_join_target(
		assign_start_ptr,
		assign_end_ptr,
		alias_start,
		alias_end,
		relation_name_start,
		relation_name_end,
		source_alias_start,
		source_alias_end,
		source_relation_name_start,
		source_relation_name_end,
		&use_source_target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	update_target_start_ptr = use_source_target ? source_start_ptr : target_start_ptr;
	update_target_end_ptr = use_source_target ? source_end_ptr : target_end_ptr;
	update_source_start_ptr = use_source_target ? target_start_ptr : source_start_ptr;
	update_source_end_ptr = use_source_target ? target_end_ptr : source_end_ptr;
	if (use_source_target) {
		alias_start = source_alias_start;
		alias_end = source_alias_end;
		relation_name_start = source_relation_name_start;
		relation_name_end = source_relation_name_end;
		join_kind = sqlparser_mysql_invert_join_kind(join_kind);
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "UPDATE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			update_target_start_ptr,
			(size_t)(update_target_end_ptr - update_target_start_ptr),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " SET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_update_join_assignments(
			&out,
			assign_start_ptr,
			assign_end_ptr,
			alias_start,
			alias_end,
			relation_name_start,
			relation_name_end,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			update_source_start_ptr,
			(size_t)(update_source_end_ptr - update_source_start_ptr),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_join_on_call(&out, join_start_ptr, join_end_ptr, join_kind, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start_ptr != NULL && where_start_ptr < where_end_ptr) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " AND ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start_ptr, (size_t)(where_end_ptr - where_start_ptr), out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_delete_join_statement(
	const char *statement_sql,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t from_pos;
	size_t join_pos;
	size_t on_pos;
	size_t where_pos;
	const char *end;
	const char *target_start_ptr;
	const char *target_end_ptr;
	const char *source_start_ptr;
	const char *source_end_ptr;
	const char *join_start_ptr;
	const char *join_end_ptr;
	const char *delete_target_start_ptr;
	const char *delete_target_end_ptr;
	const char *where_start_ptr;
	const char *where_end_ptr;
	const char *alias_start;
	const char *alias_end;
	const char *relation_name_start;
	const char *relation_name_end;
	const char *source_alias_start;
	const char *source_alias_end;
	const char *source_relation_name_start;
	const char *source_relation_name_end;
	const char *delete_target_relation_start_ptr;
	const char *delete_target_relation_end_ptr;
	const char *delete_using_relation_start_ptr;
	const char *delete_using_relation_end_ptr;
	sqlparser_mysql_join_kind_t join_kind;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	int use_source_target;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	from_pos = sqlparser_mysql_find_top_level_word_between(masked, "from", start_pos + strlen("delete"), len);
	join_pos = from_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "join", from_pos + strlen("from"), len);
	on_pos = join_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "on", join_pos + strlen("join"), len);
	where_pos = on_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", on_pos + strlen("on"), len);
	free(masked);
	if (from_pos == (size_t)-1 || join_pos == (size_t)-1 || on_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(statement_sql, statement_sql + len);
	target_start_ptr = sqlparser_mysql_trim_left(statement_sql + from_pos + strlen("from"), statement_sql + join_pos);
	target_end_ptr = sqlparser_mysql_trim_right(target_start_ptr, statement_sql + join_pos);
	sqlparser_mysql_normalize_join_target_end(target_start_ptr, &target_end_ptr, &join_kind);
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(target_start_ptr, target_end_ptr, &alias_start, &alias_end);
	relation_name_start = NULL;
	relation_name_end = NULL;
	(void)sqlparser_mysql_extract_relation_name_span(
		target_start_ptr,
		target_end_ptr,
		alias_start,
		&relation_name_start,
		&relation_name_end);
	source_start_ptr = sqlparser_mysql_trim_left(statement_sql + join_pos + strlen("join"), statement_sql + on_pos);
	source_end_ptr = sqlparser_mysql_trim_right(source_start_ptr, statement_sql + on_pos);
	source_alias_start = NULL;
	source_alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(source_start_ptr, source_end_ptr, &source_alias_start, &source_alias_end);
	source_relation_name_start = NULL;
	source_relation_name_end = NULL;
	(void)sqlparser_mysql_extract_relation_name_span(
		source_start_ptr,
		source_end_ptr,
		source_alias_start,
		&source_relation_name_start,
		&source_relation_name_end);
	delete_target_start_ptr = sqlparser_mysql_trim_left(statement_sql + start_pos + strlen("delete"), statement_sql + from_pos);
	delete_target_end_ptr = sqlparser_mysql_trim_right(delete_target_start_ptr, statement_sql + from_pos);
	use_source_target = 0;
	status = sqlparser_mysql_validate_delete_join_target(
		delete_target_start_ptr,
		delete_target_end_ptr,
		alias_start,
		alias_end,
		relation_name_start,
		relation_name_end,
		source_alias_start,
		source_alias_end,
		source_relation_name_start,
		source_relation_name_end,
		&use_source_target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	join_start_ptr = sqlparser_mysql_trim_left(statement_sql + on_pos + strlen("on"), where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	join_end_ptr = sqlparser_mysql_trim_right(join_start_ptr, where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	where_start_ptr = where_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(statement_sql + where_pos + strlen("where"), end);
	where_end_ptr = where_start_ptr == NULL ? NULL : sqlparser_mysql_trim_right(where_start_ptr, end);
	if (target_start_ptr >= target_end_ptr || source_start_ptr >= source_end_ptr || join_start_ptr >= join_end_ptr) {
		return SQLPARSER_STATUS_OK;
	}
	delete_target_relation_start_ptr = use_source_target ? source_start_ptr : target_start_ptr;
	delete_target_relation_end_ptr = use_source_target ? source_end_ptr : target_end_ptr;
	delete_using_relation_start_ptr = use_source_target ? target_start_ptr : source_start_ptr;
	delete_using_relation_end_ptr = use_source_target ? target_end_ptr : source_end_ptr;
	if (use_source_target) {
		join_kind = sqlparser_mysql_invert_join_kind(join_kind);
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "DELETE FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			delete_target_relation_start_ptr,
			(size_t)(delete_target_relation_end_ptr - delete_target_relation_start_ptr),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " USING ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			delete_using_relation_start_ptr,
			(size_t)(delete_using_relation_end_ptr - delete_using_relation_start_ptr),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_join_on_call(&out, join_start_ptr, join_end_ptr, join_kind, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start_ptr != NULL && where_start_ptr < where_end_ptr) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " AND ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start_ptr, (size_t)(where_end_ptr - where_start_ptr), out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_delete_alias_statement(
	const char *statement_sql,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t from_pos;
	size_t join_pos;
	size_t where_pos;
	size_t order_pos;
	size_t limit_pos;
	size_t relation_stop_pos;
	size_t target_start_pos;
	const char *target_start;
	const char *target_end;
	const char *relation_start;
	const char *relation_end;
	const char *alias_start;
	const char *alias_end;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	from_pos = sqlparser_mysql_find_top_level_word_between(masked, "from", start_pos + strlen("delete"), len);
	join_pos = from_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "join", from_pos + strlen("from"), len);
	if (from_pos == (size_t)-1 || join_pos != (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	where_pos = sqlparser_mysql_find_top_level_word_between(masked, "where", from_pos + strlen("from"), len);
	order_pos = sqlparser_mysql_find_top_level_word_between(masked, "order", from_pos + strlen("from"), len);
	limit_pos = sqlparser_mysql_find_top_level_word_between(masked, "limit", from_pos + strlen("from"), len);
	relation_stop_pos = len;
	if (where_pos != (size_t)-1 && where_pos < relation_stop_pos) {
		relation_stop_pos = where_pos;
	}
	if (order_pos != (size_t)-1 && order_pos < relation_stop_pos) {
		relation_stop_pos = order_pos;
	}
	if (limit_pos != (size_t)-1 && limit_pos < relation_stop_pos) {
		relation_stop_pos = limit_pos;
	}
	target_start_pos = sqlparser_mysql_skip_space(masked, start_pos + strlen("delete"));
	target_start = statement_sql + target_start_pos;
	target_end = sqlparser_mysql_trim_right(target_start, statement_sql + from_pos);
	relation_start = sqlparser_mysql_trim_left(statement_sql + from_pos + strlen("from"), statement_sql + relation_stop_pos);
	relation_end = sqlparser_mysql_trim_right(relation_start, statement_sql + relation_stop_pos);
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(relation_start, relation_end, &alias_start, &alias_end);
	free(masked);
	if (target_start >= target_end || alias_start == NULL || alias_end == NULL ||
	    !sqlparser_mysql_span_equal(target_start, target_end, alias_start, alias_end)) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "DELETE FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, relation_start, (size_t)(relation_end - relation_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK && relation_stop_pos < len) {
		status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
	}
	if (status == SQLPARSER_STATUS_OK && relation_stop_pos < len) {
		status = sqlparser_mysql_buffer_append_cstr(
			&out,
			sqlparser_mysql_trim_left(statement_sql + relation_stop_pos, statement_sql + len),
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_dml_order_limit_statement(
	const char *statement_sql,
	size_t statement_index,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	size_t origin_input_base,
	sqlparser_mysql_origin_trace_t *origin_trace,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t order_pos;
	size_t by_pos;
	size_t limit_pos;
	size_t cut_pos;
	const char *order_start;
	const char *order_end;
	const char *limit_start;
	const char *limit_end;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "update") &&
	    !sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	order_pos = sqlparser_mysql_find_top_level_word_between(masked, "order", start_pos, len);
	by_pos = order_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_skip_space(masked, order_pos + strlen("order"));
	if (by_pos == (size_t)-1 || !sqlparser_mysql_word_at(masked, by_pos, "by")) {
		order_pos = (size_t)-1;
	}
	limit_pos = sqlparser_mysql_find_top_level_word_between(masked, "limit", start_pos, len);
	if (order_pos == (size_t)-1 && limit_pos == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	cut_pos = order_pos != (size_t)-1 && (limit_pos == (size_t)-1 || order_pos < limit_pos) ? order_pos : limit_pos;
	order_start = order_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(statement_sql + order_pos, statement_sql + (limit_pos != (size_t)-1 && limit_pos > order_pos ? limit_pos : len));
	order_end = order_start == NULL ? NULL : sqlparser_mysql_trim_right(order_start, statement_sql + (limit_pos != (size_t)-1 && limit_pos > order_pos ? limit_pos : len));
	limit_start = limit_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(statement_sql + limit_pos, statement_sql + len);
	limit_end = limit_start == NULL ? NULL : sqlparser_mysql_trim_right(limit_start, statement_sql + len);
	status = sqlparser_mysql_state_add_dml_tail(
		state,
		statement_index,
		order_start,
		order_start != NULL && order_end > order_start ? (size_t)(order_end - order_start) : 0U,
		limit_start,
		limit_start != NULL && limit_end > limit_start ? (size_t)(limit_end - limit_start) : 0U,
		out_error);
	free(masked);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_begin_origin(
		&out,
		origin_trace,
		statement_sql,
		origin_input_base,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			statement_sql,
			(size_t)(sqlparser_mysql_trim_right(statement_sql, statement_sql + cut_pos) - statement_sql),
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

typedef enum {
	SQLPARSER_MYSQL_DML_ORIGIN_MODIFIER = 0,
	SQLPARSER_MYSQL_DML_ORIGIN_ON_DUPLICATE,
	SQLPARSER_MYSQL_DML_ORIGIN_UPDATE_JOIN,
	SQLPARSER_MYSQL_DML_ORIGIN_DELETE_JOIN,
	SQLPARSER_MYSQL_DML_ORIGIN_DELETE_ALIAS,
	SQLPARSER_MYSQL_DML_ORIGIN_ORDER_LIMIT
} sqlparser_mysql_dml_origin_pass_t;

static sqlparser_status_t sqlparser_mysql_rewrite_dml_origin_pass(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_mysql_dml_origin_pass_t pass,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	size_t copy_start;
	size_t len;
	size_t segment_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	sql = *io_sql;
	len = strlen(sql);
	copy_start = 0U;
	segment_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	memset(&origin, 0, sizeof(origin));
	while (segment_start <= len) {
		sqlparser_mysql_origin_trace_t statement_origin;
		const char *trimmed_end;
		const char *trimmed_start;
		char *rewritten_sql;
		char *statement_sql;
		size_t current_statement_index;
		size_t statement_end;

		memset(&statement_origin, 0, sizeof(statement_origin));
		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start =
			sqlparser_mysql_trim_left(
				sql + segment_start,
				sql + statement_end);
		trimmed_end =
			sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		current_statement_index = statement_index;
		if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		statement_sql = sqlparser_strndup(
			sql + segment_start,
			statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_origin_trace_release(&origin);
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		switch (pass) {
			case SQLPARSER_MYSQL_DML_ORIGIN_MODIFIER:
				status = sqlparser_mysql_rewrite_dml_modifier_statement(
					statement_sql,
					current_statement_index,
					state,
					&rewritten_sql,
					segment_start,
					&statement_origin,
					out_error);
				break;
			case SQLPARSER_MYSQL_DML_ORIGIN_ON_DUPLICATE:
				status = sqlparser_mysql_rewrite_on_duplicate_statement(
					statement_sql,
					current_statement_index,
					state,
					&rewritten_sql,
					segment_start,
					&statement_origin,
					out_error);
				break;
			case SQLPARSER_MYSQL_DML_ORIGIN_UPDATE_JOIN:
				status = sqlparser_mysql_rewrite_update_join_statement(
					statement_sql,
					&rewritten_sql,
					segment_start,
					&statement_origin,
					out_error);
				break;
			case SQLPARSER_MYSQL_DML_ORIGIN_DELETE_JOIN:
				status = sqlparser_mysql_rewrite_delete_join_statement(
					statement_sql,
					&rewritten_sql,
					segment_start,
					&statement_origin,
					out_error);
				break;
			case SQLPARSER_MYSQL_DML_ORIGIN_DELETE_ALIAS:
				status = sqlparser_mysql_rewrite_delete_alias_statement(
					statement_sql,
					&rewritten_sql,
					segment_start,
					&statement_origin,
					out_error);
				break;
			case SQLPARSER_MYSQL_DML_ORIGIN_ORDER_LIMIT:
				status =
					sqlparser_mysql_rewrite_dml_order_limit_statement(
						statement_sql,
						current_statement_index,
						state,
						&rewritten_sql,
						segment_start,
						&statement_origin,
						out_error);
				break;
			default:
				status = SQLPARSER_STATUS_INTERNAL_ERROR;
				sqlparser_error_set_message(
					out_error,
					status,
					"MySQL DML origin pass is invalid");
				break;
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_mysql_origin_trace_release(&statement_origin);
			sqlparser_mysql_origin_trace_release(&origin);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			size_t leading_end;

			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					&origin,
					sql,
					0U,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_reserve_input(
						&out,
						sql,
						out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					sqlparser_mysql_origin_trace_release(
						&statement_origin);
					sqlparser_mysql_origin_trace_release(&origin);
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				rewritten = 1;
			}
			leading_end = (size_t)(
				sqlparser_mysql_trim_left(
					sql + segment_start,
					sql + statement_end) -
				sql);
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				sql + copy_start,
				leading_end - copy_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_trace_cstr(
					&out,
					rewritten_sql,
					&statement_origin,
					out_error);
			}
			free(rewritten_sql);
			sqlparser_mysql_origin_trace_release(&statement_origin);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		} else {
			sqlparser_mysql_origin_trace_release(&statement_origin);
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(
		&out,
		sql + copy_start,
		len - copy_start,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			sql,
			out.data,
			&origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&origin);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_dml_extensions(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (origins != NULL) {
		sqlparser_mysql_dml_origin_pass_t pass;

		for (pass = SQLPARSER_MYSQL_DML_ORIGIN_MODIFIER;
		     pass <= SQLPARSER_MYSQL_DML_ORIGIN_ORDER_LIMIT;
		     pass = (sqlparser_mysql_dml_origin_pass_t)(pass + 1)) {
			status = sqlparser_mysql_rewrite_dml_origin_pass(
				io_sql,
				state,
				origins,
				pass,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start <= len) {
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;
		char *statement_sql;
		char *modifier_sql;
		size_t current_statement_index;
		int statement_rewritten;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		current_statement_index = statement_index;
		if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		modifier_sql = NULL;
		statement_rewritten = 0;
		status = sqlparser_mysql_rewrite_dml_modifier_statement(
			statement_sql,
			current_statement_index,
			state,
			&modifier_sql,
			0U,
			NULL,
			out_error);
		if (status == SQLPARSER_STATUS_OK && modifier_sql != NULL) {
			free(statement_sql);
			statement_sql = modifier_sql;
			statement_rewritten = 1;
		}
		if (status == SQLPARSER_STATUS_OK) {
			char *next_sql;

			next_sql = NULL;
			status = sqlparser_mysql_rewrite_on_duplicate_statement(
				statement_sql,
				current_statement_index,
				state,
				&next_sql,
				0U,
				NULL,
				out_error);
			if (status == SQLPARSER_STATUS_OK && next_sql != NULL) {
				free(statement_sql);
				statement_sql = next_sql;
				statement_rewritten = 1;
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			char *next_sql;

			next_sql = NULL;
			status = sqlparser_mysql_rewrite_update_join_statement(
				statement_sql,
				&next_sql,
				0U,
				NULL,
				out_error);
			if (status == SQLPARSER_STATUS_OK && next_sql != NULL) {
				free(statement_sql);
				statement_sql = next_sql;
				statement_rewritten = 1;
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			char *next_sql;

			next_sql = NULL;
			status = sqlparser_mysql_rewrite_delete_join_statement(
				statement_sql,
				&next_sql,
				0U,
				NULL,
				out_error);
			if (status == SQLPARSER_STATUS_OK && next_sql != NULL) {
				free(statement_sql);
				statement_sql = next_sql;
				statement_rewritten = 1;
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			char *next_sql;

			next_sql = NULL;
			status = sqlparser_mysql_rewrite_delete_alias_statement(
				statement_sql,
				&next_sql,
				0U,
				NULL,
				out_error);
			if (status == SQLPARSER_STATUS_OK && next_sql != NULL) {
				free(statement_sql);
				statement_sql = next_sql;
				statement_rewritten = 1;
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			char *next_sql;

			next_sql = NULL;
			status = sqlparser_mysql_rewrite_dml_order_limit_statement(
				statement_sql,
				current_statement_index,
				state,
				&next_sql,
				0U,
				NULL,
				out_error);
			if (status == SQLPARSER_STATUS_OK && next_sql != NULL) {
				free(statement_sql);
				statement_sql = next_sql;
				statement_rewritten = 1;
			}
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(statement_sql);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (statement_rewritten) {
			const char *rewritten_text;
			size_t leading_end;

			rewritten_text = statement_sql;
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(statement_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = (size_t)(sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end) - sql);
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, rewritten_text, out_error);
			}
			free(statement_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		} else {
			free(statement_sql);
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_identifier_to_backtick(
	const char *sql,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_char(out, '`', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (sql[pos] != '\0') {
		char c;

		c = sql[pos];
		if (c == '"') {
			if (sql[pos + 1U] == '"') {
				status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
				pos += 2U;
			} else {
				status = sqlparser_mysql_buffer_append_char(out, '`', out_error);
				pos++;
				*index = pos;
				return status;
			}
		} else if (c == '`') {
			status = sqlparser_mysql_buffer_append_cstr(out, "``", out_error);
			pos++;
		} else {
			status = sqlparser_mysql_buffer_append_char(out, c, out_error);
			pos++;
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated quoted identifier in deparse output");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_mysql_param_to_question(
	const char *sql,
	size_t *index,
	const sqlparser_mysql_state_t *state,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (state == NULL || state->positional_param_count == 0U ||
	    !isdigit((unsigned char)sql[pos])) {
		return sqlparser_mysql_buffer_append_char(out, sql[(*index)++], out_error);
	}

	value = 0UL;
	while (isdigit((unsigned char)sql[pos])) {
		unsigned int digit;

		digit = (unsigned int)(sql[pos] - '0');
		if (value > (((unsigned long)-1) - digit) / 10UL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "parameter index overflow");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		value = value * 10UL + digit;
		pos++;
	}

	if (value > 0UL && (size_t)value <= state->positional_param_count) {
		*index = pos;
		return sqlparser_mysql_buffer_append_char(out, '?', out_error);
	}

	return sqlparser_mysql_buffer_append_char(out, sql[(*index)++], out_error);
}

static size_t sqlparser_mysql_quoted_literal_end(const char *sql, size_t start)
{
	size_t pos;

	if (sql == NULL || sql[start] != '\'') {
		return 0U;
	}

	pos = start + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == '\'' && sql[pos + 1U] == '\'') {
			pos += 2U;
			continue;
		}
		if (sql[pos] == '\'') {
			return pos + 1U;
		}
		pos++;
	}

	return 0U;
}

static sqlparser_status_t sqlparser_mysql_rewrite_pg_quotes_to_backticks(
	const char *sql,
	const sqlparser_mysql_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;
	size_t literal_ordinal;

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	literal_ordinal = 0U;
	while (sql[index] != '\0') {
		int copied;

		if (sql[index] == '\'') {
			size_t literal_end;

			literal_end = sqlparser_mysql_quoted_literal_end(sql, index);
			if (literal_end > index &&
			    sqlparser_mysql_national_literal_matches(state, literal_ordinal, sql, index, literal_end)) {
				status = sqlparser_mysql_buffer_append_char(&out, 'N', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
			}
			copied = sqlparser_mysql_copy_single_quoted_or_comment(sql, &index, &out, out_error);
			if (copied < 0) {
				sqlparser_mysql_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			if (copied > 0) {
				literal_ordinal++;
				continue;
			}
		}

		copied = sqlparser_mysql_copy_single_quoted_or_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_mysql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (sql[index] == '$') {
			status = sqlparser_mysql_param_to_question(sql, &index, state, &out, out_error);
		} else if (sql[index] == '"') {
			status = sqlparser_mysql_identifier_to_backtick(sql, &index, &out, out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static const sqlparser_mysql_create_column_restore_t *sqlparser_mysql_find_create_column_restore(
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t column_ordinal)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->create_column_restore_count; index++) {
		if (state->create_column_restores[index].statement_index == statement_index &&
		    state->create_column_restores[index].column_ordinal == column_ordinal) {
			return &state->create_column_restores[index];
		}
	}
	return NULL;
}

static const sqlparser_mysql_create_table_restore_t *sqlparser_mysql_find_create_table_restore(
	const sqlparser_mysql_state_t *state,
	size_t statement_index)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->create_table_restore_count; index++) {
		if (state->create_table_restores[index].statement_index == statement_index) {
			return &state->create_table_restores[index];
		}
	}
	return NULL;
}

static sqlparser_status_t sqlparser_mysql_restore_create_table_columns(
	const char *statement_sql,
	const char *masked,
	size_t body_start,
	size_t body_end,
	size_t statement_index,
	const sqlparser_mysql_state_t *state,
	sqlparser_mysql_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	size_t segment_start;
	size_t column_ordinal;
	sqlparser_status_t status;

	segment_start = body_start;
	column_ordinal = 0U;
	while (segment_start <= body_end) {
		const sqlparser_mysql_create_column_restore_t *restore;
		const char *trimmed_start;
		const char *trimmed_end;
		size_t comma_pos;
		size_t segment_end;
		size_t ordinal;

		comma_pos = sqlparser_mysql_find_top_level_char_between(
			statement_sql,
			',',
			segment_start,
			body_end);
		segment_end = comma_pos == (size_t)-1 ? body_end : comma_pos;
		restore = NULL;
		if (sqlparser_mysql_create_table_segment_is_column(masked, segment_start, segment_end)) {
			ordinal = column_ordinal;
			column_ordinal++;
			restore = sqlparser_mysql_find_create_column_restore(state, statement_index, ordinal);
		}
		if (restore != NULL) {
			trimmed_start = sqlparser_mysql_trim_left(statement_sql + segment_start, statement_sql + segment_end);
			trimmed_end = sqlparser_mysql_trim_right(trimmed_start, statement_sql + segment_end);
			status = sqlparser_mysql_buffer_append_mem(
				out,
				statement_sql + segment_start,
				(size_t)(trimmed_start - (statement_sql + segment_start)),
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(out, restore->segment_sql, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					out,
					trimmed_end,
					(size_t)((statement_sql + segment_end) - trimmed_end),
					out_error);
			}
			*out_rewritten = 1;
		} else {
			status = sqlparser_mysql_buffer_append_mem(
				out,
				statement_sql + segment_start,
				segment_end - segment_start,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (comma_pos == (size_t)-1) {
			break;
		}
		status = sqlparser_mysql_buffer_append_char(out, ',', out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		segment_start = comma_pos + 1U;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_create_table_statement_extensions(
	const char *statement_sql,
	size_t statement_index,
	const sqlparser_mysql_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t columns;
	sqlparser_mysql_buffer_t out;
	const sqlparser_mysql_create_table_restore_t *table_restore;
	char *masked;
	const char *trimmed_end;
	sqlparser_status_t status;
	size_t len;
	size_t body_start;
	size_t body_end;
	int columns_rewritten;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (statement_sql == NULL || state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	table_restore = sqlparser_mysql_find_create_table_restore(state, statement_index);
	if (table_restore == NULL && state->create_column_restore_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	len = strlen(statement_sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!sqlparser_mysql_find_create_table_body(statement_sql, masked, len, &body_start, &body_end)) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}

	memset(&columns, 0, sizeof(columns));
	columns_rewritten = 0;
	status = sqlparser_mysql_restore_create_table_columns(
		statement_sql,
		masked,
		body_start,
		body_end,
		statement_index,
		state,
		&columns,
		&columns_rewritten,
		out_error);
	free(masked);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&columns);
		return status;
	}
	if (!columns_rewritten && table_restore == NULL) {
		sqlparser_mysql_buffer_release(&columns);
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, body_start, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, columns.data, columns.len, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && table_restore != NULL) {
		trimmed_end = sqlparser_mysql_trim_right(statement_sql + body_end, statement_sql + len);
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			statement_sql + body_end,
			(size_t)(trimmed_end - (statement_sql + body_end)),
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, table_restore->options_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				trimmed_end,
				(size_t)((statement_sql + len) - trimmed_end),
				out_error);
		}
	} else if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(
			&out,
			statement_sql + body_end,
			len - body_end,
			out_error);
	}
	sqlparser_mysql_buffer_release(&columns);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_create_table_extensions_to_mysql(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state == NULL ||
	    (state->create_column_restore_count == 0U && state->create_table_restore_count == 0U)) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;
		char *statement_sql;
		char *rewritten_sql;
		size_t current_statement_index;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		current_statement_index = statement_index;
		if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_mysql_restore_create_table_statement_extensions(
			statement_sql,
			current_statement_index,
			state,
			&rewritten_sql,
			out_error);
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				sql + copy_start,
				segment_start - copy_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_limit_count_offset_to_mysql(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	sqlparser_status_t status;
	size_t index;
	size_t copy_start;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	rewritten = 0;
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}

		if (sqlparser_mysql_ascii_word_equal(sql, index, "limit")) {
			size_t count_start_pos;
			size_t count_end_pos;
			size_t offset_word_pos;
			size_t offset_start_pos;
			size_t offset_end_pos;
			const char *count_start;
			const char *count_end;
			const char *offset_start;
			const char *offset_end;

			count_start_pos = index + 5U;
			while (isspace((unsigned char)sql[count_start_pos])) {
				count_start_pos++;
			}
			count_end_pos = count_start_pos;
			while (sql[count_end_pos] != '\0' && !isspace((unsigned char)sql[count_end_pos])) {
				count_end_pos++;
			}
			offset_word_pos = count_end_pos;
			while (isspace((unsigned char)sql[offset_word_pos])) {
				offset_word_pos++;
			}
			if (!sqlparser_mysql_ascii_word_equal(sql, offset_word_pos, "offset")) {
				index++;
				continue;
			}
			offset_start_pos = offset_word_pos + 6U;
			while (isspace((unsigned char)sql[offset_start_pos])) {
				offset_start_pos++;
			}
			offset_end_pos = offset_start_pos;
			while (sql[offset_end_pos] != '\0' && sql[offset_end_pos] != ';' &&
			       !isspace((unsigned char)sql[offset_end_pos])) {
				offset_end_pos++;
			}

			count_start = sqlparser_mysql_trim_left(sql + count_start_pos, sql + count_end_pos);
			count_end = sqlparser_mysql_trim_right(count_start, sql + count_end_pos);
			offset_start = sqlparser_mysql_trim_left(sql + offset_start_pos, sql + offset_end_pos);
			offset_end = sqlparser_mysql_trim_right(offset_start, sql + offset_end_pos);
			if (!sqlparser_mysql_simple_limit_operand(count_start, count_end) ||
			    !sqlparser_mysql_simple_limit_operand(offset_start, offset_end)) {
				index++;
				continue;
			}

			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "LIMIT ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					offset_start,
					(size_t)(offset_end - offset_start),
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, ", ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					count_start,
					(size_t)(count_end - count_start),
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index = offset_end_pos;
			copy_start = index;
			continue;
		}

		index++;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_find_first_top_level_and(
	const char *masked,
	size_t start,
	size_t end)
{
	return sqlparser_mysql_find_top_level_word_between(masked, "and", start, end);
}

static sqlparser_status_t sqlparser_mysql_rewrite_on_conflict_to_duplicate_key(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	char *masked;
	const char *sql;
	size_t len;
	size_t index;
	size_t copy_start;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	rewritten = 0;
	while (index < len) {
		size_t on_pos;
		size_t conflict_pos;
		size_t do_pos;
		size_t update_pos;
		size_t set_pos;

		on_pos = sqlparser_mysql_find_top_level_word_between(masked, "on", index, len);
		if (on_pos == (size_t)-1) {
			break;
		}
		conflict_pos = sqlparser_mysql_skip_space(masked, on_pos + strlen("on"));
		if (!sqlparser_mysql_word_at(masked, conflict_pos, "conflict")) {
			index = on_pos + strlen("on");
			continue;
		}
		do_pos = sqlparser_mysql_find_top_level_word_between(
			masked,
			"do",
			conflict_pos + strlen("conflict"),
			len);
		update_pos = do_pos == (size_t)-1 ?
			(size_t)-1 :
			sqlparser_mysql_skip_space(masked, do_pos + strlen("do"));
		set_pos = update_pos == (size_t)-1 || !sqlparser_mysql_word_at(masked, update_pos, "update") ?
			(size_t)-1 :
			sqlparser_mysql_find_top_level_word_between(masked, "set", update_pos + strlen("update"), len);
		if (set_pos == (size_t)-1) {
			index = conflict_pos + strlen("conflict");
			continue;
		}
		if (!rewritten) {
			status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(masked);
				return status;
			}
			rewritten = 1;
		}
		status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, on_pos - copy_start, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, "ON DUPLICATE KEY UPDATE ", out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(masked);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		copy_start = sqlparser_mysql_skip_space(sql, set_pos + strlen("set"));
		index = copy_start;
	}
	free(masked);
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_identifier_end(const char *sql, size_t start, size_t end)
{
	size_t pos;

	if (sql == NULL || start >= end) {
		return start;
	}
	if (sql[start] == '`') {
		pos = start + 1U;
		while (pos < end) {
			if (sql[pos] == '`') {
				if (pos + 1U < end && sql[pos + 1U] == '`') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return start;
	}
	pos = start;
	while (pos < end &&
	       (sqlparser_mysql_is_ident_char((unsigned char)sql[pos]) || sql[pos] == '.')) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_mysql_qualified_identifier_end(const char *sql, size_t start, size_t end)
{
	size_t pos;

	pos = sqlparser_mysql_identifier_end(sql, start, end);
	while (pos > start) {
		size_t dot_pos;
		size_t next_start;
		size_t next_end;

		dot_pos = sqlparser_mysql_skip_space(sql, pos);
		if (dot_pos >= end || sql[dot_pos] != '.') {
			break;
		}
		next_start = sqlparser_mysql_skip_space(sql, dot_pos + 1U);
		next_end = sqlparser_mysql_identifier_end(sql, next_start, end);
		if (next_end <= next_start) {
			break;
		}
		pos = next_end;
	}
	return pos;
}

static sqlparser_status_t sqlparser_mysql_append_on_duplicate_reference_tail(
	sqlparser_mysql_buffer_t *out,
	const char *tail,
	const char *tail_end,
	const sqlparser_mysql_on_duplicate_restore_t *restore,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	pos = tail;
	while (pos < tail_end && *pos != '\0') {
		size_t skipped;
		size_t offset;

		offset = (size_t)(pos - tail);
		skipped = sqlparser_mysql_skip_quoted_or_comment_span(tail, offset);
		if (skipped > offset) {
			status = sqlparser_mysql_buffer_append_mem(out, pos, skipped - offset, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			pos = tail + skipped;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(pos, 0U, "excluded") && pos + strlen("excluded") < tail_end &&
		    pos[strlen("excluded")] == '.') {
			size_t ident_start;
			size_t ident_end;

			ident_start = strlen("excluded") + 1U;
			ident_end = sqlparser_mysql_identifier_end(pos, ident_start, (size_t)(tail_end - pos));
			if (ident_end > ident_start) {
				if (restore->kind == SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_ALIAS &&
				    restore->alias_name != NULL &&
				    restore->alias_name[0] != '\0') {
					status = sqlparser_mysql_buffer_append_cstr(out, restore->alias_name, out_error);
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_char(out, '.', out_error);
					}
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_mem(
							out,
							pos + ident_start,
							ident_end - ident_start,
							out_error);
					}
				} else {
					status = sqlparser_mysql_buffer_append_cstr(out, "VALUES(", out_error);
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_mem(
							out,
							pos + ident_start,
							ident_end - ident_start,
							out_error);
					}
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_char(out, ')', out_error);
					}
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += ident_end;
				continue;
			}
		}
		if (sqlparser_mysql_ascii_word_equal(pos, 0U, "sqlparser_mysql_alias_column") &&
		    pos + strlen("sqlparser_mysql_alias_column") < tail_end &&
		    pos[strlen("sqlparser_mysql_alias_column")] == '.') {
			size_t ident_start;
			size_t ident_end;

			ident_start = strlen("sqlparser_mysql_alias_column") + 1U;
			ident_end = sqlparser_mysql_identifier_end(pos, ident_start, (size_t)(tail_end - pos));
			if (ident_end > ident_start) {
				status = sqlparser_mysql_buffer_append_mem(
					out,
					pos + ident_start,
					ident_end - ident_start,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += ident_end;
				continue;
			}
		}
		status = sqlparser_mysql_buffer_append_char(out, *pos, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_on_duplicate_references_statement(
	const char *statement_sql,
	const sqlparser_mysql_on_duplicate_restore_t *restore,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t tail_start;
	size_t on_pos;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	if (restore == NULL || restore->kind == SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_NONE) {
		return SQLPARSER_STATUS_OK;
	}
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	if (!sqlparser_mysql_find_on_duplicate_key_update(masked, 0U, len, &tail_start)) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	on_pos = sqlparser_mysql_find_top_level_word_between(masked, "on", 0U, len);
	free(masked);
	if (on_pos == (size_t)-1 || tail_start > len) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, on_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    restore->kind == SQLPARSER_MYSQL_ON_DUPLICATE_RESTORE_ALIAS &&
	    restore->alias_name != NULL &&
	    restore->alias_name[0] != '\0') {
		status = sqlparser_mysql_buffer_append_cstr(&out, "AS ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, restore->alias_name, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && restore->alias_columns_sql != NULL) {
			status = sqlparser_mysql_buffer_append_cstr(&out, restore->alias_columns_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql + on_pos, tail_start - on_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_on_duplicate_reference_tail(
			&out,
			statement_sql + tail_start,
			statement_sql + len,
			restore,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_on_duplicate_references(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL || state->on_duplicate_restore_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start <= len) {
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;
		const sqlparser_mysql_on_duplicate_restore_t *restore;
		char *statement_sql;
		char *next_sql;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		restore = trimmed_start < trimmed_end ?
			sqlparser_mysql_state_on_duplicate_restore(state, statement_index) :
			NULL;
		if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		if (restore == NULL) {
			if (statement_end >= len) {
				break;
			}
			segment_start = statement_end + 1U;
			continue;
		}
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_sql = NULL;
		status = sqlparser_mysql_restore_on_duplicate_references_statement(
			statement_sql,
			restore,
			&next_sql,
			out_error);
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (next_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(next_sql);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, segment_start - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, next_sql, out_error);
			}
			free(next_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_statement_has_insert_set_mode(
	const sqlparser_mysql_state_t *state,
	size_t statement_index)
{
	const sqlparser_mysql_dml_modifier_t *modifier;

	modifier = sqlparser_mysql_state_dml_modifier(
		state,
		statement_index,
		SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT);
	return modifier != NULL && modifier->insert_mode_override == SQLPARSER_GRAPH_INSERT_MODE_SET;
}

static sqlparser_status_t sqlparser_mysql_restore_insert_set_statement(
	const char *statement_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t cursor;
	size_t col_open;
	size_t col_close;
	size_t values_pos;
	size_t val_open;
	size_t val_close;
	const char *relation_start;
	const char *relation_end;
	size_t col_start;
	size_t val_start;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "insert")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	cursor = sqlparser_mysql_skip_space(masked, start_pos + strlen("insert"));
	if (sqlparser_mysql_word_at(masked, cursor, "into")) {
		cursor = sqlparser_mysql_skip_space(masked, cursor + strlen("into"));
	}
	col_open = sqlparser_mysql_find_top_level_char_between(masked, '(', cursor, len);
	if (col_open == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	col_close = sqlparser_mysql_find_matching_paren(masked, col_open, len);
	values_pos = col_close == (size_t)-1 ?
		(size_t)-1 :
		sqlparser_mysql_find_top_level_word_between(masked, "values", col_close + 1U, len);
	val_open = values_pos == (size_t)-1 ?
		(size_t)-1 :
		sqlparser_mysql_find_top_level_char_between(masked, '(', values_pos + strlen("values"), len);
	val_close = val_open == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_matching_paren(masked, val_open, len);
	free(masked);
	if (col_close == (size_t)-1 || values_pos == (size_t)-1 || val_open == (size_t)-1 || val_close == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	relation_start = sqlparser_mysql_trim_left(statement_sql + cursor, statement_sql + col_open);
	relation_end = sqlparser_mysql_trim_right(relation_start, statement_sql + col_open);
	if (relation_start >= relation_end) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "INSERT INTO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, relation_start, (size_t)(relation_end - relation_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " SET ", out_error);
	}
	col_start = col_open + 1U;
	val_start = val_open + 1U;
	while (status == SQLPARSER_STATUS_OK) {
		size_t col_comma;
		size_t val_comma;
		size_t col_end;
		size_t val_end;
		const char *col_part_start;
		const char *col_part_end;
		const char *val_part_start;
		const char *val_part_end;

		col_comma = sqlparser_mysql_find_top_level_char_between(statement_sql, ',', col_start, col_close);
		val_comma = sqlparser_mysql_find_top_level_char_between(statement_sql, ',', val_start, val_close);
		col_end = col_comma == (size_t)-1 ? col_close : col_comma;
		val_end = val_comma == (size_t)-1 ? val_close : val_comma;
		col_part_start = sqlparser_mysql_trim_left(statement_sql + col_start, statement_sql + col_end);
		col_part_end = sqlparser_mysql_trim_right(col_part_start, statement_sql + col_end);
		val_part_start = sqlparser_mysql_trim_left(statement_sql + val_start, statement_sql + val_end);
		val_part_end = sqlparser_mysql_trim_right(val_part_start, statement_sql + val_end);
		if (col_part_start >= col_part_end || val_part_start >= val_part_end) {
			sqlparser_mysql_buffer_release(&out);
			return SQLPARSER_STATUS_OK;
		}
		if (col_start != col_open + 1U) {
			status = sqlparser_mysql_buffer_append_cstr(&out, ", ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, col_part_start, (size_t)(col_part_end - col_part_start), out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, " = ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, val_part_start, (size_t)(val_part_end - val_part_start), out_error);
		}
		if (col_comma == (size_t)-1 || val_comma == (size_t)-1) {
			if (col_comma != val_comma) {
				sqlparser_mysql_buffer_release(&out);
				return SQLPARSER_STATUS_OK;
			}
			break;
		}
		col_start = col_comma + 1U;
		val_start = val_comma + 1U;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, statement_sql + val_close + 1U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_insert_set(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL || state->dml_modifier_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start <= len) {
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		if (trimmed_start < trimmed_end &&
		    sqlparser_mysql_statement_has_insert_set_mode(state, statement_index)) {
			char *statement_sql;
			char *next_sql;

			statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
			if (statement_sql == NULL) {
				sqlparser_mysql_buffer_release(&out);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			next_sql = NULL;
			status = sqlparser_mysql_restore_insert_set_statement(statement_sql, &next_sql, out_error);
			free(statement_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			if (next_sql != NULL) {
				if (!rewritten) {
					status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						free(next_sql);
						return status;
					}
					rewritten = 1;
				}
				status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, segment_start - copy_start, out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, next_sql, out_error);
				}
				free(next_sql);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				copy_start = statement_end;
			}
			statement_index++;
		} else if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_straight_join(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	size_t join_index;
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL || state->join_restore_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	join_index = 0U;
	rewritten = 0;
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			join_index = 0U;
			index++;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "join")) {
			const sqlparser_mysql_join_restore_t *restore;

			restore = sqlparser_mysql_state_join_restore(state, statement_index, join_index);
			if (restore != NULL && restore->keyword_sql != NULL) {
				if (!rewritten) {
					status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					rewritten = 1;
				}
				status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, restore->keyword_sql, out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				index += strlen("join");
				copy_start = index;
			} else {
				index += strlen("join");
			}
			join_index++;
			continue;
		}
		index++;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_relation_list_terminator(const char *sql, size_t pos)
{
	switch (tolower((unsigned char)sql[pos])) {
		case 'e':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "except");
		case 'f':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "for");
		case 'g':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "group");
		case 'h':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "having");
		case 'i':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "into") ||
				sqlparser_mysql_ascii_word_equal(sql, pos, "intersect");
		case 'l':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "limit") ||
				sqlparser_mysql_ascii_word_equal(sql, pos, "lock");
		case 'o':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "on") ||
				sqlparser_mysql_ascii_word_equal(sql, pos, "order");
		case 's':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "select") ||
				sqlparser_mysql_ascii_word_equal(sql, pos, "set");
		case 'u':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "using") ||
				sqlparser_mysql_ascii_word_equal(sql, pos, "union");
		case 'v':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "values");
		case 'w':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "where") ||
				sqlparser_mysql_ascii_word_equal(sql, pos, "window");
		default:
			return 0;
	}
}

static int sqlparser_mysql_relation_alias_boundary(const char *sql, size_t pos)
{
	if (sqlparser_mysql_relation_list_terminator(sql, pos)) {
		return 1;
	}
	switch (tolower((unsigned char)sql[pos])) {
		case 'c':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "cross");
		case 'i':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "inner");
		case 'j':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "join");
		case 'l':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "left");
		case 'n':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "natural");
		case 'r':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "right");
		case 's':
			return sqlparser_mysql_ascii_word_equal(sql, pos, "straight_join");
		default:
			return 0;
	}
}

static int sqlparser_mysql_index_hint_count_for_relation(
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t relation_index)
{
	size_t index;
	int count;

	if (state == NULL) {
		return 0;
	}
	count = 0;
	for (index = 0U; index < state->index_hint_count; index++) {
		if (state->index_hints[index].statement_index == statement_index &&
		    state->index_hints[index].relation_index == relation_index &&
		    state->index_hints[index].fragment_sql != NULL) {
			count++;
		}
	}
	return count;
}

static const sqlparser_mysql_partition_restore_t *sqlparser_mysql_partition_for_relation(
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t relation_index)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->partition_restore_count; index++) {
		const sqlparser_mysql_partition_restore_t *restore;

		restore = &state->partition_restores[index];
		if (restore->statement_index == statement_index &&
		    restore->relation_index == relation_index) {
			return restore;
		}
	}
	return NULL;
}

static int sqlparser_mysql_relation_hint_insert_pos(
	const char *sql,
	size_t relation_start,
	size_t statement_end,
	size_t *out_pos)
{
	size_t pos;
	size_t name_end;

	if (out_pos != NULL) {
		*out_pos = (size_t)-1;
	}
	pos = sqlparser_mysql_skip_space(sql, relation_start);
	if (pos >= statement_end || sql[pos] == '\0' || sql[pos] == '(') {
		return 0;
	}
	name_end = sqlparser_mysql_qualified_identifier_end(sql, pos, statement_end);
	if (name_end <= pos) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, name_end);
	if (pos < statement_end && sqlparser_mysql_ascii_word_equal(sql, pos, "as")) {
		size_t alias_pos;
		size_t alias_end;

		alias_pos = sqlparser_mysql_skip_space(sql, pos + strlen("as"));
		alias_end = sqlparser_mysql_identifier_end(sql, alias_pos, statement_end);
		if (alias_end > alias_pos) {
			pos = sqlparser_mysql_skip_space(sql, alias_end);
		}
	} else if (pos < statement_end && !sqlparser_mysql_relation_alias_boundary(sql, pos)) {
		size_t alias_end;

		alias_end = sqlparser_mysql_identifier_end(sql, pos, statement_end);
		if (alias_end > pos) {
			pos = sqlparser_mysql_skip_space(sql, alias_end);
		}
	}
	if (out_pos != NULL) {
		*out_pos = pos;
	}
	return 1;
}

static sqlparser_status_t sqlparser_mysql_append_index_hints_for_relation(
	sqlparser_mysql_buffer_t *out,
	const sqlparser_mysql_state_t *state,
	size_t statement_index,
	size_t relation_index,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	for (index = 0U; index < state->index_hint_count; index++) {
		const sqlparser_mysql_index_hint_t *hint;

		hint = &state->index_hints[index];
		if (hint->statement_index != statement_index ||
		    hint->relation_index != relation_index ||
		    hint->fragment_sql == NULL) {
			continue;
		}
		status = sqlparser_mysql_buffer_append_char(out, ' ', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(out, hint->fragment_sql, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_index_hints(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	size_t relation_index;
	size_t depth;
	unsigned char from_list_active[64];
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL ||
	    (state->index_hint_count == 0U && state->partition_restore_count == 0U)) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	relation_index = 0U;
	depth = 0U;
	memset(from_list_active, 0, sizeof(from_list_active));
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (index < len) {
		size_t skipped;
		size_t relation_start;
		size_t insert_pos;
		size_t table_start;
		size_t table_end;
		int hint_count;
		const sqlparser_mysql_partition_restore_t *partition;
		int is_relation_keyword;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			relation_index = 0U;
			depth = 0U;
			memset(from_list_active, 0, sizeof(from_list_active));
			index++;
			continue;
		}
		if (sql[index] == '(') {
			if (depth + 1U < sizeof(from_list_active)) {
				unsigned char inherited;

				inherited = from_list_active[depth];
				depth++;
				from_list_active[depth] = inherited;
			}
			index++;
			continue;
		}
		if (sql[index] == ')') {
			if (depth > 0U) {
				from_list_active[depth] = 0U;
				depth--;
			}
			index++;
			continue;
		}
		is_relation_keyword = 0;
		relation_start = 0U;
		if (sql[index] == ',' && from_list_active[depth]) {
			is_relation_keyword = 1;
			relation_start = index + 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "straight_join")) {
			is_relation_keyword = 1;
			relation_start = index + strlen("straight_join");
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "from") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "join") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "update") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "into")) {
			is_relation_keyword = 1;
			relation_start = index +
				(sqlparser_mysql_ascii_word_equal(sql, index, "update") ? strlen("update") :
					 (sqlparser_mysql_ascii_word_equal(sql, index, "join") ? strlen("join") :
					  (sqlparser_mysql_ascii_word_equal(sql, index, "into") ? strlen("into") : strlen("from"))));
		}
		if (is_relation_keyword) {
			from_list_active[depth] = 1U;
		} else if (sqlparser_mysql_relation_list_terminator(sql, index)) {
			from_list_active[depth] = 0U;
		}
		hint_count = is_relation_keyword ?
			sqlparser_mysql_index_hint_count_for_relation(state, statement_index, relation_index) :
			0;
		partition = is_relation_keyword ?
			sqlparser_mysql_partition_for_relation(state, statement_index, relation_index) :
			NULL;
		table_start = is_relation_keyword ? sqlparser_mysql_skip_space(sql, relation_start) : 0U;
		table_end = is_relation_keyword ? sqlparser_mysql_qualified_identifier_end(sql, table_start, len) : 0U;
		if (!is_relation_keyword ||
		    (hint_count == 0 && partition == NULL) ||
		    table_end <= table_start ||
		    !sqlparser_mysql_relation_hint_insert_pos(sql, relation_start, len, &insert_pos)) {
			if (is_relation_keyword) {
				relation_index++;
			}
			index++;
			continue;
		}
		if (!rewritten) {
			status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			rewritten = 1;
		}
		if (partition != NULL && partition->fragment_sql != NULL) {
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				sql + copy_start,
				table_end - copy_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, partition->fragment_sql, out_error);
			}
			copy_start = table_end;
		} else {
			status = SQLPARSER_STATUS_OK;
		}
		if (status == SQLPARSER_STATUS_OK && hint_count > 0) {
			size_t append_end;

			append_end = insert_pos;
			while (append_end > copy_start && isspace((unsigned char)sql[append_end - 1U])) {
				append_end--;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, append_end - copy_start, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && hint_count > 0) {
			status = sqlparser_mysql_append_index_hints_for_relation(
				&out,
				state,
				statement_index,
				relation_index,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && hint_count > 0 &&
		    insert_pos < len &&
		    !isspace((unsigned char)sql[insert_pos])) {
			status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (hint_count > 0) {
			copy_start = insert_pos;
		}
		index = insert_pos;
		relation_index++;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_dml_tails(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL || state->dml_tail_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start <= len) {
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;
		const sqlparser_mysql_dml_tail_t *tail;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		tail = trimmed_start < trimmed_end ? sqlparser_mysql_state_dml_tail(state, statement_index) : NULL;
		if (trimmed_start < trimmed_end) {
			statement_index++;
		}
		if (tail != NULL) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, (size_t)(trimmed_end - sql) - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK && tail->order_by_sql != NULL) {
				status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, tail->order_by_sql, out_error);
				}
			}
			if (status == SQLPARSER_STATUS_OK && tail->limit_sql != NULL) {
				status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, tail->limit_sql, out_error);
				}
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_restore_locking_reads(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL || state->lock_in_share_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (index < len) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			index++;
			continue;
		}
		if (sqlparser_mysql_state_has_lock_in_share(state, statement_index) &&
		    sqlparser_mysql_ascii_word_equal(sql, index, "for")) {
			size_t share_pos;
			size_t phrase_end;

			share_pos = sqlparser_mysql_skip_space(sql, index + strlen("for"));
			if (!sqlparser_mysql_ascii_word_equal(sql, share_pos, "share")) {
				index++;
				continue;
			}
			phrase_end = share_pos + strlen("share");
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "LOCK IN SHARE MODE", out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index = phrase_end;
			copy_start = index;
			continue;
		}
		index++;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_update_from_to_join(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	char *masked;
	const char *sql;
	const char *end;
	size_t len;
	size_t start_pos;
	size_t set_pos;
	size_t from_pos;
	size_t where_pos;
	size_t and_pos;
	const char *target_start;
	const char *target_end;
	const char *assign_start;
	const char *assign_end;
	const char *source_start;
	const char *source_end;
	const char *join_start;
	const char *join_end;
	const char *where_start;
	const char *where_end;
	sqlparser_mysql_join_kind_t join_kind;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	start_pos = (size_t)(sqlparser_mysql_trim_left(sql, sql + len) - sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "update")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", start_pos + strlen("update"), len);
	from_pos = set_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "from", set_pos + strlen("set"), len);
	where_pos = from_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", from_pos + strlen("from"), len);
	if (set_pos == (size_t)-1 || from_pos == (size_t)-1 || where_pos == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(sql, sql + len);
	and_pos = sqlparser_mysql_find_first_top_level_and(masked, where_pos + strlen("where"), (size_t)(end - sql));
	target_start = sqlparser_mysql_trim_left(sql + start_pos + strlen("update"), sql + set_pos);
	target_end = sqlparser_mysql_trim_right(target_start, sql + set_pos);
	assign_start = sqlparser_mysql_trim_left(sql + set_pos + strlen("set"), sql + from_pos);
	assign_end = sqlparser_mysql_trim_right(assign_start, sql + from_pos);
	source_start = sqlparser_mysql_trim_left(sql + from_pos + strlen("from"), sql + where_pos);
	source_end = sqlparser_mysql_trim_right(source_start, sql + where_pos);
	join_start = sqlparser_mysql_trim_left(sql + where_pos + strlen("where"), and_pos == (size_t)-1 ? end : sql + and_pos);
	join_end = sqlparser_mysql_trim_right(join_start, and_pos == (size_t)-1 ? end : sql + and_pos);
	where_start = and_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(sql + and_pos + strlen("and"), end);
	where_end = where_start == NULL ? NULL : sqlparser_mysql_trim_right(where_start, end);
	free(masked);
	if (target_start >= target_end || assign_start >= assign_end || source_start >= source_end ||
	    join_start >= join_end) {
		return SQLPARSER_STATUS_OK;
	}
	join_kind = SQLPARSER_MYSQL_JOIN_INNER;
	(void)sqlparser_mysql_unwrap_join_on_call(&join_start, &join_end, &join_kind);
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "UPDATE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start, (size_t)(target_end - target_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, sqlparser_mysql_join_keyword(join_kind), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start, (size_t)(source_end - source_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " ON ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, join_start, (size_t)(join_end - join_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " SET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, assign_start, (size_t)(assign_end - assign_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start != NULL && where_start < where_end) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start, (size_t)(where_end - where_start), out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_delete_using_to_join(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	char *masked;
	const char *sql;
	const char *end;
	size_t len;
	size_t start_pos;
	size_t using_pos;
	size_t where_pos;
	size_t and_pos;
	const char *target_start;
	const char *target_end;
	const char *source_start;
	const char *source_end;
	const char *join_start;
	const char *join_end;
	const char *where_start;
	const char *where_end;
	const char *alias_start;
	const char *alias_end;
	sqlparser_mysql_join_kind_t join_kind;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	start_pos = (size_t)(sqlparser_mysql_trim_left(sql, sql + len) - sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	using_pos = sqlparser_mysql_find_top_level_word_between(masked, "using", start_pos + strlen("delete"), len);
	where_pos = using_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", using_pos + strlen("using"), len);
	if (using_pos == (size_t)-1 || where_pos == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(sql, sql + len);
	and_pos = sqlparser_mysql_find_first_top_level_and(masked, where_pos + strlen("where"), (size_t)(end - sql));
	target_start = sqlparser_mysql_trim_left(sql + start_pos + strlen("delete from"), sql + using_pos);
	target_end = sqlparser_mysql_trim_right(target_start, sql + using_pos);
	source_start = sqlparser_mysql_trim_left(sql + using_pos + strlen("using"), sql + where_pos);
	source_end = sqlparser_mysql_trim_right(source_start, sql + where_pos);
	join_start = sqlparser_mysql_trim_left(sql + where_pos + strlen("where"), and_pos == (size_t)-1 ? end : sql + and_pos);
	join_end = sqlparser_mysql_trim_right(join_start, and_pos == (size_t)-1 ? end : sql + and_pos);
	where_start = and_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(sql + and_pos + strlen("and"), end);
	where_end = where_start == NULL ? NULL : sqlparser_mysql_trim_right(where_start, end);
	free(masked);
	if (target_start >= target_end || source_start >= source_end || join_start >= join_end) {
		return SQLPARSER_STATUS_OK;
	}
	join_kind = SQLPARSER_MYSQL_JOIN_INNER;
	(void)sqlparser_mysql_unwrap_join_on_call(&join_start, &join_end, &join_kind);
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(target_start, target_end, &alias_start, &alias_end);
	if (alias_start == NULL || alias_end == NULL) {
		alias_start = target_start;
		alias_end = target_end;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "DELETE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, alias_start, (size_t)(alias_end - alias_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start, (size_t)(target_end - target_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, sqlparser_mysql_join_keyword(join_kind), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start, (size_t)(source_end - source_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " ON ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, join_start, (size_t)(join_end - join_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start != NULL && where_start < where_end) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start, (size_t)(where_end - where_start), out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_use_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	const char *start;
	const char *end;
	const char *value_start;
	const char *value_end;
	const char *prefix;
	size_t prefix_len;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	start = sqlparser_mysql_trim_left(sql, sql + strlen(sql));
	end = sqlparser_mysql_trim_right(start, sql + strlen(sql));
	prefix = "SET " SQLPARSER_INTERNAL_CURRENT_DATABASE " TO ";
	prefix_len = strlen(prefix);
	if ((size_t)(end - start) < prefix_len ||
	    strncmp(start, prefix, prefix_len) != 0) {
		return SQLPARSER_STATUS_OK;
	}

	value_start = start + prefix_len;
	value_end = sqlparser_mysql_trim_right(value_start, end);
	if (value_start >= value_end) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_append_cstr(&out, "USE ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, value_start, (size_t)(value_end - value_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	size_t pos;
	size_t token_start;
	size_t token_end;
	char quote;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal argument output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	pos = sqlparser_mysql_skip_space(sql, *index);
	if (sql[pos] == '\'' || sql[pos] == '"' || sql[pos] == '`') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_mysql_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_mysql_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_mysql_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_mysql_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_mysql_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = pos;
	while (token_end > token_start && isspace((unsigned char)sql[token_end - 1U])) {
		token_end--;
	}
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	*out_value = sqlparser_strndup(sql + token_start, token_end - token_start);
	if (*out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_internal_set_prefix(
	const char *sql,
	const char *internal_name,
	size_t *out_pos)
{
	size_t pos;
	size_t len;

	pos = sqlparser_mysql_skip_space(sql, 0U);
	if (!sqlparser_mysql_ascii_word_equal(sql, pos, "set")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, pos + strlen("set"));
	len = strlen(internal_name);
	if (strncmp(sql + pos, internal_name, len) != 0 ||
	    sqlparser_mysql_is_ident_char((unsigned char)sql[pos + len])) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, pos + len);
	if (!sqlparser_mysql_ascii_word_equal(sql, pos, "to") && sql[pos] != '=') {
		return 0;
	}
	pos = sql[pos] == '=' ? pos + 1U : pos + strlen("to");
	*out_pos = pos;
	return 1;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_session_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	const char *sql;
	size_t pos;
	size_t end;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	if (!sqlparser_mysql_internal_set_prefix(
		    sql,
		    SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT,
		    &pos)) {
		return SQLPARSER_STATUS_OK;
	}
	public_sql = NULL;
	status = sqlparser_mysql_read_internal_string_arg(sql, &pos, &public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	end = sqlparser_mysql_skip_space(sql, pos);
	if (sql[end] != '\0') {
		free(public_sql);
		return SQLPARSER_STATUS_OK;
	}
	free(*io_sql);
	*io_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_prepared_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	static const struct {
		const char *internal_name;
		const char *prefix;
		const char *middle;
		int needs_second_arg;
	} specs[] = {
		{SQLPARSER_INTERNAL_MYSQL_PREPARE, "PREPARE ", " FROM ", 1},
		{SQLPARSER_INTERNAL_MYSQL_EXECUTE, "EXECUTE ", " USING ", 0},
		{SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE, "DEALLOCATE PREPARE ", NULL, 0},
		{SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE, "DROP PREPARE ", NULL, 0}
	};
	sqlparser_mysql_buffer_t out;
	const char *sql;
	char *arg0;
	char *arg1;
	size_t index;
	size_t pos;
	size_t spec_index;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	for (spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); spec_index++) {
		if (!sqlparser_mysql_internal_set_prefix(sql, specs[spec_index].internal_name, &pos)) {
			continue;
		}
		arg0 = NULL;
		arg1 = NULL;
		index = pos;
		status = sqlparser_mysql_read_internal_string_arg(sql, &index, &arg0, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(arg0);
			return status;
		}
		index = sqlparser_mysql_skip_space(sql, index);
		if (sql[index] == ',') {
			index++;
			status = sqlparser_mysql_read_internal_string_arg(sql, &index, &arg1, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(arg0);
				free(arg1);
				return status;
			}
			index = sqlparser_mysql_skip_space(sql, index);
		}
		if (sql[index] != '\0') {
			free(arg0);
			free(arg1);
			return SQLPARSER_STATUS_OK;
		}
		if (specs[spec_index].needs_second_arg && arg1 == NULL) {
			free(arg0);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "prepared statement SQL argument is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		memset(&out, 0, sizeof(out));
		status = sqlparser_mysql_buffer_append_cstr(&out, specs[spec_index].prefix, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, arg0, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && arg1 != NULL && specs[spec_index].middle != NULL) {
			status = sqlparser_mysql_buffer_append_cstr(&out, specs[spec_index].middle, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, arg1, out_error);
			}
		}
		free(arg0);
		free(arg1);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
		if (*io_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_use(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	char *statement_sql;
	char *original_statement_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		original_statement_sql = statement_sql;
		status = sqlparser_mysql_rewrite_internal_use_statement(&statement_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && statement_sql == original_statement_sql) {
			status = sqlparser_mysql_rewrite_internal_session_statement(&statement_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && statement_sql == original_statement_sql) {
			status = sqlparser_mysql_rewrite_internal_prepared_statement(&statement_sql, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(statement_sql);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (statement_sql != original_statement_sql) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(statement_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = (size_t)(sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end) - sql);
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, statement_sql, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(statement_sql);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		free(statement_sql);
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_dml_modifiers_to_mysql(
	char **io_sql,
	const sqlparser_mysql_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (state == NULL || state->dml_modifier_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start <= len) {
		size_t statement_end;
		const char *trimmed_start;
		const char *trimmed_end;
		size_t keyword_start;
		size_t keyword_end;
		unsigned int flags;
		sqlparser_mysql_dml_modifier_kind_t kind;
		const sqlparser_mysql_dml_modifier_t *replace_modifier;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		trimmed_start = sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end);
		trimmed_end = sqlparser_mysql_trim_right(trimmed_start, sql + statement_end);
		if (trimmed_start >= trimmed_end) {
			if (statement_end >= len) {
				break;
			}
			segment_start = statement_end + 1U;
			continue;
		}

		kind = (sqlparser_mysql_dml_modifier_kind_t)0;
		keyword_start = (size_t)(trimmed_start - sql);
		if (sqlparser_mysql_ascii_word_equal(sql, keyword_start, "insert")) {
			replace_modifier = sqlparser_mysql_state_dml_modifier(
				state,
				statement_index,
				SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE);
			kind = replace_modifier != NULL ?
				SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE :
				SQLPARSER_MYSQL_DML_MODIFIER_KIND_INSERT;
		} else if (sqlparser_mysql_ascii_word_equal(sql, keyword_start, "update")) {
			kind = SQLPARSER_MYSQL_DML_MODIFIER_KIND_UPDATE;
		} else if (sqlparser_mysql_ascii_word_equal(sql, keyword_start, "delete")) {
			kind = SQLPARSER_MYSQL_DML_MODIFIER_KIND_DELETE;
		}
		flags = kind == 0 ? 0U : sqlparser_mysql_state_dml_modifier_flags(state, statement_index, kind);
		statement_index++;
		if (flags != 0U || kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				rewritten = 1;
			}
			keyword_end = keyword_start + (kind == SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE ?
				strlen("insert") :
				strlen(sqlparser_mysql_dml_modifier_keyword(kind)));
			status = sqlparser_mysql_buffer_append_mem(
				&out,
				sql + copy_start,
				keyword_start - copy_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(
					&out,
					sqlparser_mysql_dml_modifier_keyword(kind),
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_char(&out, ' ', out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_append_dml_modifier_sql(&out, kind, flags, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			copy_start = sqlparser_mysql_skip_space(sql, keyword_end);
		}

		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_locking_reads(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	rewritten = 0;
	memset(&origin, 0, sizeof(origin));
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			index++;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "lock")) {
			size_t in_pos;
			size_t share_pos;
			size_t mode_pos;

			in_pos = sqlparser_mysql_skip_space(sql, index + strlen("lock"));
			share_pos = sqlparser_mysql_ascii_word_equal(sql, in_pos, "in") ?
				sqlparser_mysql_skip_space(sql, in_pos + strlen("in")) :
				(size_t)-1;
			mode_pos = share_pos != (size_t)-1 && sqlparser_mysql_ascii_word_equal(sql, share_pos, "share") ?
				sqlparser_mysql_skip_space(sql, share_pos + strlen("share")) :
				(size_t)-1;
			if (mode_pos != (size_t)-1 && sqlparser_mysql_ascii_word_equal(sql, mode_pos, "mode")) {
				size_t phrase_end;

				phrase_end = mode_pos + strlen("mode");
				if (!rewritten) {
					status = sqlparser_mysql_buffer_begin_origin(
						&out,
						origins != NULL ? &origin : NULL,
						sql,
						0U,
						out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_mysql_origin_trace_release(&origin);
						return status;
					}
					status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_mysql_origin_trace_release(&origin);
						return status;
					}
					rewritten = 1;
				}
				status = sqlparser_mysql_state_add_lock_in_share(state, statement_index, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_mysql_buffer_append_cstr(&out, "FOR SHARE", out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				index = phrase_end;
				copy_start = index;
				continue;
			}
		}
		index++;
	}
	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			sql,
			out.data,
			&origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&origin);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_straight_join(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	size_t join_index;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	join_index = 0U;
	rewritten = 0;
	memset(&origin, 0, sizeof(origin));
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			join_index = 0U;
			index++;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "straight_join")) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origins != NULL ? &origin : NULL,
					sql,
					0U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_state_add_join_restore(
				state,
				statement_index,
				join_index,
				sql + index,
				strlen("straight_join"),
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "JOIN", out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index += strlen("straight_join");
			copy_start = index;
			join_index++;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "join")) {
			join_index++;
			index += strlen("join");
			continue;
		}
		index++;
	}
	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			sql,
			out.data,
			&origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&origin);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_index_hints(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	const char *sql;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	size_t current_relation_index;
	size_t next_relation_index;
	size_t current_relation_location;
	size_t removed_bytes;
	size_t depth;
	unsigned char from_list_active[64];
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	current_relation_index = 0U;
	next_relation_index = 0U;
	current_relation_location = 0U;
	removed_bytes = 0U;
	depth = 0U;
	memset(from_list_active, 0, sizeof(from_list_active));
	rewritten = 0;
	memset(&origin, 0, sizeof(origin));
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			current_relation_index = 0U;
			next_relation_index = 0U;
			current_relation_location = 0U;
			depth = 0U;
			memset(from_list_active, 0, sizeof(from_list_active));
			index++;
			continue;
		}
		if (sql[index] == '(') {
			if (depth + 1U < sizeof(from_list_active)) {
				unsigned char inherited;

				inherited = from_list_active[depth];
				depth++;
				from_list_active[depth] = inherited;
			}
			index++;
			continue;
		}
		if (from_list_active[depth] && sqlparser_mysql_ascii_word_equal(sql, index, "partition")) {
			size_t open_pos;
			size_t close_pos;
			size_t nested;

			open_pos = sqlparser_mysql_skip_space(sql, index + strlen("partition"));
			if (sql[open_pos] == '(') {
				close_pos = open_pos;
				nested = 0U;
				while (sql[close_pos] != '\0') {
					size_t part_skipped;

					part_skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, close_pos);
					if (part_skipped > close_pos) {
						close_pos = part_skipped;
						continue;
					}
					if (sql[close_pos] == '(') {
						nested++;
					} else if (sql[close_pos] == ')' && nested > 0U && --nested == 0U) {
						close_pos++;
						break;
					}
					close_pos++;
				}
				index = close_pos;
				continue;
			}
		}
		if (sql[index] == ')') {
			if (depth > 0U) {
				from_list_active[depth] = 0U;
				depth--;
			}
			index++;
			continue;
		}
		if (sql[index] == ',' && from_list_active[depth]) {
			current_relation_index = next_relation_index;
			next_relation_index++;
			current_relation_location = sqlparser_mysql_skip_space(sql, index + 1U) - removed_bytes;
			index++;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "from") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "join") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "update")) {
			current_relation_index = next_relation_index;
			next_relation_index++;
			from_list_active[depth] = 1U;
			index += sqlparser_mysql_ascii_word_equal(sql, index, "update") ? strlen("update") :
				(sqlparser_mysql_ascii_word_equal(sql, index, "join") ? strlen("join") : strlen("from"));
			current_relation_location = sqlparser_mysql_skip_space(sql, index) - removed_bytes;
			continue;
		}
		if (sqlparser_mysql_relation_list_terminator(sql, index)) {
			from_list_active[depth] = 0U;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "use") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "force") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "ignore")) {
			size_t keyword_pos;
			size_t index_pos;
			size_t after_index_pos;
			size_t scope_pos;
			size_t open_search_pos;
			size_t open_pos;
			size_t close_pos;

			keyword_pos = index;
			index_pos = sqlparser_mysql_skip_space(sql, keyword_pos +
				(sqlparser_mysql_ascii_word_equal(sql, keyword_pos, "ignore") ? strlen("ignore") :
				 (sqlparser_mysql_ascii_word_equal(sql, keyword_pos, "force") ? strlen("force") : strlen("use"))));
			if (!sqlparser_mysql_ascii_word_equal(sql, index_pos, "index") &&
			    !sqlparser_mysql_ascii_word_equal(sql, index_pos, "key")) {
				index++;
				continue;
			}
			after_index_pos = index_pos + (sqlparser_mysql_ascii_word_equal(sql, index_pos, "key") ?
				strlen("key") :
				strlen("index"));
			open_search_pos = after_index_pos;
			scope_pos = sqlparser_mysql_skip_space(sql, after_index_pos);
			if (sqlparser_mysql_ascii_word_equal(sql, scope_pos, "for")) {
				size_t scope_word_pos;
				size_t by_pos;

				scope_word_pos = sqlparser_mysql_skip_space(sql, scope_pos + strlen("for"));
				if (sqlparser_mysql_ascii_word_equal(sql, scope_word_pos, "join")) {
					open_search_pos = scope_word_pos + strlen("join");
				} else if (sqlparser_mysql_ascii_word_equal(sql, scope_word_pos, "order")) {
					by_pos = sqlparser_mysql_skip_space(sql, scope_word_pos + strlen("order"));
					if (!sqlparser_mysql_ascii_word_equal(sql, by_pos, "by")) {
						index++;
						continue;
					}
					open_search_pos = by_pos + strlen("by");
				} else if (sqlparser_mysql_ascii_word_equal(sql, scope_word_pos, "group")) {
					by_pos = sqlparser_mysql_skip_space(sql, scope_word_pos + strlen("group"));
					if (!sqlparser_mysql_ascii_word_equal(sql, by_pos, "by")) {
						index++;
						continue;
					}
					open_search_pos = by_pos + strlen("by");
				} else {
					index++;
					continue;
				}
			}
			open_pos = sqlparser_mysql_skip_space(sql, open_search_pos);
			if (sql[open_pos] != '(') {
				index++;
				continue;
			}
			close_pos = open_pos + 1U;
			while (sql[close_pos] != '\0' && sql[close_pos] != ')') {
				close_pos++;
			}
			if (sql[close_pos] != ')') {
				index++;
				continue;
			}
			status = sqlparser_mysql_state_add_index_hint(
				state,
				statement_index,
				current_relation_index,
				current_relation_location <= (size_t)INT_MAX ? (int)current_relation_location : -1,
				sql + keyword_pos,
				close_pos + 1U - keyword_pos,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origins != NULL ? &origin : NULL,
					sql,
					0U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, keyword_pos - copy_start, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index = close_pos + 1U;
			copy_start = index;
			removed_bytes += close_pos + 1U - keyword_pos;
			continue;
		}
		index++;
	}
	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			sql,
			out.data,
			&origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&origin);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_table_partitions(
	char **io_sql,
	sqlparser_mysql_state_t *state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	const char *sql;
	sqlparser_mysql_buffer_t out;
	sqlparser_mysql_origin_trace_t origin;
	sqlparser_status_t status;
	size_t index;
	size_t copy_start;
	size_t statement_index;
	size_t current_relation_index;
	size_t next_relation_index;
	size_t current_relation_location;
	size_t removed_bytes;
	size_t depth;
	unsigned char from_list_active[64];
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	statement_index = 0U;
	current_relation_index = 0U;
	next_relation_index = 0U;
	current_relation_location = 0U;
	removed_bytes = 0U;
	depth = 0U;
	memset(from_list_active, 0, sizeof(from_list_active));
	rewritten = 0;
	memset(&origin, 0, sizeof(origin));
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			statement_index++;
			current_relation_index = 0U;
			next_relation_index = 0U;
			current_relation_location = 0U;
			depth = 0U;
			memset(from_list_active, 0, sizeof(from_list_active));
			index++;
			continue;
		}
		if (from_list_active[depth] && sqlparser_mysql_ascii_word_equal(sql, index, "partition")) {
			size_t open_pos;
			size_t close_pos;
			size_t nested;
			size_t removal_len;

			open_pos = sqlparser_mysql_skip_space(sql, index + strlen("partition"));
			if (sql[open_pos] != '(') {
				index++;
				continue;
			}
			close_pos = open_pos;
			nested = 0U;
			while (sql[close_pos] != '\0') {
				size_t part_skipped;

				part_skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, close_pos);
				if (part_skipped > close_pos) {
					close_pos = part_skipped;
					continue;
				}
				if (sql[close_pos] == '(') {
					nested++;
				} else if (sql[close_pos] == ')' && nested > 0U && --nested == 0U) {
					close_pos++;
					break;
				}
				close_pos++;
			}
			if (nested != 0U) {
				index++;
				continue;
			}
			removal_len = close_pos - index;
			status = sqlparser_mysql_state_add_partition_restore(
				state,
				statement_index,
				current_relation_index,
				current_relation_location <= (size_t)INT_MAX ? (int)current_relation_location : -1,
				sql + index,
				removal_len,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			if (!rewritten) {
				status = sqlparser_mysql_buffer_begin_origin(
					&out,
					origins != NULL ? &origin : NULL,
					sql,
					0U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_origin_trace_release(&origin);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_origin_trace_release(&origin);
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			sqlparser_mysql_adjust_hint_locations_after_removal(
				state,
				index - removed_bytes,
				removal_len);
			removed_bytes += removal_len;
			index = close_pos;
			copy_start = index;
			continue;
		}
		if (sql[index] == '(') {
			if (depth + 1U < sizeof(from_list_active)) {
				unsigned char inherited;

				inherited = from_list_active[depth];
				depth++;
				from_list_active[depth] = inherited;
			}
			index++;
			continue;
		}
		if (sql[index] == ')') {
			if (depth > 0U) {
				from_list_active[depth] = 0U;
				depth--;
			}
			index++;
			continue;
		}
		if (sql[index] == ',' && from_list_active[depth]) {
			current_relation_index = next_relation_index++;
			current_relation_location = sqlparser_mysql_skip_space(sql, index + 1U) - removed_bytes;
			index++;
			continue;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "from") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "join") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "update") ||
		    sqlparser_mysql_ascii_word_equal(sql, index, "into")) {
			current_relation_index = next_relation_index++;
			from_list_active[depth] = 1U;
			index += sqlparser_mysql_ascii_word_equal(sql, index, "update") ? strlen("update") :
				(sqlparser_mysql_ascii_word_equal(sql, index, "join") ? strlen("join") :
				 (sqlparser_mysql_ascii_word_equal(sql, index, "into") ? strlen("into") : strlen("from")));
			current_relation_location = sqlparser_mysql_skip_space(sql, index) - removed_bytes;
			continue;
		}
		if (sqlparser_mysql_relation_list_terminator(sql, index)) {
			from_list_active[depth] = 0U;
		}
		index++;
	}
	if (!rewritten) {
		sqlparser_mysql_origin_trace_release(&origin);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			sql,
			out.data,
			&origin,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&origin);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

typedef struct {
	unsigned int straight_join : 1;
	unsigned int index_hint : 1;
	unsigned int table_partition : 1;
	unsigned int locking_read : 1;
} sqlparser_mysql_extension_features_t;

static sqlparser_mysql_extension_features_t sqlparser_mysql_classify_extensions(const char *sql)
{
	sqlparser_mysql_extension_features_t features;
	size_t index;

	memset(&features, 0, sizeof(features));
	if (sql == NULL) {
		return features;
	}
	index = 0U;
	while (sql[index] != '\0') {
		size_t skipped;
		size_t word_end;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (!sqlparser_mysql_is_ident_start((unsigned char)sql[index])) {
			index++;
			continue;
		}
		word_end = index + 1U;
		while (sqlparser_mysql_is_ident_char((unsigned char)sql[word_end])) {
			word_end++;
		}
		if (sqlparser_mysql_ascii_word_equal(sql, index, "straight_join")) {
			features.straight_join = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "partition")) {
			features.table_partition = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "lock")) {
			features.locking_read = 1U;
		} else if (sqlparser_mysql_ascii_word_equal(sql, index, "use") ||
			   sqlparser_mysql_ascii_word_equal(sql, index, "force") ||
			   sqlparser_mysql_ascii_word_equal(sql, index, "ignore")) {
			size_t next_word;

			next_word = sqlparser_mysql_skip_space(sql, word_end);
			if (sqlparser_mysql_ascii_word_equal(sql, next_word, "index") ||
			    sqlparser_mysql_ascii_word_equal(sql, next_word, "key")) {
				features.index_hint = 1U;
			}
		}
		if (features.straight_join && features.index_hint &&
		    features.table_partition && features.locking_read) {
			break;
		}
		index = word_end;
	}
	return features;
}

static sqlparser_status_t sqlparser_mysql_preprocess_internal(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	char *quoted_sql;
	sqlparser_mysql_state_t *mysql_state;
	sqlparser_mysql_extension_features_t features;
	sqlparser_status_t status;

	(void)limits;

	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect preprocess output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;

	mysql_state = NULL;
	status = sqlparser_mysql_state_new(&mysql_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	quoted_sql = sqlparser_strdup(input_sql);
	if (quoted_sql == NULL) {
		sqlparser_mysql_state_destroy(mysql_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_mysql_rewrite_create_table_extensions(
		&quoted_sql,
		mysql_state,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	{
		char *mysql_sql;
		sqlparser_mysql_origin_trace_t quote_origin;

		mysql_sql = quoted_sql;
		quoted_sql = NULL;
		memset(&quote_origin, 0, sizeof(quote_origin));
		status = sqlparser_mysql_preprocess_quotes(
			mysql_sql,
			mysql_state,
			&quoted_sql,
			0U,
			origins != NULL ? &quote_origin : NULL,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_origin_trace_commit(
				origins,
				mysql_sql,
				quoted_sql,
				&quote_origin,
				out_error);
		}
		sqlparser_mysql_origin_trace_release(&quote_origin);
		free(mysql_sql);
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_rewrite_session_statements(
		&quoted_sql,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_rewrite_dml_extensions(
		&quoted_sql,
		mysql_state,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	features = sqlparser_mysql_classify_extensions(quoted_sql);
	if (features.straight_join) {
		status = sqlparser_mysql_rewrite_straight_join(
			&quoted_sql,
			mysql_state,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(quoted_sql);
			sqlparser_mysql_state_destroy(mysql_state);
			return status;
		}
	}

	if (features.index_hint) {
		status = sqlparser_mysql_rewrite_index_hints(
			&quoted_sql,
			mysql_state,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(quoted_sql);
			sqlparser_mysql_state_destroy(mysql_state);
			return status;
		}
	}

	if (features.table_partition) {
		status = sqlparser_mysql_rewrite_table_partitions(
			&quoted_sql,
			mysql_state,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(quoted_sql);
			sqlparser_mysql_state_destroy(mysql_state);
			return status;
		}
	}

	if (features.locking_read) {
		status = sqlparser_mysql_rewrite_locking_reads(
			&quoted_sql,
			mysql_state,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(quoted_sql);
			sqlparser_mysql_state_destroy(mysql_state);
			return status;
		}
	}

	status = sqlparser_mysql_reject_unsupported(quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_rewrite_limit_offset_count(
		&quoted_sql,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	*out_parser_sql = quoted_sql;
	*out_state = mysql_state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_preprocess_internal(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_mysql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin map must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_mysql_preprocess_internal(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_preprocess_fragment(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	(void)statement_index;
	return sqlparser_mysql_preprocess_fragment_identifier_origins(
		input_sql,
		state,
		out_parser_sql,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_mysql_preprocess_fragment_identifier_origins(
	const char *input_sql,
	void *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_origin_trace_t trace;
	sqlparser_status_t status;

	if (out_parser_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL fragment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&trace, 0, sizeof(trace));
	status = sqlparser_mysql_preprocess_quotes(
		input_sql,
		(sqlparser_mysql_state_t *)state,
		out_parser_sql,
		0U,
		origins != NULL ? &trace : NULL,
		out_error);
	if (status == SQLPARSER_STATUS_OK && origins != NULL) {
		status = sqlparser_mysql_origin_trace_commit(
			origins,
			input_sql,
			*out_parser_sql,
			&trace,
			out_error);
	}
	sqlparser_mysql_origin_trace_release(&trace);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_parser_sql);
		*out_parser_sql = NULL;
	}
	return status;
}

static sqlparser_status_t sqlparser_mysql_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *quoted_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;

	quoted_sql = NULL;
	status = sqlparser_mysql_rewrite_pg_quotes_to_backticks(
		core_sql,
		(const sqlparser_mysql_state_t *)state,
		&quoted_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_mysql_restore_create_table_extensions_to_mysql(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_limit_count_offset_to_mysql(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_on_conflict_to_duplicate_key(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_restore_on_duplicate_references(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_restore_insert_set(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_update_from_to_join(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_delete_using_to_join(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_restore_straight_join(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_restore_index_hints(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_restore_locking_reads(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_restore_dml_tails(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_internal_use(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_dml_modifiers_to_mysql(
		&quoted_sql,
		(const sqlparser_mysql_state_t *)state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_dialect_rewrite_like_escape(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}

	*out_sql = quoted_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_postprocess_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	(void)statement_index;
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "core SQL fragment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_mysql_rewrite_pg_quotes_to_backticks(
		core_sql,
		(const sqlparser_mysql_state_t *)state,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_mysql_state_t *mysql_state;
	size_t literal_end;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	(void)statement_index;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	mysql_state = (const sqlparser_mysql_state_t *)state;
	literal_end = core_sql[0] == '\'' ? sqlparser_mysql_quoted_literal_end(core_sql, 0U) : 0U;
	if (literal_end > 0U &&
	    core_sql[literal_end] == '\0' &&
	    sqlparser_mysql_national_literal_matches(mysql_state, literal_index, core_sql, 0U, literal_end)) {
		memset(&out, 0, sizeof(out));
		status = sqlparser_mysql_buffer_append_char(&out, 'N', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, core_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_mysql_buffer_take(&out);
		return SQLPARSER_STATUS_OK;
	}

	*out_sql = sqlparser_strdup(core_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_mysql_state_t *source;
	sqlparser_mysql_state_t *clone;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	source = (const sqlparser_mysql_state_t *)state;
	status = sqlparser_mysql_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->positional_param_count = source->positional_param_count;
	clone->literal_count = source->literal_count;
	if (source->national_literal_count > 0U) {
		size_t index;

		for (index = 0U; index < source->national_literal_count; index++) {
			status = sqlparser_mysql_store_national_literal(
				clone,
				source->national_literals[index],
				strlen(source->national_literals[index]),
				source->national_literal_ordinals[index],
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	if (source->dml_modifier_count > 0U) {
		if (source->dml_modifier_count > ((size_t)-1) / sizeof(*clone->dml_modifiers)) {
			sqlparser_mysql_state_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->dml_modifiers = (sqlparser_mysql_dml_modifier_t *)malloc(
			source->dml_modifier_count * sizeof(*clone->dml_modifiers));
		if (clone->dml_modifiers == NULL) {
			sqlparser_mysql_state_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		memcpy(
			clone->dml_modifiers,
			source->dml_modifiers,
			source->dml_modifier_count * sizeof(*clone->dml_modifiers));
		clone->dml_modifier_count = source->dml_modifier_count;
		clone->dml_modifier_capacity = source->dml_modifier_count;
	}
	if (source->create_column_restore_count > 0U) {
		size_t index;

		if (source->create_column_restore_count > ((size_t)-1) / sizeof(*clone->create_column_restores)) {
			sqlparser_mysql_state_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->create_column_restores = (sqlparser_mysql_create_column_restore_t *)calloc(
			source->create_column_restore_count,
			sizeof(*clone->create_column_restores));
		if (clone->create_column_restores == NULL) {
			sqlparser_mysql_state_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		for (index = 0U; index < source->create_column_restore_count; index++) {
			clone->create_column_restores[index].statement_index =
				source->create_column_restores[index].statement_index;
			clone->create_column_restores[index].column_ordinal =
				source->create_column_restores[index].column_ordinal;
			clone->create_column_restores[index].segment_sql =
				sqlparser_strdup(source->create_column_restores[index].segment_sql);
			if (clone->create_column_restores[index].segment_sql == NULL) {
				sqlparser_mysql_state_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			clone->create_column_restore_count = index + 1U;
		}
		clone->create_column_restore_capacity = source->create_column_restore_count;
	}
	if (source->create_table_restore_count > 0U) {
		size_t index;

		if (source->create_table_restore_count > ((size_t)-1) / sizeof(*clone->create_table_restores)) {
			sqlparser_mysql_state_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->create_table_restores = (sqlparser_mysql_create_table_restore_t *)calloc(
			source->create_table_restore_count,
			sizeof(*clone->create_table_restores));
		if (clone->create_table_restores == NULL) {
			sqlparser_mysql_state_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		for (index = 0U; index < source->create_table_restore_count; index++) {
			clone->create_table_restores[index].statement_index =
				source->create_table_restores[index].statement_index;
			clone->create_table_restores[index].options_sql =
				sqlparser_strdup(source->create_table_restores[index].options_sql);
			if (clone->create_table_restores[index].options_sql == NULL) {
				sqlparser_mysql_state_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			clone->create_table_restore_count = index + 1U;
		}
		clone->create_table_restore_capacity = source->create_table_restore_count;
	}
	if (source->on_duplicate_restore_count > 0U) {
		size_t index;

		for (index = 0U; index < source->on_duplicate_restore_count; index++) {
			status = sqlparser_mysql_state_add_on_duplicate_restore(
				clone,
				source->on_duplicate_restores[index].statement_index,
				source->on_duplicate_restores[index].kind,
				source->on_duplicate_restores[index].alias_name,
				source->on_duplicate_restores[index].alias_name != NULL ?
					strlen(source->on_duplicate_restores[index].alias_name) :
					0U,
				source->on_duplicate_restores[index].alias_columns_sql,
				source->on_duplicate_restores[index].alias_columns_sql != NULL ?
					strlen(source->on_duplicate_restores[index].alias_columns_sql) :
					0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	if (source->index_hint_count > 0U) {
		size_t index;

		for (index = 0U; index < source->index_hint_count; index++) {
			status = sqlparser_mysql_state_add_index_hint(
				clone,
				source->index_hints[index].statement_index,
				source->index_hints[index].relation_index,
				source->index_hints[index].relation_location,
				source->index_hints[index].fragment_sql,
				source->index_hints[index].fragment_sql != NULL ? strlen(source->index_hints[index].fragment_sql) : 0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	if (source->partition_restore_count > 0U) {
		size_t index;

		for (index = 0U; index < source->partition_restore_count; index++) {
			status = sqlparser_mysql_state_add_partition_restore(
				clone,
				source->partition_restores[index].statement_index,
				source->partition_restores[index].relation_index,
				source->partition_restores[index].relation_location,
				source->partition_restores[index].fragment_sql,
				source->partition_restores[index].fragment_sql != NULL ?
					strlen(source->partition_restores[index].fragment_sql) :
					0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	if (source->join_restore_count > 0U) {
		size_t index;

		for (index = 0U; index < source->join_restore_count; index++) {
			status = sqlparser_mysql_state_add_join_restore(
				clone,
				source->join_restores[index].statement_index,
				source->join_restores[index].join_index,
				source->join_restores[index].keyword_sql,
				source->join_restores[index].keyword_sql != NULL ? strlen(source->join_restores[index].keyword_sql) : 0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	if (source->dml_tail_count > 0U) {
		size_t index;

		for (index = 0U; index < source->dml_tail_count; index++) {
			status = sqlparser_mysql_state_add_dml_tail(
				clone,
				source->dml_tails[index].statement_index,
				source->dml_tails[index].order_by_sql,
				source->dml_tails[index].order_by_sql != NULL ? strlen(source->dml_tails[index].order_by_sql) : 0U,
				source->dml_tails[index].limit_sql,
				source->dml_tails[index].limit_sql != NULL ? strlen(source->dml_tails[index].limit_sql) : 0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	if (source->lock_in_share_count > 0U) {
		size_t index;

		for (index = 0U; index < source->lock_in_share_count; index++) {
			status = sqlparser_mysql_state_add_lock_in_share(
				clone,
				source->lock_in_share_statements[index],
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_state_destroy(clone);
				return status;
			}
		}
	}
	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_mysql_statement_keyword(
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement)
{
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
	    sqlparser_mysql_state_dml_modifier(
		    (const sqlparser_mysql_state_t *)state,
		    statement_index,
		    SQLPARSER_MYSQL_DML_MODIFIER_KIND_REPLACE) == NULL) {
		return NULL;
	}
	return "replace";
}

static sqlparser_graph_insert_mode_t sqlparser_mysql_insert_mode(
	const void *state,
	size_t statement_index,
	sqlparser_graph_insert_mode_t core_mode)
{
	sqlparser_graph_insert_mode_t override_mode;

	override_mode = sqlparser_mysql_state_insert_mode_override(
		(const sqlparser_mysql_state_t *)state,
		statement_index);
	if (override_mode == SQLPARSER_GRAPH_INSERT_MODE_UNKNOWN) {
		return core_mode;
	}
	return override_mode;
}

static const char *sqlparser_mysql_session_ast_argument(
	const PgQuery__Node *statement,
	const char *internal_name)
{
	const PgQuery__VariableSetStmt *set_stmt;
	const PgQuery__Node *arg;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    statement->variable_set_stmt->name == NULL ||
	    strcmp(statement->variable_set_stmt->name, internal_name) != 0) {
		return NULL;
	}
	set_stmt = statement->variable_set_stmt;
	if (set_stmt->n_args != 1U || set_stmt->args == NULL) {
		return NULL;
	}
	arg = set_stmt->args[0];
	if (arg == NULL ||
	    arg->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    arg->a_const == NULL ||
	    arg->a_const->val_case != PG_QUERY__A__CONST__VAL_SVAL ||
	    arg->a_const->sval == NULL) {
		return NULL;
	}
	return arg->a_const->sval->sval;
}

static sqlparser_status_t sqlparser_mysql_session_emit_text(
	const sqlparser_dialect_session_emitter_t *emitter,
	size_t item_index,
	const char *name,
	size_t name_length,
	sqlparser_graph_session_value_kind_t kind,
	const char *text,
	size_t text_length,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;

	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name_length;
	value.kind = kind;
	value.text = text;
	value.text_length = text_length;
	return emitter->add_value(
		emitter->context,
		item_index,
		&value,
		out_error);
}

static int sqlparser_mysql_session_span_is_integer(
	const char *start,
	const char *end,
	long long *out_value)
{
	unsigned long long value;
	unsigned long long limit;
	int negative;

	if (start == NULL || end == NULL || start >= end || out_value == NULL) {
		return 0;
	}
	negative = 0;
	if (*start == '+' || *start == '-') {
		negative = *start == '-';
		start++;
		if (start == end) {
			return 0;
		}
	}
	value = 0U;
	limit = negative ? (unsigned long long)LLONG_MAX + 1U : (unsigned long long)LLONG_MAX;
	while (start < end) {
		unsigned int digit;

		if (*start < '0' || *start > '9') {
			return 0;
		}
		digit = (unsigned int)(*start - '0');
		if (value > (limit - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		start++;
	}
	if (negative) {
		*out_value = value == (unsigned long long)LLONG_MAX + 1U ?
			LLONG_MIN :
			-(long long)value;
	} else {
		*out_value = (long long)value;
	}
	return 1;
}

static int sqlparser_mysql_session_span_is_float(
	const char *start,
	const char *end)
{
	const char *pos;
	int has_digit;
	int has_fraction;
	int has_exponent;

	if (start == NULL || end == NULL || start >= end) {
		return 0;
	}
	pos = start;
	if (*pos == '+' || *pos == '-') {
		pos++;
	}
	has_digit = 0;
	while (pos < end && isdigit((unsigned char)*pos)) {
		has_digit = 1;
		pos++;
	}
	has_fraction = 0;
	if (pos < end && *pos == '.') {
		has_fraction = 1;
		pos++;
		while (pos < end && isdigit((unsigned char)*pos)) {
			has_digit = 1;
			pos++;
		}
	}
	has_exponent = 0;
	if (has_digit && pos < end && (*pos == 'e' || *pos == 'E')) {
		has_exponent = 1;
		pos++;
		if (pos < end && (*pos == '+' || *pos == '-')) {
			pos++;
		}
		if (pos == end || !isdigit((unsigned char)*pos)) {
			return 0;
		}
		while (pos < end && isdigit((unsigned char)*pos)) {
			pos++;
		}
	}
	return has_digit &&
		(has_fraction || has_exponent) &&
		pos == end;
}

static char *sqlparser_mysql_session_decode_simple_quote(
	const char *start,
	const char *end,
	char quote,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *pos;
	sqlparser_status_t status;

	if (start == NULL || end == NULL || end - start < 2 ||
	    *start != quote || end[-1] != quote) {
		return NULL;
	}
	memset(&out, 0, sizeof(out));
	for (pos = start + 1; pos < end - 1; pos++) {
		if (*pos == quote && pos + 1 < end - 1 && pos[1] == quote) {
			pos++;
		}
		status = sqlparser_mysql_buffer_append_char(&out, *pos, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return NULL;
		}
	}
	status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return NULL;
	}
	if (out.data == NULL) {
		out.data = sqlparser_strdup("");
		if (out.data == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
	}
	return sqlparser_mysql_buffer_take(&out);
}

static int sqlparser_mysql_session_span_is_identifier(
	const char *start,
	const char *end)
{
	const char *pos;

	if (start == NULL || end == NULL || start >= end ||
	    !sqlparser_mysql_is_ident_start((unsigned char)*start)) {
		return 0;
	}
	for (pos = start + 1; pos < end; pos++) {
		if (!sqlparser_mysql_is_ident_char((unsigned char)*pos) &&
		    *pos != '$' && (unsigned char)*pos < 0x80U) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_mysql_session_span_equal_word(
	const char *start,
	const char *end,
	const char *word)
{
	size_t index;
	size_t length;

	if (start == NULL || end == NULL || word == NULL || end < start) {
		return 0;
	}
	length = strlen(word);
	if ((size_t)(end - start) != length) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)start[index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_mysql_session_emit_span_value(
	const sqlparser_dialect_session_emitter_t *emitter,
	size_t item_index,
	const char *name,
	size_t name_length,
	const char *source_sql,
	const char *start,
	const char *end,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;
	char *decoded;
	char *float_text;
	long long integer_value;

	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL session value is empty");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name_length;
	if (*start == '\'' &&
	    sqlparser_mysql_session_token_end(
		    start,
		    0U,
		    (size_t)(end - start)) == (size_t)(end - start)) {
		decoded = sqlparser_mysql_session_decode_simple_quote(
			start,
			end,
			'\'',
			out_error);
		if (decoded != NULL) {
			value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
			value.literal.kind = SQLPARSER_LITERAL_KIND_STRING;
			value.literal.string_value = decoded;
			if (emitter->add_value(
				    emitter->context,
				    item_index,
				    &value,
				    out_error) != SQLPARSER_STATUS_OK) {
				free(decoded);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			free(decoded);
			return SQLPARSER_STATUS_OK;
		}
		if (out_error != NULL &&
		    (out_error->code == SQLPARSER_STATUS_NO_MEMORY ||
		     out_error->code == SQLPARSER_STATUS_RESOURCE_LIMIT)) {
			return out_error->code;
		}
		sqlparser_error_clear(out_error);
	}
	if (sqlparser_mysql_session_span_is_integer(start, end, &integer_value)) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
		value.literal.integer_value = integer_value;
		return emitter->add_value(
			emitter->context,
			item_index,
			&value,
			out_error);
	}
	if (sqlparser_mysql_session_span_is_float(start, end)) {
		float_text = sqlparser_strndup(
			start,
			(size_t)(end - start));
		if (float_text == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_FLOAT;
		value.literal.float_value = float_text;
		if (emitter->add_value(
			    emitter->context,
			    item_index,
			    &value,
			    out_error) != SQLPARSER_STATUS_OK) {
			free(float_text);
			return out_error != NULL ?
				out_error->code :
				SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		free(float_text);
		return SQLPARSER_STATUS_OK;
	}
	if (end - start == 1 && *start == '?') {
		if (source_sql == NULL || start < source_sql) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL session bind source is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_BIND;
		value.bind_kind = SQLPARSER_BIND_KIND_POSITIONAL;
		value.bind_sql = start;
		value.bind_sql_length = 1U;
		value.source_sql = source_sql;
		value.source_offset = (size_t)(start - source_sql);
		return emitter->add_value(
			emitter->context,
			item_index,
			&value,
			out_error);
	}
	if (sqlparser_mysql_session_span_equal_word(start, end, "null")) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_NULL;
		return emitter->add_value(
			emitter->context,
			item_index,
			&value,
			out_error);
	}
	if (sqlparser_mysql_session_span_equal_word(start, end, "true") ||
	    sqlparser_mysql_session_span_equal_word(start, end, "false")) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_BOOLEAN;
		value.literal.boolean_value =
			sqlparser_mysql_session_span_equal_word(
				start,
				end,
				"true");
		return emitter->add_value(
			emitter->context,
			item_index,
			&value,
			out_error);
	}
	if (sqlparser_mysql_session_span_equal_word(start, end, "default") ||
	    sqlparser_mysql_session_span_equal_word(start, end, "none") ||
	    sqlparser_mysql_session_span_equal_word(start, end, "all") ||
	    sqlparser_mysql_session_span_equal_word(start, end, "on") ||
	    sqlparser_mysql_session_span_equal_word(start, end, "off")) {
		return sqlparser_mysql_session_emit_text(
			emitter,
			item_index,
			name,
			name_length,
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			start,
			(size_t)(end - start),
			out_error);
	}
	return sqlparser_mysql_session_emit_text(
		emitter,
		item_index,
		name,
		name_length,
		sqlparser_mysql_session_span_is_identifier(start, end) ?
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER :
			SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION,
		start,
		(size_t)(end - start),
		out_error);
}

static sqlparser_status_t sqlparser_mysql_session_project_assignments(
	const char *sql,
	size_t start,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t segment_start;

	segment_start = start;
	while (segment_start < end) {
		char *decoded_name;
		sqlparser_graph_session_scope_t scope;
		sqlparser_graph_session_target_kind_t target_kind;
		const char *left_start;
		const char *left_end;
		const char *name_start;
		const char *name_end;
		const char *right_start;
		const char *right_end;
		size_t comma;
		size_t equals;
		size_t item_index;
		size_t pos;
		sqlparser_status_t status;

		comma = sqlparser_mysql_find_top_level_char_between(
			sql,
			',',
			segment_start,
			end);
		if (comma == (size_t)-1) {
			comma = end;
		}
		equals = sqlparser_mysql_find_top_level_char_between(
			sql,
			'=',
			segment_start,
			comma);
		if (equals == (size_t)-1) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL session assignment is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		left_start = sqlparser_mysql_trim_left(sql + segment_start, sql + equals);
		left_end = sqlparser_mysql_trim_right(left_start, sql + equals);
		if (left_end > left_start && left_end[-1] == ':') {
			left_end = sqlparser_mysql_trim_right(left_start, left_end - 1);
		}
		right_start = sqlparser_mysql_trim_left(sql + equals + 1U, sql + comma);
		right_end = sqlparser_mysql_trim_right(right_start, sql + comma);
		scope = SQLPARSER_GRAPH_SESSION_SCOPE_SESSION;
		target_kind = SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER;
		name_start = left_start;
		name_end = left_end;
		pos = (size_t)(name_start - sql);
		if (sqlparser_mysql_ascii_word_equal(sql, pos, "session") ||
		    sqlparser_mysql_ascii_word_equal(sql, pos, "local")) {
			int is_local;

			is_local = sqlparser_mysql_ascii_word_equal(sql, pos, "local");
			pos += is_local ? strlen("local") : strlen("session");
			name_start = sqlparser_mysql_trim_left(sql + pos, left_end);
			scope = SQLPARSER_GRAPH_SESSION_SCOPE_SESSION;
		}
		if (name_end - name_start >= 2 &&
		    name_start[0] == '@' && name_start[1] == '@') {
			name_start += 2;
			if ((size_t)(name_end - name_start) > strlen("session.") &&
			    sqlparser_mysql_ascii_word_equal(
				    name_start,
				    0U,
				    "session") &&
			    name_start[strlen("session")] == '.') {
				name_start += strlen("session") + 1U;
			} else if ((size_t)(name_end - name_start) > strlen("local.") &&
				   sqlparser_mysql_ascii_word_equal(
					   name_start,
					   0U,
					   "local") &&
				   name_start[strlen("local")] == '.') {
				name_start += strlen("local") + 1U;
				scope = SQLPARSER_GRAPH_SESSION_SCOPE_SESSION;
			}
		} else if (name_start < name_end && *name_start == '@') {
			name_start++;
			target_kind = SQLPARSER_GRAPH_SESSION_TARGET_VARIABLE;
		}
		decoded_name = NULL;
		if (name_end - name_start >= 2 &&
		    ((*name_start == '\'' && name_end[-1] == '\'') ||
		     (*name_start == '"' && name_end[-1] == '"') ||
		     (*name_start == '`' && name_end[-1] == '`'))) {
			decoded_name = sqlparser_mysql_session_decode_simple_quote(
				name_start,
				name_end,
				*name_start,
				out_error);
			if (decoded_name == NULL) {
				return out_error != NULL ?
					out_error->code :
					SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			name_start = decoded_name;
			name_end = decoded_name + strlen(decoded_name);
		}
		if (name_start >= name_end) {
			free(decoded_name);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL session item name is empty");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = emitter->add_item(
			emitter->context,
			scope,
			target_kind,
			name_start,
			(size_t)(name_end - name_start),
			&item_index,
			out_error);
		free(decoded_name);
		if (status != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = sqlparser_mysql_session_emit_span_value(
			emitter,
			item_index,
			NULL,
			0U,
			sql,
			right_start,
			right_end,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (comma == end) {
			break;
		}
		segment_start = comma + 1U;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_session_project_character_set(
	const char *sql,
	size_t pos,
	size_t end,
	int allow_collation,
	const char *parameter_name,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	size_t token_end;

	pos = sqlparser_mysql_skip_space(sql, pos);
	token_end = sqlparser_mysql_session_token_end(sql, pos, end);
	if (token_end <= pos ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
		    parameter_name,
		    strlen(parameter_name),
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_mysql_session_emit_span_value(
		    emitter,
		    item_index,
		    NULL,
		    0U,
		    sql,
		    sql + pos,
		    sql + token_end,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_mysql_skip_space(sql, token_end);
	if (pos == end) {
		return SQLPARSER_STATUS_OK;
	}
	if (!allow_collation ||
	    !sqlparser_mysql_session_consume_word(sql, &pos, end, "collate")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL character set tail is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_mysql_skip_space(sql, pos);
	token_end = sqlparser_mysql_session_token_end(sql, pos, end);
	return sqlparser_mysql_session_emit_span_value(
		emitter,
		item_index,
		"collation",
		strlen("collation"),
		sql,
		sql + pos,
		sql + token_end,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_session_project_role(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	const char *value_name;
	size_t value_name_length;

	if (emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_ROLE,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_mysql_skip_space(sql, pos);
	value_name = NULL;
	value_name_length = 0U;
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "all")) {
		size_t word_end;

		word_end = pos + strlen("all");
		if (sqlparser_mysql_session_emit_text(
			    emitter,
			    item_index,
			    NULL,
			    0U,
			    SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			    sql + pos,
			    strlen("all"),
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pos = sqlparser_mysql_skip_space(sql, word_end);
		if (pos == end) {
			return SQLPARSER_STATUS_OK;
		}
		if (!sqlparser_mysql_session_consume_word(sql, &pos, end, "except")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL role list is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		value_name = "except";
		value_name_length = strlen(value_name);
	} else if (sqlparser_mysql_ascii_word_equal(sql, pos, "default") ||
		   sqlparser_mysql_ascii_word_equal(sql, pos, "none")) {
		return sqlparser_mysql_session_emit_span_value(
			emitter,
			item_index,
			NULL,
			0U,
			sql,
			sql + pos,
			sql + end,
			out_error);
	}
	pos = sqlparser_mysql_skip_space(sql, pos);
	while (pos < end) {
		size_t comma;

		comma = sqlparser_mysql_find_top_level_char_between(sql, ',', pos, end);
		if (comma == (size_t)-1) {
			comma = end;
		}
		if (sqlparser_mysql_session_emit_span_value(
			    emitter,
			    item_index,
			    value_name,
			    value_name_length,
			    sql,
			    sql + pos,
			    sql + comma,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (comma == end) {
			break;
		}
		pos = sqlparser_mysql_skip_space(sql, comma + 1U);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_session_project_transaction(
	const char *sql,
	size_t pos,
	size_t end,
	sqlparser_graph_session_scope_t scope,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;

	if (emitter->add_item(
		    emitter->context,
		    scope,
		    SQLPARSER_GRAPH_SESSION_TARGET_TRANSACTION,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	while (pos < end) {
		const char *name;
		size_t name_length;
		size_t comma;
		size_t option_start;
		size_t value_start;

		pos = sqlparser_mysql_skip_space(sql, pos);
		comma = sqlparser_mysql_find_top_level_char_between(sql, ',', pos, end);
		if (comma == (size_t)-1) {
			comma = end;
		}
		option_start = pos;
		value_start = pos;
		name = NULL;
		name_length = 0U;
		if (sqlparser_mysql_session_consume_word(sql, &value_start, comma, "isolation") &&
		    sqlparser_mysql_session_consume_word(sql, &value_start, comma, "level")) {
			name = "isolation_level";
			name_length = strlen(name);
			value_start = sqlparser_mysql_skip_space(sql, value_start);
		} else if (sqlparser_mysql_ascii_word_equal(sql, option_start, "read")) {
			name = "access_mode";
			name_length = strlen(name);
			value_start = option_start;
		}
		if (sqlparser_mysql_session_emit_text(
			    emitter,
			    item_index,
			    name,
			    name_length,
			    SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			    sql + value_start,
			    comma - value_start,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (comma == end) {
			break;
		}
		pos = comma + 1U;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_project_raw_session(
	const char *sql,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t end;
	size_t pos;
	sqlparser_graph_session_scope_t scope;

	end = strlen(sql);
	pos = sqlparser_mysql_skip_space(sql, 0U);
	if (!sqlparser_mysql_ascii_word_equal(sql, pos, "set")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL session statement is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_mysql_skip_space(sql, pos + strlen("set"));
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "character")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("character"));
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("set"));
		return sqlparser_mysql_session_project_character_set(
			sql, pos, end, 0, "character_set", emitter, out_error);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "charset")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("charset"));
		return sqlparser_mysql_session_project_character_set(
			sql, pos, end, 0, "character_set", emitter, out_error);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "names")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("names"));
		return sqlparser_mysql_session_project_character_set(
			sql, pos, end, 1, "names", emitter, out_error);
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "role")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("role"));
		return sqlparser_mysql_session_project_role(
			sql, pos, end, emitter, out_error);
	}
	scope = SQLPARSER_GRAPH_SESSION_SCOPE_TRANSACTION;
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "session") ||
	    sqlparser_mysql_ascii_word_equal(sql, pos, "local")) {
		int is_local;

		is_local = sqlparser_mysql_ascii_word_equal(sql, pos, "local");
		pos += is_local ? strlen("local") : strlen("session");
		pos = sqlparser_mysql_skip_space(sql, pos);
		scope = SQLPARSER_GRAPH_SESSION_SCOPE_SESSION;
	}
	if (sqlparser_mysql_ascii_word_equal(sql, pos, "transaction")) {
		pos = sqlparser_mysql_skip_space(sql, pos + strlen("transaction"));
		return sqlparser_mysql_session_project_transaction(
			sql, pos, end, scope, emitter, out_error);
	}
	return sqlparser_mysql_session_project_assignments(
		sql, pos, end, emitter, out_error);
}

static sqlparser_status_t sqlparser_mysql_project_session(
	const sqlparser_handle_t *handle,
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	const char *value;
	size_t item_index;
	sqlparser_dialect_session_value_t session_value;
	sqlparser_status_t status;

	(void)state;
	if (emitter == NULL || emitter->set_action == NULL ||
	    emitter->add_item == NULL || emitter->add_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph emitter is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	value = sqlparser_mysql_session_ast_argument(
		statement,
		SQLPARSER_INTERNAL_CURRENT_DATABASE);
	if (value != NULL) {
		if (emitter->set_action(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			    out_error) != SQLPARSER_STATUS_OK ||
		    emitter->add_item(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			    SQLPARSER_GRAPH_SESSION_TARGET_DATABASE,
			    NULL,
			    0U,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		memset(&session_value, 0, sizeof(session_value));
		session_value.kind = SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER;
		session_value.text = value;
		session_value.text_length = strlen(value);
		return emitter->add_value(
			emitter->context,
			item_index,
			&session_value,
			out_error);
	}
	value = sqlparser_mysql_session_ast_argument(
		statement,
		SQLPARSER_INTERNAL_MYSQL_SESSION_STATEMENT);
	if (value == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	public_sql = NULL;
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		statement_index,
		value,
		"session statement",
		&public_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		if (public_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MySQL session SQL is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = sqlparser_mysql_project_raw_session(
			public_sql, emitter, out_error);
	}
	free(public_sql);
	return status;
}

static const sqlparser_dialect_ops_t SQLPARSER_MYSQL_OPS = {
	SQLPARSER_DIALECT_MYSQL,
	"mysql",
	sqlparser_mysql_preprocess,
	sqlparser_mysql_preprocess_fragment,
	sqlparser_mysql_postprocess_deparse,
	sqlparser_mysql_clone_state,
	sqlparser_mysql_state_destroy,
	sqlparser_mysql_postprocess_literal_fragment,
	sqlparser_mysql_statement_keyword,
	sqlparser_mysql_insert_mode,
	NULL,
	NULL,
	sqlparser_mysql_postprocess_fragment,
	NULL,
	NULL,
	sqlparser_mysql_project_session
};

const sqlparser_dialect_ops_t *sqlparser_dialect_mysql_ops(void)
{
	return &SQLPARSER_MYSQL_OPS;
}
